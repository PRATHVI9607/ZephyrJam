#!/usr/bin/env python3
"""Tiny paramiko driver for the JamShield RPi4 (non-interactive).

Auth: prefers the ed25519 key (~/.ssh/id_ed25519); falls back to password from
env JS_RPI_PASS. Host/user from env JS_RPI_HOST / JS_RPI_USER.

Usage:
  python rpi.py run "<command>"
  python rpi.py putfile <local> <remote>
  python rpi.py putkey            # append local pubkey to authorized_keys
"""
import os
import sys
import paramiko

# Windows consoles default to cp1252 and choke on UTF-8 output from the Pi.
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

HOST = os.environ.get("JS_RPI_HOST", "10.88.34.137")
USER = os.environ.get("JS_RPI_USER", "prathvi")
PASS = os.environ.get("JS_RPI_PASS")
KEY = os.path.expanduser("~/.ssh/id_ed25519")
PUB = os.path.expanduser("~/.ssh/id_ed25519.pub")


def connect(force_password=False):
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    base = dict(hostname=HOST, username=USER, timeout=20,
                allow_agent=False, look_for_keys=False)
    if not force_password and os.path.exists(KEY):
        try:
            pkey = paramiko.Ed25519Key.from_private_key_file(KEY)
            c.connect(pkey=pkey, **base)
            return c
        except Exception:
            pass
    if PASS:
        c.connect(password=PASS, **base)
        return c
    sys.exit("ERROR: no usable key and JS_RPI_PASS not set")


def run(cmd):
    """Run a remote command and STREAM its output live (works for long-running
    commands like the demo dashboard, not just ones that exit)."""
    c = connect()
    chan = c.get_transport().open_session()
    chan.set_combine_stderr(True)
    chan.exec_command(cmd)
    try:
        while True:
            data = chan.recv(4096)
            if not data:
                break
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()
    except KeyboardInterrupt:
        c.close()
        sys.exit(0)
    rc = chan.recv_exit_status()
    c.close()
    sys.exit(rc)


def putfile(local, remote):
    c = connect()
    sftp = c.open_sftp()
    sftp.put(local, remote)
    sftp.close()
    c.close()
    print(f"put {local} -> {remote}")


def sudobash(remote_script):
    """Run a remote script as root via `sudo -S`, feeding the password on
    stdin (kept off the command line). Streams output."""
    if not PASS:
        sys.exit("ERROR: JS_RPI_PASS not set (needed for sudo)")
    c = connect()
    chan = c.get_transport().open_session()
    chan.exec_command(f"sudo -S -p '' bash {remote_script}")
    chan.sendall(PASS + "\n")
    chan.shutdown_write()
    buf = b""
    while True:
        if chan.recv_ready():
            data = chan.recv(4096)
            if not data:
                break
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()
        if chan.recv_stderr_ready():
            sys.stderr.write(chan.recv_stderr(4096).decode(errors="replace"))
        if chan.exit_status_ready() and not chan.recv_ready():
            break
    rc = chan.recv_exit_status()
    # drain remainder
    while chan.recv_ready():
        sys.stdout.write(chan.recv(4096).decode(errors="replace"))
    c.close()
    sys.exit(rc)


def putkey():
    pub = open(PUB).read().strip()
    c = connect(force_password=True)
    setup = ("mkdir -p ~/.ssh && chmod 700 ~/.ssh && "
             "touch ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys")
    c.exec_command(setup)[1].channel.recv_exit_status()
    chk = (f"grep -qF '{pub}' ~/.ssh/authorized_keys && echo PRESENT || "
           f"(echo '{pub}' >> ~/.ssh/authorized_keys && echo ADDED)")
    _, out, _ = c.exec_command(chk)
    print(out.read().decode().strip())
    c.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "run":
        run(sys.argv[2])
    elif cmd == "putfile":
        putfile(sys.argv[2], sys.argv[3])
    elif cmd == "putkey":
        putkey()
    elif cmd == "sudobash":
        sudobash(sys.argv[2])
    else:
        sys.exit(f"unknown subcommand {cmd}")
