"""Mirror Reachy Mini's head motion onto the eyes.

Architecture: **observer + overrides**. A background thread reads the robot's head
pose from the daemon's 50 Hz state stream and streams a continuous ``LOOK <x> <y>``
gaze target to the firmware, so the eyes follow *whatever* moves the robot makes —
SDK apps, emotes, teleop, all of it — without those callers knowing the eyes exist.

The bridge runs in-process, sharing the one ``ReachyMini`` instance. That sidesteps
the robot's single-app lock (a second *controlling* SDK client would conflict; an
in-process reader will not).

Unlike the rest of the SDK, this module needs the ``reachy_mini`` SDK (plus numpy
and scipy) — which is why :mod:`reachy_eyes` only imports it on demand.

    from reachy_mini import ReachyMini
    from reachy_eyes import EyeBridge

    with ReachyMini() as mini:
        bridge = EyeBridge(mini)          # auto-detects the eyes' serial port
        bridge.start()
        ...
        bridge.override()                 # pause head-follow
        bridge.eyes.hearts()              # scripted expression
        bridge.release()                  # back to following the head
        bridge.stop()
"""

from __future__ import annotations

import logging
import threading
import time

import numpy as np
from scipy.spatial.transform import Rotation as R

from reachy_mini import ReachyMini

from .client import EyesClient
from .transport import DEFAULT_BAUD

logger = logging.getLogger(__name__)

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
# LOOK gaze and never interrupts head-follow, unlike the billboard styles (hearts,
# trapezoid, ...) which would replace the gaze rendering and need the override path.
#
# Deflection metric = mean(|antenna angle|) in rad; neutral rest is ~0.17. Rising-
# edge + hysteresis (arm below LOW, fire crossing HIGH) stops constant antenna
# motion from flickering the expression. All three are guesses — tune on hardware.
ANTENNA_DEFLECT_HIGH = 0.8  # fire the expression at/above this
ANTENNA_DEFLECT_LOW = 0.4  # re-arm once it drops back below this
ANTENNA_SQUINT_TRANSITION_S = 0.15
ANTENNA_SQUINT_RESET_S = 0.6  # firmware auto-restores the lids after this


def head_pose_to_xy(pose: np.ndarray) -> tuple[float, float]:
    """Map a 4x4 head pose to normalized eye gaze (x right+, y up+ → down+ screen).

    Screen convention matches the firmware: +x = eyes look right, +y = look down.
    Head pitch-up should make the eyes look up, hence the sign flip on y.
    """
    _roll, pitch, yaw = R.from_matrix(pose[:3, :3]).as_euler("xyz")
    x = _clamp(yaw / MAX_YAW_RAD)
    y = _clamp(-pitch / MAX_PITCH_RAD)
    return x, y


def _clamp(v: float, lo: float = -1.0, hi: float = 1.0) -> float:
    return max(lo, min(hi, v))


class EyeBridge:
    """Streams robot head pose to the ESP32 eyes; supports scripted overrides.

    Args:
        mini: The in-process ``ReachyMini`` whose pose is mirrored.
        port: Eyes serial port; ``None`` auto-detects.
        baud: Serial baud rate.
        eyes: Use an existing :class:`~reachy_eyes.client.EyesClient` instead of
            opening one. A client passed in is *not* closed by :meth:`stop`.
        stream_hz: Gaze update rate.
        blink_on_saccade: Blink on large gaze shifts.
        express_from_antennas: Squint when the antennas deflect strongly.
    """

    def __init__(
        self,
        mini: ReachyMini,
        port: str | None = None,
        baud: int = DEFAULT_BAUD,
        *,
        eyes: EyesClient | None = None,
        stream_hz: float = STREAM_HZ,
        blink_on_saccade: bool = True,
        express_from_antennas: bool = True,
    ) -> None:
        self._mini = mini
        self._owns_eyes = eyes is None
        # wait_for_ack=False: at this rate we cannot afford a blocking read per
        # command, and LOOK is silent on the wire anyway.
        self._eyes = eyes or EyesClient(port, baud, wait_for_ack=False)
        self._eyes.connect()
        self._period = 1.0 / stream_hz
        self._blink_on_saccade = blink_on_saccade
        self._express_from_antennas = express_from_antennas
        self._stop = threading.Event()
        self._overridden = threading.Event()  # set = pause head-follow
        self._thread: threading.Thread | None = None
        self._last_xy: tuple[float, float] | None = None
        self._last_blink_t = 0.0
        self._antenna_armed = True  # hysteresis latch for the deflection trigger

    @property
    def eyes(self) -> EyesClient:
        """The eyes client — the full command set, usable while the bridge runs."""
        return self._eyes

    # ── lifecycle ────────────────────────────────────────────────────────────
    def start(self) -> None:
        if self._thread is not None:
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        logger.info("Eye bridge started on %s", self._eyes.port)

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None
        if self._owns_eyes:
            self._eyes.close(idle=True)  # restore autonomous eye behavior
        else:
            self._eyes.idle()
        logger.info("Eye bridge stopped")

    def __enter__(self) -> "EyeBridge":
        self.start()
        return self

    def __exit__(self, *exc_info) -> None:
        self.stop()

    # ── overrides ────────────────────────────────────────────────────────────
    def override(self, command: str | None = None) -> None:
        """Pause head-follow, optionally sending one raw command line.

        Prefer driving :attr:`eyes` directly for anything typed
        (``bridge.override(); bridge.eyes.hearts()``). Call :meth:`release` to
        resume mirroring the head.
        """
        self._overridden.set()
        if command is not None:
            self._eyes.send_raw(command, expect_reply=False)

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
                    self._eyes.look(x, y, LOOK_TRANSITION_S)
                    self._maybe_blink_on_saccade(x, y)
                    self._maybe_express_from_antennas()
                except Exception:  # non-fatal: keep the eyes alive across hiccups
                    logger.exception("head->eye update failed")
            # Drop any ERR replies so the OS RX buffer can't back up.
            self._eyes.transport.discard_input()
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
            self._eyes.blink(1)  # runs concurrently with the held LOOK gaze
            self._last_blink_t = now

    def _maybe_express_from_antennas(self) -> None:
        if not self._express_from_antennas:
            return
        antennas = self._mini.get_present_antenna_joint_positions()  # [right, left] rad
        deflection = float(np.mean(np.abs(antennas)))
        if self._antenna_armed and deflection >= ANTENNA_DEFLECT_HIGH:
            # Lids-only expression: composes with the held LOOK gaze, auto-restores.
            self._eyes.squint(ANTENNA_SQUINT_TRANSITION_S, ANTENNA_SQUINT_RESET_S)
            self._antenna_armed = False
        elif deflection <= ANTENNA_DEFLECT_LOW:
            self._antenna_armed = True  # re-arm once antennas relax
