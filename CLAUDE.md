# CLAUDE.md — JamShield Project
## Instructions for Claude Code / Claude AI Assistant

> This file tells Claude exactly what to do, in what order, with what tools,
> to build the JamShield project from scratch. Read this FIRST before touching
> any code. Follow phases in strict order.

---

## 🧠 PROJECT CONTEXT

**Project:** JamShield — Jamming-resilient IoT sensor node with automatic protocol hopping  
**Hardware:** ESP32 DevKit V1 (30-pin WROOM-32) + Raspberry Pi 4  
**RTOS:** Zephyr v3.6.0 on ESP32  
**OS on RPi4:** Raspberry Pi OS (Debian Bookworm)  
**Dev Environment:** Windows + WSL2 (Ubuntu 22.04) + VSCode  
**Language:** C (Zephyr), Python 3.11 (RPi4)  
**Full spec:** See PRD.md for complete architecture and requirements  

---

## 🛠 YOUR CAPABILITIES IN THIS PROJECT

You have access to (use these aggressively):

| Tool | Use For |
|---|---|
| `bash` tool | Run west build, west flash, SSH to RPi4, run Python scripts |
| `create_file` | Create new C, Python, config files |
| `str_replace` | Edit existing files |
| `view` | Read files to understand current state |
| Filesystem tools | Navigate project directory |

**WSL2 paths:**
- Zephyr workspace: `~/jamshield_workspace/`
- Project root: `~/jamshield/`
- ESP32 app: `~/jamshield/src/esp32/`
- RPi4 scripts: `~/jamshield/src/rpi4/`
- Build output: `~/jamshield/build/`

**Key commands:**
```bash
# Build
west build -p auto -b esp32_devkitc_wroom ~/jamshield/src/esp32

# Flash (ESP32 must be connected via USB and usbipd attached)
west flash --esp-device /dev/ttyUSB0

# Serial monitor
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200

# SSH to RPi4
ssh pi@raspberrypi.local

# Sync files to RPi4
rsync -avz ~/jamshield/src/rpi4/ pi@raspberrypi.local:/home/pi/jamshield/
```

---

## 📋 MASTER CHECKLIST — DO THESE IN ORDER

### ═══ PHASE 0: ENVIRONMENT SETUP ═══

**0.1 — Verify WSL2 and tools**
```bash
# Run each of these and verify no errors:
west --version              # Should show west 1.x.x
python3 --version           # Should show 3.10+
xtensa-espressif_esp32_zephyr-elf-gcc --version  # ESP32 toolchain
esptool.py version          # esptool
ls /dev/ttyUSB*             # ESP32 connected? If not, run usbipd attach
```

If west not found:
```bash
pip3 install --user west
echo 'export PATH=$HOME/.local/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
```

If Zephyr not initialized:
```bash
mkdir ~/jamshield_workspace
cd ~/jamshield_workspace
west init -m https://github.com/zephyrproject-rtos/zephyr --mr v3.6.0
west update  # Takes ~10 min, do NOT skip
pip3 install --user -r ~/jamshield_workspace/zephyr/scripts/requirements.txt
```

If ESP32 toolchain not found:
```bash
cd ~
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/zephyr-sdk-0.16.8_linux-x86_64.tar.xz
tar xvf zephyr-sdk-0.16.8_linux-x86_64.tar.xz
cd zephyr-sdk-0.16.8
./setup.sh -t xtensa-espressif_esp32_zephyr-elf
echo 'export ZEPHYR_TOOLCHAIN_VARIANT=zephyr' >> ~/.bashrc
echo 'export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-0.16.8' >> ~/.bashrc
echo 'export ZEPHYR_BASE=$HOME/jamshield_workspace/zephyr' >> ~/.bashrc
source ~/.bashrc
```

**✅ Phase 0 success criteria:**
- `west build -p auto -b esp32_devkitc_wroom samples/hello_world` completes with no errors
- ESP32 shows "Hello World!" on serial monitor after flash

---

**0.2 — Create project directory structure**
```bash
mkdir -p ~/jamshield/{src/{esp32/{src,include,boards},rpi4,jammer/main},data,analysis/figures,paper,dashboard}
cd ~/jamshield
```

**0.3 — Verify RPi4 connectivity**
```bash
# Ping RPi4
ping raspberrypi.local -c 3

# SSH (must be passwordless with key)
ssh pi@raspberrypi.local "echo 'RPi4 accessible'"

# If SSH fails, set up key:
ssh-keygen -t ed25519 -C "jamshield"
ssh-copy-id pi@raspberrypi.local
```

