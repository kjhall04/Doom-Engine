#ifndef EDITOR_UI_H
#define EDITOR_UI_H

/*
 * Tiny immediate-mode UI on top of SDL_Renderer.
 * Coordinates are "logical" pixels: the renderer is scaled by ui->scale, so
 * 8x8 debug-font text stays readable on high-DPI screens.
 *
 * Frame flow:  ui_event() for each SDL event -> ui_begin() -> draw widgets
 *              -> ui_end().
 */

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

typedef struct UIColor { Uint8 r, g, b, a; } UIColor;

#define UI_RGB(r, g, b) ((UIColor){(r), (g), (b), 255})
#define UI_RGBA(r, g, b, a) ((UIColor){(r), (g), (b), (a)})

/* Theme */
#define C_BG        UI_RGB(24, 26, 31)
#define C_PANEL     UI_RGB(34, 37, 44)
#define C_PANEL_HI  UI_RGB(44, 48, 57)
#define C_WIDGET    UI_RGB(55, 60, 72)
#define C_HOVER     UI_RGB(72, 79, 96)
#define C_ACTIVE    UI_RGB(52, 120, 214)
#define C_ACCENT    UI_RGB(86, 156, 246)
#define C_TEXT      UI_RGB(224, 228, 236)
#define C_DIM       UI_RGB(140, 148, 164)
#define C_BORDER    UI_RGB(14, 15, 18)
#define C_OK        UI_RGB(104, 200, 120)
#define C_WARN      UI_RGB(240, 184, 64)
#define C_ERR       UI_RGB(240, 92, 92)

#define UI_CHAR_W 8.0f
#define UI_ROW_H 22.0f

typedef struct UI {
    SDL_Window *window;
    SDL_Renderer *renderer;
    float scale;
    float width, height;          /* logical window size */

    float mx, my;                 /* mouse, logical px */
    bool down;                    /* left button held */
    bool pressed, released;       /* left edges this frame */
    bool double_click;            /* this frame's press was a double click */
    float wheel;                  /* wheel notches this frame */
    SDL_Keymod mod;               /* modifier state tracked from key events */

    bool key_enter, key_escape, key_backspace, key_delete;
    char text_in[32];

    uint32_t active;              /* widget being pressed */
    uint32_t edit_id;             /* number field in text-edit mode */
    char edit_buf[40];
    int  edit_len;
    bool edit_fresh;              /* first keystroke replaces the text */
    float scrub_x, scrub_value;
    bool scrubbing;

    bool blocked;                 /* a popup is on top: base widgets ignore the mouse */
    bool has_clip;
    SDL_FRect clip;

    char tip[160];
    uint32_t tip_id, tip_hover_id;
    Uint64 tip_since;

    SDL_FRect blockers[24], blockers_prev[24];
    int nblock, nblock_prev;
} UI;

void ui_init(UI *ui, SDL_Window *window, SDL_Renderer *renderer);
void ui_event(UI *ui, SDL_Event *e);          /* converts mouse coords in place */
void ui_begin(UI *ui);
void ui_end(UI *ui);
SDL_Keymod ui_mods(const UI *ui);

/* Panels register their area so the canvas can ignore clicks beneath them. */
void ui_block(UI *ui, SDL_FRect r);
bool ui_over_panel(const UI *ui, float x, float y);
bool ui_point_in(SDL_FRect r, float x, float y);
bool ui_text_editing(const UI *ui);
void ui_cancel_edit(UI *ui);

/* Drawing */
void ui_set_color(UI *ui, UIColor c);
void ui_fill(UI *ui, SDL_FRect r, UIColor c);
void ui_rect(UI *ui, SDL_FRect r, UIColor c);
void ui_line(UI *ui, float x1, float y1, float x2, float y2, UIColor c);
void ui_thick_line(UI *ui, float x1, float y1, float x2, float y2, float w, UIColor c);
void ui_circle(UI *ui, float cx, float cy, float rad, UIColor c);
void ui_disc(UI *ui, float cx, float cy, float rad, UIColor c);
void ui_tri(UI *ui, float x1, float y1, float x2, float y2, float x3, float y3, UIColor c);
void ui_text(UI *ui, float x, float y, UIColor c, const char *s);
void ui_textf(UI *ui, float x, float y, UIColor c, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
void ui_text_right(UI *ui, float right, float y, UIColor c, const char *s);
void ui_text_center(UI *ui, SDL_FRect r, UIColor c, const char *s);
float ui_text_w(const char *s);
void ui_clip(UI *ui, SDL_FRect r);
void ui_unclip(UI *ui);

/* Interaction helpers */
bool ui_hover(const UI *ui, SDL_FRect r);
void ui_tooltip(UI *ui, SDL_FRect r, const char *text);

/* Widgets. All return true on the frame the user changed/activated them. */
bool ui_button(UI *ui, SDL_FRect r, const char *label, bool selected);
bool ui_button_plain(UI *ui, SDL_FRect r, bool selected, const char *id);  /* caller draws contents */
bool ui_checkbox(UI *ui, SDL_FRect r, const char *label, bool *value);
/* Number field: drag to scrub, wheel to step, click to type. `mixed` shows "--". */
bool ui_number(UI *ui, SDL_FRect r, const char *id, float *value, float step,
               float min, float max, int decimals, bool mixed);
bool ui_slider(UI *ui, SDL_FRect r, const char *id, int *value, int min, int max, UIColor fill);
bool ui_segmented(UI *ui, SDL_FRect r, const char *id, const char *const *labels, int count, int *value);
bool ui_menu_item(UI *ui, SDL_FRect r, const char *label, const char *shortcut, bool checked, bool enabled);

#endif
