#include "editor.h"

#include <math.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_popup_age = 0;   /* frames since the current popup opened */

/* ===================================================================== */
/* Commands (menus + shortcuts share these)                              */
/* ===================================================================== */

typedef enum Cmd {
    CMD_NONE = 0,
    CMD_NEW, CMD_OPEN, CMD_SAVE, CMD_SAVE_AS, CMD_PLAY, CMD_QUIT,
    CMD_UNDO, CMD_REDO, CMD_SELECT_ALL, CMD_DESELECT, CMD_DELETE, CMD_DUP_LIGHT,
    CMD_SNAP, CMD_LABELS, CMD_NORMALS, CMD_VERTICES, CMD_UNUSED, CMD_RADII, CMD_GRID_LINES,
    CMD_FIT_ALL, CMD_FIT_SEL, CMD_CLEAN_UP, CMD_HELP, CMD_PROBLEMS,
    CMD_GRID_BASE = 100,    /* + index */
    CMD_FILL_BASE = 120,    /* + FillMode */
    CMD_UISCALE_BASE = 130, /* + index */
    CMD_RECENT_BASE = 200   /* + index */
} Cmd;

static const float k_ui_scales[] = {1.0f, 1.5f, 2.0f, 2.5f};

static void cmd_run(Editor *ed, int cmd) {
    if (cmd >= CMD_RECENT_BASE) {
        int i = cmd - CMD_RECENT_BASE;
        if (i >= 0 && i < ed->recent_count) { char p[PATH_LEN]; snprintf(p, sizeof(p), "%s", ed->recent[i]); editor_open_path(ed, p); }
        return;
    }
    if (cmd >= CMD_UISCALE_BASE && cmd < CMD_UISCALE_BASE + 4) { ed->ui.scale = k_ui_scales[cmd - CMD_UISCALE_BASE]; return; }
    if (cmd >= CMD_FILL_BASE && cmd < CMD_FILL_BASE + FILL_COUNT) { ed->fill = (FillMode)(cmd - CMD_FILL_BASE); return; }
    if (cmd >= CMD_GRID_BASE && cmd < CMD_GRID_BASE + GRID_SIZE_COUNT) { ed->grid_index = cmd - CMD_GRID_BASE; return; }
    switch ((Cmd)cmd) {
        case CMD_NEW: editor_new(ed); break;
        case CMD_OPEN: editor_open_dialog(ed); break;
        case CMD_SAVE: editor_save(ed); break;
        case CMD_SAVE_AS: editor_save_as_dialog(ed); break;
        case CMD_PLAY: editor_play(ed); break;
        case CMD_QUIT: editor_request_quit(ed); break;
        case CMD_UNDO: editor_undo(ed); break;
        case CMD_REDO: editor_redo(ed); break;
        case CMD_SELECT_ALL: editor_select_all(ed); break;
        case CMD_DESELECT: sel_clear(ed); break;
        case CMD_DELETE: editor_delete_selection(ed); break;
        case CMD_DUP_LIGHT: editor_duplicate_light(ed); break;
        case CMD_SNAP: ed->snap = !ed->snap; editor_toast(ed, false, ed->snap ? "Snap on (Ctrl = free placement)" : "Snap off"); break;
        case CMD_LABELS: ed->show_labels = !ed->show_labels; break;
        case CMD_NORMALS: ed->show_normals = !ed->show_normals; break;
        case CMD_VERTICES: ed->show_vertices = !ed->show_vertices; break;
        case CMD_UNUSED: ed->show_unused = !ed->show_unused; break;
        case CMD_RADII: ed->show_radius = !ed->show_radius; break;
        case CMD_GRID_LINES: ed->show_grid = !ed->show_grid; break;
        case CMD_FIT_ALL: view_fit_all(ed); break;
        case CMD_FIT_SEL: view_fit_selection(ed); break;
        case CMD_CLEAN_UP: editor_clean_up(ed); break;
        case CMD_HELP: ed->popup = ed->popup == POPUP_HELP ? POPUP_NONE : POPUP_HELP; break;
        case CMD_PROBLEMS: ed->problems_open = true; ed->inspector_scroll = 1e6f; break;
        default: break;
    }
}

bool panels_key(Editor *ed, SDL_Event *e) {
    if (ui_text_editing(&ed->ui)) return false;
    SDL_Keycode k = e->key.key;
    SDL_Keymod mod = ui_mods(&ed->ui);
    bool ctrl = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, shift = (mod & SDL_KMOD_SHIFT) != 0;

    if (ed->popup != POPUP_NONE) {
        if (k == SDLK_ESCAPE || k == SDLK_F1 || (ed->popup == POPUP_HELP && (k == SDLK_RETURN || k == SDLK_SPACE))) ed->popup = POPUP_NONE;
        return true;
    }
    if (ed->menu_open >= 0) {
        if (k == SDLK_ESCAPE) ed->menu_open = -1;
        return true;
    }
    if (ctrl) {
        switch (k) {
            case SDLK_N: cmd_run(ed, CMD_NEW); return true;
            case SDLK_O: cmd_run(ed, CMD_OPEN); return true;
            case SDLK_S: cmd_run(ed, shift ? CMD_SAVE_AS : CMD_SAVE); return true;
            case SDLK_Z: cmd_run(ed, shift ? CMD_REDO : CMD_UNDO); return true;
            case SDLK_Y: cmd_run(ed, CMD_REDO); return true;
            case SDLK_D: cmd_run(ed, CMD_DUP_LIGHT); return true;
            case SDLK_Q: cmd_run(ed, CMD_QUIT); return true;
            default: return false;
        }
    }
    switch (k) {
        case SDLK_F1: cmd_run(ed, CMD_HELP); return true;
        case SDLK_F5: cmd_run(ed, CMD_PLAY); return true;
        case SDLK_HOME: cmd_run(ed, CMD_FIT_ALL); return true;
        case SDLK_F: cmd_run(ed, ed->sel.kind != SEL_NONE ? CMD_FIT_SEL : CMD_FIT_ALL); return true;
        case SDLK_G: cmd_run(ed, CMD_SNAP); return true;
        case SDLK_LEFTBRACKET: editor_cycle_grid(ed, -1); return true;
        case SDLK_RIGHTBRACKET: editor_cycle_grid(ed, 1); return true;
        case SDLK_V: case SDLK_1: editor_set_tool(ed, TOOL_SELECT); return true;
        case SDLK_D: case SDLK_2: editor_set_tool(ed, TOOL_DRAW); return true;
        case SDLK_R: case SDLK_3: editor_set_tool(ed, TOOL_RECT); return true;
        case SDLK_L: case SDLK_4: editor_set_tool(ed, TOOL_LIGHT); return true;
        case SDLK_P: case SDLK_5: editor_set_tool(ed, TOOL_SPAWN); return true;
        case SDLK_EQUALS: case SDLK_KP_PLUS: case SDLK_MINUS: case SDLK_KP_MINUS: {
            SDL_FRect c = canvas_rect(ed);
            float f = (k == SDLK_EQUALS || k == SDLK_KP_PLUS) ? 1.25f : 0.8f;
            ed->zoom = ed_clampf(ed->zoom * f, 3.0f, 600.0f);
            (void)c;
            return true;
        }
        default: return false;
    }
}

/* ===================================================================== */
/* Menu bar                                                              */
/* ===================================================================== */

typedef struct MenuItem {
    char text[PATH_LEN + 16];
    const char *shortcut;
    int cmd;
    bool separator, checked, enabled;
} MenuItem;

typedef struct Menu {
    const char *title;
    MenuItem items[40];
    int count;
} Menu;

static void mi(Menu *m, const char *text, const char *shortcut, int cmd, bool checked, bool enabled) {
    if (m->count >= 40) return;
    MenuItem *it = &m->items[m->count++];
    snprintf(it->text, sizeof(it->text), "%s", text);
    it->shortcut = shortcut; it->cmd = cmd; it->checked = checked; it->enabled = enabled; it->separator = false;
}
static void msep(Menu *m) {
    if (m->count >= 40) return;
    memset(&m->items[m->count], 0, sizeof(MenuItem));
    m->items[m->count++].separator = true;
}

static const char *short_name(const char *p) {
    const char *s = strrchr(p, '/'), *b = strrchr(p, '\\');
    if (b && (!s || b > s)) s = b;
    return s ? s + 1 : p;
}

