#include "editor_model.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================== */
/* Basics                                                                */
/* ===================================================================== */

Sector model_default_sector(void) {
    Sector s = {0};
    s.floor_height = 0.0f;
    s.ceiling_height = 4.0f;
    s.floor_texture_id = 3;
    s.ceiling_texture_id = 4;
    s.light_level = 255;
    s.light_r = s.light_g = s.light_b = 255;
    return s;
}

static void *dup_array(const void *src, int count, size_t size, bool *ok) {
    if (count <= 0 || !src) return NULL;
    void *p = malloc((size_t)count * size);
    if (!p) { *ok = false; return NULL; }
    memcpy(p, src, (size_t)count * size);
    return p;
}

bool model_copy(Level *dst, const Level *src) {
    bool ok = true;
    Level c = *src;
    c.vertices = dup_array(src->vertices, src->vertex_count, sizeof(Vertex), &ok);
    c.walls    = dup_array(src->walls,    src->wall_count,   sizeof(Wall),   &ok);
    c.sectors  = dup_array(src->sectors,  src->sector_count, sizeof(Sector), &ok);
    c.lights   = dup_array(src->lights,   src->light_count,  sizeof(PointLight), &ok);
    if (!ok) { level_free(&c); return false; }
    *dst = c;
    return true;
}

static uint64_t hash_bytes(uint64_t h, const void *data, size_t n) {
    const unsigned char *p = data;
    while (n--) { h ^= *p++; h *= UINT64_C(1099511628211); }
    return h;
}
#define HF(x) (h = hash_bytes(h, &(x), sizeof(x)))

uint64_t model_fingerprint(const Level *l) {
    uint64_t h = UINT64_C(14695981039346656037);
    HF(l->vertex_count); HF(l->wall_count); HF(l->sector_count); HF(l->light_count);
    HF(l->ambient_r); HF(l->ambient_g); HF(l->ambient_b);
    for (int i = 0; i < l->vertex_count; i++) { HF(l->vertices[i].x); HF(l->vertices[i].z); }
    for (int i = 0; i < l->wall_count; i++) {
        const Wall *w = &l->walls[i];
        HF(w->vertex_start); HF(w->vertex_end); HF(w->front_sector); HF(w->back_sector); HF(w->texture_id);
    }
    for (int i = 0; i < l->sector_count; i++) {
        const Sector *s = &l->sectors[i];
        HF(s->floor_height); HF(s->ceiling_height); HF(s->floor_texture_id); HF(s->ceiling_texture_id);
        HF(s->light_level); HF(s->light_r); HF(s->light_g); HF(s->light_b);
    }
    for (int i = 0; i < l->light_count; i++) {
        const PointLight *p = &l->lights[i];
        HF(p->x); HF(p->y); HF(p->z); HF(p->radius); HF(p->intensity);
        HF(p->r); HF(p->g); HF(p->b); HF(p->effect);
    }
    HF(l->player_spawn.x); HF(l->player_spawn.y); HF(l->player_spawn.z);
    HF(l->player_spawn.yaw); HF(l->player_spawn.pitch);
    return h;
}
#undef HF

bool model_wall_valid(const Level *l, int w) {
    if (w < 0 || w >= l->wall_count) return false;
    const Wall *k = &l->walls[w];
    return k->vertex_start >= 0 && k->vertex_start < l->vertex_count &&
           k->vertex_end >= 0 && k->vertex_end < l->vertex_count;
}

float model_wall_length(const Level *l, int w) {
    if (!model_wall_valid(l, w)) return 0.0f;
    Vertex a = l->vertices[l->walls[w].vertex_start];
    Vertex b = l->vertices[l->walls[w].vertex_end];
    return hypotf(b.x - a.x, b.z - a.z);
}

void model_vertex_usage(const Level *l, unsigned char *used) {
    if (l->vertex_count > 0) memset(used, 0, (size_t)l->vertex_count);
    for (int i = 0; i < l->wall_count; i++) {
        if (!model_wall_valid(l, i)) continue;
        used[l->walls[i].vertex_start] = 1;
        used[l->walls[i].vertex_end] = 1;
    }
}

/* ===================================================================== */
/* Allocation helpers                                                    */
/* ===================================================================== */

static bool push_vertex(Level *l, float x, float z, int *out) {
    if (l->vertex_count >= INT_MAX / 2) return false;
    Vertex *p = realloc(l->vertices, (size_t)(l->vertex_count + 1) * sizeof(Vertex));
    if (!p) return false;
    l->vertices = p;
    l->vertices[l->vertex_count] = (Vertex){x, z};
    *out = l->vertex_count++;
    return true;
}

static int push_wall(Level *l, Wall w) {
    if (l->wall_count >= INT_MAX / 2) return -1;
    Wall *p = realloc(l->walls, (size_t)(l->wall_count + 1) * sizeof(Wall));
    if (!p) return -1;
    l->walls = p;
    l->walls[l->wall_count] = w;
    return l->wall_count++;
}

/* ===================================================================== */
/* Geometry helpers                                                      */
/* ===================================================================== */

static float cross2(float ax, float az, float bx, float bz) { return ax * bz - az * bx; }

/* If (px,pz) lies strictly inside wall i (not near its endpoints), returns
   true and writes the projection onto the wall. */
static bool point_inside_wall(const Level *l, int i, float px, float pz, float *ox, float *oz) {
    Vertex a = l->vertices[l->walls[i].vertex_start];
    Vertex b = l->vertices[l->walls[i].vertex_end];
    float dx = b.x - a.x, dz = b.z - a.z;
    float len2 = dx * dx + dz * dz;
    if (len2 < 1e-12f) return false;
    float len = sqrtf(len2);
    float t = ((px - a.x) * dx + (pz - a.z) * dz) / len2;
    if (t * len <= MODEL_EPS || (1.0f - t) * len <= MODEL_EPS) return false;
    float dist = fabsf(cross2(dx, dz, px - a.x, pz - a.z)) / len;
    if (dist >= MODEL_EPS) return false;
    if (ox) *ox = a.x + t * dx;
    if (oz) *oz = a.z + t * dz;
    return true;
}

