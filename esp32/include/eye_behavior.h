#pragma once
#include <Arduino.h>
#include "eye_animator.h"
#include "config.h"
#include "eye_motion.h"
#include "eye_style.h"

enum EnumEyeState
{
    EYESTATE_IDLE,      // Waiting; idle timer counts down before wandering starts
    EYESTATE_EXECUTING, // Executing a directed command (gaze, etc.)
    EYESTATE_WANDERING  // Randomly gazing around
};

class EyesControllerHighLevel
{
public:
    void init(EyesControllerLowLevel *lowLevel);

    void update(float dt);

    // ── Commands ─────────────────────────────────────────────────────────────
    void setBlinkInterval(float minSec, float maxSec);
    void idle();
    // Continuous gaze target for external streaming (e.g. mirroring robot head pose).
    // x,y in [-1,1] = full iris travel. Holds until the next look()/gaze()/idle();
    // suppresses wandering but keeps auto-blink alive. Send IDLE to return control.
    void look(float x, float y, float transitionSeconds = 0.15f, EnumEyes eyes = EYES_BOTH);
    void gaze(EnumDirection direction, float transitionSeconds, float resetAfterSeconds, EnumEyes eyes = EYES_BOTH);
    void blink(int times, EnumEyes eyes = EYES_BOTH);
    void squint(float transitionSeconds, float resetAfterSeconds);
    void hearts(EnumEyes eyes = EYES_BOTH);
    void money(EnumEyes eyes = EYES_BOTH);
    void dead(EnumEyes eyes = EYES_BOTH);
    void trapezoid(EnumEyes eyes = EYES_BOTH);
    void playGif(EnumEyes eyes = EYES_BOTH);

private:
    EnumEyeState state = EYESTATE_IDLE;

    EyesControllerLowLevel *low = nullptr;

    // ── Active style ──────────────────────────────────────────────────────────
    IEyeStyle *m_activeStyle     = nullptr;
    EnumEyes   m_activeStyleEyes = EYES_BOTH;

    // Embedded style instances — no heap allocation needed
    NormalEyeStyle    m_normalStyle;
    TrapezoidEyeStyle m_trapezoidStyle;
    BillboardEyeStyle m_billboardStyle;
    GifEyeStyle       m_gifStyle;

    // ── Idle / wander ─────────────────────────────────────────────────────────
    float idleTimer   = 0.0f;
    float wanderDwell = 1.0f;

    // ── Blink scheduling ──────────────────────────────────────────────────────
    float blinkCountdown   = BLINK_INTERVAL_DEFAULT_MIN;
    float blinkIntervalMin = BLINK_INTERVAL_DEFAULT_MIN;
    float blinkIntervalMax = BLINK_INTERVAL_DEFAULT_MAX;

    // ── Timed parameter resets (modifiers, independent of active style) ───────
    struct TimedReset
    {
        bool     active = false;
        float    timer  = 0.0f;
        EnumEyes eyes   = EYES_BOTH;
    };

    TimedReset gazeReset;
    TimedReset squintReset;

    struct BlinkQueue
    {
        bool     active    = false;
        int      remaining = 0;
        float    timer     = 0.0f;
        EnumEyes eyes      = EYES_BOTH;
    };

    BlinkQueue blinkQueue;

    // ── Private helpers ───────────────────────────────────────────────────────
    void setStyle(IEyeStyle *style, EnumEyes eyes);
    void updateWander(float dt);
    void updateAutoBlink(float dt);
    void updateTimedResets(float dt);
    void updateBlinkQueue(float dt);
    void enterIdleState();
    void enterExecutingState();
    void enterWanderingState();

    static float directionToX(EnumDirection direction);
    static float directionToY(EnumDirection direction);
    static float randF(float lo, float hi);
    static void  clampDirection(float &x, float &y);
    float sampleBlinkInterval() const;
};
