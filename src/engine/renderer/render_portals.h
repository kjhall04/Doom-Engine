#ifndef RENDER_PORTALS_H
#define RENDER_PORTALS_H

#include <stdbool.h>
#include "game/player.h"
#include "engine/level/level.h"

/* Mutable screen clipping window, owned by the wall renderer. */
typedef struct RenderPortalClip {
    int width, height;
    int left, right;
    int *top, *bottom;
} RenderPortalClip;

typedef struct PortalVertex {
    float x, y, z;
} PortalVertex;

int render_portal_clip_polygon(
    const PortalVertex *input, int input_count, PortalVertex *output
);

bool render_portal_apply_clip(
    RenderPortalClip *clip, const Player *player,
    const Vertex *start, const Vertex *end,
    float opening_bottom, float opening_top
);

#endif
