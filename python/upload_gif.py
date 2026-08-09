#!/usr/bin/env python3
"""Upload an animated GIF to the Reachy eyes ESP32 and (optionally) play it.

Protocol (see serial_protocol.h):
    host → GIFUPLOAD <byteLen>\n
    esp  → READY\n
    host → <byteLen raw bytes>
    esp  → OK <crc32>\n   (or ERR <reason>\n)
    host → PLAYGIF\n
    esp  → OK\n

The firmware computes a CRC-32 (zlib) over the uploaded bytes; this script
computes the same locally and verifies they match.

Usage:
    python3 upload_gif.py --port /dev/tty.usbserial-XXXX heart.gif
    python3 upload_gif.py --port /dev/tty.usbserial-XXXX --info
    python3 upload_gif.py --port /dev/tty.usbserial-XXXX --stop

Requires: pyserial  (pip install pyserial)
"""
import argparse
import sys
import time
import zlib

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial not installed — run: pip install pyserial")


def read_line(ser, timeout=5.0):
    """Read one CRLF/LF-terminated line as text, honoring a wall-clock timeout."""
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


def send_cmd(ser, cmd):
    ser.write((cmd + "\n").encode())
    ser.flush()


def cmd_upload(ser, path, chunk=512):
    with open(path, "rb") as f:
        data = f.read()
    n = len(data)
    local_crc = zlib.crc32(data) & 0xFFFFFFFF
    print(f"Uploading {path}: {n} bytes, crc32={local_crc:08X}")

    send_cmd(ser, f"GIFUPLOAD {n}")
    reply = read_line(ser)
    if reply != "READY":
        sys.exit(f"expected READY, got: {reply!r}")

    sent = 0
    while sent < n:
        end = min(sent + chunk, n)
        ser.write(data[sent:end])
        ser.flush()
        sent = end

    reply = read_line(ser, timeout=10.0)
    if reply is None:
        sys.exit("no reply after upload (timeout)")
    if not reply.startswith("OK"):
        sys.exit(f"upload failed: {reply!r}")

    parts = reply.split()
    dev_crc = int(parts[1], 16) if len(parts) > 1 else None
    if dev_crc != local_crc:
        sys.exit(f"CRC MISMATCH: device={dev_crc:08X} local={local_crc:08X}")
    print(f"Upload OK, crc verified ({local_crc:08X})")


def cmd_info(ser):
    send_cmd(ser, "GIFINFO")
    print("GIFINFO →", read_line(ser))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("gif", nargs="?", help="path to a .gif file to upload")
    ap.add_argument("--port", required=True, help="serial port, e.g. /dev/tty.usbserial-XXXX")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--play", action="store_true", help="send PLAYGIF after upload")
    ap.add_argument("--stop", action="store_true", help="send STOPGIF and exit")
    ap.add_argument("--info", action="store_true", help="query GIFINFO and exit")
    args = ap.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        time.sleep(0.3)  # let the port settle
        ser.reset_input_buffer()

        if args.info:
            cmd_info(ser)
            return
        if args.stop:
            send_cmd(ser, "STOPGIF")
            print("STOPGIF →", read_line(ser))
            return

        if not args.gif:
            ap.error("provide a gif path, or use --info/--stop")

        cmd_upload(ser, args.gif)
        cmd_info(ser)

        if args.play:
            send_cmd(ser, "PLAYGIF")
            print("PLAYGIF →", read_line(ser))


if __name__ == "__main__":
    main()