/* Proper (interior) crossing of segments AB and CD; returns param along AB. */
static bool segments_cross(Vertex a, Vertex b, Vertex c, Vertex d, float *t_out) {
    float rx = b.x - a.x, rz = b.z - a.z, sx = d.x - c.x, sz = d.z - c.z;
    float denom = cross2(rx, rz, sx, sz);
    if (fabsf(denom) < 1e-9f) return false;
    float qx = c.x - a.x, qz = c.z - a.z;
    float t = cross2(qx, qz, sx, sz) / denom;
    float u = cross2(qx, qz, rx, rz) / denom;
    float lr = sqrtf(rx * rx + rz * rz), ls = sqrtf(sx * sx + sz * sz);
    if (lr < 1e-9f || ls < 1e-9f) return false;
    float et = MODEL_EPS / lr, eu = MODEL_EPS / ls;
    if (t <= et || t >= 1.0f - et || u <= eu || u >= 1.0f - eu) return false;
    if (t_out) *t_out = t;
    return true;
}

/* ===================================================================== */
/* Wall / vertex primitives                                              */
/* ===================================================================== */

bool model_split_wall(Level *l, int wall, float x, float z, int *out_vertex) {
    if (!model_wall_valid(l, wall)) return false;
    Wall old = l->walls[wall];
    int nv = -1;
    for (int i = 0; i < l->vertex_count; i++) {
        if (fabsf(l->vertices[i].x - x) < MODEL_EPS && fabsf(l->vertices[i].z - z) < MODEL_EPS) { nv = i; break; }
    }
    if (nv == old.vertex_start || nv == old.vertex_end) return false;
    if (nv < 0 && !push_vertex(l, x, z, &nv)) return false;
    Wall second = old;
    second.vertex_start = nv;
    if (push_wall(l, second) < 0) return false;
    l->walls[wall].vertex_end = nv;
    if (out_vertex) *out_vertex = nv;
    return true;
}

int model_ensure_vertex(Level *l, float x, float z) {
    for (int i = 0; i < l->vertex_count; i++) {
        if (fabsf(l->vertices[i].x - x) < MODEL_EPS && fabsf(l->vertices[i].z - z) < MODEL_EPS) return i;
    }
    for (int w = 0; w < l->wall_count; w++) {
        if (!model_wall_valid(l, w)) continue;
        float px, pz;
        if (point_inside_wall(l, w, x, z, &px, &pz)) {
            int v;
            return model_split_wall(l, w, px, pz, &v) ? v : -1;
        }
    }
    int v;
    return push_vertex(l, x, z, &v) ? v : -1;
}

static int find_wall_between(const Level *l, int a, int b) {
    for (int i = 0; i < l->wall_count; i++) {
        const Wall *w = &l->walls[i];
        if ((w->vertex_start == a && w->vertex_end == b) || (w->vertex_start == b && w->vertex_end == a)) return i;
    }
    return -1;
}

