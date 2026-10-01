/*
 * RandomNoise  -  aus schwarzem/grauem Rauschen wachsen bunte Muster, Figuren und Musik.
 *
 *  - Rendering/Fenster/Audio:  SDL2 (Wayland bevorzugt, X11 Fallback; Audio PipeWire > Pulse > ALSA)
 *  - Instrumente:              TinySoundFont (MIT, unten eingebettet) + beliebige *.sf2 neben dem Programm
 *  - Bedienung:                ESC = beenden, F / F11 = Vollbild, Fenster frei skalierbar, VSync = Monitor-Hz
 *  - Optionen:                 -c N  Zellgroesse in Pixeln (Standard: automatisch)
 *                              -n N  Anzahl Farben 5..35 (Standard: zufaellig)
 *                              -s N  Seed (Standard: zufaellig)
 *                              -f    Vollbild beim Start
 *
 * Build:  gcc -O2 -o randomnoise noise.c $(sdl2-config --cflags --libs) -lm
 */
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#include <time.h>
#include <limits.h>
#include <libgen.h>

#define TSF_IMPLEMENTATION
/* TinySoundFont - v0.9 - SoundFont2 synthesizer - https://github.com/schellingb/TinySoundFont
                                     no warranty implied; use at your own risk
   Do this:
      #define TSF_IMPLEMENTATION
   before you include this file in *one* C or C++ file to create the implementation.
   // i.e. it should look like this:
   #include ...
   #include ...
   #define TSF_IMPLEMENTATION
   #include "tsf.h"

   [OPTIONAL] #define TSF_NO_STDIO to remove stdio dependency
   [OPTIONAL] #define TSF_MALLOC, TSF_REALLOC, and TSF_FREE to avoid stdlib.h
   [OPTIONAL] #define TSF_MEMCPY, TSF_MEMSET to avoid string.h
   [OPTIONAL] #define TSF_POW, TSF_POWF, TSF_EXPF, TSF_LOG, TSF_TAN, TSF_LOG10, TSF_SQRT to avoid math.h

   NOT YET IMPLEMENTED
     - Support for ChorusEffectsSend and ReverbEffectsSend generators
     - Better low-pass filter without lowering performance too much
     - Support for modulators

   LICENSE (MIT)

   Copyright (C) 2017-2025 Bernhard Schelling
   Based on SFZero, Copyright (C) 2012 Steve Folta (https://github.com/stevefolta/SFZero)

   Permission is hereby granted, free of charge, to any person obtaining a copy of this
   software and associated documentation files (the "Software"), to deal in the Software
   without restriction, including without limitation the rights to use, copy, modify, merge,
   publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
   to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
   PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
   LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#ifndef TSF_INCLUDE_TSF_INL
#define TSF_INCLUDE_TSF_INL

#ifdef __cplusplus
extern "C" {
#  define CPP_DEFAULT0 = 0
#else
#  define CPP_DEFAULT0
#endif

//define this if you want the API functions to be static
#ifdef TSF_STATIC
#define TSFDEF static
#else
#define TSFDEF extern
#endif

// The load functions will return a pointer to a struct tsf which all functions
// thereafter take as the first parameter.
// On error the tsf_load* functions will return NULL most likely due to invalid
// data (or if the file did not exist in tsf_load_filename).
typedef struct tsf tsf;

#ifndef TSF_NO_STDIO
// Directly load a SoundFont from a .sf2 file path
TSFDEF tsf* tsf_load_filename(const char* filename);
#endif

// Load a SoundFont from a block of memory
TSFDEF tsf* tsf_load_memory(const void* buffer, int size);

// Stream structure for the generic loading
struct tsf_stream
{
	// Custom data given to the functions as the first parameter
	void* data;

	// Function pointer will be called to read 'size' bytes into ptr (returns number of read bytes)
	int (*read)(void* data, void* ptr, unsigned int size);

	// Function pointer will be called to skip ahead over 'count' bytes (returns 1 on success, 0 on error)
	int (*skip)(void* data, unsigned int count);
};

// Generic SoundFont loading method using the stream structure above
TSFDEF tsf* tsf_load(struct tsf_stream* stream);

// Copy a tsf instance from an existing one, use tsf_close to close it as well.
// All copied tsf instances and their original instance are linked, and share the underlying soundfont.
// This allows loading a soundfont only once, but using it for multiple independent playbacks.
// (This function isn't thread-safe without locking.)
TSFDEF tsf* tsf_copy(tsf* f);

// Free the memory related to this tsf instance
TSFDEF void tsf_close(tsf* f);

// Stop all playing notes immediately and reset all channel parameters
TSFDEF void tsf_reset(tsf* f);

// Returns the preset index from a bank and preset number, or -1 if it does not exist in the loaded SoundFont
TSFDEF int tsf_get_presetindex(const tsf* f, int bank, int preset_number);

// Returns the number of presets in the loaded SoundFont
TSFDEF int tsf_get_presetcount(const tsf* f);

// Returns the name of a preset index >= 0 and < tsf_get_presetcount()
TSFDEF const char* tsf_get_presetname(const tsf* f, int preset_index);

// Returns the name of a preset by bank and preset number
TSFDEF const char* tsf_bank_get_presetname(const tsf* f, int bank, int preset_number);

// Supported output modes by the render methods
enum TSFOutputMode
{
	// Two channels with single left/right samples one after another
	TSF_STEREO_INTERLEAVED,
	// Two channels with all samples for the left channel first then right
	TSF_STEREO_UNWEAVED,
	// A single channel (stereo instruments are mixed into center)
	TSF_MONO
};

// Thread safety:
//
// 1. Rendering / voices:
//
// Your audio output which calls the tsf_render* functions will most likely
// run on a different thread than where the playback tsf_note* functions
// are called. In which case some sort of concurrency control like a
// mutex needs to be used so they are not called at the same time.
// Alternatively, you can pre-allocate a maximum number of voices that can
// play simultaneously by calling tsf_set_max_voices after loading.
// That way memory re-allocation will not happen during tsf_note_on and
// TSF should become mostly thread safe.
// There is a theoretical chance that ending notes would negatively influence
// a voice that is rendering at the time but it is hard to say.
// Also be aware, this has not been tested much.
//
// 2. Channels:
//
// Calls to tsf_channel_set_... functions may allocate new channels
// if no channel with that number was previously used. Make sure to
// create all channels at the beginning as required if you call tsf_render*
// from a different thread.

// Setup the parameters for the voice render methods
//   outputmode: if mono or stereo and how stereo channel data is ordered
//   samplerate: the number of samples per second (output frequency)
//   global_gain_db: volume gain in decibels (>0 means higher, <0 means lower)
TSFDEF void tsf_set_output(tsf* f, enum TSFOutputMode outputmode, int samplerate, float global_gain_db CPP_DEFAULT0);

// Set the global gain as a volume factor
//   global_gain: the desired volume where 1.0 is 100%
TSFDEF void tsf_set_volume(tsf* f, float global_gain);

// Set the maximum number of voices to play simultaneously
// Depending on the soundfond, one note can cause many new voices to be started,
// so don't keep this number too low or otherwise sounds may not play.
//   max_voices: maximum number to pre-allocate and set the limit to
//   (tsf_set_max_voices returns 0 if allocation failed, otherwise 1)
TSFDEF int tsf_set_max_voices(tsf* f, int max_voices);

// Start playing a note
//   preset_index: preset index >= 0 and < tsf_get_presetcount()
//   key: note value between 0 and 127 (60 being middle C)
//   vel: velocity as a float between 0.0 (equal to note off) and 1.0 (full)
//   bank: instrument bank number (alternative to preset_index)
//   preset_number: preset number (alternative to preset_index)
//   (tsf_note_on returns 0 if the allocation of a new voice failed, otherwise 1)
//   (tsf_bank_note_on returns 0 if preset does not exist or allocation failed, otherwise 1)
TSFDEF int tsf_note_on(tsf* f, int preset_index, int key, float vel);
TSFDEF int tsf_bank_note_on(tsf* f, int bank, int preset_number, int key, float vel);

// Stop playing a note
//   (bank_note_off returns 0 if preset does not exist, otherwise 1)
TSFDEF void tsf_note_off(tsf* f, int preset_index, int key);
TSFDEF int  tsf_bank_note_off(tsf* f, int bank, int preset_number, int key);

// Stop playing all notes (end with sustain and release)
TSFDEF void tsf_note_off_all(tsf* f);

// Returns the number of active voices
TSFDEF int tsf_active_voice_count(tsf* f);

// Render output samples into a buffer
// You can either render as signed 16-bit values (tsf_render_short) or
// as 32-bit float values (tsf_render_float)
//   buffer: target buffer of size samples * output_channels * sizeof(type)
//   samples: number of samples to render
//   flag_mixing: if 0 clear the buffer first, otherwise mix into existing data
TSFDEF void tsf_render_short(tsf* f, short* buffer, int samples, int flag_mixing CPP_DEFAULT0);
TSFDEF void tsf_render_float(tsf* f, float* buffer, int samples, int flag_mixing CPP_DEFAULT0);

// Higher level channel based functions, set up channel parameters
//   channel: channel number
//   preset_index: preset index >= 0 and < tsf_get_presetcount()
//   preset_number: preset number (alternative to preset_index)
//   flag_mididrums: 0 for normal channels, otherwise apply MIDI drum channel rules
//   bank: instrument bank number (alternative to preset_index)
//   pan: stereo panning value from 0.0 (left) to 1.0 (right) (default 0.5 center)
//   volume: linear volume scale factor (default 1.0 full)
//   pitch_wheel: pitch wheel position 0 to 16383 (default 8192 unpitched)
//   pitch_range: range of the pitch wheel in semitones (default 2.0, total +/- 2 semitones)
//   tuning: tuning of all playing voices in semitones (default 0.0, standard (A440) tuning)
//   flag_sustain: 0 to end notes that were held sustained and disable holding sustain otherwise enable it
//   (tsf_set_preset_number and set_bank_preset return 0 if preset does not exist, otherwise 1)
//   (tsf_channel_set_... return 0 if a new channel needed allocation and that failed, otherwise 1)
TSFDEF int tsf_channel_set_presetindex(tsf* f, int channel, int preset_index);
TSFDEF int tsf_channel_set_presetnumber(tsf* f, int channel, int preset_number, int flag_mididrums CPP_DEFAULT0);
TSFDEF int tsf_channel_set_bank(tsf* f, int channel, int bank);
TSFDEF int tsf_channel_set_bank_preset(tsf* f, int channel, int bank, int preset_number);
TSFDEF int tsf_channel_set_pan(tsf* f, int channel, float pan);
TSFDEF int tsf_channel_set_volume(tsf* f, int channel, float volume);
TSFDEF int tsf_channel_set_pitchwheel(tsf* f, int channel, int pitch_wheel);
TSFDEF int tsf_channel_set_pitchrange(tsf* f, int channel, float pitch_range);
TSFDEF int tsf_channel_set_tuning(tsf* f, int channel, float tuning);
TSFDEF int tsf_channel_set_sustain(tsf* f, int channel, int flag_sustain);

// Start or stop playing notes on a channel (needs channel preset to be set)
//   channel: channel number
//   key: note value between 0 and 127 (60 being middle C)
//   vel: velocity as a float between 0.0 (equal to note off) and 1.0 (full)
//   (tsf_channel_note_on returns 0 on allocation failure of new voice, otherwise 1)
TSFDEF int tsf_channel_note_on(tsf* f, int channel, int key, float vel);
TSFDEF void tsf_channel_note_off(tsf* f, int channel, int key);
TSFDEF void tsf_channel_note_off_all(tsf* f, int channel); //end with sustain and release
TSFDEF void tsf_channel_sounds_off_all(tsf* f, int channel); //end immediately

// Apply a MIDI control change to the channel (not all controllers are supported!)
//    (tsf_channel_midi_control returns 0 on allocation failure of new channel, otherwise 1)
TSFDEF int tsf_channel_midi_control(tsf* f, int channel, int controller, int control_value);

// Get current values set on the channels
TSFDEF int tsf_channel_get_preset_index(tsf* f, int channel);
TSFDEF int tsf_channel_get_preset_bank(tsf* f, int channel);
TSFDEF int tsf_channel_get_preset_number(tsf* f, int channel);
TSFDEF float tsf_channel_get_pan(tsf* f, int channel);
TSFDEF float tsf_channel_get_volume(tsf* f, int channel);
TSFDEF int tsf_channel_get_pitchwheel(tsf* f, int channel);
TSFDEF float tsf_channel_get_pitchrange(tsf* f, int channel);
TSFDEF float tsf_channel_get_tuning(tsf* f, int channel);

#ifdef __cplusplus
#  undef CPP_DEFAULT0
}
#endif

// end header
// ---------------------------------------------------------------------------------------------------------
#endif //TSF_INCLUDE_TSF_INL

#ifdef TSF_IMPLEMENTATION
#undef TSF_IMPLEMENTATION

// The lower this block size is the more accurate the effects are.
// Increasing the value significantly lowers the CPU usage of the voice rendering.
// If LFO affects the low-pass filter it can be hearable even as low as 8.
#ifndef TSF_RENDER_EFFECTSAMPLEBLOCK
#define TSF_RENDER_EFFECTSAMPLEBLOCK 64
#endif

// When using tsf_render_short, to do the conversion a buffer of a fixed size is
// allocated on the stack. On low memory platforms this could be made smaller.
// Increasing this above 512 should not have a significant impact on performance.
// The value should be a multiple of TSF_RENDER_EFFECTSAMPLEBLOCK.
#ifndef TSF_RENDER_SHORTBUFFERBLOCK
#define TSF_RENDER_SHORTBUFFERBLOCK 512
#endif

// Grace release time for quick voice off (avoid clicking noise)
#define TSF_FASTRELEASETIME 0.01f

#if !defined(TSF_MALLOC) || !defined(TSF_FREE) || !defined(TSF_REALLOC)
#  include <stdlib.h>
#  define TSF_MALLOC  malloc
#  define TSF_FREE    free
#  define TSF_REALLOC realloc
#endif

#if !defined(TSF_MEMCPY) || !defined(TSF_MEMSET)
#  include <string.h>
#  define TSF_MEMCPY  memcpy
#  define TSF_MEMSET  memset
#endif

#if !defined(TSF_POW) || !defined(TSF_POWF) || !defined(TSF_EXPF) || !defined(TSF_LOG) || !defined(TSF_TAN) || !defined(TSF_LOG10) || !defined(TSF_SQRT)
#  include <math.h>
#  if !defined(__cplusplus) && !defined(NAN) && !defined(powf) && !defined(expf) && !defined(sqrtf)
#    define powf (float)pow // deal with old math.h
#    define expf (float)exp // files that come without
#    define sqrtf (float)sqrt // powf, expf and sqrtf
#  endif
#  define TSF_POW     pow
#  define TSF_POWF    powf
#  define TSF_EXPF    expf
#  define TSF_LOG     log
#  define TSF_TAN     tan
#  define TSF_LOG10   log10
#  define TSF_SQRTF   sqrtf
#endif

#ifndef TSF_NO_STDIO
#  include <stdio.h>
#endif

#define TSF_TRUE 1
#define TSF_FALSE 0
#define TSF_BOOL unsigned char
#define TSF_PI 3.14159265358979323846264338327950288
#define TSF_NULL 0

#ifdef __cplusplus
extern "C" {
#endif

typedef char tsf_fourcc[4];
typedef signed char tsf_s8;
typedef unsigned char tsf_u8;
typedef unsigned short tsf_u16;
typedef signed short tsf_s16;
typedef unsigned int tsf_u32;
typedef char tsf_char20[20];

#define TSF_FourCCEquals(value1, value2) (value1[0] == value2[0] && value1[1] == value2[1] && value1[2] == value2[2] && value1[3] == value2[3])

struct tsf
{
	struct tsf_preset* presets;
	float* fontSamples;
	struct tsf_voice* voices;
	struct tsf_channels* channels;

	int presetNum;
	int voiceNum;
	int maxVoiceNum;
	unsigned int voicePlayIndex;

	enum TSFOutputMode outputmode;
	float outSampleRate;
	float globalGainDB;
	int* refCount;
};

#ifndef TSF_NO_STDIO
static int tsf_stream_stdio_read(FILE* f, void* ptr, unsigned int size) { return (int)fread(ptr, 1, size, f); }
static int tsf_stream_stdio_skip(FILE* f, unsigned int count) { return !fseek(f, count, SEEK_CUR); }
TSFDEF tsf* tsf_load_filename(const char* filename)
{
	tsf* res;
	struct tsf_stream stream = { TSF_NULL, (int(*)(void*,void*,unsigned int))&tsf_stream_stdio_read, (int(*)(void*,unsigned int))&tsf_stream_stdio_skip };
	#if __STDC_WANT_SECURE_LIB__
	FILE* f = TSF_NULL; fopen_s(&f, filename, "rb");
	#else
	FILE* f = fopen(filename, "rb");
	#endif
	if (!f)
	{
		//if (e) *e = TSF_FILENOTFOUND;
		return TSF_NULL;
	}
	stream.data = f;
	res = tsf_load(&stream);
	fclose(f);
	return res;
}
#endif

struct tsf_stream_memory { const char* buffer; unsigned int total, pos; };
static int tsf_stream_memory_read(struct tsf_stream_memory* m, void* ptr, unsigned int size) { if (size > m->total - m->pos) size = m->total - m->pos; TSF_MEMCPY(ptr, m->buffer+m->pos, size); m->pos += size; return size; }
static int tsf_stream_memory_skip(struct tsf_stream_memory* m, unsigned int count) { if (m->pos + count > m->total) return 0; m->pos += count; return 1; }
TSFDEF tsf* tsf_load_memory(const void* buffer, int size)
{
	struct tsf_stream stream = { TSF_NULL, (int(*)(void*,void*,unsigned int))&tsf_stream_memory_read, (int(*)(void*,unsigned int))&tsf_stream_memory_skip };
	struct tsf_stream_memory f = { 0, 0, 0 };
	f.buffer = (const char*)buffer;
	f.total = size;
	stream.data = &f;
	return tsf_load(&stream);
}

enum { TSF_LOOPMODE_NONE, TSF_LOOPMODE_CONTINUOUS, TSF_LOOPMODE_SUSTAIN };

enum { TSF_SEGMENT_NONE, TSF_SEGMENT_DELAY, TSF_SEGMENT_ATTACK, TSF_SEGMENT_HOLD, TSF_SEGMENT_DECAY, TSF_SEGMENT_SUSTAIN, TSF_SEGMENT_RELEASE, TSF_SEGMENT_DONE };

struct tsf_hydra
{
	struct tsf_hydra_phdr *phdrs; struct tsf_hydra_pbag *pbags; struct tsf_hydra_pmod *pmods;
	struct tsf_hydra_pgen *pgens; struct tsf_hydra_inst *insts; struct tsf_hydra_ibag *ibags;
	struct tsf_hydra_imod *imods; struct tsf_hydra_igen *igens; struct tsf_hydra_shdr *shdrs;
	int phdrNum, pbagNum, pmodNum, pgenNum, instNum, ibagNum, imodNum, igenNum, shdrNum;
};

union tsf_hydra_genamount { struct { tsf_u8 lo, hi; } range; tsf_s16 shortAmount; tsf_u16 wordAmount; };
struct tsf_hydra_phdr { tsf_char20 presetName; tsf_u16 preset, bank, presetBagNdx; tsf_u32 library, genre, morphology; };
struct tsf_hydra_pbag { tsf_u16 genNdx, modNdx; };
struct tsf_hydra_pmod { tsf_u16 modSrcOper, modDestOper; tsf_s16 modAmount; tsf_u16 modAmtSrcOper, modTransOper; };
struct tsf_hydra_pgen { tsf_u16 genOper; union tsf_hydra_genamount genAmount; };
struct tsf_hydra_inst { tsf_char20 instName; tsf_u16 instBagNdx; };
struct tsf_hydra_ibag { tsf_u16 instGenNdx, instModNdx; };
struct tsf_hydra_imod { tsf_u16 modSrcOper, modDestOper; tsf_s16 modAmount; tsf_u16 modAmtSrcOper, modTransOper; };
struct tsf_hydra_igen { tsf_u16 genOper; union tsf_hydra_genamount genAmount; };
struct tsf_hydra_shdr { tsf_char20 sampleName; tsf_u32 start, end, startLoop, endLoop, sampleRate; tsf_u8 originalPitch; tsf_s8 pitchCorrection; tsf_u16 sampleLink, sampleType; };

#define TSFR(FIELD) stream->read(stream->data, &i->FIELD, sizeof(i->FIELD));
static void tsf_hydra_read_phdr(struct tsf_hydra_phdr* i, struct tsf_stream* stream) { TSFR(presetName) TSFR(preset) TSFR(bank) TSFR(presetBagNdx) TSFR(library) TSFR(genre) TSFR(morphology) }
static void tsf_hydra_read_pbag(struct tsf_hydra_pbag* i, struct tsf_stream* stream) { TSFR(genNdx) TSFR(modNdx) }
static void tsf_hydra_read_pmod(struct tsf_hydra_pmod* i, struct tsf_stream* stream) { TSFR(modSrcOper) TSFR(modDestOper) TSFR(modAmount) TSFR(modAmtSrcOper) TSFR(modTransOper) }
static void tsf_hydra_read_pgen(struct tsf_hydra_pgen* i, struct tsf_stream* stream) { TSFR(genOper) TSFR(genAmount) }
static void tsf_hydra_read_inst(struct tsf_hydra_inst* i, struct tsf_stream* stream) { TSFR(instName) TSFR(instBagNdx) }
static void tsf_hydra_read_ibag(struct tsf_hydra_ibag* i, struct tsf_stream* stream) { TSFR(instGenNdx) TSFR(instModNdx) }
static void tsf_hydra_read_imod(struct tsf_hydra_imod* i, struct tsf_stream* stream) { TSFR(modSrcOper) TSFR(modDestOper) TSFR(modAmount) TSFR(modAmtSrcOper) TSFR(modTransOper) }
static void tsf_hydra_read_igen(struct tsf_hydra_igen* i, struct tsf_stream* stream) { TSFR(genOper) TSFR(genAmount) }
static void tsf_hydra_read_shdr(struct tsf_hydra_shdr* i, struct tsf_stream* stream) { TSFR(sampleName) TSFR(start) TSFR(end) TSFR(startLoop) TSFR(endLoop) TSFR(sampleRate) TSFR(originalPitch) TSFR(pitchCorrection) TSFR(sampleLink) TSFR(sampleType) }
#undef TSFR

struct tsf_riffchunk { tsf_fourcc id; tsf_u32 size; };
struct tsf_envelope { float delay, attack, hold, decay, sustain, release, keynumToHold, keynumToDecay; };
struct tsf_voice_envelope { unsigned char segment, segmentIsExponential : 1, isAmpEnv : 1; short midiVelocity; float level, slope; int samplesUntilNextSegment; struct tsf_envelope parameters; };
struct tsf_voice_lowpass { double QInv, a0, a1, b1, b2, z1, z2; TSF_BOOL active; };
struct tsf_voice_lfo { int samplesUntil; float level, delta; };

struct tsf_region
{
	int loop_mode;
	unsigned int sample_rate;
	unsigned char lokey, hikey, lovel, hivel;
	unsigned int group, offset, end, loop_start, loop_end;
	int transpose, tune, pitch_keycenter, pitch_keytrack;
	float attenuation, pan;
	struct tsf_envelope ampenv, modenv;
	int initialFilterQ, initialFilterFc;
	int modEnvToPitch, modEnvToFilterFc, modLfoToFilterFc, modLfoToVolume;
	float delayModLFO;
	int freqModLFO, modLfoToPitch;
	float delayVibLFO;
	int freqVibLFO, vibLfoToPitch;
};

struct tsf_preset
{
	tsf_char20 presetName;
	tsf_u16 preset, bank;
	struct tsf_region* regions;
	int regionNum;
};

struct tsf_voice
{
	int playingPreset, playingKey, playingChannel, heldSustain;
	struct tsf_region* region;
	double pitchInputTimecents, pitchOutputFactor;
	double sourceSamplePosition;
	float  noteGainDB, panFactorLeft, panFactorRight;
	unsigned int playIndex, loopStart, loopEnd;
	struct tsf_voice_envelope ampenv, modenv;
	struct tsf_voice_lowpass lowpass;
	struct tsf_voice_lfo modlfo, viblfo;
};

struct tsf_channel
{
	unsigned short presetIndex, bank, pitchWheel, midiPan, midiVolume, midiExpression, midiRPN, midiData : 14, sustain : 1;
	float panOffset, gainDB, pitchRange, tuning;
};

struct tsf_channels
{
	void (*setupVoice)(tsf* f, struct tsf_voice* voice);
	int channelNum, activeChannel;
	struct tsf_channel channels[1];
};

static double tsf_timecents2Secsd(double timecents) { return TSF_POW(2.0, timecents / 1200.0); }
static float tsf_timecents2Secsf(float timecents) { return TSF_POWF(2.0f, timecents / 1200.0f); }
static float tsf_cents2Hertz(float cents) { return 8.176f * TSF_POWF(2.0f, cents / 1200.0f); }
static float tsf_decibelsToGain(float db) { return (db > -100.f ? TSF_POWF(10.0f, db * 0.05f) : 0); }
static float tsf_gainToDecibels(float gain) { return (gain <= .00001f ? -100.f : (float)(20.0 * TSF_LOG10(gain))); }

static TSF_BOOL tsf_riffchunk_read(struct tsf_riffchunk* parent, struct tsf_riffchunk* chunk, struct tsf_stream* stream)
{
	TSF_BOOL IsRiff, IsList;
	if (parent && sizeof(tsf_fourcc) + sizeof(tsf_u32) > parent->size) return TSF_FALSE;
	if (!stream->read(stream->data, &chunk->id, sizeof(tsf_fourcc)) || *chunk->id <= ' ' || *chunk->id >= 'z') return TSF_FALSE;
	if (!stream->read(stream->data, &chunk->size, sizeof(tsf_u32))) return TSF_FALSE;
	if (parent && sizeof(tsf_fourcc) + sizeof(tsf_u32) + chunk->size > parent->size) return TSF_FALSE;
	if (parent) parent->size -= sizeof(tsf_fourcc) + sizeof(tsf_u32) + chunk->size;
	IsRiff = TSF_FourCCEquals(chunk->id, "RIFF"), IsList = TSF_FourCCEquals(chunk->id, "LIST");
	if (IsRiff && parent) return TSF_FALSE; //not allowed
	if (!IsRiff && !IsList) return TSF_TRUE; //custom type without sub type
	if (!stream->read(stream->data, &chunk->id, sizeof(tsf_fourcc)) || *chunk->id <= ' ' || *chunk->id >= 'z') return TSF_FALSE;
	chunk->size -= sizeof(tsf_fourcc);
	return TSF_TRUE;
}

static void tsf_region_clear(struct tsf_region* i, TSF_BOOL for_relative)
{
	TSF_MEMSET(i, 0, sizeof(struct tsf_region));
	i->hikey = i->hivel = 127;
	i->pitch_keycenter = 60; // C4
	if (for_relative) return;

	i->pitch_keytrack = 100;

	i->pitch_keycenter = -1;

	// SF2 defaults in timecents.
	i->ampenv.delay = i->ampenv.attack = i->ampenv.hold = i->ampenv.decay = i->ampenv.release = -12000.0f;
	i->modenv.delay = i->modenv.attack = i->modenv.hold = i->modenv.decay = i->modenv.release = -12000.0f;

	i->initialFilterFc = 13500;

	i->delayModLFO = -12000.0f;
	i->delayVibLFO = -12000.0f;
}

static void tsf_region_operator(struct tsf_region* region, tsf_u16 genOper, union tsf_hydra_genamount* amount, struct tsf_region* merge_region)
{
	enum
	{
		_GEN_TYPE_MASK       = 0x0F,
		GEN_FLOAT            = 0x01,
		GEN_INT              = 0x02,
		GEN_UINT_ADD         = 0x03,
		GEN_UINT_ADD15       = 0x04,
		GEN_KEYRANGE         = 0x05,
		GEN_VELRANGE         = 0x06,
		GEN_LOOPMODE         = 0x07,
		GEN_GROUP            = 0x08,
		GEN_KEYCENTER        = 0x09,

		_GEN_LIMIT_MASK      = 0xF0,
		GEN_INT_LIMIT12K     = 0x10, //min -12000, max 12000
		GEN_INT_LIMITFC      = 0x20, //min 1500, max 13500
		GEN_INT_LIMITQ       = 0x30, //min 0, max 960
		GEN_INT_LIMIT960     = 0x40, //min -960, max 960
		GEN_INT_LIMIT16K4500 = 0x50, //min -16000, max 4500
		GEN_FLOAT_LIMIT12K5K = 0x60, //min -12000, max 5000
		GEN_FLOAT_LIMIT12K8K = 0x70, //min -12000, max 8000
		GEN_FLOAT_LIMIT1200  = 0x80, //min -1200, max 1200
		GEN_FLOAT_LIMITPAN   = 0x90, //* .001f, min -.5f, max .5f,
		GEN_FLOAT_LIMITATTN  = 0xA0, //* .1f, min 0, max 144.0
		GEN_FLOAT_MAX1000    = 0xB0, //min 0, max 1000
		GEN_FLOAT_MAX1440    = 0xC0, //min 0, max 1440

		_GEN_MAX = 59
	};
	#define _TSFREGIONOFFSET(TYPE, FIELD) (unsigned char)(((TYPE*)&((struct tsf_region*)0)->FIELD) - (TYPE*)0)
	#define _TSFREGIONENVOFFSET(TYPE, ENV, FIELD) (unsigned char)(((TYPE*)&((&(((struct tsf_region*)0)->ENV))->FIELD)) - (TYPE*)0)
	static const struct { unsigned char mode, offset; } genMetas[_GEN_MAX] =
	{
		{ GEN_UINT_ADD                     , _TSFREGIONOFFSET(unsigned int, offset               ) }, // 0 StartAddrsOffset
		{ GEN_UINT_ADD                     , _TSFREGIONOFFSET(unsigned int, end                  ) }, // 1 EndAddrsOffset
		{ GEN_UINT_ADD                     , _TSFREGIONOFFSET(unsigned int, loop_start           ) }, // 2 StartloopAddrsOffset
		{ GEN_UINT_ADD                     , _TSFREGIONOFFSET(unsigned int, loop_end             ) }, // 3 EndloopAddrsOffset
		{ GEN_UINT_ADD15                   , _TSFREGIONOFFSET(unsigned int, offset               ) }, // 4 StartAddrsCoarseOffset
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, modLfoToPitch        ) }, // 5 ModLfoToPitch
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, vibLfoToPitch        ) }, // 6 VibLfoToPitch
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, modEnvToPitch        ) }, // 7 ModEnvToPitch
		{ GEN_INT   | GEN_INT_LIMITFC      , _TSFREGIONOFFSET(         int, initialFilterFc      ) }, // 8 InitialFilterFc
		{ GEN_INT   | GEN_INT_LIMITQ       , _TSFREGIONOFFSET(         int, initialFilterQ       ) }, // 9 InitialFilterQ
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, modLfoToFilterFc     ) }, //10 ModLfoToFilterFc
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, modEnvToFilterFc     ) }, //11 ModEnvToFilterFc
		{ GEN_UINT_ADD15                   , _TSFREGIONOFFSET(unsigned int, end                  ) }, //12 EndAddrsCoarseOffset
		{ GEN_INT   | GEN_INT_LIMIT960     , _TSFREGIONOFFSET(         int, modLfoToVolume       ) }, //13 ModLfoToVolume
		{ 0                                , (0                                                  ) }, //   Unused
		{ 0                                , (0                                                  ) }, //15 ChorusEffectsSend (unsupported)
		{ 0                                , (0                                                  ) }, //16 ReverbEffectsSend (unsupported)
		{ GEN_FLOAT | GEN_FLOAT_LIMITPAN   , _TSFREGIONOFFSET(       float, pan                  ) }, //17 Pan
		{ 0                                , (0                                                  ) }, //   Unused
		{ 0                                , (0                                                  ) }, //   Unused
		{ 0                                , (0                                                  ) }, //   Unused
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONOFFSET(       float, delayModLFO          ) }, //21 DelayModLFO
		{ GEN_INT   | GEN_INT_LIMIT16K4500 , _TSFREGIONOFFSET(         int, freqModLFO           ) }, //22 FreqModLFO
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONOFFSET(       float, delayVibLFO          ) }, //23 DelayVibLFO
		{ GEN_INT   | GEN_INT_LIMIT16K4500 , _TSFREGIONOFFSET(         int, freqVibLFO           ) }, //24 FreqVibLFO
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, modenv, delay        ) }, //25 DelayModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, modenv, attack       ) }, //26 AttackModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, modenv, hold         ) }, //27 HoldModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, modenv, decay        ) }, //28 DecayModEnv
		{ GEN_FLOAT | GEN_FLOAT_MAX1000    , _TSFREGIONENVOFFSET(    float, modenv, sustain      ) }, //29 SustainModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, modenv, release      ) }, //30 ReleaseModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, modenv, keynumToHold ) }, //31 KeynumToModEnvHold
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, modenv, keynumToDecay) }, //32 KeynumToModEnvDecay
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, ampenv, delay        ) }, //33 DelayVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, ampenv, attack       ) }, //34 AttackVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, ampenv, hold         ) }, //35 HoldVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, ampenv, decay        ) }, //36 DecayVolEnv
		{ GEN_FLOAT | GEN_FLOAT_MAX1440    , _TSFREGIONENVOFFSET(    float, ampenv, sustain      ) }, //37 SustainVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, ampenv, release      ) }, //38 ReleaseVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, ampenv, keynumToHold ) }, //39 KeynumToVolEnvHold
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, ampenv, keynumToDecay) }, //40 KeynumToVolEnvDecay
		{ 0                                , (0                                                  ) }, //   Instrument (special)
		{ 0                                , (0                                                  ) }, //   Reserved
		{ GEN_KEYRANGE                     , (0                                                  ) }, //43 KeyRange
		{ GEN_VELRANGE                     , (0                                                  ) }, //44 VelRange
		{ GEN_UINT_ADD15                   , _TSFREGIONOFFSET(unsigned int, loop_start           ) }, //45 StartloopAddrsCoarseOffset
		{ 0                                , (0                                                  ) }, //46 Keynum (special)
		{ 0                                , (0                                                  ) }, //47 Velocity (special)
		{ GEN_FLOAT | GEN_FLOAT_LIMITATTN  , _TSFREGIONOFFSET(       float, attenuation          ) }, //48 InitialAttenuation
		{ 0                                , (0                                                  ) }, //   Reserved
		{ GEN_UINT_ADD15                   , _TSFREGIONOFFSET(unsigned int, loop_end             ) }, //50 EndloopAddrsCoarseOffset
		{ GEN_INT                          , _TSFREGIONOFFSET(         int, transpose            ) }, //51 CoarseTune
		{ GEN_INT                          , _TSFREGIONOFFSET(         int, tune                 ) }, //52 FineTune
		{ 0                                , (0                                                  ) }, //   SampleID (special)
		{ GEN_LOOPMODE                     , _TSFREGIONOFFSET(         int, loop_mode            ) }, //54 SampleModes
		{ 0                                , (0                                                  ) }, //   Reserved
		{ GEN_INT                          , _TSFREGIONOFFSET(         int, pitch_keytrack       ) }, //56 ScaleTuning
		{ GEN_GROUP                        , _TSFREGIONOFFSET(unsigned int, group                ) }, //57 ExclusiveClass
		{ GEN_KEYCENTER                    , _TSFREGIONOFFSET(         int, pitch_keycenter      ) }, //58 OverridingRootKey
	};
	#undef _TSFREGIONOFFSET
	#undef _TSFREGIONENVOFFSET
	if (amount)
	{
		int offset;
		if (genOper >= _GEN_MAX) return;
		offset = genMetas[genOper].offset;
		switch (genMetas[genOper].mode & _GEN_TYPE_MASK)
		{
			case GEN_FLOAT:      ((       float*)region)[offset]  = amount->shortAmount;     return;
			case GEN_INT:        ((         int*)region)[offset]  = amount->shortAmount;     return;
			case GEN_UINT_ADD:   ((unsigned int*)region)[offset] += amount->shortAmount;     return;
			case GEN_UINT_ADD15: ((unsigned int*)region)[offset] += amount->shortAmount<<15; return;
			case GEN_KEYRANGE:   region->lokey = amount->range.lo; region->hikey = amount->range.hi; return;
			case GEN_VELRANGE:   region->lovel = amount->range.lo; region->hivel = amount->range.hi; return;
			case GEN_LOOPMODE:   region->loop_mode       = ((amount->wordAmount&3) == 3 ? TSF_LOOPMODE_SUSTAIN : ((amount->wordAmount&3) == 1 ? TSF_LOOPMODE_CONTINUOUS : TSF_LOOPMODE_NONE)); return;
			case GEN_GROUP:      region->group           = amount->wordAmount;  return;
			case GEN_KEYCENTER:  region->pitch_keycenter = amount->shortAmount; return;
		}
	}
	else //merge regions and clamp values
	{
		for (genOper = 0; genOper != _GEN_MAX; genOper++)
		{
			int offset = genMetas[genOper].offset;
			switch (genMetas[genOper].mode & _GEN_TYPE_MASK)
			{
				case GEN_FLOAT:
				{
					float *val = &((float*)region)[offset], vfactor, vmin, vmax;
					*val += ((float*)merge_region)[offset];
					switch (genMetas[genOper].mode & _GEN_LIMIT_MASK)
					{
						case GEN_FLOAT_LIMIT12K5K: vfactor =   1.0f; vmin = -12000.0f; vmax = 5000.0f; break;
						case GEN_FLOAT_LIMIT12K8K: vfactor =   1.0f; vmin = -12000.0f; vmax = 8000.0f; break;
						case GEN_FLOAT_LIMIT1200:  vfactor =   1.0f; vmin =  -1200.0f; vmax = 1200.0f; break;
						case GEN_FLOAT_LIMITPAN:   vfactor = 0.001f; vmin =     -0.5f; vmax =    0.5f; break;
						case GEN_FLOAT_LIMITATTN:  vfactor =  0.01f; vmin =      0.0f; vmax =   14.4f; break;
						case GEN_FLOAT_MAX1000:    vfactor =   1.0f; vmin =      0.0f; vmax = 1000.0f; break;
						case GEN_FLOAT_MAX1440:    vfactor =   1.0f; vmin =      0.0f; vmax = 1440.0f; break;
						default: continue;
					}
					*val *= vfactor;
					if      (*val < vmin) *val = vmin;
					else if (*val > vmax) *val = vmax;
					continue;
				}
				case GEN_INT:
				{
					int *val = &((int*)region)[offset], vmin, vmax;
					*val += ((int*)merge_region)[offset];
					switch (genMetas[genOper].mode & _GEN_LIMIT_MASK)
					{
						case GEN_INT_LIMIT12K:     vmin = -12000; vmax = 12000; break;
						case GEN_INT_LIMITFC:      vmin =   1500; vmax = 13500; break;
						case GEN_INT_LIMITQ:       vmin =      0; vmax =   960; break;
						case GEN_INT_LIMIT960:     vmin =   -960; vmax =   960; break;
						case GEN_INT_LIMIT16K4500: vmin = -16000; vmax =  4500; break;
						default: continue;
					}
					if      (*val < vmin) *val = vmin;
					else if (*val > vmax) *val = vmax;
					continue;
				}
				case GEN_UINT_ADD:
				{
					((unsigned int*)region)[offset] += ((unsigned int*)merge_region)[offset];
					continue;
				}
			}
		}
	}
}

static void tsf_region_envtosecs(struct tsf_envelope* p, TSF_BOOL sustainIsGain)
{
	// EG times need to be converted from timecents to seconds.
	// Pin very short EG segments.  Timecents don't get to zero, and our EG is
	// happier with zero values.
	p->delay   = (p->delay   < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->delay));
	p->attack  = (p->attack  < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->attack));
	p->release = (p->release < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->release));

	// If we have dynamic hold or decay times depending on key number we need
	// to keep the values in timecents so we can calculate it during startNote
	if (!p->keynumToHold)  p->hold  = (p->hold  < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->hold));
	if (!p->keynumToDecay) p->decay = (p->decay < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->decay));

	if (p->sustain < 0.0f) p->sustain = 0.0f;
	else if (sustainIsGain) p->sustain = tsf_decibelsToGain(-p->sustain / 10.0f);
	else p->sustain = 1.0f - (p->sustain / 1000.0f);
}

static int tsf_load_presets(tsf* res, struct tsf_hydra *hydra, unsigned int fontSampleCount)
{
	enum { GenInstrument = 41, GenKeyRange = 43, GenVelRange = 44, GenSampleID = 53 };
	// Read each preset.
	struct tsf_hydra_phdr *pphdr, *pphdrMax;
	res->presetNum = hydra->phdrNum - 1;
	res->presets = (struct tsf_preset*)TSF_MALLOC(res->presetNum * sizeof(struct tsf_preset));
	if (!res->presets) return 0;
	else { int i; for (i = 0; i != res->presetNum; i++) res->presets[i].regions = TSF_NULL; }
	for (pphdr = hydra->phdrs, pphdrMax = pphdr + hydra->phdrNum - 1; pphdr != pphdrMax; pphdr++)
	{
		int sortedIndex = 0, region_index = 0;
		struct tsf_hydra_phdr *otherphdr;
		struct tsf_preset* preset;
		struct tsf_hydra_pbag *ppbag, *ppbagEnd;
		struct tsf_region globalRegion;
		for (otherphdr = hydra->phdrs; otherphdr != pphdrMax; otherphdr++)
		{
			if (otherphdr == pphdr || otherphdr->bank > pphdr->bank) continue;
			else if (otherphdr->bank < pphdr->bank) sortedIndex++;
			else if (otherphdr->preset > pphdr->preset) continue;
			else if (otherphdr->preset < pphdr->preset) sortedIndex++;
			else if (otherphdr < pphdr) sortedIndex++;
		}

		preset = &res->presets[sortedIndex];
		TSF_MEMCPY(preset->presetName, pphdr->presetName, sizeof(preset->presetName));
		preset->presetName[sizeof(preset->presetName)-1] = '\0'; //should be zero terminated in source file but make sure
		preset->bank = pphdr->bank;
		preset->preset = pphdr->preset;
		preset->regionNum = 0;

		//count regions covered by this preset
		for (ppbag = hydra->pbags + pphdr->presetBagNdx, ppbagEnd = hydra->pbags + pphdr[1].presetBagNdx; ppbag != ppbagEnd; ppbag++)
		{
			unsigned char plokey = 0, phikey = 127, plovel = 0, phivel = 127;
			struct tsf_hydra_pgen *ppgen, *ppgenEnd; struct tsf_hydra_inst *pinst; struct tsf_hydra_ibag *pibag, *pibagEnd; struct tsf_hydra_igen *pigen, *pigenEnd;
			for (ppgen = hydra->pgens + ppbag->genNdx, ppgenEnd = hydra->pgens + ppbag[1].genNdx; ppgen != ppgenEnd; ppgen++)
			{
				if (ppgen->genOper == GenKeyRange) { plokey = ppgen->genAmount.range.lo; phikey = ppgen->genAmount.range.hi; continue; }
				if (ppgen->genOper == GenVelRange) { plovel = ppgen->genAmount.range.lo; phivel = ppgen->genAmount.range.hi; continue; }
				if (ppgen->genOper != GenInstrument) continue;
				if (ppgen->genAmount.wordAmount >= hydra->instNum) continue;
				pinst = hydra->insts + ppgen->genAmount.wordAmount;
				for (pibag = hydra->ibags + pinst->instBagNdx, pibagEnd = hydra->ibags + pinst[1].instBagNdx; pibag != pibagEnd; pibag++)
				{
					unsigned char ilokey = 0, ihikey = 127, ilovel = 0, ihivel = 127;
					for (pigen = hydra->igens + pibag->instGenNdx, pigenEnd = hydra->igens + pibag[1].instGenNdx; pigen != pigenEnd; pigen++)
					{
						if (pigen->genOper == GenKeyRange) { ilokey = pigen->genAmount.range.lo; ihikey = pigen->genAmount.range.hi; continue; }
						if (pigen->genOper == GenVelRange) { ilovel = pigen->genAmount.range.lo; ihivel = pigen->genAmount.range.hi; continue; }
						if (pigen->genOper == GenSampleID && ihikey >= plokey && ilokey <= phikey && ihivel >= plovel && ilovel <= phivel) preset->regionNum++;
					}
				}
			}
		}

		preset->regions = (struct tsf_region*)TSF_MALLOC(preset->regionNum * sizeof(struct tsf_region));
		if (!preset->regions)
		{
			int i; for (i = 0; i != res->presetNum; i++) TSF_FREE(res->presets[i].regions);
			TSF_FREE(res->presets);
			return 0;
		}
		tsf_region_clear(&globalRegion, TSF_TRUE);

		// Zones.
		for (ppbag = hydra->pbags + pphdr->presetBagNdx, ppbagEnd = hydra->pbags + pphdr[1].presetBagNdx; ppbag != ppbagEnd; ppbag++)
		{
			struct tsf_hydra_pgen *ppgen, *ppgenEnd; struct tsf_hydra_inst *pinst; struct tsf_hydra_ibag *pibag, *pibagEnd; struct tsf_hydra_igen *pigen, *pigenEnd;
			struct tsf_region presetRegion = globalRegion;
			int hadGenInstrument = 0;

			// Generators.
			for (ppgen = hydra->pgens + ppbag->genNdx, ppgenEnd = hydra->pgens + ppbag[1].genNdx; ppgen != ppgenEnd; ppgen++)
			{
				// Instrument.
				if (ppgen->genOper == GenInstrument)
				{
					struct tsf_region instRegion;
					tsf_u16 whichInst = ppgen->genAmount.wordAmount;
					if (whichInst >= hydra->instNum) continue;

					tsf_region_clear(&instRegion, TSF_FALSE);
					pinst = &hydra->insts[whichInst];
					for (pibag = hydra->ibags + pinst->instBagNdx, pibagEnd = hydra->ibags + pinst[1].instBagNdx; pibag != pibagEnd; pibag++)
					{
						// Generators.
						struct tsf_region zoneRegion = instRegion;
						int hadSampleID = 0;
						for (pigen = hydra->igens + pibag->instGenNdx, pigenEnd = hydra->igens + pibag[1].instGenNdx; pigen != pigenEnd; pigen++)
						{
							if (pigen->genOper == GenSampleID)
							{
								struct tsf_hydra_shdr* pshdr;

								//preset region key and vel ranges are a filter for the zone regions
								if (zoneRegion.hikey < presetRegion.lokey || zoneRegion.lokey > presetRegion.hikey) continue;
								if (zoneRegion.hivel < presetRegion.lovel || zoneRegion.lovel > presetRegion.hivel) continue;
								if (presetRegion.lokey > zoneRegion.lokey) zoneRegion.lokey = presetRegion.lokey;
								if (presetRegion.hikey < zoneRegion.hikey) zoneRegion.hikey = presetRegion.hikey;
								if (presetRegion.lovel > zoneRegion.lovel) zoneRegion.lovel = presetRegion.lovel;
								if (presetRegion.hivel < zoneRegion.hivel) zoneRegion.hivel = presetRegion.hivel;

								//sum regions
								tsf_region_operator(&zoneRegion, 0, TSF_NULL, &presetRegion);

								// EG times need to be converted from timecents to seconds.
								tsf_region_envtosecs(&zoneRegion.ampenv, TSF_TRUE);
								tsf_region_envtosecs(&zoneRegion.modenv, TSF_FALSE);

								// LFO times need to be converted from timecents to seconds.
								zoneRegion.delayModLFO = (zoneRegion.delayModLFO < -11950.0f ? 0.0f : tsf_timecents2Secsf(zoneRegion.delayModLFO));
								zoneRegion.delayVibLFO = (zoneRegion.delayVibLFO < -11950.0f ? 0.0f : tsf_timecents2Secsf(zoneRegion.delayVibLFO));

								// Fixup sample positions
								pshdr = &hydra->shdrs[pigen->genAmount.wordAmount];
								zoneRegion.offset += pshdr->start;
								zoneRegion.end += pshdr->end;
								zoneRegion.loop_start += pshdr->startLoop;
								zoneRegion.loop_end += pshdr->endLoop;
								if (pshdr->endLoop > 0) zoneRegion.loop_end -= 1;
								if (zoneRegion.loop_end > fontSampleCount) zoneRegion.loop_end = fontSampleCount;
								if (zoneRegion.pitch_keycenter == -1) zoneRegion.pitch_keycenter = pshdr->originalPitch;
								zoneRegion.tune += pshdr->pitchCorrection;
								zoneRegion.sample_rate = pshdr->sampleRate;
								if (zoneRegion.end && zoneRegion.end < fontSampleCount) zoneRegion.end++;
								else zoneRegion.end = fontSampleCount;

								preset->regions[region_index] = zoneRegion;
								region_index++;
								hadSampleID = 1;
							}
							else tsf_region_operator(&zoneRegion, pigen->genOper, &pigen->genAmount, TSF_NULL);
						}

						// Handle instrument's global zone.
						if (pibag == hydra->ibags + pinst->instBagNdx && !hadSampleID)
							instRegion = zoneRegion;

						// Modulators (TODO)
						//if (ibag->instModNdx < ibag[1].instModNdx) addUnsupportedOpcode("any modulator");
					}
					hadGenInstrument = 1;
				}
				else tsf_region_operator(&presetRegion, ppgen->genOper, &ppgen->genAmount, TSF_NULL);
			}

			// Modulators (TODO)
			//if (pbag->modNdx < pbag[1].modNdx) addUnsupportedOpcode("any modulator");

			// Handle preset's global zone.
			if (ppbag == hydra->pbags + pphdr->presetBagNdx && !hadGenInstrument)
				globalRegion = presetRegion;
		}
	}
	return 1;
}

#ifdef STB_VORBIS_INCLUDE_STB_VORBIS_H
static int tsf_decode_ogg(const tsf_u8 *pSmpl, const tsf_u8 *pSmplEnd, float** pRes, tsf_u32* pResNum, tsf_u32* pResMax, tsf_u32 resInitial)
{
	float *res = *pRes, *oldres; tsf_u32 resNum = *pResNum; tsf_u32 resMax = *pResMax; stb_vorbis *v;

	// Use whatever stb_vorbis API that is available (either pull or push)
	#if !defined(STB_VORBIS_NO_PULLDATA_API) && !defined(STB_VORBIS_NO_FROMMEMORY)
	v = stb_vorbis_open_memory(pSmpl, (int)(pSmplEnd - pSmpl), TSF_NULL, TSF_NULL);
	#else
	{ int use, err; v = stb_vorbis_open_pushdata(pSmpl, (int)(pSmplEnd - pSmpl), &use, &err, TSF_NULL); pSmpl += use; }
	#endif
	if (v == TSF_NULL) return 0;

	for (;;)
	{
		float** outputs; int n_samples;

		// Decode one frame of vorbis samples with whatever stb_vorbis API that is available
		#if !defined(STB_VORBIS_NO_PULLDATA_API) && !defined(STB_VORBIS_NO_FROMMEMORY)
		n_samples = stb_vorbis_get_frame_float(v, TSF_NULL, &outputs);
		if (!n_samples) break;
		#else
		if (pSmpl >= pSmplEnd) break;
		{ int use = stb_vorbis_decode_frame_pushdata(v, pSmpl, (int)(pSmplEnd - pSmpl), TSF_NULL, &outputs, &n_samples); pSmpl += use; }
		if (!n_samples) continue;
		#endif

		// Expand our output buffer if necessary then copy over the decoded frame samples
		resNum += n_samples;
		if (resNum > resMax)
		{
			do { resMax += (resMax ? (resMax < 1048576 ? resMax : 1048576) : resInitial); } while (resNum > resMax);
			oldres = res;
			res = (float*)TSF_REALLOC(res, resMax * sizeof(float));
			if (!res) { TSF_FREE(oldres); stb_vorbis_close(v); return 0; }
		}
		TSF_MEMCPY(res + resNum - n_samples, outputs[0], n_samples * sizeof(float));
	}
	stb_vorbis_close(v);
	*pRes = res; *pResNum = resNum; *pResMax = resMax;
	return 1;
}

static int tsf_decode_sf3_samples(const void* rawBuffer, float** pFloatBuffer, unsigned int* pSmplCount, struct tsf_hydra *hydra)
{
	const tsf_u8* smplBuffer = (const tsf_u8*)rawBuffer;
	tsf_u32 smplLength = *pSmplCount, resNum = 0, resMax = 0, resInitial = (smplLength > 0x100000 ? (smplLength & ~0xFFFFF) : 65536);
	float *res = TSF_NULL, *oldres;
	int i, shdrLast = hydra->shdrNum - 1, is_sf3 = 0;
	for (i = 0; i <= shdrLast; i++)
	{
		struct tsf_hydra_shdr *shdr = &hydra->shdrs[i];
		if (shdr->sampleType & 0x30) // compression flags (sometimes Vorbis flag)
		{
			const tsf_u8 *pSmpl = smplBuffer + shdr->start, *pSmplEnd = smplBuffer + shdr->end;
			if (pSmpl + 4 > pSmplEnd || !TSF_FourCCEquals(pSmpl, "OggS"))
			{
				shdr->start = shdr->end = shdr->startLoop = shdr->endLoop = 0;
				continue;
			}

			// Fix up sample indices in shdr (end index is set after decoding)
			shdr->start = resNum;
			shdr->startLoop += resNum;
			shdr->endLoop += resNum;
			if (!tsf_decode_ogg(pSmpl, pSmplEnd, &res, &resNum, &resMax, resInitial)) { TSF_FREE(res); return 0; }
			shdr->end = resNum;
			is_sf3 = 1;
		}
		else // raw PCM sample
		{
			float *out; short *in = (short*)smplBuffer + resNum, *inEnd; tsf_u32 oldResNum = resNum;
			if (is_sf3) // Fix up sample indices in shdr
			{
				tsf_u32 fix_offset = resNum - shdr->start;
				in -= fix_offset;
				shdr->start = resNum;
				shdr->end += fix_offset;
				shdr->startLoop += fix_offset;
				shdr->endLoop += fix_offset;
			}
			inEnd = in + ((shdr->end >= shdr->endLoop ? shdr->end : shdr->endLoop) - resNum);
			if (i == shdrLast || (tsf_u8*)inEnd > (smplBuffer + smplLength)) inEnd = (short*)(smplBuffer + smplLength);
			if (inEnd <= in) continue;

			// expand our output buffer if necessary then convert the PCM data from short to float
			resNum += (tsf_u32)(inEnd - in);
			if (resNum > resMax)
			{
				do { resMax += (resMax ? (resMax < 1048576 ? resMax : 1048576) : resInitial); } while (resNum > resMax);
				oldres = res;
				res = (float*)TSF_REALLOC(res, resMax * sizeof(float));
				if (!res) { TSF_FREE(oldres); return 0; }
			}

			// Convert the samples from short to float
			for (out = res + oldResNum; in < inEnd;)
				*(out++) = (float)(*(in++) / 32767.0);
		}
	}

	// Trim the sample buffer down then return success (unless out of memory)
	if (!(*pFloatBuffer = (float*)TSF_REALLOC(res, resNum * sizeof(float)))) *pFloatBuffer = res;
	*pSmplCount = resNum;
	return (res ? 1 : 0);
}
#endif

static int tsf_load_samples(void** pRawBuffer, float** pFloatBuffer, unsigned int* pSmplCount, struct tsf_riffchunk *chunkSmpl, struct tsf_stream* stream)
{
	#ifdef STB_VORBIS_INCLUDE_STB_VORBIS_H
	// With OGG Vorbis support we cannot pre-allocate the memory for tsf_decode_sf3_samples
	tsf_u32 resNum, resMax; float* oldres;
	*pSmplCount = chunkSmpl->size;
	*pRawBuffer = (void*)TSF_MALLOC(*pSmplCount);
	if (!*pRawBuffer || !stream->read(stream->data, *pRawBuffer, chunkSmpl->size)) return 0;
	if (chunkSmpl->id[3] != 'o') return 1;

	// Decode custom .sfo 'smpo' format where all samples are in a single ogg stream
	resNum = resMax = 0;
	if (!tsf_decode_ogg((tsf_u8*)*pRawBuffer, (tsf_u8*)*pRawBuffer + chunkSmpl->size, pFloatBuffer, &resNum, &resMax, 65536)) return 0;
	oldres = *pFloatBuffer;
	if (!(*pFloatBuffer = (float*)TSF_REALLOC(*pFloatBuffer, resNum * sizeof(float)))) *pFloatBuffer = oldres;
	*pSmplCount = resNum;
	return (*pFloatBuffer ? 1 : 0);
	#else
	// Inline convert the samples from short to float
	float *res, *out; const short *in;
	(void)pRawBuffer;
	*pSmplCount = chunkSmpl->size / (unsigned int)sizeof(short);
	*pFloatBuffer = (float*)TSF_MALLOC(*pSmplCount * sizeof(float));
	if (!*pFloatBuffer || !stream->read(stream->data, *pFloatBuffer, chunkSmpl->size)) return 0;
	for (res = *pFloatBuffer, out = res + *pSmplCount, in = (short*)res + *pSmplCount; out != res;)
		*(--out) = (float)(*(--in) / 32767.0);
	return 1;
	#endif
}

static int tsf_voice_envelope_release_samples(struct tsf_voice_envelope* e, float outSampleRate)
{
	return (int)((e->parameters.release <= 0 ? TSF_FASTRELEASETIME : e->parameters.release) * outSampleRate);
}

static void tsf_voice_envelope_nextsegment(struct tsf_voice_envelope* e, short active_segment, float outSampleRate)
{
	switch (active_segment)
	{
		case TSF_SEGMENT_NONE:
			e->samplesUntilNextSegment = (int)(e->parameters.delay * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				e->segment = TSF_SEGMENT_DELAY;
				e->segmentIsExponential = TSF_FALSE;
				e->level = 0.0;
				e->slope = 0.0;
				return;
			}
			/* fall through */
		case TSF_SEGMENT_DELAY:
			e->samplesUntilNextSegment = (int)(e->parameters.attack * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				if (!e->isAmpEnv)
				{
					//mod env attack duration scales with velocity (velocity of 1 is full duration, max velocity is 0.125 times duration)
					e->samplesUntilNextSegment = (int)(e->parameters.attack * ((145 - e->midiVelocity) / 144.0f) * outSampleRate);
				}
				e->segment = TSF_SEGMENT_ATTACK;
				e->segmentIsExponential = TSF_FALSE;
				e->level = 0.0f;
				e->slope = 1.0f / e->samplesUntilNextSegment;
				return;
			}
			/* fall through */
		case TSF_SEGMENT_ATTACK:
			e->samplesUntilNextSegment = (int)(e->parameters.hold * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				e->segment = TSF_SEGMENT_HOLD;
				e->segmentIsExponential = TSF_FALSE;
				e->level = 1.0f;
				e->slope = 0.0f;
				return;
			}
			/* fall through */
		case TSF_SEGMENT_HOLD:
			e->samplesUntilNextSegment = (int)(e->parameters.decay * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				e->segment = TSF_SEGMENT_DECAY;
				e->level = 1.0f;
				if (e->isAmpEnv)
				{
					// I don't truly understand this; just following what LinuxSampler does.
					float mysterySlope = -9.226f / e->samplesUntilNextSegment;
					e->slope = TSF_EXPF(mysterySlope);
					e->segmentIsExponential = TSF_TRUE;
					if (e->parameters.sustain > 0.0f)
					{
						// Again, this is following LinuxSampler's example, which is similar to
						// SF2-style decay, where "decay" specifies the time it would take to
						// get to zero, not to the sustain level.  The SFZ spec is not that
						// specific about what "decay" means, so perhaps it's really supposed
						// to specify the time to reach the sustain level.
						e->samplesUntilNextSegment = (int)(TSF_LOG(e->parameters.sustain) / mysterySlope);
					}
				}
				else
				{
					e->slope = -1.0f / e->samplesUntilNextSegment;
					e->samplesUntilNextSegment = (int)(e->parameters.decay * (1.0f - e->parameters.sustain) * outSampleRate);
					e->segmentIsExponential = TSF_FALSE;
				}
				return;
			}
			/* fall through */
		case TSF_SEGMENT_DECAY:
			e->segment = TSF_SEGMENT_SUSTAIN;
			e->level = e->parameters.sustain;
			e->slope = 0.0f;
			e->samplesUntilNextSegment = 0x7FFFFFFF;
			e->segmentIsExponential = TSF_FALSE;
			return;
		case TSF_SEGMENT_SUSTAIN:
			e->segment = TSF_SEGMENT_RELEASE;
			e->samplesUntilNextSegment = tsf_voice_envelope_release_samples(e, outSampleRate);
			if (e->isAmpEnv)
			{
				// I don't truly understand this; just following what LinuxSampler does.
				float mysterySlope = -9.226f / e->samplesUntilNextSegment;
				e->slope = TSF_EXPF(mysterySlope);
				e->segmentIsExponential = TSF_TRUE;
			}
			else
			{
				e->slope = -e->level / e->samplesUntilNextSegment;
				e->segmentIsExponential = TSF_FALSE;
			}
			return;
		case TSF_SEGMENT_RELEASE:
		default:
			e->segment = TSF_SEGMENT_DONE;
			e->segmentIsExponential = TSF_FALSE;
			e->level = e->slope = 0.0f;
			e->samplesUntilNextSegment = 0x7FFFFFF;
	}
}

