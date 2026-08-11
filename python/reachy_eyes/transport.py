"""Serial transport for the eyes protocol: port discovery, line framing, locking.

The firmware speaks a line-delimited ASCII protocol over USB serial, with two
binary side-channels (GIF upload, firmware OTA) that are framed by a header line
plus a ``READY`` handshake — see ``esp32/include/serial_protocol.h``.

This module owns exactly that wire level: opening the right port, reading/writing
lines, streaming raw payloads, and serializing whole request/response exchanges
under one lock so a background streamer (``eye_bridge``) and foreground commands
can share a single connection. Interpretation of the replies lives in
:mod:`reachy_eyes.client`.
"""

from __future__ import annotations

import logging
import os
import re
import threading
import time

import serial
from serial.tools import list_ports

from .errors import (
    CommandError,
    ConnectionFailedError,
    NotConnectedError,
    PortNotFoundError,
    ProtocolError,
)

logger = logging.getLogger(__name__)

DEFAULT_BAUD = 115200  # must match Serial.begin() in the firmware
DEFAULT_ACK_TIMEOUT = 5.0
READ_TIMEOUT_S = 0.2  # pyserial read granularity; the deadline is our own
SETTLE_S = 0.3  # let the port (and any auto-reset) settle before talking
CHUNK_SIZE = 512  # matches the firmware's binary receive buffer

# Opening a port asserts DTR/RTS, which reboots most ESP32 boards — so a probe
# asks twice: once briefly (a running board answers at once) and once with a
# window long enough for a board that just rebooted to come back up.
PROBE_TIMEOUT_S = 2.0
PROBE_QUICK_TIMEOUT_S = 0.5
PROBE_ATTEMPTS = 2

# Pin the port for every tool at once (useful inside the robot, where device
# names are neither obvious nor stable).
ENV_PORT = "REACHY_EYES_PORT"

# USB-serial bridges commonly used on ESP32 boards (CP210x, CH34x, FTDI) plus
# Espressif's own native-USB VID — used to rank candidates during discovery.
# Deliberately only a *ranking* hint: inside a Reachy Mini the robot's own motor
# controller is a CH343 (1a86:55d3) too, i.e. indistinguishable from an eyes
# board by VID/PID alone. Identity is settled by the VERSION handshake instead.
_USB_SERIAL_VIDS = {0x10C4, 0x1A86, 0x0403, 0x303A}
_PORT_NAME_HINTS = (
    "usbmodem",
    "usbserial",
    "wchusbserial",
    "slab_usbtouart",
    "ttyacm",
    "ttyusb",
)


# ── port discovery ───────────────────────────────────────────────────────────
def available_ports() -> list:
    """All serial ports the OS knows about (``ListPortInfo`` objects)."""
    return list(list_ports.comports())


def usb_ports() -> list:
    """Serial ports backed by USB, most-likely-ESP32 first.

    Non-USB entries (``/dev/cu.Bluetooth-Incoming-Port``, debug consoles) are
    dropped: they can never be the eyes and opening some of them blocks.
    """
    usb = [p for p in available_ports() if getattr(p, "vid", None) is not None]
    return sorted(usb, key=lambda p: not _looks_like_esp32(p))


def describe_ports() -> str:
    """Human-readable port list, for error messages."""
    found = [f"  {p.device}  ({describe_port(p)})" for p in available_ports()]
    return "\n".join(found) if found else "  (none found — is the ESP32 plugged in?)"


def describe_port(port_info) -> str:
    vid, pid = getattr(port_info, "vid", None), getattr(port_info, "pid", None)
    ids = f"{vid:04x}:{pid:04x}" if vid and pid else "not USB"
    serial_number = getattr(port_info, "serial_number", None)
    return f"{port_info.description}, {ids}" + (f", sn={serial_number}" if serial_number else "")


def _looks_like_esp32(port_info) -> bool:
    if getattr(port_info, "vid", None) in _USB_SERIAL_VIDS:
        return True
    name = port_info.device.lower()
    return any(hint in name for hint in _PORT_NAME_HINTS)


_VERSION_REPLY = re.compile(r"^VERSION\s+(\S+)$")


def parse_version_reply(reply: str) -> str | None:
    """Extract ``x.y.z`` from a ``VERSION x.y.z`` line; ``None`` if it isn't one.

    The version token must carry a digit, which makes this echo-proof: a device
    that simply mirrors bytes back sends our own bare ``VERSION`` line, and some
    USB gadgets (a MOTU audio interface here) really do that. Accepting it would
    make discovery hand back a random device as if it were the eyes.
    """
    match = _VERSION_REPLY.match(reply.strip())
    if not match:
        return None
    version = match.group(1)
    return version if any(char.isdigit() for char in version) else None


