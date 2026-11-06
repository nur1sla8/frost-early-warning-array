"""Log a node (D-lines) or the gateway (G-lines) from Serial to CSV on the PC.

Sets the device clock from the PC first (TIME command), so timestamps are real UTC.
Usage: python3 tools/serial_logger.py <port> <out.csv> [--baud 115200]
   e.g. python3 tools/serial_logger.py /dev/cu.usbmodem1101 data/logs/bench_2026-11-07.csv
Lines starting with '#' are device messages and are echoed to the console, not stored.
"""
import argparse
import time
from pathlib import Path

import serial

from formats import GW_HEADER, NODE_HEADER


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("out")
    ap.add_argument("--baud", type=int, default=115200)
    a = ap.parse_args()
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with serial.Serial(a.port, a.baud, timeout=1) as s:
        time.sleep(2.0)  # boards with native USB may reset on open
        s.write(f"TIME {int(time.time())}\n".encode())
        kind = None
        with open(out, "a", buffering=1) as f:
            while True:
                raw = s.readline().decode(errors="replace").strip()
                if not raw:
                    continue
                if raw.startswith("#"):
                    print(raw)
                    continue
                if raw[:2] not in ("D,", "G,"):
                    continue
                if kind is None:
                    kind = raw[0]
                    if out.stat().st_size == 0:
                        f.write((NODE_HEADER if kind == "D" else GW_HEADER) + "\n")
                f.write(raw[2:] + "\n")
                print(raw)


if __name__ == "__main__":
    main()
