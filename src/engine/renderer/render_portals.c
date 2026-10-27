#include "render_portals.h"
#include "render_camera.h"

#include <math.h>

#define PORTAL_NEAR_PLANE 0.1f
#define PORTAL_FOV 90.0f
#define PORTAL_PI 3.14159265f
#define PORTAL_MAX_VERTICES 8

typedef struct PortalScreenVertex {
    float x, y, inverse_z;
} PortalScreenVertex;

int render_portal_clip_polygon(
    const PortalVertex *input,
    int input_count,
    PortalVertex *output
) {
    int output_count = 0;

    for (int i = 0; i < input_count; i++) {
        PortalVertex current = input[i];
        PortalVertex next = input[(i + 1) % input_count];

        bool current_inside = current.z >= PORTAL_NEAR_PLANE;
        bool next_inside = next.z >= PORTAL_NEAR_PLANE;

        if (current_inside) {
            output[output_count++] = current;
        }

        if (current_inside != next_inside) {
            float t =
                (PORTAL_NEAR_PLANE - current.z) /
                (next.z - current.z);

            PortalVertex intersection = {
                current.x + (next.x - current.x) * t,
                current.y + (next.y - current.y) * t,
                PORTAL_NEAR_PLANE
            };

            output[output_count++] = intersection;
        }
    }

    return output_count;
}


/*
 * Restrict the current clipping window to the
 * projected opening of a portal.
 *
 * Returns true if at least one screen column
 * remains visible.
 *
 * The caller must save and restore the clipping
 * arrays around recursive sector rendering.
 */
bool render_portal_apply_clip(
    RenderPortalClip *clip,
    const Player *player,
    const Vertex *start,
    const Vertex *end,
    float opening_bottom,
    float opening_top
) {
    if (opening_top <= opening_bottom) {
        return false;
    }

    /*
     * Near a portal crossing, its projected polygon can collapse
     * as the camera moves through its plane. Keep the parent's
     * clipping window during that brief transition.
     *
     * Check proximity to the finite wall segment, not just the
     * infinite line containing it, to avoid bypassing unrelated
     * portals elsewhere on the same line.
     */
    const float crossing_distance = 0.2f;
    float wall_dx = end->x - start->x;
    float wall_dz = end->z - start->z;
    float wall_length_squared = wall_dx * wall_dx + wall_dz * wall_dz;

    if (wall_length_squared > 0.00000001f) {
        float player_dx = player->x - start->x;
        float player_dz = player->z - start->z;
        float along = (player_dx * wall_dx + player_dz * wall_dz) /
                      wall_length_squared;
        float inverse_length = 1.0f / sqrtf(wall_length_squared);
        float signed_distance = (wall_dx * player_dz - wall_dz * player_dx) *
                                inverse_length;
        float margin = crossing_distance * inverse_length;

        if (fabsf(signed_distance) < crossing_distance &&
            along >= -margin && along <= 1.0f + margin) {
            return true;
        }
    }

    RenderCameraPoint a = render_camera_transform(
        player, start->x, opening_bottom, start->z
    );

    RenderCameraPoint b = render_camera_transform(
        player, end->x, opening_bottom, end->z
    );

    RenderCameraPoint c = render_camera_transform(
        player, end->x, opening_top, end->z
    );

    RenderCameraPoint d = render_camera_transform(
        player, start->x, opening_top, start->z
    );

    PortalVertex polygon[PORTAL_MAX_VERTICES] = {
        { a.x, a.y, a.z },
        { b.x, b.y, b.z },
        { c.x, c.y, c.z },
        { d.x, d.y, d.z }
    };

    /*
     * Clip the portal against the near plane.
     */
    PortalVertex clipped[PORTAL_MAX_VERTICES];

    int count = render_portal_clip_polygon(
        polygon,
        4,
        clipped
    );

    if (count < 3) {
        return false;
    }

    float focal_length =
        (clip->width * 0.5f) /
        tanf((PORTAL_FOV * 0.5f) *
             (PORTAL_PI / 180.0f));

    float center_x = clip->width * 0.5f;
    float center_y = clip->height * 0.5f;

    PortalScreenVertex projected[PORTAL_MAX_VERTICES];

    float min_x = INFINITY;
    float max_x = -INFINITY;

    for (int i = 0; i < count; i++) {
        float z = clipped[i].z;

        if (z < PORTAL_NEAR_PLANE) {
            return false;
        }

        projected[i].x =
            center_x + clipped[i].x * focal_length / z;

        projected[i].y =
            center_y - clipped[i].y * focal_length / z;

        if (!isfinite(projected[i].x) ||
            !isfinite(projected[i].y)) {
            return false;
        }

        min_x = fminf(min_x, projected[i].x);
        max_x = fmaxf(max_x, projected[i].x);
    }

    if (max_x < clip->left ||
        min_x > clip->right) {
        return false;
    }

    float left_f = fmaxf(
        floorf(min_x) - 1.0f,
        (float)clip->left
    );

    float right_f = fminf(
        ceilf(max_x) + 1.0f,
        (float)clip->right
    );

    int left = (int)left_f;
    int right = (int)right_f;

    if (left > right) {
        return false;
    }

    bool any_visible = false;
    int visible_left = right;
    int visible_right = left;

    for (int x = left; x <= right; x++) {
        float sample_x = (float)x + 0.5f;

        float top = INFINITY;
        float bottom = -INFINITY;

        int intersections = 0;

        for (int i = 0; i < count; i++) {
            const PortalScreenVertex *p = &projected[i];
            const PortalScreenVertex *q =
                &projected[(i + 1) % count];

            float edge_min_x = fminf(p->x, q->x);
            float edge_max_x = fmaxf(p->x, q->x);

            if (sample_x < edge_min_x ||
                sample_x > edge_max_x) {
                continue;
            }

            float dx = q->x - p->x;

            if (fabsf(dx) < 0.00001f) {
                top = fminf(top, fminf(p->y, q->y));
                bottom = fmaxf(bottom, fmaxf(p->y, q->y));
                intersections++;
                continue;
            }

            float t = (sample_x - p->x) / dx;

            float y = p->y + t * (q->y - p->y);

            top = fminf(top, y);
            bottom = fmaxf(bottom, y);

            intersections++;
        }

        if (intersections == 0) {
            clip->top[x] = 1;
            clip->bottom[x] = 0;
            continue;
        }

        float top_f = fmaxf(
            floorf(top) - 1.0f,
            0.0f
        );

        float bottom_f = fminf(
            ceilf(bottom) + 1.0f,
            (float)(clip->height - 1)
        );

        int portal_top = (int)top_f;
        int portal_bottom = (int)bottom_f;

        if (portal_top > clip->top[x]) {
            clip->top[x] = portal_top;
        }

        if (portal_bottom < clip->bottom[x]) {
            clip->bottom[x] = portal_bottom;
        }

        if (clip->top[x] <= clip->bottom[x]) {
            any_visible = true;

            if (x < visible_left) visible_left = x;
            if (x > visible_right) visible_right = x;
        }
    }

    if (!any_visible) {
        return false;
    }

    clip->left = visible_left;
    clip->right = visible_right;

    return true;
}

