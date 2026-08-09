#include "serial_protocol.h"
#include <string.h>
#include "gif_player.h" // portable now — GifPlayer compiles on both targets
#include "config.h"     // FIRMWARE_VERSION
#ifndef SIMULATOR
#include <LittleFS.h>
#include <Update.h> // ESP32 OTA API — streams a new app image into the inactive slot
#endif

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Bitwise CRC-32 (zlib polynomial, reflected). Runs only during upload/info, so
// the table-less form is fine and saves the 1 KB lookup table.
uint32_t SerialProtocol::crc32Update(uint32_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return crc;
}

EnumEyes SerialProtocol::parseEyes(const char *s, EnumEyes def)
{
    if (strcmp(s, "LEFT") == 0)
        return EYES_LEFT_ONLY;
    if (strcmp(s, "RIGHT") == 0)
        return EYES_RIGHT_ONLY;
    if (strcmp(s, "BOTH") == 0)
        return EYES_BOTH;
    return def;
}

EnumDirection SerialProtocol::parseDir(const char *s)
{
    if (strcmp(s, "N") == 0)
        return DIRECTION_N;
    if (strcmp(s, "NE") == 0)
        return DIRECTION_NE;
    if (strcmp(s, "E") == 0)
        return DIRECTION_E;
    if (strcmp(s, "SE") == 0)
        return DIRECTION_SE;
    if (strcmp(s, "S") == 0)
        return DIRECTION_S;
    if (strcmp(s, "SW") == 0)
        return DIRECTION_SW;
    if (strcmp(s, "W") == 0)
        return DIRECTION_W;
    if (strcmp(s, "NW") == 0)
        return DIRECTION_NW;
    return DIRECTION_CENTER;
}

// ─── Poller ──────────────────────────────────────────────────────────────────

void SerialProtocol::poll()
{
    if (mode == MODE_BINARY)
        pollBinary();
    else
        pollLine();
}

void SerialProtocol::pollLine()
{
    while (Serial.available())
    {
        char c = (char)Serial.read();
        if (c == '\r')
            continue; // ignore CR in CRLF line endings
        if (c == '\n')
        {
            buf[pos] = '\0';
            bool hadLine = pos > 0;
            pos = 0;
            if (hadLine)
            {
                dispatch(buf);
                // GIFUPLOAD switches us mid-stream: the bytes already buffered on
                // Serial are the binary payload — hand off immediately.
                if (mode == MODE_BINARY)
                {
                    pollBinary();
                    return;
                }
            }
        }
        else if (pos < BUF_SIZE - 1)
        {
            buf[pos++] = c;
        }
        else
        {
            // Line too long — discard and report error
            buf[pos] = '\0';
            Serial.println("ERR line too long");
            pos = 0;
        }
    }
}

// ─── Binary gif upload ────────────────────────────────────────────────────────

void SerialProtocol::beginUpload(uint32_t byteLen)
{
#ifndef SIMULATOR
    if (byteLen == 0 || byteLen > GIF_MAX_BYTES)
    {
        Serial.println("ERR bad length");
        return;
    }
    // Release any open playback handle before overwriting the slot.
    if (gif)
        gif->close();
    uploadFile = LittleFS.open(GIF_PATH, "w");
    if (!uploadFile)
    {
        Serial.println("ERR open failed");
        return;
    }
    binRemaining  = byteLen;
    binCrc        = 0xFFFFFFFFu;
    binLastByteMs = millis();
    binTarget     = BIN_GIF;
    binExpectCrc  = false;
    mode          = MODE_BINARY;
    Serial.println("READY");
#else
    (void)byteLen;
    Serial.println("ERR no fs");
#endif
}

void SerialProtocol::beginFwUpdate(uint32_t byteLen, bool haveCrc, uint32_t expectedCrc)
{
#ifndef SIMULATOR
    if (byteLen == 0)
    {
        Serial.println("ERR bad length");
        return;
    }
    // Release the gif file handle so LittleFS isn't touched during the flash write.
    if (gif)
        gif->close();
    // Update.begin sizes and erases the inactive OTA slot; fails if the image
    // can't fit (e.g. partition table still single-app).
    if (!Update.begin(byteLen, U_FLASH))
    {
        Serial.print("ERR ");
        Serial.println(Update.errorString());
        return;
    }
    binRemaining   = byteLen;
    binCrc         = 0xFFFFFFFFu;
    binLastByteMs  = millis();
    binTarget      = BIN_FW;
    binExpectCrc   = haveCrc;
    binExpectedCrc = expectedCrc;
    mode           = MODE_BINARY;
    Serial.println("READY");
#else
    (void)byteLen;
    (void)haveCrc;
    (void)expectedCrc;
    Serial.println("ERR no ota");
#endif
}

