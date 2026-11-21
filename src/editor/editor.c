#include "editor.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/level/level_io.h"

const float GRID_SIZES[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
const int GRID_SIZE_COUNT = (int)(sizeof(GRID_SIZES) / sizeof(GRID_SIZES[0]));

/* Order must match the texture ids the game loads (game.c). */
const char *const TEXTURE_NAMES[MODEL_TEXTURE_COUNT] = {"brick", "stone", "metal", "floor", "ceiling"};

enum { USER_OPEN_PATH = 1, USER_SAVE_AS_PATH = 2 };

static char g_shot_path[PATH_LEN];
static char g_backed_up[PATH_LEN];     /* file already copied to .bak this session */

/* ===================================================================== */
/* Toasts                                                                */
/* ===================================================================== */

void editor_toast(Editor *ed, bool error, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ed->toast.text, sizeof(ed->toast.text), fmt, ap);
    va_end(ap);
    ed->toast.error = error;
    ed->toast.until = SDL_GetTicks() + (error ? 5200 : 3200);
}

/* ===================================================================== */
/* History                                                               */
/* ===================================================================== */

void history_push_snapshot(Editor *ed, Level *snap) {
    if (ed->undo_n == MAX_UNDO) {
        level_free(&ed->undo[0]);
        memmove(&ed->undo[0], &ed->undo[1], (size_t)(MAX_UNDO - 1) * sizeof(Level));
        ed->undo_n--;
    }
    ed->undo[ed->undo_n++] = *snap;
    memset(snap, 0, sizeof(*snap));
    for (int i = 0; i < ed->redo_n; i++) level_free(&ed->redo[i]);
    ed->redo_n = 0;
    ed->coalesce_key = 0;
}

bool history_push(Editor *ed) {
    Level snap = {0};
    if (!model_copy(&snap, &ed->level)) {
        editor_toast(ed, true, "Out of memory: can't record undo");
        return false;
    }
    history_push_snapshot(ed, &snap);
    return true;
}

void history_discard_last(Editor *ed) {
    if (ed->undo_n > 0) level_free(&ed->undo[--ed->undo_n]);
}

void history_touch(Editor *ed, uint32_t key) {
    Uint64 now = SDL_GetTicks();
    if (key == ed->coalesce_key && now - ed->coalesce_time < 1000) { ed->coalesce_time = now; return; }
    if (!history_push(ed)) return;
    ed->coalesce_key = key;
    ed->coalesce_time = now;
}

static void swap_in(Editor *ed, Level *replacement) {
    level_free(&ed->level);
    ed->level = *replacement;
    memset(replacement, 0, sizeof(*replacement));
    sel_clear(ed);
    ed->path_n = 0;
    ed->coalesce_key = 0;
    editor_changed(ed);
}

void editor_undo(Editor *ed) {
    if (ed->drag != DRAG_NONE) return;
    if (ed->undo_n == 0) { editor_toast(ed, false, "Nothing to undo"); return; }
    Level cur = {0};
    if (model_copy(&cur, &ed->level) && ed->redo_n < MAX_UNDO) ed->redo[ed->redo_n++] = cur;
    else level_free(&cur);
    Level prev = ed->undo[--ed->undo_n];
    memset(&ed->undo[ed->undo_n], 0, sizeof(Level));
    swap_in(ed, &prev);
    editor_toast(ed, false, "Undo");
}

void editor_redo(Editor *ed) {
    if (ed->drag != DRAG_NONE) return;
    if (ed->redo_n == 0) { editor_toast(ed, false, "Nothing to redo"); return; }
    Level cur = {0};
    if (model_copy(&cur, &ed->level) && ed->undo_n < MAX_UNDO) ed->undo[ed->undo_n++] = cur;
    else level_free(&cur);
    Level next = ed->redo[--ed->redo_n];
    memset(&ed->redo[ed->redo_n], 0, sizeof(Level));
    swap_in(ed, &next);
    editor_toast(ed, false, "Redo");
}

static void history_clear(Editor *ed) {
    for (int i = 0; i < ed->undo_n; i++) level_free(&ed->undo[i]);
    for (int i = 0; i < ed->redo_n; i++) level_free(&ed->redo[i]);
    ed->undo_n = ed->redo_n = 0;
    ed->coalesce_key = 0;
}

