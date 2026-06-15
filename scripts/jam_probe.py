#!/usr/bin/env python3
"""Probe the jammer: reset it, start jamming ('s'), print its serial log.
Usage: python jam_probe.py [COM7] [seconds]"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM7"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0

ser = serial.Serial(port, 115200, timeout=0.2)
ser.dtr = False
ser.rts = True
time.sleep(0.15)
ser.rts = False
ser.reset_input_buffer()
time.sleep(3.0)        # let it boot + jam_hal_init()
ser.write(b"s")        # start jamming
end = time.time() + secs
while time.time() < end:
    d = ser.readline()
    if d:
        sys.stdout.buffer.write(d)
        sys.stdout.flush()
ser.close()
print("\n--- probe end ---")
