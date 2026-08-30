#include "quakedef.h"
#include "xr_desktop.h"
#include "xr_virtual_screen.h"
#include "xr_virtual_environment.h"
#include "xr_math.h"
#include "xr_action_schema.h"
extern cvar_t vr_render_scale;
extern cvar_t vr_screen_skybox;
extern cvar_t vr_screen_follow;

#if defined(IW_ENABLE_OPENXR)

#include "glquake.h"
#ifndef XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME
#define XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME "XR_FB_display_refresh_rate"
typedef XrResult (XRAPI_PTR *PFN_xrEnumerateDisplayRefreshRatesFB)(XrSession session, uint32_t displayRefreshRateCapacityInput, uint32_t *displayRefreshRateCountOutput, float *displayRefreshRates);
typedef XrResult (XRAPI_PTR *PFN_xrGetDisplayRefreshRateFB)(XrSession session, float *displayRefreshRate);
typedef XrResult (XRAPI_PTR *PFN_xrRequestDisplayRefreshRateFB)(XrSession session, float displayRefreshRate);
#endif
#include <SDL_syswm.h>
#include <windows.h>
typedef struct {
    XrSwapchain swapchain;
    XrSwapchainImageOpenGLKHR *images;
    GLuint *fbos;
    GLuint *depth_textures;
    GLuint mirror_fbo;
    uint32_t image_count;
    uint32_t image_index;
    uint32_t width;
    uint32_t height;
    qboolean acquired;
} iw_xr_gl_target_t;

struct iw_xr_win_s {
    void *window;
    void (*log)(void *, const char *);
    void *userdata;
    HMODULE loader;
    XrInstance instance;
    XrSystemId system_id;
    XrSession session;
    XrSpace space;
    XrPosef screen_pose;
    XrPosef hud_pose;
    qboolean recenter_requested;
    iw_xr_virtual_screen_follow_t screen_follow;
XrSwapchain swapchain;
    XrSwapchainImageOpenGLKHR *images;
    GLuint *fbos;
    GLuint *depth_textures;
    GLuint mirror_fbo;
    uint32_t image_count;
    uint32_t image_index;
    uint32_t width;
    uint32_t height;
    qboolean swapchain_is_srgb;
    XrFrameState frame_state;
    XrSessionState session_state;
    XrViewConfigurationType view_config_type;
    qboolean session_running;
    qboolean frame_running;
    qboolean image_acquired;
    qboolean failed;
    qboolean logged_wait_failure;
    qboolean cylinder_supported;
    qboolean refresh_rate_supported;
    float requested_refresh_rate;
    float applied_refresh_rate;
    qboolean curved_screen;
    float curve_radius;
    float screen_scale;
    float screen_distance;
    float hud_scale;
    float hud_distance;
    float hud_yoffset;
    qboolean curve_submission_logged;
    iw_xr_gl_target_t eyes[2];
    iw_xr_gl_target_t multiview;
    qboolean multiview_capable;
    qboolean multiview_requested;
    qboolean multiview_active;
    void (APIENTRYP framebuffer_texture_multiview_ovr)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei);
    int64_t swapchain_format;
    iw_xr_gl_target_t pointer_target;
    qboolean pointer_active;
    float pointer_start[3];
    float pointer_hit[3];
    unsigned pointer_color;
    float pointer_alpha;
    float pointer_width;
    qboolean stereo_submission;
    qboolean views_valid;
    iw_xr_frame_snapshot_t frame_snapshot;
    iw_xr_action_snapshot_t actions;
    XrActionSet action_set;
    XrPath hand_paths[IW_XR_HAND_COUNT];
    XrAction aim_action;
    XrAction grip_pose_action;
    XrAction trigger_action;
    XrAction trigger_click_action;
    XrAction grip_action;
    XrAction grip_click_action;
    XrAction stick_action;
    XrAction trackpad_action;
    XrAction stick_click_action;
    XrAction primary_action;
    XrAction secondary_action;
    XrAction menu_action;
    XrAction haptic_action;
    XrSpace aim_spaces[IW_XR_HAND_COUNT];
    XrSpace grip_spaces[IW_XR_HAND_COUNT];

#define XR_WIN_PROC(type, name) type name;
    PFN_xrGetInstanceProcAddr get_instance_proc_addr;
    PFN_xrEnumerateInstanceExtensionProperties enumerate_instance_extension_properties;
    PFN_xrCreateInstance create_instance;
#define XR_WIN_INSTANCE_PROC(type, name) type name;
    PFN_xrDestroyInstance destroy_instance;
    PFN_xrPollEvent poll_event;
    PFN_xrGetSystem get_system;
    PFN_xrGetSystemProperties get_system_properties;
    PFN_xrEnumerateViewConfigurations enumerate_view_configurations;
    PFN_xrEnumerateViewConfigurationViews enumerate_view_configuration_views;
    PFN_xrCreateSession create_session;
    PFN_xrDestroySession destroy_session;
    PFN_xrBeginSession begin_session;
    PFN_xrEndSession end_session;
    PFN_xrCreateReferenceSpace create_reference_space;
    PFN_xrDestroySpace destroy_space;
    PFN_xrLocateViews locate_views;
    PFN_xrEnumerateSwapchainFormats enumerate_swapchain_formats;
    PFN_xrCreateSwapchain create_swapchain;
    PFN_xrDestroySwapchain destroy_swapchain;
    PFN_xrEnumerateSwapchainImages enumerate_swapchain_images;
    PFN_xrAcquireSwapchainImage acquire_swapchain_image;
    PFN_xrWaitSwapchainImage wait_swapchain_image;
    PFN_xrReleaseSwapchainImage release_swapchain_image;
    PFN_xrWaitFrame wait_frame;
    PFN_xrBeginFrame begin_frame;
    PFN_xrEndFrame end_frame;
    PFN_xrGetOpenGLGraphicsRequirementsKHR get_opengl_graphics_requirements;
    PFN_xrEnumerateDisplayRefreshRatesFB enumerate_display_refresh_rates;
    PFN_xrGetDisplayRefreshRateFB get_display_refresh_rate;
    PFN_xrRequestDisplayRefreshRateFB request_display_refresh_rate;
    PFN_xrStringToPath string_to_path;
    PFN_xrCreateActionSet create_action_set;
    PFN_xrDestroyActionSet destroy_action_set;
    PFN_xrCreateAction create_action;
    PFN_xrDestroyAction destroy_action;
    PFN_xrSuggestInteractionProfileBindings suggest_interaction_profile_bindings;
    PFN_xrAttachSessionActionSets attach_session_action_sets;
    PFN_xrSyncActions sync_actions;
    PFN_xrGetActionStateBoolean get_action_state_boolean;
    PFN_xrGetActionStateFloat get_action_state_float;
    PFN_xrGetActionStateVector2f get_action_state_vector2f;
    PFN_xrGetActionStatePose get_action_state_pose;
    PFN_xrCreateActionSpace create_action_space;
    PFN_xrLocateSpace locate_space;
    PFN_xrApplyHapticFeedback apply_haptic_feedback;
    PFN_xrStopHapticFeedback stop_haptic_feedback;
};

static void xr_apply_refresh_rate(iw_xr_win_t *xr);

static void xr_log(iw_xr_win_t *xr, const char *message)
{
    if (xr && xr->log)
        xr->log(xr->userdata, message);
}


static void xr_destroy_target(iw_xr_win_t *xr, iw_xr_gl_target_t *target)
{
    uint32_t i;
    if (!target) return;
    if (target->fbos) { for (i = 0; i < target->image_count; ++i) if (target->fbos[i]) GL_DeleteFramebuffersFunc(1, &target->fbos[i]); free(target->fbos); }
    if (target->mirror_fbo) GL_DeleteFramebuffersFunc(1, &target->mirror_fbo);
    if (target->depth_textures) { glDeleteTextures(target->image_count, target->depth_textures); free(target->depth_textures); }
    free(target->images);
    if (target->swapchain != XR_NULL_HANDLE && xr->destroy_swapchain) xr->destroy_swapchain(target->swapchain);
    memset(target, 0, sizeof(*target));
}

static qboolean xr_create_target(iw_xr_win_t *xr, iw_xr_gl_target_t *target, int64_t format, uint32_t width, uint32_t height, uint32_t sample_count, const char **reason)
{
    XrSwapchainCreateInfo info; XrSwapchainImageBaseHeader *base_images; XrResult result; uint32_t i;
    memset(&info, 0, sizeof(info)); info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT; info.format = format; info.sampleCount = q_max(sample_count, 1u); info.width = width; info.height = height; info.faceCount = info.arraySize = info.mipCount = 1;
    result = xr->create_swapchain(xr->session, &info, &target->swapchain); if (result != XR_SUCCESS) return false;
    result = xr->enumerate_swapchain_images(target->swapchain, 0, &target->image_count, NULL); if (result != XR_SUCCESS || !target->image_count) goto failed;
    target->images = calloc(target->image_count, sizeof(*target->images)); target->fbos = calloc(target->image_count, sizeof(*target->fbos)); if (!target->images || !target->fbos) goto failed;
    base_images = (XrSwapchainImageBaseHeader *)target->images; for (i = 0; i < target->image_count; ++i) target->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    result = xr->enumerate_swapchain_images(target->swapchain, target->image_count, &target->image_count, base_images); if (result != XR_SUCCESS) goto failed;
    GL_GenFramebuffersFunc(target->image_count, target->fbos);
    for (i = 0; i < target->image_count; ++i) { GL_BindFramebufferFunc(GL_FRAMEBUFFER, target->fbos[i]); GL_FramebufferTexture2DFunc(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->images[i].image, 0); if (GL_CheckFramebufferStatusFunc(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) goto failed; }
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0); target->width = width; target->height = height; return true;
failed:
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0); xr_destroy_target(xr, target); if (reason) *reason = "OpenXR stereo swapchain framebuffer incomplete"; return false;
}

static qboolean xr_has_gl_extension(const char *extension)
{
    GLint count = 0;
    GLint i;
    if (!extension || !GL_GetStringiFunc) return false;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (i = 0; i < count; ++i)
    {
        const char *name = (const char *)GL_GetStringiFunc(GL_EXTENSIONS, (GLuint)i);
        if (name && !strcmp(name, extension)) return true;
    }
    return false;
}
static qboolean xr_create_multiview_target(iw_xr_win_t *xr, int64_t format, uint32_t width, uint32_t height, uint32_t sample_count, const char **reason)
{
    iw_xr_gl_target_t *target = &xr->multiview;
    XrSwapchainCreateInfo info;
    XrSwapchainImageBaseHeader *base_images;
    XrResult result;
    uint32_t i;

    if (!xr->framebuffer_texture_multiview_ovr)
    {
        if (reason) *reason = "GL_OVR_multiview2 framebuffer entry point unavailable";
        return false;
    }
    memset(&info, 0, sizeof(info));
    info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = format;
    info.sampleCount = 1;
    info.width = width;
    info.height = height;
    info.faceCount = info.mipCount = 1;
    info.arraySize = 2;
    result = xr->create_swapchain(xr->session, &info, &target->swapchain);
    if (result != XR_SUCCESS) { if (reason) *reason = "OpenXR two-layer swapchain creation failed"; return false; }
    result = xr->enumerate_swapchain_images(target->swapchain, 0, &target->image_count, NULL);
    if (result != XR_SUCCESS || !target->image_count) goto failed;
    target->images = calloc(target->image_count, sizeof(*target->images));
    target->fbos = calloc(target->image_count, sizeof(*target->fbos));
    target->depth_textures = calloc(target->image_count, sizeof(*target->depth_textures));
    if (!target->images || !target->fbos || !target->depth_textures) goto failed;
    base_images = (XrSwapchainImageBaseHeader *)target->images;
    for (i = 0; i < target->image_count; ++i) target->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    result = xr->enumerate_swapchain_images(target->swapchain, target->image_count, &target->image_count, base_images);
    if (result != XR_SUCCESS) goto failed;
    GL_GenFramebuffersFunc(target->image_count, target->fbos);
    for (i = 0; i < target->image_count; ++i)
    {
        GL_BindFramebufferFunc(GL_FRAMEBUFFER, target->fbos[i]);
        xr->framebuffer_texture_multiview_ovr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target->images[i].image, 0, 0, 2);
        glGenTextures(1, &target->depth_textures[i]);
        glBindTexture(GL_TEXTURE_2D_ARRAY, target->depth_textures[i]);
        GL_TexStorage3DFunc(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH24_STENCIL8, width, height, 2);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        xr->framebuffer_texture_multiview_ovr(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, target->depth_textures[i], 0, 0, 2);
        if (GL_CheckFramebufferStatusFunc(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) goto failed;
    }
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
    target->width = width; target->height = height;
    return true;
failed:
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
    xr_destroy_target(xr, target);
    if (reason) *reason = "OpenXR layered multiview framebuffer incomplete";
    return false;
}
static qboolean xr_acquire_target(iw_xr_win_t *xr, iw_xr_gl_target_t *target)
{
    XrSwapchainImageAcquireInfo acquire_info; XrSwapchainImageWaitInfo wait_info; XrSwapchainImageReleaseInfo release_info;
    if (!target || target->swapchain == XR_NULL_HANDLE) return false;
    if (target->acquired) { GL_BindFramebufferFunc(GL_FRAMEBUFFER, target->fbos[target->image_index]); return true; }
    memset(&acquire_info, 0, sizeof(acquire_info)); acquire_info.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    if (xr->acquire_swapchain_image(target->swapchain, &acquire_info, &target->image_index) != XR_SUCCESS) return false;
    memset(&wait_info, 0, sizeof(wait_info)); wait_info.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO; wait_info.timeout = XR_INFINITE_DURATION;
    if (xr->wait_swapchain_image(target->swapchain, &wait_info) != XR_SUCCESS) { memset(&release_info, 0, sizeof(release_info)); release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO; xr->release_swapchain_image(target->swapchain, &release_info); return false; }
    target->acquired = true; GL_BindFramebufferFunc(GL_FRAMEBUFFER, target->fbos[target->image_index]); return true;
}

