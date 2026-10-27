#ifndef RENDERER_H
#define RENDERER_H

#include <SDL3/SDL.h>

#include "game/player.h"
#include "engine/level/level.h"
#include "engine/textures/texture_manager.h"

void renderer_render(
    SDL_Renderer *renderer,
    Player *player,
    Level *level,
    const TextureManager *textures
);

#endif