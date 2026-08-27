#include "quakedef.h"
#include "android_lifecycle.h"

#include "android_gles.h"

#if defined(ANDROID_GLES3)
#include <android/log.h>
#include <android/keycodes.h>
#include <math.h>
#define IW_TAG "Ironwail"
#define IW_LOG(...) __android_log_print(ANDROID_LOG_INFO, IW_TAG, __VA_ARGS__)
#define IW_ERR(...) __android_log_print(ANDROID_LOG_ERROR, IW_TAG, __VA_ARGS__)

#define IW_ANDROID_MEMORY (384 * 1024 * 1024)

static quakeparms_t iw_parms;
static void *iw_memory;
static qboolean iw_initialized;
static qboolean iw_surface;
static qboolean iw_context_ready;
static qboolean iw_paused;
static qboolean iw_audio_focus = true;
static qboolean iw_touch_active;
static qboolean iw_attack_as_mouse;
static qboolean iw_attack_active;
extern kbutton_t in_attack, in_jump, in_down;
static uint64_t iw_last_frame_ns;
static cvar_t iw_android_defaults_version = {"iw_android_defaults_version", "0", CVAR_ARCHIVE};
#define IW_ANDROID_LOOK_SCALE 3.5f
static int IW_Android_ScaledLook(int delta) { return (int)(delta * IW_ANDROID_LOOK_SCALE); }


static void IW_Android_QueueLaunchArgs(int argc, const char *const *argv)
{
    int i;
    qboolean active = false;

    for (i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        if (!arg || !*arg)
            continue;
        if (arg[0] == '+')
        {
            if (active)
                Cbuf_AddText("\n");
            Cbuf_AddText(arg + 1);
            Cbuf_AddText(" ");
            active = true;
        }
        else if (arg[0] == '-')
        {
            active = false;
        }
        else if (active)
        {
            Cbuf_AddText(arg);
            Cbuf_AddText(" ");
        }
    }
    if (active)
        Cbuf_AddText("\n");
}

qboolean IW_Android_Init(const char *base_dir, int argc, const char *const *argv)
{
    iw_gles_limits_t limits;
    iw_gles_features_t features;
    int i;

    if (iw_initialized)
        return true;
    if (!IW_GLES_Probe(&limits, &features))
    {
        IW_ERR("Android initialization requires a current GLES 3.1 context");
        return false;
    }

    memset(&iw_parms, 0, sizeof(iw_parms));
    iw_parms.argc = argc;
    iw_parms.argv = (char **)argv;
    iw_parms.basedir = (char *)(base_dir && *base_dir ? base_dir : ".");
    iw_parms.memsize = IW_ANDROID_MEMORY;
    for (i = 0; i < argc; ++i)
        if (argv[i] && !strcmp(argv[i], "-heapsize") && i + 1 < argc)
            iw_parms.memsize = Q_atoi(argv[++i]) * 1024;

    host_parms = &iw_parms;
    iw_parms.errstate = 0;
    COM_InitArgv(iw_parms.argc, iw_parms.argv);
    isDedicated = false;
    Sys_Init();

    iw_memory = malloc(iw_parms.memsize);
    if (!iw_memory)
    {
        IW_ERR("unable to allocate %d bytes for engine heap", iw_parms.memsize);
        return false;
    }
    iw_parms.membase = iw_memory;

    IW_LOG("initializing native Android lifecycle; tier=%s data=%s",
           IW_GLES_FeatureTier(), iw_parms.basedir);
    Host_Init();
#if defined(ANDROID_GLES3)
    Cvar_RegisterVariable(&iw_android_defaults_version);
    Cvar_SetValueQuick(&scr_conscale, 4.0f);
    Cvar_SetValueQuick(&scr_menuscale, 4.0f);
    Cvar_SetValueQuick(&scr_sbarscale, 4.0f);
    Cvar_SetValueQuick(&scr_crosshairscale, 4.0f);
    IW_LOG("Android defaults: scr_conscale=4 scr_menuscale=4 scr_sbarscale=4 scr_crosshairscale=4 desktop cvars preserved");
#endif
    Con_DPrintf("Android launch arguments:");
    for (i = 0; i < argc; ++i)
        Con_DPrintf(" %s", argv[i] ? argv[i] : "");
    Con_DPrintf("\n");
    IW_Android_QueueLaunchArgs(argc, argv);
    Cbuf_Execute();
#if defined(ANDROID_GLES3)
    if (iw_android_defaults_version.value < 1.0f)
    {
        if (vid_gamma.value == 1.0f && vid_contrast.value == 1.0f)
        {
            Cvar_SetValueQuick(&vid_gamma, 0.95f);
            Cvar_SetValueQuick(&vid_contrast, 1.2f);
        }
        Cvar_SetValueQuick(&iw_android_defaults_version, 1.0f);
    }
    Cvar_SetValueQuick(&scr_conscale, 4.0f);
    Cvar_SetValueQuick(&scr_menuscale, 4.0f);
    Cvar_SetValueQuick(&scr_sbarscale, 4.0f);
    Cvar_SetValueQuick(&scr_crosshairscale, 4.0f);
#endif
    iw_initialized = true;
    return true;
}

