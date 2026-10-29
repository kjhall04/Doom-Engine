#include "editor_viewport.h"

#include <math.h>
#include <stdio.h>

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

    editor->selected_vertex = -1;
    editor->vertex_dragging = false;
    editor->vertex_drag_offset_x = 0.0f;
    editor->vertex_drag_offset_z = 0.0f;

    editor->snap_enabled = true;
    editor->grid_size = 1.0f;
}

static void screen_to_world(
    const EditorViewport *editor, int width, int height,
    float sx, float sy, float *x, float *z
) {
    *x = editor->camera_x + (sx - width * 0.5f) / editor->zoom;
    *z = editor->camera_z + (sy - height * 0.5f) / editor->zoom;
}

static int editor_find_vertex(
    const EditorViewport *editor,
    const Level *level,
    int width,
    int height,
    float mouse_x,
    float mouse_y
) {
    const float selection_radius = 10.0f;
    const float radius_squared =
        selection_radius * selection_radius;

    int closest_vertex = -1;
    float closest_distance = radius_squared;

    for (int i = 0; i < level->vertex_count; ++i) {
        float sx, sy;

        world_to_screen(
            editor,
            level->vertices[i].x,
            level->vertices[i].z,
            width,
            height,
            &sx,
            &sy
        );

        float dx = mouse_x - sx;
        float dy = mouse_y - sy;
        float distance = dx * dx + dy * dy;

        if (distance <= closest_distance) {
            closest_distance = distance;
            closest_vertex = i;
        }
    }

    return closest_vertex;
}