/* ===================================================================== */
/* Change notification                                                   */
/* ===================================================================== */

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/'), *b = strrchr(p, '\\');
    if (b && (!s || b > s)) s = b;
    return s ? s + 1 : p;
}

static void update_title(Editor *ed) {
    char t[PATH_LEN + 64];
    snprintf(t, sizeof(t), "Level Editor - %s%s", ed->file_path[0] ? base_name(ed->file_path) : "Untitled", ed->dirty ? " *" : "");
    if (ed->window) SDL_SetWindowTitle(ed->window, t);
}

void editor_changed(Editor *ed) {
    Level *l = &ed->level;
    free(ed->vused);
    ed->vused = calloc((size_t)l->vertex_count + 1, 1);
    ed->vused_count = l->vertex_count;
    if (ed->vused) model_vertex_usage(l, ed->vused);
    ed->problem_count = model_validate(l, ed->problems, MAX_PROBLEMS);
    ed->dirty = model_fingerprint(l) != ed->saved_fp;
    canvas_invalidate(ed);

    int limit = ed->sel.kind == SEL_VERTEX ? l->vertex_count : ed->sel.kind == SEL_WALL ? l->wall_count :
                ed->sel.kind == SEL_SECTOR ? l->sector_count : ed->sel.kind == SEL_LIGHT ? l->light_count : 1 << 30;
    for (int i = ed->sel.count - 1; i >= 0 && ed->sel.kind != SEL_SPAWN; i--)
        if (ed->sel.ids[i] >= limit) ed->sel.ids[i] = ed->sel.ids[--ed->sel.count];
    if (ed->sel.count == 0 && ed->sel.kind != SEL_SPAWN) ed->sel.kind = SEL_NONE;
    update_title(ed);
}

/* ===================================================================== */
/* Files                                                                 */
/* ===================================================================== */

static void recent_file(Editor *ed, char *out, size_t n) {
    char *pref = SDL_GetPrefPath("DoomEngine", "LevelEditor");
    if (pref) { snprintf(out, n, "%srecent.txt", pref); SDL_free(pref); }
    else out[0] = 0;
}

static void recent_load(Editor *ed) {
    char f[PATH_LEN];
    recent_file(ed, f, sizeof(f));
    FILE *fp = f[0] ? fopen(f, "r") : NULL;
    if (!fp) return;
    char line[PATH_LEN];
    while (ed->recent_count < MAX_RECENT && fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0]) snprintf(ed->recent[ed->recent_count++], PATH_LEN, "%s", line);
    }
    fclose(fp);
}

static void recent_add(Editor *ed, const char *path) {
    int at = -1;
    for (int i = 0; i < ed->recent_count; i++) if (strcmp(ed->recent[i], path) == 0) at = i;
    if (at < 0) {
        if (ed->recent_count < MAX_RECENT) ed->recent_count++;
        at = ed->recent_count - 1;
    }
    for (int i = at; i > 0; i--) memcpy(ed->recent[i], ed->recent[i - 1], PATH_LEN);
    snprintf(ed->recent[0], PATH_LEN, "%s", path);
    if (ed->headless) return;
    char f[PATH_LEN];
    recent_file(ed, f, sizeof(f));
    FILE *fp = f[0] ? fopen(f, "w") : NULL;
    if (!fp) return;
    for (int i = 0; i < ed->recent_count; i++) fprintf(fp, "%s\n", ed->recent[i]);
    fclose(fp);
}

/* Returns true when it is OK to throw away the current document. */
static bool confirm_discard(Editor *ed) {
    if (!ed->dirty || ed->headless) return true;
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Save"},
        {0, 1, "Don't Save"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 2, "Cancel"},
    };
    SDL_MessageBoxData box = {SDL_MESSAGEBOX_WARNING, ed->window, "Unsaved changes",
                              "This level has unsaved changes.", 3, buttons, NULL};
    int hit = 2;
    if (!SDL_ShowMessageBox(&box, &hit)) return false;
    if (hit == 0) return editor_save(ed) && !ed->dirty;
    return hit == 1;
}

