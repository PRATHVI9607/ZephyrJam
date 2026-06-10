"""JamShield — SQLite logging layer (PRD.md Sections 10.4, 15.1).

A single writer thread owns the connection; receivers push dict rows onto a
queue. SQLite is fine for our packet rates (~6 rows/s) and keeps the whole
experiment in one portable file.
"""
from __future__ import annotations

import queue
import sqlite3
import threading
import time
from pathlib import Path
from typing import Optional

DEFAULT_DB_PATH = str(Path.home() / "jamshield" / "data" / "jamshield.db")

SCHEMA = """
CREATE TABLE IF NOT EXISTS packets (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    recv_ts     REAL NOT NULL,
    esp_ts      INTEGER,
    seq         INTEGER,
    channel     TEXT NOT NULL CHECK(channel IN ('WIFI','BLE','ESPNOW')),
    ldr_adc     INTEGER,
    ldr_lux     REAL,
    rssi        INTEGER,
    jam_state   TEXT,
    cpu_util    INTEGER,
    free_heap   INTEGER,
    latency_ms  REAL
);

CREATE TABLE IF NOT EXISTS events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts          REAL NOT NULL,
    event_type  TEXT NOT NULL,
    from_ch     TEXT,
    to_ch       TEXT,
    latency_ms  REAL,
    detail      TEXT
);

CREATE TABLE IF NOT EXISTS experiment_runs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    start_ts        REAL,
    end_ts          REAL,
    experiment_type TEXT,
    jammer_dist_cm  INTEGER,
    cpu_load_pct    INTEGER,
    notes           TEXT
);

CREATE INDEX IF NOT EXISTS idx_packets_recv_ts ON packets(recv_ts);
CREATE INDEX IF NOT EXISTS idx_packets_channel ON packets(channel);

CREATE VIEW IF NOT EXISTS failover_events AS
    SELECT e.ts AS event_ts, e.latency_ms AS failover_latency_ms,
           e.from_ch, e.to_ch,
           (SELECT ldr_adc FROM packets
            WHERE recv_ts <= e.ts ORDER BY recv_ts DESC LIMIT 1) AS ldr_at_failover
    FROM events e WHERE e.event_type = 'FAILOVER';

CREATE VIEW IF NOT EXISTS packet_loss_by_channel AS
    SELECT channel,
           COUNT(*) AS total_packets,
           MAX(seq) - MIN(seq) + 1 AS expected_packets,
           (MAX(seq) - MIN(seq) + 1 - COUNT(*)) AS lost_packets
    FROM packets GROUP BY channel;
"""

_PACKET_COLS = ("recv_ts", "esp_ts", "seq", "channel", "ldr_adc", "ldr_lux",
                "rssi", "jam_state", "cpu_util", "free_heap", "latency_ms")


def init_db(db_path: str = DEFAULT_DB_PATH) -> sqlite3.Connection:
    Path(db_path).parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(db_path, check_same_thread=False)
    conn.executescript(SCHEMA)
    conn.commit()
    return conn


def insert_packet(conn: sqlite3.Connection, payload: dict) -> None:
    row = {c: payload.get(c) for c in _PACKET_COLS}
    if row["recv_ts"] is None:
        row["recv_ts"] = time.time() * 1000.0
    conn.execute(
        f"INSERT INTO packets ({','.join(_PACKET_COLS)}) "
        f"VALUES ({','.join('?' * len(_PACKET_COLS))})",
        [row[c] for c in _PACKET_COLS],
    )


def insert_event(conn: sqlite3.Connection, event_type: str,
                 from_ch: Optional[str] = None, to_ch: Optional[str] = None,
                 latency_ms: Optional[float] = None,
                 detail: Optional[str] = None) -> None:
    conn.execute(
        "INSERT INTO events (ts,event_type,from_ch,to_ch,latency_ms,detail) "
        "VALUES (?,?,?,?,?,?)",
        (time.time() * 1000.0, event_type, from_ch, to_ch, latency_ms, detail),
    )


class DBWriter(threading.Thread):
    """Background writer: receivers .put({...}) rows; this thread commits them.

    A row dict with key ``__event__`` is logged to the events table instead.
    """

    def __init__(self, db_path: str = DEFAULT_DB_PATH):
        super().__init__(daemon=True)
        self.db_path = db_path
        self.q: "queue.Queue[dict]" = queue.Queue()
        self._stop = threading.Event()

    def put(self, row: dict) -> None:
        self.q.put(row)

    def stop(self) -> None:
        self._stop.set()

    def run(self) -> None:
        conn = init_db(self.db_path)
        last_commit = time.time()
        while not self._stop.is_set():
            try:
                row = self.q.get(timeout=0.5)
            except queue.Empty:
                row = None
            if row is not None:
                try:
                    if row.get("__event__"):
                        insert_event(conn, row["event_type"],
                                     row.get("from_ch"), row.get("to_ch"),
                                     row.get("latency_ms"), row.get("detail"))
                    else:
                        insert_packet(conn, row)
                except Exception as exc:  # never let one bad row kill logging
                    print(f"[db] insert error: {exc}")
            if time.time() - last_commit > 0.5:
                conn.commit()
                last_commit = time.time()
        conn.commit()
        conn.close()
