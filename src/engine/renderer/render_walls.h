#ifndef RENDER_WALLS_H
#define RENDER_WALLS_H

#include "game/player.h"
#include "engine/level/level.h"
#include "engine/textures/texture_manager.h"

#include <stdbool.h>
#include <SDL3/SDL.h>

void render_walls_draw_solid(
    SDL_Renderer *renderer,
    const Player *player,
    const Level *level,
    const TextureManager *textures,
    int screen_width,
    int screen_height
);

#endif