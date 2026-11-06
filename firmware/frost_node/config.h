// Build-time configuration of a sensor node (ESP32-S3-WROOM-1, Arduino-ESP32 core).
// Runtime values (thresholds, offsets, node id) can be changed over Serial and are kept in NVS.
#pragma once

// ---- DHT22 wiring (ESP32-S3) ----
// GPIO4/5 are free on the S3 (its strapping pins are 0, 3, 45, 46; 19/20 are USB; 26-32 are flash,
// and 33-37 are used by octal PSRAM on N8R8/N16R8 modules).
#define DHT1_DATA_PIN 4
#define DHT2_DATA_PIN 5
// Sensor power from a GPIO lets the firmware power-cycle a hung DHT22 (draws ~1.5 mA max).
// Set to -1 if the sensors are wired to 3V3 directly.
#define DHT1_POWER_PIN 6
#define DHT2_POWER_PIN 7

// ---- timing ----
#define SAMPLE_PERIOD_S 60        // one engine update + one ESP-NOW frame per minute (DHT22 allows >= 2 s)
#define LOG_PERIOD_S 300          // flash log every 5 min, plus every level change
#define DHT_FAILS_BEFORE_POWER_CYCLE 5

// ---- ESP-NOW ----
#define ESPNOW_CHANNEL 1
// Gateway MAC. All 0xFF = broadcast (no link-layer ACK); set the real MAC for acknowledged unicast.
#define GATEWAY_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define SEND_FAILS_BEFORE_REINIT 10

// ---- clock ----
// Optional NTP over Wi-Fi. Leave WIFI_SSID empty to rely on the gateway beacon or the TIME command.
// If Wi-Fi is used, ESP-NOW must run on the router's channel.
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define TZ_OFFSET_S (6 * 3600)    // Bishkek, UTC+6

// ---- log files on LittleFS (needs a partition scheme with a SPIFFS/LittleFS partition) ----
#define LOG_FILE_A "/log_a.csv"
#define LOG_FILE_B "/log_b.csv"
#define LOG_FILE_MAX_BYTES (256 * 1024)  // two files -> ~16 days at 5-min logging
