#include "game.h"
#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>

#define SDL_FLAGS SDL_INIT_VIDEO

#define WINDOW_TITLE "Doom Engine"
#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080

static bool game_init_sdl(Game *game);

// Init SDL
static bool game_init_sdl(Game *game) {
    if (!SDL_Init(SDL_FLAGS)) {
        fprintf(stderr, "Error initializing SDL3: %s\n", SDL_GetError());
        return false;
    }

    game->window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if (!game->window) {
        fprintf(stderr, "Error creating Window: %s\n", SDL_GetError());
        return false;
    }

    game->renderer = SDL_CreateRenderer(game->window, NULL);
    if (!game->renderer) {
        fprintf(stderr, "Error creating Renderer: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

// Start new game instance
bool game_new(struct Game **game) {
    *game = calloc(1, sizeof(struct Game));
    if (*game == NULL) {
        fprintf(stderr, "Error Calloc of New Game.\n");
        return false;
    }
    struct Game *g = *game;

    if (!game_init_sdl(g)) {
        return false;
    }

    // Player initialized itself
    player_init(&g->player);
    input_init(&g->input);
    level_init(&g->level);

    SDL_SetWindowRelativeMouseMode(g->window, true);

    g->is_running = true;

    return true;
}

// Free up memory after game is closed
void game_free(struct Game **game) {
    if (*game) {
        struct Game *g = *game;

        if (g->renderer) {
            SDL_DestroyRenderer(g->renderer);
            g->renderer = NULL;
        }

        if (g->window) {
            SDL_DestroyWindow(g->window);
            g->window = NULL;
        }
        
        level_free(&g->level);

        SDL_Quit();

        free(g);
        g = NULL;
        *game = NULL;

        printf("All Clean!\n");
    }
}


// Watch for game events
// Escape to close the window
void game_events(Game *game) {

    while (SDL_PollEvent(&game->event)) {

        if (game->event.type == SDL_EVENT_QUIT) {
            game->is_running = false;
        }

        if (game->event.type == SDL_EVENT_KEY_DOWN &&
            game->event.key.scancode == SDL_SCANCODE_ESCAPE) {

            game->is_running = false;
        }

        input_process_event(&game->input, &game->event);
    }
}

// Update data
void game_update(Game *game, float delta_time) {
    player_update(
        &game->player, 
        &game->input, 
        &game->level, 
        delta_time
    );
}

// Draw the window
void game_draw(struct Game *game) {
    SDL_SetRenderDrawColor(
        game->renderer,
        0,
        0,
        0,
        255
    );

    SDL_RenderClear(game->renderer);

    int width;
    int height;

    SDL_GetWindowSize(
        game->window,
        &width,
        &height
    );

    for (int i = 0; i < game->level.wall_count; i++) {

        Wall *wall = &game->level.walls[i];

        ScreenLine lines[MAX_WALL_LINES];

        int line_count = renderer_draw_wall(
            &game->player,
            wall,
            (float)width,
            (float)height,
            lines
        );

        if (line_count > 0) {

            SDL_SetRenderDrawColor(
                game->renderer,
                255,
                255,
                255,
                255
            );

            for (int line = 0; line < line_count; line++) {

                SDL_RenderLine(
                    game->renderer,
                    lines[line].start.x,
                    lines[line].start.y,
                    lines[line].end.x,
                    lines[line].end.y
                );
            }
        }
    }

    SDL_RenderPresent(game->renderer);
}

// Run the game and get time
void game_run(struct Game *game) {

    Uint64 previous_time = SDL_GetPerformanceCounter();
    Uint64 frequency = SDL_GetPerformanceFrequency();

    while (game->is_running) {

        Uint64 current_time = SDL_GetPerformanceCounter();

        float delta_time = 
            (float)(current_time - previous_time) / (float)frequency;

        previous_time = current_time;
        
        game_events(game);

        game_update(game, delta_time);

        game_draw(game);
    }
}