**0.4 — Set up RPi4**
```bash
# Copy setup script to RPi4 and run it
scp ~/jamshield/src/rpi4/setup_rpi4.sh pi@raspberrypi.local:~/
ssh pi@raspberrypi.local "chmod +x setup_rpi4.sh && ./setup_rpi4.sh"
# This takes ~5 minutes
```

---

### ═══ PHASE 1: WEST APPLICATION SCAFFOLD ═══

**1.1 — Create CMakeLists.txt**

Create `~/jamshield/src/esp32/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(jamshield)

target_sources(app PRIVATE
    src/main.c
    src/sensor_ldr.c
    src/jam_detect.c
    src/wifi_mqtt.c
    src/ble_gatt.c
    src/espnow_l2.c
    src/conn_mgr_setup.c
    src/adaptive_threshold.c
)

target_include_directories(app PRIVATE include)
```

**1.2 — Create prj.conf (Zephyr Kconfig)**

Create `~/jamshield/src/esp32/prj.conf` with FULL contents from PRD.md Appendix A.

**1.3 — Create board overlay**

Create `~/jamshield/src/esp32/boards/esp32_devkitc_wroom.overlay` with contents from PRD.md Section 6.2.

**1.4 — Create west.yml**

Create `~/jamshield/src/esp32/west.yml`:
```yaml
manifest:
  remotes:
    - name: zephyrproject-rtos
      url-base: https://github.com/zephyrproject-rtos
  projects:
    - name: zephyr
      remote: zephyrproject-rtos
      revision: v3.6.0
      import: true
  self:
    path: app
```

**1.5 — Create stub main.c and verify build**

Create `~/jamshield/src/esp32/src/main.c`:
```c
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(jamshield, LOG_LEVEL_DBG);

int main(void)
{
    LOG_INF("JamShield starting...");
    LOG_INF("Board: %s", CONFIG_BOARD);
    
    while (1) {
        LOG_INF("Alive at %llu ms", k_uptime_get());
        k_msleep(2000);
    }
    return 0;
}
```

Build and verify:
```bash
cd ~/jamshield
west build -p auto -b esp32_devkitc_wroom src/esp32
# Must succeed with 0 errors before proceeding
```

**✅ Phase 1 success criteria:**
- `west build` completes for stub main.c
- `west flash` works and serial shows "JamShield starting..."

---

### ═══ PHASE 2: LDR SENSOR THREAD ═══

**2.1 — Create sensor_ldr.h**

Create `~/jamshield/src/esp32/include/sensor_ldr.h`:
```c
#ifndef SENSOR_LDR_H
#define SENSOR_LDR_H

#include <stdint.h>

/* LDR reading structure */
struct ldr_reading {
    uint16_t adc_raw;       /* 0-4095, 12-bit */
    uint32_t timestamp_ms;  /* k_uptime_get() at read time */
};

/* Initialize LDR ADC */
int sensor_ldr_init(void);

/* Get latest LDR reading (thread-safe) */
struct ldr_reading sensor_ldr_get(void);

/* Convert ADC value to approximate lux (integer, x10 for one decimal) */
uint32_t sensor_ldr_to_lux_x10(uint16_t adc_raw);

#endif /* SENSOR_LDR_H */
```

**2.2 — Create sensor_ldr.c**

Create `~/jamshield/src/esp32/src/sensor_ldr.c` implementing:
- ADC initialization using Zephyr ADC API on ADC1_CH6 (GPIO34)
- Sensor thread: priority 5, 1024-byte stack, 1000ms period
- Thread-safe reading using k_mutex
- 12-bit resolution, internal reference
- LDR to lux conversion: `lux_x10 = (4095 - adc_raw) * 10 / 40` (approximate)
- LOG_INF output on every reading: "LDR: adc=%d, lux=%d.%d"

**2.3 — Wire into main.c**

Add to main.c:
```c
#include "sensor_ldr.h"
// In main(): sensor_ldr_init();
```

**2.4 — Build and test**
```bash
west build -p auto -b esp32_devkitc_wroom src/esp32
west flash --esp-device /dev/ttyUSB0
# Serial should show LDR readings every 1 second
# Cover LDR with finger → value should increase
# Shine light → value should decrease
```

