# JamShield — User Manual

**Jamming-resilient IoT sensor node with automatic protocol hopping.**
ESP32 DevKit V1 (Zephyr RTOS v3.6.0) → WiFi/MQTT, with automatic failover to
BLE when jamming is detected, and auto-recovery back to WiFi. A Raspberry Pi 4
runs the MQTT broker + multi-channel receiver and logs everything to SQLite.

This manual covers: **(1)** environment & microcontroller setup, **(2)** build &
flash, **(3)** Raspberry Pi setup, **(4)** verification commands, and **(5)** a
step-by-step live demonstration script for presenting today.

> If the system is **already deployed** (it is, as of the last session), skip to
> **§6 Verify the running system** and **§7 Live demonstration**.

---

## 1. What you need

### Hardware
| Item | Notes |
|---|---|
| ESP32 DevKit V1 (WROOM-32) | The sensor node. Connects to your laptop via USB (CP210x → a COM port). |
| Raspberry Pi 4 (Debian 13 / Raspberry Pi OS) | Runs the MQTT broker + receiver. On the same WiFi as the ESP32. |
| LDR + 10 kΩ resistor (optional) | Light sensor on GPIO34. Without it the ADC floats (~adc=130); everything else still works. |
| A 2.4 GHz WiFi network | The ESP32 radio is 2.4 GHz only. ESP32, laptop and Pi all join it. |

### Software (laptop, Windows)
- **WSL2 + Ubuntu** (the Zephyr build runs inside Linux)
- **Python 3** on Windows (for `esptool` flashing + the Pi helper)
- **esptool** and **paramiko**: `python -m pip install esptool paramiko`

### Network facts used in this manual (change to match your setup)
| Thing | Value in this deployment |
|---|---|
| WiFi SSID / password | `Loki` / `loki2536` |
| Raspberry Pi host / IP / user | `prathvi.local` / `10.88.34.137` / `prathvi` |
| ESP32 serial port (Windows) | `COM6` |
| MQTT broker | `10.88.34.137:1883` |
| Sensor topic / control topic | `jamshield/sensor/ldr` / `jamshield/control` |

---

## 2. Build environment setup (laptop / WSL)

> One-time. The bootstrap scripts in `scripts/` already did this; re-run only on
> a fresh machine.

```powershell
# From Windows PowerShell, inside the project folder c:\Workspace\IotELL

# 1) Install the Zephyr workspace (west + Zephyr v3.6.0) — long download
wsl -d Ubuntu -- bash -lc "tr -d '\r' < /mnt/c/Workspace/IotELL/scripts/west_setup.sh | bash"

# 2) Install the Zephyr SDK 0.16.8 + ESP32 toolchain
wsl -d Ubuntu -- bash -lc "tr -d '\r' < /mnt/c/Workspace/IotELL/scripts/sdk_setup.sh | bash"

# 3) Install ninja (build tool) if missing
wsl -d Ubuntu -- bash -lc "tr -d '\r' < /mnt/c/Workspace/IotELL/scripts/fix_ninja.sh | bash"

# 4) Fetch the Espressif RF binary blobs (required for WiFi/BLE)
wsl -d Ubuntu -- bash -lc "tr -d '\r' < /mnt/c/Workspace/IotELL/scripts/fetch_blobs.sh | bash"
```

The build env vars live in `~/jamshield_env.sh` (sourced from `~/.bashrc`).

---

## 3. Configure the firmware

Edit **`src/esp32/include/jamshield.h`** to match your network:

```c
#define JS_WIFI_SSID        "Loki"           // your 2.4 GHz SSID
#define JS_WIFI_PSK         "loki2536"       // your WiFi password
#define JS_MQTT_BROKER_IP   "10.88.34.137"   // the Raspberry Pi's LAN IP
```

> ⚠️ **Security:** the WiFi password is in source. Don't commit it to a public
> repo — gitignore `jamshield.h` or parameterize it before sharing.

To find the Pi's IP: `ping -4 prathvi.local` (Windows) or `hostname -I` on the Pi.

---

## 4. Build & flash the ESP32

