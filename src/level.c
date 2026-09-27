#include "level.h"

#include <stdlib.h>

void level_init(Level *level) {

    level->vertex_count = 4;

    level->vertices = malloc(
        sizeof(Vertex) * level->vertex_count
    );

    level->vertices[0] = (Vertex){
        -5.0f,
        -5.0f
    };

    level->vertices[1] = (Vertex){
         5.0f,
        -5.0f
    };

    level->vertices[2] = (Vertex){
         5.0f,
         5.0f
    };

    level->vertices[3] = (Vertex){
        -5.0f,
         5.0f
    };

    level->wall_count = 4;

    level->walls = malloc(
        sizeof(Wall) * level->wall_count
    );


    level->walls[0] = (Wall){
        0,
        1,
        0
    };

    level->walls[1] = (Wall){
        1,
        2,
        0
    };

    level->walls[2] = (Wall){
        2,
        3,
        0
    };

    level->walls[3] = (Wall){
        3,
        0,
        0
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