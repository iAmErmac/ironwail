#ifndef IRONWAIL_ANDROID_GLES_H
#define IRONWAIL_ANDROID_GLES_H

#include "q_stdinc.h"

typedef struct iw_gles_limits_s {
    int max_texture_size;
    int max_texture_units;
    int max_draw_buffers;
    int max_samples;
    int uniform_buffer_offset_alignment;
    int shader_storage_buffer_offset_alignment;
    int max_image_units;
    int max_compute_work_groups[3];
} iw_gles_limits_t;

typedef struct iw_gles_features_s {
    qboolean gles31;
    qboolean anisotropy;
    qboolean float_targets;
    qboolean msaa;
    qboolean integer_images;
    qboolean oit;
} iw_gles_features_t;

/* Called after IronRift has made its GLES context current. */
qboolean IW_GLES_Probe(iw_gles_limits_t *limits, iw_gles_features_t *features);
const char *IW_GLES_FeatureTier(void);

#endif
