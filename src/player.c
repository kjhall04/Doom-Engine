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

    const float max_step_height = 1.0f;

    float floor_difference =
        destination->floor_height -
        current_sector->floor_height;

    // The destination floor is too high
    if (floor_difference > max_step_height) {
        return false;
    }

    // The opening is not tall enough for the player
    if (
        opening_top -
        destination->floor_height <
        player->height
    ) {
        return false;
    }

    // Check the player's current position
    // against the top of the opening
    float player_top =
        player->y +
        player->height;

    if (player_top > opening_top) {
        return false;
    }

    return true;
}


// Check whether a wall should be treated as solid
static bool player_wall_is_solid(
    Player *player,
    Wall *wall
) {

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

    // A two sided wall is a portal
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


    // Get the current sector
    Sector *current_sector =
        &level->sectors[player->sector];

    float floor_height =
        current_sector->floor_height;


    // Grounding

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


    // Jumping

    if (
        player->grounded &&
        input->jump
    ) {

        player->velocity_y =
            jump_force;

        player->grounded =
            false;

        input->jump =
            false;
    }

    player->velocity_y -=
        gravity * delta_time;


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


    // Save the old position

    float old_x =
        player->x;

    float old_z =
        player->z;


    // Calculate desired position

    float new_x =
        player->x +
        player->velocity_x *
        delta_time;

    float new_z =
        player->z +
        player->velocity_z *
        delta_time;


    // Check for portal crossings

    int new_sector =
        player->sector;

    for (
        int i = 0;
        i < level->wall_count;
        i++
    ) {

        Wall *wall =
            &level->walls[i];

        if (wall->back_sector < 0) {
            continue;
        }

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

        if (player_can_pass_portal(
                player,
                level,
                wall,
                destination_sector
            )
        ) {

            new_sector =
                destination_sector;

            float destination_floor =
                level->sectors[
                    destination_sector
                ].floor_height;

            // If we are walking across the portal,
            // place the player's feet on the new floor.
            if (player->grounded) {

                player->y =
                    destination_floor;

                player->velocity_y =
                    0.0f;
            }

            break;
        }
    }


    // Temporarily use the destination sector
    // while checking the new position

    int previous_sector =
        player->sector;

    player->sector =
        new_sector;


    // Check solid wall collisions

    bool blocked = false;

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
                wall
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

            blocked = true;
            break;
        }
    }


    // Move normally if nothing blocked us

    if (!blocked) {

        player->x =
            new_x;

        player->z =
            new_z;
    }

    // Otherwise resolve X and Z separately

    else {

        float x_only =
            player->x +
            player->velocity_x *
            delta_time;

        float z_only =
            player->z +
            player->velocity_z *
            delta_time;

        bool x_blocked =
            false;

        bool z_blocked =
            false;


        // Check X movement

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
                    wall
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
            }
        }


        // Check Z movement

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
                    wall
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
            }
        }


        if (!x_blocked) {

            player->x =
                x_only;
        }

        else {

            player->velocity_x =
                0.0f;
        }


        if (!z_blocked) {

            player->z =
                z_only;
        }

        else {

            player->velocity_z =
                0.0f;
        }
    }


    // If the attempted portal crossing was blocked,
    // restore the previous sector

    if (blocked) {

        player->sector =
            previous_sector;
    }


    // Apply vertical movement

    player->y +=
        player->velocity_y *
        delta_time;


    // Apply floor collision again after vertical movement

    current_sector =
        &level->sectors[player->sector];

    floor_height =
        current_sector->floor_height;

    if (
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