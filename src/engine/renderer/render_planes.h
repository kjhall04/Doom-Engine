#ifndef RENDER_PLANES_H
#define RENDER_PLANES_H

#include <stdint.h>

#include "game/player.h"
#include "engine/level/level.h"
#include "engine/textures/texture_manager.h"
#include "render_sector_traversal.h"

/* Draws sector floors and ceilings into the shared color/depth buffers.
 *
 * visits (optional, level->sector_count entries) limits drawing to sectors
 * the portal traversal reached, and to the columns they were reached
 * through. Pass NULL to draw every sector. */
void render_planes_draw(
    const Player *player,
    const Level *level,
    const TextureManager *textures,
    uint32_t *pixels,
    float *depth,
    int width,
    int height,
    const RenderSectorVisit *visits
);

#endif
