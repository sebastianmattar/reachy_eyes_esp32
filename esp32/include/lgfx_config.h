#pragma once
// ─── LovyanGFX hardware configuration for dual GC9A01 displays ───────────────
// Create two LGFX_Eye instances in main.cpp, one per CS pin:
//   static LGFX_Eye display0(TFT1_CS);
//   static LGFX_Eye display1(TFT2_CS);
// Both share VSPI_HOST at 80 MHz; each manages its own CS signal.
// bus_shared = true tells LovyanGFX to arbitrate the bus between instances.

#include <LovyanGFX.hpp>

class LGFX_Eye : public lgfx::LGFX_Device
{
    lgfx::Panel_GC9A01 _panel;
    lgfx::Bus_SPI _bus;

public:
    // rst_pin: pass TFT_RST for the first display, -1 for the second so the
    // shared hardware RST line is only pulsed once (during display0.init()).
    explicit LGFX_Eye(int cs_pin, int dc_pin, int rst_pin = -1)
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = VSPI_HOST; // SPI3 on ESP32 WROOM (MOSI=23, SCLK=18)
            cfg.spi_mode = 0;
            cfg.freq_write = 80000000;
            cfg.freq_read = 16000000;
            cfg.pin_sclk = TFT_CLK;
            cfg.pin_mosi = TFT_SDA;
            cfg.pin_miso = -1;
            cfg.pin_dc = dc_pin;
            cfg.use_lock = true; // required when bus is shared between instances
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = cs_pin;
            cfg.pin_rst = rst_pin;
            cfg.pin_busy = -1;
            cfg.panel_width = 240;
            cfg.panel_height = 240;
            cfg.memory_width = 240;
            cfg.memory_height = 240;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;
            cfg.invert = true; // GC9A01 requires color inversion
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true; // shared SPI bus with the other display
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};
