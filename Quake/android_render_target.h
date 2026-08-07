#ifndef IRONWAIL_ANDROID_RENDER_TARGET_H
#define IRONWAIL_ANDROID_RENDER_TARGET_H

#include "q_stdinc.h"

typedef struct iw_render_target_desc_s {
    int width;
    int height;
    int color_format;
    int depth_stencil_format;
    int samples;
    int color_attachments;
    unsigned usage_flags;
} iw_render_target_desc_t;

typedef struct iw_render_target_caps_s {
    qboolean baseline;
    qboolean float_color;
    qboolean msaa;
    qboolean mrt;
    qboolean integer_image;
} iw_render_target_caps_t;

enum {
    IW_TARGET_RGBA8 = 1,
    IW_TARGET_RGBA16F = 2,
    IW_TARGET_DEPTH24_STENCIL8 = 1
};

enum {
    IW_TARGET_SCENE = 1u << 0,
    IW_TARGET_POSTPROCESS = 1u << 1,
    IW_TARGET_LAYERED = 1u << 2
};

qboolean IW_GLES_ValidateTarget(const iw_render_target_desc_t *desc);
qboolean IW_GLES_ProbeFormats(int width, int height, iw_render_target_caps_t *caps);

#endif
