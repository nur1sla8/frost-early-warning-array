"""Check that the firmware engine (C++, FrostCore) and the Python reference agree on a replay.

Usage: python3 -I tools/compare_replay.py <replay.csv> <cpp_output.csv> [node_log.csv]
  cpp_output.csv : output of firmware/host_tests/replay_cli, or the node's serial CSV in REPLAY mode
Exit code 0 only if every level and flag matches and numeric fields agree within tolerance.
"""
import sys
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "analysis"))
from frostlib import engine  # noqa: E402

TOL = {"t": 1e-6, "rh": 1e-6, "td": 1e-5, "d": 1e-5, "t_smooth": 1e-6, "rt_1h": 1e-4, "rd_1h": 1e-4,
       "rt_3h": 1e-4}


def run_python(replay):
    e = engine.Engine()
    out = []
    for r in replay.itertuples(index=False):
        o = e.update(int(r.ts), r.t1, r.rh1, bool(r.ok1), r.t2, r.rh2, bool(r.ok2), bool(r.time_valid))
        out.append((o.ts, o.t, o.rh, o.td, o.d, o.t_smooth, o.rt_1h, o.rd_1h, o.rt_3h, o.quality, o.level, o.flags))
    return pd.DataFrame(out, columns="ts t rh td d t_smooth rt_1h rd_1h rt_3h quality level flags".split())


def main():
    replay = pd.read_csv(sys.argv[1])
    cpp = pd.read_csv(sys.argv[2])
    if len(cpp) < len(replay):  # e.g. replay_serial.py --limit N
        print(f"comparing the first {len(cpp)} of {len(replay)} rows")
        replay = replay.iloc[: len(cpp)]
    py = run_python(replay)
    if len(py) != len(cpp):
        print(f"row count differs: python {len(py)} vs cpp {len(cpp)}"); sys.exit(1)
    bad = 0
    for col in ("ts", "quality", "level", "flags"):
        n = int((py[col].to_numpy() != cpp[col].to_numpy()).sum())
        print(f"{col:9s} mismatches: {n}")
        bad += n
    for col, tol in TOL.items():
        a, b = py[col].to_numpy(float), cpp[col].to_numpy(float)
        nan_mismatch = int((np.isnan(a) != np.isnan(b)).sum())
        both = ~np.isnan(a) & ~np.isnan(b)
        err = float(np.max(np.abs(a[both] - b[both]))) if both.any() else 0.0
        ok = nan_mismatch == 0 and err <= tol
        print(f"{col:9s} max|diff| = {err:.2e}  NaN mismatches = {nan_mismatch}  {'OK' if ok else 'FAIL'}")
        bad += 0 if ok else 1
    counts = py["level"].map(lambda v: engine.LEVEL_NAMES[v]).value_counts().to_dict()
    print("levels in replay:", counts)
    print("RESULT:", "MATCH" if bad == 0 else "MISMATCH")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
