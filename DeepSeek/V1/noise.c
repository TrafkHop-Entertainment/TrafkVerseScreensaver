// noise.c  —  random-noise pattern/music generator
// Build:   gcc -O2 -o noise noise.c -lSDL2 -lm
// Needs:   SDL2, tsf.h (TinySoundFont) in the same folder, *.sf2 files next to binary.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <stdint.h>
#include <unistd.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"

// ------------------------------------------------------------------
// Internal framebuffer (upscaled to the window, aspect preserved).
// Small = fast, and every dot is genuinely "small noise".
// ------------------------------------------------------------------
#define BUF_W 640
#define BUF_H 360
#define MAX_COLORS 64
#define MAX_ENT 64

static uint32_t *pixbuf;       // ARGB8888
static uint8_t  *bg;           // background gray level per pixel
static uint8_t  *bg_tint;      // 0 = none, 1 = dark red tint
static uint8_t  *fg_int;       // foreground intensity
static uint8_t  *fg_col;       // foreground color index (1..NCOLORS)

static uint8_t colors[MAX_COLORS][3];
static int     color_preset[MAX_COLORS];   // SF2 preset index per color
static int     NCOLORS = 0;

static tsf    *synth = NULL;

// ------------------------------------------------------------------
// Entities = "the patterns".
// ------------------------------------------------------------------
enum {
    E_LINE, E_CIRCLE, E_POLY, E_SPIRAL, E_STICK,
    E_WALK, E_RINGS, E_STAR, E_GRID, E_ZIGZAG
};

typedef struct {
    int    type;
    float  x, y, vx, vy;
    float  angle, spin;
    float  size;
    int    color;          // 1..NCOLORS
    float  life, maxlife;
    uint32_t seed;
    int    key;            // MIDI note
} Entity;

static Entity ents[MAX_ENT];
static uint32_t rng_state;

// ------------------------------------------------------------------
// RNG
// ------------------------------------------------------------------
static inline uint32_t xr(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return (rng_state = x);
}
static inline float frand(void)  { return (xr() & 0xFFFFFF) / 16777215.0f; }
static inline int   rrange(int a, int b) { return a + (int)(frand() * (float)(b - a + 1)); }
static inline float frange(float a, float b) { return a + frand() * (b - a); }

// ------------------------------------------------------------------
// Drawing helpers
// ------------------------------------------------------------------
static inline void plot(int x, int y, int color, uint8_t iv) {
    if ((unsigned)x >= BUF_W) return;
    if ((unsigned)y >= BUF_H) return;
    int i = y * BUF_W + x;
    if (iv > fg_int[i]) { fg_int[i] = iv; fg_col[i] = (uint8_t)color; }
}
static void plot_dot(int cx, int cy, int r, int color, uint8_t iv) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            plot(cx + dx, cy + dy, color, iv);
}
static void draw_line(float x0, float y0, float x1, float y1,
                      int t, int color, uint8_t iv) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx*dx + dy*dy);
    int steps = (int)len + 1; if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        float u = (float)i / steps;
        int px = (int)(x0 + dx*u + 0.5f);
        int py = (int)(y0 + dy*u + 0.5f);
        if (t <= 0) plot(px, py, color, iv);
        else        plot_dot(px, py, t, color, iv);
    }
}

