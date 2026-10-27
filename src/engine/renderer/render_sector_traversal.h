#ifndef RENDER_SECTOR_TRAVERSAL_H
#define RENDER_SECTOR_TRAVERSAL_H

#include "render_portals.h"

/* Draw one wall using the current portal clipping window. */
typedef void (*RenderWallDrawCallback)(
    const Player *player, const Wall *wall, const Level *level,
    const RenderPortalClip *clip, void *userdata
);

/* Traverse visible sectors, restoring clip state after each portal. */
void render_sector_traverse(
    const Player *player, const Level *level, RenderPortalClip *clip,
    RenderWallDrawCallback draw_wall, void *userdata
);

#endif
