#pragma once
#include <Arduino.h>

// Single-axis damped spring.
// Equation: acc = (target - pos) * k - vel * damping
//           vel += acc * dt
//           pos += vel * dt
//
// For critical damping: damping ≈ 2 * sqrt(k)
// Examples (k / damping  →  character):
//   150 / 20  → responsive, slight overshoot
//   250 / 17  → bouncy
//    15 / 10  → sluggish, overdamped
//  2000 / 18  → fast jitter
class SpringFloat
{
public:
    float pos = 0.0f;
    float vel = 0.0f;
    float k = 150.0f;
    float damping = 20.0f;
    float target = 0.0f; // Spring target value

    void setParams(float stiffness, float damp);
    void setTarget(float t);
    void update(float dt); // Update using stored target
    void addImpulse(float impulse);
    void snapTo(float value);
};

// Two coupled SpringFloat instances for 2-D gaze motion.
class Spring2D
{
public:
    SpringFloat x, y;

    void setParams(float stiffness, float damp);
    void setTarget(float tx, float ty);
    void update(float dt); // Update using stored targets
    void addImpulse(float vx, float vy);
    void snapTo(float sx, float sy);
};
