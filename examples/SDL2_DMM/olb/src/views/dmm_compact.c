/**
 * DMM compact — 320x170 stacked readout for small panels (CardputerZero).
 *
 * A purpose-built small-screen sibling of dmm_toolbar.c: instead of one wide
 * 700x80 row, it stacks the readout vertically to suit a near-square 320x170
 * display. Same driver interface and VFD dot-matrix renderer, so it works
 * with every DMM driver (demo, OWON, SCPI over USB-serial / Prologix GPIB,
 * native HP-IB).
 *
 *   +--------------------------------------------------+
 *   | DMM  <model>                                  () |  header + link LED
 *   |  DCV                                             |  mode label
 *   |         3 3.5 0 0 4 1 2  V                        |  big VFD reading + unit
 *   |                                                  |
 *   |  [AUTO] [MED]                            [OL]    |  badges
 *   +--------------------------------------------------+
 *
 * Read-only: mode/range changes happen on the meter front panel.
 */

#include "views.h"
#include "vfd_dotmatrix.h"
#include "platform/platform.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W    320
#define WIN_H    170
#define HEADER_H 22

typedef struct { Uint8 r, g, b, a; } Color;

static const Color COL_BG_DARK  = {22, 22, 24, 255};
static const Color COL_BG_PANEL = {32, 32, 36, 255};
static const Color COL_HEADER   = {20, 20, 22, 255};
static const Color COL_BORDER   = {55, 55, 60, 255};
static const Color COL_VAL      = {0, 255, 140, 255};
static const Color COL_UNIT     = {120, 220, 255, 255};
static const Color COL_MODE     = {255, 200, 80, 255};
static const Color COL_DIM      = {100, 100, 108, 255};
static const Color COL_ON       = {0, 255, 100, 255};
static const Color COL_ERR      = {255, 70, 70, 255};
static const Color COL_BADGE_BG = {50, 50, 55, 255};
static const Color COL_BADGE_HOT= {180, 30, 30, 255};
static const Color COL_TEXT     = {220, 220, 225, 255};

typedef struct {
    dmm_driver_t *drv;
    SDL_Window   *window;
    SDL_Renderer *renderer;
    TTF_Font     *font_small;   /* header / badges */
    TTF_Font     *font_mode;    /* mode + unit */
    dmm_reading_t reading;
    bool          running;
} app_t;

static void set_color(SDL_Renderer *r, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}
static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h}; SDL_RenderFillRect(r, &rc);
}
static void draw_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h}; SDL_RenderDrawRect(r, &rc);
}
static int draw_text(SDL_Renderer *r, TTF_Font *font, const char *text,
                     int x, int y, Color color) {
    if (!font || !text || !*text) return 0;
    SDL_Color c = {color.r, color.g, color.b, color.a};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, c);
    if (!surf) return 0;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    int w = surf->w;
    if (tex) {
        SDL_Rect dst = {x, y, surf->w, surf->h};
        SDL_RenderCopy(r, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    return w;
}
static void draw_led(SDL_Renderer *r, int cx, int cy, int rad, Color col) {
    set_color(r, col);
    for (int dy = -rad; dy <= rad; dy++)
        for (int dx = -rad; dx <= rad; dx++)
            if (dx * dx + dy * dy <= rad * rad)
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
}

/* Scale value into engineering range [1,1000) and pick an SI prefix. */
static float engineering_scale(float value, const char **prefix) {
    static const struct { float scale; const char *p; } table[] = {
        {1e12f, "T"}, {1e9f, "G"}, {1e6f, "M"}, {1e3f, "k"},
        {1.0f,  ""},  {1e-3f, "m"}, {1e-6f, "µ"}, {1e-9f, "n"}, {1e-12f, "p"},
    };
    if (value == 0.0f) { *prefix = ""; return 0.0f; }
    float abs_v = fabsf(value);
    int idx = (int)(sizeof(table) / sizeof(table[0])) - 1;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (abs_v >= table[i].scale) { idx = (int)i; break; }
    }
    *prefix = table[idx].p;
    return value / table[idx].scale;
}

