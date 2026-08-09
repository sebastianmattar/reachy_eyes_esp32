#include "gif_player.h"

#include <string.h>
#ifndef SIMULATOR
#include <LittleFS.h>
#else
#include <cstdio>
#endif

GifPlayer *GifPlayer::s_self = nullptr;

// The single streamed file handle backing the AnimatedGIF read/seek callbacks.
// One slot only, so a file-scope handle is sufficient.  On hardware it is a
// LittleFS File; in the simulator it is a stdio FILE* to a local .gif.
#ifndef SIMULATOR
static fs::File s_gifFile;
#else
static FILE *s_gifFile = nullptr;
#endif

// ─── Setup ────────────────────────────────────────────────────────────────────

void GifPlayer::begin(LGFX_Sprite *s0, LGFX_Sprite *s1)
{
    m_spr0 = s0;
    m_spr1 = s1;
    m_buf0 = (uint16_t *)s0->getBuffer();
    m_buf1 = (uint16_t *)s1->getBuffer();
}

void GifPlayer::clearSprites()
{
    if (m_buf0)
        memset(m_buf0, 0, (size_t)SCREEN_W * SCREEN_H * sizeof(uint16_t));
    if (m_buf1)
        memset(m_buf1, 0, (size_t)SCREEN_W * SCREEN_H * sizeof(uint16_t));
}

// Erase a canvas-space rect in both sprites, mapped through the same
// nearest-neighbor letterbox transform used when drawing, so the erased area
// lines up exactly with the pixels the frame painted.
void GifPlayer::clearCanvasRect(int cx, int cy, int cw, int ch)
{
    if (!m_buf0 || !m_buf1 || cw <= 0 || ch <= 0 || m_canvasW <= 0 || m_canvasH <= 0)
        return;

    int dxStart = (cx * m_scaledW + m_canvasW - 1) / m_canvasW;              // ceil
    int dxEnd   = (((cx + cw) * m_scaledW + m_canvasW - 1) / m_canvasW) - 1; // ceil-1
    int dyStart = (cy * m_scaledH + m_canvasH - 1) / m_canvasH;
    int dyEnd   = (((cy + ch) * m_scaledH + m_canvasH - 1) / m_canvasH) - 1;
    if (dxStart < 0) dxStart = 0;
    if (dyStart < 0) dyStart = 0;
    if (dxEnd > m_scaledW - 1) dxEnd = m_scaledW - 1;
    if (dyEnd > m_scaledH - 1) dyEnd = m_scaledH - 1;

    for (int dy = dyStart; dy <= dyEnd; dy++)
    {
        const int rowBase = (m_offY + dy) * SCREEN_W + m_offX;
        for (int dx = dxStart; dx <= dxEnd; dx++)
        {
            m_buf0[rowBase + dx] = 0;
            m_buf1[rowBase + dx] = 0;
        }
    }
}

// ─── Open / close ─────────────────────────────────────────────────────────────

bool GifPlayer::open(const char *path)
{
    close();

    // Palette endianness must match how each target's sprite buffer is stored:
    //  • Hardware: LovyanGFX sprites hold swap565 (big-endian) → BE palette.
    //  • Simulator: the SDL sprite buffer is native little-endian RGB565 → LE.
    // We write palette entries straight into the sprite buffer, so this must match
    // or colors come out byte-swapped.
#ifdef SIMULATOR
    m_gif.begin(GIF_PALETTE_RGB565_LE);
#else
    m_gif.begin(GIF_PALETTE_RGB565_BE);
#endif
    s_self = this;

    if (!m_gif.open(path, fileOpen, fileClose, fileRead, fileSeek, drawCallback))
        return false;

    m_canvasW = m_gif.getCanvasWidth();
    m_canvasH = m_gif.getCanvasHeight();
    if (m_canvasW <= 0 || m_canvasH <= 0)
    {
        m_gif.close();
        return false;
    }

    // Aspect-ratio preserving fit (letterbox): pick the smaller axis ratio so
    // the whole image fits inside the 200×200 sprite, then center it.
    float sx    = (float)SCREEN_W / (float)m_canvasW;
    float sy    = (float)SCREEN_H / (float)m_canvasH;
    float scale = sx < sy ? sx : sy;

    m_scaledW = (int)(m_canvasW * scale + 0.5f);
    m_scaledH = (int)(m_canvasH * scale + 0.5f);
    if (m_scaledW < 1) m_scaledW = 1;
    if (m_scaledH < 1) m_scaledH = 1;
    if (m_scaledW > SCREEN_W) m_scaledW = SCREEN_W;
    if (m_scaledH > SCREEN_H) m_scaledH = SCREEN_H;

    m_offX = (SCREEN_W - m_scaledW) / 2;
    m_offY = (SCREEN_H - m_scaledH) / 2;

    m_frameTimer  = 0.0f; // first update() draws frame 0 immediately
    m_clearNext   = true;
    m_disposeNext = false;
    m_open        = true;
    return true;
}

