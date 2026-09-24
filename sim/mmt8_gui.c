#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "emu8051.h"
#include "mmt8_hw.h"
#include "mmt8_gui.h"
#include "mmt8_keys.h"

/* ---- Window dimensions ---- */
#define WIN_W 780
#define WIN_H 600

/* ---- Colors ---- */
#define COL_BG_R       88
#define COL_BG_G       90
#define COL_BG_B       94
#define COL_LCD_BG_R   0x33
#define COL_LCD_BG_G   0x22
#define COL_LCD_BG_B   0x00
#define COL_LCD_FG_R   0xFF
#define COL_LCD_FG_G   0xAA
#define COL_LCD_FG_B   0x00
#define COL_BTN_R      205
#define COL_BTN_G      203
#define COL_BTN_B      196
#define COL_BTN_PR_R   150
#define COL_BTN_PR_G   148
#define COL_BTN_PR_B   142
#define COL_TXT_R      30
#define COL_TXT_G      30
#define COL_TXT_B      34
#define COL_LED_ON_R   0
#define COL_LED_ON_G   220
#define COL_LED_ON_B   0
#define COL_LED_OFF_R  30
#define COL_LED_OFF_G  40
#define COL_LED_OFF_B  30
#define COL_LED_RED_R  220
#define COL_LED_RED_G  0
#define COL_LED_RED_B  0
#define COL_LED_RED_OFF_R  40
#define COL_LED_RED_OFF_G  20
#define COL_LED_RED_OFF_B  20

/* ---- Button definition ---- */
typedef struct {
    SDL_Rect rect;
    const char *label;
    int col;          /* keyboard matrix column */
    int row;          /* keyboard matrix row */
    int pressed;      /* by the mouse */
    int key_held;     /* by the keyboard */
    int led_src;      /* LED_NONE, LED_TRACK (led_data latch) or LED_STATUS (status latch); both active low */
    int led_bit;      /* bit number within that latch */
    int led_red;      /* 1 = red LED (REC), else green */
    int led_side;     /* LED_ABOVE, LED_RIGHT or LED_LEFT of the button */
    int color;        /* KEY_GREY, KEY_GREEN or KEY_RED */
} button_t;

enum { LED_ABOVE = 0, LED_RIGHT = 1, LED_LEFT = 2 };
enum { KEY_GREY = 0, KEY_GREEN = 1, KEY_RED = 2 };

enum { LED_NONE = 0, LED_TRACK = 1, LED_STATUS = 2 };

/* ---- LED position relative to button ---- */
#define LED_RADIUS 4

/* ---- Forward declarations ---- */
static void init_buttons(void);
static button_t *find_button(int x, int y);

/* ---- State ---- */
static SDL_Window   *window;
static SDL_Renderer *renderer;
static TTF_Font     *font_lcd;
static TTF_Font     *font_btn;
static TTF_Font     *font_small;

/* Buttons array — filled in init_buttons() */
#define MAX_BUTTONS 50
static button_t buttons[MAX_BUTTONS];
static int num_buttons;

/* ---- Button layout helpers ---- */

static void add_button(int x, int y, int w, int h,
                       const char *label, const char *key,
                       int led_src, int led_bit, int led_red)
{
    if (num_buttons >= MAX_BUTTONS) return;
    const mmt8_key_t *k = mmt8_key_find(key);
    if (!k) { fprintf(stderr, "GUI: unknown key '%s'\n", key); return; }
    button_t *b = &buttons[num_buttons++];
    b->rect = (SDL_Rect){x, y, w, h};
    b->label = label;
    b->col = k->col;
    b->row = k->row;
    b->pressed = 0;
    b->led_src = led_src;
    b->led_bit = led_bit;
    b->led_red = led_red;
    b->led_side = LED_ABOVE;
    b->color = KEY_GREY;
}

static button_t *last_button(void) { return num_buttons ? &buttons[num_buttons - 1] : NULL; }

/* Button layout. Matrix positions come from mmt8_keys.c; LED sources:
 * track LEDs are bits of the LED data latch, the mode and transport LEDs
 * are bits of the status latch. Both latches are active low. */
