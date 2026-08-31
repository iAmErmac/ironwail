#include "quakedef.h"
#include "draw.h"
#include "keys.h"
#include "menu.h"
#include "xr_interaction.h"
#include "xr_input.h"
#include "xr_action_schema.h"
#include "json.h"

extern cvar_t ui_mouse;
extern cvar_t host_timescale;
extern double host_frametime;
extern cvar_t vr_world_scale;
extern cvar_t vr_stabilize_mode;
extern cvar_t vr_teleport_beam_color, vr_teleport_beam_alpha;
extern void R_EmitLine(const vec3_t a, const vec3_t b, uint32_t color);
extern entity_t *CL_NewTempEntity(void);
extern void VID_XR_Haptic(int hand, float amplitude, float duration_seconds);

static cvar_t vr_weaponwheel = {"vr_weaponwheel", "1", CVAR_ARCHIVE};
static cvar_t vr_weaponwheel_slowmo = {"vr_weaponwheel_slowmo", "0.3", CVAR_ARCHIVE};
static cvar_t vr_weaponwheel_distance = {"vr_weaponwheel_distance", "0.35", CVAR_ARCHIVE};
static cvar_t vr_weaponwheel_radius = {"vr_weaponwheel_radius", "0.2", CVAR_ARCHIVE};
static cvar_t vr_weaponwheel_modelsize = {"vr_weaponwheel_modelsize", "0.11", CVAR_ARCHIVE};
static cvar_t vr_weaponwheel_modelpitch = {"vr_weaponwheel_modelpitch", "20", CVAR_ARCHIVE};
static cvar_t vr_weaponwheel_modelyaw = {"vr_weaponwheel_modelyaw", "-145", CVAR_ARCHIVE};
static cvar_t vr_weaponwheel_spin = {"vr_weaponwheel_spin", "45", CVAR_ARCHIVE};
static cvar_t vr_weaponwheel_deflection = {"vr_weaponwheel_deflection", "22.5", CVAR_ARCHIVE};
cvar_t vr_melee = {"vr_melee", "1", CVAR_ARCHIVE};
cvar_t vr_melee_velocity = {"vr_melee_velocity", "2.5", CVAR_ARCHIVE};
cvar_t vr_melee_cooldown = {"vr_melee_cooldown", "0.15", CVAR_ARCHIVE};
cvar_t vr_melee_damage_scale = {"vr_melee_damage_scale", "1.0", CVAR_ARCHIVE};
cvar_t vr_melee_pitch = {"vr_melee_pitch", "45", CVAR_ARCHIVE};
cvar_t vr_melee_debug = {"vr_melee_debug", "0", CVAR_ARCHIVE};
cvar_t vr_weapon_melee = {"vr_weapon_melee", "0", CVAR_ARCHIVE};
cvar_t vr_weapon_melee_damage_scale = {"vr_weapon_melee_damage_scale", "0.3", CVAR_ARCHIVE};
cvar_t vr_comfort_vignette = {"vr_comfort_vignette", "0", CVAR_ARCHIVE};
cvar_t vr_comfort_vignette_strength = {"vr_comfort_vignette_strength", "0.6", CVAR_ARCHIVE};
cvar_t vr_mouse = {"vr_mouse", "0", CVAR_ARCHIVE};
cvar_t vr_mouse_color = {"vr_mouse_color", "FFFFFF", CVAR_ARCHIVE};
cvar_t vr_mouse_alpha = {"vr_mouse_alpha", "0.4", CVAR_ARCHIVE};
cvar_t vr_aim_beam = {"vr_aim_beam", "1", CVAR_ARCHIVE};
cvar_t vr_aim_beam_width = {"vr_aim_beam_width", "2.0", CVAR_ARCHIVE};

typedef struct { int item; int impulse; int replaces_item; int local_field_value; const char *name; qmodel_t *model; xr_melee_mode_t melee_mode; qboolean melee_explicit; float melee_damage_scale; qboolean melee_damage_explicit; } xr_weapon_slot_t;
static xr_weapon_slot_t xr_weapons[16] = {
    {IT_AXE, 1, 0, 0, "Axe"}, {IT_SHOTGUN, 2, 0, 0, "Shotgun"},
    {IT_SUPER_SHOTGUN, 3, 0, 0, "Super Shotgun"}, {IT_NAILGUN, 4, 0, 0, "Nailgun"},
    {IT_SUPER_NAILGUN, 5, 0, 0, "Super Nailgun"}, {IT_GRENADE_LAUNCHER, 6, 0, 0, "Grenade Launcher"},
    {IT_ROCKET_LAUNCHER, 7, 0, 0, "Rocket Launcher"}, {IT_LIGHTNING, 8, 0, 0, "Thunderbolt"}
};
static int xr_weapon_count = 8;
static const char *xr_builtin_model_paths[] = {
    "progs/v_axe.mdl", "progs/v_shot.mdl", "progs/v_shot2.mdl", "progs/v_nail.mdl",
    "progs/v_nail2.mdl", "progs/v_rock.mdl", "progs/v_rock2.mdl", "progs/v_light.mdl"
};
static char xr_weapon_names[16][64];
static char xr_weapon_models[16][MAX_QPATH];
static char xr_weapon_local_fields[16][32];
static int xr_weapon_local_field_values[16];

static qboolean wheel_active, wheel_bind_active, offhand_wheel_bind_active, offhand_attack_active, keyboard_active, keyboard_trigger, keyboard_select, keyboard_caps, keyboard_trigger_suppressed, virtual_mouse_trigger, virtual_mouse_trigger_suppressed, two_hand_wheel_suppressed, two_hand_mode_active;
static int wheel_selection = -1, keyboard_mode, wheel_hand, keyboard_row, keyboard_col, keyboard_nav_x, keyboard_nav_y, menu_scroll_direction;
// Each hand owns a presentation weapon; Quake retains one authoritative weapon selection.
static int offhand_weapon_item;
static struct { qboolean active; int item, expected_main, haptic_hand; double deadline; } offhand_transfer;
// Netplay wraps one canonical attack with acknowledged offhand select/fire/restore phases.
enum { XR_OFFHAND_IDLE, XR_OFFHAND_WAIT_SELECT, XR_OFFHAND_FIRING, XR_OFFHAND_WAIT_RESTORE };
static struct {
    int phase, item, main_item;
    int main_impulse;
    int pending_impulse;
    qboolean selection_sent;
    qboolean main_fire_pending;
    qboolean restore_sent;
    float presentation_frame;
    qboolean presentation_frame_valid;
    double deadline;
} offhand_fire;
static struct { qboolean executing, animation_active; float frame, attack_finished, animation_start_frame; double next_attack_time, animation_start_time, animation_end_time; } offhand_local_fire;
static iw_xr_hand_t visual_fire_hand;
static double visual_fire_deadline;
static qboolean visual_fire_active[IW_XR_HAND_COUNT], visual_fire_second[IW_XR_HAND_COUNT];
static int visual_fire_weapon[IW_XR_HAND_COUNT];
typedef struct
{
    qboolean valid;
    iw_xr_hand_t hand;
    int weapon;
    qboolean second_offset;
    double expire;
} xr_local_projectile_spawn_t;
static xr_local_projectile_spawn_t xr_local_projectile_spawns[MAX_EDICTS];
static qboolean offhand_fire_input_suppressed;
static qboolean main_fire_input_active;
static qboolean local_offhand_fired_this_frame;
static qboolean network_visual_fire_pending;
static iw_xr_hand_t network_visual_fire_hand;
static int network_visual_fire_weapon;
static double network_visual_fire_deadline;
static double fire_haptic_next_time[IW_XR_HAND_COUNT];
static struct { int entity; double deadline; } offhand_beam;

static qboolean offhand_continuous_auto_sound_played;

static int xr_server_active_weapon_item(void);

typedef struct {
    float speed;
    float haptic_scale;
    qboolean armed;
    qboolean initialized;
    qboolean haptic_pending;
    int weapon;
    xr_melee_mode_t mode;
    double last_swing_time;
} xr_melee_hand_state_t;
static xr_melee_hand_state_t xr_melee_hands[IW_XR_HAND_COUNT];
static xr_melee_attack_t xr_melee_pulses[IW_XR_HAND_COUNT];
static xr_melee_attack_t xr_melee_damage_context;
static double xr_melee_velocity_warning_time;

/* A disposable QuakeC attack must never leak timing state into the next offhand weapon. */
static void xr_offhand_reset_local_fire(void)
{
    memset(&offhand_local_fire, 0, sizeof(offhand_local_fire));
    offhand_beam.entity = 0;
    offhand_beam.deadline = 0.0;
    offhand_continuous_auto_sound_played = false;
}

static void xr_mark_visual_fire (iw_xr_hand_t hand)
{
    visual_fire_hand = hand;
    visual_fire_deadline = realtime + 0.25;
}

static void xr_notify_network_fire(iw_xr_hand_t hand, int weapon)
{
    float amplitude = 0.35f;
    double interval = 0.1;
    xr_melee_mode_t mode;
    if (hand < 0 || hand >= IW_XR_HAND_COUNT || realtime < fire_haptic_next_time[hand])
        return;
    mode = xr_melee_hands[hand].mode;
    if (xr_melee_hands[hand].haptic_pending)
    {
        amplitude = CLAMP(0.f, 0.35f * xr_melee_hands[hand].haptic_scale, 1.f);
        xr_melee_hands[hand].haptic_pending = false;
    }
    else if (mode == XR_MELEE_AXE || mode == XR_MELEE_MJOLNIR)
        interval = 0.5;
    else if (weapon == IT_NAILGUN || weapon == IT_SUPER_NAILGUN || weapon == IT_LIGHTNING)
        interval = 0.1;
    else
        interval = 0.2;
    VID_XR_Haptic(hand, amplitude, 0.025f);
    fire_haptic_next_time[hand] = realtime + interval;
}

static void xr_update_visual_fire_state(iw_xr_hand_t hand, qboolean active, int weapon)
{
    if (hand < 0 || hand >= IW_XR_HAND_COUNT) return;
    if (!active)
    {
        visual_fire_active[hand] = false;
        visual_fire_second[hand] = false;
        visual_fire_weapon[hand] = 0;
        return;
    }
    if (!visual_fire_active[hand] || visual_fire_weapon[hand] != weapon)
        visual_fire_second[hand] = false;
    visual_fire_active[hand] = true;
    visual_fire_weapon[hand] = weapon;
}

qboolean XR_Interaction_GetVisualFireHand (iw_xr_hand_t *hand)
{
    if (!hand) return false;
    *hand = visual_fire_deadline > realtime ? visual_fire_hand : XR_Input_PhysicalHandForRole (XR_HAND_MAINHAND);
    return true;
}

qboolean XR_Interaction_UseSecondVisualProjectileOffset(iw_xr_hand_t hand)
{
    qboolean second;
    if (hand < 0 || hand >= IW_XR_HAND_COUNT) return false;
    second = visual_fire_second[hand];
    visual_fire_second[hand] = !visual_fire_second[hand];
    return second;
}

void XR_Interaction_RecordLocalProjectileSpawn(int entity, iw_xr_hand_t hand, int weapon)
{
    xr_local_projectile_spawn_t *spawn;
    if (cl.maxclients != 1 || entity <= 0 || entity >= MAX_EDICTS || hand < 0 || hand >= IW_XR_HAND_COUNT)
        return;
    spawn = &xr_local_projectile_spawns[entity];
    memset(spawn, 0, sizeof(*spawn));
    spawn->valid = true;
    spawn->hand = hand;
    spawn->weapon = weapon;
    if (!visual_fire_active[hand])
        visual_fire_second[hand] = false;
    spawn->second_offset = visual_fire_second[hand];
    visual_fire_second[hand] = !visual_fire_second[hand];
    spawn->expire = realtime + 0.5;
}

qboolean XR_Interaction_ConsumeLocalProjectileSpawn(int entity, iw_xr_hand_t *hand, int *weapon, qboolean *second_offset)
{
    xr_local_projectile_spawn_t *spawn;
    if (entity <= 0 || entity >= MAX_EDICTS || !hand || !weapon || !second_offset)
        return false;
    spawn = &xr_local_projectile_spawns[entity];
    if (!spawn->valid || realtime >= spawn->expire)
    {
        memset(spawn, 0, sizeof(*spawn));
        return false;
    }
    *hand = spawn->hand;
    *weapon = spawn->weapon;
    *second_offset = spawn->second_offset;
    memset(spawn, 0, sizeof(*spawn));
    return true;
}

qboolean XR_Interaction_ConsumeNetworkProjectileVisual(iw_xr_hand_t *hand, int *weapon, qboolean *second_offset)
{
    if (cl.maxclients <= 1 || !network_visual_fire_pending || realtime >= network_visual_fire_deadline ||
        !hand || !weapon || !second_offset)
    {
        if (realtime >= network_visual_fire_deadline)
            network_visual_fire_pending = false;
        return false;
    }
    *hand = network_visual_fire_hand;
    *weapon = network_visual_fire_weapon;
    *second_offset = XR_Interaction_UseSecondVisualProjectileOffset(*hand);
    network_visual_fire_pending = false;
    return true;
}

qboolean XR_Interaction_PeekNetworkProjectileVisual(iw_xr_hand_t *hand, int *weapon)
{
    if (cl.maxclients <= 1 || !network_visual_fire_pending || realtime >= network_visual_fire_deadline || !hand || !weapon)
    {
        if (realtime >= network_visual_fire_deadline)
            network_visual_fire_pending = false;
        return false;
    }
    *hand = network_visual_fire_hand;
    *weapon = network_visual_fire_weapon;
    return true;
}

void XR_Interaction_ClearLocalProjectileSpawns(void)
{
    memset(xr_local_projectile_spawns, 0, sizeof(xr_local_projectile_spawns));
}

qboolean XR_Interaction_AllowOffhandFireInput(void)
{
    return !offhand_fire_input_suppressed && !two_hand_mode_active;
}

