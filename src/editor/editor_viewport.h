#ifndef EDITOR_VIEWPORT_H
#define EDITOR_VIEWPORT_H

#include <stdbool.h>
#include <SDL3/SDL.h>

#include "engine/level/level.h"
#include "game/player.h"

typedef enum EditorTool {
    EDITOR_TOOL_SELECT = 0,
    EDITOR_TOOL_VERTEX,
    EDITOR_TOOL_WALL,
    EDITOR_TOOL_SECTOR,
    EDITOR_TOOL_ROOM,
    EDITOR_TOOL_LIGHT
} EditorTool;

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

    bool save_notification_visible;
    bool save_notification_success;
    Uint64 save_notification_time;

    EditorTool active_tool;
    int wall_start_vertex;
    int selected_wall;
    int selected_sector;
    bool room_dragging;
    bool room_failed;
    float room_start_x, room_start_z;
    float room_end_x, room_end_z;
    bool show_sector_fill;
    int selected_light;
    bool light_dragging;
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

void editor_viewport_show_save_notification(
    EditorViewport *editor,
    bool success
);

#endif