#!/usr/bin/env python3
"""Reset the ESP32 over a Windows COM port and capture serial for N seconds.
Usage: python capture_win.py [COM6] [seconds]
"""
import sys
import time

import serial  # provided by pyserial (pulled in by esptool)

port = sys.argv[1] if len(sys.argv) > 1 else "COM6"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 18.0
noreset = len(sys.argv) > 3 and sys.argv[3] == "noreset"

ser = serial.Serial(port, 115200, timeout=0.2)
if not noreset:
    # Reset into the application via CP210x EN(RTS)/GPIO0(DTR).
    ser.dtr = False
    ser.rts = True
    time.sleep(0.15)
    ser.rts = False
ser.reset_input_buffer()

end = time.time() + secs
while time.time() < end:
    data = ser.readline()
    if data:
        sys.stdout.buffer.write(data)
        sys.stdout.flush()
ser.close()
print("\n--- capture end ---")
