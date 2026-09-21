#include "level.h"

#include <stdlib.h>

void level_init(Level *level) {

    level->wall_count = 1;

    level->walls = malloc(
        sizeof(Wall) * level->wall_count
    );

    level->walls[0] = (Wall){
        -2.0f,
        -10.0f,

        2.0f,
        -10.0f,

        0.0f,
        3.0f
    };
}

void level_free(Level *level) {

    free(level->walls);

    level->walls = NULL;
    level->wall_count = 0;
}