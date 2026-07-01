/*
 * SDL_wasmcart_video.c — SDL2 video backend for wasmcart
 *
 * Provides a virtual window + GL context via gl4es bridge.
 * PumpEvents translates wc_pads[] gamepad input to SDL keyboard events.
 *
 * REUSABLE: This backend works for any SDL2 game targeting wasmcart.
 */

#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_WASMCART

#include "SDL_video.h"
#include "SDL_mouse.h"
#include "SDL_hints.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#include <string.h>

/*---------------------------------------------------------------------------*/
/* wasmcart pad types — must match wasmcart.h */

typedef struct {
    uint16_t buttons;
    int16_t  left_x;
    int16_t  left_y;
    int16_t  right_x;
    int16_t  right_y;
    uint8_t  left_trigger;
    uint8_t  right_trigger;
    uint8_t  connected;
    uint8_t  _pad[3];
} wc_pad_t;

/* Button bit masks — must match wasmcart.h */
#define WC_BTN_A      (1 << 0)
#define WC_BTN_B      (1 << 1)
#define WC_BTN_X      (1 << 2)
#define WC_BTN_Y      (1 << 3)
#define WC_BTN_L1     (1 << 4)
#define WC_BTN_R1     (1 << 5)
#define WC_BTN_START  (1 << 6)
#define WC_BTN_SELECT (1 << 7)
#define WC_BTN_UP     (1 << 8)
#define WC_BTN_DOWN   (1 << 9)
#define WC_BTN_LEFT   (1 << 10)
#define WC_BTN_RIGHT  (1 << 11)

/* Access to wasmcart pads (set by cart entry point) */
static wc_pad_t *wc_pads_ptr = NULL;
static uint16_t  prev_buttons = 0;

/* Access to wasmcart keyboard state (32-byte bitmask, USB HID scancodes) */
static uint8_t *wc_keys_ptr = NULL;
static uint8_t  prev_keys[32] = {0};

void SDL_WASMCART_SetPads(void *pads)
{
    wc_pads_ptr = (wc_pad_t *)pads;
}

void SDL_WASMCART_SetKeys(uint8_t *keys)
{
    wc_keys_ptr = keys;
}

/*---------------------------------------------------------------------------*/
/* Window framebuffer support (for 2D software rendering)                    */
/* Cart calls SDL_WASMCART_SetFramebuffer() to point at wc_framebuffer.     */
/* SDL's software renderer then blits through CreateWindowFramebuffer →     */
/* UpdateWindowFramebuffer to push pixels to the wasmcart framebuffer.      */

static uint32_t *_wc_framebuffer_ptr = NULL;
static int       _wc_fb_width = 0;
static int       _wc_fb_height = 0;
static SDL_Surface *_wc_window_surface = NULL;

void SDL_WASMCART_SetFramebuffer(uint32_t *fb, int w, int h)
{
    _wc_framebuffer_ptr = fb;
    _wc_fb_width = w;
    _wc_fb_height = h;
}

/*---------------------------------------------------------------------------*/
/* gl4es bridge — defined in gl4es_bridge.c */

extern void  gl4es_bridge_init(int w, int h);
extern void  gl4es_bridge_set_size(int w, int h);
extern void *wc_gl4es_GetProcAddress(const char *proc);

/*---------------------------------------------------------------------------*/
/* Driver name */

#define WASMCARTVID_DRIVER_NAME "wasmcart"

/* Forward declarations */
static int  WASMCART_VideoInit(_THIS);
static void WASMCART_VideoQuit(_THIS);
static int  WASMCART_CreateWindow(_THIS, SDL_Window *window);
static void WASMCART_DestroyWindow(_THIS, SDL_Window *window);
static void WASMCART_SetWindowSize(_THIS, SDL_Window *window);
static void WASMCART_GetWindowSizeInPixels(_THIS, SDL_Window *window, int *w, int *h);
static void WASMCART_PumpEvents(_THIS);

