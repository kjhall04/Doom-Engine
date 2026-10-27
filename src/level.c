#include "level.h"

#include <stdlib.h>

void level_init(Level *level) {
    if (!level) return;

    *level = (Level){0};
}

void level_free(Level *level) {
    if (!level) return;

    free(level->vertices);
    free(level->walls);
    free(level->sectors);

    *level = (Level){0};
}