static void tsf_voice_envelope_setup(struct tsf_voice_envelope* e, struct tsf_envelope* new_parameters, int midiNoteNumber, short midiVelocity, TSF_BOOL isAmpEnv, float outSampleRate)
{
	e->parameters = *new_parameters;
	if (e->parameters.keynumToHold)
	{
		e->parameters.hold += e->parameters.keynumToHold * (60.0f - midiNoteNumber);
		e->parameters.hold = (e->parameters.hold < -10000.0f ? 0.0f : tsf_timecents2Secsf(e->parameters.hold));
	}
	if (e->parameters.keynumToDecay)
	{
		e->parameters.decay += e->parameters.keynumToDecay * (60.0f - midiNoteNumber);
		e->parameters.decay = (e->parameters.decay < -10000.0f ? 0.0f : tsf_timecents2Secsf(e->parameters.decay));
	}
	e->midiVelocity = midiVelocity;
	e->isAmpEnv = isAmpEnv;
	tsf_voice_envelope_nextsegment(e, TSF_SEGMENT_NONE, outSampleRate);
}

static void tsf_voice_envelope_process(struct tsf_voice_envelope* e, int numSamples, float outSampleRate)
{
	if (e->slope)
	{
		if (e->segmentIsExponential) e->level *= TSF_POWF(e->slope, (float)numSamples);
		else e->level += (e->slope * numSamples);
	}
	if ((e->samplesUntilNextSegment -= numSamples) <= 0)
		tsf_voice_envelope_nextsegment(e, e->segment, outSampleRate);
}