/* GL functions */
static int          WASMCART_GL_LoadLibrary(_THIS, const char *path);
static void        *WASMCART_GL_GetProcAddress(_THIS, const char *proc);
static void         WASMCART_GL_UnloadLibrary(_THIS);
static SDL_GLContext WASMCART_GL_CreateContext(_THIS, SDL_Window *window);
static int          WASMCART_GL_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext context);
static void         WASMCART_GL_GetDrawableSize(_THIS, SDL_Window *window, int *w, int *h);
static int          WASMCART_GL_SetSwapInterval(_THIS, int interval);
static int          WASMCART_GL_GetSwapInterval(_THIS);
static int          WASMCART_GL_SwapWindow(_THIS, SDL_Window *window);
static void         WASMCART_GL_DeleteContext(_THIS, SDL_GLContext context);

/*---------------------------------------------------------------------------*/
/* Window state */

static int wc_window_w = 800;
static int wc_window_h = 600;
static int gl4es_initialized = 0;

/*---------------------------------------------------------------------------*/
/* Window framebuffer functions — used by SDL_RENDERER_SOFTWARE */

static int WASMCART_CreateWindowFramebuffer(_THIS, SDL_Window *window,
                                            Uint32 *format, void **pixels, int *pitch)
{
    int w, h;
    SDL_GetWindowSize(window, &w, &h);

    if (_wc_window_surface)
        SDL_FreeSurface(_wc_window_surface);

    _wc_window_surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!_wc_window_surface)
        return -1;

    *format = SDL_PIXELFORMAT_ARGB8888;
    *pixels = _wc_window_surface->pixels;
    *pitch  = _wc_window_surface->pitch;
    return 0;
}

/*
 * Upload SDL surface pixels to GL via wc_gl_blit.
 * This replaces the old memcpy-to-framebuffer path.
 * All display goes through the GPU — no 2D framebuffer needed.
 */

/* GL blit — defined in wc_gl_blit.h, implemented wherever the cart
 * defines WC_GL_BLIT_IMPLEMENTATION. For carts that use sdl2_wc,
 * we call the blit function that the cart provides. */
extern void wc_sdl_gl_blit(const void *pixels, int w, int h);

/* Flag: 1 = use GL blit, 0 = use legacy framebuffer copy */
static int _wc_use_gl_blit = 0;

void SDL_WASMCART_SetGLBlit(int enable)
{
    _wc_use_gl_blit = enable;
}

static int WASMCART_UpdateWindowFramebuffer(_THIS, SDL_Window *window,
                                            const SDL_Rect *rects, int numrects)
{
    if (!_wc_window_surface)
        return 0;

    int src_w = _wc_window_surface->w;
    int src_h = _wc_window_surface->h;

    if (_wc_use_gl_blit) {
        /* GPU path: upload SDL surface as GL texture + fullscreen quad.
         * SDL surface is ARGB8888 — need to convert to RGBA for GL. */
        uint32_t *src = (uint32_t *)_wc_window_surface->pixels;
        /* ARGB → RGBA swap: A is in high byte, need R in byte 0.
         * ARGB8888 in memory (little-endian): B, G, R, A
         * RGBA8888 for GL: R, G, B, A
         * So we need to swap B and R. */
        static uint8_t *rgba_buf = NULL;
        static int rgba_buf_size = 0;
        int needed = src_w * src_h * 4;
        if (!rgba_buf || rgba_buf_size < needed) {
            if (rgba_buf) SDL_free(rgba_buf);
            rgba_buf = (uint8_t *)SDL_malloc(needed);
            rgba_buf_size = needed;
        }
        uint8_t *s = (uint8_t *)src;
        for (int i = 0; i < src_w * src_h; i++) {
            int si = i * 4;
            rgba_buf[si + 0] = s[si + 2]; /* R (was at offset 2 in BGRA) */
            rgba_buf[si + 1] = s[si + 1]; /* G */
            rgba_buf[si + 2] = s[si + 0]; /* B (was at offset 0 in BGRA) */
            rgba_buf[si + 3] = 255;       /* A */
        }
        wc_sdl_gl_blit(rgba_buf, src_w, src_h);
        return 0;
    }

    /* Legacy path: copy pixels to wasmcart framebuffer */
    if (!_wc_framebuffer_ptr)
        return 0;

    int copy_w = src_w < _wc_fb_width  ? src_w : _wc_fb_width;
    int copy_h = src_h < _wc_fb_height ? src_h : _wc_fb_height;

    uint32_t *src = (uint32_t *)_wc_window_surface->pixels;
    uint32_t *dst = _wc_framebuffer_ptr;

    for (int y = 0; y < copy_h; y++)
        memcpy(dst + y * _wc_fb_width, src + y * src_w, copy_w * 4);

    return 0;
}

