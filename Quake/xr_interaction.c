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
cvar_t vr_comfort_vignette = {"vr_comfort_vignette", "0", CVAR_ARCHIVE};
cvar_t vr_comfort_vignette_strength = {"vr_comfort_vignette_strength", "0.6", CVAR_ARCHIVE};
cvar_t vr_mouse = {"vr_mouse", "0", CVAR_ARCHIVE};
cvar_t vr_mouse_color = {"vr_mouse_color", "FFFFFF", CVAR_ARCHIVE};
cvar_t vr_mouse_alpha = {"vr_mouse_alpha", "0.4", CVAR_ARCHIVE};
cvar_t vr_aim_beam = {"vr_aim_beam", "1", CVAR_ARCHIVE};
cvar_t vr_aim_beam_width = {"vr_aim_beam_width", "2.0", CVAR_ARCHIVE};

typedef struct { int item; int impulse; const char *name; qmodel_t *model; } xr_weapon_slot_t;
static xr_weapon_slot_t xr_weapons[16] = {
    {IT_AXE, 1, "Axe"}, {IT_SHOTGUN, 2, "Shotgun"},
    {IT_SUPER_SHOTGUN, 3, "Super Shotgun"}, {IT_NAILGUN, 4, "Nailgun"},
    {IT_SUPER_NAILGUN, 5, "Super Nailgun"}, {IT_GRENADE_LAUNCHER, 6, "Grenade Launcher"},
    {IT_ROCKET_LAUNCHER, 7, "Rocket Launcher"}, {IT_LIGHTNING, 8, "Thunderbolt"}
};
static int xr_weapon_count = 8;
static const char *xr_builtin_model_paths[] = {
    "progs/v_axe.mdl", "progs/v_shot.mdl", "progs/v_shot2.mdl", "progs/v_nail.mdl",
    "progs/v_nail2.mdl", "progs/v_rock.mdl", "progs/v_rock2.mdl", "progs/v_light.mdl"
};
static char xr_weapon_names[16][64];
static char xr_weapon_models[16][MAX_QPATH];

static qboolean wheel_active, wheel_bind_active, keyboard_active, keyboard_trigger, keyboard_select, keyboard_caps, keyboard_trigger_suppressed, virtual_mouse_trigger;
static int wheel_selection = -1, keyboard_mode, wheel_hand, keyboard_row, keyboard_col, keyboard_nav_x, keyboard_nav_y, menu_scroll_direction;
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
static iw_xr_hand_t xr_mainhand(void) { return XR_Input_PhysicalHandForRole(XR_HAND_MAINHAND); }
static void xr_weaponwheel_bind_down(void) { wheel_bind_active = true; }
static void xr_weaponwheel_bind_up(void) { wheel_bind_active = false; }

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

static void xr_wheel_open(void)
{
    if (!xr_can_wheel() || wheel_active) return;
    xr_weaponwheel_reload_f();
    xr_weaponwheel_resolve_models();
    if (!R_GetXRMainHandWeaponPose(wheel_origin, NULL, NULL, NULL) || !R_GetXRMainHandWeaponPose(NULL, wheel_forward, wheel_right, wheel_up)) return;
    wheel_active = true;
    wheel_hand = xr_mainhand();
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
    if (!R_GetXRMainHandWeaponPose(NULL, forward, right, up) || scale <= 0.01f) return;
    wheel_cursor[0] = CLAMP(-1.f, DotProduct(forward, wheel_right) / scale, 1.f);
    wheel_cursor[1] = CLAMP(-1.f, DotProduct(forward, wheel_up) / scale, 1.f);
}

static void xr_wheel_select(float x, float y)
{
    float length = sqrtf(x * x + y * y), angle, best_delta = 1000.f;
    int i, best = -1;
    if (length < 0.25f) return;
    angle = atan2f(x, y);
    for (i = 0; i < (int)xr_weapon_count; ++i) {
        float slot_angle = (float)i * (float)(2.0 * M_PI / xr_weapon_count);
        float delta = fabsf(atan2f(sinf(angle - slot_angle), cosf(angle - slot_angle)));
        if ((cl.stats[STAT_ITEMS] & xr_weapons[i].item) && delta < best_delta) {
            best_delta = delta; best = i;
        }
    }
    if (best != wheel_selection && best >= 0) VID_XR_Haptic(wheel_hand, 0.6f, 0.05f);
    wheel_selection = best;
}

