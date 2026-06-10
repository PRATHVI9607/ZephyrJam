#!/usr/bin/env bash
# JamShield — Zephyr SDK 0.16.8 install (ESP32 xtensa toolchain only, no sudo).
set -e
cd "$HOME"
SDK_VER="0.16.8"
SDK_DIR="$HOME/zephyr-sdk-$SDK_VER"
TARBALL="zephyr-sdk-${SDK_VER}_linux-x86_64.tar.xz"
URL="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${SDK_VER}/${TARBALL}"

if [ ! -d "$SDK_DIR" ]; then
  if [ ! -f "$TARBALL" ]; then
    echo "[sdk_setup] $(date) :: downloading $TARBALL (~1.5 GB)"
    wget -q --show-progress "$URL" -O "$TARBALL"
  fi
  echo "[sdk_setup] extracting"
  tar xf "$TARBALL"
fi

cd "$SDK_DIR"
echo "[sdk_setup] installing xtensa esp32 toolchain + registering CMake package"
./setup.sh -t xtensa-espressif_esp32_zephyr-elf -c

echo "[sdk_setup] DONE $(date)"
