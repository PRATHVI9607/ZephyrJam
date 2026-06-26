#!/usr/bin/env python3
"""JamShield — unified multi-channel receiver (PRD.md Section 10.3).

Runs three receivers concurrently and funnels every packet into one SQLite DB
via a background writer:

  * WiFi   : MQTT subscribe on localhost:1883 (Mosquitto)        -> channel WIFI
  * BLE    : bleak GATT notifications from "JamShield"            -> channel BLE
  * ESPNOW : scapy sniff of vendor action frames on a monitor IF -> channel ESPNOW

The WiFi/BLE receivers run in the asyncio loop; ESP-NOW (scapy, blocking) runs
in its own thread. Channel transitions are inferred from the channel field and
logged to the events table as FAILOVER markers.

Usage:
  python3 receiver.py [--db PATH] [--mqtt-host H] [--espnow-iface wlan1]
                      [--no-ble] [--no-espnow]
"""
from __future__ import annotations

import argparse
import asyncio
import json
import struct
import threading
import time

from database import DBWriter

JAMSHIELD_BLE_NAME = "JamShield"
SENSOR_CHAR_UUID = "00001235-0000-1000-8000-00805f9b34fb"
JAM_STATES = ["CLEAR", "SUSPECTED", "CONFIRMED", "RECOVERING"]

# 18-byte payload: uint32 seq, uint64 ts, u8 ch, u16 ldr, i8 rssi, u8 jam, u8 cpu
BIN_FMT = "<IQBHbBB"
BIN_LEN = struct.calcsize(BIN_FMT)  # 18


class ChannelTracker:
    """Detects channel changes and emits FAILOVER events with latency."""

    def __init__(self, writer: DBWriter):
        self.writer = writer
        self.current: str | None = None
        self.last_pkt_ts: float | None = None
        self.lock = threading.Lock()

    def observe(self, channel: str, recv_ts: float) -> None:
        with self.lock:
            if self.current is not None and channel != self.current:
                latency = (recv_ts - self.last_pkt_ts
                           if self.last_pkt_ts else None)
                self.writer.put({
                    "__event__": True, "event_type": "FAILOVER",
                    "from_ch": self.current, "to_ch": channel,
                    "latency_ms": latency,
                    "detail": "inferred from channel change",
                })
                print(f"[event] FAILOVER {self.current} -> {channel} "
                      f"({latency:.1f} ms)" if latency else
                      f"[event] FAILOVER {self.current} -> {channel}")
            self.current = channel
            self.last_pkt_ts = recv_ts


def parse_binary(data: bytes, recv_ts: float) -> dict | None:
    if len(data) < BIN_LEN:
        return None
    seq, ts_ms, ch, ldr_adc, rssi, jam, cpu = struct.unpack(BIN_FMT,
                                                            data[:BIN_LEN])
    return {
        "recv_ts": recv_ts, "esp_ts": ts_ms, "seq": seq,
        "ldr_adc": ldr_adc, "rssi": rssi, "cpu_util": cpu,
        "jam_state": JAM_STATES[jam] if jam < len(JAM_STATES) else str(jam),
    }


# ───────────────────────── WiFi / MQTT ─────────────────────────
def start_wifi(writer: DBWriter, tracker: ChannelTracker, host: str) -> None:
    import paho.mqtt.client as mqtt

    def on_connect(client, userdata, flags, rc, *a):
        print(f"[wifi] MQTT connected rc={rc}")
        client.subscribe("jamshield/sensor/ldr")
        client.subscribe("jamshield/sensor/espnow")   # node's ESP-NOW mirror
        client.subscribe("jamshield/events/#")

    def on_message(client, userdata, msg):
        recv_ts = time.time() * 1000.0
        try:
            p = json.loads(msg.payload.decode())
        except (json.JSONDecodeError, UnicodeDecodeError):
            print("[wifi] bad JSON")
            return
        if p.get("event") == "FAILOVER":
            writer.put({"__event__": True, "event_type": "FAILOVER",
                        "from_ch": p.get("from"), "to_ch": p.get("to"),
                        "detail": "esp32 meta"})
            return
        ch = "ESPNOW" if msg.topic.endswith("espnow") else "WIFI"
        row = {
            "recv_ts": recv_ts, "esp_ts": p.get("ts_ms"), "seq": p.get("seq"),
            "channel": ch, "ldr_adc": p.get("ldr_adc"),
            "ldr_lux": p.get("ldr_lux"), "rssi": p.get("rssi"),
            "jam_state": p.get("jam_state"), "cpu_util": p.get("cpu_util"),
            "free_heap": p.get("free_heap"),
        }
        tracker.observe(ch, recv_ts)
        writer.put(row)
        publish_feed(row)

    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    except (AttributeError, TypeError):
        client = mqtt.Client()  # paho < 2.0 fallback
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(host, 1883, 60)
    client.loop_start()
    print(f"[wifi] subscriber started on {host}:1883")


