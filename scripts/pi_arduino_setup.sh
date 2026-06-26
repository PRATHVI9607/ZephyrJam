#!/usr/bin/env bash
# Install arduino-cli + ESP32 core on the Pi and compile the ESP-NOW receiver
# sketch, so the board can be flashed from the Pi terminal (no laptop/GUI).
set -e
export PATH="$HOME/bin:$PATH"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "[arduino] installing arduino-cli..."
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=$HOME/bin sh
fi

echo "[arduino] configuring ESP32 board index..."
arduino-cli config init --overwrite >/dev/null
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index

echo "[arduino] installing esp32 core (big download, be patient)..."
arduino-cli core install esp32:esp32

echo "[arduino] compiling the ESP-NOW receiver sketch..."
arduino-cli compile --fqbn esp32:esp32:esp32 "$HOME/jamshield/espnow_rx_arduino"
echo "ARDUINO-READY"
