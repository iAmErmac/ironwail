/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_main.c

#include "quakedef.h"
#include "xr_bridge.h"
#include "xr_interaction.h"
#include "xr_input.h"
#if defined(ANDROID_GLES3)
#include "gl_gles_ubo.h"
#include "gl_gles_vao.h"
#endif
#if defined(ANDROID_GLES3)
#define glDepthRange glDepthRangef
#endif

qboolean	r_cache_thrash;		// compatability

gpuframedata_t r_framedata;

vec3_t		*r_pointfile;

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

extern cvar_t vr_weapon_pitch, vr_weapon_xoffset, vr_weapon_yoffset, vr_weapon_zoffset, vr_offhand_render_flip;
extern void VR_WeaponOffset(const qmodel_t *model, vec3_t offset);
extern void VR_WeaponFireOffset(const qmodel_t *model, vec3_t offset);
extern void VR_WeaponFire2Offset(const qmodel_t *model, vec3_t offset);
extern qboolean VR_IsConfiguredProjectileModel(const qmodel_t *model);

mplane_t	frustum[4];
float		r_matview[16];
float		r_matproj[16];
float		r_matviewproj[16];

//johnfitz -- rendering statistics
int rs_brushpolys, rs_aliaspolys, rs_skypolys;
int rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses, rs_skypasses;

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

float r_fovx, r_fovy; //johnfitz -- rendering fov may be different becuase of r_waterwarp and r_stereo
qboolean water_warp;

extern byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel);
extern qboolean SV_EdictInPVS (edict_t *test, byte *pvs);
extern qboolean SV_BoxInPVS (vec3_t mins, vec3_t maxs, byte *pvs, mnode_t *node);

//
// screen size info
//
refdef_t	r_refdef;
extern cvar_t vr_world_scale, vr_roomscale, vr_smooth_stairs;
extern cvar_t vr_laser_sight, vr_laser_beam, vr_laser_color, vr_laser_beam_width, vr_laser_beam_alpha, vr_laser_alpha, vr_laser_sight_scale, vr_laser_hide_melee;
extern qboolean VID_XR_GetActions(iw_xr_action_snapshot_t *actions);
extern qboolean XR_Input_GetTeleportAim(vec3_t start, vec3_t target);

static qboolean r_xr_eye_pass;
static unsigned r_xr_eye_index;static GLuint r_xr_final_fbo;
static int r_xr_final_width;
static int r_xr_final_height;

void R_SetXRFinalTarget(GLuint fbo, int width, int height)
{
    r_xr_final_fbo = fbo;
    r_xr_final_width = width;
    r_xr_final_height = height;
}

qboolean R_HasXRFinalTarget(void) { return r_xr_final_fbo != 0; }
void R_BindXRFinalTarget(void)
{
    if (!r_xr_final_fbo)
        return;
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, r_xr_final_fbo);
    glViewport(0, 0, r_xr_final_width, r_xr_final_height);
}
static qboolean r_xr_asymmetric_projection;
static iw_xr_fov_t r_xr_fov;
static qboolean r_xr_head_anchor_valid;
static qboolean r_xr_sync_player_yaw;
static char r_xr_anchor_mapname[MAX_QPATH];
static qboolean r_xr_anchor_origin_valid;
static vec3_t r_xr_anchor_origin;
static float r_xr_recenter_yaw;
static qboolean r_xr_recenter_to_head;
static float r_xr_head_anchor_yaw;
static float r_xr_game_anchor_yaw;
static float r_xr_ipd;
static qboolean r_xr_view_basis_valid;
static vec3_t r_xr_forward, r_xr_right, r_xr_up;
static vec3_t r_xr_center_vieworg, r_xr_head_anchor_position;
static qboolean r_xr_stair_smooth_valid;
static float r_xr_stair_smooth_z;
static double r_xr_stair_smooth_time;
static double r_xr_stair_ground_time;
static qboolean r_xr_viewmodel_orientation_valid;
static entity_t *r_xr_mainhand_viewmodel, *r_xr_offhand_viewmodel;
static vec3_t r_xr_viewmodel_forward, r_xr_viewmodel_right, r_xr_viewmodel_up;
static qboolean r_xr_controller_aim_valid;
static vec3_t r_xr_controller_forward, r_xr_controller_right, r_xr_controller_up, r_xr_controller_origin;
static qboolean r_xr_hand_tracking_valid[IW_XR_HAND_COUNT], r_xr_hand_aim_valid[IW_XR_HAND_COUNT];
static vec3_t r_xr_hand_tracking_origin[IW_XR_HAND_COUNT], r_xr_hand_tracking_forward[IW_XR_HAND_COUNT], r_xr_hand_tracking_right[IW_XR_HAND_COUNT], r_xr_hand_tracking_up[IW_XR_HAND_COUNT];
static vec3_t r_xr_hand_aim_origin[IW_XR_HAND_COUNT], r_xr_hand_aim_forward[IW_XR_HAND_COUNT], r_xr_hand_aim_right[IW_XR_HAND_COUNT], r_xr_hand_aim_up[IW_XR_HAND_COUNT];
typedef struct { qboolean valid; vec3_t start, end, forward; } xr_laser_t;
static xr_laser_t r_xr_laser[IW_XR_HAND_COUNT];
static qboolean r_xr_pointer_valid, r_xr_teleport_marker_valid;
static vec3_t r_xr_pointer_start, r_xr_pointer_end, r_xr_teleport_marker_origin;
static uint32_t r_xr_teleport_marker_color;
static float r_xr_weapon_pitch;
static qboolean R_XRGetWeaponOrientation (iw_xr_hand_t hand, vec3_t origin, vec3_t forward, vec3_t right, vec3_t up);
static void R_XRApplyPitch (float pitch, vec3_t forward, vec3_t up)
{
    float c = cosf (DEG2RAD (pitch));
    float s = sinf (DEG2RAD (pitch));
    vec3_t original_forward, original_up;
    VectorCopy (forward, original_forward);
    VectorCopy (up, original_up);
    VectorScale (original_forward, c, forward);
    VectorMA (forward, s, original_up, forward);
    VectorScale (original_up, c, up);
    VectorMA (up, -s, original_forward, up);
}
static void R_XRApplyWeaponPitchCorrection (int weapon, vec3_t forward, vec3_t up)
{
    if (weapon != IT_SHOTGUN && weapon != IT_ROCKET_LAUNCHER)
        return;
    R_XRApplyPitch (5.f, forward, up);
}
static float R_XRGlobalLateralOffset (iw_xr_hand_t hand, float offset)
{
    return hand == XR_Input_PhysicalHandForRole (XR_HAND_OFFHAND) && vr_offhand_render_flip.value != 0.f ? -offset : offset;
}
static qboolean R_XRGetWeaponOrientation (iw_xr_hand_t hand, vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
    qboolean mainhand = hand == XR_Input_PhysicalHandForRole (XR_HAND_MAINHAND);
    if (mainhand)
    {
        if (!r_xr_controller_aim_valid)
            return false;
        if (origin) VectorCopy (r_xr_controller_origin, origin);
        VectorCopy (r_xr_viewmodel_forward, forward);
        VectorCopy (r_xr_viewmodel_right, right);
        VectorCopy (r_xr_viewmodel_up, up);
    }
    else
    {
        if (!R_GetXRHandTrackingPose (hand, origin, forward, right, up) && !R_GetXRHandAimPose (hand, origin, forward, right, up))
            return false;
        if (vr_offhand_render_flip.value == 0.f)
            VectorScale (right, -1.f, right);
        R_XRApplyPitch (r_xr_weapon_pitch, forward, up);
    }
    return true;
}
static qboolean R_XRGetWeaponBasis (iw_xr_hand_t hand, vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
    int weapon;
    if (!R_XRGetWeaponOrientation (hand, origin, forward, right, up))
        return false;
    weapon = hand == XR_Input_PhysicalHandForRole (XR_HAND_MAINHAND) ? XR_Interaction_MainhandWeaponItem () : XR_Interaction_OffhandWeaponItem ();
    R_XRApplyWeaponPitchCorrection (weapon, forward, up);
    return true;
}
qboolean R_GetXRViewmodelMatrix (entity_t *e, float matrix[16], const vec3_t origin, unsigned char scale)
{
	vec3_t forward, right, up;
	float scalefactor;
	qboolean offhand = e == r_xr_offhand_viewmodel;
    qboolean mainhand = e == &cl.viewent || e == r_xr_mainhand_viewmodel;
	if ((!offhand && !mainhand) || !r_xr_eye_pass || (mainhand && !r_xr_viewmodel_orientation_valid))
		return false;
	if (!R_XRGetWeaponBasis (offhand ? XR_Input_PhysicalHandForRole (XR_HAND_OFFHAND) : XR_Input_PhysicalHandForRole (XR_HAND_MAINHAND), NULL, forward, right, up))
		return false;
	scalefactor = ENTSCALE_DECODE(scale);
	matrix[0] = forward[0] * scalefactor;
	matrix[1] = forward[1] * scalefactor;
	matrix[2] = forward[2] * scalefactor;
	matrix[3] = 0.f;
	matrix[4] = right[0] * scalefactor;
	matrix[5] = right[1] * scalefactor;
	matrix[6] = right[2] * scalefactor;
	matrix[7] = 0.f;
	matrix[8] = up[0] * scalefactor;
	matrix[9] = up[1] * scalefactor;
	matrix[10] = up[2] * scalefactor;
	matrix[11] = 0.f;
	matrix[12] = origin[0];
	matrix[13] = origin[1];
	matrix[14] = origin[2];
	matrix[15] = 1.f;
	return true;
}
qboolean R_XRViewmodelMirrored (const entity_t *e) { return e == r_xr_offhand_viewmodel && vr_offhand_render_flip.value != 0.f; }
qboolean R_GetXRHandPose (iw_xr_hand_t hand, vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
    const vec3_t *hand_origin, *hand_forward, *hand_right, *hand_up;
    if (hand < 0 || hand >= IW_XR_HAND_COUNT) return false;
    /* Some mobile runtimes expose grip before aim; retain tracked-hand aiming in that case. */
    if (r_xr_hand_aim_valid[hand])
    {
        hand_origin = &r_xr_hand_aim_origin[hand]; hand_forward = &r_xr_hand_aim_forward[hand];
        hand_right = &r_xr_hand_aim_right[hand]; hand_up = &r_xr_hand_aim_up[hand];
    }
    else if (r_xr_hand_tracking_valid[hand])
    {
        hand_origin = &r_xr_hand_tracking_origin[hand]; hand_forward = &r_xr_hand_tracking_forward[hand];
        hand_right = &r_xr_hand_tracking_right[hand]; hand_up = &r_xr_hand_tracking_up[hand];
    }
    else return false;
    if (origin) VectorCopy(*hand_origin, origin);
    if (forward) VectorCopy(*hand_forward, forward);
    if (right) VectorCopy(*hand_right, right);
    if (up) VectorCopy(*hand_up, up);
    return true;
}
qboolean R_GetXRHandTrackingPose (iw_xr_hand_t hand, vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
    if (hand < 0 || hand >= IW_XR_HAND_COUNT || !r_xr_hand_tracking_valid[hand]) return false;
    if (origin) VectorCopy(r_xr_hand_tracking_origin[hand], origin);
    if (forward) VectorCopy(r_xr_hand_tracking_forward[hand], forward);
    if (right) VectorCopy(r_xr_hand_tracking_right[hand], right);
    if (up) VectorCopy(r_xr_hand_tracking_up[hand], up);
    return true;
}

qboolean R_GetXRHandAimPose (iw_xr_hand_t hand, vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
    return R_GetXRHandPose(hand, origin, forward, right, up);
}

qboolean R_GetXRMainHandWeaponPose (vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
    if (XR_Interaction_UseOffhandAim())
    {
        iw_xr_hand_t hand = XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND);
        if (R_XRGetWeaponOrientation(hand, origin, forward, right, up)) return true;
        if (r_xr_laser[hand].valid)
        {
            vec3_t basis_up = {0.f, 0.f, 1.f};
            if (origin) VectorCopy(r_xr_laser[hand].start, origin);
            if (forward) VectorCopy(r_xr_laser[hand].forward, forward);
            if (fabsf(r_xr_laser[hand].forward[2]) > 0.98f) VectorSet(basis_up, 0.f, 1.f, 0.f);
            if (right) { CrossProduct(r_xr_laser[hand].forward, basis_up, right); VectorNormalize(right); }
            if (up) { vec3_t basis_right; CrossProduct(r_xr_laser[hand].forward, basis_up, basis_right); VectorNormalize(basis_right); CrossProduct(basis_right, r_xr_laser[hand].forward, up); VectorNormalize(up); }
            return true;
        }
        return false;
    }
    if (!r_xr_controller_aim_valid) return false;
    if (origin) VectorCopy(r_xr_controller_origin, origin);
    if (forward) VectorCopy(r_xr_controller_forward, forward);
    if (right) VectorCopy(r_xr_controller_right, right);
    if (up) VectorCopy(r_xr_controller_up, up);
    return true;
}
void R_SetXRTeleportMarker (const vec3_t origin, qboolean active, uint32_t color)
{
	r_xr_teleport_marker_valid = active;
	if (active && origin) { VectorCopy (origin, r_xr_teleport_marker_origin); r_xr_teleport_marker_color = color; }
}
static void R_XRToQuakePosition (const float xr[3], float quake[3]);
static void R_XRRotateYaw (float vector[3], float yaw);
static void R_XRRotateOrientation (const float orientation[4], const float in[3], float out[3]);


static uint32_t R_XRLaserColor (float alpha)
{
	const char *s = vr_laser_color.string;
	int rgb[3] = { 255, 0, 0 };
	int i, value = 0;
	if (s && strlen (s) == 6)
	{
		for (i = 0; i < 6; ++i)
		{
			int digit;
			if (s[i] >= '0' && s[i] <= '9') digit = s[i] - '0';
			else if (s[i] >= 'a' && s[i] <= 'f') digit = s[i] - 'a' + 10;
			else if (s[i] >= 'A' && s[i] <= 'F') digit = s[i] - 'A' + 10;
			else return 0xff0000ff;
			value = (value << 4) | digit;
		}
		rgb[0] = (value >> 16) & 255;
		rgb[1] = (value >> 8) & 255;
		rgb[2] = value & 255;
	}
	return ((uint32_t)(CLAMP (0.f, alpha, 1.f) * 255.f) << 24) | ((uint32_t)rgb[2] << 16) | ((uint32_t)rgb[1] << 8) | (uint32_t)rgb[0];
}

static qboolean R_XRLaserHiddenForMelee (int weapon)
{
    return vr_laser_hide_melee.value != 0.f && (weapon == IT_AXE || weapon == RIT_AXE || weapon == HIT_MJOLNIR);
}

static qboolean R_XRGetVisualFireModel (iw_xr_hand_t hand, int weapon, const qmodel_t **model);

static void R_XRPrepareLasers (void)
{
    iw_xr_hand_t mainhand = XR_Input_PhysicalHandForRole (XR_HAND_MAINHAND);
    iw_xr_hand_t offhand = XR_Input_PhysicalHandForRole (XR_HAND_OFFHAND);
    int hand;
    if (!r_xr_eye_pass)
    {
        for (hand = 0; hand < IW_XR_HAND_COUNT; ++hand) r_xr_laser[hand].valid = false;
        return;
    }
    if (r_xr_eye_index != 0)
        return;
    for (hand = 0; hand < IW_XR_HAND_COUNT; ++hand) r_xr_laser[hand].valid = false;

        if (XR_Interaction_WheelActive () || (!vr_laser_sight.value && !vr_laser_beam.value)) return;
    /* Each laser uses its owning hand's logical weapon, never STAT_ACTIVEWEAPON globally. */
    for (hand = 0; hand < IW_XR_HAND_COUNT; ++hand)
    {
        int weapon = hand == mainhand ? XR_Interaction_MainhandWeaponItem () : (hand == offhand ? XR_Interaction_OffhandWeaponItem () : 0);
        xr_laser_t *laser = &r_xr_laser[hand];
        vec3_t target, right, up;
        uint32_t color;
        if (!weapon || R_XRLaserHiddenForMelee (weapon) ||
            (hand == mainhand ? !R_GetXRMainHandWeaponPose (laser->start, laser->forward, NULL, NULL) :
             !R_XRGetWeaponOrientation ((iw_xr_hand_t)hand, laser->start, laser->forward, right, up))) continue;
        VectorMA (laser->start, 8192.f, laser->forward, target);
        TraceLine (laser->start, target, laser->end);
        laser->valid = true;
        if (vr_laser_sight.value)
        {
            dlight_t *light = CL_AllocDlight (0x56524c53 + hand);
            color = R_XRLaserColor (1.f);
            VectorCopy (laser->end, light->origin);
            VectorSet (light->color, (color & 255) / 255.f, ((color >> 8) & 255) / 255.f, ((color >> 16) & 255) / 255.f);
            light->radius = 16.f; light->die = cl.time + 0.1; light->decay = 0.f;
        }
    }
}

