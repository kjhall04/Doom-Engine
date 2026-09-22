#include "player.h"
#include "level.h"

#include <math.h>

#define PLAYER_RADIUS 0.3f

// Init player object with starting values
void player_init(Player *player) {
    player->x = 0.0f;
    player->y = 0.0f;
    player->z = 0.0f;

    player->velocity_x = 0.0f;
    player->velocity_y = 0.0f;
    player->velocity_z = 0.0f;

    player->eye_height = 1.6f;

    player->yaw = 0.0f;
    player->pitch = 0.0f;

    player->move_speed = 0.0f;
    player->acceleration = 20.0f;

    player->grounded = true;
}

static bool player_resolve_wall_collision(
    Player *player,
    Wall *wall
) {
    float wall_x = wall->x2 - wall->x1;
    float wall_z = wall->z2 - wall->z1;

    float length_squared =
        wall_x * wall_x +
        wall_z * wall_z;

    if (length_squared <= 0.0f) {
        return false;
    }

    float player_to_wall_x =
        player->x - wall->x1;

    float player_to_wall_z =
        player->z - wall->z1;

    float t =
        (player_to_wall_x * wall_x +
         player_to_wall_z * wall_z) /
        length_squared;

    if (t < 0.0f) {
        t = 0.0f;
    }

    if (t > 1.0f) {
        t = 1.0f;
    }

    float closest_x =
        wall->x1 + wall_x * t;

    float closest_z =
        wall->z1 + wall_z * t;

    float difference_x =
        player->x - closest_x;

    float difference_z =
        player->z - closest_z;

    float distance_squared =
        difference_x * difference_x +
        difference_z * difference_z;

    float radius_squared =
        PLAYER_RADIUS * PLAYER_RADIUS;

    if (distance_squared >= radius_squared) {
        return false;
    }

    float distance = sqrtf(distance_squared);

    float normal_x;
    float normal_z;

    if (distance > 0.0001f) {

        normal_x =
            difference_x / distance;

        normal_z =
            difference_z / distance;

    } else {

        normal_x = -wall_z;
        normal_z = wall_x;

        float normal_length =
            sqrtf(
                normal_x * normal_x +
                normal_z * normal_z
            );

        if (normal_length <= 0.0001f) {
            return false;
        }

        normal_x /= normal_length;
        normal_z /= normal_length;
    }

    float penetration =
        PLAYER_RADIUS - distance;

    player->x +=
        normal_x * penetration;

    player->z +=
        normal_z * penetration;

    float velocity_into_wall =
        player->velocity_x * normal_x +
        player->velocity_z * normal_z;

    if (velocity_into_wall < 0.0f) {

        player->velocity_x -=
            velocity_into_wall * normal_x;

        player->velocity_z -=
            velocity_into_wall * normal_z;
    }

    return true;
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

    // Jumping
    player->grounded = player->y <= 0.0f;

    if (player->grounded) {
        player->y = 0.0f;

        if (player->velocity_y < 0.0f) {
            player->velocity_y = 0.0f;
        }
    }

    if (player->grounded && input->jump) {
        player->velocity_y = jump_force;
        player->grounded = false;
        input->jump = false;
    }

    player->velocity_y -= gravity * delta_time;

    // Mouse look
    player->yaw += input->mouse_delta_x * mouse_sensitivity;
    player->pitch -= input->mouse_delta_y * mouse_sensitivity;

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
    float length = sqrtf(
        direction_x * direction_x +
        direction_z * direction_z
    );

    if (length > 0.0f) {
        direction_x /= length;
        direction_z /= length;
    }

    // Rotate movement by player yaw
    float sin_yaw = sinf(player->yaw);
    float cos_yaw = cosf(player->yaw);

    float acceleration_x =
        direction_x * cos_yaw -
        direction_z * sin_yaw;

    float acceleration_z =
        direction_x * sin_yaw +
        direction_z * cos_yaw;

    // Apply drag
    float drag = expf(-delta_time * acceleration_rate);
    float diff = 1.0f - drag;

    player->velocity_x -=
        player->velocity_x * diff;

    player->velocity_z -=
        player->velocity_z * diff;

    // Check whether the player is touching a wall
    for (int i = 0; i < level->wall_count; i++) {

        float wall_x =
            level->walls[i].x2 -
            level->walls[i].x1;

        float wall_z =
            level->walls[i].z2 -
            level->walls[i].z1;

        float length_squared =
            wall_x * wall_x +
            wall_z * wall_z;

        if (length_squared <= 0.0f) {
            continue;
        }

        float player_to_wall_x =
            player->x -
            level->walls[i].x1;

        float player_to_wall_z =
            player->z -
            level->walls[i].z1;

        float t =
            (player_to_wall_x * wall_x +
            player_to_wall_z * wall_z) /
            length_squared;

        if (t < 0.0f) {
            t = 0.0f;
        }

        if (t > 1.0f) {
            t = 1.0f;
        }

        float closest_x =
            level->walls[i].x1 +
            wall_x * t;

        float closest_z =
            level->walls[i].z1 +
            wall_z * t;

        float difference_x =
            player->x -
            closest_x;

        float difference_z =
            player->z -
            closest_z;

        float distance_squared =
            difference_x * difference_x +
            difference_z * difference_z;

        if (distance_squared <
            PLAYER_RADIUS * PLAYER_RADIUS) {

            float distance =
                sqrtf(distance_squared);

            if (distance > 0.0001f) {

                float normal_x =
                    difference_x / distance;

                float normal_z =
                    difference_z / distance;

                float acceleration_into_wall =
                    acceleration_x * normal_x +
                    acceleration_z * normal_z;

                if (acceleration_into_wall < 0.0f) {

                    acceleration_x -=
                        acceleration_into_wall * normal_x;

                    acceleration_z -=
                        acceleration_into_wall * normal_z;
                }
            }
        }
    }

    // Apply acceleration
    player->velocity_x +=
        diff * acceleration_x *
        movement_speed / acceleration_rate;

    player->velocity_z +=
        diff * acceleration_z *
        movement_speed / acceleration_rate;

    // Update position
    float new_x =
        player->x +
        player->velocity_x * delta_time;

    float new_z =
        player->z +
        player->velocity_z * delta_time;

    player->x = new_x;
    player->z = new_z;

    for (int iteration = 0; iteration < 4; iteration++) {

        bool collision_found = false;

        for (int i = 0; i < level->wall_count; i++) {

            if (player_resolve_wall_collision(
                player,
                &level->walls[i]
            )) {
                collision_found = true;
            }
        }

    if (!collision_found) {
        break;
    }
}
    
    player->y += player->velocity_y * delta_time;
}