### 4.1 Build (in WSL) and stage the images for Windows
```powershell
wsl -d Ubuntu -- bash -lc "bash ~/jamshield_bootstrap/build.sh auto && tr -d '\r' < /mnt/c/Workspace/IotELL/scripts/copy_flash.sh | bash"
```
- `build.sh auto` = incremental build (`always` = clean rebuild).
- This produces 3 images and copies them to `c:\Workspace\IotELL\flash\`:
  `bootloader.bin`, `partition-table.bin`, `zephyr.bin`.

### 4.2 Find the ESP32's COM port
Plug the ESP32 into USB. In PowerShell:
```powershell
[System.IO.Ports.SerialPort]::GetPortNames()        # e.g. COM6
```

### 4.3 Flash from Windows (esptool over COM — no usbipd needed)
```powershell
python -m esptool --chip esp32 -p COM6 -b 460800 write_flash --flash_size detect `
  0x1000 C:\Workspace\IotELL\flash\bootloader.bin `
  0x8000 C:\Workspace\IotELL\flash\partition-table.bin `
  0x10000 C:\Workspace\IotELL\flash\zephyr.bin
```
Success ends with `Hash of data verified.` and `Hard resetting via RTS pin...`.
The DevKit auto-resets — no need to hold BOOT.

### 4.4 Watch it boot (serial monitor)
```powershell
python C:\Workspace\IotELL\scripts\capture_win.py COM6 16
```
> Note: opening the COM port **resets** the board (that's normal). Add `noreset`
> as a 3rd argument to read without resetting: `... capture_win.py COM6 16 noreset`.

**Expected boot output:**
```
*** Booting Zephyr OS build v3.6.0 ***
<inf> jamshield: ==== JamShield starting ====
<inf> ble_gatt: BLE advertising as 'JamShield'
<inf> wifi_mqtt: WiFi station up; will associate to SSID 'Loki'
<inf> wifi_mqtt: WiFi L4 connected: IP=10.88.34.234, broker=10.88.34.137:1883
<inf> wifi_mqtt: MQTT connected to broker 10.88.34.137
<inf> wifi_mqtt: Subscribed to jamshield/control
```

---

## 5. Raspberry Pi setup

> One-time. `scripts/rpi.py` drives the Pi over SSH (Python/paramiko) so nothing
> needs to be typed interactively.

### 5.1 First contact + install an SSH key
```powershell
# Generate a key (in WSL so it's truly passphrase-free), copy to Windows ~/.ssh
wsl -d Ubuntu -- bash -lc "ssh-keygen -t ed25519 -N '' -f /tmp/jskey -C jamshield -q; cp /tmp/jskey* /mnt/c/Users/$env:USERNAME/.ssh/"
# (rename to id_ed25519 / id_ed25519.pub if needed)

# Install the public key on the Pi (uses the password once)
$env:JS_RPI_PASS='<pi-password>'; python C:\Workspace\IotELL\scripts\rpi.py putkey; $env:JS_RPI_PASS=$null
```
`scripts/rpi.py` reads host/user from env (`JS_RPI_HOST`, `JS_RPI_USER`) or
defaults to `10.88.34.137` / `prathvi`.

### 5.2 Deploy the receiver code + install services
```powershell
# Copy src/rpi4 to the Pi
& "$env:WINDIR\System32\tar.exe" -czf "$env:TEMP\jsrpi4.tgz" -C C:\Workspace\IotELL\src rpi4
python C:\Workspace\IotELL\scripts\rpi.py putfile "$env:TEMP\jsrpi4.tgz" /tmp/jsrpi4.tgz
python C:\Workspace\IotELL\scripts\rpi.py run "mkdir -p ~/jamshield/src && tar xzf /tmp/jsrpi4.tgz -C ~/jamshield/src"

# Install Mosquitto + Python venv (root step), then the venv deps (user step)
python C:\Workspace\IotELL\scripts\rpi.py putfile C:\Workspace\IotELL\scripts\rpi_root.sh /tmp/rpi_root.sh
python C:\Workspace\IotELL\scripts\rpi.py putfile C:\Workspace\IotELL\scripts\rpi_user.sh /tmp/rpi_user.sh
python C:\Workspace\IotELL\scripts\rpi.py run "tr -d '\r' < /tmp/rpi_root.sh > /tmp/rr.sh; tr -d '\r' < /tmp/rpi_user.sh > /tmp/ru.sh"
$env:JS_RPI_PASS='<pi-password>'; python C:\Workspace\IotELL\scripts\rpi.py sudobash /tmp/rr.sh; $env:JS_RPI_PASS=$null
python C:\Workspace\IotELL\scripts\rpi.py run "bash /tmp/ru.sh"

# Install the receiver as a systemd service + enable Bluetooth
python C:\Workspace\IotELL\scripts\rpi.py putfile C:\Workspace\IotELL\scripts\rpi_service.sh /tmp/rpi_service.sh
python C:\Workspace\IotELL\scripts\rpi.py run "tr -d '\r' < /tmp/rpi_service.sh > /tmp/rsvc.sh"
$env:JS_RPI_PASS='<pi-password>'; python C:\Workspace\IotELL\scripts\rpi.py sudobash /tmp/rsvc.sh; $env:JS_RPI_PASS=$null
```

After this the Pi runs two services, both auto-starting on boot:
- **`mosquitto`** — MQTT broker on `0.0.0.0:1883`
- **`jamshield-recv`** — the WiFi+BLE receiver writing to `~/jamshield/data/jamshield.db`

---

## 6. Verify the running system

Run these from your laptop (PowerShell). Each prints what "good" looks like.

### 6.1 Services are up
```powershell
python C:\Workspace\IotELL\scripts\rpi.py run "systemctl is-active mosquitto jamshield-recv; bluetoothctl show | grep Powered"
```
✅ Expect: `active`, `active`, `Powered: yes`.

### 6.2 ESP32 is publishing live WiFi packets
```powershell
python C:\Workspace\IotELL\scripts\rpi.py run "mosquitto_sub -h localhost -t jamshield/sensor/ldr -C 3 -W 15 -v"
```
✅ Expect 3 JSON lines like:
```
jamshield/sensor/ldr {"seq":241,"ts_ms":126957,"channel":"WIFI","ldr_adc":130,"ldr_lux":99.1,"rssi":-46,"cpu_util":100,"free_heap":0,"jam_state":"CLEAR"}
```
- `seq` increments, `channel:"WIFI"`, real `rssi`, `jam_state:"CLEAR"`.

### 6.3 Packets are landing in the database
```powershell
python C:\Workspace\IotELL\scripts\rpi.py run "sqlite3 -header -column ~/jamshield/data/jamshield.db 'SELECT channel,COUNT(*) packets,MIN(seq),MAX(seq) FROM packets GROUP BY channel;'"
```
✅ Expect a growing `WIFI` count (and `BLE` rows after a failover).

### 6.4 The Pi's BLE client is connected to the node
```powershell
python C:\Workspace\IotELL\scripts\rpi.py run "journalctl -u jamshield-recv -n 20 --no-pager | grep -iE 'ble|mqtt'"
```
✅ Expect `[wifi] MQTT connected` and `[ble] connected B0:CB:D8:07:64:2E`.

---

## 7. Live demonstration (today)

**Goal:** show the audience the node streaming over WiFi, then *jam it* and watch
it fail over to BLE within ~0.5 s with no data loss, then auto-recover to WiFi.

### Before the audience arrives — sanity check (2 min)
1. Power the Pi and the ESP32 (USB). Wait ~30 s.
2. Run **§6.1–§6.4** — all green.
3. Confirm a failover/recover cycle works once (run the demo below privately).

### Setup: two terminals on screen
- **Terminal A — live packet stream (the "story"):**
  ```powershell
  python C:\Workspace\IotELL\scripts\rpi.py run "mosquitto_sub -h localhost -t jamshield/sensor/ldr -v"
  ```
  Leave it running — it scrolls one line per packet. Audience watches the
  `channel` field change `WIFI → BLE → WIFI`.

- **Terminal B — your control + the trigger:**
  used to publish the JAM/CLEAR commands and query the database.

### The script (≈90 seconds)

**Step 1 — Normal operation.** Point at Terminal A: packets every 0.5 s,
`"channel":"WIFI"`, `"jam_state":"CLEAR"`, real RSSI. *"The node is streaming a
light sensor over WiFi/MQTT to the Pi."*

**Step 2 — Jam it.** In Terminal B:
```powershell
python C:\Workspace\IotELL\scripts\rpi.py run "mosquitto_pub -h localhost -t jamshield/control -m JAM"
```
Within ~0.5 s, Terminal A flips to **`"channel":"BLE"`, `"jam_state":"CONFIRMED"`**.
*"It detected the jam in ~200 ms and hopped to Bluetooth — same data, same
sequence numbers, no loss."* (The sensor topic keeps flowing because the Pi's
bleak client republishes/logs BLE; if you subscribe only to MQTT it pauses — see
the DB query in Step 4 to show BLE packets.)

