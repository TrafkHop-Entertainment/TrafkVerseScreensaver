/*
 * Random Noise Pattern Generator V3 (Closed-Eye Hallucination Model)
 * Kompilieren: gcc -O3 main.c -o random_noise -lSDL2 -lm
 */

#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <string.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"

#define GRID_W 320
#define GRID_H 180
#define NUM_CELLS (GRID_W * GRID_H)

#define MAX_COLORS 35
#define MIN_COLORS 5
#define SAMPLE_RATE 44100

// --- DATENSTRUKTUREN ---

typedef struct {
    float r, g, b;
    int sf2_preset;
} ColorDef;

typedef struct {
    Uint8 bg_r, bg_g, bg_b; // Das statische, dunkle Hintergrund-Noise
    float state;            // 0.0 (Hintergrund) bis 1.0 (Vordergrund/Muster)
    float target_state;
    int active_color_idx;
} NoiseCell;

typedef enum {
    SHAPE_NONE,
    SHAPE_LINE,
    SHAPE_CIRCLE,
    SHAPE_TRIANGLE,
    SHAPE_JUMPING_MAN,
    SHAPE_ORGANIC_BLOB
} ShapeType;

typedef struct {
    ShapeType type;
    float x, y;          // Zentrum (0.0 bis 1.0)
    float size;          // Größe der Form
    float duration;      // Wie lange das Muster bleibt
    float timer;
    int color_idx;
    bool active;
    float anim_phase;
} ActivePattern;

// --- GLOBALE VARIABLEN ---

static NoiseCell grid[NUM_CELLS];
static ColorDef colors[MAX_COLORS];
static int num_active_colors = 0;
static ActivePattern current_pattern;

static float global_time = 0.0f;
static float startup_delay = 5.0f; // 5 Sekunden nur schwarzes Noise am Anfang

static tsf* g_sf2 = NULL;
static SDL_AudioDeviceID g_audio_device;

// --- HILFSFUNKTIONEN ---

static float randf() { return (float)rand() / (float)RAND_MAX; }

static void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b) {
    int i = (int)(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (i % 6) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        case 5: *r = v; *g = p; *b = q; break;
    }
}

// --- AUDIO SYSTEM ---

static char* find_sf2_file() {
    DIR *d = opendir(".");
    struct dirent *dir;
    static char sf2_path[512];
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            char *ext = strrchr(dir->d_name, '.');
            if (ext && strcasecmp(ext, ".sf2") == 0) {
                snprintf(sf2_path, sizeof(sf2_path), "./%s", dir->d_name);
                closedir(d);
                return sf2_path;
            }
        }
        closedir(d);
    }
    return NULL;
}

static void AudioCallback(void* userdata, Uint8* stream, int len) {
    short* samples = (short*)stream;
    int sample_count = len / sizeof(short);
    if (g_sf2) {
        tsf_render_short(g_sf2, samples, sample_count / 2, 0);
    } else {
        memset(stream, 0, len);
    }
}

static void init_audio() {
    char* sf2_file = find_sf2_file();
    if (!sf2_file) return;

    g_sf2 = tsf_load_filename(sf2_file);
    if (!g_sf2) return;
    tsf_set_output(g_sf2, TSF_STEREO_INTERLEAVED, SAMPLE_RATE, 0.0f);

    SDL_AudioSpec requested, obtained;
    SDL_zero(requested);
    requested.freq = SAMPLE_RATE;
    requested.format = AUDIO_S16SYS;
    requested.channels = 2;
    requested.samples = 1024;
    requested.callback = AudioCallback;

    g_audio_device = SDL_OpenAudioDevice(NULL, 0, &requested, &obtained, 0);
    if (g_audio_device != 0) {
        SDL_PauseAudioDevice(g_audio_device, 0);
    }
}

// --- INITIALISIERUNG ---

static void init_system() {
    num_active_colors = MIN_COLORS + rand() % (MAX_COLORS - MIN_COLORS + 1);
    int preset_count = g_sf2 ? tsf_get_presetcount(g_sf2) : 0;

    for (int i = 0; i < num_active_colors; i++) {
        float hue = randf();
        // Knallige Farben (volle Saturation & Value)
        hsv_to_rgb(hue, 1.0f, 1.0f, &colors[i].r, &colors[i].g, &colors[i].b);
        colors[i].sf2_preset = preset_count > 0 ? (rand() % preset_count) : 0;
    }

    // Grid initialisieren (Mikroskopisches Hintergrund-Noise)
    for (int i = 0; i < NUM_CELLS; i++) {
        grid[i].state = 0.0f;
        grid[i].target_state = 0.0f;
        grid[i].active_color_idx = 0;
        
        if (randf() < 0.02f) {
            // Seltene extrem dunkle farbige Pixel (meist dunkelrot)
            grid[i].bg_r = 30 + rand() % 20;
            grid[i].bg_g = 5;
            grid[i].bg_b = 5;
        } else {
            // Schwarz/Graues Spektrum
            Uint8 grey = 5 + rand() % 20;
            grid[i].bg_r = grey;
            grid[i].bg_g = grey;
            grid[i].bg_b = grey;
        }
    }
    
    current_pattern.active = false;
}