typedef struct Cut { float t; int v; } Cut;
static int cut_cmp(const void *a, const void *b) {
    float d = ((const Cut *)a)->t - ((const Cut *)b)->t;
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

static bool add_edge(Level *l, float x1, float z1, float x2, float z2, int tex) {
    int va = model_ensure_vertex(l, x1, z1);
    if (va < 0) return false;
    int vb = model_ensure_vertex(l, x2, z2);
    if (vb < 0) return false;
    if (va == vb) return true;

    int vc0 = l->vertex_count, wc0 = l->wall_count;
    unsigned char *used = calloc((size_t)vc0 + 1, 1);
    Cut *cuts = malloc(((size_t)vc0 + (size_t)wc0 + 2) * sizeof(Cut));
    if (!used || !cuts) { free(used); free(cuts); return false; }
    model_vertex_usage(l, used);

    Vertex A = l->vertices[va], B = l->vertices[vb];
    float dx = B.x - A.x, dz = B.z - A.z;
    float len = sqrtf(dx * dx + dz * dz), len2 = len * len;
    int n = 0;

    /* Existing vertices that lie on the new segment. */
    for (int v = 0; v < vc0; v++) {
        if (v == va || v == vb || !used[v]) continue;
        float px = l->vertices[v].x - A.x, pz = l->vertices[v].z - A.z;
        float t = (px * dx + pz * dz) / len2;
        if (t * len <= MODEL_EPS || (1.0f - t) * len <= MODEL_EPS) continue;
        if (fabsf(cross2(dx, dz, px, pz)) / len >= MODEL_EPS) continue;
        cuts[n++] = (Cut){t, v};
    }
    /* Existing walls the new segment properly crosses. */
    for (int w = 0; w < wc0; w++) {
        const Wall *k = &l->walls[w];
        if (k->vertex_start < 0 || k->vertex_end < 0 || k->vertex_start >= vc0 || k->vertex_end >= vc0) continue;
        if (k->vertex_start == va || k->vertex_start == vb || k->vertex_end == va || k->vertex_end == vb) continue;
        float t;
        if (!segments_cross(A, B, l->vertices[k->vertex_start], l->vertices[k->vertex_end], &t)) continue;
        int v;
        if (!model_split_wall(l, w, A.x + t * dx, A.z + t * dz, &v)) continue;
        cuts[n++] = (Cut){t, v};
    }
    qsort(cuts, (size_t)n, sizeof(Cut), cut_cmp);

    bool ok = true;
    int prev = va;
    for (int i = 0; i <= n && ok; i++) {
        int next = (i < n) ? cuts[i].v : vb;
        if (next == prev) continue;
        if (find_wall_between(l, prev, next) < 0) {
            Wall w = {prev, next, -2, -2, tex};   /* -2: derive on rebuild */
            if (push_wall(l, w) < 0) ok = false;
        }
        prev = next;
    }
    free(used);
    free(cuts);
    return ok;
}

bool model_add_path(Level *l, const Vertex *pts, int count, bool closed, int wall_texture) {
    if (!l || !pts || count < 2 || (closed && count < 3)) return false;
    Vertex *p = malloc((size_t)count * sizeof(Vertex));
    if (!p) return false;
    memcpy(p, pts, (size_t)count * sizeof(Vertex));
    if (closed) {
        double twice = 0.0;
        for (int i = 0; i < count; i++) {
            Vertex a = p[i], b = p[(i + 1) % count];
            twice += (double)a.x * b.z - (double)b.x * a.z;
        }
        if (fabs(twice) < 1e-4) { free(p); return false; }
        if (twice < 0) {  /* make interior lie on the front side */
            for (int i = 0, j = count - 1; i < j; i++, j--) { Vertex t = p[i]; p[i] = p[j]; p[j] = t; }
        }
    }
    bool ok = true;
    int edges = closed ? count : count - 1;
    for (int i = 0; i < edges && ok; i++) {
        Vertex a = p[i], b = p[(i + 1) % count];
        ok = add_edge(l, a.x, a.z, b.x, b.z, wall_texture);
    }
    free(p);
    return ok;
}

/* ===================================================================== */
/* Dirty tracking                                                        */
/* ===================================================================== */

void model_dirty_free(ModelDirty *d) {
    if (!d) return;
    free(d->sector);
    d->sector = NULL;
    d->count = 0;
}

static void dirty_mark(const Level *l, ModelDirty *d, int s) {
    if (!d || s < 0 || s >= l->sector_count) return;
    if (!d->sector) {
        d->sector = calloc((size_t)l->sector_count + 1, 1);
        if (!d->sector) return;
        d->count = l->sector_count;
    }
    if (s < d->count) d->sector[s] = 1;
}

static void dirty_mark_wall(const Level *l, ModelDirty *d, int w) {
    dirty_mark(l, d, l->walls[w].front_sector);
    dirty_mark(l, d, l->walls[w].back_sector);
}

/* ===================================================================== */
/* Removal helpers                                                       */
/* ===================================================================== */

/* Remove walls in wall_kill and vertices in vert_kill. Vertices that were in
   use and become unused because of the removed walls are dropped as well;
   pre-existing orphan vertices are left alone. */
static void remove_items(Level *l, const unsigned char *wall_kill, const unsigned char *vert_kill) {
    int V = l->vertex_count, W = l->wall_count;
    unsigned char *before = calloc((size_t)V + 1, 1);
    unsigned char *after = calloc((size_t)V + 1, 1);
    int *remap = malloc(((size_t)V + 1) * sizeof(int));
    if (!before || !after || !remap) { free(before); free(after); free(remap); return; }
    model_vertex_usage(l, before);

    int nw = 0;
    for (int i = 0; i < W; i++) {
        if (wall_kill && wall_kill[i]) continue;
        l->walls[nw++] = l->walls[i];
    }
    l->wall_count = nw;
    model_vertex_usage(l, after);

    int nv = 0;
    for (int v = 0; v < V; v++) {
        bool drop = (vert_kill && vert_kill[v]) || (before[v] && !after[v]);
        if (drop) { remap[v] = -1; continue; }
        remap[v] = nv;
        l->vertices[nv++] = l->vertices[v];
    }
    l->vertex_count = nv;
    for (int i = 0; i < l->wall_count; i++) {
        l->walls[i].vertex_start = remap[l->walls[i].vertex_start];
        l->walls[i].vertex_end = remap[l->walls[i].vertex_end];
    }
    free(before); free(after); free(remap);
}

/* Drops invalid and zero-length walls. With `dedupe`, walls joining the same
   two vertices are merged into one (their sides are combined, so two paired
   one-sided walls become a single portal). Vertex indices never change. */
static bool sanitize_walls(Level *l, bool dedupe, ModelDirty *dirty) {
    int W = l->wall_count;
    unsigned char *kill = calloc((size_t)W + 1, 1);
    if (!kill) return false;
    bool any = false;
    for (int i = 0; i < W; i++) {
        if (!model_wall_valid(l, i) || l->walls[i].vertex_start == l->walls[i].vertex_end) {
            if (model_wall_valid(l, i)) dirty_mark_wall(l, dirty, i);
            kill[i] = 1; any = true;
        }
    }
    if (dedupe && W <= 6000) {
        for (int i = 0; i < W; i++) {
            if (kill[i]) continue;
            for (int j = i + 1; j < W; j++) {
                if (kill[j]) continue;
                Wall *x = &l->walls[i], *y = &l->walls[j];
                bool same = x->vertex_start == y->vertex_start && x->vertex_end == y->vertex_end;
                bool flip = x->vertex_start == y->vertex_end && x->vertex_end == y->vertex_start;
                if (!same && !flip) continue;
                int yf = same ? y->front_sector : y->back_sector;
                int yb = same ? y->back_sector : y->front_sector;
                dirty_mark_wall(l, dirty, i);
                dirty_mark_wall(l, dirty, j);
                if (x->front_sector < 0 && yf >= 0) x->front_sector = yf;
                if (x->back_sector < 0 && yb >= 0) x->back_sector = yb;
                kill[j] = 1; any = true;
            }
        }
    }
    if (any) {
        int nw = 0;
        for (int i = 0; i < W; i++) if (!kill[i]) l->walls[nw++] = l->walls[i];
        l->wall_count = nw;
    }
    free(kill);
    return any;
}

void model_delete_walls(Level *l, const unsigned char *kill, ModelDirty *dirty) {
    for (int i = 0; i < l->wall_count; i++) if (kill[i] && model_wall_valid(l, i)) dirty_mark_wall(l, dirty, i);
    remove_items(l, kill, NULL);
}

void model_delete_vertices(Level *l, const unsigned char *kill, ModelDirty *dirty) {
    unsigned char *wk = calloc((size_t)l->wall_count + 1, 1);
    if (!wk) return;
    for (int i = 0; i < l->wall_count; i++) {
        if (!model_wall_valid(l, i)) { wk[i] = 1; continue; }
        if (kill[l->walls[i].vertex_start] || kill[l->walls[i].vertex_end]) { wk[i] = 1; dirty_mark_wall(l, dirty, i); }
    }
    remove_items(l, wk, kill);
    free(wk);
}

int model_remove_unused_vertices(Level *l) {
    int V = l->vertex_count;
    unsigned char *used = calloc((size_t)V + 1, 1);
    int *remap = malloc(((size_t)V + 1) * sizeof(int));
    if (!used || !remap) { free(used); free(remap); return 0; }
    model_vertex_usage(l, used);
    int nv = 0;
    for (int v = 0; v < V; v++) {
        if (!used[v]) { remap[v] = -1; continue; }
        remap[v] = nv;
        l->vertices[nv++] = l->vertices[v];
    }
    int removed = V - nv;
    l->vertex_count = nv;
    for (int i = 0; i < l->wall_count; i++) {
        if (l->walls[i].vertex_start >= 0 && l->walls[i].vertex_start < V) l->walls[i].vertex_start = remap[l->walls[i].vertex_start];
        if (l->walls[i].vertex_end >= 0 && l->walls[i].vertex_end < V) l->walls[i].vertex_end = remap[l->walls[i].vertex_end];
    }
    free(used); free(remap);
    return removed;
}

int model_remove_unused_sectors(Level *l) {
    int S = l->sector_count;
    if (S == 0) return 0;
    unsigned char *ref = calloc((size_t)S + 1, 1);
    int *remap = malloc(((size_t)S + 1) * sizeof(int));
    if (!ref || !remap) { free(ref); free(remap); return 0; }
    for (int i = 0; i < l->wall_count; i++) {
        int f = l->walls[i].front_sector, b = l->walls[i].back_sector;
        if (f >= 0 && f < S) ref[f] = 1;
        if (b >= 0 && b < S) ref[b] = 1;
    }
    int n = 0;
    for (int s = 0; s < S; s++) {
        if (!ref[s]) { remap[s] = -1; continue; }
        remap[s] = n;
        l->sectors[n++] = l->sectors[s];
    }
    for (int i = 0; i < l->wall_count; i++) {
        Wall *w = &l->walls[i];
        if (w->front_sector >= 0 && w->front_sector < S) w->front_sector = remap[w->front_sector];
        if (w->back_sector >= 0 && w->back_sector < S) w->back_sector = remap[w->back_sector];
    }
    int removed = S - n;
    l->sector_count = n;
    free(ref); free(remap);
    return removed;
}

void model_delete_lights(Level *l, const unsigned char *kill) {
    int n = 0;
    for (int i = 0; i < l->light_count; i++) {
        if (kill[i]) continue;
        l->lights[n++] = l->lights[i];
    }
    l->light_count = n;
}

void model_delete_sectors(Level *l, const unsigned char *kill, ModelDirty *dirty) {
    int W = l->wall_count;
    unsigned char *wk = calloc((size_t)W + 1, 1);
    if (!wk) return;
    for (int s = 0; s < l->sector_count; s++) {
        if (!kill[s]) continue;
        dirty_mark(l, dirty, s);
        /* Walls with the void on the other side belong only to this sector. */
        int shared_with = -2;      /* single neighbour id, -3 = several */
        int exclusive = 0;
        for (int i = 0; i < W; i++) {
            const Wall *w = &l->walls[i];
            int other;
            if (w->front_sector == s) other = w->back_sector;
            else if (w->back_sector == s) other = w->front_sector;
            else continue;
            if (other < 0 || kill[other] || other == s) { wk[i] = 1; exclusive++; }
            else if (shared_with == -2) shared_with = other;
            else if (shared_with != other) shared_with = -3;
        }
        /* A sector completely enclosed by one neighbour (a platform or pit)
           is removed by merging it back into that neighbour. */
        if (exclusive == 0 && shared_with >= 0) {
            for (int i = 0; i < W; i++) {
                const Wall *w = &l->walls[i];
                if (w->front_sector == s || w->back_sector == s) wk[i] = 1;
            }
        }
    }
    for (int i = 0; i < W; i++) if (wk[i]) dirty_mark_wall(l, dirty, i);
    remove_items(l, wk, NULL);
    free(wk);
}

/* ===================================================================== */
/* Moves: weld + T-junction resolution                                   */
/* ===================================================================== */

bool model_resolve_contacts(Level *l, const unsigned char *moved, ModelDirty *dirty) {
    int V = l->vertex_count;
    bool changed = false;
    if (V == 0) return false;
    unsigned char *used = calloc((size_t)V + 1, 1);
    int *rep = malloc(((size_t)V + 1) * sizeof(int));
    int *nmap = malloc(((size_t)V + 1) * sizeof(int));
    if (!used || !rep || !nmap) { free(used); free(rep); free(nmap); return false; }
    model_vertex_usage(l, used);
    for (int v = 0; v < V; v++) rep[v] = v;

    /* Walls around moved vertices are the ones whose rooms may change. */
    for (int i = 0; i < l->wall_count; i++) {
        if (!model_wall_valid(l, i)) continue;
        if (moved[l->walls[i].vertex_start] || moved[l->walls[i].vertex_end]) dirty_mark_wall(l, dirty, i);
    }

    /* A: weld each moved vertex onto any coincident used vertex. */
    for (int v = 0; v < V; v++) {
        if (!moved[v] || !used[v]) continue;
        int best = -1;
        for (int u = 0; u < V; u++) {
            if (u == v || !used[u]) continue;
            if (fabsf(l->vertices[u].x - l->vertices[v].x) >= MODEL_EPS ||
                fabsf(l->vertices[u].z - l->vertices[v].z) >= MODEL_EPS) continue;
            if (!moved[u]) { best = u; break; }   /* prefer one that did not move */
            if (u < v && best < 0) best = u;
        }
        if (best >= 0) rep[v] = rep[best];
    }
    bool welded = false;
    for (int v = 0; v < V; v++) if (rep[v] != v) welded = true;
    if (welded) {
        changed = true;
        for (int i = 0; i < l->wall_count; i++) {
            if (!model_wall_valid(l, i)) continue;
            dirty_mark_wall(l, dirty, i);
            l->walls[i].vertex_start = rep[l->walls[i].vertex_start];
            l->walls[i].vertex_end = rep[l->walls[i].vertex_end];
        }
        sanitize_walls(l, true, dirty);
        int nv = 0;
        for (int v = 0; v < V; v++) {   /* drop only the merged-away vertices */
            if (rep[v] != v) { nmap[v] = -1; continue; }
            nmap[v] = nv;
            l->vertices[nv++] = l->vertices[v];
        }
        l->vertex_count = nv;
        for (int i = 0; i < l->wall_count; i++) {
            l->walls[i].vertex_start = nmap[l->walls[i].vertex_start];
            l->walls[i].vertex_end = nmap[l->walls[i].vertex_end];
        }
    } else {
        if (sanitize_walls(l, true, dirty)) changed = true;
        for (int v = 0; v < V; v++) nmap[v] = v;
    }

    /* B: a moved vertex resting on the middle of a wall splits that wall. */
    for (int v = 0; v < V; v++) {
        if (!moved[v] || rep[v] != v || nmap[v] < 0) continue;
        int idx = nmap[v];
        if (idx >= l->vertex_count) continue;
        float px = l->vertices[idx].x, pz = l->vertices[idx].z;
        int wc = l->wall_count;
        for (int w = 0; w < wc; w++) {
            if (!model_wall_valid(l, w)) continue;
            if (l->walls[w].vertex_start == idx || l->walls[w].vertex_end == idx) continue;
            if (!point_inside_wall(l, w, px, pz, NULL, NULL)) continue;
            dirty_mark_wall(l, dirty, w);
            Wall second = l->walls[w];
            second.vertex_start = idx;
            if (push_wall(l, second) >= 0) { l->walls[w].vertex_end = idx; changed = true; }
            break;
        }
    }
    free(used); free(rep); free(nmap);
    return changed;
}

bool model_has_crossings(const Level *l, const unsigned char *moved) {
    int W = l->wall_count;
    for (int i = 0; i < W; i++) {
        if (!model_wall_valid(l, i)) continue;
        const Wall *a = &l->walls[i];
        if (moved && !moved[a->vertex_start] && !moved[a->vertex_end]) continue;
        for (int j = 0; j < W; j++) {
            if (j == i || !model_wall_valid(l, j)) continue;
            if (!moved && j < i) continue;
            const Wall *b = &l->walls[j];
            if (a->vertex_start == b->vertex_start || a->vertex_start == b->vertex_end ||
                a->vertex_end == b->vertex_start || a->vertex_end == b->vertex_end) continue;
            if (segments_cross(l->vertices[a->vertex_start], l->vertices[a->vertex_end],
                               l->vertices[b->vertex_start], l->vertices[b->vertex_end], NULL)) return true;
        }
    }
    return false;
}

/* ===================================================================== */
/* Sector derivation                                                     */
/* ===================================================================== */

static float edge_angle(const Level *l, int e) {
    const Wall *w = &l->walls[e >> 1];
    Vertex a = l->vertices[(e & 1) ? w->vertex_end : w->vertex_start];
    Vertex b = l->vertices[(e & 1) ? w->vertex_start : w->vertex_end];
    return atan2f(b.z - a.z, b.x - a.x);
}
static int edge_org(const Level *l, int e) { return (e & 1) ? l->walls[e >> 1].vertex_end : l->walls[e >> 1].vertex_start; }
static int edge_tgt(const Level *l, int e) { return (e & 1) ? l->walls[e >> 1].vertex_start : l->walls[e >> 1].vertex_end; }
static int edge_sec(const Level *l, int e) { return (e & 1) ? l->walls[e >> 1].back_sector : l->walls[e >> 1].front_sector; }

static int uf_find(int *p, int x) {
    while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; }
    return x;
}

