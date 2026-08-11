"""Python SDK for the Reachy Mini ESP32 eyes.

Everything the firmware's USB-serial protocol offers, as one client object:

    from reachy_eyes import EyesClient, Direction

    with EyesClient() as eyes:        # auto-detects the serial port
        eyes.blink(2)
        eyes.gaze(Direction.NE)
        eyes.upload_gif("heart.gif", play=True)
        eyes.idle()

:class:`~reachy_eyes.bridge.EyeBridge` (head-follow) additionally needs the
``reachy_mini`` SDK, so it is imported lazily — importing this package requires
only ``pyserial``.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from . import firmware
from .client import EyesClient
from .errors import (
    CommandError,
    ConnectionFailedError,
    CrcMismatchError,
    EyesError,
    EyesTimeoutError,
    NotConnectedError,
    PortNotFoundError,
    ProtocolError,
)
from .models import Direction, Eyes, GifInfo, UpdateStatus
from .transport import (
    DEFAULT_BAUD,
    ENV_PORT,
    SerialTransport,
    available_ports,
    describe_ports,
    discover,
    find_port,
    probe_port,
    usb_ports,
)

if TYPE_CHECKING:  # for type checkers only — no runtime reachy_mini import
    from .bridge import EyeBridge, head_pose_to_xy

__version__ = "0.1.0"

__all__ = [
    "EyesClient",
    "EyeBridge",
    "head_pose_to_xy",
    "Eyes",
    "Direction",
    "GifInfo",
    "UpdateStatus",
    "SerialTransport",
    "DEFAULT_BAUD",
    "ENV_PORT",
    "find_port",
    "probe_port",
    "discover",
    "available_ports",
    "usb_ports",
    "describe_ports",
    "firmware",
    "EyesError",
    "PortNotFoundError",
    "ConnectionFailedError",
    "NotConnectedError",
    "EyesTimeoutError",
    "ProtocolError",
    "CommandError",
    "CrcMismatchError",
]

_LAZY = {"EyeBridge": "bridge", "head_pose_to_xy": "bridge"}


def __getattr__(name: str):
    """Import the robot-dependent parts only when they are actually used."""
    if name in _LAZY:
        from importlib import import_module

        return getattr(import_module(f".{_LAZY[name]}", __name__), name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


def __dir__() -> list[str]:
    return sorted(__all__)
