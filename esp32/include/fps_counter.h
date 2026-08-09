#pragma once
#include <Arduino.h>

// Accumulates per-phase render timings and prints a summary once per second.
//
// Usage:
//   FpsCounter fps;
//   fps.record(r0_us, p0_us, r1_us, p1_us);  // call every frame
struct FpsCounter
{
    void record(uint32_t r0, uint32_t p0, uint32_t r1, uint32_t p1)
    {
        accR0 += r0;
        accP0 += p0;
        accR1 += r1;
        accP1 += p1;
        ++frames;

        uint32_t msNow = millis();
        if (msNow - lastMs >= 1000)
        {
            uint32_t f = max(frames, 1u);
            Serial.printf("FPS:%u  r0:%uus p0:%uus  r1:%uus p1:%uus  total:%uus\n",
                          frames,
                          accR0 / f, accP0 / f,
                          accR1 / f, accP1 / f,
                          (accR0 + accP0 + accR1 + accP1) / f);
            frames = 0;
            lastMs = msNow;
            accR0 = accP0 = accR1 = accP1 = 0;
        }
    }

private:
    uint32_t frames = 0;
    uint32_t lastMs = 0;
    uint32_t accR0 = 0, accP0 = 0, accR1 = 0, accP1 = 0;
};