static void xr_wheel_commit(void)
{
    if (wheel_selection >= 0 && cl.stats[STAT_ACTIVEWEAPON] != xr_weapons[wheel_selection].item) {
        VID_XR_Haptic(wheel_hand, 0.8f, 0.08f);
        Cbuf_AddText(va("impulse %i\n", xr_weapons[wheel_selection].impulse));
    }
    xr_wheel_close();
}

static void xr_keyboard_close(void) { keyboard_active = false; keyboard_trigger = false; }

static void xr_keyboard_send_special(int key)
{
    if (key == K_ESCAPE) { xr_keyboard_close(); return; }
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

static qmodel_t *xr_weaponwheel_find_model(const char *name)
{
    int i;
    if (!name || !*name) return NULL;
    for (i = 1; i < MAX_MODELS && cl.model_precache[i]; ++i)
        if (!strcmp(cl.model_precache[i]->name, name)) return cl.model_precache[i];
    return Mod_ForName(name, false);
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
        q_strlcpy(xr_weapon_models[i], xr_builtin_model_paths[i], sizeof(xr_weapon_models[i]));
    }
    xr_weapon_count = 8;
}

static void xr_weaponwheel_resolve_models(void)
{
    int i;
    for (i = 0; i < xr_weapon_count; ++i)
        xr_weapons[i].model = xr_weaponwheel_find_model(xr_weapon_models[i]);
}

static void xr_weaponwheel_reload_f(void)
{
    byte *file;
    json_t *json;
    const jsonentry_t *section, *entry;
    int count = 0;

    xr_weaponwheel_set_builtin_slots();
    file = COM_LoadMallocFile("weaponwheel.json", NULL);
    if (!file) return;
    json = JSON_Parse((const char *)file);
    if (!json) { free(file); return; }
    section = JSON_Find(json->root, COM_SkipPath(com_gamedir), JSON_ARRAY);
    if (!section) section = JSON_Find(json->root, "default", JSON_ARRAY);
    if (section) {
        for (entry = section->firstchild; entry && count < 16; entry = entry->next) {
            const char *name = JSON_FindString(entry, "name");
            const char *item_name = JSON_FindString(entry, "item");
            const double *item_number = JSON_FindNumber(entry, "item");
            const double *impulse = JSON_FindNumber(entry, "impulse");
            const char *model = JSON_FindString(entry, "model");
            int item = item_name ? xr_item_bit(item_name) : (item_number ? (int)*item_number : 0);
            if (entry->type != JSON_OBJECT || !name || !impulse || !item) continue;
            xr_weapons[count].item = item;
            xr_weapons[count].impulse = (int)*impulse;
            q_strlcpy(xr_weapon_names[count], name, sizeof(xr_weapon_names[count]));
            xr_weapons[count].name = xr_weapon_names[count];
            xr_weapons[count].model = NULL;
            xr_weapon_models[count][0] = '\0';
            if (model && *model)
                q_strlcpy(xr_weapon_models[count], model, sizeof(xr_weapon_models[count]));
            ++count;
        }
    }
    if (count > 0) xr_weapon_count = count;
    JSON_Free(json);
    free(file);
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
    Cvar_RegisterVariable(&vr_weaponwheel);
    Cvar_RegisterVariable(&vr_weaponwheel_slowmo);
    Cvar_RegisterVariable(&vr_weaponwheel_distance);
    Cvar_RegisterVariable(&vr_weaponwheel_radius);
    Cvar_RegisterVariable(&vr_weaponwheel_modelsize);
    Cvar_RegisterVariable(&vr_weaponwheel_modelpitch);
    Cvar_RegisterVariable(&vr_weaponwheel_modelyaw);
    Cvar_RegisterVariable(&vr_weaponwheel_spin);
    Cvar_RegisterVariable(&vr_weaponwheel_deflection);
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

    xr_weaponwheel_reload_f();
}

