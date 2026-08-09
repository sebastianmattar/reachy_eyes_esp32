#pragma once
#include "eye_behavior.h"
#ifndef SIMULATOR
#include <FS.h>
#endif

// Line-delimited ASCII protocol over Serial (USB CDC).
//
// Command set:
//   IDLE
//   BLINK <times> [BOTH|LEFT|RIGHT]
//   LOOK <x> <y> [transitionS] [BOTH|LEFT|RIGHT]
//                           → continuous gaze target, x,y in [-1,1]. Holds until
//                             the next LOOK/GAZE/IDLE. Suppresses wandering, keeps
//                             auto-blink. Meant to be streamed (~50Hz) to mirror
//                             robot head pose. Silent: sends no "OK" (only ERR on
//                             a malformed line), to avoid flooding the link.
//   GAZE <dir> <transitionS> <resetS> [BOTH|LEFT|RIGHT]
//   SQUINT <transitionS> <resetS>
//   HEARTS [BOTH|LEFT|RIGHT]
//   MONEY [BOTH|LEFT|RIGHT]
//   DEAD [BOTH|LEFT|RIGHT]
//   TRAPEZOID [BOTH|LEFT|RIGHT]
//   BLINK_INTERVAL <minS> <maxS>
//
//   GIFUPLOAD <byteLen>   → replies "READY", then reads exactly <byteLen> raw
//                           bytes into /eye.gif (LittleFS), then "OK <crc32>"
//                           or "ERR <reason>".  Overwrites the single slot.
//   PLAYGIF [path]        → play the stored gif, looped, on both eyes. On hardware
//                           the path is omitted (the stored slot GIF_PATH is used);
//                           the simulator passes a local .gif path instead.
//   STOPGIF               → stop gif playback, revert to idle.
//   GIFINFO               → "INFO <byteLen> <crc32>" for the stored gif, or "NONE".
//
//   VERSION               → "VERSION <FIRMWARE_VERSION>" (e.g. "VERSION 1.0.0").
//   FWUPDATE <byteLen> [crc32]
//                         → over-the-wire firmware update. Replies "READY", then
//                           reads exactly <byteLen> raw bytes and streams them into
//                           the inactive OTA slot via the ESP32 Update API. On
//                           success replies "OK <crc32>" and reboots into the new
//                           image; on failure "ERR <reason>" and keeps running the
//                           current image. If <crc32> is given it is verified before
//                           committing. No bootloader reset / esptool needed.
//
// Direction tokens: CENTER N NE E SE S SW W NW
//
// Commands respond with "OK\n" or "ERR <reason>\n", with these exceptions:
//   LOOK      — silent on success (only "ERR <reason>" on a malformed line), so a
//               ~50 Hz stream does not flood the link.
//   VERSION   — replies "VERSION <x.y.z>" instead of "OK".
//   GIFINFO   — replies "INFO <byteLen> <crc32>" or "NONE".
//   GIFUPLOAD — replies "READY", then "OK <crc32>" / "ERR <reason>".
//   FWUPDATE  — replies "READY", then "OK <crc32>" (and reboots) / "ERR <reason>".

class GifPlayer; // fwd decl — only touched on hardware builds

class SerialProtocol
{
public:
    explicit SerialProtocol(EyesControllerHighLevel &ctrl) : ctrl(ctrl) {}

    // Wire up the gif player so PLAYGIF/STOPGIF can (re)open/close the stream.
    void attachGifPlayer(GifPlayer *g) { gif = g; }

    // Call once per loop(). Reads available bytes, dispatches complete lines
    // (or streams a binary gif upload while one is in progress).
    void poll();

private:
    EyesControllerHighLevel &ctrl;
    GifPlayer *gif = nullptr;

    // Poller runs in one of two modes: ASCII line reader or raw binary receive.
    enum Mode : uint8_t { MODE_LINE, MODE_BINARY };
    Mode mode = MODE_LINE;

    // A binary stream targets either the gif slot (LittleFS) or the OTA partition.
    enum BinTarget : uint8_t { BIN_GIF, BIN_FW };
    BinTarget binTarget = BIN_GIF;

    static constexpr uint8_t BUF_SIZE = 128;
    char buf[BUF_SIZE] = {};
    uint8_t pos = 0;

    // ── Binary upload state ────────────────────────────────────────────────────
    uint32_t binRemaining  = 0; // bytes still expected
    uint32_t binCrc        = 0; // running CRC-32 (pre-final-XOR)
    uint32_t binLastByteMs = 0; // millis() of last received chunk (for timeout)
    bool     binExpectCrc  = false; // whether an expected CRC was provided (FW only)
    uint32_t binExpectedCrc = 0;     // expected final CRC-32 to verify against
#ifndef SIMULATOR
    fs::File uploadFile;
#endif

    void pollLine();
    void pollBinary();
    void beginUpload(uint32_t byteLen);
    void beginFwUpdate(uint32_t byteLen, bool haveCrc, uint32_t expectedCrc);
    void finishUpload(bool ok, const char *err);

    void dispatch(char *line);

    static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len);
    static EnumEyes parseEyes(const char *s, EnumEyes def = EYES_BOTH);
    static EnumDirection parseDir(const char *s);
};