void SerialProtocol::pollBinary()
{
#ifndef SIMULATOR
    uint8_t chunk[512];
    bool got = false;

    while (binRemaining > 0)
    {
        int avail = Serial.available();
        if (avail <= 0)
            break;
        size_t want = (size_t)avail;
        if (want > sizeof(chunk))
            want = sizeof(chunk);
        if (want > binRemaining)
            want = binRemaining;

        size_t n = Serial.readBytes(chunk, want);
        if (n == 0)
            break;
        got = true;

        size_t written = (binTarget == BIN_FW) ? Update.write(chunk, n)
                                               : uploadFile.write(chunk, n);
        if (written != n)
        {
            finishUpload(false, "write failed");
            return;
        }
        binCrc = crc32Update(binCrc, chunk, n);
        binRemaining -= n;
    }

    if (got)
        binLastByteMs = millis();

    if (binRemaining == 0)
        finishUpload(true, nullptr);
    else if (millis() - binLastByteMs > GIF_UPLOAD_TIMEOUT_MS)
        finishUpload(false, "timeout");
#else
    // No filesystem in the simulator; nothing to receive. Bail back to line mode.
    finishUpload(false, "no fs");
#endif
}

void SerialProtocol::finishUpload(bool ok, const char *err)
{
    mode = MODE_LINE;
    pos  = 0;
#ifndef SIMULATOR
    const uint32_t finalCrc = binCrc ^ 0xFFFFFFFFu;

    if (binTarget == BIN_FW)
    {
        if (!ok)
        {
            Update.abort();
            Serial.print("ERR ");
            Serial.println(err ? err : "fw upload");
            return;
        }
        if (binExpectCrc && finalCrc != binExpectedCrc)
        {
            Update.abort();
            Serial.println("ERR crc mismatch");
            return;
        }
        // end(true): finalize and mark the slot bootable. Also verifies the image.
        if (!Update.end(true))
        {
            Serial.print("ERR ");
            Serial.println(Update.errorString());
            return;
        }
        Serial.printf("OK %08X\n", finalCrc);
        Serial.flush(); // ensure the ack leaves before the port drops on reboot
        delay(100);
        ESP.restart(); // boot into the freshly written OTA slot
        return;
    }

    uploadFile.close();
    if (ok)
    {
        Serial.printf("OK %08X\n", finalCrc);
    }
    else
    {
        LittleFS.remove(GIF_PATH); // don't leave a truncated slot behind
        Serial.print("ERR ");
        Serial.println(err ? err : "upload");
    }
#else
    Serial.print("ERR ");
    Serial.println(err ? err : "no fs");
#endif
}

// ─── Dispatcher ──────────────────────────────────────────────────────────────

