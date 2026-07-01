/*
 * SDL_wasmcart_audio.c — SDL2 audio backend for wasmcart
 *
 * Uses ProvidesOwnCallbackThread = SDL_TRUE (no real thread).
 * Exposes wasmcart_audio_pump() which the cart entry point calls each frame
 * to invoke the SDL audio callback and write the result to the ring buffer.
 *
 * Philosophy: the cart writes audio in whatever format/rate the game naturally
 * produces. The host adapts (native code resampling is essentially free).
 * This backend does NO format conversion or resampling — it just bridges the
 * SDL callback to the wasmcart ring buffer.
 *
 * REUSABLE: This backend works for any SDL2 game targeting wasmcart.
 */

#include "../../SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_WASMCART

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "SDL_wasmcartaudio.h"

/*---------------------------------------------------------------------------*/
/* State */

static SDL_AudioDevice *wc_audio_device = NULL;

/* Leftover buffer: callback produces fixed-size chunks, but we may need
 * fewer bytes per frame. Leftovers carry across pump calls. */
static uint8_t leftover_buf[32768];
static int leftover_bytes = 0;

/* Backwards compat — no-op, rate is now declared in wc_info.audio_sample_rate */
void wasmcart_audio_set_host_rate(int rate)
{
    (void)rate;
}

/*---------------------------------------------------------------------------*/
/* Backend implementation */

static int WASMCART_OpenDevice(_THIS, const char *devname)
{
    (void)devname;
    _this->hidden = (struct SDL_PrivateAudioData *)0x1; /* non-NULL sentinel */

    /* Accept whatever format the game requests — no conversion.
     * The ring buffer will contain samples in this exact format.
     * The host reads the format/rate from wc_info and adapts. */

    wc_audio_device = _this;
    leftover_bytes = 0;

    return 0;
}

static void WASMCART_CloseDevice(_THIS)
{
    if (wc_audio_device == _this)
        wc_audio_device = NULL;
}

/* No-op lock/unlock since we're single-threaded */
static void WASMCART_LockOrUnlock(_THIS)
{
}

static SDL_bool WASMCART_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = WASMCART_OpenDevice;
    impl->CloseDevice = WASMCART_CloseDevice;
    impl->LockDevice = WASMCART_LockOrUnlock;
    impl->UnlockDevice = WASMCART_LockOrUnlock;

    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->ProvidesOwnCallbackThread = SDL_TRUE;

    return SDL_TRUE;
}

AudioBootStrap WASMCARTAUDIO_bootstrap = {
    "wasmcart", "SDL wasmcart audio driver", WASMCART_Init, SDL_FALSE
};

/*---------------------------------------------------------------------------*/
/* Audio pump — called from cart entry point each frame                      */
/*                                                                           */
/* Writes the callback's native format directly to the ring buffer.          */
/* The ring buffer sample type must match what the callback produces.        */
/* For F32 carts: open Mix_OpenAudio with AUDIO_F32SYS, ring is float[].    */
/* For S16 carts: open with AUDIO_S16SYS, ring is int16_t[].                */
/*                                                                           */
/* Mono callbacks are expanded to stereo by duplicating each sample.         */

