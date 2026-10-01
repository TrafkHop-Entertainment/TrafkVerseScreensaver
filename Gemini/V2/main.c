/*
 * Random Noise Pattern & Music Generator v2
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

#define MAX_COLORS 35
#define MIN_COLORS 5
#define NUM_PARTICLES 30000
#define SAMPLE_RATE 44100

typedef struct {
    float r, g, b;
    int sf2_preset;
} ColorPreset;

typedef struct {
    float x, y;             // Normierte Position (0.0 bis 1.0)
    float target_x, target_y;
    float size;             // 0.0 bis 1.0
    float target_size;
    int color_index;
    bool is_foreground;
    Uint8 base_grey;        // Individuelle Helligkeit für Hintergrund-Noise
    bool is_dark_red;       // Hintergrund-Sonderpunkt
} Particle;

typedef enum {
    PATTERN_NOISE,
    PATTERN_LISSAJOUS,
    PATTERN_SPIRAL,
    PATTERN_GEOMETRIC,
    PATTERN_JUMPING_MAN,
    PATTERN_FLOW_FIELD,
    PATTERN_COUNT
} PatternType;

// Globale Zustände
static Particle particles[NUM_PARTICLES];
static ColorPreset colors[MAX_COLORS];
static int num_active_colors = 10;

static PatternType current_pattern = PATTERN_LISSAJOUS;
static float pattern_timer = 0.0f;
static float pattern_duration = 8.0f;
static float anim_phase = 0.0f;
static float sound_cooldown = 0.0f;

static tsf* g_sf2 = NULL;
static SDL_AudioDeviceID g_audio_device;

static float randf() {
    return (float)rand() / (float)RAND_MAX;
}

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
    if (!sf2_file) {
        printf("[Audio] Keine SF2-Datei im Verzeichnis gefunden. Sound ist stumm geschaltet.\n");
        return;
    }

    g_sf2 = tsf_load_filename(sf2_file);
    if (!g_sf2) {
        printf("[Audio] Fehler beim Laden der SoundFont-Datei.\n");
        return;
    }

    tsf_set_output(g_sf2, TSF_STEREO_INTERLEAVED, SAMPLE_RATE, 0.0f);

    SDL_AudioSpec requested, obtained;
    SDL_zero(requested);
    requested.freq = SAMPLE_RATE;
    requested.format = AUDIO_S16SYS;
    requested.channels = 2;
    requested.samples = 2048;
    requested.callback = AudioCallback;

    g_audio_device = SDL_OpenAudioDevice(NULL, 0, &requested, &obtained, 0);
    if (g_audio_device != 0) {
        SDL_PauseAudioDevice(g_audio_device, 0);
        printf("[Audio] SF2 geladen: %s\n", sf2_file);
    }
}

static void init_system() {
    num_active_colors = MIN_COLORS + rand() % (MAX_COLORS - MIN_COLORS + 1);
    int preset_count = g_sf2 ? tsf_get_presetcount(g_sf2) : 0;

    for (int i = 0; i < num_active_colors; i++) {
        float hue = (float)i / (float)num_active_colors;
        hsv_to_rgb(hue, 1.0f, 1.0f, &colors[i].r, &colors[i].g, &colors[i].b);
        colors[i].sf2_preset = preset_count > 0 ? (rand() % preset_count) : 0;
    }

    for (int i = 0; i < NUM_PARTICLES; i++) {
        particles[i].x = randf();
        particles[i].y = randf();
        particles[i].target_x = particles[i].x;
        particles[i].target_y = particles[i].y;
        particles[i].size = 0.1f;
        particles[i].target_size = 0.1f;
        particles[i].color_index = rand() % num_active_colors;
        particles[i].is_foreground = false;
        
        // Gut sichtbare Graustufen (60 bis 160)
        particles[i].base_grey = 60 + (rand() % 100);
        particles[i].is_dark_red = (rand() % 120 == 0);
    }
}

static void update_pattern_targets() {
    anim_phase += 0.015f;

    for (int i = 0; i < NUM_PARTICLES; i++) {
        float t = (float)i / NUM_PARTICLES;

        // 50% der Partikel bilden Muster, der Rest bleibt als feines Hintergrund-Noise
        if (i < NUM_PARTICLES * 0.5f && current_pattern != PATTERN_NOISE) {
            particles[i].is_foreground = true;
            particles[i].target_size = 0.3f + randf() * 0.7f;

            switch (current_pattern) {
                case PATTERN_LISSAJOUS: {
                    float a = 3.0f, b = 4.0f;
                    particles[i].target_x = 0.5f + 0.38f * sinf(a * t * 6.28f + anim_phase);
                    particles[i].target_y = 0.5f + 0.38f * cosf(b * t * 6.28f);
                    break;
                }
                case PATTERN_SPIRAL: {
                    float r = (1.0f - t) * 0.42f;
                    float angle = t * 40.0f + anim_phase;
                    particles[i].target_x = 0.5f + r * cosf(angle);
                    particles[i].target_y = 0.5f + r * sinf(angle);
                    break;
                }
                case PATTERN_GEOMETRIC: {
                    int side = i % 6;
                    float angle = (side * 60.0f) * (M_PI / 180.0f) + anim_phase * 0.3f;
                    float radius = 0.32f;
                    particles[i].target_x = 0.5f + radius * cosf(angle) + (randf() - 0.5f) * 0.04f;
                    particles[i].target_y = 0.5f + radius * sinf(angle) + (randf() - 0.5f) * 0.04f;
                    break;
                }
                case PATTERN_JUMPING_MAN: {
                    float jump = fabsf(sinf(anim_phase * 3.0f)) * 0.18f;
                    if (t < 0.1f) { // Kopf
                        float a = t * 10.0f * 6.28f;
                        particles[i].target_x = 0.5f + 0.05f * cosf(a);
                        particles[i].target_y = 0.28f - jump + 0.05f * sinf(a);
                    } else if (t < 0.25f) { // Oberkörper
                        particles[i].target_x = 0.5f + (randf() - 0.5f) * 0.015f;
                        particles[i].target_y = 0.33f - jump + (t - 0.1f) * 0.375f;
                    } else if (t < 0.38f) { // Beine
                        float leg = (t - 0.25f) * 7.7f;
                        particles[i].target_x = 0.5f + ((i % 2 == 0) ? leg : -leg) * 0.12f;
                        particles[i].target_y = 0.58f - jump + leg * 0.2f;
                    } else { // Arme
                        float arm = (t - 0.38f) * 8.0f;
                        particles[i].target_x = 0.5f + ((i % 2 == 0) ? arm : -arm) * 0.16f;
                        particles[i].target_y = 0.40f - jump - arm * (0.08f + jump * 0.5f);
                    }
                    break;
                }
                case PATTERN_FLOW_FIELD: {
                    float n = sinf(particles[i].x * 8.0f + anim_phase) * cosf(particles[i].y * 8.0f + anim_phase);
                    particles[i].target_x = particles[i].x + cosf(n * 6.28f) * 0.008f;
                    particles[i].target_y = particles[i].y + sinf(n * 6.28f) * 0.008f;
                    if (particles[i].target_x < 0) particles[i].target_x += 1.0f;
                    if (particles[i].target_x > 1) particles[i].target_x -= 1.0f;
                    if (particles[i].target_y < 0) particles[i].target_y += 1.0f;
                    if (particles[i].target_y > 1) particles[i].target_y -= 1.0f;
                    break;
                }
                default: break;
            }
        } else {
            // Hintergrund-Noise Bewegung
            particles[i].is_foreground = false;
            particles[i].target_size = 0.05f;
            particles[i].target_x = particles[i].x + (randf() - 0.5f) * 0.003f;
            particles[i].target_y = particles[i].y + (randf() - 0.5f) * 0.003f;
            if (particles[i].target_x < 0) particles[i].target_x = 1.0f;
            if (particles[i].target_x > 1) particles[i].target_x = 0.0f;
            if (particles[i].target_y < 0) particles[i].target_y = 1.0f;
            if (particles[i].target_y > 1) particles[i].target_y = 0.0f;
        }

        particles[i].x += (particles[i].target_x - particles[i].x) * 0.04f;
        particles[i].y += (particles[i].target_y - particles[i].y) * 0.04f;
        particles[i].size += (particles[i].target_size - particles[i].size) * 0.04f;
    }
}

// Subtiler, rhythmischer Ambient-Sound
static void update_generative_music(float delta_time) {
    if (!g_sf2) return;

    sound_cooldown -= delta_time;
    if (sound_cooldown <= 0.0f) {
        // Intervall: Alle 0.4 bis 1.1 Sekunden ein neuer Ton
        sound_cooldown = 0.4f + randf() * 0.7f;

        tsf_channel_note_off_all(g_sf2, 0);

        int col_idx = rand() % num_active_colors;
        int preset = colors[col_idx].sf2_preset;

        // Harmonisierte Pentatonik-Noten
        int pentatonic[] = {48, 50, 52, 55, 57, 60, 62, 64, 67, 69, 72, 74, 76, 79, 81};
        int note_index = rand() % (sizeof(pentatonic) / sizeof(int));
        int key = pentatonic[note_index];

        tsf_channel_set_presetindex(g_sf2, 0, preset);
        tsf_channel_note_on(g_sf2, 0, key, 0.5f);
    }
}

int main(int argc, char* argv[]) {
    srand((unsigned int)time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        printf("SDL Fehler: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Random Noise Generator v2",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 768,
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
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.keysym.sym == SDLK_F11) {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    SDL_SetWindowFullscreen(window, (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
            }
        }

        pattern_timer += delta_time;
        if (pattern_timer >= pattern_duration) {
            pattern_timer = 0.0f;
            pattern_duration = 6.0f + randf() * 12.0f;
            current_pattern = (PatternType)(rand() % PATTERN_COUNT);
        }

        update_pattern_targets();
        update_generative_music(delta_time);

        int win_w, win_h;
        SDL_GetRendererOutputSize(renderer, &win_w, &win_h);
        float max_point_px = win_w * 0.01f;
        if (max_point_px < 6.0f) max_point_px = 6.0f;

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        for (int i = 0; i < NUM_PARTICLES; i++) {
            Particle *p = &particles[i];
            int px = (int)(p->x * win_w);
            int py = (int)(p->y * win_h);

            if (p->is_foreground) {
                ColorPreset *c = &colors[p->color_index];
                float b = p->size;
                if (b < 0.25f) b = 0.25f;

                SDL_SetRenderDrawColor(renderer,
                    (Uint8)(c->r * b * 255),
                    (Uint8)(c->g * b * 255),
                    (Uint8)(c->b * b * 255), 255);

                int draw_size = (int)(p->size * max_point_px);
                if (draw_size <= 1) {
                    SDL_RenderDrawPoint(renderer, px, py);
                } else {
                    SDL_Rect rect = { px - draw_size / 2, py - draw_size / 2, draw_size, draw_size };
                    SDL_RenderFillRect(renderer, &rect);
                }
            } else {
                // Gut sichtbares Hintergrund-Noise
                if (p->is_dark_red) {
                    SDL_SetRenderDrawColor(renderer, 120, 15, 15, 255);
                } else if (i % 15 == 0) {
                    ColorPreset *c = &colors[p->color_index];
                    SDL_SetRenderDrawColor(renderer, (Uint8)(c->r * 130), (Uint8)(c->g * 130), (Uint8)(c->b * 130), 255);
                } else {
                    SDL_SetRenderDrawColor(renderer, p->base_grey, p->base_grey, p->base_grey, 255);
                }
                SDL_RenderDrawPoint(renderer, px, py);
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