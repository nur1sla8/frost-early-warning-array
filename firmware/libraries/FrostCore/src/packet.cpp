#include "packet.h"

#include <math.h>
#include <string.h>

namespace frost {

uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

int16_t toCenti(double v) {
  if (isnan(v)) return kNoValue;
  const double c = round(v * 100.0);
  if (c <= INT16_MIN || c > INT16_MAX) return kNoValue;
  return (int16_t)c;
}

double fromCenti(int16_t v) { return v == kNoValue ? NAN : v / 100.0; }

void fillPacket(TelemetryPacket& p, uint8_t nodeId, uint16_t bootId, uint32_t seq, uint32_t uptimeS,
                int64_t unixTs, const Reading& s1, const Reading& s2, const Output& o, bool calibrated,
                uint8_t sendFailStreak) {
  memset(&p, 0, sizeof(p));
  p.magic = kPacketMagic;
  p.version = kPacketVersion;
  p.nodeId = nodeId;
  p.bootId = bootId;
  p.seq = seq;
  p.uptimeS = uptimeS;
  p.unixTs = unixTs;
  p.t1 = s1.ok ? toCenti(s1.t) : kNoValue;
  p.rh1 = s1.ok ? toCenti(s1.rh) : kNoValue;
  p.t2 = s2.ok ? toCenti(s2.t) : kNoValue;
  p.rh2 = s2.ok ? toCenti(s2.rh) : kNoValue;
  p.t = toCenti(o.t);
  p.rh = toCenti(o.rh);
  p.td = toCenti(o.td);
  p.d = toCenti(o.d);
  p.tSmooth = toCenti(o.tSmooth);
  p.rt1h = toCenti(o.rt1h);
  p.rd1h = toCenti(o.rd1h);
  p.rt3h = toCenti(o.rt3h);
  p.quality = o.quality;
  p.level = o.level;
  p.flags = o.flags;
  p.calibrated = calibrated ? 1 : 0;
  p.sendFailStreak = sendFailStreak;
}

void sealPacket(TelemetryPacket& p) {
  p.crc = crc16(reinterpret_cast<const uint8_t*>(&p), offsetof(TelemetryPacket, crc));
}

bool validPacket(const uint8_t* data, size_t len) {
  if (len != sizeof(TelemetryPacket)) return false;
  TelemetryPacket p;
  memcpy(&p, data, sizeof(p));
  if (p.magic != kPacketMagic || p.version != kPacketVersion) return false;
  return p.crc == crc16(data, offsetof(TelemetryPacket, crc));
}

void fillBeacon(TimeBeacon& b, int64_t unixTs) {
  memset(&b, 0, sizeof(b));
  b.magic = kBeaconMagic;
  b.version = kPacketVersion;
  b.unixTs = unixTs;
  b.crc = crc16(reinterpret_cast<const uint8_t*>(&b), offsetof(TimeBeacon, crc));
}

bool validBeacon(const uint8_t* data, size_t len) {
  if (len != sizeof(TimeBeacon)) return false;
  TimeBeacon b;
  memcpy(&b, data, sizeof(b));
  if (b.magic != kBeaconMagic || b.version != kPacketVersion) return false;
  return b.crc == crc16(data, offsetof(TimeBeacon, crc));
}

}  // namespace frost
