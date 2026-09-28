#include "player.h"
#include "level.h"

#include <math.h>
#include <stdbool.h>

#define PLAYER_RADIUS 0.5f

// Init player object with starting values
void player_init(Player *player) {

    player->x = -5.0f;
    player->y = 0.0f;
    player->z = 0.0f;

    player->velocity_x = 0.0f;
    player->velocity_y = 0.0f;
    player->velocity_z = 0.0f;

    player->eye_height = 1.6f;
    player->height = 1.8f;

    player->yaw = 0.0f;
    player->pitch = 0.0f;

    player->move_speed = 0.0f;
    player->acceleration = 20.0f;

    player->sector = 0;

    player->grounded = true;

    player->stepping = false;
    player->step_target_y = 0.0f;
}


// Check whether the player is colliding with a wall
static bool player_collides_with_wall(
    float player_x,
    float player_z,
    Level *level,
    Wall *wall
) {

    Vertex *start =
        &level->vertices[wall->vertex_start];

    Vertex *end =
        &level->vertices[wall->vertex_end];

    float wall_x =
        end->x - start->x;

    float wall_z =
        end->z - start->z;

    float length_squared =
        wall_x * wall_x +
        wall_z * wall_z;

    if (length_squared <= 0.0f) {
        return false;
    }

    float player_to_wall_x =
        player_x - start->x;

    float player_to_wall_z =
        player_z - start->z;

    float t =
        (
            player_to_wall_x * wall_x +
            player_to_wall_z * wall_z
        ) /
        length_squared;

    if (t < 0.0f) {
        t = 0.0f;
    }

    if (t > 1.0f) {
        t = 1.0f;
    }

    float closest_x =
        start->x +
        wall_x * t;

    float closest_z =
        start->z +
        wall_z * t;

    float distance_x =
        player_x - closest_x;

    float distance_z =
        player_z - closest_z;

    float distance_squared =
        distance_x * distance_x +
        distance_z * distance_z;

    return distance_squared <
        PLAYER_RADIUS * PLAYER_RADIUS;
}


// Determine which side of a wall a point is on
static float player_wall_side(
    float x,
    float z,
    Vertex *start,
    Vertex *end
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


// Check whether the player crossed a wall between
// the old position and the new position
static bool player_crossed_wall(
    float old_x,
    float old_z,
    float new_x,
    float new_z,
    Vertex *start,
    Vertex *end
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
        (old_side >= 0.0f && new_side >= 0.0f) ||
        (old_side <= 0.0f && new_side <= 0.0f)
    ) {
        return false;
    }

    float denominator =
        old_side - new_side;

    if (fabsf(denominator) <= 0.0001f) {
        return false;
    }

    float t =
        old_side / denominator;

    float crossing_x =
        old_x +
        (new_x - old_x) * t;

    float crossing_z =
        old_z +
        (new_z - old_z) * t;

    float wall_x =
        end->x - start->x;

    float wall_z =
        end->z - start->z;

    float wall_length_squared =
        wall_x * wall_x +
        wall_z * wall_z;

    if (wall_length_squared <= 0.0f) {
        return false;
    }

    float crossing_to_start_x =
        crossing_x - start->x;

    float crossing_to_start_z =
        crossing_z - start->z;

    float wall_position =
        (
            crossing_to_start_x * wall_x +
            crossing_to_start_z * wall_z
        ) /
        wall_length_squared;

    return
        wall_position >= 0.0f &&
        wall_position <= 1.0f;
}


