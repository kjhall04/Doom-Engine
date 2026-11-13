#include "render_planes.h"
#include "render_camera.h"
#include "render_raster.h"
#include "render_lighting.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PLANE_MAX_VERTICES 128
#define PLANE_NEAR 0.1001f

typedef struct PlanePoint {
    float x;
    float z;
} PlanePoint;

typedef struct PlaneTexturePoint {
    RenderCameraPoint camera;

    float u;
    float v;
} PlaneTexturePoint;

typedef struct PlaneTriangle {
    PlanePoint p[3];
} PlaneTriangle;

/* Per-sector data that depends only on level geometry (vertices and walls),
 * never on the camera or on sector heights. */
typedef struct PlaneSectorGeometry {
    float min_x, max_x, min_z, max_z;
    bool present;

    PlaneTriangle *triangles;
    int triangle_count;
} PlaneSectorGeometry;

typedef struct PlaneGeometryCache {
    bool valid;
    uint64_t signature;
    int sector_count;
    PlaneSectorGeometry *sectors;
} PlaneGeometryCache;

static PlaneGeometryCache geometry_cache = {0};

/* A cheap, conservative camera-space rejection test for an entire sector.
 * The eight corners of the sector's world-space bounding prism are tested
 * against the near and horizontal/vertical view planes. If every corner is
 * outside any one plane, no point in the convex bounding prism is visible.
 * This never removes geometry that could enter the camera frustum.
 */
static bool sector_outside_view(
    const Player *player,
    const Sector *sector,
    const PlaneSectorGeometry *bounds,
    int width,
    int height
) {
    if (!bounds->present) return true;
    const float xs[2] = {bounds->min_x, bounds->max_x};
    const float zs[2] = {bounds->min_z, bounds->max_z};
    const float ys[2] = {sector->floor_height, sector->ceiling_height};
    /* The projection uses focal = width/2, hence horizontal |x| <= z.
       The vertical frustum is |y| <= z * height/width. */
    const float vertical_scale = (float)height / (float)width;
    unsigned int outside_near = 0, outside_left = 0, outside_right = 0;
    unsigned int outside_top = 0, outside_bottom = 0;
    for (int ix = 0; ix < 2; ++ix) {
        for (int iz = 0; iz < 2; ++iz) {
            for (int iy = 0; iy < 2; ++iy) {
                const RenderCameraPoint v = render_camera_transform(
                    player, xs[ix], ys[iy], zs[iz]);
                outside_near += v.z < PLANE_NEAR;
                outside_left += v.x < -v.z;
                outside_right += v.x > v.z;
                outside_top += v.y > v.z * vertical_scale;
                outside_bottom += v.y < -v.z * vertical_scale;
            }
        }
    }
    return outside_near == 8 || outside_left == 8 ||
           outside_right == 8 || outside_top == 8 || outside_bottom == 8;
}

static void bounds_include(PlaneSectorGeometry *b, const Vertex *v) {
    if (!b->present) {
        b->min_x = b->max_x = v->x;
        b->min_z = b->max_z = v->z;
        b->present = true;
        return;
    }
    if (v->x < b->min_x) b->min_x = v->x;
    if (v->x > b->max_x) b->max_x = v->x;
    if (v->z < b->min_z) b->min_z = v->z;
    if (v->z > b->max_z) b->max_z = v->z;
}

static float cross2(PlanePoint a, PlanePoint b, PlanePoint c) {
    return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}

/* Assemble a closed sector loop from the wall endpoints. */
static int sector_boundary(const Level *level, int sector, PlanePoint *out) {
    int starts[PLANE_MAX_VERTICES], ends[PLANE_MAX_VERTICES];
    bool used[PLANE_MAX_VERTICES] = {false};
    int count = 0;
    for (int i = 0; i < level->wall_count; ++i) {
        const Wall *w = &level->walls[i];
        if (w->front_sector != sector && w->back_sector != sector) continue;
        if (w->vertex_start < 0 || w->vertex_end < 0 ||
            w->vertex_start >= level->vertex_count ||
            w->vertex_end >= level->vertex_count || count >= PLANE_MAX_VERTICES)
            return 0;
        starts[count] = w->vertex_start;
        ends[count] = w->vertex_end;
        ++count;
    }
    if (count < 3) return 0;
    int first = starts[0], current = first;
    for (int n = 0; n < count; ++n) {
        int match = -1, next = -1;
        for (int j = 0; j < count; ++j) {
            if (used[j]) continue;
            if (starts[j] == current) { match = j; next = ends[j]; break; }
            if (ends[j] == current) { match = j; next = starts[j]; break; }
        }
        if (match < 0) return 0;
        used[match] = true;
        out[n] = (PlanePoint){level->vertices[current].x, level->vertices[current].z};
        current = next;
    }
    return current == first ? count : 0;
}

