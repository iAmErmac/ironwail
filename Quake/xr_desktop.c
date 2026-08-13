#include "quakedef.h"
#include "xr_desktop.h"

#if defined(IW_ENABLE_OPENXR)

#include "glquake.h"
#include <SDL_syswm.h>
#include <windows.h>

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
    qboolean recenter_requested;
        XrPosef screen_follow_current;
    XrPosef screen_follow_target;
    double screen_follow_last_step_s;
    double screen_follow_ready_s;
    qboolean screen_follow_valid;
    qboolean screen_follow_targeting;
XrSwapchain swapchain;
    XrSwapchainImageOpenGLKHR *images;
    GLuint *fbos;
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
    qboolean curved_screen;
    float curve_radius;
    float screen_scale;
    float screen_distance;
    qboolean curve_submission_logged;

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
};

static void xr_log(iw_xr_win_t *xr, const char *message)
{
    if (xr && xr->log)
        xr->log(xr->userdata, message);
}

static void xr_destroy_resources(iw_xr_win_t *xr);

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

static float xr_dot3 (const float a[3], const float b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static void xr_cross3 (const float a[3], const float b[3], float o[3]) { o[0]=a[1]*b[2]-a[2]*b[1]; o[1]=a[2]*b[0]-a[0]*b[2]; o[2]=a[0]*b[1]-a[1]*b[0]; }
static qboolean xr_normalize3 (float v[3]) { float l=sqrtf(xr_dot3(v,v)); if(l<0.0001f)return false; v[0]/=l;v[1]/=l;v[2]/=l;return true; }static void xr_rotate3 (const XrQuaternionf *q, const float v[3], float out[3])
{
    float qv[3] = { q->x, q->y, q->z }, t[3], c[3];
    xr_cross3 (qv, v, t); t[0] *= 2.0f; t[1] *= 2.0f; t[2] *= 2.0f;
    xr_cross3 (qv, t, c);
    out[0] = v[0] + q->w * t[0] + c[0]; out[1] = v[1] + q->w * t[1] + c[1]; out[2] = v[2] + q->w * t[2] + c[2];
}
static float xr_yaw_delta (float a, float b)
{
    float d = a - b; while (d > 180.0f) d -= 360.0f; while (d < -180.0f) d += 360.0f; return d;
}
static XrPosef xr_build_screen_pose (const XrVector3f *center, const float forward[3], float distance)
{
    XrPosef pose; float yaw = atan2f (-forward[0], -forward[2]), half = yaw * 0.5f;
    pose.orientation.x = 0.0f; pose.orientation.y = sinf (half); pose.orientation.z = 0.0f; pose.orientation.w = cosf (half);
    pose.position.x = center->x + forward[0] * distance; pose.position.y = center->y; pose.position.z = center->z + forward[2] * distance;
    return pose;
}
static void xr_face_screen_at_center (XrPosef *pose, const XrVector3f *center)
{
    float dx = center->x - pose->position.x;
    float dz = center->z - pose->position.z;
    float length = sqrtf (dx * dx + dz * dz);
    float half;
    if (length < 0.0001f)
        return;
    half = atan2f (dx, dz) * 0.5f;
    pose->orientation.x = 0.0f;
    pose->orientation.y = sinf (half);
    pose->orientation.z = 0.0f;
    pose->orientation.w = cosf (half);
}

static float xr_position_distance (const XrVector3f *a, const XrVector3f *b)
{
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float dz = a->z - b->z;
    return sqrtf (dx * dx + dy * dy + dz * dz);
}

static void xr_keep_screen_distance (iw_xr_win_t *xr, const XrVector3f *center)
{
    float dx = xr->screen_follow_current.position.x - center->x;
    float dz = xr->screen_follow_current.position.z - center->z;
    float length = sqrtf (dx * dx + dz * dz);
    if (length < 0.0001f)
        return;
    xr->screen_follow_current.position.x = center->x + dx * xr->screen_distance / length;
    xr->screen_follow_current.position.y = center->y;
    xr->screen_follow_current.position.z = center->z + dz * xr->screen_distance / length;
}

static void xr_update_screen_pose (iw_xr_win_t *xr, const XrView *views, uint32_t view_count)
{
    XrVector3f center = { 0.0f, 0.0f, 0.0f };
    XrVector3f candidate_position;
    float forward[3] = { 0.0f, 0.0f, 0.0f };
    XrPosef candidate;
    double now = (double)GetTickCount64 () * 0.001;
    double delta;
    float easing;
    float target_delta;
    uint32_t i;

    if (!view_count)
        return;
    for (i = 0; i < view_count; ++i)
    {
        float view_forward[3] = { 0.0f, 0.0f, -1.0f };
        center.x += views[i].pose.position.x;
        center.y += views[i].pose.position.y;
        center.z += views[i].pose.position.z;
        xr_rotate3 (&views[i].pose.orientation, view_forward, view_forward);
        forward[0] += view_forward[0];
        forward[1] += view_forward[1];
        forward[2] += view_forward[2];
    }
    center.x /= (float)view_count;
    center.y /= (float)view_count;
    center.z /= (float)view_count;
    forward[1] = 0.0f;
    if (!xr_normalize3 (forward))
    {
        forward[0] = 0.0f;
        forward[1] = 0.0f;
        forward[2] = -1.0f;
    }
    candidate = xr_build_screen_pose (&center, forward, xr->screen_distance);
    candidate_position = candidate.position;

    if (!xr->screen_follow_valid || xr->recenter_requested)
    {
        xr->screen_follow_current = candidate;
        xr->screen_follow_target = candidate;
        xr->screen_follow_last_step_s = now;
        xr->screen_follow_ready_s = now + 0.75;
        xr->screen_follow_valid = true;
        xr->screen_follow_targeting = true;
        xr->recenter_requested = false;
    }
    else if (now < xr->screen_follow_ready_s)
    {
        xr->screen_follow_current = candidate;
        xr->screen_follow_target = candidate;
    }
    else
    {
        target_delta = xr_position_distance (&xr->screen_follow_target.position, &candidate_position);
        if (target_delta < 0.1f)
            xr->screen_follow_targeting = false;
        else if (target_delta > 1.5f || xr->screen_follow_targeting)
        {
            xr->screen_follow_target.position = candidate_position;
            xr->screen_follow_targeting = true;
            if (target_delta > 3.0f)
                xr->screen_follow_current.position = candidate_position;
        }

        delta = now - xr->screen_follow_last_step_s;
        if (delta < 0.0)
            delta = 0.0;
        if (delta > 0.1)
            delta = 0.1;
        xr->screen_follow_last_step_s = now;
        easing = 1.0f - powf (0.99f, (float)delta * 90.0f);
        xr->screen_follow_current.position.x += (xr->screen_follow_target.position.x - xr->screen_follow_current.position.x) * easing;
        xr->screen_follow_current.position.y += (xr->screen_follow_target.position.y - xr->screen_follow_current.position.y) * easing;
        xr->screen_follow_current.position.z += (xr->screen_follow_target.position.z - xr->screen_follow_current.position.z) * easing;
        xr_keep_screen_distance (xr, &center);
        xr_face_screen_at_center (&xr->screen_follow_current, &center);
    }
    xr->screen_pose = xr->screen_follow_current;
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
    proc = NULL;
    if (xr->get_instance_proc_addr(xr->instance, "xrGetOpenGLGraphicsRequirementsKHR", &proc) != XR_SUCCESS || !proc)
    {
        if (reason)
            *reason = "XR_KHR_opengl_enable procedure is unavailable";
        return false;
    }
    xr->get_opengl_graphics_requirements = (PFN_xrGetOpenGLGraphicsRequirementsKHR)proc;
#undef XR_LOAD_INSTANCE
    return true;
}

static void xr_destroy_resources(iw_xr_win_t *xr)
{
    uint32_t i;
    if (!xr)
        return;
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
        xr->screen_pose.position.y = 1.6f;
        xr->screen_pose.position.z = -2.0f;
        xr->screen_scale = 1.0f;
        xr->screen_distance = 2.5f;
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
    const char *extensions[2] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME };
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
    XrViewConfigurationType view_configs[8];
    uint32_t view_config_count = 0;
    uint32_t view_config_index;

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
        }
    }
    if (has_cylinder_extension)
        enabled_extension_count = 2;
    xr->cylinder_supported = has_cylinder_extension;
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
    memset(&requirements, 0, sizeof(requirements));
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
    memset(&space_info, 0, sizeof(space_info));
    space_info.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    space_info.poseInReferenceSpace.orientation.w = 1.0f;
    space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    result = xr->create_reference_space(xr->session, &space_info, &xr->space);
    if (result != XR_SUCCESS)
    {
        space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        result = xr->create_reference_space(xr->session, &space_info, &xr->space);
    }
    if (result != XR_SUCCESS)
    {
        space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
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
    memset(&event, 0, sizeof(event));
    event.type = XR_TYPE_EVENT_DATA_BUFFER;
    while (xr->poll_event(xr->instance, &event) == XR_SUCCESS)
    {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
        {
            XrEventDataSessionStateChanged *changed = (XrEventDataSessionStateChanged *)&event;
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
                xr->end_session(xr->session);
                xr->session_running = false;
                IW_XRBridge_SetFailure(bridge, IW_XR_RESULT_UNAVAILABLE, "OpenXR session stopped");
            }
            else if (changed->state == XR_SESSION_STATE_LOSS_PENDING)
            {
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
    return true;
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

iw_xr_result_t IW_XRWin_EndFrame(iw_xr_win_t *xr, qboolean submit)
{
    XrResult result;
    XrSwapchainImageReleaseInfo release_info;
    XrCompositionLayerQuad quad;
    XrCompositionLayerCylinderKHR cylinder;
    XrCompositionLayerBaseHeader *layers[1];
    XrFrameEndInfo end_info;
    qboolean submit_image;
    if (!xr || !xr->frame_running)
        return IW_XR_RESULT_INVALID;
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
    quad.size.width = 3.6f * xr->screen_scale;
    quad.size.height = 2.7f * xr->screen_scale;
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
        cylinder.pose = quad.pose;
        cylinder.radius = xr->curve_radius;
        cylinder.centralAngle = quad.size.width / xr->curve_radius;
        cylinder.aspectRatio = quad.size.width / quad.size.height;
        layers[0] = (XrCompositionLayerBaseHeader *)&cylinder;
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
        layers[0] = (XrCompositionLayerBaseHeader *)&quad;
    }
    memset(&end_info, 0, sizeof(end_info));
    end_info.type = XR_TYPE_FRAME_END_INFO;
    end_info.displayTime = xr->frame_state.predictedDisplayTime;
    end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end_info.layerCount = submit && submit_image && xr->frame_state.shouldRender && xr->screen_follow_valid && (double)GetTickCount64 () * 0.001 >= xr->screen_follow_ready_s ? 1 : 0;
    end_info.layers = submit && submit_image && xr->frame_state.shouldRender && xr->screen_follow_valid && (double)GetTickCount64 () * 0.001 >= xr->screen_follow_ready_s ? layers : NULL;
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

void IW_XRWin_RequestRecenter(iw_xr_win_t *xr)
{
    if (xr)
        xr->recenter_requested = true;
}

void IW_XRWin_Shutdown(iw_xr_win_t *xr)
{
    if (!xr)
        return;
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
qboolean IW_XRWin_BeginFrame(iw_xr_win_t *xr, iw_xr_frame_snapshot_t *snapshot) { (void)xr; (void)snapshot; return false; }
qboolean IW_XRWin_BindFrameTarget(iw_xr_win_t *xr) { (void)xr; return false; }
qboolean IW_XRWin_ResolveDefaultFramebuffer(iw_xr_win_t *xr, int source_width, int source_height) { (void)xr; (void)source_width; (void)source_height; return false; }
iw_xr_result_t IW_XRWin_EndFrame(iw_xr_win_t *xr, qboolean submit) { (void)xr; (void)submit; return IW_XR_RESULT_UNAVAILABLE; }
void IW_XRWin_Shutdown(iw_xr_win_t *xr) { (void)xr; }
void IW_XRWin_RequestRecenter(iw_xr_win_t *xr) { (void)xr; }
void IW_XRWin_SetScreenCurve(iw_xr_win_t *xr, qboolean enabled, float radius) { (void)xr; (void)enabled; (void)radius; }
void IW_XRWin_SetScreenGeometry(iw_xr_win_t *xr, float scale, float distance) { (void)xr; (void)scale; (void)distance; }

#endif