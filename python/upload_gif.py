#!/usr/bin/env python3
"""Upload an animated GIF to the Reachy eyes ESP32 and (optionally) play it.

A thin CLI over the SDK (:mod:`reachy_eyes`) — the upload, CRC-32 verification and
protocol framing all live in ``EyesClient``:

    from reachy_eyes import EyesClient
    with EyesClient() as eyes:
        eyes.upload_gif("heart.gif", play=True)

Usage:
    python3 upload_gif.py heart.gif                                   # auto-detect port
    python3 upload_gif.py --port /dev/cu.usbmodemXXXX heart.gif --play
    python3 upload_gif.py --port /dev/cu.usbmodemXXXX --info
    python3 upload_gif.py --port /dev/cu.usbmodemXXXX --stop

Requires: pyserial  (pip install pyserial)
"""

from __future__ import annotations

import argparse
import logging
import sys

from reachy_eyes import EyesClient, EyesError


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("gif", nargs="?", help="path to a .gif file to upload")
    ap.add_argument("--port", help="serial port; omit to auto-detect")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--play", action="store_true", help="play the gif after upload")
    ap.add_argument("--stop", action="store_true", help="stop playback and exit")
    ap.add_argument("--info", action="store_true", help="query the stored gif and exit")
    args = ap.parse_args()

    if not args.gif and not (args.info or args.stop):
        ap.error("provide a gif path, or use --info/--stop")

    logging.basicConfig(level=logging.INFO, format="%(message)s")

    try:
        with EyesClient(args.port, args.baud) as eyes:
            if args.info:
                info = eyes.gif_info()
                print(f"stored gif: {info}" if info else "stored gif: NONE")
                return
            if args.stop:
                eyes.stop_gif()
                print("playback stopped")
                return

            eyes.upload_gif(args.gif, play=args.play)
            info = eyes.gif_info()
            print(f"stored gif: {info}" if info else "stored gif: NONE")
            if args.play:
                print("playing")
    except EyesError as exc:
        sys.exit(str(exc))


if __name__ == "__main__":
    main()
