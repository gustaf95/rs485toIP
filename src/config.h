#pragma once

#include <Arduino.h>

// ---- RS485 / UART2 ----
#define RS485_RX_PIN_DEFAULT 16
#define RS485_TX_PIN_DEFAULT 17
#define RS485_DE_RE_PIN_DEFAULT 4
#define RS485_BAUD_DEFAULT 9600

// ---- VISCA framing ----
#define VISCA_BUFFER_SIZE 128
#define VISCA_MIN_PACKET_LEN 3
#define VISCA_PACKET_TIMEOUT_MS 50
#define VISCA_TERMINATOR 0xFF

// ---- VISCA addressing ----
#define VISCA_ADDR_CAM1 0x81
#define VISCA_ADDR_CAM2 0x82
#define VISCA_ADDR_CAM3 0x83
#define VISCA_ADDR_CAM4 0x84
#define VISCA_ADDR_CAM5 0x85
#define VISCA_ADDR_BROADCAST 0x88
#define VISCA_ADDR_REWRITE_TARGET 0x81

// ---- Network ----
#define DEFAULT_CAMERA_PORT 5678
#define SONY_VISCA_PORT 52381

// ---- Wi-Fi AP fallback ----
#define AP_FALLBACK_SSID "ESP32-VISCA-GW"
#define AP_FALLBACK_PASSWORD "visca1234"
#define AP_FALLBACK_IP IPAddress(192, 168, 4, 1)
#define WIFI_CONNECT_TIMEOUT_MS 15000

// ---- Web server ----
#define WEB_SERVER_PORT 80

// ---- Target devices ----
#define MAX_TARGET_DEVICES 8

// ---- Diagnostics ----
#define DIAG_LOG_DEPTH 10
