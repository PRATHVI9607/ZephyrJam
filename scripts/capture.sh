#!/usr/bin/env bash
# Reset the ESP32 and capture serial output for N seconds (non-interactive).
# Usage: capture.sh [/dev/ttyUSB0] [seconds]
DEV="${1:-/dev/ttyUSB0}"
SECS="${2:-14}"
export PATH="$HOME/.local/bin:$PATH"
python3 - "$DEV" "$SECS" <<'PY'
import sys, time
try:
    import serial
except ImportError:
    sys.exit("pyserial missing: pip3 install --user pyserial")
dev, secs = sys.argv[1], float(sys.argv[2])
ser = serial.Serial(dev, 115200, timeout=0.2)
# Reset into the application via the CP210x EN(RTS)/GPIO0(DTR) lines.
ser.dtr = False
ser.rts = True      # EN low  -> hold in reset
time.sleep(0.15)
ser.rts = False     # EN high -> boot the app
ser.reset_input_buffer()
end = time.time() + secs
while time.time() < end:
    data = ser.readline()
    if data:
        sys.stdout.write(data.decode(errors="replace"))
        sys.stdout.flush()
ser.close()
PY
echo "--- capture end ---"
