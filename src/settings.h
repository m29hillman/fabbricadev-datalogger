#pragma once

#include <stdbool.h>
#include <stdint.h>

struct Settings {
  char user[32];
  char upass[64];
  char hostname[33];
  char ssid[33];
  char wpass[65];
  bool dhcp;
  char ip[16];
  char mask[16];
  char gw[16];
  char dns[16];
  bool mqtt_on;
  char mqtt_url[180];
  uint16_t mqtt_every_s;
  char ntp_server[64];
  int8_t ntp_gmt;
  char modelo[32];
};

void settings_begin();
const Settings &settings_get();
bool settings_check_login(const char *user, const char *pass);
bool settings_is_default_password();
const char *settings_save_network(const char *ssid, const char *wpass, bool dhcp, const char *ip,
                                  const char *mask, const char *gw, const char *dns,
                                  const char *hostname);
const char *settings_save_mqtt(bool enabled, const char *url, uint16_t every_s);
const char *settings_save_ntp(const char *server, int gmt);
const char *settings_save_model(const char *id);
const char *settings_change_password(const char *atual, const char *nova);
void settings_reset_password();
const char *settings_factory_reset(const char *senha);