static void WASMCART_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    if (_wc_window_surface) {
        SDL_FreeSurface(_wc_window_surface);
        _wc_window_surface = NULL;
    }
}

/*---------------------------------------------------------------------------*/
/* Bootstrap */

static void WASMCART_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *WASMCART_CreateDevice(void)
{
    SDL_VideoDevice *device;

    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return 0;
    }

    /* Video */
    device->VideoInit = WASMCART_VideoInit;
    device->VideoQuit = WASMCART_VideoQuit;

    /* Window */
    device->CreateSDLWindow = WASMCART_CreateWindow;
    device->DestroyWindow = WASMCART_DestroyWindow;
    device->SetWindowSize = WASMCART_SetWindowSize;
    device->GetWindowSizeInPixels = WASMCART_GetWindowSizeInPixels;

    /* Window framebuffer (for SDL_RENDERER_SOFTWARE) */
    device->CreateWindowFramebuffer = WASMCART_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = WASMCART_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = WASMCART_DestroyWindowFramebuffer;

    /* Events */
    device->PumpEvents = WASMCART_PumpEvents;

    /* OpenGL */
    device->GL_LoadLibrary = WASMCART_GL_LoadLibrary;
    device->GL_GetProcAddress = WASMCART_GL_GetProcAddress;
    device->GL_UnloadLibrary = WASMCART_GL_UnloadLibrary;
    device->GL_CreateContext = WASMCART_GL_CreateContext;
    device->GL_MakeCurrent = WASMCART_GL_MakeCurrent;
    device->GL_GetDrawableSize = WASMCART_GL_GetDrawableSize;
    device->GL_SetSwapInterval = WASMCART_GL_SetSwapInterval;
    device->GL_GetSwapInterval = WASMCART_GL_GetSwapInterval;
    device->GL_SwapWindow = WASMCART_GL_SwapWindow;
    device->GL_DeleteContext = WASMCART_GL_DeleteContext;

    device->free = WASMCART_DeleteDevice;

    return device;
}

VideoBootStrap WASMCART_bootstrap = {
    WASMCARTVID_DRIVER_NAME, "SDL wasmcart video driver",
    WASMCART_CreateDevice,
    NULL /* no ShowMessageBox */
};

/*---------------------------------------------------------------------------*/
/* Video init/quit */

static int WASMCART_VideoInit(_THIS)
{
    SDL_DisplayMode mode;

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB888;
    mode.w = wc_window_w;
    mode.h = wc_window_h;
    mode.refresh_rate = 60;
    mode.driverdata = NULL;

    if (SDL_AddBasicVideoDisplay(&mode) < 0)
        return -1;

    SDL_AddDisplayMode(&_this->displays[0], &mode);

    return 0;
}

static void WASMCART_VideoQuit(_THIS)
{
    /* Nothing to clean up */
}

/*---------------------------------------------------------------------------*/
/* Window management */