**✅ Phase 2 success criteria:**
- LDR readings visible in serial, value changes with light
- No ADC errors in log

---

### ═══ PHASE 3: WIFI + MQTT ═══

**3.1 — Create wifi_mqtt.h**

Define:
- `wifi_mqtt_connect()` → int
- `wifi_mqtt_publish(const char *topic, const char *payload, size_t len)` → int
- `wifi_mqtt_is_connected()` → bool
- SSID: `"JamShield-AP"`, PASSWORD: `"jamshield2024"`
- MQTT broker: `"192.168.4.1"` (RPi4 hotspot IP) or use mDNS

**3.2 — Create wifi_mqtt.c**

Implement using Zephyr:
- `CONFIG_WIFI`, `CONFIG_NET_L2_WIFI_MGMT`, `CONFIG_MQTT_LIB`
- WiFi connection retry with exponential backoff (3 attempts, 1s/2s/4s)
- MQTT client with keepalive=60s, QoS=1
- JSON payload builder for `jamshield/sensor/ldr` topic
- Packet sent counter (for loss tracking)
- PUBACK tracking (for ACK confirmation)
- Send timestamp logged per packet

**3.3 — JSON Payload format (EXACT)**
```c
snprintf(buf, sizeof(buf),
    "{\"seq\":%u,\"ts_ms\":%llu,\"channel\":\"WIFI\","
    "\"ldr_adc\":%u,\"rssi\":%d,\"cpu_util\":%u,"
    "\"free_heap\":%u,\"jam_state\":\"%s\"}",
    seq++, k_uptime_get(), ldr.adc_raw, rssi,
    cpu_util, free_heap, jam_state_str);
```

**3.4 — Set up RPi4 WiFi Hotspot**

On RPi4:
```bash
# Configure hostapd for JamShield-AP on channel 6
sudo systemctl enable hostapd
sudo systemctl start hostapd

# Verify hotspot visible from laptop:
# Should see SSID "JamShield-AP" in WiFi scan

# Start Mosquitto
sudo systemctl start mosquitto

# Subscribe to verify:
mosquitto_sub -h localhost -t "jamshield/sensor/ldr" -v
```

**3.5 — Build, flash, verify**
```bash
west build -p auto -b esp32_devkitc_wroom src/esp32
west flash --esp-device /dev/ttyUSB0
# Serial should show: "WiFi connected to JamShield-AP"
# Serial should show: "MQTT connected to broker"
# Serial should show: "Published: seq=1, 2, 3..."
# RPi4 terminal should show MQTT messages arriving
```

**✅ Phase 3 success criteria:**
- WiFi connection confirmed in serial log
- MQTT messages visible on RPi4 subscriber
- Sequence numbers incrementing correctly

---

### ═══ PHASE 4: JAMMING DETECTION ═══

**4.1 — Create jam_detect.h**

Define:
```c
typedef enum {
    JAM_STATE_CLEAR,
    JAM_STATE_SUSPECTED,
    JAM_STATE_CONFIRMED,
    JAM_STATE_RECOVERING,
} jam_state_t;

int jam_detect_init(void);
jam_state_t jam_detect_get_state(void);
int8_t jam_detect_get_rssi(void);
uint8_t jam_detect_get_loss_pct(void);
void jam_detect_register_callback(void (*cb)(jam_state_t new_state));
```

**4.2 — Create jam_detect.c**

Implement EXACTLY as specified in PRD.md Section 8:
- Thread priority: **2** (HIGH — critical for WCET guarantee)
- Stack size: 2048 bytes
- Sample period: 100ms
- Dual-metric: RSSI < threshold AND loss_pct > threshold
- Consecutive confirmation: 3 samples minimum
- State machine: CLEAR → SUSPECTED → CONFIRMED → RECOVERING
- Detection latency timestamp: `k_uptime_get()` at state change
- Callback invoked when state changes to CONFIRMED
- LOG_WRN at SUSPECTED, LOG_ERR at CONFIRMED with latency

**4.3 — Create adaptive_threshold.c**

Implement from PRD.md Section 6.5:
- `update_thresholds_from_ldr(uint16_t ldr_adc_value)`
- Three tiers: bright (>3000), medium (1000-3000), dark (<1000)
- Threshold values per PRD.md Section 6.5

**4.4 — Wiring**

In sensor thread: after each LDR read, call `update_thresholds_from_ldr(ldr.adc_raw)`

