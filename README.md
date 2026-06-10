# JamShield

**Jamming-resilient IoT sensor node with automatic protocol hopping.**
ESP32 DevKit V1 (Zephyr RTOS v3.6.0) + Raspberry Pi 4 receiver.

An ESP32 reads an LDR light sensor and streams it over **WiFi/MQTT** (primary).
When it detects WiFi jamming, it fails over automatically to **BLE GATT**
(secondary) and **ESP-NOW** (tertiary), then restores WiFi when the jam clears.
A Raspberry Pi 4 receives on all three channels and logs everything to SQLite
for analysis.

See [PRD.md](PRD.md) for the full design and [CLAUDE.md](CLAUDE.md) for the
phase plan.

---

## Status

| Component | State |
|---|---|
| WSL2 Zephyr toolchain (west, SDK 0.16.8, xtensa, RF blobs) | ✅ installed |
| ESP32 firmware (LDR, WiFi+MQTT, jam-detect, BLE, bearer FSM, payload) | ✅ **builds clean → `zephyr.bin`** |
| ESP-NOW tertiary bearer | ⚙️ written, gated `CONFIG_JS_ESPNOW=n` (see notes) |
| RPi4 receiver stack (MQTT + BLE + ESP-NOW → SQLite) | ✅ written |
| RPi4 setup script (hostapd AP, Mosquitto, dnsmasq, venv) | ✅ written |
| Flashing to hardware | ⏸ needs ESP32 connected (`usbipd` installed) |
| RPi4 live test, jammer, experiments, paper | ⏸ needs hardware |

The firmware compiles with **zero errors**. Build artifact:
`~/jamshield_build/esp32/zephyr/zephyr.bin`. ESP32 RAM usage is ~92–93% of
DRAM (WiFi + BLE + net + mbedTLS coexisting), which is valid but tight.

---

## Layout

```
src/esp32/      Zephyr application (C) — the firmware
  include/      module headers
  src/          main.c, sensor_ldr.c, jam_detect.c, adaptive_threshold.c,
                wifi_mqtt.c, ble_gatt.c, espnow_l2.c, conn_mgr_setup.c,
                payload_thread.c
  boards/       esp32_devkitc_wroom.overlay (LDR on GPIO34/ADC1_CH6, LED, wifi)
  prj.conf      Kconfig    Kconfig (CONFIG_JS_ESPNOW)    CMakeLists.txt   west.yml
src/rpi4/       Python receiver/logger (receiver.py, database.py, ble_receiver.py,
                espnow_receiver.py, jammer_control.py, setup_rpi4.sh, requirements.txt)
scripts/        build.sh, flash.sh, monitor.sh, and one-time bootstrap scripts
.vscode/        build / flash / monitor tasks (invoke WSL from Windows VSCode)
```

## Build environment

Source lives on Windows (`c:\Workspace\IotELL`, presentable & VSCode-editable);
the build runs inside **WSL2 Ubuntu** against `~/jamshield_workspace` (Zephyr
v3.6.0) with a fast WSL-native build dir `~/jamshield_build/esp32`. Env vars are
in `~/jamshield_env.sh` (sourced from `~/.bashrc`).

### Build
```powershell
# From Windows (PowerShell) — or use VSCode task "Build JamShield ESP32"
wsl -d Ubuntu -- bash -lc "bash ~/jamshield_bootstrap/build.sh auto"
```
The bootstrap scripts under `scripts/` were used once to install the toolchain;
`build.sh` is the everyday command (`auto` = incremental, `always` = pristine).

## Flashing (when the ESP32 is connected)

1. Plug the ESP32 into USB.
2. In **Windows PowerShell (Admin)**:
   ```powershell
   usbipd list                      # find the "CP210x" / "CH340" bus id, e.g. 2-4
   usbipd bind   --busid <id>        # once per device
   usbipd attach --wsl --busid <id>  # makes /dev/ttyUSB0 appear in WSL
   ```
3. Flash + monitor (VSCode tasks "Flash ESP32" / "Monitor Serial", or):
   ```powershell
   wsl -d Ubuntu -- bash -lc "tr -d '\r' < /mnt/c/Workspace/IotELL/scripts/flash.sh | bash"
   wsl -d Ubuntu -- bash -lc "tr -d '\r' < /mnt/c/Workspace/IotELL/scripts/monitor.sh | bash"
   ```
   On first WSL use you may need `sudo usermod -aG dialout $USER` then reopen WSL.

Expected serial output: `==== JamShield starting ====`, LDR readings each
second, WiFi/MQTT connect logs, and `FAILOVER`/state messages under jamming.

## Raspberry Pi 4

```bash
# copy src/rpi4 to the Pi, then:
chmod +x setup_rpi4.sh && ./setup_rpi4.sh      # AP + Mosquitto + venv (reboot after)
source ~/jamshield_env/bin/activate
python3 receiver.py                            # all channels -> data/jamshield.db
```

---

## Engineering notes (deviations from the PRD draft, and why)

These are deliberate, documented choices so the system actually builds and runs
on real Zephyr 3.6 — the PRD's C listings are illustrative pseudocode.

- **Application-level bearer manager instead of custom `conn_mgr` bearers.**
  Zephyr's `conn_mgr` only abstracts `net_if`-backed L2s (WiFi here). BLE GATT
  and ESP-NOW are not network interfaces, so `conn_mgr_setup.c` implements the
  WiFi→BLE→ESP-NOW failover as a clean app-level FSM with one `send()` entry
  point — exactly what the PRD's own payload-thread dispatch implies. WiFi L4
  health still comes from `conn_mgr`/`net_mgmt` events.
- **ESP-NOW gated behind `CONFIG_JS_ESPNOW` (default off).** The Espressif
  `esp_now.h` HAL *is* present in this port and `libespnow.a` is fetched, so the
  real implementation in `espnow_l2.c` can be enabled — but DRAM is already at
  ~93%, so turning it on needs further RAM trimming first. With it off, the
  module is a compile-safe stub and WiFi↔BLE failover is fully functional.
- **Static heaps trimmed** (`HEAP_MEM_POOL_SIZE`, `MBEDTLS_HEAP_SIZE`, net
  buffers) to fit ESP32 DRAM with WiFi+BLE coexisting.
- **Integer-only math in all threads** (fixed-point lux ×10), no `malloc` in
  threads, sequence numbers + `k_uptime_get()` timestamps on every packet — per
  the CLAUDE.md hard rules.
- **`dtc` not installed** (non-fatal — Zephyr uses its own DTS parser).
```
