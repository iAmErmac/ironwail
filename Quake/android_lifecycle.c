#include "quakedef.h"
#include "android_lifecycle.h"
#include "cvar.h"
#include "xr_virtual_screen.h"
#include "xr_virtual_environment.h"
extern cvar_t vr_render_scale;
extern cvar_t vr_screen_scale;
extern cvar_t vr_screen_distance;
extern cvar_t vr_screen_follow;
extern cvar_t vr_curved_screen;
extern cvar_t vr_curve_radius;
extern cvar_t vr_screen_skybox;
extern cvar_t vr_hud_size;
extern cvar_t vr_hud_distance;
extern cvar_t vr_hud_yoffset;

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
static iw_xr_action_snapshot_t iw_xr_actions;
static unsigned iw_xr_target_fbo;
static int iw_xr_target_width;
static int iw_xr_target_height;
static iw_xr_frame_snapshot_t iw_xr_stereo_snapshot;
static unsigned iw_xr_stereo_fbos[2];
static int iw_xr_stereo_widths[2];
static int iw_xr_stereo_heights[2];
static qboolean iw_xr_stereo_active;
static qboolean iw_xr_stereo_rendered;
static qboolean iw_xr_multiview_requested = true;
static qboolean iw_xr_multiview_active;
static unsigned iw_xr_multiview_fbo;
static unsigned iw_xr_multiview_overlay_fbos[2];
static int iw_xr_multiview_width;
static int iw_xr_multiview_height;
static iw_xr_virtual_screen_follow_t iw_android_screen_follow;
static iw_xr_virtual_screen_pose_t iw_android_screen_pose;
static qboolean iw_android_screen_pose_valid;
static float iw_android_pointer_start[3], iw_android_pointer_hit[3];
static unsigned iw_android_pointer_color;
static float iw_android_pointer_alpha, iw_android_pointer_width;
static qboolean iw_android_pointer_active;
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
    Con_Printf("Android launch arguments:");
    for (i = 0; i < argc; ++i)
        Con_Printf(" %s", argv[i] ? argv[i] : "");
    Con_Printf("\n");
    IW_Android_QueueLaunchArgs(argc, argv);
    Cbuf_Execute();
#if defined(ANDROID_GLES3)
    if (!keybindings[K_LTRIGGER]) Key_SetBinding(K_LTRIGGER, "+jump");
    if (!keybindings[K_RTRIGGER]) Key_SetBinding(K_RTRIGGER, "+attack");
    if (!keybindings[K_RTHUMB]) Key_SetBinding(K_RTHUMB, "toggleconsole");
    if (!keybindings[K_BBUTTON]) Key_SetBinding(K_BBUTTON, "+jump");
    if (!keybindings[K_ABUTTON]) Key_SetBinding(K_ABUTTON, "+movedown");
    if (!keybindings[K_LGRIP]) Key_SetBinding(K_LGRIP, "+speed");
    if (!keybindings[K_RGRIP]) Key_SetBinding(K_RGRIP, "+vr_weaponwheel");
    if (!keybindings[K_YBUTTON]) Key_SetBinding(K_YBUTTON, "+showscores");
    if (!keybindings[K_XBUTTON]) Key_SetBinding(K_XBUTTON, "messagemode");
#endif
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
    IW_XRVirtualEnvironment_Invalidate();
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
    if (iw_xr_target_fbo)
        R_SetXRFinalTarget(iw_xr_target_fbo, iw_xr_target_width, iw_xr_target_height);
    Host_Frame(dt);
}

void IW_Android_FrameXR(uint64_t frame_time_ns, unsigned target_fbo, int target_width, int target_height)
{
    if (!iw_initialized || !iw_surface || !iw_context_ready || iw_paused || !iw_audio_focus)
        return;
    if (vid.width <= 0 || vid.height <= 0 || !target_fbo || target_width <= 0 || target_height <= 0)
        return;
    iw_xr_target_fbo = target_fbo;
    iw_xr_target_width = target_width;
    iw_xr_target_height = target_height;
    IW_Android_Frame(frame_time_ns);
    R_SetXRFinalTarget(0, 0, 0);
    iw_xr_target_fbo = 0;
    iw_xr_target_width = 0;
    iw_xr_target_height = 0;
}

