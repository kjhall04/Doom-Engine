#ifndef LEVEL_IO_H
#define LEVEL_IO_H

#include <stdbool.h>
#include "engine/level/level.h"

bool level_load_json(Level *level, const char *path);
bool level_save_json(const Level *level, const char *path);

#endif