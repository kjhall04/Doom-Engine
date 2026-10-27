#ifndef LEVEL_IO_H
#define LEVEL_IO_H

#include "level.h"
#include <stdbool.h>

bool level_load_json(Level *level, const char *path);

#endif