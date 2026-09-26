#include "level.h"

#include <stdlib.h>

void level_init(Level *level) {

    level->vertex_count = 3;

    level->vertices = malloc(
        sizeof(Vertex) * level->vertex_count
    );

    level->vertices[0] = (Vertex){
        -2.0f,
        -10.0f
    };

    level->vertices[1] = (Vertex){
         2.0f,
        -10.0f
    };

    level->vertices[2] = (Vertex){
         6.0f,
        -10.0f
    };

    level->wall_count = 2;

    level->walls = malloc(
        sizeof(Wall) * level->wall_count
    );


    level->walls[0] = (Wall){
        0,
        1,
        0.0f,
        3.0f
    };

    level->walls[1] = (Wall){
        1,
        2,
        0.0f,
        3.0f
    };

    level->sector_count = 1;

    level->sectors = malloc(
        sizeof(Sector) * level->sector_count
    );

    level->sectors[0] = (Sector){
        0.0,
        3.0f
    };
}

void level_free(Level *level) {

    free(level->vertices);

    level->vertices = NULL;
    level->vertex_count = 0;

    free(level->walls);

    level->walls = NULL;
    level->wall_count = 0;

    free(level->sectors);

    level->sectors = NULL;
    level->sector_count = 0;
}