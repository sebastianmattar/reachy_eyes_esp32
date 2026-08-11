"""Tests for port discovery — the "which of these is the eyes?" problem.

Inside a Reachy Mini the eyes share their USB-serial chip (WCH CH343,
``1a86:55d3``) with the robot's own motor controller, so VID/PID cannot tell them
apart. Discovery therefore probes with the ``VERSION`` handshake; these tests pin
that behavior down with fake ports.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # python/

from fake_device import FakeSerial  # noqa: E402

from reachy_eyes import EyesClient, errors, transport  # noqa: E402


def port_info(device: str, vid: int = 0x1A86, pid: int = 0x55D3, **kwargs) -> SimpleNamespace:
    return SimpleNamespace(
        device=device,
        vid=vid,
        pid=pid,
        serial_number=kwargs.get("serial_number", "SN123"),
        description=kwargs.get("description", "USB Single Serial"),
    )


class MuteSerial(FakeSerial):
    """A serial device that never answers — e.g. the robot's motor controller."""

    def _dispatch(self, line: str) -> None:
        self.sent.append(line)


class EchoSerial(FakeSerial):
    """A serial device that mirrors input back (observed: a MOTU audio interface)."""

    def _dispatch(self, line: str) -> None:
        self.sent.append(line)
        self._reply(line)


class DiscoveryTestCase(unittest.TestCase):
    """Patches the port list and ``serial.Serial`` with per-port fake devices."""

    def setUp(self):
        self.devices: dict[str, FakeSerial] = {}
        self.ports: list[SimpleNamespace] = []
        self.unopenable: set[str] = set()

        patches = [
            mock.patch.object(transport, "SETTLE_S", 0.0),  # no settle wait in tests
            mock.patch.object(transport, "PROBE_TIMEOUT_S", 0.01),
            mock.patch.object(transport, "PROBE_QUICK_TIMEOUT_S", 0.01),
            mock.patch.object(transport, "available_ports", lambda: list(self.ports)),
            mock.patch.object(transport.serial, "Serial", self._open),
            mock.patch.dict("os.environ", {}, clear=False),
        ]
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)
        import os

        os.environ.pop(transport.ENV_PORT, None)

    def _open(self, port: str, baud: int, timeout: float | None = None) -> FakeSerial:
        if port in self.unopenable:
            raise serial.SerialException("resource busy")
        device = self.devices.get(port)
        if device is None:
            raise serial.SerialException(f"no such device: {port}")
        device.is_open = True
        return device

    # ── fixture helpers ──────────────────────────────────────────────────────
    def add_eyes(self, device: str, version: str = "1.0.0", **info) -> FakeSerial:
        self.ports.append(port_info(device, **info))
        self.devices[device] = FakeSerial(version)
        return self.devices[device]

    def add_mute(self, device: str, **info) -> FakeSerial:
        self.ports.append(port_info(device, **info))
        self.devices[device] = MuteSerial()
        return self.devices[device]

    def add_echo(self, device: str, **info) -> FakeSerial:
        self.ports.append(port_info(device, **info))
        self.devices[device] = EchoSerial()
        return self.devices[device]

    def find(self, **kwargs) -> str:
        kwargs.setdefault("probe_timeout", 0.01)
        return transport.find_port(**kwargs)


class TestPortListing(DiscoveryTestCase):
    def test_non_usb_ports_are_never_candidates(self):
        # Opening /dev/cu.Bluetooth-Incoming-Port can block, so it must be dropped.
        self.ports.append(port_info("/dev/cu.Bluetooth-Incoming-Port", vid=None, pid=None))
        self.ports.append(port_info("/dev/cu.debug-console", vid=None, pid=None))
        self.add_eyes("/dev/cu.usbmodem1")
        self.assertEqual([p.device for p in transport.usb_ports()], ["/dev/cu.usbmodem1"])

    def test_likely_boards_are_probed_first(self):
        self.add_eyes("/dev/cu.someotherthing", vid=0x1234, pid=0x5678)
        self.add_eyes("/dev/cu.usbmodem1")
        self.assertEqual(
            [p.device for p in transport.usb_ports()],
            ["/dev/cu.usbmodem1", "/dev/cu.someotherthing"],
        )

    def test_describe_port_shows_ids_and_serial(self):
        text = transport.describe_port(port_info("/dev/cu.usbmodem1"))
        self.assertIn("1a86:55d3", text)
        self.assertIn("sn=SN123", text)