// ------------------------------------------------------------------
// Entity renderers — each draws a small shape in fg layer.
// ------------------------------------------------------------------
static void render_entity(const Entity *e, uint8_t iv) {
    float cx = e->x, cy = e->y, s = e->size, a = e->angle;
    int   col = e->color;
    int   t = (s > 18.0f) ? 1 : 0;

    switch (e->type) {
    case E_LINE: {
        float dx = cosf(a)*s, dy = sinf(a)*s;
        draw_line(cx-dx, cy-dy, cx+dx, cy+dy, t, col, iv);
    } break;
    case E_CIRCLE: {
        int segs = 32;
        for (int i = 0; i < segs; i++) {
            float t0 = (float)i/segs * 6.2831853f;
            float t1 = (float)(i+1)/segs * 6.2831853f;
            draw_line(cx+cosf(t0)*s, cy+sinf(t0)*s,
                      cx+cosf(t1)*s, cy+sinf(t1)*s, t, col, iv);
        }
    } break;
    case E_POLY: {
        int n = 3 + (int)(e->seed % 6);
        for (int i = 0; i < n; i++) {
            float t0 = (float)i/n * 6.2831853f + a;
            float t1 = (float)(i+1)/n * 6.2831853f + a;
            draw_line(cx+cosf(t0)*s, cy+sinf(t0)*s,
                      cx+cosf(t1)*s, cy+sinf(t1)*s, t, col, iv);
        }
    } break;
    case E_SPIRAL: {
        int steps = 120;
        float px = cx, py = cy;
        for (int i = 1; i <= steps; i++) {
            float u = (float)i/steps;
            float ang = u * 18.8495559f + a;
            float r = s * u;
            float nx = cx + cosf(ang)*r;
            float ny = cy + sinf(ang)*r;
            draw_line(px, py, nx, ny, t, col, iv);
            px = nx; py = ny;
        }
    } break;
    case E_STICK: {
        float h = s;
        float hy = cy - h*0.7f;
        int segs = 20;
        for (int i = 0; i < segs; i++) {
            float t0 = (float)i/segs * 6.2831853f;
            float t1 = (float)(i+1)/segs * 6.2831853f;
            draw_line(cx+cosf(t0)*h*0.2f, hy+sinf(t0)*h*0.2f,
                      cx+cosf(t1)*h*0.2f, hy+sinf(t1)*h*0.2f, t, col, iv);
        }
        draw_line(cx, hy+h*0.2f, cx, cy+h*0.2f, t, col, iv);
        draw_line(cx, cy-h*0.2f, cx-h*0.4f, cy+h*0.05f, t, col, iv);
        draw_line(cx, cy-h*0.2f, cx+h*0.4f, cy+h*0.05f, t, col, iv);
        draw_line(cx, cy+h*0.2f, cx-h*0.3f, cy+h*0.7f, t, col, iv);
        draw_line(cx, cy+h*0.2f, cx+h*0.3f, cy+h*0.7f, t, col, iv);
    } break;
    case E_WALK: {
        float px = cx, py = cy;
        uint32_t st = e->seed;
        for (int i = 0; i < 48; i++) {
            st ^= st << 13; st ^= st >> 17; st ^= st << 5;
            float ang = (st & 0xFFFF) / 65535.0f * 6.2831853f + a;
            float nx = px + cosf(ang) * s * 0.12f;
            float ny = py + sinf(ang) * s * 0.12f;
            draw_line(px, py, nx, ny, t, col, iv);
            px = nx; py = ny;
        }
    } break;
    case E_RINGS: {
        for (int k = 1; k <= 5; k++) {
            float r = s * k / 5.0f;
            int segs = 24;
            for (int i = 0; i < segs; i++) {
                float t0 = (float)i/segs * 6.2831853f;
                float t1 = (float)(i+1)/segs * 6.2831853f;
                draw_line(cx+cosf(t0)*r, cy+sinf(t0)*r,
                          cx+cosf(t1)*r, cy+sinf(t1)*r, t, col, iv);
            }
        }
    } break;
    case E_STAR: {
        int pts = 5 + (int)(e->seed % 5);
        for (int i = 0; i < pts; i++) {
            float t0 = (float)i/pts * 6.2831853f + a - 1.5707963f;
            float t1 = (float)((i+2)%pts)/pts * 6.2831853f + a - 1.5707963f;
            draw_line(cx+cosf(t0)*s, cy+sinf(t0)*s,
                      cx+cosf(t1)*s, cy+sinf(t1)*s, t, col, iv);
        }
    } break;
    case E_GRID: {
        int n = 3 + (int)(e->seed % 4);
        float step = s / n;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                float px = cx - s*0.5f + i*step + step*0.5f;
                float py = cy - s*0.5f + j*step + step*0.5f;
                plot_dot((int)(px+0.5f), (int)(py+0.5f), (t?1:0), col, iv);
            }
    } break;
    case E_ZIGZAG: {
        int segs = 8 + (int)(e->seed % 10);
        float px = cx - s*0.5f;
        float py = cy;
        for (int i = 0; i < segs; i++) {
            float nx = px + s/segs;
            float ny = cy + ((i & 1) ? -1.0f : 1.0f) * s * 0.35f;
            draw_line(px, py, nx, ny, t, col, iv);
            px = nx; py = ny;
        }
    } break;
    }
}

// ------------------------------------------------------------------
// Entity lifecycle
// ------------------------------------------------------------------
static void spawn_entity(void) {
    for (int i = 0; i < MAX_ENT; i++) {
        Entity *e = &ents[i];
        if (e->life <= 0.0f) {
            memset(e, 0, sizeof(*e));
            e->type   = rrange(0, 9);
            e->x      = frange(BUF_W * 0.15f, BUF_W * 0.85f);
            e->y      = frange(BUF_H * 0.15f, BUF_H * 0.85f);
            e->vx     = frange(-10.0f, 10.0f);
            e->vy     = frange(-10.0f, 10.0f);
            e->angle  = frange(0.0f, 6.2831853f);
            e->spin   = frange(-1.2f, 1.2f);
            e->size   = frange(BUF_H * 0.06f, BUF_H * 0.32f);
            e->color  = 1 + (xr() % NCOLORS);
            e->maxlife = frange(4.0f, 18.0f);
            e->life   = e->maxlife;
            e->seed   = xr();
            e->key    = 36 + (int)(xr() % 48);
            if (synth) {
                int ci = e->color - 1;
                tsf_note_on(synth, color_preset[ci], e->key,
                            0.55f + frand()*0.25f);
            }
            return;
        }
    }
}

