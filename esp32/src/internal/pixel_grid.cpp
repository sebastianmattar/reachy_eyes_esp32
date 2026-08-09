#include "internal/pixel_grid.h"

#include <math.h>
#include <string.h>

// ── Anti-aliased dot stamp ────────────────────────────────────────────────────

#define DOT_AA_MAX_R 8
#define DOT_AA_MAXSIZE (DOT_AA_MAX_R * 2 + 3)

static_assert(DOT_RADIUS <= DOT_AA_MAX_R,
              "DOT_RADIUS exceeds DOT_AA_MAX_R; increase DOT_AA_MAX_R in pixel_grid.cpp");

static uint8_t s_dotAlpha[DOT_AA_MAXSIZE][DOT_AA_MAXSIZE];
static uint16_t s_dotStamp[DOT_AA_MAXSIZE][DOT_AA_MAXSIZE];
static bool s_dotAlphaBuilt = false;
static uint16_t s_dotStampColor = 0xFFFF;

static constexpr int SAFE_DOT_RADIUS_SQ =
    (DISPLAY_RADIUS - DOT_RADIUS) * (DISPLAY_RADIUS - DOT_RADIUS);

static void buildDotAlpha()
{
    const int half = DOT_RADIUS + 1;
    const float rf = (float)DOT_RADIUS;
    for (int dy = -half; dy <= half; dy++)
    {
        for (int dx = -half; dx <= half; dx++)
        {
            float dist = sqrtf((float)(dx * dx + dy * dy));
            float a = rf + 0.5f - dist;
            if (a < 0.0f)
                a = 0.0f;
            if (a > 1.0f)
                a = 1.0f;
            s_dotAlpha[dy + half][dx + half] = (uint8_t)(a * 255.0f + 0.5f);
        }
    }
    s_dotAlphaBuilt = true;
}

static void rebuildStamp(uint16_t color)
{
    const int half = DOT_RADIUS + 1;
    const int size = half * 2 + 1;
    const uint8_t r5 = (color >> 11) & 0x1Fu;
    const uint8_t g6 = (color >> 5) & 0x3Fu;
    const uint8_t b5 = color & 0x1Fu;
    for (int i = 0; i < size; i++)
    {
        for (int j = 0; j < size; j++)
        {
            uint8_t a = s_dotAlpha[i][j];
            if (a == 0)
                s_dotStamp[i][j] = 0;
            else if (a == 255)
                s_dotStamp[i][j] = color;
            else
                s_dotStamp[i][j] = (uint16_t)((((uint32_t)r5 * a >> 8) << 11) |
                                              (((uint32_t)g6 * a >> 8) << 5) |
                                              ((uint32_t)b5 * a >> 8));
        }
    }
    s_dotStampColor = color;
}

static void drawDotAA(LGFX_Sprite &spr, int cx, int cy, uint16_t color)
{
    int ex = cx - CX;
    int ey = cy - CY;
    if (ex * ex + ey * ey > SAFE_DOT_RADIUS_SQ)
        return;

    if (!s_dotAlphaBuilt)
        buildDotAlpha();
    if (color != s_dotStampColor)
        rebuildStamp(color);

    const int half = DOT_RADIUS + 1;
    for (int dy = -half; dy <= half; dy++)
    {
        int y = cy + dy;
        if (y < 0 || y >= SCREEN_H)
            continue;
        const int row = dy + half;
        for (int dx = -half; dx <= half; dx++)
        {
            if (!s_dotAlpha[row][dx + half])
                continue;
            int x = cx + dx;
            if (x < 0 || x >= SCREEN_W)
                continue;
            spr.drawPixel(x, y, s_dotStamp[row][dx + half]);
        }
    }
}

// ── PixelGrid ─────────────────────────────────────────────────────────────────

PixelGrid::PixelGrid(int pixelWidth, int pixelHeight)
{
    m_cols = pixelWidth / GRID_SPACING;
    m_rows = pixelHeight / GRID_SPACING;
    m_offsetX = (pixelWidth - m_cols * GRID_SPACING) / 2 + GRID_SPACING / 2;
    m_offsetY = (pixelHeight - m_rows * GRID_SPACING) / 2 + GRID_SPACING / 2;
    memset(m_grid, 0, sizeof(m_grid));
}

float PixelGrid::toGridCol(float px) const
{
    return (px - (float)m_offsetX) / (float)GRID_SPACING;
}

float PixelGrid::toGridRow(float py) const
{
    return (py - (float)m_offsetY) / (float)GRID_SPACING;
}

