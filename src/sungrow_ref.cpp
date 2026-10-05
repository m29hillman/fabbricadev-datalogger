#include "sungrow_ref.h"

#include "inverter_data.h"
#include "logger.h"

#include <cstdio>

namespace {

struct Field {
  uint16_t addr;
  uint8_t words;
  int8_t decimals;
  bool sign;
  const char *name;
  const char *unit;
};

const Field kFields[] = {
    {5000, 1, 0, false, "codigo do tipo", ""},
    {5001, 1, 1, false, "potencia ativa nominal", "kW"},
    {5002, 1, 0, false, "tipo de saida", ""},
    {5003, 1, 1, false, "energia do dia", "kWh"},
    {5004, 2, 0, false, "energia total", "kWh"},
    {5006, 2, 0, false, "tempo total de operacao", "h"},
    {5008, 1, 1, true, "temperatura interna", "C"},
    {5009, 2, 0, false, "potencia aparente total", "VA"},
    {5011, 1, 1, false, "tensao MPPT 1", "V"},
    {5012, 1, 1, false, "corrente MPPT 1", "A"},
    {5013, 1, 1, false, "tensao MPPT 2", "V"},
    {5014, 1, 1, false, "corrente MPPT 2", "A"},
    {5015, 1, 1, false, "tensao MPPT 3", "V"},
    {5016, 1, 1, false, "corrente MPPT 3", "A"},
    {5017, 2, 0, false, "potencia CC total", "W"},
    {5019, 1, 1, false, "tensao fase A", "V"},
    {5020, 1, 1, false, "tensao fase B", "V"},
    {5021, 1, 1, false, "tensao fase C", "V"},
    {5022, 1, 1, false, "corrente fase A", "A"},
    {5023, 1, 1, false, "corrente fase B", "A"},
    {5024, 1, 1, false, "corrente fase C", "A"},
    {5031, 2, 0, false, "potencia ativa total", "W"},
    {5033, 2, 0, true, "potencia reativa total", "var"},
    {5035, 1, 3, true, "fator de potencia", ""},
    {5036, 1, 1, false, "frequencia da rede", "Hz"},
    {5038, 1, 0, false, "estado de trabalho", ""},
};

const Field *find_field(uint16_t addr) {
  for (const Field &field : kFields) {
    if (field.addr == addr) {
      return &field;
    }
  }
  return nullptr;
}

uint16_t read_u16(const uint8_t *data) {
  return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}

void print_scaled(int32_t value, int decimals, const char *unit) {
  if (decimals <= 0) {
    logger_printf("%ld", static_cast<long>(value));
  } else {
    int32_t div = 1;
    for (int i = 0; i < decimals; ++i) {
      div *= 10;
    }
    bool neg = value < 0;
    int32_t mag = neg ? -value : value;
    int32_t ip = mag / div;
    int32_t fp = mag % div;
    if (decimals == 1) {
      logger_printf("%s%ld.%ld", neg ? "-" : "", static_cast<long>(ip), static_cast<long>(fp));
    } else if (decimals == 3) {
      logger_printf("%s%ld.%03ld", neg ? "-" : "", static_cast<long>(ip), static_cast<long>(fp));
    } else {
      logger_printf("%s%ld/%d", neg ? "-" : "", static_cast<long>(mag), div);
    }
  }
  if (unit != nullptr && unit[0] != '\0') {
    logger_printf(" %s", unit);
  }
}

const char *output_type_name(uint16_t value) {
  switch (value) {
    case 0:
      return "bifasico";
    case 1:
      return "trifasico 3P4L";
    case 2:
      return "trifasico 3P3L";
    default:
      return nullptr;
  }
}

const char *work_state_name(uint16_t value) {
  switch (value) {
    case 0x0000:
      return "gerando";
    case 0x8000:
      return "parado";
    case 0x1200:
      return "standby inicial";
    case 0x1300:
      return "parado por tecla";
    case 0x1400:
      return "standby";
    case 0x1500:
      return "parada de emergencia";
    case 0x1600:
      return "partindo";
    case 0x2500:
      return "falha de comunicacao";
    case 0x5500:
      return "falha";
    case 0x8100:
      return "gerando com derating";
    case 0x8200:
      return "gerando sob despacho";
    case 0x9100:
      return "gerando em alarme";
    default:
      return nullptr;
  }
}

bool unavailable(const Field &field, uint32_t raw) {
  if (field.words == 1) {
    return field.sign ? raw == 0x7FFF : raw == 0xFFFF;
  }
  return field.sign ? raw == 0x7FFFFFFFUL : raw == 0xFFFFFFFFUL;
}

void print_serial(uint16_t protocol_addr, const uint8_t *data, size_t regs) {
  if (protocol_addr > 4999 || protocol_addr + regs <= 4990) {
    return;
  }
  char text[24];
  size_t n = 0;
  for (uint16_t addr = 4990; addr <= 4999 && n + 1 < sizeof(text); ++addr) {
    if (addr < protocol_addr || addr >= protocol_addr + regs) {
      continue;
    }
    size_t index = static_cast<size_t>(addr - protocol_addr) * 2;
    for (int b = 0; b < 2 && n + 1 < sizeof(text); ++b) {
      char c = static_cast<char>(data[index + b]);
      if (c == '\0') {
        text[n] = '\0';
        inverter_data_set_serial(text);
        logger_printf("  serie = \"%s\"\r\n", text);
        return;
      }
      text[n++] = (c >= 32 && c < 127) ? c : '.';
    }
  }
  text[n] = '\0';
  if (n > 0) {
    inverter_data_set_serial(text);
    logger_printf("  serie = \"%s\"\r\n", text);
  }
}

}  // namespace

