#ifndef RENDERER_H
#define RENDERER_H

#include "player.h"
#include "level.h"

#include <stdbool.h>

typedef struct ScreenPoint {
    float x;
    float y;
} ScreenPoint;

typedef struct ScreenWall {
    ScreenPoint top_left;
    ScreenPoint top_right;

    ScreenPoint bottom_left;
    ScreenPoint bottom_right;
} ScreenWall;

#define MAX_WALL_LINES 4

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
    Wall *wall,
    float screen_width,
    float screen_height,
    ScreenWall *screen_wall
);

#endif