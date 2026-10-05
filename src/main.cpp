#include "config.h"
#include "inverter_model.h"
#include "logger.h"
#include "modbus_rtu.h"
#include "mqtt_pub.h"
#include "net_wifi.h"
#include "rs485.h"
#include "settings.h"
#include "sungrow_ref.h"
#include "web_app.h"

#include <cctype>
#include <cstring>

namespace {

enum class Mode { kPoll, kListen };

struct Pending {
  bool set;
  bool know_addr;
  uint8_t frame[256];
  size_t len;
  uint16_t protocol_addr;
  uint8_t fc;
};

struct Inflight {
  bool active;
  bool know_addr;
  uint16_t protocol_addr;
  uint8_t fc;
  uint8_t slave;
  uint8_t tx[256];
  size_t tx_len;
  uint32_t started_ms;
};

struct AutoRead {
  uint8_t fc;
  uint16_t protocol_addr;
  uint16_t count;
  const char *label;
};

const AutoRead kAuto[] = {
    {0x04, 5000, 1, "tipo do equipamento"},
    {0x04, 5000, 16, "bloco 5000-5015"},
    {0x04, 5016, 23, "bloco 5016-5038"},
    {0x04, 4990, 10, "numero de serie"},
};

Mode g_mode = Mode::kPoll;
uint8_t g_slave = MODBUS_SLAVE_DEFAULT;
uint32_t g_interval_ms = MODBUS_POLL_INTERVAL_MS;
uint32_t g_last_tx_ms = 0;
uint8_t g_auto_index = 0;
uint32_t g_timeouts_in_row = 0;
uint32_t g_frames_ok = 0;
uint32_t g_frames_bad = 0;

Pending g_pending;
Inflight g_inflight;

void stamp() {
  uint32_t ms = millis();
  logger_printf("[%lu.%03lu] ", static_cast<unsigned long>(ms / 1000),
                static_cast<unsigned long>(ms % 1000));
}

void print_help() {
  logger_printf("Comandos:\r\n");
  logger_printf("  ajuda\r\n");
  logger_printf("  status\r\n");
  logger_printf("  escuta                 so mostra o que chegar no RS485\r\n");
  logger_printf("  varredura              volta a consultar o inversor\r\n");
  logger_printf("  escravo <1-247>\r\n");
  logger_printf("  baud <1200|2400|4800|9600|19200|38400|115200>\r\n");
  logger_printf("  intervalo <ms>         pausa entre consultas\r\n");
  logger_printf("  ler <3|4> <end> <qtd>  endereco do manual (o quadro usa end-1)\r\n");
  logger_printf("  lerraw <3|4> <end> <qtd>  endereco exato do PDU\r\n");
  logger_printf("  enviar <hex>           envia os bytes como estao\r\n");
  logger_printf("  enviarcrc <hex>        acrescenta CRC16 Modbus\r\n");
  logger_printf("  mapa                   referencia usada na decodificacao\r\n");
  logger_printf("  resetar-senha          volta o login da pagina para admin / admin\r\n");
  logger_printf("Funcoes 06 e 16 escrevem no inversor. Este firmware nao monta essas escritas.\r\n");
}

void print_status() {
  logger_printf("hostname %s\r\n", WIFI_HOSTNAME);
  if (net_ap_up()) {
    logger_printf("pagina http://%s  rede aberta %s\r\n", WIFI_AP_IP, WIFI_AP_SSID);
  } else {
    logger_printf("rede aberta desligada\r\n");
  }
  if (net_sta_connected()) {
    logger_printf("wifi casa ip %s  rssi %d dBm\r\n", net_sta_ip().c_str(), net_rssi());
  } else if (settings_get().ssid[0] == '\0') {
    logger_printf("wifi da casa ainda nao configurado\r\n");
  } else {
    logger_printf("wifi da casa desconectado, ssid %s\r\n", settings_get().ssid);
  }
  logger_printf("modelo %s\r\n", inverter_model_name());
  logger_printf("telnet porta %d\r\n", TELNET_PORT);
  logger_printf("rs485 TX %d RX %d DE %d  %lu 8N1  escravo %u\r\n", RS485_TX_PIN, RS485_RX_PIN,
                RS485_DE_PIN, static_cast<unsigned long>(rs485_baud()), g_slave);
  logger_printf("modo %s  intervalo %lu ms\r\n", g_mode == Mode::kPoll ? "varredura" : "escuta",
                static_cast<unsigned long>(g_interval_ms));
  logger_printf("respostas crc ok %lu  crc ruim %lu  timeouts seguidos %lu\r\n",
                static_cast<unsigned long>(g_frames_ok), static_cast<unsigned long>(g_frames_bad),
                static_cast<unsigned long>(g_timeouts_in_row));
}

void print_banner() {
  logger_printf("\r\nESP32-S3-RS485-CAN  hostname %s\r\n", WIFI_HOSTNAME);
  logger_printf("RS485 UART1 TX=%d RX=%d DE=%d (HIGH transmite)  %lu 8N1  escravo %u\r\n",
                RS485_TX_PIN, RS485_RX_PIN, RS485_DE_PIN,
                static_cast<unsigned long>(MODBUS_BAUD_DEFAULT), g_slave);
  logger_printf("Modelo %s\r\n", inverter_model_name());
  logger_printf("Ligue A+ e B- da placa no par RS485 do inversor.\r\n");
  logger_printf("Se nao houver resposta, inverta A e B e feche o jumper 120R nesta ponta.\r\n");
  logger_printf("Nao use o par do medidor de energia, se o conector tiver os dois.\r\n");
  logger_printf("O inversor e escravo: fica em silencio ate receber uma consulta.\r\n");
  logger_printf("Modo inicial: varredura. O quadro cru sai no USB e no telnet %d.\r\n", TELNET_PORT);
  logger_printf("Rede aberta sem senha: %s\r\n", WIFI_AP_SSID);
  logger_printf("Pagina http://%s  usuario admin  senha admin\r\n", WIFI_AP_IP);
  logger_printf("Digite ajuda.\r\n");
}

bool parse_u32(const char *text, uint32_t *out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char *end = nullptr;
  unsigned long value = strtoul(text, &end, 10);
  if (end == text || *end != '\0') {
    return false;
  }
  *out = static_cast<uint32_t>(value);
  return true;
}

void skip_ws(char *&cursor) {
  while (*cursor == ' ' || *cursor == '\t') {
    cursor++;
  }
}

bool take_word(char *&cursor, char *word, size_t word_len) {
  skip_ws(cursor);
  if (*cursor == '\0') {
    return false;
  }
  size_t n = 0;
  while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
    if (n + 1 < word_len) {
      word[n++] = *cursor;
    }
    cursor++;
  }
  word[n] = '\0';
  return n > 0;
}

int hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool parse_hex(const char *text, uint8_t *out, size_t max_len, size_t *out_len) {
  size_t n = 0;
  const char *p = text;
  while (*p != '\0') {
    while (*p == ' ' || *p == '\t' || *p == ',' || *p == ':' || *p == '-') {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
      p += 2;
    }
    int hi = hex_nibble(*p);
    if (hi < 0) {
      return false;
    }
    p++;
    int lo = hex_nibble(*p);
    if (lo < 0) {
      return false;
    }
    p++;
    if (n >= max_len) {
      return false;
    }
    out[n++] = static_cast<uint8_t>((hi << 4) | lo);
  }
  *out_len = n;
  return n > 0;
}

bool baud_allowed(uint32_t baud) {
  switch (baud) {
    case 1200:
    case 2400:
    case 4800:
    case 9600:
    case 19200:
    case 38400:
    case 115200:
      return true;
    default:
      return false;
  }
}

void queue_frame(const uint8_t *frame, size_t len, bool know_addr, uint16_t protocol_addr, uint8_t fc) {
  if (len == 0 || len > sizeof(g_pending.frame)) {
    logger_printf("quadro vazio ou longo demais\r\n");
    return;
  }
  if (g_pending.set) {
    logger_printf("consulta anterior substituida\r\n");
  }
  memcpy(g_pending.frame, frame, len);
  g_pending.len = len;
  g_pending.know_addr = know_addr;
  g_pending.protocol_addr = protocol_addr;
  g_pending.fc = fc;
  g_pending.set = true;
}

