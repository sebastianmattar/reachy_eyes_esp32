#!/usr/bin/env python3
"""Serial OTA updater for the Reachy eyes ESP32 — version handshake + push.

The running firmware writes the new image into the inactive OTA slot itself (via
the ESP32 Update API) and reboots into it — so this needs no bootloader reset, no
esptool, and no BOOT/EN buttons. It reuses the same USB serial link the eye
protocol already uses, which is why it works even when the board is buried in the
robot and auto-reset flashing fails.

Version policy: the firmware.bin's version is read from config.h's
FIRMWARE_VERSION (single source of truth), compared to the device's reported
version, and the image is pushed only if it's newer (unless --force).

A thin CLI over the SDK (:mod:`reachy_eyes`) — the handshake and push live in
``EyesClient.update_firmware()``:

    from reachy_eyes import EyesClient
    with EyesClient() as eyes:
        status = eyes.update_firmware()

Usage:
    python3 ota_update.py                                             # auto: port, build path, config version
    python3 ota_update.py --port /dev/cu.usbmodemXXXX --check-only    # just print versions
    python3 ota_update.py --port /dev/cu.usbmodemXXXX fw.bin --force  # push regardless

Requires: pyserial  (pip install pyserial)
"""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from reachy_eyes import EyesClient, EyesError, firmware


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("firmware", nargs="?", type=Path, default=firmware.DEFAULT_FIRMWARE,
                    help=f"path to firmware.bin (default: {firmware.DEFAULT_FIRMWARE})")
    ap.add_argument("--port", help="serial port; omit to auto-detect")
    ap.add_argument("--baud", type=int, default=115200, help="must match Serial.begin() (115200)")
    ap.add_argument("--config", type=Path, default=firmware.DEFAULT_CONFIG,
                    help="config.h to read FIRMWARE_VERSION from")
    ap.add_argument("--version", help="override the bundled version instead of reading config.h")
    ap.add_argument("--force", action="store_true", help="push regardless of version comparison")
    ap.add_argument("--check-only", action="store_true",
                    help="print device vs bundled version and exit")
    args = ap.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(message)s")

    try:
        target_version = args.version or firmware.parse_config_version(args.config)

        with EyesClient(args.port, args.baud) as eyes:
            if args.check_only:
                print(f"Device firmware: {eyes.version() or 'unknown/no-reply'}")
                print(f"Bundled version: {target_version}")
                return

            if not args.firmware.exists():
                sys.exit(f"firmware not found: {args.firmware}  (build it: pio run -e upesy_wroom)")

            status = eyes.update_firmware(
                args.firmware, version=target_version, force=args.force
            )
            print(f"Result: {status}")
    except (EyesError, OSError, ValueError) as exc:
        sys.exit(str(exc))


if __name__ == "__main__":
    main()
