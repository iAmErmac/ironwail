#include "quakedef.h"
#include "input.h"
#include "xr_bridge.h"
#include "xr_input.h"
#include "xr_interaction.h"
#include "xr_interaction.h"

extern qboolean VID_XR_GetActions(iw_xr_action_snapshot_t *actions);
extern cvar_t vr_dominant_hand;
extern cvar_t vr_world_scale;
extern cvar_t vr_stabilize_mode;
extern void R_XRAdjustYaw(float delta);
extern void VID_XR_Haptic(int hand, float amplitude, float duration_seconds);

cvar_t vr_turn_mode = {"vr_turn_mode", "0", CVAR_ARCHIVE};
cvar_t vr_turn_angle = {"vr_turn_angle", "30", CVAR_ARCHIVE};
cvar_t vr_move_deadzone = {"vr_move_deadzone", "0.20", CVAR_ARCHIVE};
cvar_t vr_turn_deadzone = {"vr_turn_deadzone", "0.20", CVAR_ARCHIVE};
cvar_t vr_haptic_intensity = {"vr_haptic_intensity", "1", CVAR_ARCHIVE};
cvar_t vr_player_speed = {"vr_player_speed", "100", CVAR_ARCHIVE};
cvar_t vr_smooth_stairs = {"vr_smooth_stairs", "1", CVAR_ARCHIVE};
cvar_t vr_roomscale = {"vr_roomscale", "1", CVAR_ARCHIVE};
cvar_t vr_teleport = {"vr_teleport", "0", CVAR_ARCHIVE};
cvar_t vr_teleport_range = {"vr_teleport_range", "400", CVAR_ARCHIVE};
cvar_t vr_teleport_speed = {"vr_teleport_speed", "2800", CVAR_ARCHIVE};
cvar_t vr_teleport_beam_color = {"vr_teleport_beam_color", "00FFFF", CVAR_ARCHIVE};
cvar_t vr_teleport_beam_alpha = {"vr_teleport_beam_alpha", "0.6", CVAR_ARCHIVE};
cvar_t vr_teleport_auto_jump = {"vr_teleport_auto_jump", "0", CVAR_ARCHIVE};

static qboolean xr_key_state[15];
static qboolean xr_owns_input;
static qboolean xr_ui_release_suppressed, xr_modal_confirm_held;
static qboolean xr_turn_held;
static float xr_smooth_turn_rate;
static double xr_menu_repeat_time[4];
static qboolean xr_roomscale_position_valid;
static float xr_roomscale_position[3];
static qboolean xr_teleport_aiming, xr_teleport_target_valid, xr_dash_active, xr_dash_auto_jump;
static iw_xr_hand_t xr_teleport_hand = IW_XR_HAND_COUNT;
static vec3_t xr_teleport_start, xr_teleport_ray_end, xr_teleport_target;
static float xr_dash_last_distance, xr_dash_jump_delay;
static int xr_dash_blocked_frames;

static void XR_Input_ApplyPlayerDelta(const vec3_t delta);

qboolean XR_Input_WantsCutsceneSkip(void)
{
    iw_xr_action_snapshot_t actions;
    iw_xr_hand_t dominant;
    if (key_dest != key_game || (!CL_InCutscene() && !cl.intermission) || !VID_XR_GetActions(&actions)) return false;
    dominant = XR_Input_PhysicalHandForRole(XR_HAND_MAINHAND);
    return (actions.hand[IW_XR_HAND_RIGHT].buttons & IW_XR_BUTTON_TRIGGER) != 0 ||
        (actions.hand[dominant].buttons & IW_XR_BUTTON_PRIMARY) != 0;
}
iw_xr_hand_t XR_Input_PhysicalHandForRole(xr_hand_role_t role)
{
    iw_xr_hand_t mainhand = vr_dominant_hand.value != 0.f ? IW_XR_HAND_LEFT : IW_XR_HAND_RIGHT;
    return role == XR_HAND_OFFHAND ? (mainhand == IW_XR_HAND_LEFT ? IW_XR_HAND_RIGHT : IW_XR_HAND_LEFT) : mainhand;
}