static void R_XRUpdateLaserDots (void)
{
    int hand;
    for (hand = 0; hand < IW_XR_HAND_COUNT; ++hand)
    {
        xr_laser_t *laser = &r_xr_laser[hand];
        vec3_t origin;
        if (!laser->valid || !vr_laser_sight.value) R_SetVRLaserDot ((iw_xr_hand_t)hand, false, vec3_origin, 0, 0.f);
        else { VectorMA (laser->end, -0.5f, laser->forward, origin); R_SetVRLaserDot ((iw_xr_hand_t)hand, true, origin, R_XRLaserColor (vr_laser_alpha.value), CLAMP (0.1f, vr_laser_sight_scale.value, 20.f)); }
    }
}
static void R_XRDrawLaserBeam (void);

static qboolean R_XRIsProjectileModel (const entity_t *e)
{
	const char *name;
	if (!e || !e->model)
		return false;
	name = e->model->name;
	return !strcmp (name, "progs/missile.mdl") || !strcmp (name, "progs/grenade.mdl") ||
		!strcmp (name, "progs/spike.mdl") || !strcmp (name, "progs/s_spike.mdl") ||
		!strcmp (name, "progs/laser.mdl") || !strcmp (name, "progs/lasrspik.mdl") ||
		!strcmp (name, "progs/proxbomb.mdl") || !strcmp (name, "progs/spikmine.mdl") ||
		!strcmp (name, "progs/lspike.mdl") || !strcmp (name, "progs/plasma.mdl") ||
		!strcmp (name, "progs/hook.mdl") ||
		(e->model->flags & (EF_ROCKET | EF_GRENADE | EF_TRACER | EF_TRACER2 | EF_TRACER3)) ||
		VR_IsConfiguredProjectileModel(e->model);
}

static qboolean R_GetXRWeaponVisualOriginInternal (iw_xr_hand_t hand, const qmodel_t *model, vec3_t origin, qboolean fire_origin, qboolean fire2_origin)
{
	vec3_t hand_origin, forward, right, up, pack_offset, fire_offset;
	if (!origin || hand < 0 || hand >= IW_XR_HAND_COUNT)
		return false;
	if (!R_XRGetWeaponBasis (hand, hand_origin, forward, right, up))
		return false;
	VR_WeaponOffset (model, pack_offset);
	if (fire_origin)
		if (fire2_origin) VR_WeaponFire2Offset (model, fire_offset);
		else VR_WeaponFireOffset (model, fire_offset);
	VectorCopy (hand_origin, origin);
	VectorMA (origin, pack_offset[0], forward, origin);
	VectorMA (origin, pack_offset[1], right, origin);
	VectorMA (origin, pack_offset[2], up, origin);
	VectorMA (origin, vr_weapon_xoffset.value, forward, origin);
	VectorMA (origin, R_XRGlobalLateralOffset (hand, vr_weapon_yoffset.value), right, origin);
	VectorMA (origin, vr_weapon_zoffset.value, up, origin);
	if (fire_origin)
	{
		VectorMA (origin, fire_offset[0], forward, origin);
		VectorMA (origin, fire_offset[1], right, origin);
		VectorMA (origin, fire_offset[2], up, origin);
	}
	return true;
}

qboolean R_GetXRWeaponVisualOrigin (iw_xr_hand_t hand, const qmodel_t *model, vec3_t origin)
{
	return R_GetXRWeaponVisualOriginInternal (hand, model, origin, false, false);
}

qboolean R_GetXRWeaponVisualFireOrigin (iw_xr_hand_t hand, const qmodel_t *model, vec3_t origin)
{
	return R_GetXRWeaponVisualOriginInternal (hand, model, origin, true, false);
}

static qboolean R_GetXRWeaponVisualFire2Origin (iw_xr_hand_t hand, const qmodel_t *model, vec3_t origin)
{
	return R_GetXRWeaponVisualOriginInternal (hand, model, origin, true, true);
}

typedef struct
{
	const entity_t *entity;
	const qmodel_t *model;
	vec3_t origin;
	double expire;
	iw_xr_hand_t hand;
	int weapon;
	qboolean second_offset;
	qboolean valid;
} xr_projectile_visual_cache_t;

static xr_projectile_visual_cache_t r_xr_projectile_visual_cache[MAX_EDICTS];

void R_ClearXRProjectileVisualCache (void)
{
	memset (r_xr_projectile_visual_cache, 0, sizeof (r_xr_projectile_visual_cache));
}

void R_InvalidateXRProjectileVisualCache (const entity_t *e)
{
	int slot;
	if (!e || !cl_entities || cl_max_edicts <= 0)
		return;
	slot = (int)(e - cl_entities);
	if (slot >= 0 && slot < cl_max_edicts && slot < MAX_EDICTS)
		memset (&r_xr_projectile_visual_cache[slot], 0, sizeof (r_xr_projectile_visual_cache[slot]));
}

static qboolean R_XRGetVisualFireModel (iw_xr_hand_t hand, int weapon, const qmodel_t **model)
{
	entity_t viewmodel;
	if (!model)
		return false;
	if (weapon && XR_Interaction_GetFireViewmodel (weapon, &viewmodel))
	{
		*model = viewmodel.model;
		return *model != NULL;
	}
	if (hand == XR_Input_PhysicalHandForRole (XR_HAND_OFFHAND))
	{
		if (!XR_Interaction_GetOffhandViewmodel (&viewmodel))
			return false;
		*model = viewmodel.model;
	}
	else if (XR_Interaction_GetMainhandViewmodel (&viewmodel))
		*model = viewmodel.model;
	else
		*model = cl.viewent.model;
	return *model != NULL;
}

static qboolean R_XRGetProjectileVisualOrigin (iw_xr_hand_t hand, int weapon, qboolean second_offset, vec3_t origin)
{
	const qmodel_t *model;
	if (!R_XRGetVisualFireModel (hand, weapon, &model))
		return false;
	return second_offset ? R_GetXRWeaponVisualFire2Origin (hand, model, origin) : R_GetXRWeaponVisualFireOrigin (hand, model, origin);
}

void R_ApplyXRProjectileVisualOffset (const entity_t *e, vec3_t origin)
{
	vec3_t delta, offset, visual_origin;
	float distance, blend;
	int slot;
	iw_xr_hand_t hand;
	int weapon;
	qboolean new_projectile;
	qboolean second_offset;
	xr_projectile_visual_cache_t *cache;
	/* This cache only moves the rendered copy; authoritative entity origins stay unchanged. */
	if (!r_xr_eye_pass || !R_XRIsProjectileModel (e) || !origin || !cl_entities || cl_max_edicts <= 0)
		return;
	slot = (int)(e - cl_entities);
	if (slot < 0 || slot >= cl_max_edicts || slot >= MAX_EDICTS)
		return;
	cache = &r_xr_projectile_visual_cache[slot];
	VectorSubtract (origin, cl_entities[cl.viewentity].origin, delta);
	distance = VectorLength (delta);
	if (distance >= 128.f)
		return;
	new_projectile = cache->entity != e || cache->model != e->model;
	if (new_projectile)
	{
		memset (cache, 0, sizeof (*cache));
		if (!XR_Interaction_ConsumeLocalProjectileSpawn (slot, &hand, &weapon, &second_offset))
		{
			hand = XR_Input_PhysicalHandForRole (XR_HAND_MAINHAND);
			if (cl.maxclients > 1)
				XR_Interaction_GetVisualFireHand (&hand);
			weapon = hand == XR_Input_PhysicalHandForRole (XR_HAND_OFFHAND) ? XR_Interaction_OffhandWeaponItem () : XR_Interaction_MainhandWeaponItem ();
			second_offset = XR_Interaction_UseSecondVisualProjectileOffset (hand);
		}
		cache->entity = e;
		cache->model = e->model;
		cache->hand = hand;
		cache->weapon = weapon;
		cache->second_offset = second_offset;
	}
	if (!cache->valid || cl.time >= cache->expire)
	{
		if (!R_XRGetProjectileVisualOrigin (cache->hand, cache->weapon, cache->second_offset, visual_origin))
			return;
		VectorCopy (visual_origin, cache->origin);
		cache->expire = cl.time + 0.25;
		cache->valid = true;
	}
	blend = CLAMP (0.f, (128.f - distance) / 80.f, 1.f);
	VectorSubtract (cache->origin, origin, offset);
	VectorMA (origin, blend, offset, origin);
}

qboolean R_GetXRCanvasOffset (float *x, float *y)
{
	float left, right, bottom, top;
	if (!r_xr_asymmetric_projection)
		return false;
	left = tanf (r_xr_fov.left);
	right = tanf (r_xr_fov.right);
	bottom = tanf (r_xr_fov.down);
	top = tanf (r_xr_fov.up);
	*x = -(right + left) / (right - left);
	*y = -(top + bottom) / (top - bottom);
	return true;
}
float R_GetXRMessageOffset (void)
{
	float left, right;
	if (!r_xr_eye_pass || r_xr_ipd <= 0.f)
		return 0.f;
	left = tanf (r_xr_fov.left);
	 right = tanf (r_xr_fov.right);
	return (r_xr_eye_index == 0 ? 1.f : -1.f) * r_xr_ipd * 160.f / (1.0f * (right - left));
}



void R_XRRecenter (void)
{
	r_xr_head_anchor_valid = false;
	r_xr_sync_player_yaw = true;
	r_xr_recenter_to_head = true;
	r_xr_view_basis_valid = false;
}

void R_XRResync (void)
{
	r_xr_recenter_yaw = r_refdef.viewangles[YAW];
	r_xr_head_anchor_valid = false;
	r_xr_sync_player_yaw = true;
	r_xr_recenter_to_head = false;
	r_xr_view_basis_valid = false;
}

void R_XRAdjustYaw (float delta)
{
	cl.viewangles[YAW] += delta;
	if (r_xr_head_anchor_valid)
		r_xr_game_anchor_yaw += delta;
}
static void R_XRToQuakePosition (const float xr[3], float quake[3]);
static void R_XRRotateYaw (float vector[3], float yaw);
qboolean R_XRTransformRoomscaleDelta (const float xr[3], float quake[3])
{
    float yaw;
    if (!xr || !quake)
        return false;
    R_XRToQuakePosition (xr, quake);
    yaw = r_xr_head_anchor_valid ? r_xr_game_anchor_yaw - r_xr_head_anchor_yaw : cl.viewangles[YAW];
    R_XRRotateYaw (quake, yaw);
    return true;
}


static void R_XRToQuakePosition (const float xr[3], float quake[3]);

static void R_XRRotateVector (const iw_xr_view_t *view, const float in[3], float out[3])
{
	float x = view->orientation[0], y = view->orientation[1];
	float z = view->orientation[2], w = view->orientation[3];
	float tx = 2.f * (y * in[2] - z * in[1]);
	float ty = 2.f * (z * in[0] - x * in[2]);
	float tz = 2.f * (x * in[1] - y * in[0]);

	out[0] = in[0] + w * tx + (y * tz - z * ty);
	out[1] = in[1] + w * ty + (z * tx - x * tz);
	out[2] = in[2] + w * tz + (x * ty - y * tx);
}

static void R_XRNormalizeAngles (float *pitch, float *yaw, float *roll)
{
	while (*pitch >= 90.f) *pitch -= 180.f;
	while (*pitch < -90.f) *pitch += 180.f;
	while (*yaw >= 180.f) *yaw -= 360.f;
	while (*yaw < -180.f) *yaw += 360.f;
	while (*roll >= 180.f) *roll -= 360.f;
	while (*roll < -180.f) *roll += 360.f;
}

static void R_XRHeadAngles (const iw_xr_view_t *view, float *pitch, float *yaw, float *roll)
{
	const float xr_forward[3] = { 0.f, 0.f, -1.f };
	const float xr_right[3] = { 1.f, 0.f, 0.f };
	const float xr_up[3] = { 0.f, 1.f, 0.f };
	float forward_xr[3], right_xr[3], up_xr[3];
	float forward[3], right[3], up[3];
	float sp, cp, sy, cy, sr, cr;

	R_XRRotateVector (view, xr_forward, forward_xr);
	R_XRRotateVector (view, xr_right, right_xr);
	R_XRRotateVector (view, xr_up, up_xr);
	R_XRToQuakePosition (forward_xr, forward);
	R_XRToQuakePosition (right_xr, right);
	R_XRToQuakePosition (up_xr, up);

	sp = -forward[2];
	sy = forward[1];
	cy = forward[0];
	sr = -right[2];
	cr = up[2];
	if (fabsf (cy) > 0.001f)
		cp = forward[0] / cy;
	else if (fabsf (sy) > 0.001f)
		cp = forward[1] / sy;
	else if (fabsf (sr) > 0.001f)
		cp = -right[2] / sr;
	else if (fabsf (cr) > 0.001f)
		cp = up[2] / cr;
	else
		cp = cosf (asinf (sp));

	*pitch = RAD2DEG (atan2f (sp, cp));
	*yaw = RAD2DEG (atan2f (sy, cy));
	*roll = RAD2DEG (atan2f (sr, cr));
	R_XRNormalizeAngles (pitch, yaw, roll);
}
static void R_XRToQuakePosition (const float xr[3], float quake[3])
{
	float x = xr[0], y = xr[1], z = xr[2];
	quake[0] = -z;
	quake[1] = -x;
	quake[2] = y;
}

static void R_XRRotateYaw (float vector[3], float yaw)
{
	float radians = DEG2RAD (yaw);
	float x = vector[0], y = vector[1];
	vector[0] = x * cosf (radians) - y * sinf (radians);
	vector[1] = x * sinf (radians) + y * cosf (radians);
}

static void R_SetXRProjection (float *matrix, float znear, float zfar)
{
	float left = tanf (r_xr_fov.left) * znear;
	float right = tanf (r_xr_fov.right) * znear;
	float bottom = tanf (r_xr_fov.down) * znear;
	float top = tanf (r_xr_fov.up) * znear;
	float xscale = 2.f * znear / (right - left);
	float yscale = 2.f * znear / (top - bottom);
	float xoffset = (right + left) / (right - left);
	float yoffset = (top + bottom) / (top - bottom);
	float zscale, ztranslate;

	if (gl_clipcontrol_able)
	{
		zscale = znear / (zfar - znear);
		ztranslate = zfar * znear / (zfar - znear);
	}
	else
	{
		zscale = -(zfar + znear) / (zfar - znear);
		ztranslate = -2.f * zfar * znear / (zfar - znear);
	}

	memset (matrix, 0, 16 * sizeof (*matrix));
	matrix[0] = -xoffset;
	matrix[1] = -yoffset;
	matrix[2] = -zscale;
	matrix[3] = 1.f;
	matrix[4] = -xscale;
	matrix[9] = yscale;
	matrix[14] = ztranslate;
}
mleaf_t		*r_viewleaf, *r_oldviewleaf;

int		d_lightstylevalue[256];	// 8.8 fraction of base light value


cvar_t	r_norefresh = {"r_norefresh","0",CVAR_NONE};
cvar_t	r_drawentities = {"r_drawentities","1",CVAR_NONE};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1",CVAR_NONE};
cvar_t	r_speeds = {"r_speeds","0",CVAR_NONE};
#if defined(ANDROID_GLES3)
cvar_t	r_gles_perf_capture = {"r_gles_perf_capture","0",CVAR_NONE};
cvar_t	r_gles_vao_validate = {"r_gles_vao_validate","0",CVAR_NONE};
cvar_t	r_gles_static_vao = {"r_gles_static_vao","1",CVAR_NONE};
cvar_t	r_gles_ubo_validate = {"r_gles_ubo_validate","0",CVAR_NONE};
cvar_t	r_gles_world_batch = {"r_gles_world_batch","1",CVAR_NONE};
cvar_t	r_gles_dlight_max = {"r_gles_dlight_max","0",CVAR_ARCHIVE};
cvar_t	r_gles_dlight_test_count = {"r_gles_dlight_test_count","0",CVAR_NONE};
#endif
cvar_t	r_pos = {"r_pos","0",CVAR_NONE};
cvar_t	r_fullbright = {"r_fullbright","0",CVAR_NONE};
cvar_t	r_lightmap = {"r_lightmap","0",CVAR_NONE};
cvar_t	r_wateralpha = {"r_wateralpha","1",CVAR_ARCHIVE};
cvar_t	r_litwater = {"r_litwater","1",CVAR_NONE};
cvar_t	r_dynamic = {"r_dynamic","1",CVAR_ARCHIVE};
cvar_t	r_novis = {"r_novis","0",CVAR_ARCHIVE};
#if defined(USE_SIMD)
cvar_t	r_simd = {"r_simd","1",CVAR_ARCHIVE};
#endif
cvar_t	r_alphasort = {"r_alphasort","1",CVAR_ARCHIVE};
#if defined(ANDROID_GLES3)
cvar_t	r_oit = {"r_oit","0",CVAR_ARCHIVE};
#else
cvar_t	r_oit = {"r_oit","1",CVAR_ARCHIVE};
#endif
cvar_t	r_dither = {"r_dither", "1.0", CVAR_ARCHIVE};

cvar_t	gl_finish = {"gl_finish","0",CVAR_NONE};
cvar_t	gl_clear = {"gl_clear","1",CVAR_NONE};
cvar_t	gl_polyblend = {"gl_polyblend","1",CVAR_NONE};
cvar_t	gl_playermip = {"gl_playermip","0",CVAR_NONE};
cvar_t	gl_nocolors = {"gl_nocolors","0",CVAR_NONE};

