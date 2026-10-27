#ifndef GAME_H
#define GAME_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "game/input.h"
#include "game/player.h"
#include "engine/level/level.h"
#include "engine/textures/texture_manager.h"

typedef struct Game {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Event event;

    bool is_running;

    Input input;
    Player player;
    Level level;
    TextureManager textures;
} Game;

bool game_new(Game **game);
void game_free(Game **game);

void game_events(Game *game);
void game_update(Game *game, float delta_time);
void game_draw(Game *game);
void game_run(Game *game);

#endif