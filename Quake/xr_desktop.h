#ifndef IRONWAIL_XR_OPENXR_WIN_H
#define IRONWAIL_XR_OPENXR_WIN_H

#include "xr_bridge.h"

#if defined(IW_ENABLE_OPENXR)
#include <windows.h>
#include <unknwn.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#endif

typedef struct iw_xr_win_s iw_xr_win_t;

iw_xr_win_t *IW_XRWin_Create(void *window, void (*log)(void *, const char *), void *userdata);
void IW_XRWin_Destroy(iw_xr_win_t *xr);
iw_xr_result_t IW_XRWin_Probe(iw_xr_win_t *xr, iw_xr_bridge_t *bridge, uint64_t deadline_ns, const char **reason);
iw_xr_result_t IW_XRWin_Pump(iw_xr_win_t *xr, iw_xr_bridge_t *bridge);
qboolean IW_XRWin_GetActions(const iw_xr_win_t *xr, iw_xr_action_snapshot_t *actions);
qboolean IW_XRWin_RaycastVirtualScreen(const iw_xr_win_t *xr, const float origin[3], const float orientation[4], iw_xr_virtual_screen_hit_t *hit);
void IW_XRWin_SetVirtualPointer(iw_xr_win_t *xr, const float start[3], const float hit[3], qboolean active, unsigned color, float alpha, float width);
void IW_XRWin_Haptic(iw_xr_win_t *xr, int hand, float amplitude, float duration_seconds);
qboolean IW_XRWin_BeginFrame(iw_xr_win_t *xr, iw_xr_frame_snapshot_t *snapshot);
qboolean IW_XRWin_BindFrameTarget(iw_xr_win_t *xr);
qboolean IW_XRWin_GetFrameTarget(const iw_xr_win_t *xr, unsigned *fbo, int *width, int *height);
qboolean IW_XRWin_ResolveDefaultFramebuffer(iw_xr_win_t *xr, int source_width, int source_height);
qboolean IW_XRWin_HasStereoTargets(const iw_xr_win_t *xr);
void IW_XRWin_SetMultiviewRequested(iw_xr_win_t *xr, qboolean requested);
qboolean IW_XRWin_UsingMultiview(const iw_xr_win_t *xr);
qboolean IW_XRWin_BeginMultiviewTarget(iw_xr_win_t *xr, unsigned *fbo, int *width, int *height);
void IW_XRWin_EndMultiviewTarget(iw_xr_win_t *xr);
qboolean IW_XRWin_BindEyeTarget(iw_xr_win_t *xr, unsigned eye);
qboolean IW_XRWin_GetEyeTarget(const iw_xr_win_t *xr, unsigned eye, unsigned *fbo, int *width, int *height);
void IW_XRWin_MirrorEye(iw_xr_win_t *xr, unsigned eye, int width, int height);
void IW_XRWin_MirrorMultiview(iw_xr_win_t *xr, int width, int height);

qboolean IW_XRWin_ResolveEyeTarget(iw_xr_win_t *xr, unsigned eye, int source_width, int source_height);
void IW_XRWin_SetStereoSubmission(iw_xr_win_t *xr, qboolean enabled);
iw_xr_result_t IW_XRWin_EndFrame(iw_xr_win_t *xr, qboolean submit);
void IW_XRWin_Shutdown(iw_xr_win_t *xr);
void IW_XRWin_RequestRecenter(iw_xr_win_t *xr);
void IW_XRWin_SetScreenCurve(iw_xr_win_t *xr, qboolean enabled, float radius);
void IW_XRWin_SetScreenGeometry(iw_xr_win_t *xr, float scale, float distance);
void IW_XRWin_SetHUDGeometry(iw_xr_win_t *xr, float scale, float distance, float yoffset);

#endif