void GifPlayer::close()
{
    if (m_open)
    {
        m_gif.close(); // invokes fileClose → closes s_gifFile
        m_open = false;
    }
    if (s_self == this)
        s_self = nullptr;
}

// ─── Playback ─────────────────────────────────────────────────────────────────

void GifPlayer::update(float dt)
{
    if (!m_open)
        return;
    m_frameTimer -= dt;
    if (m_frameTimer <= 0.0f)
        drawNextFrame();
}

void GifPlayer::drawNextFrame()
{
    if (m_clearNext)
    {
        clearSprites(); // keep letterbox margins clean at the start of each cycle
        m_clearNext   = false;
        m_disposeNext = false; // a full clear supersedes any pending disposal
    }
    else if (m_disposeNext)
    {
        // Previous frame asked for "restore to background": erase its rect so this
        // frame composites onto background instead of stacking on the old pixels.
        clearCanvasRect(m_dispX, m_dispY, m_dispW, m_dispH);
        m_disposeNext = false;
    }

    int delayMs = 0;
    s_self = this; // ensure callbacks reach us even if another player opened later
    int rc = m_gif.playFrame(false, &delayMs);

    if (delayMs <= 0)
        delayMs = 100; // sane default for gifs with a 0 delay (≈10 fps)
    m_frameTimer += (float)delayMs / 1000.0f;

    if (rc == 0)
    {
        // Reached the last frame — loop back to the start next time.
        m_gif.reset();
        m_clearNext = true;
    }
}

// ─── GIFDRAW callback: nearest-neighbor scale one source line into both eyes ───

