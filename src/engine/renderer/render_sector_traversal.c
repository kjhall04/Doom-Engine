#include "render_sector_traversal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void traverse_sector(
    const Player *player,
    const Level *level,
    int sector_index,
    bool *active_path,
    int depth,
    RenderPortalClip *clip,
    RenderWallDrawCallback draw_wall,
    void *userdata
) {
    if (sector_index < 0 ||
        sector_index >= level->sector_count ||
        active_path[sector_index] ||
        depth >= level->sector_count ||
        clip->left > clip->right) {
        return;
    }

    active_path[sector_index] = true;

    for (int i = 0; i < level->wall_count; i++) {
        const Wall *wall = &level->walls[i];

        if (wall->front_sector != sector_index &&
            wall->back_sector != sector_index) {
            continue;
        }

        draw_wall(player, wall, level, clip, userdata);
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

        int saved_left = clip->left;
        int saved_right = clip->right;

        size_t column_count =
            (size_t)(saved_right - saved_left + 1);

        if (column_count > SIZE_MAX / sizeof(int)) {
            continue;
        }

        int *saved_top = malloc(
            column_count * sizeof(*saved_top)
        );

        int *saved_bottom = malloc(
            column_count * sizeof(*saved_bottom)
        );

        if (saved_top == NULL || saved_bottom == NULL) {
            free(saved_top);
            free(saved_bottom);
            continue;
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
                depth + 1, clip, draw_wall, userdata
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

        free(saved_top);
        free(saved_bottom);
    }

    active_path[sector_index] = false;
}

void render_sector_traverse(
    const Player *player, const Level *level, RenderPortalClip *clip,
    RenderWallDrawCallback draw_wall, void *userdata
) {
    if (!player || !level || !clip || !draw_wall) return;

    bool *active_path = NULL;
    if (level->sector_count > 0) {
        active_path = calloc((size_t)level->sector_count, sizeof(*active_path));
    }

    if (active_path && player->sector >= 0 &&
        player->sector < level->sector_count) {
        traverse_sector(player, level, player->sector, active_path,
                        0, clip, draw_wall, userdata);
    } else {
        /* Match the original renderer's fallback if sector tracking fails. */
        for (int i = 0; i < level->wall_count; i++) {
            draw_wall(player, &level->walls[i], level, clip, userdata);
        }
    }
    free(active_path);
}
