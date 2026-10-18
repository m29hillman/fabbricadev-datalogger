#include "mqtt_pub.h"

#include "config.h"
#include "inverter_data.h"
#include "logger.h"
#include "net_wifi.h"
#include "settings.h"

#include <mqtt_client.h>

#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

extern "C" esp_err_t mqtt_on_event(esp_mqtt_event_handle_t event);

namespace {

enum class Transport { kTcp, kTls, kWs, kWss };

constexpr uint32_t kRetryMs = 8000;

uint32_t publish_ms() {
  uint16_t seconds = settings_get().mqtt_every_s;
  if (seconds < 1) {
    seconds = MQTT_PUBLISH_DEFAULT_S;
  }
  return static_cast<uint32_t>(seconds) * 1000UL;
}

struct Parsed {
  Transport transport;
  char host[64];
  uint16_t port;
  char user[64];
  char pass[64];
  char topic[96];
  char ws_path[48];
};

esp_mqtt_client_handle_t g_client = nullptr;
Parsed g_parsed;
bool g_parsed_ok = false;
bool g_was_up = false;
volatile bool g_mqtt_up = false;
volatile bool g_err_pending = false;
volatile int g_err_type = 0;
volatile int g_tls_esp = 0;
volatile int g_tls_stack = 0;
volatile int g_tls_flags = 0;
volatile int g_refused = 0;
volatile int g_sock = 0;
uint32_t g_last_try_ms = 0;
uint32_t g_last_pub_ms = 0;
uint32_t g_clock_log_ms = 0;

void clip(char *dst, size_t n, const char *src) {
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

bool all_digits(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  for (const char *p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return false;
    }
  }
  return true;
}

bool take_scheme(const char *&p, Transport *transport, uint16_t *port) {
  if (strncmp(p, "mqtts://", 8) == 0) {
    p += 8;
    *transport = Transport::kTls;
    *port = 8883;
    return true;
  }
  if (strncmp(p, "mqtt://", 7) == 0) {
    p += 7;
    *transport = Transport::kTcp;
    *port = 1883;
    return true;
  }
  if (strncmp(p, "wss://", 6) == 0) {
    p += 6;
    *transport = Transport::kWss;
    *port = 443;
    return true;
  }
  if (strncmp(p, "ws://", 5) == 0) {
    p += 5;
    *transport = Transport::kWs;
    *port = 80;
    return true;
  }
  return false;
}

bool websocket(Transport transport) {
  return transport == Transport::kWs || transport == Transport::kWss;
}

bool tls_transport(Transport transport) {
  return transport == Transport::kTls || transport == Transport::kWss;
}

bool clock_ready() {
  return time(nullptr) >= 1767225600;
}

const char *refuse_text(int code) {
  switch (code) {
    case MQTT_CONNECTION_REFUSE_PROTOCOL:
      return "protocolo recusado";
    case MQTT_CONNECTION_REFUSE_ID_REJECTED:
      return "identificador recusado";
    case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE:
      return "broker indisponivel";
    case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
      return "usuario recusado";
    case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
      return "senha ou permissao recusada";
    default:
      return "motivo desconhecido";
  }
}

void note_mqtt_error() {
  if (!g_err_pending) {
    return;
  }
  int type = g_err_type;
  int tls_esp = g_tls_esp;
  int stack = g_tls_stack;
  int flags = g_tls_flags;
  int refused = g_refused;
  int sock = g_sock;
  g_err_pending = false;
  if (type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
    logger_printf("MQTT recusado pelo broker (%s)\r\n", refuse_text(refused));
    return;
  }
  logger_printf("MQTT falha de transporte esp=%d pilha=%d certificado=%d socket=%d\r\n", tls_esp, stack,
                flags, sock);
  if (flags != 0 || (tls_esp == 32794 && (stack == 30592 || stack == -30592))) {
    logger_printf("MQTT o servidor encerrou o TLS.\r\n");
  }
}

const char *transport_name(Transport transport) {
  switch (transport) {
    case Transport::kTls:
      return "mqtts";
    case Transport::kWs:
      return "ws";
    case Transport::kWss:
      return "wss";
    default:
      return "mqtt";
  }
}

esp_mqtt_transport_t esp_transport(Transport transport) {
  switch (transport) {
    case Transport::kTls:
      return MQTT_TRANSPORT_OVER_SSL;
    case Transport::kWs:
      return MQTT_TRANSPORT_OVER_WS;
    case Transport::kWss:
      return MQTT_TRANSPORT_OVER_WSS;
    default:
      return MQTT_TRANSPORT_OVER_TCP;
  }
}

bool set_ws_path(Parsed *out, const char *path) {
  clip(out->ws_path, sizeof(out->ws_path), "/mqtt");
  if (path == nullptr || path[0] == '\0') {
    return true;
  }
  const char *sep = strchr(path, '/');
  if (sep == nullptr) {
    return snprintf(out->ws_path, sizeof(out->ws_path), "/%s", path) < static_cast<int>(sizeof(out->ws_path));
  }
  size_t n = static_cast<size_t>(sep - path);
  if (n == 0 || n + 2 > sizeof(out->ws_path)) {
    return false;
  }
  out->ws_path[0] = '/';
  memcpy(out->ws_path + 1, path, n);
  out->ws_path[n + 1] = '\0';
  const char *topic = sep + 1;
  while (*topic == '/') {
    topic++;
  }
  if (topic[0] != '\0') {
    clip(out->topic, sizeof(out->topic), topic);
  }
  return out->ws_path[1] != '\0';
}

bool parse_url(const char *url, Parsed *out) {
  memset(out, 0, sizeof(*out));
  out->transport = Transport::kTcp;
  out->port = 1883;
  clip(out->topic, sizeof(out->topic), MQTT_TOPIC_DEFAULT);
  clip(out->ws_path, sizeof(out->ws_path), "/mqtt");
  if (url == nullptr) {
    return false;
  }
  while (*url == ' ') {
    url++;
  }
  const char *p = url;
  if (!take_scheme(p, &out->transport, &out->port)) {
    return false;
  }

  char hostpart[160];
  char path[120];
  path[0] = '\0';
  const char *slash = strchr(p, '/');
  if (slash != nullptr) {
    size_t n = static_cast<size_t>(slash - p);
    if (n == 0 || n >= sizeof(hostpart)) {
      return false;
    }
    memcpy(hostpart, p, n);
    hostpart[n] = '\0';
    const char *rest = slash + 1;
    while (*rest == '/') {
      rest++;
    }
    if (strlen(rest) >= sizeof(path)) {
      return false;
    }
    clip(path, sizeof(path), rest);
  } else if (p[0] == '\0' || strlen(p) >= sizeof(hostpart)) {
    return false;
  } else {
    clip(hostpart, sizeof(hostpart), p);
  }

  char *at = strrchr(hostpart, '@');
  char *host = hostpart;
  if (at != nullptr) {
    *at = '\0';
    host = at + 1;
    char *colon = strchr(hostpart, ':');
    if (colon != nullptr) {
      *colon = '\0';
      clip(out->user, sizeof(out->user), hostpart);
      clip(out->pass, sizeof(out->pass), colon + 1);
    } else {
      clip(out->user, sizeof(out->user), hostpart);
    }
  }

  char *colon = strrchr(host, ':');
  if (colon != nullptr && all_digits(colon + 1)) {
    *colon = '\0';
    long port = strtol(colon + 1, nullptr, 10);
    if (port < 1 || port > 65535) {
      return false;
    }
    out->port = static_cast<uint16_t>(port);
  }
  if (host[0] == '\0' || strlen(host) >= sizeof(out->host)) {
    return false;
  }
  clip(out->host, sizeof(out->host), host);

  if (websocket(out->transport)) {
    if (!set_ws_path(out, path)) {
      return false;
    }
  } else if (path[0] != '\0') {
    clip(out->topic, sizeof(out->topic), path);
  }
  return out->topic[0] != '\0';
}

void stop_client() {
  if (g_client == nullptr) {
    return;
  }
  esp_mqtt_client_stop(g_client);
  esp_mqtt_client_destroy(g_client);
  g_client = nullptr;
  g_mqtt_up = false;
}

bool publish_now() {
  if (g_client == nullptr || !g_mqtt_up || !inverter_data_ready()) {
    return false;
  }
  char payload[1024];
  size_t n = inverter_format_json(payload, sizeof(payload));
  if (n == 0 || n >= sizeof(payload) - 1) {
    return false;
  }
  int id = esp_mqtt_client_publish(g_client, g_parsed.topic, payload, static_cast<int>(n), 0, 1);
  return id >= 0;
}

void start_client() {
  esp_mqtt_client_config_t cfg = {};
  cfg.host = g_parsed.host;
  cfg.port = g_parsed.port;
  cfg.transport = esp_transport(g_parsed.transport);
  cfg.client_id = settings_get().hostname[0] != '\0' ? settings_get().hostname : WIFI_HOSTNAME;
  cfg.event_handle = mqtt_on_event;
  cfg.keepalive = 30;
  cfg.disable_auto_reconnect = false;
  cfg.reconnect_timeout_ms = static_cast<int>(kRetryMs);
  cfg.buffer_size = 2048;
  cfg.out_buffer_size = 2048;
  cfg.task_stack = 10240;
  cfg.task_prio = 5;
  bool tls = tls_transport(g_parsed.transport);
  cfg.skip_cert_common_name_check = false;
  if (tls) {
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
  }
  if (g_parsed.user[0] != '\0') {
    cfg.username = g_parsed.user;
    if (g_parsed.pass[0] != '\0') {
      cfg.password = g_parsed.pass;
    }
  }
  if (websocket(g_parsed.transport)) {
    cfg.path = g_parsed.ws_path;
  }

  g_client = esp_mqtt_client_init(&cfg);
  if (g_client == nullptr) {
    logger_printf("MQTT sem memoria para o cliente\r\n");
    return;
  }
  if (esp_mqtt_client_start(g_client) != ESP_OK) {
    esp_mqtt_client_destroy(g_client);
    g_client = nullptr;
    logger_printf("MQTT nao iniciou em %s:%u\r\n", g_parsed.host, g_parsed.port);
    return;
  }
  const char *cert = tls ? " verificando certificado" : "";
  if (websocket(g_parsed.transport)) {
    logger_printf("MQTT conectando em %s:%u caminho %s topico %s via %s%s\r\n", g_parsed.host, g_parsed.port,
                  g_parsed.ws_path, g_parsed.topic, transport_name(g_parsed.transport), cert);
  } else {
    logger_printf("MQTT conectando em %s:%u topico %s via %s%s\r\n", g_parsed.host, g_parsed.port, g_parsed.topic,
                  transport_name(g_parsed.transport), cert);
  }
}

void refresh_parse() {
  stop_client();
  g_was_up = false;
  const Settings &cfg = settings_get();
  g_parsed_ok = cfg.mqtt_url[0] != '\0' && parse_url(cfg.mqtt_url, &g_parsed);
}

}  // namespace

