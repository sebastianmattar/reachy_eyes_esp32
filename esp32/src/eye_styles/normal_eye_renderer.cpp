#include "eye_styles/normal_eye_renderer.h"
#include "internal/pixel_grid.h"
#include "config.h"
#include <math.h>

static constexpr float kCX = (float)(SCREEN_W / 2);
static constexpr float kCY = (float)(SCREEN_H / 2);

void NormalEyeRenderer::render(LGFX_Sprite &spr, const EyeAnimationState &animationState) const
{
    spr.fillScreen(TFT_BLACK);

    const float pupilOffsetX = constrain(animationState.gazeX,      -1.0f, 1.0f);
    const float pupilOffsetY = constrain(animationState.gazeY,      -1.0f, 1.0f);
    const float eyelidOpen   = constrain(animationState.eyelidOpen,  0.0f, 1.0f);
    const float pupilScale   = constrain(animationState.pupilScale,  0.1f, 0.9f);
    const float pupilAspect  = constrain(animationState.pupilAspect, 0.3f, 3.0f);

    float hw = max((float)PUPIL_BASE_W * pupilScale * pupilAspect, 2.0f);
    float hh = max((float)PUPIL_BASE_H * pupilScale / pupilAspect * eyelidOpen, 1.0f);

    float px = kCX + pupilOffsetX * max(kCX - hw, 0.0f);
    float py = kCY + pupilOffsetY * max(kCY - hh, 0.0f);

    if (animationState.effect == EFFECT_DIZZY)
    {
        float a   = animationState.effectPhase * 2.0f * (float)M_PI;
        float orb = max(hw / 3.0f, 4.0f);
        px += cosf(a) * orb;
        py += sinf(a) * orb;
    }

    PixelGrid grid(SCREEN_W, SCREEN_H);
    float cx_g = grid.toGridCol(px);
    float cy_g = grid.toGridRow(py);
    float hw_g = grid.toGridUnits(hw);
    float hh_g = grid.toGridUnits(hh);

    grid.drawRect(cx_g, cy_g, hw_g, hh_g);
    grid.flush(spr, animationState.irisColor);
}