static void init_buttons(void)
{
    /*
     * The front panel, as on the unit (left to right):
     *   a 4x3 block of function keys with PAGE DOWN / PAGE UP beneath,
     *   the LCD with the keypad under it,
     *   a column of six keys on the right,
     *   eight TRACK keys with LEDs across the middle,
     *   and the transport along the bottom.
     */
    int kw = 60, kh = 24, gap = 8;
    num_buttons = 0;

    /* --- left block --- */
    int x0 = 36, y0 = 150, cw = kw + gap, rh = kh + 14;
    const char *grid[4][3][2] = {
        { {"QUANT", "QUANT"}, {"LENGTH", "LENGTH"}, {"PART", "PART"} },
        { {"COPY",  "COPY"},  {"NAME",   "NAME"},   {"EDIT", "EDIT"} },
        { {"TRANS", "TRANS"}, {"MERGE",  "MERGE"},  {"SONG", "SONG"} },
        { {"ERASE", "ERASE"}, {"TAPE",   "TAPE"},   {"MIDI CHAN", "MIDICH"} },
    };
    const int mode_led_bit[3] = {2, 3, 4};   /* PART, EDIT, SONG in the status latch */
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 3; c++) {
            int led = (c == 2 && r < 3) ? LED_STATUS : LED_NONE;
            add_button(x0 + c * cw, y0 + r * rh, kw, kh, grid[r][c][0], grid[r][c][1],
                       led, led ? mode_led_bit[r] : 0, 0);
            if (led) last_button()->led_side = LED_RIGHT;
        }
    add_button(x0,               y0 + 4 * rh + 6, kw + 16, kh, "PAGE DOWN", "PGDN", LED_NONE, 0, 0);
    add_button(x0 + 2 * cw - 16, y0 + 4 * rh + 6, kw + 16, kh, "PAGE UP",   "PGUP", LED_NONE, 0, 0);

    /* --- keypad under the LCD --- */
    int kx = 300, ky = 238, nw = 34, nh = 24, ng = 12;
    static const char *row1[] = {"1", "2", "3", "4", "5"};
    static const char *row2[] = {"6", "7", "8", "9", "0"};
    for (int i = 0; i < 5; i++) {
        add_button(kx + i * (nw + ng), ky,           nw, nh, row1[i], row1[i], LED_NONE, 0, 0);
        add_button(kx + i * (nw + ng), ky + nh + 12, nw, nh, row2[i], row2[i], LED_NONE, 0, 0);
    }
    add_button(kx,                 ky + 2 * (nh + 12), nw, nh, "-", "MINUS", LED_NONE, 0, 0);
    add_button(kx + 4 * (nw + ng), ky + 2 * (nh + 12), nw, nh, "+", "PLUS",  LED_NONE, 0, 0);

    /* --- right column --- */
    int rx = 626, ry = 150, rrh = 34;
    add_button(rx, ry + 0 * rrh, kw + 24, kh, "LOOP",        "LOOP",   LED_STATUS, 6, 0); last_button()->led_side = LED_LEFT;
    add_button(rx, ry + 1 * rrh, kw + 24, kh, "MIDI ECHO",   "ECHO",   LED_STATUS, 5, 0); last_button()->led_side = LED_LEFT;
    add_button(rx, ry + 2 * rrh, kw + 24, kh, "MIDI FILTER", "FILTER", LED_NONE, 0, 0);
    add_button(rx, ry + 3 * rrh, kw + 24, kh, "CLOCK",       "CLOCK",  LED_NONE, 0, 0);
    add_button(rx, ry + 4 * rrh, kw + 24, kh, "CLICK",       "CLICK",  LED_NONE, 0, 0);
    add_button(rx, ry + 5 * rrh, kw + 24, kh, "TEMPO",       "TEMPO",  LED_NONE, 0, 0);

    /* --- track keys --- */
    int tx = 36, ty = 420, tw = 74, th = 26, tg = 10;
    static const char *tlabels[] = {"TRACK 1","TRACK 2","TRACK 3","TRACK 4","TRACK 5","TRACK 6","TRACK 7","TRACK 8"};
    static const char *tkeys[]   = {"T1","T2","T3","T4","T5","T6","T7","T8"};
    for (int i = 0; i < 8; i++)
        add_button(tx + i * (tw + tg), ty, tw, th, tlabels[i], tkeys[i], LED_TRACK, i, 0);

    /* --- transport --- */
    int py = 520, pw = 62, ph = 30;
    add_button(36,  py, 48, ph, "<<", "REW", LED_NONE, 0, 0);
    add_button(96,  py, 48, ph, ">>", "FF",  LED_NONE, 0, 0);
    add_button(420, py, pw, ph, "PLAY", "PLAY", LED_STATUS, 0, 0); last_button()->color = KEY_GREEN;
    add_button(500, py, pw + 16, ph, "STOP/CONT", "STOP", LED_NONE, 0, 0);
    add_button(600, py, pw, ph, "RECORD", "REC", LED_STATUS, 1, 1); last_button()->color = KEY_RED;
}

