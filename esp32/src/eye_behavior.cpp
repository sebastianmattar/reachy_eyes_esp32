#include "eye_behavior.h"

#include <math.h>
#include "config.h"

namespace
{
    constexpr float IDLE_TO_WANDER_DELAY_S = 1.0f;
}

void EyesControllerHighLevel::init(EyesControllerLowLevel *lowLevel)
{
    low = lowLevel;
    blinkCountdown = sampleBlinkInterval();
    setStyle(&m_normalStyle, EYES_BOTH);
    enterIdleState();
}

// ── Style management ──────────────────────────────────────────────────────────

void EyesControllerHighLevel::setStyle(IEyeStyle *style, EnumEyes eyes)
{
    m_activeStyle     = style;
    m_activeStyleEyes = eyes;
    if (low && style)
        style->activate(*low, eyes);
}

// ── Update ────────────────────────────────────────────────────────────────────

void EyesControllerHighLevel::update(float dt)
{
    if (!low)
        return;

    // Advance active style; revert to normal when it expires
    if (m_activeStyle && !m_activeStyle->update(dt, *low))
    {
        low->setIdle(m_activeStyleEyes);
        setStyle(&m_normalStyle, m_activeStyleEyes);
        enterIdleState();
    }

    // Idle state machine — only counts down toward wandering when the style allows it
    if (state == EYESTATE_IDLE)
    {
        if (!m_activeStyle || m_activeStyle->canWander())
        {
            idleTimer -= dt;
            if (idleTimer <= 0.0f)
                enterWanderingState();
        }
    }
    else if (state == EYESTATE_WANDERING)
    {
        updateWander(dt);
    }

    // Blink logic only runs when the style permits it
    if (!m_activeStyle || m_activeStyle->canBlink())
    {
        updateAutoBlink(dt);
        updateBlinkQueue(dt);
    }

    updateTimedResets(dt);
}

// ── Commands ──────────────────────────────────────────────────────────────────

void EyesControllerHighLevel::idle()
{
    if (!low)
        return;

    setStyle(&m_normalStyle, EYES_BOTH);
    low->setIdle(EYES_BOTH);
    enterIdleState();

    gazeReset.active   = false;
    squintReset.active = false;
    blinkQueue.active  = false;
}

void EyesControllerHighLevel::look(float x, float y, float transitionSeconds, EnumEyes eyes)
{
    if (!low)
        return;

    clampDirection(x, y);
    enterExecutingState();          // suppress wandering; never auto-returns to idle
    low->lookDirection(x, y, transitionSeconds, eyes);

    // Streaming gaze holds indefinitely — cancel any pending auto-return from a
    // prior gaze() so a stale reset can't yank the eyes back mid-stream.
    gazeReset.active = false;
}

void EyesControllerHighLevel::gaze(EnumDirection direction, float transitionSeconds, float resetAfterSeconds, EnumEyes eyes)
{
    if (!low)
        return;

    float x = directionToX(direction);
    float y = directionToY(direction);
    clampDirection(x, y);
    enterExecutingState();
    low->lookDirection(x, y, transitionSeconds, eyes);

    if (resetAfterSeconds > 0.0f)
    {
        gazeReset.active = true;
        gazeReset.timer  = resetAfterSeconds;
        gazeReset.eyes   = eyes;
    }
}

void EyesControllerHighLevel::blink(int times, EnumEyes eyes)
{
    if (!low || times <= 0)
        return;

    blinkQueue.active = true;
    blinkQueue.eyes   = eyes;
    blinkQueue.remaining += times;
    if (blinkQueue.timer <= 0.0f)
        blinkQueue.timer = 0.0f;
}

void EyesControllerHighLevel::squint(float transitionSeconds, float resetAfterSeconds)
{
    if (!low)
        return;

    low->setLidOpenFactor(0.35f, transitionSeconds, EYES_BOTH);

    if (resetAfterSeconds > 0.0f)
    {
        squintReset.active = true;
        squintReset.timer  = resetAfterSeconds;
        squintReset.eyes   = EYES_BOTH;
    }
}

void EyesControllerHighLevel::hearts(EnumEyes eyes)
{
    if (!low)
        return;
    m_billboardStyle.configure(BILLBOARD_GLYPH_HEART, HEARTS_DURATION_S);
    setStyle(&m_billboardStyle, eyes);
    enterExecutingState();
}

void EyesControllerHighLevel::money(EnumEyes eyes)
{
    if (!low)
        return;
    m_billboardStyle.configure(BILLBOARD_GLYPH_MONEY, HEARTS_DURATION_S);
    setStyle(&m_billboardStyle, eyes);
    enterExecutingState();
}

void EyesControllerHighLevel::dead(EnumEyes eyes)
{
    if (!low)
        return;
    m_billboardStyle.configure(BILLBOARD_GLYPH_DEAD, HEARTS_DURATION_S);
    setStyle(&m_billboardStyle, eyes);
    enterExecutingState();
}

void EyesControllerHighLevel::trapezoid(EnumEyes eyes)
{
    if (!low)
        return;
    setStyle(&m_trapezoidStyle, eyes);
    enterExecutingState();
}