static bool inside_triangle(PlanePoint p, PlanePoint a, PlanePoint b,
                            PlanePoint c, float orientation) {
    const float eps = 0.000001f;
    return orientation * cross2(a,b,p) >= -eps &&
           orientation * cross2(b,c,p) >= -eps &&
           orientation * cross2(c,a,p) >= -eps;
}

static uint32_t plane_sample_floor(float u, float v) {
    float tile_u = u - floorf(u);
    float tile_v = v - floorf(v);

    // Dark grout between stone tiles.
    if (tile_u < 0.035f || tile_v < 0.035f) {
        return 0xFF292929u;
    }

    int tile_x = (int)floorf(u);
    int tile_y = (int)floorf(v);

    return ((tile_x + tile_y) & 1)
        ? 0xFF77756Eu
        : 0xFF99968Bu;
}

static uint32_t plane_sample_ceiling(float u, float v) {
    float tile_u = u - floorf(u);
    float tile_v = v - floorf(v);

    // Subtle seams between ceiling panels.
    if (tile_u < 0.02f || tile_v < 0.02f) {
        return 0xFF292938u;
    }

    int tile_x = (int)floorf(u);
    int tile_y = (int)floorf(v);

    return ((tile_x + tile_y) & 1)
        ? 0xFF454558u
        : 0xFF505066u;
}



/* ------------------------------------------------------------------ */
/* Geometry cache: triangulate each sector once, not every frame.      */
/* ------------------------------------------------------------------ */

static uint64_t hash_bytes(uint64_t h, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        h ^= bytes[i];
        h *= 1099511628211ull;
    }
    return h;
}

/* Fingerprint of everything the triangulation depends on. Sector heights
 * and textures are deliberately excluded; they are read live each frame. */
static uint64_t level_geometry_signature(const Level *level) {
    uint64_t h = 1469598103934665603ull;

    h = hash_bytes(h, &level->vertex_count, sizeof(level->vertex_count));
    h = hash_bytes(h, &level->wall_count, sizeof(level->wall_count));
    h = hash_bytes(h, &level->sector_count, sizeof(level->sector_count));

    for (int i = 0; i < level->vertex_count; ++i) {
        h = hash_bytes(h, &level->vertices[i], sizeof(Vertex));
    }

    for (int i = 0; i < level->wall_count; ++i) {
        const Wall *w = &level->walls[i];
        h = hash_bytes(h, &w->vertex_start, sizeof(int));
        h = hash_bytes(h, &w->vertex_end, sizeof(int));
        h = hash_bytes(h, &w->front_sector, sizeof(int));
        h = hash_bytes(h, &w->back_sector, sizeof(int));
    }

    return h;
}

static void geometry_cache_clear(void) {
    if (geometry_cache.sectors != NULL) {
        for (int i = 0; i < geometry_cache.sector_count; ++i) {
            free(geometry_cache.sectors[i].triangles);
        }
        free(geometry_cache.sectors);
    }

    geometry_cache.sectors = NULL;
    geometry_cache.sector_count = 0;
    geometry_cache.valid = false;
}

/* Ear-clip one sector into triangles. Same algorithm and checks as the
 * original per-frame code. */
static void triangulate_sector(
    const Level *level, int sector, PlaneSectorGeometry *geometry
) {
    PlanePoint boundary[PLANE_MAX_VERTICES];
    int n = sector_boundary(level, sector, boundary);
    if (n < 3) return;

    float signed_area = 0.0f;
    for (int i = 0; i < n; ++i) {
        PlanePoint a = boundary[i], b = boundary[(i+1)%n];
        signed_area += a.x*b.z - b.x*a.z;
    }
    if (fabsf(signed_area) < 0.000001f) return;
    float orientation = signed_area > 0 ? 1.0f : -1.0f;

    PlaneTriangle *triangles = malloc((size_t)(n - 2) * sizeof(*triangles));
    if (triangles == NULL) return;
    int triangle_count = 0;

    int remaining[PLANE_MAX_VERTICES];
    for (int i = 0; i < n; ++i) remaining[i] = i;
    int length = n;

    while (length > 2) {
        bool found = false;
        for (int i = 0; i < length; ++i) {
            int ia = remaining[(i+length-1)%length];
            int ib = remaining[i];
            int ic = remaining[(i+1)%length];
            PlanePoint a = boundary[ia], b = boundary[ib], c = boundary[ic];
            if (orientation*cross2(a,b,c) <= 0.000001f) continue;
            bool contains = false;
            for (int j = 0; j < length; ++j) {
                int id = remaining[j];
                if (id == ia || id == ib || id == ic) continue;
                if (inside_triangle(boundary[id],a,b,c,orientation)) {
                    contains = true; break;
                }
            }
            if (contains) continue;

            triangles[triangle_count++] = (PlaneTriangle){{a, b, c}};

            for (int j = i; j < length-1; ++j) remaining[j] = remaining[j+1];
            --length;
            found = true;
            break;
        }
        if (!found) break; /* Invalid or self-intersecting boundary. */
    }

    if (triangle_count == 0) {
        free(triangles);
        return;
    }

    geometry->triangles = triangles;
    geometry->triangle_count = triangle_count;
}

