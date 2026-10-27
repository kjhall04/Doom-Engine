#include "level_io.h"
#include "../external/cJSON/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <float.h>

#define LEVEL_MAX_ELEMENTS 100000

static bool read_float(
    const cJSON *object,
    const char *name,
    float *value
) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(
        object, name
    );

    if (!cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble) ||
        fabs(item->valuedouble) > FLT_MAX) {
        return false;
    }

    *value = (float)item->valuedouble;
    return true;
}

static bool read_int(
    const cJSON *object,
    const char *name,
    int *value
) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(
        object, name
    );

    if (!cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble) ||
        item->valuedouble < INT_MIN ||
        item->valuedouble > INT_MAX ||
        floor(item->valuedouble) != item->valuedouble) {
        return false;
    }

    *value = (int)item->valuedouble;
    return true;
}

static bool read_vertices(
    Level *level,
    const cJSON *array
) {
    level->vertex_count = cJSON_GetArraySize(array);

    level->vertices = calloc(
        (size_t)level->vertex_count,
        sizeof(Vertex)
    );

    if (!level->vertices) return false;

    for (int i = 0; i < level->vertex_count; ++i) {
        const cJSON *item = cJSON_GetArrayItem(array, i);
        Vertex *v = &level->vertices[i];

        if (!cJSON_IsObject(item) ||
            !read_float(item, "x", &v->x) ||
            !read_float(item, "z", &v->z)) {
            return false;
        }
    }

    return true;
}

static bool read_sectors(
    Level *level,
    const cJSON *array
) {
    level->sector_count = cJSON_GetArraySize(array);

    level->sectors = calloc(
        (size_t)level->sector_count,
        sizeof(Sector)
    );

    if (!level->sectors) return false;

    for (int i = 0; i < level->sector_count; ++i) {
        const cJSON *item = cJSON_GetArrayItem(array, i);
        Sector *s = &level->sectors[i];

        if (!cJSON_IsObject(item) ||
            !read_float(item, "floor_height", &s->floor_height) ||
            !read_float(item, "ceiling_height", &s->ceiling_height) ||
            !read_int(item, "floor_texture_id", &s->floor_texture_id) ||
            !read_int(item, "ceiling_texture_id", &s->ceiling_texture_id)) {
            return false;
        }

        if (s->floor_height >= s->ceiling_height ||
            s->floor_texture_id < 0 ||
            s->ceiling_texture_id < 0) {
            return false;
        }
    }

    return true;
}

static bool read_walls(
    Level *level,
    const cJSON *array
) {
    level->wall_count = cJSON_GetArraySize(array);

    level->walls = calloc(
        (size_t)level->wall_count,
        sizeof(Wall)
    );

    if (!level->walls) return false;

    for (int i = 0; i < level->wall_count; ++i) {
        const cJSON *item = cJSON_GetArrayItem(array, i);
        Wall *w = &level->walls[i];

        if (!cJSON_IsObject(item) ||
            !read_int(item, "vertex_start", &w->vertex_start) ||
            !read_int(item, "vertex_end", &w->vertex_end) ||
            !read_int(item, "front_sector", &w->front_sector) ||
            !read_int(item, "back_sector", &w->back_sector) ||
            !read_int(item, "texture_id", &w->texture_id)) {
            return false;
        }

        if (w->vertex_start < 0 ||
            w->vertex_start >= level->vertex_count ||
            w->vertex_end < 0 ||
            w->vertex_end >= level->vertex_count ||
            w->vertex_start == w->vertex_end ||
            w->front_sector < 0 ||
            w->front_sector >= level->sector_count ||
            w->back_sector < -1 ||
            w->back_sector >= level->sector_count ||
            w->back_sector == w->front_sector ||
            w->texture_id < 0) {
            return false;
        }
    }

    return true;
}

bool level_load_json(Level *level, const char *path) {
    if (!level || !path) return false;

    FILE *file = fopen(path, "rb");

    if (!file) {
        fprintf(stderr, "Cannot open level: %s\n", path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    long length = ftell(file);

    if (length <= 0 || length > 16 * 1024 * 1024 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    char *data = malloc((size_t)length + 1);

    if (!data) {
        fclose(file);
        return false;
    }

    size_t bytes = fread(data, 1, (size_t)length, file);
    fclose(file);

    if (bytes != (size_t)length) {
        free(data);
        return false;
    }

    data[length] = '\0';

    cJSON *root = cJSON_ParseWithLength(
        data, (size_t)length
    );

    free(data);

    if (!root) {
        fprintf(stderr, "Invalid JSON: %s\n", path);
        return false;
    }

    const cJSON *vertices = cJSON_GetObjectItemCaseSensitive(
        root, "vertices"
    );
    const cJSON *walls = cJSON_GetObjectItemCaseSensitive(
        root, "walls"
    );
    const cJSON *sectors = cJSON_GetObjectItemCaseSensitive(
        root, "sectors"
    );

    int version = 0;

    bool valid =
        cJSON_IsObject(root) &&
        read_int(root, "version", &version) &&
        version == 1 &&
        cJSON_IsArray(vertices) &&
        cJSON_IsArray(walls) &&
        cJSON_IsArray(sectors);

    if (valid) {
        int nv = cJSON_GetArraySize(vertices);
        int nw = cJSON_GetArraySize(walls);
        int ns = cJSON_GetArraySize(sectors);

        valid = nv >= 3 && nv <= LEVEL_MAX_ELEMENTS &&
                nw >= 3 && nw <= LEVEL_MAX_ELEMENTS &&
                ns >= 1 && ns <= LEVEL_MAX_ELEMENTS;
    }

    Level loaded = {0};

    if (valid) {
        valid = read_vertices(&loaded, vertices) &&
                read_sectors(&loaded, sectors) &&
                read_walls(&loaded, walls);
    }

    cJSON_Delete(root);

    if (!valid) {
        level_free(&loaded);
        fprintf(stderr, "Invalid level data: %s\n", path);
        return false;
    }

    level_free(level);
    *level = loaded;

    printf("Loaded level: %s\n", path);
    printf("Vertices: %d | Walls: %d | Sectors: %d\n",
           level->vertex_count,
           level->wall_count,
           level->sector_count);

    return true;
}