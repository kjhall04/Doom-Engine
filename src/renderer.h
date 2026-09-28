#ifndef RENDERER_H
#define RENDERER_H

#include "player.h"
#include "level.h"

#include <stdbool.h>

#define MAX_SCREEN_WALL_POINTS 16
#define MAX_SCREEN_WALL_POLYGONS 3

typedef struct ScreenPoint {
    float x;
    float y;
} ScreenPoint;


typedef struct ScreenPolygon {
    ScreenPoint points[MAX_SCREEN_WALL_POINTS];
    int point_count;
} ScreenPolygon;


typedef struct ScreenWall {
    ScreenPolygon polygons[MAX_SCREEN_WALL_POLYGONS];
    int polygon_count;
} ScreenWall;


bool renderer_project_point(
    Player *player,
    float world_x,
    float world_y,
    float world_z,
    float screen_width,
    float screen_height,
    float *screen_x,
    float *screen_y
);

bool renderer_draw_wall(
    Player *player,
    Level *level,
    Wall *wall,
    float screen_width,
    float screen_height,
    ScreenWall *screen_wall
);

#endif