**4.5 — Test jamming detection**

Test WITHOUT jammer first:
```bash
# Manually pull WiFi connection by turning off RPi4 hotspot
sudo systemctl stop hostapd
# Should see: "Jamming SUSPECTED" then "Jamming CONFIRMED" in ~300-500ms
# Turn hotspot back on
sudo systemctl start hostapd
# Should see: "Jamming CEASING" then "WiFi RESTORED"
```

**✅ Phase 4 success criteria:**
- Detection fires within 500ms of WiFi disconnect
- Detection does NOT fire during normal WiFi operation
- Recovery detected when WiFi returns
- All state transitions logged with timestamps

---

### ═══ PHASE 5: BLE GATT FALLBACK ═══

**5.1 — Create ble_gatt.h**

Define:
```c
int ble_gatt_init(void);
int ble_gatt_send(const struct ble_sensor_payload *payload);
bool ble_gatt_is_connected(void);
bool ble_gatt_notifications_enabled(void);
```

**5.2 — Create ble_gatt.c**

Implement from PRD.md Section 7.2:
- Service UUID: `BT_UUID_DECLARE_16(0x1234)`
- Characteristic UUID: `BT_UUID_DECLARE_16(0x1235)`, NOTIFY+READ
- CCC descriptor for notification enable/disable
- Advertisement: device name "JamShield", connectable
- On connect: LOG_INF with peer address
- On disconnect: LOG_WRN, restart advertising
- Send: `bt_gatt_notify()` with `ble_sensor_payload` struct (18 bytes binary)
- Pre-connect in background BEFORE jamming confirmed (reduces latency)

**5.3 — struct ble_sensor_payload**

Create in `include/jamshield.h`:
```c
struct ble_sensor_payload {
    uint32_t seq;
    uint64_t ts_ms;
    uint8_t  channel;
    uint16_t ldr_adc;
    int8_t   rssi;
    uint8_t  jam_state;
    uint8_t  cpu_util;
} __packed;
```

**5.4 — Start BLE advertising at boot**

In main.c init sequence, call `ble_gatt_init()` BEFORE WiFi connection.
BLE should be advertising even while WiFi is active.

**5.5 — RPi4 BLE receiver setup**

On RPi4:
```bash
# Verify Bluetooth is working
hciconfig hci0 up
bluetoothctl scan on
# Should see "JamShield" after ~5s
bluetoothctl scan off
```

Create `src/rpi4/ble_receiver.py` using bleak:
- Scan for device named "JamShield"
- Connect and subscribe to characteristic 0x1235
- Parse 18-byte binary payload (struct.unpack)
- Write to database with channel='BLE'

**5.6 — Test BLE**
```bash
# Run RPi4 BLE receiver
ssh pi@raspberrypi.local "python3 ~/jamshield/ble_receiver.py &"

# Disconnect WiFi on RPi4 (stop hotspot)
# ESP32 should fall through to BLE
# BLE notifications should appear in RPi4 terminal
```

**✅ Phase 5 success criteria:**
- ESP32 advertising visible in bluetoothctl scan
- BLE connection established between ESP32 and RPi4
- Notifications arriving with correct binary payload
- Database showing channel=BLE entries

---

### ═══ PHASE 6: ESP-NOW FALLBACK ═══

**6.1 — Find RPi4's WiFi MAC address**
```bash
ssh pi@raspberrypi.local "ip link show wlan0 | grep ether"
# Note the MAC: XX:XX:XX:XX:XX:XX
# Update src/esp32/src/espnow_l2.c with this MAC
```

**6.2 — Create espnow_l2.h**

Define:
```c
int espnow_l2_init(void);
int espnow_send_payload(const struct ble_sensor_payload *payload);
```

**6.3 — Create espnow_l2.c**

Implement from PRD.md Section 7.3:
- Call `esp_now_init()` from ESP-IDF HAL (available in Zephyr ESP32 port)
- Register RPi4 MAC as peer: `esp_now_add_peer()`
- `espnow_send_payload()`: serialize payload to 18-byte buffer, call `esp_now_send()`
- Register recv callback for future ACK support
- LOG_INF on init, LOG_DBG on each send

**IMPORTANT NOTE for ESP-NOW on Zephyr:**
Zephyr ESP32 port exposes ESP-IDF HAL functions. Include:
```c
#include <esp_now.h>
#include <esp_wifi.h>
```
These are available as Zephyr HAL modules, not raw ESP-IDF.