xr_hand_role_t XR_Input_RoleForPhysicalHand(iw_xr_hand_t hand)
{
    return hand == XR_Input_PhysicalHandForRole(XR_HAND_MAINHAND) ? XR_HAND_MAINHAND : XR_HAND_OFFHAND;
}

const iw_xr_hand_snapshot_t *XR_Input_HandForRole(const iw_xr_action_snapshot_t *actions, xr_hand_role_t role)
{
    return actions ? &actions->hand[XR_Input_PhysicalHandForRole(role)] : NULL;
}

iw_xr_hand_t XR_Input_MovementHand(void) { return vr_dominant_hand.value == 1.f ? IW_XR_HAND_RIGHT : IW_XR_HAND_LEFT; }
iw_xr_hand_t XR_Input_TurnHand(void) { return vr_dominant_hand.value == 1.f ? IW_XR_HAND_LEFT : IW_XR_HAND_RIGHT; }
iw_xr_hand_t XR_Input_MenuHand(void) { return XR_Input_MovementHand(); }
/* Mouse stays on the physical right, left, right controller for right-handed, left-handed, and left-handed swap-sticks. */
iw_xr_hand_t XR_Input_MouseHand(void) { return vr_dominant_hand.value == 1.f ? IW_XR_HAND_LEFT : IW_XR_HAND_RIGHT; }
iw_xr_hand_t XR_Input_RightStickHand(void) { return vr_dominant_hand.value == 1.f ? IW_XR_HAND_LEFT : IW_XR_HAND_RIGHT; }
iw_xr_hand_t XR_Input_LeftStickHand(void) { return vr_dominant_hand.value == 1.f ? IW_XR_HAND_RIGHT : IW_XR_HAND_LEFT; }

static void XR_Input_MenuHaptic(void) {
    iw_xr_hand_t offhand = XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND);
    VID_XR_Haptic(offhand, 0.22f, 0.025f);
}

static void XR_Input_Key(int slot, int key, qboolean down)
{
    if (xr_key_state[slot] == down)
        return;
    xr_key_state[slot] = down;
    Key_Event(key, down);
}

static void XR_Input_MenuKey(int slot, int key, qboolean down)
{
    int repeat_slot = slot - 9;
    if (!down)
    {
        XR_Input_Key(slot, key, false);
        xr_menu_repeat_time[repeat_slot] = 0.0;
        return;
    }
    if (!xr_key_state[slot])
    {
        XR_Input_Key(slot, key, true);
        XR_Input_MenuHaptic();
        xr_menu_repeat_time[repeat_slot] = realtime + 0.35;
    }
    else if (realtime >= xr_menu_repeat_time[repeat_slot])
    {
        Key_Event(key, true);
        XR_Input_MenuHaptic();
        xr_menu_repeat_time[repeat_slot] = realtime + 0.09;
    }
}

static float XR_Input_SmoothTurn(float axis, float delta)
{
    float abs_axis = fabsf(axis);
    float target = 0.0f;
    float setting = CLAMP(1.0f, vr_turn_angle.value, 10.0f);
    float speed_frac = CLAMP(0.0f, (vr_turn_angle.value - 1.0f) / 89.0f, 1.0f);
    float max_speed = 480.0f - 435.0f * speed_frac * speed_frac * (3.0f - 2.0f * speed_frac);
    float response_scale = 1.0f + (10.0f - setting);
    float response;
    if (abs_axis > 0.10f)
    {
        float t = CLAMP(0.0f, (abs_axis - 0.10f) / 0.90f, 1.0f);
        t = t * t * (3.0f - 2.0f * t);
        target = (axis > 0.0f ? -1.0f : 1.0f) * max_speed * t;
    }
    response = 1.0f - expf(-8.0f * response_scale * q_max(0.0f, delta));
    xr_smooth_turn_rate += (target - xr_smooth_turn_rate) * response;
    return xr_smooth_turn_rate * delta;
}

