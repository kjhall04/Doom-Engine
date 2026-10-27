#ifndef RENDERER_H
#define RENDERER_H

#include <SDL3/SDL.h>

#include "../player.h"
#include "../level.h"
#include "../textures/texture_manager.h"

void renderer_render(
    SDL_Renderer *renderer,
    Player *player,
    Level *level,
    const TextureManager *textures
);

#endif