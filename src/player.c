#include "player.h"

#include <math.h>
#include <stdbool.h>

#define PLAYER_RADIUS 0.5f

#define PLAYER_HEIGHT 1.8f
#define PLAYER_EYE_HEIGHT 1.6f

#define PLAYER_MOVE_SPEED 9.0f
#define PLAYER_ACCELERATION 30.0f

#define PLAYER_GRAVITY 21.0f
#define PLAYER_JUMP_SPEED 9.0f

#define PLAYER_MOUSE_SENSITIVITY 0.0025f

#define PLAYER_MAX_PITCH 1.55f

#define PLAYER_MAX_STEP 1.0f
#define PLAYER_STEP_SPEED 6.0f

#define PLAYER_MOVE_SUBSTEP 0.1f

#define EPSILON 0.0001f


typedef struct PlayerMoveResult {

    bool blocked;

    float floor_height;
    float ceiling_height;

    int sector;

} PlayerMoveResult;


/*
 * Clamp a value between two limits.
 */

static float player_clamp(
    float value,
    float minimum,
    float maximum
) {

    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return value;
}


/*
 * Return the squared distance between a point
 * and the closest point on a wall segment.
 */

static float player_distance_to_wall_squared(
    float px,
    float pz,
    const Vertex *start,
    const Vertex *end
) {

    float wall_x =
        end->x - start->x;

    float wall_z =
        end->z - start->z;


    float length_squared =
        wall_x * wall_x +
        wall_z * wall_z;


    if (length_squared <= EPSILON) {

        float dx =
            px - start->x;

        float dz =
            pz - start->z;

        return dx * dx + dz * dz;
    }


    float point_x =
        px - start->x;

    float point_z =
        pz - start->z;


    float projection =
        (point_x * wall_x +
         point_z * wall_z) /
        length_squared;


    projection =
        player_clamp(
            projection,
            0.0f,
            1.0f
        );


    float closest_x =
        start->x +
        wall_x * projection;

    float closest_z =
        start->z +
        wall_z * projection;


    float dx =
        px - closest_x;

    float dz =
        pz - closest_z;


    return dx * dx + dz * dz;
}


/*
 * Determine which side of a wall a point occupies.
 */

static float player_wall_side(
    float x,
    float z,
    const Vertex *start,
    const Vertex *end
) {

    float wall_x =
        end->x - start->x;

    float wall_z =
        end->z - start->z;


    float point_x =
        x - start->x;

    float point_z =
        z - start->z;


    return
        wall_x * point_z -
        wall_z * point_x;
}


/*
 * Determine whether movement crossed a wall.

 * This function only handles geometry.

 * It does not decide whether the wall is passable.
 */

static bool player_crossed_wall(
    float old_x,
    float old_z,
    float new_x,
    float new_z,
    const Vertex *start,
    const Vertex *end
) {

    float old_side =
        player_wall_side(
            old_x,
            old_z,
            start,
            end
        );


    float new_side =
        player_wall_side(
            new_x,
            new_z,
            start,
            end
        );


    if (
        old_side > EPSILON &&
        new_side < -EPSILON
    ) {
        return true;
    }


    if (
        old_side < -EPSILON &&
        new_side > EPSILON
    ) {
        return true;
    }


    return false;
}


/*
 * Get the sector on the opposite side
 * of a two sided wall.
 */

static int player_get_destination_sector(
    const Player *player,
    const Wall *wall
) {

    if (
        wall->front_sector ==
        player->sector
    ) {

        return wall->back_sector;
    }


    if (
        wall->back_sector ==
        player->sector
    ) {

        return wall->front_sector;
    }


    return -1;
}


/*
 * Check whether a portal can be crossed.
 */

