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


/*
 * Calculate the vertical field of view
 * from the horizontal field of view.
 */
static float renderer_vertical_tangent(
    float screen_width,
    float screen_height
) {
    float half_fov =
        (FOV * 0.5f) *
        (PI / 180.0f);

    float horizontal_tangent =
        tanf(half_fov);


    float focal_length =
        (screen_width * 0.5f) /
        horizontal_tangent;


    return
        (screen_height * 0.5f) /
        focal_length;
}


/*
 * Determine whether a camera point is inside
 * one clipping plane.
 *
 * The plane equation is:
 *
 *     a*x + b*y + c*z >= 0
 */
static float renderer_plane_distance(
    CameraPoint point,
    float a,
    float b,
    float c
) {
    return
        a * point.x +
        b * point.y +
        c * point.z;
}


/*
 * Find the intersection between two points
 * and a clipping plane.
 */
static CameraPoint renderer_clip_intersection(
    CameraPoint a,
    CameraPoint b,
    float plane_a,
    float plane_b,
    float plane_c
) {
    float distance_a =
        renderer_plane_distance(
            a,
            plane_a,
            plane_b,
            plane_c
        );

    float distance_b =
        renderer_plane_distance(
            b,
            plane_a,
            plane_b,
            plane_c
        );


    float t =
        distance_a /
        (distance_a - distance_b);


    CameraPoint result;

    result.x =
        a.x +
        (b.x - a.x) * t;

    result.y =
        a.y +
        (b.y - a.y) * t;

    result.z =
        a.z +
        (b.z - a.z) * t;


    return result;
}


/*
 * Clip a polygon against one plane.
 */
static int renderer_clip_polygon_against_plane(
    CameraPoint *input,
    int input_count,
    CameraPoint *output,
    float plane_a,
    float plane_b,
    float plane_c
) {
    if (input_count <= 0) {
        return 0;
    }


    int output_count = 0;


    for (int i = 0; i < input_count; i++) {

        CameraPoint current =
            input[i];

        CameraPoint next =
            input[(i + 1) % input_count];


        float current_distance =
            renderer_plane_distance(
                current,
                plane_a,
                plane_b,
                plane_c
            );

        float next_distance =
            renderer_plane_distance(
                next,
                plane_a,
                plane_b,
                plane_c
            );


        bool current_inside =
            current_distance >= 0.0f;

        bool next_inside =
            next_distance >= 0.0f;


        /*
         * Current inside, next inside.
         *
         * Keep next.
         */
        if (current_inside &&
            next_inside) {

            output[output_count] =
                next;

            output_count++;
        }


        /*
         * Current inside, next outside.
         *
         * Add intersection.
         */
        else if (current_inside &&
                 !next_inside) {

            output[output_count] =
                renderer_clip_intersection(
                    current,
                    next,
                    plane_a,
                    plane_b,
                    plane_c
                );

            output_count++;
        }


        /*
         * Current outside, next inside.
         *
         * Add intersection and next.
         */
        else if (!current_inside &&
                 next_inside) {

            output[output_count] =
                renderer_clip_intersection(
                    current,
                    next,
                    plane_a,
                    plane_b,
                    plane_c
                );

            output_count++;


            output[output_count] =
                next;

            output_count++;
        }
    }


    return output_count;
}


/*
 * Clip a wall against the complete camera frustum.
 */
static int renderer_clip_wall(
    CameraPoint *input,
    int input_count,
    CameraPoint *output,
    float screen_width,
    float screen_height
) {
    CameraPoint buffer_a[MAX_SCREEN_WALL_POINTS];
    CameraPoint buffer_b[MAX_SCREEN_WALL_POINTS];


    for (int i = 0; i < input_count; i++) {
        buffer_a[i] = input[i];
    }


    int count =
        input_count;


    float half_fov =
        (FOV * 0.5f) *
        (PI / 180.0f);

    float horizontal_tangent =
        tanf(half_fov);


    float vertical_tangent =
        renderer_vertical_tangent(
            screen_width,
            screen_height
        );


    /*
     * Near plane
     *
     * z >= NEAR_PLANE
     */
    count =
        renderer_clip_polygon_against_plane(
            buffer_a,
            count,
            buffer_b,
            0.0f,
            0.0f,
            1.0f
        );

    if (count < 3) {
        return 0;
    }


    /*
     * Left side
     *
     * x + z * tan(FOV / 2) >= 0
     */
    count =
        renderer_clip_polygon_against_plane(
            buffer_b,
            count,
            buffer_a,
            1.0f,
            0.0f,
            horizontal_tangent
        );

    if (count < 3) {
        return 0;
    }


    /*
     * Right side
     *
     * -x + z * tan(FOV / 2) >= 0
     */
    count =
        renderer_clip_polygon_against_plane(
            buffer_a,
            count,
            buffer_b,
            -1.0f,
            0.0f,
            horizontal_tangent
        );

    if (count < 3) {
        return 0;
    }


    /*
     * Top
     *
     * -y + z * vertical_tangent >= 0
     */
    count =
        renderer_clip_polygon_against_plane(
            buffer_b,
            count,
            buffer_a,
            0.0f,
            -1.0f,
            vertical_tangent
        );

    if (count < 3) {
        return 0;
    }


    /*
     * Bottom
     *
     * y + z * vertical_tangent >= 0
     */
    count =
        renderer_clip_polygon_against_plane(
            buffer_a,
            count,
            buffer_b,
            0.0f,
            1.0f,
            vertical_tangent
        );

    if (count < 3) {
        return 0;
    }


    for (int i = 0; i < count; i++) {
        output[i] = buffer_b[i];
    }


    return count;
}


/*
 * Project a camera point onto the screen.
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
 * Project a world point directly onto the screen.
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
 * Convert a wall into a clipped screen polygon.
 */
bool renderer_draw_wall(
    Player *player,
    Wall *wall,
    float screen_width,
    float screen_height,
    ScreenPolygon *screen_wall
) {
    CameraPoint wall_points[4];


    /*
     * Bottom left
     */
    wall_points[0] =
        renderer_world_to_camera(
            player,
            wall->x1,
            wall->bottom_height,
            wall->z1
        );


    /*
     * Bottom right
     */
    wall_points[1] =
        renderer_world_to_camera(
            player,
            wall->x2,
            wall->bottom_height,
            wall->z2
        );


    /*
     * Top right
     */
    wall_points[2] =
        renderer_world_to_camera(
            player,
            wall->x2,
            wall->top_height,
            wall->z2
        );


    /*
     * Top left
     */
    wall_points[3] =
        renderer_world_to_camera(
            player,
            wall->x1,
            wall->top_height,
            wall->z1
        );


    CameraPoint clipped_points[
        MAX_SCREEN_WALL_POINTS
    ];


    int point_count =
        renderer_clip_wall(
            wall_points,
            4,
            clipped_points,
            screen_width,
            screen_height
        );


    if (point_count < 3) {
        return false;
    }


    screen_wall->point_count =
        point_count;


    for (int i = 0; i < point_count; i++) {

        if (!renderer_project_camera_point(
            &clipped_points[i],
            screen_width,
            screen_height,
            &screen_wall->points[i]
        )) {
            return false;
        }
    }


    return true;
}