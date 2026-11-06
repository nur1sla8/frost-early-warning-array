// Time-stamped ring buffer of fused samples and least-squares trends.
// A trend is reported only when the window is at least 60 % covered and holds >= 3 points;
// otherwise NaN, so gaps after a reboot or sensor dropout never produce a fake rate.
#pragma once

#include <stdint.h>

namespace frost {

struct TrendSample {
  int64_t ts;  // s
  double t;    // degC
  double d;    // T - Td, degC
};

class TrendBuffer {
 public:
  static constexpr int kCapacity = 400;  // 3 h at >= 27 s per sample

  void clear() { head_ = 0; size_ = 0; }
  void push(const TrendSample& s);
  void dropOlderThan(int64_t ts);
  int size() const { return size_; }
  const TrendSample& at(int i) const;  // 0 = oldest

  // Slope in unit/hour over [ts - windowS, ts]; field 0 = T, 1 = D.
  double slopePerHour(int64_t ts, int64_t windowS, int field) const;
  // Mean T of samples with timestamp > ts - windowS.
  double meanT(int64_t ts, int64_t windowS) const;

 private:
  TrendSample buf_[kCapacity];
  int head_ = 0;  // index of the oldest sample
  int size_ = 0;
};

}  // namespace frost
