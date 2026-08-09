#include "eye_renderer.h"
#include "eye_styles/normal_eye_renderer.h"
#include "eye_styles/billboard_eye_renderer.h"
#include "eye_styles/trapezoid_eye_renderer.h"

const IEyeRenderer &getEyeRenderer(EyeRendererKind kind)
{
    static NormalEyeRenderer    normalRenderer;
    static BillboardEyeRenderer billboardRenderer;
    static TrapezoidEyeRenderer trapezoidRenderer;

    switch (kind)
    {
    case EYE_RENDERER_TRAPEZOID: return trapezoidRenderer;
    case EYE_RENDERER_BILLBOARD: return billboardRenderer;
    case EYE_RENDERER_NORMAL:
    default:                     return normalRenderer;
    }
}
