# Frost Early-Warning Array

Low-cost ESP32-based system that predicts radiation frost by tracking the rate of temperature drop and dew-point conditions with two DHT22 sensors.

**Course:** ELT.123 Electronic Components and Circuits, Ala-Too International University
**Author:** Nurislam ([@nur1sla8](https://github.com/nur1sla8))
**Status:** 🚧 In progress

---

## Project Description

Radiation frost happens on clear, calm nights, when the ground loses heat and the air near the surface drops below 0 °C. It damages orchards, gardens and crops, and farmers often get no warning.

This project builds a small, cheap sensor node that:
1. Measures air temperature and relative humidity with two DHT22 sensors.
2. Calculates the dew point on board the ESP32.
3. Tracks how fast the temperature is falling (°C/hour).
4. Raises a frost warning when the temperature trend and dew point indicate that freezing is likely within the next few hours.

## Research Question

[Paste the exact research question from the Project Bank, column "Research Question"]

## Hardware

| Component | Quantity | Purpose |
|---|---|---|
| ESP32 DevKit | 1 | Microcontroller and data logging |
| DHT22 (AM2302) sensor | 2 | Temperature and humidity |
| 10 kΩ resistor | 2 | Pull-up on data line (if the sensor has no breakout board) |
| Breadboard and jumper wires | - | Connections |
| USB power bank / 5 V supply | 1 | Power |
| [Enclosure] | [1] | Outdoor protection |

## Wiring

| DHT22 | ESP32 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| Sensor 1 DATA | GPIO [4] |
| Sensor 2 DATA | GPIO [5] |

Wiring diagram: see [`schematics/`](schematics/).

## How It Works

**Dew point** (Magnus formula):

```
γ  = ln(RH / 100) + (b · T) / (c + T)
Td = (c · γ) / (b − γ)
```
where T is air temperature in °C, RH is relative humidity in %, b = 17.62 and c = 243.12 °C.

**Frost prediction:** [Describe your logic once it is written, for example: the node calculates the temperature drop rate over the last N minutes and, if the rate and the dew point stay below the chosen thresholds, it raises a warning.]

## Repository Structure

```
frost-early-warning-array/
├── code/        # ESP32 firmware (Arduino C++)
├── docs/        # Proposal, analysis, data
├── images/      # Photos of the setup
├── schematics/  # Wiring diagrams
└── README.md
```

## Setup and Usage

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) and add ESP32 board support.
2. Install the library **DHT sensor library** (Adafruit) and its dependency **Adafruit Unified Sensor**.
3. Connect the hardware as described in the Wiring section.
4. Open the sketch from `code/` and set the GPIO pins if you changed them.
5. Select your ESP32 board and port, then upload.
6. Open the Serial Monitor at [115200] baud to see readings and warnings.

## Results

[To be completed after the measurements: plots of temperature and dew point, warning lead time, accuracy.]

## Analysis Summary

- **Goals achieved:** [TBD]
- **Goals not achieved and why:** [TBD]
- **Improvements with more time/resources:** [TBD]

## Media

- Video demonstration: [link TBD]
- Photos: see [`images/`](images/)

## Individual Contributions

This project is done individually by Nurislam.

## License

[MIT / educational use]
