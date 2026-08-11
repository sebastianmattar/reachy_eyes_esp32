"""``EyesClient`` — the whole eyes protocol as Python methods.

One class covers everything the firmware exposes over USB serial: eye behavior
(gaze, blinks, expressions), the stored-GIF slot, and the serial OTA firmware
update. See ``esp32/include/serial_protocol.h`` for the wire format.

    from reachy_eyes import EyesClient, Direction

    with EyesClient() as eyes:          # auto-detects the serial port
        eyes.blink(2)
        eyes.gaze(Direction.NE)
        eyes.look(0.4, -0.2)            # continuous gaze target, x/y in [-1,1]
        eyes.hearts()
        eyes.idle()                     # back to autonomous wandering
"""

from __future__ import annotations

import logging
import time
import zlib
from pathlib import Path

from . import firmware as fw
from .errors import (
    CommandError,
    CrcMismatchError,
    EyesTimeoutError,
    ProtocolError,
)
from .models import Direction, DirectionLike, Eyes, EyesLike, GifInfo, UpdateStatus
from .transport import (
    DEFAULT_ACK_TIMEOUT,
    DEFAULT_BAUD,
    SerialTransport,
    parse_version_reply,
)

logger = logging.getLogger(__name__)

# Defaults mirroring the firmware's own (serial_protocol.cpp / eye_behavior.h),
# so a call with no timing arguments behaves the same as the bare command.
LOOK_TRANSITION_S = 0.15
GAZE_TRANSITION_S = 0.3
GAZE_RESET_S = 2.0
SQUINT_TRANSITION_S = 0.3
SQUINT_RESET_S = 2.0
BLINK_INTERVAL_MIN_S = 3.0
BLINK_INTERVAL_MAX_S = 6.0

GIF_MAX_BYTES = 1024 * 1024  # GIF_MAX_BYTES in config.h

# The OTA slot is erased before the device answers READY, and verified before it
# acks — both are slow compared to a normal command.
_FW_READY_TIMEOUT_S = 15.0
_FW_DONE_TIMEOUT_S = 30.0
_FW_REBOOT_S = 4.0
_GIF_TIMEOUT_S = 10.0