//johnfitz -- new cvars
cvar_t	r_clearcolor = {"r_clearcolor","2",CVAR_ARCHIVE};
cvar_t	r_flatlightstyles = {"r_flatlightstyles", "0", CVAR_NONE};
cvar_t	r_lerplightstyles = {"r_lerplightstyles", "1", CVAR_ARCHIVE}; // 0=off; 1=skip abrupt transitions; 2=always lerp
cvar_t	gl_fullbrights = {"gl_fullbrights", "1", CVAR_ARCHIVE};
cvar_t	gl_farclip = {"gl_farclip", "65536", CVAR_ARCHIVE};
cvar_t	gl_overbright_models = {"gl_overbright_models", "1", CVAR_ARCHIVE};
cvar_t	r_oldskyleaf = {"r_oldskyleaf", "0", CVAR_NONE};
cvar_t	r_drawworld = {"r_drawworld", "1", CVAR_NONE};
cvar_t	r_showtris = {"r_showtris", "0", CVAR_NONE};
cvar_t	r_showbboxes = {"r_showbboxes", "0", CVAR_NONE};
cvar_t	r_showbboxes_think = {"r_showbboxes_think", "0", CVAR_NONE}; // 0=show all; 1=thinkers only; -1=non-thinkers only
cvar_t	r_showbboxes_health = {"r_showbboxes_health", "0", CVAR_NONE}; // 0=show all; 1=healthy only; -1=non-healthy only
cvar_t	r_showbboxes_links = {"r_showbboxes_links", "3", CVAR_NONE}; // 0=off; 1=outgoing only; 2=incoming only; 3=incoming+outgoing
cvar_t	r_showbboxes_targets = {"r_showbboxes_targets", "1", CVAR_NONE};
cvar_t	r_showfields = {"r_showfields", "0", CVAR_NONE};
cvar_t	r_showfields_align = {"r_showfields_align", "1", CVAR_ARCHIVE}; // 0=entity pos; 1=bottom-right
cvar_t	r_lerpmodels = {"r_lerpmodels", "1", CVAR_ARCHIVE};
cvar_t	r_lerpmove = {"r_lerpmove", "1", CVAR_ARCHIVE};
cvar_t	r_nolerp_list = {"r_nolerp_list", "progs/flame.mdl,progs/flame2.mdl,progs/braztall.mdl,progs/brazshrt.mdl,progs/longtrch.mdl,progs/flame_pyre.mdl,progs/v_saw.mdl,progs/v_xfist.mdl,progs/h2stuff/newfire.mdl", CVAR_NONE};
cvar_t	r_noshadow_list = {"r_noshadow_list", "progs/flame2.mdl,progs/flame.mdl,progs/bolt1.mdl,progs/bolt2.mdl,progs/bolt3.mdl,progs/laser.mdl", CVAR_NONE};
cvar_t	r_showskel = {"r_showskel", "0", CVAR_NONE};

extern cvar_t	r_vfog;
extern cvar_t	vid_fsaa;
//johnfitz
extern cvar_t	r_softemu_dither_screen;
extern cvar_t	r_softemu_dither_texture;

cvar_t	gl_zfix = {"gl_zfix", "1", CVAR_ARCHIVE}; // QuakeSpasm z-fighting fix

cvar_t	r_lavaalpha = {"r_lavaalpha","0",CVAR_NONE};
cvar_t	r_telealpha = {"r_telealpha","0",CVAR_NONE};
cvar_t	r_slimealpha = {"r_slimealpha","0",CVAR_NONE};

float	map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha;
float	map_fallbackalpha;

qboolean r_fullbright_cheatsafe, r_lightmap_cheatsafe, r_drawworld_cheatsafe; //johnfitz

cvar_t	r_scale = {"r_scale", "1", CVAR_ARCHIVE};

//==============================================================================
//
// FRAMEBUFFERS
//
//==============================================================================

glframebufs_t framebufs;
static int r_framebuffer_width;
static int r_framebuffer_height;

/*
=============
GL_CreateFBOAttachment
=============
*/
static GLuint GL_CreateFBOAttachment (GLenum format, int samples, GLenum filter, const char *name)
{
	GLenum target = samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
	GLuint texnum;

	glGenTextures (1, &texnum);
	GL_BindNative (GL_TEXTURE0, target, texnum);
	if (GL_ObjectLabelFunc)
	GL_ObjectLabelFunc (GL_TEXTURE, texnum, -1, name);
	if (samples > 1)
	{
		GL_TexStorage2DMultisampleFunc (target, samples, format, r_framebuffer_width, r_framebuffer_height, GL_FALSE);
	}
	else
	{
		GL_TexStorage2DFunc (target, 1, format, r_framebuffer_width, r_framebuffer_height);
		glTexParameteri (target, GL_TEXTURE_MAG_FILTER, filter);
		glTexParameteri (target, GL_TEXTURE_MIN_FILTER, filter);
	}
	glTexParameteri (target, GL_TEXTURE_MAX_LEVEL, 0);

	return texnum;
}

/*
=============
GL_CreateFBO
=============
*/
static GLuint GL_CreateFBO (GLenum target, const GLuint *colors, int numcolors, GLuint depth, GLuint stencil, const char *name)
{
	GLenum status;
	GLuint fbo;
	GLenum buffers[8];
	int i;

	if (numcolors > (int)countof (buffers))
		Sys_Error ("GL_CreateFBO: too many color buffers (%d)", numcolors);

	GL_GenFramebuffersFunc (1, &fbo);
	GL_BindFramebufferFunc (GL_FRAMEBUFFER, fbo);
	if (GL_ObjectLabelFunc)
	GL_ObjectLabelFunc (GL_FRAMEBUFFER, fbo, -1, name);

	for (i = 0; i < numcolors; i++)
	{
		GL_FramebufferTexture2DFunc (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, target, colors[i], 0);
		buffers[i] = GL_COLOR_ATTACHMENT0 + i;
	}
	GL_DrawBuffersFunc (numcolors, buffers);
	if (depth && depth == stencil)
		GL_FramebufferTexture2DFunc (GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, target, depth, 0);
	else
	{
		if (depth)
			GL_FramebufferTexture2DFunc (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, depth, 0);
		if (stencil)
			GL_FramebufferTexture2DFunc (GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, target, stencil, 0);
	}
	status = GL_CheckFramebufferStatusFunc (GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
		Sys_Error ("Failed to create %s (status code 0x%X)", name, status);

	return fbo;
}

/*
=============
GL_CreateSimpleFBO
=============
*/
static GLuint GL_CreateSimpleFBO (GLenum target, GLuint colors, GLuint depth, GLuint stencil, const char *name)
{
	return GL_CreateFBO (target, colors ? &colors : NULL, colors ? 1 : 0, depth, stencil, name);
}

/*
=============
GL_CreateFrameBuffers
=============
*/
void GL_CreateFrameBuffers (void)
{
	if (!r_framebuffer_width || !r_framebuffer_height)
	{
		r_framebuffer_width = vid.width;
		r_framebuffer_height = vid.height;
	}
	#if defined(ANDROID_GLES3)
	GLenum color_format = GL_RGBA8;
#else
	GLenum color_format = GL_RGB10_A2;
#endif
	#if defined(ANDROID_GLES3)
	GLenum depth_format = GL_DEPTH_COMPONENT24;
	Con_Printf ("GLES scene targets: color=RGBA8 depth=DEPTH_COMPONENT24 samples=1 stencil=0 alpha=sorted\n");
#else
	GLenum depth_format = GL_DEPTH24_STENCIL8;
#endif

#if defined(ANDROID_GLES3)
	GLuint depth_attachment = 1;
	GLuint stencil_attachment = 0;
#else
	GLuint depth_attachment = 1;
	GLuint stencil_attachment = 1;
#endif
	/* query MSAA limits */
	glGetIntegerv (GL_MAX_COLOR_TEXTURE_SAMPLES, &framebufs.max_color_tex_samples);
	glGetIntegerv (GL_MAX_DEPTH_TEXTURE_SAMPLES, &framebufs.max_depth_tex_samples);
	framebufs.max_samples = q_min (framebufs.max_color_tex_samples, framebufs.max_depth_tex_samples);

	/* main framebuffer (color + depth + stencil) */
	framebufs.composite.color_tex = GL_CreateFBOAttachment (color_format, 1, GL_NEAREST, "composite colors");
	framebufs.composite.depth_stencil_tex = GL_CreateFBOAttachment (depth_format, 1, GL_NEAREST, "composite depth/stencil");
	framebufs.composite.fbo = GL_CreateSimpleFBO (GL_TEXTURE_2D,
		framebufs.composite.color_tex,
		depth_attachment ? framebufs.composite.depth_stencil_tex : 0,
		stencil_attachment ? framebufs.composite.depth_stencil_tex : 0,
		"composite fbo"
	);

	/* scene framebuffer (color + depth + stencil, potentially multisampled) */
	#if defined(ANDROID_GLES3)
	framebufs.scene.samples = 1;
	#else
	framebufs.scene.samples = Q_nextPow2 ((int) q_max (1.f, vid_fsaa.value));
	framebufs.scene.samples = CLAMP (1, framebufs.scene.samples, framebufs.max_samples);
	#endif

	framebufs.scene.color_tex = GL_CreateFBOAttachment (color_format, framebufs.scene.samples, GL_NEAREST, "scene colors");
	framebufs.scene.depth_stencil_tex = GL_CreateFBOAttachment (depth_format, framebufs.scene.samples, GL_NEAREST, "scene depth/stencil");
	framebufs.scene.fbo = GL_CreateSimpleFBO (framebufs.scene.samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D,
		framebufs.scene.color_tex,
		depth_attachment ? framebufs.scene.depth_stencil_tex : 0,
		stencil_attachment ? framebufs.scene.depth_stencil_tex : 0,
		"scene fbo"
	);

	/* weighted blended order-independent transparency (accum + revealage, potentially multisampled */
	framebufs.oit.accum_tex = GL_CreateFBOAttachment (GL_RGBA16F, framebufs.scene.samples, GL_NEAREST, "oit accum");
	framebufs.oit.revealage_tex = GL_CreateFBOAttachment (GL_R8, framebufs.scene.samples, GL_NEAREST, "oit revealage");
	framebufs.oit.fbo_scene = GL_CreateFBO (framebufs.scene.samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D,
		framebufs.oit.mrt, 2,
		depth_attachment ? framebufs.scene.depth_stencil_tex : 0,
		stencil_attachment ? framebufs.scene.depth_stencil_tex : 0,
		"oit scene fbo"
	);

	/* resolved scene framebuffer (color only) */
	if (framebufs.scene.samples > 1)
	{
		framebufs.resolved_scene.color_tex = GL_CreateFBOAttachment (color_format, 1, GL_NEAREST, "resolved scene colors");
		framebufs.resolved_scene.fbo = GL_CreateSimpleFBO (GL_TEXTURE_2D, framebufs.resolved_scene.color_tex, 0, 0, "resolved scene fbo");
	}
	else
	{
		framebufs.resolved_scene.color_tex = 0;
		framebufs.resolved_scene.fbo = 0;

		framebufs.oit.fbo_composite = GL_CreateFBO (GL_TEXTURE_2D,
			framebufs.oit.mrt, 2,
			depth_attachment ? framebufs.composite.depth_stencil_tex : 0,
			stencil_attachment ? framebufs.composite.depth_stencil_tex : 0,
			"oit composite fbo"
		);
	}

	GL_BindFramebufferFunc (GL_FRAMEBUFFER, 0);
	GL_BindNative (GL_TEXTURE0, GL_TEXTURE_2D, 0);
}

/*
=============
GL_DeleteFrameBuffers
=============
*/
void GL_SetFrameBufferSize (int width, int height)
{
	if (width <= 0 || height <= 0 || (r_framebuffer_width == width && r_framebuffer_height == height))
		return;

	GL_DeleteFrameBuffers ();
	r_framebuffer_width = width;
	r_framebuffer_height = height;
	GL_CreateFrameBuffers ();
}
void GL_DeleteFrameBuffers (void)
{
	GL_DeleteFramebuffersFunc (1, &framebufs.resolved_scene.fbo);
	GL_DeleteFramebuffersFunc (1, &framebufs.oit.fbo_composite);
	GL_DeleteFramebuffersFunc (1, &framebufs.oit.fbo_scene);
	GL_DeleteFramebuffersFunc (1, &framebufs.scene.fbo);
	GL_DeleteFramebuffersFunc (1, &framebufs.composite.fbo);
	GL_BindFramebufferFunc (GL_FRAMEBUFFER, 0);

	GL_DeleteNativeTexture (framebufs.resolved_scene.color_tex);
	GL_DeleteNativeTexture (framebufs.oit.revealage_tex);
	GL_DeleteNativeTexture (framebufs.oit.accum_tex);
	GL_DeleteNativeTexture (framebufs.scene.depth_stencil_tex);
	GL_DeleteNativeTexture (framebufs.scene.color_tex);
	GL_DeleteNativeTexture (framebufs.composite.depth_stencil_tex);
	GL_DeleteNativeTexture (framebufs.composite.color_tex);

	memset (&framebufs, 0, sizeof (framebufs));
}

//==============================================================================
//
// POSTPROCESSING
//
//==============================================================================

static const float NOISESCALE = 9.f / 255.f;

extern GLuint gl_palette_lut;
extern GLuint gl_palette_buffer[2];

/*
=============
GL_PostProcess
=============
*/
void GL_PostProcess (void)
{
	int palidx, variant;
	float dither;
	if (!GL_NeedsPostprocess ())
		return;

	GL_BeginGroup ("Postprocess");

	palidx =  GLPalette_Postprocess ();
	dither = (softemu == SOFTEMU_FINE) ? NOISESCALE * r_dither.value * r_softemu_dither_screen.value : 0.f;

	GL_BindFramebufferFunc (GL_FRAMEBUFFER, r_xr_final_fbo);
	glViewport (r_xr_final_fbo ? 0 : glx, r_xr_final_fbo ? 0 : gly,
		r_xr_final_fbo ? r_xr_final_width : glwidth, r_xr_final_fbo ? r_xr_final_height : glheight);

	variant = q_min ((int)softemu, 2);
	GL_UseProgram (glprogs.postprocess[variant]);
	GL_SetState (GLS_BLEND_OPAQUE | GLS_NO_ZTEST | GLS_NO_ZWRITE | GLS_CULL_NONE | GLS_ATTRIBS(0));
	GL_BindNative (GL_TEXTURE0, GL_TEXTURE_2D, framebufs.composite.color_tex);
	GL_BindNative (GL_TEXTURE1, GL_TEXTURE_3D, gl_palette_lut);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 0, gl_palette_buffer[palidx], 0, 256 * sizeof (GLuint));
	if (variant != 2) // some AMD drivers optimize out the uniform in variant #2
		GL_Uniform4fFunc (0, vid_gamma.value, q_min(2.0f, q_max(1.0f, vid_contrast.value)), 1.f/r_refdef.scale, dither);

	GL_PerfCountDraws (1);

	glDrawArrays (GL_TRIANGLES, 0, 3);

	GL_EndGroup ();
}

/*
=================
R_CullBox -- johnfitz -- replaced with new function from lordhavoc

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox (vec3_t emins, vec3_t emaxs)
{
	if (VID_XR_UsingMultiview ())
		return false;
	int i;
	mplane_t *p;
	byte signbits;
	float vec[3];
	for (i = 0;i < 4;i++)
	{
		p = frustum + i;
		signbits = p->signbits;
		vec[0] = ((signbits & 1) ? emins : emaxs)[0];
		vec[1] = ((signbits & 2) ? emins : emaxs)[1];
		vec[2] = ((signbits & 4) ? emins : emaxs)[2];
		if (p->normal[0]*vec[0] + p->normal[1]*vec[1] + p->normal[2]*vec[2] < p->dist)
			return true;
	}
	return false;
}

/*
===============
R_GetEntityBounds -- johnfitz -- uses correct bounds based on rotation
===============
*/
void R_GetEntityBounds (const entity_t *e, vec3_t mins, vec3_t maxs)
{
	vec_t scalefactor, *minbounds, *maxbounds;

	if (e->angles[0] || e->angles[2]) //pitch or roll
	{
		minbounds = e->model->rmins;
		maxbounds = e->model->rmaxs;
	}
	else if (e->angles[1]) //yaw
	{
		minbounds = e->model->ymins;
		maxbounds = e->model->ymaxs;
	}
	else //no rotation
	{
		minbounds = e->model->mins;
		maxbounds = e->model->maxs;
	}

	scalefactor = ENTSCALE_DECODE(e->scale);
	if (scalefactor != 1.0f)
	{
		VectorMA (e->origin, scalefactor, minbounds, mins);
		VectorMA (e->origin, scalefactor, maxbounds, maxs);
	}
	else
	{
		VectorAdd (e->origin, minbounds, mins);
		VectorAdd (e->origin, maxbounds, maxs);
	}
}

/*
===============
R_CullModelForEntity -- johnfitz -- uses correct bounds based on rotation
===============
*/
qboolean R_CullModelForEntity (entity_t *e)
{
	vec3_t mins, maxs;

	R_GetEntityBounds (e, mins, maxs);

	return R_CullBox (mins, maxs);
}

