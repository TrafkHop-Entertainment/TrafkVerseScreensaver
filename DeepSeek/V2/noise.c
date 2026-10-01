// noise.c — random-noise pattern/music generator
// Build:  make  (or: gcc -O2 -o noise noise.c -lSDL2 -lm)
// Needs:  SDL2, tsf.h (TinySoundFont, fetched by Makefile),
//         any number of *.sf2 files next to the binary.
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

#define BUF_W        640
#define BUF_H        360
#define MAX_COLORS   64
#define MAX_ENT      64

static uint32_t *pixbuf;
static uint8_t  *bg;
static uint8_t  *bg_tint;
static uint8_t  *fg_int;
static uint8_t  *fg_col;

static uint8_t colors[MAX_COLORS][3];
static int     color_preset[MAX_COLORS];
static int     NCOLORS = 0;
static tsf    *synth = NULL;

// Current entity's dissolve context (set right before rendering)
static float    g_presence = 1.0f;
static uint32_t g_seed     = 0;

// ------------------------- RNG -------------------------
static uint32_t rng_state;
static inline uint32_t xr(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return (rng_state = x);
}
static inline float frand(void) { return (xr() & 0xFFFFFF) / 16777215.0f; }
static inline int   rrange(int a, int b) { return a + (int)(frand() * (float)(b - a + 1)); }
static inline float frange(float a, float b) { return a + frand() * (b - a); }

// ------------------------- pixel layer -------------------------
// A pixel is only laid down if its deterministic hash value is
// below the current presence.  At presence ~0 almost nothing is
// drawn (looks like the black noise), at 1 the shape is solid.
static inline void plot(int x, int y, int color, uint8_t iv) {
    if ((unsigned)x >= BUF_W || (unsigned)y >= BUF_H) return;
    if (g_presence < 0.99f) {
        uint32_t h = (uint32_t)x * 374761393u
                   + (uint32_t)y * 668265263u
                   + g_seed;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        float r = (h & 0xFFFF) / 65535.0f;
        if (r > g_presence) return;
    }
    int i = y * BUF_W + x;
    if (iv > fg_int[i]) { fg_int[i] = iv; fg_col[i] = (uint8_t)color; }
}

static void draw_line(float x0, float y0, float x1, float y1,
                      int t, int col, uint8_t iv) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx*dx + dy*dy);
    int steps = (int)len + 1;
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        float u = (float)i / steps;
        int px = (int)(x0 + dx*u + 0.5f);
        int py = (int)(y0 + dy*u + 0.5f);
        if (t <= 0) {
            plot(px, py, col, iv);
        } else {
            for (int dy2 = -t; dy2 <= t; dy2++)
                for (int dx2 = -t; dx2 <= t; dx2++)
                    if (dx2*dx2 + dy2*dy2 <= t*t)
                        plot(px+dx2, py+dy2, col, iv);
        }
    }
}

// ------------------------- entities -------------------------
enum {
    P_SUPERFORMULA = 0,
    P_SPIROGRAPH,
    P_SNAKE,
    P_LISSAJOUS,
    P_FOURIER,
    P_MESH,
    P_SPIRAL,
    P_CONSTELLATION,
    P_ROSETTE,
    P_MULTI_SPIRO,
    P_COUNT
};

typedef struct {
    int      type;
    float    x, y, vx, vy;
    float    angle, spin;
    float    size, size_base, size_pulse, size_pulse_t;
    int      color;
    float    life;
    float    presence;
    int      phase;           // 0 = in, 1 = hold, 2 = out, -1 = dead
    float    in_dur, hold_dur, out_dur;
    uint32_t seed;
    int      key;
    int      thickness;
    float    p[32];
    int      ip[32];
    int      will_morph;
} Entity;

static Entity ents[MAX_ENT];

