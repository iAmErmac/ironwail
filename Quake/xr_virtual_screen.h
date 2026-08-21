#ifndef IRONWAIL_XR_VIRTUAL_SCREEN_H
#define IRONWAIL_XR_VIRTUAL_SCREEN_H

#include "xr_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float position[3];
    float orientation[4];
    float width;
    float height;
    float curve_radius;
    qboolean curved;
} iw_xr_virtual_screen_t;

typedef struct {
    float position[3];
    float orientation[4];
} iw_xr_virtual_screen_pose_t;

typedef struct {
    iw_xr_virtual_screen_pose_t current;
    iw_xr_virtual_screen_pose_t target;
    double ready_time_s;
    double last_step_s;
    qboolean valid;
    qboolean targeting;
} iw_xr_virtual_screen_follow_t;

typedef struct {
    float position[3];
    float orientation[4];
} iw_xr_virtual_screen_view_t;

qboolean IW_XRVirtualScreen_UpdatePose(iw_xr_virtual_screen_follow_t *state,
                                        const iw_xr_virtual_screen_view_t *views,
                                        unsigned view_count,
                                        double now_s,
                                        float distance,
                                        qboolean follow,
                                        iw_xr_virtual_screen_pose_t *pose);
qboolean IW_XRVirtualScreen_Raycast(const iw_xr_virtual_screen_t *screen,
                                    const float origin[3],
                                    const float orientation[4],
                                    iw_xr_virtual_screen_hit_t *hit);

#ifdef __cplusplus
}
#endif

#endif