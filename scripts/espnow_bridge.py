#!/usr/bin/env python3
"""JamShield ESP-NOW bridge: Arduino RX board serial -> MQTT jamshield/feed.

Reads the ESP-NOW receiver board's USB serial (lines "ESPNOW {json}") and
republishes them to the Pi broker so the PWA shows ESP-NOW packets live.

Usage:  python espnow_bridge.py COM6 10.182.210.137
"""
import sys, json
import serial
import paho.mqtt.client as mqtt

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"
BROKER = sys.argv[2] if len(sys.argv) > 2 else "10.182.210.137"
JAM = {0: "CLEAR", 1: "SUSPECTED", 2: "CONFIRMED", 3: "RECOVERING"}

c = mqtt.Client()
c.connect(BROKER, 1883, 30)
c.loop_start()
s = serial.Serial(PORT, 115200, timeout=1)
print(f"[espnow-bridge] {PORT} -> {BROKER}:1883 jamshield/feed")

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
