# Test plan

Passing on simulated or replayed data proves that the code does what the reference says. It does
**not** prove that frost is predicted correctly at a real site.

| # | Test | Where | Status |
|---|---|---|---|
| 1 | FrostCore unit tests (50 checks) | PC: `make -C firmware/host_tests test` | passing on PC |
| 2 | C++ engine = Python reference on 40 archive nights with injected faults (36 040 steps) | PC: `replay_cli` + `tools/compare_replay.py` | MATCH on PC |
| 3 | Same replay through the real ESP32-S3 | `tools/replay_serial.py` | to do (needs board) |
| 4 | Firmware compiles (node: ESP32-S3; gateway: ESP32-S3 and ESP32) | arduino-cli 1.4.1, esp32 core 3.3.12 | compiles, 0 project warnings |
| 5–12 | Bench tests below | bench | to do |
| 13 | Field / simulated-site nights | outdoor or cold room | to do |

## 1. Automated checks on the PC

```bash
make -C firmware/host_tests test
python3 -I tools/make_replay.py data/raw/meteostat_38353_hourly.csv.gz data/replay/bishkek_test40.csv
firmware/host_tests/replay_cli < data/replay/bishkek_test40.csv > data/replay/cpp_out.csv
python3 -I tools/compare_replay.py data/replay/bishkek_test40.csv data/replay/cpp_out.csv
```

**Expected:** `0 failed`, and `RESULT: MATCH`.

The replay contains, cycling through the nights:

- a single 8 °C spike;
- a 45-min dropout of sensor 2;
- a 3 °C drift of sensor 2;
- sensor 1 stuck for 90 min;
- both sensors lost for 20 min.

## 2. Hardware-in-the-loop replay (one ESP32-S3, no sensors needed)

```bash
python3 tools/replay_serial.py /dev/cu.usbmodemXXXX data/replay/bishkek_test40.csv data/replay/device_out.csv --limit 3000
python3 -I tools/compare_replay.py data/replay/bishkek_test40.csv data/replay/device_out.csv
```

**Expected:** `RESULT: MATCH`.

- A level or flag mismatch means the device build differs from the reference: compiler, float
  handling, or the library version.
- Numeric differences up to about 1e-4 can come from the device's `printf` rounding.

## 3. Bench tests with the DHT22s

| # | Scenario | Procedure | Expected result |
|---|---|---|---|
| 5 | Each sensor alive | Power on, `STATUS` after 5 min | Both sensors report, `fails` stays 0 or rare. Quality `GOOD`. |
| 6 | Co-location / relative calibration | Both sensors side by side in the same shield, ≥ 12 h, log every minute | Mean T1−T2 and RH1−RH2 with standard deviation. If |mean ΔT| ≤ 0.5 °C, sensors agree within spec. Store offsets: `SET off_t2=…`, `SAVE`. This only aligns #2 to #1; it does not make either one accurate. |
| 7 | Absolute RH check | Sensors with a saturated NaCl solution in a closed jar (≈75 % RH at room temperature), ≥ 6 h | Reading near 75 %. The deviation is the sensor's RH bias at that point. A reference hygrometer or a standard NaCl table should confirm the exact value. |
| 8 | Step response (time constant) | Move from room into a fridge (≈ 4 °C), log at 2–10 s | T reaches 63 % of the step after τ (record it). Use τ to decide if 1-h trends are meaningful. |
| 9 | Spike filter | Breathe on or touch one sensor for 10 s | `F_SPIKE_REJECTED` at most 3 times, then the new value is accepted. Fused T follows the untouched sensor or flags `SENSOR_DISAGREE`. |
| 10 | One sensor disconnected | Pull the DATA wire of #2 | Quality `DEGRADED`, `F_SENSOR2_FAIL`. Level still computed from #1. After 5 failures, #2 is power-cycled (`powercycles` grows). Reconnect: back to `GOOD`. |
| 11 | Both disconnected | Pull both DATA wires | Level `UNKNOWN`, never `NORMAL`. Dashboard says data not trustworthy. |
| 12 | Cooling through thresholds | Fridge, then freezer (−18 °C) for ≤ 30 min, sensors in a zip bag with desiccant | Clock set to night (`TIME`): WATCH → WARNING → CRITICAL → FROST in order, with no flapping when T hovers at a threshold (hysteresis 0.5 °C). Back at room temperature: levels step down only after T exceeds threshold + 0.5. |
| 12a | Near saturation | Sensors in a humid box (wet towel) at room temperature | RH → 95–100 %, `F_RH_SATURATED` at ≥ 99.5 %, D → 0. Level is driven by T only and does not change. |
| 12b | Reboot | Press RESET during a WARNING | Node restarts with a new `boot_id`; the log survives (`DUMP`). Trends show NaN for about 36 min (`TREND_INSUFFICIENT`); the level is back on the first sample. |
| 12c | Clock not set | Boot without `TIME` and without a gateway | `time_valid=0`, `F_TIME_UNKNOWN`, the level is evaluated at any hour (conservative). |
| 12d | ESP-NOW without a receiver | Default broadcast MAC | `espnow sent` grows. Broadcast has no ACK, so `fail_streak` stays 0. This is expected and cannot measure the link. |
| 12e | ESP-NOW link (needs a 2nd ESP32) | Gateway running; node `GATEWAY_MAC` set to its MAC; walk away / shield the antenna | Gateway prints `G,…` rows. `lost` counts sequence gaps. Duplicates are dropped. `OFFLINE` after 180 s of silence; `ONLINE` again on return. A reboot is reported, not counted as loss. |

## 4. Long logging

- **48 h continuous on USB power:** no watchdog resets (`uptime` keeps growing), log rotation
  works (`STATUS` shows the active file and its size), and the `DUMP` output can be parsed by the
  dashboard.
- **Battery runtime, if needed:** measure the current with a USB power meter and size the battery.
  Many power banks switch off at low load; test the one you use overnight.

## 5. Field or simulated-site nights (the actual research)

Without an outdoor site, the closest honest substitute is a cold room, balcony, or unheated shed:

1. Place the shielded node at a fixed height. Log every night from 17:00 to 10:00.
2. **Independent event reference.** Use a second thermometer that is not a DHT22 (for example a
   DS18B20 or a calibrated digital thermometer) at the same height. Optionally add one at plant or
   grass height, plus photos of hoar frost at dawn.
3. Record cloudiness at 18:00, 21:00 and 06:00 (clear / partly / overcast) and whether there was wind.
4. Each night becomes a row: did air at sensor height reach ≤ 0 °C, and when.
5. After ≥ 30 nights, preferably including ≥ 10 events, rerun `analysis/threshold_study.py`-style
   scoring on the node logs:
   - split by night;
   - compare T only with T + R_T(1 h) and T + R_D(1 h);
   - report precision, recall and lead time with bootstrap CIs.

   With fewer events, report the counts and say the thresholds are not yet determined.
