#include "internal/geometry.h"

void fillSolidTrapezoid(LGFX_Sprite &spr,
                        float cx, float cy, float hw, float hh,
                        float sign, uint16_t color)
{
    constexpr float kSlope = 0.55f;
    float cornerRadius = max(min(hw, hh) * 0.22f, 20.0f);
    float innerHalfWidth = max(hw - cornerRadius, 0.0f);
    float innerHalfHeight = max(hh - cornerRadius, 0.0f);

    int rowMin = max((int)ceilf(cy - hh), 0);
    int rowMax = min((int)floorf(cy + hh), SCREEN_H - 1);
    for (int y = rowMin; y <= rowMax; y++)
    {
        float dy = y - cy;
        if (dy > hh)
            continue;

        float slant = (dy + hh) * hw / (hh * kSlope);
        int colMin;
        int colMax;
        if (sign > 0.0f)
        {
            if (slant < -hw)
                continue;
            colMin = (int)ceilf(cx - hw);
            colMax = (int)floorf(cx + min(hw, slant));
        }
        else
        {
            if (-slant > hw)
                continue;
            colMin = (int)ceilf(cx + max(-hw, -slant));
            colMax = (int)floorf(cx + hw);
        }

        colMin = max(colMin, 0);
        colMax = min(colMax, SCREEN_W - 1);
        for (int x = colMin; x <= colMax; x++)
        {
            float dx = x - cx;
            float fx = max(fabsf(dx) - innerHalfWidth, 0.0f);
            float fy = max(dy - innerHalfHeight, 0.0f);
            if (fx > 0.0f && fy > 0.0f && fx * fx + fy * fy > cornerRadius * cornerRadius)
                continue;
            spr.drawPixel(x, y, color);
        }
    }
}