**Step 3 — Recover.** It auto-recovers after 15 s, or force it:
```powershell
python C:\Workspace\IotELL\scripts\rpi.py run "mosquitto_pub -h localhost -t jamshield/control -m CLEAR"
```
*"When the jam clears, it restores WiFi as the primary channel automatically."*

**Step 4 — Show the receipts (the metrics).** In Terminal B:
```powershell
# Packets per channel — proves BLE actually carried data
python C:\Workspace\IotELL\scripts\rpi.py run "sqlite3 -header -column ~/jamshield/data/jamshield.db 'SELECT channel,COUNT(*) packets,MIN(seq),MAX(seq) FROM packets GROUP BY channel;'"

# Failover events with measured latency — the headline numbers
python C:\Workspace\IotELL\scripts\rpi.py run "sqlite3 -header -column ~/jamshield/data/jamshield.db 'SELECT event_type,from_ch,to_ch,ROUND(latency_ms,1) latency_ms FROM events ORDER BY id DESC LIMIT 6;'"
```
✅ Talking points from the output:
- **Both `WIFI→BLE` and `BLE→WIFI` events**, each with a measured failover latency
  (typically **500–600 ms**).
- **Sequence numbers are continuous across the handover** (e.g. WIFI ends at 311,
  BLE starts at 312) → *zero packet loss during failover*.