**6.4 — RPi4 ESP-NOW monitor mode setup**

On RPi4 (requires USB WiFi dongle as second interface for monitor mode):
```bash
# Set second WiFi interface to monitor mode
sudo iw dev wlan1 set type monitor
sudo ip link set wlan1 up

# Verify monitor mode
iw dev wlan1 info | grep type
# Should show: type monitor
```

Create `src/rpi4/espnow_receiver.py` using scapy:
- Sniff on wlan1 interface
- Filter for Dot11Action frames from ESP32's MAC
- Parse vendor-specific payload as `ble_sensor_payload` binary
- Write to database with channel='ESPNOW'

**6.5 — Test ESP-NOW**
```bash
# Start ESP-NOW receiver on RPi4
ssh pi@raspberrypi.local "sudo python3 ~/jamshield/espnow_receiver.py &"

# Manually trigger ESP-NOW mode (or wait for jamming)
# Check database for channel=ESPNOW entries
```

**✅ Phase 6 success criteria:**
- ESP-NOW packets captured by RPi4 scapy sniffer
- Binary payload correctly decoded
- Database showing channel=ESPNOW entries

---

### ═══ PHASE 7: CONN_MGR INTEGRATION ═══

**7.1 — Create conn_mgr_setup.h and conn_mgr_setup.c**

Implement from PRD.md Sections 6.3 and 9.3:
- Register WiFi, BLE, ESP-NOW as conn_mgr bearers
- Set priorities: WiFi=1, BLE=2, ESP-NOW=3
- Register event callback for NET_EVENT_L4_CONNECTED/DISCONNECTED
- `trigger_bearer_failover()`: called by jam_detect on JAM_STATE_CONFIRMED
- `attempt_wifi_restore()`: called when jamming ceasing
- Log ALL bearer transitions with timestamps for latency measurement

**7.2 — Payload thread**

Create `src/esp32/src/payload_thread.c`:
- Priority 6, 2048-byte stack, 500ms period
- Gets active bearer from conn_mgr
- If bearer=WIFI: call `wifi_mqtt_publish()`
- If bearer=BLE: call `ble_gatt_send()`
- If bearer=ESPNOW: call `espnow_send_payload()`
- Always includes `jam_state` from `jam_detect_get_state()` in payload

**7.3 — Wire event logging**

In conn_mgr event callback:
```c
// Record FAILOVER event to ESP32 log
// Also send a special MQTT/BLE/ESP-NOW meta-message to RPi4:
// {"event":"FAILOVER","from":"WIFI","to":"BLE","ts_ms":XXX,"latency_ms":YYY}
```

On RPi4, receiver.py listens for these meta-messages and inserts into events table.

**7.4 — Full system test (no jammer yet)**

Manual test sequence:
1. Start all RPi4 receivers
2. Flash and start ESP32
3. Verify WiFi packets in database
4. Stop RPi4 hotspot → verify failover to BLE → verify BLE packets in database
5. Restart RPi4 hotspot → verify recovery to WiFi
6. Stop Bluetooth on RPi4 → verify failover to ESP-NOW
7. Re-enable Bluetooth → verify partial recovery

**✅ Phase 7 success criteria:**
- Automatic failover WiFi→BLE when WiFi drops
- Automatic failover BLE→ESP-NOW when BLE drops
- Automatic recovery to WiFi when it returns
- All transitions logged with ms-accurate timestamps in database
- No crash or hang during any transition

---

### ═══ PHASE 8: JAMMER ═══

**8.1 — Set up jammer ESP32 (second board)**

This uses ESP-IDF (NOT Zephyr) since raw 802.11 frame injection requires `esp_wifi_80211_tx()`.

Create `src/jammer/main/jammer_main.c`:
```c
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

/* Beacon frame template for channel 6 flood */
static const uint8_t beacon_frame[] = {
    0x80, 0x00,             // Frame control: beacon
    0x00, 0x00,             // Duration
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // Dest: broadcast
    0xde, 0xad, 0xbe, 0xef, 0x00, 0x01,  // Source
    0xde, 0xad, 0xbe, 0xef, 0x00, 0x01,  // BSSID
    0x00, 0x00,             // Sequence
    // Beacon body...
};

void app_main(void)
{
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_AP);
    
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "JAMMER",
            .channel = 6,
        }
    };
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
    
    // Flood beacon frames on channel 6
    while (1) {
        for (int i = 0; i < 100; i++) {
            esp_wifi_80211_tx(WIFI_IF_AP, beacon_frame,
                              sizeof(beacon_frame), false);
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);  // 1ms between bursts
    }
}
```

