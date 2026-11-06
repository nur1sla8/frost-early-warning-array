// Warning engine: per-sensor QC, dual-sensor fusion, psychrometrics, trends and warning level.
// Reference implementation: analysis/frostlib/engine.py. Both must give identical levels and
// flags on the same input (checked by firmware/host_tests and tools/compare_replay.py).
//
// Initial-mode thresholds come from the Bishkek SYNOP study (results/levels.csv). On that archive
// cooling rate and dew-point convergence did not add skill beyond T and time of night, so rates
// and dew point only set context flags here and never change the level.
#pragma once

#include <math.h>
#include <stdint.h>

#include "trend.h"

namespace frost {

enum Level : uint8_t { UNKNOWN = 0, NORMAL, WATCH, WARNING, CRITICAL, FROST };
enum Quality : uint8_t { Q_GOOD = 0, Q_DEGRADED, Q_BAD };

enum Flag : uint16_t {
  F_SENSOR1_FAIL = 1u << 0,
  F_SENSOR2_FAIL = 1u << 1,
  F_SENSOR_DISAGREE = 1u << 2,
  F_SPIKE_REJECTED = 1u << 3,
  F_STUCK = 1u << 4,
  F_RH_SATURATED = 1u << 5,
  F_TIME_UNKNOWN = 1u << 6,
  F_OUTSIDE_WINDOW = 1u << 7,
  F_COOLING_FAST = 1u << 8,      // RT_1h <= -1 degC/h, context only
  F_CONVERGING_FAST = 1u << 9,   // RD_1h <= -1 degC/h, context only
  F_DEWPOINT_HIGH = 1u << 10,    // Td >= +1 degC: latent-heat buffer, context only
  F_DEWPOINT_BELOW_ZERO = 1u << 11,
  F_TREND_INSUFFICIENT = 1u << 12,
};

const char* levelName(uint8_t level);

struct Config {
  float tWatch = 4.0f;
  float tWarning = 3.0f;
  float tCritical = 2.0f;
  float tFrost = 0.0f;
  float hysteresis = 0.5f;     // DHT22 accuracy; prevents flapping
  int32_t smoothS = 600;       // level uses the mean T of the last 10 min
  int8_t windowStartH = 18;    // evaluation window, local time
  int8_t windowEndH = 9;
  int32_t tzOffsetS = 6 * 3600;
  float tAgree = 1.0f;         // 2 x DHT22 +-0.5 degC
  float rhAgree = 5.0f;
  float spikeTPerMin = 1.0f;
  float spikeRhPerMin = 10.0f;
  float spikeMinT = 1.0f;
  float spikeMinRh = 8.0f;
  uint8_t spikeMaxRejects = 3; // after N rejects accept the new value as a real step
  uint16_t stuckCount = 60;    // identical T and RH for 60 readings
  float rhSaturated = 99.5f;
  float offsetT1 = 0, offsetRh1 = 0, offsetT2 = 0, offsetRh2 = 0;  // co-location calibration
  bool calibrated = false;     // false = initial mode (regional thresholds)
};

struct Reading {
  double t = 0, rh = 0;
  bool ok = false;
};

struct Output {
  int64_t ts = 0;
  double t = NAN, rh = NAN, td = NAN, tf = NAN, d = NAN;  // fused T, RH, dew/frost point, T - Td
  double tSmooth = NAN;                                   // value the level is decided on
  double rt1h = NAN, rd1h = NAN, rt3h = NAN;              // degC/h
  uint8_t quality = Q_BAD;
  uint8_t level = UNKNOWN;
  uint16_t flags = 0;
};

class Engine {
 public:
  explicit Engine(const Config& cfg = Config()) : cfg_(cfg) {}
  void setConfig(const Config& cfg) { cfg_ = cfg; }
  const Config& config() const { return cfg_; }
  void reset();

  // ts: Unix time, s (UTC). timeValid = false if the clock is not set (no NTP / RTC yet).
  Output update(int64_t ts, Reading s1, Reading s2, bool timeValid = true);
  uint8_t level() const { return level_; }

 private:
  struct SensorState {
    double lastT = 0, lastRh = 0;
    int64_t lastTs = -1;
    uint8_t rejects = 0;
    uint16_t same = 0;
  };

  bool check(int i, int64_t ts, double t, double rh, bool ok, uint16_t& flags);
  uint8_t decideLevel(double t, bool inWindow) const;

  Config cfg_;
  SensorState s_[2];
  TrendBuffer buf_;
  uint8_t level_ = NORMAL;
};

}  // namespace frost
