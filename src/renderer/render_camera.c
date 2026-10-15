#include "render_camera.h"

#include <math.h>

#define RENDER_CAMERA_FOV 90.0f
#define RENDER_CAMERA_NEAR_PLANE 0.1f

#define RENDER_CAMERA_PI 3.14159265f

RenderCameraPoint render_camera_transform(
    const Player *player,
    float world_x,
    float world_y,
    float world_z
) {
    float x = world_x - player->x;

    float y =
        world_y -
        (player->y + player->eye_height);

    float z = world_z - player->z;

    float sin_yaw = sinf(player->yaw);
    float cos_yaw = cosf(player->yaw);

    float camera_x =
        x * cos_yaw +
        z * sin_yaw;

    float camera_z =
        x * sin_yaw -
        z * cos_yaw;

    float sin_pitch = sinf(player->pitch);
    float cos_pitch = cosf(player->pitch);

    float pitched_y =
        y * cos_pitch -
        camera_z * sin_pitch;

    float pitched_z =
        y * sin_pitch +
        camera_z * cos_pitch;

    RenderCameraPoint point = {
        .x = camera_x,
        .y = pitched_y,
        .z = pitched_z
    };

    return point;
}

bool render_camera_project(
    const RenderCameraPoint *point,
    float screen_width,
    float screen_height,
    RenderScreenPoint *screen_point
) {
    if (point == NULL || screen_point == NULL) {
        return false;
    }

    if (screen_width <= 0.0f ||
        screen_height <= 0.0f) {
        return false;
    }

    if (point->z < RENDER_CAMERA_NEAR_PLANE) {
        return false;
    }

    float half_fov =
        (RENDER_CAMERA_FOV * 0.5f) *
        (RENDER_CAMERA_PI / 180.0f);

    float focal_length =
        (screen_width * 0.5f) /
        tanf(half_fov);

    screen_point->x =
        (point->x * focal_length / point->z) +
        (screen_width * 0.5f);

    screen_point->y =
        (-point->y * focal_length / point->z) +
        (screen_height * 0.5f);

    return true;
}