**8.2 — Build and flash jammer**
```bash
cd ~/jamshield/src/jammer
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB1 flash  # Use second USB port for jammer
```

**8.3 — RPi4 jammer control script**

Create `src/rpi4/jammer_control.py`:
```python
import serial, sys, time

JAMMER_PORT = "/dev/ttyUSB0"  # Or USB-serial connected to jammer

def start_jammer():
    # Send 's' command to jammer serial to start
    pass

def stop_jammer():
    # Send 'x' command to jammer serial to stop
    pass

if __name__ == "__main__":
    if sys.argv[1] == "start":
        start_jammer()
    elif sys.argv[1] == "stop":
        stop_jammer()
```

**✅ Phase 8 success criteria:**
- Jammer running causes ESP32 WiFi to disconnect in <5s
- Confirmed by serial log showing RSSI drop
- Can start/stop jammer from RPi4 command

---

### ═══ PHASE 9: RPi4 FULL RECEIVER ═══

**9.1 — Unified receiver.py**

Create `src/rpi4/receiver.py` combining all three channels from PRD.md Section 10.3.
Use asyncio to run WiFi, BLE, and ESP-NOW receivers concurrently.

**9.2 — Database setup**

Create `src/rpi4/database.py` implementing the full schema from PRD.md Section 15.1.
```python
def init_db(db_path: str):
    # Create packets, events, experiment_runs tables
    # Create failover_events and packet_loss_by_channel views
    pass

def insert_packet(conn, payload: dict):
    # Insert into packets table
    pass

def insert_event(conn, event_type: str, from_ch: str, to_ch: str, latency_ms: float):
    # Insert into events table
    pass
```

**9.3 — Start receiver service**
```bash
# On RPi4 (add to systemd for autostart)
sudo tee /etc/systemd/system/jamshield.service << EOF
[Unit]
Description=JamShield Receiver
After=network.target bluetooth.target

[Service]
User=pi
WorkingDirectory=/home/pi/jamshield
ExecStart=/home/pi/jamshield_env/bin/python3 src/rpi4/receiver.py
Restart=always

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl enable jamshield
sudo systemctl start jamshield
sudo systemctl status jamshield
```

**✅ Phase 9 success criteria:**
- All three receivers running simultaneously
- Packets arriving on all three channels logged correctly
- No receiver crashes after 10 minutes of operation
- `sqlite3 jamshield.db "SELECT channel, COUNT(*) FROM packets GROUP BY channel"` shows data

---

### ═══ PHASE 10: EXPERIMENT RUNNER ═══

**10.1 — Create experiment runner**

Create `src/rpi4/run_experiment.py`:
```python
#!/usr/bin/env python3
"""
JamShield Experiment Runner
Usage: python3 run_experiment.py --exp 1 --trials 50 --jammer-dist 30
"""
import argparse, sqlite3, time, subprocess

def run_experiment_1(trials: int, jammer_dist_cm: int):
    """Failover latency distribution"""
    for trial in range(trials):
        # 1. Mark experiment start in DB
        # 2. Verify WiFi packets arriving (5s baseline)
        # 3. Start jammer
        # 4. Wait for failover event in DB (max 10s timeout)
        # 5. Record failover latency
        # 6. Stop jammer
        # 7. Wait for WiFi restoration (max 30s)
        # 8. Record recovery latency
        # 9. Wait 5s, then next trial
        pass
```

**10.2 — Run all experiments in sequence**

For each experiment in PRD.md Section 14:
1. Start RPi4 receiver if not running
2. Run `run_experiment.py --exp N`
3. Verify data in database
4. Export to CSV: `sqlite3 jamshield.db ".mode csv" ".output data/experiment_N.csv" "SELECT * FROM ..."`

---

### ═══ PHASE 11: ANALYSIS & FIGURES ═══

**11.1 — Compute metrics**
```bash
cd ~/jamshield/analysis
python3 compute_metrics.py
# Should print tables for all 7 experiments
```

