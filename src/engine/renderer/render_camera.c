#include "engine/renderer/render_camera.h"

#include <math.h>

#define RENDER_CAMERA_FOV 90.0f
#define RENDER_CAMERA_NEAR_PLANE 0.1f

#define RENDER_CAMERA_PI 3.14159265f

/* sinf/cosf of the view angles only change once per frame, but this is
 * called for every corner of every wall and plane triangle. Cache them. */
typedef struct CameraTrigCache {
    float yaw;
    float pitch;
    float sin_yaw;
    float cos_yaw;
    float sin_pitch;
    float cos_pitch;
    bool valid;
} CameraTrigCache;

static CameraTrigCache camera_trig = {0};

RenderCameraPoint render_camera_transform(
    const Player *player,
    float world_x,
    float world_y,
    float world_z
) {
    if (!camera_trig.valid ||
        camera_trig.yaw != player->yaw ||
        camera_trig.pitch != player->pitch) {
        camera_trig.yaw = player->yaw;
        camera_trig.pitch = player->pitch;
        camera_trig.sin_yaw = sinf(player->yaw);
        camera_trig.cos_yaw = cosf(player->yaw);
        camera_trig.sin_pitch = sinf(player->pitch);
        camera_trig.cos_pitch = cosf(player->pitch);
        camera_trig.valid = true;
    }

    float x = world_x - player->x;

    float y =
        world_y -
        (player->y + player->eye_height);

    float z = world_z - player->z;

    float camera_x =
        x * camera_trig.cos_yaw +
        z * camera_trig.sin_yaw;

    float camera_z =
        x * camera_trig.sin_yaw -
        z * camera_trig.cos_yaw;

    float pitched_y =
        y * camera_trig.cos_pitch -
        camera_z * camera_trig.sin_pitch;

    float pitched_z =
        y * camera_trig.sin_pitch +
        camera_z * camera_trig.cos_pitch;

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