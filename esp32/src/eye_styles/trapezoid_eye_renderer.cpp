#include "eye_styles/trapezoid_eye_renderer.h"
#include "config.h"
#include <math.h>
#include "internal/geometry.h"

static constexpr float kCX = (float)(SCREEN_W / 2);
static constexpr float kCY = (float)(SCREEN_H / 2);

void TrapezoidEyeRenderer::render(LGFX_Sprite &spr, const EyeAnimationState &animationState) const
{
    spr.fillScreen(TFT_BLACK);

    const float pupilOffsetX = constrain(animationState.gazeX, -1.0f, 1.0f);
    const float pupilOffsetY = constrain(animationState.gazeY, -1.0f, 1.0f);
    const float eyelidOpen = constrain(animationState.eyelidOpen, 0.0f, 1.0f);
    const float pupilScale = constrain(animationState.pupilScale, 0.15f, 0.85f);

    float hw = max((float)PUPIL_BASE_W * pupilScale * 0.96f, 8.0f);
    float hh = max((float)PUPIL_BASE_H * pupilScale * eyelidOpen * 0.74f, 5.0f);

    float px = kCX + pupilOffsetX * max(kCX - hw, 0.0f) * 0.78f;
    float py = kCY - SCREEN_H * 0.04f + pupilOffsetY * max(kCY - hh, 0.0f) * 0.46f;

    fillSolidTrapezoid(
        spr,
        px, py, hw, hh,
        animationState.leftEye ? 1.0f : -1.0f,
        animationState.irisColor);
}
