#include "editor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979323846f

/* Pick radii in logical pixels */
#define PICK_THING_PX 12.0f
#define PICK_VERTEX_PX 8.0f
#define PICK_WALL_PX 6.0f
#define SNAP_VERTEX_PX 10.0f
#define SNAP_EDGE_PX 8.0f
#define DRAG_START_PX 4.0f

enum { SNAP_F_VERTEX = 1, SNAP_F_EDGE = 2, SNAP_F_GRID = 4, SNAP_F_ALL = 7 };

static bool g_space_down = false;
static float g_last_sx, g_last_sy;
static int *g_sec_start = NULL;     /* CSR: walls touching each sector */
static int *g_sec_walls = NULL;
static int g_sec_count = 0;
static bool g_cache_dirty = true;

/* ===================================================================== */
/* Selection                                                             */
/* ===================================================================== */

void sel_clear(Editor *ed) { ed->sel.kind = SEL_NONE; ed->sel.count = 0; }

bool sel_has(const Editor *ed, SelKind kind, int id) {
    if (ed->sel.kind != kind || kind == SEL_NONE) return false;
    if (kind == SEL_SPAWN) return true;
    for (int i = 0; i < ed->sel.count; i++) if (ed->sel.ids[i] == id) return true;
    return false;
}

static void sync_template(Editor *ed) {
    if (ed->sel.count < 1 && ed->sel.kind != SEL_SPAWN) return;
    const Level *l = &ed->level;
    int id = ed->sel.count > 0 ? ed->sel.ids[0] : 0;
    if (ed->sel.kind == SEL_SECTOR && id >= 0 && id < l->sector_count) ed->tmpl = l->sectors[id];
    if (ed->sel.kind == SEL_WALL && id >= 0 && id < l->wall_count) ed->tmpl_wall_tex = l->walls[id].texture_id;
}

void sel_add(Editor *ed, SelKind kind, int id) {
    if (kind == SEL_NONE) return;
    if (ed->sel.kind != kind) sel_clear(ed);
    ed->sel.kind = kind;
    if (kind == SEL_SPAWN) { ed->sel.count = 0; return; }
    if (sel_has(ed, kind, id)) return;
    if (ed->sel.count >= ed->sel.cap) {
        int nc = ed->sel.cap ? ed->sel.cap * 2 : 64;
        int *p = realloc(ed->sel.ids, (size_t)nc * sizeof(int));
        if (!p) return;
        ed->sel.ids = p; ed->sel.cap = nc;
    }
    ed->sel.ids[ed->sel.count++] = id;
    sync_template(ed);
}

void sel_set(Editor *ed, SelKind kind, int id) { sel_clear(ed); sel_add(ed, kind, id); }

bool sel_remove(Editor *ed, SelKind kind, int id) {
    if (ed->sel.kind != kind) return false;
    for (int i = 0; i < ed->sel.count; i++) {
        if (ed->sel.ids[i] != id) continue;
        ed->sel.ids[i] = ed->sel.ids[--ed->sel.count];
        if (ed->sel.count == 0) ed->sel.kind = SEL_NONE;
        return true;
    }
    return false;
}

/* ===================================================================== */
/* View                                                                  */
/* ===================================================================== */

SDL_FRect canvas_rect(const Editor *ed) {
    float w = ed->ui.width - LAYOUT_TOOLBAR_W - LAYOUT_INSPECTOR_W;
    float h = ed->ui.height - LAYOUT_MENU_H - LAYOUT_STATUS_H;
    return (SDL_FRect){LAYOUT_TOOLBAR_W, LAYOUT_MENU_H, w > 50 ? w : 50, h > 50 ? h : 50};
}

void world_to_screen(const Editor *ed, float x, float z, float *sx, float *sy) {
    SDL_FRect c = canvas_rect(ed);
    *sx = c.x + c.w * 0.5f + (x - ed->cam_x) * ed->zoom;
    *sy = c.y + c.h * 0.5f + (z - ed->cam_z) * ed->zoom;
}

void screen_to_world(const Editor *ed, float sx, float sy, float *x, float *z) {
    SDL_FRect c = canvas_rect(ed);
    *x = ed->cam_x + (sx - c.x - c.w * 0.5f) / ed->zoom;
    *z = ed->cam_z + (sy - c.y - c.h * 0.5f) / ed->zoom;
}

static void zoom_at(Editor *ed, float sx, float sy, float factor) {
    float wx, wz;
    screen_to_world(ed, sx, sy, &wx, &wz);
    ed->zoom = ed_clampf(ed->zoom * factor, 3.0f, 600.0f);
    SDL_FRect c = canvas_rect(ed);
    ed->cam_x = wx - (sx - c.x - c.w * 0.5f) / ed->zoom;
    ed->cam_z = wz - (sy - c.y - c.h * 0.5f) / ed->zoom;
}

void view_center_on(Editor *ed, float x, float z) { ed->cam_x = x; ed->cam_z = z; }

static bool bounds_add(bool *any, float b[4], float x, float z) {
    if (!*any) { b[0] = b[2] = x; b[1] = b[3] = z; *any = true; return true; }
    if (x < b[0]) b[0] = x;
    if (x > b[2]) b[2] = x;
    if (z < b[1]) b[1] = z;
    if (z > b[3]) b[3] = z;
    return true;
}

static void fit_bounds(Editor *ed, const float b[4]) {
    SDL_FRect c = canvas_rect(ed);
    float w = b[2] - b[0], h = b[3] - b[1];
    if (w < 2.0f) w = 2.0f;
    if (h < 2.0f) h = 2.0f;
    float zx = (c.w - 80) / w, zz = (c.h - 80) / h;
    ed->zoom = ed_clampf(zx < zz ? zx : zz, 6.0f, 160.0f);
    ed->cam_x = (b[0] + b[2]) * 0.5f;
    ed->cam_z = (b[1] + b[3]) * 0.5f;
}

void view_fit_all(Editor *ed) {
    const Level *l = &ed->level;
    bool any = false;
    float b[4] = {0};
    for (int i = 0; i < l->wall_count; i++) {
        if (!model_wall_valid(l, i)) continue;
        bounds_add(&any, b, l->vertices[l->walls[i].vertex_start].x, l->vertices[l->walls[i].vertex_start].z);
        bounds_add(&any, b, l->vertices[l->walls[i].vertex_end].x, l->vertices[l->walls[i].vertex_end].z);
    }
    for (int i = 0; i < l->light_count; i++) bounds_add(&any, b, l->lights[i].x, l->lights[i].z);
    bounds_add(&any, b, l->player_spawn.x, l->player_spawn.z);
    if (!any) { ed->cam_x = 0; ed->cam_z = 0; ed->zoom = 40; return; }
    fit_bounds(ed, b);
}

void view_fit_selection(Editor *ed) {
    const Level *l = &ed->level;
    bool any = false;
    float b[4] = {0};
    for (int k = 0; k < ed->sel.count || (ed->sel.kind == SEL_SPAWN && k < 1); k++) {
        int id = ed->sel.count ? ed->sel.ids[k] : 0;
        switch (ed->sel.kind) {
            case SEL_VERTEX:
                if (id < l->vertex_count) bounds_add(&any, b, l->vertices[id].x, l->vertices[id].z);
                break;
            case SEL_WALL:
                if (model_wall_valid(l, id)) {
                    bounds_add(&any, b, l->vertices[l->walls[id].vertex_start].x, l->vertices[l->walls[id].vertex_start].z);
                    bounds_add(&any, b, l->vertices[l->walls[id].vertex_end].x, l->vertices[l->walls[id].vertex_end].z);
                }
                break;
            case SEL_SECTOR: {
                float x0, z0, x1, z1;
                if (model_sector_bounds(l, id, &x0, &z0, &x1, &z1)) { bounds_add(&any, b, x0, z0); bounds_add(&any, b, x1, z1); }
                break;
            }
            case SEL_LIGHT:
                if (id < l->light_count) bounds_add(&any, b, l->lights[id].x, l->lights[id].z);
                break;
            case SEL_SPAWN: bounds_add(&any, b, l->player_spawn.x, l->player_spawn.z); break;
            default: break;
        }
    }
    if (!any) { view_fit_all(ed); return; }
    fit_bounds(ed, b);
    if (ed->zoom > 60) ed->zoom = 60;
}

/* ===================================================================== */
/* Derived caches                                                        */
/* ===================================================================== */

void canvas_invalidate(Editor *ed) { (void)ed; g_cache_dirty = true; }

void canvas_shutdown(Editor *ed) {
    free(g_sec_start); free(g_sec_walls);
    g_sec_start = g_sec_walls = NULL;
    g_sec_count = 0;
    free(ed->move_mask); free(ed->move_orig);
    ed->move_mask = NULL; ed->move_orig = NULL;
    free(ed->sel.ids); ed->sel.ids = NULL;
}

static void ensure_cache(Editor *ed) {
    const Level *l = &ed->level;
    if (!g_cache_dirty && g_sec_count == l->sector_count) return;
    free(g_sec_start); free(g_sec_walls);
    g_sec_count = l->sector_count;
    g_sec_start = calloc((size_t)g_sec_count + 2, sizeof(int));
    g_sec_walls = malloc(((size_t)l->wall_count * 2 + 1) * sizeof(int));
    if (!g_sec_start || !g_sec_walls) { g_sec_count = 0; return; }
    for (int i = 0; i < l->wall_count; i++) {
        int f = l->walls[i].front_sector, b = l->walls[i].back_sector;
        if (f >= 0 && f < g_sec_count) g_sec_start[f + 1]++;
        if (b >= 0 && b < g_sec_count && b != f) g_sec_start[b + 1]++;
    }
    for (int s = 0; s < g_sec_count; s++) g_sec_start[s + 1] += g_sec_start[s];
    int *fill = calloc((size_t)g_sec_count + 1, sizeof(int));
    if (!fill) return;
    for (int i = 0; i < l->wall_count; i++) {
        int f = l->walls[i].front_sector, b = l->walls[i].back_sector;
        if (f >= 0 && f < g_sec_count) g_sec_walls[g_sec_start[f] + fill[f]++] = i;
        if (b >= 0 && b < g_sec_count && b != f) g_sec_walls[g_sec_start[b] + fill[b]++] = i;
    }
    free(fill);
    g_cache_dirty = false;
}

/* ===================================================================== */
/* Picking + snapping                                                    */
/* ===================================================================== */

