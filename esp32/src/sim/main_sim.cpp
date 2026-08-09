// ─── src/sim/main_sim.cpp ─────────────────────────────────────────────────────
// Native macOS entry point for the eye-firmware simulator.
//
// This file provides:
//   • main()                — SDL2 window setup, event loop, frame-rate cap
//   • simDisplayPush()      — called by LGFX_Sprite::pushSprite to blit pixels
//   • simSerialAvailable()  — called by _SerialClass::available()
//   • simSerialRead()       — called by _SerialClass::read()
//
// setup() and loop() are defined in src/main.cpp and called from here.
// Hardware-specific calls inside those functions (SPI, GPIO) become no-ops via
// the stub headers in sim/.
//
// Keyboard shortcuts (window must have focus) — each enqueues the serial command
// shown, so the simulator exercises the same SerialProtocol path as hardware:
//   b / B        BLINK 1 / BLINK 3
//   h            HEARTS
//   m            MONEY
//   d            DEAD
//   t            TRAPEZOID
//   s            SQUINT 0.3 1.5
//   g            PLAYGIF <$REACHY_GIF, default gifs/test.gif>
//   x            STOPGIF
//   i / Space    IDLE
//   arrow keys   GAZE N / S / W / E
//   q / Esc      quit
//
// Build:  pio run -e native_sim
// Run:    .pio/build/native_sim/program

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// ─── Display dimensions (must match SCREEN_W/H from config.h) ────────────────
// Defined here as literals to avoid pulling in Arduino.h before SDL2 headers.
static constexpr int EYE_W = 200;
static constexpr int EYE_H = 200;
static constexpr int EYE_GAP = 10;
static constexpr int SCALE = 2; // 2× magnification for Retina readability

// Logical (pre-scale) window size
static constexpr int LOG_W = EYE_W * 2 + EYE_GAP; // 410
static constexpr int LOG_H = EYE_H;               // 200

// ─── SDL state ────────────────────────────────────────────────────────────────
static SDL_Window *s_window = nullptr;
static SDL_Renderer *s_renderer = nullptr;
static SDL_Texture *s_tex[2] = {nullptr, nullptr};

// ─── simDisplayPush ───────────────────────────────────────────────────────────
// Called by LGFX_Sprite::pushSprite() inside loop().
// eyeIdx 0 = left eye, 1 = right eye.
// The complete frame is presented only after both eyes have been rendered.

void simDisplayPush(const uint16_t *buf, int eyeIdx)
{
    if (eyeIdx < 0 || eyeIdx > 1 || !s_tex[eyeIdx])
        return;

    SDL_UpdateTexture(s_tex[eyeIdx], nullptr, buf,
                      EYE_W * (int)sizeof(uint16_t));

    // Present when the right eye (last) has been rendered.
    if (eyeIdx == 1)
    {
        SDL_SetRenderDrawColor(s_renderer, 20, 20, 20, 255);
        SDL_RenderClear(s_renderer);

        SDL_Rect d0 = {0, 0, EYE_W, EYE_H};
        SDL_Rect d1 = {EYE_W + EYE_GAP, 0, EYE_W, EYE_H};
        SDL_RenderCopy(s_renderer, s_tex[0], nullptr, &d0);
        SDL_RenderCopy(s_renderer, s_tex[1], nullptr, &d1);

        SDL_RenderPresent(s_renderer);
    }
}

// ─── Serial FIFO ──────────────────────────────────────────────────────────────
// Keyboard shortcuts enqueue ASCII command lines here.
// SerialProtocol::poll() drains the FIFO via simSerialAvailable/simSerialRead.

static constexpr int SERIAL_BUF = 512;
static char s_serialBuf[SERIAL_BUF];
static int s_serialHead = 0; // next read index
static int s_serialTail = 0; // next write index

// Enqueue a command string followed by '\n'.
static void simSerialEnqueue(const char *cmd)
{
    for (const char *p = cmd; *p; ++p)
    {
        int next = (s_serialTail + 1) % SERIAL_BUF;
        if (next == s_serialHead)
            return; // full – drop
        s_serialBuf[s_serialTail] = *p;
        s_serialTail = next;
    }
    // append newline
    int next = (s_serialTail + 1) % SERIAL_BUF;
    if (next != s_serialHead)
    {
        s_serialBuf[s_serialTail] = '\n';
        s_serialTail = next;
    }
}

int simSerialAvailable()
{
    return (s_serialTail - s_serialHead + SERIAL_BUF) % SERIAL_BUF;
}

