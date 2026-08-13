#include "xr_bridge.h"

#include <stdlib.h>
#include <string.h>

struct iw_xr_bridge_s {
    iw_xr_bridge_config_t config;
    iw_xr_state_t state;
    iw_xr_result_t result;
    char reason[192];
    uint64_t probe_start_ns;
    uint64_t probe_duration_ns;
    qboolean paused;
    iw_xr_probe_fn probe;
};

static uint64_t bridge_now_ns(const iw_xr_bridge_t *bridge)
{
    if (bridge->config.monotonic_time_ns)
        return bridge->config.monotonic_time_ns(bridge->config.userdata);
    return 0;
}

static void bridge_copy_reason(iw_xr_bridge_t *bridge, const char *reason)
{
    if (!reason)
        reason = "OpenXR backend unavailable";
    strncpy(bridge->reason, reason, sizeof(bridge->reason) - 1);
    bridge->reason[sizeof(bridge->reason) - 1] = '\0';
}

void IW_XRBridge_Log(iw_xr_bridge_t *bridge, const char *message)
{
    if (bridge && bridge->config.log && message)
        bridge->config.log(bridge->config.userdata, message);
}

iw_xr_bridge_t *IW_XRBridge_Create(const iw_xr_bridge_config_t *config)
{
    iw_xr_bridge_t *bridge = (iw_xr_bridge_t *)calloc(1, sizeof(*bridge));
    if (!bridge)
        return NULL;
    if (config)
        bridge->config = *config;
    if (!bridge->config.probe_budget_ms)
        bridge->config.probe_budget_ms = 250;
    bridge->state = bridge->config.disabled ? IW_XR_DISABLED : IW_XR_UNAVAILABLE;
    bridge->result = IW_XR_RESULT_UNAVAILABLE;
    bridge_copy_reason(bridge, "OpenXR backend not enabled");
    return bridge;
}

void IW_XRBridge_Destroy(iw_xr_bridge_t *bridge)
{
    if (bridge)
        free(bridge);
}

iw_xr_result_t IW_XRBridge_Initialize(iw_xr_bridge_t *bridge,
                                       iw_xr_probe_fn probe)
{
    uint64_t deadline;
    const char *reason = NULL;
    iw_xr_result_t result;
    if (!bridge)
        return IW_XR_RESULT_INVALID;
    if (bridge->config.disabled) {
        bridge->state = IW_XR_DISABLED;
        bridge->result = IW_XR_RESULT_UNAVAILABLE;
        bridge_copy_reason(bridge, "OpenXR probing disabled");
        return bridge->result;
    }
    if (probe)
        bridge->probe = probe;
    if (!bridge->probe) {
        bridge->state = IW_XR_UNAVAILABLE;
        bridge->result = IW_XR_RESULT_UNAVAILABLE;
        bridge_copy_reason(bridge, "OpenXR probe callback is unavailable");
        return bridge->result;
    }
    bridge->state = IW_XR_INITIALIZING;
    bridge->probe_start_ns = bridge_now_ns(bridge);
    deadline = bridge->probe_start_ns + (uint64_t)bridge->config.probe_budget_ms * 1000000ULL;
    result = bridge->probe(bridge, deadline, &reason);
    bridge->probe_duration_ns = bridge_now_ns(bridge) - bridge->probe_start_ns;
    if (result == IW_XR_RESULT_OK) {
        bridge->state = IW_XR_ACTIVE;
        bridge->result = result;
        bridge_copy_reason(bridge, "");
    } else {
        bridge->state = (result == IW_XR_RESULT_TIMEOUT || result == IW_XR_RESULT_UNAVAILABLE) ? IW_XR_UNAVAILABLE : IW_XR_FAILED;
        bridge->result = result;
        bridge_copy_reason(bridge, reason);
    }
    return result;
}

iw_xr_result_t IW_XRBridge_Pump(iw_xr_bridge_t *bridge)
{
    if (!bridge)
        return IW_XR_RESULT_INVALID;
    if (bridge->state != IW_XR_ACTIVE || bridge->paused)
        return bridge->result;
    return IW_XR_RESULT_OK;
}