extern "C" esp_err_t mqtt_on_event(esp_mqtt_event_handle_t event) {
  if (event == nullptr) {
    return ESP_OK;
  }
  if (event->event_id == MQTT_EVENT_CONNECTED) {
    g_mqtt_up = true;
  } else if (event->event_id == MQTT_EVENT_DISCONNECTED) {
    g_mqtt_up = false;
  } else if (event->event_id == MQTT_EVENT_ERROR) {
    g_err_pending = false;
    g_err_type = 0;
    g_tls_esp = 0;
    g_tls_stack = 0;
    g_tls_flags = 0;
    g_refused = 0;
    g_sock = 0;
    if (event->error_handle != nullptr) {
      g_err_type = event->error_handle->error_type;
      g_tls_esp = event->error_handle->esp_tls_last_esp_err;
      g_tls_stack = event->error_handle->esp_tls_stack_err;
      g_tls_flags = event->error_handle->esp_tls_cert_verify_flags;
      g_refused = event->error_handle->connect_return_code;
      g_sock = event->error_handle->esp_transport_sock_errno;
    }
    g_err_pending = true;
  }
  return ESP_OK;
}

void mqtt_begin() { refresh_parse(); }

void mqtt_reconfigure() { refresh_parse(); }

bool mqtt_url_valid(const char *url) {
  if (url == nullptr || url[0] == '\0') {
    return false;
  }
  Parsed parsed;
  return parse_url(url, &parsed);
}

