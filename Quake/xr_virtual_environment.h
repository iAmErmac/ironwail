#ifndef IRONWAIL_XR_VIRTUAL_ENVIRONMENT_H
#define IRONWAIL_XR_VIRTUAL_ENVIRONMENT_H

#include "xr_bridge.h"
#include "xr_virtual_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

qboolean IW_XRVirtualEnvironment_Render(const iw_xr_view_t *view, const iw_xr_virtual_screen_t *screen, unsigned source_texture, unsigned target_fbo, int target_width, int target_height);
void IW_XRVirtualEnvironment_Invalidate(void);

#ifdef __cplusplus
}
#endif

#endif