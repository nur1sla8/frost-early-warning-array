"""Reference implementation of the on-device warning engine (FrostCore, firmware/libraries/FrostCore).

The C++ firmware must produce the same levels as this file on the same input; see
firmware/host_tests and tools/compare_replay.py. Keep the two in sync: any change of logic here
needs the same change in C++ and vice versa.

Levels (initial mode, thresholds from results/levels.csv, Bishkek SYNOP 2005-2025):
    UNKNOWN   no trustworthy sensor data
    NORMAL
    WATCH     T <= 4.0 degC in the evaluation window (18:00-09:00 local)
    WARNING   T <= 3.0 degC
    CRITICAL  T <= 2.0 degC
    FROST     T <= 0.0 degC  (detection, not a forecast)
Rates and dew point are computed and reported as context flags only: on the archive they did not
add skill beyond T and time of night, so they do not change the level until local data show otherwise.
"""
import math
from collections import deque
from dataclasses import dataclass, field

from . import psychro

UNKNOWN, NORMAL, WATCH, WARNING, CRITICAL, FROST = range(6)
LEVEL_NAMES = ["UNKNOWN", "NORMAL", "WATCH", "WARNING", "CRITICAL", "FROST"]

# quality
Q_GOOD, Q_DEGRADED, Q_BAD = 0, 1, 2

# flag bits (same values as FrostCore warning.h)
F_SENSOR1_FAIL = 1 << 0
F_SENSOR2_FAIL = 1 << 1
F_SENSOR_DISAGREE = 1 << 2
F_SPIKE_REJECTED = 1 << 3
F_STUCK = 1 << 4
F_RH_SATURATED = 1 << 5
F_TIME_UNKNOWN = 1 << 6
F_OUTSIDE_WINDOW = 1 << 7
F_COOLING_FAST = 1 << 8      # RT_1h <= -1 degC/h (context only)
F_CONVERGING_FAST = 1 << 9   # RD_1h <= -1 degC/h (context only)
F_DEWPOINT_HIGH = 1 << 10    # Td >= +1 degC: latent-heat buffer (context only)
F_DEWPOINT_BELOW_ZERO = 1 << 11
F_TREND_INSUFFICIENT = 1 << 12


@dataclass
class Config:
    t_watch: float = 4.0
    t_warning: float = 3.0
    t_critical: float = 2.0
    t_frost: float = 0.0
    hysteresis: float = 0.5          # DHT22 accuracy; prevents flapping
    smooth_s: int = 600              # level uses mean T of the last 10 min
    window_start_h: int = 18         # evaluation window, local time
    window_end_h: int = 9
    tz_offset_s: int = 6 * 3600
    t_agree: float = 1.0             # 2 x DHT22 +-0.5 degC
    rh_agree: float = 5.0            # 2 x DHT22 +-2..5 %
    spike_t_per_min: float = 1.0     # degC/min
    spike_rh_per_min: float = 10.0   # %/min
    spike_min_t: float = 1.0
    spike_min_rh: float = 8.0
    spike_max_rejects: int = 3       # after N rejects accept the new level (real step)
    stuck_count: int = 60            # identical T and RH for 60 readings (1 h at 1/min)
    rh_saturated: float = 99.5
    offset_t1: float = 0.0
    offset_rh1: float = 0.0
    offset_t2: float = 0.0
    offset_rh2: float = 0.0


@dataclass
class SensorState:
    last_t: float = math.nan
    last_rh: float = math.nan
    last_ts: int = -1
    rejects: int = 0
    same: int = 0


@dataclass
class Output:
    ts: int
    t: float = math.nan
    rh: float = math.nan
    td: float = math.nan
    tf: float = math.nan
    d: float = math.nan
    t_smooth: float = math.nan
    rt_1h: float = math.nan
    rd_1h: float = math.nan
    rt_3h: float = math.nan
    quality: int = Q_BAD
    level: int = UNKNOWN
    flags: int = 0


def _nsum(values):
    """Plain left-to-right sum, like the C++ loop (Python >= 3.12 sum() is compensated)."""
    acc = 0.0
    for v in values:
        acc += v
    return acc


def _round_centi(x):
    """Decision value rounded to 0.01 degC (half away from zero for x >= 0, as floor(x*100+0.5))."""
    return math.floor(x * 100.0 + 0.5) / 100.0


def _slope(buf, ts, window_s, idx):
    """OLS slope (unit per hour) of buf[(ts, T, D)] over [ts - window_s, ts]; NaN if sparse."""
    pts = [(p[0], p[idx]) for p in buf if p[0] >= ts - window_s]
    if len(pts) < 3:
        return math.nan
    span = pts[-1][0] - pts[0][0]
    if span < 0.6 * window_s:
        return math.nan
    n = len(pts)
    mx = _nsum(float(p[0] - ts) for p in pts) / n
    my = _nsum(p[1] for p in pts) / n
    sxx = _nsum((float(p[0] - ts) - mx) * (float(p[0] - ts) - mx) for p in pts)
    sxy = _nsum((float(p[0] - ts) - mx) * (p[1] - my) for p in pts)
    return sxy / sxx * 3600.0 if sxx > 0 else math.nan