static void reset_document(Editor *ed) {
    history_clear(ed);
    sel_clear(ed);
    ed->path_n = 0;
    ed->drag = DRAG_NONE;
    ed->tmpl = model_default_sector();
    ed->tmpl_wall_tex = 0;
    g_backed_up[0] = 0;
    ed->saved_fp = model_fingerprint(&ed->level);
    editor_changed(ed);
    view_fit_all(ed);
}

void editor_new(Editor *ed) {
    if (!confirm_discard(ed)) return;
    level_free(&ed->level);
    level_init(&ed->level);
    ed->level.player_spawn.y = 0.1f;
    ed->file_path[0] = 0;
    reset_document(ed);
    ed->zoom = 40.0f;
    editor_toast(ed, false, "New level. Press R and drag to sketch your first room.");
}

void editor_open_path(Editor *ed, const char *path) {
    if (!confirm_discard(ed)) return;
    Level loaded;
    level_init(&loaded);
    if (!level_load_json(&loaded, path)) {
        level_free(&loaded);
        editor_toast(ed, true, "Couldn't open %s", base_name(path));
        return;
    }
    level_free(&ed->level);
    ed->level = loaded;
    snprintf(ed->file_path, PATH_LEN, "%s", path);
    recent_add(ed, path);
    reset_document(ed);
    editor_toast(ed, false, "Opened %s (%d rooms, %d walls)", base_name(path), ed->level.sector_count, ed->level.wall_count);
}

static void copy_backup(const char *path) {
    if (strcmp(g_backed_up, path) == 0) return;
    FILE *in = fopen(path, "rb");
    if (!in) return;
    char bak[PATH_LEN + 8];
    snprintf(bak, sizeof(bak), "%s.bak", path);
    FILE *out = fopen(bak, "wb");
    if (out) {
        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
        fclose(out);
    }
    fclose(in);
    snprintf(g_backed_up, PATH_LEN, "%s", path);
}

static bool save_to(Editor *ed, const char *path) {
    copy_backup(path);
    if (!level_save_json(&ed->level, path)) {
        editor_toast(ed, true, "Couldn't save to %s", path);
        return false;
    }
    snprintf(ed->file_path, PATH_LEN, "%s", path);
    ed->saved_fp = model_fingerprint(&ed->level);
    recent_add(ed, path);
    editor_changed(ed);
    int errors = 0;
    for (int i = 0; i < ed->problem_count; i++) if (ed->problems[i].severity == PROBLEM_ERROR) errors++;
    if (errors) editor_toast(ed, true, "Saved %s, but the level has %d error(s). See Problems.", base_name(path), errors);
    else editor_toast(ed, false, "Saved %s", base_name(path));
    return true;
}

bool editor_save(Editor *ed) {
    if (!ed->file_path[0]) {
        if (ed->headless) return false;
        editor_save_as_dialog(ed);
        return false;   /* completes asynchronously */
    }
    return save_to(ed, ed->file_path);
}

static void SDLCALL dialog_done(void *userdata, const char *const *filelist, int filter) {
    (void)filter;
    if (!filelist || !filelist[0]) return;
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_USER;
    e.user.code = (Sint32)(intptr_t)userdata;
    e.user.data1 = SDL_strdup(filelist[0]);
    SDL_PushEvent(&e);
}

static const SDL_DialogFileFilter k_filters[] = {{"Level files (*.json)", "json"}};

void editor_open_dialog(Editor *ed) {
    if (ed->headless) return;
    SDL_ShowOpenFileDialog(dialog_done, (void *)(intptr_t)USER_OPEN_PATH, ed->window, k_filters, 1, "levels/", false);
}

void editor_save_as_dialog(Editor *ed) {
    if (ed->headless) return;
    SDL_ShowSaveFileDialog(dialog_done, (void *)(intptr_t)USER_SAVE_AS_PATH, ed->window, k_filters, 1,
                           ed->file_path[0] ? ed->file_path : "levels/untitled.json");
}