static void build_menu(Editor *ed, int index, Menu *m) {
    memset(m, 0, sizeof(*m));
    char buf[64];
    switch (index) {
        case 0:
            m->title = "File";
            mi(m, "New", "Ctrl+N", CMD_NEW, false, true);
            mi(m, "Open...", "Ctrl+O", CMD_OPEN, false, true);
            mi(m, "Save", "Ctrl+S", CMD_SAVE, false, true);
            mi(m, "Save As...", "Ctrl+Shift+S", CMD_SAVE_AS, false, true);
            msep(m);
            mi(m, "Test Play in Game", "F5", CMD_PLAY, false, true);
            if (ed->recent_count > 0) {
                msep(m);
                for (int i = 0; i < ed->recent_count; i++) {
                    snprintf(buf, sizeof(buf), "%d  %s", i + 1, short_name(ed->recent[i]));
                    mi(m, buf, NULL, CMD_RECENT_BASE + i, false, true);
                }
            }
            msep(m);
            mi(m, "Quit", "Ctrl+Q", CMD_QUIT, false, true);
            break;
        case 1:
            m->title = "Edit";
            mi(m, "Undo", "Ctrl+Z", CMD_UNDO, false, ed->undo_n > 0);
            mi(m, "Redo", "Ctrl+Shift+Z", CMD_REDO, false, ed->redo_n > 0);
            msep(m);
            mi(m, "Select All", "Ctrl+A", CMD_SELECT_ALL, false, true);
            mi(m, "Deselect", "Esc", CMD_DESELECT, false, ed->sel.kind != SEL_NONE);
            mi(m, "Delete", "Del", CMD_DELETE, false, ed->sel.kind != SEL_NONE && ed->sel.kind != SEL_SPAWN);
            mi(m, "Duplicate Light", "Ctrl+D", CMD_DUP_LIGHT, false, ed->sel.kind == SEL_LIGHT);
            break;
        case 2:
            m->title = "View";
            mi(m, "Zoom to Fit Level", "Home", CMD_FIT_ALL, false, true);
            mi(m, "Zoom to Selection", "F", CMD_FIT_SEL, false, ed->sel.kind != SEL_NONE);
            msep(m);
            mi(m, "Snap to Grid", "G", CMD_SNAP, ed->snap, true);
            for (int i = 0; i < GRID_SIZE_COUNT; i++) {
                snprintf(buf, sizeof(buf), "Grid %g", GRID_SIZES[i]);
                mi(m, buf, i == 2 ? "[ ]" : NULL, CMD_GRID_BASE + i, ed->grid_index == i, true);
            }
            msep(m);
            mi(m, "Grid Lines", NULL, CMD_GRID_LINES, ed->show_grid, true);
            mi(m, "Room Labels", NULL, CMD_LABELS, ed->show_labels, true);
            mi(m, "Wall Direction Ticks", NULL, CMD_NORMALS, ed->show_normals, true);
            mi(m, "Always Show Points", NULL, CMD_VERTICES, ed->show_vertices, true);
            mi(m, "Show Unused Points", NULL, CMD_UNUSED, ed->show_unused, true);
            mi(m, "Show All Light Radii", NULL, CMD_RADII, ed->show_radius, true);
            msep(m);
            mi(m, "Room Colors: Height", NULL, CMD_FILL_BASE + FILL_HEIGHT, ed->fill == FILL_HEIGHT, true);
            mi(m, "Room Colors: Light", NULL, CMD_FILL_BASE + FILL_LIGHT, ed->fill == FILL_LIGHT, true);
            mi(m, "Room Colors: Distinct", NULL, CMD_FILL_BASE + FILL_FLAT, ed->fill == FILL_FLAT, true);
            msep(m);
            for (int i = 0; i < 4; i++) {
                snprintf(buf, sizeof(buf), "Interface Size %d%%", (int)(k_ui_scales[i] * 100));
                mi(m, buf, NULL, CMD_UISCALE_BASE + i, fabsf(ed->ui.scale - k_ui_scales[i]) < 0.01f, true);
            }
            break;
        case 3:
            m->title = "Level";
            mi(m, "Check for Problems", NULL, CMD_PROBLEMS, false, true);
            mi(m, "Clean Up Unused Data", NULL, CMD_CLEAN_UP, false, true);
            break;
        default:
            m->title = "Help";
            mi(m, "Shortcuts and Tips", "F1", CMD_HELP, false, true);
            break;
    }
}

#define MENU_COUNT 5

static float menu_header_x(UI *ui, int i) {
    static const char *titles[MENU_COUNT] = {"File", "Edit", "View", "Level", "Help"};
    float x = 8;
    for (int k = 0; k < i; k++) x += ui_text_w(titles[k]) + 24;
    (void)ui;
    return x;
}

static SDL_FRect g_dropdown;

static void draw_menubar(Editor *ed) {
    UI *ui = &ed->ui;
    static const char *titles[MENU_COUNT] = {"File", "Edit", "View", "Level", "Help"};
    SDL_FRect bar = {0, 0, ui->width, LAYOUT_MENU_H};
    ui_fill(ui, bar, C_PANEL);
    ui_line(ui, 0, LAYOUT_MENU_H - 1, ui->width, LAYOUT_MENU_H - 1, C_BORDER);
    ui_block(ui, bar);
    for (int i = 0; i < MENU_COUNT; i++) {
        float x = menu_header_x(ui, i), w = ui_text_w(titles[i]) + 24;
        SDL_FRect r = {x, 0, w, LAYOUT_MENU_H - 1};
        bool hov = ui_hover(ui, r);
        if (ed->menu_open == i) ui_fill(ui, r, C_WIDGET);
        else if (hov) ui_fill(ui, r, C_PANEL_HI);
        ui_text_center(ui, r, C_TEXT, titles[i]);
        if (hov && ui->pressed && ed->popup == POPUP_NONE) ed->menu_open = ed->menu_open == i ? -1 : i;
        else if (hov && ed->menu_open >= 0 && ed->menu_open != i) ed->menu_open = i;
    }
    /* right side: file name + play button */
    SDL_FRect play = {ui->width - 130, 2, 124, LAYOUT_MENU_H - 5};
    if (ui_button_plain(ui, play, false, "playbtn")) { ed->menu_open = -1; cmd_run(ed, CMD_PLAY); }
    ui_tri(ui, play.x + 10, play.y + 5, play.x + 10, play.y + play.h - 5, play.x + 19, play.y + play.h * 0.5f, C_OK);
    ui_text(ui, play.x + 26, play.y + 7, C_TEXT, "Test Play F5");
    ui_tooltip(ui, play, "Launch the game with the level as it is right now (no save needed)");
    char title[PATH_LEN + 8];
    snprintf(title, sizeof(title), "%s%s", ed->file_path[0] ? short_name(ed->file_path) : "Untitled", ed->dirty ? " *" : "");
    ui_text_right(ui, play.x - 14, 8, ed->dirty ? C_WARN : C_DIM, title);
}

static void draw_dropdown(Editor *ed) {
    if (ed->menu_open < 0) return;
    UI *ui = &ed->ui;
    Menu m;
    build_menu(ed, ed->menu_open, &m);
    float w = 150;
    for (int i = 0; i < m.count; i++) {
        float tw = ui_text_w(m.items[i].text) + 60 + (m.items[i].shortcut ? ui_text_w(m.items[i].shortcut) + 24 : 0);
        if (tw > w) w = tw;
    }
    float h = 8;
    for (int i = 0; i < m.count; i++) h += m.items[i].separator ? 9 : 22;
    SDL_FRect r = {menu_header_x(ui, ed->menu_open), LAYOUT_MENU_H - 1, w, h};
    g_dropdown = r;
    ui_fill(ui, (SDL_FRect){r.x + 3, r.y + 3, r.w, r.h}, UI_RGBA(0, 0, 0, 90));
    ui_fill(ui, r, C_PANEL);
    ui_rect(ui, r, C_HOVER);
    ui_block(ui, r);
    float y = r.y + 4;
    int run = -1;
    for (int i = 0; i < m.count; i++) {
        MenuItem *it = &m.items[i];
        if (it->separator) { ui_line(ui, r.x + 8, y + 4, r.x + r.w - 8, y + 4, C_HOVER); y += 9; continue; }
        SDL_FRect row = {r.x + 2, y, r.w - 4, 22};
        if (ui_menu_item(ui, row, it->text, it->shortcut, it->checked, it->enabled)) run = it->cmd;
        y += 22;
    }
    if (run >= 0) { ed->menu_open = -1; cmd_run(ed, run); return; }
    SDL_FRect bar = {0, 0, ui->width, LAYOUT_MENU_H};
    if (ui->pressed && !ui_point_in(r, ui->mx, ui->my) && !ui_point_in(bar, ui->mx, ui->my)) ed->menu_open = -1;
}

/* ===================================================================== */
/* Toolbar                                                               */
/* ===================================================================== */

