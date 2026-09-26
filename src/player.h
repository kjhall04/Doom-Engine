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

    // Ground things
    bool grounded;
} Player;

void player_init(Player *player);
void player_update(
    Player *player, 
    Input *input, 
    Level *level,
    float delta_time);

#endif