static void xr_release_target(iw_xr_win_t *xr, iw_xr_gl_target_t *target)
{
    XrSwapchainImageReleaseInfo release_info; if (!target || !target->acquired) return;
    memset(&release_info, 0, sizeof(release_info)); release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO; xr->release_swapchain_image(target->swapchain, &release_info); target->acquired = false;
}
static void xr_destroy_resources(iw_xr_win_t *xr);
static void xr_update_actions(iw_xr_win_t *xr);

static const char *xr_result_name(XrResult result)
{
    switch (result)
    {
    case XR_SUCCESS: return "XR_SUCCESS";
    case XR_TIMEOUT_EXPIRED: return "XR_TIMEOUT_EXPIRED";
    case XR_ERROR_RUNTIME_FAILURE: return "XR_ERROR_RUNTIME_FAILURE";
    case XR_ERROR_RUNTIME_UNAVAILABLE: return "XR_ERROR_RUNTIME_UNAVAILABLE";
    case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
    default: return "OpenXR failure";
    }
}

static void xr_rotate3 (const XrQuaternionf *q, const float v[3], float out[3])
{
    float qv[3] = { q->x, q->y, q->z }, t[3], c[3];
    IW_XRMath_Cross3 (qv, v, t); t[0] *= 2.0f; t[1] *= 2.0f; t[2] *= 2.0f;
    IW_XRMath_Cross3 (qv, t, c);
    out[0] = v[0] + q->w * t[0] + c[0]; out[1] = v[1] + q->w * t[1] + c[1]; out[2] = v[2] + q->w * t[2] + c[2];
}
static XrQuaternionf xr_quaternion_from_basis(const float x[3], const float y[3], const float z[3])
{
    XrQuaternionf q;
    float trace = x[0] + y[1] + z[2];
    if (trace > 0.f) {
        float s = sqrtf(trace + 1.f) * 2.f;
        q.w = 0.25f * s; q.x = (y[2] - z[1]) / s; q.y = (z[0] - x[2]) / s; q.z = (x[1] - y[0]) / s;
    } else if (x[0] > y[1] && x[0] > z[2]) {
        float s = sqrtf(1.f + x[0] - y[1] - z[2]) * 2.f;
        q.w = (y[2] - z[1]) / s; q.x = 0.25f * s; q.y = (y[0] + x[1]) / s; q.z = (z[0] + x[2]) / s;
    } else if (y[1] > z[2]) {
        float s = sqrtf(1.f + y[1] - x[0] - z[2]) * 2.f;
        q.w = (z[0] - x[2]) / s; q.x = (y[0] + x[1]) / s; q.y = 0.25f * s; q.z = (z[1] + y[2]) / s;
    } else {
        float s = sqrtf(1.f + z[2] - x[0] - y[1]) * 2.f;
        q.w = (x[1] - y[0]) / s; q.x = (z[0] + x[2]) / s; q.y = (z[1] + y[2]) / s; q.z = 0.25f * s;
    }
    return q;
}

static XrPosef xr_pointer_pose(const float start[3], const float hit[3], const float eye[3])
{
    XrPosef pose;
    float x[3], y[3], z[3], view[3], length;
    memset(&pose, 0, sizeof(pose));
    pose.position.x = (start[0] + hit[0]) * 0.5f;
    pose.position.y = (start[1] + hit[1]) * 0.5f;
    pose.position.z = (start[2] + hit[2]) * 0.5f;
    x[0] = hit[0] - start[0]; x[1] = hit[1] - start[1]; x[2] = hit[2] - start[2];
    length = sqrtf(IW_XRMath_Dot3(x, x));
    if (length < 0.01f) { pose.orientation.w = 1.f; return pose; }
    x[0] /= length; x[1] /= length; x[2] /= length;
    view[0] = eye[0] - pose.position.x; view[1] = eye[1] - pose.position.y; view[2] = eye[2] - pose.position.z;
    if (!IW_XRMath_Normalize3(view)) { view[0] = 0.f; view[1] = 0.f; view[2] = -1.f; }
    IW_XRMath_Cross3(view, x, y);
    if (!IW_XRMath_Normalize3(y)) { y[0] = 0.f; y[1] = 1.f; y[2] = 0.f; IW_XRMath_Cross3(y, x, y); IW_XRMath_Normalize3(y); }
    IW_XRMath_Cross3(x, y, z); IW_XRMath_Normalize3(z);
    pose.orientation = xr_quaternion_from_basis(x, y, z);
    return pose;
}
static XrPosef xr_cylinder_pose(const XrPosef *screen_pose, float radius)
{
    XrPosef pose = *screen_pose;
    float offset[3] = { 0.f, 0.f, radius };
    xr_rotate3(&pose.orientation, offset, offset);
    pose.position.x += offset[0];
    pose.position.y += offset[1];
    pose.position.z += offset[2];
    return pose;
}
static XrPosef xr_build_screen_pose (const XrVector3f *center, const float forward[3], float distance)
{
    XrPosef pose; float yaw = atan2f (-forward[0], -forward[2]), half = yaw * 0.5f;
    pose.orientation.x = 0.0f; pose.orientation.y = sinf (half); pose.orientation.z = 0.0f; pose.orientation.w = cosf (half);
    pose.position.x = center->x + forward[0] * distance; pose.position.y = center->y; pose.position.z = center->z + forward[2] * distance;
    return pose;
}
static void xr_update_screen_pose (iw_xr_win_t *xr, const XrView *views, uint32_t view_count)
{
    XrVector3f center = {0.0f, 0.0f, 0.0f};
    float forward[3] = {0.0f, 0.0f, 0.0f};
    iw_xr_virtual_screen_view_t screen_views[2];
    iw_xr_virtual_screen_pose_t screen_pose;
    uint32_t i, count = q_min(view_count, (uint32_t)Q_COUNTOF(screen_views));

    if (!count) return;
    for (i = 0; i < count; ++i) {
        float view_forward[3] = {0.0f, 0.0f, -1.0f};
        center.x += views[i].pose.position.x; center.y += views[i].pose.position.y; center.z += views[i].pose.position.z;
        xr_rotate3(&views[i].pose.orientation, view_forward, view_forward);
        forward[0] += view_forward[0]; forward[1] += view_forward[1]; forward[2] += view_forward[2];
        screen_views[i].position[0] = views[i].pose.position.x; screen_views[i].position[1] = views[i].pose.position.y; screen_views[i].position[2] = views[i].pose.position.z;
        screen_views[i].orientation[0] = views[i].pose.orientation.x; screen_views[i].orientation[1] = views[i].pose.orientation.y; screen_views[i].orientation[2] = views[i].pose.orientation.z; screen_views[i].orientation[3] = views[i].pose.orientation.w;
    }
    center.x /= (float)count; center.y /= (float)count; center.z /= (float)count; forward[1] = 0.0f;
    if (!IW_XRMath_Normalize3(forward)) { forward[0] = 0.0f; forward[1] = 0.0f; forward[2] = -1.0f; }
    xr->hud_pose = xr_build_screen_pose(&center, forward, xr->hud_distance);
    xr->hud_pose.position.y += xr->hud_yoffset;
    if (xr->recenter_requested) { memset(&xr->screen_follow, 0, sizeof(xr->screen_follow)); xr->recenter_requested = false; }
    if (IW_XRVirtualScreen_UpdatePose(&xr->screen_follow, screen_views, count, (double)GetTickCount64() * 0.001, xr->screen_distance, vr_screen_follow.value != 0.f, &screen_pose)) {
        xr->screen_pose.position.x = screen_pose.position[0]; xr->screen_pose.position.y = screen_pose.position[1]; xr->screen_pose.position.z = screen_pose.position[2];
        xr->screen_pose.orientation.x = screen_pose.orientation[0]; xr->screen_pose.orientation.y = screen_pose.orientation[1]; xr->screen_pose.orientation.z = screen_pose.orientation[2]; xr->screen_pose.orientation.w = screen_pose.orientation[3];
    }
}static iw_xr_result_t xr_fail(iw_xr_win_t *xr, const char **reason, XrResult result, const char *stage)
{
    static char message[256];
    q_snprintf(message, sizeof(message), "%s: %s (%d)", stage, xr_result_name(result), (int)result);
    xr_log(xr, message);
    if (reason)
        *reason = message;
    xr_destroy_resources (xr);
    if (result == XR_TIMEOUT_EXPIRED)
        return IW_XR_RESULT_TIMEOUT;
    if (result == XR_ERROR_RUNTIME_UNAVAILABLE || result == XR_ERROR_FORM_FACTOR_UNAVAILABLE || result == XR_ERROR_EXTENSION_NOT_PRESENT)
        return IW_XR_RESULT_UNAVAILABLE;
    return IW_XR_RESULT_FAILED;
}

static qboolean xr_load_global(iw_xr_win_t *xr, const char **reason)
{
    char module_path[MAX_PATH];
    char *slash;
    PFN_xrVoidFunction proc = NULL;
    xr->loader = LoadLibraryA("openxr_loader.dll");
    if (!xr->loader)
    {
        if (GetModuleFileNameA(NULL, module_path, sizeof(module_path)) == 0)
            return false;
        slash = strrchr(module_path, '\\');
        if (!slash)
            return false;
        slash[1] = 0;
        q_strlcat(module_path, "openxr_loader.dll", sizeof(module_path));
        xr->loader = LoadLibraryA(module_path);
    }
    if (!xr->loader)
    {
        if (reason)
            *reason = "OpenXR loader DLL not found";
        return false;
    }
    xr->get_instance_proc_addr = (PFN_xrGetInstanceProcAddr)GetProcAddress(xr->loader, "xrGetInstanceProcAddr");
    if (!xr->get_instance_proc_addr)
    {
        if (reason)
            *reason = "OpenXR loader lacks xrGetInstanceProcAddr";
        return false;
    }
#define XR_LOAD_GLOBAL(name, field) do { proc = NULL; if (xr->get_instance_proc_addr(XR_NULL_HANDLE, #name, &proc) != XR_SUCCESS || !proc) return false; xr->field = (PFN_##name)proc; } while (0)
    XR_LOAD_GLOBAL(xrEnumerateInstanceExtensionProperties, enumerate_instance_extension_properties);
    XR_LOAD_GLOBAL(xrCreateInstance, create_instance);
#undef XR_LOAD_GLOBAL
    xr_log(xr, "OpenXR loader loaded and global procedures resolved");
    return true;
}

static qboolean xr_load_instance(iw_xr_win_t *xr, const char **reason)
{
    PFN_xrVoidFunction proc = NULL;
#define XR_LOAD_INSTANCE(name, field) do { proc = NULL; if (xr->get_instance_proc_addr(xr->instance, #name, &proc) != XR_SUCCESS || !proc) { if (reason) *reason = #name " is unavailable"; return false; } xr->field = (PFN_##name)proc; } while (0)
    XR_LOAD_INSTANCE(xrDestroyInstance, destroy_instance);
    XR_LOAD_INSTANCE(xrPollEvent, poll_event);
    XR_LOAD_INSTANCE(xrGetSystem, get_system);
    XR_LOAD_INSTANCE(xrGetSystemProperties, get_system_properties);
    XR_LOAD_INSTANCE(xrEnumerateViewConfigurations, enumerate_view_configurations);
    XR_LOAD_INSTANCE(xrEnumerateViewConfigurationViews, enumerate_view_configuration_views);
    XR_LOAD_INSTANCE(xrCreateSession, create_session);
    XR_LOAD_INSTANCE(xrDestroySession, destroy_session);
    XR_LOAD_INSTANCE(xrBeginSession, begin_session);
    XR_LOAD_INSTANCE(xrEndSession, end_session);
    XR_LOAD_INSTANCE(xrCreateReferenceSpace, create_reference_space);
    XR_LOAD_INSTANCE(xrDestroySpace, destroy_space);
    XR_LOAD_INSTANCE(xrLocateViews, locate_views);
    XR_LOAD_INSTANCE(xrEnumerateSwapchainFormats, enumerate_swapchain_formats);
    XR_LOAD_INSTANCE(xrCreateSwapchain, create_swapchain);
    XR_LOAD_INSTANCE(xrDestroySwapchain, destroy_swapchain);
    XR_LOAD_INSTANCE(xrEnumerateSwapchainImages, enumerate_swapchain_images);
    XR_LOAD_INSTANCE(xrAcquireSwapchainImage, acquire_swapchain_image);
    XR_LOAD_INSTANCE(xrWaitSwapchainImage, wait_swapchain_image);
    XR_LOAD_INSTANCE(xrReleaseSwapchainImage, release_swapchain_image);
    XR_LOAD_INSTANCE(xrWaitFrame, wait_frame);
    XR_LOAD_INSTANCE(xrBeginFrame, begin_frame);
    XR_LOAD_INSTANCE(xrEndFrame, end_frame);
    XR_LOAD_INSTANCE(xrStringToPath, string_to_path);
    XR_LOAD_INSTANCE(xrCreateActionSet, create_action_set);
    XR_LOAD_INSTANCE(xrDestroyActionSet, destroy_action_set);
    XR_LOAD_INSTANCE(xrCreateAction, create_action);
    XR_LOAD_INSTANCE(xrDestroyAction, destroy_action);
    XR_LOAD_INSTANCE(xrSuggestInteractionProfileBindings, suggest_interaction_profile_bindings);
    XR_LOAD_INSTANCE(xrAttachSessionActionSets, attach_session_action_sets);
    XR_LOAD_INSTANCE(xrSyncActions, sync_actions);
    XR_LOAD_INSTANCE(xrGetActionStateBoolean, get_action_state_boolean);
    XR_LOAD_INSTANCE(xrGetActionStateFloat, get_action_state_float);
    XR_LOAD_INSTANCE(xrGetActionStateVector2f, get_action_state_vector2f);
    XR_LOAD_INSTANCE(xrGetActionStatePose, get_action_state_pose);
    XR_LOAD_INSTANCE(xrCreateActionSpace, create_action_space);
    XR_LOAD_INSTANCE(xrLocateSpace, locate_space);
    XR_LOAD_INSTANCE(xrApplyHapticFeedback, apply_haptic_feedback);
    XR_LOAD_INSTANCE(xrStopHapticFeedback, stop_haptic_feedback);
    proc = NULL;
    if (xr->get_instance_proc_addr(xr->instance, "xrGetOpenGLGraphicsRequirementsKHR", &proc) != XR_SUCCESS || !proc)
    {
        if (reason)
            *reason = "XR_KHR_opengl_enable procedure is unavailable";
        return false;
    }
    xr->get_opengl_graphics_requirements = (PFN_xrGetOpenGLGraphicsRequirementsKHR)proc;
    if (xr->refresh_rate_supported)
    {
        proc = NULL; xr->get_instance_proc_addr(xr->instance, "xrEnumerateDisplayRefreshRatesFB", &proc); xr->enumerate_display_refresh_rates = (PFN_xrEnumerateDisplayRefreshRatesFB)proc;
        proc = NULL; xr->get_instance_proc_addr(xr->instance, "xrGetDisplayRefreshRateFB", &proc); xr->get_display_refresh_rate = (PFN_xrGetDisplayRefreshRateFB)proc;
        proc = NULL; xr->get_instance_proc_addr(xr->instance, "xrRequestDisplayRefreshRateFB", &proc); xr->request_display_refresh_rate = (PFN_xrRequestDisplayRefreshRateFB)proc;
        if (!xr->enumerate_display_refresh_rates || !xr->get_display_refresh_rate || !xr->request_display_refresh_rate) xr->refresh_rate_supported = false;
    }
#undef XR_LOAD_INSTANCE
    return true;
}



