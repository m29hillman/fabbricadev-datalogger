#pragma once

#include <Arduino.h>

uint16_t modbus_crc(const uint8_t *data, size_t len);
bool modbus_crc_ok(const uint8_t *frame, size_t len);
size_t modbus_append_crc(uint8_t *frame, size_t len, size_t max_len);
size_t modbus_build_read(uint8_t slave, uint8_t fc, uint16_t pdu_addr, uint16_t count,
                         uint8_t *out, size_t max_len);