// ------------------------- pattern parameter rolling -------------------------
static void roll_pattern(Entity *e) {
    e->type = rrange(0, P_COUNT - 1);
    e->seed = xr();
    memset(e->p,  0, sizeof(e->p));
    memset(e->ip, 0, sizeof(e->ip));

    switch (e->type) {
    case P_SUPERFORMULA:
        e->ip[0] = rrange(1, 12);
        e->p[0]  = frange(0.4f, 1.6f);   // a
        e->p[1]  = frange(0.4f, 1.6f);   // b
        e->p[2]  = frange(1.0f, 5.0f);   // n1
        e->p[3]  = frange(0.5f, 5.0f);   // n2
        e->p[4]  = frange(0.5f, 5.0f);   // n3
        break;
    case P_SPIROGRAPH:
        e->ip[0] = rrange(4, 14);
        e->ip[1] = rrange(1, e->ip[0] - 1);
        e->p[0]  = frange(0.4f, 1.0f);
        break;
    case P_SNAKE:
        e->ip[0] = rrange(40, 160);
        e->p[0]  = frange(0.1f, 0.7f);
        e->p[1]  = frange(0.015f, 0.06f);
        break;
    case P_LISSAJOUS:
        e->ip[0] = rrange(1, 7);
        e->ip[1] = rrange(1, 7);
        e->p[0]  = frange(0.0f, 6.28f);
        break;
    case P_FOURIER:
        e->ip[0] = rrange(2, 5);
        for (int i = 0; i < e->ip[0]; i++) {
            e->p[i*2+0] = frange(0.15f, 1.0f);
            e->p[i*2+1] = frange(0.0f, 6.28f);
            e->ip[1+i]  = rrange(1, 8);
        }
        break;
    case P_MESH:
        e->p[0] = frange(0.4f, 1.0f);   // sx
        e->p[1] = frange(0.4f, 1.0f);   // sy
        e->p[2] = frange(0.4f, 1.0f);   // sz
        e->p[3] = frange(0.0f, 6.28f);  // tilt
        break;
    case P_SPIRAL:
        e->ip[0] = rrange(2, 7);
        e->p[0]  = frange(0.2f, 0.9f);
        e->p[1]  = (xr() & 1) ? 1.0f : -1.0f;
        break;
    case P_CONSTELLATION:
        e->ip[0] = rrange(5, 12);
        for (int i = 0; i < e->ip[0]; i++) {
            float a2 = frange(0.0f, 6.283f);
            float r2 = frange(0.25f, 1.0f);
            e->p[i*2+0] = cosf(a2) * r2;
            e->p[i*2+1] = sinf(a2) * r2;
        }
        break;
    case P_ROSETTE:
        e->ip[0] = rrange(3, 10);       // petals
        e->ip[1] = rrange(2, 5);        // layers
        e->p[0]  = frange(0.3f, 0.8f);  // inner ratio
        break;
    case P_MULTI_SPIRO:
        e->ip[0] = rrange(4, 12);
        e->ip[1] = rrange(1, e->ip[0] - 1);
        e->p[0]  = frange(0.4f, 1.0f);
        e->ip[2] = rrange(2, 5);
        break;
    }
}

