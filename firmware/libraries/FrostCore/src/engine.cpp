#include "engine.h"

#include <math.h>

#include "psychro.h"

namespace frost {

const char* levelName(uint8_t level) {
  static const char* const kNames[] = {"UNKNOWN", "NORMAL", "WATCH", "WARNING", "CRITICAL", "FROST"};
  return level <= FROST ? kNames[level] : "?";
}

void Engine::reset() {
  s_[0] = SensorState();
  s_[1] = SensorState();
  buf_.clear();
  level_ = NORMAL;
}

bool Engine::check(int i, int64_t ts, double t, double rh, bool ok, uint16_t& flags) {
  SensorState& st = s_[i];
  const uint16_t failBit = i == 0 ? F_SENSOR1_FAIL : F_SENSOR2_FAIL;
  if (!ok || isnan(t) || isnan(rh) || t < -40 || t > 80 || rh < 0 || rh > 100) {
    flags |= failBit;
    return false;
  }
  if (st.lastTs >= 0) {
    double dtMin = (double)(ts - st.lastTs) / 60.0;
    if (dtMin < 1e-6) dtMin = 1e-6;
    const double dT = fabs(t - st.lastT), dRh = fabs(rh - st.lastRh);
    const bool spike = (dT > cfg_.spikeMinT && dT / dtMin > cfg_.spikeTPerMin) ||
                       (dRh > cfg_.spikeMinRh && dRh / dtMin > cfg_.spikeRhPerMin);
    if (spike && st.rejects < cfg_.spikeMaxRejects) {
      st.rejects++;
      flags |= F_SPIKE_REJECTED;
      return false;
    }
    st.same = (t == st.lastT && rh == st.lastRh) ? st.same + 1 : 0;
  }
  st.rejects = 0;
  st.lastT = t;
  st.lastRh = rh;
  st.lastTs = ts;
  if (st.same >= cfg_.stuckCount) {
    flags |= F_STUCK | failBit;
    return false;
  }
  return true;
}

Output Engine::update(int64_t ts, Reading s1, Reading s2, bool timeValid) {
  Output out;
  out.ts = ts;
  s1.t += cfg_.offsetT1;
  s1.rh += cfg_.offsetRh1;
  s2.t += cfg_.offsetT2;
  s2.rh += cfg_.offsetRh2;
  const bool v1 = check(0, ts, s1.t, s1.rh, s1.ok, out.flags);
  const bool v2 = check(1, ts, s2.t, s2.rh, s2.ok, out.flags);

  if (v1 && v2) {
    if (fabs(s1.t - s2.t) <= cfg_.tAgree && fabs(s1.rh - s2.rh) <= cfg_.rhAgree) {
      out.t = (s1.t + s2.t) / 2;
      out.rh = (s1.rh + s2.rh) / 2;
      out.quality = Q_GOOD;
    } else {
      // Cannot tell which sensor is right: take the colder pair (earlier warning).
      const Reading& c = s1.t <= s2.t ? s1 : s2;
      out.t = c.t;
      out.rh = c.rh;
      out.quality = Q_DEGRADED;
      out.flags |= F_SENSOR_DISAGREE;
    }
  } else if (v1 || v2) {
    const Reading& c = v1 ? s1 : s2;
    out.t = c.t;
    out.rh = c.rh;
    out.quality = Q_DEGRADED;
  } else {
    out.quality = Q_BAD;
    out.level = level_ = UNKNOWN;
    return out;
  }

  out.rh = clampRh(out.rh);
  out.td = dewPoint(out.t, out.rh);
  out.d = out.t - out.td;
  if (out.t <= 0) out.tf = frostPoint(out.t, out.rh);
  if (out.rh >= cfg_.rhSaturated) out.flags |= F_RH_SATURATED;

  buf_.push({ts, out.t, out.d});
  buf_.dropOlderThan(ts - 3 * 3600);
  // Rounded to 0.01 degC so that floating-point noise never decides a threshold tie.
  out.tSmooth = floor(buf_.meanT(ts, cfg_.smoothS) * 100.0 + 0.5) / 100.0;
  out.rt1h = buf_.slopePerHour(ts, 3600, 0);
  out.rd1h = buf_.slopePerHour(ts, 3600, 1);
  out.rt3h = buf_.slopePerHour(ts, 3 * 3600 - 60, 0);
  if (isnan(out.rt1h)) out.flags |= F_TREND_INSUFFICIENT;
  if (out.rt1h <= -1.0) out.flags |= F_COOLING_FAST;
  if (out.rd1h <= -1.0) out.flags |= F_CONVERGING_FAST;
  if (out.td >= 1.0) out.flags |= F_DEWPOINT_HIGH;
  if (out.td < 0.0) out.flags |= F_DEWPOINT_BELOW_ZERO;

  bool inWindow = true;
  if (timeValid) {
    const int64_t hour = ((ts + cfg_.tzOffsetS) % 86400) / 3600;
    inWindow = hour >= cfg_.windowStartH || hour < cfg_.windowEndH;
  } else {
    out.flags |= F_TIME_UNKNOWN;
  }
  if (!inWindow) out.flags |= F_OUTSIDE_WINDOW;

  out.level = level_ = decideLevel(out.tSmooth, inWindow);
  return out;
}

uint8_t Engine::decideLevel(double t, bool inWindow) const {
  if (t <= cfg_.tFrost || (level_ == FROST && t <= cfg_.tFrost + cfg_.hysteresis)) return FROST;
  if (!inWindow) return NORMAL;
  uint8_t target = NORMAL;
  if (t <= cfg_.tCritical) target = CRITICAL;
  else if (t <= cfg_.tWarning) target = WARNING;
  else if (t <= cfg_.tWatch) target = WATCH;

  const uint8_t prev = (level_ == WATCH || level_ == WARNING || level_ == CRITICAL) ? level_ : (uint8_t)NORMAL;
  if (target >= prev) return target;
  // Going down: hold the previous level until T clears its threshold by the hysteresis.
  const double thrPrev = prev == CRITICAL ? cfg_.tCritical : prev == WARNING ? cfg_.tWarning : cfg_.tWatch;
  if (t <= thrPrev + cfg_.hysteresis) return prev;
  return target;
}

}  // namespace frost