class Engine:
    def __init__(self, cfg=None):
        self.cfg = cfg or Config()
        self.s = [SensorState(), SensorState()]
        self.buf = deque()      # (ts, T, D) of fused samples, last 3 h
        self.level = NORMAL

    def _check(self, i, ts, t, rh, ok):
        """Returns (valid, flags) for one sensor reading and updates its state."""
        c, st = self.cfg, self.s[i]
        fail_bit = F_SENSOR1_FAIL if i == 0 else F_SENSOR2_FAIL
        if not ok or math.isnan(t) or math.isnan(rh) or not (-40 <= t <= 80) or not (0 <= rh <= 100):
            return False, fail_bit
        flags = 0
        if st.last_ts >= 0:
            dt_min = max((ts - st.last_ts) / 60.0, 1e-6)
            dtt, drh = abs(t - st.last_t), abs(rh - st.last_rh)
            spike = ((dtt > c.spike_min_t and dtt / dt_min > c.spike_t_per_min)
                     or (drh > c.spike_min_rh and drh / dt_min > c.spike_rh_per_min))
            if spike and st.rejects < c.spike_max_rejects:
                st.rejects += 1
                return False, F_SPIKE_REJECTED
            st.same = st.same + 1 if (t == st.last_t and rh == st.last_rh) else 0
        st.rejects = 0
        st.last_t, st.last_rh, st.last_ts = t, rh, ts
        if st.same >= c.stuck_count:
            return False, F_STUCK | fail_bit
        return True, flags

    def update(self, ts, t1, rh1, ok1, t2, rh2, ok2, time_valid=True):
        c = self.cfg
        out = Output(ts=ts)
        t1, rh1 = t1 + c.offset_t1, rh1 + c.offset_rh1
        t2, rh2 = t2 + c.offset_t2, rh2 + c.offset_rh2
        v1, f1 = self._check(0, ts, t1, rh1, ok1)
        v2, f2 = self._check(1, ts, t2, rh2, ok2)
        out.flags |= f1 | f2

        if v1 and v2:
            if abs(t1 - t2) <= c.t_agree and abs(rh1 - rh2) <= c.rh_agree:
                out.t, out.rh, out.quality = (t1 + t2) / 2, (rh1 + rh2) / 2, Q_GOOD
            else:
                # cannot tell which one is right: take the colder pair (earlier warning)
                out.t, out.rh = (t1, rh1) if t1 <= t2 else (t2, rh2)
                out.quality = Q_DEGRADED
                out.flags |= F_SENSOR_DISAGREE
        elif v1 or v2:
            out.t, out.rh = (t1, rh1) if v1 else (t2, rh2)
            out.quality = Q_DEGRADED
        else:
            out.quality = Q_BAD
            out.level = self.level = UNKNOWN
            return out

        out.rh = min(max(out.rh, psychro.RH_MIN), psychro.RH_MAX)
        out.td = float(psychro.dew_point(out.t, out.rh))
        out.d = out.t - out.td
        if out.t <= 0:
            out.tf = float(psychro.frost_point(out.t, out.rh))
        if out.rh >= c.rh_saturated:
            out.flags |= F_RH_SATURATED

        self.buf.append((ts, out.t, out.d))
        while self.buf and self.buf[0][0] < ts - 3 * 3600:
            self.buf.popleft()
        recent = [p[1] for p in self.buf if p[0] > ts - c.smooth_s]
        out.t_smooth = _round_centi(_nsum(recent) / len(recent))
        out.rt_1h = _slope(self.buf, ts, 3600, 1)
        out.rd_1h = _slope(self.buf, ts, 3600, 2)
        out.rt_3h = _slope(self.buf, ts, 3 * 3600 - 60, 1)
        if math.isnan(out.rt_1h):
            out.flags |= F_TREND_INSUFFICIENT
        if out.rt_1h <= -1.0:
            out.flags |= F_COOLING_FAST
        if out.rd_1h <= -1.0:
            out.flags |= F_CONVERGING_FAST
        if out.td >= 1.0:
            out.flags |= F_DEWPOINT_HIGH
        if out.td < 0.0:
            out.flags |= F_DEWPOINT_BELOW_ZERO

        if time_valid:
            hour = ((ts + c.tz_offset_s) % 86400) // 3600
            in_window = hour >= c.window_start_h or hour < c.window_end_h
        else:
            in_window = True
            out.flags |= F_TIME_UNKNOWN
        if not in_window:
            out.flags |= F_OUTSIDE_WINDOW

        out.level = self.level = self._level(out.t_smooth, in_window)
        return out

    def _level(self, t, in_window):
        c = self.cfg
        if t <= c.t_frost or (self.level == FROST and t <= c.t_frost + c.hysteresis):
            return FROST
        if not in_window:
            return NORMAL
        thr = {WATCH: c.t_watch, WARNING: c.t_warning, CRITICAL: c.t_critical}
        target = NORMAL
        for lvl in (CRITICAL, WARNING, WATCH):
            if t <= thr[lvl]:
                target = lvl
                break
        prev = self.level if self.level in thr else NORMAL
        if target >= prev:
            return target
        # going down: hold the previous level until T clears its threshold by the hysteresis
        if t <= thr[prev] + c.hysteresis:
            return prev
        return target
