#ifndef EDITOR_VIEWPORT_H
#define EDITOR_VIEWPORT_H

#include <stdbool.h>
#include <SDL3/SDL.h>

#include "engine/level/level.h"
#include "game/player.h"

typedef struct EditorViewport {
    float camera_x;
    float camera_z;
    float zoom;
    bool panning;
} EditorViewport;

void editor_viewport_init(EditorViewport *viewport);

void editor_viewport_process_event(
    EditorViewport *viewport,
    const SDL_Event *event
);

void editor_viewport_render(
    SDL_Renderer *renderer,
    const EditorViewport *viewport,
    const Level *level,
    const Player *player
);

#endif