void IW_Android_SetXRStereoFrame(const iw_xr_frame_snapshot_t *snapshot, const unsigned *fbos, const int *widths, const int *heights)
{
    if (!snapshot || snapshot->view_count < 2 || !fbos || !widths || !heights) return;
    iw_xr_stereo_snapshot = *snapshot;
    {
        iw_xr_virtual_screen_view_t views[2];
        float distance = 2.5f;
        qboolean follow = true;
        IW_Android_GetXRScreenGeometry(NULL, &distance, &follow);
        for (unsigned i = 0; i < 2; ++i) {
            memcpy(views[i].position, snapshot->views[i].position, sizeof(views[i].position));
            memcpy(views[i].orientation, snapshot->views[i].orientation, sizeof(views[i].orientation));
        }
        iw_android_screen_pose_valid = IW_XRVirtualScreen_UpdatePose(&iw_android_screen_follow, views, 2, Sys_DoubleTime(), distance, follow, &iw_android_screen_pose);
    }
    memcpy(iw_xr_stereo_fbos, fbos, sizeof(iw_xr_stereo_fbos));
    memcpy(iw_xr_stereo_widths, widths, sizeof(iw_xr_stereo_widths));
    memcpy(iw_xr_stereo_heights, heights, sizeof(iw_xr_stereo_heights));
    iw_xr_stereo_rendered = false;
    iw_xr_stereo_active = true;
}
void IW_Android_ClearXRStereoFrame(void)
{
    iw_xr_stereo_active = false;
    iw_xr_stereo_rendered = false;
    memset(&iw_xr_stereo_snapshot, 0, sizeof(iw_xr_stereo_snapshot));
    memset(iw_xr_stereo_fbos, 0, sizeof(iw_xr_stereo_fbos));
}
qboolean IW_Android_GetXRStereoFrame(const iw_xr_frame_snapshot_t **snapshot)
{
    if (snapshot) *snapshot = iw_xr_stereo_active ? &iw_xr_stereo_snapshot : NULL;
    if (iw_xr_stereo_active) iw_xr_stereo_rendered = true;
    return iw_xr_stereo_active;
}
qboolean IW_Android_GetXRHeadPosition(float position[3])
{
    if (!position || !iw_xr_stereo_active || iw_xr_stereo_snapshot.view_count < 2) return false;
    position[0] = 0.5f * (iw_xr_stereo_snapshot.views[0].position[0] + iw_xr_stereo_snapshot.views[1].position[0]);
    position[1] = 0.5f * (iw_xr_stereo_snapshot.views[0].position[1] + iw_xr_stereo_snapshot.views[1].position[1]);
    position[2] = 0.5f * (iw_xr_stereo_snapshot.views[0].position[2] + iw_xr_stereo_snapshot.views[1].position[2]);
    return true;
}
qboolean IW_Android_RaycastVirtualScreen(const float origin[3], const float orientation[4], iw_xr_virtual_screen_hit_t *hit)
{
    iw_xr_virtual_screen_t screen;
    float scale = 1.f, radius = 3.f;
    qboolean curved = false;
    if (!iw_android_screen_pose_valid) { if (hit) memset(hit, 0, sizeof(*hit)); return false; }
    memset(&screen, 0, sizeof(screen));
    memcpy(screen.position, iw_android_screen_pose.position, sizeof(screen.position));
    memcpy(screen.orientation, iw_android_screen_pose.orientation, sizeof(screen.orientation));
    IW_Android_GetXRScreenGeometry(&scale, NULL, NULL);
    IW_Android_GetXRScreenStyle(&curved, &radius);
    screen.width = 2.97f * scale;
    screen.height = 2.2275f * scale;
    screen.curved = curved && radius > 1.2f;
    screen.curve_radius = screen.curved ? radius : 0.f;
    return IW_XRVirtualScreen_Raycast(&screen, origin, orientation, hit);
}
qboolean IW_Android_GetXRScreenPose(float position[3], float orientation[4])
{
    if (!iw_android_screen_pose_valid) return false;
    if (position) memcpy(position, iw_android_screen_pose.position, sizeof(iw_android_screen_pose.position));
    if (orientation) memcpy(orientation, iw_android_screen_pose.orientation, sizeof(iw_android_screen_pose.orientation));
    return true;
}
void IW_Android_SetVirtualPointer(const float start[3], const float hit[3], qboolean active, unsigned color, float alpha, float width)
{
    if (start) memcpy(iw_android_pointer_start, start, sizeof(iw_android_pointer_start));
    if (hit) memcpy(iw_android_pointer_hit, hit, sizeof(iw_android_pointer_hit));
    if (active != iw_android_pointer_active) IW_LOG("virtual pointer active=%d color=%06x alpha=%.2f width=%.2f", active ? 1 : 0, color, alpha, width);
    iw_android_pointer_active = active;
    iw_android_pointer_color = color;
    iw_android_pointer_alpha = alpha;
    iw_android_pointer_width = width;
}
qboolean IW_Android_BeginXREye(unsigned eye, unsigned *fbo, int *width, int *height)
{
    if (!iw_xr_stereo_active || eye >= 2 || !iw_xr_stereo_fbos[eye]) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, iw_xr_stereo_fbos[eye]);
    glViewport(0, 0, iw_xr_stereo_widths[eye], iw_xr_stereo_heights[eye]);
    if (fbo) *fbo = iw_xr_stereo_fbos[eye];
    if (width) *width = iw_xr_stereo_widths[eye];
    if (height) *height = iw_xr_stereo_heights[eye];
    return true;
}
void IW_Android_EndXREye(unsigned eye)
{
    (void)eye;
}
void IW_Android_SetXRMultiviewRequested(qboolean requested)
{
    iw_xr_multiview_requested = requested;
}