void IW_Android_SurfaceCreated(void)
{
    iw_surface = true;
    iw_context_ready = true;
    iw_last_frame_ns = 0;
    IW_LOG("surface created");
}

void IW_Android_ContextRestored(void)
{
    GL_RestoreContextResources();
    iw_surface = true;
    iw_context_ready = true;
    iw_last_frame_ns = 0;
    IW_LOG("GLES context restored");
}

void IW_Android_SurfaceDestroyed(void)
{
    iw_surface = false;
    iw_context_ready = false;
    iw_last_frame_ns = 0;
    Key_ClearStates();
    IN_ClearStates();
    iw_touch_active = false;
    IW_Android_ClearActions();
    IW_LOG("surface destroyed; simulation suspended");
}

void IW_Android_Resize(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    vid.width = width;
    vid.height = height;
    vid.resized = true;
    vid.recalc_refdef = true;
    VID_RecalcConsoleSize();
    IW_LOG("surface resize %dx%d", width, height);
}

void IW_Android_Frame(uint64_t frame_time_ns)
{
    double dt;
    if (!iw_initialized || !iw_surface || !iw_context_ready || iw_paused || !iw_audio_focus)
        return;
    if (vid.width <= 0 || vid.height <= 0)
        return;
    if (!iw_last_frame_ns)
        dt = 1.0 / 60.0;
    else
        dt = (double)(frame_time_ns - iw_last_frame_ns) * 1e-9;
    iw_last_frame_ns = frame_time_ns;
    dt = CLAMP(0.0001, dt, 0.1);
#if defined(ANDROID_GLES3)
    if (iw_attack_active &&
        (cls.state != ca_connected || cls.signon != SIGNONS ||
         (iw_attack_as_mouse ? key_dest != key_menu : key_dest != key_game)))
        IW_Android_ClearActions();
#endif
    Host_Frame(dt);
}

