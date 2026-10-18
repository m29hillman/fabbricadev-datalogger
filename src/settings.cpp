#include "settings.h"

#include "config.h"
#include "inverter_model.h"
#include "logger.h"
#include "secrets.h"

#include <Preferences.h>

#include <cstring>

namespace {

Preferences g_prefs;
Settings g_settings;
bool g_ready = false;

void clip(char *dst, size_t n, const char *src) {
  if (src == nullptr) {
    src = "";
  }
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

bool secrets_have_wifi() {
  return WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "SUA_REDE") != 0;
}

void load_string(const char *key, char *dst, size_t n, const char *fallback) {
  String value = g_prefs.getString(key, fallback);
  clip(dst, n, value.c_str());
}

void persist() {
  if (!g_ready) {
    return;
  }
  g_prefs.putString("user", g_settings.user);
  g_prefs.putString("upass", g_settings.upass);
  g_prefs.putString("host", g_settings.hostname);
  g_prefs.putString("ssid", g_settings.ssid);
  g_prefs.putString("wpass", g_settings.wpass);
  g_prefs.putBool("dhcp", g_settings.dhcp);
  g_prefs.putString("ip", g_settings.ip);
  g_prefs.putString("mask", g_settings.mask);
  g_prefs.putString("gw", g_settings.gw);
  g_prefs.putString("dns", g_settings.dns);
  g_prefs.putString("host", g_settings.hostname);
  g_prefs.putBool("mqtt_on", g_settings.mqtt_on);
  g_prefs.putString("mqtt_url", g_settings.mqtt_url);
  g_prefs.putUShort("mqtt_s", g_settings.mqtt_every_s);
  g_prefs.putString("ntp", g_settings.ntp_server);
  g_prefs.putChar("ntp_gmt", g_settings.ntp_gmt);
  g_prefs.putString("modelo", g_settings.modelo);
}

}  // namespace

void settings_begin() {
  memset(&g_settings, 0, sizeof(g_settings));
  clip(g_settings.user, sizeof(g_settings.user), "admin");
  clip(g_settings.upass, sizeof(g_settings.upass), "admin");
  clip(g_settings.modelo, sizeof(g_settings.modelo), "sungrow-sg6k-d");
  clip(g_settings.hostname, sizeof(g_settings.hostname), WIFI_HOSTNAME);
  g_settings.mqtt_every_s = MQTT_PUBLISH_DEFAULT_S;
  clip(g_settings.ntp_server, sizeof(g_settings.ntp_server), NTP_SERVER_DEFAULT);
  g_settings.ntp_gmt = NTP_GMT_DEFAULT;
  g_settings.dhcp = true;

  g_ready = g_prefs.begin("casa", false);
  if (!g_ready) {
    logger_printf("Memoria de configuracao indisponivel. Login admin / admin vale ate reiniciar.\r\n");
    return;
  }

  if (!g_prefs.isKey("user")) {
    g_prefs.putString("user", "admin");
    g_prefs.putString("upass", "admin");
  }
  if (!g_prefs.isKey("ssid")) {
    if (secrets_have_wifi()) {
      g_prefs.putString("ssid", WIFI_SSID);
      g_prefs.putString("wpass", WIFI_PASSWORD);
      logger_printf("Rede inicial copiada de include/secrets.h. A pagina web passa a mandar.\r\n");
    } else {
      g_prefs.putString("ssid", "");
      g_prefs.putString("wpass", "");
    }
    g_prefs.putBool("dhcp", true);
  }
  if (!g_prefs.isKey("mqtt_on")) {
    g_prefs.putBool("mqtt_on", false);
    g_prefs.putString("mqtt_url", "");
  }
  if (!g_prefs.isKey("modelo")) {
    g_prefs.putString("modelo", "sungrow-sg6k-d");
  }
  if (!g_prefs.isKey("host")) {
    g_prefs.putString("host", WIFI_HOSTNAME);
  }

  load_string("user", g_settings.user, sizeof(g_settings.user), "admin");
  load_string("upass", g_settings.upass, sizeof(g_settings.upass), "admin");
  load_string("ssid", g_settings.ssid, sizeof(g_settings.ssid), "");
  load_string("wpass", g_settings.wpass, sizeof(g_settings.wpass), "");
  g_settings.dhcp = g_prefs.getBool("dhcp", true);
  load_string("ip", g_settings.ip, sizeof(g_settings.ip), "");
  load_string("mask", g_settings.mask, sizeof(g_settings.mask), "");
  load_string("gw", g_settings.gw, sizeof(g_settings.gw), "");
  load_string("dns", g_settings.dns, sizeof(g_settings.dns), "");
  load_string("host", g_settings.hostname, sizeof(g_settings.hostname), WIFI_HOSTNAME);
  if (g_settings.hostname[0] == '\0') {
    clip(g_settings.hostname, sizeof(g_settings.hostname), WIFI_HOSTNAME);
  }
  g_settings.mqtt_on = g_prefs.getBool("mqtt_on", false);
  load_string("mqtt_url", g_settings.mqtt_url, sizeof(g_settings.mqtt_url), "");
  g_settings.mqtt_every_s = g_prefs.getUShort("mqtt_s", MQTT_PUBLISH_DEFAULT_S);
  if (g_settings.mqtt_every_s < 1 || g_settings.mqtt_every_s > 3600) {
    g_settings.mqtt_every_s = MQTT_PUBLISH_DEFAULT_S;
  }
  load_string("ntp", g_settings.ntp_server, sizeof(g_settings.ntp_server), NTP_SERVER_DEFAULT);
  if (g_settings.ntp_server[0] == '\0') {
    clip(g_settings.ntp_server, sizeof(g_settings.ntp_server), NTP_SERVER_DEFAULT);
  }
  g_settings.ntp_gmt = g_prefs.getChar("ntp_gmt", NTP_GMT_DEFAULT);
  if (g_settings.ntp_gmt < -12 || g_settings.ntp_gmt > 14) {
    g_settings.ntp_gmt = NTP_GMT_DEFAULT;
  }
  load_string("modelo", g_settings.modelo, sizeof(g_settings.modelo), "sungrow-sg6k-d");
  if (inverter_model_find(g_settings.modelo) == nullptr) {
    clip(g_settings.modelo, sizeof(g_settings.modelo), "sungrow-sg6k-d");
  }
}

