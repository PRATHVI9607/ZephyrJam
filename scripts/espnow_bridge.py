#!/usr/bin/env python3
"""JamShield ESP-NOW bridge: ESP-NOW receiver board (USB serial) -> MQTT feed.

Runs ON THE PI: reads the ESP-NOW receiver board plugged into the Pi's USB
(lines "ESPNOW {json}") and republishes them to the local broker so the Pi stays
the hub for all three radios and the app shows ESP-NOW packets.

Usage:  espnow_bridge.py [port] [broker]
  port   : serial device (default: auto-detect /dev/ttyUSB*|ttyACM* or first COM)
  broker : MQTT host (default: localhost)
"""
import sys
import time
import glob
import json

import serial
from serial.tools import list_ports
import paho.mqtt.client as mqtt

JAM = {0: "CLEAR", 1: "SUSPECTED", 2: "CONFIRMED", 3: "RECOVERING"}


def find_port():
    for pat in ("/dev/ttyUSB*", "/dev/ttyACM*"):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[0]
    ports = list(list_ports.comports())
    return ports[0].device if ports else None


def main():
    want = sys.argv[1] if len(sys.argv) > 1 else None
    broker = sys.argv[2] if len(sys.argv) > 2 else "localhost"

    c = mqtt.Client()
    c.connect(broker, 1883, 30)
    c.loop_start()

    while True:                                   # survive board unplug/replug
        port = want or find_port()
        if not port:
            print("[espnow-bridge] no serial device; retrying...")
            time.sleep(3)
            continue
        try:
            s = serial.Serial(port, 115200, timeout=1)
        except Exception as exc:
            print(f"[espnow-bridge] open {port} failed: {exc}; retry")
            time.sleep(3)
            continue
        print(f"[espnow-bridge] {port} -> {broker}:1883 jamshield/feed")
        try:
            while True:
                line = s.readline().decode("utf-8", "ignore").strip()
                if not line.startswith("ESPNOW "):
                    continue
                try:
                    d = json.loads(line[7:])
                except json.JSONDecodeError:
                    continue
                msg = {"seq": d["seq"], "channel": "ESPNOW", "val": d["val"],
                       "rssi": d["rssi"], "jam_state": JAM.get(d.get("jam", 0), "CLEAR")}
                c.publish("jamshield/feed", json.dumps(msg))
                print("ESPNOW seq", d["seq"])
        except Exception as exc:
            print(f"[espnow-bridge] serial error: {exc}; reopening")
            time.sleep(2)


if __name__ == "__main__":
    main()