static void draw_tool_icon(UI *ui, Tool t, float cx, float cy, UIColor col) {
    switch (t) {
        case TOOL_SELECT:
            ui_tri(ui, cx - 6, cy - 10, cx - 6, cy + 7, cx + 6, cy + 1, col);
            ui_thick_line(ui, cx - 1, cy + 3, cx + 5, cy + 11, 3, col);
            break;
        case TOOL_DRAW:
            ui_thick_line(ui, cx - 10, cy + 8, cx - 4, cy - 6, 2, col);
            ui_thick_line(ui, cx - 4, cy - 6, cx + 4, cy + 3, 2, col);
            ui_thick_line(ui, cx + 4, cy + 3, cx + 10, cy - 8, 2, col);
            for (int i = 0; i < 4; i++) {
                float px[4] = {cx - 10, cx - 4, cx + 4, cx + 10}, py[4] = {cy + 8, cy - 6, cy + 3, cy - 8};
                ui_fill(ui, (SDL_FRect){px[i] - 2.5f, py[i] - 2.5f, 5, 5}, col);
            }
            break;
        case TOOL_RECT:
            ui_rect(ui, (SDL_FRect){cx - 10, cy - 7, 20, 14}, col);
            ui_rect(ui, (SDL_FRect){cx - 9, cy - 6, 18, 12}, col);
            for (int i = 0; i < 4; i++) ui_fill(ui, (SDL_FRect){cx + (i & 1 ? 8 : -12), cy + (i & 2 ? 5 : -9), 4, 4}, col);
            break;
        case TOOL_LIGHT:
            for (int k = 0; k < 8; k++) {
                float a = k * 0.7853982f;
                ui_thick_line(ui, cx + cosf(a) * 7, cy + sinf(a) * 7, cx + cosf(a) * 11, cy + sinf(a) * 11, 2, col);
            }
            ui_disc(ui, cx, cy, 5, col);
            break;
        case TOOL_SPAWN:
            ui_circle(ui, cx, cy + 1, 9, col);
            ui_tri(ui, cx, cy - 6, cx - 5, cy + 5, cx + 5, cy + 5, col);
            break;
        default: break;
    }
}

static void draw_toolbar(Editor *ed) {
    UI *ui = &ed->ui;
    SDL_FRect bar = {0, LAYOUT_MENU_H, LAYOUT_TOOLBAR_W, ui->height - LAYOUT_MENU_H - LAYOUT_STATUS_H};
    ui_fill(ui, bar, C_PANEL);
    ui_line(ui, bar.x + bar.w - 1, bar.y, bar.x + bar.w - 1, bar.y + bar.h, C_BORDER);
    ui_block(ui, bar);

    static const char *names[TOOL_COUNT] = {"Select", "Draw", "Rect", "Light", "Start"};
    static const char *keys[TOOL_COUNT] = {"V", "D", "R", "L", "P"};
    static const char *tips[TOOL_COUNT] = {
        "Select (V): click to select, drag to move, drag empty space to box-select",
        "Draw (D): click corners to sketch any room shape",
        "Rectangle (R): drag to sketch a rectangular room",
        "Light (L): click to drop a light",
        "Player Start (P): click to place, drag to aim",
    };
    float y = bar.y + 8;
    for (int t = 0; t < TOOL_COUNT; t++) {
        SDL_FRect r = {6, y, LAYOUT_TOOLBAR_W - 12, 56};
        bool active = (int)ed->tool == t;
        char id[24];
        snprintf(id, sizeof(id), "tool%d", t);
        if (ui_button_plain(ui, r, active, id)) editor_set_tool(ed, (Tool)t);
        draw_tool_icon(ui, (Tool)t, r.x + r.w * 0.5f, r.y + 19, C_TEXT);
        ui_text_center(ui, (SDL_FRect){r.x, r.y + 36, r.w, 12}, C_TEXT, names[t]);
        ui_text(ui, r.x + 4, r.y + 3, active ? UI_RGB(200, 225, 255) : C_DIM, keys[t]);
        ui_tooltip(ui, r, tips[t]);
        y += 60;
    }
    y += 6;
    ui_line(ui, 10, y, LAYOUT_TOOLBAR_W - 10, y, C_HOVER);
    y += 10;

    SDL_FRect snap = {6, y, LAYOUT_TOOLBAR_W - 12, 24};
    if (ui_button_plain(ui, snap, ed->snap, "snapbtn")) cmd_run(ed, CMD_SNAP);
    ui_text_center(ui, snap, C_TEXT, ed->snap ? "Snap ON" : "Snap off");
    ui_tooltip(ui, snap, "Snap to grid, points and walls (G). Hold Ctrl for free placement.");
    y += 30;
    ui_text_center(ui, (SDL_FRect){0, y, LAYOUT_TOOLBAR_W, 10}, C_DIM, "Grid");
    y += 13;
    SDL_FRect gm = {6, y, 20, 22}, gp = {LAYOUT_TOOLBAR_W - 26, y, 20, 22}, gv = {28, y, LAYOUT_TOOLBAR_W - 56, 22};
    if (ui_button(ui, gm, "-", false)) editor_cycle_grid(ed, -1);
    if (ui_button(ui, gp, "+", false)) editor_cycle_grid(ed, 1);
    char gb[16];
    snprintf(gb, sizeof(gb), "%g", editor_grid(ed));
    ui_fill(ui, gv, C_BG);
    ui_text_center(ui, gv, C_TEXT, gb);
    ui_tooltip(ui, gv, "Grid size in world units ([ and ] change it)");
    y += 32;
    ui_line(ui, 10, y, LAYOUT_TOOLBAR_W - 10, y, C_HOVER);
    y += 10;
    SDL_FRect un = {6, y, 32, 24}, re = {40, y, 32, 24};
    if (ui_button(ui, un, "Un", false)) editor_undo(ed);
    if (ui_button(ui, re, "Re", false)) editor_redo(ed);
    ui_tooltip(ui, un, "Undo (Ctrl+Z)");
    ui_tooltip(ui, re, "Redo (Ctrl+Shift+Z)");
    y += 30;
    SDL_FRect fit = {6, y, LAYOUT_TOOLBAR_W - 12, 24};
    if (ui_button(ui, fit, "Fit all", false)) view_fit_all(ed);
    ui_tooltip(ui, fit, "Zoom to show the whole level (Home)");
}

/* ===================================================================== */
/* Floating controls over the canvas                                     */
/* ===================================================================== */

static void draw_canvas_controls(Editor *ed) {
    UI *ui = &ed->ui;
    SDL_FRect c = canvas_rect(ed);
    if (ed->tool == TOOL_SELECT) {
        static const char *labels[PICK_FILTER_COUNT] = {"All", "Points", "Walls", "Rooms", "Things"};
        SDL_FRect r = {c.x + 10, c.y + 10, 330, 22};
        ui_fill(ui, (SDL_FRect){r.x - 6, r.y - 4, r.w + 12 + 44, r.h + 8}, UI_RGBA(10, 11, 14, 190));
        ui_block(ui, (SDL_FRect){r.x - 6, r.y - 4, r.w + 12 + 44, r.h + 8});
        ui_text(ui, r.x, r.y + 7, C_DIM, "Pick");
        SDL_FRect seg = {r.x + 40, r.y, r.w, r.h};
        int f = (int)ed->filter;
        if (ui_segmented(ui, seg, "pickfilter", labels, PICK_FILTER_COUNT, &f)) ed->filter = (PickFilter)f;
        ui_tooltip(ui, seg, "What a click picks. 'All' prefers lights/start, then points, walls, then rooms.");
    }
    static const char *fills[FILL_COUNT] = {"Height", "Light", "Distinct"};
    SDL_FRect box = {c.x + c.w - 290, c.y + 10, 280, 22};
    ui_fill(ui, (SDL_FRect){box.x - 6, box.y - 4, box.w + 12, box.h + 8}, UI_RGBA(10, 11, 14, 190));
    ui_block(ui, (SDL_FRect){box.x - 6, box.y - 4, box.w + 12, box.h + 8});
    ui_text(ui, box.x, box.y + 7, C_DIM, "Colors");
    int f = (int)ed->fill;
    if (ui_segmented(ui, (SDL_FRect){box.x + 56, box.y, box.w - 56, box.h}, "fillmode", fills, FILL_COUNT, &f)) ed->fill = (FillMode)f;
}

/* ===================================================================== */
/* Status bar                                                            */
/* ===================================================================== */

