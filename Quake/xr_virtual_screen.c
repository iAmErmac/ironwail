#include "xr_virtual_screen.h"

#include <math.h>
#include <string.h>

static void xr_vs_cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void xr_vs_rotate(const float q[4], const float v[3], float out[3])
{
    float qv[3] = {q[0], q[1], q[2]}, t[3], c[3];
    xr_vs_cross(qv, v, t);
    t[0] *= 2.f; t[1] *= 2.f; t[2] *= 2.f;
    xr_vs_cross(qv, t, c);
    out[0] = v[0] + q[3] * t[0] + c[0];
    out[1] = v[1] + q[3] * t[1] + c[1];
    out[2] = v[2] + q[3] * t[2] + c[2];
}

static void xr_vs_rotate_inverse(const float q[4], const float v[3], float out[3])
{
    float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    xr_vs_rotate(inverse, v, out);
}

qboolean IW_XRVirtualScreen_Raycast(const iw_xr_virtual_screen_t *screen,
                                    const float origin[3],
                                    const float orientation[4],
                                    iw_xr_virtual_screen_hit_t *hit)
{
    float direction[3] = {0.f, 0.f, -1.f};
    float offset[3], local_origin[3], local_direction[3], local_hit[3], world_normal[3];
    float distance, u, v;

    if (hit) memset(hit, 0, sizeof(*hit));
    if (!screen || !origin || !orientation || !hit || screen->width <= 0.f || screen->height <= 0.f)
        return false;

    xr_vs_rotate(orientation, direction, direction);
    if (screen->curved && screen->curve_radius > 0.001f) {
        float radius = screen->curve_radius;
        float a, b, c, discriminant, t0, t1, angle, half_angle;
        offset[0] = origin[0] - screen->position[0];
        offset[1] = origin[1] - screen->position[1];
        offset[2] = origin[2] - screen->position[2];
        xr_vs_rotate_inverse(screen->orientation, offset, local_origin);
        xr_vs_rotate_inverse(screen->orientation, direction, local_direction);
        a = local_direction[0] * local_direction[0] + local_direction[2] * local_direction[2];
        b = 2.f * (local_origin[0] * local_direction[0] + local_origin[2] * local_direction[2]);
        c = local_origin[0] * local_origin[0] + local_origin[2] * local_origin[2] - radius * radius;
        discriminant = b * b - 4.f * a * c;
        if (a < 0.00001f || discriminant < 0.f) return false;
        t0 = (-b - sqrtf(discriminant)) / (2.f * a);
        t1 = (-b + sqrtf(discriminant)) / (2.f * a);
        distance = t0 > 0.0001f ? t0 : t1;
        if (distance <= 0.0001f) return false;
        local_hit[0] = local_origin[0] + local_direction[0] * distance;
        local_hit[1] = local_origin[1] + local_direction[1] * distance;
        local_hit[2] = local_origin[2] + local_direction[2] * distance;
        angle = atan2f(local_hit[0], -local_hit[2]);
        half_angle = screen->width * 0.5f / radius;
        u = 0.5f + angle / (2.f * half_angle);
        world_normal[0] = -local_hit[0] / radius;
        world_normal[1] = 0.f;
        world_normal[2] = -local_hit[2] / radius;
        xr_vs_rotate(screen->orientation, local_hit, offset);
        hit->position[0] = screen->position[0] + offset[0];
        hit->position[1] = screen->position[1] + offset[1];
        hit->position[2] = screen->position[2] + offset[2];
        xr_vs_rotate(screen->orientation, world_normal, hit->normal);
    } else {
        offset[0] = origin[0] - screen->position[0];
        offset[1] = origin[1] - screen->position[1];
        offset[2] = origin[2] - screen->position[2];
        xr_vs_rotate_inverse(screen->orientation, offset, local_origin);
        xr_vs_rotate_inverse(screen->orientation, direction, local_direction);
        if (fabsf(local_direction[2]) < 0.0001f) return false;
        distance = -local_origin[2] / local_direction[2];
        if (distance <= 0.0001f) return false;
        local_hit[0] = local_origin[0] + local_direction[0] * distance;
        local_hit[1] = local_origin[1] + local_direction[1] * distance;
        local_hit[2] = local_origin[2] + local_direction[2] * distance;
        u = 0.5f + local_hit[0] / screen->width;
        world_normal[0] = 0.f; world_normal[1] = 0.f; world_normal[2] = -1.f;
        xr_vs_rotate(screen->orientation, local_hit, offset);
        hit->position[0] = screen->position[0] + offset[0];
        hit->position[1] = screen->position[1] + offset[1];
        hit->position[2] = screen->position[2] + offset[2];
        xr_vs_rotate(screen->orientation, world_normal, hit->normal);
    }

    v = 0.5f - local_hit[1] / screen->height;
    hit->u = u;
    hit->v = v;
    hit->valid = true;
    hit->inside = u >= 0.f && u <= 1.f && v >= 0.f && v <= 1.f;
    return true;
}