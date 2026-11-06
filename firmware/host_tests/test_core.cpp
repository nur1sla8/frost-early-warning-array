// Host unit tests for FrostCore (no hardware). Build and run: make -C firmware/host_tests test
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../libraries/FrostCore/src/FrostCore.h"

using namespace frost;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (cond) g_pass++;                                                \
    else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)
#define NEAR(a, b, tol) CHECK(fabs((a) - (b)) <= (tol))

// 2026-01-10 18:00 local (UTC+6) = 12:00 UTC
static const int64_t kEvening = 1768046400;

static Reading R(double t, double rh, bool ok = true) { Reading r; r.t = t; r.rh = rh; r.ok = ok; return r; }

static void testPsychro() {
  // Reference values from analysis/frostlib/psychro.py (same Magnus coefficients)
  NEAR(dewPoint(10, 60), 2.58, 0.01);
  NEAR(dewPoint(2, 90), 0.53, 0.01);
  NEAR(dewPoint(-2, 90), -3.42, 0.01);
  NEAR(frostPoint(-2, 90), -3.02, 0.01);
  NEAR(dewPoint(5, 100), 5.0, 1e-9);
  CHECK(isfinite(dewPoint(5, 0)));      // RH clamped, no -inf
  NEAR(dewPoint(5, 120), 5.0, 1e-9);    // RH > 100 clamped
  NEAR(esWater(0), 6.112, 1e-9);
}

static void testTrend() {
  TrendBuffer b;
  for (int i = 0; i <= 60; i++) b.push({kEvening + i * 60, 8.0 - 1.5 * i / 60.0, 3.0});
  NEAR(b.slopePerHour(kEvening + 3600, 3600, 0), -1.5, 1e-9);
  NEAR(b.slopePerHour(kEvening + 3600, 3600, 1), 0.0, 1e-9);
  CHECK(isnan(b.slopePerHour(kEvening + 3600, 3 * 3600, 0)));  // 1 h of data < 60 % of 3 h
  TrendBuffer c;
  c.push({kEvening, 1, 1});
  c.push({kEvening + 60, 1, 1});
  CHECK(isnan(c.slopePerHour(kEvening + 60, 3600, 0)));  // < 3 points
  TrendBuffer d;  // overflow keeps the newest kCapacity samples
  for (int i = 0; i < TrendBuffer::kCapacity + 10; i++) d.push({i, (double)i, 0});
  CHECK(d.size() == TrendBuffer::kCapacity);
  CHECK(d.at(0).ts == 10);
}

static void testFusionAndQc() {
  Engine e;
  Output o = e.update(kEvening, R(10, 60), R(10.4, 62));
  CHECK(o.quality == Q_GOOD);
  NEAR(o.t, 10.2, 1e-9);

  o = e.update(kEvening + 60, R(10, 60), R(12.0, 60));  // 2 degC apart, but 2 degC/min -> spike on s2
  CHECK(o.flags & F_SPIKE_REJECTED);
  CHECK(o.quality == Q_DEGRADED);
  NEAR(o.t, 10.0, 1e-9);

  Engine f;
  f.update(kEvening, R(10, 60), R(11.5, 60));
  o = f.update(kEvening + 60, R(10, 60), R(11.5, 60));
  CHECK(o.flags & F_SENSOR_DISAGREE);
  NEAR(o.t, 10.0, 1e-9);  // colder sensor taken

  Engine g;
  o = g.update(kEvening, R(NAN, NAN, false), R(0, 0, false));
  CHECK(o.quality == Q_BAD && o.level == UNKNOWN);
  CHECK((o.flags & F_SENSOR1_FAIL) && (o.flags & F_SENSOR2_FAIL));
  o = g.update(kEvening + 60, R(5, 101), R(-50, 50));  // both out of range
  CHECK(o.level == UNKNOWN);

  // persistent step: rejected spikeMaxRejects times, then accepted as real
  Engine h;
  h.update(kEvening, R(10, 60), R(10, 60));
  int rejected = 0;
  for (int i = 1; i <= 5; i++) {
    o = h.update(kEvening + i * 60, R(14, 60), R(14, 60));
    if (o.flags & F_SPIKE_REJECTED) rejected++;
  }
  CHECK(rejected == 3);
  NEAR(o.t, 14.0, 1e-9);

  // stuck sensor 1: identical output for stuckCount readings
  Engine s;
  for (int i = 0; i <= 61; i++) o = s.update(kEvening + i * 60, R(7.0, 55.0), R(7.0 + 0.01 * i, 55));
  CHECK(o.flags & F_STUCK);
  CHECK(o.quality == Q_DEGRADED);
}