def probe_port(
    port: str,
    baud: int = DEFAULT_BAUD,
    *,
    timeout: float | None = None,
    attempts: int = PROBE_ATTEMPTS,
) -> str | None:
    """Ask ``port`` for its firmware version; ``None`` if it isn't the eyes.

    Returns the version string (possibly empty) when the device answers
    ``VERSION``, and ``None`` when the port can't be opened, stays silent, or
    replies with something else. ``timeout`` bounds the *last* attempt; earlier
    ones are short, so a device that simply isn't the eyes is ruled out quickly.

    This writes one ``VERSION`` line to the port. That is inert for other
    devices: the Reachy Mini's servo bus ignores anything without its binary
    frame header, and no motion or configuration command shares that spelling.
    """
    timeout = PROBE_TIMEOUT_S if timeout is None else timeout
    for attempt in range(attempts):
        last = attempt == attempts - 1
        window = timeout if last else min(PROBE_QUICK_TIMEOUT_S, timeout)
        try:
            with SerialTransport(port, baud) as transport:
                reply = transport.exchange("VERSION", timeout=window)
        except (ConnectionFailedError, OSError) as exc:
            logger.debug("probe %s: cannot open (%s)", port, exc)
            return None
        if reply is not None:
            version = parse_version_reply(reply)
            logger.debug("probe %s: replied %r", port, reply)
            return version
        logger.debug("probe %s: silent (attempt %d/%d)", port, attempt + 1, attempts)
    return None


def discover(baud: int = DEFAULT_BAUD, *, timeout: float | None = None) -> list[tuple]:
    """Probe every USB serial port; return ``(port_info, version)`` for responders."""
    found = []
    for info in usb_ports():
        version = probe_port(info.device, baud, timeout=timeout)
        if version is not None:
            found.append((info, version))
    return found


def find_port(
    *,
    verify: bool = True,
    baud: int = DEFAULT_BAUD,
    probe_timeout: float | None = None,
) -> str:
    """Find the eyes' serial port.

    ``$REACHY_EYES_PORT`` wins if set. Otherwise USB serial ports are tried in
    likelihood order and, with ``verify`` (the default), the first one that
    answers the ``VERSION`` handshake is returned — the only reliable test when
    the eyes sit on the robot's internal hub next to identically-chipped devices.
    ``verify=False`` falls back to VID/PID-and-name guessing, which is faster but
    can hand back the robot's own motor controller.
    """
    pinned = os.environ.get(ENV_PORT)
    if pinned:
        logger.debug("using %s from $%s", pinned, ENV_PORT)
        return pinned

    candidates = usb_ports()
    if not candidates:
        raise PortNotFoundError(
            f"No USB serial port found. Ports the OS reports:\n{describe_ports()}\n"
            "Is the ESP32 plugged in (and on a data cable, not a charge-only one)?"
        )

    if not verify:
        likely = [p.device for p in candidates if _looks_like_esp32(p)]
        if len(likely) == 1:
            return likely[0]
        raise PortNotFoundError(
            "Cannot pick a port by VID/PID alone — pass one explicitly:\n"
            + describe_ports()
        )

    for info in candidates:
        if probe_port(info.device, baud, timeout=probe_timeout) is not None:
            logger.debug("found eyes on %s", info.device)
            return info.device

    raise PortNotFoundError(
        "No serial port answered the eyes' VERSION handshake. Tried:\n"
        + "\n".join(f"  {p.device}  ({describe_port(p)})" for p in candidates)
        + "\nA port that opens but stays silent is some other device (inside a "
        "Reachy Mini, the motor controller is a CH343 like many ESP32 boards).\n"
        f"If the eyes are on a port that answers slowly, set ${ENV_PORT} to it."
    )


