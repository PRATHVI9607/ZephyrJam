#!/usr/bin/env bash
# JamShield RPi4 — install the receiver as a systemd service + enable Bluetooth.
# Run as root (via rpi.py sudobash). Additive; reversible by disabling the unit.
set -e

echo "[svc] enabling Bluetooth adapter"
rfkill unblock bluetooth 2>/dev/null || true
systemctl enable bluetooth >/dev/null 2>&1 || true
systemctl start bluetooth 2>/dev/null || true
# Auto-power the adapter so bleak always sees it powered.
if [ -f /etc/bluetooth/main.conf ]; then
  if grep -qiE '^\s*#?\s*AutoEnable=' /etc/bluetooth/main.conf; then
    sed -i 's/^\s*#\?\s*AutoEnable=.*/AutoEnable=true/I' /etc/bluetooth/main.conf
  else
    echo 'AutoEnable=true' >> /etc/bluetooth/main.conf
  fi
fi
systemctl restart bluetooth
sleep 2
bluetoothctl power on 2>/dev/null || true

echo "[svc] stopping any manual receiver"
pkill -f receiver.py 2>/dev/null || true
sleep 1

echo "[svc] installing systemd unit"
cat > /etc/systemd/system/jamshield-recv.service <<'EOF'
[Unit]
Description=JamShield multi-channel receiver
After=network-online.target bluetooth.target mosquitto.service

[Service]
User=prathvi
WorkingDirectory=/home/prathvi/jamshield/src/rpi4
ExecStart=/home/prathvi/jamshield_env/bin/python -u receiver.py --no-espnow
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable jamshield-recv >/dev/null 2>&1
systemctl restart jamshield-recv
sleep 4

echo "--- bluetooth powered ---"
bluetoothctl show 2>/dev/null | grep -iE 'Powered' || echo "no adapter info"
echo "--- service status ---"
systemctl is-active jamshield-recv
echo "--- recent service log ---"
journalctl -u jamshield-recv -n 8 --no-pager 2>/dev/null | tail -8
echo "service-done"
