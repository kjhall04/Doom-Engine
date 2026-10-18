#ifndef TEXTURE_H
#define TEXTURE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Texture {
    int width;
    int height;
    uint32_t *pixels;
} Texture;

bool texture_load_bmp(Texture *texture, const char *path);

uint32_t texture_sample(
    const Texture *texture,
    float u,
    float v
);

void texture_destroy(Texture *texture);

#endif