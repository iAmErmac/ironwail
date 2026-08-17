#ifndef IRONWAIL_XR_INTERACTION_H
#define IRONWAIL_XR_INTERACTION_H

#include "q_stdinc.h"
#include "xr_bridge.h"

void XR_Interaction_Init(void);
void XR_Interaction_Shutdown(void);
void XR_Interaction_Update(const iw_xr_action_snapshot_t *actions);
void XR_Interaction_AddWorldEntities(void);
void XR_Interaction_DrawWorldModels(void);
void XR_Interaction_Draw(void);
qboolean XR_Interaction_ConsumesGameplay(void);
qboolean XR_Interaction_WheelActive(void);
qboolean XR_Interaction_GetVirtualPointer(float start[3], float hit[3]);

#endif