bool queue_read(uint8_t fc, uint32_t addr, uint32_t count, bool protocol_addr) {
  if (fc != 3 && fc != 4) {
    logger_printf("funcao aceita aqui: 3 ou 4\r\n");
    return false;
  }
  if (count < 1 || count > 125) {
    logger_printf("quantidade de 1 a 125\r\n");
    return false;
  }
  uint32_t pdu = addr;
  if (protocol_addr) {
    if (addr < 1) {
      logger_printf("endereco do manual comeca em 1\r\n");
      return false;
    }
    pdu = addr - 1;
  }
  if (pdu > 65535 || pdu + count - 1 > 65535) {
    logger_printf("endereco fora da faixa Modbus\r\n");
    return false;
  }
  uint8_t frame[8];
  size_t n = modbus_build_read(g_slave, fc, static_cast<uint16_t>(pdu), static_cast<uint16_t>(count),
                               frame, sizeof(frame));
  if (n == 0) {
    logger_printf("falha ao montar o quadro\r\n");
    return false;
  }
  queue_frame(frame, n, protocol_addr, static_cast<uint16_t>(addr), fc);
  logger_printf("na fila: fc %u  %s %lu  qtd %lu  escravo %u\r\n", fc,
                protocol_addr ? "manual" : "pdu", static_cast<unsigned long>(addr),
                static_cast<unsigned long>(count), g_slave);
  return true;
}

void handle_command(char *line) {
  logger_printf("> %s\r\n", line);
  char *cursor = line;
  char word[24];
  if (!take_word(cursor, word, sizeof(word))) {
    return;
  }
  for (char *p = word; *p != '\0'; ++p) {
    *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
  }

  if (strcmp(word, "ajuda") == 0 || strcmp(word, "help") == 0) {
    print_help();
    return;
  }
  if (strcmp(word, "status") == 0) {
    print_status();
    return;
  }
  if (strcmp(word, "mapa") == 0) {
    sungrow_print_map();
    return;
  }
  if (strcmp(word, "resetar-senha") == 0) {
    settings_reset_password();
    logger_printf("login da pagina voltou para admin / admin\r\n");
    return;
  }
  if (strcmp(word, "escuta") == 0) {
    g_mode = Mode::kListen;
    g_pending.set = false;
    logger_printf("modo escuta: a placa nao transmite. Sem outro mestre, o SG6K-D fica mudo.\r\n");
    return;
  }
  if (strcmp(word, "varredura") == 0) {
    g_mode = Mode::kPoll;
    g_last_tx_ms = 0;
    logger_printf("modo varredura\r\n");
    return;
  }
  if (strcmp(word, "escravo") == 0) {
    char num[8];
    uint32_t value = 0;
    if (!take_word(cursor, num, sizeof(num)) || !parse_u32(num, &value) || value < 1 || value > 247) {
      logger_printf("use escravo <1-247>\r\n");
      return;
    }
    g_slave = static_cast<uint8_t>(value);
    logger_printf("escravo %u\r\n", g_slave);
    return;
  }
  if (strcmp(word, "baud") == 0) {
    char num[12];
    uint32_t value = 0;
    if (!take_word(cursor, num, sizeof(num)) || !parse_u32(num, &value) || !baud_allowed(value)) {
      logger_printf("baud invalido\r\n");
      return;
    }
    rs485_set_baud(value);
    logger_printf("baud %lu\r\n", static_cast<unsigned long>(value));
    return;
  }
  if (strcmp(word, "intervalo") == 0) {
    char num[12];
    uint32_t value = 0;
    if (!take_word(cursor, num, sizeof(num)) || !parse_u32(num, &value) || value < 200 || value > 60000) {
      logger_printf("use intervalo <200-60000>\r\n");
      return;
    }
    g_interval_ms = value;
    logger_printf("intervalo %lu ms\r\n", static_cast<unsigned long>(value));
    return;
  }
  if (strcmp(word, "ler") == 0 || strcmp(word, "lerraw") == 0) {
    bool protocol = strcmp(word, "ler") == 0;
    char a[8];
    char b[12];
    char c[8];
    uint32_t fc = 0;
    uint32_t addr = 0;
    uint32_t count = 0;
    if (!take_word(cursor, a, sizeof(a)) || !take_word(cursor, b, sizeof(b)) ||
        !take_word(cursor, c, sizeof(c)) || !parse_u32(a, &fc) || !parse_u32(b, &addr) ||
        !parse_u32(c, &count)) {
      logger_printf("use %s <3|4> <endereco> <quantidade>\r\n", word);
      return;
    }
    queue_read(static_cast<uint8_t>(fc), addr, count, protocol);
    return;
  }
  if (strcmp(word, "enviar") == 0 || strcmp(word, "enviarcrc") == 0) {
    skip_ws(cursor);
    uint8_t frame[256];
    size_t n = 0;
    if (!parse_hex(cursor, frame, sizeof(frame) - 2, &n)) {
      logger_printf("hex invalido\r\n");
      return;
    }
    if (strcmp(word, "enviarcrc") == 0) {
      n = modbus_append_crc(frame, n, sizeof(frame));
      if (n == 0) {
        logger_printf("sem espaco para o CRC\r\n");
        return;
      }
    }
    queue_frame(frame, n, false, 0, n > 1 ? frame[1] : 0);
    logger_printf("na fila: %u bytes\r\n", static_cast<unsigned>(n));
    return;
  }
  logger_printf("comando desconhecido. digite ajuda\r\n");
}

