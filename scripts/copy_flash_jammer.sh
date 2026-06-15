#!/usr/bin/env bash
# Stage the jammer's 3 flash images to a Windows folder for esptool.
set -e
B="$HOME/jamshield_build/jammer"
D="/mnt/c/Workspace/IotELL/flash/jammer"
mkdir -p "$D"
cp "$B/zephyr/zephyr.bin"                       "$D/zephyr.bin"
cp "$B/esp-idf/build/bootloader/bootloader.bin"  "$D/bootloader.bin"
cp "$B/esp-idf/build/partitions_singleapp.bin"   "$D/partition-table.bin"
echo COPY_JAMMER_DONE
