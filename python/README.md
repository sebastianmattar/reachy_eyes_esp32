### ESP32 to control the eye displays

I hacked together a software to drive the two displays with an eye logic. There is a simple protocol that works via USB (serial interface) and allows to control the eyes.

By default (if not control commands arrive) the eyes wander around and blink.

There is a simple simulation included ("native_sim") that allows you to simulate the eye behaviour on your local machine using SDL2.

### Motion-linked eyes (head-follow bridge)

[`eye_bridge.py`](eye_bridge.py) mirrors the robot's head
motion onto the eyes in real time: it reads Reachy Mini's head pose from the SDK
and streams a continuous gaze target to the ESP32 over USB serial, so the eyes
follow every motion — and it blinks on large gaze shifts and squints on antenna
deflection.

**Requirements:** the `reachy_mini` SDK (which pulls in `numpy`, `scipy`, and
`pyserial`). Use **Python 3.12** — the SDK pins deps (onnxruntime, a bundled
GStreamer) that have no 3.14 wheels yet.

Set up the environment from the repo root (needs [uv](https://docs.astral.sh/uv/)):

```bash
uv venv --python 3.12                     # (re)create the .venv on Python 3.12
source .venv/bin/activate
uv pip install "reachy-mini"   # SDK + numpy, scipy, pyserial (large download)
```

Find the ESP32's serial port and run the bridge:

```bash
ls /dev/cu.*   # macOS: pick the /dev/cu.usbmodem* that is the ESP32
               #        (replug it and rerun to see which entry appears)
               # Linux: the port is /dev/ttyACM0 or /dev/ttyUSB0
python python/eye_bridge.py --port /dev/cu.usbmodemXXXX
```

> If an existing `.venv` fails to run, it likely points at a Homebrew Python that
> has since been upgraded — recreate it with the `uv venv` command above.

### Playing an animated GIF on the eyes

The firmware keeps one GIF in its LittleFS partition (up to 1 MB) and can loop it
on both eyes instead of the normal eye rendering.
[`upload_gif.py`](upload_gif.py) drives that over the same serial link:

```bash
# Upload a gif (verifies the device's CRC-32 against a locally computed one):
python python/upload_gif.py --port /dev/cu.usbmodemXXXX heart.gif

# Upload and start playing it immediately:
python python/upload_gif.py --port /dev/cu.usbmodemXXXX heart.gif --play

# What is currently stored? (byte length + crc32, or NONE)
python python/upload_gif.py --port /dev/cu.usbmodemXXXX --info

# Stop playback and go back to the autonomous eye behavior:
python python/upload_gif.py --port /dev/cu.usbmodemXXXX --stop
```

Uploading overwrites the single stored slot. Requires `pyserial` only — no
`reachy_mini` SDK needed.

### Updating the ESP32 firmware (serial OTA)

Once the eyes are mounted in the robot, USB re-flashing with PlatformIO is
painful — the auto-reset that puts the ESP32 into its bootloader often fails
through the robot's wiring. So the firmware can update **itself over the existing
USB serial link**: the running firmware writes the new image into the spare OTA
partition via the ESP32 `Update` API and reboots into it. No bootloader reset, no
`esptool`, no BOOT/EN buttons.

Tools in this directory (paths below are relative to the repo root):

```bash
# Compare the running firmware vs the bundled version, no changes:
python python/ota_update.py --port /dev/cu.usbmodemXXXX --check-only

# Push the built firmware if it is newer than what the board runs:
python python/ota_update.py --port /dev/cu.usbmodemXXXX

# Build, then push in one step:
python/build_and_push.sh /dev/cu.usbmodemXXXX
```

`eye_bridge.py` can also update on startup before it begins streaming:

```bash
python python/eye_bridge.py --port /dev/cu.usbmodemXXXX --update-firmware
```

How the version check works: the firmware reports its version (`VERSION`
command), which the host compares against `FIRMWARE_VERSION` in
[esp32/include/config.h](../esp32/include/config.h) — the single source of truth.
Bump that string on each release you intend to auto-deploy. The update is pushed
only when the bundled version is newer (use `--force` to override).

> **First time only:** the board must already run OTA-capable firmware with the
> dual-slot [partitions.csv](../esp32/partitions.csv). Flash _that_ build once over
> USB with PlatformIO (it also lays down the new partition table); every update
> after that can go over serial. Serial OTA replaces the app only, not the
> bootloader or partition table.
