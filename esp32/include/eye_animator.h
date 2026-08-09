#pragma once
#include <Arduino.h>
#include "spring.h"
#include "visuals.h"
#include "config.h"

// Animated values produced by motion/control logic for one eye.
// This remains renderer-agnostic; a separate adapter turns it into a
// renderer-facing model.
struct EyeAnimationState
{
    float gazeX = 0.0f;
    float gazeY = 0.0f;
    float eyelidOpen = 0.75f;
    float pupilScale = 0.45f;
    float pupilAspect = 1.0f;
    float effectPhase = 0.0f;

    EffectId effect = EFFECT_NONE;
    uint16_t irisColor = rgb565(70, 130, 180);
    BillboardGlyph billboard = BILLBOARD_GLYPH_NONE;
    bool leftEye = true;
};

// Manages all animation state for one eye.
class AnimController
{
public:
    // Call once at startup.  isLeft = true for left eye, false for right.
    void init(bool isLeft);

    // Advance animation by dt seconds.
    void update(float dt);

    // ── Gaze ─────────────────────────────────────────────────────────────────
    void setLook(float x, float y);             // x,y normalised -1..+1; persists until setIdle()
    void setIdle();                             // reserved for future gaze-reset behavior
    void setGazeSpring(float k, float damping); // override gaze spring (used by dart wander)

    // ── Visual parameter setters (spring-animated toward target) ─────────────
    void setOpenFactor(float v);  // 0 = closed, 1 = fully open
    void setPupilScale(float v);  // 0.1..0.9 typical
    void setPupilAspect(float v); // 0.3..3.0 typical (>1 = wide, <1 = tall)

    // ── Immediate (non-spring) visual params ─────────────────────────────────
    void setIrisColor(uint16_t color);
    void setBillboard(BillboardGlyph glyph);

    // ── Spring stiffness for all visual springs ───────────────────────────────
    void setSpringParams(float k, float damping);

    // ── Blink ────────────────────────────────────────────────────────────────
    void triggerBlink();

    // ── Effects ──────────────────────────────────────────────────────────────
    void setBaseEffect(EffectId id); // persistent until changed

    // Returns the latest animation state for adapter/renderer handoff.
    const EyeAnimationState &getAnimationState() const;

private:
    bool leftEye = true;

    // Springs
    Spring2D gaze;
    SpringFloat openSpring;
    SpringFloat pupilSpring;
    SpringFloat pupilAspectSpring;

    // Non-spring visual params
    uint16_t irisColorCurrent = rgb565(70, 130, 180);
    BillboardGlyph billboardCurrent = BILLBOARD_GLYPH_NONE;
    EffectId baseEffect = EFFECT_NONE;

    // Blink state machine
    enum BlinkPhase
    {
        BLINK_IDLE,
        BLINK_CLOSING,
        BLINK_HOLDING,
        BLINK_OPENING
    };
    BlinkPhase blinkPhase = BLINK_IDLE;
    float blinkTimer = 0.0f;
    float blinkFactor = 1.0f;

    // Effect
    EffectId activeEffect = EFFECT_NONE;
    float effectPhase = 0.0f;
    float effectTimer = 0.0f;

    // Output state published after each update.
    EyeAnimationState animationState;

    // Private helpers
    void updateBlink(float dt);
    void publishAnimationState();
};