// ------------------------- rendering -------------------------
static void render_entity(Entity *e) {
    g_presence = e->presence;
    g_seed     = e->seed;

    float s  = e->size;
    float cx = e->x, cy = e->y;
    float a  = e->angle;
    int   col = e->color;

    // Stroke width: grows from hairline to full thickness with presence,
    // so the pattern "coalesces" out of the noise.
    int t = (int)((float)e->thickness * e->presence + 0.5f);

    // Brightness: larger patterns and thicker patterns are brighter.
    float size_factor = s / (BUF_H * 0.35f);
    if (size_factor > 1.0f) size_factor = 1.0f;
    float thick_factor = e->thickness / 2.0f;
    float bright_factor = 0.5f * size_factor + 0.5f * thick_factor;
    float base_b = 100.0f + 155.0f * bright_factor;
    uint8_t iv = (uint8_t)(base_b * e->presence);
    if (iv < 6 && e->presence > 0.01f) iv = 6;

    int steps;
    switch (e->type) {

    case P_SUPERFORMULA: {
        steps = 420;
        int   m  = e->ip[0];
        float aa = e->p[0], bb = e->p[1];
        float n1 = e->p[2], n2 = e->p[3], n3 = e->p[4];
        float px = 0, py = 0;
        for (int i = 0; i <= steps; i++) {
            float th = (float)i / steps * 6.2831853f;
            float c1 = cosf(m * th / 4.0f) / aa;
            float c2 = sinf(m * th / 4.0f) / bb;
            float t1 = powf(fabsf(c1), n2);
            float t2 = powf(fabsf(c2), n3);
            float r  = powf(t1 + t2, -1.0f / n1);
            if (!isfinite(r) || r > 5.0f) r = 5.0f;
            float nx = cosf(th + a) * r * s * 0.4f + cx;
            float ny = sinf(th + a) * r * s * 0.4f + cy;
            if (i > 0) draw_line(px, py, nx, ny, t, col, iv);
            px = nx; py = ny;
        }
    } break;

    case P_SPIROGRAPH: {
        int R = e->ip[0], r = e->ip[1];
        float d = e->p[0] * (float)r;
        int ra = R, rb = r;
        while (rb) { int tmp = ra % rb; ra = rb; rb = tmp; }
        float gcd_val = (float)ra;
        float tmax = 6.2831853f * (float)r / gcd_val;
        steps = 500;
        float scale = s * 0.4f / (float)R;
        float px = 0, py = 0;
        for (int i = 0; i <= steps; i++) {
            float th = (float)i / steps * tmax;
            float nx = (R - r) * cosf(th) + d * cosf((R - r) / (float)r * th);
            float ny = (R - r) * sinf(th) - d * sinf((R - r) / (float)r * th);
            float fx = cx + (cosf(a)*nx - sinf(a)*ny) * scale;
            float fy = cy + (sinf(a)*nx + cosf(a)*ny) * scale;
            if (i > 0) draw_line(px, py, fx, fy, t, col, iv);
            px = fx; py = fy;
        }
    } break;

    case P_SNAKE: {
        int   segs = e->ip[0];
        float turn = e->p[0];
        float step = e->p[1] * s;
        float px = cx - s * 0.5f;
        float py = cy;
        float dir = frange(0.0f, 6.2831853f);
        uint32_t st = e->seed;
        for (int i = 0; i < segs; i++) {
            st ^= st << 13; st ^= st >> 17; st ^= st << 5;
            float r = ((st & 0xFFFF) / 65535.0f) - 0.5f;
            dir += r * turn * 2.0f;
            float nx = px + cosf(dir) * step;
            float ny = py + sinf(dir) * step;
            draw_line(px, py, nx, ny, t, col, iv);
            px = nx; py = ny;
        }
    } break;

    case P_LISSAJOUS: {
        steps = 420;
        float A = (float)e->ip[0], B = (float)e->ip[1];
        float d = e->p[0];
        float px = 0, py = 0;
        for (int i = 0; i <= steps; i++) {
            float th = (float)i / steps * 6.2831853f;
            float nx = sinf(A * th + d);
            float ny = sinf(B * th);
            float fx = cx + (cosf(a)*nx - sinf(a)*ny) * s * 0.5f;
            float fy = cy + (sinf(a)*nx + cosf(a)*ny) * s * 0.5f;
            if (i > 0) draw_line(px, py, fx, fy, t, col, iv);
            px = fx; py = fy;
        }
    } break;

    case P_FOURIER: {
        steps = 420;
        int nterms = e->ip[0];
        float px = 0, py = 0;
        for (int i = 0; i <= steps; i++) {
            float th = (float)i / steps * 6.2831853f;
            float r = 1.0f;
            for (int k = 0; k < nterms; k++) {
                float amp = e->p[k*2+0];
                float ph  = e->p[k*2+1];
                int  freq = e->ip[1+k];
                r += amp * cosf(freq * th + ph);
            }
            r *= 0.5f;
            if (r < 0.05f) r = 0.05f;
            float nx = cosf(th + a) * r * s * 0.5f + cx;
            float ny = sinf(th + a) * r * s * 0.5f + cy;
            if (i > 0) draw_line(px, py, nx, ny, t, col, iv);
            px = nx; py = ny;
        }
    } break;

    case P_MESH: {
        float sx = e->p[0], sy = e->p[1], sz = e->p[2];
        float tilt = e->p[3];
        float verts[8][3] = {
            {-sx,-sy,-sz}, { sx,-sy,-sz}, { sx, sy,-sz}, {-sx, sy,-sz},
            {-sx,-sy, sz}, { sx,-sy, sz}, { sx, sy, sz}, {-sx, sy, sz},
        };
        float ca = cosf(a), sa = sinf(a);
        float cb = cosf(tilt), sb = sinf(tilt);
        for (int i = 0; i < 8; i++) {
            float x = verts[i][0], y = verts[i][1], z = verts[i][2];
            float x1 = ca * x + sa * z;
            float z1 = -sa * x + ca * z;
            float y1 = cb * y - sb * z1;
            float z2 = sb * y + cb * z1;
            float d = 3.0f;
            float persp = d / (d + z2);
            verts[i][0] = cx + x1 * s * 0.5f * persp;
            verts[i][1] = cy + y1 * s * 0.5f * persp;
        }
        int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0},
            {4,5},{5,6},{6,7},{7,4},
            {0,4},{1,5},{2,6},{3,7}
        };
        for (int i = 0; i < 12; i++)
            draw_line(verts[edges[i][0]][0], verts[edges[i][0]][1],
                      verts[edges[i][1]][0], verts[edges[i][1]][1],
                      t, col, iv);
    } break;

    case P_SPIRAL: {
        int   turns  = e->ip[0];
        float growth = e->p[0];
        float dir    = e->p[1];
        steps = 320;
        float total = (float)turns * 6.2831853f;
        float px = 0, py = 0;
        for (int i = 0; i <= steps; i++) {
            float u  = (float)i / steps;
            float th = u * total * dir;
            float r  = powf(u, growth);
            float nx = cosf(th + a) * r * s * 0.5f + cx;
            float ny = sinf(th + a) * r * s * 0.5f + cy;
            if (i > 0) draw_line(px, py, nx, ny, t, col, iv);
            px = nx; py = ny;
        }
    } break;

    case P_CONSTELLATION: {
        int n = e->ip[0];
        float pts[24][2];
        for (int i = 0; i < n; i++) {
            float px = e->p[i*2+0] * s * 0.5f;
            float py = e->p[i*2+1] * s * 0.5f;
            pts[i][0] = cx + cosf(a)*px - sinf(a)*py;
            pts[i][1] = cy + sinf(a)*px + cosf(a)*py;
        }
        int rd = t + 1;
        for (int i = 0; i < n; i++) {
            for (int dy2 = -rd; dy2 <= rd; dy2++)
                for (int dx2 = -rd; dx2 <= rd; dx2++)
                    if (dx2*dx2 + dy2*dy2 <= rd*rd)
                        plot((int)pts[i][0]+dx2, (int)pts[i][1]+dy2, col, iv);
        }
        for (int i = 0; i < n; i++) {
            int best = -1; float best_d = 1e9f;
            for (int j = 0; j < n; j++) {
                if (j == i) continue;
                float dx = pts[i][0] - pts[j][0];
                float dy = pts[i][1] - pts[j][1];
                float d2 = dx*dx + dy*dy;
                if (d2 < best_d) { best_d = d2; best = j; }
            }
            if (best >= 0 && best != i)
                draw_line(pts[i][0], pts[i][1],
                          pts[best][0], pts[best][1], t, col, iv);
        }
    } break;

    case P_ROSETTE: {
        int petals = e->ip[0];
        int layers = e->ip[1];
        float inner = e->p[0];
        for (int L = 0; L < layers; L++) {
            float r_out = s * 0.5f * (1.0f - (float)L / layers * 0.3f);
            float r_in  = r_out * inner;
            int   st    = petals * 2;
            for (int i = 0; i < st; i++) {
                float th0 = (float)i       / st * 6.2831853f + a + (float)L * 0.3f;
                float th1 = (float)(i + 1) / st * 6.2831853f + a + (float)L * 0.3f;
                float rr0 = (i & 1) ? r_out : r_in;
                float rr1 = ((i + 1) & 1) ? r_out : r_in;
                draw_line(cx + cosf(th0) * rr0, cy + sinf(th0) * rr0,
                          cx + cosf(th1) * rr1, cy + sinf(th1) * rr1,
                          t, col, iv);
            }
        }
    } break;

    case P_MULTI_SPIRO: {
        int R = e->ip[0], r = e->ip[1];
        float d = e->p[0] * (float)r;
        int copies = e->ip[2];
        int ra = R, rb = r;
        while (rb) { int tmp = ra % rb; ra = rb; rb = tmp; }
        float gcd_val = (float)ra;
        float tmax  = 6.2831853f * (float)r / gcd_val;
        steps = 400;
        float scale = s * 0.4f / (float)R;
        for (int c = 0; c < copies; c++) {
            float offset = (float)c / copies * 6.2831853f + a;
            float px = 0, py = 0;
            for (int i = 0; i <= steps; i++) {
                float th = (float)i / steps * tmax;
                float nx = (R - r) * cosf(th) + d * cosf((R - r) / (float)r * th);
                float ny = (R - r) * sinf(th) - d * sinf((R - r) / (float)r * th);
                float fx = cx + (cosf(offset)*nx - sinf(offset)*ny) * scale;
                float fy = cy + (sinf(offset)*nx + cosf(offset)*ny) * scale;
                if (i > 0) draw_line(px, py, fx, fy, t, col, iv);
                px = fx; py = fy;
            }
        }
    } break;

    }
}