// --- FORMEN & MUSTER LOGIK ---

// Prüft, ob ein Punkt (px, py) Teil der aktuellen Form ist (gibt 0.0 oder 1.0 zurück)
static float evaluate_shape(float px, float py, ActivePattern* p) {
    float dx = px - p->x;
    float dy = py - p->y;
    
    if (p->type == SHAPE_CIRCLE) {
        return (dx*dx + dy*dy < p->size*p->size) ? 1.0f : 0.0f;
    }
    else if (p->type == SHAPE_LINE) {
        float dist = fabsf(dx * cosf(p->anim_phase) + dy * sinf(p->anim_phase));
        return (dist < 0.02f && fabsf(dx) < p->size && fabsf(dy) < p->size) ? 1.0f : 0.0f;
    }
    else if (p->type == SHAPE_ORGANIC_BLOB) {
        float angle = atan2f(dy, dx);
        float radius = p->size * (0.8f + 0.4f * sinf(angle * 5.0f + p->anim_phase));
        return (dx*dx + dy*dy < radius*radius) ? 1.0f : 0.0f;
    }
    else if (p->type == SHAPE_JUMPING_MAN) {
        // Simple Strichmännchen-Logik aus Pixel-Sicht
        float jump_y = fabsf(sinf(p->anim_phase * 4.0f)) * 0.1f;
        float cy = py + jump_y; // Korrigierte Y-Position für den Sprung
        
        // Kopf
        if ((px-p->x)*(px-p->x) + (cy-(p->y-0.1f))*(cy-(p->y-0.1f)) < 0.002f) return 1.0f;
        // Körper
        if (fabsf(px-p->x) < 0.01f && cy > p->y-0.08f && cy < p->y+0.05f) return 1.0f;
        // Beine
        float leg_spread = sinf(p->anim_phase * 4.0f) * 0.05f;
        if (cy >= p->y+0.05f && cy < p->y+0.15f) {
            float progress = (cy - (p->y+0.05f)) / 0.1f;
            if (fabsf(px - (p->x - leg_spread * progress)) < 0.01f) return 1.0f;
            if (fabsf(px - (p->x + leg_spread * progress)) < 0.01f) return 1.0f;
        }
        // Arme
        if (cy >= p->y-0.05f && cy < p->y+0.05f) {
            float progress = (cy - (p->y-0.05f)) / 0.1f;
            if (fabsf(px - (p->x - 0.08f * progress)) < 0.01f) return 1.0f;
            if (fabsf(px - (p->x + 0.08f * progress)) < 0.01f) return 1.0f;
        }
        return 0.0f;
    }
    return 0.0f;
}

