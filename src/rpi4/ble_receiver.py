#!/usr/bin/env python3
"""JamShield — standalone BLE receiver (PRD.md Section 5.5).

Connects to the "JamShield" peripheral, subscribes to characteristic 0x1235,
parses the 18-byte payload, and logs to SQLite with channel='BLE'. Useful for
isolating the BLE path during Phase 5 bring-up; receiver.py does all channels.

  python3 ble_receiver.py [--db PATH]
"""
from __future__ import annotations

import argparse
import asyncio
import struct
import time

from bleak import BleakClient, BleakScanner

from database import DEFAULT_DB_PATH, DBWriter

NAME = "JamShield"
CHAR_UUID = "00001235-0000-1000-8000-00805f9b34fb"
JAM_STATES = ["CLEAR", "SUSPECTED", "CONFIRMED", "RECOVERING"]
FMT = "<IQBHbBB"


async def main(db_path: str) -> None:
    writer = DBWriter(db_path)
    writer.start()
    print(f"[ble] logging to {db_path}")

    def handler(_s, data: bytearray):
        if len(data) < 18:
            return
        seq, ts, ch, ldr, rssi, jam, cpu = struct.unpack(FMT, bytes(data[:18]))
        writer.put({
            "recv_ts": time.time() * 1000.0, "esp_ts": ts, "seq": seq,
            "channel": "BLE", "ldr_adc": ldr, "rssi": rssi, "cpu_util": cpu,
            "jam_state": JAM_STATES[jam] if jam < 4 else str(jam),
        })
        print(f"[ble] seq={seq} ldr={ldr} rssi={rssi} jam={JAM_STATES[jam] if jam<4 else jam}")

    while True:
        print("[ble] scanning...")
        dev = await BleakScanner.find_device_by_name(NAME, timeout=10.0)
        if not dev:
            await asyncio.sleep(2)
            continue
        try:
            async with BleakClient(dev) as client:
                print(f"[ble] connected {dev.address}")
                await client.start_notify(CHAR_UUID, handler)
                while client.is_connected:
                    await asyncio.sleep(0.2)
        except Exception as exc:
            print(f"[ble] error: {exc}")
        await asyncio.sleep(2)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default=DEFAULT_DB_PATH)
    args = ap.parse_args()
    try:
        asyncio.run(main(args.db))
    except KeyboardInterrupt:
        pass