// ------------------------- lifecycle -------------------------
static void spawn_entity(void) {
    for (int i = 0; i < MAX_ENT; i++) {
        Entity *e = &ents[i];
        if (e->phase == -1) {
            memset(e, 0, sizeof(*e));
            e->x = frange(BUF_W * 0.2f, BUF_W * 0.8f);
            e->y = frange(BUF_H * 0.2f, BUF_H * 0.8f);
            e->vx = frange(-8.0f, 8.0f);
            e->vy = frange(-8.0f, 8.0f);
            e->angle = frange(0.0f, 6.2831853f);
            e->spin  = frange(-0.8f, 0.8f);
            e->size_base = frange(BUF_H * 0.10f, BUF_H * 0.45f);
            e->size = e->size_base;
            e->size_pulse = frange(0.05f, 0.25f);
            e->size_pulse_t = frange(0.0f, 6.28f);
            e->color = 1 + (xr() % NCOLORS);
            roll_pattern(e);
            e->in_dur   = frange(1.2f, 3.0f);
            e->hold_dur = frange(8.0f, 45.0f);
            e->out_dur  = frange(1.2f, 3.0f);
            e->phase    = 0;
            e->life     = 0.0f;
            e->presence = 0.0f;
            e->key      = 36 + (int)(xr() % 48);
            e->thickness = rrange(0, 2);
            e->will_morph = (xr() % 100) < 55;
            if (synth)
                tsf_note_on(synth, color_preset[e->color - 1], e->key,
                            0.35f + frand() * 0.25f);
            return;
        }
    }
}

