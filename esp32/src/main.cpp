#include <Arduino.h>
#include "defs.h"
#ifndef SIMULATOR
#include "lgfx_config.h"
#include <LittleFS.h>
#endif
#include "gif_player.h" // portable now — runs in the simulator too
#include "eye_behavior.h"
#include "eye_motion.h"
#include "eye_renderer.h"
#include "fps_counter.h"
#include "serial_protocol.h"

// ─── Globals ─────────────────────────────────────────────────────────────────

// Two display instances — each owns its CS pin; LovyanGFX manages CS internally.
// Only display0 pulses the shared RST line; display1 uses rst=-1 to avoid
// resetting display0 after it has already been initialised.
static LGFX_Eye display0(TFT1_CS, TFT1_DC, TFT_RST); // left eye  — pulses RST on init
static LGFX_Eye display1(TFT2_CS, TFT2_DC, -1);      // right eye — no RST pulse
// Two sprite buffers so we can render eye 1 while eye 0's DMA transfer runs.
// Each 200×200×16-bit = 80 KB; 160 KB total (fits ESP32 WROOM DRAM without PSRAM).
static LGFX_Sprite sprite0(&display0);
static LGFX_Sprite sprite1(&display1);

static EyesControllerLowLevel lowController;
static EyesControllerHighLevel highController;
static uint32_t lastMicros = 0;
static FpsCounter fpsCounter;
static SerialProtocol serialProtocol(highController);
static GifPlayer gifPlayer;

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    Serial.println("Robot Eyes – starting");

    // Backlight on
    pinMode(TFT_BL, OUTPUT);
    analogWrite(TFT_BL, BACKLIGHT_MAX);

    // Initialise displays — both init() calls first so that display0's RST
    // pulse (inside display0.init()) resets both panels while both are still
    // uninitialised.  display1 has rst=-1, so it will not pulse RST and
    // will not accidentally reset display0 a second time.
    display0.init();
    display1.init();
    display0.setRotation(TFT_1_ROT);
    display1.setRotation(TFT_2_ROT);
    display0.fillScreen(TFT_BLACK);
    display1.fillScreen(TFT_BLACK);

    // Enable GC9A01 internal brightness control and set to maximum.
    // 0x53 WRCTRLD: bit3=BCTRL (enable brightness), bit2=DD (display dimming)
    // 0x51 WRDISBV: display brightness value (0x00–0xFF)
    // seems to get a minor brightness boost
    for (auto *d : {&display0, &display1})
    {
        d->startWrite();
        d->writeCommand(0x53);
        d->writeData(0x2C); // WRCTRLD: enable BCTRL + DD
        d->writeCommand(0x51);
        d->writeData(0xFF); // WRDISBV: max brightness
        d->endWrite();
    }

    // Allocate two frame-buffer sprites (16-bit, 200×200 = 80 KB each, 160 KB total)
    sprite0.setColorDepth(16);
    sprite1.setColorDepth(16);
    Serial.printf("Free heap before sprite alloc: %u bytes\n", ESP.getFreeHeap());
    if (!sprite0.createSprite(SCREEN_W, SCREEN_H) ||
        !sprite1.createSprite(SCREEN_W, SCREEN_H))
    {
        Serial.println("ERROR: sprite RAM allocation failed.");
        while (true)
        {
        }
    }
    Serial.printf("Free heap after sprite alloc:  %u bytes\n", ESP.getFreeHeap());

#ifndef SIMULATOR
    // Mount flash storage for the uploaded gif (format on first boot).
    if (!LittleFS.begin(true))
        Serial.println("WARN: LittleFS mount failed — gif upload/playback disabled.");
#endif
    // The gif decoder writes straight into the two sprite framebuffers.
    // (In the simulator the gif is loaded from a local file instead of LittleFS.)
    gifPlayer.begin(&sprite0, &sprite1);
    serialProtocol.attachGifPlayer(&gifPlayer);

    // Initialise eyes controller stack
    lowController.init();
    highController.init(&lowController);

    lastMicros = micros();
    Serial.println("Ready.");
}

// ─── Loop ────────────────────────────────────────────────────────────────────

void loop()
{
    // Delta time in seconds (clamped to 100 ms max to avoid large jumps)
    uint32_t now = micros();
    float dt = (float)(now - lastMicros) * 1e-6f;
    lastMicros = now;
    if (dt > 0.1f)
        dt = 0.1f;

    serialProtocol.poll();
    highController.update(dt);

    // Advance animation through low-level controller
    lowController.update(dt);

    // ── Render + push with DMA overlap ─────────────────────────────────────
    // Render eye 0 into sprite0, then start its DMA transfer (non-blocking).
    // While the SPI DMA runs, render eye 1 into sprite1 on the CPU.
    // Wait for DMA completion, then push eye 1 synchronously.
    // This hides ~7 ms of SPI idle time behind useful CPU work.

    if (lowController.getEyeRendererKind(0) == EYE_RENDERER_GIF)
    {
        // GIF mode: one decode composites the same frame into both sprites.
        // Only advances the decoder when the current frame's delay has elapsed;
        // otherwise the sprites keep holding the current frame.
        uint32_t t0 = micros();
        gifPlayer.update(dt);
        uint32_t t1 = micros();

        // Push both eyes via the existing DMA-overlap path.
        sprite0.pushSprite(SPRITE_PUSH_X, SPRITE_PUSH_Y);
        display0.waitDisplay();
        uint32_t t2 = micros();
        sprite1.pushSprite(SPRITE_PUSH_X, SPRITE_PUSH_Y);
        uint32_t t4 = micros();

        fpsCounter.record(t1 - t0, t2 - t1, 0, t4 - t2);
        return;
    }

    uint32_t t0 = micros();
    getEyeRenderer(lowController.getEyeRendererKind(0)).render(sprite0, lowController.getAnimationState(0));
    uint32_t t1 = micros();

    // Start DMA push for eye 0 (returns before transfer completes)
    sprite0.pushSprite(SPRITE_PUSH_X, SPRITE_PUSH_Y);

    // Render eye 1 while eye 0 transfers over SPI
    getEyeRenderer(lowController.getEyeRendererKind(1)).render(sprite1, lowController.getAnimationState(1));
    uint32_t t3 = micros();

    // Wait for eye 0 DMA to finish
    display0.waitDisplay();
    uint32_t t2 = micros();

    // Push eye 1 synchronously
    sprite1.pushSprite(SPRITE_PUSH_X, SPRITE_PUSH_Y);
    uint32_t t4 = micros();

    fpsCounter.record(t1 - t0, t2 - t1, t3 - t2, t4 - t3);
}