void XR_Input_Init(void)
{
    Cvar_RegisterVariable(&vr_turn_mode);
    Cvar_RegisterVariable(&vr_turn_angle);
    Cvar_RegisterVariable(&vr_move_deadzone);
    Cvar_RegisterVariable(&vr_turn_deadzone);
    Cvar_RegisterVariable(&vr_haptic_intensity);
    Cvar_RegisterVariable(&vr_player_speed);
    Cvar_RegisterVariable(&vr_smooth_stairs);
    Cvar_RegisterVariable(&vr_roomscale);
    Cvar_RegisterVariable(&vr_teleport);
    Cvar_RegisterVariable(&vr_teleport_range);
    Cvar_RegisterVariable(&vr_teleport_speed);
    Cvar_RegisterVariable(&vr_teleport_beam_color);
    Cvar_RegisterVariable(&vr_teleport_beam_alpha);
    Cvar_RegisterVariable(&vr_teleport_auto_jump);
    XR_Interaction_Init();

}

void XR_Input_Shutdown(void)
{
    static const int keys[] = { K_RTRIGGER, K_LTRIGGER, K_ABUTTON, K_BBUTTON, K_XBUTTON, K_YBUTTON, K_RTHUMB, K_LTHUMB, K_ESCAPE, K_LEFTARROW, K_RIGHTARROW, K_DOWNARROW, K_UPARROW, K_LGRIP, K_RGRIP };
    int i;
    for (i = 0; i < Q_COUNTOF(xr_key_state); ++i)
    {
        if (xr_key_state[i]) Key_Event(keys[i], false);
        xr_key_state[i] = false;
    }
    xr_owns_input = false;
    xr_ui_release_suppressed = false;
    xr_modal_confirm_held = false;
    xr_turn_held = false;
    xr_roomscale_position_valid = false;
    xr_teleport_aiming = false;
    xr_teleport_target_valid = false;
    xr_teleport_hand = IW_XR_HAND_COUNT;
    xr_dash_active = false;
    xr_dash_auto_jump = false;
    xr_dash_jump_delay = 0.f;
    XR_Interaction_Shutdown();
}

static qboolean XR_Input_CanTeleport(void)
{
    return vr_teleport.value != 0.f && sv.active && !sv.paused &&
        svs.maxclients == 1 && cl.maxclients <= 1 && key_dest == key_game;
}

static void XR_Input_UpdateTeleportAim(iw_xr_hand_t hand)
{
    edict_t *player;
    qcvm_t *oldvm;
    vec3_t forward, ray_end, candidate;
    const float bias_sin = 0.5f, bias_cos = 0.8660254f;
    trace_t hit;

    xr_teleport_target_valid = false;
    if (!XR_Input_CanTeleport() || !svs.clients[0].active || !svs.clients[0].edict ||
        !R_GetXRHandTrackingPose(hand, xr_teleport_start, NULL, NULL, NULL) ||
        !R_GetXRHandAimPose(hand, NULL, forward, NULL, NULL))
        return;
    player = svs.clients[0].edict;
    if (player->free || player->v.health <= 0.f)
        return;
    {
        float planar = sqrtf(forward[0] * forward[0] + forward[1] * forward[1]);
        float vertical = forward[2];
        float rotated_planar;
        if (planar <= 0.001f)
            return;
        rotated_planar = planar * bias_cos + vertical * bias_sin;
        forward[0] *= rotated_planar / planar;
        forward[1] *= rotated_planar / planar;
        forward[2] = vertical * bias_cos - planar * bias_sin;
    }
    VectorMA(xr_teleport_start, CLAMP(64.f, vr_teleport_range.value, 800.f), forward, ray_end);
    PR_PushQCVM(&sv.qcvm, &oldvm);
    hit = SV_Move(xr_teleport_start, vec3_origin, vec3_origin, ray_end, MOVE_NORMAL, player);
    VectorCopy(hit.endpos, xr_teleport_ray_end);

    if (hit.fraction < 1.f && hit.plane.normal[2] >= 0.7f)
    {
        VectorCopy(hit.endpos, candidate);
        candidate[2] -= player->v.mins[2];
        VectorSubtract(player->v.origin, candidate, ray_end);
        if (VectorLength(ray_end) > 8.f)
        {
                VectorCopy(candidate, xr_teleport_target);
            xr_teleport_target_valid = true;
        }
    }
    PR_PopQCVM(oldvm);
}

