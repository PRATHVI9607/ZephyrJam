#!/usr/bin/env bash
# JamShield — serial monitor for the ESP32 at 115200 baud (in WSL).
# Usage: monitor.sh [/dev/ttyUSB0]
DEV="${1:-/dev/ttyUSB0}"
export PATH="$HOME/.local/bin:$PATH"
exec python3 -m serial.tools.miniterm "$DEV" 115200 --raw
