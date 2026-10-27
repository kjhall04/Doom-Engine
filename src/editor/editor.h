#ifndef EDITOR_H
#define EDITOR_H

#include <stdbool.h>
#include <SDL3/SDL.h>

#include "engine/level/level.h"
#include "editor_viewport.h"

typedef struct Editor {
    SDL_Window *window;
    SDL_Renderer *renderer;

    bool is_running;

    Level level;
    char level_path[512];
    EditorViewport viewport;
} Editor;

bool editor_init(Editor *editor, const char *level_path);
void editor_run(Editor *editor);
void editor_destroy(Editor *editor);

#endif