int simSerialRead()
{
    if (s_serialHead == s_serialTail)
        return -1;
    char c = s_serialBuf[s_serialHead];
    s_serialHead = (s_serialHead + 1) % SERIAL_BUF;
    return (unsigned char)c;
}

// ─── Forward declarations for firmware entry points ───────────────────────────
extern void setup();
extern void loop();

// ─── main ─────────────────────────────────────────────────────────────────────
int main(int /*argc*/, char * /*argv*/[])
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    s_window = SDL_CreateWindow(
        "Robot Eyes Simulator  "
        "| b:blink  B:blink3  h:hearts  m:money  d:dead  t:trapezoid  s:squint  i:idle"
        "  g:gif  x:stopgif  arrows:gaze  q:quit",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        LOG_W * SCALE, LOG_H * SCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);

    if (!s_window)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // PRESENTVSYNC caps the animation loop at the display refresh rate (60/120 Hz),
    // matching real hardware behaviour without a manual sleep.
    s_renderer = SDL_CreateRenderer(
        s_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!s_renderer)
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return 1;
    }

    // Map logical (pre-scale) coordinates to the physical window.
    // Each SDL_RenderCopy uses logical pixel units; SDL handles the scaling.
    SDL_RenderSetLogicalSize(s_renderer, LOG_W, LOG_H);

    for (int i = 0; i < 2; i++)
    {
        s_tex[i] = SDL_CreateTexture(s_renderer,
                                     SDL_PIXELFORMAT_RGB565,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     EYE_W, EYE_H);
        if (!s_tex[i])
        {
            fprintf(stderr, "SDL_CreateTexture[%d] failed: %s\n", i, SDL_GetError());
            SDL_Quit();
            return 1;
        }
    }

    // Run firmware initialisation (hardware calls all become no-ops via stubs).
    setup();

    // ── Event + animation loop ─────────────────────────────────────────────────
    static constexpr uint8_t SYNC = 0xAB;
    bool running = true;

    while (running)
    {
        // Process window events and keyboard input.
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT)
            {
                running = false;
                break;
            }

            if (ev.type == SDL_KEYDOWN)
            {
                const SDL_Keycode k = ev.key.keysym.sym;
                const bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;

                if (k == SDLK_ESCAPE || k == SDLK_q)
                {
                    running = false;
                    break;
                }

                // ── Commands map to SerialProtocol command strings ──────────
                if (k == SDLK_b && !shift)
                    simSerialEnqueue("BLINK 1");
                else if (k == SDLK_b && shift)
                    simSerialEnqueue("BLINK 3");
                else if (k == SDLK_h)
                    simSerialEnqueue("HEARTS");
                else if (k == SDLK_m)
                    simSerialEnqueue("MONEY");
                else if (k == SDLK_d)
                    simSerialEnqueue("DEAD");
                else if (k == SDLK_t)
                    simSerialEnqueue("TRAPEZOID");
                else if (k == SDLK_s)
                    simSerialEnqueue("SQUINT 0.3 1.5");
                else if (k == SDLK_g)
                {
                    // Play a local .gif (path from $REACHY_GIF, default the
                    // bundled gifs/test.gif — relative to the working directory,
                    // i.e. run the binary from esp32/).
                    const char *path = getenv("REACHY_GIF");
                    if (!path || !*path)
                        path = "gifs/test.gif";
                    char cmd[400];
                    snprintf(cmd, sizeof(cmd), "PLAYGIF %s", path);
                    simSerialEnqueue(cmd);
                }
                else if (k == SDLK_x)
                    simSerialEnqueue("STOPGIF");
                else if (k == SDLK_i || k == SDLK_SPACE)
                    simSerialEnqueue("IDLE");
                else if (k == SDLK_UP)
                    simSerialEnqueue("GAZE N 0.1 2.0");
                else if (k == SDLK_DOWN)
                    simSerialEnqueue("GAZE S 0.1 2.0");
                else if (k == SDLK_LEFT)
                    simSerialEnqueue("GAZE W 0.1 2.0");
                else if (k == SDLK_RIGHT)
                    simSerialEnqueue("GAZE E 0.1 2.0");
            }
        }

        if (!running)
            break;

        // Run one firmware frame.
        // Frame rate is controlled by SDL_RENDERER_PRESENTVSYNC inside
        // simDisplayPush, so no additional SDL_Delay is needed here.
        loop();
    }

    // ── Cleanup ────────────────────────────────────────────────────────────────
    for (int i = 0; i < 2; i++)
        if (s_tex[i])
            SDL_DestroyTexture(s_tex[i]);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    SDL_Quit();

    return 0;
}