void EyesControllerHighLevel::playGif(EnumEyes eyes)
{
    if (!low)
        return;
    // GIF style never auto-expires; it loops until IDLE/STOPGIF switches away.
    setStyle(&m_gifStyle, eyes);
    enterExecutingState();
}

// ── Blink interval ────────────────────────────────────────────────────────────

void EyesControllerHighLevel::setBlinkInterval(float minSec, float maxSec)
{
    blinkIntervalMin = max(0.0f, minSec);
    blinkIntervalMax = max(0.0f, maxSec);
    if (blinkIntervalMax < blinkIntervalMin)
    {
        float tmp        = blinkIntervalMin;
        blinkIntervalMin = blinkIntervalMax;
        blinkIntervalMax = tmp;
    }
    blinkCountdown = sampleBlinkInterval();
}

// ── Private update helpers ────────────────────────────────────────────────────

void EyesControllerHighLevel::updateWander(float dt)
{
    if (wanderDwell > 0.0f)
    {
        wanderDwell -= dt;
    }
    else
    {
        float x   = randF(-WANDER_EXTENT_X, WANDER_EXTENT_X);
        float y   = randF(-WANDER_EXTENT_Y, WANDER_EXTENT_Y);
        wanderDwell = randF(WANDER_DWELL_MIN, WANDER_DWELL_MAX);
        low->setGazeSpring(DART_SPRING_K, DART_SPRING_DAMPING);
        low->lookDirection(x, y, 0.05f, EYES_BOTH);
    }
}

void EyesControllerHighLevel::updateAutoBlink(float dt)
{
    blinkCountdown -= dt;
    if (blinkCountdown > 0.0f)
        return;

    blink(1, EYES_BOTH);
    if (randF(0.0f, 1.0f) > 0.9f)
        blinkCountdown = 0.2f;
    else
        blinkCountdown = sampleBlinkInterval();
}

void EyesControllerHighLevel::updateBlinkQueue(float dt)
{
    if (!blinkQueue.active)
        return;

    blinkQueue.timer -= dt;
    if (blinkQueue.timer > 0.0f)
        return;

    low->triggerBlink(blinkQueue.eyes);
    blinkQueue.remaining--;
    if (blinkQueue.remaining <= 0)
    {
        blinkQueue.active = false;
        blinkCountdown = sampleBlinkInterval();
        return;
    }

    blinkQueue.timer = 0.20f;
}

void EyesControllerHighLevel::updateTimedResets(float dt)
{
    if (gazeReset.active)
    {
        gazeReset.timer -= dt;
        if (gazeReset.timer <= 0.0f)
        {
            low->setIdle(gazeReset.eyes);
            enterIdleState();
            gazeReset.active = false;
        }
    }

    if (squintReset.active)
    {
        squintReset.timer -= dt;
        if (squintReset.timer <= 0.0f)
        {
            low->setLidOpenFactor(0.75f, 0.20f, squintReset.eyes);
            squintReset.active = false;
        }
    }
}

// ── State machine transitions ─────────────────────────────────────────────────

void EyesControllerHighLevel::enterIdleState()
{
    state       = EYESTATE_IDLE;
    idleTimer   = IDLE_TO_WANDER_DELAY_S;
    wanderDwell = randF(WANDER_DWELL_MIN, WANDER_DWELL_MAX);
}

void EyesControllerHighLevel::enterExecutingState()
{
    state     = EYESTATE_EXECUTING;
    idleTimer = 0.0f;
}

void EyesControllerHighLevel::enterWanderingState()
{
    state       = EYESTATE_WANDERING;
    wanderDwell = randF(WANDER_DWELL_MIN, WANDER_DWELL_MAX);
    low->setIdle(EYES_BOTH);
}

// ── Static helpers ────────────────────────────────────────────────────────────

float EyesControllerHighLevel::randF(float lo, float hi)
{
    return lo + (random(10000) / 10000.0f) * (hi - lo);
}

float EyesControllerHighLevel::sampleBlinkInterval() const
{
    float span = blinkIntervalMax - blinkIntervalMin;
    if (span <= 0.0f)
        return blinkIntervalMin;
    return blinkIntervalMin + (random(10000) / 10000.0f) * span;
}

void EyesControllerHighLevel::clampDirection(float &x, float &y)
{
    x = constrain(x, -1.0f, 1.0f);
    y = constrain(y, -1.0f, 1.0f);
}

float EyesControllerHighLevel::directionToX(EnumDirection direction)
{
    switch (direction)
    {
    case DIRECTION_E:
    case DIRECTION_NE:
    case DIRECTION_SE:
        return 0.8f;
    case DIRECTION_W:
    case DIRECTION_NW:
    case DIRECTION_SW:
        return -0.8f;
    default:
        return 0.0f;
    }
}

float EyesControllerHighLevel::directionToY(EnumDirection direction)
{
    switch (direction)
    {
    case DIRECTION_N:
    case DIRECTION_NE:
    case DIRECTION_NW:
        return -0.6f;
    case DIRECTION_S:
    case DIRECTION_SE:
    case DIRECTION_SW:
        return 0.6f;
    default:
        return 0.0f;
    }
}