static void tsf_voice_lowpass_setup(struct tsf_voice_lowpass* e, float Fc)
{
	// Lowpass filter from http://www.earlevel.com/main/2012/11/26/biquad-c-source-code/
	double K = TSF_TAN(TSF_PI * Fc), KK = K * K;
	double norm = 1 / (1 + K * e->QInv + KK);
	e->a0 = KK * norm;
	e->a1 = 2 * e->a0;
	e->b1 = 2 * (KK - 1) * norm;
	e->b2 = (1 - K * e->QInv + KK) * norm;
}

static float tsf_voice_lowpass_process(struct tsf_voice_lowpass* e, double In)
{
	double Out = In * e->a0 + e->z1; e->z1 = In * e->a1 + e->z2 - e->b1 * Out; e->z2 = In * e->a0 - e->b2 * Out; return (float)Out;
}

static void tsf_voice_lfo_setup(struct tsf_voice_lfo* e, float delay, int freqCents, float outSampleRate)
{
	e->samplesUntil = (int)(delay * outSampleRate);
	e->delta = (4.0f * tsf_cents2Hertz((float)freqCents) / outSampleRate);
	e->level = 0;
}

static void tsf_voice_lfo_process(struct tsf_voice_lfo* e, int blockSamples)
{
	if (e->samplesUntil > blockSamples) { e->samplesUntil -= blockSamples; return; }
	e->level += e->delta * blockSamples;
	if      (e->level >  1.0f) { e->delta = -e->delta; e->level =  2.0f - e->level; }
	else if (e->level < -1.0f) { e->delta = -e->delta; e->level = -2.0f - e->level; }
}

