#!/usr/bin/env bash
# JamShield — live demo dashboard. Prints the latest received packet every 2s so
# the audience watches the active channel flip WIFI <-> BLE during a jam.
DB="$HOME/jamshield/data/jamshield.db"
echo "================= JamShield LIVE ================="
echo " time     |  seq  | channel | rssi | jam_state"
echo " ---------|-------|---------|------|----------"
while true; do
  row=$(sqlite3 "$DB" "SELECT printf('%6d | %-7s | %4d | %s', seq, channel, rssi, jam_state) FROM packets ORDER BY id DESC LIMIT 1;" 2>/dev/null)
  echo " $(date +%H:%M:%S) | $row"
  sleep 2
done