/*
===============
R_EntityMatrix
===============
*/
void R_EntityMatrix (float matrix[16], vec3_t origin, vec3_t angles, unsigned char scale)
{
	float scalefactor	= ENTSCALE_DECODE(scale);
	float yaw			= DEG2RAD(angles[YAW]);
	float pitch			= angles[PITCH];
	float roll			= angles[ROLL];
	if (pitch == 0.f && roll == 0.f)
	{
		float sy = sin(yaw) * scalefactor;
		float cy = cos(yaw) * scalefactor;

		// First column
		matrix[ 0] = cy;
		matrix[ 1] = sy;
		matrix[ 2] = 0.f;
		matrix[ 3] = 0.f;

		// Second column
		matrix[ 4] = -sy;
		matrix[ 5] = cy;
		matrix[ 6] = 0.f;
		matrix[ 7] = 0.f;

		// Third column
		matrix[ 8] = 0.f;
		matrix[ 9] = 0.f;
		matrix[10] = scalefactor;
		matrix[11] = 0.f;
	}
	else
	{
		float sy, sp, sr, cy, cp, cr;
		pitch = DEG2RAD(pitch);
		roll = DEG2RAD(roll);
		sy = sin(yaw);
		sp = sin(pitch);
		sr = sin(roll);
		cy = cos(yaw);
		cp = cos(pitch);
		cr = cos(roll);

		// https://www.symbolab.com/solver/matrix-multiply-calculator FTW!

		// First column
		matrix[ 0] = scalefactor * cy*cp;
		matrix[ 1] = scalefactor * sy*cp;
		matrix[ 2] = scalefactor * sp;
		matrix[ 3] = 0.f;

		// Second column
		matrix[ 4] = scalefactor * (-cy*sp*sr - cr*sy);
		matrix[ 5] = scalefactor * (cr*cy - sy*sp*sr);
		matrix[ 6] = scalefactor * cp*sr;
		matrix[ 7] = 0.f;

		// Third column
		matrix[ 8] = scalefactor * (sy*sr - cr*cy*sp);
		matrix[ 9] = scalefactor * (-cy*sr - cr*sy*sp);
		matrix[10] = scalefactor * cr*cp;
		matrix[11] = 0.f;
	}

	// Fourth column
	matrix[12] = origin[0];
	matrix[13] = origin[1];
	matrix[14] = origin[2];
	matrix[15] = 1.f;
}

/*
=============
GL_PolygonOffset -- johnfitz

negative offset moves polygon closer to camera
=============
*/
void GL_PolygonOffset (int offset)
{
#if defined(ANDROID_GLES3)
	if (offset != 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glPolygonOffset (offset > 0 ? 1.f : -1.f, (GLfloat)offset);
	}
	else
		glDisable (GL_POLYGON_OFFSET_FILL);
	return;
#else
	if (gl_clipcontrol_able)
		offset = -offset;

	if (offset > 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glEnable (GL_POLYGON_OFFSET_LINE);
		glPolygonOffset(1, offset);
	}
	else if (offset < 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glEnable (GL_POLYGON_OFFSET_LINE);
		glPolygonOffset(-1, offset);
	}
	else
	{
		glDisable (GL_POLYGON_OFFSET_FILL);
		glDisable (GL_POLYGON_OFFSET_LINE);
	}
#endif
}

/*
=============
GL_DepthRange

Wrapper around glDepthRange that handles clip control/reversed Z differences
=============
*/
void GL_DepthRange (zrange_t range)
{
	switch (range)
	{
	default:
	case ZRANGE_FULL:
		glDepthRange (0.f, 1.f);
		break;

	case ZRANGE_VIEWMODEL:
		if (gl_clipcontrol_able)
			glDepthRange (0.7f, 1.f);
		else
			glDepthRange (0.f, 0.3f);
		break;

	case ZRANGE_NEAR:
		if (gl_clipcontrol_able)
			glDepthRange (1.f, 1.f);
		else
			glDepthRange (0.f, 0.f);
		break;
	}
}

/*
=============
R_GetAlphaMode
=============
*/
alphamode_t R_GetAlphaMode (void)
{
#if defined(ANDROID_GLES3)
	return r_alphasort.value ? ALPHAMODE_SORTED : ALPHAMODE_BASIC;
#else
	if (r_oit.value)
		return ALPHAMODE_OIT;
	return r_alphasort.value ? ALPHAMODE_SORTED : ALPHAMODE_BASIC;
#endif
}
/*
=============
R_GetEffectiveAlphaMode
=============
*/
alphamode_t R_GetEffectiveAlphaMode (void)
{
#if defined(ANDROID_GLES3)
	return r_alphasort.value ? ALPHAMODE_SORTED : ALPHAMODE_BASIC;
#endif
	if (VID_XR_UsingMultiview () || map_checks.value)
		return ALPHAMODE_BASIC;
	return R_GetAlphaMode ();
}

/*
=============
R_SetAlphaMode
=============
*/
void R_SetAlphaMode (alphamode_t mode)
{
	Cvar_SetValueQuick (&r_oit, mode == ALPHAMODE_OIT);
	if (mode != ALPHAMODE_OIT)
		Cvar_SetValueQuick (&r_alphasort, mode == ALPHAMODE_SORTED);
}


//==============================================================================
//
// SETUP FRAME
//
//==============================================================================

static uint32_t visedict_keys[MAX_VISEDICTS];
static uint16_t visedict_order[2][MAX_VISEDICTS];
static entity_t *cl_sorted_visedicts[MAX_VISEDICTS + 1]; // +1 for worldspawn
static int cl_modtype_ofs[mod_numtypes*2 + 1]; // x2: opaque/translucent; +1: total in last slot

typedef struct framesetup_s
{
	GLuint		scene_fbo;
	GLuint		oit_fbo;
} framesetup_t;

static framesetup_t framesetup;

/*
=============
R_SortEntities
=============
*/
static void R_SortEntities (void)
{
	int i, j, pass;
	int bins[1 << (MODSORT_BITS/2)];
	int typebins[mod_numtypes*2];
	alphamode_t alphamode = R_GetEffectiveAlphaMode ();

	if (!r_drawentities.value)
		cl_numvisedicts = 0;

	// remove entities with no or invisible models
	for (i = 0, j = 0; i < cl_numvisedicts; i++)
	{
		entity_t *ent = cl_visedicts[i];
		if (!ent->model || ent->alpha == ENTALPHA_ZERO)
			continue;
		if (ent->model->type == mod_brush && R_CullModelForEntity (ent))
			continue;
		cl_visedicts[j++] = ent;
	}
	cl_numvisedicts = j;

	memset (typebins, 0, sizeof(typebins));
	if (r_drawworld.value)
		typebins[mod_brush * 2 + 0]++; // count worldspawn

	// fill entity sort key array, initial order, and per-type counts
	for (i = 0; i < cl_numvisedicts; i++)
	{
		entity_t *ent = cl_visedicts[i];
		qboolean translucent = !ENTALPHA_OPAQUE (ent->alpha);

		if (translucent && alphamode == ALPHAMODE_SORTED)
		{
			float dist, delta;
			vec3_t mins, maxs;

			R_GetEntityBounds (ent, mins, maxs);
			for (j = 0, dist = 0.f; j < 3; j++)
			{
				delta = CLAMP (mins[j], r_refdef.vieworg[j], maxs[j]) - r_refdef.vieworg[j];
				dist += delta * delta;
			}
			dist = sqrt (dist);
			visedict_keys[i] = ~CLAMP (0, (int)dist, MODSORT_MASK);
		}
		else if (translucent && alphamode != ALPHAMODE_OIT)
		{
			// Note: -1 (0xfffff) for non-static entities (firstleaf=0),
			// so they are sorted after static ones
			visedict_keys[i] = ent->firstleaf - 1;
		}
		else
		{
			if (ent->model->type == mod_alias)
				visedict_keys[i] = ent->model->sortkey | (ent->skinnum & MODSORT_FRAMEMASK);
			else
				visedict_keys[i] = ent->model->sortkey | (ent->frame & MODSORT_FRAMEMASK);
		}

		if ((unsigned)ent->model->type >= (unsigned)mod_numtypes)
			Sys_Error ("Model '%s' has invalid type %d", ent->model->name, ent->model->type);
		typebins[ent->model->type * 2 + translucent]++;

		visedict_order[0][i] = i;
	}

	// convert typebin counts into offsets
	for (i = 0, j = 0; i < countof(typebins); i++)
	{
		int tmp = typebins[i];
		cl_modtype_ofs[i] = typebins[i] = j;
		j += tmp;
	}
	cl_modtype_ofs[i] = j;

	// LSD-first radix sort: 2 passes x MODSORT_BITS/2 bits
	for (pass = 0; pass < 2; pass++)
	{
		uint16_t *src = visedict_order[pass];
		uint16_t *dst = visedict_order[pass ^ 1];
		const int mask = countof (bins) - 1;
		int shift = pass * (MODSORT_BITS/2);
		int sum;

		// count number of entries in each bin
		memset (bins, 0, sizeof(bins));
		for (i = 0; i < cl_numvisedicts; i++)
			bins[(visedict_keys[i] >> shift) & mask]++;

		// turn bin counts into offsets
		sum = 0;
		for (i = 0; i < countof (bins); i++)
		{
			int tmp = bins[i];
			bins[i] = sum;
			sum += tmp;
		}

		// reorder
		for (i = 0; i < cl_numvisedicts; i++)
			dst[bins[(visedict_keys[src[i]] >> shift) & mask]++] = src[i];
	}

	// write sorted list
	if (r_drawworld.value)
		cl_sorted_visedicts[typebins[mod_brush * 2 + 0]++] = &cl_entities[0]; // add the world as the first brush entity
	for (i = 0; i < cl_numvisedicts; i++)
	{
		entity_t *ent = cl_visedicts[visedict_order[0][i]];
		qboolean translucent = !ENTALPHA_OPAQUE (ent->alpha);
		cl_sorted_visedicts[typebins[ent->model->type * 2 + translucent]++] = ent;
	}
}

int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}

/*
=============
GL_FrustumMatrix
=============
*/
static void GL_FrustumMatrix(float matrix[16], float fovx, float fovy, float n, float f)
{
	const float w = 1.0f / tanf(fovx * 0.5f);
	const float h = 1.0f / tanf(fovy * 0.5f);

	memset(matrix, 0, 16 * sizeof(float));

	if (gl_clipcontrol_able)
	{
		// reversed Z projection matrix with the coordinate system conversion baked in
		matrix[0*4 + 2] = -n / (f - n);
		matrix[0*4 + 3] = 1.f;
		matrix[1*4 + 0] = -w;
		matrix[2*4 + 1] = h;
		matrix[3*4 + 2] = f * n / (f - n);
	}
	else
	{
		// standard projection matrix with the coordinate system conversion baked in
		matrix[0*4 + 2] = (f + n) / (f - n);
		matrix[0*4 + 3] = 1.f;
		matrix[1*4 + 0] = -w;
		matrix[2*4 + 1] = h;
		matrix[3*4 + 2] = -2.f * f * n / (f - n);
	}
}

/*
===============
ExtractFrustumPlane

Extracts the normalized frustum plane from the given view-projection matrix
that corresponds to a value of 'ndcval' on the 'axis' axis in NDC space.
===============
*/
void ExtractFrustumPlane (float mvp[16], int axis, float ndcval, qboolean flip, mplane_t *out)
{
	float scale;
	out->normal[0] =  (mvp[0*4 + axis] - ndcval * mvp[0*4 + 3]);
	out->normal[1] =  (mvp[1*4 + axis] - ndcval * mvp[1*4 + 3]);
	out->normal[2] =  (mvp[2*4 + axis] - ndcval * mvp[2*4 + 3]);
	out->dist      = -(mvp[3*4 + axis] - ndcval * mvp[3*4 + 3]);

	scale = (flip ? -1.f : 1.f) / sqrtf (DotProduct (out->normal, out->normal));
	out->normal[0] *= scale;
	out->normal[1] *= scale;
	out->normal[2] *= scale;
	out->dist      *= scale;

	out->type      = PLANE_ANYZ;
	out->signbits  = SignbitsForPlane (out);
}

/*
===============
R_SetFrustum
===============
*/
void R_SetFrustum (void)
{
	float w, h, d;
	float znear, zfar;
	float logznear, logzfar;
	float translation[16];
	float rotation[16];

	// reduce near clip distance at high FOV's to avoid seeing through walls
	w = 1.f / tanf (DEG2RAD (r_fovx) * 0.5f);
	h = 1.f / tanf (DEG2RAD (r_fovy) * 0.5f);
	d = 12.f * q_min (w, h);
	znear = CLAMP (0.5f, d, 4.f);
	zfar = gl_farclip.value;

	if (r_xr_asymmetric_projection)
		R_SetXRProjection(r_matproj, znear, zfar);
	else
		GL_FrustumMatrix(r_matproj, DEG2RAD(r_fovx), DEG2RAD(r_fovy), znear, zfar);

	// View matrix
	if (r_xr_view_basis_valid)
	{
		IdentityMatrix (r_matview);
		r_matview[0] = r_xr_forward[0]; r_matview[4] = r_xr_forward[1]; r_matview[8] = r_xr_forward[2];
		r_matview[1] = -r_xr_right[0]; r_matview[5] = -r_xr_right[1]; r_matview[9] = -r_xr_right[2];
		r_matview[2] = r_xr_up[0]; r_matview[6] = r_xr_up[1]; r_matview[10] = r_xr_up[2];
	}
	else
	{
		RotationMatrix(r_matview, DEG2RAD(-r_refdef.viewangles[ROLL]), 0);
		RotationMatrix(rotation, DEG2RAD(-r_refdef.viewangles[PITCH]), 1);
		MatrixMultiply(r_matview, rotation);
		RotationMatrix(rotation, DEG2RAD(-r_refdef.viewangles[YAW]), 2);
		MatrixMultiply(r_matview, rotation);
	}

	TranslationMatrix(translation, -r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]);
	MatrixMultiply(r_matview, translation);

	// View projection matrix
	memcpy(r_matviewproj, r_matproj, 16 * sizeof(float));
	MatrixMultiply(r_matviewproj, r_matview);

	ExtractFrustumPlane (r_matviewproj, 0,  1.f, true,  &frustum[0]); // right
	ExtractFrustumPlane (r_matviewproj, 0, -1.f, false, &frustum[1]); // left
	ExtractFrustumPlane (r_matviewproj, 1, -1.f, false, &frustum[2]); // bottom
	ExtractFrustumPlane (r_matviewproj, 1,  1.f, true,  &frustum[3]); // top

	logznear = log2f (znear);
	logzfar = log2f (zfar);
	memcpy (r_framedata.viewproj, r_matviewproj, 16 * sizeof (float));
	r_framedata.zlogscale = LIGHT_TILES_Z / (logzfar - logznear);
	r_framedata.zlogbias = -r_framedata.zlogscale * logznear;
}

/*
=============
GL_NeedsSceneEffects
=============
*/
qboolean GL_NeedsSceneEffects (void)
{
	return !VID_XR_UsingMultiview () && (R_HasXRFinalTarget() || framebufs.scene.samples > 1 || water_warp || r_refdef.scale != 1);
}

/*
=============
GL_NeedsPostprocess
=============
*/
qboolean GL_NeedsPostprocess (void)
{
    return !VID_XR_UsingMultiview () && (vid_gamma.value != 1.f || vid_contrast.value != 1.f || softemu || R_GetEffectiveAlphaMode () == ALPHAMODE_OIT);
}

/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{


	if (VID_XR_UsingMultiview ())
	{
		R_BindXRFinalTarget ();
		framesetup.scene_fbo = r_xr_final_fbo;
		framesetup.oit_fbo = r_xr_final_fbo;
		glViewport (0, 0, r_xr_final_width, r_xr_final_height);
	}
	else if (!GL_NeedsSceneEffects ())
	{
		GL_BindFramebufferFunc (GL_FRAMEBUFFER, GL_NeedsPostprocess () ? framebufs.composite.fbo : 0u);
		framesetup.scene_fbo = framebufs.composite.fbo;
		framesetup.oit_fbo = framebufs.oit.fbo_composite;
		glViewport (glx + r_refdef.vrect.x, gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height, r_refdef.vrect.width, r_refdef.vrect.height);
	}
	else
	{
		GL_BindFramebufferFunc (GL_FRAMEBUFFER, framebufs.scene.fbo);
		framesetup.scene_fbo = framebufs.scene.fbo;
		framesetup.oit_fbo = framebufs.oit.fbo_scene;
		glViewport (0, 0, r_refdef.vrect.width / r_refdef.scale, r_refdef.vrect.height / r_refdef.scale);
	}
}

/*
=============
R_Clear -- johnfitz -- rewritten and gutted
=============
*/
void R_Clear (void)
{
	GLbitfield clearbits = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
	if (gl_clear.value)
		clearbits |= GL_COLOR_BUFFER_BIT;

	GL_SetState (glstate & ~GLS_NO_ZWRITE); // make sure depth writes are enabled
	glStencilMask (~0u);
	glClear (clearbits);
}

/*
===============
R_SetupScene -- johnfitz -- this is the stuff that needs to be done once per eye in stereo mode
===============
*/
void R_SetupScene (void)
{
	R_SetupGL ();
}

/*
===============
R_UploadFrameData
===============
*/
static gpumultiviewframedata_t r_multiview_framedata;