static bool point_in_loop(const Level *l, const int *edges, int n, float x, float z) {
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        Vertex a = l->vertices[edge_org(l, edges[i])];
        Vertex b = l->vertices[edge_org(l, edges[j])];
        if ((a.z > z) != (b.z > z) && x < (b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x) inside = !inside;
    }
    return inside;
}

/*
 * Half-edge e = 2*wall + side. side 0 runs start->end with the front sector on
 * its left; side 1 is the reverse with the back sector. A "face" is the loop
 * of half-edges around one region. Side values: >=0 sector, -1 void,
 * -2 "new, derive me".
 *
 * Only DIRTY faces are rewritten: those containing a -2 side or a side whose
 * sector is flagged in `dirty`. All other faces are left exactly as authored.
 */
void model_rebuild_sectors(Level *l, const Sector *fresh, ModelDirty *dirty) {
    sanitize_walls(l, false, NULL);
    const int W = l->wall_count, V = l->vertex_count, E = 2 * W;
    const int old_count = l->sector_count;
    const Sector fresh_sector = fresh ? *fresh : model_default_sector();
    const unsigned char *dirty_sec = (dirty && dirty->sector) ? dirty->sector : NULL;
    const int dirty_n = dirty ? dirty->count : 0;

    if (W == 0) {
        free(l->sectors);
        l->sectors = NULL;
        l->sector_count = 0;
        goto finish;
    }

    int *old = malloc((size_t)E * sizeof(int));
    int *sec = malloc((size_t)E * sizeof(int));
    unsigned char *derived = calloc((size_t)E + 1, 1);
    float *ang = malloc((size_t)E * sizeof(float));
    int *adj_start = calloc((size_t)V + 2, sizeof(int));
    int *adj = malloc((size_t)E * sizeof(int));
    int *pos = malloc((size_t)E * sizeof(int));
    int *loop_start = malloc(((size_t)E + 2) * sizeof(int));
    int *loop_edges = malloc((size_t)E * sizeof(int));
    double *loop_area = malloc(((size_t)E + 1) * sizeof(double));
    unsigned char *loop_dirty = calloc((size_t)E + 1, 1);
    int *comp = malloc(((size_t)V + 1) * sizeof(int));
    int *owner = malloc(((size_t)E + 1) * sizeof(int));
    int *loop_final = malloc(((size_t)E + 1) * sizeof(int));
    unsigned char *claimed = calloc((size_t)old_count + 1, 1);
    unsigned char *referenced = calloc((size_t)old_count + 1, 1);
    int *final_of_old = malloc(((size_t)old_count + 1) * sizeof(int));
    Sector *fresh_list = malloc(((size_t)E + 1) * sizeof(Sector));
    int loops = 0;
    if (!old || !sec || !derived || !ang || !adj_start || !adj || !pos || !loop_start || !loop_edges ||
        !loop_area || !loop_dirty || !comp || !owner || !loop_final || !claimed || !referenced ||
        !final_of_old || !fresh_list) goto done;

    for (int e = 0; e < E; e++) {
        int s = edge_sec(l, e);
        old[e] = s;
        sec[e] = s;
        ang[e] = edge_angle(l, e);
        if (s >= 0 && s < old_count) referenced[s] = 1;
    }

    /* Outgoing half-edges per vertex, sorted counter-clockwise. */
    for (int e = 0; e < E; e++) adj_start[edge_org(l, e) + 1]++;
    for (int v = 0; v < V; v++) adj_start[v + 1] += adj_start[v];
    {
        int *fill = calloc((size_t)V + 1, sizeof(int));
        if (!fill) goto done;
        for (int e = 0; e < E; e++) {
            int v = edge_org(l, e);
            adj[adj_start[v] + fill[v]++] = e;
        }
        free(fill);
    }
    for (int v = 0; v < V; v++) {
        int *a = adj + adj_start[v], n = adj_start[v + 1] - adj_start[v];
        for (int i = 1; i < n; i++) {
            int e = a[i], j = i - 1;
            while (j >= 0 && ang[a[j]] > ang[e]) { a[j + 1] = a[j]; j--; }
            a[j + 1] = e;
        }
        for (int i = 0; i < n; i++) pos[a[i]] = i;
    }

    /* Connected components (tells holes apart from the faces around them). */
    for (int v = 0; v < V; v++) comp[v] = v;
    for (int w = 0; w < W; w++) {
        int a = uf_find(comp, l->walls[w].vertex_start), b = uf_find(comp, l->walls[w].vertex_end);
        if (a != b) comp[a] = b;
    }

    /* Trace every face loop (the face on the left of each half-edge). */
    {
        int *loop_of = malloc((size_t)E * sizeof(int));
        if (!loop_of) goto done;
        for (int e = 0; e < E; e++) loop_of[e] = -1;
        int used_edges = 0;
        for (int s = 0; s < E; s++) {
            if (loop_of[s] >= 0) continue;
            loop_start[loops] = used_edges;
            double twice = 0.0;
            int e = s;
            do {
                loop_of[e] = loops;
                loop_edges[used_edges++] = e;
                Vertex a = l->vertices[edge_org(l, e)], b = l->vertices[edge_tgt(l, e)];
                twice += (double)a.x * b.z - (double)b.x * a.z;
                int v = edge_tgt(l, e), deg = adj_start[v + 1] - adj_start[v];
                int k = pos[e ^ 1];
                e = adj[adj_start[v] + (k - 1 + deg) % deg];
            } while (e != s && used_edges <= E);
            loop_area[loops] = twice * 0.5;
            loops++;
        }
        loop_start[loops] = used_edges;
        free(loop_of);
    }

    /* Which faces are dirty? */
    for (int L = 0; L < loops; L++) {
        for (int i = loop_start[L]; i < loop_start[L + 1]; i++) {
            int s = old[loop_edges[i]];
            if (s == -2 || (s >= 0 && s < dirty_n && dirty_sec[s])) { loop_dirty[L] = 1; break; }
        }
    }
    /* Sectors still used by an untouched face are spoken for. */
    for (int L = 0; L < loops; L++) {
        if (loop_dirty[L]) continue;
        for (int i = loop_start[L]; i < loop_start[L + 1]; i++) {
            int s = old[loop_edges[i]];
            if (s >= 0 && s < old_count) claimed[s] = 1;
        }
    }

    /* Dirty bounded faces: reuse the old sector by vote, else create one. */
    int fresh_count = 0;
    for (int L = 0; L < loops; L++) {
        owner[L] = INT_MIN;
        if (!loop_dirty[L] || loop_area[L] <= 1e-4) continue;
        const int *edges = loop_edges + loop_start[L];
        int n = loop_start[L + 1] - loop_start[L];
        int best = -1, best_votes = 0, inherit = -1, inherit_votes = 0;
        for (int i = 0; i < n; i++) {
            int c = old[edges[i]];
            if (c < 0 || c >= old_count) continue;
            int votes = 0;
            for (int j = 0; j < n; j++) if (old[edges[j]] == c) votes++;
            if (!claimed[c] && votes > best_votes) { best = c; best_votes = votes; }
            if (votes > inherit_votes) { inherit = c; inherit_votes = votes; }
        }
        if (best >= 0) {
            claimed[best] = 1;
            owner[L] = best;
        } else {
            fresh_list[fresh_count] = inherit >= 0 ? l->sectors[inherit] : fresh_sector;
            owner[L] = -(fresh_count + 2);
            fresh_count++;
        }
    }

    /* Final numbering: kept sectors keep their order, new ones follow. A
       sector that was in use but no face claims any more is dropped;
       sectors that were never referenced are left alone. */
    int surv = 0;
    for (int s = 0; s < old_count; s++) {
        bool touched = s < dirty_n && dirty_sec && dirty_sec[s];
        final_of_old[s] = (claimed[s] || (!referenced[s] && !touched)) ? surv++ : -1;
    }
    int total = surv + fresh_count;
    Sector *ns = malloc(((size_t)total + 1) * sizeof(Sector));
    if (!ns) goto done;
    for (int s = 0; s < old_count; s++) if (final_of_old[s] >= 0) ns[final_of_old[s]] = l->sectors[s];
    for (int k = 0; k < fresh_count; k++) ns[surv + k] = fresh_list[k];

    /* Which sector does each bounded face stand for? */
    for (int L = 0; L < loops; L++) {
        loop_final[L] = -1;
        if (loop_area[L] <= 1e-4) continue;
        if (loop_dirty[L]) {
            if (owner[L] >= 0) loop_final[L] = final_of_old[owner[L]];
            else if (owner[L] != INT_MIN) loop_final[L] = surv + (-owner[L] - 2);
        } else {
            for (int i = loop_start[L]; i < loop_start[L + 1]; i++) {
                int s = old[loop_edges[i]];
                if (s >= 0 && s < old_count) { loop_final[L] = final_of_old[s]; break; }
            }
        }
    }

    /* Write sides: untouched faces keep their (renumbered) sectors; dirty
       faces get their derived sector, or void when they are not enclosed. */
    for (int e = 0; e < E; e++) {
        int s = old[e];
        sec[e] = (s >= 0 && s < old_count) ? final_of_old[s] : (s == -2 ? -1 : (s < 0 ? s : -1));
    }
    for (int L = 0; L < loops; L++) {
        if (!loop_dirty[L]) continue;
        for (int i = loop_start[L]; i < loop_start[L + 1]; i++) {
            int e = loop_edges[i];
            derived[e] = 1;
            sec[e] = loop_final[L];     /* -1 for outer / degenerate loops */
        }
    }
    /* Dirty outlines that sit inside another face are holes: their outward
       half-edges belong to the surrounding sector (pillars, platforms). */
    for (int L = 0; L < loops; L++) {
        if (!loop_dirty[L] || loop_area[L] >= -1e-4) continue;
        const int *edges = loop_edges + loop_start[L];
        int n = loop_start[L + 1] - loop_start[L];
        int cL = uf_find(comp, edge_org(l, edges[0]));
        Vertex p = l->vertices[edge_org(l, edges[0])];
        int container = -1;
        double best_area = 1e300;
        for (int F = 0; F < loops; F++) {
            if (loop_final[F] < 0 || loop_area[F] >= best_area) continue;
            if (uf_find(comp, edge_org(l, loop_edges[loop_start[F]])) == cL) continue;
            if (point_in_loop(l, loop_edges + loop_start[F], loop_start[F + 1] - loop_start[F], p.x, p.z)) {
                container = F;
                best_area = loop_area[F];
            }
        }
        if (container >= 0) for (int i = 0; i < n; i++) sec[edges[i]] = loop_final[container];
    }

    for (int w = 0; w < W; w++) {
        Wall *k = &l->walls[w];
        k->front_sector = sec[2 * w];
        k->back_sector = sec[2 * w + 1];
        if ((derived[2 * w] || derived[2 * w + 1]) && k->front_sector < 0 && k->back_sector >= 0) {
            int t = k->vertex_start;   /* solid walls face their room */
            k->vertex_start = k->vertex_end;
            k->vertex_end = t;
            k->front_sector = k->back_sector;
            k->back_sector = -1;
        }
    }
    free(l->sectors);
    if (total > 0) l->sectors = ns; else { free(ns); l->sectors = NULL; }
    l->sector_count = total;

done:
    free(old); free(sec); free(derived); free(ang); free(adj_start); free(adj); free(pos);
    free(loop_start); free(loop_edges); free(loop_area); free(loop_dirty); free(comp); free(owner);
    free(loop_final); free(claimed); free(referenced); free(final_of_old); free(fresh_list);
finish:
    model_dirty_free(dirty);
}

