#!/usr/bin/env bash
# Copy the three ESP32 flash images out of the WSL build dir to a Windows
# folder so they can be flashed with Windows esptool over COM (no usbipd).
set -e
B="$HOME/jamshield_build/esp32"
D="/mnt/c/Workspace/IotELL/flash"
mkdir -p "$D"
cp "$B/zephyr/zephyr.bin"                          "$D/zephyr.bin"
cp "$B/esp-idf/build/bootloader/bootloader.bin"     "$D/bootloader.bin"
cp "$B/esp-idf/build/partitions_singleapp.bin"      "$D/partition-table.bin"
ls -la "$D"
echo COPY_DONE
