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
) {

    bool bottom1_visible = renderer_project_point(
        player,
        wall->x1,
        0.0f,
        wall->z1,
        screen_width,
        screen_height,
        bottom_x1,
        bottom_y1
    );

    bool bottom2_visible = renderer_project_point(
        player,
        wall->x2,
        0.0f,
        wall->z2,
        screen_width,
        screen_height,
        bottom_x2,
        bottom_y2
    );

    bool top1_visible = renderer_project_point(
        player,
        wall->x1,
        wall->height,
        wall->z1,
        screen_width,
        screen_height,
        top_x1,
        top_y1
    );

    bool top2_visible = renderer_project_point(
        player,
        wall->x2,
        wall->height,
        wall->z2,
        screen_width,
        screen_height,
        top_x2,
        top_y2
    );

    if (!bottom1_visible ||
        !bottom2_visible ||
        !top1_visible ||
        !top2_visible) {

        return false;
    }

    return true;
}