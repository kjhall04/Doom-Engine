#include "editor.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    const char *level_path = "levels/blacksite_12_thick_walls.json";
    if (argc > 1) level_path = argv[1];

    Editor editor;
    if (!editor_init(&editor, level_path, false)) {
        fprintf(stderr, "Editor initialization failed.\n");
        editor_shutdown(&editor);
        return 1;
    }
    editor_run(&editor);
    editor_shutdown(&editor);
    return 0;
}