static void tsf_voice_kill(struct tsf_voice* v)
{
	v->playingPreset = -1;
}

static void tsf_voice_end(tsf* f, struct tsf_voice* v)
{
	// if maxVoiceNum is set, assume that voice rendering and note queuing are on separate threads
	// so to minimize the chance that voice rendering would advance the segment at the same time
	// we just do it twice here and hope that it sticks
	int repeats = (f->maxVoiceNum ? 2 : 1);
	while (repeats--)
	{
		tsf_voice_envelope_nextsegment(&v->ampenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
		tsf_voice_envelope_nextsegment(&v->modenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
		if (v->region->loop_mode == TSF_LOOPMODE_SUSTAIN)
		{
			// Continue playing, but stop looping.
			v->loopEnd = v->loopStart;
		}
	}
}

static void tsf_voice_endquick(tsf* f, struct tsf_voice* v)
{
	// if maxVoiceNum is set, assume that voice rendering and note queuing are on separate threads
	// so to minimize the chance that voice rendering would advance the segment at the same time
	// we just do it twice here and hope that it sticks
	int repeats = (f->maxVoiceNum ? 2 : 1);
	while (repeats--)
	{
		v->ampenv.parameters.release = 0.0f; tsf_voice_envelope_nextsegment(&v->ampenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
		v->modenv.parameters.release = 0.0f; tsf_voice_envelope_nextsegment(&v->modenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
	}
}

static void tsf_voice_calcpitchratio(struct tsf_voice* v, float pitchShift, float outSampleRate)
{
	double note = v->playingKey + v->region->transpose + v->region->tune / 100.0;
	double adjustedPitch = v->region->pitch_keycenter + (note - v->region->pitch_keycenter) * (v->region->pitch_keytrack / 100.0);
	if (pitchShift) adjustedPitch += pitchShift;
	v->pitchInputTimecents = adjustedPitch * 100.0;
	v->pitchOutputFactor = v->region->sample_rate / (tsf_timecents2Secsd(v->region->pitch_keycenter * 100.0) * outSampleRate);
}

static void tsf_voice_render(tsf* f, struct tsf_voice* v, float* outputBuffer, int numSamples)
{
	struct tsf_region* region = v->region;
	float* input = f->fontSamples;
	float* outL = outputBuffer;
	float* outR = (f->outputmode == TSF_STEREO_UNWEAVED ? outL + numSamples : TSF_NULL);

	// Cache some values, to give them at least some chance of ending up in registers.
	TSF_BOOL updateModEnv = (region->modEnvToPitch || region->modEnvToFilterFc);
	TSF_BOOL updateModLFO = (v->modlfo.delta && (region->modLfoToPitch || region->modLfoToFilterFc || region->modLfoToVolume));
	TSF_BOOL updateVibLFO = (v->viblfo.delta && (region->vibLfoToPitch));
	TSF_BOOL isLooping    = (v->loopStart < v->loopEnd);
	unsigned int tmpLoopStart = v->loopStart, tmpLoopEnd = v->loopEnd;
	double tmpSampleEndDbl = (double)region->end, tmpLoopEndDbl = (double)tmpLoopEnd + 1.0;
	double tmpSourceSamplePosition = v->sourceSamplePosition;
	struct tsf_voice_lowpass tmpLowpass = v->lowpass;

	TSF_BOOL dynamicLowpass = (region->modLfoToFilterFc || region->modEnvToFilterFc);
	float tmpSampleRate = f->outSampleRate, tmpInitialFilterFc, tmpModLfoToFilterFc, tmpModEnvToFilterFc;

	TSF_BOOL dynamicPitchRatio = (region->modLfoToPitch || region->modEnvToPitch || region->vibLfoToPitch);
	double pitchRatio;
	float tmpModLfoToPitch, tmpVibLfoToPitch, tmpModEnvToPitch;

	TSF_BOOL dynamicGain = (region->modLfoToVolume != 0);
	float noteGain = 0, tmpModLfoToVolume;

	if (dynamicLowpass) tmpInitialFilterFc = (float)region->initialFilterFc, tmpModLfoToFilterFc = (float)region->modLfoToFilterFc, tmpModEnvToFilterFc = (float)region->modEnvToFilterFc;
	else tmpInitialFilterFc = 0, tmpModLfoToFilterFc = 0, tmpModEnvToFilterFc = 0;

	if (dynamicPitchRatio) pitchRatio = 0, tmpModLfoToPitch = (float)region->modLfoToPitch, tmpVibLfoToPitch = (float)region->vibLfoToPitch, tmpModEnvToPitch = (float)region->modEnvToPitch;
	else pitchRatio = tsf_timecents2Secsd(v->pitchInputTimecents) * v->pitchOutputFactor, tmpModLfoToPitch = 0, tmpVibLfoToPitch = 0, tmpModEnvToPitch = 0;

	if (dynamicGain) tmpModLfoToVolume = (float)region->modLfoToVolume * 0.1f;
	else noteGain = tsf_decibelsToGain(v->noteGainDB), tmpModLfoToVolume = 0;

	while (numSamples)
	{
		float gainMono, gainLeft, gainRight;
		int blockSamples = (numSamples > TSF_RENDER_EFFECTSAMPLEBLOCK ? TSF_RENDER_EFFECTSAMPLEBLOCK : numSamples);
		numSamples -= blockSamples;

		if (dynamicLowpass)
		{
			float fres = tmpInitialFilterFc + v->modlfo.level * tmpModLfoToFilterFc + v->modenv.level * tmpModEnvToFilterFc;
			float lowpassFc = (fres <= 13500 ? tsf_cents2Hertz(fres) / tmpSampleRate : 1.0f);
			tmpLowpass.active = (lowpassFc < 0.499f);
			if (tmpLowpass.active) tsf_voice_lowpass_setup(&tmpLowpass, lowpassFc);
		}

		if (dynamicPitchRatio)
			pitchRatio = tsf_timecents2Secsd(v->pitchInputTimecents + (v->modlfo.level * tmpModLfoToPitch + v->viblfo.level * tmpVibLfoToPitch + v->modenv.level * tmpModEnvToPitch)) * v->pitchOutputFactor;

		if (dynamicGain)
			noteGain = tsf_decibelsToGain(v->noteGainDB + (v->modlfo.level * tmpModLfoToVolume));

		gainMono = noteGain * v->ampenv.level;

		// Update EG.
		tsf_voice_envelope_process(&v->ampenv, blockSamples, tmpSampleRate);
		if (updateModEnv) tsf_voice_envelope_process(&v->modenv, blockSamples, tmpSampleRate);

		// Update LFOs.
		if (updateModLFO) tsf_voice_lfo_process(&v->modlfo, blockSamples);
		if (updateVibLFO) tsf_voice_lfo_process(&v->viblfo, blockSamples);

		switch (f->outputmode)
		{
			case TSF_STEREO_INTERLEAVED:
				gainLeft = gainMono * v->panFactorLeft, gainRight = gainMono * v->panFactorRight;
				while (blockSamples-- && tmpSourceSamplePosition < tmpSampleEndDbl)
				{
					unsigned int pos = (unsigned int)tmpSourceSamplePosition, nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1);

					// Simple linear interpolation.
					float alpha = (float)(tmpSourceSamplePosition - pos), val = (input[pos] * (1.0f - alpha) + input[nextPos] * alpha);

					// Low-pass filter.
					if (tmpLowpass.active) val = tsf_voice_lowpass_process(&tmpLowpass, val);

					*outL++ += val * gainLeft;
					*outL++ += val * gainRight;

					// Next sample.
					tmpSourceSamplePosition += pitchRatio;
					if (tmpSourceSamplePosition >= tmpLoopEndDbl && isLooping) tmpSourceSamplePosition -= (tmpLoopEnd - tmpLoopStart + 1.0);
				}
				break;

			case TSF_STEREO_UNWEAVED:
				gainLeft = gainMono * v->panFactorLeft, gainRight = gainMono * v->panFactorRight;
				while (blockSamples-- && tmpSourceSamplePosition < tmpSampleEndDbl)
				{
					unsigned int pos = (unsigned int)tmpSourceSamplePosition, nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1);

					// Simple linear interpolation.
					float alpha = (float)(tmpSourceSamplePosition - pos), val = (input[pos] * (1.0f - alpha) + input[nextPos] * alpha);

					// Low-pass filter.
					if (tmpLowpass.active) val = tsf_voice_lowpass_process(&tmpLowpass, val);

					*outL++ += val * gainLeft;
					*outR++ += val * gainRight;

					// Next sample.
					tmpSourceSamplePosition += pitchRatio;
					if (tmpSourceSamplePosition >= tmpLoopEndDbl && isLooping) tmpSourceSamplePosition -= (tmpLoopEnd - tmpLoopStart + 1.0);
				}
				break;

			case TSF_MONO:
				while (blockSamples-- && tmpSourceSamplePosition < tmpSampleEndDbl)
				{
					unsigned int pos = (unsigned int)tmpSourceSamplePosition, nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1);

					// Simple linear interpolation.
					float alpha = (float)(tmpSourceSamplePosition - pos), val = (input[pos] * (1.0f - alpha) + input[nextPos] * alpha);

					// Low-pass filter.
					if (tmpLowpass.active) val = tsf_voice_lowpass_process(&tmpLowpass, val);

					*outL++ += val * gainMono;

					// Next sample.
					tmpSourceSamplePosition += pitchRatio;
					if (tmpSourceSamplePosition >= tmpLoopEndDbl && isLooping) tmpSourceSamplePosition -= (tmpLoopEnd - tmpLoopStart + 1.0);
				}
				break;
		}

		if (tmpSourceSamplePosition >= tmpSampleEndDbl || v->ampenv.segment == TSF_SEGMENT_DONE)
		{
			tsf_voice_kill(v);
			return;
		}
	}

	v->sourceSamplePosition = tmpSourceSamplePosition;
	if (tmpLowpass.active || dynamicLowpass) v->lowpass = tmpLowpass;
}

TSFDEF tsf* tsf_load(struct tsf_stream* stream)
{
	tsf* res = TSF_NULL;
	struct tsf_riffchunk chunkHead;
	struct tsf_riffchunk chunkList;
	struct tsf_hydra hydra;
	void* rawBuffer = TSF_NULL;
	float* floatBuffer = TSF_NULL;
	tsf_u32 smplCount = 0;

	if (!tsf_riffchunk_read(TSF_NULL, &chunkHead, stream) || !TSF_FourCCEquals(chunkHead.id, "sfbk"))
	{
		//if (e) *e = TSF_INVALID_NOSF2HEADER;
		return res;
	}

	// Read hydra and locate sample data.
	TSF_MEMSET(&hydra, 0, sizeof(hydra));
	while (tsf_riffchunk_read(&chunkHead, &chunkList, stream))
	{
		struct tsf_riffchunk chunk;
		if (TSF_FourCCEquals(chunkList.id, "pdta"))
		{
			while (tsf_riffchunk_read(&chunkList, &chunk, stream))
			{
				#define HandleChunk(chunkName) (TSF_FourCCEquals(chunk.id, #chunkName) && !(chunk.size % chunkName##SizeInFile)) \
					{ \
						int num = chunk.size / chunkName##SizeInFile, i; \
						hydra.chunkName##Num = num; \
						hydra.chunkName##s = (struct tsf_hydra_##chunkName*)TSF_MALLOC(num * sizeof(struct tsf_hydra_##chunkName)); \
						if (!hydra.chunkName##s) goto out_of_memory; \
						for (i = 0; i < num; ++i) tsf_hydra_read_##chunkName(&hydra.chunkName##s[i], stream); \
					}
				enum
				{
					phdrSizeInFile = 38, pbagSizeInFile =  4, pmodSizeInFile = 10,
					pgenSizeInFile =  4, instSizeInFile = 22, ibagSizeInFile =  4,
					imodSizeInFile = 10, igenSizeInFile =  4, shdrSizeInFile = 46
				};
				if      HandleChunk(phdr) else if HandleChunk(pbag) else if HandleChunk(pmod)
				else if HandleChunk(pgen) else if HandleChunk(inst) else if HandleChunk(ibag)
				else if HandleChunk(imod) else if HandleChunk(igen) else if HandleChunk(shdr)
				else stream->skip(stream->data, chunk.size);
				#undef HandleChunk
			}
		}
		else if (TSF_FourCCEquals(chunkList.id, "sdta"))
		{
			while (tsf_riffchunk_read(&chunkList, &chunk, stream))
			{
				if ((TSF_FourCCEquals(chunk.id, "smpl")
						#ifdef STB_VORBIS_INCLUDE_STB_VORBIS_H
						|| TSF_FourCCEquals(chunk.id, "smpo")
						#endif
					) && !rawBuffer && !floatBuffer && chunk.size >= sizeof(short))
				{
					if (!tsf_load_samples(&rawBuffer, &floatBuffer, &smplCount, &chunk, stream)) goto out_of_memory;
				}
				else stream->skip(stream->data, chunk.size);
			}
		}
		else stream->skip(stream->data, chunkList.size);
	}
	if (!hydra.phdrs || !hydra.pbags || !hydra.pmods || !hydra.pgens || !hydra.insts || !hydra.ibags || !hydra.imods || !hydra.igens || !hydra.shdrs)
	{
		//if (e) *e = TSF_INVALID_INCOMPLETE;
	}
	else if (!rawBuffer && !floatBuffer)
	{
		//if (e) *e = TSF_INVALID_NOSAMPLEDATA;
	}
	else
	{
		#ifdef STB_VORBIS_INCLUDE_STB_VORBIS_H
		if (!floatBuffer && !tsf_decode_sf3_samples(rawBuffer, &floatBuffer, &smplCount, &hydra)) goto out_of_memory;
		#endif
		res = (tsf*)TSF_MALLOC(sizeof(tsf));
		if (res) TSF_MEMSET(res, 0, sizeof(tsf));
		if (!res || !tsf_load_presets(res, &hydra, smplCount)) goto out_of_memory;
		res->outSampleRate = 44100.0f;
		res->fontSamples = floatBuffer;
		floatBuffer = TSF_NULL; // don't free below
	}
	if (0)
	{
		out_of_memory:
		TSF_FREE(res);
		res = TSF_NULL;
		//if (e) *e = TSF_OUT_OF_MEMORY;
	}
	TSF_FREE(hydra.phdrs); TSF_FREE(hydra.pbags); TSF_FREE(hydra.pmods);
	TSF_FREE(hydra.pgens); TSF_FREE(hydra.insts); TSF_FREE(hydra.ibags);
	TSF_FREE(hydra.imods); TSF_FREE(hydra.igens); TSF_FREE(hydra.shdrs);
	TSF_FREE(rawBuffer);   TSF_FREE(floatBuffer);
	return res;
}

TSFDEF tsf* tsf_copy(tsf* f)
{
	tsf* res;
	if (!f) return TSF_NULL;
	if (!f->refCount)
	{
		f->refCount = (int*)TSF_MALLOC(sizeof(int));
		if (!f->refCount) return TSF_NULL;
		*f->refCount = 1;
	}
	res = (tsf*)TSF_MALLOC(sizeof(tsf));
	if (!res) return TSF_NULL;
	TSF_MEMCPY(res, f, sizeof(tsf));
	res->voices = TSF_NULL;
	res->voiceNum = 0;
	res->channels = TSF_NULL;
	(*res->refCount)++;
	return res;
}

TSFDEF void tsf_close(tsf* f)
{
	if (!f) return;
	if (!f->refCount || !--(*f->refCount))
	{
		struct tsf_preset *preset = f->presets, *presetEnd = preset + f->presetNum;
		for (; preset != presetEnd; preset++) TSF_FREE(preset->regions);
		TSF_FREE(f->presets);
		TSF_FREE(f->fontSamples);
		TSF_FREE(f->refCount);
	}
	TSF_FREE(f->channels);
	TSF_FREE(f->voices);
	TSF_FREE(f);
}

TSFDEF void tsf_reset(tsf* f)
{
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++)
		if (v->playingPreset != -1 && (v->ampenv.segment < TSF_SEGMENT_RELEASE || v->ampenv.parameters.release))
			tsf_voice_endquick(f, v);
	if (f->channels) { TSF_FREE(f->channels); f->channels = TSF_NULL; }
}

TSFDEF int tsf_get_presetindex(const tsf* f, int bank, int preset_number)
{
	const struct tsf_preset *presets;
	int i, iMax;
	for (presets = f->presets, i = 0, iMax = f->presetNum; i < iMax; i++)
		if (presets[i].preset == preset_number && presets[i].bank == bank)
			return i;
	return -1;
}

TSFDEF int tsf_get_presetcount(const tsf* f)
{
	return f->presetNum;
}

TSFDEF const char* tsf_get_presetname(const tsf* f, int preset)
{
	return (preset < 0 || preset >= f->presetNum ? TSF_NULL : f->presets[preset].presetName);
}

TSFDEF const char* tsf_bank_get_presetname(const tsf* f, int bank, int preset_number)
{
	return tsf_get_presetname(f, tsf_get_presetindex(f, bank, preset_number));
}

TSFDEF void tsf_set_output(tsf* f, enum TSFOutputMode outputmode, int samplerate, float global_gain_db)
{
	f->outputmode = outputmode;
	f->outSampleRate = (float)(samplerate >= 1 ? samplerate : 44100.0f);
	f->globalGainDB = global_gain_db;
}

TSFDEF void tsf_set_volume(tsf* f, float global_volume)
{
	f->globalGainDB = (global_volume == 1.0f ? 0 : -tsf_gainToDecibels(1.0f / global_volume));
}

TSFDEF int tsf_set_max_voices(tsf* f, int max_voices)
{
	int i = f->voiceNum;
	int newVoiceNum = (f->voiceNum > max_voices ? f->voiceNum : max_voices);
	struct tsf_voice *newVoices = (struct tsf_voice*)TSF_REALLOC(f->voices, newVoiceNum * sizeof(struct tsf_voice));
	if (!newVoices) return 0;
	f->voices = newVoices;
	f->voiceNum = f->maxVoiceNum = newVoiceNum;
	for (; i < max_voices; i++)
		f->voices[i].playingPreset = -1;
	return 1;
}

TSFDEF int tsf_note_on(tsf* f, int preset_index, int key, float vel)
{
	short midiVelocity = (short)(vel * 127);
	unsigned int voicePlayIndex;
	struct tsf_region *region, *regionEnd;

	if (preset_index < 0 || preset_index >= f->presetNum) return 1;
	if (vel <= 0.0f) { tsf_note_off(f, preset_index, key); return 1; }

	// Play all matching regions.
	voicePlayIndex = f->voicePlayIndex++;
	for (region = f->presets[preset_index].regions, regionEnd = region + f->presets[preset_index].regionNum; region != regionEnd; region++)
	{
		struct tsf_voice *voice, *v, *vEnd; TSF_BOOL doLoop; float lowpassFilterQDB, lowpassFc;
		if (key < region->lokey || key > region->hikey || midiVelocity < region->lovel || midiVelocity > region->hivel) continue;

		voice = TSF_NULL, v = f->voices, vEnd = v + f->voiceNum;
		if (region->group)
		{
			for (; v != vEnd; v++)
				if (v->playingPreset == preset_index && v->region->group == region->group) tsf_voice_endquick(f, v);
				else if (v->playingPreset == -1 && !voice) voice = v;
		}
		else for (; v != vEnd; v++) if (v->playingPreset == -1) { voice = v; break; }

		if (!voice)
		{
			if (f->maxVoiceNum)
			{
				// Voices have been pre-allocated and limited to a maximum, try to kill a voice off in its release envelope
				int bestKillReleaseSamplePos = -999999999;
				for (v = f->voices; v != vEnd; v++)
				{
					if (v->ampenv.segment == TSF_SEGMENT_RELEASE)
					{
						// We're looking for the voice furthest into its release
						int releaseSamplesDone = tsf_voice_envelope_release_samples(&v->ampenv, f->outSampleRate) - v->ampenv.samplesUntilNextSegment;
						if (releaseSamplesDone > bestKillReleaseSamplePos)
						{
							bestKillReleaseSamplePos = releaseSamplesDone;
							voice = v;
						}
					}
				}
				if (!voice)
					continue;
				tsf_voice_kill(voice);
			}
			else
			{
				// Allocate more voices so we don't need to kill one off.
				struct tsf_voice* newVoices;
				f->voiceNum += 4;
				newVoices = (struct tsf_voice*)TSF_REALLOC(f->voices, f->voiceNum * sizeof(struct tsf_voice));
				if (!newVoices) return 0;
				f->voices = newVoices;
				voice = &f->voices[f->voiceNum - 4];
				voice[1].playingPreset = voice[2].playingPreset = voice[3].playingPreset = -1;
			}
		}

		voice->region = region;
		voice->playingPreset = preset_index;
		voice->playingKey = key;
		voice->playIndex = voicePlayIndex;
		voice->heldSustain = 0;
		voice->noteGainDB = f->globalGainDB - region->attenuation - tsf_gainToDecibels(1.0f / vel);

		if (f->channels)
		{
			f->channels->setupVoice(f, voice);
		}
		else
		{
			tsf_voice_calcpitchratio(voice, 0, f->outSampleRate);
			// The SFZ spec is silent about the pan curve, but a 3dB pan law seems common. This sqrt() curve matches what Dimension LE does; Alchemy Free seems closer to sin(adjustedPan * pi/2).
			voice->panFactorLeft  = TSF_SQRTF(0.5f - region->pan);
			voice->panFactorRight = TSF_SQRTF(0.5f + region->pan);
		}

		// Offset/end.
		voice->sourceSamplePosition = region->offset;

		// Loop.
		doLoop = (region->loop_mode != TSF_LOOPMODE_NONE && region->loop_start < region->loop_end);
		voice->loopStart = (doLoop ? region->loop_start : 0);
		voice->loopEnd = (doLoop ? region->loop_end : 0);

		// Setup envelopes.
		tsf_voice_envelope_setup(&voice->ampenv, &region->ampenv, key, midiVelocity, TSF_TRUE, f->outSampleRate);
		tsf_voice_envelope_setup(&voice->modenv, &region->modenv, key, midiVelocity, TSF_FALSE, f->outSampleRate);

		// Setup lowpass filter.
		lowpassFc = (region->initialFilterFc <= 13500 ? tsf_cents2Hertz((float)region->initialFilterFc) / f->outSampleRate : 1.0f);
		lowpassFilterQDB = region->initialFilterQ / 10.0f;
		voice->lowpass.QInv = 1.0 / TSF_POW(10.0, (lowpassFilterQDB / 20.0));
		voice->lowpass.z1 = voice->lowpass.z2 = 0;
		voice->lowpass.active = (lowpassFc < 0.499f);
		if (voice->lowpass.active) tsf_voice_lowpass_setup(&voice->lowpass, lowpassFc);

		// Setup LFO filters.
		tsf_voice_lfo_setup(&voice->modlfo, region->delayModLFO, region->freqModLFO, f->outSampleRate);
		tsf_voice_lfo_setup(&voice->viblfo, region->delayVibLFO, region->freqVibLFO, f->outSampleRate);
	}
	return 1;
}

TSFDEF int tsf_bank_note_on(tsf* f, int bank, int preset_number, int key, float vel)
{
	int preset_index = tsf_get_presetindex(f, bank, preset_number);
	if (preset_index == -1) return 0;
	return tsf_note_on(f, preset_index, key, vel);
}

TSFDEF void tsf_note_off(tsf* f, int preset_index, int key)
{
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum, *vMatchFirst = TSF_NULL, *vMatchLast = TSF_NULL;
	for (; v != vEnd; v++)
	{
		//Find the first and last entry in the voices list with matching preset, key and look up the smallest play index
		if (v->playingPreset != preset_index || v->playingKey != key || v->ampenv.segment >= TSF_SEGMENT_RELEASE) continue;
		else if (!vMatchFirst || v->playIndex < vMatchFirst->playIndex) vMatchFirst = vMatchLast = v;
		else if (v->playIndex == vMatchFirst->playIndex) vMatchLast = v;
	}
	if (!vMatchFirst) return;
	for (v = vMatchFirst; v <= vMatchLast; v++)
	{
		//Stop all voices with matching preset, key and the smallest play index which was enumerated above
		if (v != vMatchFirst && v != vMatchLast &&
			(v->playIndex != vMatchFirst->playIndex || v->playingPreset != preset_index || v->playingKey != key || v->ampenv.segment >= TSF_SEGMENT_RELEASE)) continue;
		tsf_voice_end(f, v);
	}
}

TSFDEF int tsf_bank_note_off(tsf* f, int bank, int preset_number, int key)
{
	int preset_index = tsf_get_presetindex(f, bank, preset_number);
	if (preset_index == -1) return 0;
	tsf_note_off(f, preset_index, key);
	return 1;
}

TSFDEF void tsf_note_off_all(tsf* f)
{
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++) if (v->playingPreset != -1 && v->ampenv.segment < TSF_SEGMENT_RELEASE)
		tsf_voice_end(f, v);
}

