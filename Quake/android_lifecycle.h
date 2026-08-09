/* Native Android host contract.  IronRift owns the EGL/GLES context.
 * invokes these callbacks on the render thread. */
#ifndef IRONWAIL_ANDROID_LIFECYCLE_H
#define IRONWAIL_ANDROID_LIFECYCLE_H

#include <stdint.h>
#include "q_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

qboolean IW_Android_Init(const char *base_dir, int argc, const char *const *argv);
void IW_Android_SurfaceCreated(void);
void IW_Android_SurfaceDestroyed(void);
void IW_Android_ContextRestored(void);
void IW_Android_Resize(int width, int height);
void IW_Android_Frame(uint64_t frame_time_ns);
void IW_Android_Key(int android_keycode, qboolean down);
void IW_Android_Text(const char *text);
void IW_Android_Action(int action, qboolean down);
void IW_Android_Command(const char *command);
void IW_Android_Axis(int device_id, int axis, float value);
void IW_Android_Touch(int action, float x, float y);
void IW_Android_TouchPointer(int action, int pointer_id, float x, float y);
void IW_Android_Look(int delta_x, int delta_y);
int IW_Android_ScreenMode(void);
void IW_Android_Pause(qboolean paused);
void IW_Android_AudioFocus(qboolean focused);
void IW_Android_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
