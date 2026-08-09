#pragma once
#include "eye_renderer.h"

class TrapezoidEyeRenderer final : public IEyeRenderer
{
public:
    void render(LGFX_Sprite &spr, const EyeAnimationState &animationState) const override;
};
