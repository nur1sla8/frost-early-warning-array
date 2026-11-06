# Research: what rate of temperature drop and dew-point convergence triggers early frost warnings?

Status: **archive study done; local sensor measurements not yet collected.** Every number below
comes from `analysis/threshold_study.py` run on public station data. None of it is a measurement
made with our DHT22 nodes.

## 1. Question and the event we predict

- **Research question:** what rate of temperature drop (R_T) and dew-point convergence (R_D) reliably
  triggers early warnings of radiation frost?
- **Target event (operational definition):** screen-level (≈2 m) air temperature ≤ 0 °C observed between
  21:00 and 09:00 local time, on a night whose 18:00 temperature was > 0 °C.
- **What this is not:** the temperature of leaves, buds or the soil surface. On clear, calm nights the
  surface can be several degrees colder than the air at 2 m. The system measures air only and must not
  claim plant temperature.

## 2. Data

| Item | Value |
|---|---|
| Source | Meteostat bulk hourly archive, station WMO 38353 (Bishkek), `bulk.meteostat.net/v2/hourly/38353.csv.gz` |
| Variables used | T, dew point Td, wind speed |
| Period | 2005–2025; frost season October–April |
| Resolution | 3-hourly SYNOP (00, 03, … 21 UTC = 06, 09, … 03 local) |
| Nights | 1993 candidate nights with complete 15:00–09:00 data, **583 frost events** |

**Cleaning finding:** from 2020 onward the archive mixes a second source into the station series:
hourly rows with wind in knots, including about 27 % of the rows at synoptic hours. Mixing two sites
produces fake hour-to-hour "rates". Only synoptic-hour rows with wind in whole m/s (or calm) are kept
(`analysis/frostlib/data.py`).

## 3. Method

1. For every night and decision time τ ∈ {18, 21, 00, 03} local, compute the variables available
   at τ: T, Td, D = T − Td, R_T = ΔT/3 h, R_D = ΔD/3 h, and the hours left until 06:00.
2. **Streaming, event-based scoring.** A rule may fire at any τ while T > 0 and before the observed
   onset. Per night the outcome is TP (warned before onset), FN, FP or TN.
   - lead time = onset − first warning;
   - reported metrics: precision, recall, false-alarm ratio FP/(TP+FP), false-alarm rate (POFD), F1.
3. **Split by time, never by random rows:**
   - train 2005–2014: 1004 nights, 294 events;
   - validation 2015–2019: 497 nights, 163 events;
   - test 2020–2025: 492 nights, 126 events.

   Thresholds are chosen on train only and then frozen.
4. **Rule families searched on a grid:**
   - T only;
   - cooling rate only;
   - T + cooling rate;
   - D + convergence rate;
   - T + Td;
   - T + rate + Td;
   - linear projection T + R_T·h ≤ 0.
5. **Incremental value of the rates:** a logistic model per (night, τ), comparing feature sets by
   ROC-AUC on the test years. The same comparison is repeated separately for calm nights
   (wind ≤ 1 m/s at 18 and 21) and windy nights.
6. **Timing:** ordinary least squares of onset hour on T21, R_T and Td21 for event nights, with a
   bootstrap confidence interval.
7. **Uncertainty:** night-level bootstrap (1000×) for precision, recall and F1 on the test years.

## 4. Results (test years 2020–2025 unless stated)

### 4.1 Rule families, thresholds frozen from train (`results/rule_families.csv`)

| Rule (thresholds from train) | Precision | Recall | F1 | False-alarm ratio |
|---|---|---|---|---|
| T ≤ 3.0 °C | 0.659 | 0.905 | 0.763 | 0.341 |
| T ≤ 3.0 °C and R_T ≤ +1 °C/h (rate not binding) | 0.659 | 0.905 | 0.763 | 0.341 |
| T ≤ 3.0 °C and Td ≤ 0 °C | 0.689 | 0.810 | 0.745 | 0.311 |
| D ≤ 10 °C and R_D ≤ +1 °C/h (best gap + convergence) | 0.267 | 0.913 | 0.413 | 0.733 |
| Cooling rate only | 0.256 | 1.000 | 0.408 | 0.744 |
| Linear projection T + R_T·min(h, 3) ≤ 0 | 0.641 | 0.778 | 0.703 | 0.359 |

Bootstrap 95 % CI for "T ≤ 3.0": precision 0.59–0.73, recall 0.85–0.95, F1 0.71–0.81.

