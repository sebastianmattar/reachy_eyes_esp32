"""Tests for the reachy_eyes SDK against an in-memory fake of the firmware.

Run from the repo root (needs pyserial only — no robot, no hardware):

    python -m unittest discover -s python/tests
"""

from __future__ import annotations

import logging
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # python/

from fake_device import fake_client  # noqa: E402

from reachy_eyes import Direction, Eyes, GifInfo, UpdateStatus, errors, firmware  # noqa: E402

GIF = b"GIF89a" + bytes(range(256)) * 7
GIF_CRC = zlib.crc32(GIF) & 0xFFFFFFFF


def setUpModule() -> None:
    # The SDK logs progress at INFO; keep the test output readable.
    logging.getLogger("reachy_eyes").setLevel(logging.CRITICAL)


class TestBehaviorCommands(unittest.TestCase):
    def setUp(self):
        self.eyes, self.device = fake_client()

    def test_commands_render_expected_protocol_lines(self):
        self.eyes.idle()
        self.eyes.blink(2, "left")  # plain strings are accepted for enums
        self.eyes.gaze(Direction.NE)
        self.eyes.gaze("sw", 0.4, 1.0, Eyes.RIGHT)
        self.eyes.squint()
        self.eyes.hearts()
        self.eyes.money(Eyes.LEFT)
        self.eyes.dead()
        self.eyes.trapezoid()
        self.eyes.set_blink_interval(1, 2)
        self.assertEqual(
            self.device.sent,
            [
                "IDLE",
                "BLINK 2 LEFT",
                "GAZE NE 0.30 2.00 BOTH",
                "GAZE SW 0.40 1.00 RIGHT",
                "SQUINT 0.30 2.00",
                "HEARTS BOTH",
                "MONEY LEFT",
                "DEAD BOTH",
                "TRAPEZOID BOTH",
                "BLINK_INTERVAL 1.00 2.00",
            ],
        )

    def test_look_clamps_and_does_not_wait_for_a_reply(self):
        self.eyes.look(2.0, -0.5)
        self.assertEqual(self.device.sent, ["LOOK 1.000 -0.500 0.15 BOTH"])

    def test_err_reply_raises_command_error(self):
        # STOPGIF acks OK normally; make the device reject it to check the path.
        self.device._dispatch = lambda line: (  # type: ignore[method-assign]
            self.device.sent.append(line) or self.device._reply("ERR nope")
        )
        with self.assertRaises(errors.CommandError) as ctx:
            self.eyes.stop_gif()
        self.assertEqual(ctx.exception.reason, "nope")

    def test_send_raw_returns_the_device_reply(self):
        self.assertEqual(self.eyes.send_raw("BOGUS"), "ERR unknown: BOGUS")
        self.assertIsNone(self.eyes.send_raw("IDLE", expect_reply=False))

    def test_bad_enum_value_raises_value_error(self):
        with self.assertRaises(ValueError):
            self.eyes.gaze("UP")
        with self.assertRaises(ValueError):
            self.eyes.blink(1, "middle")

    def test_command_on_closed_port_raises(self):
        self.eyes.close()
        with self.assertRaises(errors.NotConnectedError):
            self.eyes.idle()

    def test_close_with_idle_restores_autonomy(self):
        self.eyes.close(idle=True)
        self.assertEqual(self.device.sent[-1], "IDLE")

    def test_fire_and_forget_client_skips_acks(self):
        eyes, device = fake_client(wait_for_ack=False)
        eyes.blink()
        self.assertEqual(device.sent, ["BLINK 1 BOTH"])