/* Draw one badge, right-anchored at bx; returns new bx to its left. */
static int badge(app_t *a, int bx, int by, int bh, const char *lbl,
                 Color bg, Color fg) {
    int tw = 0, th = 0;
    TTF_SizeText(a->font_small, lbl, &tw, &th);
    int bw = tw + 12;
    set_color(a->renderer, bg);
    fill_rect(a->renderer, bx - bw, by, bw, bh);
    set_color(a->renderer, COL_BORDER);
    draw_rect(a->renderer, bx - bw, by, bw, bh);
    draw_text(a->renderer, a->font_small, lbl, bx - bw + 6, by + (bh - th) / 2, fg);
    return bx - bw - 6;
}

static void render(app_t *a) {
    SDL_Renderer *r = a->renderer;
    set_color(r, COL_BG_DARK);
    SDL_RenderClear(r);

    /* Header: "DMM  <model>" + connection LED. */
    set_color(r, COL_HEADER);
    fill_rect(r, 0, 0, WIN_W, HEADER_H);
    set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, HEADER_H - 1, WIN_W, HEADER_H - 1);
    int hx = draw_text(r, a->font_small, "DMM", 8, 5, COL_TEXT);
    if (a->drv->caps.model_name)
        draw_text(r, a->font_small, a->drv->caps.model_name, 8 + hx + 8, 5, COL_DIM);
    bool ok = a->drv->is_connected(a->drv);
    draw_led(r, WIN_W - 12, HEADER_H / 2, 4, ok ? COL_ON : COL_ERR);

    /* Reading panel. */
    int py = HEADER_H + 6;
    int ph = WIN_H - py - 6;
    set_color(r, COL_BG_PANEL);
    fill_rect(r, 6, py, WIN_W - 12, ph);
    set_color(r, COL_BORDER);
    draw_rect(r, 6, py, WIN_W - 12, ph);

    /* Mode label, top-left of panel. */
    draw_text(r, a->font_mode, dmm_mode_label(a->reading.mode), 16, py + 8, COL_MODE);

    /* Big VFD reading, centered. Small dots (radius 2, 1px gap) render sharp
     * on the 320x170 panel -- ~25px/glyph so a full high-resolution field
     * fits with room for the unit, instead of a few blobby digits. */
    int dot_size = 2, dot_gap = 1, char_gap = 5;
    int char_h = vfd_char_height(dot_size, dot_gap);
    int vfd_y  = py + (ph - char_h) / 2 + 6;
    vfd_color_t vfd_off = { 0, 22, 13, 255 };

    if (a->reading.overload) {
        vfd_color_t vfd_err = { COL_ERR.r, COL_ERR.g, COL_ERR.b, COL_ERR.a };
        const char *s = "-OL-";
        int w = (int)strlen(s) * (vfd_char_width(dot_size, dot_gap) + char_gap);
        vfd_draw_number(r, (WIN_W - w) / 2, vfd_y, s,
                        dot_size, dot_gap, char_gap, vfd_err, vfd_off, true);
    } else if (!a->reading.valid) {
        vfd_color_t vfd_dim = { COL_DIM.r, COL_DIM.g, COL_DIM.b, COL_DIM.a };
        const char *s = "------";
        int w = (int)strlen(s) * (vfd_char_width(dot_size, dot_gap) + char_gap);
        vfd_draw_number(r, (WIN_W - w) / 2, vfd_y, s,
                        dot_size, dot_gap, char_gap, vfd_dim, vfd_off, true);
    } else {
        const char *prefix = "";
        float scaled = engineering_scale(a->reading.value, &prefix);
        /* ~6 significant digits (the sharp small-dot field is wide enough):
         * XXX.XXX / XX.XXXX / X.XXXXX. */
        float abs_v = fabsf(scaled);
        int decimals = (abs_v >= 100.0f) ? 3 : (abs_v >= 10.0f) ? 4 : 5;
        char vbuf[24];
        snprintf(vbuf, sizeof(vbuf), "%.*f", decimals, scaled);

        char unit[16];
        snprintf(unit, sizeof(unit), "%s%s", prefix, dmm_mode_unit(a->reading.mode));

        /* Center the number, then place the unit just to its right. */
        int cw = vfd_char_width(dot_size, dot_gap) + char_gap;
        int uw = 0, uh = 0;
        TTF_SizeUTF8(a->font_mode, unit, &uw, &uh);
        int num_w = (int)strlen(vbuf) * cw;
        int total = num_w + 10 + uw;
        int nx = (WIN_W - total) / 2;
        if (nx < 12) nx = 12;

        vfd_color_t vfd_on = { COL_VAL.r, COL_VAL.g, COL_VAL.b, COL_VAL.a };
        int vw = vfd_draw_number(r, nx, vfd_y, vbuf,
                                 dot_size, dot_gap, char_gap, vfd_on, vfd_off, true);
        draw_text(r, a->font_mode, unit, nx + vw + 10, vfd_y + char_h / 2 - 9, COL_UNIT);
    }

    /* Bottom badge row: AUTO / rate on the right, OL flag on the far right. */
    int bh = 18;
    int by = py + ph - bh - 6;
    int bx = WIN_W - 14;
    if (a->reading.overload)
        bx = badge(a, bx, by, bh, "OL", COL_BADGE_HOT, (Color){255,255,255,255});
    const char *rate = (a->reading.rate == DMM_RATE_SLOW) ? "SLOW"
                     : (a->reading.rate == DMM_RATE_FAST) ? "FAST" : "MED";
    bx = badge(a, bx, by, bh, rate, COL_BADGE_BG, COL_DIM);
    if (a->reading.range == 0.0f)
        bx = badge(a, bx, by, bh, "AUTO", COL_BADGE_BG, COL_TEXT);
    (void)bx;

    SDL_RenderPresent(r);
}

