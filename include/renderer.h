#ifndef RENDERER_H
#define RENDERER_H

#include "player.h"
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

#endif