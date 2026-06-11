import os
import paramiko

HOST = os.environ.get("JS_RPI_HOST", "10.88.34.137")
USER = os.environ.get("JS_RPI_USER", "prathvi")
K = os.path.expanduser("~/.ssh/id_ed25519")
PASS = os.environ.get("JS_RPI_PASS")

pk = None
try:
    pk = paramiko.Ed25519Key.from_private_key_file(K)
    print("KEY LOADED ok:", pk.get_name())
except Exception as e:
    print("KEY LOAD FAIL:", repr(e))

if pk:
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        c.connect(HOST, 22, USER, pkey=pk, allow_agent=False,
                  look_for_keys=False, timeout=15)
        print("KEY CONNECT OK")
        c.close()
    except Exception as e:
        print("KEY CONNECT FAIL:", repr(e))

if PASS:
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(HOST, 22, USER, password=PASS, allow_agent=False,
              look_for_keys=False, timeout=15)
    _, o, _ = c.exec_command(
        "ls -ld ~ ~/.ssh ~/.ssh/authorized_keys; echo '--- keys ---'; "
        "cat ~/.ssh/authorized_keys; echo '--- sshd ---'; "
        "sudo -n grep -RiE 'PubkeyAuth|AuthorizedKeysFile' /etc/ssh/ 2>/dev/null "
        "|| grep -RiE 'PubkeyAuth|AuthorizedKeysFile' /etc/ssh/sshd_config 2>/dev/null")
    print(o.read().decode())
    c.close()