void GifPlayer::drawCallback(GIFDRAW *pDraw)
{
    GifPlayer *self = s_self;
    if (!self || !self->m_buf0 || !self->m_buf1)
        return;

    // Remember this frame's disposal + rect; applied before the NEXT frame draws.
    // (Constant across the lines of a frame, so re-storing per line is harmless.)
    self->m_disposeNext = (pDraw->ucDisposalMethod == 2);
    self->m_dispX       = pDraw->iX;
    self->m_dispY       = pDraw->iY;
    self->m_dispW       = pDraw->iWidth;
    self->m_dispH       = pDraw->iHeight;

    const int canvasW = self->m_canvasW;
    const int canvasH = self->m_canvasH;
    const int scaledW = self->m_scaledW;
    const int scaledH = self->m_scaledH;
    const int offX    = self->m_offX;
    const int offY    = self->m_offY;

    // Absolute source row within the logical canvas for this scanline.
    const int sy = pDraw->iY + pDraw->y;

    // Destination rows whose nearest source row (floor(dy*canvasH/scaledH)) == sy.
    int dyStart = (sy * scaledH + canvasH - 1) / canvasH;            // ceil
    int dyEnd   = ((sy + 1) * scaledH + canvasH - 1) / canvasH - 1;  // ceil-1
    if (dyStart < 0) dyStart = 0;
    if (dyEnd > scaledH - 1) dyEnd = scaledH - 1;
    if (dyStart > dyEnd)
        return;

    const uint8_t  *pixels  = pDraw->pPixels;
    const uint16_t *palette = pDraw->pPalette;
    const int  lineX0   = pDraw->iX;      // this line only covers [iX, iX+iWidth)
    const int  lineW    = pDraw->iWidth;  // (partial-frame gifs update sub-rects)
    const bool hasTrans = pDraw->ucHasTransparency;
    const uint8_t transIdx = pDraw->ucTransparent;

    // Destination columns that map back into this (possibly partial) source line.
    int dxStart = (lineX0 * scaledW + canvasW - 1) / canvasW;              // ceil
    int dxEnd   = ((lineX0 + lineW) * scaledW + canvasW - 1) / canvasW - 1; // ceil-1
    if (dxStart < 0) dxStart = 0;
    if (dxEnd > scaledW - 1) dxEnd = scaledW - 1;

    uint16_t *buf0 = self->m_buf0;
    uint16_t *buf1 = self->m_buf1;

    for (int dy = dyStart; dy <= dyEnd; dy++)
    {
        const int rowBase = (offY + dy) * SCREEN_W + offX;
        for (int dx = dxStart; dx <= dxEnd; dx++)
        {
            const int sx = (dx * canvasW) / scaledW; // nearest source column
            const int li = sx - lineX0;
            if (li < 0 || li >= lineW)
                continue;
            const uint8_t idx = pixels[li];
            if (hasTrans && idx == transIdx)
                continue; // transparent: leave whatever was composited before
            const uint16_t color = palette[idx];
            const int di = rowBase + dx;
            buf0[di] = color;
            buf1[di] = color;
        }
    }
}

// ─── LittleFS-backed AnimatedGIF file callbacks ───────────────────────────────

void *GifPlayer::fileOpen(const char *fname, int32_t *pSize)
{
#ifndef SIMULATOR
    s_gifFile = LittleFS.open(fname, "r");
    if (!s_gifFile)
        return nullptr;
    *pSize = (int32_t)s_gifFile.size();
    return (void *)&s_gifFile; // handle is the address of the File object
#else
    s_gifFile = fopen(fname, "rb");
    if (!s_gifFile)
        return nullptr;
    fseek(s_gifFile, 0, SEEK_END);
    *pSize = (int32_t)ftell(s_gifFile);
    fseek(s_gifFile, 0, SEEK_SET);
    return (void *)s_gifFile; // handle is the FILE* itself
#endif
}

void GifPlayer::fileClose(void *pHandle)
{
#ifndef SIMULATOR
    fs::File *f = (fs::File *)pHandle;
    if (f)
        f->close();
#else
    FILE *f = (FILE *)pHandle;
    if (f)
        fclose(f);
    s_gifFile = nullptr;
#endif
}

int32_t GifPlayer::fileRead(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    int32_t remaining = pFile->iSize - pFile->iPos;
    if (iLen > remaining)
        iLen = remaining;
    if (iLen <= 0)
        return 0;
#ifndef SIMULATOR
    fs::File *f = (fs::File *)pFile->fHandle;
    int32_t n = (int32_t)f->read(pBuf, iLen);
    pFile->iPos = (int32_t)f->position();
    return n;
#else
    FILE *f = (FILE *)pFile->fHandle;
    int32_t n = (int32_t)fread(pBuf, 1, (size_t)iLen, f);
    pFile->iPos = (int32_t)ftell(f);
    return n;
#endif
}

int32_t GifPlayer::fileSeek(GIFFILE *pFile, int32_t iPosition)
{
#ifndef SIMULATOR
    fs::File *f = (fs::File *)pFile->fHandle;
    f->seek(iPosition);
    pFile->iPos = (int32_t)f->position();
#else
    FILE *f = (FILE *)pFile->fHandle;
    fseek(f, iPosition, SEEK_SET);
    pFile->iPos = (int32_t)ftell(f);
#endif
    return pFile->iPos;
}