void IW_Android_Key(int android_keycode, qboolean down)
{
    int key = -1;

    if (android_keycode >= AKEYCODE_A && android_keycode <= AKEYCODE_Z)
        key = 'a' + android_keycode - AKEYCODE_A;
    else if (android_keycode >= AKEYCODE_0 && android_keycode <= AKEYCODE_9)
        key = '0' + android_keycode - AKEYCODE_0;
    else
    {
        switch (android_keycode)
        {
        case AKEYCODE_DPAD_UP: key = K_UPARROW; break;
        case AKEYCODE_DPAD_DOWN: key = K_DOWNARROW; break;
        case AKEYCODE_DPAD_LEFT: key = K_LEFTARROW; break;
        case AKEYCODE_DPAD_RIGHT: key = K_RIGHTARROW; break;
        case AKEYCODE_DPAD_CENTER:
        case AKEYCODE_ENTER: key = K_ENTER; break;
        case AKEYCODE_TAB: key = K_TAB; break;
        case AKEYCODE_SPACE: key = K_SPACE; break;
        case AKEYCODE_DEL: key = K_BACKSPACE; break;
        case AKEYCODE_ESCAPE:
        case AKEYCODE_BACK: key = K_ESCAPE; break;
        case AKEYCODE_SHIFT_LEFT:
        case AKEYCODE_SHIFT_RIGHT: key = K_SHIFT; break;
        case AKEYCODE_CTRL_LEFT:
        case AKEYCODE_CTRL_RIGHT: key = K_CTRL; break;
        case AKEYCODE_ALT_LEFT:
        case AKEYCODE_ALT_RIGHT: key = K_ALT; break;
        case AKEYCODE_GRAVE: key = 96; break;
        case AKEYCODE_F12: key = K_F12; break;
        default: break;
        }
    }

    if (key >= 0)
        Key_EventWithKeycode(key, down, android_keycode);
    else if (down)
        IW_LOG("unmapped Android key code=%d", android_keycode);
}

void IW_Android_Text(const char *text)
{
    if (!text) return;
    while (*text) Char_Event((unsigned char)*text++);
}
void IW_Android_Axis(int device_id, int axis, float value)
{
    const float deadzone = 0.35f;
    (void)device_id;

    if (axis == 0 || axis == 1)
    {
        qboolean negative_down = value < -deadzone;
        qboolean positive_down = value > deadzone;
        if (axis == 0)
        {
            Key_Event(K_ALT, negative_down || positive_down);
            Key_Event(K_LEFTARROW, negative_down);
            Key_Event(K_RIGHTARROW, positive_down);
        }
        else
        {
            Key_Event(K_UPARROW, negative_down);
            Key_Event(K_DOWNARROW, positive_down);
        }
    }
    else if (axis == 11 || axis == 14)
    {
        int delta = (int)(value * 12.0f);
        if (axis == 11)
            IN_MouseMotion(delta, 0);
        else
            IN_MouseMotion(0, delta);
    }
}
static float iw_touch_x;
static float iw_touch_y;

void IW_Android_Touch(int action, float x, float y)
{
    if (action == 0)
    {
        iw_touch_active = true;
        iw_touch_x = x;
        iw_touch_y = y;
    }
    else if (action == 2 && iw_touch_active)
    {
        IN_MouseMotion(IW_Android_ScaledLook((int)(x - iw_touch_x)), IW_Android_ScaledLook((int)(y - iw_touch_y)));
        iw_touch_x = x;
        iw_touch_y = y;
    }
    else if ((action == 1 || action == 3) && iw_touch_active)
    {
        iw_touch_active = false;
    }
}

void IW_Android_TouchPointer(int action, int pointer_id, float x, float y) { (void)pointer_id; IW_Android_Touch(action, x, y); }
void IW_Android_Command(const char *command)
{
    if (command && *command) { Cbuf_AddText(command); Cbuf_AddText("\n"); }
}
static void IW_Android_ButtonDown(kbutton_t *button)
{
    button->state |= 1 + 2;
}

static void IW_Android_ButtonUp(kbutton_t *button)
{
    button->down[0] = button->down[1] = 0;
    button->state = 4;
}

void IW_Android_ClearActions(void)
{
    if (iw_attack_active)
    {
        if (iw_attack_as_mouse) Key_Event(K_MOUSE1, false);
        else IW_Android_ButtonUp(&in_attack);
    }
    iw_attack_as_mouse = false;
    iw_attack_active = false;
}