static qboolean XR_Input_TeleportNeedsJump(edict_t *player)
{
    static const float samples[] = {0.25f, 0.50f, 0.75f};
    qcvm_t *oldvm;
    vec3_t start, end, point;
    float source_floor, target_floor, trace_top;
    int i;

    if (!player)
        return false;
    source_floor = player->v.origin[2] + player->v.mins[2];
    target_floor = xr_teleport_target[2] + player->v.mins[2];
    if (target_floor > source_floor + 8.f)
        return true;
    trace_top = q_max(player->v.origin[2], xr_teleport_target[2]) + 48.f;
    PR_PushQCVM(&sv.qcvm, &oldvm);
    for (i = 0; i < Q_COUNTOF(samples); ++i)
    {
        trace_t hit;
        float expected_floor;

        VectorSubtract(xr_teleport_target, player->v.origin, point);
        point[2] = 0.f;
        VectorMA(player->v.origin, samples[i], point, start);
        start[2] = trace_top;
        VectorCopy(start, end);
        end[2] = trace_top - 160.f;
        hit = SV_Move(start, vec3_origin, vec3_origin, end, MOVE_NORMAL, player);
        expected_floor = source_floor + (target_floor - source_floor) * samples[i];
        if (hit.fraction == 1.f || hit.endpos[2] < expected_floor - 16.f)
        {
            PR_PopQCVM(oldvm);
            return true;
        }
    }
    PR_PopQCVM(oldvm);
    return false;
}
static void XR_Input_UpdateTeleport(const iw_xr_action_snapshot_t *actions, iw_xr_hand_t hand)
{
    float x, y, deadzone;
    qboolean aiming;

    if (!actions || !XR_Input_CanTeleport())
    {
        xr_teleport_aiming = false;
        xr_teleport_target_valid = false;
    xr_teleport_hand = IW_XR_HAND_COUNT;
        return;
    }
    x = actions->hand[hand].stick[0];
    y = actions->hand[hand].stick[1];
    deadzone = CLAMP(0.f, vr_move_deadzone.value, 0.95f);
    aiming = x * x + y * y > deadzone * deadzone;
    if (aiming)
    {
        xr_teleport_aiming = true;
        xr_teleport_hand = hand;
        XR_Input_UpdateTeleportAim(hand);
    }
    else if (xr_teleport_aiming)
    {
        xr_teleport_aiming = false;
        if (xr_teleport_target_valid)
        {
            xr_dash_active = true;
            xr_dash_auto_jump = vr_teleport_auto_jump.value != 0.f && XR_Input_TeleportNeedsJump(svs.clients[0].edict);
            xr_dash_jump_delay = 0.f;
            xr_dash_last_distance = FLT_MAX;
            xr_dash_blocked_frames = 0;
            VID_XR_Haptic(0, 0.45f, 0.04f);
            VID_XR_Haptic(1, 0.45f, 0.04f);
        }
        xr_teleport_target_valid = false;
    }
}