static void update_entities(float dt) {
    for (int i = 0; i < MAX_ENT; i++) {
        Entity *e = &ents[i];
        if (e->life <= 0.0f) continue;
        e->life -= dt;
        e->x    += e->vx * dt;
        e->y    += e->vy * dt;
        e->angle += e->spin * dt;
        if (e->x < 4.0f)          { e->x = 4.0f;          e->vx = -e->vx; }
        if (e->x > BUF_W - 4.0f)  { e->x = BUF_W-4.0f;    e->vx = -e->vx; }
        if (e->y < 4.0f)          { e->y = 4.0f;          e->vy = -e->vy; }
        if (e->y > BUF_H - 4.0f)  { e->y = BUF_H-4.0f;    e->vy = -e->vy; }

        if (e->life <= 0.0f && synth) {
            int ci = e->color - 1;
            tsf_note_off(synth, color_preset[ci], e->key);
        }
    }
}

static void render_entities(void) {
    for (int i = 0; i < MAX_ENT; i++) {
        Entity *e = &ents[i];
        if (e->life <= 0.0f) continue;
        float u = 1.0f - e->life / e->maxlife;   // 0 → 1
        // envelope: grow in, hold, fade out (but with a moving size)
        float env = sinf(u * 3.14159265f);
        float sz  = e->size * (0.35f + 0.65f * env);
        uint8_t iv = (uint8_t)(255.0f * env);
        if (iv < 8) continue;
        Entity tmp = *e;
        tmp.size = sz;
        render_entity(&tmp, iv);
    }
}

// ------------------------------------------------------------------
// Background noise: fast static, mostly very dark, with tiny
// occasional colored (reddish) dots.
// ------------------------------------------------------------------
static void update_bg(void) {
    int n = BUF_W * BUF_H;
    for (int i = 0; i < n; i++) {
        uint32_t r = xr();
        uint8_t v = (uint8_t)(r & 0x1Fu);            // 0..31 mostly
        if ((r & 0xFFu) < 22u) v = (uint8_t)(v + 28); // occasional brighter
        bg[i] = v;
        // ~0.4% dark red tint, part of the background
        bg_tint[i] = ((r >> 8) & 0x3FFu) < 4u ? 1u : 0u;
    }
}

// ------------------------------------------------------------------
// Composite everything into ARGB buffer, and decay fg towards 0.
// ------------------------------------------------------------------
static void composite(void) {
    int n = BUF_W * BUF_H;
    for (int i = 0; i < n; i++) {
        uint8_t g = bg[i];
        uint8_t r = g, gg = g, b = g;
        if (bg_tint[i]) { r = (uint8_t)(g + 18); if (r < g) r = 255; }

        uint8_t fi = fg_int[i];
        if (fi) {
            int ci = (int)fg_col[i] - 1;
            if ((unsigned)ci < (unsigned)NCOLORS) {
                uint8_t cr = colors[ci][0];
                uint8_t cg = colors[ci][1];
                uint8_t cb = colors[ci][2];
                r  = (uint8_t)((r  * (255 - fi) + cr * fi) / 255);
                gg = (uint8_t)((gg * (255 - fi) + cg * fi) / 255);
                b  = (uint8_t)((b  * (255 - fi) + cb * fi) / 255);
            }
            fg_int[i] = fi > 3 ? (uint8_t)(fi - 3) : 0;
        }
        pixbuf[i] = 0xFF000000u | ((uint32_t)r << 16)
                                | ((uint32_t)gg << 8) | (uint32_t)b;
    }
}