static const char *status_hint(Editor *ed) {
    if (ed->drag == DRAG_MOVE) return "Release to drop. Shift locks an axis, Ctrl is free. Drop on a point or wall to join. Esc cancels.";
    if (ed->drag == DRAG_BOX) return "Release to select everything inside the box. Hold Shift to add to the selection.";
    switch (ed->tool) {
        case TOOL_SELECT:
            if (ed->hover.kind == SEL_WALL) return "Wall: click to select, drag to move it, double-click to split it in two.";
            if (ed->hover.kind == SEL_VERTEX) return "Point: drag to move. Drop on another point or wall to join them.";
            if (ed->hover.kind == SEL_SECTOR) return "Room: click to select and edit. Drag to move the whole room.";
            if (ed->hover.kind == SEL_LIGHT) return "Light: click to select, drag to move.";
            if (ed->hover.kind == SEL_SPAWN) return "Player start: drag to move. Select it to rotate with the handle.";
            return "Click to select. Drag empty space for a box. Right-drag to pan, wheel to zoom.";
        case TOOL_DRAW:
            return ed->path_n == 0 ? "Click to place the first corner. Snaps to points, walls and grid, so rooms connect automatically."
                                   : "Next corner. Click the green start or Enter to close. Backspace undoes. Dbl-click ends a line. Esc cancels.";
        case TOOL_RECT: return "Drag out a rectangular room. Hold Shift for a square. It snaps to neighbouring walls.";
        case TOOL_LIGHT: return "Click to place a light, or drag one. Right-click returns to Select.";
        case TOOL_SPAWN: return "Click where the player starts, then drag to choose where they face.";
        default: return "";
    }
}

static void draw_status(Editor *ed) {
    UI *ui = &ed->ui;
    SDL_FRect bar = {0, ui->height - LAYOUT_STATUS_H, ui->width, LAYOUT_STATUS_H};
    ui_fill(ui, bar, C_PANEL);
    ui_line(ui, 0, bar.y, ui->width, bar.y, C_BORDER);
    ui_block(ui, bar);
    float ty = bar.y + 8;

    /* right cluster */
    float x = ui->width - 12;
    int errors = 0, warns = 0;
    for (int i = 0; i < ed->problem_count; i++) {
        if (ed->problems[i].severity == PROBLEM_ERROR) errors++;
        else if (ed->problems[i].severity == PROBLEM_WARNING) warns++;
    }
    char buf[96];
    if (errors || warns) snprintf(buf, sizeof(buf), "%d error%s, %d warning%s", errors, errors == 1 ? "" : "s", warns, warns == 1 ? "" : "s");
    else snprintf(buf, sizeof(buf), "No problems");
    float w = ui_text_w(buf) + 16;
    SDL_FRect pr = {x - w, bar.y + 2, w, LAYOUT_STATUS_H - 4};
    if (ui_button_plain(ui, pr, false, "problems")) cmd_run(ed, CMD_PROBLEMS);
    ui_text_center(ui, pr, errors ? C_ERR : warns ? C_WARN : C_OK, buf);
    ui_tooltip(ui, pr, "Click to see what's wrong with the level");
    x = pr.x - 16;
    snprintf(buf, sizeof(buf), "Zoom %d%%", (int)(ed->zoom / 40.0f * 100.0f));
    ui_text_right(ui, x, ty, C_DIM, buf);
    x -= ui_text_w(buf) + 16;
    snprintf(buf, sizeof(buf), "Grid %g%s", editor_grid(ed), ed->snap ? "" : " (snap off)");
    ui_text_right(ui, x, ty, C_DIM, buf);
    x -= ui_text_w(buf) + 16;
    snprintf(buf, sizeof(buf), "X %7.2f  Z %7.2f", ed->cur_wx, ed->cur_wz);
    ui_text_right(ui, x, ty, C_TEXT, buf);
    x -= ui_text_w(buf) + 16;

    ui_clip(ui, (SDL_FRect){8, bar.y, x - 8, bar.h});
    ui_text(ui, 10, ty, C_DIM, status_hint(ed));
    ui_unclip(ui);
}

/* ===================================================================== */
/* Toasts                                                                */
/* ===================================================================== */

static void draw_toast(Editor *ed) {
    UI *ui = &ed->ui;
    if (SDL_GetTicks() >= ed->toast.until || !ed->toast.text[0]) return;
    float w = ui_text_w(ed->toast.text) + 28;
    float maxw = ui->width - LAYOUT_TOOLBAR_W - LAYOUT_INSPECTOR_W - 40;
    if (w > maxw) w = maxw;
    SDL_FRect r = {LAYOUT_TOOLBAR_W + (ui->width - LAYOUT_TOOLBAR_W - LAYOUT_INSPECTOR_W - w) * 0.5f, ui->height - LAYOUT_STATUS_H - 44, w, 28};
    ui_fill(ui, (SDL_FRect){r.x + 2, r.y + 3, r.w, r.h}, UI_RGBA(0, 0, 0, 90));
    ui_fill(ui, r, UI_RGBA(14, 15, 19, 245));
    ui_rect(ui, r, ed->toast.error ? C_ERR : C_ACCENT);
    ui_clip(ui, r);
    ui_text(ui, r.x + 14, r.y + 10, ed->toast.error ? UI_RGB(255, 170, 170) : C_TEXT, ed->toast.text);
    ui_unclip(ui);
}

/* ===================================================================== */
/* Texture swatches + picker popup                                       */
/* ===================================================================== */

static void draw_texture_thumb(Editor *ed, int id, SDL_FRect r) {
    UI *ui = &ed->ui;
    if (id >= 0 && id < MODEL_TEXTURE_COUNT && ed->tex[id]) {
        SDL_RenderTexture(ed->renderer, ed->tex[id], NULL, &r);
    } else {
        ui_fill(ui, r, UI_RGB(70, 40, 80));
        ui_text_center(ui, r, C_TEXT, id < 0 ? "mix" : "?");
    }
    ui_rect(ui, r, C_BORDER);
}

static void apply_texture(Editor *ed, int tex) {
    Level *l = &ed->level;
    history_push(ed);
    int target = ed->tex_target;
    if (target == 0 && ed->sel.kind == SEL_WALL) {
        for (int i = 0; i < ed->sel.count; i++) if (ed->sel.ids[i] < l->wall_count) l->walls[ed->sel.ids[i]].texture_id = tex;
        ed->tmpl_wall_tex = tex;
    } else if ((target == 1 || target == 2) && ed->sel.kind == SEL_SECTOR) {
        for (int i = 0; i < ed->sel.count; i++) {
            if (ed->sel.ids[i] >= l->sector_count) continue;
            if (target == 1) l->sectors[ed->sel.ids[i]].floor_texture_id = tex;
            else l->sectors[ed->sel.ids[i]].ceiling_texture_id = tex;
        }
        if (ed->sel.count > 0 && ed->sel.ids[0] < l->sector_count) ed->tmpl = l->sectors[ed->sel.ids[0]];
    } else if (target == 3 && ed->sel.kind == SEL_SECTOR) {   /* walls of the selected rooms */
        unsigned char *ss = calloc((size_t)l->sector_count + 1, 1);
        if (ss) {
            for (int i = 0; i < ed->sel.count; i++) if (ed->sel.ids[i] < l->sector_count) ss[ed->sel.ids[i]] = 1;
            for (int w = 0; w < l->wall_count; w++) {
                int f = l->walls[w].front_sector, b = l->walls[w].back_sector;
                bool solid = f < 0 || b < 0;
                if (solid && ((f >= 0 && f < l->sector_count && ss[f]) || (b >= 0 && b < l->sector_count && ss[b]))) l->walls[w].texture_id = tex;
            }
            free(ss);
        }
        ed->tmpl_wall_tex = tex;
    }
    editor_changed(ed);
}

