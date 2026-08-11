"""Value types shared across the SDK: eye selection, gaze directions, results."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Union


class _Token(str, Enum):
    """A string enum whose members are the exact protocol tokens."""

    def __str__(self) -> str:  # so f-strings emit "LEFT", not "Eyes.LEFT"
        return self.value

    @classmethod
    def coerce(cls, value) -> "_Token":
        """Accept a member or a (case-insensitive) token string."""
        if isinstance(value, cls):
            return value
        if isinstance(value, str):
            try:
                return cls(value.strip().upper())
            except ValueError:
                pass
        allowed = ", ".join(m.value for m in cls)
        raise ValueError(f"invalid {cls.__name__}: {value!r} (expected one of: {allowed})")


class Eyes(_Token):
    """Which eye(s) a command applies to."""

    BOTH = "BOTH"
    LEFT = "LEFT"
    RIGHT = "RIGHT"


class Direction(_Token):
    """High-level gaze directions understood by the ``GAZE`` command."""

    CENTER = "CENTER"
    N = "N"
    NE = "NE"
    E = "E"
    SE = "SE"
    S = "S"
    SW = "SW"
    W = "W"
    NW = "NW"


EyesLike = Union[Eyes, str]
DirectionLike = Union[Direction, str]


@dataclass(frozen=True)
class GifInfo:
    """What the device reports about the GIF currently in its storage slot."""

    size_bytes: int
    crc32: int

    def __str__(self) -> str:
        return f"{self.size_bytes} bytes, crc32={self.crc32:08X}"


class UpdateStatus(str, Enum):
    """Outcome of :meth:`~reachy_eyes.client.EyesClient.update_firmware`."""

    UPDATED = "updated"
    UP_TO_DATE = "up-to-date"
    NO_DEVICE = "no-device"
    PUSHED_UNVERIFIED = "pushed-unverified"

    def __str__(self) -> str:
        return self.value
