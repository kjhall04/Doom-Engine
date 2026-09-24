#include "renderer.h"

#include <math.h>

#define FOV 90.0f
#define NEAR_PLANE 0.1f

#define PI 3.14159265f

typedef struct CameraPoint {
    float x;
    float y;
    float z;
} CameraPoint;


static CameraPoint renderer_world_to_camera(
    Player *player,
    float world_x,
    float world_y,
    float world_z
) {
    float x =
        world_x - player->x;

    float y =
        world_y -
        (player->y + player->eye_height);

    float z =
        world_z - player->z;


    float sin_yaw =
        sinf(player->yaw);

    float cos_yaw =
        cosf(player->yaw);

    float camera_x =
        x * cos_yaw +
        z * sin_yaw;

    float camera_z =
        x * sin_yaw -
        z * cos_yaw;


    float sin_pitch =
        sinf(player->pitch);

    float cos_pitch =
        cosf(player->pitch);

    float camera_y =
        y * cos_pitch -
        camera_z * sin_pitch;

    float pitched_z =
        y * sin_pitch +
        camera_z * cos_pitch;


    CameraPoint point;

    point.x = camera_x;
    point.y = camera_y;
    point.z = pitched_z;

    return point;
}


static bool renderer_point_in_frustum(
    CameraPoint *point,
    float screen_width,
    float screen_height
) {
    if (point->z <= NEAR_PLANE) {
        return false;
    }


    float half_fov =
        (FOV * 0.5f) *
        (PI / 180.0f);

    float horizontal_tangent =
        tanf(half_fov);


    float focal_length =
        (screen_width * 0.5f) /
        horizontal_tangent;

    float vertical_tangent =
        (screen_height * 0.5f) /
        focal_length;


    /*
     * Left
     */
    if (
        point->x +
        point->z * horizontal_tangent
        < 0.0f
    ) {
        return false;
    }


    /*
     * Right
     */
    if (
        -point->x +
        point->z * horizontal_tangent
        < 0.0f
    ) {
        return false;
    }


    /*
     * Top
     */
    if (
        -point->y +
        point->z * vertical_tangent
        < 0.0f
    ) {
        return false;
    }


    /*
     * Bottom
     */
    if (
        point->y +
        point->z * vertical_tangent
        < 0.0f
    ) {
        return false;
    }


    return true;
}


static bool renderer_project_camera_point(
    CameraPoint *point,
    float screen_width,
    float screen_height,
    ScreenPoint *screen_point
) {
    if (point->z <= 0.0f) {
        return false;
    }


    float half_fov =
        (FOV * 0.5f) *
        (PI / 180.0f);

    float focal_length =
        (screen_width * 0.5f) /
        tanf(half_fov);


    screen_point->x =
        (point->x * focal_length /
         point->z) +
        (screen_width * 0.5f);

    screen_point->y =
        (-point->y * focal_length /
         point->z) +
        (screen_height * 0.5f);


    return true;
}


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
    CameraPoint point =
        renderer_world_to_camera(
            player,
            world_x,
            world_y,
            world_z
        );

    ScreenPoint screen_point;

    if (!renderer_project_camera_point(
        &point,
        screen_width,
        screen_height,
        &screen_point
    )) {
        return false;
    }

    *screen_x =
        screen_point.x;

    *screen_y =
        screen_point.y;

    return true;
}


bool renderer_draw_wall(
    Player *player,
    Wall *wall,
    float screen_width,
    float screen_height,
    ScreenWall *screen_wall
) {
    CameraPoint bottom_left =
        renderer_world_to_camera(
            player,
            wall->x1,
            wall->bottom_height,
            wall->z1
        );


    CameraPoint bottom_right =
        renderer_world_to_camera(
            player,
            wall->x2,
            wall->bottom_height,
            wall->z2
        );


    CameraPoint top_right =
        renderer_world_to_camera(
            player,
            wall->x2,
            wall->top_height,
            wall->z2
        );


    CameraPoint top_left =
        renderer_world_to_camera(
            player,
            wall->x1,
            wall->top_height,
            wall->z1
        );


    /*
     * For this first solid wall test,
     * require all four corners to be
     * inside the camera.
     */
    if (!renderer_point_in_frustum(
        &bottom_left,
        screen_width,
        screen_height
    )) {
        return false;
    }

    if (!renderer_point_in_frustum(
        &bottom_right,
        screen_width,
        screen_height
    )) {
        return false;
    }

    if (!renderer_point_in_frustum(
        &top_right,
        screen_width,
        screen_height
    )) {
        return false;
    }

    if (!renderer_point_in_frustum(
        &top_left,
        screen_width,
        screen_height
    )) {
        return false;
    }


    /*
     * Project all four corners.
     */
    if (!renderer_project_camera_point(
        &top_left,
        screen_width,
        screen_height,
        &screen_wall->top_left
    )) {
        return false;
    }

    if (!renderer_project_camera_point(
        &top_right,
        screen_width,
        screen_height,
        &screen_wall->top_right
    )) {
        return false;
    }

    if (!renderer_project_camera_point(
        &bottom_left,
        screen_width,
        screen_height,
        &screen_wall->bottom_left
    )) {
        return false;
    }

    if (!renderer_project_camera_point(
        &bottom_right,
        screen_width,
        screen_height,
        &screen_wall->bottom_right
    )) {
        return false;
    }


    return true;
}