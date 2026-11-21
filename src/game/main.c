#include "game.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// main to run everything

int main(int argc, char *argv[]) {
    
    Game *game = NULL;

    const char *renderer_mode = "auto";
    const char *level_override = NULL;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--renderer=", 11) == 0)
            renderer_mode = argv[i] + 11;
        else if (strncmp(argv[i], "--level=", 8) == 0)
            level_override = argv[i] + 8;
    }
    if (strcmp(renderer_mode,"auto") && strcmp(renderer_mode,"gpu") && strcmp(renderer_mode,"cpu")) {
        fprintf(stderr,"Invalid renderer mode: %s (use auto, gpu or cpu)\n",renderer_mode);
        return EXIT_FAILURE;
    }
    if (!game_new(&game, renderer_mode, level_override)) {
        return EXIT_FAILURE;
    }

    game_run(game);

    game_free(&game);

    return EXIT_SUCCESS;
}