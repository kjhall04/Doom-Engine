#include "player.h"
#include <math.h>

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

// Update player values as they move
void player_update(Player *player, Input *input, float delta_time) {

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

    // Apply acceleration
    player->velocity_x +=
        diff * acceleration_x * movement_speed / acceleration_rate;

    player->velocity_z +=
        diff * acceleration_z * movement_speed / acceleration_rate;

    // Update position
    player->x += player->velocity_x * delta_time;
    player->y += player->velocity_y * delta_time;
    player->z += player->velocity_z * delta_time;
}