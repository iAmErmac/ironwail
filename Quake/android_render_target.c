#include "quakedef.h"
#include "android_render_target.h"

#if defined(ANDROID_GLES3)
#include <GLES3/gl31.h>
#include <android/log.h>

#define IW_TARGET_LOG(...) __android_log_print(ANDROID_LOG_INFO, "IronwailGLES", __VA_ARGS__)
#define IW_TARGET_WARN(...) __android_log_print(ANDROID_LOG_WARN, "IronwailGLES", __VA_ARGS__)

static void IW_ClearErrors(void)
{
    while (glGetError() != GL_NO_ERROR)
        ;
}

static qboolean IW_ProbeTarget(const char *label, GLenum color_format, int width, int height, int samples, int color_attachments)
{
    GLuint fbo = 0;
    GLuint color[2] = { 0, 0 };
    GLuint color_rb[2] = { 0, 0 };
    GLuint depth = 0;
    GLint old_fbo = 0;
    GLint old_rb = 0;
    GLint old_active = 0;
    GLint old_tex2d = 0;
    GLint old_tex3d = 0;
    GLenum status;
    GLenum error;    GLenum draw_buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    qboolean result = false;

    IW_ClearErrors();
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &old_rb);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_tex2d);
    glGetIntegerv(GL_TEXTURE_BINDING_3D, &old_tex3d);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    if (samples == 0)
    {
        glGenTextures(color_attachments, color);
        for (int i = 0; i < color_attachments; ++i)
        {
            glBindTexture(GL_TEXTURE_2D, color[i]);
            glTexStorage2D(GL_TEXTURE_2D, 1, color_format, width, height);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, color[i], 0);
        }

        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    }
    else
    {
        glGenRenderbuffers(color_attachments, color_rb);
        for (int i = 0; i < color_attachments; ++i)
        {
            glBindRenderbuffer(GL_RENDERBUFFER, color_rb[i]);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, color_format, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER, color_rb[i]);
        }

        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
    }

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (color_attachments > 1)
        glDrawBuffers(color_attachments, draw_buffers);
    else
        glDrawBuffers(1, draw_buffers);

    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    error = glGetError();
    result = status == GL_FRAMEBUFFER_COMPLETE && error == GL_NO_ERROR;
    if (result)
        IW_TARGET_LOG("format probe %s passed status=0x%04x gl_error=0x%04x", label, status, error);
    else
        IW_TARGET_WARN("format probe %s failed status=0x%04x gl_error=0x%04x fallback=base-direct-draw", label, status, error);

    if (color[0] || color[1])
        glDeleteTextures(color_attachments, color);
    if (color_rb[0] || color_rb[1])
        glDeleteRenderbuffers(color_attachments, color_rb);
    if (depth)
        glDeleteRenderbuffers(1, &depth);
    if (fbo)
        glDeleteFramebuffers(1, &fbo);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)old_fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)old_rb);
    glActiveTexture((GLenum)old_active);
    glBindTexture(GL_TEXTURE_2D, (GLuint)old_tex2d);
    glBindTexture(GL_TEXTURE_3D, (GLuint)old_tex3d);
    IW_ClearErrors();
    return result;
}

static qboolean IW_ProbeIntegerImage(void)
{
    GLuint texture = 0;
    GLint old_active = 0;
    GLint old_tex3d = 0;
    GLenum error;

    IW_ClearErrors();
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
    glGetIntegerv(GL_TEXTURE_BINDING_3D, &old_tex3d);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_3D, texture);
    glTexStorage3D(GL_TEXTURE_3D, 1, GL_RG32UI, 1, 1, 1);
    glBindImageTexture(0, texture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RG32UI);
    error = glGetError();
    if (error != GL_NO_ERROR)
        IW_TARGET_WARN("format probe integer-image failed status=0x0000 gl_error=0x%04x fallback=disabled", error);
    else
        IW_TARGET_LOG("format probe integer-image passed status=0x0000 gl_error=0x0000");
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32UI);
    glDeleteTextures(1, &texture);
    glActiveTexture((GLenum)old_active);
    glBindTexture(GL_TEXTURE_3D, (GLuint)old_tex3d);
    IW_ClearErrors();
    return error == GL_NO_ERROR;
}

qboolean IW_GLES_ValidateTarget(const iw_render_target_desc_t *desc)
{
    if (!desc || desc->width <= 0 || desc->height <= 0 ||
        desc->color_format != IW_TARGET_RGBA8 ||
        desc->depth_stencil_format != IW_TARGET_DEPTH24_STENCIL8 ||
        desc->samples != 0 || desc->color_attachments != 1)
        return false;

    return IW_ProbeTarget("baseline-rgba8", GL_RGBA8, desc->width, desc->height, 0, 1);
}

qboolean IW_GLES_ProbeFormats(int width, int height, iw_render_target_caps_t *caps)
{
    if (!caps || width <= 0 || height <= 0)
        return false;

    memset(caps, 0, sizeof(*caps));
    caps->baseline = IW_ProbeTarget("baseline-rgba8", GL_RGBA8, width, height, 0, 1);
    caps->float_color = IW_ProbeTarget("rgba16f", GL_RGBA16F, width, height, 0, 1);
    caps->msaa = IW_ProbeTarget("msaa2x-rgba8", GL_RGBA8, width, height, 2, 1);
    caps->mrt = IW_ProbeTarget("mrt2-rgba8", GL_RGBA8, width, height, 0, 2);
    caps->integer_image = IW_ProbeIntegerImage();

    IW_TARGET_LOG("formats baseline=%d rgba16f=%d msaa2x=%d mrt2=%d rg32ui-image=%d",
        caps->baseline, caps->float_color, caps->msaa, caps->mrt, caps->integer_image);
    return caps->baseline;
}

#else

qboolean IW_GLES_ValidateTarget(const iw_render_target_desc_t *desc)
{
    (void)desc;
    return false;
}

qboolean IW_GLES_ProbeFormats(int width, int height, iw_render_target_caps_t *caps)
{
    (void)width;
    (void)height;
    (void)caps;
    return false;
}

#endif