void R_UploadFrameData (void)
{
	GLuint	buf;
	GLbyte	*ofs;
	size_t	size;

	size = sizeof(r_lightbuffer.lightstyles) + sizeof(r_lightbuffer.lights[0]) * q_max (r_framedata.numlights, 1); // avoid zero-length array
	GL_Upload (GL_SHADER_STORAGE_BUFFER, &r_lightbuffer, size, &buf, &ofs);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 0, buf, (GLintptr)ofs, size);

	GL_Upload (GL_UNIFORM_BUFFER, &r_framedata, sizeof (r_framedata), &buf, &ofs);
	GL_BindBufferRange (GL_UNIFORM_BUFFER, 0, buf, (GLintptr)ofs, sizeof (r_framedata));
#if defined(ANDROID_GLES3)
	GL_Upload (GL_UNIFORM_BUFFER, &r_lightbuffer, sizeof (r_lightbuffer.lightstyles) + sizeof (r_lightbuffer.lights[0]) * r_framedata.numlights, &buf, &ofs);
	GL_BindBufferRange (GL_UNIFORM_BUFFER, 1, buf, (GLintptr)ofs, sizeof (r_lightbuffer.lightstyles) + sizeof (r_lightbuffer.lights[0]) * r_framedata.numlights);
#endif
}

/*
===============
R_SetupView -- johnfitz -- this is the stuff that needs to be done once per frame, even in stereo mode
===============
*/
const gpumultiviewframedata_t *R_GetMultiviewFrameData (void) { return &r_multiview_framedata; }

