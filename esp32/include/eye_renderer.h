#pragma once
#include <LovyanGFX.hpp>
#include "eye_animator.h"
#include "types.h"
#include "visuals.h"

class IEyeRenderer
{
public:
    virtual ~IEyeRenderer() = default;

    // Draw one eye into the supplied sprite. Call pushSprite() afterwards.
    virtual void render(LGFX_Sprite &spr, const EyeAnimationState &animationState) const = 0;
};

const IEyeRenderer &getEyeRenderer(EyeRendererKind kind);
