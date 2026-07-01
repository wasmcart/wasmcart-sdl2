/*
 * SDL_wasmcart_audio.h — Header for wasmcart audio backend
 */

#ifndef SDL_wasmcart_audio_h_
#define SDL_wasmcart_audio_h_

#include <stdint.h>

/*
 * Deprecated — no-op. Kept for backwards compatibility.
 */
extern void wasmcart_audio_set_host_rate(int rate);

/*
 * Pump audio: invoke SDL audio callback and write output to wasmcart ring buffer.
 * Called from wc_render() each frame.
 *
 * The callback's native format is written directly to the ring buffer with NO
 * conversion or resampling. Mono is expanded to stereo. The host reads format
 * and sample rate from wc_info and adapts.
 *
 * ring      — pointer to the stereo ring buffer (float[] or int16_t[])
 * cap       — ring buffer capacity in frames (not samples)
 * write_cur — pointer to the write cursor (frame index)
 * rate      — audio sample rate the callback produces at
 * delta_ms  — milliseconds elapsed since last frame
 */
extern void wasmcart_audio_pump(void *ring, uint32_t cap, uint32_t *write_cur,
                                 int rate, uint32_t delta_ms);

/*
 * Query the actual audio device spec after SDL_Init / Mix_OpenAudio.
 * Use this to set wc_info.audio_sample_rate and wc_info.flags correctly.
 * Any parameter may be NULL if not needed.
 *
 * rate      — callback sample rate (e.g. 44100, 22050)
 * channels  — callback channel count (1=mono, 2=stereo)
 * is_float  — 1 if callback format is float, 0 if integer (S16)
 */
extern void wasmcart_audio_get_spec(int *rate, int *channels, int *is_float);

#endif
