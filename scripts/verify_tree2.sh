#!/usr/bin/env bash
Z="$HOME/jamshield_workspace/zephyr"

echo "===== board location ====="
find "$Z/boards" -maxdepth 3 -type d -name 'esp32_devkitc_wroom' 2>/dev/null

echo; echo "===== wifi sample prj.conf ====="
cat "$Z/samples/net/wifi/prj.conf" 2>/dev/null

echo; echo "===== wifi sample esp32 overlay/conf (if any) ====="
ls "$Z/samples/net/wifi/boards/" 2>/dev/null
cat "$Z/samples/net/wifi/boards/esp32_devkitc_wroom"* 2>/dev/null

echo; echo "===== adc0 node body (which unit) ====="
sed -n '405,440p' "$Z/dts/xtensa/espressif/esp32/esp32_common.dtsi" 2>/dev/null

echo "DONE"
