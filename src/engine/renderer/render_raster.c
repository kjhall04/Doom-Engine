#include "render_raster.h"

#include <math.h>
#include <stddef.h>

#define RASTER_CHUNK 16
#define RASTER_MIN_CHUNK 4
#define RASTER_DEPTH_CHANGE_LIMIT 0.06f

/* Pixels whose centre lies this close outside an edge are still drawn, so
 * triangles sharing an edge never leave a gap. Overlap is harmless because
 * the depth test is strict. */
#define RASTER_EDGE_TOLERANCE 0.01f

static inline int fast_floor(float f) {
    int i = (int)f;
    return i - (f < (float)i);
}

static inline int fast_ceil(float f) {
    int i = (int)f;
    return i + (f > (float)i);
}

/* Wrap a non-negative texel index into [0, size). */
static inline int wrap_index(int i, int size, float inverse_size) {
    i -= (int)((float)i * inverse_size) * size;
    if (i < 0) i += size;
    else if (i >= size) i -= size;
    return i;
}

typedef struct SpanAttributes {
    float inverse_z;
    float u_over_z;
    float v_over_z;
} SpanAttributes;

static inline uint32_t shade_pixel(uint32_t color,
                                   unsigned lr, unsigned lg, unsigned lb) {
    if (lr == 255 && lg == 255 && lb == 255)
        return color;
    unsigned r = ((color >> 16) & 255u) * lr / 255u;
    unsigned g = ((color >> 8) & 255u) * lg / 255u;
    unsigned b = (color & 255u) * lb / 255u;
    return (color & 0xff000000u) | (r << 16) | (g << 8) | b;
}

/* Evaluate nearby point lights once per short span, not for every texel.
 * Surface lighting crosses sector boundaries. Occlusion/shadows are future work. */
static void span_light(const RasterTarget *t, float u, float v,
                       unsigned *r, unsigned *g, unsigned *b) {
    if (!t->lights) {
        *r=t->light_r; *g=t->light_g; *b=t->light_b;
        return;
    }
    float wx=t->world_origin[0]+u*t->world_u[0]+v*t->world_v[0];
    float wy=t->world_origin[1]+u*t->world_u[1]+v*t->world_v[1];
    float wz=t->world_origin[2]+u*t->world_u[2]+v*t->world_v[2];
    float cr=t->ambient[0], cg=t->ambient[1], cb=t->ambient[2];
    for(int i=0;i<t->light_count;i++) {
        const PointLight *l=&t->lights[i];
        float dx=wx-l->x,dy=wy-l->y,dz=wz-l->z;
        float d2=dx*dx+dy*dy+dz*dz, r2=l->radius*l->radius;
        if (d2>=r2 || r2<=0.0f) continue;
        float falloff=1.0f-d2/r2;
        falloff*=falloff*l->intensity*t->light_factors[i];
        cr+=falloff*(float)l->r/255.0f;
        cg+=falloff*(float)l->g/255.0f;
        cb+=falloff*(float)l->b/255.0f;
    }
    *r=(unsigned)(fminf(fmaxf(cr,0.0f),1.0f)*255.0f);
    *g=(unsigned)(fminf(fmaxf(cg,0.0f),1.0f)*255.0f);
    *b=(unsigned)(fminf(fmaxf(cb,0.0f),1.0f)*255.0f);
}

