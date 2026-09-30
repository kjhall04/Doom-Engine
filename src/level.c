#include "level.h"

#include <stdlib.h>

void level_init(Level *level) {

    level->vertex_count = 6;

    level->vertices = malloc(
        sizeof(Vertex) * level->vertex_count
    );

    level->vertices[0] = (Vertex){
        -10.0f,
        -5.0f
    };

    level->vertices[1] = (Vertex){
         0.0f,
        -5.0f
    };

    level->vertices[2] = (Vertex){
        10.0f,
        -5.0f
    };

    level->vertices[3] = (Vertex){
        10.0f,
         5.0f
    };

    level->vertices[4] = (Vertex){
         0.0f,
         5.0f
    };

    level->vertices[5] = (Vertex){
        -10.0f,
         5.0f
    };

    level->wall_count = 7;

    level->walls = malloc(
        sizeof(Wall) * level->wall_count
    );


    level->walls[0] = (Wall){
        0,
        1,
        0,
        -1
    };

    level->walls[1] = (Wall){
        1,
        2,
        1,
        -1
    };

    level->walls[2] = (Wall){
        2,
        3,
        1,
        -1
    };

    level->walls[3] = (Wall){
        3,
        4,
        1,
        -1
    };

    level->walls[4] = (Wall){
        4,
        5,
        0,
        -1
    };

    level->walls[5] = (Wall){
        5,
        0,
        0,
        -1
    };

    level->walls[6] = (Wall){
        1,
        4,
        0,
        1
    };

    level->sector_count = 2;

    level->sectors = malloc(
        sizeof(Sector) * level->sector_count
    );

    level->sectors[0] = (Sector){
        0.0,
        100.0f
    };

    level->sectors[1] = (Sector){
        1.0f,
        100.0f
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