/*
 * Keyboard shortcuts. Holding a key holds the button, independently of the
 * mouse, so two-button gestures (ERASE + RECORD, RECORD + LENGTH, ...) are
 * possible. Escape quits (handled in main.c).
 */
static const struct { SDL_Keycode sym; const char *key; const char *hint; } shortcuts[] = {
    { SDLK_SPACE,    "PLAY",   "Spc" }, { SDLK_s,        "STOP",   "S" },
    { SDLK_r,        "REC",    "R" },   { SDLK_LEFT,     "REW",    "<-" },  { SDLK_RIGHT,    "FF",     "->" },
    { SDLK_p,        "PART",   "P" },   { SDLK_o,        "SONG",   "O" },   { SDLK_d,        "EDIT",   "D" },
    { SDLK_n,        "NAME",   "N" },   { SDLK_PAGEUP,   "PGUP",   "PgUp" }, { SDLK_PAGEDOWN, "PGDN",  "PgDn" },
    { SDLK_t,        "TEMPO",  "T" },   { SDLK_k,        "CLICK",  "K" },   { SDLK_c,        "COPY",   "C" },
    { SDLK_e,        "ERASE",  "E" },   { SDLK_l,        "LOOP",   "L" },   { SDLK_h,        "ECHO",   "H" },
    { SDLK_g,        "LENGTH", "G" },   { SDLK_m,        "MERGE",  "M" },   { SDLK_q,        "QUANT",  "Q" },
    { SDLK_x,        "TRANS",  "X" },   { SDLK_f,        "FILTER", "F" },   { SDLK_i,        "MIDICH", "I" },
    { SDLK_j,        "CLOCK",  "J" },   { SDLK_y,        "TAPE",   "Y" },
    { SDLK_F1, "T1", "F1" }, { SDLK_F2, "T2", "F2" }, { SDLK_F3, "T3", "F3" }, { SDLK_F4, "T4", "F4" },
    { SDLK_F5, "T5", "F5" }, { SDLK_F6, "T6", "F6" }, { SDLK_F7, "T7", "F7" }, { SDLK_F8, "T8", "F8" },
    { SDLK_0, "0", "0" }, { SDLK_1, "1", "1" }, { SDLK_2, "2", "2" }, { SDLK_3, "3", "3" }, { SDLK_4, "4", "4" },
    { SDLK_5, "5", "5" }, { SDLK_6, "6", "6" }, { SDLK_7, "7", "7" }, { SDLK_8, "8", "8" }, { SDLK_9, "9", "9" },
    { SDLK_KP_0, "0", NULL }, { SDLK_KP_1, "1", NULL }, { SDLK_KP_2, "2", NULL }, { SDLK_KP_3, "3", NULL },
    { SDLK_KP_4, "4", NULL }, { SDLK_KP_5, "5", NULL }, { SDLK_KP_6, "6", NULL }, { SDLK_KP_7, "7", NULL },
    { SDLK_KP_8, "8", NULL }, { SDLK_KP_9, "9", NULL },
    { SDLK_EQUALS, "PLUS", "=" }, { SDLK_KP_PLUS, "PLUS", NULL }, { SDLK_MINUS, "MINUS", "-" }, { SDLK_KP_MINUS, "MINUS", NULL },
};
#define NUM_SHORTCUTS (int)(sizeof(shortcuts) / sizeof(shortcuts[0]))

static button_t *find_button_by_key(const char *key)
{
    const mmt8_key_t *k = mmt8_key_find(key);
    if (!k) return NULL;
    for (int i = 0; i < num_buttons; i++)
        if (buttons[i].col == k->col && buttons[i].row == k->row)
            return &buttons[i];
    return NULL;
}