static void update_logic(float delta_time) {
    global_time += delta_time;
    
    // 1. Muster-Director (Generiert neue Muster oder löst sie auf)
    if (global_time > startup_delay) {
        if (!current_pattern.active) {
            // Neues Muster generieren
            current_pattern.active = true;
            current_pattern.type = 1 + rand() % 4; // SHAPE_LINE bis SHAPE_ORGANIC_BLOB
            current_pattern.x = 0.2f + randf() * 0.6f;
            current_pattern.y = 0.2f + randf() * 0.6f;
            current_pattern.size = 0.1f + randf() * 0.2f;
            current_pattern.duration = 4.0f + randf() * 8.0f; // Bleibt einige Sekunden
            current_pattern.timer = 0.0f;
            current_pattern.color_idx = rand() % num_active_colors;
            
            // Sound für dieses Muster abspielen
            if (g_sf2) {
                int preset = colors[current_pattern.color_idx].sf2_preset;
                int note = 48 + (int)(current_pattern.y * 36.0f); // Tonhöhe hängt von Y ab
                tsf_channel_set_presetindex(g_sf2, 0, preset);
                tsf_channel_note_on(g_sf2, 0, note, 0.8f);
            }
        } else {
            // Muster animieren und Timer prüfen
            current_pattern.timer += delta_time;
            current_pattern.anim_phase += delta_time * 2.0f;
            
            if (current_pattern.timer > current_pattern.duration) {
                current_pattern.active = false; // Muster fließt wieder in Noise zurück
            }
        }
    }

    // 2. Zelluläres Automaten-Update (Noise morpht in Formen)
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            int i = y * GRID_W + x;
            float px = (float)x / GRID_W;
            float py = (float)y / GRID_H;
            
            grid[i].target_state = 0.0f;
            
            if (current_pattern.active) {
                float in_shape = evaluate_shape(px, py, &current_pattern);
                // Zufallsfaktor einbauen, damit es "Noise"-artig wächst und nicht perfekt glatt ist
                if (in_shape > 0.0f && randf() < 0.6f) {
                    grid[i].target_state = 1.0f;
                    grid[i].active_color_idx = current_pattern.color_idx;
                }
            }

            // Morphen: Sanfter Übergang zwischen Hintergrund-Noise und Muster
            if (grid[i].state < grid[i].target_state) {
                grid[i].state += delta_time * 2.0f; // Wächst
                if (grid[i].state > 1.0f) grid[i].state = 1.0f;
            } else if (grid[i].state > grid[i].target_state) {
                grid[i].state -= delta_time * 1.5f; // Zerfällt zurück
                if (grid[i].state < 0.0f) grid[i].state = 0.0f;
            }
        }
    }
}

// --- HAUPTPROGRAMM ---

int main(int argc, char* argv[]) {
    srand((unsigned int)time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        printf("SDL Fehler: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Random Noise Patterns V3",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    init_audio();
    init_system();

    bool running = true;
    SDL_Event event;
    Uint32 last_ticks = SDL_GetTicks();

    while (running) {
        Uint32 current_ticks = SDL_GetTicks();
        float delta_time = (current_ticks - last_ticks) / 1000.0f;
        if (delta_time > 0.1f) delta_time = 0.1f;
        last_ticks = current_ticks;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) { // ESC beendet das Programm
                    running = false;
                } else if (event.key.keysym.sym == SDLK_F11) {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    SDL_SetWindowFullscreen(window, (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
            }
        }

        update_logic(delta_time);

        int win_w, win_h;
        SDL_GetRendererOutputSize(renderer, &win_w, &win_h);
        
        // Raster-Skalierung berechnen
        float cell_w = (float)win_w / GRID_W;
        float cell_h = (float)win_h / GRID_H;
        
        // Maximal 1cm bei 1m Breite = ca 1% Skalierung
        float max_size_px = win_w * 0.01f; 
        if (max_size_px < 5.0f) max_size_px = 5.0f;

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        for (int y = 0; y < GRID_H; y++) {
            for (int x = 0; x < GRID_W; x++) {
                int i = y * GRID_W + x;
                NoiseCell* c = &grid[i];
                
                int draw_x = (int)(x * cell_w);
                int draw_y = (int)(y * cell_h);
                
                if (c->state <= 0.01f) {
                    // Reines Hintergrund-Noise zeichnen (immer extrem klein und dunkel, flackert)
                    if (randf() > 0.5f) { // Simuliert das "Flimmern" des Rauschens
                        SDL_SetRenderDrawColor(renderer, c->bg_r, c->bg_g, c->bg_b, 255);
                        SDL_RenderDrawPoint(renderer, draw_x, draw_y);
                    }
                } else {
                    // Punkt wächst in ein Muster hinein
                    ColorDef* col = &colors[c->active_color_idx];
                    
                    // Helligkeit hängt 1:1 von der Größe (state) ab. 
                    // state 1.0 = volle, knallige Farbe.
                    Uint8 r = (Uint8)((c->bg_r * (1.0f - c->state)) + (col->r * c->state * 255));
                    Uint8 g = (Uint8)((c->bg_g * (1.0f - c->state)) + (col->g * c->state * 255));
                    Uint8 b = (Uint8)((c->bg_b * (1.0f - c->state)) + (col->b * c->state * 255));
                    
                    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
                    
                    int current_size = (int)(c->state * max_size_px);
                    if (current_size <= 1) {
                        SDL_RenderDrawPoint(renderer, draw_x, draw_y);
                    } else {
                        SDL_Rect rect = { draw_x - current_size/2, draw_y - current_size/2, current_size, current_size };
                        SDL_RenderFillRect(renderer, &rect);
                    }
                }
            }
        }

        SDL_RenderPresent(renderer);
    }

    if (g_sf2) tsf_close(g_sf2);
    if (g_audio_device) SDL_CloseAudioDevice(g_audio_device);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}