static bool open_fonts(app_t *a) {
    const char *path = pl_find_monospace_font();
    if (!path) {
        fprintf(stderr, "dmm_compact: no monospace TTF available\n");
        return false;
    }
    a->font_small = TTF_OpenFont(path, 12);
    a->font_mode  = TTF_OpenFont(path, 16);
    return a->font_small && a->font_mode;
}

static void cleanup(app_t *a) {
    if (a->font_small) TTF_CloseFont(a->font_small);
    if (a->font_mode)  TTF_CloseFont(a->font_mode);
    if (a->renderer)   SDL_DestroyRenderer(a->renderer);
    if (a->window)     SDL_DestroyWindow(a->window);
    TTF_Quit();
    SDL_Quit();
}

int view_dmm_compact_run(dmm_driver_t *drv) {
    if (!drv) return 1;

    app_t a;
    memset(&a, 0, sizeof(a));
    a.drv     = drv;
    a.running = true;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    if (TTF_Init() < 0) { SDL_Quit(); return 1; }

    a.window = SDL_CreateWindow("Open LabBench — DMM",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!a.window) { cleanup(&a); return 1; }

    a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!a.renderer) a.renderer = SDL_CreateRenderer(a.window, -1, 0);
    if (!a.renderer) { cleanup(&a); return 1; }
    SDL_SetRenderDrawBlendMode(a.renderer, SDL_BLENDMODE_BLEND);

    if (!open_fonts(&a)) { cleanup(&a); return 1; }

    while (a.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) a.running = false;
            if (ev.type == SDL_KEYDOWN &&
                (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q))
                a.running = false;
        }
        a.drv->read(a.drv, &a.reading);
        render(&a);
        SDL_Delay(16);
    }

    cleanup(&a);
    return 0;
}
