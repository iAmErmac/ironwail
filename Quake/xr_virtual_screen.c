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

static void xr_vs_rotate_pose(const float q[4], const float v[3], float out[3])
{
    float qv[3] = {q[0], q[1], q[2]}, t[3], c[3];
    t[0] = 2.f * (qv[1] * v[2] - qv[2] * v[1]);
    t[1] = 2.f * (qv[2] * v[0] - qv[0] * v[2]);
    t[2] = 2.f * (qv[0] * v[1] - qv[1] * v[0]);
    c[0] = qv[1] * t[2] - qv[2] * t[1];
    c[1] = qv[2] * t[0] - qv[0] * t[2];
    c[2] = qv[0] * t[1] - qv[1] * t[0];
    out[0] = v[0] + q[3] * t[0] + c[0]; out[1] = v[1] + q[3] * t[1] + c[1]; out[2] = v[2] + q[3] * t[2] + c[2];
}

static void xr_vs_build_pose(const float center[3], const float forward[3], float distance, iw_xr_virtual_screen_pose_t *pose)
{
    float yaw = atan2f(-forward[0], -forward[2]), half = yaw * 0.5f;
    pose->orientation[0] = 0.f; pose->orientation[1] = sinf(half); pose->orientation[2] = 0.f; pose->orientation[3] = cosf(half);
    pose->position[0] = center[0] + forward[0] * distance; pose->position[1] = center[1]; pose->position[2] = center[2] + forward[2] * distance;
}

qboolean IW_XRVirtualScreen_UpdatePose(iw_xr_virtual_screen_follow_t *state, const iw_xr_virtual_screen_view_t *views, unsigned view_count, double now_s, float distance, qboolean follow, iw_xr_virtual_screen_pose_t *pose)
{
    float center[3] = {0.f, 0.f, 0.f}, forward[3] = {0.f, 0.f, 0.f}, len, candidate_position[3], easing, delta;
    iw_xr_virtual_screen_pose_t candidate;
    unsigned i;
    if (!state || !views || !view_count || !pose) return false;
    for (i = 0; i < view_count; ++i) { float view_forward[3] = {0.f, 0.f, -1.f}; center[0] += views[i].position[0]; center[1] += views[i].position[1]; center[2] += views[i].position[2]; xr_vs_rotate_pose(views[i].orientation, view_forward, view_forward); forward[0] += view_forward[0]; forward[1] += view_forward[1]; forward[2] += view_forward[2]; }
    center[0] /= view_count; center[1] /= view_count; center[2] /= view_count; forward[1] = 0.f;
    len = sqrtf(forward[0] * forward[0] + forward[2] * forward[2]); if (len < 0.0001f) { forward[0] = 0.f; forward[2] = -1.f; } else { forward[0] /= len; forward[2] /= len; }
    xr_vs_build_pose(center, forward, distance, &candidate); memcpy(candidate_position, candidate.position, sizeof(candidate_position));
    if (!state->valid) { state->current = candidate; state->target = candidate; state->last_step_s = now_s; state->ready_time_s = now_s + 0.75; state->valid = true; state->targeting = true; }
    else if (!follow) { *pose = state->current; return true; }
    else if (now_s < state->ready_time_s) { state->current = candidate; state->target = candidate; }
    else {
        delta = sqrtf((state->target.position[0] - candidate_position[0]) * (state->target.position[0] - candidate_position[0]) + (state->target.position[1] - candidate_position[1]) * (state->target.position[1] - candidate_position[1]) + (state->target.position[2] - candidate_position[2]) * (state->target.position[2] - candidate_position[2]));
        if (delta < 0.1f) state->targeting = false; else if (delta > 1.5f || state->targeting) { memcpy(state->target.position, candidate_position, sizeof(candidate_position)); state->targeting = true; if (delta > 3.f) memcpy(state->current.position, candidate_position, sizeof(candidate_position)); }
        delta = (float)(now_s - state->last_step_s); if (delta < 0.f) delta = 0.f; if (delta > 0.1f) delta = 0.1f; state->last_step_s = now_s; easing = 1.f - powf(0.99f, delta * 90.f);
        for (i = 0; i < 3; ++i) state->current.position[i] += (state->target.position[i] - state->current.position[i]) * easing;
        { float dx = state->current.position[0] - center[0], dz = state->current.position[2] - center[2], d = sqrtf(dx * dx + dz * dz); if (d > 0.0001f) { state->current.position[0] = center[0] + dx * distance / d; state->current.position[1] = center[1]; state->current.position[2] = center[2] + dz * distance / d; float yaw = atan2f(center[0] - state->current.position[0], center[2] - state->current.position[2]) * 0.5f; state->current.orientation[0] = 0.f; state->current.orientation[1] = sinf(yaw); state->current.orientation[2] = 0.f; state->current.orientation[3] = cosf(yaw); } }
    }
    *pose = state->current; return true;
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
        float cylinder_position[3] = {0.f, 0.f, radius};
        float a, b, c, discriminant, t0, t1, angle, half_angle;
        xr_vs_rotate(screen->orientation, cylinder_position, cylinder_position);
        cylinder_position[0] += screen->position[0];
        cylinder_position[1] += screen->position[1];
        cylinder_position[2] += screen->position[2];
        offset[0] = origin[0] - cylinder_position[0];
        offset[1] = origin[1] - cylinder_position[1];
        offset[2] = origin[2] - cylinder_position[2];
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
        hit->position[0] = cylinder_position[0] + offset[0];
        hit->position[1] = cylinder_position[1] + offset[1];
        hit->position[2] = cylinder_position[2] + offset[2];
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