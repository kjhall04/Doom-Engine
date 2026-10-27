#ifndef RENDER_PLANES_H
#define RENDER_PLANES_H

#include <stdint.h>

#include "game/player.h"
#include "engine/level/level.h"
#include "engine/textures/texture_manager.h"

void render_planes_draw(
    const Player *player,
    const Level *level,
    const TextureManager *textures,
    uint32_t *pixels,
    float *depth,
    int width,
    int height
);

#endif