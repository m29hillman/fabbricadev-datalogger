#pragma once

#include <Arduino.h>

void logger_begin();
void logger_telnet_begin();
void logger_printf(const char *fmt, ...);
void logger_hexdump(const char *title, const uint8_t *data, size_t len);
void logger_service();
bool logger_take_command(char *dest, size_t dest_len);