static int WASMCART_CreateWindow(_THIS, SDL_Window *window)
{
    wc_window_w = window->w;
    wc_window_h = window->h;

    /* Mark window as shown and with GL support */
    window->flags |= SDL_WINDOW_SHOWN;
    window->flags |= SDL_WINDOW_OPENGL;

    /* Set input focus so SDL_SendKeyboardKey/SDL_SendMouseMotion deliver events */
    SDL_SetKeyboardFocus(window);
    SDL_SetMouseFocus(window);

    return 0;
}

static void WASMCART_DestroyWindow(_THIS, SDL_Window *window)
{
    /* Nothing to destroy */
}

static void WASMCART_SetWindowSize(_THIS, SDL_Window *window)
{
    wc_window_w = window->w;
    wc_window_h = window->h;

    if (gl4es_initialized)
        gl4es_bridge_set_size(wc_window_w, wc_window_h);
}

static void WASMCART_GetWindowSizeInPixels(_THIS, SDL_Window *window, int *w, int *h)
{
    *w = window->w;
    *h = window->h;
}

/*---------------------------------------------------------------------------*/
/* OpenGL */

static int WASMCART_GL_LoadLibrary(_THIS, const char *path)
{
    /* gl4es is statically linked */
    return 0;
}

static void *WASMCART_GL_GetProcAddress(_THIS, const char *proc)
{
    /* Delegate to gl4es which wraps GL1.x to GLES2 via the wasmcart bridge */
    return wc_gl4es_GetProcAddress(proc);
}

static void WASMCART_GL_UnloadLibrary(_THIS)
{
    /* Nothing to unload */
}

static SDL_GLContext WASMCART_GL_CreateContext(_THIS, SDL_Window *window)
{
    /* Don't init gl4es here — neverball's video.c calls
     * set_getprocaddress() + initialize_gl4es() itself.
     * Just record the size for the bridge. */
    gl4es_bridge_set_size(window->w, window->h);

    /* Return a non-NULL sentinel as the "context" */
    return (SDL_GLContext)0x1;
}

static int WASMCART_GL_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext context)
{
    return 0; /* Always succeeds */
}

static void WASMCART_GL_GetDrawableSize(_THIS, SDL_Window *window, int *w, int *h)
{
    *w = window->w;
    *h = window->h;
}

static int WASMCART_GL_SetSwapInterval(_THIS, int interval)
{
    return 0; /* Wasmcart controls frame timing */
}

static int WASMCART_GL_GetSwapInterval(_THIS)
{
    return 0;
}

static int WASMCART_GL_SwapWindow(_THIS, SDL_Window *window)
{
    /* No-op: the host reads the GL framebuffer directly */
    return 0;
}

static void WASMCART_GL_DeleteContext(_THIS, SDL_GLContext context)
{
    /* Nothing to delete */
}

/*---------------------------------------------------------------------------*/
/* Event pump — translates wc_pads to SDL keyboard events */

static void push_key(SDL_Scancode scancode, Uint8 state)
{
    SDL_SendKeyboardKey(state, scancode);
}

