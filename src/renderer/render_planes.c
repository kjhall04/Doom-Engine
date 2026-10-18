#include "render_planes.h"
#include "render_camera.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define PLANE_MAX_VERTICES 128
#define PLANE_NEAR 0.1001f

typedef struct PlanePoint { 
    float x;
    float z; 
} PlanePoint;

typedef struct PlaneScreenPoint {
    float x;
    float y;
    float inverse_z;

    float u_over_z;
    float v_over_z;
} PlaneScreenPoint;

typedef struct PlaneTexturePoint {
    RenderCameraPoint camera;

    float u;
    float v;
} PlaneTexturePoint;

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

static float screen_edge(PlaneScreenPoint a, PlaneScreenPoint b, float x, float y) {
    return (b.x-a.x)*(y-a.y) - (b.y-a.y)*(x-a.x);
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

static void raster_triangle(
    PlaneScreenPoint a,
    PlaneScreenPoint b,
    PlaneScreenPoint c,
    bool is_ceiling,
    uint32_t *pixels,
    float *depth,
    int width,
    int height
) {
    float area = screen_edge(a,b,c.x,c.y);
    if (!isfinite(area) || fabsf(area) < 0.00001f) return;
    if (area < 0.0f) { PlaneScreenPoint temp = b; b = c; c = temp; area = -area; }
    float minx = fminf(a.x, fminf(b.x,c.x));
    float maxx = fmaxf(a.x, fmaxf(b.x,c.x));
    float miny = fminf(a.y, fminf(b.y,c.y));
    float maxy = fmaxf(a.y, fmaxf(b.y,c.y));
    if (maxx < 0 || maxy < 0 || minx >= width || miny >= height) return;
    int x0 = (int)fmaxf(0, floorf(minx));
    int x1 = (int)fminf((float)(width-1), ceilf(maxx));
    int y0 = (int)fmaxf(0, floorf(miny));
    int y1 = (int)fminf((float)(height-1), ceilf(maxy));
    float inv_area = 1.0f / area;
    for (int y = y0; y <= y1; ++y) {
        size_t offset = (size_t)y * (size_t)width;
        for (int x = x0; x <= x1; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = screen_edge(b,c,px,py);
            float w1 = screen_edge(c,a,px,py);
            float w2 = screen_edge(a,b,px,py);
            if (w0 < -0.00001f || w1 < -0.00001f || w2 < -0.00001f) continue;
            float iz = (w0*a.inverse_z + w1*b.inverse_z + w2*c.inverse_z)*inv_area;
            size_t index = offset + (size_t)x;
            if (iz > depth[index] && isfinite(iz) && iz > 0.0f) {

                float u_over_z =
                    (w0 * a.u_over_z +
                    w1 * b.u_over_z +
                    w2 * c.u_over_z) * inv_area;

                float v_over_z =
                    (w0 * a.v_over_z +
                    w1 * b.v_over_z +
                    w2 * c.v_over_z) * inv_area;

                float u = u_over_z / iz;
                float v = v_over_z / iz;

                uint32_t color = is_ceiling
                    ? plane_sample_ceiling(u, v)
                    : plane_sample_floor(u, v);

                depth[index] = iz;
                pixels[index] = color;
            }
        }
    }
}

/* Clip a triangle to the near plane before perspective projection. */
static void draw_world_triangle(
    const Player *player,
    PlanePoint a,
    PlanePoint b,
    PlanePoint c,
    float elevation,
    bool is_ceiling,
    uint32_t *pixels,
    float *depth,
    int width,
    int height
) {
    PlanePoint points[3] = {a, b, c};

    PlaneTexturePoint input[3];

    for (int i = 0; i < 3; ++i) {
        input[i].camera = render_camera_transform(
            player,
            points[i].x,
            elevation,
            points[i].z
        );

        // World aligned texture coordinates.
        input[i].u = points[i].x;
        input[i].v = points[i].z;
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

    float focal = width * 0.5f;

    PlaneScreenPoint screen[4];

    for (int i = 0; i < n; ++i) {
        float inverse_z = 1.0f / clipped[i].camera.z;

        screen[i].x =
            width * 0.5f +
            focal * clipped[i].camera.x * inverse_z;

        screen[i].y =
            height * 0.5f -
            focal * clipped[i].camera.y * inverse_z;

        screen[i].inverse_z = inverse_z;

        screen[i].u_over_z = clipped[i].u * inverse_z;
        screen[i].v_over_z = clipped[i].v * inverse_z;

        if (!isfinite(screen[i].x) ||
            !isfinite(screen[i].y)) {
            return;
        }
    }

    // Triangulate the clipped polygon.
    for (int i = 1; i + 1 < n; ++i) {
        raster_triangle(
            screen[0],
            screen[i],
            screen[i + 1],
            is_ceiling,
            pixels,
            depth,
            width,
            height
        );
    }
}

void render_planes_draw(const Player *player, const Level *level,
                        uint32_t *pixels, float *depth, int width, int height) {
    if (!player || !level || !pixels || !depth || width <= 0 || height <= 0 ||
        !level->vertices || !level->walls || !level->sectors) return;

    for (int sector = 0; sector < level->sector_count; ++sector) {
        PlanePoint boundary[PLANE_MAX_VERTICES];
        int n = sector_boundary(level,sector,boundary);
        if (n < 3) continue;
        float signed_area = 0.0f;
        for (int i = 0; i < n; ++i) {
            PlanePoint a = boundary[i], b = boundary[(i+1)%n];
            signed_area += a.x*b.z - b.x*a.z;
        }
        if (fabsf(signed_area) < 0.000001f) continue;
        float orientation = signed_area > 0 ? 1.0f : -1.0f;
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
                const Sector *s = &level->sectors[sector];
                draw_world_triangle(
                    player, a, b, c,
                    s->floor_height, false,
                    pixels, depth, width, height
                );

                draw_world_triangle(
                    player, a, b, c,
                    s->ceiling_height, true,
                    pixels, depth, width, height
                );
                for (int j = i; j < length-1; ++j) remaining[j] = remaining[j+1];
                --length;
                found = true;
                break;
            }
            if (!found) break; /* Invalid or self-intersecting boundary. */
        }
    }
}
