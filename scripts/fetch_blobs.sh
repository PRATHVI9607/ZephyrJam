#!/usr/bin/env bash
# Fetch Espressif proprietary RF blobs required for ESP32 WiFi/BLE.
set -e
export PATH="$HOME/.local/bin:$PATH"
cd "$HOME/jamshield_workspace"
west blobs fetch hal_espressif
echo "----- blobs present -----"
find "$HOME/jamshield_workspace/modules/hal/espressif" -name '*.a' 2>/dev/null | head
echo "fetch-blobs-done"