# ───────────────────────── BLE ─────────────────────────
async def run_ble(writer: DBWriter, tracker: ChannelTracker) -> None:
    from bleak import BleakClient, BleakScanner

    def handler(_sender, data: bytearray):
        recv_ts = time.time() * 1000.0
        row = parse_binary(bytes(data), recv_ts)
        if row:
            row["channel"] = "BLE"
            tracker.observe("BLE", recv_ts)
            writer.put(row)
            publish_feed(row)

    print("[ble] scanning for JamShield...")
    while True:
        try:
            dev = await BleakScanner.find_device_by_name(JAMSHIELD_BLE_NAME,
                                                         timeout=10.0)
            if not dev:
                await asyncio.sleep(2)
                continue
            async with BleakClient(dev) as client:
                print(f"[ble] connected {dev.address}")
                await client.start_notify(SENSOR_CHAR_UUID, handler)
                while client.is_connected:
                    await asyncio.sleep(0.2)
            print("[ble] disconnected, rescanning")
        except Exception as exc:
            print(f"[ble] error: {exc}")
            await asyncio.sleep(2)


# ───────────────────────── ESP-NOW ─────────────────────────
def run_espnow(writer: DBWriter, tracker: ChannelTracker, iface: str,
               esp_mac: str | None) -> None:
    try:
        from scapy.all import Dot11, sniff  # noqa: F401
    except Exception as exc:
        print(f"[espnow] scapy unavailable, disabled: {exc}")
        return

    from scapy.all import Dot11, sniff

    def handler(pkt):
        if not pkt.haslayer(Dot11):
            return
        src = pkt.addr2
        if esp_mac and (src or "").lower() != esp_mac.lower():
            return
        raw = bytes(pkt.payload)
        # ESP-NOW vendor frames carry the 18-byte payload after the action hdr;
        # scan for a plausible aligned struct.
        for off in range(0, max(1, len(raw) - BIN_LEN + 1)):
            row = parse_binary(raw[off:off + BIN_LEN], time.time() * 1000.0)
            if row and 0 <= row["ldr_adc"] <= 4095:
                row["channel"] = "ESPNOW"
                tracker.observe("ESPNOW", row["recv_ts"])
                writer.put(row)
                publish_feed(row)
                return

    print(f"[espnow] sniffing on {iface} (monitor mode required)")
    try:
        sniff(iface=iface, prn=handler, store=False)
    except Exception as exc:
        print(f"[espnow] sniff failed (is {iface} in monitor mode?): {exc}")


# ───────────────────────── unified feed (for the PWA) ─────────────────────────
_feed_pub = None


def init_feed(host: str) -> None:
    """Mirror every received packet (WiFi/BLE/ESP-NOW) to one MQTT topic,
    jamshield/feed, which the PWA subscribes to over WebSockets."""
    global _feed_pub
    try:
        import paho.mqtt.client as mqtt
        try:
            c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        except (AttributeError, TypeError):
            c = mqtt.Client()
        c.connect(host, 1883, 60)
        c.loop_start()
        _feed_pub = c
        print("[feed] republishing to jamshield/feed")
    except Exception as exc:
        print(f"[feed] disabled: {exc}")


def publish_feed(row: dict) -> None:
    if not _feed_pub:
        return
    try:
        _feed_pub.publish("jamshield/feed", json.dumps({
            "seq": row.get("seq"), "channel": row.get("channel"),
            "val": row.get("ldr_adc"), "rssi": row.get("rssi"),
            "jam_state": row.get("jam_state"),
        }))
    except Exception:
        pass


# ───────────────────────── main ─────────────────────────
async def amain(args) -> None:
    writer = DBWriter(args.db)
    writer.start()
    tracker = ChannelTracker(writer)
    init_feed(args.mqtt_host)
    print(f"[main] logging to {args.db}")

    start_wifi(writer, tracker, args.mqtt_host)

    if not args.no_espnow:
        threading.Thread(target=run_espnow,
                         args=(writer, tracker, args.espnow_iface, args.esp_mac),
                         daemon=True).start()

    tasks = []
    if not args.no_ble:
        tasks.append(asyncio.create_task(run_ble(writer, tracker)))

    if tasks:
        await asyncio.gather(*tasks)
    else:
        while True:
            await asyncio.sleep(3600)


def main() -> None:
    ap = argparse.ArgumentParser(description="JamShield unified receiver")
    ap.add_argument("--db", default=None)
    ap.add_argument("--mqtt-host", default="localhost")
    ap.add_argument("--espnow-iface", default="wlan1")
    ap.add_argument("--esp-mac", default=None,
                    help="ESP32 sender MAC to filter ESP-NOW frames")
    ap.add_argument("--no-ble", action="store_true")
    ap.add_argument("--no-espnow", action="store_true")
    args = ap.parse_args()
    if args.db is None:
        from database import DEFAULT_DB_PATH
        args.db = DEFAULT_DB_PATH
    try:
        asyncio.run(amain(args))
    except KeyboardInterrupt:
        print("\n[main] shutting down")


if __name__ == "__main__":
    main()