void R_UploadMultiviewFrameData (const gpumultiviewframedata_t *data)
{
	struct { float viewproj[2][16]; float eyepos[2][4]; } views;
	GLuint buf;
	GLbyte *ofs;

	if (!data)
		return;
	r_multiview_framedata = *data;
	memcpy (views.viewproj, data->viewproj, sizeof (views.viewproj));
	memcpy (views.eyepos, data->eyepos, sizeof (views.eyepos));
	R_UploadFrameData ();
	GL_Upload (GL_UNIFORM_BUFFER, &views, sizeof (views), &buf, &ofs);
	GL_BindBufferRange (GL_UNIFORM_BUFFER, 3, buf, (GLintptr)ofs, sizeof (views));
}
void R_SetupView (void)
{
	R_AnimateLight ();

		r_framecount++;
	r_framedata.eyepos[0] = r_refdef.vieworg[0];
	r_framedata.eyepos[1] = r_refdef.vieworg[1];
	r_framedata.eyepos[2] = r_refdef.vieworg[2];
	r_framedata.time = cl.time;
	if (softemu == SOFTEMU_COARSE)
	{
		r_framedata.screendither = NOISESCALE * r_dither.value * r_softemu_dither_screen.value;
		r_framedata.texturedither = NOISESCALE * r_dither.value * r_softemu_dither_texture.value;

		// r_fullbright replaces the actual lightmap texture with a 2x2 50% grey one.
		// Since texture-space dithering is applied on a scale of 1/16 of a lightmap texel,
		// this would lead to massively overscaled dithering patterns, so we disable
		// texture-space dithering in this case.
		if (r_fullbright_cheatsafe)
			r_framedata.texturedither = 0.f;
	}
	else if (softemu == SOFTEMU_OFF)
	{
		r_framedata.screendither = r_dither.value * (1.f/255.f);
		r_framedata.texturedither = 0.f;
	}
	else // FINE (screen-space dithering applied during postprocessing), or BANDED (no dithering)
	{
		r_framedata.screendither = 0.f;
		r_framedata.texturedither = 0.f;
	}

	Fog_SetupFrame (); //johnfitz
	Sky_SetupFrame ();

// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);
	if (r_xr_view_basis_valid)
	{
		VectorCopy (r_xr_forward, vpn);
		VectorCopy (r_xr_right, vright);
		VectorCopy (r_xr_up, vup);
	}
	else
		AngleVectors (r_refdef.viewangles, vpn, vright, vup);

// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	//johnfitz -- calculate r_fovx and r_fovy here
	r_fovx = r_refdef.fov_x;
	r_fovy = r_refdef.fov_y;
	water_warp = false;
	if (r_waterwarp.value)
	{
		int contents = Mod_PointInLeaf (r_origin, cl.worldmodel)->contents;
		qboolean forced = M_ForcedUnderwater ();
		if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA || cl.forceunderwater || forced)
		{
			double t = forced ? realtime : cl.time;
			if (r_waterwarp.value > 1.f)
			{
				//variance is a percentage of width, where width = 2 * tan(fov / 2) otherwise the effect is too dramatic at high FOV and too subtle at low FOV.  what a mess!
				r_fovx = atan(tan(DEG2RAD(r_refdef.fov_x) / 2) * (0.97 + sin(t * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
				r_fovy = atan(tan(DEG2RAD(r_refdef.fov_y) / 2) * (1.03 - sin(t * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
			}
			else
			{
				water_warp = true;
			}
		}
	}
	//johnfitz

	R_SetFrustum ();

	R_MarkSurfaces (); //johnfitz -- create texture chains from PVS
    XR_Interaction_AddWorldEntities ();
    R_SortEntities ();
	R_PushDlights ();

	//johnfitz -- cheat-protect some draw modes
	r_fullbright_cheatsafe = r_lightmap_cheatsafe = false;
	r_drawworld_cheatsafe = true;
	if (cl.maxclients == 1)
	{
		if (!r_drawworld.value) r_drawworld_cheatsafe = false;

		if (r_fullbright.value) r_fullbright_cheatsafe = true;
		else if (r_lightmap.value) r_lightmap_cheatsafe = true;
	}
	if (!cl.worldmodel->lightdata)
	{
		r_fullbright_cheatsafe = true;
		r_lightmap_cheatsafe = false;
	}
	//johnfitz
}

//==============================================================================
//
// RENDER VIEW
//
//==============================================================================

/*
=============
R_GetVisEntities
=============
*/
entity_t **R_GetVisEntities (modtype_t type, qboolean translucent, int *outcount)
{
	entity_t **entlist = cl_sorted_visedicts;
	int *ofs = cl_modtype_ofs + type * 2 + (translucent ? 1 : 0);
	*outcount = ofs[1] - ofs[0];
	return entlist + ofs[0];
}

/*
=============
R_DrawWater
=============
*/
static void R_DrawWater (qboolean translucent)
{
	entity_t **entlist = cl_sorted_visedicts;
	int *ofs = cl_modtype_ofs + 2 * mod_brush;

	if (translucent)
	{
		// all entities can have translucent water
		R_DrawBrushModels_Water (entlist + ofs[0], ofs[2] - ofs[0], true);
	}
	else
	{
		// only opaque entities can have opaque water
		R_DrawBrushModels_Water (entlist + ofs[0], ofs[1] - ofs[0], false);
	}

}

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList (qboolean alphapass) //johnfitz -- added parameter
{
#if defined(ANDROID_GLES3)
    if (!alphapass && r_gles_perf_capture.value)
        glperf_stats.visible_entities = cl_numvisedicts;
#endif
	int		*ofs;
	entity_t **entlist = cl_sorted_visedicts;

	GL_BeginGroup (alphapass ? "Translucent entities" : "Opaque entities");

	ofs = cl_modtype_ofs + (alphapass ? 1 : 0);
	R_DrawBrushModels  (entlist + ofs[2*mod_brush ], ofs[2*mod_brush +1] - ofs[2*mod_brush ]);
	R_DrawAliasModels  (entlist + ofs[2*mod_alias ], ofs[2*mod_alias +1] - ofs[2*mod_alias ]);
	if (!alphapass)
		R_DrawSpriteModels (entlist + cl_modtype_ofs[2*mod_sprite], cl_modtype_ofs[2*mod_sprite+2] - cl_modtype_ofs[2*mod_sprite]);

	GL_EndGroup ();
}

/*
=============
R_IsViewModelVisible
=============
*/
static qboolean R_IsViewModelVisible (const entity_t *e)
{
	if (XR_Interaction_WheelActive () || !r_drawviewmodel.value || !r_drawentities.value || chase_active.value || scr_viewsize.value >= 130)
		return false;

	if (cl.items & IT_INVISIBILITY || cl.stats[STAT_HEALTH] <= 0)
		return false;

	if (!e->model)
		return false;

	//johnfitz -- this fixes a crash
	if (e->model->type != mod_alias)
		return false;

	return true;
}

/*
=============
R_DrawViewModel -- johnfitz -- gutted
=============
*/
void R_DrawViewModel (void)
{
    entity_t mainhand_entity, *mainhand = &cl.viewent;
	entity_t offhand;
    /* The canonical view entity can be borrowed for offhand fire; keep main presentation independent. */
    if (r_xr_eye_pass && XR_Interaction_GetMainhandViewmodel(&mainhand_entity))
        mainhand = &mainhand_entity;
	qboolean draw_mainhand = R_IsViewModelVisible (mainhand);
	qboolean draw_offhand = r_xr_eye_pass && XR_Interaction_GetOffhandViewmodel (&offhand) && R_IsViewModelVisible (&offhand);

	if (!draw_mainhand && !draw_offhand)
		return;

	GL_BeginGroup ("View model");
	GL_DepthRange (ZRANGE_VIEWMODEL);
	if (draw_mainhand)
    {
        if (r_xr_eye_pass) R_GetXRWeaponVisualOrigin (XR_Input_PhysicalHandForRole (XR_HAND_MAINHAND), mainhand->model, mainhand->origin);
        r_xr_mainhand_viewmodel = mainhand;
        R_DrawAliasModels (&mainhand, 1);
        r_xr_mainhand_viewmodel = NULL;
    }
	if (draw_offhand)
	{
		if (R_GetXRWeaponVisualOrigin (XR_Input_PhysicalHandForRole (XR_HAND_OFFHAND), offhand.model, offhand.origin))
		{
			r_xr_offhand_viewmodel = &offhand;
			R_DrawAliasModels (&r_xr_offhand_viewmodel, 1);
			r_xr_offhand_viewmodel = NULL;
		}
	}
	GL_DepthRange (ZRANGE_FULL);
	GL_EndGroup ();
}

typedef struct debugvert_s
{
	vec3_t		pos;
	uint32_t	color;
} debugvert_t;

static debugvert_t	debugverts[4096];
static uint16_t		debugidx[8192];
static int			numdebugverts = 0;
static int			numdebugidx = 0;
static qboolean		debugztest = false;

/*
================
R_FlushDebugGeometry
================
*/
void R_FlushDebugGeometry (void)
{
	if (numdebugverts && numdebugidx)
	{
		GLuint	buf;
		GLbyte	*ofs;
		unsigned int state;

		GL_UseProgram (VID_XR_UsingMultiview () ? glprogs.debug3d_multiview : glprogs.debug3d);
		state = GLS_BLEND_ALPHA | GLS_NO_ZWRITE | GLS_CULL_NONE | GLS_ATTRIBS(2);
		if (!debugztest)
			state |= GLS_NO_ZTEST;
		GL_SetState (state);

		GL_UploadTransient (GL_ARRAY_BUFFER, debugverts, sizeof (debugverts[0]) * numdebugverts, buf, ofs, "debug");
		GL_BindBuffer (GL_ARRAY_BUFFER, buf);
		#if defined(ANDROID_GLES3)
GLESVAO_BindDynamic ();
#endif
GL_VertexAttribPointerFunc (0, 3, GL_FLOAT, GL_FALSE, sizeof (debugverts[0]), ofs + offsetof (debugvert_t, pos));
		GL_VertexAttribPointerFunc (1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof (debugverts[0]), ofs + offsetof (debugvert_t, color));
#if defined(ANDROID_GLES3)
		GLESVAO_UseLayout (GLES_LAYOUT_DEBUG, "debug", buf, buf, GL_UNSIGNED_SHORT);
#endif

		GL_UploadTransient (GL_ELEMENT_ARRAY_BUFFER, debugidx, sizeof (debugidx[0]) * numdebugidx, buf, ofs, "debug");
		GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, buf);
		GL_PerfCountDraws (1);
		glDrawElements (GL_LINES, numdebugidx, GL_UNSIGNED_SHORT, ofs);
	}

	numdebugverts = 0;
	numdebugidx = 0;
}

/*
================
R_SetDebugGeometryZTest
================
*/
void R_SetDebugGeometryZTest (qboolean ztest)
{
	if (debugztest == ztest)
		return;
	R_FlushDebugGeometry ();
	debugztest = ztest;
}

/*
================
R_AddDebugGeometry
================
*/
static void R_AddDebugGeometry (const debugvert_t verts[], int numverts, const uint16_t idx[], int numidx)
{
	int i;

	if (numdebugverts + numverts > countof (debugverts) ||
		numdebugidx + numidx > countof (debugidx))
		R_FlushDebugGeometry ();

	for (i = 0; i < numidx; i++)
		debugidx[numdebugidx + i] = idx[i] + numdebugverts;
	numdebugidx += numidx;

	for (i = 0; i < numverts; i++)
		debugverts[numdebugverts + i] = verts[i];
	numdebugverts += numverts;
}

static void R_XRDrawPointer(void) {
    vec3_t a, b, right, up; int i; const uint32_t color = 0xff00ffff;
    if (!r_xr_pointer_valid) return;
    R_EmitLine(r_xr_pointer_start, r_xr_pointer_end, color);
    VectorScale(vright, 5.f, right); VectorScale(vup, 5.f, up);
    for (i = 0; i < 16; ++i) { float t0=(float)i*2.f*(float)M_PI/16.f, t1=(float)(i+1)*2.f*(float)M_PI/16.f; VectorScale(right, cosf(t0), a); VectorMA(a, sinf(t0), up, a); VectorAdd(a, r_xr_pointer_end, a); VectorScale(right, cosf(t1), b); VectorMA(b, sinf(t1), up, b); VectorAdd(b, r_xr_pointer_end, b); R_EmitLine(a,b,color); }
    VectorMA(r_xr_pointer_end, -1.5f, vright, a); VectorMA(r_xr_pointer_end, 1.5f, vright, b); R_EmitLine(a,b,color);
    VectorMA(r_xr_pointer_end, -1.5f, vup, a); VectorMA(r_xr_pointer_end, 1.5f, vup, b); R_EmitLine(a,b,color);
}
static void R_XRDrawLaserBeamForHand (const xr_laser_t *laser)
{
	debugvert_t verts[16];
	uint16_t idx[48];
	vec3_t direction, side, up, radial;
	uint32_t color;
	float length, radius;
	int i;

	if (!laser->valid || !vr_laser_beam.value)
		return;
	VectorSubtract (laser->end, laser->start, direction);
	length = VectorNormalize (direction);
	if (length <= 0.01f)
		return;
	if (fabsf (direction[2]) < 0.9f)
		VectorSet (side, 0.f, 0.f, 1.f);
	else
		VectorSet (side, 0.f, 1.f, 0.f);
	CrossProduct (direction, side, side);
	VectorNormalize (side);
	CrossProduct (side, direction, up);
	VectorNormalize (up);
	radius = 0.25f * CLAMP (0.05f, vr_laser_beam_width.value, 4.f);
	color = R_XRLaserColor (vr_laser_beam_alpha.value);

	for (i = 0; i < 8; ++i)
	{
		float angle = (float)i * (2.f * (float)M_PI / 8.f);
		VectorScale (side, cosf (angle), radial);
		VectorMA (radial, sinf (angle), up, radial);
		VectorMA (laser->start, radius, radial, verts[i * 2].pos);
		VectorMA (laser->end, radius, radial, verts[i * 2 + 1].pos);
		verts[i * 2].color = color;
		verts[i * 2 + 1].color = color;
		idx[i * 6] = (uint16_t)(i * 2);
		idx[i * 6 + 1] = (uint16_t)(((i + 1) % 8) * 2);
		idx[i * 6 + 2] = (uint16_t)(i * 2 + 1);
		idx[i * 6 + 3] = (uint16_t)(i * 2 + 1);
		idx[i * 6 + 4] = (uint16_t)(((i + 1) % 8) * 2);
		idx[i * 6 + 5] = (uint16_t)(((i + 1) % 8) * 2 + 1);
	}

	{
		GLuint buf;
		GLbyte *ofs;
		GL_UseProgram (VID_XR_UsingMultiview () ? glprogs.debug3d_multiview : glprogs.debug3d);
		GL_SetState (GLS_BLEND_ALPHA | GLS_NO_ZWRITE | GLS_CULL_NONE | GLS_ATTRIBS(2));
		GL_UploadTransient (GL_ARRAY_BUFFER, verts, sizeof (verts), buf, ofs, "xr laser");
		GL_BindBuffer (GL_ARRAY_BUFFER, buf);
#if defined(ANDROID_GLES3)
		GLESVAO_BindDynamic ();
#endif
		GL_VertexAttribPointerFunc (0, 3, GL_FLOAT, GL_FALSE, sizeof (verts[0]), ofs + offsetof (debugvert_t, pos));
		GL_VertexAttribPointerFunc (1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof (verts[0]), ofs + offsetof (debugvert_t, color));
#if defined(ANDROID_GLES3)
		GLESVAO_UseLayout (GLES_LAYOUT_DEBUG, "xr laser", buf, buf, GL_UNSIGNED_SHORT);
#endif
		GL_UploadTransient (GL_ELEMENT_ARRAY_BUFFER, idx, sizeof (idx), buf, ofs, "xr laser");
		GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, buf);
		GL_PerfCountDraws (1);
		glDrawElements (GL_TRIANGLES, countof (idx), GL_UNSIGNED_SHORT, ofs);
	}
}
static void R_XRDrawLaserBeam (void)
{
    int hand;
    for (hand = 0; hand < IW_XR_HAND_COUNT; ++hand)
        R_XRDrawLaserBeamForHand (&r_xr_laser[hand]);
}

static void R_XRDrawTeleportMarker (void)
{
    debugvert_t verts[32]; uint16_t idx[96]; GLuint buf; GLbyte *ofs; int i;
    const float outer_radius = 13.333f, inner_radius = 12.833f;
    if (!r_xr_teleport_marker_valid) return;
    for (i = 0; i < 16; ++i) { float a = (float)i * 2.f * (float)M_PI / 16.f; int next = (i + 1) % 16; VectorCopy(r_xr_teleport_marker_origin, verts[i * 2].pos); VectorCopy(r_xr_teleport_marker_origin, verts[i * 2 + 1].pos); verts[i * 2].pos[0] += cosf(a) * outer_radius; verts[i * 2].pos[1] += sinf(a) * outer_radius; verts[i * 2 + 1].pos[0] += cosf(a) * inner_radius; verts[i * 2 + 1].pos[1] += sinf(a) * inner_radius; verts[i * 2].pos[2] += 0.15f; verts[i * 2 + 1].pos[2] += 0.15f; verts[i * 2].color = verts[i * 2 + 1].color = r_xr_teleport_marker_color; idx[i * 6] = (uint16_t)(i * 2); idx[i * 6 + 1] = (uint16_t)(next * 2); idx[i * 6 + 2] = (uint16_t)(i * 2 + 1); idx[i * 6 + 3] = (uint16_t)(i * 2 + 1); idx[i * 6 + 4] = (uint16_t)(next * 2); idx[i * 6 + 5] = (uint16_t)(next * 2 + 1); }
    GL_UseProgram(VID_XR_UsingMultiview () ? glprogs.debug3d_multiview : glprogs.debug3d); GL_SetState(GLS_BLEND_ALPHA | GLS_NO_ZWRITE | GLS_CULL_NONE | GLS_ATTRIBS(2)); GL_UploadTransient(GL_ARRAY_BUFFER, verts, sizeof(verts), buf, ofs, "xr teleport marker"); GL_BindBuffer(GL_ARRAY_BUFFER, buf);
#if defined(ANDROID_GLES3)
    GLESVAO_BindDynamic();
#endif
    GL_VertexAttribPointerFunc(0, 3, GL_FLOAT, GL_FALSE, sizeof(verts[0]), ofs + offsetof(debugvert_t, pos)); GL_VertexAttribPointerFunc(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(verts[0]), ofs + offsetof(debugvert_t, color));
#if defined(ANDROID_GLES3)
    GLESVAO_UseLayout(GLES_LAYOUT_DEBUG, "xr teleport marker", buf, buf, GL_UNSIGNED_SHORT);
#endif
    GL_UploadTransient(GL_ELEMENT_ARRAY_BUFFER, idx, sizeof(idx), buf, ofs, "xr teleport marker"); GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf); GL_PerfCountDraws(1); glDrawElements(GL_TRIANGLES, countof(idx), GL_UNSIGNED_SHORT, ofs);
}
/*
================
R_EmitLine
================
*/
void R_EmitLine (const vec3_t a, const vec3_t b, uint32_t color)
{
	debugvert_t verts[2];
	uint16_t idx[2];

	VectorCopy (a, verts[0].pos);
	VectorCopy (b, verts[1].pos);
	verts[0].color = color;
	verts[1].color = color;
	idx[0] = 0;
	idx[1] = 1;

	R_AddDebugGeometry (verts, 2, idx, 2);
}

/*
================
R_EmitWirePoint -- johnfitz -- draws a wireframe cross shape for point entities
================
*/
void R_EmitWirePoint (const vec3_t origin, uint32_t color)
{
	const float Size = 8.f;
	int i;
	for (i = 0; i < 3; i++)
	{
		vec3_t a, b;
		VectorCopy (origin, a);
		VectorCopy (origin, b);
		a[i] -= Size;
		b[i] += Size;
		R_EmitLine (a, b, color);
	}
}

/*
================
R_EmitWireBox -- johnfitz -- draws one axis aligned bounding box
================
*/
static const uint16_t boxidx[12*2] = { 0,1, 0,2, 0,4, 1,3, 1,5, 2,3, 2,6, 3,7, 4,5, 4,6, 5,7, 6,7, };

void R_EmitWireBox (const vec3_t mins, const vec3_t maxs, uint32_t color)
{
	int i;
	debugvert_t v[8];

	for (i = 0; i < 8; i++)
	{
		v[i].pos[0] = i & 1 ? mins[0] : maxs[0];
		v[i].pos[1] = i & 2 ? mins[1] : maxs[1];
		v[i].pos[2] = i & 4 ? mins[2] : maxs[2];
		v[i].color = color;
	}

	R_AddDebugGeometry (v, countof (v), boxidx, countof (boxidx));
}

/*
================
R_EmitArrow
================
*/
void R_EmitArrow (const vec3_t from, const vec3_t to, uint32_t color)
{
	float	frac, len;
	vec3_t	center, dir, side, tmp;

	R_EmitLine (from, to, color);

	VectorSubtract (to, from, dir);
	len = VectorNormalize (dir);
	if (len < 1e-2f)
	{
		VectorCopy (vup, dir);
		VectorCopy (vright, side);
	}
	else
	{
		VectorSubtract (from, r_origin, tmp);
		CrossProduct (dir, tmp, side);
		VectorNormalize (side);
	}

	frac = realtime - floor (realtime);
	VectorLerp (from, to, frac, center);

	VectorMA (center, 8.f, side, tmp);
	VectorMA (tmp, -8.f, dir, tmp);
	R_EmitLine (tmp, center, color);

	VectorMA (tmp, -16.f, side, tmp);
	R_EmitLine (tmp, center, color);
}

/*
================
R_EmitEdictLink
================
*/
static void R_EmitEdictLink (const edict_t *from, const edict_t *to, showbboxflags_t flags)
{
	vec3_t vec_from, vec_to;

	if (!flags)
		return;

	VectorCopy (from->v.origin, vec_from);
	if (!VectorCompare (from->v.mins, from->v.maxs))
	{
		VectorMA (vec_from, 0.5f, from->v.mins, vec_from);
		VectorMA (vec_from, 0.5f, from->v.maxs, vec_from);
	}

	VectorCopy (to->v.origin, vec_to);
	if (!VectorCompare (to->v.mins, to->v.maxs))
	{
		VectorMA (vec_to, 0.5f, to->v.mins, vec_to);
		VectorMA (vec_to, 0.5f, to->v.maxs, vec_to);
	}

	if (flags == SHOWBBOX_LINK_BOTH)
		R_EmitLine (vec_from, vec_to, 0x7f7f3f7f);
	else if (flags == SHOWBBOX_LINK_OUTGOING)
		R_EmitArrow (vec_from, vec_to, 0x7f7f3f3f);
	else if (flags == SHOWBBOX_LINK_INCOMING)
		R_EmitArrow (vec_to, vec_from, 0x7f3f3f7f);
}

/*
================
R_ShowBoundingBoxesFilter

r_showbboxes_filter artifact =trigger_secret #42
================
*/
char r_showbboxes_filter_strings[MAXCMDLINE];
qboolean r_showbboxes_filter_byindex;

static qboolean R_ShowBoundingBoxesFilter (edict_t *ed)
{
	char entnum[16] = "";
	const char *classname = NULL;
	const char *filter_p = r_showbboxes_filter_strings;

	if (!r_showbboxes_filter_strings[0])
		return true;

	if (r_showbboxes_filter_byindex)
		q_snprintf (entnum, sizeof (entnum), "%d", NUM_FOR_EDICT (ed));

	if (ed->v.classname)
		classname = PR_GetString (ed->v.classname);

	for (filter_p = r_showbboxes_filter_strings; *filter_p; filter_p += strlen (filter_p) + 1)
	{
		if (*filter_p == '#')
		{
			if (!strcmp (entnum, filter_p + 1))
				return true;
			continue;
		}

		if (!classname)
			continue;

		if (*filter_p == '=')
		{
			if (!strcmp (classname, filter_p + 1))
				return true;
			continue;
		}

		if (strstr (classname, filter_p) != NULL)
			return true;
	}

	return false;
}

static edict_t **bbox_edicts = NULL;		// all edicts shown by r_showbboxes & co
edict_t **bbox_linked = NULL;				// focused edict, followed by edicts linked from/to it

/*
================
R_AddHighlightedEntity
================
*/
static void R_AddHighlightedEntity (edict_t *ed, showbboxflags_t flags)
{
	if (ed->showbboxframe != r_framecount)
	{
		ed->showbboxframe = r_framecount;
		ed->showbboxflags = SHOWBBOX_LINK_NONE;
		VEC_PUSH (bbox_edicts, ed);
	}

	if (!(ed->showbboxflags & flags) && (int)r_showbboxes_links.value & flags)
	{
		VEC_PUSH (bbox_linked, ed);
		ed->showbboxflags |= flags;
	}
}

/*
================
R_ClearBoundingBoxes
================
*/
void R_ClearBoundingBoxes (void)
{
	VEC_CLEAR (bbox_edicts);
	VEC_CLEAR (bbox_linked);
}

/*
================
R_ShowBoundingBoxes -- johnfitz

draw bounding boxes -- the server-side boxes, not the renderer cullboxes
================
*/
static void R_ShowBoundingBoxes (void)
{
	extern		edict_t *sv_player;
	byte		*pvs;
	vec3_t		mins,maxs;
	edict_t		*ed, *focused;
	int			i, j, mode;
	uint32_t	color;
	qcvm_t 		*oldvm;	//in case we ever draw a scene from within csqc.
	float		dist, bestdist, extend;
	vec3_t		rcpdelta;

	VEC_CLEAR (bbox_edicts);
	VEC_CLEAR (bbox_linked);
	focused = NULL;

	mode = abs ((int)r_showbboxes.value);
	if ((!mode && !r_showfields.value) || cl.maxclients > 1 || !r_drawentities.value || !sv.active)
		return;

	GL_BeginGroup ("Show bounding boxes");

	R_SetDebugGeometryZTest (false);

	oldvm = qcvm;
	PR_SwitchQCVM(NULL);
	PR_SwitchQCVM(&sv.qcvm);

	// Use PVS if r_showbboxes >= 2, or if r_showbboxes is 0 (which means r_showfields is active)
	if (mode >= 2 || mode == 0)
	{
		vec3_t org;
		VectorAdd (sv_player->v.origin, sv_player->v.view_ofs, org);
		pvs = SV_FatPVS (org, sv.worldmodel);
	}
	else
		pvs = NULL;

	// Compute ray reciprocal delta
	for (i = 0; i < 3; i++)
		rcpdelta[i] = 1.f / (gl_farclip.value * vpn[i]);

	// Iterate over all server entities
	bestdist = FLT_MAX;
	for (i=1, ed=NEXT_EDICT(qcvm->edicts) ; i<qcvm->num_edicts ; i++, ed=NEXT_EDICT(ed))
	{
		if (ed == sv_player || ed->free)
			continue; // don't draw player's own bbox or freed edicts

		if (r_showbboxes_think.value && (ed->v.nextthink <= 0) == (r_showbboxes_think.value > 0))
			continue;

		if (r_showbboxes_health.value && (ed->v.health <= 0) == (r_showbboxes_health.value > 0))
			continue;

		// Compute bounding box (16 units wide for point entities)
		extend = VectorCompare (ed->v.mins, ed->v.maxs) ? 8.f : 0.f;
		for (j = 0; j < 3; j++)
		{
			mins[j] = ed->v.origin[j] + ed->v.mins[j] - extend;
			maxs[j] = ed->v.origin[j] + ed->v.maxs[j] + extend;
		}

		// Frustum culling
		if (R_CullBox (mins, maxs))
			continue;

		// Classname or edict num filter
		if (!R_ShowBoundingBoxesFilter(ed))
			continue;

		// PVS filter
		if (pvs)
		{
			qboolean inpvs =
				ed->num_leafs ?
					SV_EdictInPVS (ed, pvs) :
					SV_BoxInPVS (ed->v.absmin, ed->v.absmax, pvs, sv.worldmodel->nodes)
			;
			if (!inpvs)
				continue;
		}

		// Keep track of the closest bounding box intersecting the center ray
		// Note: if we're inside the box (dist == 0), we ignore this entity
		if (RayVsBox (r_origin, rcpdelta, mins, maxs, &dist) && dist > 0.f && dist < bestdist)
		{
			bestdist = dist;
			focused = ed;
		}

		// Add edict to list
		R_AddHighlightedEntity (ed, SHOWBBOX_LINK_NONE);
	}

	if (focused)
		VEC_PUSH (bbox_linked, focused);

	if (focused && r_showbboxes_links.value)
	{
		// Find outgoing links (entity field references other than .chain)
		if ((int)r_showbboxes_links.value & SHOWBBOX_LINK_OUTGOING)
		{
			for (i = 0; i < qcvm->numentityfields; i++)
			{
				eval_t *val = (eval_t *)((char *)&focused->v + qcvm->entityfieldofs[i]);
				if (qcvm->entityfieldofs[i] == offsetof (entvars_t, chain) || !val->edict)
					continue;
				ed = PROG_TO_EDICT (val->edict);
				if (ed == focused || ed->free || ed == sv_player)
					continue;
				R_AddHighlightedEntity (ed, SHOWBBOX_LINK_OUTGOING);
			}
		}

		// Inspect all other edicts to find incoming links
		// (either entity field references or target/targetname matches)
		if ((int)r_showbboxes_links.value & SHOWBBOX_LINK_INCOMING || r_showbboxes_targets.value)
		{
			const char *focus_target = PR_GetString (focused->v.target);
			const char *focus_targetname = PR_GetString (focused->v.targetname);

			for (i=1, ed=NEXT_EDICT(qcvm->edicts) ; i<qcvm->num_edicts ; i++, ed=NEXT_EDICT(ed))
			{
				if (ed == sv_player || ed->free || ed == focused)
					continue;

				// Check target/targetname matches
				if (r_showbboxes_targets.value && (*focus_target || *focus_targetname))
				{
					const char *target = PR_GetString (ed->v.target);
					const char *targetname = PR_GetString (ed->v.targetname);

					if (*focus_targetname && !strcmp (focus_targetname, target))
						R_AddHighlightedEntity (ed, SHOWBBOX_LINK_INCOMING);
					if (*focus_target && !strcmp (focus_target, targetname))
						R_AddHighlightedEntity (ed, SHOWBBOX_LINK_OUTGOING);
				}

				// Check for entity field references (other than .chain)
				if ((int)r_showbboxes_links.value & SHOWBBOX_LINK_INCOMING)
				{
					for (j = 0; j < qcvm->numentityfields; j++)
					{
						eval_t *val = (eval_t *)((char *)&ed->v + qcvm->entityfieldofs[j]);
						if (qcvm->entityfieldofs[i] == offsetof (entvars_t, chain) || !val->edict)
							continue;
						if (PROG_TO_EDICT (val->edict) == focused)
							R_AddHighlightedEntity (ed, SHOWBBOX_LINK_INCOMING);
					}
				}
			}
		}

		// Draw all links
		for (j = 0; j < (int) VEC_SIZE (bbox_linked); j++)
			R_EmitEdictLink (focused, bbox_linked[j], bbox_linked[j]->showbboxflags);
	}

	// Draw all the matching edicts
	for (i = 0; i < (int) VEC_SIZE (bbox_edicts); i++)
	{
		ed = bbox_edicts[i];

		if (ed == focused)
			color = 0xffffffff;
		else if (ed->showbboxflags)
			color = 0xaaaaaaaa;
		else if (r_showbboxes.value > 0.f)
		{
			int modelindex = (int)ed->v.modelindex;
			color = 0x7f800080;
			if (modelindex >= 0 && modelindex < MAX_MODELS && sv.models[modelindex])
			{
				switch (sv.models[modelindex]->type)
				{
					case mod_brush:  color = 0x7fff8080; break;
					case mod_alias:  color = 0x7f408080; break;
					case mod_sprite: color = 0x7f4040ff; break;
					default:
						break;
				}
			}
			if (ed->v.health > 0)
				color = 0x7f0000ff;
		}
		else if (r_showbboxes.value < 0.f)
			color = 0x7fffffff;
		else
			color = 0x5f7f7f7f;

		if (VectorCompare (ed->v.mins, ed->v.maxs))
		{
			//point entity
			R_EmitWirePoint (ed->v.origin, color);
		}
		else
		{
			//box entity
			VectorAdd (ed->v.mins, ed->v.origin, mins);
			VectorAdd (ed->v.maxs, ed->v.origin, maxs);
			R_EmitWireBox (mins, maxs, color);
		}
	}

	VEC_CLEAR (bbox_edicts);

	PR_SwitchQCVM(NULL);
	PR_SwitchQCVM(oldvm);

	R_FlushDebugGeometry ();

	Sbar_Changed (); //so we don't get dots collecting on the statusbar

	GL_EndGroup ();
}

/*
===============
R_ShowSkeletons
===============
*/
static void R_ShowSkeletons (void)
{
	int		*ofs;
	entity_t **entlist = cl_sorted_visedicts;

	if (!r_showskel.value || cl.maxclients > 1)
		return;

	GL_BeginGroup ("Skeletons");

	R_SetDebugGeometryZTest (false);

	ofs = cl_modtype_ofs;
	R_DrawAliasModels_ShowSkel (entlist + ofs[2*mod_alias ], ofs[2*mod_alias +2] - ofs[2*mod_alias ]);

	R_FlushDebugGeometry ();

	GL_EndGroup ();
}

/*
===============
R_ShowPointFile
===============
*/
static void R_ShowPointFile (void)
{
	size_t i;

	if (VEC_SIZE (r_pointfile) == 0)
		return;

	GL_BeginGroup ("Point file");
	R_SetDebugGeometryZTest (true);
	for (i = 1; i < VEC_SIZE (r_pointfile); i++)
		R_EmitArrow (r_pointfile[i - 1], r_pointfile[i], 0xff3f3f7f);
	R_FlushDebugGeometry ();
	GL_EndGroup ();
}

/*
===============
Collinear
===============
*/
static qboolean Collinear (const vec3_t a, const vec3_t b, const vec3_t c)
{
	return Distance (a, b) + Distance (b, c) < Distance (a, c) * 1.00001f;
}

/*
===============
R_ReadPointFile_f
===============
*/
void R_ReadPointFile_f (void)
{
	FILE		*f;
	vec3_t		org;
	int			r, n;
	qboolean	leakmode;
	char		name[MAX_QPATH];

	VEC_CLEAR (r_pointfile);

	if (cls.state != ca_connected)
		return;			// need an active map.

	q_snprintf (name, sizeof(name), "maps/%s.pts", cl.mapname);
	leakmode = Cmd_Argc () >= 2 && !strcmp (Cmd_Argv (1), "leak");

	COM_FOpenFile (name, &f, NULL);
	if (!f)
	{
		Con_Printf ("couldn't open %s\n", name);
		return;
	}

	if (!leakmode)
		Con_Printf ("Reading %s...\n", name);
	org[0] = org[1] = org[2] = 0; // silence pesky compiler warnings

	for (r = 0; fscanf (f,"%f %f %f\n", &org[0], &org[1], &org[2]) == 3; r++)
	{
		Vec_Append ((void **) &r_pointfile, sizeof (r_pointfile[0]), &org, 1);
		n = (int) VEC_SIZE (r_pointfile);
		if (n >= 3 && Collinear (r_pointfile[n-3], r_pointfile[n-2], r_pointfile[n-1]))
		{
			VectorCopy (r_pointfile[n-1], r_pointfile[n-2]);
			VEC_POP (r_pointfile);
		}
	}

	fclose (f);

	if (leakmode)
		Con_Warning ("map appears to have leaks!\n");
	else
		Con_Printf ("%i points read (%i significant)\n", r, (int) VEC_SIZE (r_pointfile));
}

/*
================
R_ShowTris -- johnfitz
================
*/
void R_ShowTris (void)
{
#if defined(ANDROID_GLES3)
	return;
#endif
	int		*ofs;
	entity_t **entlist = cl_sorted_visedicts;

	if (r_showtris.value < 1 || r_showtris.value > 2 || cl.maxclients > 1)
		return;

	GL_BeginGroup ("Show tris");

	Fog_DisableGFog (); //johnfitz
	R_UploadFrameData ();

	if (r_showtris.value == 1)
		GL_DepthRange (ZRANGE_NEAR);
#if !defined(ANDROID_GLES3)
	glPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
#endif
	GL_PolygonOffset (OFFSET_SHOWTRIS);

	ofs = cl_modtype_ofs;
	R_DrawBrushModels_ShowTris  (entlist + ofs[2*mod_brush ], ofs[2*mod_brush +2] - ofs[2*mod_brush ]);
	R_DrawAliasModels_ShowTris  (entlist + ofs[2*mod_alias ], ofs[2*mod_alias +2] - ofs[2*mod_alias ]);
	R_DrawSpriteModels_ShowTris (entlist + ofs[2*mod_sprite], ofs[2*mod_sprite+2] - ofs[2*mod_sprite]);

	// viewmodel
	if (R_IsViewModelVisible (&cl.viewent))
	{
		entity_t *e = &cl.viewent;

		if (r_showtris.value != 1.f)
			GL_DepthRange (ZRANGE_VIEWMODEL);

		R_DrawAliasModels_ShowTris (&e, 1);

		GL_DepthRange (ZRANGE_FULL);
	}

	R_DrawParticles_ShowTris ();

#if !defined(ANDROID_GLES3)
	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
#endif
	GL_PolygonOffset (OFFSET_NONE);
	if (r_showtris.value == 1)
		GL_DepthRange (ZRANGE_FULL);

	Sbar_Changed (); //so we don't get dots collecting on the statusbar

	GL_EndGroup ();
}

/*
================
R_BeginTranslucency
================
*/
static void R_BeginTranslucency (void)
{
	static const float zeroes[4] = {0.f, 0.f, 0.f, 0.f};
	static const float ones[4] = {1.f, 1.f, 1.f, 1.f};

	GL_BeginGroup ("Translucent objects");

	if (R_GetEffectiveAlphaMode () == ALPHAMODE_OIT)
	{
		GL_BindFramebufferFunc (GL_FRAMEBUFFER, framesetup.oit_fbo);
		GL_ClearBufferfvFunc (GL_COLOR, 0, zeroes);
		GL_ClearBufferfvFunc (GL_COLOR, 1, ones);

		glEnable (GL_STENCIL_TEST);
		glStencilMask (2);
		glStencilFunc (GL_ALWAYS, 2, 2);
		glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE);
	}
}

/*
================
R_EndTranslucency
================
*/
static void R_EndTranslucency (void)
{
	if (R_GetEffectiveAlphaMode () == ALPHAMODE_OIT)
	{
		GL_BeginGroup  ("OIT resolve");

		GL_BindFramebufferFunc (GL_FRAMEBUFFER, framesetup.scene_fbo);

		glStencilFunc (GL_EQUAL, 2, 2);
		glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);

		GL_UseProgram (glprogs.oit_resolve[framebufs.scene.samples > 1]);
		GL_SetState (GLS_BLEND_ALPHA | GLS_NO_ZTEST | GLS_NO_ZWRITE | GLS_CULL_NONE | GLS_ATTRIBS(0));
		GL_BindNative (GL_TEXTURE0, framebufs.scene.samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, framebufs.oit.accum_tex);
		GL_BindNative (GL_TEXTURE1, framebufs.scene.samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, framebufs.oit.revealage_tex);

		GL_PerfCountDraws (1);

		glDrawArrays (GL_TRIANGLES, 0, 3);

		glDisable (GL_STENCIL_TEST);

		GL_EndGroup ();
	}

	GL_EndGroup (); // translucent objects
}

