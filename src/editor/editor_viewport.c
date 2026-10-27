#include "editor_viewport.h"

#include <math.h>

#define EDITOR_MIN_ZOOM 4.0f
#define EDITOR_MAX_ZOOM 200.0f

static void world_to_screen(
    const EditorViewport *editor,
    float world_x,
    float world_z,
    int width,
    int height,
    float *screen_x,
    float *screen_y
) {
    *screen_x =
        width * 0.5f +
        (world_x - editor->camera_x) * editor->zoom;

    *screen_y =
        height * 0.5f +
        (world_z - editor->camera_z) * editor->zoom;
}

void editor_viewport_init(EditorViewport *editor) {
    *editor = (EditorViewport){0};

    editor->camera_x = 0.0f;
    editor->camera_z = 0.0f;
    editor->zoom = 25.0f;
}

void editor_viewport_process_event(
    EditorViewport *editor,
    const SDL_Event *event
) {
    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        float factor = event->wheel.y > 0 ? 1.1f : 0.9f;

        editor->zoom *= factor;

        if (editor->zoom < EDITOR_MIN_ZOOM)
            editor->zoom = EDITOR_MIN_ZOOM;

        if (editor->zoom > EDITOR_MAX_ZOOM)
            editor->zoom = EDITOR_MAX_ZOOM;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->button.button == SDL_BUTTON_MIDDLE) {
        editor->panning = true;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        event->button.button == SDL_BUTTON_MIDDLE) {
        editor->panning = false;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION &&
        editor->panning) {
        editor->camera_x -= event->motion.xrel / editor->zoom;
        editor->camera_z -= event->motion.yrel / editor->zoom;
    }
}

static void editor_draw_grid(
    SDL_Renderer *renderer,
    const EditorViewport *editor,
    int width,
    int height
) {
    const float spacing = editor->zoom;

    SDL_SetRenderDrawColor(renderer, 48, 55, 68, 255);

    float origin_x =
        width * 0.5f - editor->camera_x * editor->zoom;

    float origin_y =
        height * 0.5f - editor->camera_z * editor->zoom;

    float start_x = fmodf(origin_x, spacing);
    float start_y = fmodf(origin_y, spacing);

    if (start_x < 0) start_x += spacing;
    if (start_y < 0) start_y += spacing;

    for (float x = start_x; x < width; x += spacing) {
        SDL_RenderLine(renderer, x, 0, x, (float)height);
    }

    for (float y = start_y; y < height; y += spacing) {
        SDL_RenderLine(renderer, 0, y, (float)width, y);
    }
}

static void editor_draw_walls(
    SDL_Renderer *renderer,
    const EditorViewport *editor,
    const Level *level,
    int width,
    int height
) {
    for (int i = 0; i < level->wall_count; ++i) {
        const Wall *wall = &level->walls[i];

        const Vertex *a = &level->vertices[wall->vertex_start];
        const Vertex *b = &level->vertices[wall->vertex_end];

        float x1, y1, x2, y2;

        world_to_screen(
            editor, a->x, a->z,
            width, height, &x1, &y1
        );

        world_to_screen(
            editor, b->x, b->z,
            width, height, &x2, &y2
        );

        if (wall->back_sector >= 0) {
            SDL_SetRenderDrawColor(renderer, 235, 175, 75, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 220, 225, 235, 255);
        }

        SDL_RenderLine(renderer, x1, y1, x2, y2);
    }
}

static void editor_draw_vertices(
    SDL_Renderer *renderer,
    const EditorViewport *editor,
    const Level *level,
    int width,
    int height
) {
    SDL_SetRenderDrawColor(renderer, 90, 205, 245, 255);

    for (int i = 0; i < level->vertex_count; ++i) {
        float x, y;

        world_to_screen(
            editor,
            level->vertices[i].x,
            level->vertices[i].z,
            width, height,
            &x, &y
        );

        SDL_FRect point = {
            x - 3.0f,
            y - 3.0f,
            6.0f,
            6.0f
        };

        SDL_RenderFillRect(renderer, &point);
    }
}

static void editor_draw_player(
    SDL_Renderer *renderer,
    const EditorViewport *editor,
    const Player *player,
    int width,
    int height
) {
    float x, y;

    world_to_screen(
        editor,
        player->x,
        player->z,
        width,
        height,
        &x,
        &y
    );

    SDL_SetRenderDrawColor(renderer, 255, 215, 95, 255);

    // Player position marker.
    SDL_FRect marker = {
        x - 4.0f,
        y - 4.0f,
        8.0f,
        8.0f
    };

    SDL_RenderFillRect(renderer, &marker);

    // Player facing direction, matching the engine's yaw convention.
    float direction_x = sinf(player->yaw);
    float direction_z = -cosf(player->yaw);

    SDL_RenderLine(
        renderer,
        x,
        y,
        x + direction_x * 22.0f,
        y + direction_z * 22.0f
    );
}

void editor_render(
    SDL_Renderer *renderer,
    const EditorViewport *editor,
    const Level *level,
    const Player *player
) {
    int width, height;

    if (!SDL_GetRenderOutputSize(renderer, &width, &height))
        return;

    SDL_SetRenderDrawColor(renderer, 24, 29, 38, 255);
    SDL_RenderClear(renderer);

    editor_draw_grid(renderer, editor, width, height);
    editor_draw_walls(renderer, editor, level, width, height);
    editor_draw_vertices(renderer, editor, level, width, height);
    editor_draw_player(renderer, editor, player, width, height);

    SDL_RenderPresent(renderer);
}