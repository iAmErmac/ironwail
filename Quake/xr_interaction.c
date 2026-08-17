#include "quakedef.h"
#include "draw.h"
#include "keys.h"
#include "menu.h"
#include "xr_interaction.h"
#include "json.h"

extern cvar_t vr_dominant_hand;
extern cvar_t ui_mouse;
extern cvar_t host_timescale;
extern double host_frametime;
extern cvar_t vr_world_scale;
extern cvar_t vr_curved_screen;
extern cvar_t vr_curve_radius;
extern qboolean R_GetXRControllerAim(vec3_t forward, vec3_t right, vec3_t up);
extern qboolean R_GetXRControllerOrigin(vec3_t origin);
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
static cvar_t vr_comfort_vignette = {"vr_comfort_vignette", "0", CVAR_ARCHIVE};

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

static qboolean wheel_active, wheel_bind_active, keyboard_active, keyboard_trigger, virtual_mouse_trigger, previous_main_grip, previous_offhand_grip;
static int wheel_selection = -1, keyboard_mode, wheel_hand;
static float keyboard_x = 0.5f, keyboard_y = 0.5f, virtual_mouse_x = 0.5f, virtual_mouse_y = 0.5f, vignette_value;
static float virtual_mouse_last_x, virtual_mouse_last_y;
static qboolean virtual_mouse_position_valid;
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
static int xr_dominant(void) { return vr_dominant_hand.value != 0.f ? 0 : 1; }
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
    if (!R_GetXRControllerOrigin(wheel_origin) || !R_GetXRControllerAim(wheel_forward, wheel_right, wheel_up)) return;
    wheel_active = true;
    wheel_hand = xr_dominant();
    VID_XR_Haptic(wheel_hand, 0.35f, 0.03f);
    Con_Printf("XR weapon wheel opened on %s hand\\n", xr_dominant() == 0 ? "left" : "right");
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
    if (!R_GetXRControllerAim(forward, right, up) || scale <= 0.01f) return;
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
static void xr_keyboard_press(void)
{
    static const char *letters[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-."};
    static const char symbols[] = "!@#$%^&*()[]{}<>?/\\|`~_=+:;\"',";
    int row, col, ch;
    if (!keyboard_active || !xr_keyboard_key_at(keyboard_x, keyboard_y, &row, &col)) return;
    if (row == 4) {
        if (col < 2) { keyboard_mode = keyboard_mode == 1 ? 0 : 1; return; }
        if (col == 2) { keyboard_mode = keyboard_mode == 2 ? 0 : 2; return; }
        if (col > 7) { xr_keyboard_send_special(K_ENTER); return; }
        Char_Event(' '); return;
    }
    if (row == 2 && col == 9) { xr_keyboard_send_special(K_BACKSPACE); return; }
    ch = letters[row][col];
    if (keyboard_mode == 1 && row > 0) ch = toupper(ch);
    else if (keyboard_mode == 2 && row > 0) ch = symbols[(row - 1) * 10 + col];
    Char_Event(ch);
    if (keyboard_mode == 1) keyboard_mode = 0;
}
static void xr_keyboard_rotate(const float q[4], const float input[3], float output[3])
{
    float qv[3] = {q[0], q[1], q[2]}, cross[3], twice_cross[3];
    cross[0] = 2.f * (qv[1] * input[2] - qv[2] * input[1]); cross[1] = 2.f * (qv[2] * input[0] - qv[0] * input[2]); cross[2] = 2.f * (qv[0] * input[1] - qv[1] * input[0]);
    twice_cross[0] = qv[1] * cross[2] - qv[2] * cross[1]; twice_cross[1] = qv[2] * cross[0] - qv[0] * cross[2]; twice_cross[2] = qv[0] * cross[1] - qv[1] * cross[0];
    output[0] = input[0] + q[3] * cross[0] + twice_cross[0]; output[1] = input[1] + q[3] * cross[1] + twice_cross[1]; output[2] = input[2] + q[3] * cross[2] + twice_cross[2];
}

static void xr_virtual_pointer_clear(void)
{
    pointer_active = false;
    VID_XR_SetVirtualPointer(NULL, NULL, false);
}

static void xr_virtual_pointer_update(const iw_xr_hand_snapshot_t *hand)
{
    float center[3], orientation[4], inverse[4], width, height;
    float direction_local[3] = {0.f, 0.f, -1.f}, direction[3], offset[3], local_origin[3], local_direction[3], local_hit[3];
    float distance, u, v;
    xr_virtual_pointer_clear();
    if (!hand || !hand->aim_valid || !VID_XR_GetVirtualScreen(center, orientation, &width, &height) || width <= 0.f || height <= 0.f) return;
    xr_keyboard_rotate(hand->aim_orientation, direction_local, direction);
    VectorSubtract(hand->aim_position, center, offset);
    inverse[0] = -orientation[0]; inverse[1] = -orientation[1]; inverse[2] = -orientation[2]; inverse[3] = orientation[3];
    xr_keyboard_rotate(inverse, offset, local_origin);
    xr_keyboard_rotate(inverse, direction, local_direction);
    if (vr_curved_screen.value != 0.f && vr_curve_radius.value > width * 0.5f) {
        float radius = vr_curve_radius.value, a, b, c, discriminant, t0, t1, t, angle, half_angle;
        a = local_direction[0] * local_direction[0] + local_direction[2] * local_direction[2];
        b = 2.f * (local_origin[0] * local_direction[0] + (local_origin[2] - radius) * local_direction[2]);
        c = local_origin[0] * local_origin[0] + (local_origin[2] - radius) * (local_origin[2] - radius) - radius * radius;
        discriminant = b * b - 4.f * a * c;
        if (a < 0.00001f || discriminant < 0.f) return;
        t0 = (-b - sqrtf(discriminant)) / (2.f * a); t1 = (-b + sqrtf(discriminant)) / (2.f * a);
        t = t0 > 0.f ? t0 : t1; if (t <= 0.f) return;
        VectorMA(local_origin, t, local_direction, local_hit);
        angle = asinf(CLAMP(-1.f, local_hit[0] / radius, 1.f)); half_angle = width * 0.5f / radius;
        if (fabsf(angle) > half_angle || fabsf(local_hit[1]) > height * 0.5f) return;
        u = 0.5f + angle / (2.f * half_angle); v = 0.5f - local_hit[1] / height;
    } else {
        if (fabsf(local_direction[2]) < 0.001f) return;
        distance = -local_origin[2] / local_direction[2]; if (distance <= 0.f) return;
        VectorMA(local_origin, distance, local_direction, local_hit);
        if (fabsf(local_hit[0]) > width * 0.5f || fabsf(local_hit[1]) > height * 0.5f) return;
        u = 0.5f + local_hit[0] / width; v = 0.5f - local_hit[1] / height;
    }
    keyboard_x = virtual_mouse_x = u; keyboard_y = virtual_mouse_y = v;
    xr_keyboard_rotate(orientation, local_hit, offset); VectorAdd(center, offset, pointer_hit_xr);
    VectorCopy(hand->aim_position, pointer_start_xr); pointer_active = true;
    VID_XR_SetVirtualPointer(pointer_start_xr, pointer_hit_xr, true);
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
    Cmd_AddCommand("weaponwheel_reload", xr_weaponwheel_reload_f);
    Cmd_AddCommand("+vr_weaponwheel", xr_weaponwheel_bind_down);
    Cmd_AddCommand("-vr_weaponwheel", xr_weaponwheel_bind_up);
    xr_weaponwheel_reload_f();
}

void XR_Interaction_Shutdown(void)
{
    xr_wheel_close(); xr_keyboard_close(); xr_virtual_pointer_clear(); vignette_value = 0.f;
}

void XR_Interaction_Update(const iw_xr_action_snapshot_t *actions)
{
    int dominant, offhand; float move; qboolean grip, main_grip, trigger, menu_combo, both_grips;
    if (!actions || !actions->active) { XR_Interaction_Shutdown(); return; }
    if (wheel_active && !xr_can_wheel()) xr_wheel_close();
    dominant = xr_dominant(); offhand = dominant ^ 1;
    main_grip = actions->hand[dominant].grip > 0.5f || (actions->hand[dominant].buttons & 2u) != 0;
    grip = wheel_bind_active;
    { qboolean offhand_grip = actions->hand[offhand].grip > 0.5f || (actions->hand[offhand].buttons & 2u) != 0;
      if (main_grip != previous_main_grip || offhand_grip != previous_offhand_grip) Con_Printf("XR grips: main=%d offhand=%d dominant=%s\\n", main_grip, offhand_grip, dominant == 0 ? "left" : "right");
      previous_main_grip = main_grip; previous_offhand_grip = offhand_grip; }
    both_grips = main_grip && (actions->hand[offhand].grip > 0.5f || (actions->hand[offhand].buttons & 2u) != 0);
    trigger = (actions->hand[1].buttons & 1u) != 0;
    menu_combo = main_grip && (actions->hand[dominant].buttons & 16u) != 0;
    if (keyboard_active && key_dest == key_game) xr_keyboard_close();
    if (virtual_mouse_trigger && key_dest != key_menu) {
        Key_Event(K_MOUSE1, false);
        virtual_mouse_trigger = false;
    }
    if (keyboard_active) {
        xr_virtual_pointer_update(&actions->hand[1]);
        if (trigger && !keyboard_trigger) xr_keyboard_press();
        keyboard_trigger = trigger;
        if (actions->hand[dominant].buttons & 16u) xr_keyboard_close();
    } else if (key_dest == key_menu) {
        qboolean moved;
        if (!ui_mouse.value) Cvar_SetValueQuick(&ui_mouse, 1.f);
        xr_virtual_pointer_update(&actions->hand[1]);
        moved = pointer_active && (!virtual_mouse_position_valid || fabsf(virtual_mouse_x - virtual_mouse_last_x) > 0.002f || fabsf(virtual_mouse_y - virtual_mouse_last_y) > 0.002f);
        if (moved) {
            M_Mousemove((int)(vid.width * virtual_mouse_x), (int)(vid.height * virtual_mouse_y));
            if (!virtual_mouse_position_valid) M_Mousemove((int)(vid.width * virtual_mouse_x), (int)(vid.height * virtual_mouse_y));            virtual_mouse_last_x = virtual_mouse_x;
            virtual_mouse_last_y = virtual_mouse_y;
            virtual_mouse_position_valid = true;
        }
        if (!pointer_active) virtual_mouse_position_valid = false;
        if (trigger && !virtual_mouse_trigger && pointer_active) Key_Event(K_MOUSE1, true);
        if ((!trigger || !pointer_active) && virtual_mouse_trigger) Key_Event(K_MOUSE1, false);
        virtual_mouse_trigger = trigger && pointer_active;
    } else if ((key_dest == key_console || key_dest == key_message) && Key_TextEntry() == TEXTMODE_ON) {
        keyboard_active = true; keyboard_mode = 0; keyboard_trigger = false; xr_wheel_close();
    } else if (wheel_active && !wheel_bind_active) {
        xr_wheel_cursor_from_pose();
        xr_wheel_select(wheel_cursor[0], wheel_cursor[1]);
        xr_wheel_commit();
    } else if (wheel_active && both_grips) {
        xr_wheel_close();
    } else if (wheel_active && menu_combo) {
        xr_wheel_close();
    } else if (wheel_active) {
        xr_wheel_cursor_from_pose();
        xr_wheel_select(wheel_cursor[0], wheel_cursor[1]);
        if (!grip) xr_wheel_commit();
    } else if (grip && !menu_combo && !both_grips) xr_wheel_open();
    move = sqrtf(actions->hand[offhand].stick[0] * actions->hand[offhand].stick[0] +
                 actions->hand[offhand].stick[1] * actions->hand[offhand].stick[1]);
    if (vr_comfort_vignette.value > 0.f && key_dest == key_game && !keyboard_active && !wheel_active && move > 0.2f)
        vignette_value += (CLAMP(0.f, vr_comfort_vignette.value, 1.f) - vignette_value) * (float)q_min(1.0, host_frametime * 8.0);
    else vignette_value -= vignette_value * (float)q_min(1.0, host_frametime * 8.0);
    vignette_value = CLAMP(0.f, vignette_value, 1.f);
}

qboolean XR_Interaction_ConsumesGameplay(void) { return wheel_active || keyboard_active; }
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
    int i;
    wheel_entity_count = 0;
    if (!wheel_active || !R_GetXRControllerOrigin(wheel_origin) || !R_GetXRControllerAim(forward, right, up)) return;
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
void XR_Interaction_Draw(void)
{
    int i;
    GL_SetCanvas(CANVAS_DEFAULT);
    {
        vec3_t xr_origin;
        if (wheel_active && !R_GetXRControllerOrigin(xr_origin)) {
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
    if (keyboard_active && key_dest == key_game) xr_keyboard_close();
    if (keyboard_active) {
        static const char *rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM-."};
        int left = 10, top = 120, width = 300, height = 108;
        int key_width = width / 10, key_height = height / 5, selected_row = -1, selected_col = -1;
        GL_SetCanvas(CANVAS_MENU);
        xr_keyboard_key_at(keyboard_x, keyboard_y, &selected_row, &selected_col);
        Draw_Fill(left - 6, top - 6, width + 12, height + 12, 0, 0.82f);
        for (i = 0; i < 5; ++i) {
            int col;
            for (col = 0; col < 10; ++col) {
                int x = left + col * key_width, y = top + i * key_height;
                const char *label = NULL;
                char key[2] = {' ', '\0'};
                if (i < 4 && col < (int)strlen(rows[i])) { key[0] = rows[i][col]; if (keyboard_mode == 1 && i > 0) key[0] = toupper(key[0]); label = key; }
                else if (i == 2 && col == 9) label = "BKSP";
                else if (i == 4 && col < 2) label = "SHIFT";
                else if (i == 4 && col == 2) label = "SYM";
                else if (i == 4 && col > 7) label = "ENTER";
                else if (i == 4 && col == 5) label = "SPACE";
                Draw_Fill(x + 1, y + 1, key_width - 2, key_height - 2, (i == selected_row && col == selected_col) ? 14 : 8, 0.85f);
                if (label) Draw_String(x + (key_width - (int)strlen(label) * 8) / 2, y + (key_height - 8) / 2, label);
            }
        }
    }
    if (vignette_value > 0.f && !wheel_active && !keyboard_active) {
        int edge_x = (int)(vid.guiwidth * vignette_value * 0.18f);
        int edge_y = (int)(vid.guiheight * vignette_value * 0.18f);
        Draw_Fill(0, 0, edge_x, vid.guiheight, 0, vignette_value);
        Draw_Fill(vid.guiwidth - edge_x, 0, edge_x, vid.guiheight, 0, vignette_value);
        Draw_Fill(edge_x, 0, vid.guiwidth - 2 * edge_x, edge_y, 0, vignette_value);
        Draw_Fill(edge_x, vid.guiheight - edge_y, vid.guiwidth - 2 * edge_x, edge_y, 0, vignette_value);
    }
}
}