static void draw_texture_popup(Editor *ed) {
    UI *ui = &ed->ui;
    ui_fill(ui, (SDL_FRect){0, 0, ui->width, ui->height}, UI_RGBA(0, 0, 0, 150));
    float cell = 96, gap = 10;
    float w = MODEL_TEXTURE_COUNT * cell + (MODEL_TEXTURE_COUNT + 1) * gap, h = 190;
    SDL_FRect r = {(ui->width - w) * 0.5f, (ui->height - h) * 0.5f, w, h};
    ui_fill(ui, r, C_PANEL);
    ui_rect(ui, r, C_ACCENT);
    static const char *what[4] = {"wall", "floor", "ceiling", "walls of the selected rooms"};
    char title[96];
    snprintf(title, sizeof(title), "Choose a texture for the %s", what[ed->tex_target & 3]);
    ui_text(ui, r.x + gap + 4, r.y + 14, C_TEXT, title);
    ui_text_right(ui, r.x + r.w - gap - 4, r.y + 14, C_DIM, "Esc to cancel");

    int current = -1;
    const Level *l = &ed->level;
    if (ed->sel.count > 0) {
        int id = ed->sel.ids[0];
        if (ed->tex_target == 0 && ed->sel.kind == SEL_WALL && id < l->wall_count) current = l->walls[id].texture_id;
        if (ed->tex_target == 1 && ed->sel.kind == SEL_SECTOR && id < l->sector_count) current = l->sectors[id].floor_texture_id;
        if (ed->tex_target == 2 && ed->sel.kind == SEL_SECTOR && id < l->sector_count) current = l->sectors[id].ceiling_texture_id;
    }
    for (int i = 0; i < MODEL_TEXTURE_COUNT; i++) {
        SDL_FRect c = {r.x + gap + i * (cell + gap), r.y + 44, cell, 130};
        char id[24];
        snprintf(id, sizeof(id), "texpick%d", i);
        if (ui_button_plain(ui, c, current == i, id)) { apply_texture(ed, i); ed->popup = POPUP_NONE; return; }
        draw_texture_thumb(ed, i, (SDL_FRect){c.x + 8, c.y + 8, cell - 16, cell - 16});
        ui_text_center(ui, (SDL_FRect){c.x, c.y + cell, c.w, 10}, C_TEXT, TEXTURE_NAMES[i]);
        char n[8];
        snprintf(n, sizeof(n), "id %d", i);
        ui_text_center(ui, (SDL_FRect){c.x, c.y + cell + 14, c.w, 10}, C_DIM, n);
    }
    if (g_popup_age > 0 && ui->pressed && !ui_point_in(r, ui->mx, ui->my)) ed->popup = POPUP_NONE;
}

/* ===================================================================== */
/* Help overlay                                                          */
/* ===================================================================== */

static void draw_help(Editor *ed) {
    UI *ui = &ed->ui;
    ui_fill(ui, (SDL_FRect){0, 0, ui->width, ui->height}, UI_RGBA(0, 0, 0, 170));
    float w = 1000, h = 440;
    if (w > ui->width - 20) w = ui->width - 20;
    SDL_FRect r = {(ui->width - w) * 0.5f, (ui->height - h) * 0.5f, w, h};
    ui_fill(ui, r, C_PANEL);
    ui_rect(ui, r, C_ACCENT);
    ui_text(ui, r.x + 18, r.y + 16, C_TEXT, "Level Editor: quick guide");
    ui_text_right(ui, r.x + r.w - 18, r.y + 16, C_DIM, "Esc or F1 to close");

    struct Row { const char *k, *d; };
    static const struct Row left[] = {
        {"", "MAKING ROOMS"},
        {"R + drag", "Sketch a rectangular room"},
        {"D + click", "Draw any shape (Enter or click start closes)"},
        {"Shift", "Lock a line to 45 degree steps"},
        {"", "Rooms that share an edge connect on their own."},
        {"", "Draw inside a room to make a platform or pit."},
        {"", "Draw a line across a room to divide it."},
        {"", ""},
        {"", "SELECTING AND EDITING"},
        {"Click", "Select a point, wall, room or light"},
        {"Shift+click", "Add to / remove from the selection"},
        {"Drag box", "Select everything inside the box"},
        {"Drag", "Move the selection; Shift locks an axis"},
        {"Double-click", "Split a wall in two"},
        {"Delete", "Remove (deleting a wall merges rooms)"},
        {"Arrows", "Nudge one grid step (Alt = fine)"},
    };
    static const struct Row right[] = {
        {"", "VIEW"},
        {"Wheel", "Zoom toward the cursor"},
        {"Right-drag", "Pan (or middle-drag, Space+drag)"},
        {"Home / F", "Fit level / fit selection"},
        {"G", "Snap on/off (Ctrl = free placement)"},
        {"[ and ]", "Smaller / larger grid"},
        {"", ""},
        {"", "TOOLS"},
        {"V D R L P", "Select Draw Rect Light Player-start"},
        {"Right-click", "Back out one step / return to Select"},
        {"Esc", "Cancel, then deselect"},
        {"", ""},
        {"", "FILE"},
        {"Ctrl+Z / Y", "Undo / redo"},
        {"Ctrl+S", "Save (keeps a .bak of the old file)"},
        {"F5", "Test play the level in the game"},
    };
    float ytop = r.y + 52;
    for (int col = 0; col < 2; col++) {
        const struct Row *rows = col == 0 ? left : right;
        int n = col == 0 ? (int)(sizeof(left) / sizeof(left[0])) : (int)(sizeof(right) / sizeof(right[0]));
        float x = r.x + 28 + col * (w * 0.5f), y = ytop;
        for (int i = 0; i < n; i++) {
            if (!rows[i].k[0]) ui_text(ui, x, y, rows[i].d[0] && rows[i].d[1] >= 'A' && rows[i].d[1] <= 'Z' && rows[i].d[0] >= 'A' && rows[i].d[0] <= 'Z' ? C_ACCENT : C_DIM, rows[i].d);
            else {
                ui_text(ui, x, y, C_WARN, rows[i].k);
                ui_text(ui, x + 120, y, C_TEXT, rows[i].d);
            }
            y += 20;
        }
    }
    ui_text_center(ui, (SDL_FRect){r.x, r.y + r.h - 30, r.w, 12}, C_DIM, "Tip: the Problems list (bottom right) tells you if a room isn't closed or walls cross.");
    if (g_popup_age > 0 && ui->pressed && !ui_point_in(r, ui->mx, ui->my)) ed->popup = POPUP_NONE;
}

/* ===================================================================== */
/* Inspector                                                             */
/* ===================================================================== */

#define PAD 12.0f

typedef struct Pn {
    Editor *ed;
    UI *ui;
    float x, w, y;
} Pn;

static void pn_section(Pn *p, const char *title) {
    p->y += 6;
    ui_fill(p->ui, (SDL_FRect){p->x + 6, p->y, p->w - 12, 20}, C_PANEL_HI);
    ui_text(p->ui, p->x + PAD, p->y + 6, C_ACCENT, title);
    p->y += 26;
}

static SDL_FRect pn_field(Pn *p, const char *label) {
    ui_text(p->ui, p->x + PAD, p->y + 7, C_DIM, label);
    SDL_FRect r = {p->x + 108, p->y, p->w - 108 - PAD, 22};
    p->y += 28;
    return r;
}

static void pn_line(Pn *p, UIColor c, const char *text) {
    ui_text(p->ui, p->x + PAD, p->y, c, text);
    p->y += 16;
}

