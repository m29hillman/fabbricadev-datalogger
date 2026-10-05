#include "settings.h"

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
  g_prefs.putString("ssid", g_settings.ssid);
  g_prefs.putString("wpass", g_settings.wpass);
  g_prefs.putBool("dhcp", g_settings.dhcp);
  g_prefs.putString("ip", g_settings.ip);
  g_prefs.putString("mask", g_settings.mask);
  g_prefs.putString("gw", g_settings.gw);
  g_prefs.putString("dns", g_settings.dns);
  g_prefs.putBool("mqtt_on", g_settings.mqtt_on);
  g_prefs.putString("mqtt_url", g_settings.mqtt_url);
  g_prefs.putString("modelo", g_settings.modelo);
}

}  // namespace

void settings_begin() {
  memset(&g_settings, 0, sizeof(g_settings));
  clip(g_settings.user, sizeof(g_settings.user), "admin");
  clip(g_settings.upass, sizeof(g_settings.upass), "admin");
  clip(g_settings.modelo, sizeof(g_settings.modelo), "sungrow-sg6k-d");
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

  load_string("user", g_settings.user, sizeof(g_settings.user), "admin");
  load_string("upass", g_settings.upass, sizeof(g_settings.upass), "admin");
  load_string("ssid", g_settings.ssid, sizeof(g_settings.ssid), "");
  load_string("wpass", g_settings.wpass, sizeof(g_settings.wpass), "");
  if (strcmp(g_settings.ssid, "Piemont Office") == 0) {
    clip(g_settings.ssid, sizeof(g_settings.ssid), "");
    clip(g_settings.wpass, sizeof(g_settings.wpass), "");
    g_prefs.putString("ssid", "");
    g_prefs.putString("wpass", "");
    logger_printf("Rede padrao removida. Configure o Wi-Fi pela pagina.\r\n");
  }
  g_settings.dhcp = g_prefs.getBool("dhcp", true);
  load_string("ip", g_settings.ip, sizeof(g_settings.ip), "");
  load_string("mask", g_settings.mask, sizeof(g_settings.mask), "");
  load_string("gw", g_settings.gw, sizeof(g_settings.gw), "");
  load_string("dns", g_settings.dns, sizeof(g_settings.dns), "");
  g_settings.mqtt_on = g_prefs.getBool("mqtt_on", false);
  load_string("mqtt_url", g_settings.mqtt_url, sizeof(g_settings.mqtt_url), "");
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

const char *settings_save_network(const char *ssid, const char *wpass, bool dhcp, const char *ip,
                                  const char *mask, const char *gw, const char *dns) {
  if (ssid == nullptr) {
    ssid = "";
  }
  if (strlen(ssid) > 32) {
    return "SSID longo demais";
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
  persist();
  return nullptr;
}

const char *settings_save_mqtt(bool enabled, const char *url) {
  if (url == nullptr) {
    url = "";
  }
  if (strlen(url) >= sizeof(g_settings.mqtt_url)) {
    return "URL longa demais";
  }
  if (enabled && url[0] == '\0') {
    return "Informe a URL do broker";
  }
  g_settings.mqtt_on = enabled;
  clip(g_settings.mqtt_url, sizeof(g_settings.mqtt_url), url);
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
  clip(g_settings.modelo, sizeof(g_settings.modelo), "sungrow-sg6k-d");
  persist();
  return nullptr;
}
