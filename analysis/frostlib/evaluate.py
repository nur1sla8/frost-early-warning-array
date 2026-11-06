"""Event-based (per-night) evaluation of streaming warning rules.

A rule is evaluated at each decision time tau in DECISION_HOURS. A warning counts only if it is
issued while the air is still above the frost threshold and before the observed onset.
Per night:  event & warned -> TP (lead = onset - first warning),  event & not warned -> FN,
            no event & warned -> FP,  no event & not warned -> TN.
"""
import numpy as np
import pandas as pd

from .data import DECISION_HOURS


def features(wide):
    """Dict of (n_nights, n_decisions) arrays computed only from data available at tau."""
    t, td, d = wide["T"], wide["Td"], wide["D"]
    f = {}
    f["T"] = np.column_stack([t[h] for h in DECISION_HOURS])
    f["Td"] = np.column_stack([td[h] for h in DECISION_HOURS])
    f["D"] = np.column_stack([d[h] for h in DECISION_HOURS])
    f["RT"] = np.column_stack([(t[h] - t[h - 3]) / 3.0 for h in DECISION_HOURS])   # degC/h
    f["RD"] = np.column_stack([(d[h] - d[h - 3]) / 3.0 for h in DECISION_HOURS])   # degC/h
    f["RTd"] = f["RT"] - f["RD"]
    f["hleft"] = np.tile(30.0 - np.array(DECISION_HOURS, dtype=float), (len(wide), 1))  # to 06 local
    wind = wide["wind_ms"]
    f["wind"] = np.column_stack([wind[h] if h in wind else np.nan for h in DECISION_HOURS])
    return f


def eligible(feat, lab, frost_c=0.0):
    """Decision slots where a warning is still meaningful."""
    dec = np.array(DECISION_HOURS, dtype=float)[None, :]
    onset = lab["onset_h"].to_numpy(dtype=float)[:, None]
    before = np.isnan(onset) | (dec < onset)
    return before & (feat["T"] > frost_c)


def score(fire, elig, lab):
    """fire, elig: (n, k) bool. Returns dict of counts, metrics and per-night lead times."""
    f = fire & elig
    warned = f.any(axis=1)
    first = np.where(warned, np.argmax(f, axis=1), -1)
    ev = lab["event"].to_numpy()
    tp = int((ev & warned).sum()); fn = int((ev & ~warned).sum())
    fp = int((~ev & warned).sum()); tn = int((~ev & ~warned).sum())
    dec = np.array(DECISION_HOURS, dtype=float)
    lead = lab["onset_h"].to_numpy(dtype=float) - np.where(first >= 0, dec[np.maximum(first, 0)], np.nan)
    lead = lead[ev & warned]
    return metrics(tp, fn, fp, tn, lead)


def metrics(tp, fn, fp, tn, lead):
    prec = tp / (tp + fp) if tp + fp else np.nan
    rec = tp / (tp + fn) if tp + fn else np.nan
    f1 = 2 * prec * rec / (prec + rec) if tp else 0.0
    return {
        "TP": tp, "FN": fn, "FP": fp, "TN": tn,
        "precision": prec, "recall": rec, "F1": f1,
        "false_alarm_ratio": 1 - prec if tp + fp else np.nan,     # FP / (TP + FP)
        "false_alarm_rate": fp / (fp + tn) if fp + tn else np.nan,  # POFD
        "missed_rate": 1 - rec if tp + fn else np.nan,
        "lead_median_h": float(np.median(lead)) if len(lead) else np.nan,
        "lead_ge6h_share": float(np.mean(lead >= 6)) if len(lead) else np.nan,
        "_lead": lead,
    }


def bootstrap(fire, elig, lab, n=1000, seed=0):
    """Night-level bootstrap 95 % CI for precision, recall, F1, median lead."""
    rng = np.random.default_rng(seed)
    idx = np.arange(len(lab))
    out = []
    for _ in range(n):
        s = rng.choice(idx, size=len(idx), replace=True)
        m = score(fire[s], elig[s], lab.iloc[s])
        out.append((m["precision"], m["recall"], m["F1"], m["lead_median_h"]))
    lo, hi = np.nanpercentile(np.array(out), [2.5, 97.5], axis=0)
    return {k: (round(a, 3), round(b, 3)) for k, a, b in zip(("precision", "recall", "F1", "lead_median_h"), lo, hi)}


def table(rows):
    df = pd.DataFrame([{k: v for k, v in r.items() if not k.startswith("_")} for r in rows])
    return df.round(3)
