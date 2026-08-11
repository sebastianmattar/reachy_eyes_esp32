#!/usr/bin/env python3
"""Mirror Reachy Mini's head motion onto the ESP32 eyes over serial.

A thin CLI over the SDK: the bridge itself lives in
:class:`reachy_eyes.bridge.EyeBridge`, which reads the robot's head pose from the
daemon's 50 Hz state stream and streams a continuous gaze target to the firmware,
so the eyes follow *whatever* moves the robot makes — SDK apps, emotes, teleop —
without those callers knowing the eyes exist.

Run standalone (bring-up / testing):

    # macOS: the ESP32 is a dev/cu.usbserial*
    # Linux: it's /dev/ttyACM0 or /dev/ttyUSB0
    # Omit --port to let the SDK auto-detect it.
    # Requires: reachy-mini (numpy, scipy, pyserial) in the reachy_mini env.
    python eye_bridge.py --port /dev/cu.usbserialXXX

Or drive it from your own app:

    from reachy_mini import ReachyMini
    from reachy_eyes import EyeBridge

    with ReachyMini() as mini, EyeBridge(mini) as bridge:
        ...                        # your robot behavior; the eyes follow along
        bridge.override()          # pause head-follow for scripted eyes
        bridge.eyes.hearts()
        bridge.release()           # hand control back to head-follow
"""

from __future__ import annotations

import argparse
import logging
import time

from reachy_mini import ReachyMini

from reachy_eyes import EyeBridge, EyesClient, EyesError
from reachy_eyes.bridge import STREAM_HZ

logger = logging.getLogger("eye_bridge")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="ESP32 serial port; omit to auto-detect")
    ap.add_argument("--baud", type=int, default=115200)  # matches Serial.begin() in firmware
    ap.add_argument("--hz", type=float, default=STREAM_HZ)
    ap.add_argument(
        "--no-saccade-blink",
        action="store_true",
        help="Disable blinking on large gaze shifts.",
    )
    ap.add_argument(
        "--no-antenna-express",
        action="store_true",
        help="Disable antenna-deflection-driven eye expressions.",
    )
    ap.add_argument(
        "--update-firmware",
        nargs="?",
        const="__default__",
        metavar="firmware.bin",
        help="Before streaming, run a serial OTA check and push if the bundled "
        "firmware is newer than what the ESP32 is running. Optional path to a "
        ".bin (defaults to the built firmware).",
    )
    args = ap.parse_args()

    logging.basicConfig(level=logging.INFO)

    # OTA runs first, on its own exclusive serial connection, because it reboots
    # the device — do it before the bridge grabs the port to stream.
    if args.update_firmware is not None:
        fw = None if args.update_firmware == "__default__" else args.update_firmware
        with EyesClient(args.port, args.baud) as eyes:
            logger.info("Firmware update: %s", eyes.update_firmware(fw))
        time.sleep(1.0)  # let the port settle if the device rebooted

    with ReachyMini() as mini:
        bridge = EyeBridge(
            mini,
            args.port,
            args.baud,
            stream_hz=args.hz,
            blink_on_saccade=not args.no_saccade_blink,
            express_from_antennas=not args.no_antenna_express,
        )
        bridge.start()
        logger.info("Mirroring head -> eyes. Move the robot; Ctrl-C to quit.")
        try:
            while True:
                time.sleep(1.0)
        except KeyboardInterrupt:
            pass
        finally:
            bridge.stop()


if __name__ == "__main__":
    try:
        main()
    except EyesError as exc:
        raise SystemExit(str(exc))
