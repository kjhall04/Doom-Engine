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

    int texture_id;
} Wall;

typedef struct Sector {
    float floor_height;
    float ceiling_height;

    int floor_texture_id;
    int ceiling_texture_id;
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
