#!/usr/bin/env bash
# JamShield — one-time Raspberry Pi 4 setup (PRD.md Section 10.2).
# Run on a fresh Raspberry Pi OS (Bookworm):  chmod +x setup_rpi4.sh && ./setup_rpi4.sh
set -e

echo "[rpi4] apt packages..."
sudo apt update
sudo apt install -y \
    mosquitto mosquitto-clients \
    bluetooth bluez bluez-tools \
    python3-pip python3-venv \
    sqlite3 \
    hostapd dnsmasq \
    wireless-tools iw \
    tcpdump

echo "[rpi4] python venv..."
python3 -m venv "$HOME/jamshield_env"
# shellcheck disable=SC1091
source "$HOME/jamshield_env/bin/activate"
pip install --upgrade pip
pip install -r "$(dirname "$0")/requirements.txt"

echo "[rpi4] data dir..."
mkdir -p "$HOME/jamshield/data"

echo "[rpi4] hostapd config (SSID JamShield-AP, channel 6, WPA2)..."
sudo tee /etc/hostapd/hostapd.conf >/dev/null <<'EOF'
interface=wlan0
driver=nl80211
ssid=JamShield-AP
hw_mode=g
channel=6
wmm_enabled=0
macaddr_acl=0
auth_algs=1
ignore_broadcast_ssid=0
wpa=2
wpa_passphrase=jamshield2024
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
EOF
sudo sed -i 's|#DAEMON_CONF=""|DAEMON_CONF="/etc/hostapd/hostapd.conf"|' \
    /etc/default/hostapd || true

echo "[rpi4] static IP 192.168.4.1 on wlan0 + dnsmasq DHCP..."
sudo tee /etc/dnsmasq.d/jamshield.conf >/dev/null <<'EOF'
interface=wlan0
dhcp-range=192.168.4.10,192.168.4.50,255.255.255.0,24h
EOF
if ! grep -q "JAMSHIELD wlan0" /etc/dhcpcd.conf 2>/dev/null; then
  sudo tee -a /etc/dhcpcd.conf >/dev/null <<'EOF'

# JAMSHIELD wlan0 static AP address
interface wlan0
static ip_address=192.168.4.1/24
nohook wpa_supplicant
EOF
fi

echo "[rpi4] mosquitto listener config..."
sudo tee /etc/mosquitto/conf.d/jamshield.conf >/dev/null <<'EOF'
listener 1883
allow_anonymous true
EOF

echo "[rpi4] enabling services..."
sudo systemctl unmask hostapd || true
sudo systemctl enable mosquitto hostapd dnsmasq
sudo systemctl restart mosquitto
echo "[rpi4] NOTE: reboot to bring up the AP, then: sudo systemctl status hostapd"
echo "[rpi4] setup done"
