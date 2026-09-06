/* Native Android host contract.  IronRift owns the EGL/GLES context.
 * invokes these callbacks on the render thread. */
#ifndef IRONWAIL_ANDROID_LIFECYCLE_H
#define IRONWAIL_ANDROID_LIFECYCLE_H

#include <stdint.h>
#include "q_stdinc.h"
#include "xr_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

qboolean IW_Android_Init(const char *base_dir, int argc, const char *const *argv);
void IW_Android_SurfaceCreated(void);
void IW_Android_SurfaceDestroyed(void);
void IW_Android_ContextRestored(void);
void IW_Android_Resize(int width, int height);
void IW_Android_Frame(uint64_t frame_time_ns);
void IW_Android_FrameXR(uint64_t frame_time_ns, unsigned target_fbo, int target_width, int target_height);
qboolean IW_Android_FrameXRStereo(uint64_t frame_time_ns, const iw_xr_frame_snapshot_t *snapshot, unsigned mono_fbo, int mono_width, int mono_height, const unsigned *eye_fbos, const int *eye_widths, const int *eye_heights);
qboolean IW_Android_FrameXRStereoMultiview(uint64_t frame_time_ns, const iw_xr_frame_snapshot_t *snapshot, unsigned mono_fbo, int mono_width, int mono_height, unsigned layered_fbo, const unsigned *overlay_fbos, int layered_width, int layered_height);
void IW_Android_SetXRStereoFrame(const iw_xr_frame_snapshot_t *snapshot, const unsigned *fbos, const int *widths, const int *heights);
void IW_Android_ClearXRStereoFrame(void);
qboolean IW_Android_GetXRStereoFrame(const iw_xr_frame_snapshot_t **snapshot);
qboolean IW_Android_GetXRHeadPosition(float position[3]);
qboolean IW_Android_BeginXREye(unsigned eye, unsigned *fbo, int *width, int *height);
void IW_Android_EndXREye(unsigned eye);
qboolean IW_Android_BeginXRHUD(unsigned *fbo, int *width, int *height);
void IW_Android_SetXRMultiviewRequested(qboolean requested);
qboolean IW_Android_XRMultiviewRequested(void);
qboolean IW_Android_XRGameplayStereoEligible(void);
qboolean IW_Android_UsingXRMultiview(void);
qboolean IW_Android_BeginXRMultiview(unsigned *fbo, int *width, int *height);
qboolean IW_Android_BeginXRMultiviewOverlayEye(unsigned eye, unsigned *fbo, int *width, int *height);
qboolean IW_Android_RaycastVirtualScreen(const float origin[3], const float orientation[4], iw_xr_virtual_screen_hit_t *hit);
qboolean IW_Android_GetXRScreenPose(float position[3], float orientation[4]);
void IW_Android_SetVirtualPointer(const float start[3], const float hit[3], qboolean active, unsigned color, float alpha, float width);
float IW_Android_GetXRRenderScale(void);
float IW_Android_GetXRRefreshRate(void);
void IW_Android_GetXRScreenGeometry(float *scale, float *distance, qboolean *follow);
void IW_Android_GetXRScreenStyle(qboolean *curved, float *radius);
qboolean IW_Android_GetXRBackdropScene(void);
void IW_Android_GetXRHUDGeometry(float *scale, float *distance, float *yoffset);
qboolean IW_Android_GetVirtualPointer(float start[3], float hit[3], unsigned *color, float *alpha, float *width);
void IW_Android_Key(int android_keycode, qboolean down);
void IW_Android_Text(const char *text);
void IW_Android_Action(int action, qboolean down);
void IW_Android_SetXRActions(const iw_xr_action_snapshot_t *actions);
qboolean IW_Android_GetXRActions(iw_xr_action_snapshot_t *actions);
void IW_Android_Haptic(int hand, float amplitude, float duration_seconds);
void IW_Android_ClearActions(void);
void IW_Android_Command(const char *command);
void IW_Android_Axis(int device_id, int axis, float value);
void IW_Android_Touch(int action, float x, float y);
void IW_Android_TouchPointer(int action, int pointer_id, float x, float y);
void IW_Android_Look(int delta_x, int delta_y);
int IW_Android_ScreenMode(void);
void IW_Android_Pause(qboolean paused);
// Android platform transport for add-on manifests and archives.
char *IW_Android_DownloadText(const char *url);
qboolean IW_Android_DownloadFile(const char *url, const char *destination);
qboolean IW_Android_RequestRestart(void);

void IW_Android_AudioFocus(qboolean focused);
void IW_Android_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
