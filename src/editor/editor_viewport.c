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

static void screen_to_world(
    const EditorViewport *editor, int width, int height,
    float sx, float sy, float *x, float *z
) {
    *x = editor->camera_x + (sx - width * 0.5f) / editor->zoom;
    *z = editor->camera_z + (sy - height * 0.5f) / editor->zoom;
}

void editor_viewport_process_event(
    EditorViewport *editor,
    SDL_Renderer *renderer,
    const SDL_Event *event,
    Level *level
) {
    int width = 0, height = 0;
    if (!SDL_GetRenderOutputSize(renderer, &width, &height)) return;

    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        if (event->wheel.y != 0) {
            editor->zoom *= event->wheel.y > 0 ? 1.1f : 0.9f;
            if (editor->zoom < EDITOR_MIN_ZOOM) editor->zoom = EDITOR_MIN_ZOOM;
            if (editor->zoom > EDITOR_MAX_ZOOM) editor->zoom = EDITOR_MAX_ZOOM;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_MIDDLE) editor->panning = true;
        if (event->button.button == SDL_BUTTON_LEFT) {
            float sx, sy;
            world_to_screen(editor, level->player_spawn.x,
                            level->player_spawn.z, width, height, &sx, &sy);
            float dx = event->button.x - sx;
            float dy = event->button.y - sy;
            editor->spawn_selected = dx * dx + dy * dy <= 144.0f;
            editor->spawn_dragging = editor->spawn_selected;
            editor->spawn_rotating = false;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button == SDL_BUTTON_MIDDLE) editor->panning = false;
        if (event->button.button == SDL_BUTTON_LEFT) editor->spawn_dragging = false;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (editor->panning) {
            editor->camera_x -= event->motion.xrel / editor->zoom;
            editor->camera_z -= event->motion.yrel / editor->zoom;
        }
        if (editor->spawn_dragging) {
            screen_to_world(editor, width, height, event->motion.x,
                            event->motion.y, &level->player_spawn.x,
                            &level->player_spawn.z);
        }
        if (editor->spawn_rotating) {
            float sx, sy;
            world_to_screen(editor, level->player_spawn.x,
                            level->player_spawn.z, width, height, &sx, &sy);
            level->player_spawn.yaw = atan2f(event->motion.x - sx,
                                             -(event->motion.y - sy)) *
                                             (180.0f / 3.14159265358979323846f);
        }
    }

    if (event->type == SDL_EVENT_KEY_DOWN && editor->spawn_selected) {
        switch (event->key.scancode) {
            case SDL_SCANCODE_R: editor->spawn_rotating = true; break;
            case SDL_SCANCODE_LEFTBRACKET: level->player_spawn.pitch -= 1.0f; break;
            case SDL_SCANCODE_RIGHTBRACKET: level->player_spawn.pitch += 1.0f; break;
            case SDL_SCANCODE_PAGEUP: level->player_spawn.y += 0.1f; break;
            case SDL_SCANCODE_PAGEDOWN: level->player_spawn.y -= 0.1f; break;
            default: break;
        }
        if (level->player_spawn.pitch > 89.0f) level->player_spawn.pitch = 89.0f;
        if (level->player_spawn.pitch < -89.0f) level->player_spawn.pitch = -89.0f;
    }
    if (event->type == SDL_EVENT_KEY_UP &&
        event->key.scancode == SDL_SCANCODE_R) {
        editor->spawn_rotating = false;
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

static void editor_draw_spawn(
    SDL_Renderer *renderer, const EditorViewport *editor,
    const Level *level, int width, int height
) {
    float x, y;
    world_to_screen(editor, level->player_spawn.x,
                    level->player_spawn.z, width, height, &x, &y);
    float yaw = level->player_spawn.yaw *
                (3.14159265358979323846f / 180.0f);
    SDL_SetRenderDrawColor(renderer, 255, 215, 95, 255);
    SDL_FRect marker = {x - 6.0f, y - 6.0f, 12.0f, 12.0f};
    SDL_RenderFillRect(renderer, &marker);
    SDL_RenderLine(renderer, x, y,
                   x + sinf(yaw) * 30.0f,
                   y - cosf(yaw) * 30.0f);
    if (editor->spawn_selected) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_FRect selection = {x - 10.0f, y - 10.0f, 20.0f, 20.0f};
        SDL_RenderRect(renderer, &selection);
    }
}

static void editor_draw_controls(SDL_Renderer *renderer)
{
    int width, height;

    if (!SDL_GetRenderOutputSize(renderer, &width, &height))
        return;

    const float x = 12.0f;
    const float panel_height = 146.0f;
    const float y = (float)height - panel_height - 12.0f;

    SDL_FRect panel = {x, y, 270.0f, panel_height};

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 12, 16, 24, 230);
    SDL_RenderFillRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 95, 110, 135, 255);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 255, 215, 95, 255);
    SDL_RenderDebugText(renderer, x + 10, y + 10, "LEVEL EDITOR");

    SDL_SetRenderDrawColor(renderer, 220, 225, 235, 255);

    const char *lines[] = {
        "Left Drag   Move Spawn",
        "Middle Drag Pan View",
        "Wheel       Zoom",
        "Hold R      Rotate Spawn",
        "[ / ]       Adjust Pitch",
        "PgUp/PgDn   Adjust Height",
        "Ctrl+S      Save",
        "Esc         Exit"
    };

    for (int i = 0; i < 8; ++i) {
        SDL_RenderDebugText(
            renderer,
            x + 10,
            y + 30 + i * 13,
            lines[i]
        );
    }
}

void editor_viewport_render(
    SDL_Renderer *renderer,
    const EditorViewport *editor,
    const Level *level
) {
    int width, height;

    if (!SDL_GetRenderOutputSize(renderer, &width, &height))
        return;

    SDL_SetRenderDrawColor(renderer, 24, 29, 38, 255);
    SDL_RenderClear(renderer);

    editor_draw_grid(renderer, editor, width, height);
    editor_draw_walls(renderer, editor, level, width, height);
    editor_draw_vertices(renderer, editor, level, width, height);
    editor_draw_spawn(renderer, editor, level, width, height);
    editor_draw_controls(renderer);

    SDL_RenderPresent(renderer);
}