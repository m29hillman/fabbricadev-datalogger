#include "modbus_rtu.h"

uint16_t modbus_crc(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

bool modbus_crc_ok(const uint8_t *frame, size_t len) {
  if (len < 4) {
    return false;
  }
  uint16_t got = static_cast<uint16_t>(frame[len - 2]) |
                 (static_cast<uint16_t>(frame[len - 1]) << 8);
  return got == modbus_crc(frame, len - 2);
}

size_t modbus_append_crc(uint8_t *frame, size_t len, size_t max_len) {
  if (len + 2 > max_len) {
    return 0;
  }
  uint16_t crc = modbus_crc(frame, len);
  frame[len] = static_cast<uint8_t>(crc & 0xFF);
  frame[len + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  return len + 2;
}

size_t modbus_build_read(uint8_t slave, uint8_t fc, uint16_t pdu_addr, uint16_t count,
                         uint8_t *out, size_t max_len) {
  if (max_len < 8) {
    return 0;
  }
  out[0] = slave;
  out[1] = fc;
  out[2] = static_cast<uint8_t>((pdu_addr >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(pdu_addr & 0xFF);
  out[4] = static_cast<uint8_t>((count >> 8) & 0xFF);
  out[5] = static_cast<uint8_t>(count & 0xFF);
  return modbus_append_crc(out, 6, max_len);
}