static float dist_to_seg(float px, float py, float ax, float ay, float bx, float by, float *t_out) {
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-9f ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
    t = ed_clampf(t, 0.0f, 1.0f);
    if (t_out) *t_out = t;
    float cx = ax + dx * t - px, cy = ay + dy * t - py;
    return sqrtf(cx * cx + cy * cy);
}

static bool vertex_pickable(const Editor *ed, int v) {
    if (ed->show_unused) return true;
    return v < ed->vused_count && ed->vused[v];
}

static bool in_canvas(const Editor *ed, float sx, float sy) {
    SDL_FRect c = canvas_rect(ed);
    return ui_point_in(c, sx, sy);
}

static Pick pick_at(Editor *ed, float sx, float sy) {
    const Level *l = &ed->level;
    PickFilter f = ed->filter;
    Pick none = {SEL_NONE, -1};
    if (f == PICK_ALL || f == PICK_THINGS) {
        float x, y, best = PICK_THING_PX;
        world_to_screen(ed, l->player_spawn.x, l->player_spawn.z, &x, &y);
        Pick r = none;
        float d = hypotf(sx - x, sy - y);
        if (d < best) { best = d; r = (Pick){SEL_SPAWN, 0}; }
        for (int i = 0; i < l->light_count; i++) {
            world_to_screen(ed, l->lights[i].x, l->lights[i].z, &x, &y);
            d = hypotf(sx - x, sy - y);
            if (d < best) { best = d; r = (Pick){SEL_LIGHT, i}; }
        }
        if (r.kind != SEL_NONE) return r;
    }
    if (f == PICK_ALL || f == PICK_POINTS) {
        float best = PICK_VERTEX_PX;
        Pick r = none;
        for (int v = 0; v < l->vertex_count; v++) {
            if (!vertex_pickable(ed, v)) continue;
            float x, y;
            world_to_screen(ed, l->vertices[v].x, l->vertices[v].z, &x, &y);
            float d = hypotf(sx - x, sy - y);
            if (d < best) { best = d; r = (Pick){SEL_VERTEX, v}; }
        }
        if (r.kind != SEL_NONE) return r;
    }
    if (f == PICK_ALL || f == PICK_WALLS) {
        float best = PICK_WALL_PX;
        Pick r = none;
        for (int i = 0; i < l->wall_count; i++) {
            if (!model_wall_valid(l, i)) continue;
            float ax, ay, bx, by;
            world_to_screen(ed, l->vertices[l->walls[i].vertex_start].x, l->vertices[l->walls[i].vertex_start].z, &ax, &ay);
            world_to_screen(ed, l->vertices[l->walls[i].vertex_end].x, l->vertices[l->walls[i].vertex_end].z, &bx, &by);
            float d = dist_to_seg(sx, sy, ax, ay, bx, by, NULL);
            if (d < best) { best = d; r = (Pick){SEL_WALL, i}; }
        }
        if (r.kind != SEL_NONE) return r;
    }
    if (f == PICK_ALL || f == PICK_ROOMS) {
        float wx, wz;
        screen_to_world(ed, sx, sy, &wx, &wz);
        int s = model_sector_at(l, wx, wz);
        if (s >= 0) return (Pick){SEL_SECTOR, s};
    }
    return none;
}

static SnapResult snap_point(Editor *ed, float sx, float sy, const unsigned char *exclude, int flags) {
    const Level *l = &ed->level;
    SnapResult r;
    screen_to_world(ed, sx, sy, &r.x, &r.z);
    r.kind = SNAP_NONE;
    r.index = -1;
    if (ui_mods(&ed->ui) & SDL_KMOD_CTRL) return r;   /* Ctrl = free placement */

    if (flags & SNAP_F_VERTEX) {
        float best = SNAP_VERTEX_PX;
        int bi = -1;
        for (int v = 0; v < l->vertex_count; v++) {
            if (!(v < ed->vused_count && ed->vused[v]) || (exclude && v < ed->move_mask_count && exclude[v])) continue;
            float x, y;
            world_to_screen(ed, l->vertices[v].x, l->vertices[v].z, &x, &y);
            float d = hypotf(sx - x, sy - y);
            if (d < best) { best = d; bi = v; }
        }
        if (bi >= 0) {
            r.x = l->vertices[bi].x; r.z = l->vertices[bi].z;
            r.kind = SNAP_VERTEX; r.index = bi;
            return r;
        }
    }
    if (flags & SNAP_F_EDGE) {
        float best = SNAP_EDGE_PX;
        int bw = -1;
        float bt = 0;
        for (int i = 0; i < l->wall_count; i++) {
            if (!model_wall_valid(l, i)) continue;
            int a = l->walls[i].vertex_start, b = l->walls[i].vertex_end;
            if (exclude && ((a < ed->move_mask_count && exclude[a]) || (b < ed->move_mask_count && exclude[b]))) continue;
            float ax, ay, bx, by, t;
            world_to_screen(ed, l->vertices[a].x, l->vertices[a].z, &ax, &ay);
            world_to_screen(ed, l->vertices[b].x, l->vertices[b].z, &bx, &by);
            float d = dist_to_seg(sx, sy, ax, ay, bx, by, &t);
            if (d < best && t > 0.001f && t < 0.999f) { best = d; bw = i; bt = t; }
        }
        if (bw >= 0) {
            Vertex a = l->vertices[l->walls[bw].vertex_start], b = l->vertices[l->walls[bw].vertex_end];
            float x = a.x + (b.x - a.x) * bt, z = a.z + (b.z - a.z) * bt;
            if (ed->snap) {   /* axis-aligned walls: also land on the grid */
                float g = editor_grid(ed);
                if (fabsf(b.x - a.x) < 1e-5f) z = roundf(z / g) * g;
                else if (fabsf(b.z - a.z) < 1e-5f) x = roundf(x / g) * g;
                float lo = fminf(a.x, b.x), hi = fmaxf(a.x, b.x);
                float zlo = fminf(a.z, b.z), zhi = fmaxf(a.z, b.z);
                if (x < lo || x > hi || z < zlo || z > zhi) { x = a.x + (b.x - a.x) * bt; z = a.z + (b.z - a.z) * bt; }
            }
            r.x = x; r.z = z; r.kind = SNAP_EDGE; r.index = bw;
            return r;
        }
    }
    if ((flags & SNAP_F_GRID) && ed->snap) {
        float g = editor_grid(ed);
        r.x = roundf(r.x / g) * g;
        r.z = roundf(r.z / g) * g;
        r.kind = SNAP_GRID;
    }
    return r;
}

/* Shift while drawing: lock the next segment to 45-degree steps. */
static void constrain_angle(Editor *ed, float fromx, float fromz, float *x, float *z) {
    float dx = *x - fromx, dz = *z - fromz;
    float len = hypotf(dx, dz);
    if (len < 1e-4f) return;
    float step = PI_F / 4.0f;
    float a = roundf(atan2f(dz, dx) / step) * step;
    if (ed->snap) {
        float g = editor_grid(ed);
        float axis = fabsf(cosf(a)) > 0.99f || fabsf(sinf(a)) > 0.99f ? g : g * 1.41421356f;
        len = fmaxf(axis, roundf(len / axis) * axis);
    }
    *x = fromx + cosf(a) * len;
    *z = fromz + sinf(a) * len;
}

/* ===================================================================== */
/* Edit operations                                                       */
/* ===================================================================== */

/* Vertices that belong to the current selection (for moving). */
static int build_move_mask(Editor *ed, unsigned char *mask) {
    const Level *l = &ed->level;
    int n = 0;
#define MARK(v) do { int _v = (v); if (_v >= 0 && _v < l->vertex_count && !mask[_v]) { mask[_v] = 1; n++; } } while (0)
    if (ed->sel.kind == SEL_VERTEX) {
        for (int i = 0; i < ed->sel.count; i++) MARK(ed->sel.ids[i]);
    } else if (ed->sel.kind == SEL_WALL) {
        for (int i = 0; i < ed->sel.count; i++) {
            int w = ed->sel.ids[i];
            if (!model_wall_valid(l, w)) continue;
            MARK(l->walls[w].vertex_start);
            MARK(l->walls[w].vertex_end);
        }
    } else if (ed->sel.kind == SEL_SECTOR) {
        unsigned char *ss = calloc((size_t)l->sector_count + 1, 1);
        if (ss) {
            for (int i = 0; i < ed->sel.count; i++) if (ed->sel.ids[i] >= 0 && ed->sel.ids[i] < l->sector_count) ss[ed->sel.ids[i]] = 1;
            for (int w = 0; w < l->wall_count; w++) {
                if (!model_wall_valid(l, w)) continue;
                int f = l->walls[w].front_sector, b = l->walls[w].back_sector;
                if ((f >= 0 && f < l->sector_count && ss[f]) || (b >= 0 && b < l->sector_count && ss[b])) {
                    MARK(l->walls[w].vertex_start);
                    MARK(l->walls[w].vertex_end);
                }
            }
            free(ss);
        }
    }
#undef MARK
    return n;
}

static void apply_delta(Editor *ed, float dx, float dz) {
    Level *l = &ed->level;
    if (!ed->move_orig) return;
    if (ed->move_mask) {
        for (int v = 0; v < ed->move_mask_count && v < l->vertex_count; v++) {
            if (!ed->move_mask[v]) continue;
            l->vertices[v].x = ed->move_orig[2 * v] + dx;
            l->vertices[v].z = ed->move_orig[2 * v + 1] + dz;
        }
    }
    if (ed->sel.kind == SEL_LIGHT) {
        for (int i = 0; i < ed->sel.count; i++) {
            int id = ed->sel.ids[i];
            if (id < 0 || id >= l->light_count) continue;
            l->lights[id].x = ed->move_orig[2 * id] + dx;
            l->lights[id].z = ed->move_orig[2 * id + 1] + dz;
        }
    } else if (ed->sel.kind == SEL_SPAWN) {
        l->player_spawn.x = ed->move_orig[0] + dx;
        l->player_spawn.z = ed->move_orig[1] + dz;
    }
}