void XR_Interaction_NotifyWeaponFire(iw_xr_hand_t hand, float previous_attack_finished, float attack_finished, double time)
{
    float cadence, amplitude = 0.35f;
    if (hand < 0 || hand >= IW_XR_HAND_COUNT || attack_finished <= time + 0.001 ||
        fabsf(attack_finished - previous_attack_finished) <= 0.001f || realtime < fire_haptic_next_time[hand])
        return;
    if (xr_melee_hands[hand].haptic_pending)
    {
        amplitude = CLAMP(0.f, 0.35f * xr_melee_hands[hand].haptic_scale, 1.f);
        xr_melee_hands[hand].haptic_pending = false;
    }
    VID_XR_Haptic(hand, amplitude, 0.025f);
    cadence = (float)(attack_finished - time);
    fire_haptic_next_time[hand] = realtime + CLAMP(0.025f, cadence, 0.5f);
}
static entity_t offhand_fire_main_viewmodel;
static qboolean offhand_fire_main_viewmodel_valid;
static float keyboard_x = 0.5f, keyboard_y = 0.5f, virtual_mouse_x = 0.5f, virtual_mouse_y = 0.5f, vignette_value, vignette_last_yaw;
static qpic_t *xr_vignette_pic;
static qboolean vignette_yaw_valid;
static double wheel_saved_timescale;
static qboolean wheel_slowmo_active;
static vec3_t wheel_origin, wheel_forward, wheel_right, wheel_up, pointer_start_xr, pointer_hit_xr;
static qboolean pointer_active;
static float wheel_cursor[2];
static entity_t wheel_entities[17];
static int wheel_entity_count;
static float wheel_grow[16], wheel_spin;
static double wheel_spin_time;

static void xr_weaponwheel_reload_f(void);
static void xr_weaponwheel_resolve_models(void);
static void xr_weapon_apply_melee_metadata(void);
static iw_xr_hand_t xr_mainhand(void) { return XR_Input_PhysicalHandForRole(XR_HAND_MAINHAND); }

static xr_melee_mode_t xr_builtin_melee_mode(int item)
{
    if (item == IT_AXE || item == RIT_AXE) return XR_MELEE_AXE;
    if (item == HIT_MJOLNIR) return XR_MELEE_MJOLNIR;
    return XR_MELEE_NONE;
}

static xr_melee_mode_t xr_parse_melee_mode(const char *name, qboolean *valid)
{
    if (valid) *valid = false;
    if (!name) return XR_MELEE_NONE;
    if (!q_strcasecmp(name, "axe")) { if (valid) *valid = true; return XR_MELEE_AXE; }
    if (!q_strcasecmp(name, "mjolnir")) { if (valid) *valid = true; return XR_MELEE_MJOLNIR; }
    if (!q_strcasecmp(name, "fist")) { if (valid) *valid = true; return XR_MELEE_FIST; }
    if (!q_strcasecmp(name, "gunbutt")) { if (valid) *valid = true; return XR_MELEE_GUNBUTT; }
    if (!q_strcasecmp(name, "none")) { if (valid) *valid = true; return XR_MELEE_NONE; }
    return XR_MELEE_NONE;
}

static void xr_weapon_slot_set_builtin_melee(xr_weapon_slot_t *slot)
{
    if (!slot) return;
    slot->melee_mode = xr_builtin_melee_mode(slot->item);
    slot->melee_explicit = false;
    slot->melee_damage_scale = 1.f;
    slot->melee_damage_explicit = false;
}

static xr_weapon_slot_t *xr_weapon_slot_for_item(int item)
{
    int i;
    for (i = 0; i < xr_weapon_count; ++i)
        if (xr_weapons[i].item == item) return &xr_weapons[i];
    return NULL;
}

static float xr_weapon_melee_scale_for_item(int item, xr_melee_mode_t mode)
{
    xr_weapon_slot_t *slot = xr_weapon_slot_for_item(item);
    float scale = mode == XR_MELEE_GUNBUTT ? vr_weapon_melee_damage_scale.value : vr_melee_damage_scale.value;
    if (slot && slot->melee_damage_explicit) scale *= slot->melee_damage_scale;
    return CLAMP(0.f, scale, 4.f);
}

static void xr_melee_reset_hand(iw_xr_hand_t hand)
{
    if (hand < 0 || hand >= IW_XR_HAND_COUNT) return;
    memset(&xr_melee_hands[hand], 0, sizeof(xr_melee_hands[hand]));
    xr_melee_hands[hand].armed = true;
    xr_melee_hands[hand].mode = XR_MELEE_NONE;
    memset(&xr_melee_pulses[hand], 0, sizeof(xr_melee_pulses[hand]));
}

static void xr_melee_reset_all(void)
{
    xr_melee_reset_hand(IW_XR_HAND_LEFT);
    xr_melee_reset_hand(IW_XR_HAND_RIGHT);
}

void XR_Interaction_ApplyMeleePitch(vec3_t forward, vec3_t up)
{
    float angle, c, s;
    vec3_t old_forward, old_up;
    if (!forward || !up || fabsf(vr_melee_pitch.value) < 0.001f)
        return;
    angle = DEG2RAD(vr_melee_pitch.value);
    c = cosf(angle);
    s = sinf(angle);
    VectorCopy(forward, old_forward);
    VectorCopy(up, old_up);
    VectorScale(old_forward, c, forward);
    VectorMA(forward, -s, old_up, forward);
    VectorScale(old_up, c, up);
    VectorMA(up, s, old_forward, up);
}

static qboolean xr_melee_mode_is_fallback(xr_melee_mode_t mode)
{
    return mode == XR_MELEE_FIST || mode == XR_MELEE_GUNBUTT;
}

static qboolean xr_melee_mainhand_network_safe(int item, xr_melee_mode_t mode)
{
    return (item == IT_AXE && mode == XR_MELEE_AXE) ||
        (item == RIT_AXE && mode == XR_MELEE_AXE) ||
        (item == HIT_MJOLNIR && mode == XR_MELEE_MJOLNIR);
}

static float xr_melee_forward_fraction(const iw_xr_hand_snapshot_t *hand, const float velocity[3])
{
    float x, y, z, w, length, forward[3];
    if (!hand || !hand->aim_valid) return 0.f;
    x = hand->aim_orientation[0]; y = hand->aim_orientation[1];
    z = hand->aim_orientation[2]; w = hand->aim_orientation[3];
    forward[0] = -2.f * (x * z + w * y);
    forward[1] = -2.f * (y * z - w * x);
    forward[2] = -1.f + 2.f * (x * x + y * y);
    length = sqrtf(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (length < 0.5f) return 0.f;
    return DotProduct(velocity, forward) / length;
}

static void xr_melee_update(const iw_xr_action_snapshot_t *actions, int dominant, int offhand)
{
    int hand;
    int weapons[IW_XR_HAND_COUNT];
    qboolean triggers[IW_XR_HAND_COUNT];
    float threshold = CLAMP(.1f, vr_melee_velocity.value, 6.f);
    float release = threshold * .65f;
    double now = realtime;
    double cooldown = CLAMP(0.f, vr_melee_cooldown.value, 5.f);
    float dt = (float)CLAMP(.001, host_frametime, .25);
    float filter = 1.f - expf(-dt / .05f);

    memset(xr_melee_pulses, 0, sizeof(xr_melee_pulses));
    for (hand = 0; hand < IW_XR_HAND_COUNT; ++hand)
        xr_melee_hands[hand].haptic_pending = false;
    // The network transaction may temporarily select the offhand weapon; melee ownership must
    // follow the preserved main-hand presentation rather than that transient server selection.
    weapons[dominant] = XR_Interaction_MainhandWeaponItem();
    weapons[offhand] = offhand_weapon_item;
    triggers[dominant] = (actions->hand[dominant].buttons & IW_XR_BUTTON_TRIGGER) != 0;
    triggers[offhand] = (actions->hand[offhand].buttons & IW_XR_BUTTON_TRIGGER) != 0;

    for (hand = 0; hand < IW_XR_HAND_COUNT; ++hand)
    {
        xr_melee_hand_state_t *state = &xr_melee_hands[hand];
        xr_melee_mode_t mode = XR_MELEE_NONE;
        float speed, forward_fraction;
        qboolean blocked;

        if (hand == offhand && !weapons[hand]) mode = XR_MELEE_FIST;
        else mode = XR_Interaction_GetWeaponMeleeMode(weapons[hand]);
        if (state->initialized && (state->weapon != weapons[hand] || state->mode != mode))
            xr_melee_reset_hand((iw_xr_hand_t)hand);
        state = &xr_melee_hands[hand];
        state->weapon = weapons[hand];
        state->mode = mode;
        state->initialized = true;

        // Custom XR traces and damage cannot be reproduced by a remote server; online melee is ID-gated.
        blocked = !vr_melee.value || key_dest != key_game || cl.intermission || sv.paused || cl.stats[STAT_HEALTH] <= 0 ||
            wheel_active || keyboard_active || virtual_mouse_trigger || XR_Input_IsTeleportAiming() ||
            (hand == offhand && two_hand_mode_active) ||
            (hand == offhand && cl.maxclients > 1 && !xr_melee_mainhand_network_safe(weapons[hand], mode)) ||
            (xr_melee_mode_is_fallback(mode) && cl.maxclients > 1) ||
            (hand == dominant && cl.maxclients > 1 && mode != XR_MELEE_NONE &&
                !xr_melee_mainhand_network_safe(weapons[hand], mode));
        if (blocked || mode == XR_MELEE_NONE || !actions->hand[hand].velocity_valid)
        {
            state->speed = 0.f;
            state->armed = true;
            state->last_swing_time = 0.0;
            if (!actions->hand[hand].velocity_valid && actions->hand[hand].aim_valid && realtime >= xr_melee_velocity_warning_time)
            {
                Con_Printf("XR melee: controller linear velocity unavailable; motion attacks disabled until it is valid.\n");
                xr_melee_velocity_warning_time = realtime + 5.0;
            }
            continue;
        }

        speed = sqrtf(actions->hand[hand].linear_velocity[0] * actions->hand[hand].linear_velocity[0] +
            actions->hand[hand].linear_velocity[1] * actions->hand[hand].linear_velocity[1] +
            actions->hand[hand].linear_velocity[2] * actions->hand[hand].linear_velocity[2]);
        if (IS_NAN(speed)) speed = 0.f;
        speed = CLAMP(0.f, speed, 6.f);
        state->speed += (speed - state->speed) * filter;
        if (state->speed <= release) state->armed = true;
        if (triggers[hand]) state->armed = false;
        /* Ignore the rearward wind-up so it cannot consume the forward chop pulse. */
        forward_fraction = xr_melee_forward_fraction(&actions->hand[hand], actions->hand[hand].linear_velocity);
        if (state->armed && state->speed >= threshold && forward_fraction >= -0.2f &&
            now >= state->last_swing_time + cooldown && !triggers[hand])
        {
            state->armed = false;
            xr_melee_pulses[hand].valid = true;
            xr_melee_pulses[hand].hand = (iw_xr_hand_t)hand;
            xr_melee_pulses[hand].mode = mode;
            xr_melee_pulses[hand].speed = state->speed;
            xr_melee_pulses[hand].damage_scale = xr_weapon_melee_scale_for_item(weapons[hand], mode);
            if (mode == XR_MELEE_AXE || mode == XR_MELEE_MJOLNIR)
            {
                state->haptic_scale = xr_melee_pulses[hand].damage_scale;
                state->haptic_pending = true;
            }
            if (vr_melee_debug.value)
                Con_Printf("XR melee: hand=%d speed=%.2f mode=%d scale=%.2f\n", hand, state->speed, mode, xr_melee_pulses[hand].damage_scale);
        }
    }
}

static qboolean xr_offhand_continuous_weapon (void) {
    return offhand_weapon_item == IT_NAILGUN || offhand_weapon_item == IT_SUPER_NAILGUN ||
        offhand_weapon_item == IT_LIGHTNING || offhand_weapon_item == HIT_LASER_CANNON ||
        (rogue && (offhand_weapon_item == RIT_LAVA_NAILGUN || offhand_weapon_item == RIT_LAVA_SUPER_NAILGUN));
}
static void xr_weaponwheel_bind_down(void) { wheel_bind_active = true; }
static void xr_weaponwheel_bind_up(void) { wheel_bind_active = false; }
static void xr_offhand_weaponwheel_bind_down(void) { offhand_wheel_bind_active = true; }
static void xr_offhand_weaponwheel_bind_up(void) { offhand_wheel_bind_active = false; }
static void xr_offhand_attack_bind_down(void) { offhand_attack_active = true; }
static void xr_offhand_attack_bind_up(void) { offhand_attack_active = false; }

static qboolean xr_wheel_pose(int hand, vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
    if (hand == xr_mainhand())
        return R_GetXRMainHandWeaponPose(origin, forward, right, up);
    return R_GetXRHandAimPose(hand, origin, forward, right, up);
}

static qboolean xr_can_wheel(void)
{
    return vr_weaponwheel.value != 0.f && key_dest == key_game &&
        cls.state == ca_connected && cls.signon == SIGNONS && cl.stats[STAT_HEALTH] > 0 &&
        !cl.intermission && !cls.demoplayback;
}

static void xr_wheel_close(void)
{
    wheel_active = false;
    wheel_selection = -1;
    if (wheel_slowmo_active) {
        Cvar_SetValueQuick(&host_timescale, (float)wheel_saved_timescale);
        wheel_slowmo_active = false;
    }
}

static void xr_wheel_open(int hand)
{
    if (!xr_can_wheel() || wheel_active) return;
    xr_weaponwheel_reload_f();
    xr_weaponwheel_resolve_models();
    if (!xr_wheel_pose(hand, wheel_origin, NULL, NULL, NULL) || !xr_wheel_pose(hand, NULL, wheel_forward, wheel_right, wheel_up)) return;
    wheel_active = true;
    wheel_hand = hand;
    VID_XR_Haptic(wheel_hand, 0.35f, 0.03f);
    wheel_cursor[0] = wheel_cursor[1] = 0.f;
    wheel_selection = -1;
    if (cl.maxclients <= 1 && vr_weaponwheel_slowmo.value > 0.f && vr_weaponwheel_slowmo.value < 1.f) {
        wheel_saved_timescale = host_timescale.value;
        wheel_slowmo_active = true;
        Cvar_SetValueQuick(&host_timescale, vr_weaponwheel_slowmo.value);
    }
}

static void xr_wheel_cursor_from_pose(void)
{
    vec3_t forward, right, up;
    float scale = sinf(DEG2RAD(CLAMP(5.f, vr_weaponwheel_deflection.value, 60.f)));
    if (!xr_wheel_pose(wheel_hand, NULL, forward, right, up) || scale <= 0.01f) return;
    wheel_cursor[0] = CLAMP(-1.f, DotProduct(forward, wheel_right) / scale, 1.f);
    wheel_cursor[1] = CLAMP(-1.f, DotProduct(forward, wheel_up) / scale, 1.f);
}

static qboolean xr_offhand_weapon_has_ammo (int item)
{
    if (item == HIT_LASER_CANNON) return cl.stats[STAT_CELLS] > 0;
    if (hipnotic) {
        if (item == HIT_PROXIMITY_GUN) return cl.stats[STAT_ROCKETS] > 0;
        if (item == HIT_MJOLNIR) return cl.stats[STAT_CELLS] > 0;
    }
    if (rogue) {
        if (item == RIT_LAVA_NAILGUN || item == RIT_LAVA_SUPER_NAILGUN) return cl.stats[STAT_NAILS] > 0;
        if (item == RIT_MULTI_GRENADE || item == RIT_MULTI_ROCKET) return cl.stats[STAT_ROCKETS] > 0;
        if (item == RIT_PLASMA_GUN) return cl.stats[STAT_CELLS] > 0;
        if (item == RIT_AXE) return true;
    }
    switch (item) {
    case IT_SHOTGUN: case IT_SUPER_SHOTGUN: return cl.stats[STAT_SHELLS] > 0;
    case IT_NAILGUN: case IT_SUPER_NAILGUN: return cl.stats[STAT_NAILS] > 0;
    case IT_GRENADE_LAUNCHER: case IT_ROCKET_LAUNCHER: return cl.stats[STAT_ROCKETS] > 0;
    case IT_LIGHTNING: return cl.stats[STAT_CELLS] > 0;
    default: return true;
    }
}

static qboolean xr_wheel_item_available(int item)
{
    /* cl.items is the authoritative full inventory mask; STAT_ITEMS is a presentation mirror. */
    return (cl.items & item) != 0 || (cl.stats[STAT_ITEMS] & item) != 0 || xr_server_active_weapon_item() == item;
}

static void xr_weaponwheel_update_local_fields(void)
{
    edict_t *player;
    qcvm_t *oldvm;
    int i;

    memset(xr_weapon_local_field_values, 0, sizeof(xr_weapon_local_field_values));
    if (!sv.active || cl.maxclients != 1 || !svs.clients[0].active || !svs.clients[0].edict)
        return;
    player = svs.clients[0].edict;
    PR_PushQCVM(&sv.qcvm, &oldvm);
    for (i = 0; i < xr_weapon_count; ++i) {
        eval_t *value;
        if (!xr_weapon_local_fields[i][0]) continue;
        value = GetEdictFieldValueByName(player, xr_weapon_local_fields[i]);
        if (value) xr_weapon_local_field_values[i] = (int)value->_float;
    }
    PR_PopQCVM(oldvm);
}

static qboolean xr_wheel_local_field_matches(int slot)
{
    return !xr_weapon_local_fields[slot][0] ||
        xr_weapon_local_field_values[slot] == xr_weapons[slot].local_field_value;
}

static qboolean xr_wheel_slot_owned(int slot)
{
    if (!xr_wheel_item_available(xr_weapons[slot].item) || !xr_offhand_weapon_has_ammo(xr_weapons[slot].item)) return false;
    /* Only JSON variants that opt into a local field use QuakeC-state filtering. */
    return !xr_weapon_local_fields[slot][0] || xr_wheel_local_field_matches(slot);
}

static qboolean xr_wheel_slot_visible(int slot)
{
    int i;
    if (!xr_wheel_slot_owned(slot)) return false;
    for (i = 0; i < xr_weapon_count; ++i)
        /* Replacement hiding is reserved for explicit local-state variants, not stock expansion weapons. */
        if (i != slot && xr_weapon_local_fields[i][0] && xr_weapons[i].replaces_item == xr_weapons[slot].item &&
            xr_wheel_slot_owned(i))
            return false;
    return true;
}

static int xr_weapon_slot_index(int item)
{
    int i;
    for (i = 0; i < xr_weapon_count; ++i)
        if (xr_weapons[i].item == item) return i;
    return -1;
}

static int xr_server_active_weapon_item(void)
{
    int item = cl.stats[STAT_ACTIVEWEAPON];
    int model_index = cl.stats[STAT_WEAPON];
    qmodel_t *model;
    int i;

    if (item)
        return item;
    if (model_index <= 0 || model_index >= MAX_MODELS)
        return 0;
    model = cl.model_precache[model_index];
    if (!model)
        return 0;
    for (i = 0; i < xr_weapon_count; ++i)
        if (xr_weapon_models[i][0] && !q_strcasecmp(model->name, xr_weapon_models[i]))
            return xr_weapons[i].item;
    return 0;
}

static int xr_weapon_impulse_for_item(int item)
{
    int slot = xr_weapon_slot_index(item);
    if (slot >= 0 && xr_weapons[slot].impulse > 0)
        return xr_weapons[slot].impulse;
    // Known zero-ammo melee weapons still need a normal impulse for netplay selection.
    if (item == IT_AXE || item == RIT_AXE)
        return 1;
    if (item == HIT_MJOLNIR)
        return hipnotic ? 226 : 1;
    return 0;
}

static qboolean xr_offhand_motion_pending(void)
{
    iw_xr_hand_t hand = XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND);
    return xr_melee_pulses[hand].valid;
}