**Step 5 (optional) — Show the node's own view.** Plug the ESP32 serial:
```powershell
python C:\Workspace\IotELL\scripts\capture_win.py COM6 40 noreset
```
…then run Step 2/Step 3 again. The serial log narrates the full state machine:
```
control rx 'JAM' -> force_jam=1
Jamming SUSPECTED: RSSI=-47 loss=50%
FAILOVER WIFI -> BLE (jam confirmed, BLE up)
Jamming CONFIRMED! detection latency=202 ms
...
Jamming CEASING, attempting restore
FAILOVER BLE -> WIFI (jam cleared, WiFi restored)
WiFi RESTORED
```

### One-line elevator pitch
*"A commodity ESP32 running Zephyr RTOS detects WiFi jamming in ~200 ms and
deterministically hops to Bluetooth (and back) with sub-second latency and no
data loss — anti-jamming resilience on $5 hardware."*

---

## 8. Talking points / architecture (for Q&A)

- **Three-tier hierarchy:** WiFi (primary) → BLE GATT (secondary) → ESP-NOW
  (tertiary, implemented but gated off; the Espressif HAL is present, DRAM is the
  constraint).
- **Dual-metric detection:** requires *both* low RSSI *and* high packet loss,
  sustained ≥ 3 samples (300 ms), to avoid false positives. LDR light reading
  feeds **adaptive thresholds** (brighter room = noisier RF = relaxed thresholds).
- **Application-level bearer manager** (`conn_mgr_setup.c`): WiFi health uses
  Zephyr's `conn_mgr`/`net_mgmt` L4 events; BLE/ESP-NOW are unified behind one
  `send()` because they aren't `net_if` interfaces.
- **Determinism:** the jam-detection thread runs at priority 2 with a 100 ms
  period; measured detection latency **202 ms**, within the **325 ms** worst-case
  bound from the scheduling analysis (PRD §8.3).
- **The "JAM"/"CLEAR" control topic** is a test/experiment hook — in the field,
  recovery is sensed by the node itself (RSSI recovery + low loss), not commanded.

---

## 9. Troubleshooting

| Symptom | Fix |
|---|---|
| `esptool` can't connect / wrong port | Re-check `GetPortNames()`; close any open serial monitor (only one app can hold COM6); replug USB. |
| Serial monitor shows nothing | Baud is 115200; the port resets on open — that's normal; try again. |
| ESP32 stuck `WiFi disassociated` | Wrong SSID/password in `jamshield.h`, or it's a 5 GHz-only SSID. Rebuild + reflash. |
| `mqtt_connect failed: -2` repeatedly | Indicates the old socket-leak bug — ensure you're on the current firmware (connect-once-then-pump). |
| No packets in DB | `systemctl status jamshield-recv` on the Pi; `journalctl -u jamshield-recv -n 50`. |
| `[ble] No powered Bluetooth adapters` | `sudo rfkill unblock bluetooth && bluetoothctl power on`; re-run `scripts/rpi_service.sh`. |
| usbipd / WSL USB flashing flaky | Don't use it — flash from Windows with `esptool` (§4.3). |
| Pi SSH asks for password | Key not installed; re-run `rpi.py putkey` (§5.1). |

---

## 10. File map (where things live)

```
c:\Workspace\IotELL\
├── src/esp32/           ESP32 Zephyr firmware (C)
│   ├── include/jamshield.h   ← WiFi creds + broker IP (EDIT THIS)
│   └── src/*.c               ← sensor, wifi_mqtt, jam_detect, ble_gatt, bearer mgr, payload
├── src/rpi4/            Raspberry Pi receiver (Python) + setup scripts
├── scripts/            build.sh, copy_flash.sh, capture_win.py, rpi.py, rpi_*.sh
├── flash/              the 3 .bin images staged for Windows esptool
├── README.md           project overview + verified status
└── USERMANUAL.md       this file

On the Pi:  ~/jamshield/src/rpi4/  +  ~/jamshield/data/jamshield.db
On WSL:     ~/jamshield_workspace (Zephyr)  +  ~/jamshield_build/esp32 (build output)
```
