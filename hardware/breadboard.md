## Hardware (breadboard prototype only)

### Required

- ESP32 ( I have the NodeMCU Wroom ESP32 )
- 2xGC9A01 1.28" round displays
- Breadboard
- Some wires

### Wiring

May vary according to your specific ESP32 model. The "Display pin" column is the
pin number on the GC9A01 module's own connector (see the pinout below).

| GC9A01 (left) | GC9A01 (right) | Pin ESP32 | Display pin | GPIO                    |
| ------------- | -------------- | --------- | ----------- | ----------------------- |
| GND           | GND            | GND       | 1           |                         |
| VCC           | VCC            | 3V3       | 2           |                         |
| SCL           | SCL            | D18       | 3           | GPIO18 / SCK            |
| SDA           | SDA            | D23       | 4           | MOSI / GPIO23 / VSPI-D  |
| RST           | RST            | D4        | 5           | GPIO4 / HSPI-HD         |
| DC            | DC             | D2        | 6           | GPIO2                   |
| CS            |                | D14       | 7           | GPIO14                  |
|               | CS             | D15       | 7           | GPIO15 / HSPI-CS0       |

Both displays share every line except `CS` — that is what lets one SPI bus drive
two panels.

#### Connectors GC9A01

1. GND (Power Supply Ground)
2. VCC (Power Supply Positive)
3. SCL (Clock Line)
4. SDA (Data Line)
5. RES (Reset Line)
6. DC (Data/Command)
7. CS (Chip Select)

### Get the software running on your ESP32

Prerequisites:

- Visual Studio Code
- PlatformIO

Make sure to update `include/config.h` to your configuration.

### From breadboard to something better

It took about 2 hours of trying my luck with a soldering iron to get to the conclusion I could not do it myself - lacking the skills and the equipment. 12 hours after learning the meaning of the word "PCB" I came up with a very simple PCB design that I ordered at https://jlcpcb.com/.

Goal is to use flat cables to connect the displays to the PCB.