void sungrow_print_registers(uint16_t protocol_addr, const uint8_t *data, size_t len) {
  if (len < 2) {
    return;
  }
  if (len % 2 != 0) {
    logger_printf("  payload com tamanho impar (%u), ultimo byte ignorado na leitura por registrador\r\n",
                  static_cast<unsigned>(len));
    len -= 1;
  }
  size_t regs = len / 2;
  logger_printf("  referencia Sungrow V1.1.37 (conferir no SG6K-D; o quadro cru e o que vale):\r\n");
  print_serial(protocol_addr, data, regs);

  size_t i = 0;
  while (i < regs) {
    uint16_t addr = static_cast<uint16_t>(protocol_addr + i);
    uint16_t word = read_u16(data + i * 2);
    const Field *field = find_field(addr);
    if (field != nullptr && field->words == 2 && i + 1 < regs) {
      uint16_t word_hi = read_u16(data + (i + 1) * 2);
      uint32_t raw = static_cast<uint32_t>(word) | (static_cast<uint32_t>(word_hi) << 16);
      inverter_data_set(addr, unavailable(*field, raw), field->sign, 2, raw, field->decimals);
      logger_printf("  reg %u %s = 0x%08lX", addr, field->name, static_cast<unsigned long>(raw));
      if (unavailable(*field, raw)) {
        logger_printf(" (indisponivel)\r\n");
      } else if (!field->sign && field->decimals == 0) {
        logger_printf(" (%lu %s)\r\n", static_cast<unsigned long>(raw), field->unit);
      } else {
        logger_printf(" (");
        print_scaled(static_cast<int32_t>(raw), field->decimals, field->unit);
        logger_printf(")\r\n");
      }
      i += 2;
      continue;
    }

    logger_printf("  reg %u", addr);
    if (field != nullptr && field->words == 1) {
      inverter_data_set(addr, unavailable(*field, word), field->sign, 1, word, field->decimals);
      logger_printf(" %s = 0x%04X", field->name, word);
      if (unavailable(*field, word)) {
        logger_printf(" (indisponivel)\r\n");
      } else {
        logger_printf(" (");
        if (field->sign) {
          print_scaled(static_cast<int16_t>(word), field->decimals, field->unit);
        } else {
          print_scaled(word, field->decimals, field->unit);
        }
        logger_printf(")");
        if (addr == 5002) {
          const char *name = output_type_name(word);
          if (name != nullptr) {
            logger_printf(" %s", name);
          }
        } else if (addr == 5038) {
          const char *name = work_state_name(word);
          if (name != nullptr) {
            logger_printf(" %s", name);
          }
        }
        logger_printf("\r\n");
      }
    } else {
      logger_printf(" = 0x%04X (%u)\r\n", word, word);
    }
    i += 1;
  }
}

void sungrow_print_map() {
  logger_printf("Mapa de referencia Sungrow, registradores somente leitura, funcao 04.\r\n");
  logger_printf("No quadro Modbus o endereco enviado e o do manual menos 1.\r\n");
  logger_printf("U32 vai com a palavra baixa primeiro. Exemplo do manual, escravo 1, reg 5000:\r\n");
  logger_printf("  01 04 13 87 00 01 85 67\r\n");
  logger_printf("Registradores usados na decodificacao:\r\n");
  for (const Field &field : kFields) {
    logger_printf("  %u (%u palavra%s) %s\r\n", field.addr, field.words, field.words > 1 ? "s" : "",
                  field.name);
  }
  logger_printf("Serie do equipamento: registradores 4990 a 4999, texto.\r\n");
  logger_printf("Padrao de fabrica do barramento: 9600 8N1, escravo 1.\r\n");
}
