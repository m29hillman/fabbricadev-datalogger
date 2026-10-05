#pragma once

#include <Arduino.h>

void rs485_begin(uint32_t baud);
void rs485_set_baud(uint32_t baud);
uint32_t rs485_baud();
bool rs485_rx_pending();
size_t rs485_read_frame(uint8_t *out, size_t max_len);
size_t rs485_send(const uint8_t *data, size_t len);