/* ===================================================================== */
/* Sector queries                                                        */
/* ===================================================================== */

float model_sector_area(const Level *l, int s) {
    double sum = 0.0;
    for (int i = 0; i < l->wall_count; i++) {
        if (!model_wall_valid(l, i)) continue;
        const Wall *w = &l->walls[i];
        Vertex a = l->vertices[w->vertex_start], b = l->vertices[w->vertex_end];
        double c = (double)a.x * b.z - (double)b.x * a.z;
        if (w->front_sector == s) sum += c;
        if (w->back_sector == s) sum -= c;
    }
    return (float)(sum * 0.5);
}

bool model_sector_bounds(const Level *l, int s, float *x0, float *z0, float *x1, float *z1) {
    bool any = false;
    float ax = 0, az = 0, bx = 0, bz = 0;
    for (int i = 0; i < l->wall_count; i++) {
        if (!model_wall_valid(l, i)) continue;
        const Wall *w = &l->walls[i];
        if (w->front_sector != s && w->back_sector != s) continue;
        for (int k = 0; k < 2; k++) {
            Vertex v = l->vertices[k ? w->vertex_end : w->vertex_start];
            if (!any) { ax = bx = v.x; az = bz = v.z; any = true; }
            if (v.x < ax) ax = v.x;
            if (v.x > bx) bx = v.x;
            if (v.z < az) az = v.z;
            if (v.z > bz) bz = v.z;
        }
    }
    if (any) { *x0 = ax; *z0 = az; *x1 = bx; *z1 = bz; }
    return any;
}

