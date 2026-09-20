#include "renderer.h"

#include <math.h>

#define FOV 90.0f

bool renderer_project_point(
    Player *player,
    float world_x,
    float world_y,
    float world_z,
    float screen_width,
    float screen_height,
    float *screen_x,
    float *screen_y
) {
    // Move world position relative to player
    float x = world_x - player->x;
    float y = world_y - (player->y + player->eye_height);
    float z = world_z - player->z;

    // Rotate around yaw
    float sin_yaw = sinf(player->yaw);
    float cos_yaw = cosf(player->yaw);

    float camera_x =
        x * cos_yaw +
        z * sin_yaw;

    float camera_z =
        x * sin_yaw -
        z * cos_yaw;

    // Rotate around pitch
    float sin_pitch = sinf(player->pitch);
    float cos_pitch = cosf(player->pitch);

    float camera_y =
        y * cos_pitch -
        camera_z * sin_pitch;

    float pitched_z =
        y * sin_pitch +
        camera_z * cos_pitch;

    camera_z = pitched_z;

    // Dont render behind the camera
    if (camera_z <= 0.1f) {
        return false;
    }

    // Perspective Projection
    float focal_length =
        (screen_width * 0.5f) /
        tanf((FOV * 0.5f) * (3.14159265f / 180.0f));

    *screen_x =
        (camera_x * focal_length / camera_z) +
        (screen_width * 0.5f);

    *screen_y =
        (-camera_y * focal_length / camera_z) +
        (screen_height * 0.5f);

    
    return true;
}