static qboolean xr_create_action(iw_xr_win_t *xr, XrActionType type, const char *name, const char *localized_name, XrAction *action)
{
    XrActionCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = XR_TYPE_ACTION_CREATE_INFO;
    info.actionType = type;
    q_strlcpy(info.actionName, name, sizeof(info.actionName));
    q_strlcpy(info.localizedActionName, localized_name, sizeof(info.localizedActionName));
    info.countSubactionPaths = 2;
    info.subactionPaths = xr->hand_paths;
    return xr->create_action(xr->action_set, &info, action) == XR_SUCCESS;
}

static qboolean xr_add_binding(iw_xr_win_t *xr, XrActionSuggestedBinding *bindings, uint32_t *count, XrAction action, const char *path)
{
    XrPath binding;
    if (xr->string_to_path(xr->instance, path, &binding) != XR_SUCCESS)
        return false;
    bindings[*count].action = action;
    bindings[*count].binding = binding;
    ++*count;
    return true;
}

static void xr_suggest_profile(iw_xr_win_t *xr, const char *profile, XrActionSuggestedBinding *bindings, uint32_t count)
{
    XrInteractionProfileSuggestedBinding suggested;
    XrPath interaction_profile;
    if (!count || xr->string_to_path(xr->instance, profile, &interaction_profile) != XR_SUCCESS)
        return;
    memset(&suggested, 0, sizeof(suggested));
    suggested.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
    suggested.interactionProfile = interaction_profile;
    suggested.countSuggestedBindings = count;
    suggested.suggestedBindings = bindings;
    xr->suggest_interaction_profile_bindings(xr->instance, &suggested);
}

static void xr_suggest_touch_bindings(iw_xr_win_t *xr)
{
    XrActionSuggestedBinding bindings[20];
    uint32_t count = 0;
    xr_add_binding(xr, bindings, &count, xr->aim_action, "/user/hand/left/input/aim/pose");
    xr_add_binding(xr, bindings, &count, xr->aim_action, "/user/hand/right/input/aim/pose");
    xr_add_binding(xr, bindings, &count, xr->grip_pose_action, "/user/hand/left/input/grip/pose");
    xr_add_binding(xr, bindings, &count, xr->grip_pose_action, "/user/hand/right/input/grip/pose");
    xr_add_binding(xr, bindings, &count, xr->trigger_action, "/user/hand/left/input/trigger/value");
    xr_add_binding(xr, bindings, &count, xr->trigger_action, "/user/hand/right/input/trigger/value");
    xr_add_binding(xr, bindings, &count, xr->grip_action, "/user/hand/left/input/squeeze/value");
    xr_add_binding(xr, bindings, &count, xr->grip_action, "/user/hand/right/input/squeeze/value");
    xr_add_binding(xr, bindings, &count, xr->stick_action, "/user/hand/left/input/thumbstick");
    xr_add_binding(xr, bindings, &count, xr->stick_action, "/user/hand/right/input/thumbstick");
    xr_add_binding(xr, bindings, &count, xr->stick_click_action, "/user/hand/left/input/thumbstick/click");
    xr_add_binding(xr, bindings, &count, xr->stick_click_action, "/user/hand/right/input/thumbstick/click");
    xr_add_binding(xr, bindings, &count, xr->primary_action, "/user/hand/left/input/x/click");
    xr_add_binding(xr, bindings, &count, xr->primary_action, "/user/hand/right/input/a/click");
    xr_add_binding(xr, bindings, &count, xr->secondary_action, "/user/hand/left/input/y/click");
    xr_add_binding(xr, bindings, &count, xr->secondary_action, "/user/hand/right/input/b/click");
    xr_add_binding(xr, bindings, &count, xr->menu_action, "/user/hand/left/input/menu/click");
    xr_add_binding(xr, bindings, &count, xr->haptic_action, "/user/hand/left/output/haptic");
    xr_add_binding(xr, bindings, &count, xr->haptic_action, "/user/hand/right/output/haptic");
    xr_suggest_profile(xr, "/interaction_profiles/oculus/touch_controller", bindings, count);
}

static void xr_suggest_index_bindings(iw_xr_win_t *xr)
{
    XrActionSuggestedBinding bindings[20];
    uint32_t count = 0;
    xr_add_binding(xr, bindings, &count, xr->aim_action, "/user/hand/left/input/aim/pose");
    xr_add_binding(xr, bindings, &count, xr->aim_action, "/user/hand/right/input/aim/pose");
    xr_add_binding(xr, bindings, &count, xr->grip_pose_action, "/user/hand/left/input/grip/pose");
    xr_add_binding(xr, bindings, &count, xr->grip_pose_action, "/user/hand/right/input/grip/pose");
    xr_add_binding(xr, bindings, &count, xr->trigger_action, "/user/hand/left/input/trigger/value");
    xr_add_binding(xr, bindings, &count, xr->trigger_action, "/user/hand/right/input/trigger/value");
    xr_add_binding(xr, bindings, &count, xr->grip_action, "/user/hand/left/input/squeeze/force");
    xr_add_binding(xr, bindings, &count, xr->grip_action, "/user/hand/right/input/squeeze/force");
    xr_add_binding(xr, bindings, &count, xr->stick_action, "/user/hand/left/input/thumbstick");
    xr_add_binding(xr, bindings, &count, xr->stick_action, "/user/hand/right/input/thumbstick");
    xr_add_binding(xr, bindings, &count, xr->stick_click_action, "/user/hand/left/input/thumbstick/click");
    xr_add_binding(xr, bindings, &count, xr->stick_click_action, "/user/hand/right/input/thumbstick/click");
    xr_add_binding(xr, bindings, &count, xr->primary_action, "/user/hand/left/input/a/click");
    xr_add_binding(xr, bindings, &count, xr->primary_action, "/user/hand/right/input/a/click");
    xr_add_binding(xr, bindings, &count, xr->secondary_action, "/user/hand/left/input/b/click");
    xr_add_binding(xr, bindings, &count, xr->secondary_action, "/user/hand/right/input/b/click");
    xr_add_binding(xr, bindings, &count, xr->haptic_action, "/user/hand/left/output/haptic");
    xr_add_binding(xr, bindings, &count, xr->haptic_action, "/user/hand/right/output/haptic");
    xr_suggest_profile(xr, "/interaction_profiles/valve/index_controller", bindings, count);
}

static void xr_suggest_vive_bindings(iw_xr_win_t *xr)
{
    XrActionSuggestedBinding bindings[20];
    uint32_t count = 0;
    xr_add_binding(xr, bindings, &count, xr->aim_action, "/user/hand/left/input/aim/pose");
    xr_add_binding(xr, bindings, &count, xr->aim_action, "/user/hand/right/input/aim/pose");
    xr_add_binding(xr, bindings, &count, xr->grip_pose_action, "/user/hand/left/input/grip/pose");
    xr_add_binding(xr, bindings, &count, xr->grip_pose_action, "/user/hand/right/input/grip/pose");
    xr_add_binding(xr, bindings, &count, xr->trigger_action, "/user/hand/left/input/trigger/value");
    xr_add_binding(xr, bindings, &count, xr->trigger_action, "/user/hand/right/input/trigger/value");
    xr_add_binding(xr, bindings, &count, xr->trigger_click_action, "/user/hand/left/input/trigger/click");
    xr_add_binding(xr, bindings, &count, xr->trigger_click_action, "/user/hand/right/input/trigger/click");
    xr_add_binding(xr, bindings, &count, xr->grip_click_action, "/user/hand/left/input/squeeze/click");
    xr_add_binding(xr, bindings, &count, xr->grip_click_action, "/user/hand/right/input/squeeze/click");
    xr_add_binding(xr, bindings, &count, xr->trackpad_action, "/user/hand/left/input/trackpad");
    xr_add_binding(xr, bindings, &count, xr->trackpad_action, "/user/hand/right/input/trackpad");
    xr_add_binding(xr, bindings, &count, xr->stick_click_action, "/user/hand/left/input/trackpad/click");
    xr_add_binding(xr, bindings, &count, xr->stick_click_action, "/user/hand/right/input/trackpad/click");
    xr_add_binding(xr, bindings, &count, xr->menu_action, "/user/hand/left/input/menu/click");
    xr_add_binding(xr, bindings, &count, xr->haptic_action, "/user/hand/left/output/haptic");
    xr_add_binding(xr, bindings, &count, xr->haptic_action, "/user/hand/right/output/haptic");
    xr_add_binding(xr, bindings, &count, xr->menu_action, "/user/hand/right/input/menu/click");
   xr_suggest_profile(xr, "/interaction_profiles/htc/vive_controller", bindings, count);
}

