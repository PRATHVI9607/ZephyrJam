#!/usr/bin/env bash
# Build the JamShield jammer firmware (ESP32 #2).
set -e
export PATH="$HOME/.local/bin:$PATH"
export ZEPHYR_BASE="$HOME/jamshield_workspace/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-0.16.8"

APP="/mnt/c/Workspace/IotELL/src/jammer"
BUILD="$HOME/jamshield_build/jammer"
west build -p "${1:-auto}" -b esp32_devkitc_wroom "$APP" -d "$BUILD"
echo "[jammer] OK -> $BUILD/zephyr/zephyr.bin"