float PixelGrid::toGridUnits(float pixels) const
{
    return pixels / (float)GRID_SPACING;
}

// ── Shape drawing methods ─────────────────────────────────────────────────────

void PixelGrid::drawEllipse(float cx_g, float cy_g, float hw_g, float hh_g)
{
    for (int row = 0; row < m_rows; row++)
    {
        float dy = row - cy_g;
        if (fabsf(dy) > hh_g)
            continue;
        float dx = hw_g * sqrtf(1.0f - (dy / hh_g) * (dy / hh_g));
        int colMin = max((int)ceilf(cx_g - dx), 0);
        int colMax = min((int)floorf(cx_g + dx), m_cols - 1);
        for (int col = colMin; col <= colMax; col++)
            m_grid[row][col] = true;
    }
}

void PixelGrid::drawRect(float cx_g, float cy_g, float hw_g, float hh_g)
{
    int rowMin = max((int)ceilf(cy_g - hh_g), 0);
    int rowMax = min((int)floorf(cy_g + hh_g), m_rows - 1);
    int colMin = max((int)ceilf(cx_g - hw_g), 0);
    int colMax = min((int)floorf(cx_g + hw_g), m_cols - 1);
    for (int row = rowMin; row <= rowMax; row++)
        for (int col = colMin; col <= colMax; col++)
            m_grid[row][col] = true;
}

void PixelGrid::drawRoundRect(float cx_g, float cy_g, float hw_g, float hh_g)
{
    float cr = min(hw_g, hh_g) * 0.4f;
    float ex = max(hw_g - cr, 0.0f);
    float ey = max(hh_g - cr, 0.0f);
    for (int row = 0; row < m_rows; row++)
    {
        float dy = row - cy_g;
        if (fabsf(dy) > hh_g)
            continue;
        float ady = fabsf(dy);
        float dx;
        if (ady <= ey)
        {
            dx = hw_g;
        }
        else
        {
            float fdy = ady - ey;
            dx = (fdy <= cr) ? ex + sqrtf(cr * cr - fdy * fdy) : 0.0f;
        }
        int colMin = max((int)ceilf(cx_g - dx), 0);
        int colMax = min((int)floorf(cx_g + dx), m_cols - 1);
        for (int col = colMin; col <= colMax; col++)
            m_grid[row][col] = true;
    }
}

void PixelGrid::drawCross(float cx_g, float cy_g, float hw_g, float hh_g)
{
    float thick = min(hw_g, hh_g) * 0.35f;
    for (int row = 0; row < m_rows; row++)
    {
        float dy = row - cy_g;
        int colMin;
        int colMax;
        if (fabsf(dy) <= thick)
        {
            colMin = 0;
            colMax = m_cols - 1;
        }
        else
        {
            colMin = max((int)ceilf(cx_g - thick), 0);
            colMax = min((int)floorf(cx_g + thick), m_cols - 1);
        }
        for (int col = colMin; col <= colMax; col++)
            m_grid[row][col] = true;
    }
}

void PixelGrid::drawHeart(float cx_g, float cy_g, float hw_g, float hh_g)
{
    float s = min(hw_g, hh_g) * 0.85f;
    if (s < 0.001f)
        return;
    int rowMin = max((int)ceilf(cy_g - hh_g), 0);
    int rowMax = min((int)floorf(cy_g + hh_g), m_rows - 1);
    int colMin = max((int)ceilf(cx_g - hw_g), 0);
    int colMax = min((int)floorf(cx_g + hw_g), m_cols - 1);
    for (int row = rowMin; row <= rowMax; row++)
    {
        float dy = row - cy_g;
        for (int col = colMin; col <= colMax; col++)
        {
            float dx = col - cx_g;
            float nx = dx / s;
            float ny = -dy / s + 0.1f;
            float f = nx * nx + ny * ny - 1.0f;
            if (f * f * f <= nx * nx * (ny * ny * ny))
                m_grid[row][col] = true;
        }
    }
}

void PixelGrid::drawLine(float cx_g, float cy_g, float hw_g, float hh_g, float sign)
{
    for (int row = 0; row < m_rows; row++)
    {
        float dy = row - cy_g;
        float dx_ctr = sign * hw_g * dy / hh_g;
        float half_dx = hw_g * 0.28f;
        float left = max(dx_ctr - half_dx, -hw_g);
        float right = min(dx_ctr + half_dx, hw_g);
        int colMin = max((int)ceilf(cx_g + left), 0);
        int colMax = min((int)floorf(cx_g + right), m_cols - 1);
        for (int col = colMin; col <= colMax; col++)
            m_grid[row][col] = true;
    }
}

