"""Tests for eye_console.py: escape-sequence decoding and the key bindings.

The bindings are checked by dispatching keys at a console wired to the fake
firmware and asserting on the protocol lines it produced — no terminal involved.
"""

from __future__ import annotations

import io
import sys
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # python/

import eye_console  # noqa: E402
from eye_console import EyeConsole, _name_escape  # noqa: E402
from fake_device import FakeSerial  # noqa: E402

from reachy_eyes import EyesClient  # noqa: E402

GIF = Path(__file__).resolve().parents[2] / "esp32" / "gifs" / "test.gif"


class TestEscapeDecoding(unittest.TestCase):
    def test_plain_arrows(self):
        self.assertEqual(_name_escape("[A"), "UP")
        self.assertEqual(_name_escape("[B"), "DOWN")
        self.assertEqual(_name_escape("[C"), "RIGHT")
        self.assertEqual(_name_escape("[D"), "LEFT")

    def test_application_mode_arrows(self):
        self.assertEqual(_name_escape("OA"), "UP")
        self.assertEqual(_name_escape("OD"), "LEFT")

    def test_shift_arrows(self):
        self.assertEqual(_name_escape("[1;2A"), "S-UP")
        self.assertEqual(_name_escape("[1;2C"), "S-RIGHT")

    def test_other_modifiers_fall_back_to_the_plain_arrow(self):
        self.assertEqual(_name_escape("[1;5A"), "UP")  # ctrl+up

    def test_bare_escape_and_unknown_sequences(self):
        self.assertEqual(_name_escape(""), "ESC")
        self.assertEqual(_name_escape("[5~"), "ESC[5~")  # page-up: unbound


class TestBindings(unittest.TestCase):
    def setUp(self):
        self.device = FakeSerial()
        self.transport = eye_console.EchoTransport("/dev/fake", settle=0.0)
        self.transport._ser = self.device
        # Short ack timeout: the fake answers instantly, so the only thing the
        # default would buy is a 5s wait in the "device says nothing" tests.
        self.eyes = EyesClient(transport=self.transport, connect=False, ack_timeout=0.2)
        self.console = EyeConsole(self.eyes, self.transport, GIF)

    def press(self, *keys: str) -> str:
        out = io.StringIO()
        with redirect_stdout(out):
            for key in keys:
                self.console._dispatch(key)
        return out.getvalue()

    def test_every_key_is_bound_to_a_callable(self):
        for action in self.console.actions:
            for key in action.keys:
                self.assertIs(self.console.by_key[key], action)

    def test_simulator_key_parity(self):
        # Same keys the SDL simulator binds (src/sim/main_sim.cpp).
        self.press("b", "B", "h", "m", "d", "t", "s", "i", "SPACE",
                   "UP", "DOWN", "LEFT", "RIGHT", "x")
        self.assertEqual(
            self.device.sent,
            [
                "BLINK 1 BOTH",
                "BLINK 3 BOTH",
                "HEARTS BOTH",
                "MONEY BOTH",
                "DEAD BOTH",
                "TRAPEZOID BOTH",
                "SQUINT 0.30 1.50",
                "IDLE",
                "IDLE",
                "GAZE N 0.10 2.00 BOTH",
                "GAZE S 0.10 2.00 BOTH",
                "GAZE W 0.10 2.00 BOTH",
                "GAZE E 0.10 2.00 BOTH",
                "STOPGIF",
            ],
        )

    def test_eye_target_applies_to_later_commands(self):
        self.press("2", "h", "3", "b", "1", "m")
        self.assertEqual(
            self.device.sent, ["HEARTS LEFT", "BLINK 1 RIGHT", "MONEY BOTH"]
        )

    def test_shift_arrows_accumulate_a_clamped_look_target(self):
        self.press("S-RIGHT", "S-RIGHT", "S-UP")
        self.assertEqual(
            self.device.sent,
            [
                "LOOK 0.250 0.000 0.15 BOTH",
                "LOOK 0.500 0.000 0.15 BOTH",
                "LOOK 0.500 -0.250 0.15 BOTH",
            ],
        )
        self.press(*["S-LEFT"] * 8)  # walks past the edge
        self.assertEqual(self.device.sent[-1], "LOOK -1.000 -0.250 0.15 BOTH")
        self.press("c")
        self.assertEqual(self.device.sent[-1], "LOOK 0.000 0.000 0.15 BOTH")

    def test_idle_recentres_the_look_target(self):
        self.press("S-RIGHT", "i", "S-RIGHT")
        self.assertEqual(self.device.sent[-1], "LOOK 0.250 0.000 0.15 BOTH")

    def test_blink_interval_toggles(self):
        self.press("k", "k")
        self.assertEqual(
            self.device.sent, ["BLINK_INTERVAL 1.00 2.00", "BLINK_INTERVAL 3.00 6.00"]
        )

    def test_gif_upload_play_and_info(self):
        output = self.press("g")  # nothing stored yet
        self.assertIn("no gif", output)
        self.press("u")
        self.assertEqual(self.device.gif, GIF.read_bytes())
        self.assertIn("stored gif: 937 bytes", self.press("n"))
        self.press("g", "x")
        self.assertEqual(self.device.sent[-2:], ["PLAYGIF", "STOPGIF"])

    def test_missing_gif_file_is_reported_not_raised(self):
        self.console.gif = GIF.parent / "does-not-exist.gif"
        self.assertIn("no such file", self.press("u"))

    def test_version_and_help_and_unbound_keys(self):
        self.assertIn("firmware 1.0.0", self.press("v"))
        self.assertIn("blink once / three times", self.press("?"))
        self.assertIn("unbound", self.press("Z"))

    def test_device_errors_are_printed_not_fatal(self):
        self.assertIn("✗", self.press("g"))
        self.press("b")  # console keeps working afterwards
        self.assertEqual(self.device.sent[-1], "BLINK 1 BOTH")

    def test_startup_handshake_probes_the_protocol(self):
        out = io.StringIO()
        with redirect_stdout(out):
            ok = self.console.handshake()
        self.assertTrue(ok)
        self.assertEqual(self.device.sent, ["VERSION"])
        self.assertIn("firmware 1.0.0", out.getvalue())

    def test_startup_handshake_reports_a_silent_device(self):
        self.device.answer_version = False  # pre-OTA firmware, or not the eyes
        out = io.StringIO()
        with redirect_stdout(out):
            ok = self.console.handshake()
        self.assertFalse(ok)
        self.assertIn("no reply", out.getvalue())

    def test_startup_handshake_reports_a_bogus_reply(self):
        self.device.answer_version = False
        self.device._reply("HELLO")  # something else is talking on this port
        out = io.StringIO()
        with redirect_stdout(out):
            ok = self.console.handshake()
        self.assertFalse(ok)
        self.assertIn("✗", out.getvalue())

    def test_help_lists_every_binding(self):
        out = io.StringIO()
        with redirect_stdout(out):
            self.console.print_help()
        text = out.getvalue()
        for action in self.console.actions:
            self.assertIn(action.help, text)


if __name__ == "__main__":
    unittest.main()
