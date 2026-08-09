#include "eye_animator.h"
#include <math.h>

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Smooth 0→1 using a half-cosine (eases in and out).
static float smoothStep(float t)
{
    t = constrain(t, 0.0f, 1.0f);
    return 0.5f - 0.5f * cosf(t * (float)M_PI);
}

static void clampSpringAxis(SpringFloat &s, float minV, float maxV)
{
    if (s.pos < minV)
    {
        s.pos = minV;
        if (s.vel < 0.0f)
            s.vel = 0.0f;
        return;
    }
    if (s.pos > maxV)
    {
        s.pos = maxV;
        if (s.vel > 0.0f)
            s.vel = 0.0f;
    }
}

static float minNonOscillatoryDamping(float stiffness)
{
    float k = max(stiffness, 0.0f);
    return 2.0f * sqrtf(k);
}

// ─── Public interface ─────────────────────────────────────────────────────────

void AnimController::init(bool isLeft)
{
    leftEye = isLeft;
    setSpringParams(150.0f, 20.0f);
    gaze.snapTo(0.0f, 0.0f);
    openSpring.snapTo(0.75f);
    pupilSpring.snapTo(0.45f);
    pupilAspectSpring.snapTo(1.0f);
    // Initialize target values
    openSpring.setTarget(0.75f);
    pupilSpring.setTarget(0.45f);
    pupilAspectSpring.setTarget(1.0f);
}

void AnimController::setSpringParams(float k, float damping)
{
    gaze.setParams(k, max(damping, minNonOscillatoryDamping(k)));
    openSpring.setParams(k * 1.2f, damping * 1.1f);
    pupilSpring.setParams(k, 0.0f);
    pupilAspectSpring.setParams(k, 0.0f);
}

void AnimController::setOpenFactor(float v)
{
    openSpring.setTarget(constrain(v, 0.0f, 1.0f));
}

void AnimController::setPupilScale(float v)
{
    pupilSpring.setTarget(constrain(v, 0.1f, 0.9f));
}

void AnimController::setPupilAspect(float v)
{
    pupilAspectSpring.setTarget(constrain(v, 0.3f, 3.0f));
}

void AnimController::setIrisColor(uint16_t color)
{
    irisColorCurrent = color;
}

void AnimController::setBillboard(BillboardGlyph glyph)
{
    billboardCurrent = glyph;
}

void AnimController::setBaseEffect(EffectId id)
{
    baseEffect = id;
    if (activeEffect == EFFECT_NONE && id != EFFECT_NONE)
    {
        activeEffect = id;
        effectPhase = 0.0f;
    }
}

void AnimController::setLook(float x, float y)
{
    gaze.setTarget(constrain(x, -1.0f, 1.0f), constrain(y, -1.0f, 1.0f));
}

void AnimController::setIdle()
{
}

void AnimController::triggerBlink()
{
    if (blinkPhase == BLINK_IDLE)
    {
        blinkPhase = BLINK_CLOSING;
        blinkTimer = BLINK_CLOSE_S;
    }
}

void AnimController::setGazeSpring(float k, float damping)
{
    gaze.setParams(k, max(damping, minNonOscillatoryDamping(k)));
}

const EyeAnimationState &AnimController::getAnimationState() const
{
    return animationState;
}

// ─── Update ───────────────────────────────────────────────────────────────────

void AnimController::update(float dt)
{
    // ── Spring updates ───────────────────────────────────────────────────────
    gaze.update(dt);
    clampSpringAxis(gaze.x, -1.0f, 1.0f);
    clampSpringAxis(gaze.y, -1.0f, 1.0f);

    openSpring.update(dt);
    openSpring.pos = constrain(openSpring.pos, 0.0f, 1.0f);

    pupilSpring.update(dt);
    pupilSpring.pos = constrain(pupilSpring.pos, 0.1f, 0.9f);

    pupilAspectSpring.update(dt);
    pupilAspectSpring.pos = constrain(pupilAspectSpring.pos, 0.3f, 3.0f);

    // ── Blink ────────────────────────────────────────────────────────────────
    updateBlink(dt);

    // ── Effect phase / expiry ────────────────────────────────────────────────
    if (activeEffect != EFFECT_NONE)
    {
        effectPhase += dt * 0.8f; // ~0.8 rotations per second
        while (effectPhase > 1.0f)
            effectPhase -= 1.0f;
        // Timed effects expire; base effects (effectTimer == 0) persist
        if (effectTimer > 0.0f)
        {
            effectTimer -= dt;
            if (effectTimer <= 0.0f)
            {
                activeEffect = baseEffect;
                effectPhase = 0.0f;
            }
        }
    }
    else if (baseEffect != EFFECT_NONE)
    {
        activeEffect = baseEffect;
    }

    publishAnimationState();
}

// ─── Private helpers ──────────────────────────────────────────────────────────

void AnimController::updateBlink(float dt)
{
    switch (blinkPhase)
    {
    case BLINK_IDLE:
        blinkFactor = 1.0f;
        // Auto-blink is triggered externally by the high-level controller.
        break;

    case BLINK_CLOSING:
        blinkTimer -= dt;
        if (blinkTimer <= 0.0f)
        {
            blinkFactor = 0.0f;
            blinkPhase = BLINK_HOLDING;
            blinkTimer = BLINK_HOLD_S;
        }
        else
        {
            float t = 1.0f - blinkTimer / BLINK_CLOSE_S;
            blinkFactor = 1.0f - smoothStep(t);
        }
        break;

    case BLINK_HOLDING:
        blinkFactor = 0.0f;
        blinkTimer -= dt;
        if (blinkTimer <= 0.0f)
        {
            blinkPhase = BLINK_OPENING;
            blinkTimer = BLINK_OPEN_S;
        }
        break;

    case BLINK_OPENING:
        blinkTimer -= dt;
        if (blinkTimer <= 0.0f)
        {
            blinkFactor = 1.0f;
            blinkPhase = BLINK_IDLE;
        }
        else
        {
            float t = 1.0f - blinkTimer / BLINK_OPEN_S;
            blinkFactor = smoothStep(t);
        }
        break;
    }
}

void AnimController::publishAnimationState()
{
    animationState.gazeX = gaze.x.pos;
    animationState.gazeY = gaze.y.pos;
    animationState.eyelidOpen = openSpring.pos * blinkFactor;
    animationState.pupilScale = pupilSpring.pos;
    animationState.pupilAspect = pupilAspectSpring.pos;
    animationState.effectPhase = effectPhase;
    animationState.effect = activeEffect;
    animationState.irisColor = irisColorCurrent;
    animationState.billboard = billboardCurrent;
    animationState.leftEye = leftEye;
}
