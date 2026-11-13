#ifndef RENDER_GPU_H
#define RENDER_GPU_H
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "engine/level/level.h"
#include "engine/textures/texture_manager.h"
#include "game/player.h"
/* OpenGL 2.1 compatibility backend. Returns false if shaders/GL unavailable. */
bool render_gpu_init(SDL_Window *window, const TextureManager *textures);
void render_gpu_draw(SDL_Window *window, const Player *player, const Level *level);
void render_gpu_shutdown(void);
#endif
