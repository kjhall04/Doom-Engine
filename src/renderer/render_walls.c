
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
} WallRenderBuffers;

static WallRenderBuffers buffers = {0};

static void wall_release_buffers(void) {
    if (buffers.texture != NULL) {
        SDL_DestroyTexture(buffers.texture);
    }

    free(buffers.pixels);
    free(buffers.depth);

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
        buffers.depth != NULL) {
        return true;
    }

    wall_release_buffers();

    size_t w = (size_t)width;
    size_t h = (size_t)height;

    if (w > SIZE_MAX / h / sizeof(uint32_t) ||
        w > SIZE_MAX / h / sizeof(float)) {
        return false;
    }

    size_t count = w * h;

    uint32_t *pixels = malloc(count * sizeof(uint32_t));
    float *depth = malloc(count * sizeof(float));

    if (pixels == NULL || depth == NULL) {
        free(pixels);
        free(depth);
        return false;
    }

    SDL_Texture *texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height
    );

    if (texture == NULL) {
        free(pixels);
        free(depth);
        return false;
    }

    if (!SDL_SetTextureScaleMode(
        texture,
        SDL_SCALEMODE_NEAREST
    )) {
        SDL_DestroyTexture(texture);
        free(pixels);
        free(depth);
        return false;
    }

    buffers.renderer = renderer;
    buffers.texture = texture;
    buffers.pixels = pixels;
    buffers.depth = depth;
    buffers.width = width;
    buffers.height = height;

    return true;
}

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

static void wall_draw_segment(
    const Player *player,
    const Wall *wall,
    const Level *level
) {
    if (wall->front_sector < 0 ||
        wall->front_sector >= level->sector_count ||
        wall->back_sector >= 0 ||
        wall->vertex_start < 0 ||
        wall->vertex_start >= level->vertex_count ||
        wall->vertex_end < 0 ||
        wall->vertex_end >= level->vertex_count) {
        return;
    }

    const Vertex *start = &level->vertices[wall->vertex_start];
    const Vertex *end = &level->vertices[wall->vertex_end];
    const Sector *sector = &level->sectors[wall->front_sector];

    RenderCameraPoint a = render_camera_transform(
        player, start->x, sector->floor_height, start->z
    );

    RenderCameraPoint b = render_camera_transform(
        player, end->x, sector->floor_height, end->z
    );

    RenderCameraPoint c = render_camera_transform(
        player, end->x, sector->ceiling_height, end->z
    );

    RenderCameraPoint d = render_camera_transform(
        player, start->x, sector->ceiling_height, start->z
    );

    WallVertex polygon[WALL_MAX_VERTICES] = {
        { a.x, a.y, a.z },
        { b.x, b.y, b.z },
        { c.x, c.y, c.z },
        { d.x, d.y, d.z }
    };

    WallVertex clipped[WALL_MAX_VERTICES];

    int count = wall_clip_polygon(polygon, 4, clipped);

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
            clipped[i].x * focal_length / clipped[i].z + center_x;

        projected[i].y =
            center_y - clipped[i].y * focal_length / clipped[i].z;

        projected[i].inverse_z = 1.0f / clipped[i].z;

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

    /*
     * Collect timing samples without logging every frame.
     * Logging itself can affect performance, so report averages every
     * 60 successful frames instead.
     */
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

    for (int i = 0; i < level->wall_count; i++) {
        wall_draw_segment(player, &level->walls[i], level);
    }

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
