# Eyes for the Reachy Mini

## Disclaimer

- This is a hobby project done with minimal professional knowledge of hardware development
- No guarantees of any kind are given
- I only tested this with my Reachy Mini Lite and MacOS
- Doing the modification can damage your robot
- Take extra care to not break the flat cables

## Motivation and journey

I wanted my Reachy Mini to have LED eyes to express different emotions.
Doing some research (googling) I figured I could combine an ESP32 with round LED displays to do it.

The cheapest way is to use very common smart watch displays (also seemingly used in some household appliances). A 1" size would have been perfect but does not seem to be widely available. I decided to check out the 1.28" sized displays.

## What this repository includes

- How you can build the hardware (PCB, holding plate)
- ESP32 program to control the eyes, plus a desktop simulator to develop the eye
  logic without hardware
- Simple python tool that monitors the robot position and sends commands to the eyes
- Python tools to update the firmware over serial and to upload an animated GIF
  for the eyes to play

## How to build the hardware

- Print the mounting plate (3d model) that holds all components
- Order the PCBA or just the PCB and solder yourself

(more infos at [`./hardware/README.md`](./hardware/README.md))

## Software

This repository provides the following:

- Simple ESP32 firmware that implements a USB serial protocol so the eyes can be controlled by the host
- Python tool to do firmware updates
- Python tool that monitors the robot pose and updates the eyes
- Python tool to upload an animated GIF and play it on the eyes

(check out [`esp32/README.md`](./esp32/README.md) and [`python/README.md`](./python/README.md))

## Known issues

- [ ] 3d print is too large, must be smaller!
- [ ] eyes are too tall
