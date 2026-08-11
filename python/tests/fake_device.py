"""In-memory fake of the eyes firmware's serial protocol, for tests.

Implements just enough of ``esp32/src/serial_protocol.cpp`` to exercise the SDK
without hardware: line dispatch, the silent ``LOOK``, ``VERSION``/``GIFINFO``
replies, and the two binary uploads with their ``READY`` handshake and CRC ack.
"""

from __future__ import annotations

import zlib

from reachy_eyes.client import EyesClient
from reachy_eyes.transport import SerialTransport

# Commands the firmware answers with a plain "OK".
_ACK_COMMANDS = {
    "IDLE",
    "BLINK",
    "GAZE",
    "SQUINT",
    "HEARTS",
    "MONEY",
    "DEAD",
    "TRAPEZOID",
    "BLINK_INTERVAL",
    "STOPGIF",
}


class FakeSerial:
    """Stand-in for ``serial.Serial`` that behaves like the ESP32."""

    def __init__(self, version: str = "1.0.0") -> None:
        self.is_open = True
        self.version = version
        self.gif: bytes | None = None
        self.rebooted = False
        self.answer_version = True  # False simulates pre-OTA firmware
        self.sent: list[str] = []  # command lines received from the host
        self.crc_override: str | None = None  # forced upload ack, e.g. "OK DEADBEEF"
        self._rx = bytearray()  # partial host line
        self._tx = bytearray()  # bytes waiting to be read by the host
        self._bin_remaining = 0
        self._bin_buf = bytearray()
        self._bin_kind: str | None = None

    # ── the serial.Serial surface SerialTransport uses ───────────────────────
    def write(self, data: bytes) -> int:
        if self._bin_remaining:
            take = min(self._bin_remaining, len(data))
            self._bin_buf += data[:take]
            self._bin_remaining -= take
            if self._bin_remaining == 0:
                self._finish_binary()
            return len(data)
        self._rx += data
        while b"\n" in self._rx:
            line, _, rest = bytes(self._rx).partition(b"\n")
            self._rx = bytearray(rest)
            self._dispatch(line.decode().strip())
        return len(data)

    def flush(self) -> None:
        pass

    def read_until(self, expected: bytes = b"\n") -> bytes:
        idx = self._tx.find(expected)
        if idx < 0:
            out, self._tx = bytes(self._tx), bytearray()
            return out
        out = bytes(self._tx[: idx + 1])
        self._tx = self._tx[idx + 1 :]
        return out

    def reset_input_buffer(self) -> None:
        self._tx.clear()

    def close(self) -> None:
        self.is_open = False

    # ── device behavior ──────────────────────────────────────────────────────
    def _reply(self, text: str) -> None:
        self._tx += (text + "\r\n").encode()  # CRLF, as Serial.println does

    def _dispatch(self, line: str) -> None:
        self.sent.append(line)
        cmd, _, args = line.partition(" ")
        if cmd == "LOOK":
            if len(args.split()) < 2:  # otherwise silent, like the firmware
                self._reply("ERR look needs x y")
        elif cmd == "VERSION":
            if self.answer_version:
                self._reply(f"VERSION {self.version}")
        elif cmd == "GIFINFO":
            if self.gif is None:
                self._reply("NONE")
            else:
                self._reply(f"INFO {len(self.gif)} {_crc(self.gif):08X}")
        elif cmd in ("GIFUPLOAD", "FWUPDATE"):
            self._bin_remaining = int(args.split()[0])
            self._bin_buf = bytearray()
            self._bin_kind = cmd
            self._reply("READY")
        elif cmd == "PLAYGIF":
            self._reply("OK" if self.gif else "ERR no gif")
        elif cmd in _ACK_COMMANDS:
            self._reply("OK")
        else:
            self._reply(f"ERR unknown: {cmd}")

    def _finish_binary(self) -> None:
        payload = bytes(self._bin_buf)
        if self._bin_kind == "GIFUPLOAD":
            self.gif = payload
        else:
            self.rebooted = True
            self.version = "1.1.0"  # as if booted into the pushed image
        self._reply(self.crc_override or f"OK {_crc(payload):08X}")


def _crc(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def fake_client(version: str = "1.0.0", **client_kwargs) -> tuple[EyesClient, FakeSerial]:
    """An :class:`EyesClient` wired to a :class:`FakeSerial`, already "open"."""
    device = FakeSerial(version)
    transport = SerialTransport("/dev/fake", settle=0.0)
    transport._ser = device  # skip the real serial.Serial open
    client = EyesClient(transport=transport, connect=False, **client_kwargs)
    return client, device