TSFDEF int tsf_active_voice_count(tsf* f)
{
	int count = 0;
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++) if (v->playingPreset != -1) count++;
	return count;
}

TSFDEF void tsf_render_short(tsf* f, short* buffer, int samples, int flag_mixing)
{
	float outputSamples[TSF_RENDER_SHORTBUFFERBLOCK];
	int channels = (f->outputmode == TSF_MONO ? 1 : 2), maxChannelSamples = TSF_RENDER_SHORTBUFFERBLOCK / channels;
	while (samples > 0)
	{
		int channelSamples = (samples > maxChannelSamples ? maxChannelSamples : samples);
		short* bufferEnd = buffer + channelSamples * channels;
		float *floatSamples = outputSamples;
		tsf_render_float(f, floatSamples, channelSamples, TSF_FALSE);
		samples -= channelSamples;

		if (flag_mixing)
			while (buffer != bufferEnd)
			{
				float v = *floatSamples++;
				int vi = *buffer + (v < -1.00004566f ? (int)-32768 : (v > 1.00001514f ? (int)32767 : (int)(v * 32767.5f)));
				*buffer++ = (vi < -32768 ? (short)-32768 : (vi > 32767 ? (short)32767 : (short)vi));
			}
		else
			while (buffer != bufferEnd)
			{
				float v = *floatSamples++;
				*buffer++ = (v < -1.00004566f ? (short)-32768 : (v > 1.00001514f ? (short)32767 : (short)(v * 32767.5f)));
			}
	}
}

