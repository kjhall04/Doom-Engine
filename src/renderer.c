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


/*
 * Convert a world position into camera space.
 *
 * Camera space:
 *
 * x = left/right
 * y = up/down
 * z = forward/backward
 */
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


    /*
     * Apply yaw.
     */
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


    /*
     * Apply pitch.
     */
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


/*
 * Clip a line segment against one plane.
 *
 * The plane equation is:
 *
 *     a*x + b*y + c*z >= 0
 *
 * If both points are outside the plane,
 * the entire line is invisible.
 *
 * If one point is outside, that point is
 * moved to the intersection with the plane.
 */
static bool renderer_clip_line_against_plane(
    CameraPoint *start,
    CameraPoint *end,
    float a,
    float b,
    float c
) {
    float start_distance =
        a * start->x +
        b * start->y +
        c * start->z;

    float end_distance =
        a * end->x +
        b * end->y +
        c * end->z;


    /*
     * Both points are outside.
     */
    if (start_distance < 0.0f &&
        end_distance < 0.0f) {

        return false;
    }


    /*
     * One point is outside.
     */
    if (start_distance < 0.0f ||
        end_distance < 0.0f) {

        float t =
            start_distance /
            (start_distance - end_distance);

        CameraPoint intersection;

        intersection.x =
            start->x +
            (end->x - start->x) * t;

        intersection.y =
            start->y +
            (end->y - start->y) * t;

        intersection.z =
            start->z +
            (end->z - start->z) * t;


        if (start_distance < 0.0f) {
            *start = intersection;
        } else {
            *end = intersection;
        }
    }

    return true;
}


/*
 * Clip a line against the camera frustum.
 */
static bool renderer_clip_line(
    CameraPoint *start,
    CameraPoint *end,
    float screen_width,
    float screen_height
) {
    float half_fov =
        (FOV * 0.5f) *
        (PI / 180.0f);

    float horizontal_tangent =
        tanf(half_fov);


    /*
     * Calculate the vertical field of view
     * from the horizontal field of view and
     * the screen aspect ratio.
     */
    float focal_length =
        (screen_width * 0.5f) /
        horizontal_tangent;

    float vertical_tangent =
        (screen_height * 0.5f) /
        focal_length;


    /*
     * Near plane
     *
     * z >= NEAR_PLANE
     */
    if (!renderer_clip_line_against_plane(
        start,
        end,
        0.0f,
        0.0f,
        1.0f
    )) {
        return false;
    }


    /*
     * Left side of the view.
     *
     * x + z * tan(FOV / 2) >= 0
     */
    if (!renderer_clip_line_against_plane(
        start,
        end,
        1.0f,
        0.0f,
        horizontal_tangent
    )) {
        return false;
    }


    /*
     * Right side of the view.
     *
     * -x + z * tan(FOV / 2) >= 0
     */
    if (!renderer_clip_line_against_plane(
        start,
        end,
        -1.0f,
        0.0f,
        horizontal_tangent
    )) {
        return false;
    }


    /*
     * Top of the view.
     *
     * -y + z * vertical_tangent >= 0
     */
    if (!renderer_clip_line_against_plane(
        start,
        end,
        0.0f,
        -1.0f,
        vertical_tangent
    )) {
        return false;
    }


    /*
     * Bottom of the view.
     *
     * y + z * vertical_tangent >= 0
     */
    if (!renderer_clip_line_against_plane(
        start,
        end,
        0.0f,
        1.0f,
        vertical_tangent
    )) {
        return false;
    }

    return true;
}


/*
 * Convert a camera space point into screen coordinates.
 */
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


/*
 * Project a world position directly to the screen.
 *
 * This function is kept as a general utility
 * even though wall rendering now uses the
 * clipped camera points directly.
 */
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


/*
 * Draw a wall.
 *
 * A wall has four edges:
 *
 *     top
 *     bottom
 *     left
 *     right
 *
 * Each edge is clipped independently.
 */
int renderer_draw_wall(
    Player *player,
    Wall *wall,
    float screen_width,
    float screen_height,
    ScreenLine lines[MAX_WALL_LINES]
) {
    CameraPoint points[4];


    /*
     * Bottom left
     */
    points[0] =
        renderer_world_to_camera(
            player,
            wall->x1,
            wall->bottom_height,
            wall->z1
        );


    /*
     * Bottom right
     */
    points[1] =
        renderer_world_to_camera(
            player,
            wall->x2,
            wall->bottom_height,
            wall->z2
        );


    /*
     * Top right
     */
    points[2] =
        renderer_world_to_camera(
            player,
            wall->x2,
            wall->top_height,
            wall->z2
        );


    /*
     * Top left
     */
    points[3] =
        renderer_world_to_camera(
            player,
            wall->x1,
            wall->top_height,
            wall->z1
        );


    int line_count = 0;


    /*
     * Process all four wall edges.
     */
    for (int i = 0; i < 4; i++) {

        int next =
            (i + 1) % 4;


        CameraPoint start =
            points[i];

        CameraPoint end =
            points[next];


        /*
         * Clip this edge against the
         * camera frustum.
         */
        if (!renderer_clip_line(
            &start,
            &end,
            screen_width,
            screen_height
        )) {
            continue;
        }


        /*
         * Project the clipped points.
         */
        ScreenPoint screen_start;
        ScreenPoint screen_end;

        if (!renderer_project_camera_point(
            &start,
            screen_width,
            screen_height,
            &screen_start
        )) {
            continue;
        }

        if (!renderer_project_camera_point(
            &end,
            screen_width,
            screen_height,
            &screen_end
        )) {
            continue;
        }


        /*
         * Store the line.
         */
        lines[line_count].start =
            screen_start;

        lines[line_count].end =
            screen_end;

        line_count++;


        /*
         * A wall can have at most four
         * visible edges.
         */
        if (line_count >= MAX_WALL_LINES) {
            break;
        }
    }


    return line_count;
}