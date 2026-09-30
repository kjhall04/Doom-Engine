#ifndef PLAYER_H
#define PLAYER_H

#include "input.h"
#include "level.h"

// Player object structure

typedef struct Player {
    // Position
    float x;
    float y;
    float z;

    float eye_height;
    float height;

    // Velocity
    float velocity_x;
    float velocity_y;
    float velocity_z;

    // Looking
    float yaw;
    float pitch;

    // Movement
    float move_speed;
    float acceleration;

    int sector;

    // Ground things
    bool grounded;

    // Step Transition
    bool stepping;
    float step_start_y;
    float step_target_y;
    float step_progress;

} Player;

void player_init(Player *player);

void player_update(
    Player *player, 
    Input *input, 
    Level *level,
    float delta_time
);

#endif