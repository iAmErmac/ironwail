#include "quakedef.h"
#include "android_render_target.h"

#if defined(ANDROID_GLES3)
#include <GLES3/gl31.h>
#include <android/log.h>

#define IW_TARGET_LOG(...) __android_log_print(ANDROID_LOG_INFO, "IronwailGLES", __VA_ARGS__)
#define IW_TARGET_WARN(...) __android_log_print(ANDROID_LOG_WARN, "IronwailGLES", __VA_ARGS__)

qboolean IW_GLES_ValidateTarget(const iw_render_target_desc_t *desc)
{
    GLuint fbo, color, depth;
    GLenum status;
    GLint max_draw_buffers = 0;

    if (!desc || desc->width <= 0 || desc->height <= 0 || desc->samples != 0 ||
        desc->color_attachments != 1)
        return false;
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &max_draw_buffers);
    if (max_draw_buffers < desc->color_attachments)
        return false;

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, desc->width, desc->height);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, desc->width, desc->height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteRenderbuffers(1, &depth);
    glDeleteTextures(1, &color);
    glDeleteFramebuffers(1, &fbo);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        IW_TARGET_WARN("RGBA8/depth24-stencil8 target probe failed: 0x%04x", status);
        return false;
    }
    IW_TARGET_LOG("baseline target probe passed: %dx%d RGBA8 depth24-stencil8 samples=0", desc->width, desc->height);
    return true;
}

#else

qboolean IW_GLES_ValidateTarget(const iw_render_target_desc_t *desc)
{
    (void)desc;
    return false;
}

#endif
