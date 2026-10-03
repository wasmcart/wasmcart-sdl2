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

/* 20 bytes as of ABI v4; see the note in SDL_wasmcart_joystick.c. This is a
 * SECOND copy of the struct in this library, which is why both went stale
 * together when the ABI moved: the assert below makes a mismatch a build
 * failure rather than a misread field. */
typedef struct {
    uint32_t buttons;        /* bits 21-31 reserved */
    int16_t  left_x;
    int16_t  left_y;
    int16_t  right_x;
    int16_t  right_y;
    int16_t  left_trigger;   /* 0..32767, never negative */
    int16_t  right_trigger;  /* 0..32767, never negative */
    uint8_t  connected;
    uint8_t  _pad[3];
} wc_pad_t;

SDL_COMPILE_TIME_ASSERT(wc_pad_t_size_video, sizeof(wc_pad_t) == 20);

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
static SDL_Window *_wc_window = NULL;

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
    _wc_window = window;
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

/*---------------------------------------------------------------------------*/
/* Pointer + wheel (wasmcart pointer ABI v3 / wheel ABI v3.1)
 *
 * The cart points us at the same wc_pointer_t[10] and wc_wheel_t it hands the
 * host through wc_info_t.pointer_ptr / wheel_ptr (and sets WC_FLAG_POINTER, or
 * the host writes neither). The host writes both before each wc_render.
 *
 * Slot 0 is the mouse: it becomes SDL mouse motion + buttons. Slots 1-9 are
 * touch contacts: they become SDL finger events on one touch device, which SDL
 * also turns into mouse events by default (SDL_HINT_TOUCH_MOUSE_EVENTS), so a
 * mouse-only game still works on a phone and a touch-aware one gets real
 * multi-touch. Coordinates are cart pixels, which is the SDL window size.
 */

/* must match wasmcart.h */
typedef struct {
    int16_t  x;
    int16_t  y;
    uint8_t  buttons;  /* bit0 primary, bit1 secondary, bit2 middle */
    uint8_t  active;
    uint8_t  _pad[2];
} wc_pointer_t;

typedef struct {
    int32_t dx;        /* 1/120 notch, right positive */
    int32_t dy;        /* 1/120 notch, UP positive */
} wc_wheel_t;

#define WC_POINTER_SLOTS 10
#define WC_WHEEL_NOTCH   120.0f
#define WC_TOUCH_ID      ((SDL_TouchID)1)

static wc_pointer_t *wc_pointers_ptr = NULL;
static wc_wheel_t   *wc_wheel_ptr = NULL;
static wc_pointer_t  prev_pointers[WC_POINTER_SLOTS];
static int           wc_touch_added = 0;

void SDL_WASMCART_SetPointers(void *pointers)
{
    wc_pointers_ptr = (wc_pointer_t *)pointers;
    SDL_memset(prev_pointers, 0, sizeof(prev_pointers));
}

void SDL_WASMCART_SetWheel(void *wheel)
{
    wc_wheel_ptr = (wc_wheel_t *)wheel;
}

/* LEGACY: before SetPointers existed, a cart could define these four functions
 * to feed slot 0 by hand. Still honoured when SetPointers was never called. */
extern int wc_pointer_active(void) __attribute__((weak));
extern int wc_pointer_x(void) __attribute__((weak));
extern int wc_pointer_y(void) __attribute__((weak));
extern int wc_pointer_buttons(void) __attribute__((weak));

/* Read the mouse slot from whichever source the cart wired. */
static int host_mouse(int *x, int *y, int *buttons)
{
    if (wc_pointers_ptr) {
        if (!wc_pointers_ptr[0].active) return 0;
        *x = wc_pointers_ptr[0].x;
        *y = wc_pointers_ptr[0].y;
        *buttons = wc_pointers_ptr[0].buttons;
        return 1;
    }
    if (wc_pointer_active && wc_pointer_active()) {
        *x = wc_pointer_x();
        *y = wc_pointer_y();
        *buttons = wc_pointer_buttons();
        return 1;
    }
    return 0;
}

static int host_mouse_active(void)
{
    int x, y, b;
    return host_mouse(&x, &y, &b);
}

static void send_mouse_buttons(int now, int prev)
{
    static const Uint8 sdl_btn[3] = { SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE };
    for (int i = 0; i < 3; i++) {
        int bit = 1 << i;
        if ((now & bit) && !(prev & bit)) SDL_SendMouseButton(_wc_window, 0, SDL_PRESSED, sdl_btn[i]);
        if (!(now & bit) && (prev & bit)) SDL_SendMouseButton(_wc_window, 0, SDL_RELEASED, sdl_btn[i]);
    }
}

