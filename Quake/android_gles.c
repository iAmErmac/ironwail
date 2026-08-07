#include "quakedef.h"
#include "android_gles.h"

#if defined(ANDROID_GLES3)
#include <GLES3/gl31.h>
#include <android/log.h>

#define IW_TAG "IronwailGLES"
#define IW_LOG(...) __android_log_print(ANDROID_LOG_INFO, IW_TAG, __VA_ARGS__)
#define IW_WARN(...) __android_log_print(ANDROID_LOG_WARN, IW_TAG, __VA_ARGS__)

static iw_gles_features_t iw_features;

static qboolean iw_extensions_logged;

static qboolean IW_GLES_HasExtension(const char *name)
{
    GLint count = 0;
    GLint i;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (i = 0; i < count; ++i)
        if (!strcmp(name, (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i)))
            return true;
    return false;
}

static void IW_GLES_LogExtensions(void)
{
    GLint count = 0;
    GLint i;

    if (iw_extensions_logged)
        return;
    iw_extensions_logged = true;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    if (glGetError() != GL_NO_ERROR)
    {
        IW_WARN("extensions query failed; fallback=extension-dependent features disabled");
        return;
    }
    IW_LOG("extensions count=%d", count);
    for (i = 0; i < count; ++i)
        IW_LOG("extension[%d]=%s", i, glGetStringi(GL_EXTENSIONS, (GLuint)i));
}

qboolean IW_GLES_Probe(iw_gles_limits_t *limits, iw_gles_features_t *features)
{
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *vendor = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    GLint major = 0, minor = 0;
    GLint value;

    memset(limits, 0, sizeof(*limits));
    memset(features, 0, sizeof(*features));
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major == 0 && version)
    {
        if (sscanf(version, "OpenGL ES %d.%d", &major, &minor) != 2)
            sscanf(version, "%d.%d", &major, &minor);
    }
    features->gles31 = major > 3 || (major == 3 && minor >= 1);
    IW_GLES_LogExtensions();

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limits->max_texture_size);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &limits->max_texture_units);
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &limits->max_draw_buffers);
    glGetIntegerv(GL_MAX_SAMPLES, &limits->max_samples);
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &limits->uniform_buffer_offset_alignment);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &limits->shader_storage_buffer_offset_alignment);
    glGetIntegerv(GL_MAX_IMAGE_UNITS, &limits->max_image_units);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &limits->max_compute_work_groups[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &limits->max_compute_work_groups[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &limits->max_compute_work_groups[2]);

    features->anisotropy = IW_GLES_HasExtension("GL_EXT_texture_filter_anisotropic");
    features->float_targets = IW_GLES_HasExtension("GL_EXT_color_buffer_float");
    features->msaa = limits->max_samples > 1;
    features->integer_images = limits->max_image_units > 0;
    features->oit = false;
    iw_features = *features;

    IW_LOG("GLES version=%s vendor=%s renderer=%s", version ? version : "(null)",
           vendor ? vendor : "(null)", renderer ? renderer : "(null)");
    IW_LOG("limits texture=%d units=%d draw_buffers=%d samples=%d ubo_align=%d ssbo_align=%d image_units=%d compute=%d,%d,%d",
           limits->max_texture_size, limits->max_texture_units, limits->max_draw_buffers,
           limits->max_samples, limits->uniform_buffer_offset_alignment,
           limits->shader_storage_buffer_offset_alignment, limits->max_image_units,
           limits->max_compute_work_groups[0], limits->max_compute_work_groups[1],
           limits->max_compute_work_groups[2]);
    IW_LOG("features anisotropy=%d float_targets=%d msaa=%d integer_images=%d oit=%d tier=base-direct-draw",
           features->anisotropy, features->float_targets, features->msaa,
           features->integer_images, features->oit);

    value = 0;
    if (glGetError() != GL_NO_ERROR)
        value = -1;
    if (!features->gles31)
        IW_WARN("GLES 3.1 is required; refusing non-GLES context");
    return features->gles31 && value >= 0;
}

const char *IW_GLES_FeatureTier(void)
{
    return iw_features.gles31 ? "base-direct-draw" : "unsupported";
}

void IW_GLES_RecordFormatCaps(qboolean baseline, qboolean float_color, qboolean msaa, qboolean mrt, qboolean integer_image)
{
    iw_features.baseline_target = baseline;
    iw_features.float_targets = float_color;
    iw_features.msaa = msaa;
    iw_features.integer_images = integer_image;
    IW_LOG("selected baseline=%d float=%d msaa=%d mrt=%d integer_image=%d tier=%s", baseline, float_color, msaa, mrt, integer_image, IW_GLES_FeatureTier());
    if (!baseline)
        IW_WARN("baseline RGBA8/depth-stencil unavailable; fallback=software/startup refusal");
    if (!float_color)
        IW_WARN("RGBA16F target unavailable; fallback=RGBA8 direct target");
    if (!msaa)
        IW_WARN("2x MSAA target unavailable; fallback=single-sample rendering");
    if (!mrt)
        IW_WARN("2-target MRT unavailable; fallback=single-target rendering");
    if (!integer_image)
        IW_WARN("RG32UI image unavailable; fallback=clustered image/OIT disabled");
}

#else

qboolean IW_GLES_Probe(iw_gles_limits_t *limits, iw_gles_features_t *features)
{
    memset(limits, 0, sizeof(*limits));
    memset(features, 0, sizeof(*features));
    return false;
}

const char *IW_GLES_FeatureTier(void) { return "desktop"; }

#endif
