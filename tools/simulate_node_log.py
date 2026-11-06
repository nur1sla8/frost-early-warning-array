"""Turn a replay CSV into a node log (same columns as the device's D-lines / LittleFS log) using the
Python reference engine. For trying the dashboard and the analysis pipeline without hardware.
The result is SIMULATED data and must never be reported as a measurement.

Usage: python3 -I tools/simulate_node_log.py <replay.csv> <out_log.csv> [--nights K]
"""
import argparse
import math
import sys
from pathlib import Path

import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "analysis"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from formats import NODE_HEADER  # noqa: E402
from frostlib import engine  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("replay")
    ap.add_argument("out")
    ap.add_argument("--nights", type=int, default=3, help="keep the first K nights")
    a = ap.parse_args()
    r = pd.read_csv(a.replay)
    starts = r.index[r["ts"].diff().fillna(1e9) > 3600]
    end = starts[a.nights] if a.nights < len(starts) else len(r)
    r = r.iloc[:end]
    e = engine.Engine()
    rows = []
    prev_ts = None
    for i, x in enumerate(r.itertuples(index=False)):
        if prev_ts is not None and x.ts - prev_ts > 3600:
            e = engine.Engine()  # new night: the device would have been restarted
        prev_ts = x.ts
        o = e.update(int(x.ts), x.t1, x.rh1, bool(x.ok1), x.t2, x.rh2, bool(x.ok2), True)
        f = lambda v, n=2: "nan" if v is None or (isinstance(v, float) and math.isnan(v)) else f"{v:.{n}f}"
        rows.append(",".join([
            str(o.ts), "1", "4242", str(i), str(i * 60), f(x.t1, 1), f(x.rh1, 1), str(int(x.ok1)), f(x.t2, 1),
            f(x.rh2, 1), str(int(x.ok2)), f(o.t), f(o.rh), f(o.td), f(o.tf), f(o.d), f(o.t_smooth),
            f(o.rt_1h, 3), f(o.rd_1h, 3), f(o.rt_3h, 3), str(o.quality), str(o.level),
            engine.LEVEL_NAMES[o.level], str(o.flags), "0", "0", "1"]))
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text(NODE_HEADER + "\n" + "\n".join(rows) + "\n")
    print(f"{len(rows)} rows -> {a.out}")


if __name__ == "__main__":
    main()