/* Analog-cursor position (see SDL_WASMCART_UpdateAnalogCursor below).
 * -1 = uninitialized (centered on first use). */
static float wc_cursor_x = -1.0f;
static float wc_cursor_y = -1.0f;

/* Slot 0 → SDL mouse. Sends only what changed: the pump runs many times per
 * rendered frame, and an unchanged position is not a motion. */
static void pump_mouse(void)
{
    static int prev_active = 0, prev_x = -1, prev_y = -1, prev_btn = 0;
    int x = 0, y = 0, btn = 0;
    int active = host_mouse(&x, &y, &btn);

    if (active) {
        SDL_SetMouseFocus(_wc_window);
        if (!prev_active || x != prev_x || y != prev_y) {
            SDL_SendMouseMotion(_wc_window, 0, 0, x, y);
            wc_cursor_x = x; wc_cursor_y = y;   /* keep the gamepad cursor in sync */
        }
        send_mouse_buttons(btn, prev_btn);
        prev_x = x; prev_y = y; prev_btn = btn;
    } else if (prev_active) {
        send_mouse_buttons(0, prev_btn);   /* never leave a button stuck down */
        prev_btn = 0;
    }
    prev_active = active;
}

/* Slots 1-9 → SDL fingers, normalized to 0..1 as SDL expects. */
static void pump_touch(void)
{
    if (!wc_pointers_ptr || !_wc_window) return;
    float w = wc_window_w > 1 ? (float)(wc_window_w - 1) : 1.0f;
    float h = wc_window_h > 1 ? (float)(wc_window_h - 1) : 1.0f;

    for (int i = 1; i < WC_POINTER_SLOTS; i++) {
        wc_pointer_t cur = wc_pointers_ptr[i];
        wc_pointer_t *prev = &prev_pointers[i];
        if (!cur.active && !prev->active) continue;

        if (!wc_touch_added) {
            SDL_AddTouch(WC_TOUCH_ID, SDL_TOUCH_DEVICE_DIRECT, "wasmcart");
            wc_touch_added = 1;
        }
        float fx = cur.x / w, fy = cur.y / h;
        if (cur.active && !prev->active) {
            SDL_SendTouch(WC_TOUCH_ID, (SDL_FingerID)i, _wc_window, SDL_TRUE, fx, fy, 1.0f);
        } else if (cur.active) {
            if (cur.x != prev->x || cur.y != prev->y)
                SDL_SendTouchMotion(WC_TOUCH_ID, (SDL_FingerID)i, _wc_window, fx, fy, 1.0f);
        } else {
            /* lifted: the host leaves the last position, but SDL wants one */
            SDL_SendTouch(WC_TOUCH_ID, (SDL_FingerID)i, _wc_window, SDL_FALSE,
                          prev->x / w, prev->y / h, 0.0f);
        }
        *prev = cur;
    }
}

/* Wheel → SDL_MOUSEWHEEL, in notches (fractional for trackpads; SDL keeps the
 * integer y/x for old code and preciseX/Y for new).
 *
 * CONSUMED ON READ: the host writes the frame's total before wc_render and
 * zeroes it after, but the pump runs many times inside one frame, so without
 * zeroing it here one notch would scroll once per pump. The host overwrites the
 * field (it does not add to it), so clearing it cannot lose a later frame. */
static void pump_wheel(void)
{
    if (!wc_wheel_ptr) return;
    int32_t dx = wc_wheel_ptr->dx, dy = wc_wheel_ptr->dy;
    if (!dx && !dy) return;
    wc_wheel_ptr->dx = 0;
    wc_wheel_ptr->dy = 0;
    SDL_SetMouseFocus(_wc_window);
    SDL_SendMouseWheel(_wc_window, 0, dx / WC_WHEEL_NOTCH, dy / WC_WHEEL_NOTCH,
                       SDL_MOUSEWHEEL_NORMAL);
}

