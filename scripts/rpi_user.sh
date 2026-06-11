#!/usr/bin/env bash
# JamShield RPi4 — user-level prep. ADDITIVE ONLY: creates ~/jamshield_env venv
# and installs the Python receiver dependencies. Run as the normal user.
set -e

echo "[user] creating venv ~/jamshield_env"
python3 -m venv "$HOME/jamshield_env"
"$HOME/jamshield_env/bin/pip" install --upgrade pip --quiet

echo "[user] installing paho-mqtt + bleak (MQTT + BLE receivers)"
"$HOME/jamshield_env/bin/pip" install --quiet paho-mqtt bleak

mkdir -p "$HOME/jamshield/data"
echo "[user] venv python: $("$HOME/jamshield_env/bin/python" --version)"
echo "[user] paho: $("$HOME/jamshield_env/bin/python" -c 'import paho.mqtt;print(paho.mqtt.__version__)' 2>/dev/null || echo n/a)"
echo "rpi-user-done"