The grid search **never chose a cooling-rate requirement**: the best R_T threshold sat at the edge of
the grid, where it is not binding. Forcing a minimum cooling rate on top of T ≤ 3 °C only removes true
events:

| Test years | Precision | Recall | F1 |
|---|---|---|---|
| T ≤ 3 | 0.659 | 0.905 | 0.763 |
| T ≤ 3 and R_T ≤ 0 | 0.651 | 0.873 | 0.746 |
| T ≤ 3 and R_T ≤ −0.5 °C/h | 0.642 | 0.698 | 0.669 |
| T ≤ 3 and R_T ≤ −1.0 °C/h | 0.690 | 0.389 | 0.497 |
| T ≤ 3 and R_T ≤ −1.5 °C/h | 0.690 | 0.159 | 0.258 |

The F1 surface over (T threshold, rate threshold) is in `results/f1_T_rate.png`.

### 4.2 Information added by the rates (logistic model, test ROC-AUC)

| Features | All nights | Calm nights | Windy nights |
|---|---|---|---|
| T | 0.901 | – | – |
| T, hours left | 0.946 | 0.945 | 0.950 |
| + R_T | 0.945 | 0.944 | 0.950 |
| + R_D instead of R_T | 0.946 | – | – |
| T, Td, hours left | 0.952 | 0.950 | 0.955 |
| T, Td, R_T, R_D, hours left | 0.953 | 0.951 | 0.957 |

### 4.3 Onset timing

Onset hour ≈ 25.0 + 0.98·T21 + 0.28·R_T + 0.01·Td21 (n = 359). The 95 % CI of the R_T coefficient is
[−0.11, 0.66]: it includes zero. Cooling rate did not predict *when* frost starts either.

### 4.4 Dew point

Td matters, but as a level rather than as a rate. Among nights with T(21:00) between 2 and 4 °C, frost
followed in 32 % of nights when Td > +1 °C, and in 51–70 % when Td < +1 °C
(`results/conditional_tables.txt`). Physically, condensation releases latent heat and slows cooling
near the dew point.

A locally fitted minimum-temperature regression shows the same effect:

- Tmin = 0.634·T18 + 0.250·Td18 − 3.74;
- test RMSE 2.13 °C, versus 2.49 °C with T18 alone.

### 4.5 Lead time

- The observed median lead is 3 h for the T ≤ 3 °C rule; about 43 % of warned events had a lead
  of 6 h or more.
- The observations are 3-hourly, so the true onset lies up to 3 h before the observation that shows it.
  The true lead is therefore up to 3 h shorter than reported.

## 5. Answer, with its limits

**On 20 years of Bishkek station data, no rate of temperature drop and no rate of dew-point
convergence reliably triggers frost warnings on its own, and adding either one to the temperature
level did not improve warnings.**

What carries the signal:

- the air temperature level during the evening and night (T ≤ 3 °C gave the best balance);
- the time of night;
- to a lesser degree, the dew-point level. A high Td buffers cooling.

Rate requirements of −0.5 to −1.5 °C/h removed 23–82 % of the correctly warned events while
precision stayed within about ±0.04 of the T-only rule.

Limits that keep this from being a final answer for our device:

1. **3-hourly resolution.** Rates over 10–60 min, which the DHT22 node measures, could not be tested.
   They might help in the first hour after sunset; that is the hypothesis our own measurements must test.
2. **Different site.** The data come from an urban station at 2 m, not an orchard or a field.
   Local thresholds will differ.
3. **No cloud data before 2018.** Wind was used as the only proxy for radiative conditions.
4. **Air frost at 2 m is not plant frost.** A surface or leaf sensor would be needed for that.

## 6. How the device uses this

- **Initial mode:** levels by the 10-min mean air temperature in the 18:00–09:00 window, with 0.5 °C
  hysteresis:

  | Level | Rule | Recall (test) | False-alarm ratio (test) |
  |---|---|---|---|
  | WATCH | T ≤ 4.0 °C | 0.94 | 0.44 |
  | WARNING | T ≤ 3.0 °C | 0.90 | 0.34 |
  | CRITICAL | T ≤ 2.0 °C | 0.75 | 0.29 |
  | FROST | T ≤ 0 °C | detection | – |

  R_T, R_D and Td are computed, logged and shown as context flags. They do not change the level.
- **Calibrated mode:** after collecting local nights with an independent frost reference
  (`docs/TEST_PLAN.md`):
  1. rerun the same study on the node logs;
  2. test whether 10–60 min rates add skill at that resolution;
  3. write the new thresholds with `SET` / `SAVE` and set `calibrated=1`.