void note_timeouts() {
  if (g_timeouts_in_row == 3 || (g_timeouts_in_row > 3 && g_timeouts_in_row % 10 == 0)) {
    logger_printf("Sem resposta. Confira A/B, jumper 120R, escravo, baud e o par RS485 do inversor.\r\n");
  }
}

void describe_modbus(const uint8_t *frame, size_t len, bool know_addr, uint16_t protocol_addr) {
  if (len < 5) {
    logger_printf("  quadro curto para Modbus\r\n");
    return;
  }
  bool crc_ok = modbus_crc_ok(frame, len);
  logger_printf("  escravo %u  funcao 0x%02X  crc %s\r\n", frame[0], frame[1], crc_ok ? "ok" : "FALHOU");
  if (!crc_ok) {
    g_frames_bad++;
    logger_printf("  CRC invalido: baud diferente, ruido ou outro formato.\r\n");
    return;
  }
  g_frames_ok++;
  g_timeouts_in_row = 0;

  if (frame[1] & 0x80) {
    logger_printf("  excecao Modbus codigo %u\r\n", frame[2]);
    return;
  }
  if ((frame[1] == 3 || frame[1] == 4) && len >= 5) {
    uint8_t byte_count = frame[2];
    if (static_cast<size_t>(byte_count) + 5 != len) {
      logger_printf("  tamanho do payload (%u) nao fecha com o quadro (%u)\r\n", byte_count,
                    static_cast<unsigned>(len));
      return;
    }
    if (know_addr && g_inflight.fc == frame[1] && inverter_model_is_sungrow()) {
      sungrow_print_registers(protocol_addr, frame + 3, byte_count);
    } else {
      logger_printf("  %u bytes de dados, sem endereco de referencia para rotular\r\n", byte_count);
    }
  }
}

bool same_frame(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len) {
  return a_len == b_len && memcmp(a, b, a_len) == 0;
}

