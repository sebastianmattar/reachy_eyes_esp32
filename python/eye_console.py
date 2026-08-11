#!/usr/bin/env python3
"""Interactive keyboard console for the real eyes — the simulator's key map, on hardware.

The desktop simulator (``pio run -e native_sim``) lets you poke the eye logic with
single keypresses. This is the same idea against a connected ESP32: it reads keys
from the terminal and drives them through the SDK, echoing the protocol line that
went over the wire plus anything the device replied.

The bindings mirror the simulator's (``esp32/src/sim/main_sim.cpp``) and add the
things that only exist on hardware: the stored GIF slot, per-eye targeting, the
streamed ``LOOK`` gaze, and version/GIF queries.

On startup it sends ``VERSION`` as a protocol check — opening the port only
proves a serial device is there, a well-formed reply proves the eyes are the
ones listening. A failed check is reported, not fatal.

Usage:
    python3 eye_console.py                                  # auto-detect the port
    python3 eye_console.py --list-ports                     # which port is the eyes?
    python3 eye_console.py --port /dev/cu.usbmodemXXXX
    python3 eye_console.py --gif heart.gif                  # what "u" uploads

Requires: pyserial and a terminal (POSIX: macOS/Linux). Firmware updates live in
ota_update.py; this tool never touches flash.
"""

from __future__ import annotations

import argparse
import os
import re
import select
import sys
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from reachy_eyes import Direction, Eyes, EyesClient, EyesError
from reachy_eyes.transport import SerialTransport, describe_port, probe_port, usb_ports

DEFAULT_GIF = Path(__file__).resolve().parent.parent / "esp32" / "gifs" / "test.gif"

# Timings copied from the simulator's key bindings, so both feel the same.
GAZE_TRANSITION_S = 0.1
GAZE_RESET_S = 2.0
SQUINT_TRANSITION_S = 0.3
SQUINT_RESET_S = 1.5

LOOK_STEP = 0.25  # how far one shift+arrow nudges the streamed gaze target
LOOK_TRANSITION_S = 0.15
LIVELY_BLINK_INTERVAL = (1.0, 2.0)
DEFAULT_BLINK_INTERVAL = (3.0, 6.0)


# ── terminal key reading ─────────────────────────────────────────────────────
_SPECIAL = {" ": "SPACE", "\r": "ENTER", "\n": "ENTER", "\x03": "CTRL-C", "\x04": "EOF"}
_ARROWS = {"A": "UP", "B": "DOWN", "C": "RIGHT", "D": "LEFT"}
# CSI arrow, optionally with a modifier ("\x1b[1;2A" = shift+up), or the
# application-mode form ("\x1bOA") some terminals send.
_ESCAPE_ARROW = re.compile(r"(?:\[|O)(?:1;(\d+))?([A-D])\Z")


class KeyReader:
    """Reads one keypress at a time from the terminal, unbuffered.

    Uses cbreak mode (no echo, no line buffering, Ctrl-C still interrupts) and
    reads the raw fd directly so multi-byte escape sequences arrive intact.
    """

    def __init__(self) -> None:
        self._fd = sys.stdin.fileno()
        self._saved: list | None = None

    def __enter__(self) -> "KeyReader":
        import termios
        import tty

        self._saved = termios.tcgetattr(self._fd)
        tty.setcbreak(self._fd)
        return self

    def __exit__(self, *exc_info) -> None:
        self._restore()

    def _restore(self) -> None:
        import termios

        if self._saved is not None:
            termios.tcsetattr(self._fd, termios.TCSADRAIN, self._saved)

    @contextmanager
    def cooked(self):
        """Temporarily restore normal line editing (for typing a whole line)."""
        import tty

        self._restore()
        try:
            yield
        finally:
            tty.setcbreak(self._fd)

    def read_key(self) -> str:
        """Block until a key is pressed; return a normalized name for it."""
        data = os.read(self._fd, 1)
        if not data:
            return "EOF"
        ch = data.decode(errors="replace")
        if ch != "\x1b":
            return _SPECIAL.get(ch, ch)
        return _name_escape(self._read_pending())

    def _read_pending(self, limit: int = 8) -> str:
        """Drain the rest of an escape sequence (nothing follows a bare Esc)."""
        out = ""
        while len(out) < limit and select.select([self._fd], [], [], 0.05)[0]:
            char = os.read(self._fd, 1).decode(errors="replace")
            out += char
            if len(out) == 1 and char in "[O":
                continue  # sequence introducer, never the final byte
            if char.isalpha() or char == "~":
                break
        return out


