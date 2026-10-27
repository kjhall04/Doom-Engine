#ifndef TEXTURE_MANAGER_H
#define TEXTURE_MANAGER_H

#include "texture.h"

#define TEXTURE_MANAGER_CAPACITY 64

typedef struct TextureManager {
    Texture textures[TEXTURE_MANAGER_CAPACITY];
    bool loaded[TEXTURE_MANAGER_CAPACITY];
} TextureManager;

void texture_manager_init(TextureManager *manager);

bool texture_manager_load(
    TextureManager *manager,
    int texture_id,
    const char *path
);

const Texture *texture_manager_get(
    const TextureManager *manager,
    int texture_id
);

void texture_manager_destroy(TextureManager *manager);

#endif