static bool begin_move(Editor *ed) {
    Level *l = &ed->level;
    if (!model_copy(&ed->backup, l)) return false;
    ed->backup_valid = true;
    free(ed->move_mask); free(ed->move_orig);
    int V = l->vertex_count;
    ed->move_mask = calloc((size_t)V + 1, 1);
    ed->move_mask_count = V;
    int cap = V > l->light_count ? V : l->light_count;
    if (cap < 1) cap = 1;
    ed->move_orig = calloc((size_t)cap * 2, sizeof(float));
    if (!ed->move_mask || !ed->move_orig) return false;
    build_move_mask(ed, ed->move_mask);
    for (int v = 0; v < V; v++) { ed->move_orig[2 * v] = l->vertices[v].x; ed->move_orig[2 * v + 1] = l->vertices[v].z; }
    if (ed->sel.kind == SEL_LIGHT) {
        for (int i = 0; i < l->light_count; i++) { ed->move_orig[2 * i] = l->lights[i].x; ed->move_orig[2 * i + 1] = l->lights[i].z; }
    } else if (ed->sel.kind == SEL_SPAWN) {
        ed->move_orig[0] = l->player_spawn.x; ed->move_orig[1] = l->player_spawn.z;
    }
    /* The handle the user grabbed decides what lands on the grid. */
    Pick p = ed->press_pick;
    ed->anchor_x = ed->press_wx; ed->anchor_z = ed->press_wz;
    if (p.kind == SEL_VERTEX && p.id < V) { ed->anchor_x = l->vertices[p.id].x; ed->anchor_z = l->vertices[p.id].z; }
    else if (p.kind == SEL_WALL && model_wall_valid(l, p.id)) {
        Vertex a = l->vertices[l->walls[p.id].vertex_start];
        ed->anchor_x = a.x; ed->anchor_z = a.z;
    } else if (p.kind == SEL_SECTOR) {
        for (int w = 0; w < l->wall_count; w++) {
            if (!model_wall_valid(l, w)) continue;
            if (l->walls[w].front_sector == p.id || l->walls[w].back_sector == p.id) {
                Vertex a = l->vertices[l->walls[w].vertex_start];
                ed->anchor_x = a.x; ed->anchor_z = a.z;
                break;
            }
        }
    } else if (p.kind == SEL_LIGHT && p.id < l->light_count) { ed->anchor_x = l->lights[p.id].x; ed->anchor_z = l->lights[p.id].z; }
    else if (p.kind == SEL_SPAWN) { ed->anchor_x = l->player_spawn.x; ed->anchor_z = l->player_spawn.z; }
    ed->drag = DRAG_MOVE;
    ed->drag_invalid = false;
    ed->drag_dx = ed->drag_dz = 0;
    return true;
}

static void update_move(Editor *ed, float sx, float sy) {
    float cx, cz;
    screen_to_world(ed, sx, sy, &cx, &cz);
    float tx = cx - (ed->press_wx - ed->anchor_x), tz = cz - (ed->press_wz - ed->anchor_z);
    bool shift = (ui_mods(&ed->ui) & SDL_KMOD_SHIFT) != 0, lock_z = false, lock_x = false;
    if (shift) {
        if (fabsf(tx - ed->anchor_x) > fabsf(tz - ed->anchor_z)) { tz = ed->anchor_z; lock_z = true; }
        else { tx = ed->anchor_x; lock_x = true; }
    }
    float tsx, tsy;
    world_to_screen(ed, tx, tz, &tsx, &tsy);
    bool geometry = ed->sel.kind == SEL_VERTEX || ed->sel.kind == SEL_WALL || ed->sel.kind == SEL_SECTOR;
    int flags = geometry ? (SNAP_F_VERTEX | SNAP_F_GRID) : SNAP_F_GRID;
    if (ed->sel.kind == SEL_VERTEX && ed->sel.count == 1) flags |= SNAP_F_EDGE;
    SnapResult s = snap_point(ed, tsx, tsy, geometry ? ed->move_mask : NULL, flags);
    float dx = s.x - ed->anchor_x, dz = s.z - ed->anchor_z;
    if (lock_z) dz = 0;
    if (lock_x) dx = 0;
    ed->drag_dx = dx; ed->drag_dz = dz;
    ed->snap_hint = s; ed->snap_hint_valid = true;
    apply_delta(ed, dx, dz);
    ed->drag_invalid = geometry && model_has_crossings(&ed->level, ed->move_mask);
}

static void end_move(Editor *ed, bool cancel) {
    Level *l = &ed->level;
    bool moved = fabsf(ed->drag_dx) > 1e-6f || fabsf(ed->drag_dz) > 1e-6f;
    bool owned = ed->backup_valid;
    if (owned && (cancel || !moved || ed->drag_invalid)) {
        apply_delta(ed, 0, 0);
        if (ed->drag_invalid && !cancel) editor_toast(ed, true, "Can't move there: walls would cross each other");
        level_free(&ed->backup);
        memset(&ed->backup, 0, sizeof(ed->backup));
    } else if (owned) {
        bool topo = false;
        ModelDirty d = {0};
        bool geometry = ed->sel.kind == SEL_VERTEX || ed->sel.kind == SEL_WALL || ed->sel.kind == SEL_SECTOR;
        if (geometry) topo = model_resolve_contacts(l, ed->move_mask, &d);
        if (topo) { model_rebuild_sectors(l, &ed->tmpl, &d); sel_clear(ed); }
        else model_dirty_free(&d);
        history_push_snapshot(ed, &ed->backup);
        memset(&ed->backup, 0, sizeof(ed->backup));
        editor_changed(ed);
        if (topo) editor_toast(ed, false, "Joined: touching vertices and walls were connected");
    }
    ed->backup_valid = false;
    free(ed->move_mask); free(ed->move_orig);
    ed->move_mask = NULL; ed->move_orig = NULL; ed->move_mask_count = 0;
    ed->drag = DRAG_NONE;
    ed->snap_hint_valid = false;
}

void editor_cycle_grid(Editor *ed, int dir) {
    ed->grid_index = (ed->grid_index + dir + GRID_SIZE_COUNT) % GRID_SIZE_COUNT;
    editor_toast(ed, false, "Grid: %g units", editor_grid(ed));
}

void editor_set_tool(Editor *ed, Tool t) {
    if (ed->drag == DRAG_MOVE) end_move(ed, true);
    ed->drag = DRAG_NONE;
    ed->path_n = 0;
    ed->tool = t;
    ed->snap_hint_valid = false;
    ed->hover = (Pick){SEL_NONE, -1};
    if (t != TOOL_SELECT && ed->sel.kind != SEL_NONE && t != TOOL_SPAWN && t != TOOL_LIGHT) sel_clear(ed);
}

void editor_cancel_interaction(Editor *ed) {
    if (ed->drag == DRAG_MOVE) { end_move(ed, true); return; }
    if (ed->drag != DRAG_NONE) { ed->drag = DRAG_NONE; return; }
    if (ed->path_n > 0) { ed->path_n = 0; return; }
    if (ed->tool != TOOL_SELECT) { editor_set_tool(ed, TOOL_SELECT); return; }
    sel_clear(ed);
}

void editor_delete_selection(Editor *ed) {
    Level *l = &ed->level;
    if (ed->sel.kind == SEL_NONE) return;
    if (ed->sel.kind == SEL_SPAWN) { editor_toast(ed, true, "The player start can't be deleted. Drag it somewhere else instead."); return; }
    if (!history_push(ed)) return;
    uint64_t before = model_fingerprint(l);
    ModelDirty dirty = {0};
    size_t n = (size_t)(ed->sel.kind == SEL_VERTEX ? l->vertex_count : ed->sel.kind == SEL_WALL ? l->wall_count :
                        ed->sel.kind == SEL_SECTOR ? l->sector_count : l->light_count) + 1;
    unsigned char *mask = calloc(n, 1);
    if (!mask) { history_discard_last(ed); return; }
    for (int i = 0; i < ed->sel.count; i++) if (ed->sel.ids[i] >= 0 && (size_t)ed->sel.ids[i] < n - 1) mask[ed->sel.ids[i]] = 1;
    int count = ed->sel.count;
    SelKind kind = ed->sel.kind;
    switch (kind) {
        case SEL_VERTEX: model_delete_vertices(l, mask, &dirty); break;
        case SEL_WALL: model_delete_walls(l, mask, &dirty); break;
        case SEL_SECTOR: model_delete_sectors(l, mask, &dirty); break;
        case SEL_LIGHT: model_delete_lights(l, mask); break;
        default: break;
    }
    free(mask);
    model_rebuild_sectors(l, &ed->tmpl, &dirty);
    sel_clear(ed);
    if (model_fingerprint(l) == before) {
        history_discard_last(ed);
        editor_toast(ed, true, "Nothing was removed: this room is enclosed by several neighbours. Delete its walls to merge it.");
    } else {
        editor_toast(ed, false, "Deleted %d %s", count, kind == SEL_VERTEX ? "point(s)" : kind == SEL_WALL ? "wall(s)" : kind == SEL_SECTOR ? "room(s)" : "light(s)");
    }
    editor_changed(ed);
}

void editor_select_all(Editor *ed) {
    const Level *l = &ed->level;
    SelKind k = ed->sel.kind;
    if (k == SEL_NONE || k == SEL_SPAWN) k = SEL_SECTOR;
    sel_clear(ed);
    int n = k == SEL_VERTEX ? l->vertex_count : k == SEL_WALL ? l->wall_count : k == SEL_SECTOR ? l->sector_count : l->light_count;
    for (int i = 0; i < n; i++) {
        if (k == SEL_VERTEX && !vertex_pickable(ed, i)) continue;
        sel_add(ed, k, i);
    }
}

void editor_nudge(Editor *ed, float dx, float dz) {
    Level *l = &ed->level;
    if (ed->sel.kind == SEL_NONE) return;
    history_touch(ed, 0x4e554447u + (uint32_t)ed->sel.kind);
    unsigned char *mask = calloc((size_t)l->vertex_count + 1, 1);
    if (!mask) return;
    int n = build_move_mask(ed, mask);
    for (int v = 0; v < l->vertex_count; v++) if (mask[v]) { l->vertices[v].x += dx; l->vertices[v].z += dz; }
    if (ed->sel.kind == SEL_LIGHT) for (int i = 0; i < ed->sel.count; i++) { l->lights[ed->sel.ids[i]].x += dx; l->lights[ed->sel.ids[i]].z += dz; }
    if (ed->sel.kind == SEL_SPAWN) { l->player_spawn.x += dx; l->player_spawn.z += dz; }
    if (n > 0 && model_has_crossings(l, mask)) {
        for (int v = 0; v < l->vertex_count; v++) if (mask[v]) { l->vertices[v].x -= dx; l->vertices[v].z -= dz; }
        editor_toast(ed, true, "Can't nudge: walls would cross");
    } else if (n > 0) {
        ModelDirty d = {0};
        if (model_resolve_contacts(l, mask, &d)) { model_rebuild_sectors(l, &ed->tmpl, &d); sel_clear(ed); }
        else model_dirty_free(&d);
    }
    free(mask);
    editor_changed(ed);
}

