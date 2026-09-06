#ifndef IRONWAIL_XR_INPUT_H
#define IRONWAIL_XR_INPUT_H

#include "q_stdinc.h"
#include "xr_bridge.h"
#include "xr_action_schema.h"

typedef enum {
    XR_HAND_MAINHAND = 0,
    XR_HAND_OFFHAND = 1
} xr_hand_role_t;

void XR_Input_Init(void);
void XR_Input_Shutdown(void);
void XR_Input_MapBegin(void);
void XR_Input_Update(void);
void XR_Input_PrepareInputGrab(void);
qboolean XR_Input_OwnsInput(void);
qboolean XR_Input_WantsCutsceneSkip(void);
qboolean XR_Input_TeamSelectionActive(void);
int XR_Input_ConsumeTeamSelectionImpulse(void);
qboolean XR_Input_UsesRoomscale(void);
qboolean XR_Input_Move(usercmd_t *cmd);
qboolean XR_Input_FlyModeActive(void);
void XR_Input_PrepareFlyViewAngles(vec3_t angles);
void XR_Input_ApplyDash(void);
qboolean XR_Input_IsDashing(void);
qboolean XR_Input_IsTeleportAiming(void);
qboolean XR_Input_HasTeleportTarget(void);
qboolean XR_Input_GetTeleportAim(vec3_t start, vec3_t target);
iw_xr_hand_t XR_Input_PhysicalHandForRole(xr_hand_role_t role);
xr_hand_role_t XR_Input_RoleForPhysicalHand(iw_xr_hand_t hand);
const iw_xr_hand_snapshot_t *XR_Input_HandForRole(const iw_xr_action_snapshot_t *actions, xr_hand_role_t role);
iw_xr_hand_t XR_Input_MovementHand(void);
iw_xr_hand_t XR_Input_TurnHand(void);
iw_xr_hand_t XR_Input_MenuHand(void);
iw_xr_hand_t XR_Input_MouseHand(void);
iw_xr_hand_t XR_Input_RightStickHand(void);
iw_xr_hand_t XR_Input_LeftStickHand(void);

#endif
