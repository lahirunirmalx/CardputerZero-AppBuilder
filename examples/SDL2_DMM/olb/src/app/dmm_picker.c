/**
 * DMM picker — keyboard-driven 320x170 selection UI for the CardputerZero.
 *
 * Two columns: METER (every registered DMM driver) and PORT (a set of common
 * transports plus an editable custom entry). Arrow keys move within a column,
 * TAB / LEFT / RIGHT switch columns, ENTER connects, ESC / Q quits. On connect
 * it opens the chosen driver and runs the compact view IN-PROCESS (no
 * fork/exec, no env vars); closing the view returns here to pick again.
 *
 * This replaces env-var driver/port selection for the packaged CardputerZero
 * app. psu_app --pick (or the app with no arguments) lands here.
 */

#include "dmm_picker.h"

#include "drivers/registry.h"
#include "views/views.h"
#include "platform/platform.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <sys/wait.h>
#include <unistd.h>

#define WIN_W    320
#define WIN_H    170
#define HEADER_H 22

typedef struct { Uint8 r, g, b, a; } Color;

static const Color COL_BG      = {18, 18, 22, 255};
static const Color COL_HEADER  = {12, 12, 14, 255};
static const Color COL_BORDER  = {55, 55, 60, 255};
static const Color COL_TEXT    = {220, 220, 225, 255};
static const Color COL_DIM     = {110, 110, 118, 255};
static const Color COL_SECTION = {200, 160, 60, 255};
static const Color COL_SEL_BG  = {0, 90, 110, 255};   /* active-column row  */
static const Color COL_SEL_BG2 = {40, 44, 50, 255};   /* inactive-column row*/
static const Color COL_SEL_TX  = {255, 255, 255, 255};
static const Color COL_ACCENT  = {0, 200, 140, 255};

/* Common port presets. Index 0 = demo sentinel "-". The last entry is a
 * user-editable custom string. */
typedef struct { const char *label; const char *value; } port_preset_t;
static port_preset_t PORTS[] = {
    { "demo (no hardware)",     "-" },
    { "/dev/ttyUSB0",           "/dev/ttyUSB0" },
    { "/dev/ttyACM0",           "/dev/ttyACM0" },
    { "usbtmc:/dev/usbtmc0",    "usbtmc:/dev/usbtmc0" },
    { "prologix:/dev/ttyUSB0",  "prologix:/dev/ttyUSB0:22" },
    { "custom...",              NULL },   /* NULL => use edit buffer */
};
#define N_PORTS ((int)(sizeof(PORTS) / sizeof(PORTS[0])))
#define PORT_CUSTOM (N_PORTS - 1)

typedef enum { COL_METER = 0, COL_PORT = 1 } column_t;

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *r;
    TTF_Font     *font;      /* rows */
    TTF_Font     *font_sm;   /* header / hints */

    const dmm_driver_factory_t *const *drv;
    size_t n_drv;

    column_t col;
    int  meter_sel;
    int  meter_scroll;
    int  port_sel;

    char custom[96];
    bool editing_custom;

    char self_exe[1024];   /* path to psu_app, for spawning the view child */
    bool running;
} picker_t;

static void set_color(SDL_Renderer *r, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}
static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h}; SDL_RenderFillRect(r, &rc);
}
static int draw_text(SDL_Renderer *r, TTF_Font *f, const char *s,
                     int x, int y, Color c) {
    if (!f || !s || !*s) return 0;
    SDL_Color sc = {c.r, c.g, c.b, c.a};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s, sc);
    if (!surf) return 0;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, surf);
    int w = surf->w;
    if (t) { SDL_Rect d = {x, y, surf->w, surf->h}; SDL_RenderCopy(r, t, NULL, &d);
             SDL_DestroyTexture(t); }
    SDL_FreeSurface(surf);
    return w;
}

/* Truncate s into buf so it fits within max_px using font f (adds "..."). */
static const char *fit(TTF_Font *f, const char *s, int max_px, char *buf, size_t n) {
    int w = 0, h = 0;
    TTF_SizeUTF8(f, s, &w, &h);
    if (w <= max_px) return s;
    snprintf(buf, n, "%s", s);
    size_t len = strlen(buf);
    while (len > 1) {
        buf[--len] = '\0';
        char tmp[160];
        snprintf(tmp, sizeof(tmp), "%s...", buf);  /* separate dest: no overlap */
        TTF_SizeUTF8(f, tmp, &w, &h);
        if (w <= max_px) { snprintf(buf, n, "%.*s", (int)n - 1, tmp); return buf; }
    }
    return buf;
}

static const char *current_port_value(picker_t *P) {
    if (P->port_sel == PORT_CUSTOM) return P->custom;
    return PORTS[P->port_sel].value;
}

