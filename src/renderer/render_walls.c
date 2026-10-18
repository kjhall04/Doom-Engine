#include "render_walls.h"
#include "render_camera.h"
#include "render_portals.h"
#include "render_sector_traversal.h"
#include "render_planes.h"
#include "../textures/texture.h"

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

typedef struct ScreenVertex {
    float x;
    float y;
    float inverse_z;

    float u_over_z;
    float v_over_z;
} ScreenVertex;

typedef struct WallTextureVertex {
    float x;
    float y;
    float z;

    float u;
    float v;
} WallTextureVertex;

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


static Texture wall_texture = {0};
static bool wall_texture_initialized = false;

static void wall_initialize_texture(void) {
    if (wall_texture_initialized) {
        return;
    }

    wall_texture_initialized = true;

    if (!texture_load_bmp(
        &wall_texture,
        "assets/textures/brick.bmp"
    )) {
        SDL_Log("Using procedural wall texture fallback.");
    }
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


static uint32_t wall_sample_procedural(float u, float v) {
    /*
     * One texture repeat per world unit.
     * Each repeat contains a 4 x 4 brick pattern.
     */
    float wrapped_u = u - floorf(u);
    float wrapped_v = v - floorf(v);

    int row = (int)(wrapped_v * 4.0f);
    float stagger = (row & 1) ? 0.5f : 0.0f;

    float brick_u = wrapped_u * 4.0f + stagger;
    float brick_v = wrapped_v * 4.0f;

    float local_u = brick_u - floorf(brick_u);
    float local_v = brick_v - floorf(brick_v);

    if (local_u < 0.055f || local_v < 0.055f) {
        return 0xFF29252Au;
    }

    int brick = (int)floorf(brick_u) + row * 7;

    return (brick & 1)
        ? 0xFF93665Au
        : 0xFFB27B65u;
}


static uint32_t wall_sample_texture(float u, float v) {
    if (wall_texture.pixels) {
        return texture_sample(&wall_texture, u, v);
    }

    return wall_sample_procedural(u, v);
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

                        float u_over_z =
                            w0 * a->u_over_z +
                            w1 * b->u_over_z +
                            w2 * c->u_over_z;

                        float v_over_z =
                            w0 * a->v_over_z +
                            w1 * b->v_over_z +
                            w2 * c->v_over_z;

                        float u = u_over_z / inverse_z;
                        float v = v_over_z / inverse_z;

                        buffers.depth[index] = inverse_z;
                        buffers.pixels[index] = wall_sample_texture(u, v);
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


static WallTextureVertex wall_texture_intersection(
    WallTextureVertex a,
    WallTextureVertex b
) {
    float t =
        (WALL_NEAR_PLANE - a.z) / (b.z - a.z);

    WallTextureVertex result = {
        .x = a.x + (b.x - a.x) * t,
        .y = a.y + (b.y - a.y) * t,
        .z = WALL_NEAR_PLANE,
        .u = a.u + (b.u - a.u) * t,
        .v = a.v + (b.v - a.v) * t
    };

    return result;
}

static int wall_clip_textured_polygon(
    const WallTextureVertex *input,
    int input_count,
    WallTextureVertex *output
) {
    int output_count = 0;

    for (int i = 0; i < input_count; i++) {
        WallTextureVertex a = input[i];
        WallTextureVertex b = input[(i + 1) % input_count];

        bool a_inside = a.z >= WALL_NEAR_PLANE;
        bool b_inside = b.z >= WALL_NEAR_PLANE;

        if (a_inside && b_inside) {
            output[output_count++] = b;
        }
        else if (a_inside && !b_inside) {
            output[output_count++] =
                wall_texture_intersection(a, b);
        }
        else if (!a_inside && b_inside) {
            output[output_count++] =
                wall_texture_intersection(a, b);

            output[output_count++] = b;
        }
    }

    return output_count;
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

    float wall_length = hypotf(
        end->x - start->x,
        end->z - start->z
    );

    WallTextureVertex polygon[WALL_MAX_VERTICES] = {
        { a.x, a.y, a.z, 0.0f, bottom_height },
        { b.x, b.y, b.z, wall_length, bottom_height },
        { c.x, c.y, c.z, wall_length, top_height },
        { d.x, d.y, d.z, 0.0f, top_height }
    };

    WallTextureVertex clipped[WALL_MAX_VERTICES];

    int count = wall_clip_textured_polygon(
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

        projected[i].u_over_z =
            clipped[i].u * projected[i].inverse_z;

        projected[i].v_over_z =
            clipped[i].v * projected[i].inverse_z;

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

static void wall_draw_callback(
    const Player *player, const Wall *wall, const Level *level,
    const RenderPortalClip *clip, void *userdata
) {
    (void)userdata;
    wall_clip_range.left = clip->left;
    wall_clip_range.right = clip->right;
    wall_draw_segment(player, wall, level);
}

void render_walls_draw_solid(
    SDL_Renderer *renderer,
    const Player *player,
    const Level *level,
    int screen_width,
    int screen_height
) {
    wall_initialize_texture();

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

    /* Render sector floors and ceilings using shared inverse depth. */
    render_planes_draw(player, level, buffers.pixels, buffers.depth,
                       buffers.width, buffers.height);

    Uint64 stage_end = SDL_GetPerformanceCounter();
    clear_ticks += stage_end - stage_start;

    stage_start = SDL_GetPerformanceCounter();

    RenderPortalClip clip = {
        buffers.width, buffers.height,
        wall_clip_range.left, wall_clip_range.right,
        buffers.clip_top, buffers.clip_bottom
    };
    render_sector_traverse(player, level, &clip, wall_draw_callback, NULL);

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