static void update_entities(float dt) {
    for (int i = 0; i < MAX_ENT; i++) {
        Entity *e = &ents[i];
        if (e->phase == -1) continue;

        e->life += dt;
        e->x += e->vx * dt;
        e->y += e->vy * dt;
        e->angle += e->spin * dt;
        e->size_pulse_t += dt;
        e->size = e->size_base * (1.0f + e->size_pulse * sinf(e->size_pulse_t * 0.7f));

        if (e->x <  20)         { e->x = 20;          e->vx = -e->vx; }
        if (e->x > BUF_W - 20)  { e->x = BUF_W - 20;  e->vx = -e->vx; }
        if (e->y <  20)         { e->y = 20;          e->vy = -e->vy; }
        if (e->y > BUF_H - 20)  { e->y = BUF_H - 20;  e->vy = -e->vy; }

        if (e->phase == 0) {
            float u = e->life / e->in_dur;
            if (u >= 1.0f) { e->phase = 1; e->life = 0.0f; e->presence = 1.0f; }
            else e->presence = u;
        } else if (e->phase == 1) {
            e->presence = 1.0f;
            if (e->life >= e->hold_dur) { e->phase = 2; e->life = 0.0f; }
        } else if (e->phase == 2) {
            float u = e->life / e->out_dur;
            if (u >= 1.0f) {
                if (e->will_morph) {
                    // Keep the color, roll a brand-new pattern, dissolve back in.
                    int keep = e->color;
                    roll_pattern(e);
                    e->color    = keep;
                    e->phase    = 0;
                    e->life     = 0.0f;
                    e->presence = 0.0f;
                    e->in_dur   = frange(1.2f, 3.0f);
                    e->hold_dur = frange(8.0f, 45.0f);
                    e->out_dur  = frange(1.2f, 3.0f);
                    e->thickness = rrange(0, 2);
                    e->will_morph = (xr() % 100) < 55;
                } else {
                    if (synth)
                        tsf_note_off(synth, color_preset[e->color - 1], e->key);
                    e->phase = -1;
                }
            } else {
                e->presence = 1.0f - u;
            }
        }
    }
}

