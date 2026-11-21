#include "ui.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Frame + input                                                      */
/* ------------------------------------------------------------------ */

void ui_init(UI *ui, SDL_Window *window, SDL_Renderer *renderer) {
    memset(ui, 0, sizeof(*ui));
    ui->window = window;
    ui->renderer = renderer;
    ui->scale = 1.0f;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

SDL_Keymod ui_mods(const UI *ui) { return (SDL_Keymod)(SDL_GetModState() | ui->mod); }

static void track_mod(UI *ui, const SDL_Event *e) {
    ui->mod = e->key.mod;
    if (e->type == SDL_EVENT_KEY_UP) {  /* releasing a modifier must clear it */
        switch (e->key.key) {
            case SDLK_LSHIFT: case SDLK_RSHIFT: ui->mod &= ~SDL_KMOD_SHIFT; break;
            case SDLK_LCTRL: case SDLK_RCTRL: ui->mod &= ~SDL_KMOD_CTRL; break;
            case SDLK_LALT: case SDLK_RALT: ui->mod &= ~SDL_KMOD_ALT; break;
            default: break;
        }
    }
}

void ui_event(UI *ui, SDL_Event *e) {
    switch (e->type) {
        case SDL_EVENT_MOUSE_MOTION:
            SDL_ConvertEventToRenderCoordinates(ui->renderer, e);
            ui->mx = e->motion.x; ui->my = e->motion.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            SDL_ConvertEventToRenderCoordinates(ui->renderer, e);
            ui->mx = e->button.x; ui->my = e->button.y;
            if (e->button.button == SDL_BUTTON_LEFT) {
                if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    ui->down = ui->pressed = true;
                    ui->double_click = e->button.clicks >= 2;
                } else {
                    ui->down = false;
                    ui->released = true;
                }
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            SDL_ConvertEventToRenderCoordinates(ui->renderer, e);
            ui->mx = e->wheel.mouse_x; ui->my = e->wheel.mouse_y;
            ui->wheel += e->wheel.y;
            break;
        case SDL_EVENT_KEY_DOWN:
            track_mod(ui, e);
            if (e->key.key == SDLK_RETURN || e->key.key == SDLK_KP_ENTER) ui->key_enter = true;
            if (e->key.key == SDLK_ESCAPE) ui->key_escape = true;
            if (e->key.key == SDLK_BACKSPACE) ui->key_backspace = true;
            if (e->key.key == SDLK_DELETE) ui->key_delete = true;
            break;
        case SDL_EVENT_KEY_UP:
            track_mod(ui, e);
            break;
        case SDL_EVENT_TEXT_INPUT: {
            size_t n = strlen(ui->text_in), m = strlen(e->text.text);
            if (n + m < sizeof(ui->text_in)) memcpy(ui->text_in + n, e->text.text, m + 1);
            break;
        }
        default: break;
    }
}

void ui_begin(UI *ui) {
    SDL_SetRenderScale(ui->renderer, ui->scale, ui->scale);
    int pw = 0, ph = 0;
    SDL_GetRenderOutputSize(ui->renderer, &pw, &ph);
    ui->width = pw / ui->scale;
    ui->height = ph / ui->scale;
    memcpy(ui->blockers_prev, ui->blockers, sizeof(ui->blockers));
    ui->nblock_prev = ui->nblock;
    ui->nblock = 0;
    ui->tip[0] = 0;
    ui->tip_hover_id = 0;
    ui->blocked = false;
    ui->has_clip = false;
}

void ui_end(UI *ui) {
    if (ui->tip_hover_id == 0) ui->tip_id = 0;
    if (ui->tip[0]) {
        float w = ui_text_w(ui->tip) + 12, h = 20;
        float x = ui->mx + 14, y = ui->my + 18;
        if (x + w > ui->width - 4) x = ui->width - 4 - w;
        if (y + h > ui->height - 4) y = ui->my - h - 6;
        ui_fill(ui, (SDL_FRect){x, y, w, h}, UI_RGBA(10, 11, 14, 240));
        ui_rect(ui, (SDL_FRect){x, y, w, h}, C_HOVER);
        ui_text(ui, x + 6, y + 6, C_TEXT, ui->tip);
    }
    if (ui->released) { ui->active = 0; ui->scrubbing = false; }
    ui->pressed = ui->released = ui->double_click = false;
    ui->wheel = 0;
    ui->key_enter = ui->key_escape = ui->key_backspace = ui->key_delete = false;
    ui->text_in[0] = 0;
}

void ui_block(UI *ui, SDL_FRect r) {
    if (ui->nblock < (int)(sizeof(ui->blockers) / sizeof(ui->blockers[0]))) ui->blockers[ui->nblock++] = r;
}
bool ui_point_in(SDL_FRect r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
bool ui_over_panel(const UI *ui, float x, float y) {
    for (int i = 0; i < ui->nblock_prev; i++) if (ui_point_in(ui->blockers_prev[i], x, y)) return true;
    return false;
}
bool ui_text_editing(const UI *ui) { return ui->edit_id != 0; }
void ui_cancel_edit(UI *ui) {
    if (ui->edit_id) { ui->edit_id = 0; SDL_StopTextInput(ui->window); }
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

void ui_set_color(UI *ui, UIColor c) { SDL_SetRenderDrawColor(ui->renderer, c.r, c.g, c.b, c.a); }
void ui_fill(UI *ui, SDL_FRect r, UIColor c) { ui_set_color(ui, c); SDL_RenderFillRect(ui->renderer, &r); }
void ui_rect(UI *ui, SDL_FRect r, UIColor c) { ui_set_color(ui, c); SDL_RenderRect(ui->renderer, &r); }
void ui_line(UI *ui, float x1, float y1, float x2, float y2, UIColor c) {
    ui_set_color(ui, c);
    SDL_RenderLine(ui->renderer, x1, y1, x2, y2);
}

static SDL_FColor fcol(UIColor c) { return (SDL_FColor){c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f}; }

void ui_thick_line(UI *ui, float x1, float y1, float x2, float y2, float w, UIColor c) {
    float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    if (w <= 1.0f) { ui_line(ui, x1, y1, x2, y2, c); return; }
    float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
    SDL_FColor fc = fcol(c);
    SDL_Vertex v[4] = {
        {{x1 + nx, y1 + ny}, fc, {0, 0}}, {{x1 - nx, y1 - ny}, fc, {0, 0}},
        {{x2 + nx, y2 + ny}, fc, {0, 0}}, {{x2 - nx, y2 - ny}, fc, {0, 0}},
    };
    static const int idx[6] = {0, 1, 2, 2, 1, 3};
    SDL_RenderGeometry(ui->renderer, NULL, v, 4, idx, 6);
}

void ui_circle(UI *ui, float cx, float cy, float rad, UIColor c) {
    int n = (int)(rad * 1.2f);
    if (n < 12) n = 12;
    if (n > 96) n = 96;
    SDL_FPoint pts[98];
    for (int i = 0; i <= n; i++) {
        float a = (float)i / n * 6.2831853f;
        pts[i] = (SDL_FPoint){cx + cosf(a) * rad, cy + sinf(a) * rad};
    }
    ui_set_color(ui, c);
    SDL_RenderLines(ui->renderer, pts, n + 1);
}

void ui_disc(UI *ui, float cx, float cy, float rad, UIColor c) {
    int n = (int)(rad * 1.2f);
    if (n < 10) n = 10;
    if (n > 48) n = 48;
    SDL_Vertex v[50];
    int idx[48 * 3];
    SDL_FColor fc = fcol(c);
    v[0] = (SDL_Vertex){{cx, cy}, fc, {0, 0}};
    for (int i = 0; i < n; i++) {
        float a = (float)i / n * 6.2831853f;
        v[i + 1] = (SDL_Vertex){{cx + cosf(a) * rad, cy + sinf(a) * rad}, fc, {0, 0}};
        idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = (i + 1) % n + 1;
    }
    SDL_RenderGeometry(ui->renderer, NULL, v, n + 1, idx, n * 3);
}

void ui_tri(UI *ui, float x1, float y1, float x2, float y2, float x3, float y3, UIColor c) {
    SDL_FColor fc = fcol(c);
    SDL_Vertex v[3] = {{{x1, y1}, fc, {0, 0}}, {{x2, y2}, fc, {0, 0}}, {{x3, y3}, fc, {0, 0}}};
    SDL_RenderGeometry(ui->renderer, NULL, v, 3, NULL, 0);
}

float ui_text_w(const char *s) { return (float)strlen(s) * UI_CHAR_W; }

void ui_text(UI *ui, float x, float y, UIColor c, const char *s) {
    ui_set_color(ui, c);
    SDL_RenderDebugText(ui->renderer, floorf(x), floorf(y), s);
}
void ui_textf(UI *ui, float x, float y, UIColor c, const char *fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ui_text(ui, x, y, c, buf);
}
void ui_text_right(UI *ui, float right, float y, UIColor c, const char *s) { ui_text(ui, right - ui_text_w(s), y, c, s); }
void ui_text_center(UI *ui, SDL_FRect r, UIColor c, const char *s) {
    ui_text(ui, r.x + (r.w - ui_text_w(s)) * 0.5f, r.y + (r.h - 8.0f) * 0.5f, c, s);
}

void ui_clip(UI *ui, SDL_FRect r) {
    SDL_Rect cr = {(int)r.x, (int)r.y, (int)r.w, (int)r.h};
    SDL_SetRenderClipRect(ui->renderer, &cr);
    ui->has_clip = true;
    ui->clip = r;
}
void ui_unclip(UI *ui) {
    SDL_SetRenderClipRect(ui->renderer, NULL);
    ui->has_clip = false;
}

/* ------------------------------------------------------------------ */
/* Interaction                                                        */
/* ------------------------------------------------------------------ */

bool ui_hover(const UI *ui, SDL_FRect r) {
    if (ui->blocked) return false;
    if (!ui_point_in(r, ui->mx, ui->my)) return false;
    if (ui->has_clip && !ui_point_in(ui->clip, ui->mx, ui->my)) return false;
    return true;
}

static uint32_t make_id(const char *s, SDL_FRect r) {
    uint32_t h = 2166136261u;
    for (; s && *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
    h ^= (uint32_t)(int)r.x * 73856093u;
    h ^= (uint32_t)(int)r.y * 19349663u;
    return h ? h : 1;
}

void ui_tooltip(UI *ui, SDL_FRect r, const char *text) {
    if (!text || !ui_hover(ui, r)) return;
    uint32_t id = make_id(text, r);
    ui->tip_hover_id = id;
    if (ui->tip_id != id) { ui->tip_id = id; ui->tip_since = SDL_GetTicks(); }
    if (!ui->down && SDL_GetTicks() - ui->tip_since > 450) {
        strncpy(ui->tip, text, sizeof(ui->tip) - 1);
        ui->tip[sizeof(ui->tip) - 1] = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Widgets                                                            */
/* ------------------------------------------------------------------ */

static bool click_logic(UI *ui, SDL_FRect r, uint32_t id) {
    bool hov = ui_hover(ui, r);
    if (hov && ui->pressed) ui->active = id;
    bool clicked = ui->active == id && ui->released && hov;
    return clicked;
}

bool ui_button_plain(UI *ui, SDL_FRect r, bool selected, const char *idstr) {
    uint32_t id = make_id(idstr, r);
    bool hov = ui_hover(ui, r);
    bool clicked = click_logic(ui, r, id);
    UIColor bg = selected ? C_ACTIVE : (ui->active == id && hov) ? C_ACCENT : hov ? C_HOVER : C_WIDGET;
    ui_fill(ui, r, bg);
    return clicked;
}

bool ui_button(UI *ui, SDL_FRect r, const char *label, bool selected) {
    bool c = ui_button_plain(ui, r, selected, label);
    ui_text_center(ui, r, C_TEXT, label);
    return c;
}

bool ui_checkbox(UI *ui, SDL_FRect r, const char *label, bool *value) {
    uint32_t id = make_id(label, r);
    bool hov = ui_hover(ui, r);
    bool clicked = click_logic(ui, r, id);
    if (clicked) *value = !*value;
    SDL_FRect box = {r.x, r.y + (r.h - 14) * 0.5f, 14, 14};
    ui_fill(ui, box, hov ? C_HOVER : C_WIDGET);
    ui_rect(ui, box, C_BORDER);
    if (*value) {
        ui_thick_line(ui, box.x + 3, box.y + 7, box.x + 6, box.y + 10, 2, C_ACCENT);
        ui_thick_line(ui, box.x + 6, box.y + 10, box.x + 11, box.y + 3, 2, C_ACCENT);
    }
    ui_text(ui, r.x + 22, r.y + (r.h - 8) * 0.5f, C_TEXT, label);
    return clicked;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

bool ui_number(UI *ui, SDL_FRect r, const char *idstr, float *value, float step,
               float min, float max, int decimals, bool mixed) {
    uint32_t id = make_id(idstr, r);
    bool hov = ui_hover(ui, r);
    bool changed = false;
    bool editing = ui->edit_id == id;

    if (editing) {
        for (const char *p = ui->text_in; *p; p++) {
            if (isdigit((unsigned char)*p) || *p == '.' || *p == '-' || *p == '+') {
                if (ui->edit_fresh) { ui->edit_len = 0; ui->edit_fresh = false; }
                if (ui->edit_len < (int)sizeof(ui->edit_buf) - 1) ui->edit_buf[ui->edit_len++] = *p;
            }
        }
        if (ui->key_backspace) {
            if (ui->edit_fresh) ui->edit_len = 0;
            else if (ui->edit_len > 0) ui->edit_len--;
            ui->edit_fresh = false;
        }
        ui->edit_buf[ui->edit_len] = 0;
        if (ui->key_enter || (ui->pressed && !hov)) {
            if (ui->edit_len > 0) {
                float v = clampf(strtof(ui->edit_buf, NULL), min, max);
                if (mixed || v != *value) { *value = v; changed = true; }
            }
            ui_cancel_edit(ui);
            editing = false;
        } else if (ui->key_escape) {
            ui_cancel_edit(ui);
            editing = false;
        }
    } else {
        if (hov && ui->pressed) {
            ui->active = id; ui->scrub_x = ui->mx; ui->scrub_value = *value; ui->scrubbing = false;
        }
        if (ui->active == id && ui->down) {
            if (!ui->scrubbing && fabsf(ui->mx - ui->scrub_x) > 3.0f) ui->scrubbing = true;
            if (ui->scrubbing) {
                bool fine = (ui_mods(ui) & SDL_KMOD_SHIFT) != 0;
                float base = mixed ? 0.0f : ui->scrub_value;
                float v = base + (ui->mx - ui->scrub_x) / 4.0f * step * (fine ? 0.1f : 1.0f);
                if (!fine) v = roundf(v / step) * step;
                v = clampf(v, min, max);
                if (v != *value || mixed) { *value = v; changed = true; }
            }
        }
        if (ui->active == id && ui->released && !ui->scrubbing && hov) {
            ui->edit_id = id;
            ui->edit_fresh = true;
            if (mixed) ui->edit_buf[0] = 0;
            else snprintf(ui->edit_buf, sizeof(ui->edit_buf), "%.*f", decimals, *value);
            ui->edit_len = (int)strlen(ui->edit_buf);
            SDL_StartTextInput(ui->window);
            editing = true;
        }
        if (hov && ui->wheel != 0 && ui->active == 0) {
            float v = clampf(*value + ui->wheel * step, min, max);
            if (v != *value) { *value = v; changed = true; }
            ui->wheel = 0;
        }
    }

    UIColor bg = editing ? C_BG : (ui->active == id && ui->scrubbing) ? C_ACTIVE : hov ? C_HOVER : C_WIDGET;
    ui_fill(ui, r, bg);
    if (editing) ui_rect(ui, r, C_ACCENT);
    char buf[48];
    if (editing) {
        bool blink = (SDL_GetTicks() / 450) % 2 == 0;
        snprintf(buf, sizeof(buf), "%s%s", ui->edit_buf, blink ? "_" : " ");
        ui_text_center(ui, r, ui->edit_fresh ? C_ACCENT : C_TEXT, buf);
    } else {
        if (mixed) snprintf(buf, sizeof(buf), "--");
        else snprintf(buf, sizeof(buf), "%.*f", decimals, *value);
        ui_text_center(ui, r, C_TEXT, buf);
        if (hov) {  /* drag affordance */
            float cy = r.y + r.h * 0.5f;
            ui_tri(ui, r.x + 5, cy, r.x + 9, cy - 3, r.x + 9, cy + 3, C_DIM);
            ui_tri(ui, r.x + r.w - 5, cy, r.x + r.w - 9, cy - 3, r.x + r.w - 9, cy + 3, C_DIM);
        }
    }
    return changed;
}

bool ui_slider(UI *ui, SDL_FRect r, const char *idstr, int *value, int min, int max, UIColor fill) {
    uint32_t id = make_id(idstr, r);
    bool hov = ui_hover(ui, r);
    bool changed = false;
    if (hov && ui->pressed) ui->active = id;
    if (ui->active == id && ui->down) {
        float t = clampf((ui->mx - r.x) / r.w, 0, 1);
        int v = min + (int)lroundf(t * (max - min));
        if (v != *value) { *value = v; changed = true; }
    }
    if (hov && ui->wheel != 0 && ui->active == 0) {
        int v = (int)clampf((float)(*value + (int)ui->wheel * 5), (float)min, (float)max);
        if (v != *value) { *value = v; changed = true; }
        ui->wheel = 0;
    }
    ui_fill(ui, r, C_WIDGET);
    float t = (float)(*value - min) / (float)(max - min);
    ui_fill(ui, (SDL_FRect){r.x, r.y, r.w * t, r.h}, fill);
    ui_rect(ui, r, hov || ui->active == id ? C_ACCENT : C_BORDER);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", *value);
    ui_text_center(ui, r, UI_RGB(255, 255, 255), buf);
    return changed;
}

bool ui_segmented(UI *ui, SDL_FRect r, const char *idstr, const char *const *labels, int count, int *value) {
    bool changed = false;
    float w = r.w / count;
    for (int i = 0; i < count; i++) {
        SDL_FRect cell = {r.x + w * i, r.y, w - 1, r.h};
        char id[96];
        snprintf(id, sizeof(id), "%s#%d", idstr, i);
        if (ui_button_plain(ui, cell, *value == i, id)) { *value = i; changed = true; }
        ui_text_center(ui, cell, C_TEXT, labels[i]);
    }
    return changed;
}

bool ui_menu_item(UI *ui, SDL_FRect r, const char *label, const char *shortcut, bool checked, bool enabled) {
    uint32_t id = make_id(label, r);
    bool hov = enabled && ui_hover(ui, r);
    bool clicked = false;
    if (enabled) {
        if (hov && ui->pressed) ui->active = id;
        clicked = ui->active == id && ui->released && hov;
    }
    if (hov) ui_fill(ui, r, C_ACTIVE);
    UIColor tc = enabled ? C_TEXT : C_DIM;
    if (checked) {
        ui_thick_line(ui, r.x + 7, r.y + r.h * 0.5f, r.x + 10, r.y + r.h * 0.5f + 3, 2, tc);
        ui_thick_line(ui, r.x + 10, r.y + r.h * 0.5f + 3, r.x + 15, r.y + r.h * 0.5f - 4, 2, tc);
    }
    ui_text(ui, r.x + 22, r.y + (r.h - 8) * 0.5f, tc, label);
    if (shortcut && shortcut[0]) ui_text_right(ui, r.x + r.w - 10, r.y + (r.h - 8) * 0.5f, hov ? C_TEXT : C_DIM, shortcut);
    return clicked;
}
