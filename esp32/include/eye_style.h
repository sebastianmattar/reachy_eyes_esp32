#pragma once
#include "eye_motion.h"
#include "types.h"
#include "config.h"

// Strategy interface: one implementation per eye style.
// The high-level controller holds a pointer to the active style and delegates
// all style-specific decisions (renderer selection, blink/wander eligibility,
// expiry) to it.  Concrete instances live as value members of the controller,
// so no heap allocation is needed.
class IEyeStyle
{
public:
    virtual ~IEyeStyle() = default;

    // Called once when this style becomes active.  Configures the low-level
    // controller (renderer kind, glyph, etc.) for the given eye selection.
    virtual void activate(EyesControllerLowLevel &low, EnumEyes eyes) = 0;

    // Called every frame while active.  Returns false when the style has
    // naturally expired and the controller should revert to the default style.
    virtual bool update(float dt, EyesControllerLowLevel &low) = 0;

    // Whether the high-level auto-blink should fire while this style is active.
    virtual bool canBlink() const = 0;

    // Whether the idle-wander state machine should run while this style is active.
    virtual bool canWander() const = 0;
};

// ── Concrete styles ───────────────────────────────────────────────────────────

class NormalEyeStyle final : public IEyeStyle
{
public:
    void activate(EyesControllerLowLevel &low, EnumEyes eyes) override
    {
        low.setBillboard(BILLBOARD_GLYPH_NONE, eyes);
        low.setEyeRenderer(EYE_RENDERER_NORMAL, eyes);
    }
    bool update(float, EyesControllerLowLevel &) override { return true; }
    bool canBlink()  const override { return true; }
    bool canWander() const override { return true; }
};

class TrapezoidEyeStyle final : public IEyeStyle
{
public:
    void activate(EyesControllerLowLevel &low, EnumEyes eyes) override
    {
        low.setEyeRenderer(EYE_RENDERER_TRAPEZOID, eyes);
    }
    bool update(float, EyesControllerLowLevel &) override { return true; }
    bool canBlink()  const override { return true; }
    bool canWander() const override { return false; }
};

// Animated-GIF style: selects the GIF renderer on both eyes and stays active
// indefinitely (loops until another command switches away).  No auto-blink or
// idle-wander while a gif plays.  The actual decode/scale happens in GifPlayer,
// driven from main.cpp's render branch — this style only owns renderer selection.
class GifEyeStyle final : public IEyeStyle
{
public:
    void activate(EyesControllerLowLevel &low, EnumEyes eyes) override
    {
        low.setEyeRenderer(EYE_RENDERER_GIF, eyes);
    }
    bool update(float, EyesControllerLowLevel &) override { return true; }
    bool canBlink()  const override { return false; }
    bool canWander() const override { return false; }
};

// Stateful: carries glyph and countdown timer.  Call configure() before
// handing to setStyle() so the timer and glyph are set for the new activation.
class BillboardEyeStyle final : public IEyeStyle
{
public:
    void configure(BillboardGlyph glyph, float duration)
    {
        m_glyph = glyph;
        m_timer = duration;
    }

    void activate(EyesControllerLowLevel &low, EnumEyes eyes) override
    {
        low.setBillboard(m_glyph, eyes);
        low.setEyeRenderer(EYE_RENDERER_BILLBOARD, eyes);
    }

    bool update(float dt, EyesControllerLowLevel &) override
    {
        m_timer -= dt;
        return m_timer > 0.0f;
    }

    bool canBlink()  const override { return false; }
    bool canWander() const override { return false; }

private:
    BillboardGlyph m_glyph = BILLBOARD_GLYPH_NONE;
    float          m_timer = 0.0f;
};
