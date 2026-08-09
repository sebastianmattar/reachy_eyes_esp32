#pragma once

#include <LovyanGFX.hpp>
#include "config.h"

// Grid of boolean cells, each cell corresponding to one lit dot on screen.
// Instantiate with the pixel dimensions of the target area; the grid
// dimensions and offsets are derived from those and GRID_SPACING.
// Call shape methods to mark cells, then flush() to render anti-aliased dots.
class PixelGrid
{
public:
    PixelGrid(int pixelWidth, int pixelHeight);

    // Pixel-to-grid coordinate conversion helpers
    float toGridCol(float px) const;
    float toGridRow(float py) const;
    float toGridUnits(float pixels) const;

    // Shape drawing — all coordinates and sizes in grid units
    void drawEllipse(float cx_g, float cy_g, float hw_g, float hh_g);
    void drawRect(float cx_g, float cy_g, float hw_g, float hh_g);
    void drawRoundRect(float cx_g, float cy_g, float hw_g, float hh_g);
    void drawCross(float cx_g, float cy_g, float hw_g, float hh_g);
    void drawHeart(float cx_g, float cy_g, float hw_g, float hh_g);
    void drawLine(float cx_g, float cy_g, float hw_g, float hh_g, float sign);
    void drawSpiral(float cx_g, float cy_g, float hw_g, float hh_g, float phase);
    void drawArc(float cx_g, float cy_g, float hw_g, float hh_g);
    void drawTrapezoid(float cx_g, float cy_g, float hw_g, float hh_g, float sign);

    // Render all lit cells to the sprite as anti-aliased dots
    void flush(LGFX_Sprite &spr, uint16_t color) const;

private:
    int m_cols;
    int m_rows;
    int m_offsetX;
    int m_offsetY;
    bool m_grid[GRID_ROWS][GRID_COLS];
};
