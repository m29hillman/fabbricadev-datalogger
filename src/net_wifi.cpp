#include "net_wifi.h"

#include "config.h"
#include "logger.h"
#include "settings.h"

#include <ESPmDNS.h>
#include <WiFi.h>

namespace {

bool g_sta_up = false;
bool g_ap_up = false;
bool g_apply_pending = false;
uint32_t g_last_begin_ms = 0;
uint32_t g_sta_since_ms = 0;
uint32_t g_sta_down_ms = 0;

constexpr uint32_t kApStopAfterMs = 2000;
constexpr uint32_t kApRestartAfterMs = 3000;

bool parse_ip(const char *text, IPAddress *out) {
  int a = 0;
  int b = 0;
  int c = 0;
  int d = 0;
  if (text == nullptr || sscanf(text, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
    return false;
  }
  if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) {
    return false;
  }
  *out = IPAddress(static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c),
                   static_cast<uint8_t>(d));
  return true;
}

void ap_start() {
  IPAddress ap_ip;
  IPAddress ap_mask(255, 255, 255, 0);
  parse_ip(WIFI_AP_IP, &ap_ip);
  WiFi.softAPConfig(ap_ip, ap_ip, ap_mask);
  if (!WiFi.softAP(WIFI_AP_SSID)) {
    g_ap_up = false;
    return;
  }
  WiFi.softAPsetHostname(WIFI_HOSTNAME);
  WiFi.setSleep(false);
  g_ap_up = true;
}

void ap_stop() {
  WiFi.softAPdisconnect(true);
  WiFi.setSleep(false);
  g_ap_up = false;
}

}  // namespace

void net_apply() {
  const Settings &cfg = settings_get();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(WIFI_HOSTNAME);

  if (cfg.ssid[0] != '\0' && !cfg.dhcp) {
    IPAddress ip;
    IPAddress mask;
    IPAddress gw;
    IPAddress dns;
    const char *dns_text = cfg.dns[0] != '\0' ? cfg.dns : cfg.gw;
    if (parse_ip(cfg.ip, &ip) && parse_ip(cfg.mask, &mask) && parse_ip(cfg.gw, &gw) &&
        parse_ip(dns_text, &dns)) {
      WiFi.config(ip, gw, mask, dns);
    } else {
      logger_printf("IP fixo invalido, ficando com IP dinamico nesta tentativa\r\n");
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }
  } else {
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  }

  if (cfg.ssid[0] != '\0') {
    WiFi.begin(cfg.ssid, cfg.wpass);
    g_last_begin_ms = millis();
  } else {
    WiFi.disconnect(false, false);
  }

  ap_start();
  g_sta_up = false;
  g_sta_since_ms = 0;
  g_sta_down_ms = 0;
}

void net_begin() {
  net_apply();
  logger_telnet_begin();
  logger_printf("Rede aberta sem senha: %s\r\n", WIFI_AP_SSID);
  logger_printf("Pagina http://%s  usuario admin  senha admin\r\n", WIFI_AP_IP);
  if (settings_get().ssid[0] != '\0') {
    logger_printf("Conectando na rede %s\r\n", settings_get().ssid);
  } else {
    logger_printf("Nenhuma rede da casa salva. Configure pela pagina.\r\n");
  }
}

void net_request_apply() { g_apply_pending = true; }

void net_service() {
  if (g_apply_pending) {
    g_apply_pending = false;
    g_sta_up = false;
    net_apply();
    logger_printf("Configuracao de rede aplicada\r\n");
    return;
  }
  const Settings &cfg = settings_get();
  if (cfg.ssid[0] == '\0') {
    g_sta_up = false;
    if (!g_ap_up) {
      ap_start();
      logger_printf("Rede aberta sem senha: %s\r\n", WIFI_AP_SSID);
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    g_sta_down_ms = 0;
    if (!g_sta_up) {
      g_sta_up = true;
      g_sta_since_ms = millis();
      WiFi.setHostname(WIFI_HOSTNAME);
      MDNS.end();
      if (MDNS.begin(WIFI_HOSTNAME)) {
        MDNS.addService("http", "tcp", WEB_PORT);
        MDNS.addService("telnet", "tcp", TELNET_PORT);
      }
      logger_printf("Wi-Fi %s conectado  ip %s  rssi %d dBm\r\n", WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      logger_printf("Pagina tambem em http://%s/\r\n", WiFi.localIP().toString().c_str());
    }
    if (g_ap_up && millis() - g_sta_since_ms >= kApStopAfterMs) {
      ap_stop();
      logger_printf("Rede aberta desligada enquanto %s estiver conectada\r\n", cfg.ssid);
    }
    return;
  }

  if (g_sta_up) {
    g_sta_up = false;
    g_sta_down_ms = millis();
    logger_printf("Wi-Fi da casa caiu\r\n");
  } else if (g_sta_down_ms == 0) {
    g_sta_down_ms = millis();
  }
  if (!g_ap_up && millis() - g_sta_down_ms >= kApRestartAfterMs) {
    ap_start();
    logger_printf("Rede aberta de volta: %s\r\n", WIFI_AP_SSID);
    logger_printf("Pagina http://%s\r\n", WIFI_AP_IP);
  }
  if (millis() - g_last_begin_ms >= 15000) {
    g_last_begin_ms = millis();
    WiFi.begin(cfg.ssid, cfg.wpass);
    logger_printf("Tentando Wi-Fi %s\r\n", cfg.ssid);
  }
}

bool net_sta_connected() { return g_sta_up && WiFi.status() == WL_CONNECTED; }

bool net_ap_up() { return g_ap_up; }

int net_rssi() { return net_sta_connected() ? WiFi.RSSI() : 0; }

String net_sta_ip() {
  if (!net_sta_connected()) {
    return "";
  }
  return WiFi.localIP().toString();
}

String net_ap_ip() { return WiFi.softAPIP().toString(); }
