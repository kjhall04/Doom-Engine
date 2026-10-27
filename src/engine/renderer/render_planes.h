#ifndef RENDER_PLANES_H
#define RENDER_PLANES_H

#include <stdint.h>

#include "../player.h"
#include "../level.h"
#include "../textures/texture_manager.h"

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