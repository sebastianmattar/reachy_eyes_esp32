"""Exceptions raised by the Reachy eyes SDK.

Everything derives from :class:`EyesError`, so a caller that only wants "did the
eyes work?" can catch that one class.
"""

from __future__ import annotations


class EyesError(Exception):
    """Base class for every error raised by this SDK."""


class PortNotFoundError(EyesError):
    """No serial port was given and auto-discovery could not pick one."""


class ConnectionFailedError(EyesError):
    """The serial port exists but could not be opened."""


class NotConnectedError(EyesError):
    """A command was issued on a client whose port is not open."""


class EyesTimeoutError(EyesError):
    """The device did not answer within the timeout."""


class ProtocolError(EyesError):
    """The device answered, but not with anything the protocol allows."""


class CommandError(EyesError):
    """The device replied ``ERR <reason>``."""

    def __init__(self, command: str, reason: str) -> None:
        super().__init__(f"{command!r} failed: {reason}")
        self.command = command
        self.reason = reason


class CrcMismatchError(EyesError):
    """The CRC-32 the device computed over an upload differs from ours."""

    def __init__(self, device_crc: int | None, local_crc: int) -> None:
        dev = f"{device_crc:08X}" if device_crc is not None else "missing"
        super().__init__(f"CRC mismatch: device={dev} local={local_crc:08X}")
        self.device_crc = device_crc
        self.local_crc = local_crc
