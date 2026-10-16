#include "render_visibility.h"

#include <stdlib.h>
#include <string.h>

bool render_visibility_collect(
    const Player *player,
    const Level *level,
    bool *visible_sectors
) {
    if (player == NULL ||
        level == NULL ||
        visible_sectors == NULL ||
        level->walls == NULL ||
        level->sector_count <= 0 ||
        level->wall_count < 0 ||
        player->sector < 0 ||
        player->sector >= level->sector_count) {
        return false;
    }

    size_t sector_count = (size_t)level->sector_count;

    memset(
        visible_sectors,
        0,
        sector_count * sizeof(*visible_sectors)
    );

    int *queue = malloc(
        sector_count * sizeof(*queue)
    );

    if (queue == NULL) {
        return false;
    }

    int head = 0;
    int tail = 0;

    queue[tail++] = player->sector;
    visible_sectors[player->sector] = true;

    while (head < tail) {
        int current_sector = queue[head++];

        for (int i = 0; i < level->wall_count; i++) {
            const Wall *wall = &level->walls[i];

            int neighbor = -1;

            if (wall->front_sector == current_sector) {
                neighbor = wall->back_sector;
            }
            else if (wall->back_sector == current_sector) {
                neighbor = wall->front_sector;
            }

            if (neighbor < 0 ||
                neighbor >= level->sector_count ||
                visible_sectors[neighbor]) {
                continue;
            }

            visible_sectors[neighbor] = true;

            queue[tail++] = neighbor;
        }
    }

    free(queue);

    return true;
}