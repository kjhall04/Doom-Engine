#include "render_walls.h"
#include "render_camera.h"
#include "render_visibility.h"

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

/*
 * Horizontal clipping window.
 *
 * Both bounds are inclusive.
 */
typedef struct WallClipRange {
    int left;
    int right;
} WallClipRange;

static WallClipRange wall_clip_range = {0, 0};

/*
 * Allow drawing across the entire screen.
 */
static void wall_clip_reset(int screen_width) {
    wall_clip_range.left = 0;
    wall_clip_range.right = screen_width - 1;
}

/*
 * Restrict drawing to a horizontal screen range.
 */
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
 * Find the horizontal screen range occupied by a portal.
 *
 * Returns false if the portal is entirely behind the
 * camera or outside the current clipping window.
 */
static bool wall_portal_screen_range(
    const Player *player,
    const Vertex *start,
    const Vertex *end,
    int *out_left,
    int *out_right
) {
    /*
    * Horizontal portal projection is currently
    * designed for a level camera.
    *
    * With pitch, use the parent's full clipping range
    * until we implement projected portal polygons.
    */
    if (fabsf(player->pitch) > 0.001f) {
        *out_left = wall_clip_range.left;
        *out_right = wall_clip_range.right;
        return true;
    }

    RenderCameraPoint a = render_camera_transform(
        player, start->x, 0.0f, start->z
    );

    RenderCameraPoint b = render_camera_transform(
        player, end->x, 0.0f, end->z
    );

    /*
     * The horizontal projection does not depend on
     * the wall's world height, but camera pitch can
     * change the camera space depth.
     *
     * To remain conservative when looking up or down,
     * this first version uses a horizontal yaw projection.
     */
    float dx_a = start->x - player->x;
    float dz_a = start->z - player->z;

    float dx_b = end->x - player->x;
    float dz_b = end->z - player->z;

    float sin_yaw = sinf(player->yaw);
    float cos_yaw = cosf(player->yaw);

    a.x = dx_a * cos_yaw + dz_a * sin_yaw;
    a.z = dx_a * sin_yaw - dz_a * cos_yaw;

    b.x = dx_b * cos_yaw + dz_b * sin_yaw;
    b.z = dx_b * sin_yaw - dz_b * cos_yaw;

    if (a.z < WALL_NEAR_PLANE &&
        b.z < WALL_NEAR_PLANE) {
        return false;
    }

    /*
     * Clip one endpoint if it lies behind the near plane.
     */
    if (a.z < WALL_NEAR_PLANE) {
        float t = (WALL_NEAR_PLANE - a.z) /
                  (b.z - a.z);

        a.x += t * (b.x - a.x);
        a.z = WALL_NEAR_PLANE;
    }
    else if (b.z < WALL_NEAR_PLANE) {
        float t = (WALL_NEAR_PLANE - b.z) /
                  (a.z - b.z);

        b.x += t * (a.x - b.x);
        b.z = WALL_NEAR_PLANE;
    }

    float focal_length =
        (buffers.width * 0.5f) /
        tanf((WALL_FOV * 0.5f) *
             (WALL_PI / 180.0f));

    float center_x = buffers.width * 0.5f;

    float screen_a =
        center_x + a.x * focal_length / a.z;

    float screen_b =
        center_x + b.x * focal_length / b.z;

    if (!isfinite(screen_a) ||
        !isfinite(screen_b)) {
        return false;
    }

    float min_x = fminf(screen_a, screen_b);
    float max_x = fmaxf(screen_a, screen_b);

    /*
     * Reject completely offscreen portals before
     * converting to integer coordinates.
     */
    if (max_x < (float)wall_clip_range.left ||
        min_x > (float)wall_clip_range.right) {
        return false;
    }

    min_x = fmaxf(
        min_x, (float)wall_clip_range.left
    );

    max_x = fminf(
        max_x, (float)wall_clip_range.right
    );

    *out_left = (int)floorf(min_x);
    *out_right = (int)ceilf(max_x);

    return *out_left <= *out_right;
}

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
    * Intersect the triangle's horizontal bounds
    * with the current clipping window.
    */
    if (first_x < wall_clip_range.left) {
        first_x = wall_clip_range.left;
    }

    if (last_x > wall_clip_range.right) {
        last_x = wall_clip_range.right;
    }

    /*
    * Nothing remains inside the clipping window.
    */
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

