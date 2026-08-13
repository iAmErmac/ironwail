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
qboolean IW_XRWin_BeginFrame(iw_xr_win_t *xr, iw_xr_frame_snapshot_t *snapshot);
qboolean IW_XRWin_BindFrameTarget(iw_xr_win_t *xr);
qboolean IW_XRWin_ResolveDefaultFramebuffer(iw_xr_win_t *xr, int source_width, int source_height);
iw_xr_result_t IW_XRWin_EndFrame(iw_xr_win_t *xr, qboolean submit);
void IW_XRWin_Shutdown(iw_xr_win_t *xr);
void IW_XRWin_RequestRecenter(iw_xr_win_t *xr);
void IW_XRWin_SetScreenCurve(iw_xr_win_t *xr, qboolean enabled, float radius);
void IW_XRWin_SetScreenGeometry(iw_xr_win_t *xr, float scale, float distance);

#endif