void XR_Input_ApplyDash(void)
{
    edict_t *player;
    vec3_t delta;
    float distance, speed;

    if (!xr_dash_active)
        return;
    if (!XR_Input_CanTeleport() || !svs.clients[0].active || !svs.clients[0].edict)
    {
        xr_dash_active = false;
        return;
    }
    player = svs.clients[0].edict;
    if (player->free || player->v.health <= 0.f)
    {
        xr_dash_active = false;
        return;
    }
    if (xr_dash_auto_jump)
    {
        player->v.flags = (int)player->v.flags & ~FL_ONGROUND;
        player->v.groundentity = 0;
        player->v.velocity[2] = 270.f;
        xr_dash_auto_jump = false;
        xr_dash_jump_delay = 0.10f;
        return;
    }
    if (xr_dash_jump_delay > 0.f)
    {
        xr_dash_jump_delay = q_max(0.f, xr_dash_jump_delay - host_frametime);
        return;
    }
    VectorSubtract(xr_teleport_target, player->v.origin, delta);
    delta[2] = 0.f;
    distance = VectorLength(delta);
    if (distance < q_max(4.f, host_frametime * CLAMP(800.f, vr_teleport_speed.value, 6000.f)))
    {
        xr_dash_active = false;
        xr_dash_jump_delay = 0.f;
        player->v.velocity[0] = player->v.velocity[1] = player->v.velocity[2] = 0.f;
        return;
    }
    if (distance >= xr_dash_last_distance - 1.f)
    {
        if (++xr_dash_blocked_frames >= 3)
        {
            xr_dash_active = false;
            xr_dash_jump_delay = 0.f;
            return;
        }
    }
    else
        xr_dash_blocked_frames = 0;
    xr_dash_last_distance = distance;
    speed = CLAMP(800.f, vr_teleport_speed.value, 6000.f);
    if (player->v.waterlevel >= 2.f)
    {
        vec3_t move;
        float move_distance = q_min(distance, (float)host_frametime * speed);
        VectorScale(delta, move_distance / distance, move);
        move[2] = CLAMP(-move_distance, xr_teleport_target[2] - player->v.origin[2], move_distance);
        XR_Input_ApplyPlayerDelta(move);
        player->v.velocity[0] = player->v.velocity[1] = player->v.velocity[2] = 0.f;
        return;
    }
    VectorScale(delta, speed / distance, player->v.velocity);
    player->v.velocity[2] = 0.f;
}

qboolean XR_Input_IsDashing(void)
{
    return xr_dash_active;
}

qboolean XR_Input_IsTeleportAiming(void)
{
    return xr_teleport_aiming;
}

qboolean XR_Input_HasTeleportTarget(void)
{
    return xr_teleport_target_valid;
}

qboolean XR_Input_GetTeleportAim(vec3_t start, vec3_t target)
{
    if (!xr_teleport_aiming)
        return false;
    if (start && !R_GetXRHandTrackingPose(xr_teleport_hand, start, NULL, NULL, NULL)) VectorCopy(xr_teleport_start, start);
    if (target) VectorCopy(xr_teleport_ray_end, target);
    return true;
}
void XR_Input_PrepareInputGrab(void)
{
    iw_xr_action_snapshot_t actions;
    iw_xr_hand_t dominant, offhand;
    if (!VID_XR_GetActions(&actions)) return;
    dominant = XR_Input_PhysicalHandForRole(XR_HAND_MAINHAND);
    offhand = XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND);
    xr_key_state[0] = (actions.hand[dominant].buttons & IW_XR_BUTTON_TRIGGER) != 0;
    xr_key_state[1] = (actions.hand[offhand].buttons & IW_XR_BUTTON_TRIGGER) != 0;
    xr_key_state[2] = (actions.hand[dominant].buttons & IW_XR_BUTTON_PRIMARY) != 0;
    xr_key_state[3] = (actions.hand[dominant].buttons & IW_XR_BUTTON_SECONDARY) != 0;
    xr_key_state[4] = (actions.hand[offhand].buttons & IW_XR_BUTTON_PRIMARY) != 0;
    xr_key_state[5] = (actions.hand[offhand].buttons & IW_XR_BUTTON_SECONDARY) != 0;
    xr_key_state[6] = (actions.hand[XR_Input_RightStickHand()].buttons & IW_XR_BUTTON_STICK) != 0;
    xr_key_state[7] = (actions.hand[XR_Input_LeftStickHand()].buttons & IW_XR_BUTTON_STICK) != 0;
    xr_key_state[8] = (actions.hand[IW_XR_HAND_LEFT].buttons & IW_XR_BUTTON_MENU) != 0 || (actions.hand[IW_XR_HAND_RIGHT].buttons & IW_XR_BUTTON_MENU) != 0;
    xr_key_state[13] = actions.hand[offhand].grip > 0.5f || (actions.hand[offhand].buttons & IW_XR_BUTTON_GRIP) != 0;
    xr_key_state[14] = actions.hand[dominant].grip > 0.5f || (actions.hand[dominant].buttons & IW_XR_BUTTON_GRIP) != 0;
    xr_modal_confirm_held = (actions.hand[IW_XR_HAND_RIGHT].buttons & IW_XR_BUTTON_TRIGGER) != 0 ||
        (actions.hand[dominant].buttons & IW_XR_BUTTON_PRIMARY) != 0;
}

