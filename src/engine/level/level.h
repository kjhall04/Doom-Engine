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

    int light_level; /* 0..255, default 255 */
    int light_r, light_g, light_b; /* 0..255, default white */
} Sector;

typedef struct PointLight {
    float x, y, z;
    float radius, intensity;
    int r, g, b;
    int effect; /* 0 steady, 1 pulse, 2 flicker */
} PointLight;

typedef struct PlayerSpawn {
    float x;
    float y;
    float z;

    float yaw;
    float pitch;
} PlayerSpawn;

typedef struct Level {
    Vertex *vertices;
    int vertex_count;

    Wall *walls;
    int wall_count;

    Sector *sectors;
    int sector_count;

    PointLight *lights;
    int light_count;
    float ambient_r, ambient_g, ambient_b;

    PlayerSpawn player_spawn;
} Level;

void level_init(Level *level);
void level_free(Level *level);

#endif
