#pragma once
// ─── GifPlayer ────────────────────────────────────────────────────────────────
// Streams the single stored GIF (GIF_PATH on LittleFS) and decodes one frame at
// a time straight into the two eye sprites — no full-frame scratch buffer.
//
// A single decode fills BOTH eyes with an identical (non-mirrored) copy: the
// GIFDRAW callback nearest-neighbor-scales each source scanline into the sprite,
// writing each pixel to the same (x,y) in sprite0 and sprite1.  The image is
// scaled to fit the 200×200 sprite preserving aspect ratio (letterboxed,
// centered); the surrounding margin is filled black on the first frame of each
// loop cycle.
//
// Runs on both targets: AnimatedGIF is portable C, so only the file backend
// differs (LittleFS on hardware, stdio in the simulator — see gif_player.cpp).
#include <LovyanGFX.hpp>
#include <AnimatedGIF.h>
#include "config.h"

class GifPlayer
{
public:
    // Cache the two sprite framebuffers the decoder writes into.  Call once after
    // the sprites have been created (createSprite) in setup().
    void begin(LGFX_Sprite *s0, LGFX_Sprite *s1);

    // Open the gif at `path` (default: the stored slot GIF_PATH — the hardware
    // case; the simulator passes a local filesystem path), compute the letterbox
    // scale/offsets, and arm the first frame. Returns false if it cannot be parsed.
    bool open(const char *path = GIF_PATH);

    // Release the open gif (safe to call when not open).
    void close();

    bool isOpen() const { return m_open; }

    // Advance the frame timer; decode + composite the next frame when it is due.
    void update(float dt);

private:
    // AnimatedGIF callbacks (static — reach the active instance via s_self).
    static void    drawCallback(GIFDRAW *pDraw);
    static void   *fileOpen(const char *fname, int32_t *pSize);
    static void    fileClose(void *pHandle);
    static int32_t fileRead(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen);
    static int32_t fileSeek(GIFFILE *pFile, int32_t iPosition);

    void drawNextFrame();
    void clearSprites();
    // Erase a canvas-space rect (mapped through the same letterbox/scale) in both
    // sprites — used to apply GIF "restore to background" disposal.
    void clearCanvasRect(int cx, int cy, int cw, int ch);

    static GifPlayer *s_self; // active player, for the static callbacks

    AnimatedGIF  m_gif;
    LGFX_Sprite *m_spr0 = nullptr;
    LGFX_Sprite *m_spr1 = nullptr;
    uint16_t    *m_buf0 = nullptr; // sprite0 framebuffer (swap565, SCREEN_W stride)
    uint16_t    *m_buf1 = nullptr; // sprite1 framebuffer

    bool  m_open      = false;
    bool  m_clearNext = true;  // clear letterbox margins before the next frame 0
    float m_frameTimer = 0.0f; // seconds until the next frame is due

    // Deferred GIF frame disposal.  A frame's disposal method describes what to do
    // with THAT frame's rect after it has been shown, before the next frame is
    // drawn.  Method 2 ("restore to background") must erase the rect, otherwise
    // successive frames composite on top of each other and the animation smears.
    bool m_disposeNext = false;
    int  m_dispX = 0, m_dispY = 0, m_dispW = 0, m_dispH = 0;

    // Source canvas + scaled (letterboxed) placement in sprite coordinates.
    int m_canvasW = 0, m_canvasH = 0;
    int m_scaledW = 0, m_scaledH = 0;
    int m_offX = 0, m_offY = 0;
};
