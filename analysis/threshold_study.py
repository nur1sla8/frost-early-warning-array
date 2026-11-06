"""Research question: what rate of temperature drop and dew-point convergence reliably triggers
early warnings of night-time air frost?

Data: Bishkek SYNOP (WMO 38353), 3-hourly, 2005-2025 (see frostlib/data.py for cleaning).
Target event: screen-level (2 m) air temperature <= 0 degC observed between 21 and 09 local time,
on nights whose 18 local temperature was > 0 degC. This is NOT plant or surface temperature.

Split by time (no random split of neighbouring hours):
    train 2005-2014, validation 2015-2019, test 2020-2025 (Oct-Apr frost season only).

Usage:  python3 threshold_study.py <meteostat_csv_gz> <out_dir>
"""
import itertools
import json
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from sklearn.linear_model import LinearRegression, LogisticRegression
from sklearn.metrics import roc_auc_score

from frostlib import data, evaluate as ev

SEASON = [10, 11, 12, 1, 2, 3, 4]
TRANSITION = [3, 4, 10, 11]   # months that matter for orchards / crops
SPLITS = {"train": (2005, 2014), "val": (2015, 2019), "test": (2020, 2025)}


# ---------- rule families (each param set -> boolean (n, k) "fire" matrix) ----------

def r_T(f, a):            return f["T"] <= a
def r_rate(f, r):         return f["RT"] <= -r
def r_T_rate(f, a, r):    return (f["T"] <= a) & (f["RT"] <= -r)
def r_D_conv(f, d, q):    return (f["D"] <= d) & (f["RD"] <= -q)
def r_T_Td(f, a, c):      return (f["T"] <= a) & (f["Td"] <= c)
def r_T_rate_Td(f, a, r, c): return (f["T"] <= a) & (f["RT"] <= -r) & (f["Td"] <= c)
def r_proj(f, h):         return f["T"] + f["RT"] * np.minimum(f["hleft"], h) <= 0


A = np.arange(0.5, 10.01, 0.5)            # T threshold, degC
R = np.round(np.arange(-1.0, 2.51, 0.1), 2)  # cooling-rate threshold, degC/h (RT <= -r)
C = np.arange(-12, 6.01, 1.0)             # dew-point threshold, degC
Dg = np.arange(1, 15.01, 1.0)             # T - Td threshold, degC
Q = np.round(np.arange(-1.0, 2.01, 0.1), 2)  # convergence-rate threshold, degC/h (RD <= -q)

FAMILIES = {
    "T only":            (r_T, [A]),
    "cooling rate only": (r_rate, [R]),
    "T + cooling rate":  (r_T_rate, [A, R]),
    "D + convergence":   (r_D_conv, [Dg, Q]),
    "T + Td":            (r_T_Td, [A, C]),
    "T + rate + Td":     (r_T_rate_Td, [A, R, C]),
    "linear projection": (r_proj, [np.array([3.0, 6.0, 9.0, 12.0])]),
}


def subset(wide, lab, years, months):
    y, m = wide.index.year, wide.index.month
    keep = (y >= years[0]) & (y <= years[1]) & np.isin(m, months) & lab["candidate"].to_numpy()
    return wide[keep], lab[keep]


def search(fn, grids, feat, elig, lab):
    rows = []
    for p in itertools.product(*grids):
        m = ev.score(fn(feat, *p), elig, lab)
        m["params"] = tuple(float(x) for x in p)
        rows.append(m)
    return rows


def pick(rows, mode):
    if mode == "maxF1":
        return max(rows, key=lambda m: (m["F1"], m["lead_median_h"]))
    ok = [m for m in rows if m["recall"] >= 0.95]
    return max(ok, key=lambda m: (m["precision"], m["lead_median_h"])) if ok else None


def conditional_table(feat, elig, lab, col, bins, t_band):
    """P(frost later tonight | T at 21 local in t_band, feature bin), decision at 21 local."""
    k = data.DECISION_HOURS.index(21)
    t = feat["T"][:, k]; x = feat[col][:, k]
    ok = elig[:, k] & (t > t_band[0]) & (t <= t_band[1])
    df = pd.DataFrame({"bin": pd.cut(x[ok], bins), "event": lab["event"].to_numpy()[ok]})
    g = df.groupby("bin", observed=False)["event"].agg(["count", "sum"])
    g["p_frost"] = (g["sum"] / g["count"]).round(3)
    return g.rename(columns={"sum": "events", "count": "nights"})


