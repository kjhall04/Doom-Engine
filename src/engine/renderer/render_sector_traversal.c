#include "render_sector_traversal.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_WALL_SORT_MAX 128

typedef struct SectorWallKey {
    int wall;
    float distance;
} SectorWallKey;

/* Squared distance from the player to the finite wall segment. */
static float wall_distance_squared(
    const Player *player, const Level *level, const Wall *wall
) {
    if (wall->vertex_start < 0 || wall->vertex_start >= level->vertex_count ||
        wall->vertex_end < 0 || wall->vertex_end >= level->vertex_count) {
        return INFINITY;
    }

    const Vertex *a = &level->vertices[wall->vertex_start];
    const Vertex *b = &level->vertices[wall->vertex_end];

    float dx = b->x - a->x;
    float dz = b->z - a->z;
    float length_squared = dx * dx + dz * dz;
    float t = 0.0f;

    if (length_squared > 0.0f) {
        t = ((player->x - a->x) * dx + (player->z - a->z) * dz) /
            length_squared;
        t = fminf(1.0f, fmaxf(0.0f, t));
    }

    float ex = player->x - (a->x + t * dx);
    float ez = player->z - (a->z + t * dz);

    return ex * ex + ez * ez;
}

/* Saved clip columns are kept on a reusable stack so a portal visit does
 * not hit malloc/free. If the stack is exhausted we fall back to malloc. */
#define SCRATCH_DEPTH 32

static int *scratch_stack = NULL;
static size_t scratch_capacity = 0;
static size_t scratch_used = 0;

static bool scratch_reserve(size_t ints) {
    if (scratch_capacity >= ints) {
        return true;
    }

    int *grown = realloc(scratch_stack, ints * sizeof(*grown));

    if (grown == NULL) {
        return false;
    }

    scratch_stack = grown;
    scratch_capacity = ints;
    return true;
}