TSFDEF void tsf_render_float(tsf* f, float* buffer, int samples, int flag_mixing)
{
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	if (!flag_mixing) TSF_MEMSET(buffer, 0, (f->outputmode == TSF_MONO ? 1 : 2) * sizeof(float) * samples);
	for (; v != vEnd; v++)
		if (v->playingPreset != -1)
			tsf_voice_render(f, v, buffer, samples);
}

static void tsf_channel_setup_voice(tsf* f, struct tsf_voice* v)
{
	struct tsf_channel* c = &f->channels->channels[f->channels->activeChannel];
	float newpan = v->region->pan + c->panOffset;
	v->playingChannel = f->channels->activeChannel;
	v->noteGainDB += c->gainDB;
	tsf_voice_calcpitchratio(v, (c->pitchWheel == 8192 ? c->tuning : ((c->pitchWheel / 16383.0f * c->pitchRange * 2.0f) - c->pitchRange + c->tuning)), f->outSampleRate);
	if      (newpan <= -0.5f) { v->panFactorLeft = 1.0f; v->panFactorRight = 0.0f; }
	else if (newpan >=  0.5f) { v->panFactorLeft = 0.0f; v->panFactorRight = 1.0f; }
	else { v->panFactorLeft = TSF_SQRTF(0.5f - newpan); v->panFactorRight = TSF_SQRTF(0.5f + newpan); }
}

static struct tsf_channel* tsf_channel_init(tsf* f, int channel)
{
	int i;
	if (f->channels && channel < f->channels->channelNum) return &f->channels->channels[channel];
	if (!f->channels)
	{
		f->channels = (struct tsf_channels*)TSF_MALLOC(sizeof(struct tsf_channels) + sizeof(struct tsf_channel) * channel);
		if (!f->channels) return TSF_NULL;
		f->channels->setupVoice = &tsf_channel_setup_voice;
		f->channels->channelNum = 0;
		f->channels->activeChannel = 0;
	}
	else
	{
		struct tsf_channels *newChannels = (struct tsf_channels*)TSF_REALLOC(f->channels, sizeof(struct tsf_channels) + sizeof(struct tsf_channel) * channel);
		if (!newChannels) return TSF_NULL;
		f->channels = newChannels;
	}
	i = f->channels->channelNum;
	f->channels->channelNum = channel + 1;
	for (; i <= channel; i++)
	{
		struct tsf_channel* c = &f->channels->channels[i];
		c->presetIndex = c->bank = 0;
		c->pitchWheel = c->midiPan = 8192;
		c->midiVolume = c->midiExpression = 16383;
		c->midiRPN = 0xFFFF;
		c->midiData = c->sustain = 0;
		c->panOffset = 0.0f;
		c->gainDB = 0.0f;
		c->pitchRange = 2.0f;
		c->tuning = 0.0f;
	}
	return &f->channels->channels[channel];
}

static void tsf_channel_applypitch(tsf* f, int channel, struct tsf_channel* c)
{
	struct tsf_voice *v, *vEnd;
	float pitchShift = (c->pitchWheel == 8192 ? c->tuning : ((c->pitchWheel / 16383.0f * c->pitchRange * 2.0f) - c->pitchRange + c->tuning));
	for (v = f->voices, vEnd = v + f->voiceNum; v != vEnd; v++)
		if (v->playingPreset != -1 && v->playingChannel == channel)
			tsf_voice_calcpitchratio(v, pitchShift, f->outSampleRate);
}

TSFDEF int tsf_channel_set_presetindex(tsf* f, int channel, int preset_index)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	c->presetIndex = (unsigned short)preset_index;
	return 1;
}

TSFDEF int tsf_channel_set_presetnumber(tsf* f, int channel, int preset_number, int flag_mididrums)
{
	int preset_index;
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	if (flag_mididrums)
	{
		preset_index = tsf_get_presetindex(f, 128 | (c->bank & 0x7FFF), preset_number);
		if (preset_index == -1) preset_index = tsf_get_presetindex(f, 128, preset_number);
		if (preset_index == -1) preset_index = tsf_get_presetindex(f, 128, 0);
		if (preset_index == -1) preset_index = tsf_get_presetindex(f, (c->bank & 0x7FFF), preset_number);
	}
	else preset_index = tsf_get_presetindex(f, (c->bank & 0x7FFF), preset_number);
	if (preset_index == -1) preset_index = tsf_get_presetindex(f, 0, preset_number);
	if (preset_index != -1)
	{
		c->presetIndex = (unsigned short)preset_index;
		return 1;
	}
	return 0;
}

TSFDEF int tsf_channel_set_bank(tsf* f, int channel, int bank)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	c->bank = (unsigned short)bank;
	return 1;
}

TSFDEF int tsf_channel_set_bank_preset(tsf* f, int channel, int bank, int preset_number)
{
	int preset_index;
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	preset_index = tsf_get_presetindex(f, bank, preset_number);
	if (preset_index == -1) return 0;
	c->presetIndex = (unsigned short)preset_index;
	c->bank = (unsigned short)bank;
	return 1;
}

TSFDEF int tsf_channel_set_pan(tsf* f, int channel, float pan)
{
	struct tsf_voice *v, *vEnd;
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	for (v = f->voices, vEnd = v + f->voiceNum; v != vEnd; v++)
		if (v->playingPreset != -1 && v->playingChannel == channel)
		{
			float newpan = v->region->pan + pan - 0.5f;
			if      (newpan <= -0.5f) { v->panFactorLeft = 1.0f; v->panFactorRight = 0.0f; }
			else if (newpan >=  0.5f) { v->panFactorLeft = 0.0f; v->panFactorRight = 1.0f; }
			else { v->panFactorLeft = TSF_SQRTF(0.5f - newpan); v->panFactorRight = TSF_SQRTF(0.5f + newpan); }
		}
	c->panOffset = pan - 0.5f;
	return 1;
}

TSFDEF int tsf_channel_set_volume(tsf* f, int channel, float volume)
{
	float gainDB = tsf_gainToDecibels(volume), gainDBChange;
	struct tsf_voice *v, *vEnd;
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	if (gainDB == c->gainDB) return 1;
	for (v = f->voices, vEnd = v + f->voiceNum, gainDBChange = gainDB - c->gainDB; v != vEnd; v++)
		if (v->playingPreset != -1 && v->playingChannel == channel)
			v->noteGainDB += gainDBChange;
	c->gainDB = gainDB;
	return 1;
}

TSFDEF int tsf_channel_set_pitchwheel(tsf* f, int channel, int pitch_wheel)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	if (c->pitchWheel == pitch_wheel) return 1;
	c->pitchWheel = (unsigned short)pitch_wheel;
	tsf_channel_applypitch(f, channel, c);
	return 1;
}

TSFDEF int tsf_channel_set_pitchrange(tsf* f, int channel, float pitch_range)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	if (c->pitchRange == pitch_range) return 1;
	c->pitchRange = pitch_range;
	if (c->pitchWheel != 8192) tsf_channel_applypitch(f, channel, c);
	return 1;
}

TSFDEF int tsf_channel_set_tuning(tsf* f, int channel, float tuning)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	if (c->tuning == tuning) return 1;
	c->tuning = tuning;
	tsf_channel_applypitch(f, channel, c);
	return 1;
}

TSFDEF int tsf_channel_set_sustain(tsf* f, int channel, int flag_sustain)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	if (!c->sustain == !flag_sustain) return 1;
	c->sustain = (unsigned short)(flag_sustain != 0);
	//Turning on sustain does no action now, just starts note_off behaving differently
	if (flag_sustain) return 1;
	//Turning off sustain, actually end voices that got a note_off and were set to heldSustain status
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++)
		if (v->playingPreset != -1 && v->playingChannel == channel && v->ampenv.segment < TSF_SEGMENT_RELEASE && v->heldSustain)
			tsf_voice_end(f, v);
	return 1;
}

TSFDEF int tsf_channel_note_on(tsf* f, int channel, int key, float vel)
{
	if (!f->channels || channel >= f->channels->channelNum) return 1;
	f->channels->activeChannel = channel;
	if (!vel)
	{
		tsf_channel_note_off(f, channel, key);
		return 1;
	}
	return tsf_note_on(f, f->channels->channels[channel].presetIndex, key, vel);
}

TSFDEF void tsf_channel_note_off(tsf* f, int channel, int key)
{
	unsigned sustain;
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum, *vMatchFirst = TSF_NULL, *vMatchLast = TSF_NULL;
	for (; v != vEnd; v++)
	{
		//Find the first and last entry in the voices list with matching channel, key and look up the smallest play index
		if (v->playingPreset == -1 || v->playingChannel != channel || v->playingKey != key || v->ampenv.segment >= TSF_SEGMENT_RELEASE || v->heldSustain) continue;
		else if (!vMatchFirst || v->playIndex < vMatchFirst->playIndex) vMatchFirst = vMatchLast = v;
		else if (v->playIndex == vMatchFirst->playIndex) vMatchLast = v;
	}
	if (!vMatchFirst) return;
	for (sustain = f->channels->channels[channel].sustain, v = vMatchFirst; v <= vMatchLast; v++)
	{
		//Stop all voices with matching channel, key and the smallest play index which was enumerated above
		if (v != vMatchFirst && v != vMatchLast &&
			(v->playIndex != vMatchFirst->playIndex || v->playingPreset == -1 || v->playingChannel != channel || v->playingKey != key || v->ampenv.segment >= TSF_SEGMENT_RELEASE)) continue;
		//Don't turn off if sustain is active, just mark as held by sustain so we don't forget it
		if (sustain)
			v->heldSustain = 1;
		else
			tsf_voice_end(f, v);
	}
}

TSFDEF void tsf_channel_note_off_all(tsf* f, int channel)
{
	//Ignore sustain channel settings, note_off_all overrides
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++)
		if (v->playingPreset != -1 && v->playingChannel == channel && v->ampenv.segment < TSF_SEGMENT_RELEASE)
			tsf_voice_end(f, v);
}

TSFDEF void tsf_channel_sounds_off_all(tsf* f, int channel)
{
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++)
		if (v->playingPreset != -1 && v->playingChannel == channel && (v->ampenv.segment < TSF_SEGMENT_RELEASE || v->ampenv.parameters.release))
			tsf_voice_endquick(f, v);
}

TSFDEF int tsf_channel_midi_control(tsf* f, int channel, int controller, int control_value)
{
	struct tsf_channel* c = tsf_channel_init(f, channel);
	if (!c) return 0;
	switch (controller)
	{
		case   7 /*VOLUME_MSB*/      : c->midiVolume     = (unsigned short)((c->midiVolume     & 0x7F  ) | (control_value << 7)); goto TCMC_SET_VOLUME;
		case  39 /*VOLUME_LSB*/      : c->midiVolume     = (unsigned short)((c->midiVolume     & 0x3F80) |  control_value);       goto TCMC_SET_VOLUME;
		case  11 /*EXPRESSION_MSB*/  : c->midiExpression = (unsigned short)((c->midiExpression & 0x7F  ) | (control_value << 7)); goto TCMC_SET_VOLUME;
		case  43 /*EXPRESSION_LSB*/  : c->midiExpression = (unsigned short)((c->midiExpression & 0x3F80) |  control_value);       goto TCMC_SET_VOLUME;
		case  10 /*PAN_MSB*/         : c->midiPan        = (unsigned short)((c->midiPan        & 0x7F  ) | (control_value << 7)); goto TCMC_SET_PAN;
		case  42 /*PAN_LSB*/         : c->midiPan        = (unsigned short)((c->midiPan        & 0x3F80) |  control_value);       goto TCMC_SET_PAN;
		case   6 /*DATA_ENTRY_MSB*/  : c->midiData       = (unsigned short)((c->midiData       & 0x7F)   | (control_value << 7)); goto TCMC_SET_DATA;
		case  38 /*DATA_ENTRY_LSB*/  : c->midiData       = (unsigned short)((c->midiData       & 0x3F80) |  control_value);       goto TCMC_SET_DATA;
		case   0 /*BANK_SELECT_MSB*/ : c->bank = (unsigned short)(0x8000 | control_value); return 1; //bank select MSB alone acts like LSB
		case  32 /*BANK_SELECT_LSB*/ : c->bank = (unsigned short)((c->bank & 0x8000 ? ((c->bank & 0x7F) << 7) : 0) | control_value); return 1;
		case 101 /*RPN_MSB*/         : c->midiRPN = (unsigned short)(((c->midiRPN == 0xFFFF ? 0 : c->midiRPN) & 0x7F  ) | (control_value << 7)); return 1;
		case 100 /*RPN_LSB*/         : c->midiRPN = (unsigned short)(((c->midiRPN == 0xFFFF ? 0 : c->midiRPN) & 0x3F80) |  control_value); return 1;
		case  98 /*NRPN_LSB*/        : c->midiRPN = 0xFFFF; return 1;
		case  99 /*NRPN_MSB*/        : c->midiRPN = 0xFFFF; return 1;
		case  64 /*SUSTAIN*/         : tsf_channel_set_sustain(f, channel, (int)(control_value >= 64)); return 1;
		case 120 /*ALL_SOUND_OFF*/   : tsf_channel_sounds_off_all(f, channel); return 1;
		case 123 /*ALL_NOTES_OFF*/   : tsf_channel_note_off_all(f, channel);   return 1;
		case 121 /*ALL_CTRL_OFF*/    :
			c->midiVolume = c->midiExpression = 16383;
			c->midiPan = 8192;
			c->bank = 0;
			c->midiRPN = 0xFFFF;
			c->midiData = 0;
			tsf_channel_set_volume(f, channel, 1.0f);
			tsf_channel_set_pan(f, channel, 0.5f);
			tsf_channel_set_pitchrange(f, channel, 2.0f);
			tsf_channel_set_tuning(f, channel, 0);
			return 1;
	}
	return 1;
TCMC_SET_VOLUME:
	//Raising to the power of 3 seems to result in a decent sounding volume curve for MIDI
	tsf_channel_set_volume(f, channel, TSF_POWF((c->midiVolume / 16383.0f) * (c->midiExpression / 16383.0f), 3.0f));
	return 1;
TCMC_SET_PAN:
	tsf_channel_set_pan(f, channel, c->midiPan / 16383.0f);
	return 1;
TCMC_SET_DATA:
	if      (c->midiRPN == 0) tsf_channel_set_pitchrange(f, channel, (c->midiData >> 7) + 0.01f * (c->midiData & 0x7F));
	else if (c->midiRPN == 1) tsf_channel_set_tuning(f, channel, (int)c->tuning + ((float)c->midiData - 8192.0f) / 8192.0f); //fine tune
	else if (c->midiRPN == 2 && controller == 6) tsf_channel_set_tuning(f, channel, ((float)control_value - 64.0f) + (c->tuning - (int)c->tuning)); //coarse tune
	return 1;
}

TSFDEF int tsf_channel_get_preset_index(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->channels->channels[channel].presetIndex : 0);
}

TSFDEF int tsf_channel_get_preset_bank(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? (f->channels->channels[channel].bank & 0x7FFF) : 0);
}

TSFDEF int tsf_channel_get_preset_number(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->presets[f->channels->channels[channel].presetIndex].preset : 0);
}

TSFDEF float tsf_channel_get_pan(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->channels->channels[channel].panOffset - 0.5f : 0.5f);
}

TSFDEF float tsf_channel_get_volume(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? tsf_decibelsToGain(f->channels->channels[channel].gainDB) : 1.0f);
}

TSFDEF int tsf_channel_get_pitchwheel(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->channels->channels[channel].pitchWheel : 8192);
}

TSFDEF float tsf_channel_get_pitchrange(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->channels->channels[channel].pitchRange : 2.0f);
}

TSFDEF float tsf_channel_get_tuning(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->channels->channels[channel].tuning : 0.0f);
}

#ifdef __cplusplus
}
#endif

#endif //TSF_IMPLEMENTATION


#define PI 3.14159265358979f
#define TAU 6.28318530717959f
#define MAXCOL 35
#define MAXI 8
#define MAXDOTS 900
#define MAXSP 2500
#define MAXTINT 1800

/* ------------------------------------------------------------ Zufall */
static uint64_t rs = 88172645463325252ull;
static inline uint32_t rnd(void){ rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }
static inline float rf(void){ return (rnd() & 0xFFFFFF) / 16777216.f; }
static inline float rr(float a, float b){ return a + (b - a) * rf(); }
static uint64_t as_ = 0x9E3779B97F4A7C15ull;               /* eigener Zufall fuer den Audio-Thread */
static inline uint32_t arnd(void){ as_ ^= as_ << 13; as_ ^= as_ >> 7; as_ ^= as_ << 17; return (uint32_t)(as_ >> 16); }
static inline float arf(void){ return (arnd() & 0xFFFFFF) / 16777216.f; }
static inline float clampf(float v, float a, float b){ return v < a ? a : (v > b ? b : v); }

/* ------------------------------------------------------------ Farben */
static float pr[MAXCOL], pg[MAXCOL], pb[MAXCOL];
static int ncol = 12;

