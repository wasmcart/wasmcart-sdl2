/*
 * audio_bridge.c - Bridge between SDL2_mixer and the wasmcart audio ring buffer
 *
 * Reusable library for porting SDL2_mixer games to wasmcart.
 * See audio_bridge.h for usage instructions.
 *
 * Strategy:
 *   1. Override SDL_OpenAudioDevice (via --wrap) to capture the mixer callback
 *      without creating a real audio device or thread.
 *   2. Each frame, manually invoke the mixer callback to generate mixed audio
 *      in the format SDL_mixer produces (typically 22050Hz, S16, mono).
 *   3. Resample from source rate to 48000Hz stereo and write to the
 *      wasmcart ring buffer.
 *
 * The game calls Mix_OpenAudio(freq, format, channels, chunksize) which internally
 * calls SDL_OpenAudioDevice. Our __wrap version intercepts this.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <string.h>
#include <stdint.h>

#include "audio_bridge.h"

/* --- Stored mixer callback info --- */

static SDL_AudioCallback mixer_callback = NULL;
static void *mixer_userdata = NULL;
static SDL_AudioSpec mixer_spec;
static int fake_audio_opened = 0;

/* --- Mixer output buffer --- */
/* We ask the mixer for 1024 mono S16 samples per pump.
 * mixer_spec.size will be 1024 * channels * bytes_per_sample.
 */
#define MIXER_SAMPLES 1024
static int16_t mixer_buffer[MIXER_SAMPLES];

/* --- Resampling state --- */
/* Linear interpolation resampler from source rate (e.g. 22050) to 48000Hz stereo.
 * Uses fixed-point (16.16) for the fractional position.
 */
#define DST_RATE 48000

static uint32_t resample_step = 0;  /* (SRC_RATE << 16) / DST_RATE, set on open */
static uint32_t resample_frac = 0;  /* fractional position into source buffer (16.16) */
static int16_t  resample_prev = 0;  /* last sample from previous buffer for interpolation */

/* --- SDL audio device function overrides (linked via --wrap) --- */

SDL_AudioDeviceID __wrap_SDL_OpenAudioDevice(
    const char *device, int iscapture,
    const SDL_AudioSpec *desired, SDL_AudioSpec *obtained,
    int allowed_changes)
{
    (void)device; (void)allowed_changes;
    if (iscapture) return 0;

    /* Store the callback - this is SDL_mixer's mix_channels function */
    mixer_callback = desired->callback;
    mixer_userdata = desired->userdata;

    /* Copy desired spec to obtained. SDL_mixer's Mix_OpenAudioDevice
     * reads the obtained spec to know the actual format. We keep the
     * requested format exactly as-is. */
    if (obtained) {
        memcpy(obtained, desired, sizeof(SDL_AudioSpec));
        /* Calculate buffer size in bytes: samples * channels * bytes_per_sample */
        obtained->size = obtained->samples * obtained->channels *
                         (SDL_AUDIO_BITSIZE(obtained->format) / 8);
    }

    /* Also save a copy for our use */
    memcpy(&mixer_spec, desired, sizeof(SDL_AudioSpec));
    mixer_spec.size = mixer_spec.samples * mixer_spec.channels *
                      (SDL_AUDIO_BITSIZE(mixer_spec.format) / 8);

    /* Calculate resample step based on actual source rate */
    uint32_t src_rate = desired->freq;
    resample_step = (uint32_t)((uint64_t)src_rate * 65536 / DST_RATE);

    fake_audio_opened = 1;
    return 1; /* fake device ID (must be non-zero for success) */
}

void __wrap_SDL_CloseAudioDevice(SDL_AudioDeviceID dev) {
    (void)dev;
    fake_audio_opened = 0;
    mixer_callback = NULL;
}

/* No-ops: single-threaded, no real audio device */
void __wrap_SDL_LockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
void __wrap_SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
void __wrap_SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) { (void)dev; (void)pause_on; }

/* SDL1-compat wrappers (SDL_mixer calls some of these) */
int __wrap_SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
    SDL_AudioDeviceID dev = __wrap_SDL_OpenAudioDevice(NULL, 0, desired, obtained, 0);
    return (dev > 0) ? 0 : -1;
}
void __wrap_SDL_CloseAudio(void) { __wrap_SDL_CloseAudioDevice(1); }
void __wrap_SDL_LockAudio(void) {}
void __wrap_SDL_UnlockAudio(void) {}
void __wrap_SDL_PauseAudio(int pause_on) { (void)pause_on; }

/* --- Audio pump: called from wc_render each frame --- */

void audio_bridge_pump(float *ring, uint32_t cap, uint32_t *write_cur,
                       int samples_needed)
{
    if (!mixer_callback || !fake_audio_opened || samples_needed <= 0) return;

    uint32_t wr = *write_cur;

    while (samples_needed > 0) {
        /* Ask the mixer to fill our mono S16 buffer */
        memset(mixer_buffer, 0, sizeof(mixer_buffer));
        mixer_callback(mixer_userdata, (Uint8 *)mixer_buffer, MIXER_SAMPLES * sizeof(int16_t));

        /* Resample this buffer from source rate mono to 48000Hz stereo using
         * linear interpolation. resample_frac carries over between calls
         * for seamless output. */
        int src_idx = 0;
        int16_t prev = resample_prev;

        while (src_idx < MIXER_SAMPLES && samples_needed > 0) {
            /* Current integer source index from the fractional position */
            int si = (int)(resample_frac >> 16);

            /* Advance source if fractional position has moved past current */
            while (src_idx < si && src_idx < MIXER_SAMPLES) {
                prev = mixer_buffer[src_idx];
                src_idx++;
            }

            if (si >= MIXER_SAMPLES) break;

            /* Linear interpolation between prev and mixer_buffer[si] */
            int frac = resample_frac & 0xFFFF;
            int16_t cur = mixer_buffer[si];
            int32_t sample = prev + (((int32_t)(cur - prev) * frac) >> 16);

            /* Clamp */
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;

            /* Write stereo float [-1.0, 1.0] (duplicate mono to both channels) */
            float fsample = (float)sample / 32768.0f;
            uint32_t ring_idx = (wr % cap) * 2;
            ring[ring_idx]     = fsample;
            ring[ring_idx + 1] = fsample;
            wr++;
            samples_needed--;

            /* Advance fractional position by one output sample step */
            resample_frac += resample_step;

            /* Update source index */
            int new_si = (int)(resample_frac >> 16);
            while (src_idx < new_si && src_idx < MIXER_SAMPLES) {
                prev = mixer_buffer[src_idx];
                src_idx++;
            }
        }

        /* After consuming this mixer buffer, subtract MIXER_SAMPLES from
         * the fractional position (we've consumed those source samples) */
        resample_frac -= ((uint32_t)MIXER_SAMPLES << 16);

        /* Remember last sample for next buffer's interpolation */
        resample_prev = (src_idx > 0) ? mixer_buffer[src_idx - 1] :
                        (MIXER_SAMPLES > 0 ? mixer_buffer[MIXER_SAMPLES - 1] : prev);
    }

    *write_cur = wr;
}

void audio_bridge_init(void) {
    resample_frac = 0;
    resample_prev = 0;
}