class EyesClient:
    """Client for one pair of ESP32 eyes on a serial port.

    Args:
        port: Serial device, e.g. ``/dev/cu.usbmodem1101``. ``None`` auto-detects.
        baud: Must match ``Serial.begin()`` in the firmware.
        wait_for_ack: Read and check each command's ``OK``/``ERR`` reply. Set
            ``False`` for fire-and-forget sending in a high-rate control loop
            (errors then go unnoticed, but nothing ever blocks on a read).
        ack_timeout: How long to wait for a command reply.
        connect: Open the port immediately.
        transport: Inject a pre-built :class:`~reachy_eyes.transport.SerialTransport`
            (mainly for tests, or to share one link).
    """

    def __init__(
        self,
        port: str | None = None,
        baud: int = DEFAULT_BAUD,
        *,
        wait_for_ack: bool = True,
        ack_timeout: float = DEFAULT_ACK_TIMEOUT,
        connect: bool = True,
        transport: SerialTransport | None = None,
    ) -> None:
        self._transport = transport or SerialTransport(port, baud)
        self._wait_for_ack = wait_for_ack
        self._ack_timeout = ack_timeout
        if connect:
            self.connect()

    # ── lifecycle ────────────────────────────────────────────────────────────
    @property
    def transport(self) -> SerialTransport:
        """The underlying serial link (port name, raw I/O, reconnects)."""
        return self._transport

    @property
    def port(self) -> str | None:
        return self._transport.port

    @property
    def is_connected(self) -> bool:
        return self._transport.is_open

    def connect(self) -> None:
        """Open the serial port. Idempotent."""
        self._transport.open()

    def close(self, *, idle: bool = False) -> None:
        """Close the serial port, optionally handing the eyes back to autonomy.

        ``idle=True`` sends ``IDLE`` first, so the eyes resume their own
        wandering/blinking instead of freezing on the last commanded pose.
        """
        if idle and self.is_connected:
            try:
                self.idle()
            except Exception:  # closing must not fail on a dying link
                logger.debug("IDLE on close failed", exc_info=True)
        self._transport.close()

    def __enter__(self) -> "EyesClient":
        self.connect()
        return self

    def __exit__(self, *exc_info) -> None:
        self.close()

    # ── eye behavior ─────────────────────────────────────────────────────────
    def idle(self) -> None:
        """Return the eyes to autonomous behavior (wander + auto-blink)."""
        self._command("IDLE")

    def look(
        self,
        x: float,
        y: float,
        transition: float = LOOK_TRANSITION_S,
        eyes: EyesLike = Eyes.BOTH,
    ) -> None:
        """Set a continuous gaze target; ``x``/``y`` in [-1, 1] (+x right, +y down).

        Held until the next :meth:`look`/:meth:`gaze`/:meth:`idle`. Suppresses
        wandering but keeps auto-blink. Meant to be streamed (~50 Hz) — the
        firmware answers nothing on success, so this never waits for a reply.
        """
        x, y = _clamp(x), _clamp(y)
        self._transport.write_line(
            f"LOOK {x:.3f} {y:.3f} {transition:.2f} {Eyes.coerce(eyes)}"
        )

    def gaze(
        self,
        direction: DirectionLike,
        transition: float = GAZE_TRANSITION_S,
        reset_after: float = GAZE_RESET_S,
        eyes: EyesLike = Eyes.BOTH,
    ) -> None:
        """Dart to one of the nine discrete directions, then recentre."""
        self._command(
            f"GAZE {Direction.coerce(direction)} {transition:.2f} "
            f"{reset_after:.2f} {Eyes.coerce(eyes)}"
        )

    def blink(self, times: int = 1, eyes: EyesLike = Eyes.BOTH) -> None:
        """Blink ``times`` times. Composes with a held :meth:`look` gaze."""
        self._command(f"BLINK {int(times)} {Eyes.coerce(eyes)}")

    def squint(
        self,
        transition: float = SQUINT_TRANSITION_S,
        reset_after: float = SQUINT_RESET_S,
    ) -> None:
        """Narrow the lids, auto-restoring after ``reset_after`` seconds.

        Lids only: unlike the billboard expressions it composes with a held
        :meth:`look` gaze instead of replacing the rendering.
        """
        self._command(f"SQUINT {transition:.2f} {reset_after:.2f}")

    def hearts(self, eyes: EyesLike = Eyes.BOTH) -> None:
        """Heart-eyes expression (replaces the eye rendering while it runs)."""
        self._command(f"HEARTS {Eyes.coerce(eyes)}")

    def money(self, eyes: EyesLike = Eyes.BOTH) -> None:
        """Money-eyes expression."""
        self._command(f"MONEY {Eyes.coerce(eyes)}")

    def dead(self, eyes: EyesLike = Eyes.BOTH) -> None:
        """Dead ("X") eyes expression."""
        self._command(f"DEAD {Eyes.coerce(eyes)}")

    def trapezoid(self, eyes: EyesLike = Eyes.BOTH) -> None:
        """Switch to the trapezoid eye style."""
        self._command(f"TRAPEZOID {Eyes.coerce(eyes)}")

    def set_blink_interval(
        self,
        min_s: float = BLINK_INTERVAL_MIN_S,
        max_s: float = BLINK_INTERVAL_MAX_S,
    ) -> None:
        """Set the random interval between automatic blinks."""
        self._command(f"BLINK_INTERVAL {min_s:.2f} {max_s:.2f}")

    def send_raw(self, line: str, *, expect_reply: bool = True) -> str | None:
        """Escape hatch: send a protocol line verbatim.

        Returns the reply line when ``expect_reply`` is set, else ``None``.
        """
        if not expect_reply:
            self._transport.write_line(line)
            return None
        return self._transport.exchange(line, timeout=self._ack_timeout)

    # ── device info ──────────────────────────────────────────────────────────
    def version(self) -> str | None:
        """The device's firmware version, or ``None`` if it doesn't answer.

        A missing reply is meaningful rather than exceptional here: pre-OTA
        firmware doesn't implement ``VERSION``.
        """
        reply = self._transport.exchange("VERSION", timeout=self._ack_timeout)
        if reply is None:
            return None
        version = parse_version_reply(reply)
        if version is None:
            raise ProtocolError(f"VERSION: unexpected reply {reply!r}")
        return version

    # ── stored GIF ───────────────────────────────────────────────────────────
    def gif_info(self) -> GifInfo | None:
        """Size and CRC-32 of the stored GIF, or ``None`` if the slot is empty."""
        reply = self._transport.exchange("GIFINFO", timeout=_GIF_TIMEOUT_S)
        if reply is None:
            raise EyesTimeoutError("GIFINFO: no reply")
        if reply == "NONE":
            return None
        if reply.startswith("ERR"):
            raise CommandError("GIFINFO", reply[3:].strip() or "unspecified")
        parts = reply.split()
        if len(parts) < 3 or parts[0] != "INFO":
            raise ProtocolError(f"GIFINFO: unexpected reply {reply!r}")
        return GifInfo(size_bytes=int(parts[1]), crc32=int(parts[2], 16))

    def upload_gif(self, gif: str | Path | bytes, *, play: bool = False) -> int:
        """Upload a GIF into the device's single storage slot (overwrites it).

        Accepts a path or raw bytes. The device's CRC-32 is verified against a
        locally computed one; returns that CRC.
        """
        data = bytes(gif) if isinstance(gif, (bytes, bytearray)) else Path(gif).read_bytes()
        if not data:
            raise ValueError("refusing to upload an empty GIF")
        if len(data) > GIF_MAX_BYTES:
            raise ValueError(
                f"GIF is {len(data)} bytes; the device accepts at most {GIF_MAX_BYTES}"
            )
        local_crc = zlib.crc32(data) & 0xFFFFFFFF
        logger.info("uploading GIF: %d bytes, crc32=%08X", len(data), local_crc)

        reply = self._transport.upload(
            f"GIFUPLOAD {len(data)}",
            data,
            ready_timeout=_GIF_TIMEOUT_S,
            done_timeout=_GIF_TIMEOUT_S,
        )
        _verify_upload_ack("GIFUPLOAD", reply, local_crc)
        logger.info("upload OK, crc verified (%08X)", local_crc)

        if play:
            self.play_gif()
        return local_crc

    def play_gif(self, path: str | None = None) -> None:
        """Loop the stored GIF on both eyes.

        ``path`` is only for the desktop simulator, which plays a local file
        instead of the on-device slot.
        """
        self._command("PLAYGIF" if path is None else f"PLAYGIF {path}")

    def stop_gif(self) -> None:
        """Stop GIF playback and revert to autonomous eye behavior."""
        self._command("STOPGIF")

    # ── firmware update (serial OTA) ─────────────────────────────────────────
    def push_firmware(self, data: bytes) -> int:
        """Stream a firmware image into the inactive OTA slot unconditionally.

        The device verifies the CRC-32, commits the slot and reboots into it.
        Returns the CRC. Prefer :meth:`update_firmware`, which checks versions.
        """
        crc = zlib.crc32(data) & 0xFFFFFFFF
        logger.info("pushing firmware: %d bytes, crc32=%08X", len(data), crc)
        reply = self._transport.upload(
            f"FWUPDATE {len(data)} {crc:08x}",
            data,
            ready_timeout=_FW_READY_TIMEOUT_S,
            done_timeout=_FW_DONE_TIMEOUT_S,
        )
        _verify_upload_ack("FWUPDATE", reply, crc)
        logger.info("update accepted (crc %08X); device is rebooting into it", crc)
        return crc

    def update_firmware(
        self,
        firmware: str | Path | None = None,
        *,
        version: str | None = None,
        config: str | Path | None = None,
        force: bool = False,
        reboot_wait: float = _FW_REBOOT_S,
    ) -> UpdateStatus:
        """Push ``firmware`` only if it is newer than what the device runs.

        Args:
            firmware: Path to a ``firmware.bin``; defaults to the PlatformIO
                build output for the ``upesy_wroom`` environment.
            version: The image's version. Defaults to ``FIRMWARE_VERSION`` from
                ``config.h`` — the single source of truth for a local build.
            config: Alternative ``config.h`` to read the version from.
            force: Push regardless of the version comparison, and even if the
                device never answered ``VERSION``.
            reboot_wait: Seconds to wait before re-querying the version.

        Returns the resulting :class:`~reachy_eyes.models.UpdateStatus`. Nothing
        is sent when the device is already up to date.
        """
        target_version = version or fw.parse_config_version(
            Path(config) if config is not None else fw.DEFAULT_CONFIG
        )
        device_version = self.version()

        if device_version is None:
            logger.warning(
                "no VERSION reply — device may run pre-OTA firmware or be unresponsive"
            )
            if not force:
                return UpdateStatus.NO_DEVICE
            logger.info("force set: pushing anyway")
        else:
            logger.info(
                "device firmware: %s   bundled: %s", device_version, target_version
            )
            if not force and not fw.is_newer(target_version, device_version):
                logger.info("device is already up to date")
                return UpdateStatus.UP_TO_DATE

        _, data = fw.load_firmware(firmware)
        self.push_firmware(data)

        # Give the ESP32 time to reboot (bootloader + app init), then re-query.
        time.sleep(reboot_wait)
        self._transport.discard_input()
        new_version = self.version()
        if new_version is None:
            logger.warning("rebooted, but no VERSION reply yet (give it a moment)")
            return UpdateStatus.PUSHED_UNVERIFIED
        logger.info("now running: %s", new_version)
        return UpdateStatus.UPDATED

    # ── internals ────────────────────────────────────────────────────────────
    def _command(self, line: str) -> None:
        """Send a command that the firmware acks with ``OK`` / ``ERR <reason>``."""
        if not self._wait_for_ack:
            self._transport.write_line(line)
            return
        name = line.split()[0]
        reply = self._transport.exchange(line, timeout=self._ack_timeout)
        if reply is None:
            raise EyesTimeoutError(f"{name}: no reply within {self._ack_timeout:g}s")
        if reply.startswith("ERR"):
            raise CommandError(name, reply[3:].strip() or "unspecified")
        if reply != "OK":
            raise ProtocolError(f"{name}: expected OK, got {reply!r}")


# ── helpers ──────────────────────────────────────────────────────────────────
def _clamp(v: float, lo: float = -1.0, hi: float = 1.0) -> float:
    return max(lo, min(hi, float(v)))


def _verify_upload_ack(command: str, reply: str, local_crc: int) -> None:
    """Check an ``OK <crc32>`` upload ack against the locally computed CRC."""
    if reply.startswith("ERR"):
        raise CommandError(command, reply[3:].strip() or "unspecified")
    if not reply.startswith("OK"):
        raise ProtocolError(f"{command}: expected OK, got {reply!r}")
    parts = reply.split()
    device_crc = int(parts[1], 16) if len(parts) > 1 else None
    if device_crc != local_crc:
        raise CrcMismatchError(device_crc, local_crc)