static float editor_snap_value(
    float value,
    float grid_size
) {
    if (grid_size <= 0.0f) {
        return value;
    }

    return roundf(value / grid_size) * grid_size;
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

        if (event->button.button == SDL_BUTTON_MIDDLE) {
            editor->panning = true;
        }

        if (event->button.button == SDL_BUTTON_LEFT) {
            float mouse_x = event->button.x;
            float mouse_y = event->button.y;

            float spawn_x, spawn_y;

            world_to_screen(
                editor,
                level->player_spawn.x,
                level->player_spawn.z,
                width,
                height,
                &spawn_x,
                &spawn_y
            );

            float dx = mouse_x - spawn_x;
            float dy = mouse_y - spawn_y;

            bool clicked_spawn =
                dx * dx + dy * dy <= 144.0f;

            editor->spawn_dragging = false;
            editor->vertex_dragging = false;
            editor->spawn_rotating = false;

            if (clicked_spawn) {
                editor->spawn_selected = true;
                editor->selected_vertex = -1;
                editor->spawn_dragging = true;
            } else {
                editor->spawn_selected = false;

                int vertex = editor_find_vertex(
                    editor,
                    level,
                    width,
                    height,
                    mouse_x,
                    mouse_y
                );

                editor->selected_vertex = vertex;

                if (vertex >= 0) {
                    editor->vertex_dragging = true;

                    float world_x, world_z;

                    screen_to_world(
                        editor,
                        width,
                        height,
                        mouse_x,
                        mouse_y,
                        &world_x,
                        &world_z
                    );

                    editor->vertex_drag_offset_x =
                        level->vertices[vertex].x - world_x;

                    editor->vertex_drag_offset_z =
                        level->vertices[vertex].z - world_z;
                }
            }
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {

        if (event->button.button == SDL_BUTTON_MIDDLE) {
            editor->panning = false;
        }

        if (event->button.button == SDL_BUTTON_LEFT) {
            editor->spawn_dragging = false;
            editor->vertex_dragging = false;
        }
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

            if (editor->snap_enabled) {
                level->player_spawn.x = editor_snap_value(
                    level->player_spawn.x,
                    editor->grid_size
                );

                level->player_spawn.z = editor_snap_value(
                    level->player_spawn.z,
                    editor->grid_size
                );
            }
        }
        if (editor->vertex_dragging &&
            editor->selected_vertex >= 0 &&
            editor->selected_vertex < level->vertex_count) {

            float world_x, world_z;

            screen_to_world(
                editor,
                width,
                height,
                event->motion.x,
                event->motion.y,
                &world_x,
                &world_z
            );

            Vertex *vertex =
                &level->vertices[editor->selected_vertex];

            float new_x = world_x + editor->vertex_drag_offset_x;
            float new_z = world_z + editor->vertex_drag_offset_z;

            if (editor->snap_enabled) {
                new_x = editor_snap_value(new_x, editor->grid_size);
                new_z = editor_snap_value(new_z, editor->grid_size);
            }

            vertex->x = new_x;
            vertex->z = new_z;
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

    // Toggle grid snapping with G
    if (event->type == SDL_EVENT_KEY_DOWN &&
        event->key.scancode == SDL_SCANCODE_G &&
        !event->key.repeat) {

        editor->snap_enabled = !editor->snap_enabled;
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

        bool selected = i == editor->selected_vertex;

        if (selected) {
            SDL_SetRenderDrawColor(
                renderer, 255, 220, 90, 255
            );
        } else {
            SDL_SetRenderDrawColor(
                renderer, 90, 205, 245, 255
            );
        }

        float size = selected ? 10.0f : 6.0f;

        SDL_FRect point = {
            x - size * 0.5f,
            y - size * 0.5f,
            size,
            size
        };

        SDL_RenderFillRect(renderer, &point);

        if (selected) {
            SDL_SetRenderDrawColor(
                renderer, 255, 255, 255, 255
            );

            SDL_FRect outline = {
                x - 7.0f,
                y - 7.0f,
                14.0f,
                14.0f
            };

            SDL_RenderRect(renderer, &outline);
        }
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

    const char *lines[] = {
        "Left Drag   Move Vertex/Spawn",
        "Left Click  Select / Deselect",
        "Middle Drag Pan View",
        "Wheel       Zoom",
        "Hold R      Rotate Spawn",
        "[ / ]       Adjust Pitch",
        "PgUp/PgDn   Adjust Height",
        "G           Toggle Grid Snap",
        "Ctrl+S      Save",
        "Esc         Exit"
    };

    const int line_count = sizeof(lines) / sizeof(lines[0]);

    const float padding = 12.0f;
    const float line_height = 15.0f;
    const float header_height = 30.0f;

    // Automatically calculate panel dimensions
    const float panel_height =
        header_height + line_count * line_height + padding;

    float max_text_width = 0.0f;

    for (int i = 0; i < line_count; ++i) {
        float text_width = (float)SDL_strlen(lines[i]) * 8.0f;

        if (text_width > max_text_width)
            max_text_width = text_width;
    }

    const float panel_width = max_text_width + padding * 2.0f;

    const float x = 12.0f;
    const float y = (float)height - panel_height - 12.0f;

    SDL_FRect panel = {
        x, y, panel_width, panel_height
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(renderer, 12, 16, 24, 230);
    SDL_RenderFillRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 95, 110, 135, 255);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 255, 215, 95, 255);
    SDL_RenderDebugText(
        renderer,
        x + padding,
        y + padding,
        "LEVEL EDITOR"
    );

    SDL_SetRenderDrawColor(renderer, 220, 225, 235, 255);

    for (int i = 0; i < line_count; ++i) {
        SDL_RenderDebugText(
            renderer,
            x + padding,
            y + header_height + i * line_height,
            lines[i]
        );
    }
}

static void editor_draw_properties(
    SDL_Renderer *renderer,
    const EditorViewport *editor,
    const Level *level
) {
    int width, height;

    if (!SDL_GetRenderOutputSize(renderer, &width, &height))
        return;

    const float panel_width = 230.0f;
    const float x = (float)width - panel_width - 12.0f;
    const float y = 12.0f;
    const float panel_height = 190.0f;

    SDL_FRect panel = {
        x, y, panel_width, panel_height
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 12, 16, 24, 230);
    SDL_RenderFillRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 95, 110, 135, 255);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 255, 215, 95, 255);
    SDL_RenderDebugText(
        renderer,
        x + 12,
        y + 12,
        "PROPERTIES"
    );

    SDL_SetRenderDrawColor(renderer, 220, 225, 235, 255);

    char text[128];
    float text_y = y + 38.0f;

    if (editor->spawn_selected) {
        const PlayerSpawn *spawn = &level->player_spawn;

        SDL_RenderDebugText(
            renderer, x + 12, text_y, "Player Spawn"
        );
        text_y += 24.0f;

        snprintf(text, sizeof(text), "X:     %.3f", spawn->x);
        SDL_RenderDebugText(renderer, x + 12, text_y, text);
        text_y += 18.0f;

        snprintf(text, sizeof(text), "Y:     %.3f", spawn->y);
        SDL_RenderDebugText(renderer, x + 12, text_y, text);
        text_y += 18.0f;

        snprintf(text, sizeof(text), "Z:     %.3f", spawn->z);
        SDL_RenderDebugText(renderer, x + 12, text_y, text);
        text_y += 18.0f;

        snprintf(text, sizeof(text), "Yaw:   %.1f deg", spawn->yaw);
        SDL_RenderDebugText(renderer, x + 12, text_y, text);
        text_y += 18.0f;

        snprintf(text, sizeof(text), "Pitch: %.1f deg", spawn->pitch);
        SDL_RenderDebugText(renderer, x + 12, text_y, text);
    }
    else if (
        editor->selected_vertex >= 0 &&
        editor->selected_vertex < level->vertex_count
    ) {
        int index = editor->selected_vertex;
        const Vertex *vertex = &level->vertices[index];

        snprintf(text, sizeof(text), "Vertex #%d", index);
        SDL_RenderDebugText(renderer, x + 12, text_y, text);
        text_y += 24.0f;

        snprintf(text, sizeof(text), "X: %.3f", vertex->x);
        SDL_RenderDebugText(renderer, x + 12, text_y, text);
        text_y += 18.0f;

        snprintf(text, sizeof(text), "Z: %.3f", vertex->z);
        SDL_RenderDebugText(renderer, x + 12, text_y, text);
        text_y += 24.0f;

        int connected_walls = 0;

        for (int i = 0; i < level->wall_count; ++i) {
            const Wall *wall = &level->walls[i];

            if (wall->vertex_start == index ||
                wall->vertex_end == index) {
                connected_walls++;
            }
        }

        snprintf(
            text,
            sizeof(text),
            "Connected Walls: %d",
            connected_walls
        );

        SDL_RenderDebugText(renderer, x + 12, text_y, text);
    }
    else {
        SDL_RenderDebugText(
            renderer,
            x + 12,
            text_y,
            "Nothing selected"
        );

        SDL_RenderDebugText(
            renderer,
            x + 12,
            text_y + 22,
            "Click a vertex or spawn"
        );
    }

    char snap_text[64];

    snprintf(
        snap_text,
        sizeof(snap_text),
        "Snap: %s (%.2f)",
        editor->snap_enabled ? "ON" : "OFF",
        editor->grid_size
    );

    SDL_SetRenderDrawColor(renderer, 150, 210, 170, 255);

    SDL_RenderDebugText(
        renderer,
        x + 12,
        y + panel_height - 22,
        snap_text
    );
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
    editor_draw_properties(renderer, editor, level);

    SDL_RenderPresent(renderer);
}