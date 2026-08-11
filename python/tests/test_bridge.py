"""Tests for the head-follow bridge, driven by a fake robot and a fake device.

Skipped unless the ``reachy_mini`` SDK (plus numpy/scipy) is installed, since
:mod:`reachy_eyes.bridge` is the only part of the SDK that needs it.
"""

from __future__ import annotations

import importlib.util
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # python/

from fake_device import FakeSerial  # noqa: E402

from reachy_eyes import EyesClient  # noqa: E402
from reachy_eyes.transport import SerialTransport  # noqa: E402

HAS_ROBOT_SDK = all(
    importlib.util.find_spec(m) is not None for m in ("reachy_mini", "numpy", "scipy")
)

if HAS_ROBOT_SDK:
    import numpy as np
    from scipy.spatial.transform import Rotation as R

    from reachy_eyes.bridge import (
        ANTENNA_DEFLECT_HIGH,
        MAX_YAW_RAD,
        EyeBridge,
        head_pose_to_xy,
    )


def _pose(pitch: float = 0.0, yaw: float = 0.0):
    pose = np.eye(4)
    pose[:3, :3] = R.from_euler("xyz", [0.0, pitch, yaw]).as_matrix()
    return pose


class FakeMini:
    """Head that flips yaw every few reads; antennas deflect on demand."""

    def __init__(self) -> None:
        self.reads = 0
        self.yaw = 0.4
        self.antennas_deflected = False

    def get_current_head_pose(self):
        self.reads += 1
        if self.reads % 5 == 0:  # big jump → should trigger a saccade blink
            self.yaw = -self.yaw
        return _pose(0.1, self.yaw)

    def get_present_antenna_joint_positions(self):
        deflection = ANTENNA_DEFLECT_HIGH + 0.2 if self.antennas_deflected else 0.17
        return np.array([deflection, deflection])


@unittest.skipUnless(HAS_ROBOT_SDK, "needs the reachy_mini SDK (numpy, scipy)")
class TestHeadPoseMapping(unittest.TestCase):
    def test_centered_head_looks_straight_ahead(self):
        self.assertEqual(head_pose_to_xy(_pose()), (0.0, 0.0))

    def test_yaw_maps_to_horizontal_gaze(self):
        x, y = head_pose_to_xy(_pose(pitch=-0.1, yaw=MAX_YAW_RAD / 2))
        self.assertAlmostEqual(x, 0.5, places=3)  # +x = looking right
        self.assertGreater(y, 0.0)  # pitch down → +y, which is down on screen

    def test_pitch_up_makes_the_eyes_look_up(self):
        _x, y = head_pose_to_xy(_pose(pitch=0.15))
        self.assertLess(y, 0.0)  # screen y is down-positive, so up is negative

    def test_pose_beyond_the_mapped_range_saturates(self):
        # 1 rad is far past MAX_YAW_RAD / MAX_PITCH_RAD (25° / 20°).
        x, y = head_pose_to_xy(_pose(pitch=-1.0, yaw=1.0))
        self.assertEqual((x, y), (1.0, 1.0))


@unittest.skipUnless(HAS_ROBOT_SDK, "needs the reachy_mini SDK (numpy, scipy)")
class TestEyeBridge(unittest.TestCase):
    def setUp(self):
        self.device = FakeSerial()
        transport = SerialTransport("/dev/fake", settle=0.0)
        transport._ser = self.device
        self.eyes = EyesClient(transport=transport, connect=False, wait_for_ack=False)
        self.mini = FakeMini()
        self.bridge = EyeBridge(self.mini, eyes=self.eyes, stream_hz=100.0)

    def tearDown(self):
        self.bridge.stop()
        self.eyes.close()

    def _counts(self) -> dict[str, int]:
        counts: dict[str, int] = {}
        for line in self.device.sent:
            counts[line.split()[0]] = counts.get(line.split()[0], 0) + 1
        return counts

    def test_streams_look_and_blinks_on_saccades(self):
        self.bridge.start()
        time.sleep(0.3)
        counts = self._counts()
        self.assertGreater(counts.get("LOOK", 0), 5)
        self.assertGreater(counts.get("BLINK", 0), 0)

    def test_antenna_deflection_squints_once_per_rising_edge(self):
        self.mini.antennas_deflected = True
        self.bridge.start()
        time.sleep(0.2)
        self.assertEqual(self._counts().get("SQUINT", 0), 1)  # hysteresis latch

        self.mini.antennas_deflected = False
        time.sleep(0.1)
        self.mini.antennas_deflected = True
        time.sleep(0.1)
        self.assertEqual(self._counts().get("SQUINT", 0), 2)

    def test_override_pauses_the_stream_and_release_resumes_it(self):
        self.bridge.start()
        time.sleep(0.1)
        self.bridge.override()
        self.bridge.eyes.hearts()
        mark = len(self.device.sent)
        time.sleep(0.1)
        self.assertEqual(
            [s for s in self.device.sent[mark:] if s.startswith("LOOK")], []
        )

        self.bridge.release()
        time.sleep(0.1)
        self.assertTrue(any(s.startswith("LOOK") for s in self.device.sent[mark:]))

    def test_stop_idles_but_keeps_a_borrowed_client_open(self):
        self.bridge.start()
        time.sleep(0.05)
        self.bridge.stop()
        self.assertEqual(self.device.sent[-1], "IDLE")
        self.assertTrue(self.eyes.is_connected)


if __name__ == "__main__":
    unittest.main()
