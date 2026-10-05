#include "logger.h"

#include "config.h"

#include <WiFi.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

constexpr size_t kBacklog = 6144;
constexpr size_t kLineMax = 160;
constexpr size_t kQueue = 4;

char g_backlog[kBacklog];
size_t g_back_pos = 0;
bool g_back_full = false;

char g_queue[kQueue][kLineMax];
uint8_t g_q_head = 0;
uint8_t g_q_count = 0;

struct LineAcc {
  char buf[kLineMax];
  size_t len;
  uint8_t iac;
};

LineAcc g_serial;
LineAcc g_client_line[TELNET_MAX_CLIENTS];
WiFiClient g_clients[TELNET_MAX_CLIENTS];
WiFiServer g_server(TELNET_PORT);
bool g_server_up = false;

void backlog_add(const char *data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    g_backlog[g_back_pos++] = data[i];
    if (g_back_pos >= kBacklog) {
      g_back_pos = 0;
      g_back_full = true;
    }
  }
}

void backlog_replay(WiFiClient &client) {
  if (!g_back_full) {
    if (g_back_pos > 0) {
      client.write(reinterpret_cast<const uint8_t *>(g_backlog), g_back_pos);
    }
    return;
  }
  client.write(reinterpret_cast<const uint8_t *>(g_backlog + g_back_pos), kBacklog - g_back_pos);
  if (g_back_pos > 0) {
    client.write(reinterpret_cast<const uint8_t *>(g_backlog), g_back_pos);
  }
}

void enqueue(const char *line) {
  if (g_q_count >= kQueue) {
    return;
  }
  uint8_t idx = (g_q_head + g_q_count) % kQueue;
  strncpy(g_queue[idx], line, kLineMax - 1);
  g_queue[idx][kLineMax - 1] = '\0';
  g_q_count++;
}

void feed_char(LineAcc &line, char ch) {
  if (ch == '\r' || ch == '\0') {
    return;
  }
  if (ch == '\n') {
    line.buf[line.len] = '\0';
    if (line.len > 0) {
      enqueue(line.buf);
    }
    line.len = 0;
    return;
  }
  if (line.len + 1 < kLineMax) {
    line.buf[line.len++] = ch;
  }
}

// Resposta minima a negociacao Telnet para o cliente nao misturar IAC no comando.
void feed_telnet(LineAcc &line, uint8_t byte) {
  if (line.iac == 1) {
    if (byte == 255) {
      feed_char(line, static_cast<char>(255));
      line.iac = 0;
    } else if (byte == 250) {
      line.iac = 3;
    } else if (byte >= 251 && byte <= 254) {
      line.iac = 2;
    } else {
      line.iac = 0;
    }
    return;
  }
  if (line.iac == 2) {
    line.iac = 0;
    return;
  }
  if (line.iac == 3) {
    if (byte == 240) {
      line.iac = 0;
    }
    return;
  }
  if (byte == 255) {
    line.iac = 1;
    return;
  }
  feed_char(line, static_cast<char>(byte));
}

void write_all(const char *data, size_t len) {
  Serial.write(reinterpret_cast<const uint8_t *>(data), len);
  for (uint8_t i = 0; i < TELNET_MAX_CLIENTS; ++i) {
    if (!g_clients[i] || !g_clients[i].connected()) {
      continue;
    }
    g_clients[i].write(reinterpret_cast<const uint8_t *>(data), len);
  }
  backlog_add(data, len);
}

}  // namespace

void logger_begin() {
  Serial.begin(115200);
  delay(200);
}

void logger_telnet_begin() {
  if (g_server_up) {
    return;
  }
  g_server.begin();
  g_server.setNoDelay(true);
  g_server_up = true;
}

void logger_printf(const char *fmt, ...) {
  char buf[384];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0) {
    return;
  }
  if (static_cast<size_t>(n) >= sizeof(buf)) {
    n = sizeof(buf) - 1;
  }
  write_all(buf, static_cast<size_t>(n));
}

void logger_hexdump(const char *title, const uint8_t *data, size_t len) {
  logger_printf("%s (%u bytes)\r\n", title, static_cast<unsigned>(len));
  for (size_t off = 0; off < len; off += 16) {
    char line[96];
    size_t n = len - off;
    if (n > 16) {
      n = 16;
    }
    int p = snprintf(line, sizeof(line), "  %04X  ", static_cast<unsigned>(off));
    if (p < 0) {
      continue;
    }
    for (size_t i = 0; i < 16 && p < static_cast<int>(sizeof(line)); ++i) {
      int wrote;
      if (i < n) {
        wrote = snprintf(line + p, sizeof(line) - static_cast<size_t>(p), "%02X ", data[off + i]);
      } else {
        wrote = snprintf(line + p, sizeof(line) - static_cast<size_t>(p), "   ");
      }
      if (wrote < 0) {
        break;
      }
      p += wrote;
    }
    if (p > 0 && p < static_cast<int>(sizeof(line))) {
      int wrote = snprintf(line + p, sizeof(line) - static_cast<size_t>(p), " ");
      if (wrote > 0) {
        p += wrote;
      }
    }
    for (size_t i = 0; i < n && p + 1 < static_cast<int>(sizeof(line)); ++i) {
      char c = static_cast<char>(data[off + i]);
      line[p++] = (c >= 32 && c < 127) ? c : '.';
    }
    if (p + 2 < static_cast<int>(sizeof(line))) {
      line[p++] = '\r';
      line[p++] = '\n';
      line[p] = '\0';
      write_all(line, static_cast<size_t>(p));
    }
  }
}

void logger_service() {
  if (g_server_up && g_server.hasClient()) {
    WiFiClient incoming = g_server.available();
    incoming.setNoDelay(true);
    int slot = -1;
    for (uint8_t i = 0; i < TELNET_MAX_CLIENTS; ++i) {
      if (!g_clients[i] || !g_clients[i].connected()) {
        if (g_clients[i]) {
          g_clients[i].stop();
        }
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      incoming.println("sem vaga no telnet");
      incoming.stop();
    } else {
      g_clients[slot] = incoming;
      g_client_line[slot].len = 0;
      g_client_line[slot].iac = 0;
      g_clients[slot].println("cliente telnet conectado. historico recente:");
      backlog_replay(g_clients[slot]);
      g_clients[slot].println("--- fim do historico ---");
      g_clients[slot].println("digite ajuda");
    }
  }

  for (uint8_t i = 0; i < TELNET_MAX_CLIENTS; ++i) {
    if (!g_clients[i]) {
      continue;
    }
    if (!g_clients[i].connected()) {
      g_clients[i].stop();
      continue;
    }
    int budget = 128;
    while (g_clients[i].available() && budget-- > 0) {
      int raw = g_clients[i].read();
      if (raw < 0) {
        break;
      }
      feed_telnet(g_client_line[i], static_cast<uint8_t>(raw));
    }
  }

  int budget = 128;
  while (Serial.available() && budget-- > 0) {
    int raw = Serial.read();
    if (raw < 0) {
      break;
    }
    feed_char(g_serial, static_cast<char>(raw));
  }
}

bool logger_take_command(char *dest, size_t dest_len) {
  if (g_q_count == 0 || dest_len == 0) {
    return false;
  }
  strncpy(dest, g_queue[g_q_head], dest_len - 1);
  dest[dest_len - 1] = '\0';
  g_q_head = (g_q_head + 1) % kQueue;
  g_q_count--;
  return true;
}