static void WASMCART_PumpEvents(_THIS)
{
    if (!wc_pads_ptr) return;

    uint16_t buttons = wc_pads_ptr[0].buttons;
    uint16_t pressed  = buttons & ~prev_buttons;
    uint16_t released = ~buttons & prev_buttons;

    /* Map gamepad buttons to SDL keyboard scancodes.
     * Neverball uses these keys for navigation and gameplay. */

    /* A → Return + Space (select/confirm/action) */
    if (pressed & WC_BTN_A)  { push_key(SDL_SCANCODE_RETURN, SDL_PRESSED); push_key(SDL_SCANCODE_SPACE, SDL_PRESSED); }
    if (released & WC_BTN_A) { push_key(SDL_SCANCODE_RETURN, SDL_RELEASED); push_key(SDL_SCANCODE_SPACE, SDL_RELEASED); }

    /* B → Escape (back/cancel) */
    if (pressed & WC_BTN_B)  push_key(SDL_SCANCODE_ESCAPE, SDL_PRESSED);
    if (released & WC_BTN_B) push_key(SDL_SCANCODE_ESCAPE, SDL_RELEASED);

    /* X → Space (alternate action) */
    if (pressed & WC_BTN_X)  push_key(SDL_SCANCODE_SPACE, SDL_PRESSED);
    if (released & WC_BTN_X) push_key(SDL_SCANCODE_SPACE, SDL_RELEASED);

    /* Start → F10 (neverball uses F10 for pause) */
    if (pressed & WC_BTN_START)  push_key(SDL_SCANCODE_F10, SDL_PRESSED);
    if (released & WC_BTN_START) push_key(SDL_SCANCODE_F10, SDL_RELEASED);

    /* D-pad → Arrow keys (menu navigation) */
    if (pressed & WC_BTN_UP)    push_key(SDL_SCANCODE_UP, SDL_PRESSED);
    if (released & WC_BTN_UP)   push_key(SDL_SCANCODE_UP, SDL_RELEASED);
    if (pressed & WC_BTN_DOWN)  push_key(SDL_SCANCODE_DOWN, SDL_PRESSED);
    if (released & WC_BTN_DOWN) push_key(SDL_SCANCODE_DOWN, SDL_RELEASED);
    if (pressed & WC_BTN_LEFT)  push_key(SDL_SCANCODE_LEFT, SDL_PRESSED);
    if (released & WC_BTN_LEFT) push_key(SDL_SCANCODE_LEFT, SDL_RELEASED);
    if (pressed & WC_BTN_RIGHT) push_key(SDL_SCANCODE_RIGHT, SDL_PRESSED);
    if (released & WC_BTN_RIGHT)push_key(SDL_SCANCODE_RIGHT, SDL_RELEASED);

    /* Shoulder buttons → page up/down or screenshot */
    if (pressed & WC_BTN_L1)  push_key(SDL_SCANCODE_PAGEUP, SDL_PRESSED);
    if (released & WC_BTN_L1) push_key(SDL_SCANCODE_PAGEUP, SDL_RELEASED);
    if (pressed & WC_BTN_R1)  push_key(SDL_SCANCODE_PAGEDOWN, SDL_PRESSED);
    if (released & WC_BTN_R1) push_key(SDL_SCANCODE_PAGEDOWN, SDL_RELEASED);

    /* X → Space (used for camera rotation in some states) */
    if (pressed & WC_BTN_X)  push_key(SDL_SCANCODE_SPACE, SDL_PRESSED);
    if (released & WC_BTN_X) push_key(SDL_SCANCODE_SPACE, SDL_RELEASED);

    /* Select → Tab */
    if (pressed & WC_BTN_SELECT)  push_key(SDL_SCANCODE_TAB, SDL_PRESSED);
    if (released & WC_BTN_SELECT) push_key(SDL_SCANCODE_TAB, SDL_RELEASED);

    prev_buttons = buttons;

    /* --- Raw keyboard passthrough from wasmcart keyboard ABI --- */
    /* wc_keys is a 32-byte bitmask of USB HID scancodes (same as SDL scancodes). */
    if (wc_keys_ptr) {
        for (int byte = 0; byte < 32; byte++) {
            uint8_t cur = wc_keys_ptr[byte];
            uint8_t prev = prev_keys[byte];
            uint8_t changed = cur ^ prev;
            if (!changed) continue;
            for (int bit = 0; bit < 8; bit++) {
                if (changed & (1 << bit)) {
                    int scancode = byte * 8 + bit;
                    if (cur & (1 << bit)) {
                        push_key(scancode, SDL_PRESSED);
                    } else {
                        push_key(scancode, SDL_RELEASED);
                    }
                }
            }
            prev_keys[byte] = cur;
        }
    }
}

#endif /* SDL_VIDEO_DRIVER_WASMCART */
