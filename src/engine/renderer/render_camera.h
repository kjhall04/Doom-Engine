#ifndef RENDER_CAMERA_H
#define RENDER_CAMERA_H

#include "src/player.h"

#include <stdbool.h>

typedef struct RenderCameraPoint {
    float x;
    float y;
    float z;
} RenderCameraPoint;

typedef struct RenderScreenPoint {
    float x;
    float y;
} RenderScreenPoint;

RenderCameraPoint render_camera_transform(
    const Player *player,
    float world_x,
    float world_y,
    float world_z
);

bool render_camera_project(
    const RenderCameraPoint *point,
    float screen_width,
    float screen_height,
    RenderScreenPoint *screen_point
);

#endif