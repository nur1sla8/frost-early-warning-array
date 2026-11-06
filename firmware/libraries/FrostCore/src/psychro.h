// Psychrometrics: Magnus approximation, Sonntag (1990) coefficients as in WMO-No. 8.
// RH from DHT22 is relative to liquid water, also below 0 degC, so the dew point is computed
// over water; the frost point is the same vapour pressure inverted over ice.
// Accuracy vs Murphy & Koop (2005): |dTd| <= 0.053 degC for -20..30 degC, 20..100 % RH
// (analysis/frostlib/psychro.py). This is far below the DHT22 error.
#pragma once

namespace frost {

constexpr double kRhMin = 1.0;    // ln(0) is undefined
constexpr double kRhMax = 100.0;  // RH > 100 % is a sensor error

double clampRh(double rh);
double esWater(double tC);            // saturation vapour pressure over water, hPa
double esIce(double tC);              // over ice, hPa
double dewPoint(double tC, double rh);    // degC
double frostPoint(double tC, double rh);  // degC, meaningful when T <= 0

}  // namespace frost
