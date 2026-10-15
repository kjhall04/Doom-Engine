
#include "render_walls.h"
#include "render_camera.h"

#include <math.h>
#include <stdlib.h>

#define WALL_NEAR_PLANE 0.1f
#define WALL_FOV 90.0f
#define WALL_PI 3.14159265f

typedef struct WallVertex {
    float x;
    float y;
    float z;
} WallVertex;

static WallVertex wall_clip_near(
    WallVertex a,
    WallVertex b
) {
    float t =
        (WALL_NEAR_PLANE - a.z) /
        (b.z - a.z);

    WallVertex result = {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        WALL_NEAR_PLANE
    };

    return result;
}

static void wall_draw_segment(
    SDL_Renderer *renderer,
    const Player *player,
    const Wall *wall,
    const Level *level,
    int screen_width,
    int screen_height,
    float *depth_buffer
) {
    if (wall->front_sector < 0 ||
        wall->front_sector >= level->sector_count ||
        wall->back_sector >= 0) {
        return;
    }

    if (wall->vertex_start < 0 ||
        wall->vertex_start >= level->vertex_count ||
        wall->vertex_end < 0 ||
        wall->vertex_end >= level->vertex_count) {
        return;
    }

    const Vertex *start =
        &level->vertices[wall->vertex_start];

    const Vertex *end =
        &level->vertices[wall->vertex_end];

    const Sector *sector =
        &level->sectors[wall->front_sector];

    RenderCameraPoint a =
        render_camera_transform(
            player,
            start->x,
            sector->floor_height,
            start->z
        );

    RenderCameraPoint b =
        render_camera_transform(
            player,
            end->x,
            sector->floor_height,
            end->z
        );

    RenderCameraPoint c =
        render_camera_transform(
            player,
            end->x,
            sector->ceiling_height,
            end->z
        );

    RenderCameraPoint d =
        render_camera_transform(
            player,
            start->x,
            sector->ceiling_height,
            start->z
        );

    WallVertex bottom_a = { a.x, a.y, a.z };
    WallVertex bottom_b = { b.x, b.y, b.z };
    WallVertex top_b = { c.x, c.y, c.z };
    WallVertex top_a = { d.x, d.y, d.z };

    if (bottom_a.z < WALL_NEAR_PLANE &&
        bottom_b.z < WALL_NEAR_PLANE) {
        return;
    }

    if (bottom_a.z < WALL_NEAR_PLANE) {
        bottom_a = wall_clip_near(bottom_a, bottom_b);
        top_a = wall_clip_near(top_a, top_b);
    }

    if (bottom_b.z < WALL_NEAR_PLANE) {
        bottom_b = wall_clip_near(bottom_b, bottom_a);
        top_b = wall_clip_near(top_b, top_a);
    }

    float focal_length =
        (screen_width * 0.5f) /
        tanf((WALL_FOV * 0.5f) *
             (WALL_PI / 180.0f));

    float screen_center_x = screen_width * 0.5f;
    float screen_center_y = screen_height * 0.5f;

    float x1 =
        bottom_a.x * focal_length / bottom_a.z +
        screen_center_x;

    float x2 =
        bottom_b.x * focal_length / bottom_b.z +
        screen_center_x;

    if (x1 > x2) {
        float temp_x = x1;
        x1 = x2;
        x2 = temp_x;

        WallVertex temp = bottom_a;
        bottom_a = bottom_b;
        bottom_b = temp;

        temp = top_a;
        top_a = top_b;
        top_b = temp;
    }

    int first_x = (int)ceilf(x1);
    int last_x = (int)floorf(x2);

    if (first_x < 0) {
        first_x = 0;
    }

    if (last_x >= screen_width) {
        last_x = screen_width - 1;
    }

    if (last_x < first_x || x2 <= x1) {
        return;
    }

    SDL_SetRenderDrawColor(
        renderer,
        220,
        220,
        220,
        255
    );

    for (int x = first_x; x <= last_x; x++) {
        float screen_x = (float)x + 0.5f;
        float t = (screen_x - x1) / (x2 - x1);

        if (t < 0.0f || t > 1.0f) {
            continue;
        }

        float inverse_z =
            (1.0f - t) / bottom_a.z +
            t / bottom_b.z;

        if (inverse_z <= 0.0f) {
            continue;
        }

        float depth = 1.0f / inverse_z;

        if (depth >= depth_buffer[x]) {
            continue;
        }

        float top_inverse_z =
            (1.0f - t) / top_a.z +
            t / top_b.z;

        float bottom_y =
            (1.0f - t) * bottom_a.y / bottom_a.z +
            t * bottom_b.y / bottom_b.z;

        float top_y =
            (1.0f - t) * top_a.y / top_a.z +
            t * top_b.y / top_b.z;

        float projected_bottom =
            screen_center_y -
            bottom_y * focal_length;

        float projected_top =
            screen_center_y -
            top_y * focal_length;

        if (!isfinite(projected_top) ||
            !isfinite(projected_bottom) ||
            !isfinite(top_inverse_z)) {
            continue;
        }

        int y_top = (int)ceilf(projected_top);
        int y_bottom = (int)floorf(projected_bottom);

        if (y_top < 0) {
            y_top = 0;
        }

        if (y_bottom >= screen_height) {
            y_bottom = screen_height - 1;
        }

        if (y_bottom < y_top) {
            continue;
        }

        if (SDL_RenderLine(
            renderer,
            (float)x,
            (float)y_top,
            (float)x,
            (float)y_bottom
        )) {
            depth_buffer[x] = depth;
        }
    }
}

void render_walls_draw_solid(
    SDL_Renderer *renderer,
    const Player *player,
    const Level *level,
    int screen_width,
    int screen_height
) {
    if (renderer == NULL ||
        player == NULL ||
        level == NULL ||
        screen_width <= 0 ||
        screen_height <= 0) {
        return;
    }

    float *depth_buffer =
        malloc(sizeof(float) * (size_t)screen_width);

    if (depth_buffer == NULL) {
        return;
    }

    for (int x = 0; x < screen_width; x++) {
        depth_buffer[x] = INFINITY;
    }

    for (int i = 0; i < level->wall_count; i++) {
        wall_draw_segment(
            renderer,
            player,
            &level->walls[i],
            level,
            screen_width,
            screen_height,
            depth_buffer
        );
    }

    free(depth_buffer);
}
