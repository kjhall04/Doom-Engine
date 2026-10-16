#include "render_walls.h"
#include "render_camera.h"

#include <SDL3/SDL_timer.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define WALL_NEAR_PLANE 0.1f
#define WALL_FOV 90.0f
#define WALL_PI 3.14159265f
#define WALL_MAX_VERTICES 8

#define COLOR_BACKGROUND 0xFF1C1414u
#define COLOR_WALL       0xFFDCDCDCu

typedef struct WallVertex {
    float x;
    float y;
    float z;
} WallVertex;

typedef struct ScreenVertex {
    float x;
    float y;
    float inverse_z;
} ScreenVertex;

typedef struct WallRenderBuffers {
    SDL_Renderer *renderer;
    SDL_Texture *texture;

    uint32_t *pixels;
    float *depth;

    int width;
    int height;

    int *clip_top;
    int *clip_bottom;
} WallRenderBuffers;

static WallRenderBuffers buffers = {0};

typedef struct WallClipRange {
    int left;
    int right;
} WallClipRange;

static WallClipRange wall_clip_range = {0, 0};

static int wall_clip_polygon(
    const WallVertex *input,
    int input_count,
    WallVertex *output
) {
    int output_count = 0;

    for (int i = 0; i < input_count; i++) {
        WallVertex current = input[i];
        WallVertex next = input[(i + 1) % input_count];

        bool current_inside = current.z >= WALL_NEAR_PLANE;
        bool next_inside = next.z >= WALL_NEAR_PLANE;

        if (current_inside) {
            output[output_count++] = current;
        }

        if (current_inside != next_inside) {
            float t =
                (WALL_NEAR_PLANE - current.z) /
                (next.z - current.z);

            WallVertex intersection = {
                current.x + (next.x - current.x) * t,
                current.y + (next.y - current.y) * t,
                WALL_NEAR_PLANE
            };

            output[output_count++] = intersection;
        }
    }

    return output_count;
}

static void wall_clip_reset(int screen_width) {
    wall_clip_range.left = 0;
    wall_clip_range.right = screen_width - 1;
}

static void wall_clip_vertical_reset(void) {
    for (int x = 0; x < buffers.width; x++) {
        buffers.clip_top[x] = 0;
        buffers.clip_bottom[x] = buffers.height - 1;
    }
}

