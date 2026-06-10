#!/usr/bin/env bash
# Ensure ninja is available for the Zephyr build (no sudo). dtc is optional.
export PATH="$HOME/.local/bin:$PATH"

echo "before:"
printf '  ninja: '; command -v ninja || echo MISSING
printf '  dtc:   '; command -v dtc   || echo MISSING
printf '  cmake: '; command -v cmake || echo MISSING

if ! command -v ninja >/dev/null 2>&1; then
  echo "installing ninja via pip --user"
  pip3 install --user ninja
fi

echo "after:"
printf '  ninja: '; command -v ninja && ninja --version
printf '  cmake: '; command -v cmake && cmake --version | head -1
echo "fix-ninja-done"
