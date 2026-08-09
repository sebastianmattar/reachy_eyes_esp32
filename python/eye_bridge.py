#!/usr/bin/env python3
"""Mirror Reachy Mini's head motion onto the ESP32 eyes over serial.

Architecture: **observer + overrides** (see the eyes/robot integration notes).
A background thread reads the robot's head pose from the daemon's 50 Hz state
stream and streams a continuous ``LOOK <x> <y>`` gaze target to the firmware, so
the eyes follow *whatever* moves the robot makes — SDK apps, emotes, teleop, all
of it — without those callers knowing the eyes exist.

The bridge runs in-process, sharing the one ``ReachyMini`` instance. That
sidesteps the robot's single-app lock (a second *controlling* SDK client would
conflict; an in-process reader will not).

Run standalone (bring-up / testing):

    # macOS: the ESP32 is a /dev/cu.usbmodem* (or /dev/cu.usbserial* / .SLAB_USBtoUART)
    # Linux: it's /dev/ttyACM0 or /dev/ttyUSB0
    # Find it: `ls /dev/cu.*` (mac) — replug the ESP32 to see which entry appears.
    # Requires: pyserial  (pip/uv pip install pyserial) in the reachy_mini env.
    python eye_bridge.py --port /dev/cu.usbmodemXXXX

Or drive it from your own app: construct ``EyeBridge`` and call ``start()`` /
``stop()`` around your robot behavior; call ``override(...)`` for scripted eyes
(BLINK, HEARTS, ...) and ``release()`` to hand control back to head-follow.
"""

from __future__ import annotations

import argparse
import logging
import threading
import time
from pathlib import Path

import numpy as np
import serial
from scipy.spatial.transform import Rotation as R

from reachy_mini import ReachyMini

logger = logging.getLogger("eye_bridge")

# Head yaw/pitch (rad) that map to full iris travel (x/y = ±1). The head's own
# range is small, so we saturate well before the mechanical limit to get lively
# eyes. Tune to taste.
MAX_YAW_RAD = np.deg2rad(25.0)
MAX_PITCH_RAD = np.deg2rad(20.0)

STREAM_HZ = 50.0
# LOOK transition ≈ a few frames: long enough to smooth the discrete stream,
# short enough that the eyes don't visibly lag the head.
LOOK_TRANSITION_S = 0.12

# Saccade-triggered blink: a large, fast gaze shift fires a blink, mimicking the
# blink that naturally accompanies a big eye/head reorientation. Threshold is in
# gaze units (the full iris span is 2.0, corner-to-corner ~2.8); cooldown stops a
# sustained sweep from machine-gunning blinks.
SACCADE_BLINK_DISTANCE = 0.6
SACCADE_BLINK_COOLDOWN_S = 0.8

# Antenna-driven expression: the antennas are the robot's built-in "mood" channel,
# so a strong deflection drives a matching eye expression. We use SQUINT because on
# the firmware it only moves the lids (auto-restoring) — it composes with the held
# LOOK gaze and never interrupts head-follow, unlike the billboard styles (HEARTS,
# TRAPEZOID, ...) which would replace the gaze rendering and need the override path.
#
# Deflection metric = mean(|antenna angle|) in rad; neutral rest is ~0.17. Rising-
# edge + hysteresis (arm below LOW, fire crossing HIGH) stops constant antenna
# motion from flickering the expression. All three are guesses — tune on hardware.
ANTENNA_DEFLECT_HIGH = 0.8   # fire the expression at/above this
ANTENNA_DEFLECT_LOW = 0.4    # re-arm once it drops back below this
ANTENNA_SQUINT_TRANSITION_S = 0.15
ANTENNA_SQUINT_RESET_S = 0.6  # firmware auto-restores the lids after this


def _clamp(v: float, lo: float = -1.0, hi: float = 1.0) -> float:
    return max(lo, min(hi, v))


def head_pose_to_xy(pose: np.ndarray) -> tuple[float, float]:
    """Map a 4x4 head pose to normalized eye gaze (x right+, y up+ → down+ screen).

    Screen convention matches the firmware: +x = eyes look right, +y = look down.
    Head pitch-up should make the eyes look up, hence the sign flip on y.
    """
    roll, pitch, yaw = R.from_matrix(pose[:3, :3]).as_euler("xyz")
    x = _clamp(yaw / MAX_YAW_RAD)
    y = _clamp(-pitch / MAX_PITCH_RAD)
    return x, y


def _open_serial(port: str, baud: int) -> "serial.Serial":
    """Open the ESP32 serial port, or fail with the list of ports to try instead."""
    try:
        return serial.Serial(port, baud, timeout=0.1)
    except serial.SerialException as exc:
        try:
            from serial.tools import list_ports

            found = [f"  {p.device}  ({p.description})" for p in list_ports.comports()]
        except Exception:
            found = []
        hint = "\n".join(found) if found else "  (none found — is the ESP32 plugged in?)"
        raise SystemExit(
            f"Could not open serial port {port!r}: {exc}\n"
            f"Available ports:\n{hint}\n"
            f"On macOS the ESP32 is typically a /dev/cu.usbmodem* or /dev/cu.usbserial* entry."
        ) from exc


