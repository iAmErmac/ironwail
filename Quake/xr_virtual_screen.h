#ifndef IRONWAIL_XR_VIRTUAL_SCREEN_H
#define IRONWAIL_XR_VIRTUAL_SCREEN_H

#include "xr_bridge.h"

typedef struct {
    float position[3];
    float orientation[4];
    float width;
    float height;
    float curve_radius;
    qboolean curved;
} iw_xr_virtual_screen_t;

qboolean IW_XRVirtualScreen_Raycast(const iw_xr_virtual_screen_t *screen,
                                    const float origin[3],
                                    const float orientation[4],
                                    iw_xr_virtual_screen_hit_t *hit);

#endif