void editor_clean_up(Editor *ed) {
    if (!history_push(ed)) return;
    int v = model_remove_unused_vertices(&ed->level);
    int s = model_remove_unused_sectors(&ed->level);
    sel_clear(ed);
    if (v + s == 0) { history_discard_last(ed); editor_toast(ed, false, "Nothing to clean up"); }
    else editor_toast(ed, false, "Removed %d unused vertices and %d unused sectors", v, s);
    editor_changed(ed);
}

void editor_duplicate_light(Editor *ed) {
    Level *l = &ed->level;
    if (ed->sel.kind != SEL_LIGHT || ed->sel.count == 0) return;
    if (!history_push(ed)) return;
    int n = ed->sel.count, first = l->light_count;
    PointLight *p = realloc(l->lights, (size_t)(l->light_count + n) * sizeof(PointLight));
    if (!p) { history_discard_last(ed); return; }
    l->lights = p;
    int *ids = malloc((size_t)n * sizeof(int));
    if (!ids) { history_discard_last(ed); return; }
    memcpy(ids, ed->sel.ids, (size_t)n * sizeof(int));
    sel_clear(ed);
    for (int i = 0; i < n; i++) {
        PointLight c = l->lights[ids[i]];
        c.x += editor_grid(ed) * 2; c.z += editor_grid(ed) * 2;
        l->lights[l->light_count] = c;
        sel_add(ed, SEL_LIGHT, l->light_count++);
    }
    (void)first;
    free(ids);
    editor_changed(ed);
}

void editor_select_problem(Editor *ed, const Problem *p) {
    const Level *l = &ed->level;
    editor_set_tool(ed, TOOL_SELECT);
    sel_clear(ed);
    switch (p->kind) {
        case PROBLEM_OPEN_END: if (p->object >= 0 && p->object < l->vertex_count) sel_set(ed, SEL_VERTEX, p->object); break;
        case PROBLEM_FLOOR_ABOVE_CEILING: case PROBLEM_BAD_TEXTURE:
            if (p->object >= 0 && p->object < l->sector_count && p->text[0] == 'S') sel_set(ed, SEL_SECTOR, p->object);
            else if (p->object >= 0 && p->object < l->wall_count) sel_set(ed, SEL_WALL, p->object);
            break;
        case PROBLEM_CROSSING: case PROBLEM_ORPHAN_WALL: case PROBLEM_TINY_WALL:
            if (p->object >= 0 && p->object < l->wall_count) sel_set(ed, SEL_WALL, p->object);
            break;
        case PROBLEM_LIGHT_OUTSIDE: case PROBLEM_LIGHT_HEIGHT:
            if (p->object >= 0 && p->object < l->light_count) sel_set(ed, SEL_LIGHT, p->object);
            break;
        case PROBLEM_SPAWN_OUTSIDE: sel_set(ed, SEL_SPAWN, 0); break;
        default: break;
    }
    if (p->kind != PROBLEM_UNUSED_VERTICES && p->kind != PROBLEM_BAD_REFERENCE) {
        view_center_on(ed, p->x, p->z);
        if (ed->zoom < 24.0f) ed->zoom = 24.0f;
    }
}

/* ===================================================================== */
/* Box select, wall split, path commit                                   */
/* ===================================================================== */