static bool geometry_cache_prepare(const Level *level) {
    uint64_t signature = level_geometry_signature(level);

    if (geometry_cache.valid &&
        geometry_cache.signature == signature &&
        geometry_cache.sector_count == level->sector_count) {
        return true;
    }

    geometry_cache_clear();

    if (level->sector_count <= 0) {
        return false;
    }

    geometry_cache.sectors =
        calloc((size_t)level->sector_count, sizeof(*geometry_cache.sectors));

    if (geometry_cache.sectors == NULL) {
        return false;
    }

    geometry_cache.sector_count = level->sector_count;

    /* Bounds in a single wall pass. */
    for (int i = 0; i < level->wall_count; ++i) {
        const Wall *wall = &level->walls[i];
        if (wall->vertex_start < 0 || wall->vertex_start >= level->vertex_count ||
            wall->vertex_end < 0 || wall->vertex_end >= level->vertex_count) continue;
        const Vertex *a = &level->vertices[wall->vertex_start];
        const Vertex *b = &level->vertices[wall->vertex_end];
        const int ids[2] = {wall->front_sector, wall->back_sector};
        for (int side = 0; side < 2; ++side) {
            int id = ids[side];
            if (id < 0 || id >= level->sector_count) continue;
            bounds_include(&geometry_cache.sectors[id], a);
            bounds_include(&geometry_cache.sectors[id], b);
        }
    }

    for (int sector = 0; sector < level->sector_count; ++sector) {
        triangulate_sector(level, sector, &geometry_cache.sectors[sector]);
    }

    geometry_cache.signature = signature;
    geometry_cache.valid = true;
    return true;
}


/* ------------------------------------------------------------------ */
/* Draw order                                                          */
/* ------------------------------------------------------------------ */

typedef struct PlaneOrderEntry {
    int sector;
    float distance;
} PlaneOrderEntry;

static PlaneOrderEntry *plane_order = NULL;
static int plane_order_capacity = 0;
static int plane_order_count = 0;

/* Collect the sectors to draw, sorted by squared distance from the player
 * to the sector's bounding rectangle (zero when inside it). */
