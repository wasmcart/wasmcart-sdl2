/*
 * audio_bridge.h - Bridge between SDL2_mixer and the wasmcart audio ring buffer
 *
 * This is a reusable library for porting SDL2_mixer-based games to wasmcart.
 * It intercepts SDL_OpenAudioDevice via linker --wrap flags to capture the
 * mixer callback, then provides audio_bridge_pump() to manually invoke the
 * mixer each frame and write resampled output to the wasmcart ring buffer.
 *
 * USAGE:
 *   1. Include this header in your cart source
 *   2. Compile audio_bridge.c alongside your cart
 *   3. Add --wrap linker flags to your build.sh (see below)
 *   4. Call audio_bridge_init() after Mix_OpenAudio()
 *   5. Call audio_bridge_pump() every frame in wc_render()
 *
 * REQUIRED LINKER FLAGS:
 *   -Wl,--wrap=SDL_OpenAudioDevice
 *   -Wl,--wrap=SDL_CloseAudioDevice
 *   -Wl,--wrap=SDL_LockAudioDevice
 *   -Wl,--wrap=SDL_UnlockAudioDevice
 *   -Wl,--wrap=SDL_PauseAudioDevice
 *   -Wl,--wrap=SDL_OpenAudio
 *   -Wl,--wrap=SDL_CloseAudio
 *   -Wl,--wrap=SDL_LockAudio
 *   -Wl,--wrap=SDL_UnlockAudio
 *   -Wl,--wrap=SDL_PauseAudio
 *
 * HOW IT WORKS:
 *   - SDL_mixer calls SDL_OpenAudioDevice internally. Our __wrap version
 *     intercepts this, stores the mixer callback, and returns a fake device ID.
 *   - No real audio device or thread is created.
 *   - Each frame, audio_bridge_pump() invokes the stored mixer callback to
 *     generate mixed audio, then resamples it from the mixer's native format
 *     (typically 22050Hz S16 mono) to the wasmcart ring buffer format
 *     (48000Hz F32 stereo) using fixed-point linear interpolation.
 *   - The resampler maintains fractional state between calls for seamless output.
 */

#ifndef AUDIO_BRIDGE_H
#define AUDIO_BRIDGE_H

#include <stdint.h>

/*
 * Initialize the audio bridge. Call once after Mix_OpenAudio() succeeds.
 * Resets the resampler state (fractional position, previous sample).
 */
void audio_bridge_init(void);

/*
 * Pump the SDL_mixer and write resampled audio to the wasmcart ring buffer.
 * Call this every frame in wc_render(), even on skip frames (for smooth audio).
 *
 * Parameters:
 *   ring           - pointer to the ring buffer (float[cap*2], stereo interleaved)
 *   cap            - ring buffer capacity in stereo frames
 *   write_cur      - pointer to the write cursor (updated by this function)
 *   samples_needed - how many 48000Hz stereo frames to produce
 *                    (typically 800 for 60fps: 48000/60 = 800)
 *
 * Example:
 *   #define AUDIO_CAP 4096
 *   static float audio_ring[AUDIO_CAP * 2];
 *   static uint32_t audio_write_cursor;
 *
 *   // In wc_render():
 *   audio_bridge_pump(audio_ring, AUDIO_CAP, &audio_write_cursor, 800);
 */
void audio_bridge_pump(float *ring, uint32_t cap, uint32_t *write_cur,
                       int samples_needed);

#endif /* AUDIO_BRIDGE_H */
