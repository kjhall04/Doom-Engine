#include "texture.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool texture_load_bmp(Texture *texture, const char *path) {
    if (!texture || !path) {
        return false;
    }

    SDL_Surface *loaded = SDL_LoadBMP(path);

    if (!loaded) {
        SDL_Log(
            "Failed to load texture %s: %s",
            path,
            SDL_GetError()
        );

        return false;
    }

    SDL_Surface *converted = SDL_ConvertSurface(
        loaded,
        SDL_PIXELFORMAT_ARGB8888
    );

    SDL_DestroySurface(loaded);

    if (!converted) {
        SDL_Log(
            "Failed to convert texture: %s",
            SDL_GetError()
        );

        return false;
    }

    int width = converted->w;
    int height = converted->h;

    uint32_t *pixels = malloc(
        (size_t)width * (size_t)height * sizeof(uint32_t)
    );

    if (!pixels) {
        SDL_DestroySurface(converted);
        return false;
    }

    for (int y = 0; y < height; ++y) {
        const uint8_t *source_row =
            (const uint8_t *)converted->pixels +
            (size_t)y * converted->pitch;

        memcpy(
            pixels + (size_t)y * width,
            source_row,
            (size_t)width * sizeof(uint32_t)
        );
    }

    SDL_DestroySurface(converted);

    texture->width = width;
    texture->height = height;
    texture->pixels = pixels;

    return true;
}

uint32_t texture_sample(
    const Texture *texture,
    float u,
    float v
) {
    if (!texture || !texture->pixels ||
        texture->width <= 0 || texture->height <= 0) {
        return 0xFFFF00FFu;
    }

    // Repeat texture coordinates.
    u = u - floorf(u);
    v = v - floorf(v);

    int x = (int)(u * texture->width);
    int y = (int)(v * texture->height);

    if (x >= texture->width) {
        x = texture->width - 1;
    }

    if (y >= texture->height) {
        y = texture->height - 1;
    }

    return texture->pixels[
        (size_t)y * texture->width + x
    ];
}

void texture_destroy(Texture *texture) {
    if (!texture) {
        return;
    }

    free(texture->pixels);

    texture->pixels = NULL;
    texture->width = 0;
    texture->height = 0;
}