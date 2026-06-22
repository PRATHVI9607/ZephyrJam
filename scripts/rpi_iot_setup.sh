#!/usr/bin/env bash
# JamShield RPi4 - IoT layer setup (run as root via rpi.py sudobash).
# Adds: Mosquitto WebSocket listener (for the PWA), optional cloud bridge,
# and a systemd-hosted PWA on :8081. ADDITIVE; reversible by removing the files.
set -e
UHOME=/home/prathvi
PWA="$UHOME/jamshield/pwa"

echo "[iot] Mosquitto WebSocket listener (9001)"
cp /tmp/mosquitto_ws.conf /etc/mosquitto/conf.d/jamshield-ws.conf

if [ -f /tmp/jamshield-cloud.conf ]; then
  cp /tmp/jamshield-cloud.conf /etc/mosquitto/conf.d/jamshield-cloud.conf
  echo "[iot] cloud bridge config installed"
fi

systemctl restart mosquitto
echo "[iot] mosquitto: $(systemctl is-active mosquitto)"

echo "[iot] fetching MQTT.js into the PWA (one-time, needs internet)"
mkdir -p "$PWA"
if [ ! -s "$PWA/mqtt.min.js" ]; then
  curl -fsSL https://unpkg.com/mqtt@5/dist/mqtt.min.js -o "$PWA/mqtt.min.js" \
    || curl -fsSL https://cdnjs.cloudflare.com/ajax/libs/mqtt/5.10.1/mqtt.min.js -o "$PWA/mqtt.min.js" \
    || echo "[iot] WARN: could not fetch mqtt.min.js - copy it manually into $PWA"
fi
chown -R prathvi:prathvi "$PWA" 2>/dev/null || true

echo "[iot] systemd service to serve the PWA on :8081"
cat > /etc/systemd/system/jamshield-pwa.service <<EOF
[Unit]
Description=JamShield PWA host
After=network-online.target
[Service]
User=prathvi
WorkingDirectory=$PWA
ExecStart=/usr/bin/python3 -m http.server 8081 --bind 0.0.0.0
Restart=always
RestartSec=3
[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable jamshield-pwa >/dev/null 2>&1
systemctl restart jamshield-pwa
sleep 1

echo "[iot] pwa service: $(systemctl is-active jamshield-pwa)"
echo "[iot] open listeners:"; ss -tln | grep -E ':1883|:9001|:8081' || true
IP=$(hostname -I | awk '{print $1}')
echo "[iot] PWA (web/mobile app):  http://$IP:8081/"
echo "[iot] MQTT WS for the app:   ws://$IP:9001"
echo "iot-setup-done"