void XR_Input_Update(void)
{
    iw_xr_action_snapshot_t actions;
    iw_xr_hand_t dominant = XR_Input_PhysicalHandForRole(XR_HAND_MAINHAND);
    iw_xr_hand_t offhand = XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND);
    float turn;
    qboolean was_ui, gameplay_controls_held;
    xr_owns_input = VID_XR_GetActions(&actions);
    if (!xr_owns_input)
    {
        XR_Input_Shutdown();
        return;
    }
    if (Key_IsInputGrabActive())
    {
        qboolean confirm = (actions.hand[IW_XR_HAND_RIGHT].buttons & IW_XR_BUTTON_TRIGGER) != 0 ||
            (actions.hand[dominant].buttons & IW_XR_BUTTON_PRIMARY) != 0;
        if (confirm && !xr_modal_confirm_held) Key_Event(K_ABUTTON, true);
        xr_modal_confirm_held = confirm;
        return;
    }
    XR_Input_Key(14, K_RGRIP, (actions.hand[dominant].grip > 0.5f || (actions.hand[dominant].buttons & IW_XR_BUTTON_GRIP)) != 0);
    XR_Input_Key(13, K_LGRIP, (!vr_stabilize_mode.value || (actions.hand[dominant].grip <= 0.5f && (actions.hand[dominant].buttons & IW_XR_BUTTON_GRIP) == 0)) && (actions.hand[offhand].grip > 0.5f || (actions.hand[offhand].buttons & IW_XR_BUTTON_GRIP)));
    was_ui = key_dest != key_game;
    XR_Interaction_Update(&actions);
    if (was_ui && key_dest == key_game)
        xr_ui_release_suppressed = true;
    gameplay_controls_held = ((actions.hand[IW_XR_HAND_LEFT].buttons | actions.hand[IW_XR_HAND_RIGHT].buttons) &
        (IW_XR_BUTTON_TRIGGER | IW_XR_BUTTON_PRIMARY | IW_XR_BUTTON_SECONDARY)) != 0;
    if (!gameplay_controls_held)
        xr_ui_release_suppressed = false;
    xr_modal_confirm_held = false;
    XR_Input_UpdateTeleport(&actions, XR_Input_MovementHand());
    if (xr_ui_release_suppressed || XR_Interaction_ConsumesGameplay()) {
        XR_Input_Key(0, K_RTRIGGER, false);
        XR_Input_Key(1, K_LTRIGGER, false);
        XR_Input_Key(2, K_ABUTTON, false);
        XR_Input_Key(3, K_BBUTTON, false);
        XR_Input_Key(4, K_XBUTTON, false);
        XR_Input_Key(5, K_YBUTTON, false);
        XR_Input_Key(6, K_RTHUMB, false);
        XR_Input_Key(7, K_LTHUMB, false);
        XR_Input_Key(8, K_ESCAPE, false);
        xr_turn_held = false;
        xr_roomscale_position_valid = false;
        return;
    }
    XR_Input_Key(0, K_RTRIGGER, (actions.hand[dominant].buttons & IW_XR_BUTTON_TRIGGER) != 0);
    XR_Input_Key(1, K_LTRIGGER, (actions.hand[offhand].buttons & IW_XR_BUTTON_TRIGGER) != 0 && XR_Interaction_AllowOffhandFireInput());


    {
        qboolean menu_toggle = (actions.hand[IW_XR_HAND_LEFT].buttons & IW_XR_BUTTON_MENU) != 0 || (actions.hand[IW_XR_HAND_RIGHT].buttons & IW_XR_BUTTON_MENU) != 0 ||
            ((actions.hand[dominant].buttons & (IW_XR_BUTTON_GRIP | IW_XR_BUTTON_SECONDARY)) == (IW_XR_BUTTON_GRIP | IW_XR_BUTTON_SECONDARY));
        XR_Input_Key(2, K_ABUTTON, (actions.hand[dominant].buttons & IW_XR_BUTTON_PRIMARY) != 0);
        XR_Input_Key(3, K_BBUTTON, !menu_toggle && (actions.hand[dominant].buttons & IW_XR_BUTTON_SECONDARY) != 0);
        XR_Input_Key(4, K_XBUTTON, (actions.hand[offhand].buttons & IW_XR_BUTTON_PRIMARY) != 0);
        XR_Input_Key(5, K_YBUTTON, (actions.hand[offhand].buttons & IW_XR_BUTTON_SECONDARY) != 0);
        XR_Input_Key(6, K_RTHUMB, (actions.hand[XR_Input_RightStickHand()].buttons & IW_XR_BUTTON_STICK) != 0);
        XR_Input_Key(7, K_LTHUMB, (actions.hand[XR_Input_LeftStickHand()].buttons & IW_XR_BUTTON_STICK) != 0);
        XR_Input_Key(8, K_ESCAPE, menu_toggle);
    }

    if (key_dest == key_menu)
    {
        float x = actions.hand[XR_Input_MenuHand()].stick[0];
        float y = actions.hand[XR_Input_MenuHand()].stick[1];
        float ax = fabsf(x), ay = fabsf(y);
        qboolean horizontal = ax > 0.25f && ax >= ay + 0.15f;
        qboolean vertical = ay > 0.25f && ay >= ax + 0.15f;
        XR_Input_MenuKey(9, K_LEFTARROW, horizontal && x < 0.0f);
        XR_Input_MenuKey(10, K_RIGHTARROW, horizontal && x > 0.0f);
        XR_Input_MenuKey(11, K_DOWNARROW, vertical && y < 0.0f);
        XR_Input_MenuKey(12, K_UPARROW, vertical && y > 0.0f);
        xr_turn_held = false;
        xr_roomscale_position_valid = false;
        return;
    }
    XR_Input_Key(9, K_LEFTARROW, false);
    XR_Input_Key(10, K_RIGHTARROW, false);
    XR_Input_Key(11, K_DOWNARROW, false);
    XR_Input_Key(12, K_UPARROW, false);

    turn = actions.hand[XR_Input_TurnHand()].stick[0];
    if (fabsf(turn) < q_max(vr_turn_deadzone.value, 0.20f) || fabsf(turn) < fabsf(actions.hand[XR_Input_TurnHand()].stick[1]) + 0.20f)
    {
        xr_turn_held = false;
        xr_smooth_turn_rate = 0.0f;
    }
    else if (vr_turn_mode.value != 0 || vr_turn_angle.value <= 10.0f)
        R_XRAdjustYaw(XR_Input_SmoothTurn(turn, host_rawframetime));
    else if (!xr_turn_held)
    {
        R_XRAdjustYaw(turn > 0.0f ? -vr_turn_angle.value : vr_turn_angle.value);
        xr_turn_held = true;
    }
}

