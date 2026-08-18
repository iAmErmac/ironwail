#ifndef IRONWAIL_XR_MATH_H
#define IRONWAIL_XR_MATH_H

#include "q_stdinc.h"

float IW_XRMath_Dot3(const float a[3], const float b[3]);
void IW_XRMath_Cross3(const float a[3], const float b[3], float out[3]);
qboolean IW_XRMath_Normalize3(float v[3]);

#endif