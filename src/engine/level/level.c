#include "level.h"

#include <stdlib.h>

void level_init(Level *level) {
    if (!level) return;

    *level = (Level){0};

    level->player_spawn = (PlayerSpawn) {
        .x = -5.0f,
        .y = 0.0f,
        .z = 0.0f,
        .yaw = 0.0f,
        .pitch = 0.0f
    };
}

void level_free(Level *level) {
    if (!level) return;

    free(level->vertices);
    free(level->walls);
    free(level->sectors);

    *level = (Level){0};
}