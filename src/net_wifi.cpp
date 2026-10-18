#include "net_wifi.h"

#include "config.h"
#include "logger.h"
#include "settings.h"

#include <ESPmDNS.h>
#include <WiFi.h>

namespace {

enum class KickStep { kIdle, kBegin, kRadio };

bool g_sta_up = false;
bool g_ap_up = false;
bool g_apply_pending = false;
KickStep g_kick = KickStep::kIdle;
uint32_t g_kick_at = 0;
uint32_t g_last_begin_ms = 0;
uint32_t g_last_radio_ms = 0;
uint32_t g_sta_since_ms = 0;
uint32_t g_sta_down_ms = 0;

constexpr uint32_t kApStopAfterMs = 2000;
constexpr uint32_t kApAfterFailMs = 60000;
constexpr uint32_t kStaRetryMs = 20000;
constexpr uint32_t kStaDownBeforeRetryMs = 5000;
constexpr uint32_t kDhcpWaitMs = 45000;
constexpr uint32_t kRadioResetMs = 180000;

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

const char *device_name() {
  const char *name = settings_get().hostname;
  if (name == nullptr || name[0] == '\0') {
    return WIFI_HOSTNAME;
  }
  return name;
}

void ap_start() {
  IPAddress ap_ip;
  IPAddress ap_mask(255, 255, 255, 0);
  parse_ip(WIFI_AP_IP, &ap_ip);
  WiFi.softAPConfig(ap_ip, ap_ip, ap_mask);
  if (!WiFi.softAP(device_name())) {
    g_ap_up = false;
    return;
  }
  WiFi.softAPsetHostname(device_name());
  WiFi.setSleep(false);
  g_ap_up = true;
}

void ap_stop() {
  WiFi.softAPdisconnect(true);
  WiFi.setSleep(false);
  g_ap_up = false;
}

void apply_ip(const Settings &cfg) {
  if (cfg.ssid[0] != '\0' && !cfg.dhcp) {
    IPAddress ip;
    IPAddress mask;
    IPAddress gw;
    IPAddress dns;
    const char *dns_text = cfg.dns[0] != '\0' ? cfg.dns : cfg.gw;
    if (parse_ip(cfg.ip, &ip) && parse_ip(cfg.mask, &mask) && parse_ip(cfg.gw, &gw) &&
        parse_ip(dns_text, &dns)) {
      WiFi.config(ip, gw, mask, dns);
      return;
    }
    logger_printf("IP fixo invalido, ficando com IP dinamico nesta tentativa\r\n");
  }
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
}

void sta_prepare() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);
  WiFi.setHostname(device_name());
}

void sta_kick(bool radio_reset) {
  const Settings &cfg = settings_get();
  if (cfg.ssid[0] == '\0' || g_kick != KickStep::kIdle) {
    return;
  }
  g_last_begin_ms = millis();
  if (radio_reset) {
    logger_printf("Wi-Fi sem conexao. Reiniciando o radio para %s\r\n", cfg.ssid);
    g_last_radio_ms = millis();
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    g_ap_up = false;
    g_kick = KickStep::kRadio;
    g_kick_at = millis() + 200;
    return;
  }
  logger_printf("Tentando Wi-Fi %s\r\n", cfg.ssid);
  WiFi.disconnect(false, false);
  g_kick = KickStep::kBegin;
  g_kick_at = millis() + 100;
}

void kick_finish() {
  if (g_kick == KickStep::kIdle || static_cast<int32_t>(millis() - g_kick_at) < 0) {
    return;
  }
  const Settings &cfg = settings_get();
  KickStep step = g_kick;
  g_kick = KickStep::kIdle;
  if (cfg.ssid[0] == '\0') {
    return;
  }
  if (step == KickStep::kRadio) {
    sta_prepare();
  }
  apply_ip(cfg);
  WiFi.begin(cfg.ssid, cfg.wpass);
  g_last_begin_ms = millis();
  if (step == KickStep::kRadio) {
    ap_start();
    logger_printf("Rede aberta de volta: %s\r\n", device_name());
  }
}

}  // namespace

void net_apply() {
  const Settings &cfg = settings_get();
  uint32_t now = millis();
  g_kick = KickStep::kIdle;
  g_last_begin_ms = now;
  g_last_radio_ms = now;
  g_sta_up = false;
  g_sta_since_ms = 0;
  g_sta_down_ms = 0;
  g_ap_up = false;

  sta_prepare();
  if (cfg.ssid[0] == '\0') {
    WiFi.disconnect(true, true);
    ap_start();
    return;
  }

  ap_stop();
  apply_ip(cfg);
  WiFi.begin(cfg.ssid, cfg.wpass);
}

void net_begin() {
  net_apply();
  logger_telnet_begin();
  if (settings_get().ssid[0] != '\0') {
    logger_printf("Conectando na rede %s\r\n", settings_get().ssid);
    logger_printf("Sem conexao por 1 minuto, a rede aberta %s volta em http://%s\r\n", device_name(),
                  WIFI_AP_IP);
  } else {
    logger_printf("Rede aberta sem senha: %s\r\n", device_name());
    logger_printf("Pagina http://%s  usuario admin  senha admin\r\n", WIFI_AP_IP);
    logger_printf("Nenhuma rede da casa salva. Configure pela pagina.\r\n");
  }
}

void net_request_apply() { g_apply_pending = true; }

void net_apply_clock() {
  const Settings &cfg = settings_get();
  const char *server = cfg.ntp_server[0] != '\0' ? cfg.ntp_server : NTP_SERVER_DEFAULT;
  configTime(static_cast<long>(cfg.ntp_gmt) * 3600L, 0, server, "b.st1.ntp.br", "pool.ntp.br");
  logger_printf("NTP %s fuso GMT%+d\r\n", server, cfg.ntp_gmt);
}

void net_service() {
  kick_finish();
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
      logger_printf("Rede aberta sem senha: %s\r\n", device_name());
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    g_sta_down_ms = 0;
    if (!g_sta_up) {
      g_sta_up = true;
      g_sta_since_ms = millis();
      WiFi.setHostname(device_name());
      MDNS.end();
      if (MDNS.begin(device_name())) {
        MDNS.addService("http", "tcp", WEB_PORT);
        MDNS.addService("telnet", "tcp", TELNET_PORT);
      }
      net_apply_clock();
      logger_printf("Wi-Fi %s conectado  ip %s  rssi %d dBm\r\n", WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      logger_printf("Pagina tambem em http://%s/ e http://%s.local/\r\n",
                    WiFi.localIP().toString().c_str(), device_name());
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
  if (g_kick == KickStep::kIdle && !g_ap_up && millis() - g_sta_down_ms >= kApAfterFailMs) {
    ap_start();
    logger_printf("Rede aberta de volta: %s\r\n", device_name());
    logger_printf("Pagina http://%s\r\n", WIFI_AP_IP);
  }

  uint32_t down_for = millis() - g_sta_down_ms;
  bool attempt_fresh = millis() - g_last_begin_ms < kStaRetryMs;
  bool waiting_ip = WiFi.status() == WL_IDLE_STATUS && millis() - g_last_begin_ms < kDhcpWaitMs;
  if (attempt_fresh || waiting_ip || down_for < kStaDownBeforeRetryMs) {
    return;
  }
  bool radio_reset = down_for >= kRadioResetMs && millis() - g_last_radio_ms >= kRadioResetMs &&
                     WiFi.softAPgetStationNum() == 0;
  sta_kick(radio_reset);
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
