#ifndef GAME_H
#define GAME_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "input.h"
#include "player.h"

// Game object structure

typedef struct Game {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Event event;

    bool is_running;

    Input input;
    Player player;
} Game;

bool game_new(Game **game);
void game_free(Game **game);

void game_events(Game *game);
void game_update(Game *game, float delta_time);
void game_draw(Game *game);
void game_run(Game *game);

#endif