void editor_play(Editor *ed) {
    char *pref = SDL_GetPrefPath("DoomEngine", "LevelEditor");
    if (!pref) { editor_toast(ed, true, "No writable folder for the preview file"); return; }
    char preview[PATH_LEN];
    snprintf(preview, sizeof(preview), "%spreview.json", pref);
    SDL_free(pref);
    if (!level_save_json(&ed->level, preview)) { editor_toast(ed, true, "Couldn't write the preview file"); return; }

    const char *base = SDL_GetBasePath();
    const char *names[] = {"GameEngine.exe", "GameEngine"};
    char exe[PATH_LEN] = {0};
    for (int i = 0; i < 2 && !exe[0]; i++) {
        char cand[PATH_LEN];
        snprintf(cand, sizeof(cand), "%s%s", base ? base : "", names[i]);
        SDL_PathInfo info;
        if (SDL_GetPathInfo(cand, &info) && info.type == SDL_PATHTYPE_FILE) snprintf(exe, sizeof(exe), "%s", cand);
    }
    if (!exe[0]) { editor_toast(ed, true, "GameEngine wasn't found next to the editor. Build it first."); return; }

    char arg[PATH_LEN + 16];
    snprintf(arg, sizeof(arg), "--level=%s", preview);
    const char *args[] = {exe, arg, NULL};
    SDL_Process *p = SDL_CreateProcess(args, false);
    if (!p) { editor_toast(ed, true, "Couldn't start the game: %s", SDL_GetError()); return; }
    SDL_DestroyProcess(p);
    editor_toast(ed, false, "Launched the game with your current (unsaved) level");
}

void editor_request_quit(Editor *ed) {
    if (confirm_discard(ed)) ed->running = false;
}

/* ===================================================================== */
/* Setup / teardown                                                      */
/* ===================================================================== */

static bool file_exists(const char *p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p, &info) && info.type == SDL_PATHTYPE_FILE;
}

static void find_asset_root(Editor *ed) {
    const char *base = SDL_GetBasePath();
    const char *rel[] = {"", "../", "../../", "../../../"};
    ed->asset_root[0] = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 4; i++) {
            char probe[PATH_LEN];
            snprintf(probe, sizeof(probe), "%s%sassets/textures/brick.bmp", pass ? (base ? base : "") : "", rel[i]);
            if (file_exists(probe)) {
                snprintf(ed->asset_root, sizeof(ed->asset_root), "%s%s", pass ? (base ? base : "") : "", rel[i]);
                return;
            }
        }
    }
}

static void load_textures(Editor *ed) {
    for (int i = 0; i < MODEL_TEXTURE_COUNT; i++) {
        char p[PATH_LEN];
        snprintf(p, sizeof(p), "%sassets/textures/%s.bmp", ed->asset_root, TEXTURE_NAMES[i]);
        SDL_Surface *s = SDL_LoadBMP(p);
        if (!s) continue;
        ed->tex[i] = SDL_CreateTextureFromSurface(ed->renderer, s);
        if (ed->tex[i]) SDL_SetTextureScaleMode(ed->tex[i], SDL_SCALEMODE_NEAREST);
        SDL_DestroySurface(s);
    }
}

static void pick_ui_scale(Editor *ed) {
    float s = ed->window ? SDL_GetWindowDisplayScale(ed->window) : 1.0f;
    if (s < 1.0f) s = 1.0f;
    ed->ui.scale = ed_clampf(roundf(s * 4.0f) / 4.0f, 1.0f, 3.0f);
}