void wasmcart_audio_pump(void *ring, uint32_t cap, uint32_t *write_cur, int rate, uint32_t delta_ms)
{
    SDL_AudioDevice *dev = wc_audio_device;

    if (!dev || SDL_AtomicGet(&dev->paused) || !SDL_AtomicGet(&dev->enabled))
        return;

    SDL_AudioCallback callback = dev->callbackspec.callback;
    void *userdata = dev->callbackspec.userdata;

    if (!callback) return;

    /* Callback format info */
    int cb_channels = dev->callbackspec.channels;
    int cb_sample_size = SDL_AUDIO_BITSIZE(dev->callbackspec.format) / 8;
    int cb_frame_size = cb_channels * cb_sample_size; /* bytes per frame from callback */
    int callback_size = dev->callbackspec.size;       /* bytes per callback invocation */
    uint8_t *work = dev->work_buffer;

    /* Ring buffer frame size: always stereo */
    int ring_sample_size = cb_sample_size;
    int ring_frame_size = 2 * ring_sample_size; /* stereo */

    /* How many frames to produce for this time slice */
    int frames_needed = (int)((uint64_t)rate * delta_ms / 1000);
    if (frames_needed <= 0) return;
    if (frames_needed > 8192) frames_needed = 8192;

    int bytes_needed = frames_needed * cb_frame_size;
    uint32_t wc = *write_cur;

    /* Serve from leftover first */
    int from_left = leftover_bytes < bytes_needed ? leftover_bytes : bytes_needed;
    if (from_left > 0) {
        uint8_t *src = leftover_buf;
        int frames_from_left = from_left / cb_frame_size;

        if (cb_channels == 1) {
            /* Mono → stereo: duplicate each sample */
            for (int i = 0; i < frames_from_left; i++) {
                uint32_t idx = (wc % cap) * 2;
                SDL_memcpy((uint8_t *)ring + idx * ring_sample_size,
                           src + i * cb_sample_size, cb_sample_size);
                SDL_memcpy((uint8_t *)ring + (idx + 1) * ring_sample_size,
                           src + i * cb_sample_size, cb_sample_size);
                wc++;
            }
        } else {
            /* Stereo: copy frame directly */
            for (int i = 0; i < frames_from_left; i++) {
                uint32_t idx = (wc % cap) * 2;
                SDL_memcpy((uint8_t *)ring + idx * ring_sample_size,
                           src + i * cb_frame_size, cb_frame_size);
                wc++;
            }
        }

        /* Shift leftover */
        leftover_bytes -= from_left;
        if (leftover_bytes > 0)
            SDL_memmove(leftover_buf, leftover_buf + from_left, leftover_bytes);

        bytes_needed -= from_left;
    }

    /* Generate more from callback as needed */
    while (bytes_needed > 0) {
        SDL_memset(work, dev->callbackspec.silence, callback_size);
        callback(userdata, work, callback_size);

        int consume = bytes_needed < callback_size ? bytes_needed : callback_size;
        int frames_consume = consume / cb_frame_size;

        if (cb_channels == 1) {
            for (int i = 0; i < frames_consume; i++) {
                uint32_t idx = (wc % cap) * 2;
                SDL_memcpy((uint8_t *)ring + idx * ring_sample_size,
                           work + i * cb_sample_size, cb_sample_size);
                SDL_memcpy((uint8_t *)ring + (idx + 1) * ring_sample_size,
                           work + i * cb_sample_size, cb_sample_size);
                wc++;
            }
        } else {
            for (int i = 0; i < frames_consume; i++) {
                uint32_t idx = (wc % cap) * 2;
                SDL_memcpy((uint8_t *)ring + idx * ring_sample_size,
                           work + i * cb_frame_size, cb_frame_size);
                wc++;
            }
        }

        /* Save leftover */
        int leftover = callback_size - consume;
        if (leftover > 0 && leftover <= (int)sizeof(leftover_buf)) {
            SDL_memcpy(leftover_buf, work + consume, leftover);
            leftover_bytes = leftover;
        }

        bytes_needed -= consume;
    }

    *write_cur = wc;
}

/*---------------------------------------------------------------------------*/
/* Query the actual audio device spec after SDL_Init / Mix_OpenAudio.        */
/* Cart calls this to set wc_info.audio_sample_rate and flags correctly.     */

void wasmcart_audio_get_spec(int *rate, int *channels, int *is_float)
{
    SDL_AudioDevice *dev = wc_audio_device;
    if (dev) {
        if (rate)     *rate     = dev->callbackspec.freq;
        if (channels) *channels = dev->callbackspec.channels;
        if (is_float) *is_float = SDL_AUDIO_ISFLOAT(dev->callbackspec.format);
    } else {
        if (rate)     *rate     = 0;
        if (channels) *channels = 0;
        if (is_float) *is_float = 0;
    }
}

#endif /* SDL_AUDIO_DRIVER_WASMCART */
