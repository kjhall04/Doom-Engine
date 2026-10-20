#include "texture_manager.h"

#include <stddef.h>
#include <string.h>

static bool texture_id_valid(int id) {
    return id >= 0 && id < TEXTURE_MANAGER_CAPACITY;
}

void texture_manager_init(TextureManager *manager) {
    if (!manager) {
        return;
    }

    memset(manager, 0, sizeof(*manager));
}

bool texture_manager_load(
    TextureManager *manager,
    int texture_id,
    const char *path
) {
    if (!manager || !path || !texture_id_valid(texture_id)) {
        return false;
    }

    Texture replacement = {0};

    if (!texture_load_bmp(&replacement, path)) {
        return false;
    }

    if (manager->loaded[texture_id]) {
        texture_destroy(&manager->textures[texture_id]);
    }

    manager->textures[texture_id] = replacement;
    manager->loaded[texture_id] = true;

    return true;
}

const Texture *texture_manager_get(
    const TextureManager *manager,
    int texture_id
) {
    if (!manager ||
        !texture_id_valid(texture_id) ||
        !manager->loaded[texture_id]) {
        return NULL;
    }

    return &manager->textures[texture_id];
}

void texture_manager_destroy(TextureManager *manager) {
    if (!manager) {
        return;
    }

    for (int i = 0; i < TEXTURE_MANAGER_CAPACITY; ++i) {
        if (manager->loaded[i]) {
            texture_destroy(&manager->textures[i]);
        }
    }

    texture_manager_init(manager);
}