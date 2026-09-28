#ifndef LEVEL_H
#define LEVEL_H

typedef struct Vertex {
    float x;
    float z;
} Vertex;

typedef struct Wall {
    int vertex_start;
    int vertex_end;

    int front_sector;
    int back_sector;
} Wall;

typedef struct Sector {
    float floor_height;
    float ceiling_height;
} Sector;

typedef struct Level {
    Vertex *vertices;
    int vertex_count;

    Wall *walls;
    int wall_count;

    Sector *sectors;
    int sector_count;
} Level;

void level_init(Level *level);
void level_free(Level *level);

#endif