def samples(feat, elig, lab, cols):
    """Per (night, decision) samples for logistic models: y = frost later this night."""
    X = np.column_stack([feat[c][elig] for c in cols])
    y = np.broadcast_to(lab["event"].to_numpy()[:, None], elig.shape)[elig]
    return X, y


def main(src, out):
    out = Path(out); out.mkdir(parents=True, exist_ok=True)
    obs = data.load_synop(src)
    wide = data.build_nights(obs)
    lab = data.label_nights(wide)
    report = {"n_obs": len(obs), "n_nights_complete": len(wide)}

    sets = {}
    for name, yrs in SPLITS.items():
        w, l = subset(wide, lab, yrs, SEASON)
        f = ev.features(w)
        sets[name] = (w, l, f, ev.eligible(f, l))
        report[f"{name}_nights"] = len(l); report[f"{name}_events"] = int(l["event"].sum())
    wt, lt, ft, et = sets["train"]

    # ---- 1. rule families: thresholds chosen on train only, then frozen ----
    results, chosen = [], {}
    for fam, (fn, grids) in FAMILIES.items():
        rows = search(fn, grids, ft, et, lt)
        if fam == "T + cooling rate":
            grid_tr = rows
        if fam == "D + convergence":
            grid_dc = rows
        for mode in ("watch", "maxF1"):
            best = pick(rows, mode)
            if best is None:
                continue
            chosen[(fam, mode)] = best["params"]
            for split in ("train", "val", "test"):
                w, l, f, e = sets[split]
                m = ev.score(fn(f, *best["params"]), e, l)
                m.update(family=fam, mode=mode, split=split, params=best["params"])
                results.append(m)
    res = ev.table(results)
    cols = ["family", "mode", "params", "split", "TP", "FN", "FP", "TN", "precision", "recall", "F1",
            "false_alarm_ratio", "false_alarm_rate", "lead_median_h", "lead_ge6h_share"]
    res[cols].to_csv(out / "rule_families.csv", index=False)

    # ---- 2. bootstrap CI on test for the main candidates ----
    w, l, f, e = sets["test"]
    ci = {}
    for fam in ("T only", "T + cooling rate", "D + convergence", "T + Td", "T + rate + Td"):
        fn = FAMILIES[fam][0]
        ci[fam] = ev.bootstrap(fn(f, *chosen[(fam, "maxF1")]), e, l)
    report["bootstrap_test_maxF1"] = ci

    # ---- 3. transition months only (Mar, Apr, Oct, Nov), same frozen thresholds ----
    trans = []
    for split in ("val", "test"):
        w2, l2 = subset(wide, lab, SPLITS[split], TRANSITION)
        f2 = ev.features(w2); e2 = ev.eligible(f2, l2)
        for fam in ("T only", "T + cooling rate", "D + convergence", "T + Td", "T + rate + Td"):
            m = ev.score(FAMILIES[fam][0](f2, *chosen[(fam, "maxF1")]), e2, l2)
            m.update(family=fam, split=split, events=int(l2["event"].sum()))
            trans.append(m)
    ev.table(trans)[["family", "split", "events", "TP", "FN", "FP", "precision", "recall", "F1",
                     "lead_median_h"]].to_csv(out / "transition_months.csv", index=False)

    # ---- 4. does the rate add information beyond the level? (logistic, sample level) ----
    feats = {
        "T": ["T"], "T+hleft": ["T", "hleft"],
        "T+RT+hleft": ["T", "RT", "hleft"],
        "T+RD+hleft": ["T", "RD", "hleft"],
        "T+Td+hleft": ["T", "Td", "hleft"],
        "T+Td+RT+hleft": ["T", "Td", "RT", "hleft"],
        "T+Td+RT+RD+hleft": ["T", "Td", "RT", "RD", "hleft"],
        "T+Td+RT+hleft+wind (needs anemometer)": ["T", "Td", "RT", "hleft", "wind"],
    }
    auc, coefs = {}, {}
    for name, c in feats.items():
        Xtr, ytr = samples(ft, et, lt, c)
        Xte, yte = samples(f, e, l, c)
        ok_tr = ~np.isnan(Xtr).any(1); ok_te = ~np.isnan(Xte).any(1)
        mu, sd = Xtr[ok_tr].mean(0), Xtr[ok_tr].std(0)
        mdl = LogisticRegression(max_iter=1000).fit((Xtr[ok_tr] - mu) / sd, ytr[ok_tr])
        p = mdl.predict_proba((Xte[ok_te] - mu) / sd)[:, 1]
        auc[name] = round(float(roc_auc_score(yte[ok_te], p)), 4)
        coefs[name] = dict(zip(c, np.round(mdl.coef_[0] / sd, 3).tolist()))
    report["logistic_test_auc"] = auc
    report["logistic_coef_per_unit"] = coefs

    # ---- 5. classic minimum-temperature regression Tmin ~ T18 + Td18 (FAO-style, local fit) ----
    def tmin_xy(wd):
        tmin = wd["T"][[21, 24, 27, 30, 33]].min(axis=1)
        return np.column_stack([wd["T"][18], wd["Td"][18]]), tmin.to_numpy()
    Xa, ya = tmin_xy(wt); Xb, yb = tmin_xy(sets["test"][0])
    lr = LinearRegression().fit(Xa, ya)
    resid = yb - lr.predict(Xb)
    report["tmin_regression"] = {
        "coef_T18": round(float(lr.coef_[0]), 3), "coef_Td18": round(float(lr.coef_[1]), 3),
        "intercept": round(float(lr.intercept_), 3),
        "test_rmse": round(float(np.sqrt(np.mean(resid ** 2))), 3),
        "test_bias": round(float(resid.mean()), 3),
        "test_resid_p90_abs": round(float(np.percentile(np.abs(resid), 90)), 3),
    }
    lr_T = LinearRegression().fit(Xa[:, :1], ya)
    report["tmin_regression"]["test_rmse_T18_only"] = round(
        float(np.sqrt(np.mean((yb - lr_T.predict(Xb[:, :1])) ** 2))), 3)

    # ---- 6. conditional frequency tables (directly answer "which rate") ----
    w_all, l_all = subset(wide, lab, (2005, 2025), SEASON)
    f_all = ev.features(w_all); e_all = ev.eligible(f_all, l_all)
    rt_bins = [-10, -2.0, -1.5, -1.0, -0.67, -0.33, 0, 10]
    rd_bins = [-10, -1.5, -1.0, -0.67, -0.33, 0, 10]
    with open(out / "conditional_tables.txt", "w") as fh:
        for band in [(0, 2), (2, 4), (4, 6), (6, 9)]:
            fh.write(f"\n=== T(21 local) in ({band[0]}, {band[1]}] degC, all years Oct-Apr ===\n")
            fh.write("-- by 18->21 cooling rate RT, degC/h --\n")
            fh.write(conditional_table(f_all, e_all, l_all, "RT", rt_bins, band).to_string() + "\n")
            fh.write("-- by 18->21 change of D = T - Td, degC/h --\n")
            fh.write(conditional_table(f_all, e_all, l_all, "RD", rd_bins, band).to_string() + "\n")
            fh.write("-- by Td at 21 local, degC --\n")
            fh.write(conditional_table(f_all, e_all, l_all, "Td", [-30, -8, -5, -3, -1, 1, 20], band).to_string() + "\n")

    # ---- 6b. does the rate matter on calm (radiation-like) nights, or for onset timing? ----
    calm = ((w_all["wind_ms"][18] <= 1) & (w_all["wind_ms"][21] <= 1)).to_numpy()
    by_wind = {}
    for name, mask in (("calm<=1m/s", calm), ("windy", ~calm)):
        W, L = w_all[mask], l_all[mask]
        tr, te = W.index.year <= 2014, W.index.year >= 2015
        f1, f2 = ev.features(W[tr]), ev.features(W[te])
        e1, e2 = ev.eligible(f1, L[tr]), ev.eligible(f2, L[te])
        aucs = {"nights": int(mask.sum()), "events": int(L["event"].sum())}
        for c in (["T", "hleft"], ["T", "RT", "hleft"], ["T", "Td", "hleft"], ["T", "Td", "RT", "RD", "hleft"]):
            X, y = samples(f1, e1, L[tr], c); X2, y2 = samples(f2, e2, L[te], c)
            mu, sd = X.mean(0), X.std(0)
            p = LogisticRegression(max_iter=1000).fit((X - mu) / sd, y).predict_proba((X2 - mu) / sd)[:, 1]
            aucs["+".join(c)] = round(float(roc_auc_score(y2, p)), 4)
        by_wind[name] = aucs
    report["logistic_test_auc_by_wind"] = by_wind

    k = data.DECISION_HOURS.index(21)
    evm = l_all["event"].to_numpy() & (l_all["onset_h"].to_numpy() > 21)
    X = np.column_stack([np.ones(evm.sum()), f_all["T"][evm, k], f_all["RT"][evm, k], f_all["Td"][evm, k]])
    y = l_all["onset_h"].to_numpy()[evm]
    b = np.linalg.lstsq(X, y, rcond=None)[0]
    rng = np.random.default_rng(0)
    bs = np.array([np.linalg.lstsq(X[s], y[s], rcond=None)[0]
                   for s in (rng.integers(0, len(y), len(y)) for _ in range(2000))])
    report["onset_timing_ols"] = {
        "n_events": int(len(y)), "formula": "onset_hour ~ 1 + T21 + RT(18->21) + Td21",
        "coef": dict(zip(["const", "T21", "RT", "Td21"], np.round(b, 3).tolist())),
        "ci95_RT": np.round(np.percentile(bs[:, 2], [2.5, 97.5]), 3).tolist(),
        "ci95_T21": np.round(np.percentile(bs[:, 1], [2.5, 97.5]), 3).tolist(),
    }

    # ---- 6c. operating levels used by the firmware (frozen thresholds, evaluated on val/test) ----
    levels = []
    for lvl, a in (("WATCH", 4.0), ("WARNING", 3.0), ("CRITICAL", 2.0)):
        for split in ("train", "val", "test"):
            w, l, f, e = sets[split]
            m = ev.score(r_T(f, a), e, l)
            m.update(level=lvl, rule=f"T <= {a}", split=split)
            levels.append(m)
    ev.table(levels)[["level", "rule", "split", "TP", "FN", "FP", "TN", "precision", "recall",
                      "false_alarm_ratio", "lead_median_h", "lead_ge6h_share"]].to_csv(out / "levels.csv", index=False)

    # ---- 6d. historical frequency table shown on the dashboard (context, not a forecast) ----
    bands = [(0, 1), (1, 2), (2, 3), (3, 4), (4, 5), (5, 6), (6, 8), (8, 10)]
    hist = {}
    for h in data.DECISION_HOURS:
        k = data.DECISION_HOURS.index(h)
        t = f_all["T"][:, k]
        hist[str(h % 24)] = [
            {"lo": lo, "hi": hi, "nights": int((m := e_all[:, k] & (t > lo) & (t <= hi)).sum()),
             "frost": int(l_all["event"].to_numpy()[m].sum())}
            for lo, hi in bands]
    (out / "historical_frequency.json").write_text(json.dumps(hist, indent=1))

    # ---- 7. figures ----
    def heat(rows, xs, ys, xlabel, ylabel, title, path):
        z = np.full((len(ys), len(xs)), np.nan)
        for m in rows:
            i = np.argmin(np.abs(ys - m["params"][1])); j = np.argmin(np.abs(xs - m["params"][0]))
            z[i, j] = m["F1"]
        fig, ax = plt.subplots(figsize=(7, 5))
        im = ax.imshow(z, origin="lower", aspect="auto", cmap="viridis",
                       extent=[xs[0], xs[-1], ys[0], ys[-1]])
        fig.colorbar(im, ax=ax, label="F1 (train, per night)")
        ax.set_xlabel(xlabel); ax.set_ylabel(ylabel); ax.set_title(title)
        fig.tight_layout(); fig.savefig(path, dpi=130); plt.close(fig)
    heat(grid_tr, A, R, "T threshold a, °C  (T ≤ a)", "cooling-rate threshold r, °C/h  (R_T ≤ −r)",
         "T + cooling rate", out / "f1_T_rate.png")
    heat(grid_dc, Dg, Q, "gap threshold d, °C  (T−Td ≤ d)", "convergence threshold q, °C/h  (R_D ≤ −q)",
         "Gap + convergence", out / "f1_D_conv.png")

    fig, ax = plt.subplots(figsize=(6, 4))
    for fam in ("T only", "T + cooling rate", "T + Td", "T + rate + Td"):
        lead = [m for m in results if m["family"] == fam and m["mode"] == "maxF1" and m["split"] == "test"][0]["_lead"]
        ax.hist(lead, bins=np.arange(1.5, 16, 3), histtype="step", lw=2, label=fam)
    ax.set_xlabel("lead time, h (3-hourly resolution)"); ax.set_ylabel("events (test)")
    ax.legend(); fig.tight_layout(); fig.savefig(out / "lead_time_test.png", dpi=130); plt.close(fig)

    report["chosen_params"] = {f"{k[0]} | {k[1]}": v for k, v in chosen.items()}
    (out / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False))
    print(json.dumps(report, indent=2, ensure_ascii=False))
    print(res[cols][res["split"] != "train"].to_string(index=False))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