static void render(picker_t *P) {
    SDL_Renderer *r = P->r;
    set_color(r, COL_BG);
    SDL_RenderClear(r);

    /* Header. */
    set_color(r, COL_HEADER);
    fill_rect(r, 0, 0, WIN_W, HEADER_H);
    set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, HEADER_H - 1, WIN_W, HEADER_H - 1);
    draw_text(r, P->font_sm, "Select Multimeter", 8, 5, COL_TEXT);

    const int col_w = WIN_W / 2;
    const int list_y = HEADER_H + 2;
    const int foot_h = 16;
    const int list_h = WIN_H - list_y - foot_h;
    const int row_h = 16;
    const int rows_visible = list_h / row_h - 1; /* minus section label */

    /* ---- METER column ---- */
    int mx = 6;
    draw_text(r, P->font_sm, "METER", mx, list_y,
              P->col == COL_METER ? COL_ACCENT : COL_SECTION);
    /* keep selection in view */
    if (P->meter_sel < P->meter_scroll) P->meter_scroll = P->meter_sel;
    if (P->meter_sel >= P->meter_scroll + rows_visible)
        P->meter_scroll = P->meter_sel - rows_visible + 1;
    int my = list_y + row_h;
    for (int i = P->meter_scroll;
         i < (int)P->n_drv && i < P->meter_scroll + rows_visible; i++) {
        bool sel = (i == P->meter_sel);
        if (sel) {
            set_color(r, P->col == COL_METER ? COL_SEL_BG : COL_SEL_BG2);
            fill_rect(r, mx - 2, my, col_w - 8, row_h - 1);
        }
        char buf[64];
        const char *name = fit(P->font, P->drv[i]->display_name, col_w - 16, buf, sizeof(buf));
        draw_text(r, P->font, name, mx + 2, my + 1, sel ? COL_SEL_TX : COL_TEXT);
        my += row_h;
    }
    if ((int)P->n_drv > rows_visible)
        draw_text(r, P->font_sm,
                  (P->meter_scroll + rows_visible < (int)P->n_drv) ? "v" : " ",
                  col_w - 12, list_y + list_h - 14, COL_DIM);

    /* divider */
    set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, col_w, list_y, col_w, list_y + list_h);

    /* ---- PORT column ---- */
    int px = col_w + 6;
    draw_text(r, P->font_sm, "PORT", px, list_y,
              P->col == COL_PORT ? COL_ACCENT : COL_SECTION);
    int pyy = list_y + row_h;
    for (int i = 0; i < N_PORTS; i++) {
        bool sel = (i == P->port_sel);
        if (sel) {
            set_color(r, P->col == COL_PORT ? COL_SEL_BG : COL_SEL_BG2);
            fill_rect(r, px - 2, pyy, col_w - 8, row_h - 1);
        }
        const char *shown;
        char buf[80];
        if (i == PORT_CUSTOM && (P->editing_custom || P->custom[0])) {
            char tmp[112];
            snprintf(tmp, sizeof(tmp), "%s%s", P->custom,
                     (P->editing_custom && (SDL_GetTicks() / 500) % 2) ? "_" : "");
            shown = fit(P->font, tmp, col_w - 16, buf, sizeof(buf));
        } else {
            shown = fit(P->font, PORTS[i].label, col_w - 16, buf, sizeof(buf));
        }
        draw_text(r, P->font, shown, px + 2, pyy + 1, sel ? COL_SEL_TX : COL_TEXT);
        pyy += row_h;
    }

    /* ---- footer hint ---- */
    const char *hint = P->editing_custom
        ? "type port  ENTER accept"
        : "arrows move  TAB col  ENTER connect  ESC quit";
    draw_text(r, P->font_sm, hint, 6, WIN_H - 14, COL_DIM);

    SDL_RenderPresent(r);
}

/* Launch the compact view for the selected meter+port in a CHILD process and
 * wait for it. Running the view in its own process keeps the picker's SDL /
 * TTF context intact (the view calls SDL_Init/SDL_Quit itself, which would
 * otherwise tear down the picker) and gives the child sole ownership of the
 * screen. When the view window closes, control returns here. */
static void connect_selected(picker_t *P) {
    const dmm_driver_factory_t *fac = P->drv[P->meter_sel];
    const char *port = current_port_value(P);
    if (!port || !*port) port = "-";

    char arg_driver[64], arg_view[64], arg_port[160], arg_baud[32];
    snprintf(arg_driver, sizeof(arg_driver), "--driver=%s", fac->id);
    snprintf(arg_view,   sizeof(arg_view),   "--view=%s",   "dmm-compact");
    snprintf(arg_port,   sizeof(arg_port),   "--port=%s",   port);
    snprintf(arg_baud,   sizeof(arg_baud),   "--baud=%d",   fac->default_baud);
    char *argv[] = { P->self_exe, arg_driver, arg_view, arg_port, arg_baud, NULL };

    /* Hide the picker window so the child view owns the panel. */
    SDL_HideWindow(P->win);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "dmm-picker: fork failed\n");
        SDL_ShowWindow(P->win);
        return;
    }
    if (pid == 0) {
        execv(P->self_exe, argv);
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);

    SDL_ShowWindow(P->win);
    SDL_RaiseWindow(P->win);
}