void XR_Interaction_Shutdown(void)
{
    xr_wheel_close(); xr_keyboard_close(); xr_virtual_pointer_clear(); keyboard_trigger_suppressed = false; vignette_value = 0.f; vignette_yaw_valid = false;
}

void XR_Interaction_Update(const iw_xr_action_snapshot_t *actions)
{
    int dominant, offhand; float move, yaw_delta; qboolean grip, main_grip, trigger, menu_combo, both_grips;
    if (!actions || !actions->active) { XR_Interaction_Shutdown(); return; }
    if (key_dest != key_game) wheel_bind_active = false;
    if (wheel_active && !xr_can_wheel()) xr_wheel_close();
    dominant = XR_Input_PhysicalHandForRole(XR_HAND_MAINHAND); offhand = XR_Input_PhysicalHandForRole(XR_HAND_OFFHAND);
    main_grip = actions->hand[dominant].grip > 0.5f || (actions->hand[dominant].buttons & IW_XR_BUTTON_GRIP) != 0;
    grip = wheel_bind_active;
    both_grips = main_grip && (actions->hand[offhand].grip > 0.5f || (actions->hand[offhand].buttons & IW_XR_BUTTON_GRIP) != 0);
    trigger = (actions->hand[dominant].buttons & IW_XR_BUTTON_TRIGGER) != 0;
    if (!trigger) keyboard_trigger_suppressed = false;
    menu_combo = main_grip && (actions->hand[dominant].buttons & IW_XR_BUTTON_SECONDARY) != 0;
    if (keyboard_active && key_dest == key_game) xr_keyboard_close();
    if (virtual_mouse_trigger && (key_dest != key_menu || (!vr_mouse.value && actions->hand[dominant].grip <= 0.5f && !(actions->hand[dominant].buttons & IW_XR_BUTTON_GRIP))) ) {
        Key_Event(K_MOUSE1, false);
        virtual_mouse_trigger = false;
    }
    if (keyboard_active) {
        qboolean select = (actions->hand[dominant].buttons & IW_XR_BUTTON_PRIMARY) != 0;
        xr_virtual_pointer_update(&actions->hand[dominant]);
        if (pointer_active) xr_keyboard_key_at(keyboard_x, keyboard_y, &keyboard_row, &keyboard_col);
        xr_keyboard_navigate(&actions->hand[offhand]);
        if (trigger && !keyboard_trigger && pointer_active) {
            xr_keyboard_press();
            if (!keyboard_active) keyboard_trigger_suppressed = true;
        } else if (select && !keyboard_select) xr_keyboard_press();
        keyboard_trigger = trigger;
        keyboard_select = select;
        if (actions->hand[dominant].buttons & IW_XR_BUTTON_SECONDARY) xr_keyboard_close();
    } else if (key_dest == key_menu) {
        int scroll = actions->hand[dominant].stick[1] > 0.6f ? 1 : actions->hand[dominant].stick[1] < -0.6f ? -1 : 0;
        if (scroll && scroll != menu_scroll_direction)
            M_Keydown(scroll > 0 ? K_MWHEELUP : K_MWHEELDOWN, false);
        menu_scroll_direction = scroll;
        if (!ui_mouse.value) Cvar_SetValueQuick(&ui_mouse, 1.f);
        xr_virtual_pointer_update(&actions->hand[dominant]);
        if (pointer_active || virtual_mouse_trigger)
            M_MousemoveNormalized(CLAMP(0.f, virtual_mouse_x, 1.f), CLAMP(0.f, virtual_mouse_y, 1.f));
        if (trigger && !virtual_mouse_trigger && pointer_active) Key_Event(K_MOUSE1, true);
        if (!trigger && virtual_mouse_trigger) Key_Event(K_MOUSE1, false);
        virtual_mouse_trigger = trigger && (pointer_active || virtual_mouse_trigger);
    } else if ((key_dest == key_console || key_dest == key_message) && Key_TextEntry() == TEXTMODE_ON) {
        menu_scroll_direction = 0;
        keyboard_active = true; keyboard_mode = 0; keyboard_caps = false; keyboard_trigger = keyboard_select = false;
        keyboard_nav_x = keyboard_nav_y = 0; xr_keyboard_set_selection(0, 0); xr_wheel_close();
    } else if (wheel_active && !wheel_bind_active) {
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
        xr_wheel_cursor_from_pose();
        xr_wheel_select(wheel_cursor[0], wheel_cursor[1]);
        if (!grip) xr_wheel_commit();
    } else if (grip && !menu_combo && !both_grips) {
        menu_scroll_direction = 0;
        xr_wheel_open();
    } else {
        menu_scroll_direction = 0;
        xr_virtual_pointer_clear();
    }
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

qboolean XR_Interaction_ConsumesGameplay(void) { return wheel_active || keyboard_active || keyboard_trigger_suppressed; }
qboolean XR_Interaction_WheelActive(void) { return wheel_active; }
qboolean XR_Interaction_GetVirtualPointer(float start[3], float hit[3]) { if (!pointer_active) return false; if (start) VectorCopy(pointer_start_xr, start); if (hit) VectorCopy(pointer_hit_xr, hit); return true; }

static void xr_weaponwheel_place_model(entity_t *ent, const vec3_t target, const vec3_t angles)
{
    float matrix[16];
    vec3_t centre, offset, zero = {0, 0, 0};

    centre[0] = (ent->model->mins[0] + ent->model->maxs[0]) * 0.5f;
    centre[1] = (ent->model->mins[1] + ent->model->maxs[1]) * 0.5f;
    centre[2] = (ent->model->mins[2] + ent->model->maxs[2]) * 0.5f;
    R_EntityMatrix(matrix, zero, (vec_t *)angles, ent->scale);
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
    int i, teleport_rgb = 0;
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
    if (!wheel_active || !R_GetXRMainHandWeaponPose(wheel_origin, NULL, NULL, NULL) || !R_GetXRMainHandWeaponPose(NULL, forward, right, up)) return;
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
    for (i = 0; i < xr_weapon_count && wheel_entity_count < (int)Q_COUNTOF(wheel_entities); ++i) {
        entity_t *ent;
        float angle = (float)i * (2.f * (float)M_PI / xr_weapon_count);
        if (!(cl.stats[STAT_ITEMS] & xr_weapons[i].item) || !xr_weapons[i].model) continue;
        VectorMA(hub, sinf(angle) * radius, wheel_right, slot);
        VectorMA(slot, cosf(angle) * radius, wheel_up, slot);
        ent = &wheel_entities[wheel_entity_count++];
        memset(ent, 0, sizeof(*ent));
        ent->model = xr_weapons[i].model;
        ent->colormap = vid.colormap;
        VectorCopy(slot, ent->origin);
        biggest = q_max(ent->model->maxs[0] - ent->model->mins[0], ent->model->maxs[1] - ent->model->mins[1]);
        biggest = q_max(biggest, ent->model->maxs[2] - ent->model->mins[2]);
        ent->scale = ENTSCALE_ENCODE(biggest > 0.001f ? q_max(0.0625f, targetsize * (1.f + 0.6f * wheel_grow[i]) / biggest) : 1.f);
        xr_weaponwheel_place_model(ent, slot, angles);
        ent->wheel_brightness = 0.55f + 0.95f * wheel_grow[i];
        ent->alpha = ENTALPHA_DEFAULT;
    }
    for (i = 0; i < xr_weapon_count && wheel_entity_count < (int)Q_COUNTOF(wheel_entities); ++i) {
        if (cl.stats[STAT_ACTIVEWEAPON] != xr_weapons[i].item || !xr_weapons[i].model) continue;
        entity_t *ent = &wheel_entities[wheel_entity_count++];
        memset(ent, 0, sizeof(*ent));
        ent->model = xr_weapons[i].model;
        ent->colormap = vid.colormap;
        VectorCopy(hub, ent->origin);
        biggest = q_max(ent->model->maxs[0] - ent->model->mins[0], ent->model->maxs[1] - ent->model->mins[1]);
        biggest = q_max(biggest, ent->model->maxs[2] - ent->model->mins[2]);
        ent->scale = ENTSCALE_ENCODE(biggest > 0.001f ? q_max(0.0625f, targetsize * 0.7f / biggest) : 1.f);
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
        if (wheel_active && !R_GetXRMainHandWeaponPose(xr_origin, NULL, NULL, NULL)) {
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