static void box_select(Editor *ed, float x0, float y0, float x1, float y1, bool add) {
    const Level *l = &ed->level;
    if (x0 > x1) { float t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { float t = y0; y0 = y1; y1 = t; }
    PickFilter f = ed->filter;
    if (!add) sel_clear(ed);
    bool found = false;
#define INSIDE(sx, sy) ((sx) >= x0 && (sx) <= x1 && (sy) >= y0 && (sy) <= y1)
    if (!found && (f == PICK_ALL || f == PICK_POINTS)) {
        for (int v = 0; v < l->vertex_count; v++) {
            if (!vertex_pickable(ed, v)) continue;
            float sx, sy;
            world_to_screen(ed, l->vertices[v].x, l->vertices[v].z, &sx, &sy);
            if (INSIDE(sx, sy)) { sel_add(ed, SEL_VERTEX, v); found = true; }
        }
    }
    if (!found && (f == PICK_ALL || f == PICK_THINGS)) {
        for (int i = 0; i < l->light_count; i++) {
            float sx, sy;
            world_to_screen(ed, l->lights[i].x, l->lights[i].z, &sx, &sy);
            if (INSIDE(sx, sy)) { sel_add(ed, SEL_LIGHT, i); found = true; }
        }
        if (!found) {
            float sx, sy;
            world_to_screen(ed, l->player_spawn.x, l->player_spawn.z, &sx, &sy);
            if (INSIDE(sx, sy)) { sel_set(ed, SEL_SPAWN, 0); found = true; }
        }
    }
    if (!found && (f == PICK_ALL || f == PICK_WALLS)) {
        for (int i = 0; i < l->wall_count; i++) {
            if (!model_wall_valid(l, i)) continue;
            float ax, ay, bx, by;
            world_to_screen(ed, l->vertices[l->walls[i].vertex_start].x, l->vertices[l->walls[i].vertex_start].z, &ax, &ay);
            world_to_screen(ed, l->vertices[l->walls[i].vertex_end].x, l->vertices[l->walls[i].vertex_end].z, &bx, &by);
            if (INSIDE(ax, ay) && INSIDE(bx, by)) { sel_add(ed, SEL_WALL, i); found = true; }
        }
    }
    if (!found && (f == PICK_ALL || f == PICK_ROOMS)) {
        for (int s = 0; s < l->sector_count; s++) {
            float bx0, bz0, bx1, bz1, sx0, sy0, sx1, sy1;
            if (!model_sector_bounds(l, s, &bx0, &bz0, &bx1, &bz1)) continue;
            world_to_screen(ed, bx0, bz0, &sx0, &sy0);
            world_to_screen(ed, bx1, bz1, &sx1, &sy1);
            if (INSIDE(sx0, sy0) && INSIDE(sx1, sy1)) { sel_add(ed, SEL_SECTOR, s); found = true; }
        }
    }
#undef INSIDE
}

static void split_wall_at(Editor *ed, int w, float sx, float sy) {
    Level *l = &ed->level;
    if (!model_wall_valid(l, w)) return;
    Vertex a = l->vertices[l->walls[w].vertex_start], b = l->vertices[l->walls[w].vertex_end];
    float ax, ay, bx, by, t;
    world_to_screen(ed, a.x, a.z, &ax, &ay);
    world_to_screen(ed, b.x, b.z, &bx, &by);
    dist_to_seg(sx, sy, ax, ay, bx, by, &t);
    float x = a.x + (b.x - a.x) * t, z = a.z + (b.z - a.z) * t;
    if (ed->snap && !(ui_mods(&ed->ui) & SDL_KMOD_CTRL)) {
        float g = editor_grid(ed);
        if (fabsf(b.x - a.x) < 1e-5f) z = roundf(z / g) * g;
        else if (fabsf(b.z - a.z) < 1e-5f) x = roundf(x / g) * g;
    }
    if (!history_push(ed)) return;
    int nv;
    if (model_split_wall(l, w, x, z, &nv)) {
        editor_changed(ed);
        sel_set(ed, SEL_VERTEX, nv);
        editor_toast(ed, false, "Wall split. Drag the new point to bend it.");
    } else {
        history_discard_last(ed);
        editor_toast(ed, true, "Can't split here: too close to an existing point");
    }
}

static bool seg_cross_strict(float ax, float az, float bx, float bz, float cx, float cz, float dx, float dz) {
    float d1 = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
    float d2 = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
    float d3 = (dx - cx) * (az - cz) - (dz - cz) * (ax - cx);
    float d4 = (dx - cx) * (bz - cz) - (dz - cz) * (bx - cx);
    return ((d1 > 1e-6f && d2 < -1e-6f) || (d1 < -1e-6f && d2 > 1e-6f)) &&
           ((d3 > 1e-6f && d4 < -1e-6f) || (d3 < -1e-6f && d4 > 1e-6f));
}

/* Would the segment p[n-1] -> (x,z) cross the line drawn so far? */
static bool path_crosses_itself(const Vertex *p, int n, float x, float z, bool closing) {
    if (n < 2) return false;
    Vertex last = p[n - 1];
    for (int i = 0; i + 1 < n; i++) {
        if (i + 1 == n - 1) continue;              /* neighbour of the new segment */
        if (closing && i == 0) continue;           /* shares the first point */
        if (seg_cross_strict(last.x, last.z, x, z, p[i].x, p[i].z, p[i + 1].x, p[i + 1].z)) return true;
    }
    return false;
}

static void commit_shape(Editor *ed, const Vertex *pts, int n, bool closed) {
    Level *l = &ed->level;
    if (n < (closed ? 3 : 2)) return;
    if (!history_push(ed)) return;
    uint64_t before = model_fingerprint(l);
    int sectors_before = l->sector_count;
    if (!model_add_path(l, pts, n, closed, ed->tmpl_wall_tex)) {
        history_discard_last(ed);
        editor_toast(ed, true, closed ? "That shape is too small or degenerate" : "Couldn't add that line");
        return;
    }
    model_rebuild_sectors(l, &ed->tmpl, NULL);
    ed->path_n = 0;
    sel_clear(ed);
    if (model_fingerprint(l) == before) {
        history_discard_last(ed);
        editor_toast(ed, false, "Nothing new: those walls already exist");
    } else if (l->sector_count > sectors_before) {
        sel_set(ed, SEL_SECTOR, l->sector_count - 1);
        editor_toast(ed, false, "Room created. Adjust its heights and textures on the right.");
    } else if (!closed) {
        editor_toast(ed, false, "Line added");
    }
    editor_changed(ed);
}

/* ===================================================================== */
/* Pointer + keyboard handling                                           */
/* ===================================================================== */

static bool press_blocked(Editor *ed, float sx, float sy) {
    return ed->popup != POPUP_NONE || ed->menu_open >= 0 || ui_text_editing(&ed->ui) ||
           ui_over_panel(&ed->ui, sx, sy) || !in_canvas(ed, sx, sy);
}

static SnapResult draw_snap(Editor *ed, float sx, float sy) {
    SnapResult s = snap_point(ed, sx, sy, NULL, SNAP_F_ALL);
    if (ed->tool == TOOL_DRAW && ed->path_n > 0 && (ui_mods(&ed->ui) & SDL_KMOD_SHIFT)) {
        Vertex last = ed->path[ed->path_n - 1];
        constrain_angle(ed, last.x, last.z, &s.x, &s.z);
        s.kind = SNAP_GRID;
    }
    return s;
}

static float spawn_yaw_from(Editor *ed, float sx, float sy) {
    float px, py;
    world_to_screen(ed, ed->level.player_spawn.x, ed->level.player_spawn.z, &px, &py);
    float yaw = atan2f(sx - px, -(sy - py)) * (180.0f / PI_F);
    if (ed->snap && !(ui_mods(&ed->ui) & SDL_KMOD_CTRL)) yaw = roundf(yaw / 15.0f) * 15.0f;
    return yaw;
}

static void press_select(Editor *ed, float sx, float sy, int clicks, bool shift) {
    const Level *l = &ed->level;
    if (ed->sel.kind == SEL_SPAWN) {   /* rotation handle */
        float x, y;
        world_to_screen(ed, l->player_spawn.x, l->player_spawn.z, &x, &y);
        float yaw = l->player_spawn.yaw * (PI_F / 180.0f);
        if (hypotf(sx - (x + sinf(yaw) * 34.0f), sy - (y - cosf(yaw) * 34.0f)) < 9.0f && history_push(ed)) {
            ed->drag = DRAG_AIM;
            ed->aiming_after_place = false;
            return;
        }
    }
    Pick p = pick_at(ed, sx, sy);
    ed->press_sx = sx; ed->press_sy = sy;
    screen_to_world(ed, sx, sy, &ed->press_wx, &ed->press_wz);
    ed->press_pick = p;
    ed->press_shift = shift;
    if (p.kind == SEL_WALL && clicks >= 2) { split_wall_at(ed, p.id, sx, sy); return; }
    if (p.kind == SEL_NONE) {
        if (!shift) sel_clear(ed);
        ed->drag = DRAG_BOX;
        return;
    }
    bool selected = sel_has(ed, p.kind, p.id);
    if (shift) {
        if (selected) { sel_remove(ed, p.kind, p.id); ed->drag = DRAG_NONE; return; }
        sel_add(ed, p.kind, p.id);
    } else if (!selected) {
        sel_set(ed, p.kind, p.id);
    }
    ed->press_was_selected = selected && !shift;
    ed->drag = DRAG_PRESS;
}

static void press_draw(Editor *ed, float sx, float sy, int clicks) {
    SnapResult s = draw_snap(ed, sx, sy);
    if (clicks >= 2 && ed->path_n >= 2) { commit_shape(ed, ed->path, ed->path_n, false); return; }
    if (ed->path_n >= 3) {
        float fx, fy;
        world_to_screen(ed, ed->path[0].x, ed->path[0].z, &fx, &fy);
        if (hypotf(sx - fx, sy - fy) < SNAP_VERTEX_PX + 2.0f) {
            if (path_crosses_itself(ed->path, ed->path_n, ed->path[0].x, ed->path[0].z, true))
                editor_toast(ed, true, "The closing line would cross your drawing");
            else commit_shape(ed, ed->path, ed->path_n, true);
            return;
        }
    }
    if (ed->path_n > 0) {
        Vertex last = ed->path[ed->path_n - 1];
        if (fabsf(s.x - last.x) < 1e-4f && fabsf(s.z - last.z) < 1e-4f) return;
    }
    if (ed->path_n >= 2 && path_crosses_itself(ed->path, ed->path_n, s.x, s.z, false)) {
        editor_toast(ed, true, "Lines can't cross each other. Click a different spot.");
        return;
    }
    if (ed->path_n < MAX_PATH_POINTS) ed->path[ed->path_n++] = (Vertex){s.x, s.z};
}

static void press_light(Editor *ed, float sx, float sy) {
    Level *l = &ed->level;
    int hit = -1;
    float best = PICK_THING_PX;
    for (int i = 0; i < l->light_count; i++) {
        float x, y;
        world_to_screen(ed, l->lights[i].x, l->lights[i].z, &x, &y);
        float d = hypotf(sx - x, sy - y);
        if (d < best) { best = d; hit = i; }
    }
    if (hit >= 0) {   /* grab an existing light */
        ed->press_sx = sx; ed->press_sy = sy;
        screen_to_world(ed, sx, sy, &ed->press_wx, &ed->press_wz);
        ed->press_pick = (Pick){SEL_LIGHT, hit};
        if (!sel_has(ed, SEL_LIGHT, hit)) sel_set(ed, SEL_LIGHT, hit);
        ed->press_was_selected = false;
        ed->drag = DRAG_PRESS;
        return;
    }
    SnapResult s = snap_point(ed, sx, sy, NULL, SNAP_F_GRID);
    if (!history_push(ed)) return;
    PointLight *p = realloc(l->lights, (size_t)(l->light_count + 1) * sizeof(PointLight));
    if (!p) { history_discard_last(ed); return; }
    l->lights = p;
    int sector = model_sector_at(l, s.x, s.z);
    float y = 2.5f;
    if (sector >= 0) y = l->sectors[sector].floor_height + (l->sectors[sector].ceiling_height - l->sectors[sector].floor_height) * 0.75f;
    l->lights[l->light_count] = (PointLight){s.x, y, s.z, 8.0f, 1.2f, 255, 224, 176, 0};
    sel_set(ed, SEL_LIGHT, l->light_count++);
    editor_changed(ed);
}

static void press_spawn(Editor *ed, float sx, float sy) {
    SnapResult s = snap_point(ed, sx, sy, NULL, SNAP_F_GRID);
    if (!history_push(ed)) return;
    ed->level.player_spawn.x = s.x;
    ed->level.player_spawn.z = s.z;
    ed->press_sx = sx; ed->press_sy = sy;
    sel_set(ed, SEL_SPAWN, 0);
    ed->drag = DRAG_AIM;
    ed->aiming_after_place = true;
    editor_changed(ed);
}

static void on_mouse_down(Editor *ed, SDL_Event *e) {
    float sx = e->button.x, sy = e->button.y;
    bool shift = (ui_mods(&ed->ui) & SDL_KMOD_SHIFT) != 0;
    g_last_sx = sx; g_last_sy = sy;
    if (e->button.button == SDL_BUTTON_MIDDLE) {
        if (!press_blocked(ed, sx, sy)) ed->drag = DRAG_PAN, ed->pan_with_right = false;
        return;
    }
    if (e->button.button == SDL_BUTTON_RIGHT) {
        if (press_blocked(ed, sx, sy)) return;
        ed->right_pending = true;
        ed->right_press_sx = sx; ed->right_press_sy = sy;
        return;
    }
    if (e->button.button != SDL_BUTTON_LEFT || press_blocked(ed, sx, sy)) return;
    if (g_space_down) { ed->drag = DRAG_PAN; ed->pan_with_right = false; return; }
    if (ed->drag != DRAG_NONE) return;
    switch (ed->tool) {
        case TOOL_SELECT: press_select(ed, sx, sy, e->button.clicks, shift); break;
        case TOOL_DRAW: press_draw(ed, sx, sy, e->button.clicks); break;
        case TOOL_RECT: {
            SnapResult s = draw_snap(ed, sx, sy);
            ed->rect_x0 = ed->rect_x1 = s.x; ed->rect_z0 = ed->rect_z1 = s.z;
            ed->drag = DRAG_RECT;
            break;
        }
        case TOOL_LIGHT: press_light(ed, sx, sy); break;
        case TOOL_SPAWN: press_spawn(ed, sx, sy); break;
        default: break;
    }
}

static void finish_rect(Editor *ed) {
    float w = fabsf(ed->rect_x1 - ed->rect_x0), h = fabsf(ed->rect_z1 - ed->rect_z0);
    ed->drag = DRAG_NONE;
    if (w < 0.1f || h < 0.1f) { editor_toast(ed, false, "Drag to size the room, then release"); return; }
    Vertex p[4] = {{ed->rect_x0, ed->rect_z0}, {ed->rect_x1, ed->rect_z0}, {ed->rect_x1, ed->rect_z1}, {ed->rect_x0, ed->rect_z1}};
    commit_shape(ed, p, 4, true);
}

static void on_mouse_up(Editor *ed, SDL_Event *e) {
    float sx = e->button.x, sy = e->button.y;
    if (e->button.button == SDL_BUTTON_MIDDLE) { if (ed->drag == DRAG_PAN) ed->drag = DRAG_NONE; return; }
    if (e->button.button == SDL_BUTTON_RIGHT) {
        if (ed->drag == DRAG_PAN && ed->pan_with_right) { ed->drag = DRAG_NONE; ed->right_pending = false; return; }
        if (!ed->right_pending) return;
        ed->right_pending = false;          /* a right *click*: back out one step */
        if (ed->drag == DRAG_RECT) ed->drag = DRAG_NONE;
        else if (ed->tool == TOOL_DRAW && ed->path_n > 0) ed->path_n--;
        else if (ed->tool != TOOL_SELECT && ed->drag == DRAG_NONE) editor_set_tool(ed, TOOL_SELECT);
        return;
    }
    if (e->button.button != SDL_BUTTON_LEFT) return;
    switch (ed->drag) {
        case DRAG_PAN: ed->drag = DRAG_NONE; break;
        case DRAG_PRESS:
            if (ed->press_was_selected && ed->sel.count > 1) sel_set(ed, ed->press_pick.kind, ed->press_pick.id);
            ed->drag = DRAG_NONE;
            break;
        case DRAG_MOVE: end_move(ed, false); break;
        case DRAG_BOX:
            if (hypotf(sx - ed->press_sx, sy - ed->press_sy) > DRAG_START_PX)
                box_select(ed, ed->press_sx, ed->press_sy, sx, sy, ed->press_shift);
            ed->drag = DRAG_NONE;
            break;
        case DRAG_RECT: finish_rect(ed); break;
        case DRAG_AIM:
            ed->drag = DRAG_NONE;
            editor_changed(ed);
            if (ed->aiming_after_place) { ed->aiming_after_place = false; editor_set_tool(ed, TOOL_SELECT); sel_set(ed, SEL_SPAWN, 0); }
            break;
        default: break;
    }
}

static void on_mouse_motion(Editor *ed, SDL_Event *e) {
    float sx = e->motion.x, sy = e->motion.y;
    screen_to_world(ed, sx, sy, &ed->cur_wx, &ed->cur_wz);
    if (ed->right_pending && ed->drag == DRAG_NONE &&
        hypotf(sx - ed->right_press_sx, sy - ed->right_press_sy) > 5.0f) {
        ed->drag = DRAG_PAN; ed->pan_with_right = true; ed->right_pending = false;
    }
    switch (ed->drag) {
        case DRAG_PAN:
            ed->cam_x -= (sx - g_last_sx) / ed->zoom;
            ed->cam_z -= (sy - g_last_sy) / ed->zoom;
            break;
        case DRAG_PRESS:
            if (hypotf(sx - ed->press_sx, sy - ed->press_sy) > DRAG_START_PX) {
                if (begin_move(ed)) update_move(ed, sx, sy);
                else ed->drag = DRAG_NONE;
            }
            break;
        case DRAG_MOVE: update_move(ed, sx, sy); break;
        case DRAG_RECT: {
            SnapResult s = draw_snap(ed, sx, sy);
            float x1 = s.x, z1 = s.z;
            if (ui_mods(&ed->ui) & SDL_KMOD_SHIFT) {
                float dx = x1 - ed->rect_x0, dz = z1 - ed->rect_z0;
                float m = fmaxf(fabsf(dx), fabsf(dz));
                x1 = ed->rect_x0 + (dx < 0 ? -m : m);
                z1 = ed->rect_z0 + (dz < 0 ? -m : m);
            }
            ed->rect_x1 = x1; ed->rect_z1 = z1;
            ed->snap_hint = s; ed->snap_hint_valid = true;
            break;
        }
        case DRAG_AIM:
            if (hypotf(sx - ed->press_sx, sy - ed->press_sy) > 6.0f || !ed->aiming_after_place)
                ed->level.player_spawn.yaw = spawn_yaw_from(ed, sx, sy);
            break;
        case DRAG_BOX: break;
        case DRAG_NONE: {
            bool over = press_blocked(ed, sx, sy);
            ed->hover = (Pick){SEL_NONE, -1};
            ed->snap_hint_valid = false;
            if (over) break;
            if (ed->tool == TOOL_SELECT) ed->hover = pick_at(ed, sx, sy);
            else if (ed->tool == TOOL_DRAW || ed->tool == TOOL_RECT) { ed->snap_hint = draw_snap(ed, sx, sy); ed->snap_hint_valid = true; }
            else if (ed->tool == TOOL_LIGHT || ed->tool == TOOL_SPAWN) { ed->snap_hint = snap_point(ed, sx, sy, NULL, SNAP_F_GRID); ed->snap_hint_valid = true; }
            break;
        }
    }
    g_last_sx = sx; g_last_sy = sy;
}

static void finish_path_from_keyboard(Editor *ed) {
    bool shift = (ui_mods(&ed->ui) & SDL_KMOD_SHIFT) != 0;
    if (ed->path_n < 2) return;
    if (ed->path_n >= 3 && !shift) {
        if (path_crosses_itself(ed->path, ed->path_n, ed->path[0].x, ed->path[0].z, true))
            editor_toast(ed, true, "The closing line would cross your drawing");
        else commit_shape(ed, ed->path, ed->path_n, true);
    } else {
        commit_shape(ed, ed->path, ed->path_n, false);
    }
}

static void on_key(Editor *ed, SDL_Event *e) {
    SDL_Keycode k = e->key.key;
    if (k == SDLK_SPACE && !ui_text_editing(&ed->ui)) { g_space_down = true; return; }
    if (ui_text_editing(&ed->ui) || ed->popup != POPUP_NONE || ed->menu_open >= 0) return;
    SDL_Keymod mod = ui_mods(&ed->ui);
    if (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) {
        if (k == SDLK_A) editor_select_all(ed);
        return;
    }
    switch (k) {
        case SDLK_ESCAPE: editor_cancel_interaction(ed); break;
        case SDLK_DELETE:
        case SDLK_BACKSPACE:
            if (ed->tool == TOOL_DRAW && ed->path_n > 0) ed->path_n--;
            else editor_delete_selection(ed);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (ed->tool == TOOL_DRAW) finish_path_from_keyboard(ed);
            break;
        case SDLK_LEFT: case SDLK_RIGHT: case SDLK_UP: case SDLK_DOWN: {
            float dx = k == SDLK_LEFT ? -1.0f : k == SDLK_RIGHT ? 1.0f : 0.0f;
            float dz = k == SDLK_UP ? -1.0f : k == SDLK_DOWN ? 1.0f : 0.0f;
            if (ed->sel.kind != SEL_NONE) {
                float step = (mod & SDL_KMOD_ALT) ? 0.05f : editor_grid(ed);
                editor_nudge(ed, dx * step, dz * step);
            } else {
                ed->cam_x += dx * 48.0f / ed->zoom;
                ed->cam_z += dz * 48.0f / ed->zoom;
            }
            break;
        }
        default: break;
    }
}

void canvas_event(Editor *ed, SDL_Event *e) {
    switch (e->type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: on_mouse_down(ed, e); break;
        case SDL_EVENT_MOUSE_BUTTON_UP: on_mouse_up(ed, e); break;
        case SDL_EVENT_MOUSE_MOTION: on_mouse_motion(ed, e); break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (!press_blocked(ed, ed->ui.mx, ed->ui.my) && e->wheel.y != 0)
                zoom_at(ed, ed->ui.mx, ed->ui.my, powf(1.16f, e->wheel.y));
            break;
        case SDL_EVENT_KEY_DOWN: on_key(ed, e); break;
        case SDL_EVENT_KEY_UP:
            if (e->key.key == SDLK_SPACE) { g_space_down = false; if (ed->drag == DRAG_PAN && !ed->pan_with_right) ed->drag = DRAG_NONE; }
            break;
        default: break;
    }
}

void canvas_update_cursor(Editor *ed) {
    static SDL_Cursor *cur[4];
    static int current = -1;
    if (!cur[0]) {
        cur[0] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        cur[1] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
        cur[2] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);
        cur[3] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    }
    int want = 0;
    bool over = ui_over_panel(&ed->ui, ed->ui.mx, ed->ui.my) || !in_canvas(ed, ed->ui.mx, ed->ui.my);
    if (!over && ed->popup == POPUP_NONE) {
        if (ed->drag == DRAG_PAN || g_space_down || ed->drag == DRAG_MOVE) want = 2;
        else if (ed->tool != TOOL_SELECT) want = 1;
        else if (ed->hover.kind != SEL_NONE) want = 3;
    }
    if (want != current && cur[want]) { SDL_SetCursor(cur[want]); current = want; }
}

