#include "eye_styles/billboard_eye_renderer.h"
#include "internal/pixel_grid.h"
#include "config.h"

static void drawBillboardMoney(PixelGrid &grid, float cx_g, float cy_g)
{
    const float stemHw   = grid.toGridUnits(SCREEN_W * 0.04f);
    const float stemHh   = grid.toGridUnits(SCREEN_H * 0.34f);
    const float armHw    = grid.toGridUnits(SCREEN_W * 0.16f);
    const float armHh    = grid.toGridUnits(SCREEN_H * 0.045f);
    const float sideHw   = grid.toGridUnits(SCREEN_W * 0.045f);
    const float sideHh   = grid.toGridUnits(SCREEN_H * 0.12f);
    const float sideOffX = grid.toGridUnits(SCREEN_W * 0.11f);
    const float sideOffY = grid.toGridUnits(SCREEN_H * 0.11f);
    const float topY     = cy_g - grid.toGridUnits(SCREEN_H * 0.22f);
    const float bottomY  = cy_g + grid.toGridUnits(SCREEN_H * 0.22f);

    grid.drawRect(cx_g,            cy_g,            stemHw, stemHh);
    grid.drawRect(cx_g,            topY,            armHw,  armHh);
    grid.drawRect(cx_g,            cy_g,            armHw,  armHh);
    grid.drawRect(cx_g,            bottomY,         armHw,  armHh);
    grid.drawRect(cx_g - sideOffX, cy_g - sideOffY, sideHw, sideHh);
    grid.drawRect(cx_g + sideOffX, cy_g + sideOffY, sideHw, sideHh);
}

void BillboardEyeRenderer::render(LGFX_Sprite &spr, const EyeAnimationState &animationState) const
{
    spr.fillScreen(TFT_BLACK);

    PixelGrid grid(SCREEN_W, SCREEN_H);
    const float cx_g = grid.toGridCol((float)(SCREEN_W / 2));
    const float cy_g = grid.toGridRow((float)(SCREEN_H / 2));

    switch (animationState.billboard)
    {
    case BILLBOARD_GLYPH_HEART:
        grid.drawHeart(
            cx_g,
            cy_g - grid.toGridUnits(SCREEN_H * 0.03f),
            grid.toGridUnits(SCREEN_W * 0.32f),
            grid.toGridUnits(SCREEN_H * 0.32f));
        break;
    case BILLBOARD_GLYPH_MONEY:
        drawBillboardMoney(grid, cx_g, cy_g);
        break;
    case BILLBOARD_GLYPH_DEAD:
        grid.drawLine(cx_g, cy_g,
            grid.toGridUnits(SCREEN_W * 0.34f),
            grid.toGridUnits(SCREEN_H * 0.34f),
            1.0f);
        grid.drawLine(cx_g, cy_g,
            grid.toGridUnits(SCREEN_W * 0.34f),
            grid.toGridUnits(SCREEN_H * 0.34f),
            -1.0f);
        break;
    case BILLBOARD_GLYPH_NONE:
    default:
        break;
    }

    grid.flush(spr, animationState.irisColor);
}
