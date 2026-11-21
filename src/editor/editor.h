#ifndef EDITOR_H
#define EDITOR_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "editor_model.h"
#include "engine/level/level.h"
#include "ui.h"

/* Layout (logical UI pixels) */
#define LAYOUT_MENU_H 24.0f
#define LAYOUT_STATUS_H 24.0f
#define LAYOUT_TOOLBAR_W 78.0f
#define LAYOUT_INSPECTOR_W 300.0f

#define MAX_UNDO 120
#define MAX_PATH_POINTS 256
#define MAX_PROBLEMS 64
#define MAX_RECENT 6
#define PATH_LEN 512

typedef enum Tool {
    TOOL_SELECT = 0,
    TOOL_DRAW,
    TOOL_RECT,
    TOOL_LIGHT,
    TOOL_SPAWN,
    TOOL_COUNT
} Tool;

typedef enum SelKind {
    SEL_NONE = 0,
    SEL_VERTEX,
    SEL_WALL,
    SEL_SECTOR,
    SEL_LIGHT,
    SEL_SPAWN
} SelKind;

typedef enum PickFilter { PICK_ALL = 0, PICK_POINTS, PICK_WALLS, PICK_ROOMS, PICK_THINGS, PICK_FILTER_COUNT } PickFilter;
typedef enum FillMode { FILL_HEIGHT = 0, FILL_LIGHT, FILL_FLAT, FILL_COUNT } FillMode;

typedef struct Selection {
    SelKind kind;
    int *ids;
    int count, cap;
} Selection;

typedef struct Pick { SelKind kind; int id; } Pick;

typedef enum SnapKind { SNAP_NONE = 0, SNAP_GRID, SNAP_EDGE, SNAP_VERTEX } SnapKind;
typedef struct SnapResult { float x, z; SnapKind kind; int index; } SnapResult;

typedef enum DragMode {
    DRAG_NONE = 0,
    DRAG_PAN,
    DRAG_PRESS,      /* pressed on something, not yet moved */
    DRAG_MOVE,
    DRAG_BOX,
    DRAG_RECT,
    DRAG_AIM,        /* spawn placement / rotation */
} DragMode;

typedef enum PopupKind { POPUP_NONE = 0, POPUP_TEXTURE, POPUP_HELP } PopupKind;

typedef struct Toast {
    char text[200];
    Uint64 until;
    bool error;
} Toast;

typedef struct Editor {
    SDL_Window *window;
    SDL_Renderer *renderer;
    UI ui;
    bool running;

    /* Document */
    Level level;
    char file_path[PATH_LEN];
    char recent[MAX_RECENT][PATH_LEN];
    int recent_count;
    uint64_t saved_fp;
    bool dirty;
    char asset_root[PATH_LEN];

    /* View */
    float cam_x, cam_z, zoom;
    bool snap;
    int grid_index;
    FillMode fill;
    bool show_labels, show_vertices, show_unused, show_radius, show_normals, show_grid;

    /* Tools + selection */
    Tool tool;
    PickFilter filter;
    Selection sel;
    Pick hover;
    SnapResult snap_hint;     /* live snap indicator for the active tool */
    bool snap_hint_valid;

    /* Pointer interaction */
    DragMode drag;
    float press_sx, press_sy;           /* screen */
    float press_wx, press_wz;           /* world */
    float cur_wx, cur_wz;               /* last mouse world position */
    Pick press_pick;
    bool press_was_selected;
    bool press_shift;
    bool pan_with_right;
    float right_press_sx, right_press_sy;
    bool right_pending;
    Level backup;                       /* level at drag start (undo / revert) */
    bool backup_valid;
    unsigned char *move_mask;           /* vertices being moved */
    int move_mask_count;
    float *move_orig;                   /* x,z per vertex (moved) / light / spawn */
    float anchor_x, anchor_z;           /* orig position of the grabbed handle */
    int anchor_vertex;
    bool drag_invalid;
    float drag_dx, drag_dz;
    float rect_x0, rect_z0, rect_x1, rect_z1;
    bool aiming_after_place;

    /* Draw tool */
    Vertex path[MAX_PATH_POINTS];
    int path_n;

    /* New-object defaults (copied from whatever was edited last) */
    Sector tmpl;
    int tmpl_wall_tex;

    /* History */
    Level undo[MAX_UNDO];
    int undo_n;
    Level redo[MAX_UNDO];
    int redo_n;
    uint32_t coalesce_key;
    Uint64 coalesce_time;

    /* Derived caches */
    unsigned char *vused;
    int vused_count;
    Problem problems[MAX_PROBLEMS];
    int problem_count;

    /* Assets */
    SDL_Texture *tex[MODEL_TEXTURE_COUNT];

    /* Panels */
    PopupKind popup;
    int menu_open;                      /* -1 none, else menu index */
    int tex_target;                     /* 0 wall, 1 floor, 2 ceiling */
    float inspector_scroll;
    float inspector_content_h;
    bool problems_open;
    Toast toast;
    bool headless;                      /* tests: skip native dialogs */
    bool show_welcome;
} Editor;