static void xr_offhand_fire_clear(void)
{
    offhand_fire.phase = XR_OFFHAND_IDLE;
    offhand_fire.item = 0;
    offhand_fire.main_item = 0;
    offhand_fire.main_impulse = 0;
    offhand_fire.pending_impulse = 0;
    offhand_fire.selection_sent = false;
    offhand_fire.main_fire_pending = false;
    offhand_fire.restore_sent = false;
    offhand_fire.presentation_frame_valid = false;
    offhand_fire_main_viewmodel_valid = false;
}

static void xr_offhand_queue_restore(void)
{
    int impulse;
    if (!offhand_fire.main_item || offhand_fire.main_item == offhand_fire.item)
    {
        offhand_attack_active = false;
        memset(&xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)], 0,
            sizeof(xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)]));
        xr_offhand_fire_clear();
        return;
    }
    impulse = offhand_fire.main_impulse;
    if (!impulse)
        impulse = xr_weapon_impulse_for_item(offhand_fire.main_item);
    if (!impulse)
    {
        // Do not expose the temporary server weapon when its restore mapping is unknown.
        offhand_attack_active = false;
        memset(&xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)], 0,
            sizeof(xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)]));
        offhand_fire.phase = XR_OFFHAND_WAIT_RESTORE;
        offhand_fire.pending_impulse = 0;
        offhand_fire.restore_sent = false;
        return;
    }
    offhand_attack_active = false;
    memset(&xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)], 0,
        sizeof(xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)]));
    offhand_fire.phase = XR_OFFHAND_WAIT_RESTORE;
    offhand_fire.pending_impulse = impulse;
    offhand_fire.restore_sent = false;
    offhand_fire.deadline = realtime + 1.0;
}

static void xr_offhand_fire_cancel_external(void)
{
    offhand_attack_active = false;
    memset(&xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)], 0,
        sizeof(xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)]));
    xr_offhand_fire_clear();
}

static void xr_offhand_fire_cancel_for_main(void)
{
    if (offhand_fire.phase == XR_OFFHAND_IDLE)
    {
        offhand_attack_active = false;
        memset(&xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)], 0,
            sizeof(xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)]));
        return;
    }
    offhand_attack_active = false;
    memset(&xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)], 0,
        sizeof(xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)]));
    offhand_fire.main_fire_pending = true;
    if (offhand_fire.phase == XR_OFFHAND_WAIT_SELECT &&
        !offhand_fire.selection_sent && xr_server_active_weapon_item() != offhand_fire.item)
    {
        xr_offhand_fire_clear();
        return;
    }
    if (offhand_fire.phase == XR_OFFHAND_WAIT_SELECT &&
        xr_server_active_weapon_item() != offhand_fire.item)
    {
        // Wait for the server's selection acknowledgement before restoring.
        offhand_fire.pending_impulse = 0;
        return;
    }
    if (offhand_fire.phase == XR_OFFHAND_WAIT_RESTORE)
    {
        if (xr_server_active_weapon_item() == offhand_fire.main_item)
        {
            xr_offhand_fire_clear();
            return;
        }
        if (!offhand_fire.pending_impulse && offhand_fire.restore_sent)
        {
            int impulse = offhand_fire.main_impulse;
            if (impulse)
            {
                offhand_fire.pending_impulse = impulse;
                offhand_fire.restore_sent = false;
                offhand_fire.deadline = realtime + 1.0;
            }
        }
        // Keep the main model until the server acknowledges the restore impulse.
        return;
    }
    xr_offhand_queue_restore();
}

// Multiplayer temporarily selects the offhand weapon for the canonical network command, while preserving the main-hand viewmodel locally.
static void xr_offhand_fire_update(void)
{
    if (two_hand_mode_active)
    {
        xr_offhand_fire_cancel_for_main();
        return;
    }
    if (cl.maxclients == 1) {
        xr_offhand_fire_clear();
        offhand_fire_main_viewmodel_valid = false;
        return;
    }
    if (wheel_active)
    {
        xr_offhand_fire_cancel_for_main();
        return;
    }
    if (main_fire_input_active)
        xr_offhand_fire_cancel_for_main();

    if (offhand_fire.phase == XR_OFFHAND_IDLE)
    {
        if (main_fire_input_active)
            return;
        if ((!offhand_attack_active && !xr_offhand_motion_pending()) || !offhand_weapon_item || wheel_active)
            return;
        offhand_fire.presentation_frame_valid = false;
        offhand_fire.item = offhand_weapon_item;
        offhand_fire.main_item = xr_server_active_weapon_item();
        offhand_fire.main_impulse = xr_weapon_impulse_for_item(offhand_fire.main_item);
        offhand_fire.pending_impulse = xr_weapon_impulse_for_item(offhand_fire.item);
        if (offhand_fire.main_item != offhand_fire.item &&
            (!offhand_fire.main_impulse || !offhand_fire.pending_impulse))
        {
            xr_offhand_fire_clear();
            return;
        }
        offhand_fire_main_viewmodel = cl.viewent;
        offhand_fire_main_viewmodel_valid = true;
        offhand_fire.deadline = realtime + 1.0;
        if (xr_server_active_weapon_item() == offhand_fire.item)
            offhand_fire.phase = XR_OFFHAND_FIRING;
        else
        {
            offhand_fire.phase = XR_OFFHAND_WAIT_SELECT;
        }
        return;
    }
    if (offhand_fire.phase == XR_OFFHAND_WAIT_SELECT)
    {
        if (xr_server_active_weapon_item() == offhand_fire.item)
        {
            if (offhand_fire.main_fire_pending || (!offhand_attack_active && !xr_offhand_motion_pending()))
                xr_offhand_queue_restore();
            else
                offhand_fire.phase = XR_OFFHAND_FIRING;
        }
        else if (realtime >= offhand_fire.deadline)
        {
            if (offhand_fire.selection_sent)
            {
                // A lost selection packet must be retried before any attack or restore decision.
                offhand_fire.pending_impulse = xr_weapon_impulse_for_item(offhand_fire.item);
                offhand_fire.selection_sent = false;
                offhand_fire.deadline = realtime + 1.0;
            }
            else
            {
                if (!offhand_attack_active)
                    memset(&xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)], 0,
                        sizeof(xr_melee_pulses[XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND)]));
                xr_offhand_fire_clear();
            }
        }
        return;
    }
    if (offhand_fire.phase == XR_OFFHAND_FIRING &&
        (main_fire_input_active || (!offhand_attack_active && !xr_offhand_motion_pending())))
    {
        offhand_fire.presentation_frame = cl.viewent.frame;
        offhand_fire.presentation_frame_valid = true;
        xr_offhand_queue_restore();
    }
    else if (offhand_fire.phase == XR_OFFHAND_WAIT_RESTORE)
    {
        if (xr_server_active_weapon_item() == offhand_fire.main_item)
            xr_offhand_fire_clear();
        else if (realtime >= offhand_fire.deadline && offhand_fire.main_impulse)
        {
            // Movement packets are unreliable, so keep restoring until the server confirms it.
            offhand_fire.pending_impulse = offhand_fire.main_impulse;
            offhand_fire.restore_sent = false;
            offhand_fire.deadline = realtime + 1.0;
        }
    }
    if (offhand_fire.phase == XR_OFFHAND_IDLE)
    {
        offhand_fire_main_viewmodel_valid = false;
        offhand_fire.presentation_frame_valid = false;
    }
}
static int xr_wheel_fallback_slot(int excluded_item)
{
    int i, start = xr_weapon_slot_index(excluded_item);
    for (i = 1; i <= xr_weapon_count; ++i) {
        int slot = (start + i) % xr_weapon_count;
        if (slot >= 0 && xr_weapons[slot].item != excluded_item && xr_wheel_slot_visible(slot))
            return slot;
    }
    return -1;
}

static void xr_offhand_transfer_update(void)
{
    if (!offhand_transfer.active) return;
    if (xr_server_active_weapon_item() == offhand_transfer.expected_main) {
        offhand_weapon_item = offhand_transfer.item;
        xr_offhand_reset_local_fire();
        offhand_transfer.active = false;
        VID_XR_Haptic(offhand_transfer.haptic_hand, 0.8f, 0.08f);
    } else if (realtime >= offhand_transfer.deadline)
        offhand_transfer.active = false;
}

// Taking the active weapon waits for QuakeC to select a fallback before ownership changes, so one inventory weapon cannot appear in both hands.
static void xr_offhand_assign_weapon(int item)
{
    int fallback;

    if (item == offhand_weapon_item || offhand_transfer.active || !xr_offhand_weapon_has_ammo(item)) return;
    xr_offhand_reset_local_fire();
    if (item != xr_server_active_weapon_item()) {
        offhand_weapon_item = item;
        VID_XR_Haptic(wheel_hand, 0.8f, 0.08f);
        return;
    }

    fallback = xr_wheel_fallback_slot(item);
    if (fallback < 0) return;
    offhand_transfer.active = true;
    offhand_transfer.item = item;
    offhand_transfer.expected_main = xr_weapons[fallback].item;
    offhand_transfer.haptic_hand = wheel_hand;
    offhand_transfer.deadline = realtime + 1.0;
    VID_XR_Haptic(wheel_hand, 0.8f, 0.08f);
    Cbuf_AddText(va("impulse %i\n", xr_weapons[fallback].impulse));
}
static int xr_wheel_visible_count(void)
{
    int i, count = 0;
    for (i = 0; i < xr_weapon_count; ++i)
        if (xr_wheel_slot_visible(i)) ++count;
    return count;
}