static void wall_clip_set(int left, int right) {
    if (left < 0) {
        left = 0;
    }

    if (right >= buffers.width) {
        right = buffers.width - 1;
    }

    wall_clip_range.left = left;
    wall_clip_range.right = right;
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
static bool wall_portal_apply_clip(
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

    WallVertex polygon[WALL_MAX_VERTICES] = {
        { a.x, a.y, a.z },
        { b.x, b.y, b.z },
        { c.x, c.y, c.z },
        { d.x, d.y, d.z }
    };

    /*
     * Clip the portal against the near plane.
     */
    WallVertex clipped[WALL_MAX_VERTICES];

    int count = wall_clip_polygon(
        polygon,
        4,
        clipped
    );

    if (count < 3) {
        return false;
    }

    float focal_length =
        (buffers.width * 0.5f) /
        tanf((WALL_FOV * 0.5f) *
             (WALL_PI / 180.0f));

    float center_x = buffers.width * 0.5f;
    float center_y = buffers.height * 0.5f;

    ScreenVertex projected[WALL_MAX_VERTICES];

    float min_x = INFINITY;
    float max_x = -INFINITY;

    for (int i = 0; i < count; i++) {
        float z = clipped[i].z;

        if (z < WALL_NEAR_PLANE) {
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

    if (max_x < wall_clip_range.left ||
        min_x > wall_clip_range.right) {
        return false;
    }

    float left_f = fmaxf(
        floorf(min_x) - 1.0f,
        (float)wall_clip_range.left
    );

    float right_f = fminf(
        ceilf(max_x) + 1.0f,
        (float)wall_clip_range.right
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
            const ScreenVertex *p = &projected[i];
            const ScreenVertex *q =
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
            buffers.clip_top[x] = 1;
            buffers.clip_bottom[x] = 0;
            continue;
        }

        float top_f = fmaxf(
            floorf(top) - 1.0f,
            0.0f
        );

        float bottom_f = fminf(
            ceilf(bottom) + 1.0f,
            (float)(buffers.height - 1)
        );

        int portal_top = (int)top_f;
        int portal_bottom = (int)bottom_f;

        if (portal_top > buffers.clip_top[x]) {
            buffers.clip_top[x] = portal_top;
        }

        if (portal_bottom < buffers.clip_bottom[x]) {
            buffers.clip_bottom[x] = portal_bottom;
        }

        if (buffers.clip_top[x] <= buffers.clip_bottom[x]) {
            any_visible = true;

            if (x < visible_left) visible_left = x;
            if (x > visible_right) visible_right = x;
        }
    }

    if (!any_visible) {
        return false;
    }

    wall_clip_set(visible_left, visible_right);

    return true;
}

static void wall_release_buffers(void) {
    if (buffers.texture != NULL) {
        SDL_DestroyTexture(buffers.texture);
    }

    free(buffers.pixels);
    free(buffers.depth);

    free(buffers.clip_top);
    free(buffers.clip_bottom);

    buffers.clip_top = NULL;
    buffers.clip_bottom = NULL;
    buffers.texture = NULL;
    buffers.pixels = NULL;
    buffers.depth = NULL;
    buffers.width = 0;
    buffers.height = 0;
    buffers.renderer = NULL;
}

static bool wall_prepare_buffers(
    SDL_Renderer *renderer,
    int width,
    int height
) {
    if (buffers.renderer == renderer &&
        buffers.width == width &&
        buffers.height == height &&
        buffers.texture != NULL &&
        buffers.pixels != NULL &&
        buffers.depth != NULL &&
        buffers.clip_top != NULL &&
        buffers.clip_bottom != NULL) {
        return true;
    }

    wall_release_buffers();

    size_t w = (size_t)width;
    size_t h = (size_t)height;

    if (w > SIZE_MAX / h / sizeof(uint32_t) ||
        w > SIZE_MAX / h / sizeof(float) ||
        w > SIZE_MAX / sizeof(*buffers.clip_top) ||
        w > SIZE_MAX / sizeof(*buffers.clip_bottom)) {
        return false;
    }

    size_t count = w * h;

    buffers.pixels = malloc(count * sizeof(*buffers.pixels));
    buffers.depth = malloc(count * sizeof(*buffers.depth));
    buffers.clip_top = malloc(w * sizeof(*buffers.clip_top));
    buffers.clip_bottom = malloc(w * sizeof(*buffers.clip_bottom));

    if (buffers.pixels == NULL ||
        buffers.depth == NULL ||
        buffers.clip_top == NULL ||
        buffers.clip_bottom == NULL) {
        wall_release_buffers();
        return false;
    }

    buffers.texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height
    );

    if (buffers.texture == NULL) {
        wall_release_buffers();
        return false;
    }

    if (!SDL_SetTextureScaleMode(
        buffers.texture,
        SDL_SCALEMODE_NEAREST
    )) {
        wall_release_buffers();
        return false;
    }

    buffers.renderer = renderer;
    buffers.width = width;
    buffers.height = height;

    return true;
}



static float wall_edge(
    const ScreenVertex *a,
    const ScreenVertex *b,
    float x,
    float y
) {
    return (b->x - a->x) * (y - a->y)
         - (b->y - a->y) * (x - a->x);
}

static void wall_rasterize_triangle(
    const ScreenVertex *a,
    const ScreenVertex *b,
    const ScreenVertex *c
) {
    float area = wall_edge(a, b, c->x, c->y);

    if (!isfinite(area) || fabsf(area) < 0.00001f) {
        return;
    }

    float min_x = fminf(a->x, fminf(b->x, c->x));
    float max_x = fmaxf(a->x, fmaxf(b->x, c->x));
    float min_y = fminf(a->y, fminf(b->y, c->y));
    float max_y = fmaxf(a->y, fmaxf(b->y, c->y));

    if (max_x < 0.0f || max_y < 0.0f ||
        min_x >= (float)buffers.width ||
        min_y >= (float)buffers.height) {
        return;
    }

    if (min_x < 0.0f) min_x = 0.0f;
    if (min_y < 0.0f) min_y = 0.0f;

    if (max_x > (float)(buffers.width - 1)) {
        max_x = (float)(buffers.width - 1);
    }

    if (max_y > (float)(buffers.height - 1)) {
        max_y = (float)(buffers.height - 1);
    }

    int first_x = (int)floorf(min_x);
    int last_x = (int)ceilf(max_x);
    int first_y = (int)floorf(min_y);
    int last_y = (int)ceilf(max_y);

    if (first_x < wall_clip_range.left) {
        first_x = wall_clip_range.left;
    }

    if (last_x > wall_clip_range.right) {
        last_x = wall_clip_range.right;
    }

    if (first_x > last_x) {
        return;
    }

    /*
     * Evaluate the three edge functions once at the first pixel center.
     * Moving one pixel right changes each edge value by a constant amount,
     * and moving one row down does the same. This avoids recalculating the
     * edge functions from scratch for every pixel.
     */
    float start_x = (float)first_x + 0.5f;
    float start_y = (float)first_y + 0.5f;

    float row_w0 = wall_edge(b, c, start_x, start_y);
    float row_w1 = wall_edge(c, a, start_x, start_y);
    float row_w2 = wall_edge(a, b, start_x, start_y);

    float w0_step_x = -(c->y - b->y);
    float w1_step_x = -(a->y - c->y);
    float w2_step_x = -(b->y - a->y);

    float w0_step_y = c->x - b->x;
    float w1_step_y = a->x - c->x;
    float w2_step_y = b->x - a->x;

    float inverse_area = 1.0f / area;
    const float epsilon = -0.00001f;

    for (int y = first_y; y <= last_y; y++) {
        float edge_w0 = row_w0;
        float edge_w1 = row_w1;
        float edge_w2 = row_w2;

        size_t index =
            (size_t)y * (size_t)buffers.width + (size_t)first_x;

        for (int x = first_x; x <= last_x; x++) {
            float w0 = edge_w0 * inverse_area;
            float w1 = edge_w1 * inverse_area;
            float w2 = edge_w2 * inverse_area;

            /* Keep edge increments running even for clipped pixels. */
            if (y >= buffers.clip_top[x] &&
                y <= buffers.clip_bottom[x]) {
                if (w0 >= epsilon && w1 >= epsilon && w2 >= epsilon) {
                    float inverse_z =
                        w0 * a->inverse_z +
                        w1 * b->inverse_z +
                        w2 * c->inverse_z;

                    /*
                     * Store inverse depth. Larger values are closer to the
                     * camera, so this avoids a reciprocal for every covered
                     * pixel while preserving the depth test result.
                     */
                    if (inverse_z > 0.0f && isfinite(inverse_z) &&
                        inverse_z > buffers.depth[index]) {
                        buffers.depth[index] = inverse_z;
                        buffers.pixels[index] = COLOR_WALL;
                    }
                }
            }

            edge_w0 += w0_step_x;
            edge_w1 += w1_step_x;
            edge_w2 += w2_step_x;
            index++;
        }

        row_w0 += w0_step_y;
        row_w1 += w1_step_y;
        row_w2 += w2_step_y;
    }
}

static void wall_draw_section(
    const Player *player,
    const Vertex *start,
    const Vertex *end,
    float bottom_height,
    float top_height
) {
    if (top_height <= bottom_height) {
        return;
    }

    RenderCameraPoint a = render_camera_transform(
        player,
        start->x,
        bottom_height,
        start->z
    );

    RenderCameraPoint b = render_camera_transform(
        player,
        end->x,
        bottom_height,
        end->z
    );

    RenderCameraPoint c = render_camera_transform(
        player,
        end->x,
        top_height,
        end->z
    );

    RenderCameraPoint d = render_camera_transform(
        player,
        start->x,
        top_height,
        start->z
    );

    WallVertex polygon[WALL_MAX_VERTICES] = {
        { a.x, a.y, a.z },
        { b.x, b.y, b.z },
        { c.x, c.y, c.z },
        { d.x, d.y, d.z }
    };

    WallVertex clipped[WALL_MAX_VERTICES];

    int count = wall_clip_polygon(
        polygon,
        4,
        clipped
    );

    if (count < 3) {
        return;
    }

    float focal_length =
        (buffers.width * 0.5f) /
        tanf((WALL_FOV * 0.5f) * (WALL_PI / 180.0f));

    float center_x = buffers.width * 0.5f;
    float center_y = buffers.height * 0.5f;

    ScreenVertex projected[WALL_MAX_VERTICES];

    for (int i = 0; i < count; i++) {
        if (clipped[i].z < WALL_NEAR_PLANE) {
            return;
        }

        projected[i].x =
            clipped[i].x * focal_length /
            clipped[i].z + center_x;

        projected[i].y =
            center_y -
            clipped[i].y * focal_length /
            clipped[i].z;

        projected[i].inverse_z =
            1.0f / clipped[i].z;

        if (!isfinite(projected[i].x) ||
            !isfinite(projected[i].y)) {
            return;
        }
    }

    for (int i = 1; i < count - 1; i++) {
        wall_rasterize_triangle(
            &projected[0],
            &projected[i],
            &projected[i + 1]
        );
    }
}

static void wall_draw_segment(
    const Player *player,
    const Wall *wall,
    const Level *level
) {
    if (wall->front_sector < 0 ||
        wall->front_sector >= level->sector_count ||
        wall->vertex_start < 0 ||
        wall->vertex_start >= level->vertex_count ||
        wall->vertex_end < 0 ||
        wall->vertex_end >= level->vertex_count) {
        return;
    }

    const Vertex *start =
        &level->vertices[wall->vertex_start];

    const Vertex *end =
        &level->vertices[wall->vertex_end];

    const Sector *front =
        &level->sectors[wall->front_sector];

    if (wall->back_sector < 0) {
        wall_draw_section(
            player,
            start,
            end,
            front->floor_height,
            front->ceiling_height
        );

        return;
    }

    if (wall->back_sector >= level->sector_count) {
        return;
    }

    const Sector *back =
        &level->sectors[wall->back_sector];

    float lower_bottom = fminf(
        front->floor_height,
        back->floor_height
    );

    float lower_top = fmaxf(
        front->floor_height,
        back->floor_height
    );

    float upper_bottom = fminf(
        front->ceiling_height,
        back->ceiling_height
    );

    float upper_top = fmaxf(
        front->ceiling_height,
        back->ceiling_height
    );

    float opening_bottom = fmaxf(
        front->floor_height,
        back->floor_height
    );

    float opening_top = fminf(
        front->ceiling_height,
        back->ceiling_height
    );

    if (opening_bottom >= opening_top) {
        wall_draw_section(
            player,
            start,
            end,
            fminf(
                front->floor_height,
                back->floor_height
            ),
            fmaxf(
                front->ceiling_height,
                back->ceiling_height
            )
        );

        return;
    }

    wall_draw_section(
        player,
        start,
        end,
        lower_bottom,
        lower_top
    );

    wall_draw_section(
        player,
        start,
        end,
        upper_bottom,
        upper_top
    );
}

static void wall_render_sector(
    const Player *player,
    const Level *level,
    int sector_index,
    bool *active_path,
    int depth
) {
    if (sector_index < 0 ||
        sector_index >= level->sector_count ||
        active_path[sector_index] ||
        depth >= level->sector_count ||
        wall_clip_range.left > wall_clip_range.right) {
        return;
    }

    active_path[sector_index] = true;

    for (int i = 0; i < level->wall_count; i++) {
        const Wall *wall = &level->walls[i];

        if (wall->front_sector != sector_index &&
            wall->back_sector != sector_index) {
            continue;
        }

        wall_draw_segment(
            player,
            wall,
            level
        );
    }

    for (int i = 0; i < level->wall_count; i++) {
        const Wall *wall = &level->walls[i];

        int neighbor = -1;

        if (wall->front_sector == sector_index) {
            neighbor = wall->back_sector;
        }
        else if (wall->back_sector == sector_index) {
            neighbor = wall->front_sector;
        }

        if (neighbor < 0 ||
            neighbor >= level->sector_count ||
            active_path[neighbor]) {
            continue;
        }

        if (wall->vertex_start < 0 ||
            wall->vertex_start >= level->vertex_count ||
            wall->vertex_end < 0 ||
            wall->vertex_end >= level->vertex_count) {
            continue;
        }

        const Sector *current =
            &level->sectors[sector_index];

        const Sector *next =
            &level->sectors[neighbor];

        float opening_bottom = fmaxf(
            current->floor_height,
            next->floor_height
        );

        float opening_top = fminf(
            current->ceiling_height,
            next->ceiling_height
        );

        if (opening_bottom >= opening_top) {
            continue;
        }

        const Vertex *start =
            &level->vertices[wall->vertex_start];

        const Vertex *end =
            &level->vertices[wall->vertex_end];

        WallClipRange previous_clip = wall_clip_range;

        int saved_left = wall_clip_range.left;
        int saved_right = wall_clip_range.right;

        size_t column_count =
            (size_t)(saved_right - saved_left + 1);

        if (column_count > SIZE_MAX / sizeof(int)) {
            continue;
        }

        int *saved_top = malloc(
            column_count * sizeof(*saved_top)
        );

        int *saved_bottom = malloc(
            column_count * sizeof(*saved_bottom)
        );

        if (saved_top == NULL || saved_bottom == NULL) {
            free(saved_top);
            free(saved_bottom);
            continue;
        }

        memcpy(
            saved_top,
            buffers.clip_top + saved_left,
            column_count * sizeof(*saved_top)
        );

        memcpy(
            saved_bottom,
            buffers.clip_bottom + saved_left,
            column_count * sizeof(*saved_bottom)
        );

        bool portal_visible = wall_portal_apply_clip(
            player,
            start,
            end,
            opening_bottom,
            opening_top
        );

        if (portal_visible) {
            
            wall_render_sector(
                player,
                level,
                neighbor,
                active_path,
                depth + 1
            );
        }

        /*
        * Restore the parent sector's clipping state.
        *
        * This is essential because the next portal
        * may have a completely different opening.
        */
        memcpy(
            buffers.clip_top + saved_left,
            saved_top,
            column_count * sizeof(*saved_top)
        );

        memcpy(
            buffers.clip_bottom + saved_left,
            saved_bottom,
            column_count * sizeof(*saved_bottom)
        );

        wall_clip_range = previous_clip;

        free(saved_top);
        free(saved_bottom);
    }

    active_path[sector_index] = false;
}

void render_walls_draw_solid(
    SDL_Renderer *renderer,
    const Player *player,
    const Level *level,
    int screen_width,
    int screen_height
) {
    if (renderer == NULL || player == NULL || level == NULL ||
        screen_width <= 0 || screen_height <= 0 ||
        level->vertices == NULL || level->walls == NULL ||
        level->sectors == NULL) {
        return;
    }

    if (!wall_prepare_buffers(renderer, screen_width, screen_height)) {
        return;
    }

    wall_clip_reset(screen_width);
    wall_clip_vertical_reset();

    static Uint64 clear_ticks = 0;
    static Uint64 raster_ticks = 0;
    static Uint64 upload_ticks = 0;
    static Uint64 draw_ticks = 0;
    static unsigned int sample_frames = 0;

    Uint64 stage_start = SDL_GetPerformanceCounter();

    size_t pixel_count =
        (size_t)screen_width * (size_t)screen_height;

    /*
     * Clear inverse depth in one contiguous operation. An all-zero
     * representation of float is 0.0f on our supported platform.
     */
    memset(buffers.depth, 0, pixel_count * sizeof(*buffers.depth));

    /* Background is a nonzero ARGB color, so fill color pixels explicitly. */
    for (size_t i = 0; i < pixel_count; i++) {
        buffers.pixels[i] = COLOR_BACKGROUND;
    }

    Uint64 stage_end = SDL_GetPerformanceCounter();
    clear_ticks += stage_end - stage_start;

    stage_start = SDL_GetPerformanceCounter();

    bool *active_path = NULL;

    if (level->sector_count > 0) {
        active_path = calloc(
            (size_t)level->sector_count,
            sizeof(*active_path)
        );
    }

    if (active_path != NULL &&
        player->sector >= 0 &&
        player->sector < level->sector_count) {

        wall_render_sector(
            player,
            level,
            player->sector,
            active_path,
            0
        );
    }
    else {
        
        for (int i = 0; i < level->wall_count; i++) {
            wall_draw_segment(
                player,
                &level->walls[i],
                level
            );
        }
    }

    free(active_path);

    stage_end = SDL_GetPerformanceCounter();
    raster_ticks += stage_end - stage_start;

    stage_start = SDL_GetPerformanceCounter();

    if (!SDL_UpdateTexture(
        buffers.texture,
        NULL,
        buffers.pixels,
        screen_width * (int)sizeof(uint32_t)
    )) {
        return;
    }

    stage_end = SDL_GetPerformanceCounter();
    upload_ticks += stage_end - stage_start;

    stage_start = SDL_GetPerformanceCounter();

    SDL_RenderTexture(
        renderer,
        buffers.texture,
        NULL,
        NULL
    );

    stage_end = SDL_GetPerformanceCounter();
    draw_ticks += stage_end - stage_start;

    sample_frames++;

    if (sample_frames >= 60) {
        double ticks_to_ms =
            1000.0 / (double)SDL_GetPerformanceFrequency();
        double divisor = (double)sample_frames;

        double clear_ms = (double)clear_ticks * ticks_to_ms / divisor;
        double raster_ms = (double)raster_ticks * ticks_to_ms / divisor;
        double upload_ms = (double)upload_ticks * ticks_to_ms / divisor;
        double draw_ms = (double)draw_ticks * ticks_to_ms / divisor;
        double total_ms = clear_ms + raster_ms + upload_ms + draw_ms;

        SDL_Log(
            "Wall renderer average over %u frames | "
            "Clear: %.2f ms | Rasterize: %.2f ms | "
            "Upload: %.2f ms | Draw: %.2f ms | Total: %.2f ms",
            sample_frames,
            clear_ms,
            raster_ms,
            upload_ms,
            draw_ms,
            total_ms
        );

        clear_ticks = 0;
        raster_ticks = 0;
        upload_ticks = 0;
        draw_ticks = 0;
        sample_frames = 0;
    }
}