static bool player_can_cross_portal(
    Player *player,
    Level *level,
    const Wall *wall,
    int destination_sector
) {

    Sector *current_sector =
        &level->sectors[player->sector];


    Sector *destination =
        &level->sectors[destination_sector];


    /*
     * The usable opening begins at the
     * higher floor.
     */

    float opening_bottom =
        fmaxf(
            current_sector->floor_height,
            destination->floor_height
        );


    /*
     * The usable opening ends at the
     * lower ceiling.
     */

    float opening_top =
        fminf(
            current_sector->ceiling_height,
            destination->ceiling_height
        );


    float opening_height =
        opening_top -
        opening_bottom;


    /*
     * The player must physically fit.
     */

    if (
        opening_height <
        PLAYER_HEIGHT
    ) {

        return false;
    }


    /*
     * When walking, the floor difference
     * must be within our maximum step.
     */

    if (player->grounded) {

        float step_height =
            destination->floor_height -
            current_sector->floor_height;


        if (
            step_height >
            PLAYER_MAX_STEP
        ) {

            return false;
        }
    }


    /*
     * When airborne, make sure the player's
     * body can occupy the opening.
     */

    if (!player->grounded) {

        float player_bottom =
            player->y;

        float player_top =
            player->y +
            PLAYER_HEIGHT;


        if (
            player_bottom <
            opening_bottom
        ) {

            return false;
        }


        if (
            player_top >
            opening_top
        ) {

            return false;
        }
    }


    return true;
}


/*
 * Check whether a proposed position is valid.
 */

static void player_check_position(
    Player *player,
    Level *level,
    float new_x,
    float new_z,
    PlayerMoveResult *result
) {

    result->blocked = false;


    Sector *current_sector =
        &level->sectors[player->sector];


    result->floor_height =
        current_sector->floor_height;

    result->ceiling_height =
        current_sector->ceiling_height;

    result->sector =
        player->sector;


    /*
     * Examine every wall belonging to
     * the player's current sector.
     */

    for (
        int i = 0;
        i < level->wall_count;
        i++
    ) {

        Wall *wall =
            &level->walls[i];


        if (
            wall->front_sector !=
                player->sector &&
            wall->back_sector !=
                player->sector
        ) {

            continue;
        }


        Vertex *start =
            &level->vertices[
                wall->vertex_start
            ];


        Vertex *end =
            &level->vertices[
                wall->vertex_end
            ];


        /*
         * One sided walls are always solid.

         * Only check actual contact with the wall.
         */

        if (
            wall->back_sector < 0
        ) {

            float distance_squared =
                player_distance_to_wall_squared(
                    new_x,
                    new_z,
                    start,
                    end
                );


            if (
                distance_squared <=
                PLAYER_RADIUS *
                PLAYER_RADIUS
            ) {

                result->blocked = true;

                return;
            }


            continue;
        }


        /*
         * This is a portal.

         * A portal is only relevant when
         * the movement actually crosses it.
         */

        bool crossed =
            player_crossed_wall(
                player->x,
                player->z,
                new_x,
                new_z,
                start,
                end
            );


        if (!crossed) {

            continue;
        }


        /*
         * Determine the destination sector.
         */

        int destination_sector =
            player_get_destination_sector(
                player,
                wall
            );


        if (
            destination_sector < 0 ||
            destination_sector >=
                level->sector_count
        ) {

            result->blocked = true;

            return;
        }


        /*
         * Check the portal opening.
         */

        if (
            !player_can_cross_portal(
                player,
                level,
                wall,
                destination_sector
            )
        ) {

            result->blocked = true;

            return;
        }


        /*
         * The portal is valid.

         * Move the player into the
         * destination sector.
         */

        result->sector =
            destination_sector;


        result->floor_height =
            level->sectors[
                destination_sector
            ].floor_height;


        result->ceiling_height =
            level->sectors[
                destination_sector
            ].ceiling_height;
    }
}


/*
 * Attempt to move the player to a new
 * horizontal position.
 */