static int xr_wheel_visible_index(int slot)
{
    int i, index = 0;
    for (i = 0; i < slot; ++i)
        if (xr_wheel_slot_visible(i)) ++index;
    return index;
}
static void xr_wheel_select(float x, float y)
{
    float length = sqrtf(x * x + y * y), angle, best_delta = 1000.f;
    int i, best = -1, count = xr_wheel_visible_count();
    if (length < 0.25f || count == 0) return;
    angle = atan2f(x, y);
    for (i = 0; i < (int)xr_weapon_count; ++i) {
        float slot_angle, delta;
        if (!xr_wheel_slot_visible(i)) continue;
        slot_angle = (float)xr_wheel_visible_index(i) * (float)(2.0 * M_PI / count);
        delta = fabsf(atan2f(sinf(angle - slot_angle), cosf(angle - slot_angle)));
        if (delta < best_delta) { best_delta = delta; best = i; }
    }
    if (best != wheel_selection && best >= 0) VID_XR_Haptic(wheel_hand, 0.6f, 0.05f);
    wheel_selection = best;
}
static void xr_mainhand_assign_weapon(int slot)
{
    if (xr_weapons[slot].item == offhand_weapon_item) {
        if (offhand_transfer.active || xr_server_active_weapon_item() == offhand_weapon_item) return;
        offhand_transfer.active = true;
        offhand_transfer.item = xr_server_active_weapon_item();
        offhand_transfer.expected_main = offhand_weapon_item;
        offhand_transfer.haptic_hand = wheel_hand;
        offhand_transfer.deadline = realtime + 1.0;
        VID_XR_Haptic(wheel_hand, 0.8f, 0.08f);
        Cbuf_AddText(va("impulse %i\n", xr_weapons[slot].impulse));
    } else if (xr_server_active_weapon_item() != xr_weapons[slot].item) {
        VID_XR_Haptic(wheel_hand, 0.8f, 0.08f);
        Cbuf_AddText(va("impulse %i\n", xr_weapons[slot].impulse));
    }
}
static void xr_wheel_commit(void)
{
    if (wheel_selection >= 0 && wheel_hand != xr_mainhand())
        xr_offhand_assign_weapon(xr_weapons[wheel_selection].item);
    else if (wheel_selection >= 0)
        xr_mainhand_assign_weapon(wheel_selection);
    xr_wheel_close();
}

static void xr_keyboard_set_selection(int row, int col);
static void xr_keyboard_close(void) { keyboard_active = false; keyboard_trigger = false; }

static void xr_keyboard_open(qboolean trigger, qboolean select)
{
    menu_scroll_direction = 0;
    keyboard_active = true;
    keyboard_mode = 0;
    keyboard_caps = false;
    keyboard_trigger = trigger;
    keyboard_select = select;
    keyboard_nav_x = keyboard_nav_y = 0;
    xr_keyboard_set_selection(0, 0);
    xr_wheel_close();
}

static void xr_keyboard_send_special(int key)
{
    if (key == K_ESCAPE) { xr_keyboard_close(); return; }
    if (key == K_ENTER) virtual_mouse_trigger_suppressed = true;
    Key_Event(key, true); Key_Event(key, false);
    if (key == K_ENTER && key_dest != key_console) xr_keyboard_close();
}

static qboolean xr_keyboard_key_at(float x, float y, int *row_out, int *col_out)
{
    const float left = 0.03125f, top = 0.50f, width = 0.9375f, height = 0.45f;
    int row, col;
    if (x < left || x >= left + width || y < top || y >= top + height) return false;
    row = (int)((y - top) * 5.f / height); col = (int)((x - left) * 10.f / width);
    if (row < 0 || row > 4 || col < 0 || col > 9) return false;
    *row_out = row; *col_out = col; return true;
}
static void xr_keyboard_set_selection(int row, int col)
{
    keyboard_row = CLAMP(0, row, 4);
    keyboard_col = CLAMP(0, col, 9);
    keyboard_x = 0.03125f + ((float)keyboard_col + 0.5f) * 0.09375f;
    keyboard_y = 0.50f + ((float)keyboard_row + 0.5f) * 0.09f;
}