class TestProbe(DiscoveryTestCase):
    def test_probe_returns_the_firmware_version(self):
        device = self.add_eyes("/dev/cu.usbmodem1", version="1.2.3")
        self.assertEqual(transport.probe_port("/dev/cu.usbmodem1", timeout=0.01), "1.2.3")
        self.assertEqual(device.sent, ["VERSION"])

    def test_probe_of_a_silent_device_is_none_and_retries(self):
        device = self.add_mute("/dev/cu.usbmodem1")
        self.assertIsNone(transport.probe_port("/dev/cu.usbmodem1", timeout=0.01))
        self.assertEqual(device.sent, ["VERSION"] * transport.PROBE_ATTEMPTS)

    def test_probe_rejects_a_device_that_echoes_our_own_line(self):
        self.add_echo("/dev/cu.usbmodem-echo")
        self.assertIsNone(transport.probe_port("/dev/cu.usbmodem-echo", timeout=0.01))

    def test_version_parsing_needs_a_version_token(self):
        self.assertEqual(transport.parse_version_reply("VERSION 1.0.0"), "1.0.0")
        self.assertIsNone(transport.parse_version_reply("VERSION"))  # bare echo
        self.assertIsNone(transport.parse_version_reply("VERSION unknown"))
        self.assertIsNone(transport.parse_version_reply("OK"))

    def test_probe_of_an_unopenable_port_is_none(self):
        self.add_eyes("/dev/cu.usbmodem1")
        self.unopenable.add("/dev/cu.usbmodem1")
        self.assertIsNone(transport.probe_port("/dev/cu.usbmodem1", timeout=0.01))

    def test_discover_lists_only_responders(self):
        self.add_mute("/dev/cu.usbmodem-motors")
        self.add_eyes("/dev/cu.usbmodem-eyes", version="2.0.0")
        found = transport.discover(timeout=0.01)
        self.assertEqual([(p.device, v) for p, v in found], [("/dev/cu.usbmodem-eyes", "2.0.0")])


class TestFindPort(DiscoveryTestCase):
    def test_env_var_wins_without_probing(self):
        device = self.add_mute("/dev/cu.usbmodem1")
        with mock.patch.dict("os.environ", {transport.ENV_PORT: "/dev/pinned"}):
            self.assertEqual(self.find(), "/dev/pinned")
        self.assertEqual(device.sent, [])  # nothing was probed

    def test_skips_the_robots_identically_chipped_controller(self):
        # Exactly the Reachy Mini case: two 1a86:55d3 devices, one is the motors.
        self.add_mute("/dev/cu.usbmodem-motors")
        self.add_eyes("/dev/cu.usbmodem-eyes")
        self.assertEqual(self.find(), "/dev/cu.usbmodem-eyes")

    def test_reports_what_it_tried_when_nothing_answers(self):
        self.add_mute("/dev/cu.usbmodem-motors")
        self.add_mute("/dev/cu.usbmodem-other", description="M4", vid=0x07FD, pid=0x000B)
        with self.assertRaises(errors.PortNotFoundError) as raised:
            self.find()
        message = str(raised.exception)
        self.assertIn("/dev/cu.usbmodem-motors", message)
        self.assertIn("/dev/cu.usbmodem-other", message)
        self.assertIn(transport.ENV_PORT, message)

    def test_no_usb_ports_at_all_mentions_the_cable(self):
        with self.assertRaises(errors.PortNotFoundError) as raised:
            self.find()
        self.assertIn("charge-only", str(raised.exception))

    def test_unverified_mode_guesses_by_ids(self):
        self.add_mute("/dev/cu.usbmodem1")
        self.assertEqual(self.find(verify=False), "/dev/cu.usbmodem1")

    def test_unverified_mode_refuses_to_guess_between_two(self):
        self.add_mute("/dev/cu.usbmodem1")
        self.add_eyes("/dev/cu.usbmodem2")
        with self.assertRaises(errors.PortNotFoundError):
            self.find(verify=False)

    def test_client_auto_detect_lands_on_the_eyes(self):
        self.add_mute("/dev/cu.usbmodem-motors")
        device = self.add_eyes("/dev/cu.usbmodem-eyes")
        with EyesClient() as eyes:
            self.assertEqual(eyes.port, "/dev/cu.usbmodem-eyes")
            eyes.blink()
        self.assertIn("BLINK 1 BOTH", device.sent)


if __name__ == "__main__":
    unittest.main()