void IW_XRBridge_Pause(iw_xr_bridge_t *bridge) { if (bridge) bridge->paused = true; }
void IW_XRBridge_Resume(iw_xr_bridge_t *bridge) { if (bridge) bridge->paused = false; }

void IW_XRBridge_Shutdown(iw_xr_bridge_t *bridge)
{
    if (!bridge)
        return;
    bridge->state = bridge->config.disabled ? IW_XR_DISABLED : IW_XR_UNAVAILABLE;
    bridge->result = IW_XR_RESULT_UNAVAILABLE;
    bridge->paused = false;
    bridge_copy_reason(bridge, "OpenXR session shut down");
}

void IW_XRBridge_SetDisabled(iw_xr_bridge_t *bridge, qboolean disabled)
{
    if (!bridge)
        return;
    bridge->config.disabled = disabled;
    if (disabled) {
        IW_XRBridge_Shutdown(bridge);
        bridge->state = IW_XR_DISABLED;
        bridge_copy_reason(bridge, "OpenXR probing disabled");
    } else if (bridge->state == IW_XR_DISABLED) {
        bridge->state = IW_XR_UNAVAILABLE;
        bridge->result = IW_XR_RESULT_UNAVAILABLE;
        bridge_copy_reason(bridge, "OpenXR probing re-enabled");
    }
}

iw_xr_result_t IW_XRBridge_RequestRetry(iw_xr_bridge_t *bridge)
{
    if (!bridge || bridge->config.disabled)
        return IW_XR_RESULT_UNAVAILABLE;
    return IW_XRBridge_Initialize(bridge, bridge->probe);
}

iw_xr_state_t IW_XRBridge_State(const iw_xr_bridge_t *bridge)
{ return bridge ? bridge->state : IW_XR_FAILED; }
const char *IW_XRBridge_FailureReason(const iw_xr_bridge_t *bridge)
{ return bridge ? bridge->reason : "Invalid bridge"; }
uint64_t IW_XRBridge_ProbeDurationNs(const iw_xr_bridge_t *bridge)
{ return bridge ? bridge->probe_duration_ns : 0; }
qboolean IW_XRBridge_OwnsPresentation(const iw_xr_bridge_t *bridge)
{ return bridge && bridge->state == IW_XR_ACTIVE; }
qboolean IW_XRBridge_OwnsInput(const iw_xr_bridge_t *bridge)
{ return bridge && bridge->state == IW_XR_ACTIVE; }

qboolean IW_XRBridge_GetFrame(const iw_xr_bridge_t *bridge,
                              iw_xr_frame_snapshot_t *snapshot)
{ if (!bridge || !snapshot || bridge->state != IW_XR_ACTIVE) return false; memset(snapshot, 0, sizeof(*snapshot)); return false; }
qboolean IW_XRBridge_GetActions(const iw_xr_bridge_t *bridge,
                                iw_xr_action_snapshot_t *actions)
{ if (!bridge || !actions || bridge->state != IW_XR_ACTIVE) return false; memset(actions, 0, sizeof(*actions)); return false; }
qboolean IW_XRBridge_GetListenerPose(const iw_xr_bridge_t *bridge,
                                     iw_xr_listener_pose_t *pose)
{ if (!bridge || !pose || bridge->state != IW_XR_ACTIVE) return false; memset(pose, 0, sizeof(*pose)); return false; }
qboolean IW_XRBridge_AcquireTarget(iw_xr_bridge_t *bridge, unsigned view, void **target)
{ (void)bridge; (void)view; if (target) *target = NULL; return false; }
qboolean IW_XRBridge_BindTarget(iw_xr_bridge_t *bridge, void *target)
{ (void)bridge; (void)target; return false; }
void IW_XRBridge_ReleaseTarget(iw_xr_bridge_t *bridge, void *target)
{ (void)bridge; (void)target; }

void IW_XRBridge_SetFailure(iw_xr_bridge_t *bridge, iw_xr_result_t result,
                            const char *reason)
{
    if (!bridge)
        return;
    bridge->result = result;
    bridge->state = (result == IW_XR_RESULT_TIMEOUT || result == IW_XR_RESULT_UNAVAILABLE) ? IW_XR_UNAVAILABLE : IW_XR_FAILED;
    bridge_copy_reason(bridge, reason);
}
