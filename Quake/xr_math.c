#include "quakedef.h"
#include "xr_math.h"

float IW_XRMath_Dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void IW_XRMath_Cross3(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

qboolean IW_XRMath_Normalize3(float v[3])
{
    float length = sqrtf(IW_XRMath_Dot3(v, v));
    if (length < 0.0001f)
        return false;
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
    return true;
}