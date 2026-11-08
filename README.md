# ❄️ Frost Early-Warning Array

Low-cost ESP32-S3 nodes that compute the dew point on board, track temperature and dew-point trends,
and raise graded warnings of night-time frost. Telemetry goes over ESP-NOW to a gateway and a browser
dashboard.

Course project: ELT.123 Electronic Components and Circuits, Ala-Too International University.
Author: Nurislam ([@nur1sla8](https://github.com/nur1sla8)).


## Photos

Assembled breadboard prototype: an ESP32 dev board (left) and two DHT22 sensors (right).

![Assembled breadboard prototype: ESP32 dev board with two DHT22 sensors](docs/images/breadboard-prototype.jpg)

Parts: ESP32 board, 2 × DHT22, breadboard, pull-up resistors, jumper wires and a USB cable.

![Parts: ESP32 board, two DHT22 sensors, breadboard, pull-up resistors, jumper wires, USB cable](docs/images/components.jpg)



**1. Dew point.** Magnus formula (Sonntag 1990 coefficients, as in WMO-No. 8):

```
γ  = ln(RH/100) + 17.62·T / (243.12 + T)        Td = 243.12·γ / (17.62 − γ)
D  = T − Td        R_T = dT/dt,  R_D = dD/dt   (least-squares slope over 1 h and 3 h)
```

**2. Sensor QC and fusion.** Each reading passes a range check, spike rejection and stuck-output
detection. Then:

- both sensors agree (≤ 1 °C and ≤ 5 % RH): the mean is used, quality GOOD;
- they disagree: the colder sensor is used, DEGRADED;
- only one is valid: that one is used, DEGRADED;
- none is valid: level UNKNOWN.

**3. Warning levels.** Decided on the 10-min mean temperature in the 18:00–09:00 local window, with
0.5 °C hysteresis. Thresholds come from a Bishkek archive study (2005–2025); recall and false-alarm
ratio are on the test years 2020–2025:

| Level | Rule | Recall | False-alarm ratio |
|---|---|---|---|
| WATCH | T ≤ 4.0 °C | 0.94 | 0.44 |
| WARNING | T ≤ 3.0 °C | 0.90 | 0.34 |
| CRITICAL | T ≤ 2.0 °C | 0.75 | 0.29 |
| FROST | T ≤ 0 °C | detection, not a forecast | – |

Cooling rate and dew-point convergence did not improve warnings beyond the temperature level and the
time of night, so they are reported as context flags and do not change the level.

**4. Transport.** ESP-NOW is used as a star, not a mesh: every node must be in radio range of the
gateway. Frames are 54 bytes with CRC-16, node id, boot id and sequence number.