class EyeBridge:
    """Streams robot head pose to the ESP32 eyes; supports scripted overrides."""

    def __init__(
        self,
        mini: ReachyMini,
        port: str,
        baud: int = 115200,
        stream_hz: float = STREAM_HZ,
        blink_on_saccade: bool = True,
        express_from_antennas: bool = True,
    ) -> None:
        self._mini = mini
        self._ser = _open_serial(port, baud)
        self._period = 1.0 / stream_hz
        self._blink_on_saccade = blink_on_saccade
        self._express_from_antennas = express_from_antennas
        self._stop = threading.Event()
        self._overridden = threading.Event()  # set = pause head-follow
        self._thread: threading.Thread | None = None
        self._write_lock = threading.Lock()
        self._last_xy: tuple[float, float] | None = None
        self._last_blink_t = 0.0
        self._antenna_armed = True  # hysteresis latch for the deflection trigger

    # ── lifecycle ────────────────────────────────────────────────────────────
    def start(self) -> None:
        if self._thread is not None:
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        logger.info("Eye bridge started")

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None
        try:
            self._send("IDLE")  # restore autonomous eye behavior
        finally:
            self._ser.close()
        logger.info("Eye bridge stopped")

    # ── overrides ────────────────────────────────────────────────────────────
    def override(self, command: str) -> None:
        """Pause head-follow and send a scripted eye command (e.g. ``"HEARTS"``).

        Call ``release()`` to resume mirroring the head.
        """
        self._overridden.set()
        self._send(command)

    def release(self) -> None:
        """Resume head-follow after an override."""
        self._last_xy = None  # re-seed so the resume frame isn't seen as a saccade
        self._overridden.clear()

    # ── internals ────────────────────────────────────────────────────────────
    def _run(self) -> None:
        next_t = time.monotonic()
        while not self._stop.is_set():
            if not self._overridden.is_set():
                try:
                    pose = self._mini.get_current_head_pose()
                    x, y = head_pose_to_xy(pose)
                    self._send(f"LOOK {x:.3f} {y:.3f} {LOOK_TRANSITION_S:.2f}")
                    self._maybe_blink_on_saccade(x, y)
                    self._maybe_express_from_antennas()
                except Exception:  # non-fatal: keep the eyes alive across hiccups
                    logger.exception("head->eye update failed")
            # Drain any ERR replies so the OS RX buffer can't back up.
            if self._ser.in_waiting:
                self._ser.read(self._ser.in_waiting)
            next_t += self._period
            sleep = next_t - time.monotonic()
            if sleep > 0:
                time.sleep(sleep)
            else:
                next_t = time.monotonic()  # fell behind; resync

    def _maybe_blink_on_saccade(self, x: float, y: float) -> None:
        prev, self._last_xy = self._last_xy, (x, y)
        if not self._blink_on_saccade or prev is None:
            return
        jump = np.hypot(x - prev[0], y - prev[1])
        now = time.monotonic()
        if jump >= SACCADE_BLINK_DISTANCE and now - self._last_blink_t >= SACCADE_BLINK_COOLDOWN_S:
            self._send("BLINK 1")  # runs concurrently with the held LOOK gaze
            self._last_blink_t = now

    def _maybe_express_from_antennas(self) -> None:
        if not self._express_from_antennas:
            return
        antennas = self._mini.get_present_antenna_joint_positions()  # [right, left] rad
        deflection = float(np.mean(np.abs(antennas)))
        if self._antenna_armed and deflection >= ANTENNA_DEFLECT_HIGH:
            # Lids-only expression: composes with the held LOOK gaze, auto-restores.
            self._send(
                f"SQUINT {ANTENNA_SQUINT_TRANSITION_S:.2f} {ANTENNA_SQUINT_RESET_S:.2f}"
            )
            self._antenna_armed = False
        elif deflection <= ANTENNA_DEFLECT_LOW:
            self._antenna_armed = True  # re-arm once antennas relax

    def _send(self, line: str) -> None:
        with self._write_lock:
            self._ser.write((line + "\n").encode())


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", required=True, help="ESP32 serial port, e.g. /dev/ttyACM0")
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
        import ota_update

        fw = (
            ota_update.DEFAULT_FIRMWARE
            if args.update_firmware == "__default__"
            else Path(args.update_firmware)
        )
        version = ota_update.parse_config_version(ota_update.DEFAULT_CONFIG)
        with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
            time.sleep(0.3)
            ser.reset_input_buffer()
            ota_update.run_update(ser, fw, version)
        time.sleep(1.0)  # let the port settle if the device rebooted

    with ReachyMini() as mini:
        bridge = EyeBridge(
            mini,
            args.port,
            baud=args.baud,
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
    main()
