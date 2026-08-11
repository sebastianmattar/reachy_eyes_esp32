### ESP32 to control the eye displays

I hacked together a software to drive the two displays with an eye logic. There is a simple protocol that works via USB (serial interface) and allows to control the eyes.

By default (if not control commands arrive) the eyes wander around and blink.

There is a simple simulation included ("native_sim") that allows you to simulate the eye behaviour on your local machine using SDL2.

### The Python SDK (`reachy_eyes`)

Everything the firmware's serial protocol offers is wrapped in one client, so you
never have to format protocol lines yourself. The command-line tools in this
directory are thin wrappers around it.

```python
from reachy_eyes import EyesClient, Direction, Eyes

with EyesClient() as eyes:          # no port given → auto-detected
    eyes.blink(2)
    eyes.gaze(Direction.NE)         # or just "NE"
    eyes.look(0.4, -0.2)            # continuous gaze target, x/y in [-1, 1]
    eyes.squint()
    eyes.hearts(Eyes.LEFT)
    eyes.set_blink_interval(2.0, 5.0)
    eyes.idle()                     # back to autonomous wandering + blinking

    eyes.upload_gif("heart.gif", play=True)
    print(eyes.gif_info())          # e.g. "18422 bytes, crc32=1B4C9A02"
    eyes.stop_gif()

    print(eyes.version())           # firmware version, or None if it can't answer
    eyes.update_firmware()          # serial OTA, only if the local build is newer
```

What the SDK handles for you:

| Concern | How |
| --- | --- |
| Finding the board | `EyesClient()` with no port scans for it; `find_port()` if you want the name |
| Replies | every command checks the `OK`/`ERR` ack and raises on failure |
| Errors | `EyesError` subclasses: `CommandError`, `EyesTimeoutError`, `CrcMismatchError`, `PortNotFoundError`, … |
| Binary transfers | `GIFUPLOAD`/`FWUPDATE` framing, chunking and CRC-32 verification |
| Firmware versions | reads `FIRMWARE_VERSION` from `config.h`, compares with the device |
| Threads | one lock serializes whole request/response exchanges, so a background streamer and foreground commands can share the link |

Modules: [`client.py`](reachy_eyes/client.py) (the API),
[`transport.py`](reachy_eyes/transport.py) (port discovery, line framing),
[`models.py`](reachy_eyes/models.py) (`Eyes`, `Direction`, results),
[`firmware.py`](reachy_eyes/firmware.py) (OTA versioning),
[`bridge.py`](reachy_eyes/bridge.py) (head-follow),
[`errors.py`](reachy_eyes/errors.py).

Importing `reachy_eyes` needs **pyserial only**; the head-follow bridge is imported
lazily because it is the one part that needs the `reachy_mini` SDK.

Running the scripts in this directory already puts the SDK on the path. To use it
from elsewhere, install it (editable):

```bash
uv pip install -e python/                    # SDK + pyserial
uv pip install -e "python/[bridge]"          # ...plus reachy-mini for EyeBridge
```

Tests run against an in-memory fake of the firmware — no hardware needed
(the bridge tests skip themselves if `reachy_mini` is missing):

```bash
python -m unittest discover -s python/tests
```

### Finding the eyes' serial port (especially inside the robot)

Every tool here takes `--port`, and every tool can also find the board itself —
`EyesClient()` with no port auto-detects. Detection is a **`VERSION` handshake**,
not a VID/PID guess, and that distinction matters once the ESP32 lives inside the
robot: a Reachy Mini Lite's internal USB hub already carries the camera, the audio
board, and the **motor controller — which is a WCH CH343 (`1a86:55d3`), the same
chip family used on ESP32 dev boards**. Guessing by USB IDs can hand you the
robot's motors; only the handshake tells the two apart. (Detection writes a single
`VERSION` line to each candidate. That is inert for the servo bus, which ignores
anything without its binary frame header.)

To see what is on the bus and which port is the eyes:

```bash
python python/eye_console.py --list-ports
```

```
  /dev/cu.usbmodem5B420746081
      USB Single Serial, 1a86:55d3, sn=5B42074608
      → not the eyes (no VERSION reply)      # this is the robot's motor controller
  /dev/cu.usbserial-0001
      USB Serial, 1a86:7523, sn=None
      → EYES — firmware 1.0.0
```

Once identified, pin it for every tool at once instead of passing `--port`:

```bash
export REACHY_EYES_PORT=/dev/cu.usbserial-0001
```

On Linux (e.g. a wireless Mini's Raspberry Pi, where the eyes enumerate on the Pi
rather than on your laptop — run these tools over ssh there) `/dev/ttyACM*`
numbering is not stable across reboots. Pin it by the board's USB serial number:

```
# /etc/udev/rules.d/99-reachy-eyes.rules
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55d3", \
  ATTRS{serial}=="YOUR_BOARD_SN", SYMLINK+="reachy-eyes"
```