/* ===================================================================== */
/* Rendering                                                             */
/* ===================================================================== */

static UIColor lerp_col(UIColor a, UIColor b, float t) {
    t = ed_clampf(t, 0, 1);
    return UI_RGBA((Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t),
                   (Uint8)(a.b + (b.b - a.b) * t), (Uint8)(a.a + (b.a - a.a) * t));
}

static UIColor sector_color(const Editor *ed, int s) {
    const Sector *sc = &ed->level.sectors[s];
    switch (ed->fill) {
        case FILL_HEIGHT: {
            float t = ed_clampf((sc->floor_height + 2.0f) / 6.0f, 0, 1);
            return lerp_col(UI_RGB(36, 50, 84), UI_RGB(92, 132, 104), t);
        }
        case FILL_LIGHT: {
            float k = 0.18f + 0.62f * ed_clampf(sc->light_level / 255.0f, 0, 1);
            return UI_RGB((Uint8)(sc->light_r * k * 0.8f), (Uint8)(sc->light_g * k * 0.8f), (Uint8)(sc->light_b * k * 0.8f));
        }
        default: {
            uint32_t h = (uint32_t)s * 2654435761u;
            return UI_RGB(40 + (h >> 24) % 70, 40 + (h >> 16) % 70, 50 + (h >> 8) % 80);
        }
    }
}

static void draw_grid(Editor *ed, SDL_FRect c) {
    UI *ui = &ed->ui;
    float x0, z0, x1, z1;
    screen_to_world(ed, c.x, c.y, &x0, &z0);
    screen_to_world(ed, c.x + c.w, c.y + c.h, &x1, &z1);
    float step = editor_grid(ed);
    while (step * ed->zoom < 10.0f) step *= 2.0f;
    if (ed->show_grid) {
        for (int i = (int)floorf(x0 / step); i <= (int)ceilf(x1 / step); i++) {
            float sx, sy;
            world_to_screen(ed, i * step, 0, &sx, &sy);
            ui_line(ui, sx, c.y, sx, c.y + c.h, i % 4 == 0 ? UI_RGB(46, 51, 62) : UI_RGB(33, 37, 45));
        }
        for (int i = (int)floorf(z0 / step); i <= (int)ceilf(z1 / step); i++) {
            float sx, sy;
            world_to_screen(ed, 0, i * step, &sx, &sy);
            ui_line(ui, c.x, sy, c.x + c.w, sy, i % 4 == 0 ? UI_RGB(46, 51, 62) : UI_RGB(33, 37, 45));
        }
    }
    float ox, oy;
    world_to_screen(ed, 0, 0, &ox, &oy);
    if (ox > c.x && ox < c.x + c.w) ui_line(ui, ox, c.y, ox, c.y + c.h, UI_RGBA(90, 140, 220, 150));
    if (oy > c.y && oy < c.y + c.h) ui_line(ui, c.x, oy, c.x + c.w, oy, UI_RGBA(220, 100, 100, 150));
}

/* Even-odd scanline fill: handles concave rooms and rooms with holes. */
static void fill_sector(Editor *ed, int s, SDL_FRect c, UIColor col) {
    if (!g_sec_start || s >= g_sec_count) return;
    const Level *l = &ed->level;
    enum { MAX_E = 2048, MAX_R = 512 };
    static float E[MAX_E][4];
    int a = g_sec_start[s], n = g_sec_start[s + 1] - a;
    if (n < 3) return;
    if (n > MAX_E) n = MAX_E;
    float miny = 1e30f, maxy = -1e30f, minx = 1e30f, maxx = -1e30f;
    for (int i = 0; i < n; i++) {
        int w = g_sec_walls[a + i];
        if (!model_wall_valid(l, w)) { E[i][0] = E[i][1] = E[i][2] = E[i][3] = 0; continue; }
        Vertex p = l->vertices[l->walls[w].vertex_start], q = l->vertices[l->walls[w].vertex_end];
        world_to_screen(ed, p.x, p.z, &E[i][0], &E[i][1]);
        world_to_screen(ed, q.x, q.z, &E[i][2], &E[i][3]);
        miny = fminf(miny, fminf(E[i][1], E[i][3])); maxy = fmaxf(maxy, fmaxf(E[i][1], E[i][3]));
        minx = fminf(minx, fminf(E[i][0], E[i][2])); maxx = fmaxf(maxx, fmaxf(E[i][0], E[i][2]));
    }
    if (maxx < c.x || minx > c.x + c.w || maxy < c.y || miny > c.y + c.h) return;
    float ystart = fmaxf(miny, c.y), yend = fminf(maxy, c.y + c.h);
    ystart = floorf(ystart / 2.0f) * 2.0f;
    ui_set_color(&ed->ui, col);
    SDL_FRect rects[MAX_R];
    int nr = 0;
    static float xs[MAX_E];
    for (float y = ystart; y < yend; y += 2.0f) {
        float yy = y + 1.0f;
        int nx = 0;
        for (int i = 0; i < n; i++) {
            float y1 = E[i][1], y2 = E[i][3];
            if ((y1 > yy) == (y2 > yy)) continue;
            xs[nx++] = E[i][0] + (yy - y1) * (E[i][2] - E[i][0]) / (y2 - y1);
        }
        for (int i = 1; i < nx; i++) {
            float v = xs[i];
            int j = i - 1;
            while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; }
            xs[j + 1] = v;
        }
        for (int i = 0; i + 1 < nx; i += 2) {
            float xa = fmaxf(xs[i], c.x), xb = fminf(xs[i + 1], c.x + c.w);
            if (xb <= xa) continue;
            rects[nr++] = (SDL_FRect){xa, y, xb - xa, 2.0f};
            if (nr == MAX_R) { SDL_RenderFillRects(ed->ui.renderer, rects, nr); nr = 0; }
        }
    }
    if (nr) SDL_RenderFillRects(ed->ui.renderer, rects, nr);
}

