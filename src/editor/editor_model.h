#ifndef EDITOR_MODEL_H
#define EDITOR_MODEL_H

/*
 * Pure level-geometry logic for the editor (no SDL, no UI).
 *
 * Conventions (match the engine and the original editor):
 *   - A wall runs vertex_start -> vertex_end. Its FRONT sector lies on the
 *     (-dz, dx) side of that direction; BACK is the other side (-1 = solid).
 *   - Sectors are never edited directly in terms of walls: draw closed loops,
 *     then model_rebuild_sectors() derives sectors and portals from geometry.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "engine/level/level.h"

#define MODEL_EPS 0.002f          /* world units: "same point" tolerance */
#define MODEL_TEXTURE_COUNT 5     /* brick, stone, metal, floor, ceiling */

typedef enum ProblemSeverity {
    PROBLEM_INFO = 0,
    PROBLEM_WARNING,
    PROBLEM_ERROR
} ProblemSeverity;

typedef enum ProblemKind {
    PROBLEM_NONE = 0,
    PROBLEM_BAD_REFERENCE,
    PROBLEM_OPEN_END,
    PROBLEM_CROSSING,
    PROBLEM_FLOOR_ABOVE_CEILING,
    PROBLEM_SPAWN_OUTSIDE,
    PROBLEM_LIGHT_OUTSIDE,
    PROBLEM_LIGHT_HEIGHT,
    PROBLEM_UNUSED_VERTICES,
    PROBLEM_ORPHAN_WALL,
    PROBLEM_TINY_WALL,
    PROBLEM_BAD_TEXTURE
} ProblemKind;

typedef struct Problem {
    ProblemSeverity severity;
    ProblemKind kind;
    int object;             /* vertex / wall / sector / light index, or -1 */
    float x, z;             /* where to jump to */
    char text[112];
} Problem;

/* ---- basics ---------------------------------------------------------- */
Sector   model_default_sector(void);
bool     model_copy(Level *dst, const Level *src);   /* deep copy into an EMPTY dst */
uint64_t model_fingerprint(const Level *l);
void     model_vertex_usage(const Level *l, unsigned char *used);

/* ---- editing primitives ---------------------------------------------- */
/* Returns an existing vertex at (x,z), splits a wall if the point lies on
   one, or appends a new vertex. Returns -1 on allocation failure. */
int  model_ensure_vertex(Level *l, float x, float z);
bool model_split_wall(Level *l, int wall, float x, float z, int *out_vertex);

/* Add a closed polygon (>=3 points) or an open polyline (>=2 points).
   Existing walls are split wherever the new line crosses or touches them,
   so the result is always a clean planar graph. */
bool model_add_path(Level *l, const Vertex *pts, int count, bool closed,
                    int wall_texture);

/* Edits record which sectors they disturbed here, so the rebuild below only
   re-derives the faces around the edit. Zero-initialise, pass to the edit
   calls, then hand to model_rebuild_sectors() (which frees it). */
typedef struct ModelDirty {
    unsigned char *sector;
    int count;
} ModelDirty;
void model_dirty_free(ModelDirty *d);

/* Re-derive sectors and portals for faces touched by recent edits (new walls
   and the sectors recorded in *dirty). Every other face keeps its sector
   assignment exactly, so hand-authored levels (thick walls, solid slabs,
   duplicate paired walls) are never rewritten behind your back.
   Surviving sectors keep their ids and properties; brand-new faces receive
   *fresh (defaults when NULL). Nested loops (pillars, platforms) become
   portal-linked inner sectors. `dirty` may be NULL. */
void model_rebuild_sectors(Level *l, const Sector *fresh, ModelDirty *dirty);

/* Deletion. Masks are sized to the matching count. Follow with
   model_rebuild_sectors(). */
void model_delete_walls(Level *l, const unsigned char *kill, ModelDirty *dirty);
void model_delete_vertices(Level *l, const unsigned char *kill, ModelDirty *dirty);
void model_delete_sectors(Level *l, const unsigned char *kill, ModelDirty *dirty);
void model_delete_lights(Level *l, const unsigned char *kill);
int  model_remove_unused_vertices(Level *l);   /* returns number removed */
int  model_remove_unused_sectors(Level *l);    /* sectors no wall refers to */

/* After vertices were moved: weld coincident ones, split walls that a moved
   vertex now touches, merge duplicate walls. Returns true if the topology
   changed (then call model_rebuild_sectors with the same dirty set). */
bool model_resolve_contacts(Level *l, const unsigned char *moved, ModelDirty *dirty);

/* True if any wall touching a vertex in `moved` (NULL = any wall) properly
   crosses another wall. */
bool model_has_crossings(const Level *l, const unsigned char *moved);

/* ---- queries --------------------------------------------------------- */
float model_sector_area(const Level *l, int s);           /* net area, holes subtracted */
bool  model_sector_bounds(const Level *l, int s, float *x0, float *z0, float *x1, float *z1);
bool  model_sector_contains(const Level *l, int s, float x, float z);
int   model_sector_at(const Level *l, float x, float z);  /* -1 if none */
bool  model_sector_label_point(const Level *l, int s, float *x, float *z);
float model_wall_length(const Level *l, int w);
bool  model_wall_valid(const Level *l, int w);

/* Collect issues, worst first. Returns the number written (<= max). */
int model_validate(const Level *l, Problem *out, int max);

#endif
