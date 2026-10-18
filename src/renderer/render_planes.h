#ifndef RENDER_PLANES_H
#define RENDER_PLANES_H

#include <stdint.h>
#include "../player.h"
#include "../level.h"

void render_planes_draw(const Player *player, const Level *level,
                        uint32_t *pixels, float *depth, int width, int height);

#endif
