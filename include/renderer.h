#ifndef RENDERER_H
#define RENDERER_H

#include "player.h"
#include "level.h"
#include <stdbool.h>

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
    float *top_x1,
    float *top_y1,
    float *top_x2,
    float *top_y2,
    float *bottom_x1,
    float *bottom_y1,
    float *bottom_x2,
    float *bottom_y2
);

#endif