void on_rx_frame(const uint8_t *frame, size_t len) {
  stamp();
  if (g_inflight.active) {
    uint32_t dt = millis() - g_inflight.started_ms;
    if (same_frame(frame, len, g_inflight.tx, g_inflight.tx_len)) {
      logger_printf("RX igual ao TX apos %lu ms (eco, direcao RS485 suspeita)\r\n",
                    static_cast<unsigned long>(dt));
    } else {
      logger_printf("RX resposta em %lu ms\r\n", static_cast<unsigned long>(dt));
    }
    logger_hexdump("RX", frame, len);
    describe_modbus(frame, len, g_inflight.know_addr, g_inflight.protocol_addr);
    g_inflight.active = false;
    return;
  }
  logger_printf("RX espontaneo\r\n");
  logger_hexdump("RX", frame, len);
  describe_modbus(frame, len, false, 0);
}

void start_request(const uint8_t *frame, size_t len, bool know_addr, uint16_t protocol_addr, uint8_t fc) {
  size_t echo = rs485_send(frame, len);
  g_last_tx_ms = millis();
  g_inflight.active = true;
  g_inflight.know_addr = know_addr;
  g_inflight.protocol_addr = protocol_addr;
  g_inflight.fc = fc;
  g_inflight.slave = frame[0];
  g_inflight.tx_len = len > sizeof(g_inflight.tx) ? sizeof(g_inflight.tx) : len;
  memcpy(g_inflight.tx, frame, g_inflight.tx_len);
  g_inflight.started_ms = g_last_tx_ms;

  stamp();
  logger_printf("TX fc %u escravo %u eco_local %u\r\n", fc, frame[0], static_cast<unsigned>(echo));
  logger_hexdump("TX", frame, len);
}

void service_poll() {
  if (g_inflight.active) {
    if (millis() - g_inflight.started_ms >= MODBUS_RESPONSE_TIMEOUT_MS) {
      stamp();
      logger_printf("RX timeout %lu ms, escravo %u fc %u\r\n",
                    static_cast<unsigned long>(MODBUS_RESPONSE_TIMEOUT_MS), g_inflight.slave,
                    g_inflight.fc);
      g_inflight.active = false;
      g_timeouts_in_row++;
      note_timeouts();
    }
    return;
  }
  if (rs485_rx_pending()) {
    return;
  }

  if (g_pending.set) {
    Pending req = g_pending;
    g_pending.set = false;
    start_request(req.frame, req.len, req.know_addr, req.protocol_addr, req.fc);
    return;
  }
  if (g_mode != Mode::kPoll) {
    return;
  }
  if (g_last_tx_ms != 0 && millis() - g_last_tx_ms < g_interval_ms) {
    return;
  }

  if (!inverter_model_is_sungrow()) {
    return;
  }
  const AutoRead &item = kAuto[g_auto_index];
  g_auto_index = (g_auto_index + 1) % (sizeof(kAuto) / sizeof(kAuto[0]));
  uint8_t frame[8];
  size_t n = modbus_build_read(g_slave, item.fc, static_cast<uint16_t>(item.protocol_addr - 1), item.count,
                               frame, sizeof(frame));
  if (n == 0) {
    return;
  }
  stamp();
  logger_printf("consulta %s  manual %u  pdu %u  qtd %u\r\n", item.label, item.protocol_addr,
                item.protocol_addr - 1, item.count);
  start_request(frame, n, true, item.protocol_addr, item.fc);
}

}  // namespace

void setup() {
  pinMode(RS485_DE_PIN, OUTPUT);
  digitalWrite(RS485_DE_PIN, LOW);
  logger_begin();
  rs485_begin(MODBUS_BAUD_DEFAULT);
  settings_begin();
  print_banner();
  net_begin();
  web_begin();
  mqtt_begin();
}

void loop() {
  net_service();
  web_service();
  logger_service();

  char command[160];
  while (logger_take_command(command, sizeof(command))) {
    handle_command(command);
  }

  uint8_t frame[512];
  size_t n = rs485_read_frame(frame, sizeof(frame));
  if (n > 0) {
    on_rx_frame(frame, n);
  }
  service_poll();
  mqtt_service(!g_inflight.active && !rs485_rx_pending());
}
