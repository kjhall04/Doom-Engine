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

    bool spawn_selected;
    bool spawn_dragging;
    bool spawn_rotating;

    int selected_vertex;
    bool vertex_dragging;
    float vertex_drag_offset_x;
    float vertex_drag_offset_z;

    bool snap_enabled;
    float grid_size;
} EditorViewport;

void editor_viewport_init(EditorViewport *viewport);

void editor_viewport_process_event(
    EditorViewport *editor,
    SDL_Renderer *renderer,
    const SDL_Event *event,
    Level *level
);

void editor_viewport_render(
    SDL_Renderer *renderer,
    const EditorViewport *editor,
    const Level *level
);

#endif