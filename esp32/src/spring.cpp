#include "spring.h"

// ─── SpringFloat ──────────────────────────────────────────────────────────────

void SpringFloat::setParams(float stiffness, float damp)
{
    k = stiffness;
    damping = damp;
}

void SpringFloat::setTarget(float t)
{
    target = t;
}

void SpringFloat::update(float dt)
{
    if (dt <= 0.0f)
        return;

    // Integrate in small fixed steps so behavior remains stable if frame time spikes.
    float remaining = dt;
    const float maxStep = 1.0f / 240.0f;
    while (remaining > 0.0f)
    {
        float h = remaining > maxStep ? maxStep : remaining;
        float acc = (target - pos) * k - vel * damping;
        vel += acc * h;
        pos += vel * h;
        remaining -= h;
    }
}

void SpringFloat::addImpulse(float impulse)
{
    vel += impulse;
}

void SpringFloat::snapTo(float value)
{
    pos = value;
    vel = 0.0f;
}

// ─── Spring2D ─────────────────────────────────────────────────────────────────

void Spring2D::setParams(float stiffness, float damp)
{
    x.setParams(stiffness, damp);
    y.setParams(stiffness, damp);
}

void Spring2D::setTarget(float tx, float ty)
{
    x.setTarget(tx);
    y.setTarget(ty);
}

void Spring2D::update(float dt)
{
    x.update(dt);
    y.update(dt);
}

void Spring2D::addImpulse(float vx, float vy)
{
    x.addImpulse(vx);
    y.addImpulse(vy);
}

void Spring2D::snapTo(float sx, float sy)
{
    x.snapTo(sx);
    y.snapTo(sy);
}