static void hsv(float h, float s, float v, float *r, float *g, float *b){
    h = fmodf(h, 360.f) / 60.f; int i = (int)h; float f = h - i;
    float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i % 6){
        case 0: *r = v; *g = t; *b = p; break; case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break; case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break; default: *r = v; *g = p; *b = q; break;
    }
}
static void make_palette(int n){
    /* 34 knallige Farbtoene + 1 Weiss = 35 Grundfarben, davon zufaellig n */
    float R[35], G[35], B[35];
    for (int i = 0; i < 34; i++){ hsv(i * 360.f / 34.f, 1.f, 1.f, &R[i], &G[i], &B[i]); R[i] *= 255; G[i] *= 255; B[i] *= 255; }
    R[34] = 255; G[34] = 250; B[34] = 245;
    int idx[35]; for (int i = 0; i < 35; i++) idx[i] = i;
    for (int i = 34; i > 0; i--){ int j = rnd() % (i + 1); int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
    ncol = n;
    for (int i = 0; i < n; i++){ pr[i] = R[idx[i]]; pg[i] = G[idx[i]]; pb[i] = B[idx[i]]; }
}

/* ------------------------------------------------------------ Welt */
static int Wc, Hc, cell = 1;
static uint32_t *fb; static uint8_t *bg; static uint32_t graytab[256];
static float DMAX = 8.f;           /* max. Punktdurchmesser in Zellen (= 1% der Breite) */
static float T = 0;                /* Simulationszeit */

typedef struct { float x, y, d, sf; } Dot;
typedef struct {
    int on, type, n, col, span, decided, nomorph; Dot *d;
    float cx, cy, sc, rot, spin, vx, vy, age, tin, thold, tout, speed, t, dfrac, rate, p[6];
} Inst;
typedef struct { int on, col; float x, y, vx, vy, age, life, dmax; } Spark;
typedef struct { float x, y, br; int col; } Tint;
static Inst inst[MAXI];
static Spark sp[MAXSP];
static Tint tn[MAXTINT];
static int nsp, ntn;

/* Statistik pro Farbe -> Musik */
typedef struct { volatile float act, x, y, br; } Stat;
static Stat stats[MAXCOL];
static float M_[MAXCOL], SX[MAXCOL], SY[MAXCOL], B3[MAXCOL];

static inline void put(int ix, int iy, int r, int g, int b){
    uint8_t *p = (uint8_t *)&fb[iy * Wc + ix];           /* ARGB8888 little endian: B G R A */
    if (p[2] < r) p[2] = r; if (p[1] < g) p[1] = g; if (p[0] < b) p[0] = b;
}
static void splat(float x, float y, float d, int cr, int cg, int cb){
    if (d < 1.25f){
        int ix = (int)x, iy = (int)y; if (x < 0 || y < 0 || ix >= Wc || iy >= Hc) return;
        put(ix, iy, cr, cg, cb); return;
    }
    float r = d * 0.5f;
    int x0 = (int)floorf(x - r - 0.5f), x1 = (int)ceilf(x + r + 0.5f), y0 = (int)floorf(y - r - 0.5f), y1 = (int)ceilf(y + r + 0.5f);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= Wc) x1 = Wc - 1; if (y1 >= Hc) y1 = Hc - 1;
    for (int yy = y0; yy <= y1; yy++){
        float dy = yy + 0.5f - y;
        for (int xx = x0; xx <= x1; xx++){
            float dx = xx + 0.5f - x, c = r - sqrtf(dx * dx + dy * dy) + 0.5f;
            if (c <= 0) continue; if (c > 1) c = 1;
            put(xx, yy, (int)(cr * c), (int)(cg * c), (int)(cb * c));
        }
    }
}
/* Groesse -> Helligkeit: winzig = fast schwarz/grau, gross = voll leuchtend */
static inline void paint(float x, float y, float d, int c){
    float dn = clampf((d - 1.f) / (DMAX - 1.f), 0, 1);
    float b = 0.18f + 0.82f * powf(dn, 0.55f);
    splat(x, y, d, (int)(pr[c] * b), (int)(pg[c] * b), (int)(pb[c] * b));
    if (dn > 0.06f){ float m = dn * dn; M_[c] += m; SX[c] += x * m; SY[c] += y * m; B3[c] += dn * m; }
}

