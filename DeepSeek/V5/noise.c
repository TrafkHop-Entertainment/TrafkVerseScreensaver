// noise.c — random-noise pattern/music generator
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
#define MAX_COLORS   256
#define MAX_ENT      32

static uint32_t *pixbuf;
static uint8_t  *bg;
static uint8_t  *bg_tint;
static uint8_t  *fg_int;
static uint16_t *fg_col;

static uint8_t colors[MAX_COLORS][3];
static int     color_preset[MAX_COLORS];
static int     NCOLORS = 0;
static tsf    *synth = NULL;

static int g_scale[7];
static int g_scale_size = 7;
static int g_root = 48;

static float    g_presence   = 1.0f;
static uint32_t g_seed       = 0;
static float    g_blend      = 0.0f;
static uint32_t g_blend_seed = 0;

// animation phase for wiggling the current pattern
static float g_pt  = 0.0f;
static float g_wf1 = 0.5f, g_wf2 = 0.5f;
static float g_wa1 = 2.0f, g_wa2 = 2.0f;

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

// ------------------------- wobble helpers -------------------------
static inline float wobx(float x, float y) {
    float u = x * 0.013f + y * 0.017f;
    return sinf(g_pt * g_wf1 + u) * g_wa1;
}
static inline float woby(float x, float y) {
    float u = x * 0.013f + y * 0.017f;
    return cosf(g_pt * g_wf2 + u * 1.31f) * g_wa2;
}

// ------------------------- pixel layer -------------------------
static inline void plot(int x, int y, int color, uint8_t iv) {
    if ((unsigned)x >= BUF_W || (unsigned)y >= BUF_H) return;

    uint32_t h = (uint32_t)x * 374761393u
               + (uint32_t)y * 668265263u
               + g_seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;

    // presence gate: dissolve / emerge
    if (g_presence < 0.99f) {
        float r = (h & 0xFFFF) / 65535.0f;
        if (r > g_presence) return;
    }

    // edge blending into background noise:
    // 1) skip some pixels entirely (dissolve look),
    // 2) attenuate the rest.
    if (g_blend > 0.0f) {
        uint32_t h2 = h ^ g_blend_seed;
        h2 = (h2 ^ (h2 >> 15)) * 2246822519u;
        h2 ^= h2 >> 13;
        float r2 = (h2 & 0xFF) / 255.0f;
        if (r2 < g_blend * 0.55f) return;                    // drop pixel
        iv = (uint8_t)((float)iv * (1.0f - g_blend * 0.55f * r2));
        if (iv < 4) return;
    }

    int i = y * BUF_W + x;
    if (iv > fg_int[i]) { fg_int[i] = iv; fg_col[i] = (uint16_t)color; }
}

static inline void plot_w(int x, int y, int color, uint8_t iv) {
    int wx = x + (int)wobx((float)x, (float)y);
    int wy = y + (int)woby((float)x, (float)y);
    plot(wx, wy, color, iv);
}

