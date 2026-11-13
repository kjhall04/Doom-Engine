#ifndef RENDER_RASTER_H
#define RENDER_RASTER_H

#include <stdint.h>

#include "engine/textures/texture.h"
#include "engine/level/level.h"

/*
 * Shared scanline rasterizer used by the wall and plane renderers.
 *
 * Vertices carry 1/z, u/z and v/z, which are linear in screen space.
 * Triangles are walked row by row, so only covered pixels are visited,
 * and the perspective divide is done once per 16-pixel chunk instead of
 * once per pixel.
 */
typedef struct RasterVertex {
    float x;
    float y;
    float inverse_z;
    float u_over_z;
    float v_over_z;
} RasterVertex;

/* Used when a surface has no loaded texture. */
typedef uint32_t (*RasterFallbackFn)(float u, float v);

typedef struct RasterTarget {
    uint32_t *pixels;
    float *depth;               /* inverse depth, larger is closer */
    int width;
    int height;

    int x_min;                  /* inclusive column window */
    int x_max;

    int y_min;                  /* inclusive row window */
    int y_max;

    unsigned char light_r, light_g, light_b; /* combined sector lighting */

    /* Texture-coordinate-to-world mapping: world = origin + u*axis_u + v*axis_v. */
    float world_origin[3], world_u[3], world_v[3];
    const PointLight *lights;
    const float *light_factors;
    int light_count;
    float ambient[3];
    const int *clip_top;        /* optional per-column inclusive window */
    const int *clip_bottom;
} RasterTarget;

void render_raster_triangle(
    const RasterTarget *target,
    const RasterVertex *a,
    const RasterVertex *b,
    const RasterVertex *c,
    const Texture *texture,
    RasterFallbackFn fallback
);

#endif