static const char *hint_for(const button_t *b)
{
    for (int i = 0; i < NUM_SHORTCUTS; i++) {
        const mmt8_key_t *k = mmt8_key_find(shortcuts[i].key);
        if (k && k->col == b->col && k->row == b->row && shortcuts[i].hint)
            return shortcuts[i].hint;
    }
    return NULL;
}

/* Find which button contains point (x,y) */
static button_t *find_button(int x, int y)
{
    for (int i = 0; i < num_buttons; i++) {
        SDL_Rect *r = &buttons[i].rect;
        if (x >= r->x && x < r->x + r->w &&
            y >= r->y && y < r->y + r->h)
            return &buttons[i];
    }
    return NULL;
}

/* ---- Rendering ---- */

static void render_lcd(void)
{
    lcd_state_t *lcd = mmt8_get_lcd();
    SDL_Rect border = {286, 150, 246, 74};
    SDL_Rect inner  = {290, 154, 238, 66};

    /* Header strip with the model name, and the ALESIS badge */
    SDL_Rect strip = {0, 0, WIN_W, 70};
    SDL_SetRenderDrawColor(renderer, 60, 62, 66, 255);
    SDL_RenderFillRect(renderer, &strip);
    for (int i = 0; i < WIN_W; i += 4) { SDL_SetRenderDrawColor(renderer, 40, 42, 46, 255); SDL_RenderDrawLine(renderer, i, 0, i, 12); }
    if (font_lcd) {
        SDL_Color white = {235, 235, 235, 255};
        SDL_Surface *surf = TTF_RenderText_Blended(font_lcd, "MMT-8", white);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_Rect dst = {36, 24, surf->w * 2, surf->h * 2};
            SDL_RenderCopy(renderer, tex, NULL, &dst);
            SDL_DestroyTexture(tex); SDL_FreeSurface(surf);
        }
        if (font_small) {
            surf = TTF_RenderText_Blended(font_small, "MULTI TRACK MIDI RECORDER", white);
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_Rect dst = {200, 46, surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, NULL, &dst);
                SDL_DestroyTexture(tex); SDL_FreeSurface(surf);
            }
            SDL_Rect badge = {36, 100, 74, 22};
            SDL_SetRenderDrawColor(renderer, 20, 20, 22, 255); SDL_RenderFillRect(renderer, &badge);
            surf = TTF_RenderText_Blended(font_btn ? font_btn : font_small, "ALESIS", white);
            if (surf) {
                SDL_Texture *tex = TTF_RenderText_Blended ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
                SDL_Rect dst = {badge.x + (badge.w - surf->w) / 2, badge.y + (badge.h - surf->h) / 2, surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, NULL, &dst);
                SDL_DestroyTexture(tex); SDL_FreeSurface(surf);
            }
        }
    }

    /* Border */
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderDrawRect(renderer, &border);

    /* Background */
    SDL_SetRenderDrawColor(renderer, COL_LCD_BG_R, COL_LCD_BG_G, COL_LCD_BG_B, 255);
    SDL_RenderFillRect(renderer, &inner);

    if (!font_lcd) return;

    SDL_Color fg = {COL_LCD_FG_R, COL_LCD_FG_G, COL_LCD_FG_B, 255};

    for (int line = 0; line < 2; line++) {
        char text[17];
        for (int i = 0; i < 16; i++) {
            uint8_t ch = lcd->ddram[line][i];
            if (ch < 0x20 || ch > 0x7E)
                text[i] = ' ';
            else
                text[i] = ch;
        }
        text[16] = '\0';

        SDL_Surface *surf = TTF_RenderText_Blended(font_lcd, text, fg);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_Rect dst = {296, 158 + line * 30, surf->w, surf->h};
            SDL_RenderCopy(renderer, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }
    }
}

