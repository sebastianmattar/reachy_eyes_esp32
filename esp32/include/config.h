#pragma once

// ── Hardware ─────────────────────────────────────────────────────────────────
#define NUM_EYES 2
#define TFT1_CS 5  // Chip-select for display 0 (left eye)
#define TFT2_CS 17 // Chip-select for display 1 (right eye)
#define TFT1_DC 16
#define TFT2_DC 25
#define TFT_SDA 23
#define TFT_CLK 18
#define TFT_RST 26        // Shared reset pin (both displays)
#define TFT_BL 22         // Backlight PWM pin
#define TFT_1_ROT 2       // Display 0 rotation (0–3)
#define TFT_2_ROT 2       // Display 1 rotation (0–3)
#define BACKLIGHT_MAX 255 // PWM backlight level (0–255)

// ── Display ──────────────────────────────────────────────────────────────────
// GC9A01 native res is 240×240.
// Sprite is 200×200 (80 KB) – fits ESP32 heap reliably without PSRAM.
// It is pushed centered at (SPRITE_PUSH_X, SPRITE_PUSH_Y) on the 240×240 display;
// the 20 px corners are clipped by the circular GC9A01 bezel anyway.
#define SCREEN_W 200
#define SCREEN_H 200
#define SPRITE_PUSH_X 20 // pixel offset when pushing sprite onto 240×240 display
#define SPRITE_PUSH_Y 20

#define CX (SCREEN_W / 2)
#define CY (SCREEN_H / 2)

// Physical display circle: radius 120 px on the 240×240 panel.
// In sprite coordinates the circle is centred at (CX, CY) with the same radius
// because SPRITE_PUSH_X == SPRITE_PUSH_Y == 120 - SCREEN_W/2.
#define DISPLAY_RADIUS 120

// ── Dot-grid eye geometry ─────────────────────────────────────────────────────
#define GRID_SPACING 12 // Pixels between dot centres
#define DOT_RADIUS 5    // Radius of each lit dot (px)
#define GRID_COLS (SCREEN_W / GRID_SPACING)
#define GRID_ROWS (SCREEN_H / GRID_SPACING)

// Pupil bounding half-extents at scale=1.0, aspect=1.0
#define PUPIL_BASE_W (SCREEN_W * 0.60f) // ~120 px on 200px sprite
#define PUPIL_BASE_H (SCREEN_H * 0.45f) // ~90 px

// ── Blink timing (seconds) ───────────────────────────────────────────────────
#define BLINK_CLOSE_S 0.045f
#define BLINK_HOLD_S 0.005f
#define BLINK_OPEN_S 0.065f

// ── Idle wander ──────────────────────────────────────────────────────────────
#define WANDER_EXTENT_X 0.70f // Max gaze travel on X (−1..+1 = full iris travel)
#define WANDER_EXTENT_Y 0.55f
#define WANDER_DWELL_MIN 0.5f // Seconds at a position before next move
#define WANDER_DWELL_MAX 3.0f

// ── Autoblink fallback interval (used by shared countdown in main.cpp) ────────
#define BLINK_INTERVAL_DEFAULT_MIN 3.0f // Seconds
#define BLINK_INTERVAL_DEFAULT_MAX 6.0f

// ── Dart spring (used by both gaze demo and idle wander) ─────────────────────
// Underdamped dart spring used for directed gaze and idle wander.
// Critical damping = 2*sqrt(k); e.g. for k=700 that is ≈53.
#define DART_SPRING_K 700.0f      // Stiffness during dart
#define DART_SPRING_DAMPING 12.0f // Underdamped — gives the snap

// ── Hearts animation ─────────────────────────────────────────────────────────
#define HEARTS_DURATION_S 2.5f // Seconds before returning to default eye mode

// ── Firmware version ──────────────────────────────────────────────────────────
// Reported by the serial `VERSION` command; the host OTA tool compares it against
// the version baked into a firmware.bin to decide whether to push an update.
// Bump this on every release you intend to auto-deploy.
#define FIRMWARE_VERSION "1.0.0"

// ── Animated GIF playback ─────────────────────────────────────────────────────
#define GIF_PATH "/eye.gif"               // Single-slot stored GIF (LittleFS)
#define GIF_MAX_BYTES (1024u * 1024u)     // Upload size cap (spiffs partition ~1.3 MB)
#define GIF_UPLOAD_TIMEOUT_MS 3000        // Abort upload after this idle gap (ms)
