// Telemetry packet sent by a sensor node over ESP-NOW (one frame, well under the 250-byte limit).
// Values are fixed-point integers so the layout does not depend on float ABI.
// Integrity: magic + version + CRC-16/CCITT-FALSE over all bytes before `crc`.
// Duplicates: the gateway drops a packet whose (nodeId, bootId, seq) it has already seen.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "engine.h"

namespace frost {

constexpr uint16_t kPacketMagic = 0xF7A5;
constexpr uint8_t kPacketVersion = 1;
constexpr int16_t kNoValue = INT16_MIN;  // NaN marker for centi-degrees

#pragma pack(push, 1)
struct TelemetryPacket {
  uint16_t magic;
  uint8_t version;
  uint8_t nodeId;
  uint16_t bootId;      // random per boot: a reboot restarts seq without looking like a duplicate
  uint32_t seq;         // per-boot counter, gaps = lost packets
  uint32_t uptimeS;
  int64_t unixTs;       // 0 when the node clock is not set
  int16_t t1, rh1, t2, rh2;           // raw sensors, 0.01 degC / 0.01 %
  int16_t t, rh, td, d, tSmooth;      // fused values
  int16_t rt1h, rd1h, rt3h;           // 0.01 degC/h
  uint8_t quality;
  uint8_t level;
  uint16_t flags;
  uint8_t calibrated;
  uint8_t sendFailStreak;  // consecutive failed sends before this one
  uint16_t crc;
};
#pragma pack(pop)

static_assert(sizeof(TelemetryPacket) <= 250, "ESP-NOW v1 payload limit");

// Broadcast by the gateway once its clock is set, so nodes without RTC/Wi-Fi get time.
constexpr uint16_t kBeaconMagic = 0xF7A6;
#pragma pack(push, 1)
struct TimeBeacon {
  uint16_t magic;
  uint8_t version;
  uint8_t reserved;
  int64_t unixTs;
  uint16_t crc;
};
#pragma pack(pop)

void fillBeacon(TimeBeacon& b, int64_t unixTs);
bool validBeacon(const uint8_t* data, size_t len);

uint16_t crc16(const uint8_t* data, size_t len);
int16_t toCenti(double v);   // NaN or out of range -> kNoValue
double fromCenti(int16_t v); // kNoValue -> NaN

void fillPacket(TelemetryPacket& p, uint8_t nodeId, uint16_t bootId, uint32_t seq, uint32_t uptimeS,
                int64_t unixTs, const Reading& s1, const Reading& s2, const Output& o, bool calibrated,
                uint8_t sendFailStreak);
void sealPacket(TelemetryPacket& p);            // computes crc
bool validPacket(const uint8_t* data, size_t len);  // size, magic, version, crc

}  // namespace frost
