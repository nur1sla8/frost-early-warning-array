// Frost Early-Warning Array: central node / gateway (any ESP32 with Arduino-ESP32 core 2.x/3.x).
//
// Receives TelemetryPacket frames over ESP-NOW, validates them (size, magic, version, CRC-16),
// drops duplicates, counts lost packets from sequence gaps, marks nodes OFFLINE after silence and
// prints everything as CSV on Serial for tools/serial_logger.py. Once its clock is set (TIME command
// from the PC) it broadcasts a time beacon every minute so nodes without RTC get timestamps.
//
// NOTE: with a single ESP32 available this sketch cannot be tested on hardware yet; the PC-side
// logger reads the node's own Serial CSV instead (same columns).

#include <Arduino.h>
#include <FrostCore.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <sys/time.h>
#include <time.h>

#define ESPNOW_CHANNEL 1
#define MAX_NODES 16
#define NODE_TIMEOUT_S 180  // 3 missed 60-s frames -> OFFLINE
#define BEACON_PERIOD_S 60
#define QUEUE_LEN 16

namespace {

const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct NodeState {
  bool used = false, online = false;
  uint8_t nodeId = 0;
  uint16_t bootId = 0;
  uint32_t lastSeq = 0, received = 0, lost = 0, duplicates = 0;
  uint32_t lastSeenMs = 0;
  uint8_t lastLevel = frost::UNKNOWN;
};

struct RxItem {
  frost::TelemetryPacket p;
  int8_t rssi;
  uint32_t ms;
};

NodeState g_nodes[MAX_NODES];
RxItem g_queue[QUEUE_LEN];
volatile uint8_t g_qHead = 0, g_qTail = 0;
volatile uint32_t g_badFrames = 0, g_queueOverflow = 0;
uint32_t g_lastBeaconMs = 0, g_lastCheckMs = 0;
String g_line;

bool clockValid() { return time(nullptr) > 1700000000; }

// Receive callback runs in the Wi-Fi task: only validate and enqueue, print from loop().
void handleRecv(const uint8_t* data, int len, int8_t rssi) {
  if (!frost::validPacket(data, (size_t)len)) {
    if (!frost::validBeacon(data, (size_t)len)) g_badFrames = g_badFrames + 1;
    return;
  }
  const uint8_t next = (g_qHead + 1) % QUEUE_LEN;
  if (next == g_qTail) {
    g_queueOverflow = g_queueOverflow + 1;
    return;
  }
  memcpy(&g_queue[g_qHead].p, data, sizeof(frost::TelemetryPacket));
  g_queue[g_qHead].rssi = rssi;
  g_queue[g_qHead].ms = millis();
  g_qHead = next;
}

#if ESP_IDF_VERSION_MAJOR >= 5
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  handleRecv(data, len, info && info->rx_ctrl ? info->rx_ctrl->rssi : 0);
}
#else
void onRecv(const uint8_t*, const uint8_t* data, int len) { handleRecv(data, len, 0); }
#endif

NodeState* findNode(uint8_t id) {
  NodeState* freeSlot = nullptr;
  for (auto& n : g_nodes) {
    if (n.used && n.nodeId == id) return &n;
    if (!n.used && !freeSlot) freeSlot = &n;
  }
  if (freeSlot) {
    freeSlot->used = true;
    freeSlot->nodeId = id;
  }
  return freeSlot;
}