static void pn_linef(Pn *p, UIColor c, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void pn_linef(Pn *p, UIColor c, const char *fmt, ...) {
    char buf[160];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    pn_line(p, c, buf);
}

static float pn_wrap(Pn *p, float x, UIColor c, const char *text, float width) {
    int per = (int)(width / UI_CHAR_W);
    if (per < 8) per = 8;
    float y = p->y;
    const char *s = text;
    while (*s) {
        int len = (int)strlen(s);
        if (len > per) {
            int cut = per;
            while (cut > 0 && s[cut] != ' ') cut--;
            if (cut == 0) cut = per;
            len = cut;
        }
        char line[128];
        int n = len < 127 ? len : 127;
        memcpy(line, s, (size_t)n);
        line[n] = 0;
        ui_text(p->ui, x, y, c, line);
        y += 12;
        s += len;
        while (*s == ' ') s++;
    }
    return y - p->y;
}

/* --- generic access to the selected items' fields ---------------------- */
static void *item_field(Editor *ed, int id, size_t off) {
    Level *l = &ed->level;
    switch (ed->sel.kind) {
        case SEL_SECTOR: if (id >= 0 && id < l->sector_count) return (char *)&l->sectors[id] + off; break;
        case SEL_LIGHT: if (id >= 0 && id < l->light_count) return (char *)&l->lights[id] + off; break;
        case SEL_WALL: if (id >= 0 && id < l->wall_count) return (char *)&l->walls[id] + off; break;
        default: break;
    }
    return NULL;
}

static void gather_f(Editor *ed, size_t off, float *v, bool *mixed) {
    bool first = true;
    *mixed = false; *v = 0;
    for (int i = 0; i < ed->sel.count; i++) {
        float *f = item_field(ed, ed->sel.ids[i], off);
        if (!f) continue;
        if (first) { *v = *f; first = false; } else if (*f != *v) *mixed = true;
    }
}
static void gather_i(Editor *ed, size_t off, int *v, bool *mixed) {
    bool first = true;
    *mixed = false; *v = 0;
    for (int i = 0; i < ed->sel.count; i++) {
        int *f = item_field(ed, ed->sel.ids[i], off);
        if (!f) continue;
        if (first) { *v = *f; first = false; } else if (*f != *v) *mixed = true;
    }
}

static void after_item_edit(Editor *ed) {
    const Level *l = &ed->level;
    if (ed->sel.kind == SEL_SECTOR && ed->sel.count > 0 && ed->sel.ids[0] < l->sector_count) ed->tmpl = l->sectors[ed->sel.ids[0]];
    if (ed->sel.kind == SEL_WALL && ed->sel.count > 0 && ed->sel.ids[0] < l->wall_count) ed->tmpl_wall_tex = l->walls[ed->sel.ids[0]].texture_id;
    editor_changed(ed);
}

static void apply_f(Editor *ed, size_t off, float v) {
    history_touch(ed, 0x2000u + (uint32_t)ed->sel.kind * 0x400u + (uint32_t)off);
    for (int i = 0; i < ed->sel.count; i++) { float *f = item_field(ed, ed->sel.ids[i], off); if (f) *f = v; }
    after_item_edit(ed);
}
static void apply_i(Editor *ed, size_t off, int v) {
    history_touch(ed, 0x2000u + (uint32_t)ed->sel.kind * 0x400u + (uint32_t)off);
    for (int i = 0; i < ed->sel.count; i++) { int *f = item_field(ed, ed->sel.ids[i], off); if (f) *f = v; }
    after_item_edit(ed);
}

static bool pn_num(Pn *p, const char *label, size_t off, float step, float lo, float hi, int dec) {
    float v; bool mixed;
    gather_f(p->ed, off, &v, &mixed);
    SDL_FRect r = pn_field(p, label);
    if (ui_number(p->ui, r, label, &v, step, lo, hi, dec, mixed)) { apply_f(p->ed, off, v); return true; }
    return false;
}

static void pn_texture(Pn *p, const char *label, int tex, bool mixed, int target) {
    ui_text(p->ui, p->x + PAD, p->y + 6, C_DIM, label);
    SDL_FRect r = {p->x + 108, p->y, 48, 48};
    char id[48];
    snprintf(id, sizeof(id), "tex-%s", label);
    if (ui_button_plain(p->ui, r, false, id)) { p->ed->popup = POPUP_TEXTURE; p->ed->tex_target = target; }
    draw_texture_thumb(p->ed, mixed ? -1 : tex, (SDL_FRect){r.x + 2, r.y + 2, r.w - 4, r.h - 4});
    const char *name = mixed ? "(mixed)" : (tex >= 0 && tex < MODEL_TEXTURE_COUNT ? TEXTURE_NAMES[tex] : "unknown");
    ui_text(p->ui, r.x + 58, r.y + 12, C_TEXT, name);
    ui_text(p->ui, r.x + 58, r.y + 28, C_DIM, "click to change");
    ui_tooltip(p->ui, r, "Choose a different texture");
    p->y += 56;
}

static bool pn_button_row(Pn *p, const char *a, const char *b, bool *hit_a, bool *hit_b) {
    float w = (p->w - 2 * PAD - 8) * 0.5f;
    SDL_FRect ra = {p->x + PAD, p->y, b ? w : p->w - 2 * PAD, 24}, rb = {p->x + PAD + w + 8, p->y, w, 24};
    *hit_a = ui_button(p->ui, ra, a, false);
    *hit_b = b ? ui_button(p->ui, rb, b, false) : false;
    p->y += 32;
    return *hit_a || *hit_b;
}

static const struct { const char *name; int r, g, b; } k_presets[] = {
    {"White", 255, 255, 255}, {"Warm", 255, 214, 160}, {"Cool", 170, 200, 255}, {"Red", 255, 90, 80},
    {"Green", 110, 255, 140}, {"Blue", 90, 130, 255}, {"Amber", 255, 170, 60},
};

static void pn_rgb(Pn *p, size_t roff, size_t goff, size_t boff) {
    Editor *ed = p->ed;
    int r, g, b; bool mr, mg, mb;
    gather_i(ed, roff, &r, &mr); gather_i(ed, goff, &g, &mg); gather_i(ed, boff, &b, &mb);
    SDL_FRect sw = pn_field(p, "Color");
    sw.h = 22;
    ui_fill(p->ui, sw, UI_RGB((Uint8)r, (Uint8)g, (Uint8)b));
    ui_rect(p->ui, sw, C_BORDER);
    if (mr || mg || mb) ui_text_center(p->ui, sw, UI_RGB(20, 20, 20), "(mixed)");
    static const char *names[3] = {"Red", "Green", "Blue"};
    static const UIColor fills[3] = {{190, 60, 60, 255}, {50, 150, 70, 255}, {60, 90, 200, 255}};
    int *vals[3] = {&r, &g, &b};
    size_t offs[3] = {roff, goff, boff};
    for (int i = 0; i < 3; i++) {
        SDL_FRect s = pn_field(p, names[i]);
        s.h = 18;
        if (ui_slider(p->ui, s, names[i], vals[i], 0, 255, fills[i])) apply_i(ed, offs[i], *vals[i]);
        p->y -= 4;
    }
    p->y += 4;
    float x = p->x + 108, y = p->y;
    for (int i = 0; i < 7; i++) {
        SDL_FRect pr = {x + i * 25.0f, y, 22, 18};
        char id[24];
        snprintf(id, sizeof(id), "preset%d", i);
        if (ui_button_plain(p->ui, pr, false, id)) {
            history_touch(ed, 0x2999u);
            apply_i(ed, roff, k_presets[i].r); apply_i(ed, goff, k_presets[i].g); apply_i(ed, boff, k_presets[i].b);
        }
        ui_fill(p->ui, (SDL_FRect){pr.x + 2, pr.y + 2, pr.w - 4, pr.h - 4}, UI_RGB((Uint8)k_presets[i].r, (Uint8)k_presets[i].g, (Uint8)k_presets[i].b));
        ui_tooltip(p->ui, pr, k_presets[i].name);
    }
    ui_text(p->ui, p->x + PAD, y + 5, C_DIM, "Presets");
    p->y += 28;
}

/* --- selection kinds ------------------------------------------------- */

static void inspect_sectors(Pn *p) {
    Editor *ed = p->ed;
    const Level *l = &ed->level;
    char title[64];
    if (ed->sel.count == 1) snprintf(title, sizeof(title), "ROOM #%d", ed->sel.ids[0]);
    else snprintf(title, sizeof(title), "%d ROOMS", ed->sel.count);
    pn_section(p, title);
    pn_num(p, "Floor", offsetof(Sector, floor_height), 0.25f, -64, 64, 2);
    pn_num(p, "Ceiling", offsetof(Sector, ceiling_height), 0.25f, -64, 64, 2);
    float fl, ce; bool mf, mc;
    gather_f(ed, offsetof(Sector, floor_height), &fl, &mf);
    gather_f(ed, offsetof(Sector, ceiling_height), &ce, &mc);
    if (!mf && !mc) {
        if (ce <= fl) pn_line(p, C_ERR, "Floor must be below the ceiling");
        else pn_linef(p, C_DIM, "Headroom: %.2f units", ce - fl);
    }
    int ft, ct; bool mft, mct;
    gather_i(ed, offsetof(Sector, floor_texture_id), &ft, &mft);
    gather_i(ed, offsetof(Sector, ceiling_texture_id), &ct, &mct);
    pn_section(p, "TEXTURES");
    pn_texture(p, "Floor", ft, mft, 1);
    pn_texture(p, "Ceiling", ct, mct, 2);
    {
        ui_text(p->ui, p->x + PAD, p->y + 7, C_DIM, "Walls");
        SDL_FRect r = {p->x + 108, p->y, p->w - 108 - PAD, 24};
        if (ui_button(p->ui, r, "Retexture all walls...", false)) { ed->popup = POPUP_TEXTURE; ed->tex_target = 3; }
        ui_tooltip(p->ui, r, "Apply one texture to every solid wall of the selected room(s)");
        p->y += 32;
    }

    pn_section(p, "LIGHTING");
    int ll; bool ml;
    gather_i(ed, offsetof(Sector, light_level), &ll, &ml);
    SDL_FRect s = pn_field(p, "Brightness");
    s.h = 18;
    if (ui_slider(p->ui, s, "brightness", &ll, 0, 255, UI_RGB(150, 140, 60))) apply_i(ed, offsetof(Sector, light_level), ll);
    p->y -= 4;
    pn_rgb(p, offsetof(Sector, light_r), offsetof(Sector, light_g), offsetof(Sector, light_b));

    if (ed->sel.count == 1 && ed->sel.ids[0] < l->sector_count) {
        pn_section(p, "INFO");
        int id = ed->sel.ids[0], walls = 0, portals = 0;
        for (int i = 0; i < l->wall_count; i++) {
            if (l->walls[i].front_sector != id && l->walls[i].back_sector != id) continue;
            walls++;
            if (l->walls[i].front_sector >= 0 && l->walls[i].back_sector >= 0) portals++;
        }
        pn_linef(p, C_DIM, "Area %.1f sq units", fabsf(model_sector_area(l, id)));
        pn_linef(p, C_DIM, "%d walls, %d open to neighbours", walls, portals);
    }
    p->y += 6;
    bool zoom, del;
    pn_button_row(p, "Zoom to (F)", "Delete room", &zoom, &del);
    if (zoom) view_fit_selection(ed);
    if (del) editor_delete_selection(ed);
}

static void inspect_walls(Pn *p) {
    Editor *ed = p->ed;
    const Level *l = &ed->level;
    char title[64];
    if (ed->sel.count == 1) snprintf(title, sizeof(title), "WALL #%d", ed->sel.ids[0]);
    else snprintf(title, sizeof(title), "%d WALLS", ed->sel.count);
    pn_section(p, title);
    int id = ed->sel.ids[0];
    if (ed->sel.count == 1 && model_wall_valid(l, id)) {
        const Wall *w = &l->walls[id];
        bool portal = w->front_sector >= 0 && w->back_sector >= 0;
        bool orphan = w->front_sector < 0 && w->back_sector < 0;
        pn_linef(p, C_TEXT, "%s", orphan ? "Loose wall (not part of a room)" : portal ? "Opening between two rooms" : "Solid wall");
        pn_linef(p, C_DIM, "Length %.2f units", model_wall_length(l, id));
        if (portal) {
            const Sector *a = &l->sectors[w->front_sector], *b = &l->sectors[w->back_sector];
            float lo = fmaxf(a->floor_height, b->floor_height), hi = fminf(a->ceiling_height, b->ceiling_height);
            if (hi <= lo) pn_line(p, C_ERR, "Blocked: no gap between floors/ceilings");
            else pn_linef(p, C_DIM, "Gap %.2f to %.2f (%.2f tall)", lo, hi, hi - lo);
            p->y += 4;
            bool ra, rb;
            char la[32], lb[32];
            snprintf(la, sizeof(la), "Room #%d", w->front_sector);
            snprintf(lb, sizeof(lb), "Room #%d", w->back_sector);
            pn_button_row(p, la, lb, &ra, &rb);
            if (ra) sel_set(ed, SEL_SECTOR, w->front_sector);
            if (rb) sel_set(ed, SEL_SECTOR, w->back_sector);
            if (ra || rb) return;
        } else if (!orphan) {
            int s = w->front_sector >= 0 ? w->front_sector : w->back_sector;
            p->y += 4;
            bool ra, rb;
            char la[32];
            snprintf(la, sizeof(la), "Go to Room #%d", s);
            pn_button_row(p, la, NULL, &ra, &rb);
            if (ra) { sel_set(ed, SEL_SECTOR, s); return; }
        }
    } else {
        float total = 0;
        for (int i = 0; i < ed->sel.count; i++) total += model_wall_length(l, ed->sel.ids[i]);
        pn_linef(p, C_DIM, "Total length %.2f units", total);
    }
    int tex; bool mixed;
    gather_i(ed, offsetof(Wall, texture_id), &tex, &mixed);
    p->y += 4;
    pn_texture(p, "Texture", tex, mixed, 0);
    bool split = false, del;
    if (ed->sel.count == 1) pn_button_row(p, "Split in half", "Delete wall", &split, &del);
    else pn_button_row(p, "Delete walls", NULL, &del, &split), split = false;
    if (split && model_wall_valid(l, id) && history_push(ed)) {
        Vertex a = ed->level.vertices[ed->level.walls[id].vertex_start], b = ed->level.vertices[ed->level.walls[id].vertex_end];
        int nv;
        if (model_split_wall(&ed->level, id, (a.x + b.x) * 0.5f, (a.z + b.z) * 0.5f, &nv)) { editor_changed(ed); sel_set(ed, SEL_VERTEX, nv); }
        else history_discard_last(ed);
        return;
    }
    if (del) editor_delete_selection(ed);
}

static void move_one_vertex(Editor *ed, int v, float x, float z) {
    Level *l = &ed->level;
    history_touch(ed, 0x3000u + (uint32_t)v);
    float ox = l->vertices[v].x, oz = l->vertices[v].z;
    l->vertices[v].x = x; l->vertices[v].z = z;
    unsigned char *m = calloc((size_t)l->vertex_count + 1, 1);
    if (!m) return;
    m[v] = 1;
    if (model_has_crossings(l, m)) {
        l->vertices[v].x = ox; l->vertices[v].z = oz;
        editor_toast(ed, true, "Can't move there: walls would cross");
    } else {
        ModelDirty d = {0};
        if (model_resolve_contacts(l, m, &d)) { model_rebuild_sectors(l, &ed->tmpl, &d); sel_clear(ed); }
        else model_dirty_free(&d);
    }
    free(m);
    editor_changed(ed);
}

static void inspect_vertices(Pn *p) {
    Editor *ed = p->ed;
    const Level *l = &ed->level;
    char title[64];
    if (ed->sel.count == 1) snprintf(title, sizeof(title), "POINT #%d", ed->sel.ids[0]);
    else snprintf(title, sizeof(title), "%d POINTS", ed->sel.count);
    pn_section(p, title);
    int id = ed->sel.ids[0];
    if (ed->sel.count == 1 && id < l->vertex_count) {
        float x = l->vertices[id].x, z = l->vertices[id].z;
        SDL_FRect rx = pn_field(p, "X");
        bool cx = ui_number(p->ui, rx, "vx", &x, editor_grid(ed), -1000, 1000, 2, false);
        SDL_FRect rz = pn_field(p, "Z");
        bool cz = ui_number(p->ui, rz, "vz", &z, editor_grid(ed), -1000, 1000, 2, false);
        if (cx || cz) { move_one_vertex(ed, id, x, z); return; }
        int uses = 0;
        for (int i = 0; i < l->wall_count; i++) if (l->walls[i].vertex_start == id || l->walls[i].vertex_end == id) uses++;
        pn_linef(p, C_DIM, "Joins %d wall%s", uses, uses == 1 ? "" : "s");
        if (uses == 1) pn_line(p, C_ERR, "Open end: the room isn't closed");
    } else {
        pn_line(p, C_DIM, "Drag to move them together.");
    }
    p->y += 6;
    bool del, dummy;
    pn_button_row(p, "Delete (and its walls)", NULL, &del, &dummy);
    if (del) editor_delete_selection(ed);
}

static void inspect_lights(Pn *p) {
    Editor *ed = p->ed;
    char title[64];
    if (ed->sel.count == 1) snprintf(title, sizeof(title), "LIGHT #%d", ed->sel.ids[0]);
    else snprintf(title, sizeof(title), "%d LIGHTS", ed->sel.count);
    pn_section(p, title);
    pn_num(p, "X", offsetof(PointLight, x), editor_grid(ed), -1000, 1000, 2);
    pn_num(p, "Z", offsetof(PointLight, z), editor_grid(ed), -1000, 1000, 2);
    pn_num(p, "Height", offsetof(PointLight, y), 0.25f, -64, 64, 2);
    pn_num(p, "Radius", offsetof(PointLight, radius), 0.5f, 0.5f, 100, 1);
    pn_num(p, "Intensity", offsetof(PointLight, intensity), 0.1f, 0, 10, 2);
    int eff; bool me;
    gather_i(ed, offsetof(PointLight, effect), &eff, &me);
    static const char *effects[3] = {"Steady", "Pulse", "Flicker"};
    SDL_FRect r = pn_field(p, "Effect");
    if (ui_segmented(p->ui, r, "effect", effects, 3, &eff)) apply_i(ed, offsetof(PointLight, effect), eff);
    pn_section(p, "COLOR");
    pn_rgb(p, offsetof(PointLight, r), offsetof(PointLight, g), offsetof(PointLight, b));
    p->y += 6;
    bool dup, del;
    pn_button_row(p, "Duplicate", "Delete", &dup, &del);
    if (dup) editor_duplicate_light(ed);
    if (del) editor_delete_selection(ed);
}

static bool spawn_f(Pn *p, const char *label, float *field, float step, float lo, float hi, int dec) {
    float v = *field;
    SDL_FRect r = pn_field(p, label);
    if (ui_number(p->ui, r, label, &v, step, lo, hi, dec, false)) {
        history_touch(p->ed, 0x4000u + (uint32_t)(uintptr_t)(label[0]));
        *field = v;
        editor_changed(p->ed);
        return true;
    }
    return false;
}

static void inspect_spawn(Pn *p) {
    Editor *ed = p->ed;
    PlayerSpawn *sp = &ed->level.player_spawn;
    pn_section(p, "PLAYER START");
    spawn_f(p, "X", &sp->x, editor_grid(ed), -1000, 1000, 2);
    spawn_f(p, "Z", &sp->z, editor_grid(ed), -1000, 1000, 2);
    spawn_f(p, "Height", &sp->y, 0.1f, -64, 64, 2);
    spawn_f(p, "Facing (deg)", &sp->yaw, 15.0f, -720, 720, 0);
    spawn_f(p, "Look pitch", &sp->pitch, 5.0f, -89, 89, 0);
    p->y += 4;
    pn_line(p, C_DIM, "Drag the yellow handle on the map");
    pn_line(p, C_DIM, "to turn the player.");
}

static void inspect_level(Pn *p) {
    Editor *ed = p->ed;
    Level *l = &ed->level;
    pn_section(p, "LEVEL");
    pn_linef(p, C_TEXT, "%d rooms, %d walls", l->sector_count, l->wall_count);
    pn_linef(p, C_TEXT, "%d lights", l->light_count);
    p->y += 4;
    pn_section(p, "AMBIENT LIGHT");
    pn_line(p, C_DIM, "Base light everywhere, even far");
    pn_line(p, C_DIM, "from any lamp.");
    p->y += 2;
    float *ch[3] = {&l->ambient_r, &l->ambient_g, &l->ambient_b};
    static const char *names[3] = {"Red", "Green", "Blue"};
    static const UIColor fills[3] = {{190, 60, 60, 255}, {50, 150, 70, 255}, {60, 90, 200, 255}};
    SDL_FRect sw = pn_field(p, "Color");
    ui_fill(p->ui, sw, UI_RGB((Uint8)(ed_clampf(l->ambient_r, 0, 1) * 255), (Uint8)(ed_clampf(l->ambient_g, 0, 1) * 255), (Uint8)(ed_clampf(l->ambient_b, 0, 1) * 255)));
    ui_rect(p->ui, sw, C_BORDER);
    for (int i = 0; i < 3; i++) {
        int v = (int)lroundf(ed_clampf(*ch[i], 0, 1) * 255.0f);
        SDL_FRect s = pn_field(p, names[i]);
        s.h = 18;
        if (ui_slider(p->ui, s, names[i], &v, 0, 255, fills[i])) {
            history_touch(ed, 0x5000u + (uint32_t)i);
            *ch[i] = v / 255.0f;
            editor_changed(ed);
        }
        p->y -= 4;
    }
    p->y += 8;
    pn_section(p, "GETTING AROUND");
    p->y += 2;
    const char *tips[] = {
        "R: drag out a rectangle room.",
        "D: click corners, Enter to close.",
        "Draw next to a room and the",
        "shared wall becomes a doorway.",
        "Click a room to set its height,",
        "textures and light.",
        "F5 plays the level instantly.",
        "F1 lists every shortcut.",
    };
    for (size_t i = 0; i < sizeof(tips) / sizeof(tips[0]); i++) pn_line(p, C_DIM, tips[i]);
    p->y += 6;
    bool a, b;
    pn_button_row(p, "Go to player start", NULL, &a, &b);
    if (a) { sel_set(ed, SEL_SPAWN, 0); view_center_on(ed, l->player_spawn.x, l->player_spawn.z); }
}

static void inspect_problems(Pn *p) {
    Editor *ed = p->ed;
    UI *ui = p->ui;
    p->y += 8;
    SDL_FRect head = {p->x + 6, p->y, p->w - 12, 22};
    if (ui_button_plain(ui, head, false, "problemhead")) ed->problems_open = !ed->problems_open;
    int errors = 0, warns = 0;
    for (int i = 0; i < ed->problem_count; i++) {
        if (ed->problems[i].severity == PROBLEM_ERROR) errors++;
        else if (ed->problems[i].severity == PROBLEM_WARNING) warns++;
    }
    char t[64];
    snprintf(t, sizeof(t), "%s  PROBLEMS (%d)", ed->problems_open ? "-" : "+", ed->problem_count);
    ui_text(ui, head.x + 6, head.y + 7, errors ? C_ERR : warns ? C_WARN : C_OK, t);
    p->y += 28;
    if (!ed->problems_open) return;
    if (ed->problem_count == 0) { pn_line(p, C_OK, "Nothing wrong found."); return; }
    for (int i = 0; i < ed->problem_count; i++) {
        const Problem *pr = &ed->problems[i];
        float tw = p->w - 2 * PAD - 16;
        int lines = 0;
        {   /* measure */
            int per = (int)(tw / UI_CHAR_W), len = (int)strlen(pr->text);
            lines = (len + per - 1) / per;
            if (lines < 1) lines = 1;
            if (lines > 5) lines = 5;
        }
        SDL_FRect row = {p->x + 6, p->y, p->w - 12, lines * 12.0f + 8};
        char id[24];
        snprintf(id, sizeof(id), "prob%d", i);
        bool hit = ui_button_plain(ui, row, false, id);
        UIColor dot = pr->severity == PROBLEM_ERROR ? C_ERR : pr->severity == PROBLEM_WARNING ? C_WARN : C_ACCENT;
        ui_fill(ui, (SDL_FRect){row.x + 5, row.y + 7, 5, 5}, dot);
        float hold = p->y;
        p->y += 4;
        pn_wrap(p, p->x + PAD + 10, C_TEXT, pr->text, tw);
        p->y = hold + row.h + 2;
        if (hit) {
            if (pr->kind == PROBLEM_UNUSED_VERTICES) editor_clean_up(ed);
            else editor_select_problem(ed, pr);
        }
    }
}

static void draw_inspector(Editor *ed) {
    UI *ui = &ed->ui;
    SDL_FRect panel = {ui->width - LAYOUT_INSPECTOR_W, LAYOUT_MENU_H, LAYOUT_INSPECTOR_W, ui->height - LAYOUT_MENU_H - LAYOUT_STATUS_H};
    ui_fill(ui, panel, C_PANEL);
    ui_line(ui, panel.x, panel.y, panel.x, panel.y + panel.h, C_BORDER);
    ui_block(ui, panel);

    float max_scroll = fmaxf(0.0f, ed->inspector_content_h - panel.h);
    ed->inspector_scroll = ed_clampf(ed->inspector_scroll, 0.0f, max_scroll);
    ui_clip(ui, panel);
    Pn p = {ed, ui, panel.x, panel.w, panel.y + 4 - ed->inspector_scroll};
    float start = p.y;
    switch (ed->sel.kind) {
        case SEL_SECTOR: inspect_sectors(&p); break;
        case SEL_WALL: inspect_walls(&p); break;
        case SEL_VERTEX: inspect_vertices(&p); break;
        case SEL_LIGHT: inspect_lights(&p); break;
        case SEL_SPAWN: inspect_spawn(&p); break;
        default: inspect_level(&p); break;
    }
    inspect_problems(&p);
    ed->inspector_content_h = p.y - start + 16;
    if (ui_point_in(panel, ui->mx, ui->my) && !ui->blocked && ui->wheel != 0) {
        ed->inspector_scroll = ed_clampf(ed->inspector_scroll - ui->wheel * 40.0f, 0.0f, fmaxf(0.0f, ed->inspector_content_h - panel.h));
        ui->wheel = 0;
    }
    ui_unclip(ui);
    if (ed->inspector_content_h > panel.h) {   /* scrollbar */
        float th = panel.h * panel.h / ed->inspector_content_h;
        float ty = panel.y + (panel.h - th) * (max_scroll > 0 ? ed->inspector_scroll / max_scroll : 0);
        ui_fill(ui, (SDL_FRect){panel.x + panel.w - 4, ty, 3, th}, UI_RGBA(140, 148, 164, 120));
    }
}

/* ===================================================================== */
/* Entry point                                                           */
/* ===================================================================== */

void panels_draw(Editor *ed) {
    UI *ui = &ed->ui;
    static PopupKind last_popup = POPUP_NONE;
    if (ed->popup != last_popup) { g_popup_age = 0; last_popup = ed->popup; }
    else g_popup_age++;

    draw_menubar(ed);
    ui->blocked = ed->popup != POPUP_NONE || ed->menu_open >= 0;
    draw_toolbar(ed);
    draw_inspector(ed);
    draw_canvas_controls(ed);
    draw_status(ed);
    draw_toast(ed);
    ui->blocked = false;
    draw_dropdown(ed);
    if (ed->popup == POPUP_TEXTURE) draw_texture_popup(ed);
    else if (ed->popup == POPUP_HELP) draw_help(ed);
}
