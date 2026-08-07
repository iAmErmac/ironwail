#include "quakedef.h"
#include "android_gles.h"

#if defined(ANDROID_GLES3)
#include <GLES3/gl31.h>
#include <android/log.h>

#define IW_TAG "IronwailGLES"
#define IW_LOG(...) __android_log_print(ANDROID_LOG_INFO, IW_TAG, __VA_ARGS__)
#define IW_WARN(...) __android_log_print(ANDROID_LOG_WARN, IW_TAG, __VA_ARGS__)

static iw_gles_features_t iw_features;

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

qboolean IW_GLES_Probe(iw_gles_limits_t *limits, iw_gles_features_t *features)
{
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *vendor = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    GLint value;

    memset(limits, 0, sizeof(*limits));
    memset(features, 0, sizeof(*features));
    features->gles31 = version && strstr(version, "OpenGL ES 3.") != NULL;

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

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &value);
    if (!features->gles31)
        IW_WARN("GLES 3.1 is required; refusing non-GLES context");
    return features->gles31 && value >= 0;
}

const char *IW_GLES_FeatureTier(void)
{
    return iw_features.gles31 ? "base-direct-draw" : "unsupported";
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
