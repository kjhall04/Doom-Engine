#include "editor.h"

#include <stdio.h>

int main(int argc, char *argv[]) {
    const char *level_path = "levels/test_level.json";

    if (argc > 1) {
        level_path = argv[1];
    }

    Editor editor = {0};

    if (!editor_init(&editor, level_path)) {
        fprintf(stderr, "Editor initialization failed.\n");
        editor_destroy(&editor);
        return 1;
    }

    editor_run(&editor);
    editor_destroy(&editor);

    return 0;
}