static void traverse_sector(
    const Player *player,
    const Level *level,
    int sector_index,
    bool *active_path,
    int depth,
    RenderPortalClip *clip,
    RenderWallDrawCallback draw_wall,
    void *userdata,
    RenderSectorVisit *visits
) {
    if (sector_index < 0 ||
        sector_index >= level->sector_count ||
        active_path[sector_index] ||
        depth >= level->sector_count ||
        clip->left > clip->right) {
        return;
    }

    active_path[sector_index] = true;

    if (visits != NULL) {
        RenderSectorVisit *visit = &visits[sector_index];
        if (clip->left < visit->left) visit->left = clip->left;
        if (clip->right > visit->right) visit->right = clip->right;
    }

    /* Draw this sector's walls nearest-first. The depth buffer keeps the
     * result correct either way, but drawing near walls first means far
     * walls are rejected per pixel instead of overwriting finished pixels. */
    SectorWallKey keys[SECTOR_WALL_SORT_MAX];
    int key_count = 0;
    bool sortable = true;

    for (int i = 0; i < level->wall_count; i++) {
        const Wall *wall = &level->walls[i];

        if (wall->front_sector != sector_index &&
            wall->back_sector != sector_index) {
            continue;
        }

        if (key_count == SECTOR_WALL_SORT_MAX) {
            sortable = false;
            break;
        }

        keys[key_count].wall = i;
        keys[key_count].distance = wall_distance_squared(player, level, wall);
        key_count++;
    }

    if (sortable) {
        /* Insertion sort: a sector rarely has more than a few dozen walls. */
        for (int i = 1; i < key_count; i++) {
            SectorWallKey key = keys[i];
            int j = i - 1;

            while (j >= 0 && keys[j].distance > key.distance) {
                keys[j + 1] = keys[j];
                j--;
            }

            keys[j + 1] = key;
        }

        for (int i = 0; i < key_count; i++) {
            draw_wall(player, &level->walls[keys[i].wall], level, clip,
                      userdata);
        }
    } else {
        for (int i = 0; i < level->wall_count; i++) {
            const Wall *wall = &level->walls[i];

            if (wall->front_sector != sector_index &&
                wall->back_sector != sector_index) {
                continue;
            }

            draw_wall(player, wall, level, clip, userdata);
        }
    }

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

        int previous_left = clip->left;
        int previous_right = clip->right;
        int previous_y_min = clip->y_min;
        int previous_y_max = clip->y_max;

        int saved_left = clip->left;
        int saved_right = clip->right;

        size_t column_count =
            (size_t)(saved_right - saved_left + 1);

        if (column_count > SIZE_MAX / sizeof(int) / 2) {
            continue;
        }

        const size_t needed = column_count * 2;
        const bool on_stack = scratch_used + needed <= scratch_capacity;

        int *saved_top = NULL;
        int *saved_bottom = NULL;

        if (on_stack) {
            saved_top = scratch_stack + scratch_used;
            saved_bottom = saved_top + column_count;
            scratch_used += needed;
        } else {
            saved_top = malloc(column_count * sizeof(*saved_top));
            saved_bottom = malloc(column_count * sizeof(*saved_bottom));

            if (saved_top == NULL || saved_bottom == NULL) {
                free(saved_top);
                free(saved_bottom);
                continue;
            }
        }

        memcpy(
            saved_top,
            clip->top + saved_left,
            column_count * sizeof(*saved_top)
        );

        memcpy(
            saved_bottom,
            clip->bottom + saved_left,
            column_count * sizeof(*saved_bottom)
        );

        bool portal_visible = render_portal_apply_clip(
            clip,
            player,
            start,
            end,
            opening_bottom,
            opening_top
        );

        if (portal_visible) {
            traverse_sector(
                player, level, neighbor, active_path,
                depth + 1, clip, draw_wall, userdata, visits
            );
        }

        /*
        * Restore the parent sector's clipping state.
        *
        * This is essential because the next portal
        * may have a completely different opening.
        */
        memcpy(
            clip->top + saved_left,
            saved_top,
            column_count * sizeof(*saved_top)
        );

        memcpy(
            clip->bottom + saved_left,
            saved_bottom,
            column_count * sizeof(*saved_bottom)
        );

        clip->left = previous_left;
        clip->right = previous_right;
        clip->y_min = previous_y_min;
        clip->y_max = previous_y_max;

        if (on_stack) {
            scratch_used -= needed;
        } else {
            free(saved_top);
            free(saved_bottom);
        }
    }

    active_path[sector_index] = false;
}

void render_sector_traverse(
    const Player *player, const Level *level, RenderPortalClip *clip,
    RenderWallDrawCallback draw_wall, void *userdata,
    RenderSectorVisit *visits
) {
    if (!player || !level || !clip || !draw_wall) return;

    if (visits != NULL) {
        for (int i = 0; i < level->sector_count; i++) {
            visits[i].left = INT_MAX;
            visits[i].right = -1;
        }
    }

    /* Active path flags are small; keep them on a persistent buffer too. */
    static bool *active_path = NULL;
    static int active_capacity = 0;

    if (level->sector_count > active_capacity) {
        bool *grown = realloc(active_path, (size_t)level->sector_count);
        if (grown == NULL) {
            return;
        }
        active_path = grown;
        active_capacity = level->sector_count;
    }

    if (level->sector_count > 0) {
        memset(active_path, 0, (size_t)level->sector_count);
    }

    scratch_used = 0;
    scratch_reserve((size_t)clip->width * 2 * SCRATCH_DEPTH);

    if (level->sector_count > 0 && player->sector >= 0 &&
        player->sector < level->sector_count) {
        traverse_sector(player, level, player->sector, active_path,
                        0, clip, draw_wall, userdata, visits);
    } else {
        /* Match the original renderer's fallback if sector tracking fails:
         * draw every wall and treat every sector as visible. */
        if (visits != NULL) {
            for (int i = 0; i < level->sector_count; i++) {
                visits[i].left = clip->left;
                visits[i].right = clip->right;
            }
        }

        for (int i = 0; i < level->wall_count; i++) {
            draw_wall(player, &level->walls[i], level, clip, userdata);
        }
    }
}
