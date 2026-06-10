#!/usr/bin/env bash
# JamShield — Zephyr workspace bootstrap (no sudo required).
# Installs west (pip --user), inits the Zephyr v3.6.0 workspace, and updates it.
set -e
export PATH="$HOME/.local/bin:$PATH"
WS="$HOME/jamshield_workspace"

echo "[west_setup] $(date) :: installing west via pip --user"
pip3 install --user --upgrade west

WEST="$HOME/.local/bin/west"
echo "[west_setup] west version: $($WEST --version)"

if [ ! -d "$WS/.west" ]; then
  echo "[west_setup] west init (zephyr v3.6.0) at $WS"
  mkdir -p "$WS"
  cd "$WS"
  "$WEST" init -m https://github.com/zephyrproject-rtos/zephyr --mr v3.6.0
else
  echo "[west_setup] workspace already initialized, skipping init"
fi

cd "$WS"
echo "[west_setup] west update (long: clones zephyr + modules)"
"$WEST" update

echo "[west_setup] west zephyr-export"
"$WEST" zephyr-export

echo "[west_setup] installing zephyr python requirements (--user)"
pip3 install --user -r "$WS/zephyr/scripts/requirements.txt"
pip3 install --user esptool || true

echo "[west_setup] DONE $(date)"