qboolean IW_Android_XRMultiviewRequested(void)
{
    return iw_xr_multiview_requested;
}
qboolean IW_Android_XRGameplayStereoEligible(void)
{
    return iw_initialized && key_dest == key_game && !cl.paused && !con_forcedup && cl.intermission == 0 && !cls.demoplayback && !CL_InCutscene();
}

qboolean IW_Android_UsingXRMultiview(void)
{
    return iw_xr_stereo_active && iw_xr_multiview_active;
}

qboolean IW_Android_BeginXRMultiview(unsigned *fbo, int *width, int *height)
{
    if (!IW_Android_UsingXRMultiview() || !iw_xr_multiview_fbo)
        return false;
    glBindFramebuffer(GL_FRAMEBUFFER, iw_xr_multiview_fbo);
    glViewport(0, 0, iw_xr_multiview_width, iw_xr_multiview_height);
    if (fbo) *fbo = iw_xr_multiview_fbo;
    if (width) *width = iw_xr_multiview_width;
    if (height) *height = iw_xr_multiview_height;
    return true;
}

qboolean IW_Android_BeginXRMultiviewOverlayEye(unsigned eye, unsigned *fbo, int *width, int *height)
{
    if (!IW_Android_UsingXRMultiview() || eye >= 2 || !iw_xr_multiview_overlay_fbos[eye])
        return false;
    glBindFramebuffer(GL_FRAMEBUFFER, iw_xr_multiview_overlay_fbos[eye]);
    glViewport(0, 0, iw_xr_multiview_width, iw_xr_multiview_height);
    if (fbo) *fbo = iw_xr_multiview_overlay_fbos[eye];
    if (width) *width = iw_xr_multiview_width;
    if (height) *height = iw_xr_multiview_height;
    return true;
}
qboolean IW_Android_FrameXRStereoMultiview(uint64_t frame_time_ns, const iw_xr_frame_snapshot_t *snapshot, unsigned mono_fbo, int mono_width, int mono_height, unsigned layered_fbo, const unsigned *overlay_fbos, int layered_width, int layered_height)
{
    qboolean stereo_used;
    if (!iw_initialized || !iw_surface || !iw_context_ready || iw_paused || !iw_audio_focus || !snapshot || snapshot->view_count < 2 || !mono_fbo || !layered_fbo || !overlay_fbos || !overlay_fbos[0] || !overlay_fbos[1] || mono_width <= 0 || mono_height <= 0 || layered_width <= 0 || layered_height <= 0) {
        static qboolean reject_logged;
        if (!reject_logged) {
            IW_LOG("XR multiview frame rejected init=%d surface=%d context=%d paused=%d audio=%d snapshot=%d views=%u mono=%u layered=%u sizes=%dx%d/%dx%d", iw_initialized ? 1 : 0, iw_surface ? 1 : 0, iw_context_ready ? 1 : 0, iw_paused ? 1 : 0, iw_audio_focus ? 1 : 0, snapshot ? 1 : 0, snapshot ? snapshot->view_count : 0, mono_fbo, layered_fbo, mono_width, mono_height, layered_width, layered_height);
            reject_logged = true;
        }
        return false;
    }
    iw_xr_target_fbo = mono_fbo;
    iw_xr_target_width = mono_width;
    iw_xr_target_height = mono_height;
    iw_xr_stereo_snapshot = *snapshot;
    iw_xr_stereo_rendered = false;
    iw_xr_stereo_active = true;
    iw_xr_multiview_fbo = layered_fbo;
    iw_xr_multiview_overlay_fbos[0] = overlay_fbos ? overlay_fbos[0] : 0;
    iw_xr_multiview_overlay_fbos[1] = overlay_fbos ? overlay_fbos[1] : 0;
    iw_xr_multiview_width = layered_width;
    iw_xr_multiview_height = layered_height;
    iw_xr_multiview_active = IW_Android_XRMultiviewRequested();
    IW_Android_Frame(frame_time_ns);
    stereo_used = iw_xr_stereo_rendered && iw_xr_multiview_active;
    iw_xr_multiview_active = false;
    iw_xr_multiview_fbo = 0;
    iw_xr_multiview_overlay_fbos[0] = iw_xr_multiview_overlay_fbos[1] = 0;
    iw_xr_multiview_width = iw_xr_multiview_height = 0;
    IW_Android_ClearXRStereoFrame();
    R_SetXRFinalTarget(0, 0, 0);
    iw_xr_target_fbo = 0;
    iw_xr_target_width = iw_xr_target_height = 0;
    return stereo_used;
}
qboolean IW_Android_BeginXRHUD(unsigned *fbo, int *width, int *height)
{
    static qboolean logged;
    if (!iw_xr_stereo_active || !iw_xr_target_fbo || iw_xr_target_width <= 0 || iw_xr_target_height <= 0) return false;
    if (!logged) { IW_LOG("XR HUD target active fbo=%u size=%dx%d", iw_xr_target_fbo, iw_xr_target_width, iw_xr_target_height); logged = true; }
    if (fbo) *fbo = iw_xr_target_fbo;
    if (width) *width = iw_xr_target_width;
    if (height) *height = iw_xr_target_height;
    return true;
}

