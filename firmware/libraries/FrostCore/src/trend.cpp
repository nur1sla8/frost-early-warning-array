#include "trend.h"

#include <math.h>

namespace frost {

void TrendBuffer::push(const TrendSample& s) {
  if (size_ == kCapacity) {  // full: overwrite the oldest
    buf_[head_] = s;
    head_ = (head_ + 1) % kCapacity;
    return;
  }
  buf_[(head_ + size_) % kCapacity] = s;
  size_++;
}

void TrendBuffer::dropOlderThan(int64_t ts) {
  while (size_ > 0 && buf_[head_].ts < ts) {
    head_ = (head_ + 1) % kCapacity;
    size_--;
  }
}

const TrendSample& TrendBuffer::at(int i) const { return buf_[(head_ + i) % kCapacity]; }

double TrendBuffer::slopePerHour(int64_t ts, int64_t windowS, int field) const {
  int n = 0;
  int64_t first = 0, last = 0;
  double sx = 0, sy = 0;
  for (int i = 0; i < size_; i++) {
    const TrendSample& p = at(i);
    if (p.ts < ts - windowS) continue;
    if (n == 0) first = p.ts;
    last = p.ts;
    sx += (double)(p.ts - ts);
    sy += field == 0 ? p.t : p.d;
    n++;
  }
  if (n < 3) return NAN;
  if ((double)(last - first) < 0.6 * (double)windowS) return NAN;
  const double mx = sx / n, my = sy / n;
  double sxx = 0, sxy = 0;
  for (int i = 0; i < size_; i++) {
    const TrendSample& p = at(i);
    if (p.ts < ts - windowS) continue;
    const double dx = (double)(p.ts - ts) - mx;
    sxx += dx * dx;
    sxy += dx * ((field == 0 ? p.t : p.d) - my);
  }
  return sxx > 0 ? sxy / sxx * 3600.0 : NAN;
}

double TrendBuffer::meanT(int64_t ts, int64_t windowS) const {
  int n = 0;
  double s = 0;
  for (int i = 0; i < size_; i++) {
    const TrendSample& p = at(i);
    if (p.ts > ts - windowS) {
      s += p.t;
      n++;
    }
  }
  return n ? s / n : NAN;
}

}  // namespace frost