**11.2 — Generate paper figures**
```bash
python3 plot_latency_cdf.py     # Figure 2: CDF of failover latency
python3 plot_timeline.py        # Figure 3: Packet timeline with jam window
python3 plot_distance.py        # Figure 4: Distance vs latency
python3 plot_espnow.py          # Figure 5: ESP-NOW survivability
```

Each script saves to `analysis/figures/*.pdf`

---

### ═══ PHASE 12: PAPER ═══

**12.1 — Paper structure**

Create `paper/jamshield.tex` following structure in PRD.md Section 17.2.
Use IEEE Conference format (IEEEtran class, 2-column, 8 pages max).

**12.2 — Include all figures**
```latex
\begin{figure}[t]
  \centering
  \includegraphics[width=\columnwidth]{figures/fig2_failover_cdf.pdf}
  \caption{CDF of WiFi→BLE failover latency across 50 trials.}
  \label{fig:cdf}
\end{figure}
```

**12.3 — Build paper**
```bash
cd ~/jamshield/paper
pdflatex jamshield.tex
bibtex jamshield
pdflatex jamshield.tex
pdflatex jamshield.tex
# jamshield.pdf is the output
```

---

## 🚨 CRITICAL RULES — NEVER BREAK THESE

1. **NEVER use floating point in any Zephyr thread with priority < 6**
   - Use integer arithmetic: lux_x10 (fixed point), loss_pct as uint8_t
   - Float operations cause ISR stack corruption on Xtensa LX6

2. **NEVER malloc() in Zephyr threads**
   - Use static buffers or Zephyr memory slabs (k_mem_slab)
   - Dynamic allocation can cause heap fragmentation on 520KB RAM

3. **ALWAYS check return codes**
   ```c
   int ret = mqtt_publish(&client, &param);
   if (ret != 0) {
       LOG_ERR("MQTT publish failed: %d", ret);
       // Handle error
   }
   ```

4. **ALWAYS use k_uptime_get() for timestamps, NEVER time()**
   - `k_uptime_get()` is monotonic, millisecond resolution
   - POSIX time() is unreliable on embedded without NTP sync

5. **NEVER call ESP-IDF functions directly from Zephyr threads**
   - Exception: `esp_now_init()`, `esp_now_send()`, `esp_now_add_peer()` from the espnow_l2.c shim ONLY
   - These are specifically allowed because Zephyr has no native ESP-NOW

6. **ALWAYS include sequence numbers in every packet**
   - Required for loss calculation
   - Use `atomic_inc()` for thread-safe increment

7. **Thread stack sizes are MINIMUM — increase if stack overflow**
   ```
   # Enable stack overflow detection in prj.conf:
   CONFIG_STACK_SENTINEL=y
   CONFIG_THREAD_ANALYZER=y
   ```

8. **USB passthrough MUST be active before flashing**
   ```powershell
   # Run in Windows PowerShell before each session:
   usbipd attach --wsl --busid X-Y
   ```

---

## 🔧 DEBUGGING GUIDE

### ESP32 won't flash
```bash
# Hold BOOT button while running:
west flash --esp-device /dev/ttyUSB0
# Release BOOT after "Connecting..." appears
```

### No serial output
```bash
# Verify baud rate:
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200
# Try alternate device:
ls /dev/ttyACM* /dev/ttyUSB*
```

### WiFi won't connect
```bash
# Verify hotspot is up on RPi4:
ssh pi@raspberrypi.local "systemctl status hostapd"
# Verify SSID matches exactly: "JamShield-AP"
# Verify password matches: "jamshield2024"
# Check Zephyr log: "wifi connect" → "connected" or error code
```

### BLE not advertising
```bash
# Enable BT on RPi4:
sudo systemctl start bluetooth
bluetoothctl power on
bluetoothctl scan on
# If not visible after 10s, check:
# - CONFIG_BT=y in prj.conf
# - bt_enable() called in main.c before ble_gatt_init()
```

### ESP-NOW packets not received
```bash
# Verify monitor mode:
iw dev wlan1 info | grep type
# Verify MAC address in espnow_l2.c matches RPi4's wlan0 MAC:
ssh pi@raspberrypi.local "ip link show wlan0 | grep ether"
# Verify scapy running as root:
sudo python3 espnow_receiver.py
```

### Database empty
```bash
# Check receiver is running:
systemctl status jamshield
# Check for Python errors:
journalctl -u jamshield -n 50
# Manually test DB write:
sqlite3 ~/jamshield/data/jamshield.db "SELECT COUNT(*) FROM packets"
```