/*---------------------------------------------------------------------------*/
/* Analog-cursor gamepad control (generic — works for any pointer-driven cart).
 *
 * The left stick drives a software cursor that we feed into SDL as mouse motion
 * (same delivery path the absolute-pointer ABI uses). A = left click, B = right
 * click at the cursor. This makes a mouse-UI game (menus + RTS unit control)
 * playable on a couch gamepad — the console-RTS-port idiom (PS Vita/PS4
 * Stratagus, Command & Conquer console ports, etc.).
 *
 * Tuning matches the stratagus-vita port: pow-curve acceleration on stick
 * magnitude so a light push nudges precisely and a full push travels fast.
 * L1 held = 2x speed boost. Right stick edge-scrolls the map via arrow keys.
 */

/* int16 stick range is -32768..32767 (wasmcart pad ABI == SDL axis range). */
#define WC_STICK_DEADZONE_L   3000
#define WC_STICK_DEADZONE_R   16000
/* Speed tuning (matches stratagus-vita feel). Per-frame step ≈
 * pow(axis,POW) * dt(16.6) * boost / SPEED_MOD * resScale. At full deflection
 * (axis≈32767, pow≈36700) with 720p resScale (1.5): 36700*16.6*1.5/130000 ≈
 * 7 px/frame ≈ ~420 px/s — a couch-comfortable full-screen traverse in ~3s,
 * with the pow curve giving fine control on light pushes. */
#define WC_CURSOR_SPEED_MOD   130000.0   /* larger = slower */
#define WC_CURSOR_AXIS_POW    1.03       /* accel curve exponent */


/* Called once per rendered frame by the cart (e.g. Stratagus WaitEventsOneFrame).
 * Integrates left-stick deflection into the software cursor and feeds SDL a
 * single mouse-motion event per frame — framerate-correct, unlike the pump. */
void SDL_WASMCART_UpdateAnalogCursor(void)
{
    if (!wc_pads_ptr || !wc_pads_ptr[0].connected) return;
    /* If the host is driving an absolute pointer this frame, it owns the cursor. */
    if (host_mouse_active()) return;

    if (wc_cursor_x < 0.0f) { wc_cursor_x = wc_window_w * 0.5f; wc_cursor_y = wc_window_h * 0.5f; }

    int16_t lx = wc_pads_ptr[0].left_x;
    int16_t ly = wc_pads_ptr[0].left_y;
    if (lx > -WC_STICK_DEADZONE_L && lx < WC_STICK_DEADZONE_L) lx = 0;
    if (ly > -WC_STICK_DEADZONE_L && ly < WC_STICK_DEADZONE_L) ly = 0;
    if (lx == 0 && ly == 0) return;

    const double dt = 16.6;                 /* one ~60fps frame */
    double resScale = (double)wc_window_h / 480.0;
    double boost = (wc_pads_ptr[0].buttons & WC_BTN_L1) ? 2.0 : 1.0;
    double sx = (lx > 0) ? 1.0 : -1.0;
    double sy = (ly > 0) ? 1.0 : -1.0;
    double ax = lx < 0 ? -(double)lx : (double)lx;
    double ay = ly < 0 ? -(double)ly : (double)ly;
    wc_cursor_x += (float)(SDL_pow(ax, WC_CURSOR_AXIS_POW) * sx * dt * boost / WC_CURSOR_SPEED_MOD * resScale);
    wc_cursor_y += (float)(SDL_pow(ay, WC_CURSOR_AXIS_POW) * sy * dt * boost / WC_CURSOR_SPEED_MOD * resScale);

    if (wc_cursor_x < 0) wc_cursor_x = 0;
    else if (wc_cursor_x > wc_window_w - 1) wc_cursor_x = wc_window_w - 1;
    if (wc_cursor_y < 0) wc_cursor_y = 0;
    else if (wc_cursor_y > wc_window_h - 1) wc_cursor_y = wc_window_h - 1;

    SDL_SetMouseFocus(_wc_window);
    SDL_SendMouseMotion(_wc_window, 0, 0, (int)wc_cursor_x, (int)wc_cursor_y);
}

