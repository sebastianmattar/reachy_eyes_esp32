#pragma once
#include <Arduino.h>

// ─── Colour helper ────────────────────────────────────────────────────────────
// Convert 8-bit R/G/B to RGB565 at compile time.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

// ─── Billboard glyph IDs ─────────────────────────────────────────────────────
enum BillboardGlyph : uint8_t
{
    BILLBOARD_GLYPH_NONE = 0,
    BILLBOARD_GLYPH_HEART,
    BILLBOARD_GLYPH_MONEY,
    BILLBOARD_GLYPH_DEAD,
};

// ─── Special-effect IDs ───────────────────────────────────────────────────────
enum EffectId : uint8_t
{
    EFFECT_NONE = 0x00,
    EFFECT_SPIRAL = 0x03, // Three orbiting dots rotating inside pupil
    EFFECT_DIZZY = 0x04,  // Iris orbits the eye centre slowly
};
