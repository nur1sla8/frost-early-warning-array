#include "psychro.h"

#include <math.h>

namespace frost {

namespace {
constexpr double kBw = 17.62, kCw = 243.12;  // over water
constexpr double kBi = 22.46, kCi = 272.62;  // over ice
constexpr double kE0 = 6.112;                // hPa
}  // namespace

double clampRh(double rh) {
  if (rh < kRhMin) return kRhMin;
  if (rh > kRhMax) return kRhMax;
  return rh;
}

double esWater(double tC) { return kE0 * exp(kBw * tC / (kCw + tC)); }

double esIce(double tC) { return kE0 * exp(kBi * tC / (kCi + tC)); }

double dewPoint(double tC, double rh) {
  const double g = log(clampRh(rh) / 100.0) + kBw * tC / (kCw + tC);
  return kCw * g / (kBw - g);
}

double frostPoint(double tC, double rh) {
  const double e = clampRh(rh) / 100.0 * esWater(tC);
  const double g = log(e / kE0);
  return kCi * g / (kBi - g);
}

}  // namespace frost