static void draw_sectors(Editor *ed, SDL_FRect c) {
    const Level *l = &ed->level;
    for (int s = 0; s < l->sector_count; s++) {
        UIColor col = sector_color(ed, s);
        if (l->sectors[s].floor_height >= l->sectors[s].ceiling_height) col = lerp_col(col, C_ERR, 0.5f);
        if (sel_has(ed, SEL_SECTOR, s)) col = lerp_col(col, C_ACCENT, 0.5f);
        else if (ed->hover.kind == SEL_SECTOR && ed->hover.id == s && ed->drag == DRAG_NONE) col = lerp_col(col, UI_RGB(255, 255, 255), 0.16f);
        col.a = 236;
        fill_sector(ed, s, c, col);
    }
}

static bool offscreen_seg(SDL_FRect c, float ax, float ay, float bx, float by) {
    return (ax < c.x && bx < c.x) || (ax > c.x + c.w && bx > c.x + c.w) ||
           (ay < c.y && by < c.y) || (ay > c.y + c.h && by > c.y + c.h);
}

static void draw_walls(Editor *ed, SDL_FRect c) {
    UI *ui = &ed->ui;
    const Level *l = &ed->level;
    unsigned char *sec_sel = NULL;
    if (ed->sel.kind == SEL_SECTOR && l->sector_count > 0) {
        sec_sel = calloc((size_t)l->sector_count + 1, 1);
        if (sec_sel) for (int i = 0; i < ed->sel.count; i++) if (ed->sel.ids[i] < l->sector_count) sec_sel[ed->sel.ids[i]] = 1;
    }
    for (int pass = 0; pass < 2; pass++) {   /* highlighted walls last, on top */
        for (int i = 0; i < l->wall_count; i++) {
            if (!model_wall_valid(l, i)) continue;
            const Wall *w = &l->walls[i];
            bool sel = sel_has(ed, SEL_WALL, i);
            bool hov = ed->hover.kind == SEL_WALL && ed->hover.id == i && ed->drag == DRAG_NONE;
            bool in_sel_sector = sec_sel && ((w->front_sector >= 0 && sec_sel[w->front_sector]) || (w->back_sector >= 0 && sec_sel[w->back_sector]));
            bool moving_bad = ed->drag == DRAG_MOVE && ed->drag_invalid && ed->move_mask &&
                              (ed->move_mask[w->vertex_start] || ed->move_mask[w->vertex_end]);
            bool emphasised = sel || hov || in_sel_sector || moving_bad;
            if ((pass == 1) != emphasised) continue;
            Vertex p = l->vertices[w->vertex_start], q = l->vertices[w->vertex_end];
            float ax, ay, bx, by;
            world_to_screen(ed, p.x, p.z, &ax, &ay);
            world_to_screen(ed, q.x, q.z, &bx, &by);
            if (offscreen_seg(c, ax, ay, bx, by)) continue;
            bool orphan = w->front_sector < 0 && w->back_sector < 0;
            bool solid = w->front_sector < 0 || w->back_sector < 0;
            UIColor col = orphan ? C_WARN : solid ? UI_RGB(228, 232, 242) : UI_RGB(84, 164, 250);
            float th = orphan ? 1.5f : solid ? 2.0f : 1.2f;
            if (in_sel_sector) { col = UI_RGB(255, 226, 120); th = 2.6f; }
            if (hov) { col = lerp_col(col, UI_RGB(255, 255, 255), 0.5f); th += 1.0f; }
            if (sel) { col = UI_RGB(255, 200, 40); th = 3.5f; }
            if (moving_bad) { col = C_ERR; th = 3.5f; }
            ui_thick_line(ui, ax, ay, bx, by, th, col);
            float len = hypotf(bx - ax, by - ay);
            if (ed->show_normals && len > 22.0f && (solid || sel) && !orphan) {
                float dx = (q.x - p.x), dz = (q.z - p.z), wl = hypotf(dx, dz);
                if (wl > 1e-6f) {
                    float sign = w->front_sector >= 0 ? 1.0f : -1.0f;
                    float nx = -dz / wl * sign, nz = dx / wl * sign;
                    float mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f;
                    ui_line(ui, mx, my, mx + nx * 6.0f, my + nz * 6.0f, sel ? col : UI_RGBA(228, 232, 242, 130));
                }
            }
        }
    }
    free(sec_sel);
}

static void draw_vertices(Editor *ed, SDL_FRect c) {
    UI *ui = &ed->ui;
    const Level *l = &ed->level;
    bool show = ed->show_vertices || ed->zoom >= 14.0f;
    for (int v = 0; v < l->vertex_count; v++) {
        bool used = v < ed->vused_count && ed->vused[v];
        bool sel = sel_has(ed, SEL_VERTEX, v);
        bool hov = ed->hover.kind == SEL_VERTEX && ed->hover.id == v && ed->drag == DRAG_NONE;
        if (!used && !ed->show_unused) continue;
        if (!sel && !hov && !show && used) continue;
        float x, y;
        world_to_screen(ed, l->vertices[v].x, l->vertices[v].z, &x, &y);
        if (x < c.x - 8 || x > c.x + c.w + 8 || y < c.y - 8 || y > c.y + c.h + 8) continue;
        if (!used) { ui_fill(ui, (SDL_FRect){x - 1.5f, y - 1.5f, 3, 3}, UI_RGB(220, 90, 220)); continue; }
        float r = sel ? 4.5f : hov ? 4.5f : 3.0f;
        UIColor col = sel ? UI_RGB(255, 200, 40) : hov ? UI_RGB(255, 255, 255) : UI_RGB(190, 198, 214);
        ui_fill(ui, (SDL_FRect){x - r - 1, y - r - 1, 2 * r + 2, 2 * r + 2}, UI_RGB(10, 11, 14));
        ui_fill(ui, (SDL_FRect){x - r, y - r, 2 * r, 2 * r}, col);
    }
}

static void draw_things(Editor *ed, SDL_FRect c) {
    UI *ui = &ed->ui;
    const Level *l = &ed->level;
    for (int i = 0; i < l->light_count; i++) {
        const PointLight *p = &l->lights[i];
        float x, y;
        world_to_screen(ed, p->x, p->z, &x, &y);
        bool sel = sel_has(ed, SEL_LIGHT, i);
        bool hov = ed->hover.kind == SEL_LIGHT && ed->hover.id == i && ed->drag == DRAG_NONE;
        float rad = p->radius * ed->zoom;
        if (x + rad < c.x || x - rad > c.x + c.w || y + rad < c.y || y - rad > c.y + c.h) { if (x < c.x - 20 || x > c.x + c.w + 20) continue; }
        UIColor lc = UI_RGB((Uint8)p->r, (Uint8)p->g, (Uint8)p->b);
        if ((ed->show_radius || sel || hov) && rad > 5.0f) {
            UIColor rc = lc; rc.a = sel ? 40 : 14;
            ui_disc(ui, x, y, rad, rc);
            rc.a = sel ? 200 : 90;
            ui_circle(ui, x, y, rad, rc);
        }
        for (int k = 0; k < 8; k++) {
            float a = k * PI_F / 4.0f;
            ui_line(ui, x + cosf(a) * 8, y + sinf(a) * 8, x + cosf(a) * 11, y + sinf(a) * 11, lc);
        }
        ui_disc(ui, x, y, 6.5f, UI_RGB(10, 11, 14));
        ui_disc(ui, x, y, 5.0f, lc);
        if (sel || hov) ui_circle(ui, x, y, 13.0f, sel ? UI_RGB(255, 200, 40) : UI_RGB(255, 255, 255));
    }
    /* player start */
    const PlayerSpawn *sp = &l->player_spawn;
    float x, y;
    world_to_screen(ed, sp->x, sp->z, &x, &y);
    float yaw = sp->yaw * (PI_F / 180.0f);
    float fx = sinf(yaw), fy = -cosf(yaw);
    bool sel = ed->sel.kind == SEL_SPAWN;
    bool hov = ed->hover.kind == SEL_SPAWN && ed->drag == DRAG_NONE;
    UIColor gc = UI_RGB(96, 220, 132);
    for (int s = -1; s <= 1; s += 2) {
        float a = yaw + s * 0.6f;
        ui_line(ui, x, y, x + sinf(a) * 44, y - cosf(a) * 44, UI_RGBA(96, 220, 132, 110));
    }
    ui_disc(ui, x, y, 9.0f, UI_RGB(10, 11, 14));
    ui_disc(ui, x, y, 7.5f, gc);
    ui_thick_line(ui, x, y, x + fx * 22, y + fy * 22, 2.5f, UI_RGB(10, 11, 14));
    ui_thick_line(ui, x, y, x + fx * 22, y + fy * 22, 1.5f, gc);
    ui_tri(ui, x + fx * 30, y + fy * 30, x + fx * 20 - fy * 5, y + fy * 20 + fx * 5, x + fx * 20 + fy * 5, y + fy * 20 - fx * 5, gc);
    if (ed->zoom > 10) ui_text_center(ui, (SDL_FRect){x - 30, y + 11, 60, 10}, gc, "START");
    if (sel || hov) ui_circle(ui, x, y, 14.0f, sel ? UI_RGB(255, 200, 40) : UI_RGB(255, 255, 255));
    if (sel) {  /* rotation handle */
        float hx = x + fx * 34, hy = y + fy * 34;
        ui_disc(ui, hx, hy, 5.0f, UI_RGB(255, 200, 40));
        ui_circle(ui, hx, hy, 6.0f, UI_RGB(10, 11, 14));
    }
}