void SerialProtocol::dispatch(char *line)
{
    // Split into command token + remainder
    char cmd[32] = {};
    sscanf(line, "%31s", cmd);
    const char *args = line + strlen(cmd);
    while (*args == ' ')
        ++args;

    if (strcmp(cmd, "IDLE") == 0)
    {
        ctrl.idle();
    }
    else if (strcmp(cmd, "BLINK") == 0)
    {
        int times = 1;
        char eyeStr[8] = "BOTH";
        sscanf(args, "%d %7s", &times, eyeStr);
        ctrl.blink(times, parseEyes(eyeStr));
    }
    else if (strcmp(cmd, "LOOK") == 0)
    {
        // Continuous gaze stream (e.g. mirroring robot head yaw/pitch). Returns
        // without an "OK" ack on purpose: at ~50 Hz an ack per frame would flood
        // the link and grow the host's RX buffer. Malformed lines still get ERR.
        float x = 0.0f, y = 0.0f, trans = 0.15f;
        char eyeStr[8] = "BOTH";
        int n = sscanf(args, "%f %f %f %7s", &x, &y, &trans, eyeStr);
        if (n < 2)
        {
            Serial.println("ERR look needs x y");
            return;
        }
        ctrl.look(x, y, trans, parseEyes(eyeStr));
        return; // silent — no OK
    }
    else if (strcmp(cmd, "GAZE") == 0)
    {
        char dir[8] = "CENTER";
        char eyeStr[8] = "BOTH";
        float trans = 0.3f, reset = 2.0f;
        sscanf(args, "%7s %f %f %7s", dir, &trans, &reset, eyeStr);
        ctrl.gaze(parseDir(dir), trans, reset, parseEyes(eyeStr));
    }
    else if (strcmp(cmd, "SQUINT") == 0)
    {
        float trans = 0.3f, reset = 2.0f;
        sscanf(args, "%f %f", &trans, &reset);
        ctrl.squint(trans, reset);
    }
    else if (strcmp(cmd, "HEARTS") == 0)
    {
        char eyeStr[8] = "BOTH";
        sscanf(args, "%7s", eyeStr);
        ctrl.hearts(parseEyes(eyeStr));
    }
    else if (strcmp(cmd, "MONEY") == 0)
    {
        char eyeStr[8] = "BOTH";
        sscanf(args, "%7s", eyeStr);
        ctrl.money(parseEyes(eyeStr));
    }
    else if (strcmp(cmd, "DEAD") == 0)
    {
        char eyeStr[8] = "BOTH";
        sscanf(args, "%7s", eyeStr);
        ctrl.dead(parseEyes(eyeStr));
    }
    else if (strcmp(cmd, "TRAPEZOID") == 0)
    {
        char eyeStr[8] = "BOTH";
        sscanf(args, "%7s", eyeStr);
        ctrl.trapezoid(parseEyes(eyeStr));
    }
    else if (strcmp(cmd, "BLINK_INTERVAL") == 0)
    {
        float mn = BLINK_INTERVAL_DEFAULT_MIN, mx = BLINK_INTERVAL_DEFAULT_MAX;
        sscanf(args, "%f %f", &mn, &mx);
        ctrl.setBlinkInterval(mn, mx);
    }
    else if (strcmp(cmd, "VERSION") == 0)
    {
        Serial.print("VERSION ");
        Serial.println(FIRMWARE_VERSION);
        return;
    }
    else if (strcmp(cmd, "GIFUPLOAD") == 0)
    {
        unsigned long len = 0;
        sscanf(args, "%lu", &len);
        beginUpload((uint32_t)len); // sends its own READY / ERR reply
        return;
    }
    else if (strcmp(cmd, "FWUPDATE") == 0)
    {
        unsigned long len = 0;
        unsigned long crc = 0;
        int n = sscanf(args, "%lu %lx", &len, &crc);
        // sends its own READY / ERR reply; on success switches to binary mode
        beginFwUpdate((uint32_t)len, n >= 2, (uint32_t)crc);
        return;
    }
    else if (strcmp(cmd, "PLAYGIF") == 0)
    {
        // Optional path arg: hardware defaults to the stored slot (GIF_PATH); the
        // simulator passes a local .gif path (`PLAYGIF /abs/path.gif`).
        const char *path = (*args) ? args : GIF_PATH;
        if (!gif || !gif->open(path))
        {
            Serial.println("ERR no gif");
            return;
        }
        ctrl.playGif(EYES_BOTH);
    }
    else if (strcmp(cmd, "STOPGIF") == 0)
    {
        if (gif)
            gif->close();
        ctrl.idle();
    }
    else if (strcmp(cmd, "GIFINFO") == 0)
    {
#ifndef SIMULATOR
        fs::File f = LittleFS.open(GIF_PATH, "r");
        if (!f)
        {
            Serial.println("NONE");
            return;
        }
        uint32_t crc = 0xFFFFFFFFu;
        uint32_t total = 0;
        uint8_t chunk[512];
        size_t n;
        while ((n = f.read(chunk, sizeof(chunk))) > 0)
        {
            crc = crc32Update(crc, chunk, n);
            total += (uint32_t)n;
        }
        f.close();
        Serial.printf("INFO %u %08X\n", (unsigned)total, crc ^ 0xFFFFFFFFu);
#else
        Serial.println("NONE");
#endif
        return;
    }
    else
    {
        Serial.print("ERR unknown: ");
        Serial.println(cmd);
        return;
    }

    Serial.println("OK");
}
