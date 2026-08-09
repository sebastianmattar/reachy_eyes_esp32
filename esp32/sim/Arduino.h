#pragma once
// ─── Arduino.h stub for native macOS simulator ────────────────────────────────
// Provides just the API surface used by the eye-firmware source files.
// Requires C++17 (inline variables, if constexpr).

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <chrono>
#include <string>

// ── Constants ─────────────────────────────────────────────────────────────────
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define PI ((float)M_PI)

// ── min / max / abs / constrain (Arduino-style macros) ───────────────────────
// Use macros so mixed-type calls (e.g. max(uint32_t, unsigned)) compile cleanly.
#undef min
#undef max
#undef abs
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define abs(x) ((x) >= 0 ? (x) : -(x))
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define sq(x) ((x) * (x))

// ── Time ─────────────────────────────────────────────────────────────────────
// Static locals in inline functions are shared across TUs in C++17 (ODR rule).
inline uint32_t millis()
{
    static const auto t0 = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0)
        .count();
}

inline uint32_t micros()
{
    static const auto t0 = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - t0)
        .count();
}

inline void delay(unsigned long) {}

// ── Random ────────────────────────────────────────────────────────────────────
inline long random(long maxVal)
{
    return (maxVal > 0) ? (rand() % maxVal) : 0;
}
inline long random(long minVal, long maxVal)
{
    long span = maxVal - minVal;
    return minVal + ((span > 0) ? (rand() % span) : 0);
}

// ── Pin state (inline array: one shared instance across all TUs) ───────────────
inline uint8_t _pinState[64] = {};

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t pin, uint8_t v)
{
    if (pin < 64)
        _pinState[pin] = v;
}
inline int digitalRead(uint8_t pin) { return (pin < 64) ? _pinState[pin] : HIGH; }
inline void analogWrite(uint8_t, int) {}

// Convenience accessor for display stubs that need to inspect a CS/DC line.
inline int simGetPinState(int pin) { return (pin >= 0 && pin < 64) ? (int)_pinState[pin] : HIGH; }

// ── Serial injection (ring-buffer, defined in src/sim/main_sim.cpp) ────────────
extern int simSerialAvailable();
extern int simSerialRead();

// ── Serial class ──────────────────────────────────────────────────────────────
struct _SerialClass
{
    void begin(int) {}
    void print(const char *s)
    {
        fputs(s, stdout);
        fflush(stdout);
    }
    void println(const char *s)
    {
        puts(s);
        fflush(stdout);
    }
    void println()
    {
        puts("");
        fflush(stdout);
    }
    void printf(const char *fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        ::vprintf(fmt, ap);
        va_end(ap);
        ::fflush(stdout);
    }
    int available() { return simSerialAvailable(); }
    int read() { return simSerialRead(); }
};
inline _SerialClass Serial;

// ── ESP stub ──────────────────────────────────────────────────────────────────
struct _ESPClass
{
    uint32_t getFreeHeap() const { return 0; }
};
inline _ESPClass ESP;

// ── Minimal String stub ───────────────────────────────────────────────────────
struct String
{
    std::string s;
    String() = default;
    String(const char *cs) : s(cs ? cs : "") {}
    String(int v) : s(std::to_string(v)) {}
    String(float v) : s(std::to_string(v)) {}
    const char *c_str() const { return s.c_str(); }
    bool operator==(const String &o) const { return s == o.s; }
};