void process(const RxItem& it) {
  const frost::TelemetryPacket& p = it.p;
  NodeState* n = findNode(p.nodeId);
  if (!n) return;
  if (n->received > 0 && n->bootId == p.bootId) {
    if (p.seq <= n->lastSeq) {  // duplicate or reordered old frame
      n->duplicates++;
      return;
    }
    n->lost += p.seq - n->lastSeq - 1;
  } else if (n->received > 0) {
    Serial.printf("# node %u rebooted (boot %u -> %u)\n", p.nodeId, n->bootId, p.bootId);
  }
  n->bootId = p.bootId;
  n->lastSeq = p.seq;
  n->received++;
  n->lastSeenMs = it.ms;
  if (!n->online) Serial.printf("# node %u ONLINE\n", p.nodeId);
  n->online = true;
  if (p.level != n->lastLevel) {
    Serial.printf("# node %u level %s -> %s\n", p.nodeId, frost::levelName(n->lastLevel), frost::levelName(p.level));
    n->lastLevel = p.level;
  }
  using frost::fromCenti;
  Serial.printf(
      "G,%lld,%lld,%u,%u,%lu,%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%u,%s,%u,%u,%u,%d,%lu,%lu\n",
      clockValid() ? (long long)time(nullptr) : 0LL, (long long)p.unixTs, p.nodeId, p.bootId,
      (unsigned long)p.seq, (unsigned long)p.uptimeS, fromCenti(p.t1), fromCenti(p.rh1), fromCenti(p.t2),
      fromCenti(p.rh2), fromCenti(p.t), fromCenti(p.rh), fromCenti(p.td), fromCenti(p.d), fromCenti(p.tSmooth),
      fromCenti(p.rt1h), fromCenti(p.rd1h), fromCenti(p.rt3h), p.quality, p.level, frost::levelName(p.level),
      p.flags, p.calibrated, p.sendFailStreak, it.rssi, (unsigned long)n->lost, (unsigned long)n->duplicates);
}

void checkTimeouts() {
  const uint32_t now = millis();
  for (auto& n : g_nodes) {
    if (n.used && n.online && now - n.lastSeenMs > NODE_TIMEOUT_S * 1000UL) {
      n.online = false;
      Serial.printf("# node %u OFFLINE (silent %lus) - its last level %s is stale\n", n.nodeId,
                    (unsigned long)((now - n.lastSeenMs) / 1000), frost::levelName(n.lastLevel));
    }
  }
}

void sendBeacon() {
  frost::TimeBeacon b;
  frost::fillBeacon(b, (int64_t)time(nullptr));
  esp_now_send(kBroadcast, reinterpret_cast<const uint8_t*>(&b), sizeof(b));
}

void handleCommand(String line) {
  line.trim();
  if (line.startsWith("TIME ")) {
    timeval tv = {(time_t)atoll(line.c_str() + 5), 0};
    settimeofday(&tv, nullptr);
    Serial.printf("# clock set to %lld\n", (long long)time(nullptr));
  } else if (line == "STATUS") {
    Serial.printf("# mac=%s bad_frames=%lu queue_overflow=%lu clock=%s\n", WiFi.macAddress().c_str(),
                  (unsigned long)g_badFrames, (unsigned long)g_queueOverflow, clockValid() ? "set" : "NOT SET");
    for (auto& n : g_nodes) {
      if (!n.used) continue;
      const uint32_t total = n.received + n.lost;
      Serial.printf("# node %u %s received=%lu lost=%lu (%.1f%%) dup=%lu level=%s\n", n.nodeId,
                    n.online ? "ONLINE" : "OFFLINE", (unsigned long)n.received, (unsigned long)n.lost,
                    total ? 100.0 * n.lost / total : 0.0, (unsigned long)n.duplicates, frost::levelName(n.lastLevel));
    }
  } else if (line.length()) {
    Serial.println("# commands: STATUS | TIME <unix_utc>");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  const bool ok = esp_now_init() == ESP_OK;
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcast, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&peer);
  Serial.printf("# frost_gateway espnow=%s ch=%d mac=%s  (put this MAC into the node's GATEWAY_MAC)\n",
                ok ? "ok" : "FAIL", ESPNOW_CHANNEL, WiFi.macAddress().c_str());
  Serial.println("# G,gw_unix_ts,node_unix_ts,node_id,boot_id,seq,uptime_s,t1,rh1,t2,rh2,t,rh,td,d,t_smooth,"
                 "rt_1h,rd_1h,rt_3h,quality,level,level_name,flags,calibrated,send_fail_streak,rssi,lost,duplicates");
}

void loop() {
  while (g_qTail != g_qHead) {
    process(g_queue[g_qTail]);
    g_qTail = (g_qTail + 1) % QUEUE_LEN;
  }
  while (Serial.available()) {
    const char ch = (char)Serial.read();
    if (ch == '\n') { handleCommand(g_line); g_line = ""; }
    else if (ch != '\r' && g_line.length() < 64) g_line += ch;
  }
  const uint32_t now = millis();
  if (now - g_lastCheckMs >= 5000) { g_lastCheckMs = now; checkTimeouts(); }
  if (clockValid() && now - g_lastBeaconMs >= BEACON_PERIOD_S * 1000UL) { g_lastBeaconMs = now; sendBeacon(); }
  delay(5);
}
