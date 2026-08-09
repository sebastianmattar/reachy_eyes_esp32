#pragma once
// ─── LovyanGFX stub for native macOS simulator ────────────────────────────────
// Provides LGFX_Eye (hardware display → no-ops) and LGFX_Sprite (software
// framebuffer).  Intercepted instead of the real <LovyanGFX.hpp> because sim/
// is prepended to CPPPATH by sim/sim_extra.py.

#include <Arduino.h>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <new>
#include "config.h" // TFT1_CS / TFT2_CS — map a sprite's CS pin to its eye index

// ── Colour constants ──────────────────────────────────────────────────────────
#define TFT_BLACK (0x0000u)
#define TFT_WHITE (0xFFFFu)
#define TFT_RED (0xF800u)
#define TFT_GREEN (0x07E0u)
#define TFT_BLUE (0x001Fu)
#define TFT_CYAN (0x07FFu)
#define TFT_MAGENTA (0xF81Fu)
#define TFT_YELLOW (0xFFE0u)
#define TFT_ORANGE (0xFD20u)

// ── simDisplayPush: implemented in src/sim/main_sim.cpp ───────────────────────
// eyeIdx 0 = left eye (TFT1_CS), 1 = right eye (TFT2_CS).
extern void simDisplayPush(const uint16_t *buf, int eyeIdx);

// Forward declaration so LGFX_Sprite can reference LGFX_Eye.
class LGFX_Eye;

// ── LGFX_Sprite — off-screen 16-bit framebuffer ───────────────────────────────
class LGFX_Sprite
{
public:
    explicit LGFX_Sprite(LGFX_Eye *display) : _display(display) {}

    ~LGFX_Sprite() { _freeBuffer(); }

    void setColorDepth(uint8_t) {} // always 16-bit in this stub

    bool createSprite(int w, int h)
    {
        _freeBuffer();
        _w = w;
        _h = h;
        _buf = new (std::nothrow) uint16_t[w * h]();
        return _buf != nullptr;
    }

    void deleteSprite()
    {
        _freeBuffer();
        _w = _h = 0;
    }

    // Raw framebuffer access (mirrors LovyanGFX). GifPlayer writes pixels here
    // directly. Native buffer is little-endian RGB565 (matches the SDL texture).
    void *getBuffer() { return _buf; }

    // ── Drawing primitives ────────────────────────────────────────────────────

    void drawPixel(int32_t x, int32_t y, uint32_t color)
    {
        if (!_buf || x < 0 || x >= _w || y < 0 || y >= _h)
            return;
        _buf[y * _w + x] = (uint16_t)color;
    }

    // LovyanGFX uses fillScreen (not fillSprite) to clear a sprite.
    void fillScreen(uint16_t color)
    {
        if (!_buf)
            return;
        int n = _w * _h;
        for (int i = 0; i < n; i++)
            _buf[i] = color;
    }

    // Filled circle via per-scanline span fill.
    void fillCircle(int cx, int cy, int r, uint16_t color)
    {
        if (!_buf || r <= 0)
            return;
        int r2 = r * r;
        for (int dy = -r; dy <= r; dy++)
        {
            int y = cy + dy;
            if (y < 0 || y >= _h)
                continue;
            int dx_max = (int)::sqrtf((float)(r2 - dy * dy));
            int x0 = cx - dx_max;
            int x1 = cx + dx_max;
            if (x0 < 0)
                x0 = 0;
            if (x1 >= _w)
                x1 = _w - 1;
            for (int x = x0; x <= x1; x++)
                _buf[y * _w + x] = color;
        }
    }

    // Push sprite to the display it was created with.
    // In the simulator there is no DMA; this is always synchronous.
    void pushSprite(int offX, int offY);

private:
    LGFX_Eye *_display = nullptr;
    uint16_t *_buf = nullptr;
    int _w = 0;
    int _h = 0;

    void _freeBuffer()
    {
        delete[] _buf;
        _buf = nullptr;
    }
};

// ── LGFX_Eye — hardware display (all operations are no-ops in the simulator) ──
// Mirrors the hardware LGFX_Eye class from include/lgfx_config.h.
class LGFX_Eye
{
public:
    // Mirrors the hardware ctor signature (cs, dc, rst); dc/rst unused in sim.
    explicit LGFX_Eye(int cs_pin, int /*dc_pin*/ = -1, int /*rst_pin*/ = -1) : _csPin(cs_pin) {}

    void init() {}
    void setRotation(uint8_t) {}
    void fillScreen(uint16_t) {}
    void waitDisplay() {} // no-op: sim pushSprite is always synchronous
    void startWrite() {}
    void endWrite() {}
    void writeCommand(uint8_t) {}
    void writeData(uint8_t) {}

    int getCsPin() const { return _csPin; }

private:
    int _csPin;
};

// ── LGFX_Sprite::pushSprite — defined after LGFX_Eye is complete ─────────────
inline void LGFX_Sprite::pushSprite(int /*offX*/, int /*offY*/)
{
    if (!_buf || !_display)
        return;
    // Map CS pin → eye index using the actual config pins (not hardcoded), so a
    // future pin renumber can't silently break the sim display again.
    int cs = _display->getCsPin();
    int eyeIdx = (cs == TFT1_CS) ? 0 : (cs == TFT2_CS ? 1 : -1);
    if (eyeIdx >= 0)
        simDisplayPush(_buf, eyeIdx);
}
