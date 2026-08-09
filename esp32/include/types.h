#pragma once
#include <Arduino.h>

// Public API eye selection.
enum EnumEyes : uint8_t
{
    EYES_BOTH,
    EYES_LEFT_ONLY,
    EYES_RIGHT_ONLY
};

// Public API high-level gaze directions.
enum EnumDirection : uint8_t
{
    DIRECTION_CENTER,
    DIRECTION_N,
    DIRECTION_NE,
    DIRECTION_E,
    DIRECTION_SE,
    DIRECTION_S,
    DIRECTION_SW,
    DIRECTION_W,
    DIRECTION_NW
};

// Public API eye modes.
enum EnumEyeMode : uint8_t
{
    EYEMODE_DEFAULT,
    EYEMODE_TRAPEZOID,
    EYEMODE_SPIRAL,
    EYEMODE_EFFECT_DIZZY,
    EYEMODE_BILLBOARD_HEART,
    EYEMODE_BILLBOARD_MONEY,
    EYEMODE_BILLBOARD_DEAD,
    EYEMODE_GIF,
};

enum EyeRendererKind : uint8_t
{
    EYE_RENDERER_NORMAL,
    EYE_RENDERER_TRAPEZOID,
    EYE_RENDERER_BILLBOARD,
    EYE_RENDERER_GIF,
};

struct VersionInfo
{
    String Name;
    int major;
    int minor;
    int patch;
};