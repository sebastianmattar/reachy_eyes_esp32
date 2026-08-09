#!/usr/bin/env bash
# Build the ESP32 eyes firmware and push it to a connected board over serial OTA.
#
#   build_and_push.sh /dev/cu.usbmodemXXXX [extra ota_update.py args...]
#
# Runs `pio run -e upesy_wroom` then `ota_update.py`. The OTA step only pushes if
# the freshly built firmware is newer than what the board runs (pass --force to
# override). Needs PlatformIO on PATH and pyserial in the python3 you run this
# with (e.g. an activated .venv).
set -euo pipefail

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
    echo "usage: $0 /dev/cu.usbmodemXXXX [extra ota_update.py args...]" >&2
    exit 1
fi
shift

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # python
ESP32_DIR="$(dirname "$HERE")/esp32"                    # esp32

# Locate the PlatformIO CLI (PATH, then the default install location).
PIO="$(command -v pio || true)"
if [[ -z "$PIO" && -x "$HOME/.platformio/penv/bin/pio" ]]; then
    PIO="$HOME/.platformio/penv/bin/pio"
fi
if [[ -z "$PIO" ]]; then
    echo "PlatformIO 'pio' not found on PATH or ~/.platformio/penv/bin." >&2
    exit 1
fi

echo "==> Building firmware (upesy_wroom)..."
"$PIO" run -e upesy_wroom -d "$ESP32_DIR"

echo "==> Pushing to $PORT over serial OTA..."
python3 "$HERE/ota_update.py" --port "$PORT" "$@"