def _name_escape(seq: str) -> str:
    if not seq:
        return "ESC"
    m = _ESCAPE_ARROW.match(seq)
    if m:
        name = _ARROWS[m.group(2)]
        return f"S-{name}" if m.group(1) == "2" else name  # 2 = shift
    return "ESC" + seq  # unknown sequence: shown, but unbound


# ── wire echo ────────────────────────────────────────────────────────────────
class EchoTransport(SerialTransport):
    """Transport that remembers the last line it wrote.

    Lets the console print exactly what went over the wire, the way the
    simulator's key map documents the commands each key enqueues.
    """

    last_line: str | None = None

    def write_line(self, line: str) -> None:
        self.last_line = line
        super().write_line(line)


# ── key bindings ─────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class Action:
    """One key binding.

    ``run`` takes the pressed key (bindings with several keys branch on it) and
    returns either ``None`` — the console then echoes the line that went over the
    wire — or a message to print instead.
    """

    keys: tuple[str, ...]
    help: str
    run: Callable[[str], object]
    label: str = field(default="")

    @property
    def display(self) -> str:
        return self.label or " / ".join(self.keys)


class EyeConsole:
    """Maps keypresses onto SDK calls against a connected pair of eyes."""

    def __init__(self, eyes: EyesClient, transport: EchoTransport, gif: Path) -> None:
        self.eyes = eyes
        self.transport = transport
        self.gif = gif
        self.target = Eyes.BOTH  # which eye(s) subsequent commands address
        self.look_xy = (0.0, 0.0)  # streamed gaze target, nudged by shift+arrows
        self.lively_blink = False
        self.keys: KeyReader | None = None
        self.actions = self._bindings()
        self.by_key = {key: a for a in self.actions for key in a.keys}

    # ── the table ────────────────────────────────────────────────────────────
    def _bindings(self) -> list[Action]:
        return [
            # Mirrors of the simulator's bindings.
            Action(("b", "B"), "blink once / three times", self._blink, "b / B"),
            Action(("h",), "hearts", lambda _k: self.eyes.hearts(self.target)),
            Action(("m",), "money eyes", lambda _k: self.eyes.money(self.target)),
            Action(("d",), "dead eyes", lambda _k: self.eyes.dead(self.target)),
            Action(("t",), "trapezoid style", lambda _k: self.eyes.trapezoid(self.target)),
            Action(("s",), "squint", self._squint),
            Action(("i", "SPACE"), "idle (autonomous wander + blink)", self._idle, "i / space"),
            Action(("UP", "DOWN", "LEFT", "RIGHT"), "dart the gaze N / S / W / E",
                   self._gaze, "↑ ↓ ← →"),
            Action(("g",), "play the stored gif", lambda _k: self.eyes.play_gif()),
            Action(("x",), "stop gif playback", lambda _k: self.eyes.stop_gif()),
            # Hardware-only additions.
            Action(("S-UP", "S-DOWN", "S-LEFT", "S-RIGHT"),
                   f"nudge the streamed LOOK target by {LOOK_STEP}",
                   self._nudge_look, "shift+↑↓←→"),
            Action(("c",), "recentre the LOOK target", self._center_look),
            Action(("1", "2", "3"), "target both / left / right eye", self._set_target,
                   "1 / 2 / 3"),
            Action(("k",), "toggle lively auto-blink interval", self._toggle_blink_interval),
            Action(("u",), f"upload {self.gif.name} and play it", self._upload_gif),
            Action(("n",), "what gif is stored?", self._gif_info),
            Action(("v",), "firmware version", self._version),
            Action((":",), "type a raw protocol line", self._raw_line),
            Action(("?",), "show this key map", self._show_help),
            Action(("q", "ESC"), "quit (leaves the eyes idle)", lambda _k: None, "q / esc"),
        ]

    # ── actions ──────────────────────────────────────────────────────────────
    def _blink(self, key: str) -> None:
        self.eyes.blink(3 if key == "B" else 1, self.target)

    def _squint(self, _key: str) -> None:
        self.eyes.squint(SQUINT_TRANSITION_S, SQUINT_RESET_S)

    def _idle(self, _key: str) -> None:
        self.look_xy = (0.0, 0.0)
        self.eyes.idle()

    def _gaze(self, key: str) -> None:
        direction = {
            "UP": Direction.N,
            "DOWN": Direction.S,
            "LEFT": Direction.W,
            "RIGHT": Direction.E,
        }[key]
        self.eyes.gaze(direction, GAZE_TRANSITION_S, GAZE_RESET_S, self.target)

    def _nudge_look(self, key: str) -> None:
        dx, dy = {
            "S-UP": (0.0, -LOOK_STEP),  # screen y is down-positive
            "S-DOWN": (0.0, LOOK_STEP),
            "S-LEFT": (-LOOK_STEP, 0.0),
            "S-RIGHT": (LOOK_STEP, 0.0),
        }[key]
        x = _clamp(self.look_xy[0] + dx)
        y = _clamp(self.look_xy[1] + dy)
        self.look_xy = (x, y)
        self.eyes.look(x, y, LOOK_TRANSITION_S, self.target)

    def _center_look(self, _key: str) -> None:
        self.look_xy = (0.0, 0.0)
        self.eyes.look(0.0, 0.0, LOOK_TRANSITION_S, self.target)

    def _set_target(self, key: str) -> str:
        self.target = {"1": Eyes.BOTH, "2": Eyes.LEFT, "3": Eyes.RIGHT}[key]
        return f"commands now target: {self.target}"

    def _toggle_blink_interval(self, _key: str) -> None:
        self.lively_blink = not self.lively_blink
        interval = LIVELY_BLINK_INTERVAL if self.lively_blink else DEFAULT_BLINK_INTERVAL
        self.eyes.set_blink_interval(*interval)

    def _upload_gif(self, _key: str) -> str:
        if not self.gif.exists():
            return f"✗ no such file: {self.gif}"
        size = self.gif.stat().st_size
        print(f"       uploading {self.gif} ({size} bytes)...", flush=True)
        crc = self.eyes.upload_gif(self.gif, play=True)
        return f"GIFUPLOAD {self.gif.name} → crc {crc:08X}, playing"

    def _gif_info(self, _key: str) -> str:
        info = self.eyes.gif_info()
        return f"stored gif: {info}" if info else "stored gif: none"

    def _version(self, _key: str) -> str:
        version = self.eyes.version()
        return f"firmware {version}" if version else "no VERSION reply"

    def _show_help(self, _key: str) -> str:
        self.print_help()
        return "(key map)"

    def _raw_line(self, _key: str) -> str:
        assert self.keys is not None  # only reachable from run()'s loop
        with self.keys.cooked():
            try:
                line = input("       : ").strip()
            except (EOFError, KeyboardInterrupt):
                return "(cancelled)"
        if not line:
            return "(cancelled)"
        reply = self.eyes.send_raw(line)
        # LOOK is silent by design, so "no reply" is not necessarily an error.
        return f"{line} → {reply if reply is not None else '(no reply)'}"

    # ── loop ─────────────────────────────────────────────────────────────────
    def print_help(self) -> None:
        width = max(len(a.display) for a in self.actions)
        print("\n  Key" + " " * (width - 1) + "Action")
        for action in self.actions:
            print(f"  {action.display:<{width}}  {action.help}")
        print()

    def handshake(self) -> bool:
        """Send ``VERSION`` and report whether the device speaks the protocol.

        This is the console's smoke test: the port opening only proves a serial
        device is there, while a well-formed ``VERSION`` reply proves the eyes
        are on the other end and listening. Returns False when the device did
        not answer or answered with something the protocol doesn't allow — the
        console still runs, since pre-OTA firmware has no ``VERSION``, but every
        later command is then a guess.
        """
        print(f"\nEyes console — {self.eyes.port} @ {self.transport.baud} baud")
        print("  → VERSION", flush=True)
        try:
            version = self.eyes.version()
        except EyesError as exc:
            print(f"  ✗ {exc}")
            print("    The device answered, but not with a VERSION line. Wrong "
                  "port, or something else is talking on it?")
            return False
        if version is None:
            print("  ✗ no reply — protocol check failed.")
            print("    Check the port (--list-ports), the baud rate, and that "
                  "nothing else holds the port open. Firmware older than OTA "
                  "has no VERSION command.")
            return False
        print(f"  ✓ firmware {version} — protocol OK")
        return True

    def run(self) -> None:
        self.handshake()
        self.print_help()
        with KeyReader() as keys:
            self.keys = keys
            while True:
                key = keys.read_key()
                if key in ("q", "ESC", "CTRL-C", "EOF"):
                    return
                self._dispatch(key)

    def _dispatch(self, key: str) -> None:
        action = self.by_key.get(key)
        if action is None:
            print(f"  [{key}] unbound — press ? for the key map")
            return
        self.transport.last_line = None
        try:
            result = action.run(key)
        except EyesError as exc:
            print(f"  [{key}] ✗ {exc}")
            return
        if isinstance(result, str):
            print(f"  [{key}] {result}")
        else:
            print(f"  [{key}] {self.transport.last_line or '(nothing sent)'}")


