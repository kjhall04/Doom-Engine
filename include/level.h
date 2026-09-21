#ifndef LEVEL_H
#define LEVEL_H

typedef struct Wall {
    float x1;
    float z1;

    float x2;
    float z2;

    float bottom_height;
    float top_height;
} Wall;

typedef struct Level {
    Wall *walls;
    int wall_count;
} Level;

void level_init(Level *level);
void level_free(Level *level);

#endif
