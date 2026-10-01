/*
 * Random Noise Pattern Generator V4
 * Linux (Wayland/PulseAudio via SDL2 + TSF)
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

// Raster-Auflösung für feines Rauschen
#define GRID_W 400
#define GRID_H 225
#define NUM_CELLS (GRID_W * GRID_H)

#define MAX_COLORS 35
#define MIN_COLORS 5
#define MAX_PATTERNS 8
#define MAX_NOTES 16
#define SAMPLE_RATE 44100

typedef struct {
    float r, g, b;
    int sf2_preset;
} ColorDef;

typedef struct {
    float state;         // 0.0 (Hintergrund) bis 1.0 (Vordergrund)
    float target_state;
    int color_idx;
    Uint8 bg_brightness;
    bool is_dark_red;
} Cell;

typedef enum {
    SHAPE_LINE,
    SHAPE_CIRCLE,
    SHAPE_TRIANGLE,
    SHAPE_ORGANIC_BLOB,
    SHAPE_JUMPING_MAN,
    SHAPE_SPIRAL
} ShapeType;

typedef struct {
    bool active;
    ShapeType type;
    float x, y;          // Normierte Position (0.0 bis 1.0)
    float size;          // Ziel-Größe
    float life_time;     // Gesamte Lebensdauer
    float age;           // Aktuelles Alter
    int color_idx;
    float anim_phase;
    float speed;
} Pattern;

typedef struct {
    bool active;
    int channel;
    int key;
    float duration;
    float time_left;
} ActiveNote;

// --- GLOBALE ZUSTÄNDE ---
static Cell grid[NUM_CELLS];
static ColorDef colors[MAX_COLORS];
static int num_active_colors = 0;

static Pattern patterns[MAX_PATTERNS];
static ActiveNote active_notes[MAX_NOTES];

static tsf* g_sf2 = NULL;
static SDL_AudioDeviceID g_audio_device;

// --- HELPER FUNKTIONEN ---
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

// Spiele Ton ab und verwalte die Laufzeit (verhindert endloses Stacking)
static void play_color_note(int color_idx, float norm_y) {
    if (!g_sf2) return;

    // Freien Noten-Slot suchen
    int slot = -1;
    for (int i = 0; i < MAX_NOTES; i++) {
        if (!active_notes[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return; // Alle Noten-Kanäle voll

    int preset = colors[color_idx].sf2_preset;
    int channel = slot % 16; // Auf MIDI-Kanäle verteilen
    
    // Pentatonische Skala für harmonischen Klang
    int scale[] = {48, 50, 52, 55, 57, 60, 62, 64, 67, 69, 72, 74, 76, 79};
    int note_idx = (int)((1.0f - norm_y) * (sizeof(scale)/sizeof(int) - 1));
    if (note_idx < 0) note_idx = 0;
    int key = scale[note_idx];

    tsf_channel_set_presetindex(g_sf2, channel, preset);
    tsf_channel_note_on(g_sf2, channel, key, 0.6f);

    active_notes[slot].active = true;
    active_notes[slot].channel = channel;
    active_notes[slot].key = key;
    active_notes[slot].duration = 0.8f + randf() * 1.5f; // Dauer 0.8s - 2.3s
    active_notes[slot].time_left = active_notes[slot].duration;
}

static void update_audio_notes(float delta_time) {
    if (!g_sf2) return;

    for (int i = 0; i < MAX_NOTES; i++) {
        if (active_notes[i].active) {
            active_notes[i].time_left -= delta_time;
            if (active_notes[i].time_left <= 0.0f) {
                // Ton nach Ablauf der Zeit beenden
                tsf_channel_note_off(g_sf2, active_notes[i].channel, active_notes[i].key);
                active_notes[i].active = false;
            }
        }
    }
}

// --- SYSTEM INITIALISIERUNG ---
static void init_system() {
    num_active_colors = MIN_COLORS + rand() % (MAX_COLORS - MIN_COLORS + 1);
    int preset_count = g_sf2 ? tsf_get_presetcount(g_sf2) : 0;

    for (int i = 0; i < num_active_colors; i++) {
        float hue = randf();
        // Knallige, volle Farben (Full Saturation & Value)
        hsv_to_rgb(hue, 1.0f, 1.0f, &colors[i].r, &colors[i].g, &colors[i].b);
        colors[i].sf2_preset = preset_count > 0 ? (rand() % preset_count) : 0;
    }

    for (int i = 0; i < NUM_CELLS; i++) {
        grid[i].state = 0.0f;
        grid[i].target_state = 0.0f;
        grid[i].color_idx = 0;
        grid[i].bg_brightness = 5 + (rand() % 25);
        grid[i].is_dark_red = (rand() % 150 == 0); // Seltene dunkelrote Punkte
    }

    for (int i = 0; i < MAX_PATTERNS; i++) {
        patterns[i].active = false;
    }

    for (int i = 0; i < MAX_NOTES; i++) {
        active_notes[i].active = false;
    }
}

// --- MUSTER EVALUATOR ---
static float evaluate_pattern(Pattern* p, float px, float py) {
    float dx = px - p->x;
    float dy = py - p->y;
    float dist_sq = dx*dx + dy*dy;

    switch (p->type) {
        case SHAPE_CIRCLE: {
            float r = p->size;
            return (dist_sq < r*r) ? 1.0f : 0.0f;
        }
        case SHAPE_LINE: {
            float angle = p->anim_phase;
            float line_dist = fabsf(dx * cosf(angle) + dy * sinf(angle));
            return (line_dist < 0.015f && dist_sq < p->size * p->size) ? 1.0f : 0.0f;
        }
        case SHAPE_ORGANIC_BLOB: {
            float angle = atan2f(dy, dx);
            float blob_r = p->size * (0.7f + 0.3f * sinf(angle * 4.0f + p->anim_phase));
            return (dist_sq < blob_r * blob_r) ? 1.0f : 0.0f;
        }
        case SHAPE_TRIANGLE: {
            float r = p->size;
            if (dist_sq > r*r) return 0.0f;
            float qx = fabsf(dx);
            return (dy > -r && dy < r && qx < (r - dy) * 0.6f) ? 1.0f : 0.0f;
        }
        case SHAPE_JUMPING_MAN: {
            float jump = fabsf(sinf(p->anim_phase * 5.0f)) * 0.08f;
            float cy = py + jump;
            // Kopf
            if ((px-p->x)*(px-p->x) + (cy-(p->y-0.08f))*(cy-(p->y-0.08f)) < 0.001f) return 1.0f;
            // Körper
            if (fabsf(px-p->x) < 0.008f && cy > p->y-0.06f && cy < p->y+0.04f) return 1.0f;
            // Beine
            float spread = sinf(p->anim_phase * 5.0f) * 0.04f;
            if (cy >= p->y+0.04f && cy < p->y+0.12f) {
                float leg_p = (cy - (p->y+0.04f)) / 0.08f;
                if (fabsf(px - (p->x - spread * leg_p)) < 0.008f) return 1.0f;
                if (fabsf(px - (p->x + spread * leg_p)) < 0.008f) return 1.0f;
            }
            return 0.0f;
        }
        case SHAPE_SPIRAL: {
            float r = sqrtf(dist_sq);
            if (r > p->size) return 0.0f;
            float angle = atan2f(dy, dx) + p->anim_phase;
            float spiral_r = (fmodf(angle, 3.14159f) / 3.14159f) * p->size;
            return (fabsf(r - spiral_r) < 0.02f) ? 1.0f : 0.0f;
        }
    }
    return 0.0f;
}

// --- LOGIK & LOGIK-DIRECTOR ---
static void update_simulation(float delta_time) {
    update_audio_notes(delta_time);

    // 1. Spawne neue Muster zufällig über die Zeit
    if (randf() < 0.03f) { // Chance pro Frame
        for (int i = 0; i < MAX_PATTERNS; i++) {
            if (!patterns[i].active) {
                patterns[i].active = true;
                patterns[i].type = (ShapeType)(rand() % 6);
                patterns[i].x = 0.15f + randf() * 0.7f;
                patterns[i].y = 0.15f + randf() * 0.7f;
                patterns[i].size = 0.08f + randf() * 0.18f;
                patterns[i].life_time = 3.0f + randf() * 12.0f; // 3 bis 15 Sekunden
                patterns[i].age = 0.0f;
                patterns[i].color_idx = rand() % num_active_colors;
                patterns[i].anim_phase = randf() * 6.28f;
                patterns[i].speed = 1.0f + randf() * 2.0f;

                // Musik-Trigger bei Muster-Entstehung
                play_color_note(patterns[i].color_idx, patterns[i].y);
                break;
            }
        }
    }

    // 2. Muster aktualisieren
    for (int i = 0; i < MAX_PATTERNS; i++) {
        if (patterns[i].active) {
            patterns[i].age += delta_time;
            patterns[i].anim_phase += delta_time * patterns[i].speed;

            if (patterns[i].age >= patterns[i].life_time) {
                patterns[i].active = false; // Zurück ins Rauschen fließen
            }
        }
    }

    // 3. Grid-Zellen berechnen (Muster + Wuselndes Rauschen)
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            int idx = y * GRID_W + x;
            float px = (float)x / GRID_W;
            float py = (float)y / GRID_H;

            float max_val = 0.0f;
            int dominant_color = 0;

            // Alle aktiven Muster auf diese Zelle anwenden
            for (int p = 0; p < MAX_PATTERNS; p++) {
                if (patterns[p].active) {
                    float val = evaluate_pattern(&patterns[p], px, py);
                    
                    // Sanftes Ein- und Ausblenden über Lebensdauer
                    float fade = 1.0f;
                    if (patterns[p].age < 1.0f) fade = patterns[p].age;
                    else if (patterns[p].age > patterns[p].life_time - 1.5f) {
                        fade = (patterns[p].life_time - patterns[p].age) / 1.5f;
                    }
                    val *= fade;

                    if (val > max_val) {
                        max_val = val;
                        dominant_color = patterns[p].color_idx;
                    }
                }
            }

            // Zufallseinfluss für organisches Noise-Wachsen
            if (max_val > 0.0f && randf() < 0.75f) {
                grid[idx].target_state = max_val;
                grid[idx].color_idx = dominant_color;
            } else {
                grid[idx].target_state = 0.0f;
            }

            // Fließende Transition zwischen Rauschen und Muster
            if (grid[idx].state < grid[idx].target_state) {
                grid[idx].state += delta_time * 2.5f;
            } else {
                grid[idx].state -= delta_time * 1.2f;
            }
            if (grid[idx].state < 0.0f) grid[idx].state = 0.0f;
            if (grid[idx].state > 1.0f) grid[idx].state = 1.0f;

            // Dynamisches Flimmern des Hintergrund-Rauschens
            if (randf() < 0.1f) {
                grid[idx].bg_brightness = 5 + (rand() % 30);
            }
        }
    }
}

// --- RENDER ENGINE ---
int main(int argc, char* argv[]) {
    srand((unsigned int)time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Random Noise Pattern Generator V4",
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
        if (delta_time > 0.05f) delta_time = 0.05f;
        last_ticks = current_ticks;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.keysym.sym == SDLK_F11) {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    SDL_SetWindowFullscreen(window, (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
            }
        }

        update_simulation(delta_time);

        int win_w, win_h;
        SDL_GetRendererOutputSize(renderer, &win_w, &win_h);

        float cell_w = (float)win_w / GRID_W;
        float cell_h = (float)win_h / GRID_H;
        float max_point_size = win_w * 0.01f; // Maximal ~1 cm Skalierung

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        for (int y = 0; y < GRID_H; y++) {
            for (int x = 0; x < GRID_W; x++) {
                int idx = y * GRID_W + x;
                Cell* c = &grid[idx];

                int px = (int)(x * cell_w);
                int py = (int)(y * cell_h);

                if (c->state <= 0.02f) {
                    // Wuselndes Hintergrund-Noise (Schwarz/Grau mit dunklen Rotpunkten)
                    if (c->is_dark_red) {
                        SDL_SetRenderDrawColor(renderer, 70, 8, 8, 255);
                    } else {
                        SDL_SetRenderDrawColor(renderer, c->bg_brightness, c->bg_brightness, c->bg_brightness, 255);
                    }
                    SDL_RenderDrawPoint(renderer, px, py);
                } else {
                    // Vordergrund-Muster: Helligkeit & Größe sind direkt an `state` gekoppelt
                    ColorDef* col = &colors[c->color_idx];

                    Uint8 r = (Uint8)((c->bg_brightness * (1.0f - c->state)) + (col->r * c->state * 255));
                    Uint8 g = (Uint8)((c->bg_brightness * (1.0f - c->state)) + (col->g * c->state * 255));
                    Uint8 b = (Uint8)((c->bg_brightness * (1.0f - c->state)) + (col->b * c->state * 255));

                    SDL_SetRenderDrawColor(renderer, r, g, b, 255);

                    int size = (int)(c->state * max_point_size);
                    if (size <= 1) {
                        SDL_RenderDrawPoint(renderer, px, py);
                    } else {
                        SDL_Rect rect = { px - size/2, py - size/2, size, size };
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