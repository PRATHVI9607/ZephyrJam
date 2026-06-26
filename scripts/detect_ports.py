#!/usr/bin/env python3
"""Identify which CP210x COM port is the JamShield node vs the jammer by probing
each for its boot/STAT signature. Prints JSON: {"node": "COMx", "jammer": "COMy"}.
Robust to the two ESP32s swapping USB ports between sessions.
"""
import json
import sys
import time

import serial
from serial.tools import list_ports


def role_of(port: str, secs: float = 5.0):
    try:
        s = serial.Serial(port, 115200, timeout=0.3)
    except Exception:
        return None
    try:
        s.dtr = False
        s.rts = True
        time.sleep(0.12)
        s.rts = False           # reset into the app
        s.reset_input_buffer()
        end = time.time() + secs
        while time.time() < end:
            ln = s.readline().decode("utf-8", "replace")
            if "STAT " in ln or "JamShield starting" in ln:
                return "node"
            if "JAM:" in ln or "JAMMER" in ln:
                return "jammer"
            if "ESPNOW " in ln or "ESP-NOW receiver" in ln:
                return "espnow_rx"
    finally:
        s.close()
    return None


def main():
    ports = [p.device for p in list_ports.comports()
             if "CP210" in (p.description or "") or "Silicon" in (p.description or "")
             or "CH340" in (p.description or "")]
    found = {"node": None, "jammer": None, "espnow_rx": None}
    for port in ports:
        r = role_of(port)
        if r and not found[r]:
            found[r] = port
    print(json.dumps(found))


if __name__ == "__main__":
    main()
