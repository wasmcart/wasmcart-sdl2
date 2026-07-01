/*
 * SDL_wasmcartaudio.h — SDL2 wasmcart audio backend header
 */
#include "../../SDL_internal.h"

#ifndef SDL_wasmcartaudio_h_
#define SDL_wasmcartaudio_h_

#include "../SDL_sysaudio.h"

/* Hidden "this" pointer for the audio functions */
#define _THIS SDL_AudioDevice *_this

struct SDL_PrivateAudioData
{
    int unused;
};

#endif /* SDL_wasmcartaudio_h_ */
