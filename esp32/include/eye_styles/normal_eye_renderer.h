#pragma once
#include "eye_renderer.h"

class NormalEyeRenderer final : public IEyeRenderer
{
public:
    void render(LGFX_Sprite &spr, const EyeAnimationState &animationState) const override;
};