# ── transport ────────────────────────────────────────────────────────────────
class SerialTransport:
    """A line-oriented, thread-safe serial link to the eyes firmware."""

    def __init__(
        self,
        port: str | None = None,
        baud: int = DEFAULT_BAUD,
        *,
        settle: float | None = None,
        read_timeout: float = READ_TIMEOUT_S,
    ) -> None:
        self._port = port
        self._baud = baud
        self._settle = SETTLE_S if settle is None else settle
        self._read_timeout = read_timeout
        self._ser: serial.Serial | None = None
        self._rx = bytearray()  # bytes read past the last newline
        # Re-entrant so a whole exchange (write + read) can be held while the
        # individual write/read helpers take the lock again.
        self._lock = threading.RLock()

    # ── lifecycle ────────────────────────────────────────────────────────────
    @property
    def port(self) -> str | None:
        return self._port

    @property
    def baud(self) -> int:
        return self._baud

    @property
    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open

    def open(self) -> None:
        """Open the port (auto-detecting it if none was given). Idempotent."""
        with self._lock:
            if self.is_open:
                return
            if self._port is None:
                self._port = find_port()
            try:
                self._ser = serial.Serial(self._port, self._baud, timeout=self._read_timeout)
            except serial.SerialException as exc:
                raise ConnectionFailedError(
                    f"Could not open serial port {self._port!r}: {exc}\n"
                    f"Available ports:\n{describe_ports()}"
                ) from exc
            if self._settle > 0:
                time.sleep(self._settle)
            self.discard_input()
            logger.debug("opened %s at %d baud", self._port, self._baud)

    def close(self) -> None:
        """Close the port. Idempotent."""
        with self._lock:
            if self._ser is not None:
                try:
                    self._ser.close()
                finally:
                    self._ser = None
            self._rx.clear()

    def reopen(self, delay: float = 1.0) -> None:
        """Close, wait, and open again — e.g. after the device reboots."""
        self.close()
        if delay > 0:
            time.sleep(delay)
        self.open()

    def __enter__(self) -> "SerialTransport":
        self.open()
        return self

    def __exit__(self, *exc_info) -> None:
        self.close()

    # ── raw I/O ──────────────────────────────────────────────────────────────
    def _require(self) -> serial.Serial:
        if not self.is_open:
            raise NotConnectedError("serial port is not open — call open() first")
        assert self._ser is not None
        return self._ser

    def discard_input(self) -> None:
        """Drop anything already received (stale acks, unsolicited ``ERR``)."""
        with self._lock:
            self._rx.clear()
            if self.is_open:
                assert self._ser is not None
                self._ser.reset_input_buffer()

    def write_line(self, line: str) -> None:
        """Send one command line. Does not wait for a reply."""
        with self._lock:
            ser = self._require()
            ser.write((line + "\n").encode())
            ser.flush()

    def write_bytes(self, data: bytes, chunk: int = CHUNK_SIZE) -> None:
        """Stream a raw binary payload in firmware-sized chunks."""
        with self._lock:
            ser = self._require()
            for start in range(0, len(data), chunk):
                ser.write(data[start : start + chunk])
                ser.flush()

    def read_line(self, timeout: float = DEFAULT_ACK_TIMEOUT) -> str | None:
        """Read one LF-terminated line, or ``None`` if the timeout expires.

        CR is stripped, so CRLF and LF endings both work.
        """
        deadline = time.monotonic() + timeout
        with self._lock:
            ser = self._require()
            while True:
                if b"\n" in self._rx:
                    raw, _, rest = bytes(self._rx).partition(b"\n")
                    self._rx = bytearray(rest)
                    return raw.decode(errors="replace").strip()
                # read_until returns early on '\n', or after the port's own
                # read timeout — which is what makes the deadline check work.
                chunk = ser.read_until(b"\n")
                if chunk:
                    self._rx += chunk
                    continue
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                # Only reached if the port returned without blocking for its own
                # read timeout — don't spin the CPU while waiting out the deadline.
                time.sleep(min(0.005, remaining))

    # ── exchanges ────────────────────────────────────────────────────────────
    def exchange(self, line: str, timeout: float = DEFAULT_ACK_TIMEOUT) -> str | None:
        """Send a command and return its single reply line (``None`` on timeout).

        Pending input is discarded first so a stale ``ERR`` from an earlier
        streamed command can't be mistaken for this command's reply.
        """
        with self._lock:
            self.discard_input()
            self.write_line(line)
            return self.read_line(timeout)

    def upload(
        self,
        header: str,
        data: bytes,
        *,
        ready_timeout: float = 15.0,
        done_timeout: float = 30.0,
        chunk: int = CHUNK_SIZE,
    ) -> str:
        """Run a header → ``READY`` → payload → reply binary upload.

        Returns the final reply line (``OK <crc32>`` or ``ERR <reason>``) for the
        caller to interpret. A bad or missing handshake raises
        :class:`CommandError` / :class:`ProtocolError`.
        """
        command = header.split()[0]
        with self._lock:
            self.discard_input()
            self.write_line(header)
            # The device may erase flash before answering (OTA slot, GIF slot),
            # which is why the READY timeout is generous.
            reply = self.read_line(ready_timeout)
            if reply is None:
                raise ProtocolError(f"{command}: no READY within {ready_timeout:g}s")
            if reply.startswith("ERR"):
                raise CommandError(command, reply[3:].strip() or "unspecified")
            if reply != "READY":
                raise ProtocolError(f"{command}: expected READY, got {reply!r}")

            self.write_bytes(data, chunk=chunk)

            reply = self.read_line(done_timeout)
            if reply is None:
                raise ProtocolError(f"{command}: no reply after payload (timeout)")
            return reply
