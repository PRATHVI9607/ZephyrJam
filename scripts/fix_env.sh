#!/usr/bin/env bash
# Repair the corrupted JAMSHIELD_ENV block in ~/.bashrc and install a clean
# env file that ~/.bashrc sources.
set -e

# Drop the previous JAMSHIELD_ENV comment + the 4 broken export lines.
sed -i '/# JAMSHIELD_ENV/,+4d' ~/.bashrc 2>/dev/null || true
# Drop any stale source line and the broken C:\Users export lines.
sed -i '\#jamshield_env.sh#d' ~/.bashrc 2>/dev/null || true
sed -i '\#C:.Users#d'         ~/.bashrc 2>/dev/null || true

# Install clean env file (normalize CRLF).
tr -d '\r' < /mnt/c/Workspace/IotELL/scripts/jamshield_env.sh > "$HOME/jamshield_env.sh"
chmod +x "$HOME/jamshield_env.sh"

printf '\n# JAMSHIELD_ENV\nsource "$HOME/jamshield_env.sh"\n' >> ~/.bashrc

echo "===== bashrc tail after fix ====="
tail -6 ~/.bashrc
echo "===== resolved env (login shell) ====="
bash -lc 'echo ZEPHYR_BASE=$ZEPHYR_BASE; echo SDK=$ZEPHYR_SDK_INSTALL_DIR; command -v west'
echo "fix-env-done"
