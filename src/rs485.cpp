#include "rs485.h"

#include "config.h"

#include <cstring>

namespace {

HardwareSerial g_serial(RS485_UART_NUM);
uint32_t g_baud = MODBUS_BAUD_DEFAULT;
uint8_t g_buf[512];
size_t g_len = 0;
uint32_t g_last_us = 0;

uint32_t frame_gap_us() {
  uint32_t char_us = (10UL * 1000000UL) / g_baud;
  uint32_t gap = char_us * 4;
  if (gap < 2000) {
    gap = 2000;
  }
  return gap;
}

void drain_assembler() {
  g_len = 0;
}

}  // namespace

void rs485_begin(uint32_t baud) {
  pinMode(RS485_DE_PIN, OUTPUT);
  digitalWrite(RS485_DE_PIN, LOW);
  g_baud = baud;
  g_serial.setRxBufferSize(1024);
  g_serial.begin(baud, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  drain_assembler();
}

void rs485_set_baud(uint32_t baud) {
  g_serial.end();
  rs485_begin(baud);
}

uint32_t rs485_baud() { return g_baud; }

bool rs485_rx_pending() { return g_len > 0; }

size_t rs485_read_frame(uint8_t *out, size_t max_len) {
  while (g_serial.available() > 0) {
    int raw = g_serial.read();
    if (raw < 0) {
      break;
    }
    if (g_len < sizeof(g_buf)) {
      g_buf[g_len++] = static_cast<uint8_t>(raw);
    }
    g_last_us = micros();
  }
  if (g_len == 0) {
    return 0;
  }
  if (static_cast<uint32_t>(micros() - g_last_us) < frame_gap_us()) {
    return 0;
  }
  size_t n = g_len;
  if (n > max_len) {
    n = max_len;
  }
  memcpy(out, g_buf, n);
  g_len = 0;
  return n;
}

size_t rs485_send(const uint8_t *data, size_t len) {
  while (g_serial.available() > 0) {
    g_serial.read();
  }
  drain_assembler();

  digitalWrite(RS485_DE_PIN, HIGH);
  delayMicroseconds(80);
  g_serial.write(data, len);
  g_serial.flush();

  uint32_t bit_us = (1000000UL + g_baud - 1) / g_baud;
  uint32_t tail_us = bit_us * 12;
  if (tail_us < 80) {
    tail_us = 80;
  }
  delayMicroseconds(tail_us);

  digitalWrite(RS485_DE_PIN, LOW);
  delayMicroseconds(30);

  size_t echo = 0;
  while (g_serial.available() > 0) {
    g_serial.read();
    echo++;
  }
  return echo;
}