bool editor_init(Editor *ed, const char *level_path, bool headless) {
    memset(ed, 0, sizeof(*ed));
    ed->headless = headless;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    ed->window = SDL_CreateWindow("Level Editor", 1600, 900, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!ed->window) { fprintf(stderr, "Window failed: %s\n", SDL_GetError()); return false; }
    SDL_SetWindowMinimumSize(ed->window, 900, 560);
    ed->renderer = SDL_CreateRenderer(ed->window, NULL);
    if (!ed->renderer) { fprintf(stderr, "Renderer failed: %s\n", SDL_GetError()); return false; }
    SDL_SetRenderVSync(ed->renderer, 1);
    ui_init(&ed->ui, ed->window, ed->renderer);
    pick_ui_scale(ed);
    {
        int pw = 0, ph = 0;
        SDL_GetRenderOutputSize(ed->renderer, &pw, &ph);
        ed->ui.width = pw / ed->ui.scale;
        ed->ui.height = ph / ed->ui.scale;
    }

    find_asset_root(ed);
    load_textures(ed);

    ed->snap = true;
    ed->grid_index = 2;
    ed->fill = FILL_HEIGHT;
    ed->show_labels = ed->show_normals = ed->show_grid = true;
    ed->show_radius = false;   /* radii show for the selected/hovered light; View menu shows all */
    ed->show_vertices = false;
    ed->menu_open = -1;
    ed->tool = TOOL_SELECT;
    ed->filter = PICK_ALL;
    ed->problems_open = true;
    ed->zoom = 40.0f;
    ed->tmpl = model_default_sector();
    ed->running = true;
    level_init(&ed->level);
    if (!headless) recent_load(ed);

    bool loaded = false;
    if (level_path && level_path[0]) {
        loaded = level_load_json(&ed->level, level_path);
        if (loaded) { snprintf(ed->file_path, PATH_LEN, "%s", level_path); recent_add(ed, level_path); }
        else {
            level_free(&ed->level);
            level_init(&ed->level);
        }
    }
    if (!loaded) {
        ed->level.player_spawn.y = 0.1f;
        if (level_path && level_path[0]) snprintf(ed->file_path, PATH_LEN, "%s", level_path);   /* will be created on save */
    }
    ed->saved_fp = model_fingerprint(&ed->level);
    editor_changed(ed);
    view_fit_all(ed);
    ed->show_welcome = !loaded;
    if (!loaded && level_path && level_path[0]) editor_toast(ed, true, "%s doesn't exist yet. Starting a new level; Save will create it.", base_name(level_path));
    return true;
}

void editor_shutdown(Editor *ed) {
    history_clear(ed);
    canvas_shutdown(ed);
    for (int i = 0; i < MODEL_TEXTURE_COUNT; i++) if (ed->tex[i]) SDL_DestroyTexture(ed->tex[i]);
    free(ed->vused);
    level_free(&ed->level);
    if (ed->backup_valid) level_free(&ed->backup);
    if (ed->renderer) SDL_DestroyRenderer(ed->renderer);
    if (ed->window) SDL_DestroyWindow(ed->window);
    SDL_Quit();
}

/* ===================================================================== */
/* Frame                                                                 */
/* ===================================================================== */

void editor_handle_event(Editor *ed, SDL_Event *e) {
    switch (e->type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            editor_request_quit(ed);
            return;
        case SDL_EVENT_USER:
            if (e->user.data1) {
                if (e->user.code == USER_OPEN_PATH) editor_open_path(ed, (const char *)e->user.data1);
                else if (e->user.code == USER_SAVE_AS_PATH) {
                    char path[PATH_LEN];
                    snprintf(path, sizeof(path), "%s", (const char *)e->user.data1);
                    size_t n = strlen(path);
                    if (n < 5 || SDL_strcasecmp(path + n - 5, ".json") != 0) snprintf(path + n, sizeof(path) - n, ".json");
                    save_to(ed, path);
                }
                SDL_free(e->user.data1);
            }
            return;
        case SDL_EVENT_DROP_FILE:
            if (e->drop.data) editor_open_path(ed, e->drop.data);
            return;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            return;
        default: break;
    }
    ui_event(&ed->ui, e);
    if (e->type == SDL_EVENT_KEY_DOWN && panels_key(ed, e)) return;
    canvas_event(ed, e);
}

void editor_render(Editor *ed) {
    ui_begin(&ed->ui);
    ui_set_color(&ed->ui, C_BG);
    SDL_RenderClear(ed->renderer);
    canvas_draw(ed);
    panels_draw(ed);
    canvas_update_cursor(ed);
    ui_end(&ed->ui);
    if (g_shot_path[0]) {
        SDL_Surface *s = SDL_RenderReadPixels(ed->renderer, NULL);
        if (s) { SDL_SaveBMP(s, g_shot_path); SDL_DestroySurface(s); }
        g_shot_path[0] = 0;
    }
    SDL_RenderPresent(ed->renderer);
}

bool editor_screenshot(Editor *ed, const char *bmp_path) {
    snprintf(g_shot_path, sizeof(g_shot_path), "%s", bmp_path);
    editor_render(ed);
    return true;
}

void editor_frame(Editor *ed) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) editor_handle_event(ed, &e);
    editor_render(ed);
}

void editor_run(Editor *ed) {
    while (ed->running) {
        editor_frame(ed);
        int vs = 0;
        if (!SDL_GetRenderVSync(ed->renderer, &vs) || vs == 0) SDL_Delay(4);
    }
}