// ------------------------------------------------------------------
// Color palette + SF2 setup
// ------------------------------------------------------------------
static void gen_colors(void) {
    NCOLORS = rrange(5, 35);
    for (int i = 0; i < NCOLORS; i++) {
        float h = frand() * 360.0f;
        float s = 0.85f + frand() * 0.15f;   // saturated
        float v = 0.90f + frand() * 0.10f;   // bright
        float c = v * s;
        float hh = h / 60.0f;
        float x = c * (1.0f - fabsf(fmodf(hh, 2.0f) - 1.0f));
        float rr = 0, gg = 0, bb = 0;
        if      (hh < 1) { rr = c; gg = x; }
        else if (hh < 2) { rr = x; gg = c; }
        else if (hh < 3) { gg = c; bb = x; }
        else if (hh < 4) { gg = x; bb = c; }
        else if (hh < 5) { rr = x; bb = c; }
        else             { rr = c; bb = x; }
        float m = v - c;
        colors[i][0] = (uint8_t)((rr + m) * 255.0f);
        colors[i][1] = (uint8_t)((gg + m) * 255.0f);
        colors[i][2] = (uint8_t)((bb + m) * 255.0f);
    }
}

static char sf2_path[512];

static int find_sf2(void) {
    DIR *d = opendir(".");
    if (!d) return 0;
    char cand[64][256]; int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < 64) {
        int l = (int)strlen(de->d_name);
        if (l > 4 && strcasecmp(de->d_name + l - 4, ".sf2") == 0) {
            strncpy(cand[n], de->d_name, 255);
            cand[n][255] = 0;
            n++;
        }
    }
    closedir(d);
    if (!n) return 0;
    int pick = (int)(xr() % (uint32_t)n);
    strncpy(sf2_path, cand[pick], 511);
    sf2_path[511] = 0;
    return 1;
}

static void assign_presets(void) {
    if (!synth) return;
    int pc = tsf_get_presetcount(synth);
    if (pc <= 0) return;
    for (int i = 0; i < NCOLORS; i++)
        color_preset[i] = (int)(xr() % (uint32_t)pc);
}

// ------------------------------------------------------------------
// Audio
// ------------------------------------------------------------------
static void audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    short *out = (short *)stream;
    int frames = len / 4;              // 16-bit stereo
    if (synth) tsf_render_short(synth, out, frames, 0);
    else       memset(stream, 0, len);
}

// ------------------------------------------------------------------
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    SDL_SetMainReady();

    rng_state = (uint32_t)time(NULL)
              ^ (uint32_t)clock()
              ^ (uint32_t)getpid() * 2654435761u;
    if (!rng_state) rng_state = 0xDEADBEEFu;
    for (int i = 0; i < 32; i++) xr();

    // ---- load one random SF2 from CWD ------------------------------
    int have_sf2 = find_sf2();
    if (have_sf2) {
        synth = tsf_load_filename(sf2_path);
        if (synth) {
            tsf_set_output(synth, TSF_STEREO_INTERLEAVED, 44100, 0.0f);
            tsf_set_max_voices(synth, 64);
            fprintf(stderr, "noise: loaded %s (%d presets)\n",
                    sf2_path, tsf_get_presetcount(synth));
        }
    }
    if (!synth) fprintf(stderr, "noise: no .sf2 found — running silent\n");

    gen_colors();
    assign_presets();

    // ---- SDL -------------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "Noise",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 540,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); return 1; }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "CreateRenderer: %s\n", SDL_GetError()); return 1; }

    SDL_RenderSetLogicalSize(ren, BUF_W, BUF_H);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    SDL_Texture *tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, BUF_W, BUF_H);

    // ---- Audio device ---------------------------------------------
    SDL_AudioSpec want, have; SDL_zero(want);
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 512;
    want.callback = audio_cb;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (adev) SDL_PauseAudioDevice(adev, 0);

    // ---- Allocate framebuffers ------------------------------------
    int n = BUF_W * BUF_H;
    pixbuf  = malloc(sizeof(uint32_t) * n);
    bg      = malloc(n);
    bg_tint = malloc(n);
    fg_int  = malloc(n);
    fg_col  = malloc(n);
    memset(fg_int, 0, n);
    memset(fg_col, 0, n);

    // ---- Main loop -------------------------------------------------
    Uint32 last = SDL_GetTicks();
    float  spawn_timer = 0.5f;
    int    running = 1;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            else if (ev.type == SDL_KEYDOWN &&
                     ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
        }

        Uint32 now = SDL_GetTicks();
        float dt = (now - last) * 0.001f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        // spawn logic: slowly keep a handful alive
        spawn_timer -= dt;
        if (spawn_timer <= 0.0f) {
            spawn_entity();
            spawn_timer = frange(0.35f, 1.8f);
        }

        update_entities(dt);

        update_bg();
        render_entities();
        composite();

        SDL_UpdateTexture(tex, NULL, pixbuf, BUF_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);    // vsync here
    }

    // ---- cleanup ---------------------------------------------------
    if (synth) tsf_close(synth);
    if (adev)  SDL_CloseAudioDevice(adev);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    free(pixbuf); free(bg); free(bg_tint); free(fg_int); free(fg_col);
    return 0;
}