bool model_sector_contains(const Level *l, int s, float x, float z) {
    bool inside = false;
    for (int i = 0; i < l->wall_count; i++) {
        if (!model_wall_valid(l, i)) continue;
        const Wall *w = &l->walls[i];
        if (w->front_sector != s && w->back_sector != s) continue;
        Vertex a = l->vertices[w->vertex_start], b = l->vertices[w->vertex_end];
        if ((a.z > z) != (b.z > z) && x < (b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x) inside = !inside;
    }
    return inside;
}

int model_sector_at(const Level *l, float x, float z) {
    int best = -1;
    float best_area = 1e30f;
    for (int s = 0; s < l->sector_count; s++) {
        if (!model_sector_contains(l, s, x, z)) continue;
        float a = fabsf(model_sector_area(l, s));
        if (a < best_area) { best_area = a; best = s; }
    }
    return best;
}

static int float_cmp(const void *a, const void *b) {
    float d = *(const float *)a - *(const float *)b;
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

bool model_sector_label_point(const Level *l, int s, float *x, float *z) {
    float x0, z0, x1, z1;
    if (!model_sector_bounds(l, s, &x0, &z0, &x1, &z1)) return false;
    float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
    if (model_sector_contains(l, s, cx, cz)) { *x = cx; *z = cz; return true; }
    float xs[256];
    for (int attempt = 0; attempt < 5; attempt++) {
        float zz = z0 + (z1 - z0) * (0.5f + 0.0137f + attempt * 0.07f);
        int n = 0;
        for (int i = 0; i < l->wall_count && n < 256; i++) {
            if (!model_wall_valid(l, i)) continue;
            const Wall *w = &l->walls[i];
            if (w->front_sector != s && w->back_sector != s) continue;
            Vertex a = l->vertices[w->vertex_start], b = l->vertices[w->vertex_end];
            if ((a.z > zz) != (b.z > zz)) xs[n++] = (b.x - a.x) * (zz - a.z) / (b.z - a.z) + a.x;
        }
        qsort(xs, (size_t)n, sizeof(float), float_cmp);
        float best = -1, bx = cx;
        for (int i = 0; i + 1 < n; i += 2) {
            if (xs[i + 1] - xs[i] > best) { best = xs[i + 1] - xs[i]; bx = (xs[i] + xs[i + 1]) * 0.5f; }
        }
        if (best > 0) { *x = bx; *z = zz; return true; }
    }
    *x = cx; *z = cz;
    return true;
}

/* ===================================================================== */
/* Validation                                                            */
/* ===================================================================== */

typedef struct ProblemList { Problem *out; int max, n; } ProblemList;

static void add_problem(ProblemList *pl, ProblemSeverity sev, ProblemKind kind, int object,
                        float x, float z, const char *fmt, ...) __attribute__((format(printf, 7, 8)));
static void add_problem(ProblemList *pl, ProblemSeverity sev, ProblemKind kind, int object,
                        float x, float z, const char *fmt, ...) {
    if (pl->n >= pl->max) return;
    Problem *p = &pl->out[pl->n++];
    p->severity = sev; p->kind = kind; p->object = object; p->x = x; p->z = z;
    va_list ap; va_start(ap, fmt);
    vsnprintf(p->text, sizeof(p->text), fmt, ap);
    va_end(ap);
}

static int problem_cmp(const void *a, const void *b) {
    const Problem *pa = a, *pb = b;
    return (int)pb->severity - (int)pa->severity;
}

int model_validate(const Level *l, Problem *out, int max) {
    ProblemList pl = {out, max, 0};
    int V = l->vertex_count, W = l->wall_count;
    unsigned char *used = calloc((size_t)V + 1, 1);
    int *deg = calloc((size_t)V + 1, sizeof(int));
    if (!used || !deg) { free(used); free(deg); return 0; }

    int bad_refs = 0;
    for (int i = 0; i < W; i++) {
        const Wall *w = &l->walls[i];
        bool bad = !model_wall_valid(l, i) || w->front_sector >= l->sector_count ||
                   w->back_sector >= l->sector_count || w->front_sector < -1 || w->back_sector < -1;
        if (bad) {
            bad_refs++;
            if (bad_refs <= 5) add_problem(&pl, PROBLEM_ERROR, PROBLEM_BAD_REFERENCE, i, 0, 0, "Wall %d references a missing vertex or sector", i);
            continue;
        }
        deg[w->vertex_start]++; deg[w->vertex_end]++;
        used[w->vertex_start] = used[w->vertex_end] = 1;
    }

    int open_ends = 0;
    for (int v = 0; v < V; v++) {
        if (deg[v] != 1) continue;
        open_ends++;
        if (open_ends <= 12) add_problem(&pl, PROBLEM_ERROR, PROBLEM_OPEN_END, v, l->vertices[v].x, l->vertices[v].z,
                                         "Open wall end at (%.2f, %.2f): room is not closed", l->vertices[v].x, l->vertices[v].z);
    }
    if (open_ends > 12) add_problem(&pl, PROBLEM_ERROR, PROBLEM_OPEN_END, -1, 0, 0, "...and %d more open wall ends", open_ends - 12);

    int crossings = 0;
    for (int i = 0; i < W && W <= 3000; i++) {
        if (!model_wall_valid(l, i)) continue;
        const Wall *a = &l->walls[i];
        for (int j = i + 1; j < W; j++) {
            if (!model_wall_valid(l, j)) continue;
            const Wall *b = &l->walls[j];
            if (a->vertex_start == b->vertex_start || a->vertex_start == b->vertex_end ||
                a->vertex_end == b->vertex_start || a->vertex_end == b->vertex_end) continue;
            float t;
            if (!segments_cross(l->vertices[a->vertex_start], l->vertices[a->vertex_end],
                                l->vertices[b->vertex_start], l->vertices[b->vertex_end], &t)) continue;
            crossings++;
            if (crossings <= 8) {
                Vertex p = l->vertices[a->vertex_start], q = l->vertices[a->vertex_end];
                add_problem(&pl, PROBLEM_ERROR, PROBLEM_CROSSING, i, p.x + (q.x - p.x) * t, p.z + (q.z - p.z) * t,
                            "Walls %d and %d cross each other", i, j);
            }
        }
    }
    if (crossings > 8) add_problem(&pl, PROBLEM_ERROR, PROBLEM_CROSSING, -1, 0, 0, "...and %d more crossings", crossings - 8);

    for (int s = 0; s < l->sector_count; s++) {
        const Sector *sc = &l->sectors[s];
        float x, z;
        if (sc->floor_height >= sc->ceiling_height) {
            bool ok = model_sector_label_point(l, s, &x, &z);
            add_problem(&pl, PROBLEM_ERROR, PROBLEM_FLOOR_ABOVE_CEILING, s, ok ? x : 0, ok ? z : 0,
                        "Sector %d: floor (%.2f) is not below ceiling (%.2f)", s, sc->floor_height, sc->ceiling_height);
        }
        if (sc->floor_texture_id < 0 || sc->floor_texture_id >= MODEL_TEXTURE_COUNT ||
            sc->ceiling_texture_id < 0 || sc->ceiling_texture_id >= MODEL_TEXTURE_COUNT) {
            bool ok = model_sector_label_point(l, s, &x, &z);
            add_problem(&pl, PROBLEM_WARNING, PROBLEM_BAD_TEXTURE, s, ok ? x : 0, ok ? z : 0,
                        "Sector %d uses a texture id that is not loaded", s);
        }
    }

    int orphan_walls = 0, tiny = 0, bad_tex = 0;
    for (int i = 0; i < W; i++) {
        if (!model_wall_valid(l, i)) continue;
        const Wall *w = &l->walls[i];
        Vertex a = l->vertices[w->vertex_start], b = l->vertices[w->vertex_end];
        float mx = (a.x + b.x) * 0.5f, mz = (a.z + b.z) * 0.5f;
        if (w->front_sector < 0 && w->back_sector < 0) {
            if (++orphan_walls <= 6) add_problem(&pl, PROBLEM_WARNING, PROBLEM_ORPHAN_WALL, i, mx, mz,
                                                "Wall %d does not belong to any room", i);
        } else if (w->front_sector == w->back_sector) {
            if (++orphan_walls <= 6) add_problem(&pl, PROBLEM_WARNING, PROBLEM_ORPHAN_WALL, i, mx, mz,
                                                "Wall %d is a loose divider inside sector %d", i, w->front_sector);
        }
        if (model_wall_length(l, i) < 0.05f) {
            if (++tiny <= 6) add_problem(&pl, PROBLEM_WARNING, PROBLEM_TINY_WALL, i, mx, mz,
                                         "Wall %d is tiny (%.3f units)", i, model_wall_length(l, i));
        }
        if (w->texture_id < 0 || w->texture_id >= MODEL_TEXTURE_COUNT) {
            if (++bad_tex <= 4) add_problem(&pl, PROBLEM_WARNING, PROBLEM_BAD_TEXTURE, i, mx, mz,
                                            "Wall %d uses texture %d which is not loaded", i, w->texture_id);
        }
    }

    const PlayerSpawn *sp = &l->player_spawn;
    int spawn_sector = model_sector_at(l, sp->x, sp->z);
    if (spawn_sector < 0)
        add_problem(&pl, PROBLEM_WARNING, PROBLEM_SPAWN_OUTSIDE, -1, sp->x, sp->z, "Player start is outside every sector");

    int lights_out = 0;
    for (int i = 0; i < l->light_count; i++) {
        const PointLight *p = &l->lights[i];
        int s = model_sector_at(l, p->x, p->z);
        if (s < 0) {
            if (++lights_out <= 6) add_problem(&pl, PROBLEM_INFO, PROBLEM_LIGHT_OUTSIDE, i, p->x, p->z,
                                               "Light %d is outside every sector", i);
        } else if (p->y < l->sectors[s].floor_height || p->y > l->sectors[s].ceiling_height) {
            add_problem(&pl, PROBLEM_INFO, PROBLEM_LIGHT_HEIGHT, i, p->x, p->z,
                        "Light %d (height %.2f) is outside sector %d (%.2f to %.2f)", i, p->y, s,
                        l->sectors[s].floor_height, l->sectors[s].ceiling_height);
        }
    }

    int unused = 0;
    for (int v = 0; v < V; v++) if (!used[v]) unused++;
    if (unused > 0)
        add_problem(&pl, PROBLEM_INFO, PROBLEM_UNUSED_VERTICES, -1, 0, 0, "%d unused vertices (Level > Clean Up)", unused);

    free(used); free(deg);
    qsort(pl.out, (size_t)pl.n, sizeof(Problem), problem_cmp);
    return pl.n;
}
