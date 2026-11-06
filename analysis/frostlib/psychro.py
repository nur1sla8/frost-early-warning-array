"""Psychrometric calculations shared by the analysis and the firmware reference tests.

Magnus-type approximation (Sonntag 1990 coefficients, as recommended by WMO-No. 8):
    e_s,w(T) = 6.112 * exp(17.62 * T / (243.12 + T))   hPa, over liquid water, -45..60 degC
    e_s,i(T) = 6.112 * exp(22.46 * T / (272.62 + T))   hPa, over ice,          -65..0 degC

RH reported by DHT22 (and by weather stations) is relative to liquid water, also below 0 degC.
Dew point is therefore computed over water; frost point is computed from the same vapour
pressure but inverted over ice. Murphy & Koop (2005) formulas are included only as an
independent reference for accuracy checks.
"""
import math

import numpy as np

B_W, C_W = 17.62, 243.12   # over water
B_I, C_I = 22.46, 272.62   # over ice
E0 = 6.112                 # hPa

RH_MIN, RH_MAX = 1.0, 100.0  # clamp: ln(0) is undefined; RH > 100 % is sensor error


def es_water(t_c):
    return E0 * np.exp(B_W * t_c / (C_W + t_c))


def es_ice(t_c):
    return E0 * np.exp(B_I * t_c / (C_I + t_c))


def vapour_pressure(t_c, rh):
    return np.clip(rh, RH_MIN, RH_MAX) / 100.0 * es_water(t_c)


def _invert(e_hpa, b, c):
    g = np.log(e_hpa / E0)
    return c * g / (b - g)


def dew_point(t_c, rh):
    """Dew point over water, degC."""
    g = np.log(np.clip(rh, RH_MIN, RH_MAX) / 100.0) + B_W * t_c / (C_W + t_c)
    return C_W * g / (B_W - g)


def frost_point(t_c, rh):
    """Frost point (saturation over ice) for the same vapour pressure, degC. Meaningful when <= 0."""
    return _invert(vapour_pressure(t_c, rh), B_I, C_I)


def rh_from_dew_point(t_c, td_c):
    return 100.0 * es_water(td_c) / es_water(t_c)


def dew_point_uncertainty(t_c, rh, sigma_t=0.5, sigma_rh=2.0):
    """First-order propagation of independent T and RH errors to Td and to D = T - Td.

    Returns (sigma_td, sigma_d). Analytic derivatives of the Magnus inversion.
    """
    rh = np.clip(rh, RH_MIN, RH_MAX)
    td = dew_point(t_c, rh)
    # Td = c*g/(b-g), dTd/dg = b*c/(b-g)^2
    g = np.log(rh / 100.0) + B_W * t_c / (C_W + t_c)
    dtd_dg = B_W * C_W / (B_W - g) ** 2
    dtd_drh = dtd_dg / rh
    dtd_dt = dtd_dg * B_W * C_W / (C_W + t_c) ** 2
    sigma_td = np.hypot(dtd_dt * sigma_t, dtd_drh * sigma_rh)
    sigma_d = np.hypot((1.0 - dtd_dt) * sigma_t, dtd_drh * sigma_rh)
    return sigma_td, sigma_d


# --- Independent reference (Murphy & Koop 2005, Q. J. R. Meteorol. Soc. 131, 1539) ---

def es_water_mk(t_c):
    t = np.asarray(t_c, dtype=float) + 273.15
    ln_p = (54.842763 - 6763.22 / t - 4.210 * np.log(t) + 0.000367 * t
            + np.tanh(0.0415 * (t - 218.8))
            * (53.878 - 1331.22 / t - 9.44523 * np.log(t) + 0.014025 * t))
    return np.exp(ln_p) / 100.0


def es_ice_mk(t_c):
    t = np.asarray(t_c, dtype=float) + 273.15
    ln_p = 9.550426 - 5723.265 / t + 3.53068 * np.log(t) - 0.00728332 * t
    return np.exp(ln_p) / 100.0


def dew_point_mk(t_c, rh):
    """Reference dew point by numerically inverting Murphy-Koop (bisection)."""
    e = np.clip(rh, RH_MIN, RH_MAX) / 100.0 * es_water_mk(t_c)
    lo, hi = -80.0, float(t_c) + 1.0
    for _ in range(60):
        mid = 0.5 * (lo + hi)
        if es_water_mk(mid) < e:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


if __name__ == "__main__":
    for t in (-10, 0, 10, 20):
        print(t, round(float(es_water(t)), 3), round(float(es_water_mk(t)), 3),
              round(float(es_ice(min(t, 0))), 3), round(float(es_ice_mk(min(t, 0))), 3))
    print(math.isfinite(float(dew_point(5.0, 0.0))))