static bool player_try_move(
    Player *player,
    Level *level,
    float new_x,
    float new_z
) {

    PlayerMoveResult result;


    player_check_position(
        player,
        level,
        new_x,
        new_z,
        &result
    );


    if (result.blocked) {

        return false;
    }


    /*
     * Commit the position.
     */

    player->x =
        new_x;

    player->z =
        new_z;


    /*
        * Commit the sector.
        */

        int old_sector =
            player->sector;

        player->sector =
            result.sector;


        /*
        * If grounded, handle a floor height change.
        */

        if (player->grounded) {

            float old_floor =
                level->sectors[old_sector].floor_height;

            float new_floor =
                result.floor_height;

            if (fabsf(new_floor - old_floor) > EPSILON) {

                player->stepping = true;

                player->step_start_y =
                    old_floor;

                player->step_target_y =
                    new_floor;

                player->step_progress =
                    0.0f;
            }
            else {

                player->y =
                    new_floor;
            }
        }


    return true;
}


/*
 * Move the player using small steps.
 */

static void player_move_substeps(
    Player *player,
    Level *level,
    float movement_x,
    float movement_z
) {

    float distance =
        sqrtf(
            movement_x * movement_x +
            movement_z * movement_z
        );


    int steps =
        (int)ceilf(
            distance /
            PLAYER_MOVE_SUBSTEP
        );


    if (steps < 1) {
        steps = 1;
    }


    float step_x =
        movement_x /
        (float)steps;


    float step_z =
        movement_z /
        (float)steps;


    for (
        int i = 0;
        i < steps;
        i++
    ) {

        float target_x =
            player->x +
            step_x;


        float target_z =
            player->z +
            step_z;


        /*
         * First attempt the complete movement.
         */

        if (
            player_try_move(
                player,
                level,
                target_x,
                target_z
            )
        ) {

            continue;
        }


        /*
         * If blocked, try X independently.
         */

        player_try_move(
            player,
            level,
            player->x + step_x,
            player->z
        );


        /*
         * Then try Z independently.
         */

        player_try_move(
            player,
            level,
            player->x,
            player->z + step_z
        );
    }
}


/*
 * Initialize the player.
 */

void player_init(Player *player)
{
    player->x =
        -5.0f;

    player->y =
        0.0f;

    player->z =
        0.0f;


    player->eye_height =
        PLAYER_EYE_HEIGHT;

    player->height =
        PLAYER_HEIGHT;


    player->velocity_x =
        0.0f;

    player->velocity_y =
        0.0f;

    player->velocity_z =
        0.0f;


    player->yaw =
        0.0f;

    player->pitch =
        0.0f;


    player->move_speed =
        PLAYER_MOVE_SPEED;

    player->acceleration =
        PLAYER_ACCELERATION;


    player->sector =
        0;


    player->grounded =
        true;

    player->stepping = false;
    player->step_start_y = player->y;
    player->step_target_y = player->y;
    player->step_progress = 1.0f;
}


static void player_update_step(Player *player, float delta_time) {

    if (!player->stepping) {
        return;
    }

    player->step_progress += PLAYER_STEP_SPEED * delta_time;

    if (player->step_progress >= 1.0f) {

        player->step_progress = 1.0f;
        player->stepping = false;

        player->y =
            player->step_target_y;

        return;
    }

    float progress = player->step_progress;

    // Smoothstep interpolation
    progress = progress * progress * (3.0f - 2.0f * progress);

    player->y =
        player->step_start_y +
        (player->step_target_y - player->step_start_y) * progress;
}


/*
 * Update the player.
 */