static void draw_snap_hint(Editor *ed) {
    if (!ed->snap_hint_valid) return;
    UI *ui = &ed->ui;
    float x, y;
    world_to_screen(ed, ed->snap_hint.x, ed->snap_hint.z, &x, &y);
    switch (ed->snap_hint.kind) {
        case SNAP_VERTEX: ui_rect(ui, (SDL_FRect){x - 7, y - 7, 14, 14}, C_OK); ui_rect(ui, (SDL_FRect){x - 6, y - 6, 12, 12}, C_OK); break;
        case SNAP_EDGE:
            ui_line(ui, x, y - 8, x + 8, y, C_WARN); ui_line(ui, x + 8, y, x, y + 8, C_WARN);
            ui_line(ui, x, y + 8, x - 8, y, C_WARN); ui_line(ui, x - 8, y, x, y - 8, C_WARN);
            break;
        case SNAP_GRID:
            ui_line(ui, x - 5, y, x + 5, y, UI_RGBA(255, 255, 255, 210));
            ui_line(ui, x, y - 5, x, y + 5, UI_RGBA(255, 255, 255, 210));
            break;
        default:
            ui_circle(ui, x, y, 4.0f, UI_RGBA(255, 255, 255, 140));
            break;
    }
}

static void length_label(Editor *ed, float ax, float ay, float bx, float by, float len, UIColor col) {
    if (len < 0.005f) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", len);
    float mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f - 12.0f;
    float w = ui_text_w(buf) + 6;
    ui_fill(&ed->ui, (SDL_FRect){mx - w * 0.5f, my - 2, w, 12}, UI_RGBA(10, 11, 14, 200));
    ui_text(&ed->ui, mx - w * 0.5f + 3, my, col, buf);
}

static void draw_overlays(Editor *ed, SDL_FRect c) {
    UI *ui = &ed->ui;
    (void)c;
    if (ed->tool == TOOL_DRAW && ed->path_n > 0) {
        float px = 0, py = 0;
        for (int i = 0; i < ed->path_n; i++) {
            float x, y;
            world_to_screen(ed, ed->path[i].x, ed->path[i].z, &x, &y);
            if (i > 0) {
                ui_thick_line(ui, px, py, x, y, 2.5f, C_ACCENT);
                Vertex a = ed->path[i - 1], b = ed->path[i];
                length_label(ed, px, py, x, y, hypotf(b.x - a.x, b.z - a.z), C_TEXT);
            }
            px = x; py = y;
        }
        float hx, hy;
        world_to_screen(ed, ed->snap_hint.x, ed->snap_hint.z, &hx, &hy);
        if (ed->snap_hint_valid) {
            ui_thick_line(ui, px, py, hx, hy, 1.5f, UI_RGBA(86, 156, 246, 170));
            Vertex last = ed->path[ed->path_n - 1];
            length_label(ed, px, py, hx, hy, hypotf(ed->snap_hint.x - last.x, ed->snap_hint.z - last.z), C_ACCENT);
            if (ed->path_n >= 2) {
                float fx, fy;
                world_to_screen(ed, ed->path[0].x, ed->path[0].z, &fx, &fy);
                ui_line(ui, hx, hy, fx, fy, UI_RGBA(86, 156, 246, 70));
            }
        }
        for (int i = 0; i < ed->path_n; i++) {
            float x, y;
            world_to_screen(ed, ed->path[i].x, ed->path[i].z, &x, &y);
            ui_fill(ui, (SDL_FRect){x - 4, y - 4, 8, 8}, UI_RGB(10, 11, 14));
            ui_fill(ui, (SDL_FRect){x - 3, y - 3, 6, 6}, i == 0 ? C_OK : C_ACCENT);
        }
        if (ed->path_n >= 3) {   /* closing target */
            float fx, fy;
            world_to_screen(ed, ed->path[0].x, ed->path[0].z, &fx, &fy);
            bool near_first = ed->snap_hint_valid && hypotf(ui->mx - fx, ui->my - fy) < SNAP_VERTEX_PX + 2.0f;
            ui_circle(ui, fx, fy, near_first ? 11.0f : 8.0f, near_first ? C_OK : UI_RGBA(104, 200, 120, 140));
            if (near_first) ui_text(ui, fx + 14, fy - 4, C_OK, "Click to close");
        }
    }
    if (ed->drag == DRAG_RECT) {
        float x0, y0, x1, y1;
        world_to_screen(ed, ed->rect_x0, ed->rect_z0, &x0, &y0);
        world_to_screen(ed, ed->rect_x1, ed->rect_z1, &x1, &y1);
        SDL_FRect r = {fminf(x0, x1), fminf(y0, y1), fabsf(x1 - x0), fabsf(y1 - y0)};
        ui_fill(ui, r, UI_RGBA(86, 156, 246, 55));
        ui_rect(ui, r, C_ACCENT);
        ui_rect(ui, (SDL_FRect){r.x - 1, r.y - 1, r.w + 2, r.h + 2}, C_ACCENT);
        float w = fabsf(ed->rect_x1 - ed->rect_x0), h = fabsf(ed->rect_z1 - ed->rect_z0);
        length_label(ed, r.x, r.y - 6, r.x + r.w, r.y - 6, w, C_TEXT);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", h);
        ui_text(ui, r.x + r.w + 6, r.y + r.h * 0.5f - 4, C_TEXT, buf);
    }
    if (ed->drag == DRAG_BOX && hypotf(ui->mx - ed->press_sx, ui->my - ed->press_sy) > DRAG_START_PX) {
        SDL_FRect r = {fminf(ed->press_sx, ui->mx), fminf(ed->press_sy, ui->my), fabsf(ui->mx - ed->press_sx), fabsf(ui->my - ed->press_sy)};
        ui_fill(ui, r, UI_RGBA(86, 156, 246, 40));
        ui_rect(ui, r, C_ACCENT);
    }
    if (ed->drag == DRAG_MOVE) {
        char buf[96];
        snprintf(buf, sizeof(buf), ed->drag_invalid ? "Walls would cross - can't drop here" : "dx %+.2f   dz %+.2f", ed->drag_dx, ed->drag_dz);
        float w = ui_text_w(buf) + 12;
        ui_fill(ui, (SDL_FRect){ui->mx + 16, ui->my + 16, w, 18}, UI_RGBA(10, 11, 14, 230));
        ui_text(ui, ui->mx + 22, ui->my + 21, ed->drag_invalid ? C_ERR : C_TEXT, buf);
    }
    if (ed->drag == DRAG_AIM) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Facing %.0f deg", ed->level.player_spawn.yaw);
        ui_fill(ui, (SDL_FRect){ui->mx + 16, ui->my + 16, ui_text_w(buf) + 12, 18}, UI_RGBA(10, 11, 14, 230));
        ui_text(ui, ui->mx + 22, ui->my + 21, C_TEXT, buf);
    }
    draw_snap_hint(ed);
}

static void draw_labels(Editor *ed, SDL_FRect c) {
    if (!ed->show_labels || ed->zoom < 9.0f) return;
    UI *ui = &ed->ui;
    const Level *l = &ed->level;
    for (int s = 0; s < l->sector_count; s++) {
        float bx0, bz0, bx1, bz1, sx0, sy0, sx1, sy1;
        if (!model_sector_bounds(l, s, &bx0, &bz0, &bx1, &bz1)) continue;
        world_to_screen(ed, bx0, bz0, &sx0, &sy0);
        world_to_screen(ed, bx1, bz1, &sx1, &sy1);
        if (sx1 < c.x || sx0 > c.x + c.w || sy1 < c.y || sy0 > c.y + c.h) continue;
        if (sx1 - sx0 < 26 || sy1 - sy0 < 14) continue;
        float lx, lz;
        if (!model_sector_label_point(l, s, &lx, &lz)) continue;
        float x, y;
        world_to_screen(ed, lx, lz, &x, &y);
        if (x < c.x + 10 || x > c.x + c.w - 10 || y < c.y + 8 || y > c.y + c.h - 8) continue;
        bool sel = sel_has(ed, SEL_SECTOR, s);
        UIColor col = sel ? UI_RGB(255, 255, 255) : UI_RGBA(214, 222, 238, 200);
        char a[24], b[40];
        snprintf(a, sizeof(a), "#%d", s);
        ui_text_center(ui, (SDL_FRect){x - 40, y - 9, 80, 10}, col, a);
        if (ed->zoom >= 16.0f && sy1 - sy0 > 34) {
            snprintf(b, sizeof(b), "%g to %g", l->sectors[s].floor_height, l->sectors[s].ceiling_height);
            ui_text_center(ui, (SDL_FRect){x - 56, y + 2, 112, 10}, sel ? col : UI_RGBA(170, 180, 200, 190), b);
        }
    }
}

void canvas_draw(Editor *ed) {
    UI *ui = &ed->ui;
    SDL_FRect c = canvas_rect(ed);
    ensure_cache(ed);
    ui_fill(ui, c, UI_RGB(22, 24, 29));
    ui_clip(ui, c);
    draw_grid(ed, c);
    draw_sectors(ed, c);
    draw_walls(ed, c);
    draw_vertices(ed, c);
    draw_things(ed, c);
    draw_labels(ed, c);
    draw_overlays(ed, c);
    if (ed->level.wall_count == 0) {
        float w = 430, h = 92;
        SDL_FRect r = {c.x + (c.w - w) * 0.5f, c.y + (c.h - h) * 0.5f - 40, w, h};
        ui_fill(ui, r, UI_RGBA(10, 11, 14, 210));
        ui_rect(ui, r, C_HOVER);
        ui_text_center(ui, (SDL_FRect){r.x, r.y + 10, r.w, 12}, C_TEXT, "Empty level");
        ui_text_center(ui, (SDL_FRect){r.x, r.y + 34, r.w, 12}, C_DIM, "Press R and drag to sketch a room,");
        ui_text_center(ui, (SDL_FRect){r.x, r.y + 50, r.w, 12}, C_DIM, "or D and click corner by corner (Enter closes it).");
        ui_text_center(ui, (SDL_FRect){r.x, r.y + 70, r.w, 12}, C_ACCENT, "F1 shows every shortcut");
    }
    ui_unclip(ui);
    ui_rect(ui, c, C_BORDER);
}
