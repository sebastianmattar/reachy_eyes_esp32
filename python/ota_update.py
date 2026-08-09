#!/usr/bin/env python3
"""Serial OTA updater for the Reachy eyes ESP32 — version handshake + push.

The running firmware writes the new image into the inactive OTA slot itself
(via the ESP32 Update API) and reboots into it — so this needs no bootloader
reset, no esptool, and no BOOT/EN buttons. It reuses the same USB serial link the
eye protocol already uses, which is why it works even when the board is buried in
the robot and auto-reset flashing fails.

Protocol (see serial_protocol.h):
    host → VERSION\n
    esp  → VERSION <x.y.z>\n
    host → FWUPDATE <byteLen> <crc32>\n
    esp  → READY\n              (after erasing the target slot)
    host → <byteLen raw bytes>
    esp  → OK <crc32>\n         (then reboots) | ERR <reason>\n

Version policy: the firmware.bin's version is read from config.h's
FIRMWARE_VERSION (single source of truth), compared to the device's reported
version, and the image is pushed only if it's newer (unless --force).

Usage:
    python3 ota_update.py --port /dev/cu.usbmodemXXXX                 # auto: build path + config version
    python3 ota_update.py --port /dev/cu.usbmodemXXXX --check-only    # just print versions
    python3 ota_update.py --port /dev/cu.usbmodemXXXX fw.bin --force  # push regardless

Requires: pyserial  (pip install pyserial)
"""
from __future__ import annotations

import argparse
import re
import sys
import time
import zlib
from pathlib import Path

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial not installed — run: pip install pyserial")

_HERE = Path(__file__).resolve().parent  # python/
_ESP32_DIR = _HERE.parent / "esp32"
DEFAULT_FIRMWARE = _ESP32_DIR / ".pio" / "build" / "upesy_wroom" / "firmware.bin"
DEFAULT_CONFIG = _ESP32_DIR / "include" / "config.h"


# ── serial line helpers (same framing as upload_gif.py) ──────────────────────
def read_line(ser: "serial.Serial", timeout: float = 5.0) -> str | None:
    """Read one LF-terminated line as text within a wall-clock timeout."""
    deadline = time.time() + timeout
    buf = bytearray()
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        if b == b"\n":
            return buf.decode(errors="replace").strip()
        if b != b"\r":
            buf += b
    return None


def send_cmd(ser: "serial.Serial", cmd: str) -> None:
    ser.write((cmd + "\n").encode())
    ser.flush()


# ── version handling ─────────────────────────────────────────────────────────
def parse_config_version(config_path: Path) -> str:
    """Read FIRMWARE_VERSION "x.y.z" from config.h."""
    text = config_path.read_text()
    m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', text)
    if not m:
        raise ValueError(f"FIRMWARE_VERSION not found in {config_path}")
    return m.group(1)


def semver(s: str) -> tuple[int, ...]:
    """Loose semver → comparable tuple; non-numeric parts fall back to 0."""
    return tuple(int(p) if p.isdigit() else 0 for p in s.strip().split("."))


def query_version(ser: "serial.Serial", timeout: float = 5.0) -> str | None:
    """Ask the device for its firmware version; None if it doesn't answer."""
    ser.reset_input_buffer()
    send_cmd(ser, "VERSION")
    reply = read_line(ser, timeout)
    if reply and reply.startswith("VERSION"):
        parts = reply.split()
        return parts[1] if len(parts) > 1 else ""
    return None


# ── firmware push ────────────────────────────────────────────────────────────
def push_firmware(ser: "serial.Serial", data: bytes, chunk: int = 512) -> None:
    """Stream `data` into the device's OTA slot. Raises RuntimeError on failure."""
    n = len(data)
    crc = zlib.crc32(data) & 0xFFFFFFFF
    print(f"Pushing firmware: {n} bytes, crc32={crc:08X}")

    ser.reset_input_buffer()
    send_cmd(ser, f"FWUPDATE {n} {crc:08x}")
    # Update.begin() erases the target slot first — that can take a few seconds.
    reply = read_line(ser, timeout=15.0)
    if reply != "READY":
        raise RuntimeError(f"expected READY, got: {reply!r}")

    sent = 0
    while sent < n:
        end = min(sent + chunk, n)
        ser.write(data[sent:end])
        ser.flush()
        sent = end

    reply = read_line(ser, timeout=30.0)
    if reply is None:
        raise RuntimeError("no reply after firmware stream (timeout)")
    if not reply.startswith("OK"):
        raise RuntimeError(f"update failed: {reply!r}")

    parts = reply.split()
    dev_crc = int(parts[1], 16) if len(parts) > 1 else None
    if dev_crc != crc:
        raise RuntimeError(f"CRC MISMATCH: device={dev_crc:08X} local={crc:08X}")
    print(f"Update accepted (crc {crc:08X}); device is rebooting into the new image.")


def run_update(
    ser: "serial.Serial",
    firmware_path: Path,
    target_version: str,
    force: bool = False,
) -> str:
    """Handshake + conditional push on an already-open serial port.

    Returns one of: "updated", "up-to-date", "no-device", "pushed-unverified".
    """
    device_version = query_version(ser)
    if device_version is None:
        print("No VERSION reply — device may be pre-OTA firmware or not responding.")
        if not force:
            return "no-device"
        print("--force set: pushing anyway.")
    else:
        print(f"Device firmware: {device_version}   bundled: {target_version}")
        if not force and semver(target_version) <= semver(device_version):
            print("Device is already up to date.")
            return "up-to-date"

    data = firmware_path.read_bytes()
    push_firmware(ser, data)

    # Give the ESP32 time to reboot (bootloader + app init), then re-query.
    time.sleep(4.0)
    ser.reset_input_buffer()
    new_version = query_version(ser)
    if new_version is None:
        print("Rebooted, but no VERSION reply yet (give it a moment).")
        return "pushed-unverified"
    print(f"Now running: {new_version}")
    return "updated"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("firmware", nargs="?", type=Path, default=DEFAULT_FIRMWARE,
                    help=f"path to firmware.bin (default: {DEFAULT_FIRMWARE})")
    ap.add_argument("--port", required=True, help="serial port, e.g. /dev/cu.usbmodemXXXX")
    ap.add_argument("--baud", type=int, default=115200, help="must match Serial.begin() (115200)")
    ap.add_argument("--config", type=Path, default=DEFAULT_CONFIG,
                    help="config.h to read FIRMWARE_VERSION from")
    ap.add_argument("--version", help="override the bundled version instead of reading config.h")
    ap.add_argument("--force", action="store_true", help="push regardless of version comparison")
    ap.add_argument("--check-only", action="store_true", help="print device vs bundled version and exit")
    args = ap.parse_args()

    target_version = args.version or parse_config_version(args.config)

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        time.sleep(0.3)  # let the port settle
        ser.reset_input_buffer()

        if args.check_only:
            dev = query_version(ser)
            print(f"Device firmware: {dev or 'unknown/no-reply'}")
            print(f"Bundled version: {target_version}")
            return

        if not args.firmware.exists():
            sys.exit(f"firmware not found: {args.firmware}  (build it: pio run -e upesy_wroom)")

        status = run_update(ser, args.firmware, target_version, force=args.force)
        print(f"Result: {status}")


if __name__ == "__main__":
    main()
