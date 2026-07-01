/*
 * wc_sdl_stubs.h - Minimal SDL2 type definitions for wasmcart ports
 *
 * When porting an SDL2 game to wasmcart, the game code may reference SDL
 * types (Uint8, SDL_Rect, SDL_Color) throughout its codebase even if the
 * actual SDL rendering/audio/input code has been replaced with wasmcart
 * equivalents.
 *
 * This header provides just the type definitions and no-op function stubs
 * needed to compile the game without linking actual SDL2.
 *
 * USAGE:
 *   Option A - Replace SDL.h entirely:
 *     Copy or symlink this file as "SDL.h" in your include path so
 *     #include "SDL.h" picks it up instead of the real SDL2 header.
 *
 *   Option B - Include alongside a compat header:
 *     #include "wc_sdl_stubs.h"
 *
 * WHAT'S INCLUDED:
 *   - Integer types: Uint8, Uint16, Uint32, Uint64, Sint8/16/32/64
 *   - SDL_Rect, SDL_Color, SDL_Point
 *   - SDL_bool
 *   - SDL_Init, SDL_Quit, SDL_GetTicks, SDL_Delay → no-op stubs
 *   - SDL_GetError → returns ""
 *   - SDL_mutex/SDL_cond → opaque void* stubs
 *   - Byte order macros
 *   - SDL version macros
 *
 * WHAT'S NOT INCLUDED:
 *   - SDL_Surface, SDL_Renderer, SDL_Window, SDL_Texture
 *     (you shouldn't be using these — replace with wasmcart framebuffer)
 *   - SDL_Event, SDL_PollEvent
 *     (replace with wasmcart pad input)
 *   - SDL audio functions
 *     (use wc_pcm_mixer.h or audio_bridge.h instead)
 */

#ifndef WC_SDL_STUBS_H
#define WC_SDL_STUBS_H

/* Prevent real SDL headers from being included */
#define _SDL_H
#define SDL_h_
#define SDL2_SDL_h_

#include <stdint.h>
#include <stddef.h>

/* ── Integer types ────────────────────────────────────────────────── */

typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int8_t   Sint8;
typedef int16_t  Sint16;
typedef int32_t  Sint32;
typedef int64_t  Sint64;

/* ── SDL_bool ─────────────────────────────────────────────────────── */

typedef int SDL_bool;
#define SDL_TRUE  1
#define SDL_FALSE 0

/* ── Basic structs ────────────────────────────────────────────────── */

typedef struct SDL_Rect {
    int x, y, w, h;
} SDL_Rect;

typedef struct SDL_Color {
    Uint8 r, g, b, a;
} SDL_Color;

typedef struct SDL_Point {
    int x, y;
} SDL_Point;

/* ── Opaque types (used as pointers only) ─────────────────────────── */

typedef void SDL_mutex;
typedef void SDL_cond;

/* ── SDL_Init flags ───────────────────────────────────────────────── */

#define SDL_INIT_TIMER          0x00000001
#define SDL_INIT_AUDIO          0x00000010
#define SDL_INIT_VIDEO          0x00000020
#define SDL_INIT_JOYSTICK       0x00000200
#define SDL_INIT_HAPTIC         0x00001000
#define SDL_INIT_GAMECONTROLLER 0x00002000
#define SDL_INIT_EVENTS         0x00004000
#define SDL_INIT_EVERYTHING     0x0000FFFF

/* ── Function stubs ───────────────────────────────────────────────── */

static inline int SDL_Init(Uint32 flags) { (void)flags; return 0; }
static inline void SDL_Quit(void) {}
static inline Uint32 SDL_GetTicks(void) { return 0; }
static inline Uint64 SDL_GetTicks64(void) { return 0; }
static inline Uint64 SDL_GetPerformanceCounter(void) { return 0; }
static inline Uint64 SDL_GetPerformanceFrequency(void) { return 1; }
static inline void SDL_Delay(Uint32 ms) { (void)ms; }
static inline const char *SDL_GetError(void) { return ""; }
static inline const char *SDL_GetPlatform(void) { return "wasmcart"; }

/* ── Byte order ───────────────────────────────────────────────────── */

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#ifdef __BYTE_ORDER__
  #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    #define SDL_BYTEORDER SDL_BIG_ENDIAN
  #else
    #define SDL_BYTEORDER SDL_LIL_ENDIAN
  #endif
#else
  #define SDL_BYTEORDER SDL_LIL_ENDIAN
#endif

/* ── Version macros ───────────────────────────────────────────────── */

#define SDL_VERSION_ATLEAST(x, y, z) 0
#define SDL_COMPILEDVERSION 0

/* ── Utility macros ───────────────────────────────────────────────── */

#define SDL_arraysize(a) (sizeof(a) / sizeof(a[0]))

/* ── Thread stubs ─────────────────────────────────────────────────── */

static inline SDL_mutex *SDL_CreateMutex(void) { return (SDL_mutex*)1; }
static inline void SDL_DestroyMutex(SDL_mutex *m) { (void)m; }
static inline int SDL_LockMutex(SDL_mutex *m) { (void)m; return 0; }
static inline int SDL_UnlockMutex(SDL_mutex *m) { (void)m; return 0; }

static inline SDL_cond *SDL_CreateCond(void) { return (SDL_cond*)1; }
static inline void SDL_DestroyCond(SDL_cond *c) { (void)c; }
static inline int SDL_CondSignal(SDL_cond *c) { (void)c; return 0; }
static inline int SDL_CondBroadcast(SDL_cond *c) { (void)c; return 0; }
static inline int SDL_CondWait(SDL_cond *c, SDL_mutex *m) { (void)c; (void)m; return 0; }

/* ── Message box stubs ────────────────────────────────────────────── */

#define SDL_MESSAGEBOX_ERROR       0x00000010
#define SDL_MESSAGEBOX_WARNING     0x00000020
#define SDL_MESSAGEBOX_INFORMATION 0x00000040

static inline int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title,
                                           const char *msg, void *window) {
    (void)flags; (void)title; (void)msg; (void)window;
    return 0;
}

/* ── Log stubs ────────────────────────────────────────────────────── */

#define SDL_LOG_CATEGORY_APPLICATION 0

typedef enum {
    SDL_LOG_PRIORITY_VERBOSE = 1,
    SDL_LOG_PRIORITY_DEBUG,
    SDL_LOG_PRIORITY_INFO,
    SDL_LOG_PRIORITY_WARN,
    SDL_LOG_PRIORITY_ERROR,
    SDL_LOG_PRIORITY_CRITICAL,
    SDL_NUM_LOG_PRIORITIES
} SDL_LogPriority;

#include <stdarg.h>

static inline void SDL_LogMessageV(int category, SDL_LogPriority priority,
                                   const char *fmt, va_list ap) {
    (void)category; (void)priority; (void)fmt; (void)ap;
}

static inline void SDL_Log(const char *fmt, ...) { (void)fmt; }
static inline void SDL_LogError(int category, const char *fmt, ...) { (void)category; (void)fmt; }
static inline void SDL_LogWarn(int category, const char *fmt, ...) { (void)category; (void)fmt; }

/* ── Controller enums (for header compat) ─────────────────────────── */

#define SDL_CONTROLLER_BUTTON_MAX 21
#define SDL_CONTROLLER_AXIS_MAX   6

#endif /* WC_SDL_STUBS_H */