static void raster_span(
    const RasterTarget *target,
    int y,
    int x_first,
    int x_last,
    SpanAttributes start,
    SpanAttributes step,
    const Texture *texture,
    RasterFallbackFn fallback
) {
    uint32_t *pixel_row = target->pixels + (size_t)y * (size_t)target->width;
    float *depth_row = target->depth + (size_t)y * (size_t)target->width;
    const int *clip_top = target->clip_top;
    const int *clip_bottom = target->clip_bottom;

    const bool textured = texture != NULL && texture->pixels != NULL &&
                          texture->width > 0 && texture->height > 0;

    const uint32_t *texels = textured ? texture->pixels : NULL;
    const int tex_w = textured ? texture->width : 1;
    const int tex_h = textured ? texture->height : 1;
    const float tex_wf = (float)tex_w;
    const float tex_hf = (float)tex_h;
    const float inverse_tex_w = 1.0f / tex_wf;
    const float inverse_tex_h = 1.0f / tex_hf;
    /* 16.16 fixed point is safe while sizes stay well below 2^15. */
    const bool fast_steps = tex_w <= 8192 && tex_h <= 8192;

    SpanAttributes current = start;
    bool current_valid = current.inverse_z > 0.0f;
    float current_u = 0.0f;
    float current_v = 0.0f;

    if (current_valid) {
        float reciprocal = 1.0f / current.inverse_z;
        current_u = current.u_over_z * reciprocal;
        current_v = current.v_over_z * reciprocal;
    }

    int x = x_first;
    int remaining = x_last - x_first + 1;

    while (remaining > 0) {
        int n = remaining < (target->lights ? 4 : RASTER_CHUNK) ? remaining : (target->lights ? 4 : RASTER_CHUNK);
        float nf = (float)n;

        SpanAttributes next = {
            current.inverse_z + step.inverse_z * nf,
            current.u_over_z + step.u_over_z * nf,
            current.v_over_z + step.v_over_z * nf
        };

        /* Affine interpolation is only accurate while depth changes slowly
         * across the chunk. Where it changes quickly (surfaces seen at a
         * grazing angle) use shorter chunks. */
        while (n > RASTER_MIN_CHUNK &&
               fabsf(next.inverse_z - current.inverse_z) >
                   RASTER_DEPTH_CHANGE_LIMIT *
                   fmaxf(current.inverse_z, next.inverse_z)) {
            n >>= 1;
            nf = (float)n;
            next.inverse_z = current.inverse_z + step.inverse_z * nf;
            next.u_over_z = current.u_over_z + step.u_over_z * nf;
            next.v_over_z = current.v_over_z + step.v_over_z * nf;
        }

        const bool next_valid = next.inverse_z > 0.0f;
        float next_u = 0.0f;
        float next_v = 0.0f;

        if (next_valid) {
            float reciprocal = 1.0f / next.inverse_z;
            next_u = next.u_over_z * reciprocal;
            next_v = next.v_over_z * reciprocal;
        }

        unsigned shade_r=target->light_r,shade_g=target->light_g,shade_b=target->light_b;
        if (current_valid && next_valid && target->lights)
            span_light(target, (current_u+next_u)*0.5f, (current_v+next_v)*0.5f,
                       &shade_r,&shade_g,&shade_b);
        if (current_valid && next_valid) {
            const float inverse_n =
                (n == RASTER_CHUNK) ? (1.0f / (float)RASTER_CHUNK) : 1.0f / nf;
            const float du = (next_u - current_u) * inverse_n;
            const float dv = (next_v - current_v) * inverse_n;
            const float base_iz = current.inverse_z;

            if (textured) {
                /* Work in texel units. Bring the chunk start into
                 * [0, size) so the per-pixel wrap only ever has to correct
                 * a single step. */
                float tu = current_u * tex_wf;
                float tv = current_v * tex_hf;
                const float dtu = du * tex_wf;
                const float dtv = dv * tex_hf;

                tu -= (float)fast_floor(tu * inverse_tex_w) * tex_wf;
                tv -= (float)fast_floor(tv * inverse_tex_h) * tex_hf;

                if (fast_steps &&
                    fabsf(dtu) < tex_wf * 0.5f &&
                    fabsf(dtv) < tex_hf * 0.5f) {
                    /* 16.16 fixed point: no float-to-int conversion or
                     * integer division per pixel. */
                    int fu = (int)(tu * 65536.0f);
                    int fv = (int)(tv * 65536.0f);
                    const int dfu = (int)(dtu * 65536.0f);
                    const int dfv = (int)(dtv * 65536.0f);
                    const int wrap_u = tex_w << 16;
                    const int wrap_v = tex_h << 16;
                    const int adjust_u = dfu < 0 ? wrap_u : -wrap_u;
                    const int adjust_v = dfv < 0 ? wrap_v : -wrap_v;

                    if (fu >= wrap_u) fu -= wrap_u;
                    if (fv >= wrap_v) fv -= wrap_v;

                    float iz = base_iz;

                    for (int k = 0; k < n; ++k,
                         iz += step.inverse_z, fu += dfu, fv += dfv) {
                        const int px = x + k;

                        /* Unsigned compare catches both under- and
                         * overflow of the single step taken. */
                        if ((unsigned int)fu >= (unsigned int)wrap_u) {
                            fu += adjust_u;
                        }
                        if ((unsigned int)fv >= (unsigned int)wrap_v) {
                            fv += adjust_v;
                        }

                        if (clip_top != NULL &&
                            (y < clip_top[px] || y > clip_bottom[px])) {
                            continue;
                        }

                        if (iz > depth_row[px]) {
                            depth_row[px] = iz;
                            pixel_row[px] = shade_pixel(
                                texels[(size_t)(fv >> 16) * (size_t)tex_w +
                                       (size_t)(fu >> 16)], shade_r, shade_g, shade_b);
                        }
                    }
                } else {
                    /* Heavily minified chunk: step by more than half a
                     * texture per pixel. Use the general float path. */
                    const float reach_u = dtu * (float)(n - 1);
                    const float reach_v = dtv * (float)(n - 1);

                    if (reach_u < 0.0f) {
                        tu += tex_wf *
                              (float)((int)(-reach_u * inverse_tex_w) + 1);
                    }
                    if (reach_v < 0.0f) {
                        tv += tex_hf *
                              (float)((int)(-reach_v * inverse_tex_h) + 1);
                    }

                    for (int k = 0; k < n; ++k) {
                        const int px = x + k;

                        if (clip_top != NULL &&
                            (y < clip_top[px] || y > clip_bottom[px])) {
                            continue;
                        }

                        const float iz = base_iz + step.inverse_z * (float)k;

                        if (iz > depth_row[px]) {
                            const float fk = (float)k;
                            const int xi = wrap_index(
                                (int)(tu + dtu * fk), tex_w, inverse_tex_w);
                            const int yi = wrap_index(
                                (int)(tv + dtv * fk), tex_h, inverse_tex_h);

                            depth_row[px] = iz;
                            pixel_row[px] = shade_pixel(
                                texels[(size_t)yi * (size_t)tex_w + xi], shade_r, shade_g, shade_b);
                        }
                    }
                }
            } else {
                for (int k = 0; k < n; ++k) {
                    const int px = x + k;

                    if (clip_top != NULL &&
                        (y < clip_top[px] || y > clip_bottom[px])) {
                        continue;
                    }

                    const float iz = base_iz + step.inverse_z * (float)k;

                    if (iz > depth_row[px]) {
                        const float fk = (float)k;

                        depth_row[px] = iz;
                        pixel_row[px] = shade_pixel(fallback != NULL
                            ? fallback(current_u + du * fk, current_v + dv * fk)
                            : 0xFFFF00FFu, shade_r, shade_g, shade_b);
                    }
                }
            }
        }

        x += n;
        remaining -= n;
        current = next;
        current_valid = next_valid;
        current_u = next_u;
        current_v = next_v;
    }
}