static void xr_keyboard_navigate(const iw_xr_hand_snapshot_t *hand)
{
    int x = 0, y = 0;
    if (!hand) return;
    if (hand->stick[0] > 0.55f) x = 1;
    else if (hand->stick[0] < -0.55f) x = -1;
    if (hand->stick[1] > 0.55f) y = -1;
    else if (hand->stick[1] < -0.55f) y = 1;
    if ((x || y) && (x != keyboard_nav_x || y != keyboard_nav_y))
        xr_keyboard_set_selection(keyboard_row + y, keyboard_col + x);
    keyboard_nav_x = x;
    keyboard_nav_y = y;
}
static void xr_keyboard_press(void)
{
    static const char *letters[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-."};
    static const char symbols[] = "!@#$%^&*()[]{}<>?/\\|`~_=+:;\"',";
    int row, col, ch;
    if (!keyboard_active || !xr_keyboard_key_at(keyboard_x, keyboard_y, &row, &col)) return;
    if (row == 4) {
        if (col == 0) {
            if (keyboard_mode != 2) keyboard_caps = !keyboard_caps;
            return;
        }
        if (col == 1) { keyboard_mode = keyboard_mode == 1 ? 0 : 1; return; }
        if (col == 2) { keyboard_mode = keyboard_mode == 2 ? 0 : 2; return; }
        if (col >= 8) { xr_keyboard_send_special(K_ENTER); return; }
        Char_Event(' '); return;
    }
    if (row == 2 && col == 9) { xr_keyboard_send_special(K_BACKSPACE); return; }
    ch = letters[row][col];
    if (keyboard_mode == 2 && row > 0) ch = symbols[(row - 1) * 10 + col];
    else if ((keyboard_caps || keyboard_mode == 1) && row > 0) ch = toupper(ch);
    Char_Event(ch);
    if (keyboard_mode == 1) keyboard_mode = 0;
}

static unsigned xr_mouse_rgb(void)
{
    const char *text = vr_mouse_color.string;
    unsigned color;
    if (*text == '#') ++text;
    if (sscanf(text, "%x", &color) != 1) color = 0xffffff;
    return color & 0xffffff;
}

static void xr_virtual_pointer_clear(void)
{
    pointer_active = false;
    VID_XR_SetVirtualPointer(NULL, NULL, false, 0, 0.f, 1.f);
}

static void xr_virtual_pointer_update(const iw_xr_hand_snapshot_t *hand)
{
    iw_xr_virtual_screen_hit_t hit;
    xr_virtual_pointer_clear();
    if ((!vr_mouse.value && hand && hand->grip <= 0.5f && !(hand->buttons & IW_XR_BUTTON_GRIP)) || !hand || !hand->aim_valid ||
        !VID_XR_RaycastVirtualScreen(hand->aim_position, hand->aim_orientation, &hit) || !hit.valid)
        return;
    keyboard_x = virtual_mouse_x = hit.u;
    keyboard_y = virtual_mouse_y = hit.v;
    VectorCopy(hit.position, pointer_hit_xr);
    VectorCopy(hand->aim_position, pointer_start_xr);
    pointer_active = hit.inside;
    VID_XR_SetVirtualPointer(pointer_start_xr, pointer_hit_xr, vr_aim_beam.value != 0.f, xr_mouse_rgb(), CLAMP(0.f, vr_mouse_alpha.value, 1.f), CLAMP(0.25f, vr_aim_beam_width.value, 8.f));
}

static int xr_item_bit(const char *name)
{
    static const struct { const char *name; int bit; } bits[] = {
        {"IT_AXE", IT_AXE}, {"IT_SHOTGUN", IT_SHOTGUN}, {"IT_SUPER_SHOTGUN", IT_SUPER_SHOTGUN},
        {"IT_NAILGUN", IT_NAILGUN}, {"IT_SUPER_NAILGUN", IT_SUPER_NAILGUN},
        {"IT_GRENADE_LAUNCHER", IT_GRENADE_LAUNCHER}, {"IT_ROCKET_LAUNCHER", IT_ROCKET_LAUNCHER},
        {"IT_LIGHTNING", IT_LIGHTNING}, {"IT_SUPER_LIGHTNING", IT_SUPER_LIGHTNING},
        {"HIT_PROXIMITY_GUN", HIT_PROXIMITY_GUN}, {"HIT_MJOLNIR", HIT_MJOLNIR}, {"HIT_LASER_CANNON", HIT_LASER_CANNON},
        {"RIT_AXE", RIT_AXE}, {"RIT_LAVA_NAILGUN", RIT_LAVA_NAILGUN},
        {"RIT_LAVA_SUPER_NAILGUN", RIT_LAVA_SUPER_NAILGUN}, {"RIT_MULTI_GRENADE", RIT_MULTI_GRENADE},
        {"RIT_MULTI_ROCKET", RIT_MULTI_ROCKET}, {"RIT_PLASMA_GUN", RIT_PLASMA_GUN}
    };
    int i;
    if (!name) return 0;
    for (i = 0; i < (int)Q_COUNTOF(bits); ++i)
        if (!strcmp(name, bits[i].name)) return bits[i].bit;
    return atoi(name);
}

extern qmodel_t *VR_GetWeaponModel(qmodel_t *model);
extern qboolean VR_IsConfiguredWeaponModel(const qmodel_t *model);
extern float VR_WeaponWheelScale(const qmodel_t *model);
extern float VR_WeaponModelScale(const qmodel_t *model);
extern const char *VR_WeaponMeleeModeName(const qmodel_t *model);
extern qboolean VR_WeaponMeleeDamageScale(const qmodel_t *model, float *scale);

static qmodel_t *xr_weaponwheel_find_model(const char *name)
{
    int i;
    if (!name || !*name) return NULL;
    for (i = 1; i < MAX_MODELS && cl.model_precache[i]; ++i)
        if (!strcmp(cl.model_precache[i]->name, name)) return VR_GetWeaponModel(cl.model_precache[i]);
    return VR_GetWeaponModel(Mod_ForName(name, false));
}

static void xr_weaponwheel_add_builtin_slot(int item, int impulse, const char *name, const char *model)
{
    int slot = xr_weapon_count;
    if (slot >= (int)Q_COUNTOF(xr_weapons)) return;
    xr_weapons[slot].item = item;
    xr_weapons[slot].impulse = impulse;
    xr_weapons[slot].replaces_item = 0;
    xr_weapons[slot].local_field_value = 0;
    q_strlcpy(xr_weapon_names[slot], name, sizeof(xr_weapon_names[slot]));
    xr_weapons[slot].name = xr_weapon_names[slot];
    xr_weapons[slot].model = NULL;
    xr_weapon_slot_set_builtin_melee(&xr_weapons[slot]);
    q_strlcpy(xr_weapon_models[slot], model, sizeof(xr_weapon_models[slot]));
    xr_weapon_local_fields[slot][0] = 0;
    xr_weapon_local_field_values[slot] = 0;
    ++xr_weapon_count;
}

static void xr_weaponwheel_set_builtin_slots(void)
{
    static const int items[8] = {IT_AXE, IT_SHOTGUN, IT_SUPER_SHOTGUN, IT_NAILGUN,
        IT_SUPER_NAILGUN, IT_GRENADE_LAUNCHER, IT_ROCKET_LAUNCHER, IT_LIGHTNING};
    static const int impulses[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const char *names[8] = {"Axe", "Shotgun", "Super Shotgun", "Nailgun",
        "Super Nailgun", "Grenade Launcher", "Rocket Launcher", "Thunderbolt"};
    int i;
    for (i = 0; i < 8; ++i) {
        xr_weapons[i].item = items[i];
        xr_weapons[i].impulse = impulses[i];
        xr_weapons[i].name = names[i];
        xr_weapons[i].model = NULL;
        xr_weapons[i].replaces_item = 0;
        xr_weapons[i].local_field_value = 0;
        xr_weapon_slot_set_builtin_melee(&xr_weapons[i]);
        xr_weapon_local_fields[i][0] = 0;
        q_strlcpy(xr_weapon_models[i], xr_builtin_model_paths[i], sizeof(xr_weapon_models[i]));
    }
    xr_weapon_count = 8;

    if (hipnotic)
    {
        xr_weaponwheel_add_builtin_slot(HIT_MJOLNIR, 226, "Mjolnir", "progs/v_hammer.mdl");
        xr_weaponwheel_add_builtin_slot(HIT_PROXIMITY_GUN, 6, "Proximity Gun", "progs/v_prox.mdl");
        xr_weaponwheel_add_builtin_slot(HIT_LASER_CANNON, 225, "Laser Cannon", "progs/v_laserg.mdl");
    }
    else if (rogue)
    {
        xr_weapons[0].item = RIT_AXE;
        xr_weaponwheel_add_builtin_slot(RIT_LAVA_NAILGUN, 4, "Lava Nailgun", "progs/v_lava.mdl");
        xr_weaponwheel_add_builtin_slot(RIT_LAVA_SUPER_NAILGUN, 5, "Lava Super Nailgun", "progs/v_lava2.mdl");
        xr_weaponwheel_add_builtin_slot(RIT_MULTI_GRENADE, 6, "Multi-Grenade Launcher", "progs/v_multi.mdl");
        xr_weaponwheel_add_builtin_slot(RIT_MULTI_ROCKET, 7, "Multi-Rocket Launcher", "progs/v_multi2.mdl");
        xr_weaponwheel_add_builtin_slot(RIT_PLASMA_GUN, 8, "Plasma Gun", "progs/v_plasma.mdl");
    }
}

static void xr_weaponwheel_apply_extension_entry(const jsonentry_t *entry)
{
    const char *name, *item_name, *model, *replaces_name, *local_field;
    const double *item_number, *impulse, *local_value;
    int item, slot;

    if (!entry || entry->type != JSON_OBJECT) return;
    name = JSON_FindString(entry, "name");
    item_name = JSON_FindString(entry, "item");
    item_number = JSON_FindNumber(entry, "item");
    impulse = JSON_FindNumber(entry, "impulse");
    if (!name || !impulse) return;
    item = item_name ? xr_item_bit(item_name) : (item_number ? (int)*item_number : 0);
    if (!item) return;

    for (slot = 0; slot < xr_weapon_count; ++slot)
        if (!q_strcasecmp(xr_weapons[slot].name, name)) break;
    if (slot == xr_weapon_count) {
        if (xr_weapon_count >= (int)Q_COUNTOF(xr_weapons)) return;
        ++xr_weapon_count;
    }
    xr_weapons[slot].item = item;
    xr_weapons[slot].impulse = (int)*impulse;
    replaces_name = JSON_FindString(entry, "replaces");
    xr_weapons[slot].replaces_item = replaces_name ? xr_item_bit(replaces_name) : 0;
    local_value = JSON_FindNumber(entry, "local_value");
    xr_weapons[slot].local_field_value = local_value ? (int)*local_value : 0;
    xr_weapon_slot_set_builtin_melee(&xr_weapons[slot]);
    q_strlcpy(xr_weapon_names[slot], name, sizeof(xr_weapon_names[slot]));
    xr_weapons[slot].name = xr_weapon_names[slot];
    xr_weapon_models[slot][0] = 0;
    model = JSON_FindString(entry, "model");
    if (model && *model) q_strlcpy(xr_weapon_models[slot], model, sizeof(xr_weapon_models[slot]));
    xr_weapon_local_fields[slot][0] = 0;
    local_field = JSON_FindString(entry, "local_field");
    if (local_field && *local_field) q_strlcpy(xr_weapon_local_fields[slot], local_field, sizeof(xr_weapon_local_fields[slot]));
    xr_weapons[slot].model = NULL;
}

static void xr_weaponwheel_apply_extension_manifest(const char *data)
{
    json_t *json;
    const jsonentry_t *section, *entry;

    if (!data) return;
    json = JSON_Parse(data);
    if (!json) return;
    section = JSON_Find(json->root, COM_SkipPath(com_gamedir), JSON_ARRAY);
    if (!section) section = JSON_Find(json->root, "default", JSON_ARRAY);
    if (section)
        for (entry = section->firstchild; entry; entry = entry->next)
            xr_weaponwheel_apply_extension_entry(entry);
    JSON_Free(json);
}

static void xr_weaponwheel_apply_extension_path(const char *path)
{
    byte *data;
    if (!path) return;
    data = COM_LoadMallocFile_TextMode_OSPath(path, NULL);
    if (!data) return;
    xr_weaponwheel_apply_extension_manifest((const char *)data);
    free(data);
}

static void xr_weaponwheel_apply_extension_pack(searchpath_t *search)
{
    int i;
    if (!search) return;
    xr_weaponwheel_apply_extension_pack(search->next);
    if (!search->pack || search->pack->type != PACK_TYPE_PK3) return;
    for (i = 0; i < search->pack->numfiles; ++i) {
        FILE *file;
        byte *data;
        int len;
        if (strcmp(search->pack->files[i].name, "vr_wheel.json")) continue;
        len = search->pack->files[i].filelen;
        file = FS_Pk3OpenFile(search->pack, &search->pack->files[i]);
        if (!file || len < 0) { if (file) fclose(file); return; }
        data = malloc((size_t)len + 1);
        if (!data) { fclose(file); return; }
        if (fread(data, 1, len, file) != (size_t)len) { fclose(file); free(data); return; }
        fclose(file);
        data[len] = 0;
        xr_weaponwheel_apply_extension_manifest((const char *)data);
        free(data);
        return;
    }
}

static void xr_weaponwheel_apply_extension_loose(searchpath_t *search)
{
    char path[MAX_OSPATH];
    if (!search) return;
    xr_weaponwheel_apply_extension_loose(search->next);
    if (search->pack || (host_parms && host_parms->exedir && !q_strcasecmp(search->filename, host_parms->exedir)) || (host_parms && host_parms->basedir && !q_strcasecmp(search->filename, host_parms->basedir))) return;
    q_snprintf(path, sizeof(path), "%s/vr_wheel.json", search->filename);
    xr_weaponwheel_apply_extension_path(path);
}

static void xr_weaponwheel_load_extensions(void)
{
    char path[MAX_OSPATH];
    /* Package extensions load before loose extensions so loose files have the final say. */
    xr_weaponwheel_apply_extension_pack(com_searchpaths);
    if (host_parms && host_parms->exedir) {
        q_snprintf(path, sizeof(path), "%s/vr_wheel.json", host_parms->exedir);
        xr_weaponwheel_apply_extension_path(path);
    }
    if (host_parms && host_parms->basedir && (!host_parms->exedir || q_strcasecmp(host_parms->basedir, host_parms->exedir))) {
        q_snprintf(path, sizeof(path), "%s/vr_wheel.json", host_parms->basedir);
        xr_weaponwheel_apply_extension_path(path);
    }
    xr_weaponwheel_apply_extension_loose(com_searchpaths);
}

static void xr_weaponwheel_resolve_models(void)
{
    int i;
    for (i = 0; i < xr_weapon_count; ++i)
        xr_weapons[i].model = xr_weaponwheel_find_model(xr_weapon_models[i]);
    xr_weapon_apply_melee_metadata();
}

static void xr_weapon_apply_melee_metadata(void)
{
    int i;
    for (i = 0; i < xr_weapon_count; ++i)
    {
        const char *melee = VR_WeaponMeleeModeName(xr_weapons[i].model);
        float scale;
        qboolean valid_melee;

        xr_weapon_slot_set_builtin_melee(&xr_weapons[i]);
        if (melee)
        {
            xr_weapons[i].melee_mode = xr_parse_melee_mode(melee, &valid_melee);
            if (valid_melee) xr_weapons[i].melee_explicit = true;
        }
        if (VR_WeaponMeleeDamageScale(xr_weapons[i].model, &scale))
        {
            xr_weapons[i].melee_damage_scale = scale;
            xr_weapons[i].melee_damage_explicit = true;
        }
    }
}

static void xr_weaponwheel_reload_f(void)
{
    byte *file;
    json_t *json;
    const jsonentry_t *section, *entry;
    int count = 0;

    xr_weaponwheel_set_builtin_slots();
    file = COM_LoadMallocFile("weaponwheel.json", NULL);
    if (file) {
        json = JSON_Parse((const char *)file);
        if (json) {
            section = JSON_Find(json->root, COM_SkipPath(com_gamedir), JSON_ARRAY);
            if (!section) section = JSON_Find(json->root, "default", JSON_ARRAY);
            if (section) {
                for (entry = section->firstchild; entry && count < (int)Q_COUNTOF(xr_weapons); entry = entry->next) {
                    const char *name = JSON_FindString(entry, "name");
                    const char *item_name = JSON_FindString(entry, "item");
                    const double *item_number = JSON_FindNumber(entry, "item");
                    const double *impulse = JSON_FindNumber(entry, "impulse");
                    const char *model = JSON_FindString(entry, "model");
                    const char *replaces_name = JSON_FindString(entry, "replaces");
                    const char *local_field = JSON_FindString(entry, "local_field");
                    const double *local_value = JSON_FindNumber(entry, "local_value");
                    int item = item_name ? xr_item_bit(item_name) : (item_number ? (int)*item_number : 0);
                    if (entry->type != JSON_OBJECT || !name || !impulse || !item) continue;
                    xr_weapons[count].item = item;
                    xr_weapons[count].impulse = (int)*impulse;
                    xr_weapons[count].replaces_item = replaces_name ? xr_item_bit(replaces_name) : 0;
                    xr_weapons[count].local_field_value = local_value ? (int)*local_value : 0;
                    q_strlcpy(xr_weapon_names[count], name, sizeof(xr_weapon_names[count]));
                    xr_weapons[count].name = xr_weapon_names[count];
                    xr_weapons[count].model = NULL;
                    xr_weapon_slot_set_builtin_melee(&xr_weapons[count]);
                    xr_weapon_models[count][0] = 0;
                    xr_weapon_local_fields[count][0] = 0;
                    if (model && *model)
                        q_strlcpy(xr_weapon_models[count], model, sizeof(xr_weapon_models[count]));
                    if (local_field && *local_field)
                        q_strlcpy(xr_weapon_local_fields[count], local_field, sizeof(xr_weapon_local_fields[count]));
                    ++count;
                }
            }
            JSON_Free(json);
        }
        free(file);
        if (count > 0) xr_weapon_count = count;
    }
    xr_weaponwheel_load_extensions();
}
static void xr_vignette_init(void)
{
    xr_vignette_pic = Draw_LoadPicRGBA("gfx/vignette");
    if (xr_vignette_pic) return;
    enum { size = 256 };
    byte *data = (byte *) malloc(size * size * 4);
    int x, y;
    if (!data) return;
    for (y = 0; y < size; ++y) {
        for (x = 0; x < size; ++x) {
            float nx = fabsf((x + 0.5f) / (size * 0.5f) - 1.f);
            float ny = fabsf((y + 0.5f) / (size * 0.5f) - 1.f);
            float d = powf(nx, 4.f) + powf(ny, 4.f);
            float a = CLAMP(0.f, (d - 0.52f) / 0.34f, 1.f);
            a = a * a * (3.f - 2.f * a);
            data[(y * size + x) * 4 + 0] = 0;
            data[(y * size + x) * 4 + 1] = 0;
            data[(y * size + x) * 4 + 2] = 0;
            data[(y * size + x) * 4 + 3] = (byte)(a * 255.f + 0.5f);
        }
    }
    xr_vignette_pic = Draw_MakePicRGBA("xr_vignette", size, size, data);
    free(data);
}
void XR_Interaction_Init(void)
{
    xr_melee_reset_all();
    Cvar_RegisterVariable(&vr_weaponwheel);
    Cvar_RegisterVariable(&vr_weaponwheel_slowmo);
    Cvar_RegisterVariable(&vr_weaponwheel_distance);
    Cvar_RegisterVariable(&vr_weaponwheel_radius);
    Cvar_RegisterVariable(&vr_weaponwheel_modelsize);
    Cvar_RegisterVariable(&vr_weaponwheel_modelpitch);
    Cvar_RegisterVariable(&vr_weaponwheel_modelyaw);
    Cvar_RegisterVariable(&vr_weaponwheel_spin);
    Cvar_RegisterVariable(&vr_weaponwheel_deflection);
    Cvar_RegisterVariable(&vr_melee);
    Cvar_RegisterVariable(&vr_melee_velocity);
    Cvar_RegisterVariable(&vr_melee_cooldown);
    Cvar_RegisterVariable(&vr_melee_damage_scale);
    Cvar_RegisterVariable(&vr_melee_pitch);
    Cvar_RegisterVariable(&vr_melee_debug);
    Cvar_RegisterVariable(&vr_weapon_melee);
    Cvar_RegisterVariable(&vr_weapon_melee_damage_scale);
    Cvar_RegisterVariable(&vr_comfort_vignette);
    Cvar_RegisterVariable(&vr_comfort_vignette_strength);
    Cvar_RegisterVariable(&vr_mouse);
    Cvar_RegisterVariable(&vr_mouse_color);
    Cvar_RegisterVariable(&vr_mouse_alpha);
    Cvar_RegisterVariable(&vr_aim_beam);
    Cvar_RegisterVariable(&vr_aim_beam_width);
    Cmd_AddCommand("weaponwheel_reload", xr_weaponwheel_reload_f);
    Cmd_AddCommand("+vr_weaponwheel", xr_weaponwheel_bind_down);
    Cmd_AddCommand("-vr_weaponwheel", xr_weaponwheel_bind_up);
    Cmd_AddCommand("+vr_offhandweaponwheel", xr_offhand_weaponwheel_bind_down);
    Cmd_AddCommand("-vr_offhandweaponwheel", xr_offhand_weaponwheel_bind_up);
    Cmd_AddCommand("+vr_offhandattack", xr_offhand_attack_bind_down);
    Cmd_AddCommand("-vr_offhandattack", xr_offhand_attack_bind_up);
    if (keybindings[K_LGRIP] && !strcmp(keybindings[K_LGRIP], "+speed")) Key_SetBinding(K_LGRIP, "+vr_offhandweaponwheel");
    if (keybindings[K_LTRIGGER] && !strcmp(keybindings[K_LTRIGGER], "+jump")) Key_SetBinding(K_LTRIGGER, "+vr_offhandattack");

    xr_weaponwheel_reload_f();
}

void XR_Interaction_Shutdown(void)
{
    xr_wheel_close(); xr_keyboard_close(); xr_virtual_pointer_clear(); wheel_bind_active = offhand_wheel_bind_active = false; offhand_weapon_item = 0; offhand_transfer.active = false; memset(&offhand_fire, 0, sizeof(offhand_fire)); offhand_local_fire.executing = false; offhand_local_fire.frame = offhand_local_fire.attack_finished = 0.f; offhand_fire_main_viewmodel_valid = false; offhand_fire_input_suppressed = false; main_fire_input_active = false; local_offhand_fired_this_frame = false; network_visual_fire_pending = false; network_visual_fire_deadline = 0.0; visual_fire_deadline = 0.0; memset(visual_fire_active, 0, sizeof(visual_fire_active)); memset(visual_fire_second, 0, sizeof(visual_fire_second)); memset(visual_fire_weapon, 0, sizeof(visual_fire_weapon)); memset(xr_local_projectile_spawns, 0, sizeof(xr_local_projectile_spawns)); memset(fire_haptic_next_time, 0, sizeof(fire_haptic_next_time)); keyboard_trigger_suppressed = false; virtual_mouse_trigger_suppressed = false; two_hand_wheel_suppressed = false; two_hand_mode_active = false; vignette_value = 0.f; vignette_yaw_valid = false; xr_melee_reset_all(); memset(&xr_melee_damage_context, 0, sizeof(xr_melee_damage_context));
}

void XR_Interaction_Update(const iw_xr_action_snapshot_t *actions)
{
    int dominant, offhand, mouse_hand; float move, yaw_delta; qboolean grip, main_grip, offhand_grip, wheel_hold, trigger, mouse_trigger, mouse_confirm, mouse_grip, menu_combo, both_grips;
    if (!actions || !actions->active) { XR_Interaction_Shutdown(); return; }
    if (cl.maxclients > 1 && (cls.state != ca_connected || cls.signon != SIGNONS || cl.intermission || cl.stats[STAT_HEALTH] <= 0))
    {
        offhand_attack_active = false;
        xr_offhand_fire_clear();
        network_visual_fire_pending = false;
    }
    xr_weaponwheel_update_local_fields();
    if (offhand_weapon_item && !xr_wheel_item_available(offhand_weapon_item)) {
        // The network attack temporarily uses the main weapon slot; do not clear a valid offhand weapon when both hands share an item.
        offhand_weapon_item = 0;
        xr_offhand_reset_local_fire();
    }
    if (offhand_local_fire.frame && cl.time >= offhand_local_fire.next_attack_time &&
        !xr_offhand_continuous_weapon ())
        offhand_local_fire.frame = 0.f;
    if (key_dest != key_game) wheel_bind_active = offhand_wheel_bind_active = false;
    if (wheel_active && !xr_can_wheel()) xr_wheel_close();
    dominant = XR_Input_PhysicalHandForRole(XR_HAND_MAINHAND); offhand = XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND); mouse_hand = XR_Input_MouseHand();
    main_grip = actions->hand[dominant].grip > 0.5f || (actions->hand[dominant].buttons & IW_XR_BUTTON_GRIP) != 0;
    offhand_grip = actions->hand[offhand].grip > 0.5f || (actions->hand[offhand].buttons & IW_XR_BUTTON_GRIP) != 0;
    grip = wheel_bind_active || offhand_wheel_bind_active;
    both_grips = main_grip && offhand_grip;
    two_hand_mode_active = vr_stabilize_mode.value != 0.f &&
        (actions->hand[offhand].grip_valid || actions->hand[offhand].aim_valid) && both_grips;
    if (both_grips) two_hand_wheel_suppressed = true;
    else if (!main_grip && !offhand_grip) two_hand_wheel_suppressed = false;
    trigger = (actions->hand[dominant].buttons & IW_XR_BUTTON_TRIGGER) != 0;
    mouse_trigger = (actions->hand[mouse_hand].buttons & IW_XR_BUTTON_TRIGGER) != 0;
    main_fire_input_active = trigger;
    local_offhand_fired_this_frame = false;
    offhand_fire_input_suppressed = cl.maxclients > 1 && trigger;
    xr_offhand_transfer_update();
    xr_offhand_fire_update();
    if (!offhand_attack_active && xr_offhand_continuous_weapon ())
    {
        offhand_local_fire.animation_active = false;
        offhand_local_fire.frame = 0.f;
        XR_Interaction_ResetOffhandContinuousAudio ();
    }
    {
        qboolean main_fire = trigger;
        qboolean offhand_fire_active = offhand_attack_active && !trigger && offhand_weapon_item && (cl.maxclients == 1 || offhand_fire.phase == XR_OFFHAND_FIRING);
        xr_update_visual_fire_state((iw_xr_hand_t)dominant, main_fire, XR_Interaction_MainhandWeaponItem());
        xr_update_visual_fire_state((iw_xr_hand_t)offhand, offhand_fire_active, offhand_weapon_item);
    }
    if (cl.maxclients > 1 && offhand_fire.phase == XR_OFFHAND_FIRING && offhand_attack_active && !trigger)
        xr_mark_visual_fire (XR_Input_PhysicalHandForRole (XR_HAND_OFFHAND));
    else if (trigger && !offhand_attack_active)
        xr_mark_visual_fire ((iw_xr_hand_t)dominant);

    mouse_confirm = mouse_trigger || (actions->hand[dominant].buttons & IW_XR_BUTTON_PRIMARY) != 0;
    mouse_grip = actions->hand[mouse_hand].grip > 0.5f || (actions->hand[mouse_hand].buttons & IW_XR_BUTTON_GRIP) != 0;
    if (!trigger) keyboard_trigger_suppressed = false;
    if (!mouse_confirm) virtual_mouse_trigger_suppressed = false;
    menu_combo = main_grip && (actions->hand[dominant].buttons & IW_XR_BUTTON_SECONDARY) != 0;
    if (keyboard_active && key_dest == key_game) xr_keyboard_close();
    if (virtual_mouse_trigger && (key_dest != key_menu || (!vr_mouse.value && !mouse_grip)) ) {
        Key_Event(K_MOUSE1, false);
        virtual_mouse_trigger = false;
    }
    if (keyboard_active) {
        qboolean select = (actions->hand[dominant].buttons & IW_XR_BUTTON_PRIMARY) != 0;
        xr_virtual_pointer_update(&actions->hand[mouse_hand]);
        if (pointer_active) xr_keyboard_key_at(keyboard_x, keyboard_y, &keyboard_row, &keyboard_col);
        xr_keyboard_navigate(&actions->hand[offhand]);
        if (mouse_trigger && !keyboard_trigger && pointer_active) {
            xr_keyboard_press();
            if (!keyboard_active) keyboard_trigger_suppressed = true;
        } else if (select && !keyboard_select) xr_keyboard_press();
        keyboard_trigger = mouse_trigger;
        keyboard_select = select;
        if (actions->hand[dominant].buttons & IW_XR_BUTTON_SECONDARY) xr_keyboard_close();
    } else if (key_dest == key_menu) {
        int menu_hand = (vr_mouse.value || mouse_grip) ? mouse_hand : XR_Input_MenuHand();
        int scroll = actions->hand[menu_hand].stick[1] > 0.6f ? 1 : actions->hand[menu_hand].stick[1] < -0.6f ? -1 : 0;
        if (scroll && scroll != menu_scroll_direction)
            M_Keydown(scroll > 0 ? K_MWHEELUP : K_MWHEELDOWN, false);
        menu_scroll_direction = scroll;
        if (!ui_mouse.value) Cvar_SetValueQuick(&ui_mouse, 1.f);
        xr_virtual_pointer_update(&actions->hand[mouse_hand]);
        if (pointer_active || virtual_mouse_trigger)
            M_MousemoveNormalized(CLAMP(0.f, virtual_mouse_x, 1.f), CLAMP(0.f, virtual_mouse_y, 1.f));
        if (mouse_confirm && !virtual_mouse_trigger && pointer_active) {
            virtual_mouse_trigger_suppressed = true;
            Key_Event(K_MOUSE1, true);
			// Menu text rows consume the click, then the XR keyboard handles typing.
			if (Key_TextEntry() == TEXTMODE_ON) {
				Key_Event(K_MOUSE1, false);
				xr_keyboard_open(mouse_trigger, (actions->hand[dominant].buttons & IW_XR_BUTTON_PRIMARY) != 0);
				virtual_mouse_trigger = false;
			} else {
				virtual_mouse_trigger = mouse_confirm && (pointer_active || virtual_mouse_trigger);
			}
        }
        if (!keyboard_active) {
            if (!mouse_confirm && virtual_mouse_trigger) Key_Event(K_MOUSE1, false);
            virtual_mouse_trigger = mouse_confirm && (pointer_active || virtual_mouse_trigger);
        }
    } else if ((key_dest == key_console || key_dest == key_message) && Key_TextEntry() == TEXTMODE_ON) {
        xr_keyboard_open(false, false);
    } else if (wheel_active && !(wheel_hand == dominant ? wheel_bind_active : offhand_wheel_bind_active)) {
        menu_scroll_direction = 0;
        xr_wheel_cursor_from_pose();
        xr_wheel_select(wheel_cursor[0], wheel_cursor[1]);
        xr_wheel_commit();
    } else if (wheel_active && both_grips) {
        menu_scroll_direction = 0;
        xr_wheel_close();
    } else if (wheel_active && menu_combo) {
        menu_scroll_direction = 0;
        xr_wheel_close();
    } else if (wheel_active) {
        menu_scroll_direction = 0;
        wheel_hold = wheel_hand == dominant ? wheel_bind_active : offhand_wheel_bind_active;
        xr_wheel_cursor_from_pose();
        xr_wheel_select(wheel_cursor[0], wheel_cursor[1]);
        if (!wheel_hold) xr_wheel_commit();
    } else if (grip && !menu_combo && !both_grips && !two_hand_wheel_suppressed) {
        menu_scroll_direction = 0;
        xr_wheel_open(wheel_bind_active ? dominant : offhand);
    } else {
        menu_scroll_direction = 0;
        xr_virtual_pointer_clear();
    }
    xr_melee_update(actions, dominant, offhand);
    move = sqrtf(actions->hand[offhand].stick[0] * actions->hand[offhand].stick[0] +
                 actions->hand[offhand].stick[1] * actions->hand[offhand].stick[1]);
    yaw_delta = 0.f;
    if (!vignette_yaw_valid)
        vignette_yaw_valid = true;
    else
    {
        yaw_delta = cl.viewangles[YAW] - vignette_last_yaw;
        while (yaw_delta > 180.f) yaw_delta -= 360.f;
        while (yaw_delta < -180.f) yaw_delta += 360.f;
    }
    vignette_last_yaw = cl.viewangles[YAW];
    {
        int vignette_mode = (int)CLAMP(0.f, vr_comfort_vignette.value, 2.f);
        qboolean vignette_active = vignette_mode >= 1 && (move > 0.2f || XR_Input_IsTeleportAiming() || XR_Input_IsDashing() ||
            (vignette_mode >= 2 && fabsf(yaw_delta) > 1.f));
        if (vignette_active && key_dest == key_game && !keyboard_active && !wheel_active)
                    vignette_value += (CLAMP(0.f, vr_comfort_vignette_strength.value, 1.f) - vignette_value) * (float)q_min(1.0, host_frametime * 8.0);
        else vignette_value -= vignette_value * (float)q_min(1.0, host_frametime * 8.0);
    }
    vignette_value = CLAMP(0.f, vignette_value, 1.f);
}

qboolean XR_Interaction_ConsumesGameplay(void) { return wheel_active || keyboard_active || keyboard_trigger_suppressed || virtual_mouse_trigger || virtual_mouse_trigger_suppressed; }
qboolean XR_Interaction_WheelActive(void) { return wheel_active; }
qboolean XR_Interaction_OffhandAttackActive(void) { return offhand_attack_active && offhand_weapon_item && !wheel_active && !two_hand_mode_active; }
qboolean XR_Interaction_MainhandFireInputActive(void) { return main_fire_input_active; }
qboolean XR_Interaction_GetMeleePulse(xr_hand_role_t role, xr_melee_attack_t *attack)
{
    iw_xr_hand_t hand = XR_Input_PhysicalHandForRole(role);
    if (attack) *attack = xr_melee_pulses[hand];
    return xr_melee_pulses[hand].valid;
}
qboolean XR_Interaction_ConsumeMeleePulse(xr_hand_role_t role, xr_melee_attack_t *attack)
{
    iw_xr_hand_t hand = XR_Input_PhysicalHandForRole(role);
    qboolean valid = xr_melee_pulses[hand].valid;
    if (attack) *attack = xr_melee_pulses[hand];
    memset(&xr_melee_pulses[hand], 0, sizeof(xr_melee_pulses[hand]));
    return valid;
}
void XR_Interaction_NotifyMeleeResult(iw_xr_hand_t hand, qboolean contacted)
{
    if (hand < 0 || hand >= IW_XR_HAND_COUNT || !contacted)
        return;
    xr_melee_hands[hand].last_swing_time = realtime;
}
qboolean XR_Interaction_GetMeleeDamageContext(xr_melee_attack_t *attack)
{
    if (attack) *attack = xr_melee_damage_context;
    return xr_melee_damage_context.valid;
}
void XR_Interaction_SetMeleeDamageContext(const xr_melee_attack_t *attack)
{
    if (attack) xr_melee_damage_context = *attack;
    else memset(&xr_melee_damage_context, 0, sizeof(xr_melee_damage_context));
}
void XR_Interaction_ClearMeleeDamageContext(void) { memset(&xr_melee_damage_context, 0, sizeof(xr_melee_damage_context)); }
void XR_Interaction_ResetMeleeState(void) { xr_melee_reset_all(); memset(&xr_melee_damage_context, 0, sizeof(xr_melee_damage_context)); }
qboolean XR_Interaction_ConsumeLocalOffhandFireEvent(void)
{
    qboolean fired = local_offhand_fired_this_frame;
    local_offhand_fired_this_frame = false;
    return fired;
}
qboolean XR_Interaction_LocalOffhandAttackReady(double time, xr_melee_attack_t *motion)
{
    iw_xr_hand_t hand = XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND);
    if (motion) memset(motion, 0, sizeof(*motion));
    if (xr_melee_pulses[hand].valid && time >= offhand_local_fire.next_attack_time)
    {
        if (motion) *motion = xr_melee_pulses[hand];
        memset(&xr_melee_pulses[hand], 0, sizeof(xr_melee_pulses[hand]));
        return true;
    }
    return XR_Interaction_OffhandAttackActive() && offhand_weapon_item && time >= offhand_local_fire.next_attack_time;
}
void XR_Interaction_SetLocalOffhandCooldown(double time, float attack_finished)
{
    float delay;
    if (offhand_weapon_item == HIT_LASER_CANNON) {
        delay = 0.1f;
        goto cooldown_ready;
    }
    if (hipnotic) {
        switch (offhand_weapon_item) {
        case HIT_MJOLNIR: delay = 0.5f; break;
        case HIT_PROXIMITY_GUN: delay = 0.6f; break;
        default: delay = -1.f; break;
        }
        if (delay >= 0.f) goto cooldown_ready;
    }
    if (rogue) {
        switch (offhand_weapon_item) {
        case RIT_AXE: delay = 0.5f; break;
        case RIT_LAVA_NAILGUN: case RIT_LAVA_SUPER_NAILGUN: delay = 0.1f; break;
        case RIT_MULTI_GRENADE: delay = 0.6f; break;
        case RIT_MULTI_ROCKET: delay = 0.8f; break;
        case RIT_PLASMA_GUN: delay = 0.2f; break;
        default: delay = -1.f; break;
        }
        if (delay >= 0.f) goto cooldown_ready;
    }
    switch (offhand_weapon_item) {
    case IT_AXE: delay = 0.5f; break;
    case IT_SHOTGUN: delay = 0.5f; break;
    case IT_SUPER_SHOTGUN: delay = 0.7f; break;
    case IT_NAILGUN: case IT_SUPER_NAILGUN: case IT_LIGHTNING:
        delay = 0.1f; break;
    case IT_GRENADE_LAUNCHER: delay = 0.6f; break;
    case IT_ROCKET_LAUNCHER: delay = 0.8f; break;
    default: delay = 0.2f; break;
    }
cooldown_ready:
    if (xr_offhand_continuous_weapon ())
        offhand_local_fire.next_attack_time = time + delay;
    else
        offhand_local_fire.next_attack_time = attack_finished > time ? attack_finished : time + delay;
}
void XR_Interaction_BeginLocalOffhandAttack(void) {
    offhand_local_fire.executing = true;
    xr_update_visual_fire_state(XR_Input_PhysicalHandForRole (XR_HAND_OFFHAND), true, offhand_weapon_item);
    xr_mark_visual_fire (XR_Input_PhysicalHandForRole (XR_HAND_OFFHAND));
}
void XR_Interaction_EndLocalOffhandAttack(void) { offhand_local_fire.executing = false; }
void XR_Interaction_BeginLocalOffhandAnimation(double time, float frame)
{
    if (xr_offhand_continuous_weapon () && offhand_local_fire.animation_active)
        return;
    offhand_local_fire.animation_active = true;
    offhand_local_fire.animation_start_time = time;
    offhand_local_fire.animation_start_frame = frame;
    offhand_local_fire.animation_end_time = time;
}

