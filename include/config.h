#pragma once

// Nome anunciado no DHCP, no mDNS e na rede aberta de configuracao.
#define WIFI_HOSTNAME "fabbricadev-datalogger"
#define WIFI_AP_SSID "fabbricadev-datalogger"
#define WIFI_AP_IP "192.168.4.1"

#define TELNET_PORT 23
#define TELNET_MAX_CLIENTS 2
#define WEB_PORT 80
#define MQTT_TOPIC_DEFAULT "fabbricadev/inversor"

// RS485 isolado da Waveshare ESP32-S3-RS485-CAN.
// DE em HIGH transmite; em LOW recebe. DE e /RE estao unidos no GPIO21.
#define RS485_TX_PIN 17
#define RS485_RX_PIN 18
#define RS485_DE_PIN 21
#define RS485_UART_NUM 1

#define MODBUS_SLAVE_DEFAULT 1
#define MODBUS_BAUD_DEFAULT 9600
#define MODBUS_RESPONSE_TIMEOUT_MS 800
#define MODBUS_POLL_INTERVAL_MS 2500
