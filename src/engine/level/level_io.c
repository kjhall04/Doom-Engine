#include "level_io.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <float.h>
#include <string.h>

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

        /* Optional lighting fields preserve compatibility with older maps. */
        s->light_level = 255;
        s->light_r = s->light_g = s->light_b = 255;
        const char *keys[] = {"light_level", "light_r", "light_g", "light_b"};
        int *values[] = {&s->light_level, &s->light_r, &s->light_g, &s->light_b};
        for (int j = 0; j < 4; ++j) {
            if (cJSON_GetObjectItemCaseSensitive(item, keys[j]) != NULL) {
                if (!read_int(item, keys[j], values[j]) ||
                    *values[j] < 0 || *values[j] > 255) return false;
            }
        }

        if (s->floor_height >= s->ceiling_height ||
            s->floor_texture_id < 0 ||
            s->ceiling_texture_id < 0) {
            return false;
        }
    }

    return true;
}

static bool read_lights(Level *level, const cJSON *array) {
    if (!array) return true; /* old level */
    if (!cJSON_IsArray(array)) return false;
    int count = cJSON_GetArraySize(array);
    if (count < 0 || count > 1024) return false;
    if (count == 0) return true;
    level->lights = calloc((size_t)count, sizeof(PointLight));
    if (!level->lights) return false;
    level->light_count = count;
    for (int i = 0; i < count; ++i) {
        const cJSON *item = cJSON_GetArrayItem(array, i);
        PointLight *l = &level->lights[i];
        if (!cJSON_IsObject(item) ||
            !read_float(item, "x", &l->x) ||
            !read_float(item, "y", &l->y) ||
            !read_float(item, "z", &l->z) ||
            !read_float(item, "radius", &l->radius) ||
            !read_float(item, "intensity", &l->intensity) ||
            !read_int(item, "r", &l->r) ||
            !read_int(item, "g", &l->g) ||
            !read_int(item, "b", &l->b) ||
            l->radius <= 0 || l->radius > 10000 ||
            l->intensity < 0 || l->intensity > 100 ||
            l->r < 0 || l->r > 255 || l->g < 0 || l->g > 255 ||
            l->b < 0 || l->b > 255) return false;
        const cJSON *effect = cJSON_GetObjectItemCaseSensitive(item, "effect");
        if (effect && (!read_int(item, "effect", &l->effect) ||
                       l->effect < 0 || l->effect > 2)) return false;
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
    level_init(&loaded);

    const cJSON *spawn = cJSON_GetObjectItemCaseSensitive(root, "player_spawn");

    if (valid && spawn) {
        valid = cJSON_IsObject(spawn) &&
                read_float(spawn, "x", &loaded.player_spawn.x) &&
                read_float(spawn, "y", &loaded.player_spawn.y) &&
                read_float(spawn, "z", &loaded.player_spawn.z) &&
                read_float(spawn, "yaw", &loaded.player_spawn.yaw) &&
                read_float(spawn, "pitch", &loaded.player_spawn.pitch) &&
                loaded.player_spawn.pitch >= -89.0f &&
                loaded.player_spawn.pitch <= 89.0f;
    }

    if (valid) {
        valid = read_vertices(&loaded, vertices) &&
                read_sectors(&loaded, sectors) &&
                read_walls(&loaded, walls) &&
                read_lights(&loaded, cJSON_GetObjectItemCaseSensitive(root, "lights"));
    }

    const cJSON *ambient=cJSON_GetObjectItemCaseSensitive(root,"ambient");
    if(valid && ambient) {
        if(!cJSON_IsArray(ambient) || cJSON_GetArraySize(ambient)!=3) valid=false;
        else {
            float *channels[]={&loaded.ambient_r,&loaded.ambient_g,&loaded.ambient_b};
            for(int i=0;i<3;i++) {
                const cJSON *v=cJSON_GetArrayItem(ambient,i);
                if(!cJSON_IsNumber(v) || !isfinite(v->valuedouble) ||
                   v->valuedouble<0.0 || v->valuedouble>1.0) valid=false;
                else *channels[i]=(float)v->valuedouble;
            }
        }
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

bool level_save_json(const Level *level, const char *path) {
    if (!level || !path ||
        !level->vertices || !level->walls || !level->sectors ||
        level->vertex_count <= 0 ||
        level->wall_count <= 0 ||
        level->sector_count <= 0) {
        return false;
    }

    // Validate references before writing; never save a level the loader rejects.
    for (int i = 0; i < level->sector_count; ++i) {
        const Sector *sec = &level->sectors[i];
        if (!isfinite(sec->floor_height) || !isfinite(sec->ceiling_height) ||
            sec->floor_height >= sec->ceiling_height ||
            sec->floor_texture_id < 0 || sec->ceiling_texture_id < 0) {
            fprintf(stderr, "Cannot save: invalid sector %d\n", i);
            return false;
        }
    }
    for (int i = 0; i < level->wall_count; ++i) {
        const Wall *w = &level->walls[i];
        if (w->vertex_start < 0 || w->vertex_start >= level->vertex_count ||
            w->vertex_end < 0 || w->vertex_end >= level->vertex_count ||
            w->vertex_start == w->vertex_end ||
            w->front_sector < 0 || w->front_sector >= level->sector_count ||
            w->back_sector < -1 || w->back_sector >= level->sector_count ||
            w->back_sector == w->front_sector || w->texture_id < 0) {
            fprintf(stderr, "Cannot save: invalid wall %d (front=%d back=%d)\n",
                i, w->front_sector, w->back_sector);
            return false;
        }
    }
    for (int i = 0; i < level->vertex_count; ++i) {
        if (!isfinite(level->vertices[i].x) || !isfinite(level->vertices[i].z)) {
            fprintf(stderr, "Cannot save: invalid vertex %d\n", i);
            return false;
        }
    }
    if (!isfinite(level->player_spawn.x) || !isfinite(level->player_spawn.y) ||
        !isfinite(level->player_spawn.z) || !isfinite(level->player_spawn.yaw) ||
        !isfinite(level->player_spawn.pitch) || level->player_spawn.pitch < -89.0f ||
        level->player_spawn.pitch > 89.0f) return false;

    for (int i = 0; i < level->light_count; ++i) {
        const PointLight *l = &level->lights[i];
        if (!isfinite(l->x) || !isfinite(l->y) || !isfinite(l->z) ||
            !isfinite(l->radius) || !isfinite(l->intensity) ||
            l->radius <= 0 || l->radius > 10000 ||
            l->intensity < 0 || l->intensity > 100 ||
            l->r < 0 || l->r > 255 || l->g < 0 || l->g > 255 ||
            l->b < 0 || l->b > 255 || l->effect < 0 || l->effect > 2)
            return false;
    }
    if (level->light_count < 0 || (level->light_count && !level->lights)) return false;
    cJSON *root = cJSON_CreateObject();

    if (!root) return false;

    cJSON *vertices = cJSON_AddArrayToObject(root, "vertices");
    cJSON *walls = cJSON_AddArrayToObject(root, "walls");
    cJSON *sectors = cJSON_AddArrayToObject(root, "sectors");

    bool success =
        cJSON_AddNumberToObject(root, "version", 1) != NULL &&
        cJSON_AddStringToObject(root, "name", "Untitled Level") != NULL &&
        vertices != NULL &&
        walls != NULL &&
        sectors != NULL;

    for (int i = 0; success && i < level->vertex_count; ++i) {
        const Vertex *v = &level->vertices[i];

        cJSON *item = cJSON_CreateObject();

        if (!item) {
            success = false;
            break;
        }

        cJSON_AddItemToArray(vertices, item);

        success =
            cJSON_AddNumberToObject(item, "x", v->x) != NULL &&
            cJSON_AddNumberToObject(item, "z", v->z) != NULL;
    }

    for (int i = 0; success && i < level->wall_count; ++i) {
        const Wall *w = &level->walls[i];

        cJSON *item = cJSON_CreateObject();

        if (!item) {
            success = false;
            break;
        }

        cJSON_AddItemToArray(walls, item);

        success =
            cJSON_AddNumberToObject(item, "vertex_start", w->vertex_start) != NULL &&
            cJSON_AddNumberToObject(item, "vertex_end", w->vertex_end) != NULL &&
            cJSON_AddNumberToObject(item, "front_sector", w->front_sector) != NULL &&
            cJSON_AddNumberToObject(item, "back_sector", w->back_sector) != NULL &&
            cJSON_AddNumberToObject(item, "texture_id", w->texture_id) != NULL;
    }

    for (int i = 0; success && i < level->sector_count; ++i) {
        const Sector *s = &level->sectors[i];

        cJSON *item = cJSON_CreateObject();

        if (!item) {
            success = false;
            break;
        }

        cJSON_AddItemToArray(sectors, item);

        success =
            cJSON_AddNumberToObject(item, "floor_height", s->floor_height) != NULL &&
            cJSON_AddNumberToObject(item, "ceiling_height", s->ceiling_height) != NULL &&
            cJSON_AddNumberToObject(item, "floor_texture_id", s->floor_texture_id) != NULL &&
            cJSON_AddNumberToObject(item, "ceiling_texture_id", s->ceiling_texture_id) != NULL &&
            cJSON_AddNumberToObject(item, "light_level", s->light_level) != NULL &&
            cJSON_AddNumberToObject(item, "light_r", s->light_r) != NULL &&
            cJSON_AddNumberToObject(item, "light_g", s->light_g) != NULL &&
            cJSON_AddNumberToObject(item, "light_b", s->light_b) != NULL;
    }

    cJSON *ambient=cJSON_AddArrayToObject(root,"ambient");
    if(!ambient) success=false;
    else {
        cJSON_AddItemToArray(ambient,cJSON_CreateNumber(level->ambient_r));
        cJSON_AddItemToArray(ambient,cJSON_CreateNumber(level->ambient_g));
        cJSON_AddItemToArray(ambient,cJSON_CreateNumber(level->ambient_b));
    }
    cJSON *lights = cJSON_AddArrayToObject(root, "lights");
    if (!lights) success = false;
    for (int i = 0; success && i < level->light_count; ++i) {
        const PointLight *l = &level->lights[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) { success = false; break; }
        cJSON_AddItemToArray(lights, item);
        success =
            cJSON_AddNumberToObject(item, "x", l->x) != NULL &&
            cJSON_AddNumberToObject(item, "y", l->y) != NULL &&
            cJSON_AddNumberToObject(item, "z", l->z) != NULL &&
            cJSON_AddNumberToObject(item, "radius", l->radius) != NULL &&
            cJSON_AddNumberToObject(item, "intensity", l->intensity) != NULL &&
            cJSON_AddNumberToObject(item, "r", l->r) != NULL &&
            cJSON_AddNumberToObject(item, "g", l->g) != NULL &&
            cJSON_AddNumberToObject(item, "b", l->b) != NULL &&
            cJSON_AddNumberToObject(item, "effect", l->effect) != NULL;
    }

    cJSON *spawn = cJSON_CreateObject();

    if (!spawn) {
        cJSON_Delete(root);
        return false;
    }

    cJSON_AddNumberToObject(spawn, "x", level->player_spawn.x);
    cJSON_AddNumberToObject(spawn, "y", level->player_spawn.y);
    cJSON_AddNumberToObject(spawn, "z", level->player_spawn.z);

    cJSON_AddNumberToObject(spawn, "yaw", level->player_spawn.yaw);
    cJSON_AddNumberToObject(spawn, "pitch", level->player_spawn.pitch);

    cJSON_AddItemToObject(root, "player_spawn", spawn);

    if (!success) {
        cJSON_Delete(root);
        return false;
    }

    char *json = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json) return false;

    FILE *file = fopen(path, "wb");

    if (!file) {
        cJSON_free(json);
        fprintf(stderr, "Cannot save level: %s\n", path);
        return false;
    }

    size_t length = strlen(json);
    size_t written = fwrite(json, 1, length, file);

    bool saved = written == length;

    if (saved) {
        saved = fputc('\n', file) != EOF;
    }

    if (fclose(file) != 0) {
        saved = false;
    }

    if (!saved) {
        fprintf(stderr, "Failed to write level: %s\n", path);
    } else {
        printf("Saved level: %s\n", path);
    }

    cJSON_free(json);
    return saved;
}