void XR_Interaction_LocalOffhandAttackResult(qboolean fired, qboolean ammo_empty)
{
    if (fired)
        local_offhand_fired_this_frame = true;
    if (fired && !ammo_empty)
        return;
    offhand_attack_active = false;
    offhand_weapon_item = 0;
    xr_offhand_reset_local_fire ();
    xr_update_visual_fire_state (XR_Input_PhysicalHandForRole (XR_HAND_OFFHAND), false, 0);
}

void XR_Interaction_SetLocalOffhandWeaponState(float frame, float attack_finished) {
    XR_Interaction_NotifyWeaponFire(XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND), offhand_local_fire.attack_finished, attack_finished, cl.time);
    offhand_local_fire.frame = frame;
    offhand_local_fire.attack_finished = attack_finished;
    if (frame > 0.f)
        offhand_local_fire.animation_start_frame = frame;
    if (offhand_local_fire.animation_active)
    {
        offhand_local_fire.animation_end_time = q_max (attack_finished, offhand_local_fire.next_attack_time);
    }
}
float XR_Interaction_LocalOffhandWeaponFrame(void) { return offhand_local_fire.frame; }
static float xr_local_offhand_presentation_frame(int numframes)
{
    float interval = 0.1f;
    double animation_finished_time;
    int first, frame;
    if (xr_offhand_continuous_weapon ())
        return offhand_local_fire.animation_active ? offhand_local_fire.frame : 0.f;
    if (!offhand_local_fire.animation_active || numframes <= 1)
        return offhand_local_fire.frame;
    first = CLAMP(0, (int)offhand_local_fire.animation_start_frame, numframes - 1);
    if (!first) first = 1;
    /* Quake viewmodels advance their weapon frames at 10 Hz, independently of the next-shot delay. */
    frame = first + (int)((cl.time - offhand_local_fire.animation_start_time) / interval);
    if (frame >= numframes)
    {
        animation_finished_time = offhand_local_fire.animation_start_time + (numframes - first) * interval;
        if (cl.time < offhand_local_fire.animation_end_time || cl.time < animation_finished_time)
            return (float)(numframes - 1);
        offhand_local_fire.animation_active = false;
        return 0.f;
    }
    return frame;
}float XR_Interaction_LocalOffhandAttackFinished(void) { return offhand_local_fire.attack_finished; }
void XR_Interaction_SetLocalOffhandBeamEntity(int entity, double time)
{
    offhand_beam.entity = entity;
    offhand_beam.deadline = time + 0.25;
}
qboolean XR_Interaction_IsLocalOffhandBeamEntity(int entity)
{
    return entity == offhand_beam.entity && cl.time <= offhand_beam.deadline;
}
qboolean XR_Interaction_AllowOffhandContinuousAutoSound(void)
{
    if (offhand_continuous_auto_sound_played) return false;
    offhand_continuous_auto_sound_played = true;
    return true;
}
void XR_Interaction_ResetOffhandContinuousAudio(void) { offhand_continuous_auto_sound_played = false; }
qboolean XR_Interaction_GetNetworkGrenadePitch(float *pitch)
{
    vec3_t forward;
    vec3_t right, up, angles;
    int weapon;

    if (!pitch || cl.maxclients <= 1)
        return false;
    weapon = xr_server_active_weapon_item();
    if (weapon != IT_GRENADE_LAUNCHER ||
        !R_GetXRMainHandWeaponPose (NULL, forward, right, up))
        return false;
    VectorAngles (forward, angles);
    *pitch = angles[PITCH];
    return true;
}