def _clamp(v: float, lo: float = -1.0, hi: float = 1.0) -> float:
    return max(lo, min(hi, v))


def list_ports(baud: int) -> None:
    """Print every USB serial port and whether it answers as the eyes.

    The identifying test is the ``VERSION`` handshake, not the USB IDs: a Reachy
    Mini's own motor controller uses the same CH343 chip as many ESP32 boards.
    """
    ports = usb_ports()
    if not ports:
        print("No USB serial ports. Is the ESP32 plugged in on a data cable?")
        return
    print(f"\nProbing {len(ports)} USB serial port(s) at {baud} baud:\n")
    for info in ports:
        version = probe_port(info.device, baud)
        verdict = (
            f"EYES — firmware {version or 'unknown'}" if version is not None
            else "not the eyes (no VERSION reply)"
        )
        print(f"  {info.device}\n      {describe_port(info)}\n      → {verdict}")
    print()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="ESP32 serial port; omit to auto-detect")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--gif", type=Path, default=DEFAULT_GIF,
                    help=f"gif the 'u' key uploads (default: {DEFAULT_GIF})")
    ap.add_argument("--list-ports", action="store_true",
                    help="probe every serial port, report which one is the eyes, and exit")
    args = ap.parse_args()

    if args.list_ports:
        list_ports(args.baud)
        return

    if not sys.stdin.isatty():
        sys.exit("eye_console.py needs an interactive terminal (stdin is not a tty).")
    if os.name != "posix":
        sys.exit("eye_console.py needs a POSIX terminal (macOS/Linux).")

    transport = EchoTransport(args.port, args.baud)
    try:
        eyes = EyesClient(transport=transport)
    except EyesError as exc:
        sys.exit(str(exc))

    console = EyeConsole(eyes, transport, args.gif)
    try:
        console.run()
    except KeyboardInterrupt:
        pass
    finally:
        eyes.close(idle=True)  # hand the eyes back to their own behavior
        print("\nBye — eyes back to autonomous mode.")


if __name__ == "__main__":
    main()
