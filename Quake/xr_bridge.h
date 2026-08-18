#ifndef IRONWAIL_XR_BRIDGE_H
#define IRONWAIL_XR_BRIDGE_H

#include "q_stdinc.h"

#include <stddef.h>
#include <stdint.h>

typedef struct iw_xr_bridge_s iw_xr_bridge_t;

typedef enum {
    IW_XR_DISABLED = 0,
    IW_XR_UNAVAILABLE,
    IW_XR_INITIALIZING,
    IW_XR_ACTIVE,
    IW_XR_FAILED
} iw_xr_state_t;

typedef enum {
    IW_XR_RESULT_OK = 0,
    IW_XR_RESULT_UNAVAILABLE = 1,
    IW_XR_RESULT_TIMEOUT = 2,
    IW_XR_RESULT_INVALID = 3,
    IW_XR_RESULT_FAILED = 4
} iw_xr_result_t;

typedef enum {
    IW_XR_HAND_LEFT = 0,
    IW_XR_HAND_RIGHT = 1,
    IW_XR_HAND_COUNT = 2
} iw_xr_hand_t;

typedef struct {
    float left;
    float right;
    float up;
    float down;
} iw_xr_fov_t;

typedef struct {
    float position[3];
    float orientation[4];
    iw_xr_fov_t fov;
} iw_xr_view_t;

typedef struct {
    uint64_t predicted_display_time;
    uint64_t predicted_display_period;
    unsigned view_count;
    iw_xr_view_t views[2];
    qboolean should_render;
} iw_xr_frame_snapshot_t;

typedef struct {
    qboolean active;
    qboolean aim_valid;
    qboolean grip_valid;
    float aim_position[3];
    float aim_orientation[4];
    float grip_position[3];
    float grip_orientation[4];
    float trigger;
    float grip;
    float stick[2];
    unsigned buttons;
} iw_xr_hand_snapshot_t;

typedef struct {
    qboolean active;
    iw_xr_hand_snapshot_t hand[IW_XR_HAND_COUNT];
} iw_xr_action_snapshot_t;

typedef struct {
    float position[3];
    float orientation[4];
    qboolean valid;
} iw_xr_listener_pose_t;

typedef struct {
    float position[3];
    float normal[3];
    float u;
    float v;
    qboolean valid;
    qboolean inside;
} iw_xr_virtual_screen_hit_t;

typedef struct {
    uint64_t (*monotonic_time_ns)(void *userdata);
    void (*log)(void *userdata, const char *message);
    void *userdata;
    unsigned probe_budget_ms;
    qboolean disabled;
} iw_xr_bridge_config_t;

typedef iw_xr_result_t (*iw_xr_probe_fn)(iw_xr_bridge_t *bridge,
                                         uint64_t deadline_ns,
                                         const char **reason);

iw_xr_bridge_t *IW_XRBridge_Create(const iw_xr_bridge_config_t *config);
void IW_XRBridge_Destroy(iw_xr_bridge_t *bridge);

iw_xr_result_t IW_XRBridge_Initialize(iw_xr_bridge_t *bridge,
                                       iw_xr_probe_fn probe);
iw_xr_result_t IW_XRBridge_Pump(iw_xr_bridge_t *bridge);
void IW_XRBridge_Pause(iw_xr_bridge_t *bridge);
void IW_XRBridge_Resume(iw_xr_bridge_t *bridge);
void IW_XRBridge_Shutdown(iw_xr_bridge_t *bridge);

void IW_XRBridge_SetDisabled(iw_xr_bridge_t *bridge, qboolean disabled);
iw_xr_result_t IW_XRBridge_RequestRetry(iw_xr_bridge_t *bridge);

iw_xr_state_t IW_XRBridge_State(const iw_xr_bridge_t *bridge);
const char *IW_XRBridge_FailureReason(const iw_xr_bridge_t *bridge);
uint64_t IW_XRBridge_ProbeDurationNs(const iw_xr_bridge_t *bridge);
qboolean IW_XRBridge_OwnsPresentation(const iw_xr_bridge_t *bridge);
qboolean IW_XRBridge_OwnsInput(const iw_xr_bridge_t *bridge);

qboolean IW_XRBridge_GetFrame(const iw_xr_bridge_t *bridge,
                              iw_xr_frame_snapshot_t *snapshot);
qboolean IW_XRBridge_GetActions(const iw_xr_bridge_t *bridge,
                                iw_xr_action_snapshot_t *actions);
qboolean IW_XRBridge_GetListenerPose(const iw_xr_bridge_t *bridge,
                                     iw_xr_listener_pose_t *pose);

/* Target operations remain opaque so desktop GL and Android GLES can supply
 * backend-owned framebuffers without leaking handles into engine code. */
qboolean IW_XRBridge_AcquireTarget(iw_xr_bridge_t *bridge, unsigned view,
                                   void **target);
qboolean IW_XRBridge_BindTarget(iw_xr_bridge_t *bridge, void *target);
void IW_XRBridge_ReleaseTarget(iw_xr_bridge_t *bridge, void *target);

void IW_XRBridge_Log(iw_xr_bridge_t *bridge, const char *message);
void IW_XRBridge_SetFailure(iw_xr_bridge_t *bridge, iw_xr_result_t result,
                            const char *reason);

#endif
