
#include "eye_motion.h"

void EyesControllerLowLevel::init()
{
    anims[0].init(true);
    anims[1].init(false);
}

void EyesControllerLowLevel::update(float dt)
{
    for (size_t i = 0; i < animCount; i++)
    {
        anims[i].update(dt);
    }
}

const EyeAnimationState &EyesControllerLowLevel::getAnimationState(uint8_t eyeIndex) const
{
    return anims[eyeIndex].getAnimationState();
}

EyeRendererKind EyesControllerLowLevel::getEyeRendererKind(uint8_t eyeIndex) const
{
    return rendererKinds[eyeIndex];
}

bool EyesControllerLowLevel::eyeSelected(size_t index, EnumEyes eyes) const
{
    if (eyes == EYES_BOTH)
        return true;
    if (eyes == EYES_LEFT_ONLY)
        return index == 0;
    return index == 1;
}

void EyesControllerLowLevel::applyVisualTransition(AnimController &anim, float transitionSeconds) const
{
    // Approximate spring params from desired settle time.
    float t = constrain(transitionSeconds, 0.05f, 2.0f);
    float k = constrain(16.0f / (t * t), 40.0f, 900.0f);
    float d = 2.0f * sqrtf(k);
    anim.setSpringParams(k, d);
}

void EyesControllerLowLevel::applyGazeTransition(AnimController &anim, float transitionSeconds) const
{
    // Use non-oscillatory damping for gaze so movement has no springy bounce.
    float t = constrain(transitionSeconds, 0.05f, 2.0f);
    float k = constrain(16.0f / (t * t), 40.0f, 900.0f);
    float d = 2.2f * sqrtf(k);
    anim.setGazeSpring(k, d);
}

void EyesControllerLowLevel::lookDirection(float gazeX, float gazeY, float transitionSeconds, EnumEyes eyes)
{
    for (size_t i = 0; i < animCount; i++)
    {
        if (!eyeSelected(i, eyes))
            continue;
        applyGazeTransition(anims[i], transitionSeconds);
        anims[i].setLook(gazeX, gazeY);
    }
}

void EyesControllerLowLevel::setLidOpenFactor(float openFactor, float transitionSeconds, EnumEyes eyes)
{
    for (size_t i = 0; i < animCount; i++)
    {
        if (!eyeSelected(i, eyes))
            continue;
        applyVisualTransition(anims[i], transitionSeconds);
        anims[i].setOpenFactor(openFactor);
    }
}

void EyesControllerLowLevel::setEyeRenderer(EyeRendererKind kind, EnumEyes eyes)
{
    for (size_t i = 0; i < animCount; i++)
    {
        if (eyeSelected(i, eyes))
            rendererKinds[i] = kind;
    }
}

void EyesControllerLowLevel::setBillboard(BillboardGlyph glyph, EnumEyes eyes)
{
    for (size_t i = 0; i < animCount; i++)
    {
        if (eyeSelected(i, eyes))
            anims[i].setBillboard(glyph);
    }
}

void EyesControllerLowLevel::setIdle(EnumEyes eyes)
{
    for (size_t i = 0; i < animCount; i++)
    {
        if (eyeSelected(i, eyes))
            anims[i].setIdle();
    }
}

void EyesControllerLowLevel::triggerBlink(EnumEyes eyes)
{
    for (size_t i = 0; i < animCount; i++)
    {
        if (eyeSelected(i, eyes))
            anims[i].triggerBlink();
    }
}

void EyesControllerLowLevel::setGazeSpring(float k, float damping, EnumEyes eyes)
{
    for (size_t i = 0; i < animCount; i++)
    {
        if (eyeSelected(i, eyes))
            anims[i].setGazeSpring(k, damping);
    }
}