static void render_entities(void) {
    for (int i = 0; i < MAX_ENT; i++) {
        if (ents[i].phase == -1) continue;
        render_entity(&ents[i]);
    }
}

// ------------------------- background noise -------------------------
static void update_bg(void) {
    int n = BUF_W * BUF_H;
    for (int i = 0; i < n; i++) {
        uint32_t r = xr();
        uint8_t  v = (uint8_t)(r & 0x1Fu);
        if ((r & 0xFFu) < 22u) v = (uint8_t)(v + 28);
        bg[i] = v;
        bg_tint[i] = ((r >> 8) & 0x3FFu) < 4u ? 1u : 0u;
    }
}

// ------------------------- composite -------------------------
static void composite(void) {
    int n = BUF_W * BUF_H;
    for (int i = 0; i < n; i++) {
        uint8_t g = bg[i];
        uint8_t r = g, gg = g, b = g;
        if (bg_tint[i]) {
            int rr = g + 18;
            r = (rr > 255) ? 255 : (uint8_t)rr;
        }

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
            fg_int[i] = fi > 4 ? (uint8_t)(fi - 4) : 0;
        }
        pixbuf[i] = 0xFF000000u | ((uint32_t)r << 16)
                                | ((uint32_t)gg << 8) | (uint32_t)b;
    }
}

// ------------------------- palette & SF2 -------------------------
static void gen_colors(void) {
    NCOLORS = rrange(5, 35);
    for (int i = 0; i < NCOLORS; i++) {
        float h = frand() * 360.0f;
        float s = 0.85f + frand() * 0.15f;
        float v = 0.90f + frand() * 0.10f;
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
    char cand[64][256];
    int  n = 0;
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

// ------------------------- audio -------------------------
static void audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    short *out = (short *)stream;
    int frames = len / 4;
    if (synth) {
        tsf_render_short(synth, out, frames, 0);
        int total = frames * 2;
        for (int i = 0; i < total; i++) {
            int v = (int)out[i] * 40 / 100;   // 40 % overall level
            if (v >  32767) v =  32767;
            if (v < -32768) v = -32768;
            out[i] = (short)v;
        }
    } else {
        memset(stream, 0, len);
    }
}

// ------------------------- main -------------------------
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    SDL_SetMainReady();

    rng_state = (uint32_t)time(NULL)
              ^ (uint32_t)clock()
              ^ (uint32_t)getpid() * 2654435761u;
    if (!rng_state) rng_state = 0xDEADBEEFu;
    for (int i = 0; i < 32; i++) xr();

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

    SDL_AudioSpec want, have; SDL_zero(want);
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 512;
    want.callback = audio_cb;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (adev) SDL_PauseAudioDevice(adev, 0);

    int n = BUF_W * BUF_H;
    pixbuf  = malloc(sizeof(uint32_t) * n);
    bg      = malloc(n);
    bg_tint = malloc(n);
    fg_int  = malloc(n);
    fg_col  = malloc(n);
    memset(fg_int, 0, n);
    memset(fg_col, 0, n);
    for (int i = 0; i < MAX_ENT; i++) ents[i].phase = -1;

    Uint32 last = SDL_GetTicks();
    float  spawn_timer = 0.3f;
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

        spawn_timer -= dt;
        if (spawn_timer <= 0.0f) {
            spawn_entity();
            spawn_timer = frange(0.7f, 2.0f);
        }

        update_entities(dt);
        update_bg();
        render_entities();
        composite();

        SDL_UpdateTexture(tex, NULL, pixbuf, BUF_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }

    if (synth) tsf_close(synth);
    if (adev)  SDL_CloseAudioDevice(adev);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    free(pixbuf); free(bg); free(bg_tint); free(fg_int); free(fg_col);
    return 0;
}