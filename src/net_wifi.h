#pragma once

#include <Arduino.h>

void net_begin();
void net_service();
void net_apply();
void net_request_apply();
bool net_sta_connected();
bool net_ap_up();
int net_rssi();
String net_sta_ip();
String net_ap_ip();