static void draw_line(float x0, float y0, float x1, float y1,
                      int t, int col, uint8_t iv) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.5f) return;

    // Subdivide long lines so the wobble actually bends them.
    int subs = 1;
    if (len > 10.0f) subs = (int)(len / 10.0f) + 1;
    if (subs > 48) subs = 48;

    for (int k = 0; k < subs; k++) {
        float u0 = (float)k / subs;
        float u1 = (float)(k + 1) / subs;
        float sx = x0 + dx * u0, sy = y0 + dy * u0;
        float ex = x0 + dx * u1, ey = y0 + dy * u1;
        sx += wobx(sx, sy); sy += woby(sx, sy);
        ex += wobx(ex, ey); ey += woby(ex, ey);

        float sdx = ex - sx, sdy = ey - sy;
        float slen = sqrtf(sdx*sdx + sdy*sdy);
        int steps = (int)slen + 1;
        if (steps < 1) steps = 1;
        for (int i = 0; i <= steps; i++) {
            float uu = (float)i / steps;
            int px = (int)(sx + sdx*uu + 0.5f);
            int py = (int)(sy + sdy*uu + 0.5f);
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
}

// ------------------------- pattern types -------------------------
enum {
    P_SUPERFORMULA = 0,
    P_SPIROGRAPH, P_SNAKE, P_LISSAJOUS, P_FOURIER, P_MESH,
    P_SPIRAL, P_CONSTELLATION, P_ROSETTE, P_MULTI_SPIRO,
    P_BEND, P_STRETCH, P_BLOB, P_DOTS, P_BEZIER, P_WAVES,
    P_POLYLINE, P_CURL,
    P_TRIANGLE, P_HEX, P_CROSS, P_AMOEBA, P_MANY_CIRCLES,
    P_CONCENTRIC, P_GRID_DOTS, P_SCATTER, P_DNA, P_RIPPLE,
    P_PETAL, P_MOON, P_ARC_FAN,
    // new
    P_SINE, P_ZIGZAG, P_CARDIOID, P_LEMNISCATE, P_BRANCH, P_TRI_FAN,
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
    int      phase;
    float    in_dur, hold_dur, out_dur;
    uint32_t seed;
    int      key;
    float    velocity;
    float    retrig_timer;
    int      thickness;
    float    blend;
    float    pattern_t;             // seconds since pattern rolled
    float    anim_f1, anim_f2, anim_a1, anim_a2;
    int      note_playing;
    float    p[32];
    int      ip[32];
    int      will_morph;
} Entity;

static Entity ents[MAX_ENT];

// ------------------------- scale / notes -------------------------
static void pick_scale(void) {
    static const int scales[5][7] = {
        {0, 2, 4, 5, 7, 9, 11},
        {0, 2, 3, 5, 7, 8, 10},
        {0, 2, 3, 5, 7, 9, 10},
        {0, 3, 5, 7, 10, -1, -1},
        {0, 2, 4, 6, 8, 10, -1},
    };
    int which = (int)(xr() % 5);
    for (int i = 0; i < 7; i++) g_scale[i] = scales[which][i];
    g_scale_size = (which == 3) ? 5 : (which == 4 ? 6 : 7);
    g_root = 36 + (int)(xr() % 12);
}

static int pick_note_biased(int steps) {
    int idx = (int)(xr() % (uint32_t)g_scale_size);
    int oct = 1 + (int)(xr() % 2);
    if (steps > 0)      idx = (idx + steps) % g_scale_size;
    else if (steps < 0) idx = (idx + steps + g_scale_size) % g_scale_size;
    int note = g_root + g_scale[idx] + 12 * oct;
    if (note > 100) note = 100;
    if (note < 24)  note = 24;
    return note;
}
static inline int pick_note(void) { return pick_note_biased(0); }

// ------------------------- pattern parameter rolling -------------------------
static void roll_pattern(Entity *e) {
    e->type = rrange(0, P_COUNT - 1);
    e->seed = xr();
    memset(e->p,  0, sizeof(e->p));
    memset(e->ip, 0, sizeof(e->ip));

    switch (e->type) {
    case P_SUPERFORMULA:
        e->ip[0]=rrange(1,12); e->p[0]=frange(.4f,1.6f); e->p[1]=frange(.4f,1.6f);
        e->p[2]=frange(1.f,5.f); e->p[3]=frange(.5f,5.f); e->p[4]=frange(.5f,5.f);
        break;
    case P_SPIROGRAPH:
        e->ip[0]=rrange(4,14); e->ip[1]=rrange(1,e->ip[0]-1); e->p[0]=frange(.4f,1.f);
        break;
    case P_SNAKE:
        e->ip[0]=rrange(40,160); e->p[0]=frange(.1f,.7f); e->p[1]=frange(.015f,.06f);
        break;
    case P_LISSAJOUS:
        e->ip[0]=rrange(1,7); e->ip[1]=rrange(1,7); e->p[0]=frange(0.f,6.28f);
        break;
    case P_FOURIER:
        e->ip[0]=rrange(2,5);
        for (int i=0;i<e->ip[0];i++){
            e->p[i*2]=frange(.15f,1.f); e->p[i*2+1]=frange(0.f,6.28f);
            e->ip[1+i]=rrange(1,8);
        } break;
    case P_MESH:
        e->p[0]=frange(.4f,1.f); e->p[1]=frange(.4f,1.f);
        e->p[2]=frange(.4f,1.f); e->p[3]=frange(0.f,6.28f);
        break;
    case P_SPIRAL:
        e->ip[0]=rrange(2,7); e->p[0]=frange(.2f,.9f); e->p[1]=(xr()&1)?1.f:-1.f;
        break;
    case P_CONSTELLATION:
        e->ip[0]=rrange(5,12);
        for (int i=0;i<e->ip[0];i++){
            float a2=frange(0.f,6.283f), r2=frange(.25f,1.f);
            e->p[i*2]=cosf(a2)*r2; e->p[i*2+1]=sinf(a2)*r2;
        } break;
    case P_ROSETTE:
        e->ip[0]=rrange(3,10); e->ip[1]=rrange(2,5); e->p[0]=frange(.3f,.8f);
        break;
    case P_MULTI_SPIRO:
        e->ip[0]=rrange(4,12); e->ip[1]=rrange(1,e->ip[0]-1);
        e->p[0]=frange(.4f,1.f); e->ip[2]=rrange(2,5);
        break;
    case P_BEND:
        e->p[0]=frange(0.f,6.28f); e->p[1]=frange(1.5f,4.5f);
        e->p[2]=frange(-1.5f,1.5f); e->p[3]=frange(-1.5f,1.5f);
        break;
    case P_STRETCH:
        e->p[0]=frange(.5f,1.f); e->p[1]=frange(.15f,.5f); e->p[2]=frange(.3f,1.2f);
        break;
    case P_BLOB:
        e->ip[0]=rrange(150,500); e->p[0]=frange(.6f,1.4f);
        break;
    case P_DOTS:      e->ip[0]=rrange(4,16); break;
    case P_BEZIER:
        e->p[0]=frange(0.f,6.28f); e->p[1]=frange(2.f,5.f);
        e->p[2]=frange(-1.5f,1.5f); e->p[3]=frange(-1.5f,1.5f);
        e->p[4]=frange(-1.5f,1.5f); e->p[5]=frange(-1.5f,1.5f);
        break;
    case P_WAVES:
        e->ip[0]=rrange(2,6); e->p[0]=frange(.5f,3.f); e->p[1]=frange(.1f,.3f);
        break;
    case P_POLYLINE:  e->ip[0]=rrange(4,12); break;
    case P_CURL:
        e->ip[0]=rrange(2,6); e->p[0]=frange(1.f,3.f);
        break;
    case P_TRIANGLE:  e->p[0]=frange(.6f,1.f); break;
    case P_HEX:       e->ip[0]=rrange(3,8); break;
    case P_CROSS:
        e->p[0]=frange(.3f,1.f); e->p[1]=frange(.3f,1.f); e->ip[0]=(xr()&1);
        break;
    case P_AMOEBA:
        e->ip[0]=rrange(2,5);
        for (int i=0;i<e->ip[0];i++){
            e->p[i*2]=frange(.1f,.45f); e->p[i*2+1]=frange(0.f,6.28f);
            e->ip[1+i]=rrange(2,7);
        } break;
    case P_MANY_CIRCLES:
        e->ip[0]=rrange(3,10); e->p[0]=frange(.04f,.18f);
        break;
    case P_CONCENTRIC:
        e->ip[0]=rrange(3,9); e->ip[1]=(xr()&1)?4:24;
        break;
    case P_GRID_DOTS: e->ip[0]=rrange(3,9); break;
    case P_SCATTER:
        e->ip[0]=rrange(6,24); e->p[0]=frange(.05f,.2f);
        break;
    case P_DNA:
        e->ip[0]=rrange(2,6); e->p[0]=frange(.2f,.45f);
        break;
    case P_RIPPLE:
        e->ip[0]=rrange(3,7); e->p[0]=frange(.4f,1.4f);
        break;
    case P_PETAL:     e->ip[0]=rrange(2,7); break;
    case P_MOON:
        e->p[0]=frange(.55f,.9f); e->p[1]=frange(.3f,.7f);
        break;
    case P_ARC_FAN:
        e->ip[0]=rrange(4,12); e->p[0]=frange(.2f,.7f);
        break;
    case P_SINE:
        e->ip[0]=rrange(1,4); e->p[0]=frange(.5f,3.f); e->p[1]=frange(.1f,.35f);
        break;
    case P_ZIGZAG:
        e->ip[0]=rrange(4,20); e->p[0]=frange(.15f,.7f);
        break;
    case P_CARDIOID:
        e->p[0]=frange(-1.5f,-0.5f);
        break;
    case P_LEMNISCATE:
        e->ip[0]=rrange(1,4);
        break;
    case P_BRANCH:
        e->ip[0]=rrange(3,7); e->ip[1]=rrange(2,4);
        break;
    case P_TRI_FAN:
        e->ip[0]=rrange(4,14);
        break;
    }
}

// ------------------------- rendering -------------------------
static void render_entity(Entity *e) {
    g_presence   = e->presence;
    g_seed       = e->seed;
    g_blend_seed = e->seed ^ 0x5A5A5A5Au;

    g_pt  = e->pattern_t;
    g_wf1 = e->anim_f1;
    g_wf2 = e->anim_f2;
    g_wa1 = e->anim_a1;
    g_wa2 = e->anim_a2;

    float s  = e->size;
    float cx = e->x, cy = e->y;
    float a  = e->angle;
    int   col = e->color;

    int t = (int)((float)e->thickness * e->presence + 0.5f);

    float size_factor = s / (BUF_H * 0.45f);
    if (size_factor > 1.0f) size_factor = 1.0f;
    float thick_factor = e->thickness / 2.0f;
    float bright_factor = size_factor * (0.7f + 0.3f * thick_factor);
    float base_b = 40.0f + 215.0f * bright_factor;
    uint8_t iv = (uint8_t)(base_b * e->presence);
    if (iv < 6 && e->presence > 0.01f) iv = 6;

    g_blend = e->blend * (0.35f + 0.65f * bright_factor);

    int steps;
    switch (e->type) {

    case P_SUPERFORMULA: {
        steps = 420;
        int   m  = e->ip[0];
        float aa = e->p[0], bb = e->p[1];
        float n1 = e->p[2], n2 = e->p[3], n3 = e->p[4];
        float px=0, py=0;
        for (int i=0;i<=steps;i++){
            float th=(float)i/steps*6.2831853f;
            float c1=cosf(m*th/4.f)/aa, c2=sinf(m*th/4.f)/bb;
            float t1=powf(fabsf(c1),n2), t2=powf(fabsf(c2),n3);
            float r=powf(t1+t2,-1.f/n1);
            if(!isfinite(r)||r>5.f) r=5.f;
            float nx=cosf(th+a)*r*s*0.4f+cx;
            float ny=sinf(th+a)*r*s*0.4f+cy;
            if(i>0) draw_line(px,py,nx,ny,t,col,iv);
            px=nx;py=ny;
        }
    } break;

    case P_SPIROGRAPH: {
        int R=e->ip[0], r=e->ip[1];
        float d=e->p[0]*(float)r;
        int ra=R,rb=r;
        while(rb){int tmp=ra%rb;ra=rb;rb=tmp;}
        float g=(float)ra;
        float tmax=6.2831853f*(float)r/g;
        steps=500;
        float sc=s*0.4f/(float)R;
        float px=0,py=0;
        for(int i=0;i<=steps;i++){
            float th=(float)i/steps*tmax;
            float nx=(R-r)*cosf(th)+d*cosf((R-r)/(float)r*th);
            float ny=(R-r)*sinf(th)-d*sinf((R-r)/(float)r*th);
            float fx=cx+(cosf(a)*nx-sinf(a)*ny)*sc;
            float fy=cy+(sinf(a)*nx+cosf(a)*ny)*sc;
            if(i>0) draw_line(px,py,fx,fy,t,col,iv);
            px=fx;py=fy;
        }
    } break;

    case P_SNAKE: {
        int   segs=e->ip[0];
        float turn=e->p[0];
        float step=e->p[1]*s;
        float px=cx-s*0.5f, py=cy;
        float dir=frange(0.f,6.2831853f);
        uint32_t st=e->seed;
        for(int i=0;i<segs;i++){
            st^=st<<13;st^=st>>17;st^=st<<5;
            float r=((st&0xFFFF)/65535.f)-0.5f;
            dir += r*turn*2.f;
            float nx=px+cosf(dir)*step;
            float ny=py+sinf(dir)*step;
            draw_line(px,py,nx,ny,t,col,iv);
            px=nx;py=ny;
        }
    } break;

    case P_LISSAJOUS: {
        steps=420;
        float A=(float)e->ip[0],B=(float)e->ip[1],d=e->p[0];
        float px=0,py=0;
        for(int i=0;i<=steps;i++){
            float th=(float)i/steps*6.2831853f;
            float nx=sinf(A*th+d), ny=sinf(B*th);
            float fx=cx+(cosf(a)*nx-sinf(a)*ny)*s*0.5f;
            float fy=cy+(sinf(a)*nx+cosf(a)*ny)*s*0.5f;
            if(i>0) draw_line(px,py,fx,fy,t,col,iv);
            px=fx;py=fy;
        }
    } break;

    case P_FOURIER: {
        steps=420;
        int nterms=e->ip[0];
        float px=0,py=0;
        for(int i=0;i<=steps;i++){
            float th=(float)i/steps*6.2831853f;
            float r=1.f;
            for(int k=0;k<nterms;k++){
                r += e->p[k*2]*cosf(e->ip[1+k]*th+e->p[k*2+1]);
            }
            r*=0.5f; if(r<0.05f) r=0.05f;
            float nx=cosf(th+a)*r*s*0.5f+cx;
            float ny=sinf(th+a)*r*s*0.5f+cy;
            if(i>0) draw_line(px,py,nx,ny,t,col,iv);
            px=nx;py=ny;
        }
    } break;

    case P_MESH: {
        float sx=e->p[0], sy=e->p[1], sz=e->p[2];
        float tilt=e->p[3] + e->pattern_t*0.15f;
        float verts[8][3]={{-sx,-sy,-sz},{sx,-sy,-sz},{sx,sy,-sz},{-sx,sy,-sz},
                           {-sx,-sy,sz},{sx,-sy,sz},{sx,sy,sz},{-sx,sy,sz}};
        float ca=cosf(a),sa=sinf(a),cb=cosf(tilt),sb=sinf(tilt);
        for(int i=0;i<8;i++){
            float x=verts[i][0],y=verts[i][1],z=verts[i][2];
            float x1=ca*x+sa*z;
            float z1=-sa*x+ca*z;
            float y1=cb*y-sb*z1;
            float z2=sb*y+cb*z1;
            float d=3.f; float persp=d/(d+z2);
            verts[i][0]=cx+x1*s*0.5f*persp;
            verts[i][1]=cy+y1*s*0.5f*persp;
        }
        int edges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
                          {0,4},{1,5},{2,6},{3,7}};
        for(int i=0;i<12;i++)
            draw_line(verts[edges[i][0]][0],verts[edges[i][0]][1],
                      verts[edges[i][1]][0],verts[edges[i][1]][1],t,col,iv);
    } break;

    case P_SPIRAL: {
        int turns=e->ip[0];
        float growth=e->p[0], dir=e->p[1];
        steps=320;
        float total=(float)turns*6.2831853f;
        float px=0,py=0;
        for(int i=0;i<=steps;i++){
            float u=(float)i/steps;
            float th=u*total*dir;
            float r=powf(u,growth);
            float nx=cosf(th+a)*r*s*0.5f+cx;
            float ny=sinf(th+a)*r*s*0.5f+cy;
            if(i>0) draw_line(px,py,nx,ny,t,col,iv);
            px=nx;py=ny;
        }
    } break;

    case P_CONSTELLATION: {
        int n=e->ip[0];
        float pts[24][2];
        for(int i=0;i<n;i++){
            float px=e->p[i*2]*s*0.5f, py=e->p[i*2+1]*s*0.5f;
            float fx=cx+cosf(a)*px-sinf(a)*py;
            float fy=cy+sinf(a)*px+cosf(a)*py;
            fx += wobx(fx,fy); fy += woby(fx,fy);
            pts[i][0]=fx; pts[i][1]=fy;
        }
        int rd=t+1;
        for(int i=0;i<n;i++)
            for(int dy2=-rd;dy2<=rd;dy2++)
                for(int dx2=-rd;dx2<=rd;dx2++)
                    if(dx2*dx2+dy2*dy2<=rd*rd)
                        plot((int)pts[i][0]+dx2,(int)pts[i][1]+dy2,col,iv);
        for(int i=0;i<n;i++){
            int best=-1; float bd=1e9f;
            for(int j=0;j<n;j++){
                if(j==i) continue;
                float dx=pts[i][0]-pts[j][0], dy=pts[i][1]-pts[j][1];
                float d2=dx*dx+dy*dy;
                if(d2<bd){bd=d2;best=j;}
            }
            if(best>=0&&best!=i)
                draw_line(pts[i][0],pts[i][1],pts[best][0],pts[best][1],t,col,iv);
        }
    } break;

    case P_ROSETTE: {
        int petals=e->ip[0], layers=e->ip[1];
        float inner=e->p[0];
        for(int L=0;L<layers;L++){
            float r_out=s*0.5f*(1.f-(float)L/layers*0.3f);
            float r_in=r_out*inner;
            int st=petals*2;
            for(int i=0;i<st;i++){
                float th0=(float)i/st*6.2831853f+a+(float)L*0.3f;
                float th1=(float)(i+1)/st*6.2831853f+a+(float)L*0.3f;
                float rr0=(i&1)?r_out:r_in;
                float rr1=((i+1)&1)?r_out:r_in;
                draw_line(cx+cosf(th0)*rr0,cy+sinf(th0)*rr0,
                          cx+cosf(th1)*rr1,cy+sinf(th1)*rr1,t,col,iv);
            }
        }
    } break;

    case P_MULTI_SPIRO: {
        int R=e->ip[0], r=e->ip[1];
        float d=e->p[0]*(float)r;
        int copies=e->ip[2];
        int ra=R,rb=r;
        while(rb){int tmp=ra%rb;ra=rb;rb=tmp;}
        float g=(float)ra;
        float tmax=6.2831853f*(float)r/g;
        steps=400;
        float sc=s*0.4f/(float)R;
        for(int c=0;c<copies;c++){
            float offset=(float)c/copies*6.2831853f+a;
            float px=0,py=0;
            for(int i=0;i<=steps;i++){
                float th=(float)i/steps*tmax;
                float nx=(R-r)*cosf(th)+d*cosf((R-r)/(float)r*th);
                float ny=(R-r)*sinf(th)-d*sinf((R-r)/(float)r*th);
                float fx=cx+(cosf(offset)*nx-sinf(offset)*ny)*sc;
                float fy=cy+(sinf(offset)*nx+cosf(offset)*ny)*sc;
                if(i>0) draw_line(px,py,fx,fy,t,col,iv);
                px=fx;py=fy;
            }
        }
    } break;

    case P_BEND: {
        float ang0=e->p[0];
        float ang1=ang0+e->p[1];
        float p0x=cx+cosf(ang0)*s*0.5f, p0y=cy+sinf(ang0)*s*0.5f;
        float p2x=cx+cosf(ang1)*s*0.5f, p2y=cy+sinf(ang1)*s*0.5f;
        float c1x=cx+e->p[2]*s*0.5f, c1y=cy+e->p[3]*s*0.5f;
        steps=80;
        float px=p0x, py=p0y;
        for(int i=1;i<=steps;i++){
            float u=(float)i/steps, mu=1.f-u;
            float bx=mu*mu*p0x+2.f*mu*u*c1x+u*u*p2x;
            float by=mu*mu*p0y+2.f*mu*u*c1y+u*u*p2y;
            draw_line(px,py,bx,by,t,col,iv);
            px=bx;py=by;
        }
    } break;

    case P_STRETCH: {
        float rx0=e->p[0]*s*0.5f, ry0=e->p[1]*s*0.5f;
        float st=1.f+0.8f*sinf(e->size_pulse_t*e->p[2]);
        float rx=rx0*st, ry=ry0/st;
        int segs=64;
        float px=0,py=0;
        for(int i=0;i<=segs;i++){
            float th=(float)i/segs*6.2831853f;
            float ex=cosf(th)*rx, ey=sinf(th)*ry;
            float fx=cx+cosf(a)*ex-sinf(a)*ey;
            float fy=cy+sinf(a)*ex+cosf(a)*ey;
            if(i>0) draw_line(px,py,fx,fy,t,col,iv);
            px=fx;py=fy;
        }
    } break;

    case P_BLOB: {
        int ndots=e->ip[0];
        float fall=e->p[0];
        uint32_t st=e->seed;
        int rd=t;
        for(int i=0;i<ndots;i++){
            st^=st<<13;st^=st>>17;st^=st<<5;
            float u1=(st&0xFFFF)/65535.f;
            st^=st<<13;st^=st>>17;st^=st<<5;
            float u2=(st&0xFFFF)/65535.f;
            float r=powf(u1,fall)*s*0.5f;
            float th=u2*6.2831853f;
            int px=(int)(cx+cosf(th)*r+0.5f);
            int py=(int)(cy+sinf(th)*r+0.5f);
            if(rd<=0) plot_w(px,py,col,iv);
            else {
                for(int dy2=-rd;dy2<=rd;dy2++)
                    for(int dx2=-rd;dx2<=rd;dx2++)
                        if(dx2*dx2+dy2*dy2<=rd*rd)
                            plot_w(px+dx2,py+dy2,col,iv);
            }
        }
    } break;

    case P_DOTS: {
        int n=e->ip[0];
        uint32_t st=e->seed;
        int rd=t;
        for(int i=0;i<n;i++){
            st^=st<<13;st^=st>>17;st^=st<<5;
            float dx=((st&0xFFFF)/65535.f-0.5f)*s;
            st^=st<<13;st^=st>>17;st^=st<<5;
            float dy=((st&0xFFFF)/65535.f-0.5f)*s;
            int px=(int)(cx+dx+0.5f), py=(int)(cy+dy+0.5f);
            if(rd<=0) plot_w(px,py,col,iv);
            else {
                for(int dy2=-rd;dy2<=rd;dy2++)
                    for(int dx2=-rd;dx2<=rd;dx2++)
                        if(dx2*dx2+dy2*dy2<=rd*rd)
                            plot_w(px+dx2,py+dy2,col,iv);
            }
        }
    } break;

    case P_BEZIER: {
        float ang0=e->p[0], ang1=ang0+e->p[1];
        float p0x=cx+cosf(ang0)*s*0.5f, p0y=cy+sinf(ang0)*s*0.5f;
        float p3x=cx+cosf(ang1)*s*0.5f, p3y=cy+sinf(ang1)*s*0.5f;
        float c1x=cx+e->p[2]*s*0.5f, c1y=cy+e->p[3]*s*0.5f;
        float c2x=cx+e->p[4]*s*0.5f, c2y=cy+e->p[5]*s*0.5f;
        steps=90;
        float px=p0x, py=p0y;
        for(int i=1;i<=steps;i++){
            float u=(float)i/steps, mu=1.f-u;
            float bx=mu*mu*mu*p0x+3*mu*mu*u*c1x+3*mu*u*u*c2x+u*u*u*p3x;
            float by=mu*mu*mu*p0y+3*mu*mu*u*c1y+3*mu*u*u*c2y+u*u*u*p3y;
            draw_line(px,py,bx,by,t,col,iv);
            px=bx;py=by;
        }
    } break;

    case P_WAVES: {
        int nw=e->ip[0];
        float freq=e->p[0], amp=e->p[1]*s;
        float spacing=s/(float)(nw+1);
        for(int w=0;w<nw;w++){
            float base_y=cy-s*0.5f+spacing*(float)(w+1);
            float px=cx-s*0.5f;
            float py=base_y+sinf((px-cx)/s*freq*6.2831853f+a)*amp;
            int N=60;
            for(int i=1;i<=N;i++){
                float u=(float)i/N;
                float x=cx-s*0.5f+u*s;
                float y=base_y+sinf((x-cx)/s*freq*6.2831853f+a)*amp;
                draw_line(px,py,x,y,t,col,iv);
                px=x;py=y;
            }
        }
    } break;

    case P_POLYLINE: {
        int n=e->ip[0];
        float vx[16],vy[16];
        uint32_t st=e->seed;
        for(int i=0;i<=n;i++){
            st^=st<<13;st^=st>>17;st^=st<<5;
            float u1=(st&0xFFFF)/65535.f;
            st^=st<<13;st^=st>>17;st^=st<<5;
            float u2=(st&0xFFFF)/65535.f;
            float fx=(u1-0.5f)*s, fy=(u2-0.5f)*s;
            vx[i]=cx+cosf(a)*fx-sinf(a)*fy;
            vy[i]=cy+sinf(a)*fx+cosf(a)*fy;
        }
        for(int i=0;i<n;i++)
            draw_line(vx[i],vy[i],vx[i+1],vy[i+1],t,col,iv);
    } break;

    case P_CURL: {
        int narms=e->ip[0];
        float turns=e->p[0];
        for(int k=0;k<narms;k++){
            float off=(float)k/narms*6.2831853f;
            int N=200;
            float px=cx,py=cy;
            for(int i=1;i<=N;i++){
                float u=(float)i/N;
                float th=u*turns*6.2831853f+off+a;
                float r=u*s*0.5f;
                float nx=cx+cosf(th)*r;
                float ny=cy+sinf(th)*r;
                draw_line(px,py,nx,ny,t,col,iv);
                px=nx;py=ny;
            }
        }
    } break;

    case P_TRIANGLE: {
        float r=s*0.5f, d0=e->p[0]*0.4f;
        float a0=a, a1=a0+2.094f+frange(-d0,d0), a2=a1+2.094f+frange(-d0,d0);
        float x0=cx+cosf(a0)*r,y0=cy+sinf(a0)*r;
        float x1=cx+cosf(a1)*r,y1=cy+sinf(a1)*r;
        float x2=cx+cosf(a2)*r,y2=cy+sinf(a2)*r;
        draw_line(x0,y0,x1,y1,t,col,iv);
        draw_line(x1,y1,x2,y2,t,col,iv);
        draw_line(x2,y2,x0,y0,t,col,iv);
    } break;

    case P_HEX: {
        int n=e->ip[0]; float r=s*0.5f;
        float prevx=0,prevy=0;
        for(int i=0;i<=n;i++){
            float th=(float)(i%n)/n*6.2831853f+a;
            float nx=cx+cosf(th)*r, ny=cy+sinf(th)*r;
            if(i>0) draw_line(prevx,prevy,nx,ny,t,col,iv);
            prevx=nx;prevy=ny;
        }
    } break;

    case P_CROSS: {
        float ax=s*0.5f*e->p[0], ay=s*0.5f*e->p[1];
        draw_line(cx-ax,cy,cx+ax,cy,t,col,iv);
        draw_line(cx,cy-ay,cx,cy+ay,t,col,iv);
        if(e->ip[0]){
            draw_line(cx-ax*0.7f,cy-ay*0.7f,cx+ax*0.7f,cy+ay*0.7f,t,col,iv);
            draw_line(cx-ax*0.7f,cy+ay*0.7f,cx+ax*0.7f,cy-ay*0.7f,t,col,iv);
        }
    } break;

    case P_AMOEBA: {
        int nterms=e->ip[0], segs=72;
        float px=0,py=0;
        for(int i=0;i<=segs;i++){
            float th=(float)i/segs*6.2831853f;
            float r=1.f;
            for(int k=0;k<nterms;k++)
                r += e->p[k*2]*cosf(e->ip[1+k]*th+e->p[k*2+1]);
            r*=0.5f; if(r<0.1f) r=0.1f;
            float nx=cx+cosf(th+a)*r*s*0.5f;
            float ny=cy+sinf(th+a)*r*s*0.5f;
            if(i>0) draw_line(px,py,nx,ny,t,col,iv);
            px=nx;py=ny;
        }
    } break;

    case P_MANY_CIRCLES: {
        int n=e->ip[0];
        float r=e->p[0]*s;
        uint32_t st=e->seed;
        for(int i=0;i<n;i++){
            st^=st<<13;st^=st>>17;st^=st<<5;
            float u1=(st&0xFFFF)/65535.f;
            st^=st<<13;st^=st>>17;st^=st<<5;
            float u2=(st&0xFFFF)/65535.f;
            float px=cx+(u1-0.5f)*s*0.8f, py=cy+(u2-0.5f)*s*0.8f;
            int segs=16;
            float ax=0,ay=0;
            for(int j=0;j<=segs;j++){
                float th=(float)j/segs*6.2831853f;
                float nx=px+cosf(th)*r;
                float ny=py+sinf(th)*r;
                if(j>0) draw_line(ax,ay,nx,ny,t,col,iv);
                ax=nx;ay=ny;
            }
        }
    } break;

    case P_CONCENTRIC: {
        int n=e->ip[0], sides=e->ip[1];
        float rmax=s*0.5f;
        for(int k=1;k<=n;k++){
            float r=rmax*(float)k/n;
            float ax=0,ay=0;
            for(int i=0;i<=sides;i++){
                float th=(float)(i%sides)/sides*6.2831853f+a;
                float nx=cx+cosf(th)*r, ny=cy+sinf(th)*r;
                if(i>0) draw_line(ax,ay,nx,ny,t,col,iv);
                ax=nx;ay=ny;
            }
        }
    } break;

    case P_GRID_DOTS: {
        int n=e->ip[0]; int rd=t+1;
        float step=s/(float)(n-1);
        for(int i=0;i<n;i++)
            for(int j=0;j<n;j++){
                float px=cx-s*0.5f+i*step;
                float py=cy-s*0.5f+j*step;
                for(int dy=-rd;dy<=rd;dy++)
                    for(int dx=-rd;dx<=rd;dx++)
                        if(dx*dx+dy*dy<=rd*rd)
                            plot_w((int)(px+dx+0.5f),(int)(py+dy+0.5f),col,iv);
            }
    } break;

    case P_SCATTER: {
        int n=e->ip[0];
        float dl=s*e->p[0];
        uint32_t st=e->seed;
        for(int i=0;i<n;i++){
            st^=st<<13;st^=st>>17;st^=st<<5;
            float u1=(st&0xFFFF)/65535.f;
            st^=st<<13;st^=st>>17;st^=st<<5;
            float u2=(st&0xFFFF)/65535.f;
            st^=st<<13;st^=st>>17;st^=st<<5;
            float u3=(st&0xFFFF)/65535.f;
            float px=cx+(u1-0.5f)*s, py=cy+(u2-0.5f)*s;
            float da=u3*6.2831853f;
            draw_line(px,py,px+cosf(da)*dl,py+sinf(da)*dl,t,col,iv);
        }
    } break;

    case P_DNA: {
        int cycles=e->ip[0];
        float amp=s*e->p[0];
        int N=60;
        float px1=0,py1=0,px2=0,py2=0;
        for(int i=0;i<=N;i++){
            float u=(float)i/N;
            float x=cx-s*0.5f+u*s;
            float y1=cy+sinf(u*cycles*6.2831853f+a)*amp;
            float y2=cy+sinf(u*cycles*6.2831853f+a+3.14159265f)*amp;
            if(i>0){
                draw_line(px1,py1,x,y1,t,col,iv);
                draw_line(px2,py2,x,y2,t,col,iv);
            }
            if(i%5==0) draw_line(x,y1,x,y2,t,col,iv);
            px1=x;py1=y1;px2=x;py2=y2;
        }
    } break;

    case P_RIPPLE: {
        int n=e->ip[0];
        float speed=e->p[0];
        float phase=fmodf(e->size_pulse_t*speed,1.f);
        for(int k=0;k<n;k++){
            float r=((float)k+phase)/n*s*0.5f;
            int segs=32;
            float px=0,py=0;
            for(int i=0;i<=segs;i++){
                float th=(float)i/segs*6.2831853f;
                float nx=cx+cosf(th)*r, ny=cy+sinf(th)*r;
                if(i>0) draw_line(px,py,nx,ny,t,col,iv);
                px=nx;py=ny;
            }
        }
    } break;

    case P_PETAL: {
        int k=e->ip[0]; int N=240;
        float px=0,py=0;
        for(int i=0;i<=N;i++){
            float th=(float)i/N*6.2831853f;
            float r=fabsf(cosf(k*th*0.5f));
            float nx=cx+cosf(th+a)*r*s*0.5f;
            float ny=cy+sinf(th+a)*r*s*0.5f;
            if(i>0) draw_line(px,py,nx,ny,t,col,iv);
            px=nx;py=ny;
        }
    } break;

    case P_MOON: {
        float r=s*0.5f, r2=r*e->p[0], d=r*e->p[1];
        int segs=64;
        float px=0,py=0; int first=1;
        for(int i=0;i<=segs;i++){
            float th=(float)i/segs*6.2831853f+a;
            float nx=cx+cosf(th)*r, ny=cy+sinf(th)*r;
            float dxc=nx-(cx+cosf(a)*d), dyc=ny-(cy+sinf(a)*d);
            if(dxc*dxc+dyc*dyc>r2*r2){
                if(!first) draw_line(px,py,nx,ny,t,col,iv);
                px=nx;py=ny;first=0;
            } else first=1;
        }
    } break;

    case P_ARC_FAN: {
        int n=e->ip[0];
        float r=s*0.5f, arc_len=e->p[0]*3.14159265f;
        for(int k=0;k<n;k++){
            float start=a+(float)k/n*6.2831853f;
            int segs=10;
            float px=0,py=0;
            for(int i=0;i<=segs;i++){
                float th=start+(float)i/segs*arc_len;
                float nx=cx+cosf(th)*r, ny=cy+sinf(th)*r;
                if(i>0) draw_line(px,py,nx,ny,t,col,iv);
                px=nx;py=ny;
            }
        }
    } break;

    // ------------- new -------------
    case P_SINE: {
        int nw=e->ip[0];
        float freq=e->p[0], amp=e->p[1]*s;
        float spacing=s/(float)(nw+1);
        for(int w=0;w<nw;w++){
            float base_y=cy-s*0.5f+spacing*(float)(w+1);
            float px=cx-s*0.5f;
            float py=base_y+sinf((px-cx)/s*freq*6.2831853f+a)*amp;
            int N=80;
            for(int i=1;i<=N;i++){
                float u=(float)i/N;
                float x=cx-s*0.5f+u*s;
                float y=base_y+sinf((x-cx)/s*freq*6.2831853f+a)*amp;
                draw_line(px,py,x,y,t,col,iv);
                px=x;py=y;
            }
        }
    } break;

    case P_ZIGZAG: {
        int n=e->ip[0];
        float amp=e->p[0]*s*0.5f;
        float prevx=cx-s*0.5f, prevy=cy;
        for(int i=1;i<=n;i++){
            float u=(float)i/n;
            float x=cx-s*0.5f+u*s;
            float y=cy+((i&1)?-1.f:1.f)*amp*(0.5f+frand()*0.5f);
            draw_line(prevx,prevy,x,y,t,col,iv);
            prevx=x;prevy=y;
        }
    } break;

    case P_CARDIOID: {
        int N=300;
        float k=e->p[0];
        float px=0,py=0;
        for(int i=0;i<=N;i++){
            float th=(float)i/N*6.2831853f;
            float r=(1.f+cosf(th))*0.5f;
            r += k*0.15f*sinf(th*3.f);
            if(r<0) r=0;
            float nx=cx+cosf(th+a)*r*s*0.5f;
            float ny=cy+sinf(th+a)*r*s*0.5f;
            if(i>0) draw_line(px,py,nx,ny,t,col,iv);
            px=nx;py=ny;
        }
    } break;

    case P_LEMNISCATE: {
        int N=280;
        int k=e->ip[0];
        float px=0,py=0;
        for(int i=0;i<=N;i++){
            float th=(float)i/N*6.2831853f;
            float d=1.f+sinf(k*th)*sinf(k*th);
            float r=(d>0.01f)?(1.f/d):1.f;
            float nx=cx+cosf(th+a)*r*s*0.35f;
            float ny=cy+sinf(th+a)*r*s*0.35f;
            if(i>0) draw_line(px,py,nx,ny,t,col,iv);
            px=nx;py=ny;
        }
    } break;

    case P_BRANCH: {
        int arms=e->ip[0], depth=e->ip[1];
        for(int k=0;k<arms;k++){
            float base_ang=a+(float)k/arms*6.2831853f;
            // draw a simple 2-level branch
            float x0=cx, y0=cy;
            float L=s*0.5f;
            for(int d=0;d<depth;d++){
                float ang=base_ang+frange(-0.6f,0.6f);
                float x1=x0+cosf(ang)*L, y1=y0+sinf(ang)*L;
                draw_line(x0,y0,x1,y1,t,col,iv);
                // branch off
                float b_ang=ang+frange(-0.8f,0.8f);
                float bx=x1+cosf(b_ang)*L*0.6f;
                float by=y1+sinf(b_ang)*L*0.6f;
                draw_line(x1,y1,bx,by,t,col,iv);
                x0=x1;y0=y1;
                L*=0.6f;
            }
        }
    } break;

    case P_TRI_FAN: {
        int n=e->ip[0];
        float r=s*0.5f;
        for(int k=0;k<n;k++){
            float th=a+(float)k/n*6.2831853f;
            float nx=cx+cosf(th)*r, ny=cy+sinf(th)*r;
            draw_line(cx,cy,nx,ny,t,col,iv);
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
            e->x = frange(BUF_W * 0.15f, BUF_W * 0.85f);
            e->y = frange(BUF_H * 0.15f, BUF_H * 0.85f);
            e->vx = frange(-22.0f, 22.0f);
            e->vy = frange(-22.0f, 22.0f);
            e->angle = frange(0.0f, 6.2831853f);
            e->spin  = frange(-1.2f, 1.2f);

            // size: bias to small, but allow big
            float u = frand();
            u = u * u;
            e->size_base = BUF_H * (0.05f + u * 0.45f);   // ~18..180 px

            e->size = e->size_base;

            // size pulse distribution: 15 / 35 / 45 / 5
            int pr = (int)(xr() % 100);
            if (pr < 15)      e->size_pulse = frange(0.30f, 0.60f);
            else if (pr < 50) e->size_pulse = frange(0.08f, 0.20f);
            else if (pr < 95) e->size_pulse = frange(0.00f, 0.02f);
            else              e->size_pulse = frange(0.00f, 0.80f);

            e->size_pulse_t = frange(0.0f, 6.28f);
            e->color        = 1 + (xr() % NCOLORS);

            roll_pattern(e);

            // per-entity wobble parameters
            e->pattern_t = 0.0f;
            e->anim_f1 = 0.4f + frand() * 2.0f;
            e->anim_f2 = 0.4f + frand() * 2.0f;
            e->anim_a1 = 1.0f + frand() * 4.0f;
            e->anim_a2 = 1.0f + frand() * 4.0f;

            e->in_dur   = frange(1.2f, 3.0f);
            e->hold_dur = frange(8.0f, 45.0f);
            e->out_dur  = frange(1.2f, 3.0f);
            e->phase    = 0;
            e->life     = 0.0f;
            e->presence = 0.0f;
            e->key      = pick_note();

            float sz_u = (e->size_base - BUF_H * 0.05f) / (BUF_H * 0.45f);
            if (sz_u < 0.0f) sz_u = 0.0f;
            if (sz_u > 1.0f) sz_u = 1.0f;
            e->velocity = 0.15f + sz_u * 0.45f;

            // Edge blend: no more "60% low" bias, and higher ceiling.
            float b = frand();
            if (frand() < 0.35f) b *= 0.35f;
            e->blend = 0.05f + b * 0.95f;

            e->retrig_timer = frange(2.5f, 5.5f);
            e->thickness    = rrange(0, 2);
            e->will_morph   = (xr() % 100) < 30;
            e->note_playing = 0;

            // NOTE: no note_on yet.  It triggers when the pattern becomes visible.
            return;
        }
    }
}

static void update_entities(float dt) {
    for (int i = 0; i < MAX_ENT; i++) {
        Entity *e = &ents[i];
        if (e->phase == -1) continue;

        e->life += dt;
        e->pattern_t += dt;
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

            // trigger note once the pattern is visibly emerging
            if (synth && !e->note_playing && e->presence > 0.35f) {
                e->key = pick_note();
                tsf_note_on(synth, color_preset[e->color - 1],
                            e->key, e->velocity);
                e->note_playing = 1;
            }
        } else if (e->phase == 1) {
            e->presence = 1.0f;

            // if note never played (e.g. fast skip), start it now
            if (synth && !e->note_playing) {
                e->key = pick_note();
                tsf_note_on(synth, color_preset[e->color - 1],
                            e->key, e->velocity);
                e->note_playing = 1;
            }

            // retrigger, modulated by size and motion
            e->retrig_timer -= dt;
            if (e->retrig_timer <= 0.0f && synth) {
                tsf_note_off(synth, color_preset[e->color - 1], e->key);

                float size_ratio = e->size / e->size_base;
                int bias = 0;
                if (size_ratio > 1.05f && (xr() % 100) < 70) bias =  1;
                else if (size_ratio < 0.95f && (xr() % 100) < 70) bias = -1;
                e->key = pick_note_biased(bias);

                tsf_note_on(synth, color_preset[e->color - 1], e->key,
                            e->velocity * frange(0.85f, 1.15f));

                // activity = size pulse + movement speed
                float pulse_act = e->size_pulse * fabsf(sinf(e->size_pulse_t * 0.7f));
                float speed = sqrtf(e->vx*e->vx + e->vy*e->vy);
                float speed_act = speed / 22.0f;
                float activity = pulse_act + speed_act * 0.5f;
                if (activity > 1.5f) activity = 1.5f;

                float base_int = frange(1.8f, 4.0f);
                e->retrig_timer = base_int / (1.0f + 1.8f * activity);
                if (e->retrig_timer < 0.5f) e->retrig_timer = 0.5f;
            }

            if (e->life >= e->hold_dur) { e->phase = 2; e->life = 0.0f; }
        } else if (e->phase == 2) {
            float u = e->life / e->out_dur;
            if (u >= 1.0f) {
                // stop note
                if (synth && e->note_playing) {
                    tsf_note_off(synth, color_preset[e->color - 1], e->key);
                    e->note_playing = 0;
                }

                if (e->will_morph) {
                    int   keep = e->color;
                    float kv   = e->velocity;
                    roll_pattern(e);
                    e->color       = keep;
                    e->velocity    = kv;
                    e->phase       = 0;
                    e->life        = 0.0f;
                    e->presence    = 0.0f;
                    e->pattern_t   = 0.0f;
                    e->anim_f1 = 0.4f + frand() * 2.0f;
                    e->anim_f2 = 0.4f + frand() * 2.0f;
                    e->anim_a1 = 1.0f + frand() * 4.0f;
                    e->anim_a2 = 1.0f + frand() * 4.0f;
                    e->in_dur      = frange(1.2f, 3.0f);
                    e->hold_dur    = frange(8.0f, 45.0f);
                    e->out_dur     = frange(1.2f, 3.0f);
                    e->thickness   = rrange(0, 2);
                    e->retrig_timer = frange(2.5f, 5.5f);
                    e->will_morph  = (xr() % 100) < 30;

                    float b = frand();
                    if (frand() < 0.35f) b *= 0.35f;
                    e->blend = 0.05f + b * 0.95f;
                } else {
                    e->phase = -1;
                }
            } else {
                e->presence = 1.0f - u;
                // stop the note as the pattern fades out
                if (synth && e->note_playing && e->presence < 0.3f) {
                    tsf_note_off(synth, color_preset[e->color - 1], e->key);
                    e->note_playing = 0;
                }
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
    NCOLORS = rrange(2, 256);

    int n_white = 0, n_black = 0, n_gray = 0;
    if (NCOLORS >= 4) {
        if (xr() % 100 < 30) n_white = 1;
        if (xr() % 100 < 20) n_black = 1;
        if (xr() % 100 < 10) n_gray  = 1;
    } else {
        if (xr() % 100 < 15) n_white = 1;
    }
    int achroma = n_white + n_black + n_gray;
    int chromatic_n = NCOLORS - achroma;
    if (chromatic_n < 1) chromatic_n = 1;

    for (int i = 0; i < chromatic_n; i++) {
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

    int idx = chromatic_n;
    if (n_white) { colors[idx][0]=colors[idx][1]=colors[idx][2]=255; idx++; }
    if (n_black) { colors[idx][0]=colors[idx][1]=colors[idx][2]=0;   idx++; }
    if (n_gray)  { colors[idx][0]=colors[idx][1]=colors[idx][2]=128; idx++; }
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
            int v = (int)out[i] * 40 / 100;
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

    pick_scale();

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
    fg_col  = malloc(sizeof(uint16_t) * n);
    memset(fg_int, 0, n);
    memset(fg_col, 0, sizeof(uint16_t) * n);
    for (int i = 0; i < MAX_ENT; i++) ents[i].phase = -1;

    Uint32 last = SDL_GetTicks();
    float  spawn_timer = 0.2f;
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
            spawn_timer = frange(3.0f, 8.0f);
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