#include "renderer.h"
#include "render_walls.h"

void renderer_render(
    SDL_Renderer *renderer,
    Player *player,
    Level *level,
    const TextureManager *textures
) {
    SDL_SetRenderDrawColor(
        renderer,
        20, 20, 28, 255
    );

    SDL_RenderClear(renderer);

    int screen_width;
    int screen_height;

    if (!SDL_GetRenderOutputSize(
        renderer,
        &screen_width,
        &screen_height
    )) {
        SDL_RenderPresent(renderer);
        return;
    }

    render_walls_draw_solid(
        renderer,
        player,
        level,
        textures,
        screen_width,
        screen_height
    );

    SDL_RenderPresent(renderer);
}