#!/usr/bin/env bash
# JamShield RPi4 — root-level prep (LAN/home-wifi topology). ADDITIVE ONLY:
# installs Mosquitto + venv tooling and adds a broker listener config.
# Does NOT modify any network configuration.
set -e
export DEBIAN_FRONTEND=noninteractive

echo "[root] apt-get update"
apt-get update -y

echo "[root] installing mosquitto + python venv tooling"
apt-get install -y mosquitto mosquitto-clients python3-venv python3-full

echo "[root] mosquitto listener (1883, anonymous) -> conf.d/jamshield.conf"
cat > /etc/mosquitto/conf.d/jamshield.conf <<'EOF'
# JamShield broker — added by rpi_root.sh (safe to remove this file to revert)
listener 1883 0.0.0.0
allow_anonymous true
EOF

systemctl enable mosquitto
systemctl restart mosquitto
echo "[root] mosquitto active: $(systemctl is-active mosquitto)"
echo "rpi-root-done"