qboolean XR_Input_OwnsInput(void)
{
    return xr_owns_input;
}

qboolean XR_Input_UsesRoomscale(void)
{
    return xr_owns_input && vr_roomscale.value != 0.f;
}

static void XR_Input_ApplyPlayerDelta (const vec3_t delta)
{
    edict_t *player;
    qcvm_t *oldvm;
    vec3_t end, moved;
    trace_t trace;

    if (!sv.active || sv.paused || svs.maxclients != 1 || !svs.clients[0].active || !svs.clients[0].edict)
        return;
    player = svs.clients[0].edict;
    if (player->free || player->v.health <= 0)
        return;
    PR_PushQCVM (&sv.qcvm, &oldvm);
    VectorAdd (player->v.origin, delta, end);
    trace = SV_Move (player->v.origin, player->v.mins, player->v.maxs, end, MOVE_NORMAL, player);
    VectorSubtract (trace.endpos, player->v.origin, moved);
    VectorCopy (trace.endpos, player->v.origin);
    SV_LinkEdict (player, false);
    PR_PopQCVM (oldvm);
    if (cl.viewentity > 0 && cl.viewentity < cl.num_entities)
    {
        entity_t *view = &cl_entities[cl.viewentity];
        VectorAdd (view->origin, moved, view->origin);
        VectorAdd (view->msg_origins[0], moved, view->msg_origins[0]);
        VectorAdd (view->msg_origins[1], moved, view->msg_origins[1]);
    }
}

