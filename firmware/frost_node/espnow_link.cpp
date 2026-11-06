#include "espnow_link.h"

#include <FrostCore.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "config.h"

namespace {
uint8_t g_gateway[6];
uint8_t g_channel = 1;
BeaconHandler g_onBeacon = nullptr;
volatile uint8_t g_failStreak = 0;
volatile uint32_t g_sent = 0, g_failed = 0;

void handleStatus(bool ok) {
  if (ok) {
    g_failStreak = 0;
  } else {
    g_failed = g_failed + 1;  // diagnostic counters: a rare lost increment between tasks is harmless
    if (g_failStreak < 255) g_failStreak = g_failStreak + 1;
  }
}

// The callback signatures changed in ESP-IDF 5.0 (receive) and 5.5 (send).
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void onSent(const wifi_tx_info_t*, esp_now_send_status_t status) { handleStatus(status == ESP_NOW_SEND_SUCCESS); }
#else
void onSent(const uint8_t*, esp_now_send_status_t status) { handleStatus(status == ESP_NOW_SEND_SUCCESS); }
#endif

void handleRecv(const uint8_t* data, int len) {
  if (g_onBeacon && frost::validBeacon(data, (size_t)len)) {
    frost::TimeBeacon b;
    memcpy(&b, data, sizeof(b));
    g_onBeacon(b.unixTs);
  }
}

#if ESP_IDF_VERSION_MAJOR >= 5
void onRecv(const esp_now_recv_info_t*, const uint8_t* data, int len) { handleRecv(data, len); }
#else
void onRecv(const uint8_t*, const uint8_t* data, int len) { handleRecv(data, len); }
#endif

bool startEspNow() {
  esp_now_deinit();
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, g_gateway, 6);
  peer.channel = g_channel;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}
}  // namespace

bool linkBegin(uint8_t channel, const uint8_t gatewayMac[6], BeaconHandler onBeacon) {
  memcpy(g_gateway, gatewayMac, 6);
  g_channel = channel;
  g_onBeacon = onBeacon;
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  return startEspNow();
}

bool linkSend(const void* data, size_t len) {
  if (g_failStreak >= SEND_FAILS_BEFORE_REINIT) {
    // Gateway unreachable for a while: re-create the ESP-NOW stack and peer, then keep trying.
    startEspNow();
    g_failStreak = 0;
  }
  g_sent = g_sent + 1;
  const esp_err_t err = esp_now_send(g_gateway, static_cast<const uint8_t*>(data), len);
  if (err != ESP_OK) handleStatus(false);
  return err == ESP_OK;
}

uint8_t linkFailStreak() { return g_failStreak; }
uint32_t linkSentTotal() { return g_sent; }
uint32_t linkFailedTotal() { return g_failed; }

void linkMacString(char* out, size_t n) { snprintf(out, n, "%s", WiFi.macAddress().c_str()); }