void render_raster_triangle(
    const RasterTarget *target,
    const RasterVertex *p0,
    const RasterVertex *p1,
    const RasterVertex *p2,
    const Texture *texture,
    RasterFallbackFn fallback
) {
    if (target == NULL || target->pixels == NULL || target->depth == NULL ||
        target->width <= 0 || target->height <= 0) {
        return;
    }

    /* Sort the vertices by y: a is the top, c is the bottom. */
    const RasterVertex *a = p0;
    const RasterVertex *b = p1;
    const RasterVertex *c = p2;
    const RasterVertex *swap;

    if (a->y > b->y) { swap = a; a = b; b = swap; }
    if (b->y > c->y) { swap = b; b = c; c = swap; }
    if (a->y > b->y) { swap = a; a = b; b = swap; }

    const float area =
        (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);

    if (!isfinite(area) || fabsf(area) < 0.00001f) {
        return;
    }

    const float min_x = fminf(a->x, fminf(b->x, c->x));
    const float max_x = fmaxf(a->x, fmaxf(b->x, c->x));

    if (c->y < 0.0f || a->y >= (float)target->height ||
        max_x < 0.0f || min_x >= (float)target->width) {
        return;
    }

    int column_min = target->x_min > 0 ? target->x_min : 0;
    int column_max = target->x_max < target->width - 1
        ? target->x_max : target->width - 1;

    if (column_min > column_max ||
        max_x < (float)column_min - RASTER_EDGE_TOLERANCE ||
        min_x > (float)column_max + 1.0f + RASTER_EDGE_TOLERANCE) {
        return;
    }

    int row_first = fast_ceil(a->y - 0.5f - RASTER_EDGE_TOLERANCE);
    int row_last = fast_floor(c->y - 0.5f + RASTER_EDGE_TOLERANCE);

    if (row_first < target->y_min) row_first = target->y_min;
    if (row_last > target->y_max) row_last = target->y_max;

    if (row_first > row_last) {
        return;
    }

    /* Screen-space gradients of 1/z, u/z and v/z. */
    const float inverse_area = 1.0f / area;
    const float ab_x = b->x - a->x;
    const float ab_y = b->y - a->y;
    const float ac_x = c->x - a->x;
    const float ac_y = c->y - a->y;

    SpanAttributes step_x;
    SpanAttributes step_y;

    {
        const float d1 = b->inverse_z - a->inverse_z;
        const float d2 = c->inverse_z - a->inverse_z;
        step_x.inverse_z = (d1 * ac_y - d2 * ab_y) * inverse_area;
        step_y.inverse_z = (ab_x * d2 - ac_x * d1) * inverse_area;
    }
    {
        const float d1 = b->u_over_z - a->u_over_z;
        const float d2 = c->u_over_z - a->u_over_z;
        step_x.u_over_z = (d1 * ac_y - d2 * ab_y) * inverse_area;
        step_y.u_over_z = (ab_x * d2 - ac_x * d1) * inverse_area;
    }
    {
        const float d1 = b->v_over_z - a->v_over_z;
        const float d2 = c->v_over_z - a->v_over_z;
        step_x.v_over_z = (d1 * ac_y - d2 * ab_y) * inverse_area;
        step_y.v_over_z = (ab_x * d2 - ac_x * d1) * inverse_area;
    }

    /* Edge slopes (dx per dy). Zero-height edges never contribute rows. */
    const float slope_long = ac_y > 0.0f ? ac_x / ac_y : 0.0f;
    const float slope_upper = ab_y > 0.0f ? ab_x / ab_y : 0.0f;
    const float bc_y = c->y - b->y;
    const float slope_lower = bc_y > 0.0f ? (c->x - b->x) / bc_y : 0.0f;

    for (int y = row_first; y <= row_last; ++y) {
        const float yc = (float)y + 0.5f;
        float ye = yc;

        if (ye < a->y) ye = a->y;
        if (ye > c->y) ye = c->y;

        const float x_long = a->x + (ye - a->y) * slope_long;
        const float x_short = (ye < b->y)
            ? a->x + (ye - a->y) * slope_upper
            : b->x + (ye - b->y) * slope_lower;

        const float left = x_long < x_short ? x_long : x_short;
        const float right = x_long < x_short ? x_short : x_long;

        int x_first = fast_ceil(left - 0.5f - RASTER_EDGE_TOLERANCE);
        int x_last = fast_floor(right - 0.5f + RASTER_EDGE_TOLERANCE);

        if (x_first < column_min) x_first = column_min;
        if (x_last > column_max) x_last = column_max;

        if (x_first > x_last) {
            continue;
        }

        const float dx = (float)x_first + 0.5f - a->x;
        const float dy = yc - a->y;

        SpanAttributes start = {
            a->inverse_z + step_x.inverse_z * dx + step_y.inverse_z * dy,
            a->u_over_z + step_x.u_over_z * dx + step_y.u_over_z * dy,
            a->v_over_z + step_x.v_over_z * dx + step_y.v_over_z * dy
        };

        raster_span(target, y, x_first, x_last, start, step_x,
                    texture, fallback);
    }
}