static void WASMCART_PumpEvents(_THIS)
{
    /* Host mouse, touch and wheel first: they need no gamepad. */
    pump_mouse();
    pump_touch();
    pump_wheel();

    if (!wc_pads_ptr) return;

    uint32_t buttons = wc_pads_ptr[0].buttons;
    uint16_t pressed  = buttons & ~prev_buttons;
    uint16_t released = ~buttons & prev_buttons;

    /* NOTE: left-stick cursor integration is NOT done here. SDL_PumpEvents runs
     * many times per rendered frame (SDL_PollEvent pumps whenever its queue
     * drains), so integrating motion here would move the cursor ~30x too fast.
     * The cart calls SDL_WASMCART_UpdateAnalogCursor() exactly once per frame
     * (from WaitEventsOneFrame) for a stable, framerate-correct cursor speed. */

    /* --- A = left click, B = right click (at the analog cursor) --- */
    if (pressed & WC_BTN_A)  { SDL_SetMouseFocus(_wc_window); SDL_SendMouseButton(_wc_window, 0, SDL_PRESSED,  SDL_BUTTON_LEFT); }
    if (released & WC_BTN_A) { SDL_SendMouseButton(_wc_window, 0, SDL_RELEASED, SDL_BUTTON_LEFT); }
    if (pressed & WC_BTN_B)  { SDL_SetMouseFocus(_wc_window); SDL_SendMouseButton(_wc_window, 0, SDL_PRESSED,  SDL_BUTTON_RIGHT); }
    if (released & WC_BTN_B) { SDL_SendMouseButton(_wc_window, 0, SDL_RELEASED, SDL_BUTTON_RIGHT); }

    /* --- Right stick → map edge-scroll via arrow keys (held while deflected) --- */
    {
        static uint16_t rscroll_prev = 0;  /* bit0=L bit1=R bit2=U bit3=D */
        int16_t rx = wc_pads_ptr[0].right_x;
        int16_t ry = wc_pads_ptr[0].right_y;
        uint16_t rs = 0;
        if (rx >  WC_STICK_DEADZONE_R) rs |= 2;
        if (rx < -WC_STICK_DEADZONE_R) rs |= 1;
        if (ry >  WC_STICK_DEADZONE_R) rs |= 8;
        if (ry < -WC_STICK_DEADZONE_R) rs |= 4;
        uint16_t rchg = rs ^ rscroll_prev;
        if (rchg & 1) push_key(SDL_SCANCODE_LEFT,  (rs & 1) ? SDL_PRESSED : SDL_RELEASED);
        if (rchg & 2) push_key(SDL_SCANCODE_RIGHT, (rs & 2) ? SDL_PRESSED : SDL_RELEASED);
        if (rchg & 4) push_key(SDL_SCANCODE_UP,    (rs & 4) ? SDL_PRESSED : SDL_RELEASED);
        if (rchg & 8) push_key(SDL_SCANCODE_DOWN,  (rs & 8) ? SDL_PRESSED : SDL_RELEASED);
        rscroll_prev = rs;
    }

    /* Start = Return/Enter (skips intro splash, confirms dialogs). */
    if (pressed & WC_BTN_START)  { push_key(SDL_SCANCODE_RETURN, SDL_PRESSED); }
    if (released & WC_BTN_START) { push_key(SDL_SCANCODE_RETURN, SDL_RELEASED); }
    /* Select = Escape (back/cancel/game menu) */
    if (pressed & WC_BTN_SELECT)  push_key(SDL_SCANCODE_ESCAPE, SDL_PRESSED);
    if (released & WC_BTN_SELECT) push_key(SDL_SCANCODE_ESCAPE, SDL_RELEASED);

    /* X → Space (RTS: often stop / no-op action / rotate) */
    if (pressed & WC_BTN_X)  push_key(SDL_SCANCODE_SPACE, SDL_PRESSED);
    if (released & WC_BTN_X) push_key(SDL_SCANCODE_SPACE, SDL_RELEASED);

    /* D-pad → Arrow keys (menu navigation fallback / discrete map scroll) */
    if (pressed & WC_BTN_UP)    push_key(SDL_SCANCODE_UP, SDL_PRESSED);
    if (released & WC_BTN_UP)   push_key(SDL_SCANCODE_UP, SDL_RELEASED);
    if (pressed & WC_BTN_DOWN)  push_key(SDL_SCANCODE_DOWN, SDL_PRESSED);
    if (released & WC_BTN_DOWN) push_key(SDL_SCANCODE_DOWN, SDL_RELEASED);
    if (pressed & WC_BTN_LEFT)  push_key(SDL_SCANCODE_LEFT, SDL_PRESSED);
    if (released & WC_BTN_LEFT) push_key(SDL_SCANCODE_LEFT, SDL_RELEASED);
    if (pressed & WC_BTN_RIGHT) push_key(SDL_SCANCODE_RIGHT, SDL_PRESSED);
    if (released & WC_BTN_RIGHT)push_key(SDL_SCANCODE_RIGHT, SDL_RELEASED);

    /* R1 = speed-boost modifier only (handled above); no key. L1 reserved.
     * (Both shoulders are free for in-game bindings later.) */

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