...then `export REACHY_EYES_PORT=/dev/reachy-eyes`.

**Nothing shows up at all?** The board is not enumerating, so no software can
reach it. In order of likelihood: a charge-only USB-C cable (very common — no data
pair); an internal connector that only carries power; a C-to-C link between two
device-role ports (neither side pulls CC up, so no data connection is negotiated);
or the board not being powered. Bisect by plugging the ESP32 into your computer
directly **with the same cable** — if it appears there, the cable and board are
fine and the robot's internal port is the problem.

### Keyboard console (the simulator, on real hardware)

[`eye_console.py`](eye_console.py) is the hardware counterpart of the desktop
simulator: single keypresses drive the connected eyes through the SDK, and each
one echoes the protocol line that went over the wire plus whatever the device
replied. Handy for bring-up, for checking a display's wiring, and for seeing what
a command actually looks like on the eyes.

```bash
python python/eye_console.py                       # port auto-detected
python python/eye_console.py --port /dev/cu.usbmodemXXXX --gif heart.gif
```

It opens with a `VERSION` handshake, so you know the link speaks the protocol
before you start pressing keys:

```
Eyes console — /dev/cu.usbmodem1101 @ 115200 baud
  → VERSION
  ✓ firmware 1.2.0 — protocol OK
```

If nothing answers it says so and still drops you at the key map — handy for
poking at firmware too old to implement `VERSION`.

| Key | Action |
| --- | --- |
| `b` / `B` | blink once / three times |
| `h` `m` `d` | hearts / money / dead eyes |
| `t` | trapezoid style |
| `s` | squint |
| `i` / Space | idle (autonomous wander + blink) |
| ↑ ↓ ← → | dart the gaze N / S / W / E |
| shift + ↑↓←→ | nudge the streamed `LOOK` target; `c` recentres it |
| `1` / `2` / `3` | aim the following commands at both / left / right eye |
| `k` | toggle a livelier auto-blink interval |
| `u` / `g` / `x` | upload the `--gif` file / play the stored gif / stop it |
| `n` / `v` | what gif is stored? / firmware version |
| `:` | type a raw protocol line |
| `?` | show the key map |
| `q` / Esc | quit (eyes go back to autonomous behavior) |

The first ten rows are the simulator's own bindings
([`esp32/src/sim/main_sim.cpp`](../esp32/src/sim/main_sim.cpp)); the rest are
things that only exist on hardware. Needs a POSIX terminal (macOS/Linux) and
`pyserial`. Firmware updates are deliberately not bound — that is
[`ota_update.py`](ota_update.py)'s job.

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

Run the bridge (the port is auto-detected; pass `--port` to be explicit):

```bash
python python/eye_bridge.py

ls /dev/cu.*   # macOS: pick the /dev/cu.usbmodem* that is the ESP32
               #        (replug it and rerun to see which entry appears)
               # Linux: the port is /dev/ttyACM0 or /dev/ttyUSB0
python python/eye_bridge.py --port /dev/cu.usbmodemXXXX
```

Or embed it in your own app — the bridge shares one `ReachyMini` instance with your
code, and `bridge.eyes` is the full SDK client:

```python
from reachy_mini import ReachyMini
from reachy_eyes import EyeBridge

with ReachyMini() as mini, EyeBridge(mini) as bridge:
    ...                          # your robot behavior; the eyes follow along
    bridge.override()            # pause head-follow
    bridge.eyes.hearts()         # scripted eyes
    bridge.release()             # hand control back to head-follow
```

> If an existing `.venv` fails to run, it likely points at a Homebrew Python that
> has since been upgraded — recreate it with the `uv venv` command above.

### Playing an animated GIF on the eyes

The firmware keeps one GIF in its LittleFS partition (up to 1 MB) and can loop it
on both eyes instead of the normal eye rendering.
[`upload_gif.py`](upload_gif.py) drives that over the same serial link:

```bash
# Upload a gif (verifies the device's CRC-32 against a locally computed one):
python python/upload_gif.py heart.gif

# Upload and start playing it immediately (--port is optional everywhere):
python python/upload_gif.py --port /dev/cu.usbmodemXXXX heart.gif --play

# What is currently stored? (byte length + crc32, or NONE)
python python/upload_gif.py --info

# Stop playback and go back to the autonomous eye behavior:
python python/upload_gif.py --stop
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
python python/ota_update.py --check-only

# Push the built firmware if it is newer than what the board runs:
python python/ota_update.py

# Build, then push in one step:
python/build_and_push.sh /dev/cu.usbmodemXXXX
```

`eye_bridge.py` can also update on startup before it begins streaming:

```bash
python python/eye_bridge.py --update-firmware
```

From Python it is one call — `EyesClient.update_firmware()`, which returns an
`UpdateStatus` (`updated`, `up-to-date`, `no-device`, `pushed-unverified`).

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