### Build errors: "undefined reference to..."
```bash
# Add missing source file to CMakeLists.txt target_sources()
# Rebuild clean:
west build -p always -b esp32_devkitc_wroom src/esp32
```

### Memory errors / stack overflow
```bash
# Add to prj.conf:
# CONFIG_STACK_SENTINEL=y
# CONFIG_THREAD_ANALYZER=y
# CONFIG_THREAD_ANALYZER_AUTO=y
# CONFIG_THREAD_ANALYZER_AUTO_INTERVAL=5
# This prints stack usage every 5s to serial
```

---

## 📊 KEY METRICS TO VERIFY AT EACH PHASE

| Phase | Metric | Expected Value |
|---|---|---|
| 1 | Build time | < 2 min |
| 2 | LDR read period | 990-1010 ms (±1%) |
| 3 | MQTT publish rate | ~2 pkt/s |
| 3 | WiFi RSSI | -40 to -70 dBm typical |
| 4 | Detection latency | 300-500 ms |
| 4 | False positives/hour | < 1 without jammer |
| 5 | BLE connection time | 1-3 s |
| 5 | BLE notify rate | ~2 msg/s |
| 6 | ESP-NOW send rate | ~2 pkt/s |
| 7 | Failover latency (WiFi→BLE) | 400-700 ms |
| 7 | Failover latency (BLE→ESPNOW) | 100-300 ms |
| 9 | DB insert rate | ~6 rows/s (2 per channel) |
| 10 | Experiment 1 N=50 | ~45 min total |

---

## 📁 VSCODE WORKSPACE SETUP

When Claude opens this project in VSCode:

1. Open WSL2 terminal: Ctrl+` → select "WSL" profile
2. Navigate to `~/jamshield`
3. Open with VSCode: `code .`
4. VSCode will automatically load `.vscode/settings.json` and `tasks.json`
5. Install all extensions listed in PRD.md Section 12.1 if not already installed
6. For nRF Connect: open sidebar → "Add existing application" → `src/esp32`

**Keyboard shortcuts after setup:**
- `Ctrl+Shift+B` → Build ESP32
- `Ctrl+Shift+P` → "Tasks: Run Task" → Flash / Monitor / Start RPi4 / etc.

---

## 🌐 NETWORK TOPOLOGY

```
┌─────────────────────────────────────────────────┐
│                  Your Laptop                      │
│  WSL2: Zephyr build, west flash, analysis Python │
│  VSCode: remote WSL2                             │
│  IP: 192.168.4.X (when on JamShield-AP)         │
└──────────────────────────┬──────────────────────┘
                           │ SSH / rsync
                    ┌──────▼──────┐
                    │   RPi4      │
                    │ 192.168.4.1 │  ← hostapd creates this subnet
                    │ wlan0: AP   │  ← SSID: JamShield-AP ch6
                    │ wlan1: mon  │  ← USB dongle, monitor mode
                    └──────┬──────┘
                           │ WiFi (primary)
                    ┌──────▼──────┐
                    │   ESP32 #1  │  ← JamShield firmware
                    │ Zephyr RTOS │
                    └─────────────┘
                    
                    ┌─────────────┐
                    │   ESP32 #2  │  ← Jammer (ESP-IDF)
                    │ Beacon flood│
                    └─────────────┘
```

---

## 🎯 HOW TO ASK CLAUDE FOR HELP

When stuck, phrase questions like this:

**For build errors:**
> "I'm in Phase 3 of JamShield. `west build` gives this error: [paste error]. 
> I'm building for esp32_devkitc_wroom with Zephyr v3.6.0. 
> Here's my prj.conf: [paste]. Here's the relevant C file: [paste]."

**For Zephyr API questions:**
> "In JamShield jam_detect.c, I need to get WiFi RSSI from the Zephyr API.
> I'm using CONFIG_WIFI_ESP32. What's the correct net_mgmt call and struct?"

**For RPi4 Python questions:**
> "In jamshield ble_receiver.py using bleak, how do I parse this 18-byte 
> binary notification payload: [paste struct definition]?"

**For experiment questions:**
> "I've completed Phase 7. Running Experiment 1 with 10 trials.
> The failover_latency column in my events table is NULL. 
> How do I compute it from the packets table timestamps?"

---

*CLAUDE.md v1.0 — JamShield Project*
*Last updated: June 2026*
*Follow phases in strict order. Do not skip phases.*