qboolean XR_Interaction_OffhandNetworkAttackActive(void)
{
    // The server has one attack bit; offhand may own it only after active-weapon acknowledgement.
    return cl.maxclients > 1 && !two_hand_mode_active && offhand_fire.phase == XR_OFFHAND_FIRING &&
        xr_server_active_weapon_item() == offhand_fire.item && offhand_weapon_item &&
        !wheel_active && (offhand_attack_active || xr_offhand_motion_pending());
}

xr_network_attack_owner_t XR_Interaction_PrepareNetworkAttack(qboolean main_requested, int user_impulse, int *network_impulse)
{
    if (network_impulse)
        *network_impulse = user_impulse;
    if (cl.maxclients <= 1)
        return main_requested ? XR_NETWORK_ATTACK_MAINHAND : XR_NETWORK_ATTACK_NONE;

    if (user_impulse)
    {
        xr_offhand_fire_cancel_external();
        return XR_NETWORK_ATTACK_NONE;
    }
    if (main_requested)
    {
        xr_offhand_fire_cancel_for_main();
        if (offhand_fire.phase != XR_OFFHAND_IDLE)
        {
            // Main fire is suppressed, but the restore impulse must still reach the server.
            if (network_impulse && offhand_fire.pending_impulse)
                *network_impulse = offhand_fire.pending_impulse;
            return XR_NETWORK_ATTACK_NONE;
        }
        return XR_NETWORK_ATTACK_MAINHAND;
    }
    if (offhand_fire.phase == XR_OFFHAND_WAIT_SELECT || offhand_fire.phase == XR_OFFHAND_WAIT_RESTORE)
    {
        if (network_impulse && offhand_fire.pending_impulse)
            *network_impulse = offhand_fire.pending_impulse;
        return XR_NETWORK_ATTACK_NONE;
    }
    return XR_Interaction_OffhandNetworkAttackActive() ? XR_NETWORK_ATTACK_OFFHAND : XR_NETWORK_ATTACK_NONE;
}

void XR_Interaction_CommitNetworkAttack(xr_network_attack_owner_t owner, int network_impulse)
{
    iw_xr_hand_t offhand = XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND);
    if (offhand_fire.pending_impulse && network_impulse == offhand_fire.pending_impulse)
    {
        if (offhand_fire.phase == XR_OFFHAND_WAIT_SELECT)
            offhand_fire.selection_sent = true;
        else if (offhand_fire.phase == XR_OFFHAND_WAIT_RESTORE)
            offhand_fire.restore_sent = true;
        offhand_fire.pending_impulse = 0;
    }
    if (cl.maxclients <= 1)
        return;
    if (owner == XR_NETWORK_ATTACK_OFFHAND)
    {
        if (!offhand_attack_active && xr_melee_pulses[offhand].valid)
            memset(&xr_melee_pulses[offhand], 0, sizeof(xr_melee_pulses[offhand]));
        network_visual_fire_hand = offhand;
        network_visual_fire_weapon = offhand_weapon_item;
        network_visual_fire_deadline = realtime + 0.25;
        network_visual_fire_pending = true;
        xr_mark_visual_fire(offhand);
        xr_notify_network_fire(offhand, offhand_weapon_item);
    }
    else if (owner == XR_NETWORK_ATTACK_MAINHAND)
    {
        iw_xr_hand_t mainhand = XR_Input_PhysicalHandForRole(XR_HAND_MAINHAND);
        if (xr_melee_pulses[mainhand].valid)
            memset(&xr_melee_pulses[mainhand], 0, sizeof(xr_melee_pulses[mainhand]));
        network_visual_fire_hand = mainhand;
        network_visual_fire_weapon = XR_Interaction_MainhandWeaponItem();
        network_visual_fire_deadline = realtime + 0.25;
        network_visual_fire_pending = true;
        xr_notify_network_fire(mainhand, network_visual_fire_weapon);
    }
}

qboolean XR_Interaction_PrepareNetworkViewAngles(xr_network_attack_owner_t owner, vec3_t angles)
{
    vec3_t forward;
    vec3_t right, up;
    if (owner == XR_NETWORK_ATTACK_MAINHAND || owner == XR_NETWORK_ATTACK_OFFHAND)
    {
        // Convert the owning hand to legacy Quake angles; no XR pose enters the packet.
        if (!R_GetXRMainHandWeaponPose(NULL, forward, right, up))
            return owner == XR_NETWORK_ATTACK_MAINHAND;
        VectorAngles(forward, angles);
        angles[ROLL] = 0.f;
        if (owner == XR_NETWORK_ATTACK_MAINHAND)
        {
            float grenade_pitch;
            if (XR_Interaction_GetNetworkGrenadePitch(&grenade_pitch))
                angles[PITCH] = grenade_pitch;
        }
    }
    return true;
}

qboolean XR_Interaction_UseOffhandAim(void) { return offhand_local_fire.executing || (offhand_fire.phase == XR_OFFHAND_FIRING && xr_server_active_weapon_item() == offhand_fire.item); }
int XR_Interaction_MainhandWeaponItem(void) { return offhand_fire_main_viewmodel_valid ? offhand_fire.main_item : xr_server_active_weapon_item(); }
int XR_Interaction_MainhandCurrentAmmo(void)
{
    int weapon = XR_Interaction_MainhandWeaponItem();
    if (!offhand_fire_main_viewmodel_valid)
        return cl.stats[STAT_AMMO];
    if (weapon == IT_AXE || weapon == RIT_AXE)
        return 0;
    if (weapon == IT_SHOTGUN || weapon == IT_SUPER_SHOTGUN)
        return cl.stats[STAT_SHELLS];
    if (weapon == IT_NAILGUN || weapon == IT_SUPER_NAILGUN)
        return cl.stats[STAT_NAILS];
    if (weapon == IT_GRENADE_LAUNCHER || weapon == IT_ROCKET_LAUNCHER)
        return cl.stats[STAT_ROCKETS];
    if (weapon == IT_LIGHTNING || weapon == HIT_LASER_CANNON || weapon == HIT_MJOLNIR)
        return cl.stats[STAT_CELLS];
    if (rogue)
    {
        if (weapon == RIT_LAVA_NAILGUN || weapon == RIT_LAVA_SUPER_NAILGUN)
            return cl.stats[STAT_NAILS];
        if (weapon == RIT_MULTI_GRENADE || weapon == RIT_MULTI_ROCKET)
            return cl.stats[STAT_ROCKETS];
        if (weapon == RIT_PLASMA_GUN)
            return cl.stats[STAT_CELLS];
    }
    // Unknown mod weapons retain the server-reported value until metadata defines their ammo pool.
    return cl.stats[STAT_AMMO];
}
int XR_Interaction_OffhandWeaponItem(void) { return offhand_weapon_item; }
xr_melee_mode_t XR_Interaction_GetWeaponMeleeMode(int item)
{
    xr_weapon_slot_t *slot;
    xr_melee_mode_t builtin;
    if (!item) return XR_MELEE_FIST;
    slot = xr_weapon_slot_for_item(item);
    if (slot && slot->melee_explicit) return slot->melee_mode;
    builtin = xr_builtin_melee_mode(item);
    if (builtin != XR_MELEE_NONE) return builtin;
    return vr_weapon_melee.value != 0.f ? XR_MELEE_GUNBUTT : XR_MELEE_NONE;
}
/* Render entities stay hand-owned even while cl.viewent temporarily follows the firing weapon. */
static qboolean xr_get_weapon_viewmodel(entity_t *out, int item, qboolean animate)
{
    int i;
    if (!out || !item) return false;
    xr_weaponwheel_resolve_models();
    for (i = 0; i < xr_weapon_count; ++i) {
        if (xr_weapons[i].item != item || !xr_weapons[i].model) continue;
        *out = cl.viewent;
        out->model = xr_weapons[i].model;
        if (!q_strncasecmp(out->model->name, "progs/vr/", 9) || VR_IsConfiguredWeaponModel(out->model)) out->scale = ENTSCALE_ENCODE(VR_WeaponModelScale(out->model));
        if (!animate) {
            out->frame = 0;
            out->lerpflags = 0;
        }
        return true;
    }
    return false;
}

qboolean XR_Interaction_GetMainhandViewmodel(entity_t *out)
{
    entity_t resolved;
    if (!out || wheel_active || !offhand_fire_main_viewmodel_valid) return false;
    *out = offhand_fire_main_viewmodel;
    if (offhand_fire.main_item && xr_get_weapon_viewmodel(&resolved, offhand_fire.main_item, true))
    {
        // The server briefly reports the offhand weapon as active; rebuild the main model from its saved item.
        out->model = resolved.model;
        out->scale = resolved.scale;
    }
    return true;
}

qboolean XR_Interaction_GetFireViewmodel(int weapon, entity_t *out)
{
    if (!out || !weapon)
        return false;
    return xr_get_weapon_viewmodel(out, weapon, false);
}

static int xr_alias_frame_count (qmodel_t *model)
{
    if (!model || model->type != mod_alias) return 0;
    return ((aliashdr_t *)Mod_Extradata (model))->numframes;
}

qboolean XR_Interaction_GetOffhandViewmodel(entity_t *out)
{
    if (!offhand_weapon_item || wheel_active || two_hand_mode_active) return false;
    if (!xr_get_weapon_viewmodel(out, offhand_weapon_item, false)) return false;
    if (cl.maxclients == 1)
    {
        /* Local fire preserves its own frame timeline because cl.viewent remains the main-hand presentation. */
        out->frame = (int)xr_local_offhand_presentation_frame (xr_alias_frame_count (out->model));
    }
    else if (offhand_fire.phase == XR_OFFHAND_FIRING && xr_server_active_weapon_item() == offhand_weapon_item)
    {
        out->frame = cl.viewent.frame;
        offhand_fire.presentation_frame = cl.viewent.frame;
        offhand_fire.presentation_frame_valid = true;
    }
    else if (offhand_fire.phase == XR_OFFHAND_WAIT_RESTORE && offhand_fire.presentation_frame_valid)
        out->frame = (int)offhand_fire.presentation_frame;
    out->lerpflags = LERP_RESETMOVE | LERP_RESETANIM;
    return true;
}
qboolean XR_Interaction_GetVirtualPointer(float start[3], float hit[3]) { if (!pointer_active) return false; if (start) VectorCopy(pointer_start_xr, start); if (hit) VectorCopy(pointer_hit_xr, hit); return true; }

