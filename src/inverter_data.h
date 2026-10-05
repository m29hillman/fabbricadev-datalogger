#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void inverter_data_clear();
void inverter_data_set_serial(const char *text);
void inverter_data_set(uint16_t addr, bool missing, bool as_signed, uint8_t words, uint32_t raw,
                       int8_t decimals);
bool inverter_data_ready();
uint32_t inverter_data_age_ms();
size_t inverter_format_json(char *buf, size_t cap);