static void move_sel(picker_t *P, int delta) {
    if (P->col == COL_METER) {
        int n = (int)P->n_drv;
        P->meter_sel = (P->meter_sel + delta % n + n) % n;
    } else {
        P->port_sel = (P->port_sel + delta % N_PORTS + N_PORTS) % N_PORTS;
    }
}

static void handle_key(picker_t *P, SDL_Keycode k) {
    if (P->editing_custom) {
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) { P->editing_custom = false; }
        else if (k == SDLK_ESCAPE)                  { P->editing_custom = false; }
        else if (k == SDLK_BACKSPACE) {
            size_t n = strlen(P->custom);
            if (n) P->custom[n - 1] = '\0';
        }
        return;
    }

    switch (k) {
        case SDLK_ESCAPE:
        case SDLK_q:      P->running = false; break;
        case SDLK_UP:     move_sel(P, -1); break;
        case SDLK_DOWN:   move_sel(P, +1); break;
        case SDLK_TAB:
        case SDLK_LEFT:
        case SDLK_RIGHT:  P->col = (P->col == COL_METER) ? COL_PORT : COL_METER; break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (P->col == COL_PORT && P->port_sel == PORT_CUSTOM) {
                P->editing_custom = true;           /* first Enter: edit */
                SDL_StartTextInput();
            } else {
                connect_selected(P);
            }
            break;
        default: break;
    }
}

static void handle_text(picker_t *P, const char *t) {
    if (!P->editing_custom) return;
    size_t n = strlen(P->custom);
    for (const char *p = t; *p && n < sizeof(P->custom) - 1; p++) {
        if (*p < 0x20) continue;
        P->custom[n++] = *p;
    }
    P->custom[n] = '\0';
}

static bool open_fonts(picker_t *P) {
    const char *path = pl_find_monospace_font();
    if (!path) { fprintf(stderr, "dmm-picker: no monospace TTF\n"); return false; }
    P->font    = TTF_OpenFont(path, 11);
    P->font_sm = TTF_OpenFont(path, 10);
    return P->font && P->font_sm;
}

static void cleanup(picker_t *P) {
    if (P->font)    TTF_CloseFont(P->font);
    if (P->font_sm) TTF_CloseFont(P->font_sm);
    if (P->r)   SDL_DestroyRenderer(P->r);
    if (P->win) SDL_DestroyWindow(P->win);
    TTF_Quit();
    SDL_Quit();
}

int dmm_picker_run(void) {
    picker_t P;
    memset(&P, 0, sizeof(P));
    P.drv = dmm_drivers_list(&P.n_drv);
    if (P.n_drv == 0) { fprintf(stderr, "dmm-picker: no DMM drivers\n"); return 1; }
    P.col = COL_METER;
    /* Default to the demo driver + demo port so ENTER works out of the box. */
    P.meter_sel = 0;
    for (size_t i = 0; i < P.n_drv; i++)
        if (strcmp(P.drv[i]->id, "dmm-demo") == 0) { P.meter_sel = (int)i; break; }
    P.port_sel = 0; /* "-" */
    if (!pl_self_exe(P.self_exe, sizeof(P.self_exe)))
        snprintf(P.self_exe, sizeof(P.self_exe), "psu_app");
    P.running  = true;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    if (TTF_Init() < 0) { SDL_Quit(); return 1; }
    P.win = SDL_CreateWindow("Open LabBench - DMM",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!P.win) { cleanup(&P); return 1; }
    P.r = SDL_CreateRenderer(P.win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!P.r) P.r = SDL_CreateRenderer(P.win, -1, 0);
    if (!P.r) { cleanup(&P); return 1; }
    SDL_SetRenderDrawBlendMode(P.r, SDL_BLENDMODE_BLEND);
    if (!open_fonts(&P)) { cleanup(&P); return 1; }

    while (P.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:      P.running = false; break;
                case SDL_KEYDOWN:   handle_key(&P, ev.key.keysym.sym); break;
                case SDL_TEXTINPUT: handle_text(&P, ev.text.text); break;
                default: break;
            }
        }
        render(&P);
        SDL_Delay(16);
    }
    cleanup(&P);
    return 0;
}