static void xr_weaponwheel_place_model(entity_t *ent, const vec3_t target, const vec3_t angles)
{
    float matrix[16];
    vec3_t centre, offset, zero = {0, 0, 0};

    centre[0] = (ent->model->mins[0] + ent->model->maxs[0]) * 0.5f;
    centre[1] = (ent->model->mins[1] + ent->model->maxs[1]) * 0.5f;
    centre[2] = (ent->model->mins[2] + ent->model->maxs[2]) * 0.5f;
    R_EntityMatrix(matrix, zero, (vec_t *)angles, ent->scale);
    if (ent->wheel_scale > 0.f) { int i; for (i = 0; i < 12; ++i) if (i % 4 != 3) matrix[i] *= ent->wheel_scale; }
    offset[0] = matrix[0] * centre[0] + matrix[4] * centre[1] + matrix[8] * centre[2];
    offset[1] = matrix[1] * centre[0] + matrix[5] * centre[1] + matrix[9] * centre[2];
    offset[2] = matrix[2] * centre[0] + matrix[6] * centre[1] + matrix[10] * centre[2];
    VectorSubtract(target, offset, ent->origin);
    VectorCopy(angles, ent->angles);
    ent->lerpflags = LERP_RESETMOVE;
}
void XR_Interaction_AddWorldEntities(void)
{
    vec3_t hub, cursor, slot, a, b, forward, right, up, angles;
    float worldscale, distance, radius, targetsize, biggest;
    int i, teleport_rgb = 0, visible_count;
    uint32_t teleport_color;
    wheel_entity_count = 0;
    sscanf(vr_teleport_beam_color.string, "%x", &teleport_rgb);
    teleport_color = ((uint32_t)(CLAMP(0.f, vr_teleport_beam_alpha.value, 1.f) * 255.f) << 24) | ((uint32_t)(teleport_rgb & 0xff) << 16) | ((uint32_t)(teleport_rgb & 0xff00)) | ((uint32_t)((teleport_rgb >> 16) & 0xff));
    if (XR_Input_GetTeleportAim(a, b))
    {
        { vec3_t previous, point, delta; int segment;
          VectorCopy(a, previous); VectorSubtract(b, a, delta);
          for (segment = 1; segment <= 12; ++segment) { float t = (float)segment / 12.f; VectorMA(a, t, delta, point); point[2] += 0.035f * VectorLength(delta) * (4.f * t * (1.f - t)); R_EmitLine(previous, point, teleport_color); VectorCopy(point, previous); } }
        R_SetXRTeleportMarker(b, XR_Input_HasTeleportTarget(), teleport_color);
    }
    else
        R_SetXRTeleportMarker(NULL, false, 0);
    if (!wheel_active || !xr_wheel_pose(wheel_hand, wheel_origin, NULL, NULL, NULL) || !xr_wheel_pose(wheel_hand, NULL, forward, right, up)) return;
    worldscale = CLAMP(1.f, vr_world_scale.value, 60.f);
    distance = vr_weaponwheel_distance.value * worldscale;
    radius = vr_weaponwheel_radius.value * worldscale;
    targetsize = vr_weaponwheel_modelsize.value * worldscale;
    VectorMA(wheel_origin, distance, wheel_forward, hub);
    VectorMA(hub, radius * wheel_cursor[0], wheel_right, cursor);
    VectorMA(cursor, radius * wheel_cursor[1], wheel_up, cursor);
    R_EmitLine(wheel_origin, cursor, 0xffd08020u);
    VectorMA(cursor, -0.02f * worldscale, wheel_right, a); VectorMA(cursor, 0.02f * worldscale, wheel_right, b); R_EmitLine(a, b, 0xffffffffu);
    VectorMA(cursor, -0.02f * worldscale, wheel_up, a); VectorMA(cursor, 0.02f * worldscale, wheel_up, b); R_EmitLine(a, b, 0xffffffffu);
    {
        float framelerp;
        double elapsed = wheel_spin_time > 0.0 ? q_max(0.0, realtime - wheel_spin_time) : 0.0;
        wheel_spin_time = realtime;
        framelerp = (float)q_min(1.0, elapsed * 12.0);
        wheel_spin += (float)(elapsed * vr_weaponwheel_spin.value);
        while (wheel_spin >= 360.f) wheel_spin -= 360.f;
        while (wheel_spin < 0.f) wheel_spin += 360.f;
        for (i = 0; i < xr_weapon_count; ++i)
            wheel_grow[i] += ((i == wheel_selection ? 1.f : 0.f) - wheel_grow[i]) * framelerp;
    }
    angles[PITCH] = vr_weaponwheel_modelpitch.value;
    angles[YAW] = RAD2DEG(atan2f(wheel_forward[1], wheel_forward[0])) + vr_weaponwheel_modelyaw.value + wheel_spin;
    angles[ROLL] = 0.f;
    visible_count = xr_wheel_visible_count();
    for (i = 0; i < xr_weapon_count && wheel_entity_count < (int)Q_COUNTOF(wheel_entities); ++i) {
        entity_t *ent;
        float angle;
        if (!xr_wheel_slot_visible(i) || !xr_weapons[i].model || visible_count == 0) continue;
        angle = (float)xr_wheel_visible_index(i) * (2.f * (float)M_PI / visible_count);
        VectorMA(hub, sinf(angle) * radius, wheel_right, slot);
        VectorMA(slot, cosf(angle) * radius, wheel_up, slot);
        ent = &wheel_entities[wheel_entity_count++];
        memset(ent, 0, sizeof(*ent));
        ent->model = xr_weapons[i].model;
        ent->colormap = vid.colormap;
        VectorCopy(slot, ent->origin);
        biggest = q_max(ent->model->maxs[0] - ent->model->mins[0], ent->model->maxs[1] - ent->model->mins[1]);
        biggest = q_max(biggest, ent->model->maxs[2] - ent->model->mins[2]);
        ent->scale = ENTSCALE_DEFAULT;
        ent->wheel_scale = biggest > 0.001f ? targetsize * VR_WeaponWheelScale(ent->model) * (1.f + 0.6f * wheel_grow[i]) / biggest : 1.f;
        xr_weaponwheel_place_model(ent, slot, angles);
        ent->wheel_brightness = 0.55f + 0.95f * wheel_grow[i];
        ent->alpha = ENTALPHA_DEFAULT;
    }
    for (i = 0; i < xr_weapon_count && wheel_entity_count < (int)Q_COUNTOF(wheel_entities); ++i) {
        if (xr_server_active_weapon_item() != xr_weapons[i].item || !xr_wheel_slot_visible(i) || !xr_weapons[i].model) continue;
        entity_t *ent = &wheel_entities[wheel_entity_count++];
        memset(ent, 0, sizeof(*ent));
        ent->model = xr_weapons[i].model;
        ent->colormap = vid.colormap;
        VectorCopy(hub, ent->origin);
        biggest = q_max(ent->model->maxs[0] - ent->model->mins[0], ent->model->maxs[1] - ent->model->mins[1]);
        biggest = q_max(biggest, ent->model->maxs[2] - ent->model->mins[2]);
        ent->scale = ENTSCALE_DEFAULT;
        ent->wheel_scale = biggest > 0.001f ? targetsize * VR_WeaponWheelScale(ent->model) * 0.7f / biggest : 1.f;
        xr_weaponwheel_place_model(ent, hub, angles);
        ent->wheel_brightness = 0.45f;
        ent->alpha = ENTALPHA_DEFAULT;
        break;
    }
}

void XR_Interaction_DrawWorldModels(void)
{
    entity_t *entities[17];
    int i;
    for (i = 0; i < wheel_entity_count; ++i) entities[i] = &wheel_entities[i];
    if (wheel_entity_count) R_DrawAliasModels(entities, wheel_entity_count);
}
static void xr_keyboard_label(int x, int y, const char *text)
{
    GL_SetCanvasColor(1.f, 0.72f, 0.12f, 1.f);
    Draw_StringEx(x, y, 16, text);
    GL_SetCanvasColor(1.f, 1.f, 1.f, 1.f);
}

static void xr_keyboard_key(int x, int y, int width, int height, qboolean selected)
{
    static const float normal[3] = {0.12f, 0.12f, 0.12f};
    static const float highlight[3] = {0.26f, 0.20f, 0.08f};
    Draw_FillEx(x, y, width, height, selected ? highlight : normal, 0.70f);
}

void XR_Interaction_Draw(void)
{
    int i;
    if (!xr_vignette_pic) xr_vignette_init();
    GL_SetCanvas(CANVAS_DEFAULT);
    {
        vec3_t xr_origin;
        if (wheel_active && !xr_wheel_pose(wheel_hand, xr_origin, NULL, NULL, NULL)) {
        int cx = vid.guiwidth / 2, cy = vid.guiheight / 2;
        Draw_Fill(cx - 155, cy - 155, 310, 310, 0, 0.55f);
        for (i = 0; i < (int)xr_weapon_count; ++i) {
            int x = cx + (int)(cosf((float)i * (float)(2.0 * M_PI / xr_weapon_count)) * 110.f);
            int y = cy + (int)(sinf((float)i * (float)(2.0 * M_PI / xr_weapon_count)) * 110.f);
            if (i == wheel_selection) Draw_Fill(x - 44, y - 12, 88, 24, 14, 0.8f);
            Draw_String(x - (int)strlen(xr_weapons[i].name) * 4, y - 4, xr_weapons[i].name);
        }
        if (wheel_selection >= 0) Draw_String(cx - 56, cy + 135, xr_weapons[wheel_selection].name);
    }
    }
    if (keyboard_active && key_dest == key_game) xr_keyboard_close();
    if (keyboard_active) {
        static const char *rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM-."};
        static const char symbols[] = "!@#$%^&*()[]{}<>?/\\|`~_=+:;\"',";
        int left = (int)(vid.guiwidth * 0.03125f), top = (int)(vid.guiheight * 0.50f), width = (int)(vid.guiwidth * 0.9375f), height = (int)(vid.guiheight * 0.45f);
        int key_width = width / 10, key_height = height / 5, selected_row = -1, selected_col = -1;
        GL_SetCanvas(CANVAS_DEFAULT);
        xr_keyboard_key_at(keyboard_x, keyboard_y, &selected_row, &selected_col);
        {
            static const float background[3] = {0.05f, 0.05f, 0.05f};
            Draw_FillEx(left - 6, top - 6, width + 12, height + 12, background, 0.70f);
        }
        for (i = 0; i < 4; ++i) {
            int col;
            for (col = 0; col < 10; ++col) {
                int x = left + col * key_width, y = top + i * key_height;
                const char *label = NULL;
                char key[2] = {' ', '\0'};
                if (i < 4 && col < (int)strlen(rows[i])) {
                    key[0] = rows[i][col];
                    if (keyboard_mode == 2 && i > 0) key[0] = symbols[(i - 1) * 10 + col];
                    else if (keyboard_caps || keyboard_mode == 1) key[0] = toupper(key[0]);
                    label = key;
                } else if (i == 2 && col == 9) label = "BKSP";
                xr_keyboard_key(x + 1, y + 1, key_width - 2, key_height - 2, i == selected_row && col == selected_col);
                if (label) xr_keyboard_label(x + (key_width - (int)strlen(label) * 16) / 2, y + (key_height - 16) / 2, label);
            }
        }
        {
            int y = top + 4 * key_height;
            xr_keyboard_key(left + 1, y + 1, key_width - 2, key_height - 2, (selected_row == 4 && selected_col == 0) || (keyboard_caps && keyboard_mode != 2));
            xr_keyboard_label(left + (key_width - 16) / 2, y + (key_height - 16) / 2, "^");
            xr_keyboard_key(left + key_width + 1, y + 1, key_width - 2, key_height - 2, (selected_row == 4 && selected_col == 1) || keyboard_mode == 1);
            xr_keyboard_label(left + key_width + (key_width - 5 * 16) / 2, y + (key_height - 16) / 2, "SHIFT");
            xr_keyboard_key(left + 2 * key_width + 1, y + 1, key_width - 2, key_height - 2, (selected_row == 4 && selected_col == 2) || keyboard_mode == 2);
            xr_keyboard_label(left + 2 * key_width + (key_width - 3 * 16) / 2, y + (key_height - 16) / 2, "SYM");
            xr_keyboard_key(left + 3 * key_width + 1, y + 1, 5 * key_width - 2, key_height - 2, selected_row == 4 && selected_col >= 3 && selected_col < 8);
            xr_keyboard_label(left + 3 * key_width + (5 * key_width - 5 * 16) / 2, y + (key_height - 16) / 2, "SPACE");
            xr_keyboard_key(left + 8 * key_width + 1, y + 1, 2 * key_width - 2, key_height - 2, selected_row == 4 && selected_col >= 8);
            xr_keyboard_label(left + 8 * key_width + (2 * key_width - 5 * 16) / 2, y + (key_height - 16) / 2, "ENTER");
        }
    }
    if (pointer_active && !VID_XR_GetStereoFrame(NULL)) {
        unsigned color = xr_mouse_rgb();
        int x = (int)(virtual_mouse_x * vid.guiwidth + 0.5f);
        int y = (int)(virtual_mouse_y * vid.guiheight + 0.5f);
        GL_SetCanvas(CANVAS_DEFAULT);
        float rgb[3] = {((color >> 16) & 255) / 255.f, ((color >> 8) & 255) / 255.f, (color & 255) / 255.f};
        float alpha = CLAMP(0.f, vr_mouse_alpha.value, 1.f);
        Draw_FillEx(x - 7, y - 1, 15, 3, rgb, alpha);
        Draw_FillEx(x - 1, y - 7, 3, 15, rgb, alpha);
        Draw_FillEx(x - 3, y - 3, 7, 7, rgb, alpha);
        { static const float center[3] = {0.f, 0.f, 0.f}; Draw_FillEx(x - 1, y - 1, 3, 3, center, alpha); }
    }
    if (vignette_value > 0.f && !wheel_active && !keyboard_active && xr_vignette_pic) {
        static const float black[3] = {0.f, 0.f, 0.f};
        const float alpha = CLAMP(0.f, vignette_value, 1.f);
        const float overscan_x = vid.guiwidth * 0.16f;
        const float overscan_y = vid.guiheight * 0.16f;
        Draw_SubPic((int)-overscan_x, (int)-overscan_y,
                     (int)(vid.guiwidth + overscan_x * 2.f),
                     (int)(vid.guiheight + overscan_y * 2.f),
                     xr_vignette_pic, 0, 0, 1, 1, black, alpha);
    }}