static void testLevels() {
  Engine e;
  Output o;
  uint8_t seen[6] = {0};
  // smooth night cooling 6 -> -1 degC over 7 h starting 18:00 local
  for (int i = 0; i <= 420; i++) {
    const double t = 6.0 - i / 60.0;
    o = e.update(kEvening + i * 60, R(t, 80), R(t, 80));
    seen[o.level] = 1;
  }
  CHECK(seen[NORMAL] && seen[WATCH] && seen[WARNING] && seen[CRITICAL] && seen[FROST]);
  CHECK(o.level == FROST);
  NEAR(o.rt1h, -1.0, 1e-6);

  // warming back: FROST holds until T > 0 + hysteresis, then CRITICAL holds until T > 2.5
  Engine w;
  for (int i = 0; i < 20; i++) w.update(kEvening + 6 * 3600 + i * 60, R(-0.5, 90), R(-0.5, 90));
  o = w.update(kEvening + 6 * 3600 + 20 * 60, R(0.3, 90), R(0.3, 90));
  CHECK(o.level == FROST);  // smoothed mean still below 0.5
  Engine x;
  for (int i = 0; i < 20; i++) x.update(kEvening + 3600 + i * 60, R(1.8, 80), R(1.8, 80));
  CHECK(x.level() == CRITICAL);
  for (int i = 20; i < 40; i++) o = x.update(kEvening + 3600 + i * 60, R(2.3, 80), R(2.3, 80));
  CHECK(o.level == CRITICAL);  // 2.3 <= 2.0 + 0.5
  for (int i = 40; i < 60; i++) o = x.update(kEvening + 3600 + i * 60, R(2.7, 80), R(2.7, 80));
  CHECK(o.level == WARNING);

  // daytime (12:00 local): cold but above 0 -> NORMAL with OUTSIDE_WINDOW; below 0 -> FROST
  Engine d;
  o = d.update(kEvening - 6 * 3600, R(0.2, 70), R(0.2, 70));
  CHECK(o.level == NORMAL && (o.flags & F_OUTSIDE_WINDOW));
  o = d.update(kEvening - 6 * 3600 + 60, R(-0.6, 70), R(-0.6, 70));  // 10-min mean -0.2
  CHECK(o.level == FROST);

  // clock not set: evaluate anyway, flag it
  Engine u;
  o = u.update(1000, R(3.5, 70), R(3.5, 70), false);
  CHECK(o.level == WATCH && (o.flags & F_TIME_UNKNOWN));

  // high dew point flag is context only, does not change the level
  Engine hd;
  o = hd.update(kEvening, R(2.5, 95), R(2.5, 95));
  CHECK(o.level == WARNING && (o.flags & F_DEWPOINT_HIGH));
}

static void testPacket() {
  CHECK(sizeof(TelemetryPacket) == 54);
  Engine e;
  Reading a = R(3.21, 81.5), b = R(3.4, 80.0, false);
  Output o = e.update(kEvening, a, b);
  TelemetryPacket p;
  fillPacket(p, 7, 0xBEEF, 42, 3600, kEvening, a, b, o, false, 0);
  sealPacket(p);
  uint8_t raw[sizeof(p)];
  memcpy(raw, &p, sizeof(p));
  CHECK(validPacket(raw, sizeof(raw)));
  CHECK(p.t2 == kNoValue);
  NEAR(fromCenti(p.t), 3.21, 1e-9);
  CHECK(isnan(fromCenti(p.rt1h)));
  raw[20] ^= 0x01;
  CHECK(!validPacket(raw, sizeof(raw)));
  CHECK(!validPacket(raw, sizeof(raw) - 1));
  NEAR(crc16((const uint8_t*)"123456789", 9), 0x29B1, 0);  // CRC-16/CCITT-FALSE check value

  TimeBeacon tb;
  fillBeacon(tb, kEvening);
  uint8_t braw[sizeof(tb)];
  memcpy(braw, &tb, sizeof(tb));
  CHECK(validBeacon(braw, sizeof(braw)));
  CHECK(!validPacket(braw, sizeof(braw)));
  braw[5] ^= 0x80;
  CHECK(!validBeacon(braw, sizeof(braw)));
}

int main() {
  testPsychro();
  testTrend();
  testFusionAndQc();
  testLevels();
  testPacket();
  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
