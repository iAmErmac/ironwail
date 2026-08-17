#include "quakedef.h"
#include "input.h"
#include "xr_bridge.h"
#include "xr_input.h"
#include "xr_interaction.h"

extern qboolean VID_XR_GetActions(iw_xr_action_snapshot_t *actions);
extern cvar_t vr_dominant_hand;
extern cvar_t vr_stabilize_mode;
extern void R_XRAdjustYaw(float delta);
extern void VID_XR_Haptic(int hand, float amplitude, float duration_seconds);

cvar_t vr_turn_mode = {"vr_turn_mode", "0", CVAR_ARCHIVE};
cvar_t vr_turn_angle = {"vr_turn_angle", "30", CVAR_ARCHIVE};
cvar_t vr_move_deadzone = {"vr_move_deadzone", "0.20", CVAR_ARCHIVE};
cvar_t vr_turn_deadzone = {"vr_turn_deadzone", "0.20", CVAR_ARCHIVE};
cvar_t vr_haptic_intensity = {"vr_haptic_intensity", "1", CVAR_ARCHIVE};
cvar_t vr_player_speed = {"vr_player_speed", "100", CVAR_ARCHIVE};

static qboolean xr_key_state[15];
static qboolean xr_owns_input, xr_main_trigger_previous;
static qboolean xr_turn_held;
static float xr_smooth_turn_rate;
static double xr_menu_repeat_time[4];

static void XR_Input_MenuHaptic(void) {
    int offhand = vr_dominant_hand.value != 0.f ? 1 : 0;
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
    xr_turn_held = false;
    XR_Interaction_Shutdown();
}

void XR_Input_Update(void)
{
    iw_xr_action_snapshot_t actions;
    int dominant = vr_dominant_hand.value != 0 ? 0 : 1;
    int offhand = dominant ^ 1;
    float turn;
    xr_owns_input = VID_XR_GetActions(&actions);
    if (!xr_owns_input)
    {
        XR_Input_Shutdown();
        return;
    }
    XR_Input_Key(14, K_RGRIP, (actions.hand[1].grip > 0.5f || (actions.hand[1].buttons & 2u)) != 0);
    XR_Interaction_Update(&actions);
    if ((actions.hand[dominant].buttons & 1u) && !xr_main_trigger_previous) VID_XR_Haptic(dominant, 0.30f, 0.025f);
    xr_main_trigger_previous = (actions.hand[dominant].buttons & 1u) != 0;
    if (XR_Interaction_ConsumesGameplay()) {
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
        return;
    }
    XR_Input_Key(0, K_RTRIGGER, (actions.hand[dominant].buttons & 1u) != 0);
    XR_Input_Key(1, K_LTRIGGER, (actions.hand[offhand].buttons & 1u) != 0);
    XR_Input_Key(13, K_LGRIP, (!vr_stabilize_mode.value || (actions.hand[dominant].grip <= 0.5f && (actions.hand[dominant].buttons & 2u) == 0)) && (actions.hand[offhand].grip > 0.5f || (actions.hand[offhand].buttons & 2u)));

    {
        qboolean menu_toggle = (actions.hand[0].buttons & 32u) != 0 || (actions.hand[1].buttons & 32u) != 0 ||
            ((actions.hand[dominant].buttons & (2u | 16u)) == (2u | 16u));
        XR_Input_Key(2, K_ABUTTON, (actions.hand[dominant].buttons & 8u) != 0);
        XR_Input_Key(3, K_BBUTTON, !menu_toggle && (actions.hand[dominant].buttons & 16u) != 0);
        XR_Input_Key(4, K_XBUTTON, (actions.hand[offhand].buttons & 8u) != 0);
        XR_Input_Key(5, K_YBUTTON, (actions.hand[offhand].buttons & 16u) != 0);
        XR_Input_Key(6, K_RTHUMB, (actions.hand[dominant].buttons & 4u) != 0);
        XR_Input_Key(7, K_LTHUMB, (actions.hand[offhand].buttons & 4u) != 0);
        XR_Input_Key(8, K_ESCAPE, menu_toggle);
    }

    if (key_dest == key_menu)
    {
        float x = actions.hand[offhand].stick[0];
        float y = actions.hand[offhand].stick[1];
        float ax = fabsf(x), ay = fabsf(y);
        qboolean horizontal = ax > 0.25f && ax >= ay + 0.15f;
        qboolean vertical = ay > 0.25f && ay >= ax + 0.15f;
        XR_Input_MenuKey(9, K_LEFTARROW, horizontal && x < 0.0f);
        XR_Input_MenuKey(10, K_RIGHTARROW, horizontal && x > 0.0f);
        XR_Input_MenuKey(11, K_DOWNARROW, vertical && y < 0.0f);
        XR_Input_MenuKey(12, K_UPARROW, vertical && y > 0.0f);
        xr_turn_held = false;
        return;
    }
    XR_Input_Key(9, K_LEFTARROW, false);
    XR_Input_Key(10, K_RIGHTARROW, false);
    XR_Input_Key(11, K_DOWNARROW, false);
    XR_Input_Key(12, K_UPARROW, false);

    turn = actions.hand[dominant].stick[0];
    if (fabsf(turn) < q_max(vr_turn_deadzone.value, 0.20f) || fabsf(turn) < fabsf(actions.hand[dominant].stick[1]) + 0.20f)
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

qboolean XR_Input_Move(usercmd_t *cmd)
{
    iw_xr_action_snapshot_t actions;
    int offhand = vr_dominant_hand.value != 0 ? 1 : 0;
    float x, y, length, deadzone;
    if (!cmd || !xr_owns_input || !VID_XR_GetActions(&actions))
        return false;
    x = actions.hand[offhand].stick[0];
    y = actions.hand[offhand].stick[1];
    length = sqrtf(x * x + y * y);
    deadzone = CLAMP(0.0f, vr_move_deadzone.value, 0.95f);
    if (length > deadzone)
    {
        float scale = (length - deadzone) / (1.0f - deadzone);
        scale /= length;
        cmd->sidemove += cl_sidespeed.value * x * scale * CLAMP(0.f, vr_player_speed.value, 300.f) * 0.01f;
        cmd->forwardmove += cl_forwardspeed.value * y * scale * CLAMP(0.f, vr_player_speed.value, 300.f) * 0.01f;
    }
    return true;
}