void PixelGrid::drawSpiral(float cx_g, float cy_g, float hw_g, float hh_g, float phase)
{
    float orbitR = min(hw_g, hh_g) * 0.45f;
    float dotR = min(hw_g, hh_g) * 0.22f;
    float dotR2 = dotR * dotR;
    int rowMin = max((int)ceilf(cy_g - hh_g), 0);
    int rowMax = min((int)floorf(cy_g + hh_g), m_rows - 1);
    int colMin = max((int)ceilf(cx_g - hw_g), 0);
    int colMax = min((int)floorf(cx_g + hw_g), m_cols - 1);
    for (int row = rowMin; row <= rowMax; row++)
    {
        float dy = row - cy_g;
        for (int col = colMin; col <= colMax; col++)
        {
            float dx = col - cx_g;
            for (int i = 0; i < 3; i++)
            {
                float a = (phase + i * (1.0f / 3.0f)) * 2.0f * (float)M_PI;
                float ox = cosf(a) * orbitR;
                float oy = sinf(a) * orbitR;
                float ex = dx - ox;
                float ey = dy - oy;
                if (ex * ex + ey * ey <= dotR2)
                {
                    m_grid[row][col] = true;
                    break;
                }
            }
        }
    }
}

void PixelGrid::drawArc(float cx_g, float cy_g, float hw_g, float hh_g)
{
    constexpr float kInner = 0.52f;
    for (int row = 0; row < m_rows; row++)
    {
        float ny = (row - cy_g) / hh_g;
        if (ny < -0.15f || fabsf(ny) > 1.0f)
            continue;
        float outer_dx = hw_g * sqrtf(1.0f - ny * ny);
        float inner_sq = kInner * kInner - ny * ny;
        if (inner_sq > 0.0f)
        {
            float inner_dx = hw_g * sqrtf(inner_sq);
            int c0 = max((int)ceilf(cx_g - outer_dx), 0);
            int c1 = min((int)floorf(cx_g - inner_dx), m_cols - 1);
            for (int col = c0; col <= c1; col++)
                m_grid[row][col] = true;
            int c2 = max((int)ceilf(cx_g + inner_dx), 0);
            int c3 = min((int)floorf(cx_g + outer_dx), m_cols - 1);
            for (int col = c2; col <= c3; col++)
                m_grid[row][col] = true;
        }
        else
        {
            int c0 = max((int)ceilf(cx_g - outer_dx), 0);
            int c1 = min((int)floorf(cx_g + outer_dx), m_cols - 1);
            for (int col = c0; col <= c1; col++)
                m_grid[row][col] = true;
        }
    }
}

void PixelGrid::drawTrapezoid(float cx_g, float cy_g, float hw_g, float hh_g, float sign)
{
    constexpr float kSlope = 0.55f;
    float cr_g = min(hw_g, hh_g) * 0.30f;
    float ex_g = hw_g - cr_g;
    float ey_g = hh_g - cr_g;
    for (int row = 0; row < m_rows; row++)
    {
        float dy = row - cy_g;
        if (dy > hh_g)
            continue;
        float slant = (dy + hh_g) * hw_g / (hh_g * kSlope);
        int colMin;
        int colMax;
        if (sign > 0.0f)
        {
            if (slant < -hw_g)
                continue;
            colMin = (int)ceilf(cx_g - hw_g);
            colMax = (int)floorf(cx_g + min(hw_g, slant));
        }
        else
        {
            if (-slant > hw_g)
                continue;
            colMin = (int)ceilf(cx_g + max(-hw_g, -slant));
            colMax = (int)floorf(cx_g + hw_g);
        }
        colMin = max(colMin, 0);
        colMax = min(colMax, m_cols - 1);
        for (int col = colMin; col <= colMax; col++)
        {
            float dx = col - cx_g;
            float fex = max(fabsf(dx) - ex_g, 0.0f);
            float fey = max(dy - ey_g, 0.0f);
            if (fex > 0.0f && fey > 0.0f && fex * fex + fey * fey > cr_g * cr_g)
                continue;
            m_grid[row][col] = true;
        }
    }
}

// ── Flush ─────────────────────────────────────────────────────────────────────

void PixelGrid::flush(LGFX_Sprite &spr, uint16_t color) const
{
    for (int row = 0; row < m_rows; row++)
    {
        int dotY = m_offsetY + row * GRID_SPACING;
        for (int col = 0; col < m_cols; col++)
        {
            if (m_grid[row][col])
                drawDotAA(spr, m_offsetX + col * GRID_SPACING, dotY, color);
        }
    }
}
