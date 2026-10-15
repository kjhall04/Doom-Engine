#ifndef RENDER_WALLS_H
#define RENDER_WALLS_H

#include "../player.h"
#include "../level.h"

#include <SDL3/SDL.h>

void render_walls_draw_solid(
    SDL_Renderer *renderer,
    const Player *player,
    const Level *level,
    int screen_width,
    int screen_height
);

#endif