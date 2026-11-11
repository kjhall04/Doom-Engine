#ifndef RENDER_SECTOR_TRAVERSAL_H
#define RENDER_SECTOR_TRAVERSAL_H

#include "render_portals.h"

/* Draw one wall using the current portal clipping window. */
typedef void (*RenderWallDrawCallback)(
    const Player *player, const Wall *wall, const Level *level,
    const RenderPortalClip *clip, void *userdata
);

/* Columns of the screen through which a sector was reached. A sector that
 * was never reached has left > right. */
typedef struct RenderSectorVisit {
    int left;
    int right;
} RenderSectorVisit;

/* Traverse visible sectors, restoring clip state after each portal.
 *
 * If visits is not NULL it must hold level->sector_count entries; it is
 * filled with the column range each sector was visible through, so later
 * passes can skip sectors that cannot be seen. */
void render_sector_traverse(
    const Player *player, const Level *level, RenderPortalClip *clip,
    RenderWallDrawCallback draw_wall, void *userdata,
    RenderSectorVisit *visits
);

#endif