void player_update(
    Player *player,
    Input *input,
    Level *level,
    float delta_time
) {

    /*
     * Limit the maximum frame time.
     */

    if (
        delta_time >
        0.1f
    ) {

        delta_time =
            0.1f;
    }


    /*
     * Mouse look.
     */

    player->yaw +=
        input->mouse_delta_x *
        PLAYER_MOUSE_SENSITIVITY;


    player->pitch -=
        input->mouse_delta_y *
        PLAYER_MOUSE_SENSITIVITY;


    player->pitch =
        player_clamp(
            player->pitch,
            -PLAYER_MAX_PITCH,
            PLAYER_MAX_PITCH
        );


    input->mouse_delta_x =
        0.0f;

    input->mouse_delta_y =
        0.0f;


    /*
     * Local movement direction.

     * Negative Z is forward.
     */

    float direction_x =
        0.0f;

    float direction_z =
        0.0f;


    if (input->move_forward) {

        direction_z -=
            1.0f;
    }


    if (input->move_backward) {

        direction_z +=
            1.0f;
    }


    if (input->move_left) {

        direction_x -=
            1.0f;
    }


    if (input->move_right) {

        direction_x +=
            1.0f;
    }


    /*
     * Normalize diagonal movement.
     */

    float movement_length =
        sqrtf(
            direction_x * direction_x +
            direction_z * direction_z
        );


    if (
        movement_length >
        0.0f
    ) {

        direction_x /=
            movement_length;

        direction_z /=
            movement_length;
    }


    /*
     * Rotate local movement by yaw.
     */

    float sin_yaw =
        sinf(player->yaw);

    float cos_yaw =
        cosf(player->yaw);


    float acceleration_x =
        direction_x * cos_yaw -
        direction_z * sin_yaw;


    float acceleration_z =
        direction_x * sin_yaw +
        direction_z * cos_yaw;


    /*
     * Apply acceleration.
     */

    float acceleration =
        player->acceleration *
        delta_time;


    float target_velocity_x =
        acceleration_x *
        player->move_speed;


    float target_velocity_z =
        acceleration_z *
        player->move_speed;


    float difference_x =
        target_velocity_x -
        player->velocity_x;


    float difference_z =
        target_velocity_z -
        player->velocity_z;


    difference_x =
        player_clamp(
            difference_x,
            -acceleration,
            acceleration
        );


    difference_z =
        player_clamp(
            difference_z,
            -acceleration,
            acceleration
        );


    player->velocity_x +=
        difference_x;


    player->velocity_z +=
        difference_z;


    /*
     * Stop completely when there is no
     * movement input.
     */

    if (
        direction_x == 0.0f &&
        direction_z == 0.0f
    ) {

        player->velocity_x =
            0.0f;

        player->velocity_z =
            0.0f;
    }


    /*
     * Jump.
     */

    if (
        input->jump &&
        player->grounded
    ) {

        player->velocity_y =
            PLAYER_JUMP_SPEED;

        player->grounded =
            false;

        input->jump =
            false;
    }


    /*
     * Gravity.
     */

    player->velocity_y -=
        PLAYER_GRAVITY *
        delta_time;


    /*
     * Horizontal movement.
     */

    float movement_x =
        player->velocity_x *
        delta_time;


    float movement_z =
        player->velocity_z *
        delta_time;


    player_move_substeps(
        player,
        level,
        movement_x,
        movement_z
    );

    /*
    * Smooth floor transitions.
    */

    if (player->grounded) {
        player_update_step(
            player,
            delta_time
        );
    }

    /*
     * Get the current sector.
     */

    Sector *sector =
        &level->sectors[
            player->sector
        ];


    float floor_height =
        sector->floor_height;


    float ceiling_height =
        sector->ceiling_height;


    /*
     * Vertical movement.
     */

    player->y +=
        player->velocity_y *
        delta_time;


    /*
    * Floor collision.
    */

    if (!player->stepping &&
        player->y <= floor_height) {

        player->y =
            floor_height;

        player->velocity_y =
            0.0f;

        player->grounded =
            true;
    }
    else if (!player->stepping) {

        player->grounded =
            false;
    }


    /*
     * Ceiling collision.
     */

    float player_top =
        player->y +
        player->height;


    if (
        player_top >
        ceiling_height
    ) {

        player->y =
            ceiling_height -
            player->height;

        player->velocity_y =
            0.0f;
    }
}