#ifndef RENDER_VISIBILITY_H
#define RENDER_VISIBILITY_H

#include "../level.h"
#include "../player.h"

#include <stdbool.h>

bool render_visibility_collect(
    const Player *player,
    const Level *level,
    bool *visible_sectors
);

#endif