/* ------------------------------------------------------------ Muster */
#define NTYPES 16
static void stick(float u, float t, float *ox, float *oy, float *sz){
    float s = sinf(t * 2.2f), air = s > 0 ? s : 0, cr = s < 0 ? -s : 0;
    float hx = 0, hy = 0.15f + cr * 0.18f - air * 0.55f, shx = 0, shy = hy - 0.55f;
    float bx[9], by[9], ex[9], ey[9], len[9];
    bx[0] = hx; by[0] = hy; ex[0] = shx; ey[0] = shy;
    for (int side = 0; side < 2; side++){
        float sg = side ? 1.f : -1.f;
        float a1 = sg * (0.10f + 0.55f * air + 0.65f * cr), kx = hx + 0.38f * sinf(a1), ky = hy + 0.38f * cosf(a1);
        float a2 = a1 - sg * (1.1f * air + 0.6f * cr), fx = kx + 0.38f * sinf(a2), fy = ky + 0.38f * cosf(a2);
        bx[1 + side] = hx; by[1 + side] = hy; ex[1 + side] = kx; ey[1 + side] = ky;
        bx[3 + side] = kx; by[3 + side] = ky; ex[3 + side] = fx; ey[3 + side] = fy;
        float b1 = sg * (0.35f + 2.4f * air - 0.2f * cr), elx = shx + 0.3f * sinf(b1), ely = shy + 0.3f * cosf(b1);
        float b2 = b1 + sg * 0.45f, hdx = elx + 0.3f * sinf(b2), hdy = ely + 0.3f * cosf(b2);
        bx[5 + side] = shx; by[5 + side] = shy; ex[5 + side] = elx; ey[5 + side] = ely;
        bx[7 + side] = elx; by[7 + side] = ely; ex[7 + side] = hdx; ey[7 + side] = hdy;
    }
    float tot = 0;
    for (int i = 0; i < 9; i++){ len[i] = hypotf(ex[i] - bx[i], ey[i] - by[i]) + 0.001f; tot += len[i]; }
    if (u < 0.2f){ float a = TAU * u / 0.2f; *ox = shx + 0.14f * cosf(a); *oy = shy - 0.24f + 0.14f * sinf(a); *sz = 1.f; return; }
    float v = (u - 0.2f) / 0.8f * tot, acc = 0; int i = 0;
    for (; i < 8; i++){ if (v < acc + len[i]) break; acc += len[i]; }
    float f = clampf((v - acc) / len[i], 0, 1);
    *ox = bx[i] + (ex[i] - bx[i]) * f; *oy = by[i] + (ey[i] - by[i]) * f; *sz = 0.65f;
}
static const int CE[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
static void pat(int type, float u, float t, int n, const float *p, float *ox, float *oy, float *sz){
    float x = 0, y = 0, s = 0.8f;
    switch (type){
    case 0: { int k = 1 + (int)(p[0] * 3); float uu = u * k; int j = (int)uu; uu -= j;
        float R = 1.f - 0.6f * j / k, a = TAU * uu + t * (0.3f + 0.25f * j) * (p[1] > 0.5f ? 1 : -1);
        x = R * cosf(a); y = R * sinf(a); s = 0.9f; } break;
    case 1: { int k = 1 + (int)(p[0] * 4); float uu = u * k; int j = (int)uu; uu -= j;
        x = -1 + 2 * uu; y = (j - (k - 1) * 0.5f) * 0.5f + 0.15f * sinf(t + x * 3 * (0.3f + p[1])); s = 0.7f + 0.3f * sinf(uu * 6 + t); } break;
    case 2: { int arms = 1 + (int)(p[0] * 3); float uu = u * arms; int j = (int)uu; uu -= j;
        float a = TAU * (p[1] * 3 + 1.5f) * uu + t * 0.5f + TAU * j / arms; x = uu * cosf(a); y = uu * sinf(a); s = 0.4f + 0.6f * uu; } break;
    case 3: { int k = 2 + (int)(p[0] * 5); float th = TAU * u * ((k & 1) ? 1.f : 2.f), r = cosf(k * th);
        x = r * cosf(th + t * 0.2f); y = r * sinf(th + t * 0.2f); s = 0.5f + 0.5f * fabsf(r); } break;
    case 4: { float a = TAU * u; x = sinf(a * (1 + (int)(p[0] * 3)) + t * 0.5f); y = sinf(a * (2 + (int)(p[1] * 3)) + p[2] * 6); s = 0.75f; } break;
    case 5: { int nn = 3 + (int)(p[0] * 5), star = p[1] > 0.5f, m = nn * (star ? 2 : 1);
        float q = u * m; int k = (int)q; float f = q - k, a0 = TAU * k / m + t * 0.15f, a1 = TAU * (k + 1) / m + t * 0.15f;
        float r0 = star ? ((k & 1) ? 0.45f : 1.f) : 1.f, r1 = star ? (((k + 1) & 1) ? 0.45f : 1.f) : 1.f;
        x = (1 - f) * r0 * cosf(a0) + f * r1 * cosf(a1); y = (1 - f) * r0 * sinf(a0) + f * r1 * sinf(a1); s = 0.8f; } break;
    case 6: { int cols = (int)ceilf(sqrtf((float)n)), i = (int)(u * n); if (cols < 2) cols = 2;
        x = (i % cols) / (float)(cols - 1) * 2 - 1; y = (i / cols) / (float)(cols - 1) * 2 - 1;
        float wx = 0.12f * sinf(x * 4 + t * 1.5f), wy = 0.08f * sinf(y * 4 + t);
        s = 0.35f + 0.65f * (0.5f + 0.5f * sinf(x * 5 + y * 5 - t * 2)); x += wy; y += wx; } break;
    case 7: { int k = 2 + (int)(p[0] * 3); float uu = u * k; int j = (int)uu; uu -= j;
        x = -1 + 2 * uu; y = (j - (k - 1) * 0.5f) * 0.45f + 0.35f * sinf(x * (3 + j) + t * (1 + 0.4f * j)); s = 0.4f + 0.6f * fabsf(cosf(x * 3 + t)); } break;
    case 8: stick(u, t, &x, &y, &s); break;
    case 9: { float f = sinf(t * 3.f);
        if (u < 0.2f){ float a = TAU * u / 0.2f; if (u < 0.12f){ x = 0.18f * cosf(a * 1.67f); y = 0.1f + 0.3f * sinf(a * 1.67f); } else { float b = TAU * (u - 0.12f) / 0.08f; x = 0.1f * cosf(b); y = -0.28f + 0.1f * sinf(b); } s = 1.f; }
        else if (u < 0.88f){ float q = (u - 0.2f) / 0.68f * 4; int idx = (int)q; float sf = q - idx, sg = (idx & 1) ? 1.f : -1.f; int e = idx >> 1;
            x = sg * (0.15f + 0.95f * sf); y = -0.1f - f * 0.75f * sf * sf + e * 0.28f * (1 - sf) + 0.1f * sf; s = 0.6f + 0.4f * (1 - sf); }
        else { float v = (u - 0.88f) / 0.12f * 2 - 1; x = v * 0.22f; y = 0.4f + 0.4f * (1 - fabsf(v)) + 0.06f * f; s = 0.5f; } } break;
    case 10: { float flip = p[0] > 0.5f ? -1.f : 1.f;
        if (u < 0.88f){ int side = u < 0.44f ? 1 : -1; float xx = (u < 0.44f ? u : u - 0.44f) / 0.44f;
            float bx = -0.8f + 1.6f * xx, w = 0.38f * powf(sinf(PI * (xx * 0.9f + 0.05f)), 0.8f);
            y = side * w + 0.15f * sinf(t * 4 - bx * 3) * (0.5f - 0.5f * bx); x = bx * flip; s = 0.45f + 0.55f * xx; }
        else if (u < 0.9f){ x = 0.5f * flip; y = -0.08f; s = 1.f; }
        else { float v = (u - 0.9f) / 0.1f; int side = v < 0.5f ? 1 : -1; float vv = (v < 0.5f ? v : v - 0.5f) * 2;
            x = (-0.8f - 0.35f * vv) * flip; y = side * 0.32f * vv + 0.15f * sinf(t * 4 + 2.4f); s = 0.6f; } } break;
    case 11: { int k = 1 + (int)(p[0] * 2); float uu = u * k; int j = (int)uu; uu -= j; float a = TAU * uu, sc = (1 - j * 0.35f) * (1 + 0.08f * sinf(t * 4));
        float sn = sinf(a); x = 16 * sn * sn * sn / 17.f * sc; y = -(13 * cosf(a) - 5 * cosf(2 * a) - 2 * cosf(3 * a) - cosf(4 * a)) / 17.f * sc; s = 0.8f; } break;
    case 12: { int k = 3 + (int)(p[0] * 5); float r = 1.f / k, d = r * (0.8f + p[1] * 0.9f), a = TAU * u, nrm = 1 - r + d;
        float rot = t * 0.3f; float xx = ((1 - r) * cosf(a) + d * cosf((k - 1) * a)) / nrm, yy = ((1 - r) * sinf(a) - d * sinf((k - 1) * a)) / nrm;
        x = xx * cosf(rot) - yy * sinf(rot); y = xx * sinf(rot) + yy * cosf(rot); s = 0.7f; } break;
    case 13: { int k = 4; float q = u * k; int j = (int)q; float a = TAU * (q - j), R = fmodf(j / (float)k + t * 0.15f, 1.f);
        x = R * cosf(a); y = R * sinf(a); s = 0.3f + 0.7f * (1 - R); } break;
    case 14: { float e = u * 12; int ei = (int)e; if (ei > 11) ei = 11; float f = e - ei, vx[8], vy[8], vz[8];
        float ax = t * 0.7f, ay = t * 0.9f, az = t * 0.4f;
        for (int i = 0; i < 8; i++){ float X = (i & 1) ? 0.6f : -0.6f, Y = (i & 2) ? 0.6f : -0.6f, Z = (i & 4) ? 0.6f : -0.6f;
            float y1 = Y * cosf(ax) - Z * sinf(ax), z1 = Y * sinf(ax) + Z * cosf(ax), x2 = X * cosf(ay) + z1 * sinf(ay), z2 = -X * sinf(ay) + z1 * cosf(ay);
            vx[i] = x2 * cosf(az) - y1 * sinf(az); vy[i] = x2 * sinf(az) + y1 * cosf(az); vz[i] = z2; }
        static const int Q[8] = {0,1,3,2,6,7,5,4}; (void)Q;
        /* Wuerfelecken: Index-Bits -> Kanten zwischen Ecken, die sich in genau einem Bit unterscheiden */
        int a = 0, b = 0, cnt = 0;
        for (int i = 0; i < 8 && cnt <= ei; i++) for (int bit = 0; bit < 3 && cnt <= ei; bit++){ int j = i ^ (1 << bit); if (j > i){ if (cnt == ei){ a = i; b = j; } cnt++; } }
        float X = vx[a] + (vx[b] - vx[a]) * f, Y = vy[a] + (vy[b] - vy[a]) * f, Z = vz[a] + (vz[b] - vz[a]) * f, pz = 1.f / (1.f + 0.25f * Z);
        x = X * pz * 1.4f; y = Y * pz * 1.4f; s = 0.45f + 0.55f * clampf(0.5f - Z, 0, 1); } break;
    case 15: { int j = u < 0.5f ? 0 : 1; float uu = (u - j * 0.5f) * 2; x = -1 + 2 * uu; float ph = x * 5 + t * 2 + j * PI;
        y = 0.4f * sinf(ph); s = 0.55f + 0.45f * cosf(ph); } break;
    }
    (void)CE;
    *ox = x; *oy = y; *sz = s;
}

/* ------------------------------------------------------------ Instanzen / Funken */
static float Eng = 0, ramp = 0;
static int upright(int type){ return type == 8 || type == 9 || type == 10 || type == 11; }
static void inst_setup(Inst *I, int keepdots){
    I->type = rnd() % NTYPES;
    for (int k = 0; k < 6; k++) I->p[k] = rf();
    I->col = rnd() % ncol; I->span = (rf() < 0.6f) ? 0 : 2 + rnd() % 4;
    I->tin = rr(5, 14); I->thold = rr(8, 70) * (rf() < 0.2f ? 2.f : 1.f); I->tout = rr(5, 14);
    I->speed = rr(0.5f, 1.6f); I->spin = (rf() < 0.35f && !upright(I->type)) ? rr(-0.15f, 0.15f) : 0;
    I->rot = upright(I->type) ? rr(-0.3f, 0.3f) : rr(0, TAU);
    I->decided = 0; I->dfrac = 0.3f + 0.7f * powf(rf(), 0.9f);
    if (!keepdots){
        I->sc = Hc * rr(0.15f, 0.38f);
        float m = I->sc * 0.7f; I->cx = rr(m, fmaxf(m + 1, Wc - m)); I->cy = rr(m, fmaxf(m + 1, Hc - m));
        float a = rr(0, TAU), v = rf() < 0.5f ? 0 : rr(1, 9); I->vx = cosf(a) * v; I->vy = sinf(a) * v;
        float dm = 2.f + I->dfrac * (DMAX - 2.f);
        I->n = (int)clampf(I->sc * 6.f / (0.5f + dm * 0.35f), 120, MAXDOTS); if (I->type == 8) I->n = (int)(I->n * 0.8f);
        I->d = malloc(sizeof(Dot) * I->n);
        for (int i = 0; i < I->n; i++){ float a2 = rr(0, TAU), r = sqrtf(rf()) * I->sc * 1.3f;
            I->d[i].x = I->cx + cosf(a2) * r; I->d[i].y = I->cy + sinf(a2) * r; I->d[i].d = 1; I->d[i].sf = rr(0.6f, 1.f); }
        I->age = 0; I->rate = 6.f; I->t = rr(0, 20); I->nomorph = 0; I->on = 1;
    }
}
static int count_inst(void){ int c = 0; for (int i = 0; i < MAXI; i++) c += inst[i].on; return c; }

static void update_inst(Inst *I, float dt){
    I->age += dt; I->t += dt * I->speed; I->rot += I->spin * dt;
    I->rate += (6.f - I->rate) * fminf(1.f, dt * 0.35f);
    I->cx += I->vx * dt; I->cy += I->vy * dt;
    if (I->cx < I->sc * 0.4f || I->cx > Wc - I->sc * 0.4f) I->vx = -I->vx;
    if (I->cy < I->sc * 0.4f || I->cy > Hc - I->sc * 0.4f) I->vy = -I->vy;
    float fo = I->tin + I->thold, env;
    if (I->age < I->tin) env = I->age / I->tin; else if (I->age < fo) env = 1; else env = 1 - (I->age - fo) / I->tout;
    if (I->age >= fo && !I->decided){
        I->decided = 1;
        if (!I->nomorph && rf() < 0.55f){ int oc = I->col; inst_setup(I, 1); if (rf() < 0.5f) I->col = oc; I->age = I->tin; I->rate = 0.8f; env = 1; }
    }
    if (env <= 0){ free(I->d); I->d = NULL; I->on = 0; return; }
    float w = env * env * (3 - 2 * env), cr = cosf(I->rot), sr = sinf(I->rot);
    float dmaxi = 2.f + I->dfrac * (DMAX - 2.f), k = 1.f - expf(-dt * I->rate * w), jit = 0.35f + (1 - w) * 1.6f;
    for (int i = 0; i < I->n; i++){
        float u = (i + 0.5f) / I->n, lx, ly, sz; pat(I->type, u, I->t, I->n, I->p, &lx, &ly, &sz);
        float tx = I->cx + (lx * cr - ly * sr) * I->sc, ty = I->cy + (lx * sr + ly * cr) * I->sc; Dot *q = &I->d[i];
        q->x += (tx - q->x) * k + (rf() - 0.5f) * jit; q->y += (ty - q->y) * k + (rf() - 0.5f) * jit;
        float d = 1.f + (dmaxi - 1.f) * w * sz * q->sf;
        int ci = (I->col + (I->span ? (int)(u * I->span) : 0)) % ncol;
        paint(q->x, q->y, d, ci);
    }
}

static void update_sparks(float dt){
    float rate = 0.12f * (0.25f + Eng) * clampf((T - 2) / 25.f, 0, 1);
    for (int i = 0; i < nsp; i++){
        Spark *s = &sp[i];
        if (!s->on){
            if (rf() < rate * dt){ s->on = 1; s->x = rr(0, Wc); s->y = rr(0, Hc); s->col = rnd() % ncol; s->age = 0; s->life = rr(2, 14);
                s->dmax = 1.6f + powf(rf(), 2.2f) * (DMAX * 0.6f - 1.6f); float a = rr(0, TAU), v = rr(0, 8); s->vx = cosf(a) * v; s->vy = sinf(a) * v; }
            continue;
        }
        s->age += dt; if (s->age > s->life){ s->on = 0; continue; }
        s->x += s->vx * dt + (rf() - 0.5f) * 0.9f; s->y += s->vy * dt + (rf() - 0.5f) * 0.9f;
        float d = 1.f + (s->dmax - 1.f) * powf(sinf(PI * s->age / s->life), 1.5f);
        paint(s->x, s->y, d, s->col);
    }
    /* dunkle, leicht farbige Hintergrundpunkte (meist rot), werden mit der Zeit mehr */
    int act = (int)(ntn * clampf((T - 15) / 150.f, 0, 1));
    for (int i = 0; i < act; i++){
        Tint *t = &tn[i];
        if (rf() < 6.f * dt){ t->x += rr(-2, 2); t->y += rr(-2, 2); if (t->x < 0 || t->x >= Wc || t->y < 0 || t->y >= Hc){ t->x = rr(0, Wc); t->y = rr(0, Hc); } }
        float b = t->br; splat(t->x, t->y, 1.f, (int)(pr[t->col] * b), (int)(pg[t->col] * b), (int)(pb[t->col] * b));
    }
}

static void world_resize(int nw, int nh){
    float sx = Wc ? (float)nw / Wc : 1, sy = Hc ? (float)nh / Hc : 1;
    free(fb); free(bg); fb = malloc((size_t)nw * nh * 4); bg = malloc((size_t)nw * nh);
    for (size_t i = 0; i < (size_t)nw * nh; i++){ uint32_t r = rnd(); bg[i] = (r & 255) < 26 ? ((r >> 8) & 0x3F) + 12 : 0; }
    if (Wc){
        for (int i = 0; i < MAXI; i++) if (inst[i].on){ Inst *I = &inst[i]; I->cx *= sx; I->cy *= sy; I->sc *= sy;
            for (int k = 0; k < I->n; k++){ I->d[k].x *= sx; I->d[k].y *= sy; } }
        for (int i = 0; i < MAXSP; i++){ sp[i].x *= sx; sp[i].y *= sy; }
        for (int i = 0; i < MAXTINT; i++){ tn[i].x *= sx; tn[i].y *= sy; }
    } else {
        for (int i = 0; i < MAXSP; i++){ sp[i].x = rr(0, nw); sp[i].y = rr(0, nh); }
        for (int i = 0; i < MAXTINT; i++){ tn[i].x = rr(0, nw); tn[i].y = rr(0, nh); tn[i].col = rf() < 0.7f ? 0 : rnd() % ncol; tn[i].br = rr(0.14f, 0.3f); }
    }
    Wc = nw; Hc = nh; DMAX = fmaxf(5.f, Wc / 100.f);
    nsp = (int)fminf(MAXSP, (float)Wc * Hc / 1500.f); ntn = (int)fminf(MAXTINT, (float)Wc * Hc / 2500.f);
}

static void frame_update(float dt){
    T += dt;
    ramp = clampf((T - 4) / 40.f, 0, 1);
    static float e1 = 0, e2 = 0; if (e1 == 0){ e1 = rr(0, TAU); e2 = rr(0, TAU); }
    Eng = clampf(0.5f + 0.38f * sinf(T * 0.066f + e1) + 0.22f * sinf(T * 0.171f + e2), 0, 1);
    /* Hintergrund-Rauschen: Anteil der Punkte wird staendig neu gewuerfelt (feines Gewusel, Punktgroesse bleibt) */
    size_t N = (size_t)Wc * Hc, cnt = (size_t)(N * clampf(13.f * dt, 0.02f, 0.6f));
    for (size_t i = 0; i < cnt; i++){ uint32_t r = rnd(); size_t idx = ((uint64_t)rnd() * N) >> 32; bg[idx] = (r & 255) < 26 ? ((r >> 8) & 0x3F) + 12 : 0; }
    for (size_t i = 0; i < N; i++) fb[i] = graytab[bg[i]];
    memset(M_, 0, sizeof M_); memset(SX, 0, sizeof SX); memset(SY, 0, sizeof SY); memset(B3, 0, sizeof B3);
    /* Zeitplan: wie viele Muster gleichzeitig? */
    static float sched = 0; sched += dt;
    if (sched > 0.5f){ sched = 0;
        int target = (int)floorf(Eng * ramp * 6.5f), c = count_inst();
        if (c < target && rf() < 0.6f){ for (int i = 0; i < MAXI; i++) if (!inst[i].on){ inst_setup(&inst[i], 0); break; } }
        else if (c > target && rf() < 0.5f){ int best = -1; float ba = 0;
            for (int i = 0; i < MAXI; i++) if (inst[i].on && inst[i].age > inst[i].tin && inst[i].age < inst[i].tin + inst[i].thold && inst[i].age > ba){ ba = inst[i].age; best = i; }
            if (best >= 0){ inst[best].age = inst[best].tin + inst[best].thold; inst[best].nomorph = 1; } }
    }
    update_sparks(dt);
    for (int i = 0; i < MAXI; i++) if (inst[i].on) update_inst(&inst[i], dt);
    for (int c = 0; c < ncol; c++){
        float a = 1.f - expf(-M_[c] / 30.f), k = fminf(1.f, dt * 6.f);
        stats[c].act += (a - stats[c].act) * k;
        if (M_[c] > 0.5f){ stats[c].x += (SX[c] / M_[c] / Wc - stats[c].x) * fminf(1.f, dt * 4); stats[c].y += (SY[c] / M_[c] / Hc - stats[c].y) * fminf(1.f, dt * 4); stats[c].br = B3[c] / M_[c]; }
    }
}

/* ------------------------------------------------------------ Audio */
#define MAXFONTS 8
static tsf *fonts[MAXFONTS]; static int nfonts = 0;
typedef struct { int font, chan, idx, drum, oct, mask, off, cur; } Voice;
static Voice voice[MAXCOL];
static int a_rate = 48000, a_step = 0, a_scale[12], a_slen, a_root = 52; static double a_next = 0, a_stepLen;
static int a_on = 0;
static const int SCALES[7][8] = {{5,0,2,4,7,9,0,0},{5,0,3,5,7,10,0,0},{7,0,2,3,5,7,9,10},{7,0,2,4,5,7,9,10},{7,0,2,4,6,7,9,11},{7,0,2,3,5,7,8,10},{7,0,2,3,5,7,8,11}};
static const int DK[9] = {36, 38, 42, 42, 46, 45, 49, 39, 50};

static void do_step(void){
    a_step++;
    if (a_step % 16 == 0 && (a_step / 16) % 12 == 0){ static const int sh[7] = {0, 5, 7, -5, -7, 2, -2}; a_root += sh[arnd() % 7]; if (a_root > 58) a_root -= 12; if (a_root < 45) a_root += 12; }
    for (int c = 0; c < ncol; c++){
        Voice *V = &voice[c]; if (!V->font && !fonts[0]) continue; if (V->idx < 0) continue; tsf *f = fonts[V->font];
        if (V->cur >= 0 && a_step >= V->off){ tsf_channel_note_off(f, V->chan, V->cur); V->cur = -1; }
        float act = stats[c].act; if (act < 0.05f) continue;
        if (!((V->mask >> (a_step & 15)) & 1)) continue;
        if (arf() > 0.25f + act * 0.75f) continue;
        int key;
        if (V->drum) key = DK[(arnd() ^ (unsigned)(stats[c].y * 8)) % 9];
        else { int deg = (int)((1.f - clampf(stats[c].y, 0, 1)) * a_slen * 2.4f) + (int)(arnd() % 3) - 1; if (deg < 0) deg = 0;
            key = a_root + V->oct + a_scale[deg % a_slen] + 12 * (deg / a_slen); while (key > 100) key -= 12; }
        if (V->cur >= 0){ tsf_channel_note_off(f, V->chan, V->cur); V->cur = -1; }
        tsf_channel_set_pan(f, V->chan, clampf(stats[c].x, 0, 1));
        tsf_channel_note_on(f, V->chan, key, 0.3f + 0.6f * clampf(stats[c].br, 0, 1));
        V->cur = key; V->off = a_step + (V->drum ? 1 : 1 + (int)(arnd() % 4));
    }
}
static void audio_cb(void *ud, Uint8 *stream, int len){
    (void)ud; float *out = (float *)stream; int frames = len / 8, pos = 0; memset(out, 0, len);
    while (pos < frames){
        if (a_next <= 0){ do_step(); a_next += a_stepLen; }
        int n = (int)ceil(a_next); if (n > frames - pos) n = frames - pos; if (n < 1) n = 1;
        for (int i = 0; i < nfonts; i++) tsf_render_float(fonts[i], out + pos * 2, n, 1);
        a_next -= n; pos += n;
    }
    for (int i = 0; i < frames * 2; i++) out[i] = tanhf(out[i] * 0.9f);
}
static int scan_sf2(const char *dir){
    DIR *d = opendir(dir); if (!d) return 0; struct dirent *e; int found = 0;
    while ((e = readdir(d)) && nfonts < MAXFONTS){
        size_t l = strlen(e->d_name); if (l < 5 || strcasecmp(e->d_name + l - 4, ".sf2")) continue;
        char path[PATH_MAX]; snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        tsf *f = tsf_load_filename(path); if (!f) continue;
        tsf_set_output(f, TSF_STEREO_INTERLEAVED, a_rate, -4.f); tsf_set_max_voices(f, 64);
        fonts[nfonts++] = f; found++;
    }
    closedir(d); return found;
}
static SDL_AudioDeviceID audio_start(void){
    char dir[PATH_MAX] = "."; const char *ai = getenv("APPIMAGE"); char tmp[PATH_MAX];
    int found = 0;
    if (ai){ strncpy(tmp, ai, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0; found = scan_sf2(dirname(tmp)); }
    if (!found){ ssize_t n = readlink("/proc/self/exe", tmp, sizeof tmp - 1); if (n > 0){ tmp[n] = 0; strncpy(dir, dirname(tmp), sizeof dir - 1); found = scan_sf2(dir); } }
    if (!found) found = scan_sf2(".");
    if (!nfonts){ fprintf(stderr, "Keine .sf2 gefunden -> kein Ton.\n"); return 0; }
    /* melodische und Drum-Presets sammeln */
    int mf[4096], mi[4096], nm = 0, df[512], di[512], nd = 0;
    for (int fi = 0; fi < nfonts; fi++) for (int i = 0; i < tsf_get_presetcount(fonts[fi]); i++){
        struct tsf_preset *p = &fonts[fi]->presets[i];
        if (p->bank == 128){ if (nd < 512){ df[nd] = fi; di[nd++] = i; } }
        else if (!(p->bank == 0 && p->preset >= 120) && nm < 4096){ mf[nm] = fi; mi[nm++] = i; }
    }
    if (!nm){ fprintf(stderr, "SF2 ohne melodische Instrumente.\n"); return 0; }
    as_ ^= (uint64_t)rnd() << 20 ^ rnd();
    int sc = arnd() % 7; a_slen = SCALES[sc][0]; for (int i = 0; i < a_slen; i++) a_scale[i] = SCALES[sc][1 + i];
    a_root = 46 + arnd() % 10; double bpm = 62 + arf() * 56; a_stepLen = a_rate * 60.0 / bpm / 2.0;
    static const int OCT[4] = {-12, 0, 0, 12};
    for (int c = 0; c < ncol; c++){
        Voice *V = &voice[c]; V->chan = c; V->cur = -1; V->oct = OCT[arnd() % 4];
        V->drum = (nd && ncol >= 6 && arf() < 0.15f);
        if (V->drum){ int k = arnd() % nd; V->font = df[k]; V->idx = di[k]; V->oct = 0; }
        else { int k = arnd() % nm; V->font = mf[k]; V->idx = mi[k]; }
        tsf *f = fonts[V->font]; tsf_channel_set_presetindex(f, c, V->idx); tsf_channel_set_volume(f, c, V->drum ? 0.6f : 0.5f + 0.4f * arf());
        float dens = 0.2f + 0.4f * arf(); V->mask = 0; for (int b = 0; b < 16; b++) if (arf() < dens) V->mask |= 1 << b;
        if (!V->mask) V->mask = 0x0101;
    }
    SDL_AudioSpec want, have; SDL_zero(want); want.freq = a_rate; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev){ fprintf(stderr, "Audio nicht verfuegbar: %s\n", SDL_GetError()); return 0; }
    fprintf(stderr, "Audio: %s, %d SF2, %d Farben, %.0f BPM\n", SDL_GetCurrentAudioDriver(), nfonts, ncol, bpm);
    a_on = 1; SDL_PauseAudioDevice(dev, 0); return dev;
}

/* ------------------------------------------------------------ main */
int main(int argc, char **argv){
    int fullscreen = 0, ncfg = 0; uint64_t seed = (uint64_t)time(NULL) * 6364136223846793005ull ^ (uint64_t)getpid() << 17;
    int dump = 0; const char *dumpf = NULL; float warp = 0;
    for (int i = 1; i < argc; i++){
        if (!strcmp(argv[i], "-c") && i + 1 < argc) cell = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) ncfg = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) seed = strtoull(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "-f")) fullscreen = 1;
        else if (!strcmp(argv[i], "--dump") && i + 2 < argc){ dumpf = argv[++i]; dump = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--warp") && i + 1 < argc) warp = atof(argv[++i]);
        else { fprintf(stderr, "Nutzung: %s [-c Zellgroesse] [-n Farben 5..35] [-s Seed] [-f]\n", argv[0]); return 1; }
    }
    int fixedcell = cell > 0 && cell != 1 ? cell : 0; if (cell == 1) fixedcell = 1;
    rs ^= seed; for (int i = 0; i < 16; i++) rnd();
    make_palette(ncfg >= 5 && ncfg <= 35 ? ncfg : 5 + rnd() % 31);
    for (int i = 0; i < 256; i++) graytab[i] = 0xFF000000u | (uint32_t)i * 0x010101u;
    if (!getenv("SDL_VIDEODRIVER")) SDL_SetHint(SDL_HINT_VIDEODRIVER, "wayland,x11");
    SDL_SetHint(SDL_HINT_AUDIODRIVER, "pipewire,pulseaudio,alsa");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0){ fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("RandomNoise", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win){ fprintf(stderr, "Fenster: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren){ fprintf(stderr, "Renderer: %s\n", SDL_GetError()); return 1; }
    SDL_Texture *tex = NULL; int ow = 0, oh = 0; SDL_ShowCursor(fullscreen ? SDL_DISABLE : SDL_ENABLE);
    float tw = warp; SDL_AudioDeviceID adev = audio_start(); (void)adev; T = warp; (void)tw;
    if (warp > 0) for (int i = 0; i < 1; i++) {}
    uint64_t last = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency(); int running = 1, frames = 0;
    while (running){
        SDL_Event e;
        while (SDL_PollEvent(&e)){
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN){
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                else if (e.key.keysym.sym == SDLK_f || e.key.keysym.sym == SDLK_F11){
                    int fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP); SDL_ShowCursor(fs ? SDL_ENABLE : SDL_DISABLE);
                }
            }
        }
        int pw, ph; SDL_GetRendererOutputSize(ren, &pw, &ph);
        if (pw != ow || ph != oh || !tex){
            ow = pw; oh = ph; long px = (long)pw * ph;
            cell = fixedcell ? fixedcell : (px <= 1300000 ? 1 : (px <= 4500000 ? 2 : 3));
            int nw = pw / cell, nh = ph / cell; if (nw < 64) nw = 64; if (nh < 64) nh = 64;
            if (tex) SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, nw, nh);
            world_resize(nw, nh);
        }
        uint64_t now = SDL_GetPerformanceCounter(); float dt = (float)((now - last) / (double)freq); last = now;
        if (dump) dt = 1.f / 60.f; if (dt > 0.1f) dt = 0.1f;
        frame_update(dt);
        SDL_UpdateTexture(tex, NULL, fb, Wc * 4); SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, NULL, NULL); SDL_RenderPresent(ren);
        if (dump && ++frames >= dump){
            FILE *fp = fopen(dumpf, "wb"); fprintf(fp, "P6\n%d %d\n255\n", Wc, Hc);
            for (int i = 0; i < Wc * Hc; i++){ unsigned char px[3] = {(fb[i] >> 16) & 255, (fb[i] >> 8) & 255, fb[i] & 255}; fwrite(px, 1, 3, fp); }
            fclose(fp); break;
        }
        if (dump) continue;
    }
    if (adev) SDL_CloseAudioDevice(adev);
    SDL_Quit(); return 0;
}