extern const float GRID_SIZES[];
extern const int GRID_SIZE_COUNT;
extern const char *const TEXTURE_NAMES[MODEL_TEXTURE_COUNT];
static inline float editor_grid(const Editor *ed) { return GRID_SIZES[ed->grid_index]; }
static inline float ed_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* editor.c */
bool editor_init(Editor *ed, const char *level_path, bool headless);
void editor_shutdown(Editor *ed);
void editor_frame(Editor *ed);                 /* one iteration: events + draw */
void editor_run(Editor *ed);
void editor_toast(Editor *ed, bool error, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void editor_new(Editor *ed);
void editor_open_path(Editor *ed, const char *path);
bool editor_save(Editor *ed);
void editor_save_as_dialog(Editor *ed);
void editor_open_dialog(Editor *ed);
void editor_play(Editor *ed);
void editor_request_quit(Editor *ed);
void editor_changed(Editor *ed);               /* call after any level mutation */
bool editor_screenshot(Editor *ed, const char *bmp_path);
void editor_handle_event(Editor *ed, SDL_Event *e);
void editor_render(Editor *ed);

bool history_push(Editor *ed);                 /* snapshot BEFORE mutating */
void history_push_snapshot(Editor *ed, Level *snap);   /* takes ownership */
void history_discard_last(Editor *ed);
void history_touch(Editor *ed, uint32_t key);  /* coalesced snapshot for scrubbing */
void editor_undo(Editor *ed);
void editor_redo(Editor *ed);

/* editor_canvas.c */
SDL_FRect canvas_rect(const Editor *ed);
void canvas_event(Editor *ed, SDL_Event *e);
void canvas_draw(Editor *ed);
void canvas_update_cursor(Editor *ed);
void view_fit_all(Editor *ed);
void view_fit_selection(Editor *ed);
void view_center_on(Editor *ed, float x, float z);
void world_to_screen(const Editor *ed, float x, float z, float *sx, float *sy);
void screen_to_world(const Editor *ed, float sx, float sy, float *x, float *z);
void editor_set_tool(Editor *ed, Tool t);
void editor_delete_selection(Editor *ed);
void editor_select_all(Editor *ed);
void editor_nudge(Editor *ed, float dx, float dz);
void editor_clean_up(Editor *ed);
void editor_cycle_grid(Editor *ed, int dir);
void editor_cancel_interaction(Editor *ed);
void editor_select_problem(Editor *ed, const Problem *p);
void editor_duplicate_light(Editor *ed);
void canvas_shutdown(Editor *ed);
void canvas_invalidate(Editor *ed);

void sel_clear(Editor *ed);
void sel_set(Editor *ed, SelKind kind, int id);
void sel_add(Editor *ed, SelKind kind, int id);
bool sel_has(const Editor *ed, SelKind kind, int id);
bool sel_remove(Editor *ed, SelKind kind, int id);

/* editor_panels.c */
void panels_draw(Editor *ed);
bool panels_key(Editor *ed, SDL_Event *e);     /* true if consumed */

#endif
