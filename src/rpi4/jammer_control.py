#!/usr/bin/env python3
"""JamShield — jammer control (PRD.md Section 8.3).

Sends a single-byte command over serial to the second ESP32 running the
ESP-IDF beacon-flood jammer:  's' = start, 'x' = stop.

  python3 jammer_control.py start [--port /dev/ttyUSB1]
  python3 jammer_control.py stop  [--port /dev/ttyUSB1]
"""
from __future__ import annotations

import argparse
import sys

try:
    import serial
except ImportError:
    serial = None


def send(cmd: bytes, port: str, baud: int) -> None:
    if serial is None:
        sys.exit("pyserial not installed: pip install pyserial")
    with serial.Serial(port, baud, timeout=1) as ser:
        ser.write(cmd)
        ser.flush()
    print(f"[jammer] sent {cmd!r} to {port}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=["start", "stop"])
    ap.add_argument("--port", default="/dev/ttyUSB1")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()
    send(b"s" if args.action == "start" else b"x", args.port, args.baud)


if __name__ == "__main__":
    main()
