#!/usr/bin/env python3
"""JamShield — standalone ESP-NOW receiver (PRD.md Section 6.4).

ESP-NOW frames are 802.11 vendor-specific action frames. Capturing them needs
a second WiFi interface in monitor mode (e.g. a USB dongle as wlan1):

  sudo iw dev wlan1 set type monitor
  sudo ip link set wlan1 up
  sudo python3 espnow_receiver.py --iface wlan1 [--esp-mac AA:BB:.. ] [--db PATH]

Parses the 18-byte payload and logs with channel='ESPNOW'.
"""
from __future__ import annotations

import argparse
import struct
import time

from scapy.all import Dot11, sniff

from database import DEFAULT_DB_PATH, DBWriter

JAM_STATES = ["CLEAR", "SUSPECTED", "CONFIRMED", "RECOVERING"]
FMT = "<IQBHbBB"
BIN_LEN = struct.calcsize(FMT)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", default="wlan1")
    ap.add_argument("--esp-mac", default=None,
                    help="ESP32 sender MAC to filter on")
    ap.add_argument("--db", default=DEFAULT_DB_PATH)
    args = ap.parse_args()

    writer = DBWriter(args.db)
    writer.start()
    print(f"[espnow] sniffing {args.iface}; logging to {args.db}")

    def handler(pkt):
        if not pkt.haslayer(Dot11):
            return
        src = pkt.addr2
        if args.esp_mac and (src or "").lower() != args.esp_mac.lower():
            return
        raw = bytes(pkt.payload)
        for off in range(0, max(1, len(raw) - BIN_LEN + 1)):
            chunk = raw[off:off + BIN_LEN]
            if len(chunk) < BIN_LEN:
                break
            seq, ts, ch, ldr, rssi, jam, cpu = struct.unpack(FMT, chunk)
            if 0 <= ldr <= 4095 and jam < 4:
                writer.put({
                    "recv_ts": time.time() * 1000.0, "esp_ts": ts, "seq": seq,
                    "channel": "ESPNOW", "ldr_adc": ldr, "rssi": rssi,
                    "cpu_util": cpu, "jam_state": JAM_STATES[jam],
                })
                print(f"[espnow] seq={seq} ldr={ldr} rssi={rssi} from={src}")
                return

    try:
        sniff(iface=args.iface, prn=handler, store=False)
    except PermissionError:
        print("[espnow] need root: sudo python3 espnow_receiver.py")
    except Exception as exc:
        print(f"[espnow] sniff error (is {args.iface} in monitor mode?): {exc}")


if __name__ == "__main__":
    main()