const Settings &settings_get() { return g_settings; }

bool settings_check_login(const char *user, const char *pass) {
  if (user == nullptr || pass == nullptr) {
    return false;
  }
  return strcmp(user, g_settings.user) == 0 && strcmp(pass, g_settings.upass) == 0;
}

bool settings_is_default_password() {
  return strcmp(g_settings.user, "admin") == 0 && strcmp(g_settings.upass, "admin") == 0;
}

bool hostname_chars(const char *name) {
  if (name == nullptr || name[0] == '\0' || name[0] == '-' || strlen(name) > 32) {
    return false;
  }
  for (const char *p = name; *p != '\0'; ++p) {
    char c = *p;
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) {
      return false;
    }
  }
  return name[strlen(name) - 1] != '-';
}

const char *settings_save_network(const char *ssid, const char *wpass, bool dhcp, const char *ip,
                                  const char *mask, const char *gw, const char *dns,
                                  const char *hostname) {
  if (ssid == nullptr) {
    ssid = "";
  }
  if (strlen(ssid) > 32) {
    return "SSID longo demais";
  }
  char host[33];
  clip(host, sizeof(host), hostname);
  for (char *p = host; *p != '\0'; ++p) {
    if (*p >= 'A' && *p <= 'Z') {
      *p = static_cast<char>(*p - 'A' + 'a');
    }
  }
  if (!hostname_chars(host)) {
    return "Nome invalido. Use letras, numeros e hifen";
  }
  if (ssid[0] != '\0' && strcmp(ssid, g_settings.ssid) != 0 && (wpass == nullptr || wpass[0] == '\0')) {
    return "Informe a senha da nova rede";
  }
  if (wpass != nullptr && strlen(wpass) > 63) {
    return "Senha do Wi-Fi longa demais";
  }
  if (!dhcp) {
    if (ip == nullptr || ip[0] == '\0' || mask == nullptr || mask[0] == '\0' || gw == nullptr ||
        gw[0] == '\0') {
      return "No IP fixo informe endereco, mascara e gateway";
    }
  }

  clip(g_settings.ssid, sizeof(g_settings.ssid), ssid);
  if (wpass != nullptr && wpass[0] != '\0') {
    clip(g_settings.wpass, sizeof(g_settings.wpass), wpass);
  }
  if (ssid[0] == '\0') {
    g_settings.wpass[0] = '\0';
  }
  g_settings.dhcp = dhcp;
  clip(g_settings.ip, sizeof(g_settings.ip), ip);
  clip(g_settings.mask, sizeof(g_settings.mask), mask);
  clip(g_settings.gw, sizeof(g_settings.gw), gw);
  clip(g_settings.dns, sizeof(g_settings.dns), dns != nullptr && dns[0] != '\0' ? dns : gw);
  clip(g_settings.hostname, sizeof(g_settings.hostname), host);
  persist();
  return nullptr;
}

