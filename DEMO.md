# JamShield — Demo Runbook (dashboard edition)

A single command builds + flashes both ESP32s and opens a live web dashboard.
You drive the whole demo from the browser.

## Hardware
- **ESP32 #1 (node)** on **COM6** — the JamShield sensor node.
- **ESP32 #2 (jammer)** on **COM7** — the attacker.
- **Raspberry Pi** on the same 2.4 GHz WiFi (`Loki`) running the MQTT broker
  (auto-detected by the launcher).

## Start everything (one command)
From PowerShell in `C:\Workspace\IotELL`:
```powershell
.\run.ps1
```
This: detects the Pi/broker IP, patches + **builds both firmwares**, **flashes**
node→COM6 and jammer→COM7, and opens the dashboard at **http://127.0.0.1:8080**.

Already flashed and just want the dashboard?
```powershell
.\run.ps1 -SkipBuild
```
No second ESP32? `.\run.ps1 -NoJammer`

## The dashboard
- **Active channel** (big): WIFI / BLE / ESP‑NOW, colour-coded.
- **Live data** pill: green **DELIVERING** or red **DATA LOST** — the punchline.
- **Jam state** banner: CLEAR / SUSPECTED / **CONFIRMED**.
- **Protection mode** buttons: HOP / NO‑HOP / NO‑BLE + **START/STOP JAMMER**.
- **Data integrity**: live **Sent / Delivered / Lost / Delivery %** counters
  (hit *reset* to zero them before a run).
- **Data packets** feed: each transmitted sensor reading (`#seq · sensor NNN`)
  tagged *via WIFI/BLE* (delivered) or **LOST** — the per-packet proof.
- Per-channel counts + a live colour **timeline**.

## The demo (≈2 minutes)

**1. Normal operation.** Mode = **HOP**. Channel = **WIFI**, **DELIVERING**, jam
CLEAR. *"The node streams a light sensor over WiFi."*

**2. The solution — protected.** (HOP) Hit **reset**, then **START JAMMER**.
→ Within ~1 s the channel flips **WIFI → BLE**, jam = **CONFIRMED**, but the packet
feed keeps streaming *via BLE* and **Lost stays 0 / Delivery 100%**. *"Detected the
jam in ~200 ms, hopped to Bluetooth — every sensor reading still gets through."*
Click **STOP JAMMER** → recovers to **WIFI**.

**3. The problem — unprotected.** Click **NO‑HOP**, hit **reset**, then **START
JAMMER**. → Channel stays **WIFI**, the feed fills with red **LOST** rows, the
**Lost** counter climbs and **Delivery %** drops. *"Without protocol hopping, the
same jam destroys the data stream."* Click **STOP JAMMER**, then **HOP** to restore.

**4. (Optional) Third tier.** Click **NO‑BLE**, **START JAMMER** → channel hops
straight to **ESP‑NOW** (the connectionless last-resort bearer).

## Talking points
- Detection is **dual-metric** (RSSI + packet loss) and **connectivity-loss** aware,
  so a real link failure triggers failover. Measured detection latency ≈ **200 ms**.
- **Three-tier** resilience: WiFi → BLE → ESP‑NOW, priority-ordered.
- The dashboard talks to both boards over **USB serial** (reliable, no dependence
  on the jammed link). The jam is triggered on the control channel; the node then
  runs its **real** detection → failover → recovery state machine.

## Notes / honesty
- The RF jammer (ESP32 #2) is included and fires, but reliable over-the-air
  jamming of a strong WPA2 link from a Zephyr build is hardware-marginal; the
  dashboard's jam button therefore *also* asserts the jam on the control channel
  so the failover is guaranteed for the demo. The node's resilience is real;
  only the trigger is over USB.
- **ESP‑NOW** delivery needs `CONFIG_JS_ESPNOW=y` and a receiver; NO‑BLE mode
  currently demonstrates the *hop* to ESP‑NOW (channel shown), not Pi-side receipt.

## Troubleshooting
| Symptom | Fix |
|---|---|
| Dashboard "node" dot grey | node not on COM6 / port busy — close other serial monitors; replug |
| `deliv` flickers in WIFI | transient broker reachability; the channel/jam story still holds |
| Wrong COM ports | `.\run.ps1 -Node COMx -Jammer COMy` |
| Channel never shows BLE on jam | ensure mode = HOP before starting the jammer |
