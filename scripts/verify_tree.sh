#!/usr/bin/env bash
# Probe version-specific facts in the installed Zephyr tree.
Z="$HOME/jamshield_workspace/zephyr"

echo "===== west update tail ====="
tail -3 "$HOME/jamshield_bootstrap/west.log" 2>/dev/null

echo; echo "===== esp32 board dirs ====="
ls "$Z/boards/espressif/" 2>/dev/null | grep -i wroom
echo "--- board yaml names ---"
find "$Z/boards/espressif" -name '*esp32_devkitc*' -name '*.yaml' 2>/dev/null | head

echo; echo "===== ADC nodes in esp32 dtsi ====="
grep -rnE 'adc[0-9]+:' "$Z/dts/xtensa/espressif/esp32/" 2>/dev/null | head

echo; echo "===== conn manager Kconfig symbol ====="
grep -rl 'config NET_CONNECTION_MANAGER' "$Z/subsys/net" 2>/dev/null | head -1 && echo "-> NET_CONNECTION_MANAGER exists"
grep -rlE 'config NET_CONN_MGR\b' "$Z/subsys/net" 2>/dev/null | head -1 && echo "-> NET_CONN_MGR exists"

echo; echo "===== net_if_get_first_wifi ====="
grep -rn 'net_if_get_first_wifi' "$Z/include" 2>/dev/null | head -1

echo; echo "===== wifi sample prj.conf (esp32 reference) ====="
for d in samples/net/wifi samples/net/wifi/shell; do
  if [ -f "$Z/$d/prj.conf" ]; then echo "--- $d ---"; fi
done
ls "$Z/samples/net/wifi/" 2>/dev/null
echo "DONE"