bool mqtt_connected() { return g_mqtt_up; }

const char *mqtt_topic() { return g_parsed_ok ? g_parsed.topic : MQTT_TOPIC_DEFAULT; }

void mqtt_service(bool allow_connect) {
  note_mqtt_error();
  const Settings &cfg = settings_get();
  if (!cfg.mqtt_on) {
    if (g_client != nullptr) {
      stop_client();
      g_was_up = false;
      logger_printf("MQTT desligado\r\n");
    }
    return;
  }
  if (!g_parsed_ok) {
    return;
  }

  if (g_mqtt_up && !g_was_up) {
    g_was_up = true;
    g_last_pub_ms = millis() - publish_ms();
    logger_printf("MQTT conectado em %s:%u topico %s via %s\r\n", g_parsed.host, g_parsed.port,
                  g_parsed.topic, transport_name(g_parsed.transport));
  } else if (!g_mqtt_up && g_was_up) {
    g_was_up = false;
    logger_printf("MQTT desconectou\r\n");
  }

  if (g_client != nullptr && g_mqtt_up && millis() - g_last_pub_ms >= publish_ms()) {
    publish_now();
    g_last_pub_ms = millis();
  }

  if (g_client != nullptr || !allow_connect || !net_sta_connected()) {
    return;
  }
  if (tls_transport(g_parsed.transport) && !clock_ready()) {
    if (millis() - g_clock_log_ms >= 15000) {
      g_clock_log_ms = millis();
      logger_printf("MQTT aguardando o horario da internet para conferir o certificado\r\n");
    }
    return;
  }
  if (millis() - g_last_try_ms < kRetryMs) {
    return;
  }
  g_last_try_ms = millis();
  start_client();
}
