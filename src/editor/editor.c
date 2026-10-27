#include "editor.h"

#include <stdio.h>

#define EDITOR_WINDOW_WIDTH 1280
#define EDITOR_WINDOW_HEIGHT 800
#define EDITOR_WINDOW_TITLE "Doom Engine - Level Editor"

bool editor_init(Editor *editor, const char *level_path) {
    if (!editor || !level_path) {
        return false;
    }

    *editor = (Editor){0};

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

    // Temporary preview marker.
    // This will become a configurable spawn point later.
    editor->preview_player.x = 0.0f;
    editor->preview_player.y = 0.0f;
    editor->preview_player.z = 0.0f;
    editor->preview_player.yaw = 0.0f;

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

        editor_viewport_process_event(
            &editor->viewport,
            &event
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
            &editor->level,
            &editor->preview_player
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