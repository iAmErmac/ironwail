#ifndef IRONWAIL_XR_INTERACTION_H
#define IRONWAIL_XR_INTERACTION_H

#include "q_stdinc.h"
#include "xr_bridge.h"
#include "xr_input.h"

typedef enum {
    XR_MELEE_NONE = 0,
    XR_MELEE_AXE,
    XR_MELEE_MJOLNIR,
    XR_MELEE_FIST,
    XR_MELEE_GUNBUTT
} xr_melee_mode_t;

typedef struct {
    qboolean valid;
    iw_xr_hand_t hand;
    xr_melee_mode_t mode;
    float speed;
    float damage_scale;
} xr_melee_attack_t;

typedef enum {
    XR_NETWORK_ATTACK_NONE = 0,
    XR_NETWORK_ATTACK_MAINHAND,
    XR_NETWORK_ATTACK_OFFHAND
} xr_network_attack_owner_t;

void XR_Interaction_Init(void);
void XR_Interaction_Shutdown(void);
void XR_Interaction_ReloadGameData(void);
void XR_Interaction_Update(const iw_xr_action_snapshot_t *actions);
void XR_Interaction_AddWorldEntities(void);
void XR_Interaction_DrawWorldModels(void);
void XR_Interaction_Draw(void);
qboolean XR_Interaction_ConsumesGameplay(void);
qboolean XR_Interaction_OpenKeyboardForMenu(void);
qboolean XR_Interaction_WheelActive(void);
qboolean XR_Interaction_OffhandAttackActive(void);
qboolean XR_Interaction_MainhandFireInputActive(void);
qboolean XR_Interaction_OffhandNetworkAttackActive(void);
xr_network_attack_owner_t XR_Interaction_PrepareNetworkAttack(qboolean main_requested, int user_impulse, int *network_impulse);
void XR_Interaction_CommitNetworkAttack(xr_network_attack_owner_t owner, int network_impulse);
qboolean XR_Interaction_PrepareNetworkViewAngles(xr_network_attack_owner_t owner, vec3_t angles);
qboolean XR_Interaction_ConsumeLocalOffhandFireEvent(void);
qboolean XR_Interaction_UseOffhandAim(void);
void XR_Interaction_SetLocalOffhandCooldown(double time, float attack_finished);
void XR_Interaction_BeginLocalOffhandAttack(void);
void XR_Interaction_EndLocalOffhandAttack(void);
void XR_Interaction_SetLocalOffhandWeaponState(float frame, float attack_finished);
void XR_Interaction_BeginLocalOffhandAnimation(double time, float frame);
void XR_Interaction_LocalOffhandAttackResult(qboolean fired, qboolean ammo_empty);
float XR_Interaction_LocalOffhandWeaponFrame(void);
float XR_Interaction_LocalOffhandAttackFinished(void);
void XR_Interaction_SetLocalOffhandBeamEntity(int entity, double time);
qboolean XR_Interaction_IsLocalOffhandBeamEntity(int entity);
void XR_Interaction_RecordLocalProjectileSpawn(int entity, iw_xr_hand_t hand, int weapon);
qboolean XR_Interaction_ConsumeLocalProjectileSpawn(int entity, iw_xr_hand_t *hand, int *weapon, qboolean *second_offset);
qboolean XR_Interaction_PeekNetworkProjectileVisual(iw_xr_hand_t *hand, int *weapon);
qboolean XR_Interaction_ConsumeNetworkProjectileVisual(iw_xr_hand_t *hand, int *weapon, qboolean *second_offset);
void XR_Interaction_ClearLocalProjectileSpawns(void);
qboolean XR_Interaction_AllowOffhandContinuousAutoSound(void);
qboolean XR_Interaction_GetVisualFireHand(iw_xr_hand_t *hand);
qboolean XR_Interaction_UseSecondVisualProjectileOffset(iw_xr_hand_t hand);
qboolean XR_Interaction_AllowOffhandFireInput(void);
void XR_Interaction_NotifyWeaponFire(iw_xr_hand_t hand, float previous_attack_finished, float attack_finished, double time);
qboolean XR_Interaction_GetNetworkGrenadePitch(float *pitch);
void XR_Interaction_ResetOffhandContinuousAudio(void);
int XR_Interaction_MainhandWeaponItem(void);
int XR_Interaction_MainhandCurrentAmmo(void);
int XR_Interaction_OffhandWeaponItem(void);
xr_melee_mode_t XR_Interaction_GetWeaponMeleeMode(int item);
qboolean XR_Interaction_GetMeleePulse(xr_hand_role_t role, xr_melee_attack_t *attack);
qboolean XR_Interaction_ConsumeMeleePulse(xr_hand_role_t role, xr_melee_attack_t *attack);
void XR_Interaction_NotifyMeleeResult(iw_xr_hand_t hand, qboolean contacted);
qboolean XR_Interaction_LocalOffhandAttackReady(double time, xr_melee_attack_t *motion);
void XR_Interaction_SetMeleeDamageContext(const xr_melee_attack_t *attack);
qboolean XR_Interaction_GetMeleeDamageContext(xr_melee_attack_t *attack);
void XR_Interaction_ApplyMeleePitch(vec3_t forward, vec3_t up);
void XR_Interaction_ClearMeleeDamageContext(void);
void XR_Interaction_ResetMeleeState(void);
qboolean XR_Interaction_GetFireViewmodel(int weapon, entity_t *out);
qboolean XR_Interaction_GetMainhandViewmodel(entity_t *out);
qboolean XR_Interaction_GetOffhandViewmodel(entity_t *out);
qboolean XR_Interaction_GetVirtualPointer(float start[3], float hit[3]);

#endif
