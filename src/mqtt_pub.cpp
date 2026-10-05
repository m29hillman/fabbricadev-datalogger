#include "mqtt_pub.h"

#include "config.h"
#include "inverter_data.h"
#include "logger.h"
#include "net_wifi.h"
#include "settings.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cstdlib>
#include <cstring>

namespace {

constexpr uint32_t kRetryMs = 8000;
constexpr uint32_t kPublishMs = 10000;

struct Parsed {
  bool tls;
  char host[64];
  uint16_t port;
  char user[64];
  char pass[64];
  char topic[96];
};

WiFiClient g_plain;
WiFiClientSecure g_secure;
PubSubClient g_mqtt;
Parsed g_parsed;
bool g_parsed_ok = false;
bool g_was_connected = false;
uint32_t g_last_try_ms = 0;
uint32_t g_last_pub_ms = 0;

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

bool parse_url(const char *url, Parsed *out) {
  memset(out, 0, sizeof(*out));
  out->port = 1883;
  clip(out->topic, sizeof(out->topic), MQTT_TOPIC_DEFAULT);
  if (url == nullptr) {
    return false;
  }
  while (*url == ' ') {
    url++;
  }
  const char *p = url;
  if (strncmp(p, "mqtt://", 7) == 0) {
    p += 7;
    out->tls = false;
    out->port = 1883;
  } else if (strncmp(p, "mqtts://", 8) == 0) {
    p += 8;
    out->tls = true;
    out->port = 8883;
  } else {
    return false;
  }

  char hostpart[160];
  const char *slash = strchr(p, '/');
  if (slash != nullptr) {
    size_t n = static_cast<size_t>(slash - p);
    if (n == 0 || n >= sizeof(hostpart)) {
      return false;
    }
    memcpy(hostpart, p, n);
    hostpart[n] = '\0';
    const char *topic = slash + 1;
    while (*topic == '/') {
      topic++;
    }
    if (*topic != '\0') {
      clip(out->topic, sizeof(out->topic), topic);
    }
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
  if (host[0] == '\0') {
    return false;
  }
  clip(out->host, sizeof(out->host), host);
  return out->topic[0] != '\0';
}

void apply_client() {
  if (g_parsed.tls) {
    g_secure.setInsecure();
    g_mqtt.setClient(g_secure);
  } else {
    g_mqtt.setClient(g_plain);
  }
  g_mqtt.setServer(g_parsed.host, g_parsed.port);
  g_mqtt.setBufferSize(1024);
  g_mqtt.setSocketTimeout(2);
}

void refresh_parse() {
  const Settings &cfg = settings_get();
  g_parsed_ok = cfg.mqtt_url[0] != '\0' && parse_url(cfg.mqtt_url, &g_parsed);
  if (g_mqtt.connected()) {
    g_mqtt.disconnect();
  }
  g_was_connected = false;
  if (g_parsed_ok) {
    apply_client();
  }
}

void publish_now() {
  if (!inverter_data_ready()) {
    return;
  }
  char payload[1024];
  size_t n = inverter_format_json(payload, sizeof(payload));
  if (n == 0 || n >= sizeof(payload) - 1) {
    return;
  }
  if (g_mqtt.publish(g_parsed.topic, payload, true)) {
    g_last_pub_ms = millis();
  }
}

}  // namespace

void mqtt_begin() {
  g_mqtt.setBufferSize(1024);
  refresh_parse();
}

void mqtt_reconfigure() { refresh_parse(); }

bool mqtt_url_valid(const char *url) {
  if (url == nullptr || url[0] == '\0') {
    return false;
  }
  Parsed parsed;
  return parse_url(url, &parsed);
}

bool mqtt_connected() { return g_mqtt.connected(); }

const char *mqtt_topic() { return g_parsed_ok ? g_parsed.topic : MQTT_TOPIC_DEFAULT; }

void mqtt_service(bool allow_connect) {
  const Settings &cfg = settings_get();
  if (!cfg.mqtt_on) {
    if (g_mqtt.connected()) {
      g_mqtt.disconnect();
      logger_printf("MQTT desligado\r\n");
    }
    g_was_connected = false;
    return;
  }
  if (!g_parsed_ok) {
    return;
  }
  if (g_mqtt.connected()) {
    g_mqtt.loop();
    if (!g_was_connected) {
      g_was_connected = true;
      logger_printf("MQTT conectado em %s:%u topico %s\r\n", g_parsed.host, g_parsed.port,
                    g_parsed.topic);
      publish_now();
    } else if (millis() - g_last_pub_ms >= kPublishMs) {
      publish_now();
    }
    return;
  }
  if (g_was_connected) {
    g_was_connected = false;
    logger_printf("MQTT desconectou\r\n");
  }
  if (!allow_connect || !net_sta_connected()) {
    return;
  }
  if (millis() - g_last_try_ms < kRetryMs) {
    return;
  }
  g_last_try_ms = millis();
  bool ok;
  if (g_parsed.user[0] != '\0') {
    ok = g_mqtt.connect(WIFI_HOSTNAME, g_parsed.user, g_parsed.pass);
  } else {
    ok = g_mqtt.connect(WIFI_HOSTNAME);
  }
  if (!ok) {
    logger_printf("MQTT sem conexao em %s:%u\r\n", g_parsed.host, g_parsed.port);
  }
}
