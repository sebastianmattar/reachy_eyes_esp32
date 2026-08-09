# ESP32 firmware

Firmware that drives the two GC9A01 eye displays and exposes a line-based USB
serial protocol so the host can control them. Without commands the eyes wander
and blink on their own.

## What is in here

| Path                               | Contents                                                                                                                          |
| ---------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| [`src/`](src/)                     | Implementation (`main.cpp`, eye behaviour/motion/renderer, gif player)                                                            |
| [`include/`](include/)             | Headers — `config.h` (pins, geometry, firmware version), `lgfx_config.h` (display setup), `serial_protocol.h` (full command list) |
| [`sim/`](sim/)                     | Stubs that let the same sources build against SDL2 on the desktop                                                                 |
| [`platformio.ini`](platformio.ini) | Two environments: `upesy_wroom` (hardware) and `native_sim` (desktop)                                                             |
| [`partitions.csv`](partitions.csv) | Dual OTA app slots + a LittleFS data partition for the stored gif                                                                 |

## Prerequisites

- [PlatformIO](https://platformio.org/) — either the VS Code extension or the
  CLI (`pip install platformio`, or `brew install platformio` on macOS). The
  build commands below assume `pio` is on your `PATH`; the VS Code extension
  installs it to `~/.platformio/penv/bin/pio`.
- A USB serial driver for the PCB's USB-UART chip if your OS does not ship one
  (CH340 and CP210x are the usual suspects). If no port shows up in `pio device
  list` once the board is plugged in, install the vendor driver for your chip.
- Nothing else — PlatformIO downloads the ESP32 toolchain, the Arduino
  framework, and the `LovyanGFX` / `AnimatedGIF` libraries on the first build.

Before building, check [`include/config.h`](include/config.h) against your
board. The pin map there is for the PCB in [`../hardware/`](../hardware/); a
breadboard build almost certainly needs different GPIOs (see
[`../hardware/breadboard.md`](../hardware/breadboard.md)).

## Initial flash of the PCB

This is the one flash that has to happen over USB, because it also writes the
bootloader and the partition table — serial OTA later replaces only the app.

Do it **before mounting the eyes in the robot** if you can: the auto-reset that
drops the ESP32 into its bootloader is unreliable through the robot's wiring.

1. Connect the PCB to your computer with a USB-C data cable (a charge-only
   cable will not enumerate a serial port).

2. Find the port:

   ```bash
   pio device list
   # macOS: /dev/cu.usbserial-* or /dev/cu.usbmodem*
   # Linux: /dev/ttyUSB0 or /dev/ttyACM0
   ```

3. Build and flash:

   ```bash
   cd esp32
   pio run -e upesy_wroom -t upload --upload-port /dev/cu.usbserialXXXX
   ```

   Omit `--upload-port` and PlatformIO will try to autodetect it.

   If the upload stalls at `Connecting........_____`, the board did not enter
   the bootloader by itself. Hold **BOOT**, tap **EN/RST**, release **BOOT**,
   and start the upload again. If it connects but fails partway through, drop
   `upload_speed` in [`platformio.ini`](platformio.ini) — 460800 is faster,
   115200 is the safe value.

4. Verify. The displays should light up and the eyes should start wandering
   and blinking within a second or two. On the serial monitor:

   ```bash
   pio device monitor -e upesy_wroom   # 115200 baud
   ```

   You should see `Robot Eyes – starting`. Type `VERSION` and press enter — the
   board replies `VERSION 1.0.0` (whatever `FIRMWARE_VERSION` in
   [`include/config.h`](include/config.h) says). `BLINK 2` makes both eyes
   blink and answers `OK`. Most commands answer `OK` or `ERR <reason>`; the
   exceptions (`LOOK`, `VERSION`, `GIFINFO`, `GIFUPLOAD`, `FWUPDATE`) are listed
   in [`include/serial_protocol.h`](include/serial_protocol.h).

The LittleFS partition needs no flashing — it holds a single optional gif that
is uploaded at runtime with the `GIFUPLOAD` command.

### Nothing on the displays?

- Both panels dark: check the backlight pin (`TFT_BL`) and the 3V3/GND lines.
- One panel dark: that eye's `CS` or `DC` line — `TFT1_CS`/`TFT2_CS` and
  `TFT1_DC`/`TFT2_DC` in `config.h`.
- Garbled or mirrored image: `TFT_1_ROT` / `TFT_2_ROT` in `config.h`.

## Later updates

After the initial flash, updates go over the existing serial link — no
bootloader reset, no `esptool`, no buttons:

```bash
# from the repo root:
python/build_and_push.sh /dev/cu.usbmodemXXXX
```

Bump `FIRMWARE_VERSION` in [`include/config.h`](include/config.h) for each
release you want auto-deployed; the host tool only pushes when the bundled
version is newer. See [`../python/README.md`](../python/README.md) for the full
OTA workflow and the head-follow bridge.

## Desktop simulator

The eye logic (including gif playback) builds natively against SDL2, which is a
much faster loop than reflashing:

```bash
brew install sdl2          # macOS; apt install libsdl2-dev on Debian/Ubuntu
pio run -e native_sim
.pio/build/native_sim/program
```

The simulator window renders both eyes and injects serial commands from the
keyboard:

| Key         | Command                                    |
| ----------- | ------------------------------------------ |
| `b` / `B`   | `BLINK 1` / `BLINK 3`                      |
| `h`         | `HEARTS`                                   |
| `m`         | `MONEY`                                    |
| `d`         | `DEAD`                                     |
| `t`         | `TRAPEZOID`                                |
| `s`         | `SQUINT 0.3 1.5`                           |
| `g` / `x`   | play the local gif / `STOPGIF`             |
| `i` / Space | `IDLE`                                     |
| ↑ ↓ ← →     | `GAZE N` / `S` / `W` / `E`                 |
| `q` / Esc   | quit                                       |

`g` plays `gifs/test.gif` (relative to the working directory, so run the binary
from `esp32/`). Point `$REACHY_GIF` at another file to play that one instead:

```bash
REACHY_GIF=/path/to/other.gif .pio/build/native_sim/program
```