void IW_Android_Action(int action, qboolean down)
{
    switch (action)
    {
#if defined(ANDROID_GLES3)
    case 0:
        if (down)
        {
            IW_Android_ClearActions();
            iw_attack_as_mouse = key_dest == key_menu;
            iw_attack_active = true;
            if (iw_attack_as_mouse) Key_Event(K_MOUSE1, true);
            else IW_Android_ButtonDown(&in_attack);
        }
        else
            IW_Android_ClearActions();
        break;
#else
    case 0: down ? IN_AttackDown() : IN_AttackUp(); break;
#endif
    case 1: down ? IN_UseDown() : IN_UseUp(); break;
#if defined(ANDROID_GLES3)
    case 2: down ? IW_Android_ButtonDown(&in_jump) : IW_Android_ButtonUp(&in_jump); break;
    case 3: down ? IW_Android_ButtonDown(&in_down) : IW_Android_ButtonUp(&in_down); break;
#else
    case 2: down ? IN_JumpDown() : IN_JumpUp(); break;
    case 3: down ? IN_DownDown() : IN_DownUp(); break;
#endif
    case 4: if (down) Cvar_SetValue("cl_alwaysrun", cl_alwaysrun.value ? 0 : 1); break;
    default: break;
    }
}
void IW_Android_Look(int delta_x, int delta_y) { if (iw_initialized && iw_surface && !iw_paused) IN_MouseMotion(IW_Android_ScaledLook(delta_x), IW_Android_ScaledLook(delta_y)); }
int IW_Android_ScreenMode(void) { return key_dest == key_console ? 2 : (key_dest == key_game ? 0 : 1); }

void IW_Android_Pause(qboolean paused)
{
    iw_paused = paused;
    if (paused)
    {
        Key_ClearStates ();
        IN_ClearStates ();
        iw_touch_active = false;
        IW_Android_ClearActions();
    }
    else if (iw_audio_focus)
        S_UnblockSound();
    IW_LOG("lifecycle %s", paused ? "paused" : "resumed");
}

void IW_Android_AudioFocus(qboolean focused)
{
    iw_audio_focus = focused;
    if (!focused)
    {
        S_BlockSound();
        Key_ClearStates();
        IN_ClearStates();
        iw_touch_active = false;
        IW_Android_ClearActions();
    }
    else if (!iw_paused)
        S_UnblockSound();
    IW_LOG("audio focus %s", focused ? "gained" : "lost");
}

void IW_Android_Shutdown(void)
{
    if (!iw_initialized)
        return;
    Host_Shutdown();
    free(iw_memory);
    iw_memory = NULL;
    iw_initialized = false;
    iw_surface = false;
    iw_context_ready = false;
    IW_LOG("shutdown complete");
}


#else
qboolean IW_Android_Init(const char *base_dir, int argc, const char *const *argv) { (void)base_dir; (void)argc; (void)argv; return false; }
void IW_Android_SurfaceCreated(void) {}
void IW_Android_SurfaceDestroyed(void) {}
void IW_Android_ContextRestored(void) {}
void IW_Android_Resize(int width, int height) { (void)width; (void)height; }
void IW_Android_Frame(uint64_t frame_time_ns) { (void)frame_time_ns; }
void IW_Android_Key(int android_keycode, qboolean down) { (void)android_keycode; (void)down; }
void IW_Android_Text(const char *text) { (void)text; }
void IW_Android_Axis(int device_id, int axis, float value) { (void)device_id; (void)axis; (void)value; }
void IW_Android_Touch(int action, float x, float y) { (void)action; (void)x; (void)y; }
void IW_Android_TouchPointer(int action, int pointer_id, float x, float y) { (void)action; (void)pointer_id; (void)x; (void)y; }
void IW_Android_Command(const char *command) { (void)command; }
void IW_Android_Action(int action, qboolean down) { (void)action; (void)down; }
void IW_Android_ClearActions(void) {}
void IW_Android_Look(int delta_x, int delta_y) { (void)delta_x; (void)delta_y; }
int IW_Android_ScreenMode(void) { return 3; }
void IW_Android_Pause(qboolean paused) { (void)paused; }
void IW_Android_AudioFocus(qboolean focused) { (void)focused; }
void IW_Android_Shutdown(void) {}

#endif
