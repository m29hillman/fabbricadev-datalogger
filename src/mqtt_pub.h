#pragma once

#include <stdbool.h>

void mqtt_begin();
void mqtt_service(bool allow_connect);
void mqtt_reconfigure();
bool mqtt_url_valid(const char *url);
bool mqtt_connected();
const char *mqtt_topic();
