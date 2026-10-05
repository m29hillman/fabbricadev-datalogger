#include "inverter_data.h"

#include "inverter_model.h"

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

struct Num {
  bool known;
  bool missing;
  bool neg;
  uint32_t mag;
  int8_t decimals;
};

struct Slot {
  uint16_t addr;
  const char *key;
  Num val;
};

Slot g_slots[] = {
    {5000, "tipo", {}},
    {5001, "potencia_nominal_kw", {}},
    {5003, "energia_dia_kwh", {}},
    {5004, "energia_total_kwh", {}},
    {5008, "temperatura_c", {}},
    {5011, "mppt1_tensao_v", {}},
    {5012, "mppt1_corrente_a", {}},
    {5013, "mppt2_tensao_v", {}},
    {5014, "mppt2_corrente_a", {}},
    {5017, "potencia_cc_w", {}},
    {5019, "tensao_v", {}},
    {5022, "corrente_a", {}},
    {5031, "potencia_w", {}},
    {5035, "fator_potencia", {}},
    {5036, "frequencia_hz", {}},
    {5038, "estado_codigo", {}},
};

char g_serial[24];
bool g_has_serial = false;
char g_estado[40];
bool g_has_estado = false;
uint32_t g_updated_ms = 0;
bool g_ever = false;

struct Buf {
  char *data;
  size_t cap;
  size_t n;
  bool first;

  void add(const char *text) {
    if (text == nullptr || n + 1 >= cap) {
      return;
    }
    size_t len = strlen(text);
    if (n + len >= cap) {
      len = cap - n - 1;
    }
    memcpy(data + n, text, len);
    n += len;
    data[n] = '\0';
  }

  void addf(const char *fmt, ...) {
    char tmp[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    add(tmp);
  }

  void key(const char *name) {
    if (!first) {
      add(",");
    }
    first = false;
    add("\"");
    add(name);
    add("\":");
  }

  void add_num(const Num &val) {
    if (!val.known || val.missing) {
      add("null");
      return;
    }
    if (val.decimals <= 0) {
      addf("%s%lu", val.neg ? "-" : "", static_cast<unsigned long>(val.mag));
      return;
    }
    uint32_t div = 1;
    for (int i = 0; i < val.decimals; ++i) {
      div *= 10;
    }
    uint32_t ip = val.mag / div;
    uint32_t fp = val.mag % div;
    if (val.decimals == 1) {
      addf("%s%lu.%lu", val.neg ? "-" : "", static_cast<unsigned long>(ip), static_cast<unsigned long>(fp));
    } else if (val.decimals == 3) {
      addf("%s%lu.%03lu", val.neg ? "-" : "", static_cast<unsigned long>(ip),
           static_cast<unsigned long>(fp));
    } else {
      addf("%s%lu", val.neg ? "-" : "", static_cast<unsigned long>(val.mag));
    }
  }

  void add_str(const char *text) {
    add("\"");
    for (const char *p = text; *p != '\0'; ++p) {
      if (*p == '"' || *p == '\\') {
        char esc[3] = {'\\', *p, '\0'};
        add(esc);
      } else if (static_cast<unsigned char>(*p) >= 32) {
        char one[2] = {*p, '\0'};
        add(one);
      }
    }
    add("\"");
  }
};

const char *estado_nome(uint16_t value) {
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
      return "";
  }
}

}  // namespace

void inverter_data_clear() {
  for (Slot &slot : g_slots) {
    slot.val = {};
  }
  g_serial[0] = '\0';
  g_has_serial = false;
  g_estado[0] = '\0';
  g_has_estado = false;
  g_updated_ms = 0;
  g_ever = false;
}

void inverter_data_set_serial(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  strncpy(g_serial, text, sizeof(g_serial) - 1);
  g_serial[sizeof(g_serial) - 1] = '\0';
  g_has_serial = true;
  g_updated_ms = millis();
  g_ever = true;
}

void inverter_data_set(uint16_t addr, bool missing, bool as_signed, uint8_t words, uint32_t raw,
                       int8_t decimals) {
  Slot *slot = nullptr;
  for (Slot &item : g_slots) {
    if (item.addr == addr) {
      slot = &item;
      break;
    }
  }
  if (slot == nullptr) {
    return;
  }
  slot->val.known = true;
  slot->val.missing = missing;
  slot->val.decimals = decimals;
  slot->val.neg = false;
  slot->val.mag = 0;
  if (!missing) {
    if (as_signed) {
      int32_t signed_value =
          words == 1 ? static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(raw)))
                     : static_cast<int32_t>(raw);
      if (signed_value < 0) {
        slot->val.neg = true;
        slot->val.mag = static_cast<uint32_t>(-signed_value);
      } else {
        slot->val.mag = static_cast<uint32_t>(signed_value);
      }
    } else {
      slot->val.mag = raw;
    }
  }
  if (addr == 5038) {
    g_has_estado = !missing;
    g_estado[0] = '\0';
    if (!missing) {
      strncpy(g_estado, estado_nome(static_cast<uint16_t>(raw)), sizeof(g_estado) - 1);
      g_estado[sizeof(g_estado) - 1] = '\0';
    }
  }
  g_updated_ms = millis();
  g_ever = true;
}

bool inverter_data_ready() { return g_ever; }

uint32_t inverter_data_age_ms() {
  if (!g_ever) {
    return UINT32_MAX;
  }
  return millis() - g_updated_ms;
}

size_t inverter_format_json(char *buf, size_t cap) {
  if (buf == nullptr || cap < 3) {
    return 0;
  }
  Buf out{buf, cap, 0, true};
  out.add("{");
  out.key("modelo");
  out.add_str(inverter_model_name());
  out.key("serie");
  if (g_has_serial) {
    out.add_str(g_serial);
  } else {
    out.add("null");
  }
  for (const Slot &slot : g_slots) {
    out.key(slot.key);
    out.add_num(slot.val);
  }
  out.key("estado");
  if (g_has_estado && g_estado[0] != '\0') {
    out.add_str(g_estado);
  } else {
    out.add("null");
  }
  out.add("}");
  return out.n;
}
