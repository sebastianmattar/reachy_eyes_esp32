"""Firmware image discovery and version comparison for the serial OTA path.

``FIRMWARE_VERSION`` in ``esp32/include/config.h`` is the single source of truth
for what a locally built ``firmware.bin`` contains; the device reports its own
version over the ``VERSION`` command. The OTA push is gated on comparing the two.
"""

from __future__ import annotations

import re
from pathlib import Path

# python/reachy_eyes/ → python/ → repo root → esp32/
_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
_ESP32_DIR = _REPO_ROOT / "esp32"

DEFAULT_FIRMWARE = _ESP32_DIR / ".pio" / "build" / "upesy_wroom" / "firmware.bin"
DEFAULT_CONFIG = _ESP32_DIR / "include" / "config.h"


def parse_config_version(config_path: Path = DEFAULT_CONFIG) -> str:
    """Read ``FIRMWARE_VERSION "x.y.z"`` from config.h."""
    text = Path(config_path).read_text()
    m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', text)
    if not m:
        raise ValueError(f"FIRMWARE_VERSION not found in {config_path}")
    return m.group(1)


def semver(s: str) -> tuple[int, ...]:
    """Loose semver → comparable tuple; non-numeric parts fall back to 0."""
    return tuple(int(p) if p.isdigit() else 0 for p in s.strip().split("."))


def is_newer(candidate: str, installed: str) -> bool:
    """True if ``candidate`` is a strictly newer version than ``installed``."""
    return semver(candidate) > semver(installed)


def load_firmware(path: Path | str | None = None) -> tuple[Path, bytes]:
    """Read a firmware image, defaulting to the PlatformIO build output."""
    path = Path(path) if path is not None else DEFAULT_FIRMWARE
    if not path.exists():
        raise FileNotFoundError(
            f"firmware not found: {path}  (build it: pio run -e upesy_wroom)"
        )
    return path, path.read_bytes()