const char *settings_save_mqtt(bool enabled, const char *url, uint16_t every_s) {
  if (url == nullptr) {
    url = "";
  }
  if (strlen(url) >= sizeof(g_settings.mqtt_url)) {
    return "URL longa demais";
  }
  if (enabled && url[0] == '\0') {
    return "Informe a URL do broker";
  }
  if (every_s < 1 || every_s > 3600) {
    return "Intervalo de 1 a 3600 segundos";
  }
  g_settings.mqtt_on = enabled;
  clip(g_settings.mqtt_url, sizeof(g_settings.mqtt_url), url);
  g_settings.mqtt_every_s = every_s;
  persist();
  return nullptr;
}

const char *settings_save_ntp(const char *server, int gmt) {
  if (server == nullptr) {
    server = "";
  }
  size_t n = strlen(server);
  if (n < 1 || n >= sizeof(g_settings.ntp_server)) {
    return "Servidor NTP invalido";
  }
  for (size_t i = 0; i < n; ++i) {
    char c = server[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
    if (!ok || (i == 0 && (c == '.' || c == '-')) || (i + 1 == n && (c == '.' || c == '-'))) {
      return "Servidor NTP invalido";
    }
  }
  if (gmt < -12 || gmt > 14) {
    return "Fuso de -12 a 14";
  }
  clip(g_settings.ntp_server, sizeof(g_settings.ntp_server), server);
  g_settings.ntp_gmt = static_cast<int8_t>(gmt);
  persist();
  return nullptr;
}

const char *settings_save_model(const char *id) {
  if (inverter_model_find(id) == nullptr) {
    return "Modelo desconhecido";
  }
  clip(g_settings.modelo, sizeof(g_settings.modelo), id);
  persist();
  return nullptr;
}

const char *settings_change_password(const char *atual, const char *nova) {
  if (atual == nullptr || nova == nullptr || strcmp(atual, g_settings.upass) != 0) {
    return "Senha atual incorreta";
  }
  if (strlen(nova) < 4 || strlen(nova) > 63) {
    return "A nova senha precisa ter de 4 a 63 caracteres";
  }
  clip(g_settings.upass, sizeof(g_settings.upass), nova);
  persist();
  return nullptr;
}

void settings_reset_password() {
  clip(g_settings.user, sizeof(g_settings.user), "admin");
  clip(g_settings.upass, sizeof(g_settings.upass), "admin");
  persist();
}

const char *settings_factory_reset(const char *senha) {
  if (senha == nullptr || strcmp(senha, g_settings.upass) != 0) {
    return "Senha incorreta";
  }
  clip(g_settings.user, sizeof(g_settings.user), "admin");
  clip(g_settings.upass, sizeof(g_settings.upass), "admin");
  g_settings.ssid[0] = '\0';
  g_settings.wpass[0] = '\0';
  g_settings.dhcp = true;
  g_settings.ip[0] = '\0';
  g_settings.mask[0] = '\0';
  g_settings.gw[0] = '\0';
  g_settings.dns[0] = '\0';
  g_settings.mqtt_on = false;
  g_settings.mqtt_url[0] = '\0';
  g_settings.mqtt_every_s = MQTT_PUBLISH_DEFAULT_S;
  clip(g_settings.ntp_server, sizeof(g_settings.ntp_server), NTP_SERVER_DEFAULT);
  g_settings.ntp_gmt = NTP_GMT_DEFAULT;
  clip(g_settings.hostname, sizeof(g_settings.hostname), WIFI_HOSTNAME);
  clip(g_settings.modelo, sizeof(g_settings.modelo), "sungrow-sg6k-d");
  persist();
  return nullptr;
}