/*
================
R_RenderScene
================
*/
void R_RenderScene (void)
{
	R_XRPrepareLasers ();
	R_XRUpdateLaserDots ();
	R_SetupScene (); //johnfitz -- this does everything that should be done once per call to RenderScene
    R_Clear ();

	Fog_EnableGFog (); //johnfitz

	if (!r_xr_eye_pass || r_xr_eye_index == 0)
		S_ExtraUpdate (); // don't let sound get messed up if going slow

	R_DrawEntitiesOnList (false); //johnfitz -- false means this is the pass for nonalpha entities
    XR_Interaction_DrawWorldModels ();

	R_DrawParticles (false);

	Sky_DrawSky (); //johnfitz

	R_DrawWater (false);

	R_BeginTranslucency ();

	R_DrawWater (true);

	R_DrawEntitiesOnList (true); //johnfitz -- true means this is the pass for alpha entities

	R_DrawParticles (true);

	R_EndTranslucency ();

	R_DrawViewModel (); //johnfitz -- moved here from R_RenderView -- il8r -- moved for oit reasons
R_XRDrawLaserBeam ();
	R_XRDrawTeleportMarker ();

	R_FlushDebugGeometry ();


	R_ShowTris (); //johnfitz

	R_ShowBoundingBoxes (); //johnfitz

	R_ShowSkeletons ();

	R_ShowPointFile ();
}

