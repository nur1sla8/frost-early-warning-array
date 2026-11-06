"""Build a replay CSV for the node (REPLAY mode) and for the host C++ engine from real archive nights.

Each selected night (18:00-09:00 local) is linearly interpolated from the 3-hourly SYNOP data to
`--step` seconds. The interpolation is artificial smoothness: replay checks that the firmware
behaves like the reference, it is NOT new evidence about frost prediction.
Two "DHT22" channels are simulated: rounding to 0.1, a fixed inter-sensor offset, small noise, and
scripted faults (spike, dropout, disagreement, stuck sensor, both sensors lost).

Usage: python3 -I tools/make_replay.py <meteostat_csv_gz> <out_csv> [--nights N] [--step S] [--seed K]
Output columns: ts,t1,rh1,ok1,t2,rh2,ok2,time_valid   (ts = Unix UTC seconds)
"""
import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "analysis"))
from frostlib import data, psychro  # noqa: E402


def night_series(wide, night, step):
    hours = [18, 21, 24, 27, 30, 33]
    t = np.array([wide.loc[night, ("T", h)] for h in hours])
    td = np.array([wide.loc[night, ("Td", h)] for h in hours])
    x = np.array(hours, dtype=float) * 3600
    grid = np.arange(x[0], x[-1] + 1, step)
    T = np.interp(grid, x, t)
    Td = np.interp(grid, x, td)
    RH = np.clip(psychro.rh_from_dew_point(T, Td), 0, 100)
    local_midnight = pd.Timestamp(night).tz_localize("UTC")  # naive local date -> epoch arithmetic
    ts = (local_midnight.value // 10**9) - data.UTC_OFFSET_H * 3600 + grid.astype(np.int64)
    return ts, T, RH


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--nights", type=int, default=40)
    ap.add_argument("--step", type=int, default=60)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    rng = np.random.default_rng(a.seed)

    obs = data.load_synop(a.src)
    wide = data.build_nights(obs)
    lab = data.label_nights(wide)
    m = wide.index.month
    pool = lab[(wide.index.year >= 2020) & np.isin(m, [10, 11, 12, 1, 2, 3, 4]) & lab["candidate"]]
    ev = pool[pool["event"]].sample(a.nights // 2, random_state=a.seed)
    ne = pool[~pool["event"]].sample(a.nights - len(ev), random_state=a.seed)
    nights = sorted(list(ev.index) + list(ne.index))

    rows = []
    for k, night in enumerate(nights):
        ts, T, RH = night_series(wide, night, a.step)
        n = len(ts)
        t1 = np.round(T + rng.normal(0, 0.05, n), 1)
        rh1 = np.round(np.clip(RH + rng.normal(0, 0.3, n), 0, 100), 1)
        t2 = np.round(T + 0.3 + rng.normal(0, 0.05, n), 1)          # +0.3 degC inter-sensor bias
        rh2 = np.round(np.clip(RH - 1.5 + rng.normal(0, 0.3, n), 0, 100), 1)
        ok1 = np.ones(n, int); ok2 = np.ones(n, int)
        fault = k % 6
        i0 = n // 3
        if fault == 1:                       # single-sample spike on sensor 1
            t1[i0] += 8.0
        elif fault == 2:                     # sensor 2 dropout for 45 min
            ok2[i0:i0 + 45 * 60 // a.step] = 0
        elif fault == 3:                     # sensor 2 drifts away (disagreement)
            t2[i0:] += np.linspace(0, 3.0, n - i0)
        elif fault == 4:                     # sensor 1 stuck for 90 min
            t1[i0:i0 + 90 * 60 // a.step] = t1[i0]; rh1[i0:i0 + 90 * 60 // a.step] = rh1[i0]
        elif fault == 5:                     # both lost for 20 min
            ok1[i0:i0 + 20 * 60 // a.step] = 0; ok2[i0:i0 + 20 * 60 // a.step] = 0
        for j in range(n):
            rows.append((int(ts[j]), t1[j], rh1[j], ok1[j], t2[j], rh2[j], ok2[j], 1))
    df = pd.DataFrame(rows, columns="ts t1 rh1 ok1 t2 rh2 ok2 time_valid".split())
    df.to_csv(a.out, index=False)
    meta = pd.DataFrame({"night": nights, "event": lab.loc[nights, "event"].values,
                         "onset_h": lab.loc[nights, "onset_h"].values,
                         "fault": [["none", "spike", "dropout", "disagree", "stuck", "both_lost"][k % 6]
                                   for k in range(len(nights))]})
    meta.to_csv(Path(a.out).with_suffix(".nights.csv"), index=False)
    print(f"{len(nights)} nights, {len(df)} rows -> {a.out}")


if __name__ == "__main__":
    main()
