#!/usr/bin/env bash
# JamShield — flash the built firmware to the ESP32 over USB (in WSL).
# Requires the device passed through with usbipd (see README "Flashing").
# Usage: flash.sh [/dev/ttyUSB0]
set -e
export PATH="$HOME/.local/bin:$PATH"
export ZEPHYR_BASE="$HOME/jamshield_workspace/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-0.16.8"

BUILD="$HOME/jamshield_build/esp32"
DEV="${1:-/dev/ttyUSB0}"

if [ ! -e "$DEV" ]; then
  echo "ERROR: $DEV not present in WSL."
  echo "Attach the board first (Windows PowerShell, Admin):"
  echo "  usbipd list                 # find the CP210x bus id (e.g. 2-4)"
  echo "  usbipd bind   --busid <id>   # once per board"
  echo "  usbipd attach --wsl --busid <id>"
  exit 1
fi

echo "[flash] flashing $BUILD -> $DEV"
west flash -d "$BUILD" --esp-device "$DEV"
echo "[flash] done. Monitor with: scripts/monitor.sh"