static qboolean xr_create_actions(iw_xr_win_t *xr)
{
    XrActionSetCreateInfo set_info;
    XrSessionActionSetsAttachInfo attach_info;
    XrActionSpaceCreateInfo space_info;
    int hand;

    if (xr->string_to_path(xr->instance, "/user/hand/left", &xr->hand_paths[IW_XR_HAND_LEFT]) != XR_SUCCESS ||
        xr->string_to_path(xr->instance, "/user/hand/right", &xr->hand_paths[IW_XR_HAND_RIGHT]) != XR_SUCCESS)
        return false;
    memset(&set_info, 0, sizeof(set_info));
    set_info.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    q_strlcpy(set_info.actionSetName, "gameplay", sizeof(set_info.actionSetName));
    q_strlcpy(set_info.localizedActionSetName, "Gameplay", sizeof(set_info.localizedActionSetName));
    if (xr->create_action_set(xr->instance, &set_info, &xr->action_set) != XR_SUCCESS)
        return false;
    if (!xr_create_action(xr, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim Pose", &xr->aim_action) ||
        !xr_create_action(xr, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip Pose", &xr->grip_pose_action) ||
        !xr_create_action(xr, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", &xr->trigger_action) ||
        !xr_create_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "trigger_click", "Trigger Click", &xr->trigger_click_action) ||
        !xr_create_action(xr, XR_ACTION_TYPE_FLOAT_INPUT, "grip", "Grip", &xr->grip_action) ||
        !xr_create_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "grip_click", "Grip Click", &xr->grip_click_action) ||
        !xr_create_action(xr, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", &xr->stick_action) ||
        !xr_create_action(xr, XR_ACTION_TYPE_VECTOR2F_INPUT, "trackpad", "Trackpad", &xr->trackpad_action) ||
        !xr_create_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click", "Thumbstick Click", &xr->stick_click_action) ||
        !xr_create_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "primary", "Primary Button", &xr->primary_action) ||
        !xr_create_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary", "Secondary Button", &xr->secondary_action) ||
        !xr_create_action(xr, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu Button", &xr->menu_action) ||
        !xr_create_action(xr, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Haptic", &xr->haptic_action))
        return false;
    xr_suggest_touch_bindings(xr);
    xr_suggest_index_bindings(xr);
    xr_suggest_vive_bindings(xr);
    memset(&attach_info, 0, sizeof(attach_info));
    attach_info.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    attach_info.countActionSets = 1;
    attach_info.actionSets = &xr->action_set;
    if (xr->attach_session_action_sets(xr->session, &attach_info) != XR_SUCCESS)
        return false;
    for (hand = 0; hand < 2; ++hand)
    {
        memset(&space_info, 0, sizeof(space_info));
        space_info.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
        space_info.subactionPath = xr->hand_paths[hand];
        space_info.poseInActionSpace.orientation.w = 1.0f;
        space_info.action = xr->aim_action;
        if (xr->create_action_space(xr->session, &space_info, &xr->aim_spaces[hand]) != XR_SUCCESS)
            return false;
        space_info.action = xr->grip_pose_action;
        if (xr->create_action_space(xr->session, &space_info, &xr->grip_spaces[hand]) != XR_SUCCESS)
            return false;
    }
    xr_log(xr, "OpenXR gameplay actions attached");
    return true;
}

static void xr_destroy_actions(iw_xr_win_t *xr)
{
    int hand;
    if (!xr) return;
    for (hand = 0; hand < 2; ++hand)
    {
        if (xr->aim_spaces[hand] != XR_NULL_HANDLE && xr->destroy_space) xr->destroy_space(xr->aim_spaces[hand]);
        if (xr->grip_spaces[hand] != XR_NULL_HANDLE && xr->destroy_space) xr->destroy_space(xr->grip_spaces[hand]);
        xr->aim_spaces[hand] = xr->grip_spaces[hand] = XR_NULL_HANDLE;
    }
    if (xr->action_set != XR_NULL_HANDLE && xr->destroy_action_set) xr->destroy_action_set(xr->action_set);
    xr->action_set = XR_NULL_HANDLE;
}
static void xr_destroy_resources(iw_xr_win_t *xr)
{
    uint32_t i;
    if (!xr)
        return;
    xr_destroy_actions(xr);
    xr_destroy_target(xr, &xr->eyes[0]);
    xr_destroy_target(xr, &xr->eyes[1]);
    xr_destroy_target(xr, &xr->multiview);
    xr_destroy_target(xr, &xr->pointer_target);
    if (xr->fbos)
    {
        for (i = 0; i < xr->image_count; ++i)
            if (xr->fbos[i])
                GL_DeleteFramebuffersFunc(1, &xr->fbos[i]);
        free(xr->fbos);
        xr->fbos = NULL;
    }
    free(xr->images);
    xr->images = NULL;
    xr->image_count = 0;
    if (xr->swapchain != XR_NULL_HANDLE && xr->destroy_swapchain)
    {
        xr->destroy_swapchain(xr->swapchain);
        xr->swapchain = XR_NULL_HANDLE;
    }
    if (xr->space != XR_NULL_HANDLE && xr->destroy_space)
    {
        xr->destroy_space(xr->space);
        xr->space = XR_NULL_HANDLE;
    }
    if (xr->session != XR_NULL_HANDLE && xr->destroy_session)
    {
        xr->destroy_session(xr->session);
        xr->session = XR_NULL_HANDLE;
    }
    if (xr->instance != XR_NULL_HANDLE && xr->destroy_instance)
    {
        xr->destroy_instance(xr->instance);
        xr->instance = XR_NULL_HANDLE;
    }
    if (xr->loader)
    {
        FreeLibrary(xr->loader);
        xr->loader = NULL;
    }
}

iw_xr_win_t *IW_XRWin_Create(void *window, void (*log)(void *, const char *), void *userdata)
{
    iw_xr_win_t *xr = (iw_xr_win_t *)calloc(1, sizeof(*xr));
    if (xr)
    {
        xr->window = window;
        xr->log = log;
        xr->userdata = userdata;
        xr->session_state = XR_SESSION_STATE_UNKNOWN;
        xr->screen_pose.orientation.w = 1.0f;
        xr->hud_pose.orientation.w = 1.0f;
        xr->screen_pose.position.y = 1.6f;
        xr->screen_pose.position.z = -2.0f;
        xr->screen_scale = 1.0f;
        xr->screen_distance = 2.5f;
        xr->hud_scale = 0.35f;
        xr->hud_distance = 0.5f;
        xr->hud_yoffset = 0.0f;
    }
    return xr;
}

void IW_XRWin_Destroy(iw_xr_win_t *xr)
{
    if (xr)
    {
        IW_XRWin_Shutdown(xr);
        free(xr);
    }
}

iw_xr_result_t IW_XRWin_Probe(iw_xr_win_t *xr, iw_xr_bridge_t *bridge, uint64_t deadline_ns, const char **reason)
{
    const char *extensions[3];
    uint32_t enabled_extension_count = 1;
    XrApplicationInfo app;
    XrInstanceCreateInfo instance_info;
    XrSystemGetInfo system_info;
    XrGraphicsRequirementsOpenGLKHR requirements;
    XrSessionCreateInfo session_info;
    XrReferenceSpaceCreateInfo space_info;
    XrSwapchainCreateInfo swap_info;
    XrSwapchainImageBaseHeader *base_images;
    int64_t formats[16];
    uint32_t count, i;
    XrResult result;
    SDL_SysWMinfo wm;
    XrEventDataBuffer event;
    XrExtensionProperties *available_extensions = NULL;
    uint32_t extension_count = 0;
    qboolean has_opengl_extension = false;
    qboolean has_cylinder_extension = false;
    qboolean has_refresh_rate_extension = false;
    XrViewConfigurationType view_configs[8];
    uint32_t view_config_count = 0;
    uint32_t stereo_view_count = 0;
    uint32_t view_config_index;
    XrViewConfigurationView view_config_views[2];

    (void)bridge;
    (void)deadline_ns;
    if (xr->instance || xr->loader)
        IW_XRWin_Shutdown(xr);
    if (!xr_load_global(xr, reason))
    {
        xr_destroy_resources (xr);
        return IW_XR_RESULT_UNAVAILABLE;
    }
    result = xr->enumerate_instance_extension_properties (NULL, 0, &extension_count, NULL);
    if (result != XR_SUCCESS || extension_count == 0)
        return xr_fail (xr, reason, result, "xrEnumerateInstanceExtensionProperties");
    available_extensions = (XrExtensionProperties *)calloc (extension_count, sizeof (*available_extensions));
    if (!available_extensions)
    {
        if (reason) *reason = "OpenXR extension list allocation failed";
        xr_destroy_resources (xr);
        return IW_XR_RESULT_FAILED;
    }
    for (view_config_index = 0; view_config_index < extension_count; ++view_config_index)
        available_extensions[view_config_index].type = XR_TYPE_EXTENSION_PROPERTIES;
    result = xr->enumerate_instance_extension_properties (NULL, extension_count, &extension_count, available_extensions);
    if (result == XR_SUCCESS)
    {
        for (view_config_index = 0; view_config_index < extension_count; ++view_config_index)
        {
            if (!strcmp (available_extensions[view_config_index].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME))
                has_opengl_extension = true;
            if (!strcmp (available_extensions[view_config_index].extensionName, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME))
                has_cylinder_extension = true;
            if (!strcmp (available_extensions[view_config_index].extensionName, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
                has_refresh_rate_extension = true;
        }
    }
    extensions[0] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
    if (has_cylinder_extension)
        extensions[enabled_extension_count++] = XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME;
    if (has_refresh_rate_extension)
        extensions[enabled_extension_count++] = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
    xr->cylinder_supported = has_cylinder_extension;
    xr->refresh_rate_supported = has_refresh_rate_extension;
    xr_log (xr, has_cylinder_extension ? "OpenXR cylinder composition layer enabled" : "OpenXR cylinder layer unavailable; using flat virtual screen");
    free (available_extensions);
    available_extensions = NULL;
    if (result != XR_SUCCESS)
        return xr_fail (xr, reason, result, "xrEnumerateInstanceExtensionProperties");
    if (!has_opengl_extension)
    {
        if (reason) *reason = "Runtime does not advertise XR_KHR_opengl_enable";
        xr_destroy_resources (xr);
        return IW_XR_RESULT_UNAVAILABLE;
    }
    memset(&app, 0, sizeof(app));
    q_strlcpy(app.applicationName, "Ironwail", sizeof(app.applicationName));
    q_strlcpy(app.engineName, "Ironwail", sizeof(app.engineName));
    app.applicationVersion = 1;
    app.engineVersion = 1;
    app.apiVersion = XR_API_VERSION_1_0;
    memset(&instance_info, 0, sizeof(instance_info));
    instance_info.type = XR_TYPE_INSTANCE_CREATE_INFO;
    instance_info.applicationInfo = app;
    instance_info.enabledExtensionCount = enabled_extension_count;
    instance_info.enabledExtensionNames = extensions;
    result = xr->create_instance(&instance_info, &xr->instance);
    if (result != XR_SUCCESS)
        return xr_fail(xr, reason, result, "xrCreateInstance");
    if (!xr_load_instance(xr, reason))
    {
        xr_destroy_resources (xr);
        return IW_XR_RESULT_FAILED;
    }
    memset(&system_info, 0, sizeof(system_info));
    system_info.type = XR_TYPE_SYSTEM_GET_INFO;
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    result = xr->get_system(xr->instance, &system_info, &xr->system_id);
    if (result != XR_SUCCESS)
        return xr_fail(xr, reason, result, "xrGetSystem");
    result = xr->enumerate_view_configurations (xr->instance, xr->system_id, 8, &view_config_count, view_configs);
    if (result != XR_SUCCESS || view_config_count == 0)
        return xr_fail (xr, reason, result, "xrEnumerateViewConfigurations");
    if (view_config_count > 8)
        view_config_count = 8;
    xr->view_config_type = view_configs[0];
    for (view_config_index = 0; view_config_index < view_config_count; ++view_config_index)
        if (view_configs[view_config_index] == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
            xr->view_config_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    memset(view_config_views, 0, sizeof(view_config_views));
    for (view_config_index = 0; view_config_index < Q_COUNTOF(view_config_views); ++view_config_index)
        view_config_views[view_config_index].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    result = xr->enumerate_view_configuration_views(xr->instance, xr->system_id, xr->view_config_type,
                                                     Q_COUNTOF(view_config_views), &count, view_config_views);
    if (result != XR_SUCCESS || count < 2)
        return xr_fail(xr, reason, result, "xrEnumerateViewConfigurationViews");
    stereo_view_count = count;    memset(&requirements, 0, sizeof(requirements));
    requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
    result = xr->get_opengl_graphics_requirements(xr->instance, xr->system_id, &requirements);
    if (result != XR_SUCCESS)
        return xr_fail(xr, reason, result, "xrGetOpenGLGraphicsRequirementsKHR");
    memset(&wm, 0, sizeof(wm));
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo((SDL_Window *)xr->window, &wm))
    {
        if (reason) *reason = "SDL could not provide Win32 window handles";
                xr_destroy_resources (xr);
        return IW_XR_RESULT_FAILED;
    }
    memset(&session_info, 0, sizeof(session_info));
    {
        XrGraphicsBindingOpenGLWin32KHR binding;
        memset(&binding, 0, sizeof(binding));
        binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
        binding.hDC = wm.info.win.hdc;
        binding.hGLRC = (HGLRC)SDL_GL_GetCurrentContext();
        session_info.type = XR_TYPE_SESSION_CREATE_INFO;
        session_info.next = &binding;
        session_info.systemId = xr->system_id;
        result = xr->create_session(xr->instance, &session_info, &xr->session);
    }
    if (result != XR_SUCCESS)
        return xr_fail(xr, reason, result, "xrCreateSession");
    xr_apply_refresh_rate(xr);
    if (!xr_create_actions(xr))
    {
        if (reason) *reason = "OpenXR gameplay action creation failed";
        xr_destroy_resources(xr);
        return IW_XR_RESULT_FAILED;
    }    memset(&space_info, 0, sizeof(space_info));
    space_info.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    space_info.poseInReferenceSpace.orientation.w = 1.0f;
    space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    result = xr->create_reference_space(xr->session, &space_info, &xr->space);
    if (result != XR_SUCCESS)
    {
        space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        result = xr->create_reference_space(xr->session, &space_info, &xr->space);
    }
    if (result != XR_SUCCESS)
        return xr_fail(xr, reason, result, "xrCreateReferenceSpace");
    result = xr->enumerate_swapchain_formats(xr->session, 16, &count, formats);
    if (result != XR_SUCCESS || count == 0)
        return xr_fail(xr, reason, result, "xrEnumerateSwapchainFormats");
    if (count > 16)
        count = 16;
    memset(&swap_info, 0, sizeof(swap_info));
    swap_info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    swap_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swap_info.format = formats[0];
    xr->swapchain_is_srgb = false;
    for (i = 0; i < count; ++i)
    {
        if (formats[i] == GL_SRGB8_ALPHA8)
        {
            swap_info.format = formats[i];
            xr->swapchain_is_srgb = true;
            break;
        }
    }
    if (!xr->swapchain_is_srgb)
    {
        for (i = 0; i < count; ++i)
        {
            if (formats[i] == GL_RGBA8)
            {
                swap_info.format = formats[i];
                break;
            }
        }
    }
    {
        char message[128];
        q_snprintf (message, sizeof (message), "OpenXR swapchain format=%lld srgb=%d", (long long)swap_info.format, xr->swapchain_is_srgb);
        xr_log (xr, message);
    }
    swap_info.sampleCount = 1;
    swap_info.width = xr->width = 1024;
    swap_info.height = xr->height = 768;
    swap_info.faceCount = 1;
    swap_info.arraySize = 1;
    swap_info.mipCount = 1;
    result = xr->create_swapchain(xr->session, &swap_info, &xr->swapchain);
    if (result != XR_SUCCESS)
        return xr_fail(xr, reason, result, "xrCreateSwapchain");
    result = xr->enumerate_swapchain_images(xr->swapchain, 0, &xr->image_count, NULL);
    if (result != XR_SUCCESS || !xr->image_count)
        return xr_fail(xr, reason, result, "xrEnumerateSwapchainImages");
    xr->images = (XrSwapchainImageOpenGLKHR *)calloc(xr->image_count, sizeof(*xr->images));
xr->fbos = (GLuint *)calloc(xr->image_count, sizeof(*xr->fbos));
    if (!xr->images || !xr->fbos)
    {
        if (reason) *reason = "OpenXR swapchain image allocation failed";
        xr_destroy_resources (xr);
        return IW_XR_RESULT_FAILED;
    }
    base_images = (XrSwapchainImageBaseHeader *)xr->images;
    for (i = 0; i < xr->image_count; ++i)
        xr->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    result = xr->enumerate_swapchain_images(xr->swapchain, xr->image_count, &xr->image_count, base_images);
    if (result != XR_SUCCESS)
        return xr_fail(xr, reason, result, "xrEnumerateSwapchainImages");
    GL_GenFramebuffersFunc(xr->image_count, xr->fbos);
    for (i = 0; i < xr->image_count; ++i)
    {
        GL_BindFramebufferFunc(GL_FRAMEBUFFER, xr->fbos[i]);
        GL_FramebufferTexture2DFunc(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, xr->images[i].image, 0);
        if (GL_CheckFramebufferStatusFunc(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            if (reason) *reason = "OpenXR swapchain framebuffer incomplete";
                        xr_destroy_resources (xr);
            return IW_XR_RESULT_FAILED;
        }
    }
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
    xr->swapchain_format = swap_info.format;
    for (i = 0; i < 2; ++i)
    {
        if (!xr_create_target(xr, &xr->eyes[i], swap_info.format,
                              q_max(1u, (uint32_t)(view_config_views[i].recommendedImageRectWidth * CLAMP(0.3f, vr_render_scale.value, 2.f) + 0.5f)),
                              q_max(1u, (uint32_t)(view_config_views[i].recommendedImageRectHeight * CLAMP(0.3f, vr_render_scale.value, 2.f) + 0.5f)),
                              view_config_views[i].recommendedSwapchainSampleCount, reason))
        {
            xr_log(xr, "OpenXR stereo target creation failed; flat screen remains available");
            xr_destroy_target(xr, &xr->eyes[0]);
            xr_destroy_target(xr, &xr->eyes[1]);
    xr_destroy_target(xr, &xr->multiview);
    xr_destroy_target(xr, &xr->pointer_target);
            break;
        }
    }
    if (!xr_create_target(xr, &xr->pointer_target, swap_info.format, 256, 64, 1, NULL))
        xr_log(xr, "OpenXR pointer layer unavailable");
    {
        qboolean matching_views = stereo_view_count == 2 && xr->eyes[0].width == xr->eyes[1].width && xr->eyes[0].height == xr->eyes[1].height;
        qboolean extension = xr_has_gl_extension("GL_OVR_multiview2");
        char message[192];

        xr->framebuffer_texture_multiview_ovr = (void (APIENTRYP)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei))SDL_GL_GetProcAddress("glFramebufferTextureMultiviewOVR");
        q_snprintf(message, sizeof(message), "OpenXR multiview prerequisites: requested=%d view_count=%u matching_views=%d extension=%d framebuffer_entry=%d", xr->multiview_requested, stereo_view_count, matching_views, extension, xr->framebuffer_texture_multiview_ovr != NULL);
        xr_log(xr, message);
        if (matching_views && extension && xr->framebuffer_texture_multiview_ovr)
        {
            const char *multiview_reason = NULL;
            xr->multiview_capable = xr_create_multiview_target(xr, swap_info.format, xr->eyes[0].width, xr->eyes[0].height,
                q_min(view_config_views[0].recommendedSwapchainSampleCount, view_config_views[1].recommendedSwapchainSampleCount), &multiview_reason);
            xr_log(xr, xr->multiview_capable ? "OpenXR multiview capability: layered two-view target ready" : multiview_reason);
            xr->multiview_active = xr->multiview_requested && xr->multiview_capable;
            xr_log(xr, xr->multiview_active ? "OpenXR multiview active for gameplay stereo" : "OpenXR multiview disabled; using two-pass stereo");
        }
        else
            xr_log(xr, "OpenXR multiview capability: unavailable; using two-pass stereo");
    }    if (xr->eyes[0].swapchain != XR_NULL_HANDLE && xr->eyes[1].swapchain != XR_NULL_HANDLE)
    {
        char message[192];
        q_snprintf(message, sizeof(message), "OpenXR stereo targets: scale=%.2f, recommended=%ux%u / %ux%u, allocated=%ux%u / %ux%u", CLAMP(0.3f, vr_render_scale.value, 2.f), view_config_views[0].recommendedImageRectWidth, view_config_views[0].recommendedImageRectHeight, view_config_views[1].recommendedImageRectWidth, view_config_views[1].recommendedImageRectHeight, xr->eyes[0].width, xr->eyes[0].height, xr->eyes[1].width, xr->eyes[1].height);
        xr_log(xr, message);
    }    memset(&event, 0, sizeof(event));
    event.type = XR_TYPE_EVENT_DATA_BUFFER;
    while (xr->poll_event(xr->instance, &event) == XR_SUCCESS)
    {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
        {
            XrEventDataSessionStateChanged *changed = (XrEventDataSessionStateChanged *)&event;
            XrSessionState previous_state = xr->session_state;
            xr->session_state = changed->state;
            {
                char message[96];
                q_snprintf (message, sizeof (message), "OpenXR session state=%d", (int)changed->state);
                xr_log (xr, message);
            }
            if (changed->state == XR_SESSION_STATE_READY)
            {
                XrSessionBeginInfo begin_info;
                memset(&begin_info, 0, sizeof(begin_info));
                begin_info.type = XR_TYPE_SESSION_BEGIN_INFO;
                begin_info.primaryViewConfigurationType = xr->view_config_type;
                result = xr->begin_session(xr->session, &begin_info);
                if (result != XR_SUCCESS)
                    return xr_fail(xr, reason, result, "xrBeginSession");
                xr->session_running = true;
            }
            else if (previous_state == XR_SESSION_STATE_FOCUSED && changed->state != XR_SESSION_STATE_FOCUSED)
            {
                IW_XRWin_Haptic (xr, 0, 0.f, 0.f);
                IW_XRWin_Haptic (xr, 1, 0.f, 0.f);
            }
        }
        memset(&event, 0, sizeof(event));
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
    if (!xr->session_running)
    {
        if (reason) *reason = "OpenXR session is not ready";
                xr_destroy_resources (xr);
        return IW_XR_RESULT_UNAVAILABLE;
    }
    return IW_XR_RESULT_OK;
}

iw_xr_result_t IW_XRWin_Pump(iw_xr_win_t *xr, iw_xr_bridge_t *bridge)
{
    XrEventDataBuffer event;
    XrResult result;
    if (!xr || !xr->instance)
        return IW_XR_RESULT_UNAVAILABLE;
    memset(&event, 0, sizeof(event));
    event.type = XR_TYPE_EVENT_DATA_BUFFER;
    while ((result = xr->poll_event(xr->instance, &event)) == XR_SUCCESS)
    {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
        {
            XrEventDataSessionStateChanged *changed = (XrEventDataSessionStateChanged *)&event;
            xr->session_state = changed->state;
            if (changed->state == XR_SESSION_STATE_STOPPING && xr->session_running)
            {
                IW_XRWin_Haptic(xr, 0, 0.f, 0.f);
                IW_XRWin_Haptic(xr, 1, 0.f, 0.f);
                xr->end_session(xr->session);
                xr->session_running = false;
                IW_XRBridge_SetFailure(bridge, IW_XR_RESULT_UNAVAILABLE, "OpenXR session stopped");
            }
            else if (changed->state == XR_SESSION_STATE_LOSS_PENDING)
            {
                IW_XRWin_Haptic(xr, 0, 0.f, 0.f);
                IW_XRWin_Haptic(xr, 1, 0.f, 0.f);
                IW_XRBridge_SetFailure(bridge, IW_XR_RESULT_FAILED, "OpenXR session loss pending");
                return IW_XR_RESULT_FAILED;
            }
        }
    memset(&event, 0, sizeof(event));
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
    return result == XR_EVENT_UNAVAILABLE ? IW_XR_RESULT_OK : IW_XR_RESULT_FAILED;
}

qboolean IW_XRWin_BeginFrame(iw_xr_win_t *xr, iw_xr_frame_snapshot_t *snapshot)
{
    XrFrameWaitInfo wait_info;
    XrFrameBeginInfo begin_info;
    XrResult result;
    if (!xr || !xr->session_running)
    {
        if (xr && !xr->logged_wait_failure)
        {
            xr_log (xr, "OpenXR frame skipped because session is not running");
            xr->logged_wait_failure = true;
        }
        return false;
    }
    memset (&xr->frame_state, 0, sizeof (xr->frame_state));
    xr->frame_state.type = XR_TYPE_FRAME_STATE;
    memset(&wait_info, 0, sizeof(wait_info));
    wait_info.type = XR_TYPE_FRAME_WAIT_INFO;
    result = xr->wait_frame(xr->session, &wait_info, &xr->frame_state);
    if (result != XR_SUCCESS)
    {
        char message[96];
        q_snprintf (message, sizeof (message), "OpenXR xrWaitFrame failed (%d)", (int)result);
        xr_log (xr, message);
        return false;
    }
    memset(&begin_info, 0, sizeof(begin_info));
    begin_info.type = XR_TYPE_FRAME_BEGIN_INFO;
    result = xr->begin_frame(xr->session, &begin_info);
    if (result != XR_SUCCESS)
    {
        char message[96];
        q_snprintf (message, sizeof (message), "OpenXR xrBeginFrame failed (%d)", (int)result);
        xr_log (xr, message);
        return false;
    }
    xr->frame_running = true;
    xr_update_actions(xr);
    xr->views_valid = false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->predicted_display_time = (uint64_t)xr->frame_state.predictedDisplayTime;
    snapshot->predicted_display_period = (uint64_t)xr->frame_state.predictedDisplayPeriod;
    snapshot->should_render = xr->frame_state.shouldRender != 0;
    if (snapshot->should_render && xr->locate_views)
    {
        XrViewLocateInfo locate_info;
        XrViewState view_state;
        XrView views[2];
        uint32_t view_count = 0;
        uint32_t i;
        memset (&locate_info, 0, sizeof (locate_info));
        locate_info.type = XR_TYPE_VIEW_LOCATE_INFO;
        locate_info.viewConfigurationType = xr->view_config_type;
        locate_info.displayTime = xr->frame_state.predictedDisplayTime;
        locate_info.space = xr->space;
        memset (&view_state, 0, sizeof (view_state));
        view_state.type = XR_TYPE_VIEW_STATE;
        memset (views, 0, sizeof (views));
        for (i = 0; i < Q_COUNTOF (views); ++i)
            views[i].type = XR_TYPE_VIEW;
        if (xr->locate_views (xr->session, &locate_info, &view_state,
                              Q_COUNTOF (views), &view_count, views) == XR_SUCCESS)
        {
            if (view_count > 0 &&
                (view_state.viewStateFlags & (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT)) ==
                (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT))
                xr_update_screen_pose (xr, views, view_count);
            snapshot->view_count = q_min (view_count, (uint32_t)Q_COUNTOF (snapshot->views));
            for (i = 0; i < snapshot->view_count; ++i)
            {
                snapshot->views[i].position[0] = views[i].pose.position.x;
                snapshot->views[i].position[1] = views[i].pose.position.y;
                snapshot->views[i].position[2] = views[i].pose.position.z;
                snapshot->views[i].orientation[0] = views[i].pose.orientation.x;
                snapshot->views[i].orientation[1] = views[i].pose.orientation.y;
                snapshot->views[i].orientation[2] = views[i].pose.orientation.z;
                snapshot->views[i].orientation[3] = views[i].pose.orientation.w;
                snapshot->views[i].fov.left = views[i].fov.angleLeft;
                snapshot->views[i].fov.right = views[i].fov.angleRight;
                snapshot->views[i].fov.up = views[i].fov.angleUp;
                snapshot->views[i].fov.down = views[i].fov.angleDown;
            }
        }
    }
    xr->views_valid = snapshot->view_count >= 2;
    xr->frame_snapshot = *snapshot;
    return true;
}

static qboolean xr_action_bool(iw_xr_win_t *xr, XrAction action, XrPath hand)
{
    XrActionStateGetInfo info;
    XrActionStateBoolean state;
    memset(&info, 0, sizeof(info));
    memset(&state, 0, sizeof(state));
    info.type = XR_TYPE_ACTION_STATE_GET_INFO;
    info.action = action;
    info.subactionPath = hand;
    state.type = XR_TYPE_ACTION_STATE_BOOLEAN;
    return xr->get_action_state_boolean(xr->session, &info, &state) == XR_SUCCESS && state.isActive && state.currentState;
}

static float xr_action_float(iw_xr_win_t *xr, XrAction action, XrPath hand)
{
    XrActionStateGetInfo info;
    XrActionStateFloat state;
    memset(&info, 0, sizeof(info));
    memset(&state, 0, sizeof(state));
    info.type = XR_TYPE_ACTION_STATE_GET_INFO;
    info.action = action;
    info.subactionPath = hand;
    state.type = XR_TYPE_ACTION_STATE_FLOAT;
    if (xr->get_action_state_float(xr->session, &info, &state) != XR_SUCCESS || !state.isActive) return 0.0f;
    return state.currentState;
}

static qboolean xr_action_pose_active(iw_xr_win_t *xr, XrAction action, XrPath hand)
{
    XrActionStateGetInfo info;
    XrActionStatePose state;
    memset(&info, 0, sizeof(info));
    memset(&state, 0, sizeof(state));
    info.type = XR_TYPE_ACTION_STATE_GET_INFO;
    info.action = action;
    info.subactionPath = hand;
    state.type = XR_TYPE_ACTION_STATE_POSE;
    return xr->get_action_state_pose(xr->session, &info, &state) == XR_SUCCESS && state.isActive;
}

static void xr_action_vec2(iw_xr_win_t *xr, XrAction action, XrPath hand, float out[2])
{
    XrActionStateGetInfo info;
    XrActionStateVector2f state;
    out[0] = out[1] = 0.0f;
    memset(&info, 0, sizeof(info));
    memset(&state, 0, sizeof(state));
    info.type = XR_TYPE_ACTION_STATE_GET_INFO;
    info.action = action;
    info.subactionPath = hand;
    state.type = XR_TYPE_ACTION_STATE_VECTOR2F;
    if (xr->get_action_state_vector2f(xr->session, &info, &state) == XR_SUCCESS && state.isActive)
    {
        out[0] = state.currentState.x;
        out[1] = state.currentState.y;
    }
}

static void xr_update_actions(iw_xr_win_t *xr)
{
    XrActiveActionSet active_set;
    XrActionsSyncInfo sync_info;
    int hand;
    memset(&xr->actions, 0, sizeof(xr->actions));
    if (!xr->session_running || xr->action_set == XR_NULL_HANDLE || xr->session_state != XR_SESSION_STATE_FOCUSED) return;
    memset(&active_set, 0, sizeof(active_set));
    active_set.actionSet = xr->action_set;
    memset(&sync_info, 0, sizeof(sync_info));
    sync_info.type = XR_TYPE_ACTIONS_SYNC_INFO;
    sync_info.countActiveActionSets = 1;
    sync_info.activeActionSets = &active_set;
    if (xr->sync_actions(xr->session, &sync_info) != XR_SUCCESS) return;
    for (hand = 0; hand < 2; ++hand)
    {
        iw_xr_hand_snapshot_t *snapshot = &xr->actions.hand[hand];
        XrSpaceLocation location;
        XrSpaceVelocity aim_velocity, grip_velocity;
        float trackpad[2];
        snapshot->trigger = xr_action_float(xr, xr->trigger_action, xr->hand_paths[hand]);
        snapshot->grip = xr_action_float(xr, xr->grip_action, xr->hand_paths[hand]);
        xr_action_vec2(xr, xr->stick_action, xr->hand_paths[hand], snapshot->stick);
        xr_action_vec2(xr, xr->trackpad_action, xr->hand_paths[hand], trackpad);
        if (snapshot->stick[0] == 0.0f && snapshot->stick[1] == 0.0f)
        {
            snapshot->stick[0] = trackpad[0];
            snapshot->stick[1] = trackpad[1];
        }
        if (xr_action_bool(xr, xr->trigger_click_action, xr->hand_paths[hand])) snapshot->trigger = 1.0f;
        if (xr_action_bool(xr, xr->grip_click_action, xr->hand_paths[hand])) snapshot->grip = 1.0f;
        if (snapshot->trigger > 0.5f) snapshot->buttons |= IW_XR_BUTTON_TRIGGER;
        if (snapshot->grip > 0.5f) snapshot->buttons |= IW_XR_BUTTON_GRIP;
        if (xr_action_bool(xr, xr->stick_click_action, xr->hand_paths[hand])) snapshot->buttons |= IW_XR_BUTTON_STICK;
        if (xr_action_bool(xr, xr->primary_action, xr->hand_paths[hand])) snapshot->buttons |= IW_XR_BUTTON_PRIMARY;
        if (xr_action_bool(xr, xr->secondary_action, xr->hand_paths[hand])) snapshot->buttons |= IW_XR_BUTTON_SECONDARY;
        if (xr_action_bool(xr, xr->menu_action, xr->hand_paths[hand])) snapshot->buttons |= IW_XR_BUTTON_MENU;
        memset(&location, 0, sizeof(location));
        location.type = XR_TYPE_SPACE_LOCATION;
        memset(&aim_velocity, 0, sizeof(aim_velocity));
        aim_velocity.type = XR_TYPE_SPACE_VELOCITY;
        location.next = &aim_velocity;
        if (xr_action_pose_active(xr, xr->aim_action, xr->hand_paths[hand]) &&
            xr->locate_space(xr->aim_spaces[hand], xr->space, xr->frame_state.predictedDisplayTime, &location) == XR_SUCCESS &&
            (location.locationFlags & (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) ==
            (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        {
            snapshot->aim_valid = true;
            snapshot->aim_position[0] = location.pose.position.x; snapshot->aim_position[1] = location.pose.position.y; snapshot->aim_position[2] = location.pose.position.z;
            snapshot->aim_orientation[0] = location.pose.orientation.x; snapshot->aim_orientation[1] = location.pose.orientation.y; snapshot->aim_orientation[2] = location.pose.orientation.z; snapshot->aim_orientation[3] = location.pose.orientation.w;
        }
        memset(&location, 0, sizeof(location));
        location.type = XR_TYPE_SPACE_LOCATION;
        memset(&grip_velocity, 0, sizeof(grip_velocity));
        grip_velocity.type = XR_TYPE_SPACE_VELOCITY;
        location.next = &grip_velocity;
        if (xr_action_pose_active(xr, xr->grip_pose_action, xr->hand_paths[hand]) &&
            xr->locate_space(xr->grip_spaces[hand], xr->space, xr->frame_state.predictedDisplayTime, &location) == XR_SUCCESS &&
            (location.locationFlags & (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) ==
            (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        {
            snapshot->grip_valid = true;
            snapshot->grip_position[0] = location.pose.position.x; snapshot->grip_position[1] = location.pose.position.y; snapshot->grip_position[2] = location.pose.position.z;
            snapshot->grip_orientation[0] = location.pose.orientation.x; snapshot->grip_orientation[1] = location.pose.orientation.y; snapshot->grip_orientation[2] = location.pose.orientation.z; snapshot->grip_orientation[3] = location.pose.orientation.w;
        }
        if ((grip_velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0)
        {
            snapshot->velocity_valid = true;
            snapshot->linear_velocity[0] = grip_velocity.linearVelocity.x;
            snapshot->linear_velocity[1] = grip_velocity.linearVelocity.y;
            snapshot->linear_velocity[2] = grip_velocity.linearVelocity.z;
        }
        else if ((aim_velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0)
        {
            snapshot->velocity_valid = true;
            snapshot->linear_velocity[0] = aim_velocity.linearVelocity.x;
            snapshot->linear_velocity[1] = aim_velocity.linearVelocity.y;
            snapshot->linear_velocity[2] = aim_velocity.linearVelocity.z;
        }
        snapshot->active = snapshot->aim_valid || snapshot->grip_valid || snapshot->trigger != 0.0f || snapshot->grip != 0.0f || snapshot->stick[0] != 0.0f || snapshot->stick[1] != 0.0f || snapshot->buttons != 0;
        xr->actions.active |= snapshot->active;
    }
}

qboolean IW_XRWin_GetActions(const iw_xr_win_t *xr, iw_xr_action_snapshot_t *actions)
{
    if (!xr || !actions) return false;
    *actions = xr->actions;
    return actions->active;
}
static qboolean xr_get_virtual_screen(const iw_xr_win_t *xr, iw_xr_virtual_screen_t *screen)
{
    XrPosef pose;
    if (!xr || !screen || !xr->screen_follow.valid) return false;
    memset(screen, 0, sizeof(*screen));
    pose = xr->screen_pose;
    screen->width = 2.97f * xr->screen_scale;
    screen->height = 2.2275f * xr->screen_scale;
    screen->curved = xr->curved_screen && xr->cylinder_supported && xr->curve_radius > 1.2f;
    screen->curve_radius = screen->curved ? xr->curve_radius : 0.f;
    screen->position[0] = pose.position.x;
    screen->position[1] = pose.position.y;
    screen->position[2] = pose.position.z;
    screen->orientation[0] = pose.orientation.x;
    screen->orientation[1] = pose.orientation.y;
    screen->orientation[2] = pose.orientation.z;
    screen->orientation[3] = pose.orientation.w;
    return true;
}

qboolean IW_XRWin_RaycastVirtualScreen(const iw_xr_win_t *xr, const float origin[3], const float orientation[4], iw_xr_virtual_screen_hit_t *hit)
{
    iw_xr_virtual_screen_t screen;
    if (!xr_get_virtual_screen(xr, &screen)) {
        if (hit) memset(hit, 0, sizeof(*hit));
        return false;
    }
    return IW_XRVirtualScreen_Raycast(&screen, origin, orientation, hit);
}
void IW_XRWin_SetVirtualPointer(iw_xr_win_t *xr, const float start[3], const float hit[3], qboolean active, unsigned color, float alpha, float width)
{
    if (!xr) return;
    xr->pointer_active = active && start && hit;
    if (xr->pointer_active) {
        memcpy(xr->pointer_start, start, sizeof(xr->pointer_start));
        memcpy(xr->pointer_hit, hit, sizeof(xr->pointer_hit));
        xr->pointer_color = color;
        xr->pointer_alpha = alpha;
        xr->pointer_width = width;
    }
}

void IW_XRWin_Haptic(iw_xr_win_t *xr, int hand, float amplitude, float duration_seconds)
{
    XrHapticActionInfo info;
    XrHapticVibration vibration;
    if (!xr || hand < 0 || hand >= IW_XR_HAND_COUNT || xr->session == XR_NULL_HANDLE || xr->haptic_action == XR_NULL_HANDLE || !xr->apply_haptic_feedback)
        return;
    memset(&info, 0, sizeof(info));
    memset(&vibration, 0, sizeof(vibration));
        info.type = XR_TYPE_HAPTIC_ACTION_INFO;
    info.action = xr->haptic_action;
    info.subactionPath = xr->hand_paths[hand];
    if (amplitude <= 0.f || duration_seconds <= 0.f) {
        if (xr->stop_haptic_feedback) xr->stop_haptic_feedback(xr->session, &info);
        return;
    }
    vibration.type = XR_TYPE_HAPTIC_VIBRATION;
    vibration.amplitude = CLAMP(0.f, amplitude, 1.f);
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    vibration.duration = (XrDuration)(CLAMP(0.001f, duration_seconds, 2.f) * 1000000000.0f);
    xr->apply_haptic_feedback(xr->session, &info, (const XrHapticBaseHeader *)&vibration);
}
qboolean IW_XRWin_HasStereoTargets(const iw_xr_win_t *xr)
{
    return xr && xr->eyes[0].swapchain != XR_NULL_HANDLE && xr->eyes[1].swapchain != XR_NULL_HANDLE;
}

static void xr_apply_refresh_rate(iw_xr_win_t *xr)
{
    float rates[16], current = 0.f;
    uint32_t count = 0, i;
    XrResult result;
    char message[256], *cursor = message;
    if (!xr || !xr->refresh_rate_supported || xr->session == XR_NULL_HANDLE || !xr->enumerate_display_refresh_rates || !xr->request_display_refresh_rate)
        return;
    result = xr->enumerate_display_refresh_rates(xr->session, Q_COUNTOF(rates), &count, rates);
    if (result != XR_SUCCESS || !count) { xr_log(xr, "OpenXR refresh-rate query failed; runtime default retained"); return; }
    count = q_min(count, (uint32_t)Q_COUNTOF(rates));
    cursor += q_snprintf(cursor, sizeof(message), "OpenXR refresh rates:");
    for (i = 0; i < count && cursor < message + sizeof(message); ++i) cursor += q_snprintf(cursor, (size_t)(message + sizeof(message) - cursor), " %.0f", rates[i]);
    xr_log(xr, message);
    for (i = 0; i < count; ++i) if (fabsf(rates[i] - xr->requested_refresh_rate) < 0.01f) break;
    if (i == count) { q_snprintf(message, sizeof(message), "OpenXR refresh rate %.0f Hz unsupported; runtime default retained", xr->requested_refresh_rate); xr_log(xr, message); return; }
    result = xr->request_display_refresh_rate(xr->session, rates[i]);
    if (result == XR_SUCCESS) { xr->applied_refresh_rate = rates[i]; q_snprintf(message, sizeof(message), "OpenXR requested refresh rate %.0f Hz", rates[i]); }
    else q_snprintf(message, sizeof(message), "OpenXR refresh-rate request %.0f Hz failed (%d)", rates[i], (int)result);
    xr_log(xr, message);
    if (xr->get_display_refresh_rate && xr->get_display_refresh_rate(xr->session, &current) == XR_SUCCESS) { q_snprintf(message, sizeof(message), "OpenXR current refresh rate %.0f Hz", current); xr_log(xr, message); }
}
void IW_XRWin_SetRefreshRate(iw_xr_win_t *xr, float requested_hz)
{
    if (!xr) return;
    xr->requested_refresh_rate = requested_hz;
    xr_apply_refresh_rate(xr);
}

void IW_XRWin_SetMultiviewRequested(iw_xr_win_t *xr, qboolean requested)
{
    if (!xr) return;
    xr->multiview_requested = requested;
    xr->multiview_active = requested && xr->multiview_capable;
    xr_log(xr, xr->multiview_active ? "OpenXR multiview active for gameplay stereo" : (requested ? "OpenXR multiview requested; awaiting capability check" : "OpenXR multiview disabled; using two-pass stereo"));
}

qboolean IW_XRWin_UsingMultiview(const iw_xr_win_t *xr)
{
    return xr && xr->multiview_active;
}
qboolean IW_XRWin_BeginMultiviewTarget(iw_xr_win_t *xr, unsigned *fbo, int *width, int *height)
{
    iw_xr_gl_target_t *target;
    if (!xr || !xr->multiview_active)
        return false;
    if (!xr_acquire_target(xr, &xr->multiview))
    {
        xr->multiview_active = false;
        xr_log(xr, "OpenXR multiview target acquire failed; using two-pass stereo");
        return false;
    }
    target = &xr->multiview;
    if (fbo) *fbo = target->fbos[target->image_index];
    if (width) *width = (int)target->width;
    if (height) *height = (int)target->height;
    return true;
}

qboolean IW_XRWin_BeginMultiviewOverlayEye(iw_xr_win_t *xr, unsigned eye, unsigned *fbo, int *width, int *height)
{
    iw_xr_gl_target_t *target;
    if (!xr || eye >= 2) return false;
    target = &xr->multiview;
    if (!target->acquired) return false;
    if (!target->mirror_fbo) GL_GenFramebuffersFunc(1, &target->mirror_fbo);
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, target->mirror_fbo);
    GL_FramebufferTextureLayerFunc(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target->images[target->image_index].image, 0, (GLint)eye);
    if (GL_CheckFramebufferStatusFunc(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    if (fbo) *fbo = target->mirror_fbo;
    if (width) *width = (int)target->width;
    if (height) *height = (int)target->height;
    return true;
}

void IW_XRWin_EndMultiviewTarget(iw_xr_win_t *xr)
{
    /* EndFrame releases the layered image after selecting it for projection submission. */
    (void)xr;
}
qboolean IW_XRWin_BindEyeTarget(iw_xr_win_t *xr, unsigned eye)
{
    return xr && eye < 2 && xr_acquire_target(xr, &xr->eyes[eye]);
}

qboolean IW_XRWin_GetEyeTarget(const iw_xr_win_t *xr, unsigned eye, unsigned *fbo, int *width, int *height)
{
    const iw_xr_gl_target_t *target;
    if (!xr || eye >= 2) return false;
    target = &xr->eyes[eye];
    if (!target->acquired) return false;
    if (fbo) *fbo = target->fbos[target->image_index];
    if (width) *width = (int)target->width;
    if (height) *height = (int)target->height;
    return true;
}

void IW_XRWin_MirrorMultiview(iw_xr_win_t *xr, int width, int height)
{
    iw_xr_gl_target_t *target;
    if (!xr || width <= 0 || height <= 0) return;
    target = &xr->multiview;
    if (!target->acquired) return;
    if (!target->mirror_fbo) GL_GenFramebuffersFunc(1, &target->mirror_fbo);
    GL_BindFramebufferFunc(GL_READ_FRAMEBUFFER, target->mirror_fbo);
    GL_FramebufferTextureLayerFunc(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target->images[target->image_index].image, 0, 0);
    if (GL_CheckFramebufferStatusFunc(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    {
        GL_BindFramebufferFunc(GL_DRAW_FRAMEBUFFER, 0);
        GL_BlitFramebufferFunc(0, 0, target->width, target->height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
}

void IW_XRWin_MirrorEye(iw_xr_win_t *xr, unsigned eye, int width, int height)
{
    iw_xr_gl_target_t *target;
    if (!xr || eye >= 2 || width <= 0 || height <= 0) return;
    target = &xr->eyes[eye];
    if (!target->acquired) return;
    GL_BindFramebufferFunc(GL_READ_FRAMEBUFFER, target->fbos[target->image_index]);
    GL_BindFramebufferFunc(GL_DRAW_FRAMEBUFFER, 0);
    GL_BlitFramebufferFunc(0, 0, target->width, target->height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
}
qboolean IW_XRWin_ResolveEyeTarget(iw_xr_win_t *xr, unsigned eye, int source_width, int source_height)
{
    iw_xr_gl_target_t *target;
    GLboolean framebuffer_srgb_was_enabled = GL_FALSE;
    if (!xr || eye >= 2 || source_width <= 0 || source_height <= 0) return false;
    target = &xr->eyes[eye]; if (!target->acquired) return false;
#ifdef GL_FRAMEBUFFER_SRGB
    if (xr->swapchain_is_srgb) { framebuffer_srgb_was_enabled = glIsEnabled(GL_FRAMEBUFFER_SRGB); glDisable(GL_FRAMEBUFFER_SRGB); }
#endif
    GL_BindFramebufferFunc(GL_READ_FRAMEBUFFER, 0); GL_BindFramebufferFunc(GL_DRAW_FRAMEBUFFER, target->fbos[target->image_index]);
    GL_BlitFramebufferFunc(0, 0, source_width, source_height, 0, 0, target->width, target->height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
#ifdef GL_FRAMEBUFFER_SRGB
    if (framebuffer_srgb_was_enabled) glEnable(GL_FRAMEBUFFER_SRGB);
#endif
    return true;
}

void IW_XRWin_SetStereoSubmission(iw_xr_win_t *xr, qboolean enabled)
{
    if (xr) xr->stereo_submission = enabled && IW_XRWin_HasStereoTargets(xr) && xr->views_valid;
}
qboolean IW_XRWin_ResolveDefaultFramebuffer(iw_xr_win_t *xr, int source_width, int source_height)
{
    GLboolean framebuffer_srgb_was_enabled = GL_FALSE;
    if (!xr || !xr->image_acquired || source_width <= 0 || source_height <= 0)
        return false;
#ifdef GL_FRAMEBUFFER_SRGB
    if (xr->swapchain_is_srgb)
    {
        framebuffer_srgb_was_enabled = glIsEnabled (GL_FRAMEBUFFER_SRGB);
        glDisable (GL_FRAMEBUFFER_SRGB);
    }
#endif
    GL_BindFramebufferFunc (GL_READ_FRAMEBUFFER, 0);
    GL_BindFramebufferFunc (GL_DRAW_FRAMEBUFFER, xr->fbos[xr->image_index]);
    GL_BlitFramebufferFunc (0, 0, source_width, source_height,
                            0, 0, (GLint)xr->width, (GLint)xr->height,
                            GL_COLOR_BUFFER_BIT, GL_LINEAR);
    GL_BindFramebufferFunc (GL_FRAMEBUFFER, 0);
#ifdef GL_FRAMEBUFFER_SRGB
    if (framebuffer_srgb_was_enabled)
        glEnable (GL_FRAMEBUFFER_SRGB);
#endif
    return true;
}

qboolean IW_XRWin_BindFrameTarget(iw_xr_win_t *xr)
{
    XrSwapchainImageAcquireInfo acquire_info;
    XrSwapchainImageWaitInfo wait_info;
    if (!xr || !xr->frame_running || !xr->frame_state.shouldRender)
        return false;
    if (xr->image_acquired)
    {
        GL_BindFramebufferFunc(GL_DRAW_FRAMEBUFFER, xr->fbos[xr->image_index]);
        return true;
    }
    memset(&acquire_info, 0, sizeof(acquire_info));
    acquire_info.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    if (xr->acquire_swapchain_image(xr->swapchain, &acquire_info, &xr->image_index) != XR_SUCCESS)
        return false;
    memset(&wait_info, 0, sizeof(wait_info));
    wait_info.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    wait_info.timeout = XR_INFINITE_DURATION;
    if (xr->wait_swapchain_image (xr->swapchain, &wait_info) != XR_SUCCESS)
    {
        XrSwapchainImageReleaseInfo release_info;
        memset (&release_info, 0, sizeof (release_info));
        release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        xr->release_swapchain_image (xr->swapchain, &release_info);
        return false;
    }
    xr->image_acquired = true;
    GL_BindFramebufferFunc(GL_FRAMEBUFFER, xr->fbos[xr->image_index]);
    return true;
}

qboolean IW_XRWin_GetFrameTarget(const iw_xr_win_t *xr, unsigned *fbo, int *width, int *height)
{
    if (!xr || !xr->image_acquired)
        return false;
    if (fbo) *fbo = xr->fbos[xr->image_index];
    if (width) *width = (int)xr->width;
    if (height) *height = (int)xr->height;
    return true;
}
static qboolean xr_submit_screen_skybox(iw_xr_win_t *xr, qboolean submit)
{
    XrCompositionLayerProjection projection; XrCompositionLayerProjectionView views[2]; XrCompositionLayerBaseHeader *layers[2]; XrCompositionLayerQuad pointer; XrFrameEndInfo end; iw_xr_virtual_screen_t screen; uint32_t i;
    if (!xr || !vr_screen_skybox.value || !xr->image_acquired || !xr_get_virtual_screen(xr, &screen) || !IW_XRWin_HasStereoTargets(xr)) return false;
    for (i=0;i<2;i++) { if (!xr_acquire_target(xr,&xr->eyes[i]) || !IW_XRVirtualEnvironment_Render(&xr->frame_snapshot.views[i],&screen,xr->images[xr->image_index].image,xr->eyes[i].fbos[xr->eyes[i].image_index],xr->eyes[i].width,xr->eyes[i].height)) { xr_release_target(xr,&xr->eyes[i]); while(i--) xr_release_target(xr,&xr->eyes[i]); return false; } }
    for(i=0;i<2;i++) xr_release_target(xr,&xr->eyes[i]); { XrSwapchainImageReleaseInfo release={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO}; xr->release_swapchain_image(xr->swapchain,&release); xr->image_acquired=false; }
    memset(&projection,0,sizeof(projection)); memset(views,0,sizeof(views)); projection.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION; projection.space=xr->space; projection.viewCount=2; projection.views=views;
    for(i=0;i<2;i++){ const iw_xr_view_t *in=&xr->frame_snapshot.views[i]; views[i].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW; views[i].pose.position.x=in->position[0];views[i].pose.position.y=in->position[1];views[i].pose.position.z=in->position[2];views[i].pose.orientation.x=in->orientation[0];views[i].pose.orientation.y=in->orientation[1];views[i].pose.orientation.z=in->orientation[2];views[i].pose.orientation.w=in->orientation[3];views[i].fov.angleLeft=in->fov.left;views[i].fov.angleRight=in->fov.right;views[i].fov.angleUp=in->fov.up;views[i].fov.angleDown=in->fov.down;views[i].subImage.swapchain=xr->eyes[i].swapchain;views[i].subImage.imageRect.extent.width=xr->eyes[i].width;views[i].subImage.imageRect.extent.height=xr->eyes[i].height;}
    layers[0]=(XrCompositionLayerBaseHeader*)&projection; if (xr->pointer_active && xr_acquire_target(xr,&xr->pointer_target)) { GL_BindFramebufferFunc(GL_FRAMEBUFFER,xr->pointer_target.fbos[xr->pointer_target.image_index]); glViewport(0,0,xr->pointer_target.width,xr->pointer_target.height); glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT); glEnable(GL_SCISSOR_TEST); glScissor(0,26,xr->pointer_target.width,12); glClearColor(((xr->pointer_color>>16)&255)/255.f,((xr->pointer_color>>8)&255)/255.f,(xr->pointer_color&255)/255.f,xr->pointer_alpha); glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST); xr_release_target(xr,&xr->pointer_target); memset(&pointer,0,sizeof(pointer)); pointer.type=XR_TYPE_COMPOSITION_LAYER_QUAD; pointer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT; pointer.space=xr->space; pointer.eyeVisibility=XR_EYE_VISIBILITY_BOTH; pointer.subImage.swapchain=xr->pointer_target.swapchain; pointer.subImage.imageRect.extent.width=xr->pointer_target.width; pointer.subImage.imageRect.extent.height=xr->pointer_target.height; pointer.pose=xr_pointer_pose(xr->pointer_start,xr->pointer_hit,xr->frame_snapshot.views[0].position); pointer.size.width=sqrtf((xr->pointer_hit[0]-xr->pointer_start[0])*(xr->pointer_hit[0]-xr->pointer_start[0])+(xr->pointer_hit[1]-xr->pointer_start[1])*(xr->pointer_hit[1]-xr->pointer_start[1])+(xr->pointer_hit[2]-xr->pointer_start[2])*(xr->pointer_hit[2]-xr->pointer_start[2])); pointer.size.height=.012f*CLAMP(.25f,xr->pointer_width,8.f); layers[1]=(XrCompositionLayerBaseHeader*)&pointer; } memset(&end,0,sizeof(end));end.type=XR_TYPE_FRAME_END_INFO;end.displayTime=xr->frame_state.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=submit&&xr->frame_state.shouldRender?(xr->pointer_active?2:1):0;end.layers=end.layerCount?layers:NULL;xr->frame_running=false;return xr->end_frame(xr->session,&end)==XR_SUCCESS;
}
iw_xr_result_t IW_XRWin_EndFrame(iw_xr_win_t *xr, qboolean submit)
{
    XrResult result;
    XrSwapchainImageReleaseInfo release_info;
    XrCompositionLayerQuad quad;
    XrCompositionLayerCylinderKHR cylinder;
    XrCompositionLayerBaseHeader *layers[3];
    XrCompositionLayerQuad pointer;
    XrFrameEndInfo end_info;
    qboolean submit_image;
    uint32_t layer_count = 0;
    if (!xr || !xr->frame_running)
        return IW_XR_RESULT_INVALID;    if (xr->stereo_submission)
    {
        XrCompositionLayerProjection projection;
        XrCompositionLayerProjectionView projection_views[2];
        XrCompositionLayerQuad hud;
        XrCompositionLayerBaseHeader *layers[3];
        XrFrameEndInfo projection_end;
        uint32_t i, layer_count = 0;
        qboolean multiview_submit = xr->multiview_active && xr->multiview.acquired;
        qboolean stereo_submit = multiview_submit || (xr->eyes[0].acquired && xr->eyes[1].acquired);
        qboolean hud_submit = xr->image_acquired;

        for (i = 0; i < 2; ++i)
            xr_release_target (xr, &xr->eyes[i]);
        if (xr->image_acquired)
        {
            memset (&release_info, 0, sizeof (release_info));
            release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
            xr->release_swapchain_image (xr->swapchain, &release_info);
            xr->image_acquired = false;
        }

        memset (&projection, 0, sizeof (projection));
        memset (projection_views, 0, sizeof (projection_views));
        projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        projection.space = xr->space;
        projection.viewCount = 2;
        projection.views = projection_views;
        for (i = 0; i < 2; ++i)
        {
            projection_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projection_views[i].pose.position.x = xr->frame_snapshot.views[i].position[0];
            projection_views[i].pose.position.y = xr->frame_snapshot.views[i].position[1];
            projection_views[i].pose.position.z = xr->frame_snapshot.views[i].position[2];
            projection_views[i].pose.orientation.x = xr->frame_snapshot.views[i].orientation[0];
            projection_views[i].pose.orientation.y = xr->frame_snapshot.views[i].orientation[1];
            projection_views[i].pose.orientation.z = xr->frame_snapshot.views[i].orientation[2];
            projection_views[i].pose.orientation.w = xr->frame_snapshot.views[i].orientation[3];
            projection_views[i].fov.angleLeft = xr->frame_snapshot.views[i].fov.left;
            projection_views[i].fov.angleRight = xr->frame_snapshot.views[i].fov.right;
            projection_views[i].fov.angleUp = xr->frame_snapshot.views[i].fov.up;
            projection_views[i].fov.angleDown = xr->frame_snapshot.views[i].fov.down;
            projection_views[i].subImage.swapchain = multiview_submit ? xr->multiview.swapchain : xr->eyes[i].swapchain;
            projection_views[i].subImage.imageArrayIndex = multiview_submit ? i : 0;
            projection_views[i].subImage.imageRect.extent.width = multiview_submit ? xr->multiview.width : xr->eyes[i].width;
            projection_views[i].subImage.imageRect.extent.height = multiview_submit ? xr->multiview.height : xr->eyes[i].height;
        }
        layers[layer_count++] = (XrCompositionLayerBaseHeader *)&projection;

        if (hud_submit)
        {
            memset (&hud, 0, sizeof (hud));
            hud.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
            hud.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            hud.space = xr->space;
            hud.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            hud.subImage.swapchain = xr->swapchain;
            hud.subImage.imageRect.extent.width = (int32_t)xr->width;
            hud.subImage.imageRect.extent.height = (int32_t)xr->height;
            hud.pose = xr->hud_pose;
            hud.size.width = 1.8f * xr->hud_scale;
            hud.size.height = hud.size.width * (float)xr->height / (float)xr->width;
            layers[layer_count++] = (XrCompositionLayerBaseHeader *)&hud;
        }

        if (multiview_submit)
            xr_release_target (xr, &xr->multiview);

                memset (&projection_end, 0, sizeof (projection_end));
        projection_end.type = XR_TYPE_FRAME_END_INFO;
        projection_end.displayTime = xr->frame_state.predictedDisplayTime;
        projection_end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        projection_end.layerCount = submit && stereo_submit && xr->frame_state.shouldRender ? layer_count : 0;
        projection_end.layers = projection_end.layerCount ? layers : NULL;
        result = xr->end_frame (xr->session, &projection_end);
        xr->frame_running = false;
        xr->stereo_submission = false;
        return result == XR_SUCCESS ? IW_XR_RESULT_OK : IW_XR_RESULT_FAILED;
    }
    if (xr_submit_screen_skybox(xr, submit)) return IW_XR_RESULT_OK;
    if (!xr->frame_running) return IW_XR_RESULT_FAILED;
    submit_image = xr->image_acquired;
    if (xr->image_acquired)
    {
        memset(&release_info, 0, sizeof(release_info));
        release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        xr->release_swapchain_image(xr->swapchain, &release_info);
        xr->image_acquired = false;
    }
    memset(&quad, 0, sizeof(quad));
    quad.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    quad.space = xr->space;
    quad.layerFlags = 0;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = xr->swapchain;
    quad.subImage.imageRect.offset.x = 0;
    quad.subImage.imageRect.offset.y = 0;
    quad.subImage.imageRect.extent.width = (int32_t)xr->width;
    quad.subImage.imageRect.extent.height = (int32_t)xr->height;
    quad.pose = xr->screen_pose;
    quad.size.width = 2.97f * xr->screen_scale;
    quad.size.height = 2.2275f * xr->screen_scale;
    if (xr->curved_screen && xr->cylinder_supported && xr->curve_radius > 1.2f)
    {
        if (!xr->curve_submission_logged)
        {
            char curve_log[160];
            q_snprintf (curve_log, sizeof (curve_log), "OpenXR submitting cylinder layer: radius=%.2f angle=%.2f", xr->curve_radius, quad.size.width / xr->curve_radius);
            xr_log (xr, curve_log);
            xr->curve_submission_logged = true;
        }
        memset (&cylinder, 0, sizeof (cylinder));
        cylinder.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
        cylinder.space = xr->space;
        cylinder.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        cylinder.subImage = quad.subImage;
        cylinder.pose = xr_cylinder_pose(&quad.pose, xr->curve_radius);
        cylinder.radius = xr->curve_radius;
        cylinder.centralAngle = quad.size.width / xr->curve_radius;
        cylinder.aspectRatio = quad.size.width / quad.size.height;
        layers[layer_count++] = (XrCompositionLayerBaseHeader *)&cylinder;
    }
    else
    {
        if (!xr->curve_submission_logged)
        {
            char curve_log[160];
            q_snprintf (curve_log, sizeof (curve_log), "OpenXR submitting flat layer: requested=%d cylinder_supported=%d radius=%.2f", xr->curved_screen, xr->cylinder_supported, xr->curve_radius);
            xr_log (xr, curve_log);
            xr->curve_submission_logged = true;
        }
        layers[layer_count++] = (XrCompositionLayerBaseHeader *)&quad;
    }
    if (xr->pointer_active && xr_acquire_target(xr, &xr->pointer_target))
    {
        GL_BindFramebufferFunc(GL_FRAMEBUFFER, xr->pointer_target.fbos[xr->pointer_target.image_index]);
        glViewport(0, 0, xr->pointer_target.width, xr->pointer_target.height);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 26, xr->pointer_target.width, 12);
        glClearColor(((xr->pointer_color >> 16) & 255) / 255.f, ((xr->pointer_color >> 8) & 255) / 255.f, (xr->pointer_color & 255) / 255.f, xr->pointer_alpha);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        xr_release_target(xr, &xr->pointer_target);
        memset(&pointer, 0, sizeof(pointer));
        pointer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
        pointer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        pointer.space = xr->space;
        pointer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        pointer.subImage.swapchain = xr->pointer_target.swapchain;
        pointer.subImage.imageRect.extent.width = xr->pointer_target.width;
        pointer.subImage.imageRect.extent.height = xr->pointer_target.height;
        pointer.pose = xr_pointer_pose(xr->pointer_start, xr->pointer_hit, xr->frame_snapshot.views[0].position);
        pointer.size.width = sqrtf((xr->pointer_hit[0] - xr->pointer_start[0]) * (xr->pointer_hit[0] - xr->pointer_start[0]) + (xr->pointer_hit[1] - xr->pointer_start[1]) * (xr->pointer_hit[1] - xr->pointer_start[1]) + (xr->pointer_hit[2] - xr->pointer_start[2]) * (xr->pointer_hit[2] - xr->pointer_start[2]));
        pointer.size.height = 0.012f * CLAMP(0.25f, xr->pointer_width, 8.f);
        layers[layer_count++] = (XrCompositionLayerBaseHeader *)&pointer;    }    memset(&end_info, 0, sizeof(end_info));
    end_info.type = XR_TYPE_FRAME_END_INFO;
    end_info.displayTime = xr->frame_state.predictedDisplayTime;
    end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end_info.layerCount = submit && submit_image && xr->frame_state.shouldRender && xr->screen_follow.valid && (double)GetTickCount64 () * 0.001 >= xr->screen_follow.ready_time_s ? layer_count : 0;
    end_info.layers = submit && submit_image && xr->frame_state.shouldRender && xr->screen_follow.valid && (double)GetTickCount64 () * 0.001 >= xr->screen_follow.ready_time_s ? layers : NULL;
    result = xr->end_frame(xr->session, &end_info);
    xr->frame_running = false;
    if (result == XR_SUCCESS)
        return IW_XR_RESULT_OK;
    {
        char message[96];
        q_snprintf (message, sizeof (message), "OpenXR xrEndFrame failed (%d)", (int)result);
        xr_log (xr, message);
    }
    return result == XR_TIMEOUT_EXPIRED ? IW_XR_RESULT_TIMEOUT : IW_XR_RESULT_FAILED;
}


void IW_XRWin_SetScreenCurve(iw_xr_win_t *xr, qboolean enabled, float radius)
{
    if (xr)
    {
        xr->curved_screen = enabled;
        xr->curve_radius = q_max (1.2f, radius);
    }
}

void IW_XRWin_SetScreenGeometry(iw_xr_win_t *xr, float scale, float distance)
{
    if (xr)
    {
        xr->screen_scale = q_max (0.25f, scale);
        xr->screen_distance = q_max (0.5f, distance);
    }
}

void IW_XRWin_SetHUDGeometry(iw_xr_win_t *xr, float scale, float distance, float yoffset)
{
    if (xr)
    {
        xr->hud_scale = q_max (0.25f, scale);
        xr->hud_distance = q_max (0.25f, distance);
        xr->hud_yoffset = yoffset;
    }
}

void IW_XRWin_RequestRecenter(iw_xr_win_t *xr)
{
    if (xr)
        xr->recenter_requested = true;
}

void IW_XRWin_Shutdown(iw_xr_win_t *xr)
{
    int hand;
    if (!xr)
        return;
    for (hand = 0; hand < IW_XR_HAND_COUNT; ++hand)
        IW_XRWin_Haptic(xr, hand, 0.f, 0.f);
    if (xr->frame_running)
        IW_XRWin_EndFrame(xr, false);
    if (xr->session_running && xr->end_session)
    {
        xr->end_session(xr->session);
        xr->session_running = false;
    }
    xr_destroy_resources(xr);
}

#else

struct iw_xr_win_s { int unused; };
iw_xr_win_t *IW_XRWin_Create(void *window, void (*log)(void *, const char *), void *userdata) { (void)window; (void)log; (void)userdata; return NULL; }
void IW_XRWin_Destroy(iw_xr_win_t *xr) { (void)xr; }
iw_xr_result_t IW_XRWin_Probe(iw_xr_win_t *xr, iw_xr_bridge_t *bridge, uint64_t deadline_ns, const char **reason) { (void)xr; (void)bridge; (void)deadline_ns; if (reason) *reason = "OpenXR backend not compiled"; return IW_XR_RESULT_UNAVAILABLE; }
iw_xr_result_t IW_XRWin_Pump(iw_xr_win_t *xr, iw_xr_bridge_t *bridge) { (void)xr; (void)bridge; return IW_XR_RESULT_UNAVAILABLE; }
qboolean IW_XRWin_GetActions(const iw_xr_win_t *xr, iw_xr_action_snapshot_t *actions) { (void)xr; if (actions) memset(actions, 0, sizeof(*actions)); return false; }
qboolean IW_XRWin_RaycastVirtualScreen(const iw_xr_win_t *xr, const float origin[3], const float orientation[4], iw_xr_virtual_screen_hit_t *hit) { (void)xr; (void)origin; (void)orientation; if (hit) memset(hit, 0, sizeof(*hit)); return false; }
void IW_XRWin_SetVirtualPointer(iw_xr_win_t *xr, const float start[3], const float hit[3], qboolean active, unsigned color, float alpha, float width) { (void)xr; (void)start; (void)hit; (void)active; (void)color; (void)alpha; (void)width; }
qboolean IW_XRWin_BeginFrame(iw_xr_win_t *xr, iw_xr_frame_snapshot_t *snapshot) { (void)xr; (void)snapshot; return false; }
qboolean IW_XRWin_BindFrameTarget(iw_xr_win_t *xr) { (void)xr; return false; }
qboolean IW_XRWin_GetFrameTarget(const iw_xr_win_t *xr, unsigned *fbo, int *width, int *height) { (void)xr; if (fbo) *fbo = 0; if (width) *width = 0; if (height) *height = 0; return false; }
qboolean IW_XRWin_HasStereoTargets(const iw_xr_win_t *xr) { (void)xr; return false; }
void IW_XRWin_SetRefreshRate(iw_xr_win_t *xr, float requested_hz) { (void)xr; (void)requested_hz; }
void IW_XRWin_SetMultiviewRequested(iw_xr_win_t *xr, qboolean requested) { (void)xr; (void)requested; }
qboolean IW_XRWin_UsingMultiview(const iw_xr_win_t *xr) { (void)xr; return false; }
qboolean IW_XRWin_BeginMultiviewTarget(iw_xr_win_t *xr, unsigned *fbo, int *width, int *height) { (void)xr; if (fbo) *fbo = 0; if (width) *width = 0; if (height) *height = 0; return false; }
qboolean IW_XRWin_BeginMultiviewOverlayEye(iw_xr_win_t *xr, unsigned eye, unsigned *fbo, int *width, int *height) { (void)xr; (void)eye; if (fbo) *fbo = 0; if (width) *width = 0; if (height) *height = 0; return false; }
void IW_XRWin_EndMultiviewTarget(iw_xr_win_t *xr) { (void)xr; }
qboolean IW_XRWin_BindEyeTarget(iw_xr_win_t *xr, unsigned eye) { (void)xr; (void)eye; return false; }
qboolean IW_XRWin_GetEyeTarget(const iw_xr_win_t *xr, unsigned eye, unsigned *fbo, int *width, int *height) { (void)xr; (void)eye; if (fbo) *fbo = 0; if (width) *width = 0; if (height) *height = 0; return false; }
void IW_XRWin_MirrorEye(iw_xr_win_t *xr, unsigned eye, int width, int height) { (void)xr; (void)eye; (void)width; (void)height; }
void IW_XRWin_MirrorMultiview(iw_xr_win_t *xr, int width, int height) { (void)xr; (void)width; (void)height; }
qboolean IW_XRWin_ResolveEyeTarget(iw_xr_win_t *xr, unsigned eye, int source_width, int source_height) { (void)xr; (void)eye; (void)source_width; (void)source_height; return false; }
void IW_XRWin_SetStereoSubmission(iw_xr_win_t *xr, qboolean enabled) { (void)xr; (void)enabled; }
qboolean IW_XRWin_ResolveDefaultFramebuffer(iw_xr_win_t *xr, int source_width, int source_height) { (void)xr; (void)source_width; (void)source_height; return false; }

iw_xr_result_t IW_XRWin_EndFrame(iw_xr_win_t *xr, qboolean submit) { (void)xr; (void)submit; return IW_XR_RESULT_UNAVAILABLE; }
void IW_XRWin_Shutdown(iw_xr_win_t *xr) { (void)xr; }
void IW_XRWin_RequestRecenter(iw_xr_win_t *xr) { (void)xr; }
void IW_XRWin_SetScreenCurve(iw_xr_win_t *xr, qboolean enabled, float radius) { (void)xr; (void)enabled; (void)radius; }
void IW_XRWin_SetScreenGeometry(iw_xr_win_t *xr, float scale, float distance) { (void)xr; (void)scale; (void)distance; }
void IW_XRWin_SetHUDGeometry(iw_xr_win_t *xr, float scale, float distance, float yoffset) { (void)xr; (void)scale; (void)distance; (void)yoffset; }
void IW_XRWin_Haptic(iw_xr_win_t *xr, int hand, float amplitude, float duration_seconds) { (void)xr; (void)hand; (void)amplitude; (void)duration_seconds; }

#endif