static bool plane_order_prepare(
    const Player *player,
    const Level *level,
    const RenderSectorVisit *visits
) {
    if (level->sector_count > plane_order_capacity) {
        PlaneOrderEntry *grown = realloc(
            plane_order, (size_t)level->sector_count * sizeof(*grown));

        if (grown == NULL) {
            return false;
        }

        plane_order = grown;
        plane_order_capacity = level->sector_count;
    }

    plane_order_count = 0;

    for (int sector = 0; sector < level->sector_count; ++sector) {
        const PlaneSectorGeometry *g = &geometry_cache.sectors[sector];

        if (g->triangle_count == 0) continue;
        if (visits != NULL && visits[sector].left > visits[sector].right)
            continue;

        float dx = 0.0f, dz = 0.0f;
        if (player->x < g->min_x) dx = g->min_x - player->x;
        else if (player->x > g->max_x) dx = player->x - g->max_x;
        if (player->z < g->min_z) dz = g->min_z - player->z;
        else if (player->z > g->max_z) dz = player->z - g->max_z;

        PlaneOrderEntry entry = { sector, dx * dx + dz * dz };

        /* Insertion sort keeps the list ordered as it is built. */
        int j = plane_order_count++;
        while (j > 0 && plane_order[j - 1].distance > entry.distance) {
            plane_order[j] = plane_order[j - 1];
            --j;
        }
        plane_order[j] = entry;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

/* Clip a triangle to the near plane before perspective projection. */
static void draw_world_triangle(
    const Player *player,
    const PlaneTriangle *triangle,
    float elevation,
    const Texture *texture,
    RasterFallbackFn fallback,
    const RasterTarget *target
) {
    PlaneTexturePoint input[3];

    for (int i = 0; i < 3; ++i) {
        input[i].camera = render_camera_transform(
            player,
            triangle->p[i].x,
            elevation,
            triangle->p[i].z
        );

        // World aligned texture coordinates.
        input[i].u = triangle->p[i].x;
        input[i].v = triangle->p[i].z;
    }

    PlaneTexturePoint clipped[4];
    int n = 0;

    // Clip against the camera near plane.
    for (int i = 0; i < 3; ++i) {
        PlaneTexturePoint p = input[i];
        PlaneTexturePoint q = input[(i + 1) % 3];

        bool pin = p.camera.z >= PLANE_NEAR;
        bool qin = q.camera.z >= PLANE_NEAR;

        if (pin) {
            clipped[n++] = p;
        }

        if (pin != qin) {
            float t =
                (PLANE_NEAR - p.camera.z) /
                (q.camera.z - p.camera.z);

            PlaneTexturePoint intersection;

            intersection.camera.x =
                p.camera.x +
                t * (q.camera.x - p.camera.x);

            intersection.camera.y =
                p.camera.y +
                t * (q.camera.y - p.camera.y);

            intersection.camera.z = PLANE_NEAR;

            // Interpolate UVs at the clipping intersection.
            intersection.u = p.u + t * (q.u - p.u);
            intersection.v = p.v + t * (q.v - p.v);

            clipped[n++] = intersection;
        }
    }

    if (n < 3) {
        return;
    }

    float focal = target->width * 0.5f;

    RasterVertex screen[4];

    for (int i = 0; i < n; ++i) {
        float inverse_z = 1.0f / clipped[i].camera.z;

        screen[i].x =
            target->width * 0.5f +
            focal * clipped[i].camera.x * inverse_z;

        screen[i].y =
            target->height * 0.5f -
            focal * clipped[i].camera.y * inverse_z;

        screen[i].inverse_z = inverse_z;

        screen[i].u_over_z = clipped[i].u * inverse_z;
        screen[i].v_over_z = clipped[i].v * inverse_z;

        if (!isfinite(screen[i].x) ||
            !isfinite(screen[i].y)) {
            return;
        }
    }

    RasterTarget local_target=*target;
    local_target.world_origin[1]=elevation;
    // Triangulate the clipped polygon.
    for (int i = 1; i + 1 < n; ++i) {
        render_raster_triangle(
            &local_target,
            &screen[0],
            &screen[i],
            &screen[i + 1],
            texture,
            fallback
        );
    }
}

void render_planes_draw(
    const Player *player,
    const Level *level,
    const TextureManager *textures,
    uint32_t *pixels,
    float *depth,
    int width,
    int height,
    const RenderSectorVisit *visits
) {
    if (!player || !level || !pixels || !depth || width <= 0 || height <= 0 ||
        !level->vertices || !level->walls || !level->sectors) return;

    if (!geometry_cache_prepare(level)) return;

    RasterTarget target = {
        .pixels = pixels,
        .depth = depth,
        .width = width,
        .height = height,
        .x_min = 0,
        .x_max = width - 1,
        .y_min = 0,
        .y_max = height - 1,
        .clip_top = NULL,
        .clip_bottom = NULL
    };

    float *light_factors=render_light_factors(level);
    render_light_target(&target, level, light_factors);
    target.world_u[0]=1.0f;
    target.world_v[2]=1.0f;
    /* Draw nearest sectors first so farther floors and ceilings are
     * rejected by the depth test instead of being overwritten. */
    if (!plane_order_prepare(player, level, visits)) { free(light_factors); return; }

    for (int slot = 0; slot < plane_order_count; ++slot) {
        const int sector = plane_order[slot].sector;
        const PlaneSectorGeometry *geometry = &geometry_cache.sectors[sector];

        if (geometry->triangle_count == 0) continue;

        /* Sectors the portal traversal never reached are hidden behind
         * walls; skip their floors and ceilings entirely. */
        if (visits != NULL) {
            target.x_min = visits[sector].left;
            target.x_max = visits[sector].right;
        }

        const Sector *sector_data = &level->sectors[sector];
        target.light_r = (unsigned char)(sector_data->light_r * sector_data->light_level / 255);
        target.light_g = (unsigned char)(sector_data->light_g * sector_data->light_level / 255);
        target.light_b = (unsigned char)(sector_data->light_b * sector_data->light_level / 255);
        PointLight local_lights[64]; float local_factors[64];
        const float minv[3]={geometry->min_x,sector_data->floor_height,geometry->min_z};
        const float maxv[3]={geometry->max_x,sector_data->ceiling_height,geometry->max_z};
        render_light_cull(&target,level,light_factors,minv,maxv,local_lights,local_factors);


        if (sector_outside_view(player, sector_data, geometry, width, height))
            continue;

        const Texture *floor_texture = texture_manager_get(
            textures, sector_data->floor_texture_id);
        const Texture *ceiling_texture = texture_manager_get(
            textures, sector_data->ceiling_texture_id);

        for (int t = 0; t < geometry->triangle_count; ++t) {
            draw_world_triangle(
                player,
                &geometry->triangles[t],
                sector_data->floor_height,
                floor_texture,
                plane_sample_floor,
                &target
            );

            draw_world_triangle(
                player,
                &geometry->triangles[t],
                sector_data->ceiling_height,
                ceiling_texture,
                plane_sample_ceiling,
                &target
            );
        }
    }
    free(light_factors);
}
