#include "editor.h"
#include "engine/level/level_io.h"

#include <stdio.h>

#define EDITOR_WINDOW_WIDTH 1920
#define EDITOR_WINDOW_HEIGHT 1080
#define EDITOR_WINDOW_TITLE "Doom Engine - Level Editor"

bool editor_init(Editor *editor, const char *level_path) {
    if (!editor || !level_path) {
        return false;
    }

    *editor = (Editor){0};

    if (SDL_strlen(level_path) >= sizeof(editor->level_path)) {
        fprintf(stderr, "Level path too long.\n");
        return false;
    }
    SDL_strlcpy(editor->level_path, level_path, sizeof(editor->level_path));

    level_init(&editor->level);
    editor_viewport_init(&editor->viewport);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL initialization failed: %s\n",
                SDL_GetError());
        return false;
    }

    editor->window = SDL_CreateWindow(
        EDITOR_WINDOW_TITLE,
        EDITOR_WINDOW_WIDTH,
        EDITOR_WINDOW_HEIGHT,
        SDL_WINDOW_RESIZABLE
    );

    if (!editor->window) {
        fprintf(stderr, "Editor window creation failed: %s\n",
                SDL_GetError());
        return false;
    }

    editor->renderer = SDL_CreateRenderer(
        editor->window,
        NULL
    );

    if (!editor->renderer) {
        fprintf(stderr, "Editor renderer creation failed: %s\n",
                SDL_GetError());
        return false;
    }

    if (!level_load_json(&editor->level, level_path)) {
        fprintf(stderr, "Failed to load editor level: %s\n",
                level_path);
        return false;
    }

    editor->is_running = true;

    return true;
}

static void editor_events(Editor *editor) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            editor->is_running = false;
            continue;
        }

        if (event.type == SDL_EVENT_KEY_DOWN &&
            !event.key.repeat &&
            event.key.scancode == SDL_SCANCODE_ESCAPE) {
            editor->is_running = false;
            continue;
        }

        if (event.type == SDL_EVENT_KEY_DOWN &&
            !event.key.repeat &&
            event.key.scancode == SDL_SCANCODE_S &&
            (event.key.mod & SDL_KMOD_CTRL)) {
            if (level_save_json(&editor->level, editor->level_path)) {
                SDL_Log("Saved level: %s", editor->level_path);
            } else {
                SDL_Log("Failed to save: %s", editor->level_path);
            }
            continue;
        }

        editor_viewport_process_event(
            &editor->viewport,
            editor->renderer,
            &event,
            &editor->level
        );
    }
}

void editor_run(Editor *editor) {
    while (editor->is_running) {
        editor_events(editor);

        if (!editor->is_running) {
            break;
        }

        editor_viewport_render(
            editor->renderer,
            &editor->viewport,
            &editor->level
        );

        // Prevent the editor from consuming an entire CPU core
        // while its viewport is idle.
        SDL_Delay(8);
    }
}

void editor_destroy(Editor *editor) {
    if (!editor) {
        return;
    }

    level_free(&editor->level);

    if (editor->renderer) {
        SDL_DestroyRenderer(editor->renderer);
    }

    if (editor->window) {
        SDL_DestroyWindow(editor->window);
    }

    SDL_Quit();

    *editor = (Editor){0};
}