qboolean IW_Android_FrameXRStereo(uint64_t frame_time_ns, const iw_xr_frame_snapshot_t *snapshot, unsigned mono_fbo, int mono_width, int mono_height, const unsigned *eye_fbos, const int *eye_widths, const int *eye_heights)
{
    qboolean stereo_used;
    if (!iw_initialized || !iw_surface || !iw_context_ready || iw_paused || !iw_audio_focus || !snapshot || snapshot->view_count < 2 || !mono_fbo || mono_width <= 0 || mono_height <= 0) return false;
    iw_xr_target_fbo = mono_fbo;
    iw_xr_target_width = mono_width;
    iw_xr_target_height = mono_height;
    IW_Android_SetXRStereoFrame(snapshot, eye_fbos, eye_widths, eye_heights);
    IW_Android_Frame(frame_time_ns);
    stereo_used = iw_xr_stereo_rendered;
    IW_Android_ClearXRStereoFrame();
    R_SetXRFinalTarget(0, 0, 0);
    iw_xr_target_fbo = 0;
    iw_xr_target_width = 0;
    iw_xr_target_height = 0;
    return stereo_used;
}

float IW_Android_GetXRRenderScale(void) { return CLAMP(0.3f, vr_render_scale.value, 2.0f); }
void IW_Android_GetXRScreenGeometry(float *scale, float *distance, qboolean *follow)
{
    if (scale) *scale = q_max(0.25f, vr_screen_scale.value);
    if (distance) *distance = q_max(0.5f, vr_screen_distance.value);
    if (follow) *follow = vr_screen_follow.value != 0.0f;
}

void IW_Android_GetXRHUDGeometry(float *scale, float *distance, float *yoffset)
{
    if (scale) *scale = q_max(0.1f, vr_hud_size.value);
    if (distance) *distance = q_max(0.1f, vr_hud_distance.value);
    if (yoffset) *yoffset = vr_hud_yoffset.value;
}
qboolean IW_Android_GetVirtualPointer(float start[3], float hit[3], unsigned *color, float *alpha, float *width)
{
    if (!iw_android_pointer_active) return false;
    if (start) memcpy(start, iw_android_pointer_start, sizeof(iw_android_pointer_start));
    if (hit) memcpy(hit, iw_android_pointer_hit, sizeof(iw_android_pointer_hit));
    if (color) *color = iw_android_pointer_color;
    if (alpha) *alpha = iw_android_pointer_alpha;
    if (width) *width = iw_android_pointer_width;
    return true;
}
void IW_Android_GetXRScreenStyle(qboolean *curved, float *radius)
{
    if (curved) *curved = vr_curved_screen.value != 0.0f;
    if (radius) *radius = q_max(1.2f, vr_curve_radius.value);
}

