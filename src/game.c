#include "game.h"
#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <SDL3/SDL.h>

#define SDL_FLAGS SDL_INIT_VIDEO

#define WINDOW_TITLE "Doom Engine"
#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080

typedef struct RenderWall {
    ScreenWall screen_wall;
    float distance;
} RenderWall;

static float game_wall_distance(
    Player *player,
    Wall *wall
) {
    float center_x =
        (wall->x1 + wall->x2) * 0.5f;

    float center_z =
        (wall->z1 + wall->z2) * 0.5f;

    float difference_x =
        center_x - player->x;

    float difference_z =
        center_z - player->z;

    return
        difference_x * difference_x +
        difference_z * difference_z;
}

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

        RenderWall *render_walls =
            malloc(
                sizeof(RenderWall) *
                game->level.wall_count
            );

        if (render_walls == NULL) {
            SDL_RenderPresent(game->renderer);
            return;
        }


        int render_wall_count = 0;


        /*
        * Project every visible wall.
        */
        for (int i = 0;
            i < game->level.wall_count;
            i++) {

            Wall *wall =
                &game->level.walls[i];

            ScreenWall screen_wall;

            bool wall_visible =
                renderer_draw_wall(
                    &game->player,
                    wall,
                    (float)width,
                    (float)height,
                    &screen_wall
                );

            if (!wall_visible) {
                continue;
            }


            RenderWall *render_wall =
                &render_walls[render_wall_count];

            render_wall->screen_wall =
                screen_wall;

            render_wall->distance =
                game_wall_distance(
                    &game->player,
                    wall
                );

            render_wall_count++;
        }


        /*
        * Sort from farthest to nearest.
        */
        for (int i = 0;
            i < render_wall_count - 1;
            i++) {

            for (int j = i + 1;
                j < render_wall_count;
                j++) {

                if (
                    render_walls[j].distance >
                    render_walls[i].distance
                ) {

                    RenderWall temporary =
                        render_walls[i];

                    render_walls[i] =
                        render_walls[j];

                    render_walls[j] =
                        temporary;
                }
            }
        }


        /*
        * Draw the walls.
        */
        for (int i = 0;
            i < render_wall_count;
            i++) {

            ScreenWall *screen_wall =
                &render_walls[i].screen_wall;

            SDL_Vertex vertices[4];

            vertices[0].position.x =
                screen_wall->top_left.x;

            vertices[0].position.y =
                screen_wall->top_left.y;

            vertices[0].color.r = 255;
            vertices[0].color.g = 255;
            vertices[0].color.b = 255;
            vertices[0].color.a = 255;


            vertices[1].position.x =
                screen_wall->top_right.x;

            vertices[1].position.y =
                screen_wall->top_right.y;

            vertices[1].color.r = 255;
            vertices[1].color.g = 255;
            vertices[1].color.b = 255;
            vertices[1].color.a = 255;


            vertices[2].position.x =
                screen_wall->bottom_right.x;

            vertices[2].position.y =
                screen_wall->bottom_right.y;

            vertices[2].color.r = 255;
            vertices[2].color.g = 255;
            vertices[2].color.b = 255;
            vertices[2].color.a = 255;


            vertices[3].position.x =
                screen_wall->bottom_left.x;

            vertices[3].position.y =
                screen_wall->bottom_left.y;

            vertices[3].color.r = 255;
            vertices[3].color.g = 255;
            vertices[3].color.b = 255;
            vertices[3].color.a = 255;


            int indices[6] = {
                0, 1, 2,
                0, 2, 3
            };


            SDL_RenderGeometry(
                game->renderer,
                NULL,
                vertices,
                4,
                indices,
                6
            );

            /*
            * Draw the wall outline in blue.
            */
            SDL_SetRenderDrawColor(
                game->renderer,
                0,
                0,
                0,
                255
            );

            SDL_RenderLine(
                game->renderer,
                screen_wall->top_left.x,
                screen_wall->top_left.y,
                screen_wall->top_right.x,
                screen_wall->top_right.y
            );

            SDL_RenderLine(
                game->renderer,
                screen_wall->bottom_left.x,
                screen_wall->bottom_left.y,
                screen_wall->bottom_right.x,
                screen_wall->bottom_right.y
            );

            SDL_RenderLine(
                game->renderer,
                screen_wall->top_left.x,
                screen_wall->top_left.y,
                screen_wall->bottom_left.x,
                screen_wall->bottom_left.y
            );

            SDL_RenderLine(
                game->renderer,
                screen_wall->top_right.x,
                screen_wall->top_right.y,
                screen_wall->bottom_right.x,
                screen_wall->bottom_right.y
            );
        }

        free(render_walls);
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