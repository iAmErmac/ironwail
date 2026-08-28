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
qboolean XR_Interaction_OffhandAttackActive(void);
qboolean XR_Interaction_UseOffhandAim(void);
qboolean XR_Interaction_LocalOffhandAttackRequested(void);
qboolean XR_Interaction_LocalOffhandAttackReady(double time);
void XR_Interaction_SetLocalOffhandCooldown(double time, float attack_finished);
void XR_Interaction_BeginLocalOffhandAttack(void);
void XR_Interaction_EndLocalOffhandAttack(void);
void XR_Interaction_SetLocalOffhandWeaponState(float frame, float attack_finished);
void XR_Interaction_BeginLocalOffhandAnimation(double time, float frame);
float XR_Interaction_LocalOffhandWeaponFrame(void);
float XR_Interaction_LocalOffhandAttackFinished(void);
void XR_Interaction_SetLocalOffhandBeamEntity(int entity, double time);
qboolean XR_Interaction_IsLocalOffhandBeamEntity(int entity);
qboolean XR_Interaction_AllowOffhandContinuousAutoSound(void);
qboolean XR_Interaction_GetVisualFireHand(iw_xr_hand_t *hand);
qboolean XR_Interaction_GetNetworkGrenadePitch(float *pitch);
void XR_Interaction_ResetOffhandContinuousAudio(void);
int XR_Interaction_MainhandWeaponItem(void);
int XR_Interaction_OffhandWeaponItem(void);
qboolean XR_Interaction_GetMainhandViewmodel(entity_t *out);
qboolean XR_Interaction_GetOffhandViewmodel(entity_t *out);
qboolean XR_Interaction_GetVirtualPointer(float start[3], float hit[3]);

#endif
