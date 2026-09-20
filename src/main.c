#include "game.h"

#include <stdlib.h>

// main to run everything

int main(int argc, char *argv[]) {
    
    Game *game = NULL;

    if (!game_new(&game)) {
        return EXIT_FAILURE;
    }

    game_run(game);

    game_free(&game);

    return EXIT_SUCCESS;
}