qboolean XR_Input_Move(usercmd_t *cmd)
{
    iw_xr_action_snapshot_t actions;
    iw_xr_hand_t movement = XR_Input_MovementHand();
    float x, y, length, deadzone;
    qboolean jump;
    if (!cmd || !xr_owns_input)
        return false;
    if (VID_XR_GetActions (&actions))
    {
        x = actions.hand[movement].stick[0];
        y = actions.hand[movement].stick[1];
    }
    else
    {
        x = y = 0.f;
        jump = false;
    }
    length = sqrtf(x * x + y * y);
    deadzone = CLAMP(0.0f, vr_move_deadzone.value, 0.95f);
    if (vr_roomscale.value != 0.f && cl.maxclients <= 1 && host_frametime > 0.f)
    {
        float head_position[3], xr_delta[3], quake_delta[3];
        if (VID_XR_GetHeadPosition (head_position))
        {
            if (xr_roomscale_position_valid)
            {
                xr_delta[0] = head_position[0] - xr_roomscale_position[0];
                xr_delta[1] = head_position[1] - xr_roomscale_position[1];
                xr_delta[2] = head_position[2] - xr_roomscale_position[2];
                if (R_XRTransformRoomscaleDelta (xr_delta, quake_delta))
                {
                    /* Roomscale collision is planar; tracked height is presentation-only. */
                    quake_delta[2] = 0.f;
                    if (sqrtf (quake_delta[0] * quake_delta[0] + quake_delta[1] * quake_delta[1]) < 0.5f)
                    {
                        VectorScale (quake_delta, vr_world_scale.value, quake_delta);
                        XR_Input_ApplyPlayerDelta (quake_delta);
                    }
                }
            }
            xr_roomscale_position[0] = head_position[0];
            xr_roomscale_position[1] = head_position[1];
            xr_roomscale_position[2] = head_position[2];
            xr_roomscale_position_valid = true;
        }
        else
            xr_roomscale_position_valid = false;
    }

    if (cl.inwater && cmd->upmove > 0.f)
        cmd->upmove = q_max(cmd->upmove, cl_upspeed.value * 2.f);

    if (vr_teleport.value == 0.f && length > deadzone)
    {
        float scale = (length - deadzone) / (1.0f - deadzone);
        scale /= length;
        cmd->sidemove += cl_sidespeed.value * x * scale * CLAMP(0.f, vr_player_speed.value, 300.f) * 0.01f;
        cmd->forwardmove += cl_forwardspeed.value * y * scale * CLAMP(0.f, vr_player_speed.value, 300.f) * 0.01f;
    }
    return true;
}
