#pragma once
#include "types.h"
#include "eye_animator.h"

class EyesControllerLowLevel
{
public:
    void init();

    void update(float dt);
    const EyeAnimationState &getAnimationState(uint8_t eyeIndex) const;
    EyeRendererKind getEyeRendererKind(uint8_t eyeIndex) const;

    // move pupil to direction, holds until setIdle().
    void lookDirection(float gazeX, float gazeY, float transitionSeconds, EnumEyes eyes = EYES_BOTH);

    void setLidOpenFactor(float openFactor, float transitionSeconds, EnumEyes eyes = EYES_BOTH);
    void setEyeRenderer(EyeRendererKind kind, EnumEyes eyes = EYES_BOTH);
    void setBillboard(BillboardGlyph glyph, EnumEyes eyes = EYES_BOTH);

    // Internal orchestration helpers used by high-level controller.
    void setIdle(EnumEyes eyes = EYES_BOTH);
    void setGazeSpring(float k, float damping, EnumEyes eyes = EYES_BOTH);

private:
    friend class EyesControllerHighLevel;

    AnimController anims[NUM_EYES];
    EyeRendererKind rendererKinds[NUM_EYES] = {
        EYE_RENDERER_NORMAL,
        EYE_RENDERER_NORMAL,
    };
    size_t animCount = NUM_EYES;

    void triggerBlink(EnumEyes eyes = EYES_BOTH);

    bool eyeSelected(size_t index, EnumEyes eyes) const;
    void applyVisualTransition(AnimController &anim, float transitionSeconds) const;
    void applyGazeTransition(AnimController &anim, float transitionSeconds) const;
};