qboolean IW_Android_GetXRBackdropScene(void) { return vr_screen_skybox.value != 0.f; }
void IW_Android_SetXRActions(const iw_xr_action_snapshot_t *actions) { if (actions) iw_xr_actions = *actions; else memset(&iw_xr_actions, 0, sizeof(iw_xr_actions)); }
qboolean IW_Android_GetXRActions(iw_xr_action_snapshot_t *actions) { if (!actions) return false; *actions = iw_xr_actions; return actions->active; }
extern void IW_Android_NativeHaptic(int hand, float amplitude, float duration_seconds);
void IW_Android_Haptic(int hand, float amplitude, float duration_seconds) { IW_Android_NativeHaptic(hand, amplitude, duration_seconds); }
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
void IW_Android_FrameXR(uint64_t frame_time_ns, unsigned target_fbo, int target_width, int target_height) { (void)frame_time_ns; (void)target_fbo; (void)target_width; (void)target_height; }
qboolean IW_Android_FrameXRStereo(uint64_t frame_time_ns, const iw_xr_frame_snapshot_t *snapshot, unsigned mono_fbo, int mono_width, int mono_height, const unsigned *eye_fbos, const int *eye_widths, const int *eye_heights) { (void)frame_time_ns; (void)snapshot; (void)mono_fbo; (void)mono_width; (void)mono_height; (void)eye_fbos; (void)eye_widths; (void)eye_heights; return false; }
qboolean IW_Android_FrameXRStereoMultiview(uint64_t frame_time_ns, const iw_xr_frame_snapshot_t *snapshot, unsigned mono_fbo, int mono_width, int mono_height, unsigned layered_fbo, const unsigned *overlay_fbos, int layered_width, int layered_height) { (void)frame_time_ns; (void)snapshot; (void)mono_fbo; (void)mono_width; (void)mono_height; (void)layered_fbo; (void)overlay_fbos; (void)layered_width; (void)layered_height; return false; }
void IW_Android_SetXRStereoFrame(const iw_xr_frame_snapshot_t *snapshot, const unsigned *fbos, const int *widths, const int *heights) { (void)snapshot; (void)fbos; (void)widths; (void)heights; }
void IW_Android_ClearXRStereoFrame(void) {}
qboolean IW_Android_GetXRStereoFrame(const iw_xr_frame_snapshot_t **snapshot) { if (snapshot) *snapshot = NULL; return false; }
qboolean IW_Android_GetXRHeadPosition(float position[3]) { (void)position; return false; }
qboolean IW_Android_RaycastVirtualScreen(const float origin[3], const float orientation[4], iw_xr_virtual_screen_hit_t *hit) { (void)origin; (void)orientation; if (hit) memset(hit, 0, sizeof(*hit)); return false; }
qboolean IW_Android_GetXRScreenPose(float position[3], float orientation[4]) { (void)position; (void)orientation; return false; }
void IW_Android_SetVirtualPointer(const float start[3], const float hit[3], qboolean active, unsigned color, float alpha, float width)
{
    (void)start; (void)hit; (void)active; (void)color; (void)alpha; (void)width;
}
qboolean IW_Android_BeginXREye(unsigned eye, unsigned *fbo, int *width, int *height) { (void)eye; if (fbo) *fbo = 0; if (width) *width = 0; if (height) *height = 0; return false; }
void IW_Android_EndXREye(unsigned eye) { (void)eye; }
qboolean IW_Android_BeginXRHUD(unsigned *fbo, int *width, int *height) { (void)fbo; (void)width; (void)height; return false; }
void IW_Android_SetXRMultiviewRequested(qboolean requested) { (void)requested; }
qboolean IW_Android_XRMultiviewRequested(void) { return false; }
qboolean IW_Android_XRGameplayStereoEligible(void) { return false; }
qboolean IW_Android_UsingXRMultiview(void) { return false; }
qboolean IW_Android_BeginXRMultiview(unsigned *fbo, int *width, int *height) { if (fbo) *fbo = 0; if (width) *width = 0; if (height) *height = 0; return false; }
qboolean IW_Android_BeginXRMultiviewOverlayEye(unsigned eye, unsigned *fbo, int *width, int *height) { (void)eye; if (fbo) *fbo = 0; if (width) *width = 0; if (height) *height = 0; return false; }
#endif