/*
================
R_WarpScaleView

The r_scale cvar allows rendering the 3D view at 1/2, 1/3, or 1/4 resolution.
This function scales the reduced resolution 3D view back up to fill 
r_refdef.vrect. This is for emulating a low-resolution pixellated look,
or possibly as a perforance boost on slow graphics cards.
================
*/
void R_WarpScaleView (void)
{
	int srcx, srcy, srcw, srch;
	float smax, tmax;
	qboolean msaa = framebufs.scene.samples > 1;
	qboolean needwarpscale;
	GLuint fbodest;
	double t;

	if (VID_XR_UsingMultiview () || !GL_NeedsSceneEffects ())
		return;

	srcx = glx + r_refdef.vrect.x;
	srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	srcw = r_refdef.vrect.width / r_refdef.scale;
	srch = r_refdef.vrect.height / r_refdef.scale;

	needwarpscale = R_HasXRFinalTarget() || r_refdef.scale != 1 || water_warp || (v_blend[3] && gl_polyblend.value && !softemu);
	fbodest = GL_NeedsPostprocess () ? framebufs.composite.fbo : r_xr_final_fbo;

	if (msaa)
	{
		GL_BeginGroup ("MSAA resolve");

		GL_BindFramebufferFunc (GL_READ_FRAMEBUFFER, framebufs.scene.fbo);
		if (needwarpscale)
		{
			GL_BindFramebufferFunc (GL_DRAW_FRAMEBUFFER, framebufs.resolved_scene.fbo);
			GL_BlitFramebufferFunc (0, 0, srcw, srch, 0, 0, srcw, srch, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		else
		{
			GL_BindFramebufferFunc (GL_DRAW_FRAMEBUFFER, fbodest);
			GL_BlitFramebufferFunc (0, 0, srcw, srch, srcx, srcy, srcx + srcw, srcy + srch, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}

		GL_EndGroup ();
	}

	GL_BindFramebufferFunc (GL_FRAMEBUFFER, fbodest);
	if (GL_NeedsPostprocess ())
		glViewport (0, 0, r_framebuffer_width, r_framebuffer_height);
	else
		glViewport (r_xr_final_fbo ? 0 : srcx, r_xr_final_fbo ? 0 : srcy,
			r_xr_final_fbo ? r_xr_final_width : r_refdef.vrect.width,
			r_xr_final_fbo ? r_xr_final_height : r_refdef.vrect.height);

	if (!needwarpscale)
		return;

	GL_BeginGroup ("Warp/scale view");

	smax = srcw/(float)r_framebuffer_width;
	tmax = srch/(float)r_framebuffer_height;

	GL_UseProgram (glprogs.warpscale[water_warp]);
	GL_SetState (GLS_BLEND_OPAQUE | GLS_NO_ZTEST | GLS_NO_ZWRITE | GLS_CULL_NONE | GLS_ATTRIBS(0));

	t = M_ForcedUnderwater () ? realtime : cl.time;
	GL_Uniform4fFunc (0, smax, tmax, water_warp ? 1.f/256.f : 0.f, (float)t);
	if (v_blend[3] && gl_polyblend.value && !softemu)
		GL_Uniform4fvFunc (1, 1, v_blend);
	else
		GL_Uniform4fFunc (1, 0.f, 0.f, 0.f, 0.f);
	GL_BindNative (GL_TEXTURE0, GL_TEXTURE_2D, msaa ? framebufs.resolved_scene.color_tex : framebufs.scene.color_tex);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, water_warp && msaa ? GL_LINEAR : GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, water_warp && msaa ? GL_LINEAR : GL_NEAREST);

	GL_PerfCountDraws (1);

	glDrawArrays (GL_TRIANGLES, 0, 3);

	GL_EndGroup ();
}

/*
================
R_RenderView
================
*/
extern cvar_t vr_stabilize_mode;

static void R_XRRotateOrientation(const float orientation[4], const float in[3], float out[3])
{
	float x = orientation[0], y = orientation[1], z = orientation[2], w = orientation[3];
	float tx = 2.f * (y * in[2] - z * in[1]);
	float ty = 2.f * (z * in[0] - x * in[2]);
	float tz = 2.f * (x * in[1] - y * in[0]);
	out[0] = in[0] + w * tx + (y * tz - z * ty);
	out[1] = in[1] + w * ty + (z * tx - x * tz);
	out[2] = in[2] + w * tz + (x * ty - y * tx);
}

static void R_XRWeaponAngles(const float forward[3], const float right[3], const float up[3], vec3_t angles)
{
	float sp = -forward[2];
	float sy = forward[1];
	float cy = forward[0];
	float sr = -right[2];
	float cr = up[2];
	float cp;
	if (fabsf(cy) > 0.001f)
		cp = forward[0] / cy;
	else if (fabsf(sy) > 0.001f)
		cp = forward[1] / sy;
	else if (fabsf(sr) > 0.001f)
		cp = -right[2] / sr;
	else if (fabsf(cr) > 0.001f)
		cp = up[2] / cr;
	else
		cp = cosf(asinf(sp));
	angles[PITCH] = RAD2DEG(atan2f(sp, cp));
	angles[YAW] = RAD2DEG(atan2f(sy, cy));
	angles[ROLL] = RAD2DEG(atan2f(sr, cr));
	R_XRNormalizeAngles(&angles[PITCH], &angles[YAW], &angles[ROLL]);
}

static void R_XRTransformHandPose(const iw_xr_frame_snapshot_t *snapshot, const float position_xr[3], const float orientation_xr[4], vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
    const float xr_forward[3] = {0.f, 0.f, -1.f};
    const float xr_right[3] = {1.f, 0.f, 0.f};
    const float xr_up[3] = {0.f, 1.f, 0.f};
    float delta_xr[3], delta[3], rotated[3];
    float yaw = r_xr_game_anchor_yaw - r_xr_head_anchor_yaw;
    delta_xr[0] = position_xr[0] - 0.5f * (snapshot->views[0].position[0] + snapshot->views[1].position[0]);
    delta_xr[1] = position_xr[1] - 0.5f * (snapshot->views[0].position[1] + snapshot->views[1].position[1]);
    delta_xr[2] = position_xr[2] - 0.5f * (snapshot->views[0].position[2] + snapshot->views[1].position[2]);
    R_XRToQuakePosition(delta_xr, delta);
    R_XRRotateYaw(delta, yaw);
    VectorMA(r_xr_center_vieworg, vr_world_scale.value, delta, origin);
    R_XRRotateOrientation(orientation_xr, xr_forward, rotated);
    R_XRToQuakePosition(rotated, forward);
    R_XRRotateYaw(forward, yaw);
    R_XRRotateOrientation(orientation_xr, xr_right, rotated);
    R_XRToQuakePosition(rotated, right);
    R_XRRotateYaw(right, yaw);
    R_XRRotateOrientation(orientation_xr, xr_up, rotated);
    R_XRToQuakePosition(rotated, up);
    R_XRRotateYaw(up, yaw);
}

static void R_XRApplyWeaponPose(const iw_xr_frame_snapshot_t *snapshot)
{
	const float xr_forward[3] = { 0.f, 0.f, -1.f };
	const float xr_right[3] = { 1.f, 0.f, 0.f };
	const float xr_up[3] = { 0.f, 1.f, 0.f };
	iw_xr_action_snapshot_t actions;
	iw_xr_hand_snapshot_t *hand;
	int dominant, offhand;
	float head_delta_xr[3], position[3], forward_xr[3], dominant_forward_xr[3], forward[3], dominant_forward[3], right_xr[3], right[3], up_xr[3], up[3];
	qboolean stabilized = false;
	const float *hand_position, *hand_orientation, *offhand_position;

	if (!snapshot || !VID_XR_GetActions(&actions))
		return;
	/* A non-XR mirror/UI pass must retain the latest stereo controller poses. */
	r_xr_controller_aim_valid = false;
	memset (r_xr_hand_aim_valid, 0, sizeof (r_xr_hand_aim_valid));
    memset (r_xr_hand_tracking_valid, 0, sizeof (r_xr_hand_tracking_valid));
	{
		int i;
		for (i = 0; i < IW_XR_HAND_COUNT; ++i)
		{
			iw_xr_hand_snapshot_t *h = &actions.hand[i];
			if (h->grip_valid)
			{
				R_XRTransformHandPose(snapshot, h->grip_position, h->grip_orientation, r_xr_hand_tracking_origin[i], r_xr_hand_tracking_forward[i], r_xr_hand_tracking_right[i], r_xr_hand_tracking_up[i]);
				r_xr_hand_tracking_valid[i] = true;
			}
			if (h->aim_valid)
			{
				R_XRTransformHandPose(snapshot, h->aim_position, h->aim_orientation, r_xr_hand_aim_origin[i], r_xr_hand_aim_forward[i], r_xr_hand_aim_right[i], r_xr_hand_aim_up[i]);
				r_xr_hand_aim_valid[i] = true;
			}
		}
	}
	dominant = XR_Input_PhysicalHandForRole(XR_HAND_MAINHAND);
    offhand = XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND);
	hand = &actions.hand[dominant];
	if (!hand->grip_valid && !hand->aim_valid)
		return;
	hand_position = hand->grip_valid ? hand->grip_position : hand->aim_position;
	hand_orientation = hand->grip_valid ? hand->grip_orientation : hand->aim_orientation;
	offhand_position = actions.hand[offhand].grip_valid ? actions.hand[offhand].grip_position : actions.hand[offhand].aim_position;

	head_delta_xr[0] = hand_position[0] - 0.5f * (snapshot->views[0].position[0] + snapshot->views[1].position[0]);
	head_delta_xr[1] = hand_position[1] - 0.5f * (snapshot->views[0].position[1] + snapshot->views[1].position[1]);
	head_delta_xr[2] = hand_position[2] - 0.5f * (snapshot->views[0].position[2] + snapshot->views[1].position[2]);
	R_XRToQuakePosition(head_delta_xr, position);
	R_XRRotateYaw(position, r_xr_game_anchor_yaw - r_xr_head_anchor_yaw);
	VectorMA(r_xr_center_vieworg, vr_world_scale.value, position, cl.viewent.origin);
	VectorCopy(cl.viewent.origin, r_xr_controller_origin);
    r_xr_pointer_valid = false;
    { vec3_t pointer_start_xr, pointer_hit_xr, pointer_delta;
      if (XR_Interaction_GetVirtualPointer(pointer_start_xr, pointer_hit_xr)) {
        pointer_delta[0] = pointer_hit_xr[0] - 0.5f * (snapshot->views[0].position[0] + snapshot->views[1].position[0]);
        pointer_delta[1] = pointer_hit_xr[1] - 0.5f * (snapshot->views[0].position[1] + snapshot->views[1].position[1]);
        pointer_delta[2] = pointer_hit_xr[2] - 0.5f * (snapshot->views[0].position[2] + snapshot->views[1].position[2]);
        R_XRToQuakePosition(pointer_delta, pointer_delta); R_XRRotateYaw(pointer_delta, r_xr_game_anchor_yaw - r_xr_head_anchor_yaw);
        VectorMA(r_xr_center_vieworg, vr_world_scale.value, pointer_delta, r_xr_pointer_end); pointer_delta[0] = pointer_start_xr[0] - 0.5f * (snapshot->views[0].position[0] + snapshot->views[1].position[0]); pointer_delta[1] = pointer_start_xr[1] - 0.5f * (snapshot->views[0].position[1] + snapshot->views[1].position[1]); pointer_delta[2] = pointer_start_xr[2] - 0.5f * (snapshot->views[0].position[2] + snapshot->views[1].position[2]); R_XRToQuakePosition(pointer_delta, pointer_delta); R_XRRotateYaw(pointer_delta, r_xr_game_anchor_yaw - r_xr_head_anchor_yaw); VectorMA(r_xr_center_vieworg, vr_world_scale.value, pointer_delta, r_xr_pointer_start); r_xr_pointer_valid = true;
      } }

	R_XRRotateOrientation(hand_orientation, xr_forward, forward_xr);
	VectorCopy(forward_xr, dominant_forward_xr);
	if (vr_stabilize_mode.value != 0 && (actions.hand[offhand].grip_valid || actions.hand[offhand].aim_valid) && (actions.hand[dominant].grip > 0.5f || (actions.hand[dominant].buttons & IW_XR_BUTTON_GRIP)) && (actions.hand[offhand].grip > 0.5f || (actions.hand[offhand].buttons & IW_XR_BUTTON_GRIP)))
	{
		float dx = offhand_position[0] - hand_position[0];
		float dy = offhand_position[1] - hand_position[1];
		float dz = offhand_position[2] - hand_position[2];
		float distance = sqrtf(dx * dx + dy * dy + dz * dz);
		float planar = sqrtf(dx * dx + dz * dz);
		if (distance > 0.05f && distance < 0.50f && planar > 0.05f)
		{
			forward_xr[0] = dx / distance;
			forward_xr[1] = dy / distance;
			forward_xr[2] = dz / distance;
			stabilized = true;
		}
	}
	R_XRRotateOrientation(hand_orientation, xr_right, right_xr);
	R_XRRotateOrientation(hand_orientation, xr_up, up_xr);
	R_XRToQuakePosition(forward_xr, forward);
	R_XRToQuakePosition(right_xr, right);
	R_XRToQuakePosition(up_xr, up);
	R_XRRotateYaw(forward, r_xr_game_anchor_yaw - r_xr_head_anchor_yaw);
	R_XRRotateYaw(right, r_xr_game_anchor_yaw - r_xr_head_anchor_yaw);
	R_XRRotateYaw(up, r_xr_game_anchor_yaw - r_xr_head_anchor_yaw);
	R_XRToQuakePosition(dominant_forward_xr, dominant_forward);
	R_XRRotateYaw(dominant_forward, r_xr_game_anchor_yaw - r_xr_head_anchor_yaw);
	if (stabilized)
	{
		vec3_t dominant_angles, stabilized_angles;
		float planar = sqrtf(forward[0] * forward[0] + forward[1] * forward[1]);
		R_XRWeaponAngles(dominant_forward, right, up, dominant_angles);
		stabilized_angles[PITCH] = RAD2DEG(atan2f(-forward[2], planar));
		stabilized_angles[YAW] = RAD2DEG(atan2f(forward[1], forward[0]));
		stabilized_angles[ROLL] = dominant_angles[ROLL];
		AngleVectors(stabilized_angles, forward, right, up);
	}
	VectorCopy(forward, r_xr_controller_forward);
	VectorScale(right, -1.f, r_xr_controller_right);
	VectorCopy(up, r_xr_controller_up);
	r_xr_controller_aim_valid = true;
	{
		r_xr_weapon_pitch = stabilized ? 0.f : vr_weapon_pitch.value;
		float correction = DEG2RAD(r_xr_weapon_pitch);
		float c = cosf(correction), s = sinf(correction);
		vec3_t model_right, controller_forward, controller_up, model_forward, model_up;
		VectorScale(right, -1.f, model_right);
		VectorCopy(r_xr_controller_forward, controller_forward);
		VectorCopy(r_xr_controller_up, controller_up);
		VectorScale(controller_forward, c, r_xr_controller_forward);
		VectorMA(r_xr_controller_forward, s, controller_up, r_xr_controller_forward);
		VectorScale(controller_up, c, r_xr_controller_up);
		VectorMA(r_xr_controller_up, -s, controller_forward, r_xr_controller_up);
		VectorCopy(forward, model_forward);
		VectorCopy(up, model_up);
		VectorScale(model_forward, c, r_xr_viewmodel_forward);
		VectorMA(r_xr_viewmodel_forward, s, model_up, r_xr_viewmodel_forward);
		VectorScale(model_up, c, r_xr_viewmodel_up);
		VectorMA(r_xr_viewmodel_up, -s, model_forward, r_xr_viewmodel_up);
		VectorCopy(model_right, r_xr_viewmodel_right);
		R_GetXRWeaponVisualOrigin (XR_Input_PhysicalHandForRole (XR_HAND_MAINHAND), cl.viewent.model, cl.viewent.origin);
	}
	r_xr_viewmodel_orientation_valid = true;
	R_XRWeaponAngles(forward, right, up, cl.viewent.angles);
	cl.viewent.angles[PITCH] = -cl.viewent.angles[PITCH];
}

void R_SetXREye (const iw_xr_frame_snapshot_t *snapshot, unsigned eye)
{
	const float xr_forward[3] = { 0.f, 0.f, -1.f };
	const float xr_right[3] = { 1.f, 0.f, 0.f };
	const float xr_up[3] = { 0.f, 1.f, 0.f };
	float forward_xr[3], right_xr[3], up_xr[3];
	float head_pitch, head_yaw, head_roll;
	float dx, dy, dz, ipd, separation;

	if (!snapshot || eye >= 2)
		return;
	/* Re-anchor after server map changes and large player teleports. */
	if (strcmp (r_xr_anchor_mapname, cl.mapname) != 0)
	{
		q_strlcpy (r_xr_anchor_mapname, cl.mapname, sizeof (r_xr_anchor_mapname));
		r_xr_head_anchor_valid = false;
		r_xr_sync_player_yaw = false;
		r_xr_view_basis_valid = false;
		r_xr_anchor_origin_valid = false;
	}
	if (cl.viewentity > 0 && cl.viewentity < cl.num_entities)
	{
		vec3_t origin_delta;
		VectorSubtract (cl_entities[cl.viewentity].origin, r_xr_anchor_origin, origin_delta);
		if (r_xr_anchor_origin_valid &&
			(fabsf (origin_delta[0]) > 128.f || fabsf (origin_delta[1]) > 128.f || fabsf (origin_delta[2]) > 128.f))
		{
			r_xr_head_anchor_valid = false;
			r_xr_sync_player_yaw = false;
			r_xr_view_basis_valid = false;
		}
		VectorCopy (cl_entities[cl.viewentity].origin, r_xr_anchor_origin);
		r_xr_anchor_origin_valid = true;
	}
	R_XRHeadAngles (&snapshot->views[0], &head_pitch, &head_yaw, &head_roll);
	if (!r_xr_head_anchor_valid)
	{
		if (r_xr_sync_player_yaw)
		{
			cl.viewangles[YAW] = head_yaw;
			r_refdef.viewangles[YAW] = head_yaw;
			r_xr_sync_player_yaw = false;
		}
		r_xr_head_anchor_yaw = head_yaw;

		r_xr_head_anchor_position[0] = 0.5f * (snapshot->views[0].position[0] + snapshot->views[1].position[0]);
		r_xr_head_anchor_position[1] = 0.5f * (snapshot->views[0].position[1] + snapshot->views[1].position[1]);
		r_xr_head_anchor_position[2] = 0.5f * (snapshot->views[0].position[2] + snapshot->views[1].position[2]);
		r_xr_game_anchor_yaw = r_refdef.viewangles[YAW];
		r_xr_head_anchor_valid = true;
	}

	R_XRRotateVector (&snapshot->views[0], xr_forward, forward_xr);
	R_XRRotateVector (&snapshot->views[0], xr_right, right_xr);
	R_XRRotateVector (&snapshot->views[0], xr_up, up_xr);
	R_XRToQuakePosition (forward_xr, r_xr_forward);
	R_XRToQuakePosition (right_xr, r_xr_right);
	R_XRToQuakePosition (up_xr, r_xr_up);
	R_XRRotateYaw (r_xr_forward, r_xr_game_anchor_yaw - r_xr_head_anchor_yaw);
	R_XRRotateYaw (r_xr_right, r_xr_game_anchor_yaw - r_xr_head_anchor_yaw);
	R_XRRotateYaw (r_xr_up, r_xr_game_anchor_yaw - r_xr_head_anchor_yaw);
	r_xr_view_basis_valid = true;

	r_refdef.viewangles[YAW] = r_xr_game_anchor_yaw + (head_yaw - r_xr_head_anchor_yaw);

	cl.viewangles[YAW] = r_refdef.viewangles[YAW];
	r_refdef.viewangles[PITCH] = head_pitch;
	r_refdef.viewangles[ROLL] = head_roll;

	{
		float head_delta_xr[3], head_delta[3];
		head_delta_xr[0] = 0.5f * (snapshot->views[0].position[0] + snapshot->views[1].position[0]) - r_xr_head_anchor_position[0];
		head_delta_xr[1] = 0.5f * (snapshot->views[0].position[1] + snapshot->views[1].position[1]) - r_xr_head_anchor_position[1];
		head_delta_xr[2] = 0.5f * (snapshot->views[0].position[2] + snapshot->views[1].position[2]) - r_xr_head_anchor_position[2];
		R_XRToQuakePosition (head_delta_xr, head_delta);
		R_XRRotateYaw (head_delta, r_xr_game_anchor_yaw - r_xr_head_anchor_yaw);
		/* Keep the engine viewheight; only tracked vertical motion offsets it. */
		r_refdef.vieworg[2] += head_delta[2] * vr_world_scale.value;
	}
	if (vr_roomscale.value != 0.f && sv.active && svs.maxclients == 1 &&
		svs.clients[0].active && svs.clients[0].edict && cl.viewentity > 0 && cl.viewentity < cl.num_entities)
	{
		vec3_t server_delta;
		VectorSubtract (svs.clients[0].edict->v.origin, cl_entities[cl.viewentity].origin, server_delta);
		VectorAdd (r_refdef.vieworg, server_delta, r_refdef.vieworg);
	}
    if (!r_xr_stair_smooth_valid || r_xr_stair_smooth_time != cl.time)
    {
        double stair_dt = CLAMP (0.0, cl.time - r_xr_stair_smooth_time, 0.1);
        float stair_step = q_min ((float)stair_dt * 160.f, 2.25f);
        // Keep a low presentation rate from turning a fixed correction speed into visible stair jumps.
        if (cl.onground)
            r_xr_stair_ground_time = cl.time;
        // A short descent remains stair movement; a larger drop must still snap to avoid a floating fall.
        if (vr_smooth_stairs.value == 0.f || !r_xr_stair_smooth_valid || (cl.time - r_xr_stair_ground_time > 0.12 && r_refdef.vieworg[2] < r_xr_stair_smooth_z - 24.f))
            r_xr_stair_smooth_z = r_refdef.vieworg[2];
        else if (r_xr_stair_smooth_z < r_refdef.vieworg[2])
            r_xr_stair_smooth_z = CLAMP (r_refdef.vieworg[2] - 18.f, r_xr_stair_smooth_z + stair_step, r_refdef.vieworg[2]);
        else if (r_xr_stair_smooth_z > r_refdef.vieworg[2])
            r_xr_stair_smooth_z = CLAMP (r_refdef.vieworg[2], r_xr_stair_smooth_z - stair_step, r_refdef.vieworg[2] + 18.f);
        r_xr_stair_smooth_time = cl.time;
        r_xr_stair_smooth_valid = true;
    }
    r_refdef.vieworg[2] = r_xr_stair_smooth_z;
    VectorCopy (r_refdef.vieworg, r_xr_center_vieworg);
	dx = snapshot->views[1].position[0] - snapshot->views[0].position[0];
	dy = snapshot->views[1].position[1] - snapshot->views[0].position[1];
	dz = snapshot->views[1].position[2] - snapshot->views[0].position[2];
	ipd = sqrtf (dx * dx + dy * dy + dz * dz);
	r_xr_ipd = ipd;
	separation = vr_world_scale.value * ipd * (0.5f - (float)eye);
	VectorMA (r_refdef.vieworg, -separation, r_xr_right, r_refdef.vieworg);

	r_xr_fov = snapshot->views[eye].fov;
	r_xr_asymmetric_projection = true;
	r_xr_eye_pass = true;
	r_xr_eye_index = eye;
	r_xr_viewmodel_orientation_valid = false;
	R_XRApplyWeaponPose(snapshot);
	CL_UpdateTEntsXR ();
}
void R_ClearXREye (void)
{
	r_xr_asymmetric_projection = false;
	r_xr_eye_pass = false;
	r_xr_view_basis_valid = false;
	R_SetXRFinalTarget (0, 0, 0);
}
void R_RenderView (void)
{
	double	time1, time2;

	if (r_norefresh.value)
		return;

	if (!cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");
#if defined(ANDROID_GLES3)
	if (r_refdef.vrect.width <= 0 || r_refdef.vrect.height <= 0)
		return;
#endif
	time1 = 0; /* avoid compiler warning */
	if (r_speeds.value)
	{
		#if defined(ANDROID_GLES3)
		if (!r_gles_perf_capture.value)
#endif
		glFinish ();
		time1 = Sys_DoubleTime ();

		//johnfitz -- rendering statistics
		rs_brushpolys = rs_aliaspolys = rs_skypolys =
		rs_dynamiclightmaps = rs_aliaspasses = rs_skypasses = rs_brushpasses = 0;
	}
	else if (gl_finish.value)
		glFinish ();

	R_SetupView (); //johnfitz -- this does everything that should be done once per frame
    R_UploadFrameData ();
	R_RenderScene ();
	R_WarpScaleView ();

	//johnfitz -- modified r_speeds output
	time2 = Sys_DoubleTime ();
	if (r_pos.value)
		Con_Printf ("x %i y %i z %i (pitch %i yaw %i roll %i)\n",
					(int)cl_entities[cl.viewentity].origin[0],
					(int)cl_entities[cl.viewentity].origin[1],
					(int)cl_entities[cl.viewentity].origin[2],
					(int)cl.viewangles[PITCH],
					(int)cl.viewangles[YAW],
					(int)cl.viewangles[ROLL]);
	else if (r_speeds.value == 2)
		Con_Printf ("%3i ms  %4i/%4i wpoly %4i/%4i epoly %3i lmap %4i/%4i sky %1.1f mtex\n",
					(int)((time2-time1)*1000),
					rs_brushpolys,
					rs_brushpasses,
					rs_aliaspolys,
					rs_aliaspasses,
					rs_dynamiclightmaps,
					rs_skypolys,
					rs_skypasses,
					TexMgr_FrameUsage ());
	else if (r_speeds.value)
		Con_Printf ("%3i ms  %4i wpoly %4i epoly %3i lmap\n",
					(int)((time2-time1)*1000),
					rs_brushpolys,
					rs_aliaspolys,
					rs_dynamiclightmaps);
	//johnfitz
}