class TestGif(unittest.TestCase):
    def setUp(self):
        self.eyes, self.device = fake_client()

    def test_empty_slot_reports_none(self):
        self.assertIsNone(self.eyes.gif_info())

    def test_upload_verifies_crc_and_can_play(self):
        crc = self.eyes.upload_gif(GIF, play=True)
        self.assertEqual(crc, GIF_CRC)
        self.assertEqual(self.device.gif, GIF)
        self.assertEqual(self.eyes.gif_info(), GifInfo(len(GIF), GIF_CRC))
        self.assertIn("PLAYGIF", self.device.sent)

    def test_upload_accepts_a_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "eye.gif"
            path.write_bytes(GIF)
            self.assertEqual(self.eyes.upload_gif(path), GIF_CRC)

    def test_crc_mismatch_is_reported(self):
        self.device.crc_override = "OK DEADBEEF"
        with self.assertRaises(errors.CrcMismatchError) as ctx:
            self.eyes.upload_gif(GIF)
        self.assertEqual(ctx.exception.device_crc, 0xDEADBEEF)

    def test_device_error_during_upload_is_reported(self):
        self.device.crc_override = "ERR write failed"
        with self.assertRaises(errors.CommandError):
            self.eyes.upload_gif(GIF)

    def test_empty_and_oversized_gifs_are_rejected_locally(self):
        with self.assertRaises(ValueError):
            self.eyes.upload_gif(b"")
        with self.assertRaises(ValueError):
            self.eyes.upload_gif(b"\x00" * (1024 * 1024 + 1))

    def test_play_without_a_stored_gif_raises(self):
        with self.assertRaises(errors.CommandError):
            self.eyes.play_gif()


class TestFirmwareUpdate(unittest.TestCase):
    def setUp(self):
        # Short ack timeout: two cases here deliberately let a reply time out.
        self.eyes, self.device = fake_client("1.0.0", ack_timeout=0.2)
        self._tmp = tempfile.TemporaryDirectory()
        self.image = Path(self._tmp.name) / "firmware.bin"
        self.image.write_bytes(b"\xe9" + b"firmware image" * 100)
        self.addCleanup(self._tmp.cleanup)

    def test_version_query(self):
        self.assertEqual(self.eyes.version(), "1.0.0")

    def test_older_or_equal_image_is_not_pushed(self):
        status = self.eyes.update_firmware(self.image, version="1.0.0")
        self.assertEqual(status, UpdateStatus.UP_TO_DATE)
        self.assertFalse(self.device.rebooted)

    def test_newer_image_is_pushed_and_verified(self):
        status = self.eyes.update_firmware(self.image, version="1.2.0", reboot_wait=0)
        self.assertEqual(status, UpdateStatus.UPDATED)
        self.assertTrue(self.device.rebooted)
        self.assertTrue(any(s.startswith("FWUPDATE") for s in self.device.sent))

    def test_silent_device_is_not_pushed_unless_forced(self):
        self.device.answer_version = False
        self.assertIsNone(self.eyes.version())
        self.assertEqual(
            self.eyes.update_firmware(self.image, version="9.9.9"),
            UpdateStatus.NO_DEVICE,
        )
        self.assertFalse(self.device.rebooted)

        status = self.eyes.update_firmware(
            self.image, version="9.9.9", force=True, reboot_wait=0
        )
        self.assertEqual(status, UpdateStatus.PUSHED_UNVERIFIED)
        self.assertTrue(self.device.rebooted)

    def test_missing_image_raises(self):
        with self.assertRaises(FileNotFoundError):
            self.eyes.update_firmware(self.image.parent / "nope.bin", version="9.9.9")


class TestFirmwareHelpers(unittest.TestCase):
    def test_version_comes_from_the_real_config_header(self):
        version = firmware.parse_config_version()
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")

    def test_semver_comparison(self):
        self.assertTrue(firmware.is_newer("1.0.1", "1.0.0"))
        self.assertTrue(firmware.is_newer("1.1.0", "1.0.9"))
        self.assertFalse(firmware.is_newer("1.0.0", "1.0.0"))
        self.assertFalse(firmware.is_newer("0.9.0", "1.0.0"))


if __name__ == "__main__":
    unittest.main()