/*
 * Draw a rectangular section of a wall between
 * two world heights.
 *
 * This works for:
 *   1. Solid walls
 *   2. Lower portal walls
 *   3. Upper portal walls
 */
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

    /*
     * Convert the clipped polygon into triangles.
     * The optimized rasterizer remains unchanged.
     */
    for (int i = 1; i < count - 1; i++) {
        wall_rasterize_triangle(
            &projected[0],
            &projected[i],
            &projected[i + 1]
        );
    }
}


/*
 * Decide which sections of the wall need drawing.
 */
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

    /*
     * A wall without a back sector is solid.
     */
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

    /*
     * Reject invalid back sector references.
     */
    if (wall->back_sector >= level->sector_count) {
        return;
    }

    const Sector *back =
        &level->sectors[wall->back_sector];

    /*
     * The lower wall occupies the space below
     * the higher of the two floors.
     */
    float lower_bottom = fminf(
        front->floor_height,
        back->floor_height
    );

    float lower_top = fmaxf(
        front->floor_height,
        back->floor_height
    );

    /*
     * The upper wall occupies the space above
     * the lower of the two ceilings.
     */
    float upper_bottom = fminf(
        front->ceiling_height,
        back->ceiling_height
    );

    float upper_top = fmaxf(
        front->ceiling_height,
        back->ceiling_height
    );

    /*
     * The portal opening is the intersection
     * of both sectors' vertical spaces.
     */
    float opening_bottom = fmaxf(
        front->floor_height,
        back->floor_height
    );

    float opening_top = fminf(
        front->ceiling_height,
        back->ceiling_height
    );

    /*
     * If the sectors have no vertical overlap,
     * the shared boundary is completely solid.
     */
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

    /*
     * Draw the lower wall if floor heights differ.
     */
    wall_draw_section(
        player,
        start,
        end,
        lower_bottom,
        lower_top
    );

    /*
     * Draw the upper wall if ceiling heights differ.
     */
    wall_draw_section(
        player,
        start,
        end,
        upper_bottom,
        upper_top
    );
}

/*
 * Render one sector and follow its visible portals.
 *
 * active_path prevents infinite loops when sectors
 * connect back to one another.
 */
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

    /*
     * First draw wall sections belonging to this sector.
     */
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

    /*
     * Then follow portals into neighboring sectors.
     */
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

        /*
         * A portal must have an actual vertical opening.
         */
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

        int portal_left;
        int portal_right;

        if (!wall_portal_screen_range(
            player,
            start,
            end,
            &portal_left,
            &portal_right
        )) {
            continue;
        }

        /*
         * Save the parent's clipping window.
         */
        WallClipRange previous_clip =
            wall_clip_range;

        /*
         * Restrict the neighboring sector to the
         * portion visible through this portal.
         */
        wall_clip_set(
            portal_left,
            portal_right
        );

        wall_render_sector(
            player,
            level,
            neighbor,
            active_path,
            depth + 1
        );

        /*
         * Restore the parent's clipping window.
         */
        wall_clip_range = previous_clip;
    }

    /*
     * This sector may be reached again through
     * another portal path.
     */
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

    /*
    * Start every frame with the full screen visible.
    */
    wall_clip_reset(screen_width);

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

    /*
    * Allocate one visibility flag per sector.
    */
    bool *visible_sectors = NULL;

    if (level->sector_count > 0) {
        visible_sectors = calloc(
            (size_t)level->sector_count,
            sizeof(*visible_sectors)
        );
    }

    stage_start = SDL_GetPerformanceCounter();

    /*
    * Track sectors currently on the portal traversal path.
    */
    bool *active_path = NULL;

    if (level->sector_count > 0) {
        active_path = calloc(
            (size_t)level->sector_count,
            sizeof(*active_path)
        );
    }

    /*
    * Start from the player's current sector.
    */
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
        /*
        * Fallback to the original drawing approach.
        */
        wall_clip_reset(screen_width);

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