// Determine which sector is on the other side
// of a portal wall
static int player_get_portal_destination(
    Player *player,
    Wall *wall
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


// Determine whether the player can pass through
// a portal into another sector
static bool player_can_pass_portal(
    Player *player,
    Level *level,
    Wall *wall,
    int destination_sector
) {

    if (wall->back_sector < 0) {
        return false;
    }

    if (
        destination_sector < 0 ||
        destination_sector >= level->sector_count
    ) {
        return false;
    }

    Sector *current_sector =
        &level->sectors[player->sector];

    Sector *destination =
        &level->sectors[destination_sector];

    float opening_bottom =
        fmaxf(
            current_sector->floor_height,
            destination->floor_height
        );

    float opening_top =
        fminf(
            current_sector->ceiling_height,
            destination->ceiling_height
        );

    // The opening must be tall enough for the player.

    if (
        opening_top -
        opening_bottom <
        player->height
    ) {
        return false;
    }

    float player_top =
        player->y +
        player->height;

    // The player's body must fit inside the opening.

    if (player->y < opening_bottom) {
        return false;
    }

    if (player_top > opening_top) {
        return false;
    }

    return true;
}


static bool player_can_walk_through_portal(
    Player *player,
    Level *level,
    int destination_sector
) {

    Sector *current_sector =
        &level->sectors[player->sector];

    Sector *destination =
        &level->sectors[destination_sector];

    const float max_step_height = 1.0f;

    float floor_difference =
        destination->floor_height -
        current_sector->floor_height;

    return floor_difference <=
        max_step_height;
}


// Check whether a wall should be treated as solid
static bool player_wall_is_solid(
    Player *player,
    Level *level,
    Wall *wall,
    Wall *ignored_portal
) {

    // This is the portal we have just successfully
    // crossed, so it cannot block this movement step.

    if (wall == ignored_portal) {
        return false;
    }

    // Wall does not belong to the player's
    // current sector
    if (
        wall->front_sector != player->sector &&
        wall->back_sector != player->sector
    ) {
        return false;
    }

    // A wall with no back sector is solid
    if (wall->back_sector < 0) {
        return true;
    }

    // This is a portal
    // Determine where the player would go
    int destination_sector =
        player_get_portal_destination(
            player,
            wall
        );

    // If the portal cannot be traversed,
    // treat it like a solid wall
    if (
        !player_can_pass_portal(
            player,
            level,
            wall,
            destination_sector
        )
    ) {
        return true;
    }

    // A grounded player must also respect
    // the maximum walking step.

    if (player->grounded) {

        if (
            !player_can_walk_through_portal(
                player,
                level,
                destination_sector
            )
        ) {
            return true;
        }
    }

    return false;
}


// Update player values as they move
void player_update(
    Player *player,
    Input *input,
    Level *level,
    float delta_time
) {

    const float gravity = 25.0f;
    const float jump_force = 10.0f;

    const float movement_speed = 60.0f;
    const float acceleration_rate = 6.0f;

    const float mouse_sensitivity = 0.0025f;
    const float max_pitch = 1.55f;

    const float collision_step = 0.1f;
    const float step_speed = 8.0f;


    // Get the current sector

    Sector *current_sector =
        &level->sectors[player->sector];

    float floor_height =
        current_sector->floor_height;


    // Grounding

    if (!player->stepping) {

        player->grounded =
            player->y <=
            floor_height + 0.001f;

        if (player->grounded) {

            player->y =
                floor_height;

            if (player->velocity_y < 0.0f) {
                player->velocity_y = 0.0f;
            }
        }
    }


    // Jumping

    if (
        player->grounded &&
        !player->stepping &&
        input->jump
    ) {

        player->velocity_y =
            jump_force;

        player->grounded =
            false;

        input->jump =
            false;
    }


    // Gravity

    if (!player->stepping) {

        player->velocity_y -=
            gravity *
            delta_time;
    }


    // Mouse look

    player->yaw +=
        input->mouse_delta_x *
        mouse_sensitivity;

    player->pitch -=
        input->mouse_delta_y *
        mouse_sensitivity;

    if (player->pitch > max_pitch) {
        player->pitch = max_pitch;
    }

    if (player->pitch < -max_pitch) {
        player->pitch = -max_pitch;
    }

    input->mouse_delta_x = 0.0f;
    input->mouse_delta_y = 0.0f;


    // Calculate movement direction

    float direction_x = 0.0f;
    float direction_z = 0.0f;

    if (input->move_forward) {
        direction_z -= 1.0f;
    }

    if (input->move_backward) {
        direction_z += 1.0f;
    }

    if (input->move_left) {
        direction_x -= 1.0f;
    }

    if (input->move_right) {
        direction_x += 1.0f;
    }


    // Normalize movement direction

    float length =
        sqrtf(
            direction_x * direction_x +
            direction_z * direction_z
        );

    if (length > 0.0f) {

        direction_x /=
            length;

        direction_z /=
            length;
    }


    // Rotate movement by player yaw

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


    // Apply drag

    float drag =
        expf(
            -delta_time *
            acceleration_rate
        );

    float diff =
        1.0f - drag;

    player->velocity_x -=
        player->velocity_x *
        diff;

    player->velocity_z -=
        player->velocity_z *
        diff;


    // Apply acceleration

    player->velocity_x +=
        diff *
        acceleration_x *
        movement_speed /
        acceleration_rate;

    player->velocity_z +=
        diff *
        acceleration_z *
        movement_speed /
        acceleration_rate;


    /*
     * Calculate total horizontal movement.
     *
     * We divide it into small steps so that a player
     * cannot move completely through a wall between
     * collision checks.
     */

    float movement_x =
        player->velocity_x *
        delta_time;

    float movement_z =
        player->velocity_z *
        delta_time;

    float movement_distance =
        sqrtf(
            movement_x * movement_x +
            movement_z * movement_z
        );

    int movement_steps =
        (int)ceilf(
            movement_distance /
            collision_step
        );

    if (movement_steps < 1) {
        movement_steps = 1;
    }

    float step_x =
        movement_x /
        (float)movement_steps;

    float step_z =
        movement_z /
        (float)movement_steps;


    /*
     * Horizontal movement
     *
     * Each small movement is checked independently.
     */

    for (
        int step = 0;
        step < movement_steps;
        step++
    ) {

        float old_x =
            player->x;

        float old_z =
            player->z;

        float new_x =
            player->x +
            step_x;

        float new_z =
            player->z +
            step_z;

        int previous_sector =
            player->sector;

        int new_sector =
            player->sector;

        Wall *crossed_portal =
            NULL;


        /*
         * Check for portal crossings.
         */

        for (
            int i = 0;
            i < level->wall_count;
            i++
        ) {

            Wall *wall =
                &level->walls[i];

            // Not a portal.

            if (wall->back_sector < 0) {
                continue;
            }

            // The wall must belong to the
            // player's current sector.

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


            // Did this small movement step
            // cross the portal?

            if (
                !player_crossed_wall(
                    old_x,
                    old_z,
                    new_x,
                    new_z,
                    start,
                    end
                )
            ) {
                continue;
            }


            int destination_sector =
                player_get_portal_destination(
                    player,
                    wall
                );


            // Does the player physically fit?

            if (
                !player_can_pass_portal(
                    player,
                    level,
                    wall,
                    destination_sector
                )
            ) {
                continue;
            }


            /*
             * A grounded player must also be able
             * to walk up the step.
             */

            if (player->grounded) {

                if (
                    !player_can_walk_through_portal(
                        player,
                        level,
                        destination_sector
                    )
                ) {
                    continue;
                }
            }


            // The portal can be crossed.

            new_sector =
                destination_sector;

            crossed_portal =
                wall;

            break;
        }


        /*
         * Check the complete movement.
         */

        bool blocked =
            false;

        for (
            int i = 0;
            i < level->wall_count;
            i++
        ) {

            Wall *wall =
                &level->walls[i];

            if (
                !player_wall_is_solid(
                    player,
                    level,
                    wall,
                    crossed_portal
                )
            ) {
                continue;
            }

            if (
                player_collides_with_wall(
                    new_x,
                    new_z,
                    level,
                    wall
                )
            ) {

                blocked =
                    true;

                break;
            }
        }


        /*
         * Complete movement succeeded.
         */

        if (!blocked) {

            player->x =
                new_x;

            player->z =
                new_z;

            // The horizontal movement succeeded.
            // Now commit the sector transition.

            if (
                new_sector !=
                previous_sector
            ) {

                player->sector =
                    new_sector;
            }

            /*
             * We successfully crossed into another
             * sector while standing on the floor.
             */

            if (
                player->grounded &&
                new_sector !=
                    previous_sector
            ) {

                float destination_floor =
                    level->sectors[
                        new_sector
                    ].floor_height;

                /*
                 * Start smooth step movement.
                 */

                if (
                    fabsf(
                        destination_floor -
                        player->y
                    ) > 0.001f
                ) {

                    player->stepping =
                        true;

                    player->step_target_y =
                        destination_floor;

                    player->velocity_y =
                        0.0f;
                }
            }

            continue;
        }


        /*
         * Complete movement was blocked.
         *
         * Restore the previous sector.
         */

        player->sector =
            previous_sector;


        /*
         * Try X movement independently.
         */

        bool x_blocked =
            false;

        float x_only =
            player->x +
            step_x;

        for (
            int i = 0;
            i < level->wall_count;
            i++
        ) {

            Wall *wall =
                &level->walls[i];

            if (
                !player_wall_is_solid(
                    player,
                    level,
                    wall,
                    crossed_portal
                )
            ) {
                continue;
            }

            if (
                player_collides_with_wall(
                    x_only,
                    player->z,
                    level,
                    wall
                )
            ) {

                x_blocked =
                    true;

                break;
            }
        }


        if (!x_blocked) {

            player->x =
                x_only;
        }


        /*
         * Try Z movement independently.
         */

        bool z_blocked =
            false;

        float z_only =
            player->z +
            step_z;

        for (
            int i = 0;
            i < level->wall_count;
            i++
        ) {

            Wall *wall =
                &level->walls[i];

            if (
                !player_wall_is_solid(
                    player,
                    level,
                    wall,
                    crossed_portal
                )
            ) {
                continue;
            }

            if (
                player_collides_with_wall(
                    player->x,
                    z_only,
                    level,
                    wall
                )
            ) {

                z_blocked =
                    true;

                break;
            }
        }


        if (!z_blocked) {

            player->z =
                z_only;
        }


        /*
         * If both directions were blocked,
         * stop the corresponding velocity.
         */

        if (x_blocked) {

            player->velocity_x =
                0.0f;
        }

        if (z_blocked) {

            player->velocity_z =
                0.0f;
        }


        /*
         * We cannot cross the portal during this
         * movement step, so remain in the original
         * sector.
         */

        player->sector =
            previous_sector;
    }


    /*
     * Vertical movement
     */

    if (player->stepping) {

        float difference =
            player->step_target_y -
            player->y;

        float step_amount =
            difference *
            step_speed *
            delta_time;


        // Prevent overshooting.

        if (
            fabsf(step_amount) >
            fabsf(difference)
        ) {

            step_amount =
                difference;
        }


        player->y +=
            step_amount;

        player->velocity_y =
            0.0f;


        // Step completed.

        if (
            fabsf(
                player->step_target_y -
                player->y
            ) < 0.001f
        ) {

            player->y =
                player->step_target_y;

            player->stepping =
                false;

            player->grounded =
                true;
        }
    }

    else {

        player->y +=
            player->velocity_y *
            delta_time;
    }


    /*
     * Apply floor collision.
     */

    current_sector =
        &level->sectors[
            player->sector
        ];

    floor_height =
        current_sector->floor_height;


    if (
        !player->stepping &&
        player->y <=
        floor_height
    ) {

        player->y =
            floor_height;

        player->velocity_y =
            0.0f;

        player->grounded =
            true;
    }
}