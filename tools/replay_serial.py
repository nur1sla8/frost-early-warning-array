"""Hardware-in-the-loop replay: feed a replay CSV to the ESP32 node and collect its answers.

The node must run frost_node. The script switches it to REPLAY mode, sends one "R,..." row,
waits for the matching "O,..." answer (lock-step, no buffer overrun), and writes the answers in the
same format as firmware/host_tests/replay_cli. Then compare with the Python reference:
    python3 -I tools/compare_replay.py data/replay/bishkek_test40.csv data/replay/device_out.csv

Usage: python3 tools/replay_serial.py <port> <replay.csv> <device_out.csv> [--limit N]
"""
import argparse
import time

import serial

HEADER = "ts,t,rh,td,d,t_smooth,rt_1h,rd_1h,rt_3h,quality,level,flags"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("replay")
    ap.add_argument("out")
    ap.add_argument("--limit", type=int, default=0, help="only the first N rows (0 = all)")
    ap.add_argument("--baud", type=int, default=115200)
    a = ap.parse_args()

    rows = [l.strip() for l in open(a.replay) if l[:1].isdigit()]
    if a.limit:
        rows = rows[: a.limit]
    with serial.Serial(a.port, a.baud, timeout=5) as s, open(a.out, "w") as f:
        time.sleep(2.0)
        s.reset_input_buffer()
        s.write(b"MODE REPLAY\n")
        f.write(HEADER + "\n")
        t0 = time.time()
        for i, row in enumerate(rows):
            s.write(("R," + row + "\n").encode())
            while True:
                line = s.readline().decode(errors="replace").strip()
                if not line:
                    raise SystemExit(f"timeout waiting for answer to row {i}: {row}")
                if line.startswith("O,"):
                    f.write(line[2:] + "\n")
                    break
                if line.startswith("# ERR"):
                    raise SystemExit(f"device error on row {i}: {line}")
            if i % 1000 == 0:
                print(f"{i}/{len(rows)} rows, {time.time() - t0:.0f} s")
        s.write(b"MODE LIVE\n")
    print(f"done: {len(rows)} rows -> {a.out}")


if __name__ == "__main__":
    main()
