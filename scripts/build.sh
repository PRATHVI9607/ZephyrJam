#!/usr/bin/env bash
# JamShield — build the ESP32 firmware.
# Source lives on Windows (presentable); build dir is WSL-native (fast).
# Usage: build.sh [pristine-mode]   e.g. build.sh auto | build.sh always
set -e
export PATH="$HOME/.local/bin:$PATH"
export ZEPHYR_BASE="$HOME/jamshield_workspace/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-0.16.8"

APP="/mnt/c/Workspace/IotELL/src/esp32"
BUILD="$HOME/jamshield_build/esp32"
BOARD="esp32_devkitc_wroom"
PRISTINE="${1:-auto}"

echo "[build] ZEPHYR_BASE=$ZEPHYR_BASE"
echo "[build] board=$BOARD app=$APP build=$BUILD pristine=$PRISTINE"
west build -p "$PRISTINE" -b "$BOARD" "$APP" -d "$BUILD"
echo "[build] OK -> $BUILD/zephyr/zephyr.bin"