static void render_buttons(void)
{
    uint8_t led_d = mmt8_get_led_data();
    uint8_t led_s = mmt8_get_status_latch();

    SDL_Color txt_color = {COL_TXT_R, COL_TXT_G, COL_TXT_B, 255};

    for (int i = 0; i < num_buttons; i++) {
        button_t *b = &buttons[i];

        /* Button fill: grey membrane keys, green PLAY, red RECORD */
        int down = b->pressed || b->key_held;
        if (b->color == KEY_GREEN)     SDL_SetRenderDrawColor(renderer, down ? 40 : 70, down ? 120 : 170, down ? 60 : 90, 255);
        else if (b->color == KEY_RED)  SDL_SetRenderDrawColor(renderer, down ? 150 : 205, down ? 40 : 60, down ? 40 : 55, 255);
        else if (down)                 SDL_SetRenderDrawColor(renderer, COL_BTN_PR_R, COL_BTN_PR_G, COL_BTN_PR_B, 255);
        else                           SDL_SetRenderDrawColor(renderer, COL_BTN_R, COL_BTN_G, COL_BTN_B, 255);
        SDL_RenderFillRect(renderer, &b->rect);

        /* Button border */
        SDL_SetRenderDrawColor(renderer, 40, 40, 44, 255);
        SDL_RenderDrawRect(renderer, &b->rect);

        /* Label */
        int label_w = 0;
        if (font_btn && b->label) {
            TTF_Font *f = (strlen(b->label) > 3) ? font_small : font_btn;
            SDL_Surface *surf = TTF_RenderText_Blended(f, b->label, txt_color);
            if (surf) {
                label_w = surf->w;
                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                int tx = b->rect.x + (b->rect.w - surf->w) / 2;
                int ty = b->rect.y + (b->rect.h - surf->h) / 2;
                SDL_Rect dst = {tx, ty, surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }

        /* Keyboard shortcut hint in the corner */
        const char *hint = hint_for(b);
        if (font_small && hint && b->rect.w - label_w >= 40) {
            SDL_Color dim = {140, 140, 150, 255};
            SDL_Surface *surf = TTF_RenderText_Blended(font_small, hint, dim);
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_Rect dst = {b->rect.x + b->rect.w - surf->w - 2, b->rect.y + 1, surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }

        /* LED indicator */
        if (b->led_src != LED_NONE) {
            int cx = b->rect.x + b->rect.w / 2;
            int cy = b->rect.y - LED_RADIUS - 3;
            if (b->led_side == LED_RIGHT) { cx = b->rect.x + b->rect.w + LED_RADIUS + 6; cy = b->rect.y + b->rect.h / 2; }
            if (b->led_side == LED_LEFT)  { cx = b->rect.x - LED_RADIUS - 6;             cy = b->rect.y + b->rect.h / 2; }
            /* both latches are active low: a cleared bit lights the LED */
            int on = (b->led_src == LED_TRACK)
                   ? !((led_d >> b->led_bit) & 1)
                   : !((led_s >> b->led_bit) & 1);

            if (b->led_red) {
                /* Red LED (REC) */
                if (on)
                    SDL_SetRenderDrawColor(renderer, COL_LED_RED_R, COL_LED_RED_G, COL_LED_RED_B, 255);
                else
                    SDL_SetRenderDrawColor(renderer, COL_LED_RED_OFF_R, COL_LED_RED_OFF_G, COL_LED_RED_OFF_B, 255);
            } else {
                /* Green LED */
                if (on)
                    SDL_SetRenderDrawColor(renderer, COL_LED_ON_R, COL_LED_ON_G, COL_LED_ON_B, 255);
                else
                    SDL_SetRenderDrawColor(renderer, COL_LED_OFF_R, COL_LED_OFF_G, COL_LED_OFF_B, 255);
            }

            /* Draw filled circle (simple square approximation) */
            SDL_Rect led_rect = {cx - LED_RADIUS, cy - LED_RADIUS,
                                 LED_RADIUS * 2, LED_RADIUS * 2};
            SDL_RenderFillRect(renderer, &led_rect);
        }
    }
}

/* MIDI IN / OUT activity dots: lit for a moment after each message byte moves.
 * Real-time bytes (clock, start/stop, active sensing) are ignored, otherwise
 * the OUT dot would flicker constantly whenever the sequencer runs. */
#define MIDI_LED_HOLD_MS 120
static void render_midi_activity(void)
{
    static unsigned long last_rx, last_tx;
    static Uint32 rx_until, tx_until;
    const mmt8_uart_stats_t *st = mmt8_uart_stats();
    Uint32 now = SDL_GetTicks();
    if (st->rx_msg_bytes != last_rx) { last_rx = st->rx_msg_bytes; rx_until = now + MIDI_LED_HOLD_MS; }
    if (st->tx_msg_bytes != last_tx) { last_tx = st->tx_msg_bytes; tx_until = now + MIDI_LED_HOLD_MS; }

    struct { const char *label; int on; int x; } dots[2] = {
        { "MIDI IN",  now < rx_until, 560 },
        { "MIDI OUT", now < tx_until, 660 },
    };
    SDL_Color txt = {COL_TXT_R, COL_TXT_G, COL_TXT_B, 255};
    for (int i = 0; i < 2; i++) {
        SDL_Rect r = {dots[i].x, 40 - LED_RADIUS, LED_RADIUS * 2, LED_RADIUS * 2};
        if (dots[i].on) SDL_SetRenderDrawColor(renderer, 255, 170, 0, 255);
        else            SDL_SetRenderDrawColor(renderer, 60, 45, 20, 255);
        SDL_RenderFillRect(renderer, &r);
        if (font_small) {
            SDL_Surface *surf = TTF_RenderText_Blended(font_small, dots[i].label, txt);
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_Rect dst = {dots[i].x + LED_RADIUS * 2 + 4, 40 - surf->h / 2, surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }
    }
}

/* ---- Public API ---- */

int gui_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return -1;
    }

    window = SDL_CreateWindow("MMT-8 Simulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)   /* e.g. no GPU, or SDL_VIDEODRIVER=dummy: fall back to software */
        renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Try to load a monospace font for the LCD */
    const char *font_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
        NULL
    };

    const char *font_path = NULL;
    for (int i = 0; font_paths[i]; i++) {
        FILE *f = fopen(font_paths[i], "r");
        if (f) {
            fclose(f);
            font_path = font_paths[i];
            break;
        }
    }

    if (font_path) {
        font_lcd   = TTF_OpenFont(font_path, 22);
        font_btn   = TTF_OpenFont(font_path, 12);
        font_small = TTF_OpenFont(font_path, 10);
    } else {
        fprintf(stderr, "Warning: no monospace font found, text will not render\n");
    }

    init_buttons();
    return 0;
}

void gui_handle_event(SDL_Event *ev)
{
    if (ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP) {
        if (ev->key.repeat) return;
        for (int i = 0; i < NUM_SHORTCUTS; i++) {
            if (shortcuts[i].sym != ev->key.keysym.sym) continue;
            button_t *b = find_button_by_key(shortcuts[i].key);
            if (!b) return;
            if (ev->type == SDL_KEYDOWN) {
                if (!b->key_held) { b->key_held = 1; if (!b->pressed) mmt8_key_press(b->col, b->row); }
            } else {
                if (b->key_held) { b->key_held = 0; if (!b->pressed) mmt8_key_release(b->col, b->row); }
            }
            return;
        }
        return;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN) {
        button_t *b = find_button(ev->button.x, ev->button.y);
        if (b) {
            b->pressed = 1;
            if (!b->key_held) mmt8_key_press(b->col, b->row);
        }
    } else if (ev->type == SDL_MOUSEBUTTONUP) {
        /* Release all mouse-pressed buttons (keys held on the keyboard stay down) */
        for (int i = 0; i < num_buttons; i++) {
            if (buttons[i].pressed) {
                buttons[i].pressed = 0;
                if (!buttons[i].key_held) mmt8_key_release(buttons[i].col, buttons[i].row);
            }
        }
    }
}

void gui_render(struct em8051 *cpu)
{
    (void)cpu;

    /* Clear background */
    SDL_SetRenderDrawColor(renderer, COL_BG_R, COL_BG_G, COL_BG_B, 255);
    SDL_RenderClear(renderer);

    render_lcd();
    render_buttons();
    render_midi_activity();

    SDL_RenderPresent(renderer);
}

int gui_screenshot(const char *path)
{
    if (!renderer) return -1;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, WIN_W, WIN_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return -1;
    int rc = SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch);
    if (rc == 0) rc = SDL_SaveBMP(surf, path);
    SDL_FreeSurface(surf);
    if (rc != 0) fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError());
    return rc;
}

void gui_shutdown(void)
{
    if (font_lcd)   TTF_CloseFont(font_lcd);
    if (font_btn)   TTF_CloseFont(font_btn);
    if (font_small) TTF_CloseFont(font_small);
    if (renderer)   SDL_DestroyRenderer(renderer);
    if (window)     SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}
