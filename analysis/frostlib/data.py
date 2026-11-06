"""Loading and cleaning the Meteostat hourly archive, and building one row per night.

Source: https://bulk.meteostat.net/v2/hourly/<station>.csv.gz (station 38353, Bishkek).
Findings that drive the cleaning:
  * Before 2020 the archive holds 3-hourly SYNOP reports (wind in whole m/s = multiples of 3.6 km/h).
  * From 2020 hourly rows from a second source (wind in knots = multiples of 1.852 km/h) are mixed in,
    including ~27 % of rows at synoptic hours. Mixing two sites creates fake temperature "rates".
  => keep only synoptic hours (00, 03, ... 21 UTC) whose wind is in m/s (or calm).
"""
import numpy as np
import pandas as pd

from . import psychro

COLS = "date hour temp dwpt rhum prcp snow wdir wspd wpgt pres tsun coco".split()
UTC_OFFSET_H = 6  # Bishkek, UTC+6

# Local observation times used for one night (evening date D): 15, 18, 21 on D, 00, 03, 06, 09 on D+1
NIGHT_HOURS = [15, 18, 21, 24, 27, 30, 33]  # hours after local midnight of D
DECISION_HOURS = [18, 21, 24, 27]           # when a warning may be issued
ONSET_HOURS = [21, 24, 27, 30, 33]          # where an event may be observed


def _is_multiple(x, unit, tol=0.06):
    r = (x / unit) % 1.0
    return np.minimum(r, 1.0 - r) * unit < tol


def load_synop(path, year_from=2005):
    df = pd.read_csv(path, names=COLS, header=None)
    df["utc"] = pd.to_datetime(df["date"]) + pd.to_timedelta(df["hour"], unit="h")
    df = df[(df["utc"].dt.year >= year_from) & (df["hour"] % 3 == 0)]
    synop_wind = df["wspd"].isna() | (df["wspd"] == 0) | _is_multiple(df["wspd"], 3.6)
    df = df[synop_wind].dropna(subset=["temp", "dwpt"])
    df = df[df["dwpt"] <= df["temp"] + 0.5]  # physically impossible supersaturation -> bad row
    out = pd.DataFrame({
        "local": df["utc"] + pd.Timedelta(hours=UTC_OFFSET_H),
        "T": df["temp"].astype(float),
        "Td": df["dwpt"].astype(float),
        "wind_ms": df["wspd"] / 3.6,
    }).set_index("local").sort_index()
    out = out[~out.index.duplicated()]
    out["D"] = out["T"] - out["Td"]
    out["RH"] = psychro.rh_from_dew_point(out["T"], out["Td"]).clip(upper=100)
    return out


def build_nights(obs):
    """Wide table: one row per evening date with T/Td/D/wind at each NIGHT_HOURS slot.

    Nights with any missing slot are dropped (rates need consecutive observations).
    """
    evening = obs.index.normalize()
    hours = obs.index.hour
    rows = []
    for offset in (0, 1):  # slot belongs to the evening of (date - offset)
        sel = obs.copy()
        sel["night"] = evening - pd.Timedelta(days=offset)
        sel["slot"] = hours + 24 * offset
        rows.append(sel)
    long = pd.concat(rows)
    long = long[long["slot"].isin(NIGHT_HOURS)]
    wide = long.pivot_table(index="night", columns="slot", values=["T", "Td", "D", "wind_ms"])
    wide = wide.dropna(subset=[(v, h) for v in ("T", "Td") for h in NIGHT_HOURS])
    return wide


def label_nights(wide, frost_c=0.0):
    """Event = first observation in ONSET_HOURS with T <= frost_c, provided T(18) > frost_c."""
    t = wide["T"]
    cand = t[18] > frost_c
    onset = pd.Series(np.nan, index=wide.index)
    for h in ONSET_HOURS:
        hit = onset.isna() & (t[h] <= frost_c)
        onset[hit] = h
    lab = pd.DataFrame({"candidate": cand, "event": cand & onset.notna(), "onset_h": onset})
    lab.loc[~lab["candidate"], "onset_h"] = np.nan
    return lab
