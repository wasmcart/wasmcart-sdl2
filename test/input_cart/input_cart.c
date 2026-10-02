/*
 * input_cart.c - an SDL2 cart that counts the events the wasmcart video
 * backend produces from the host's pointer array and scroll wheel.
 *
 * It has no game in it: wc_render pumps SDL and tallies every mouse, finger
 * and wheel event into debug fields, which input_test.mjs drives through the
 * reference Node host and reads back. Built by build.sh against
 * sdl2_wc/lib/libSDL2_wc.a.
 */
#include <SDL.h>
#include "wasmcart.h"
#include "wc_cart.h"
#include "SDL_wasmcart_video.h"

#define W 320
#define H 240

static wc_info_t      info;
static wc_pointer_t   pointers[10];
static wc_wheel_t     wheel;
static uint32_t       fb[W * H];
static SDL_Window    *win;

/* tallies, read by the test through wc_debug_state */
static int32_t motions, mouse_x, mouse_y;
static int32_t down_l, up_l, down_r, up_r, down_m, up_m;
static int32_t touch_mouse_events;   /* SDL's synthesized mouse-from-touch */
static int32_t finger_down, finger_up, finger_motion, finger_x, finger_y;
static int32_t wheel_events, wheel_y_int, wheel_x_int;
static float   wheel_py, wheel_px;   /* SDL preciseY / preciseX sums */

WC_DEBUG_FIELDS(
    WC_DBG("motions", motions, WC_DBG_I32),
    WC_DBG("mouse_x", mouse_x, WC_DBG_I32),
    WC_DBG("mouse_y", mouse_y, WC_DBG_I32),
    WC_DBG("down_l", down_l, WC_DBG_I32),
    WC_DBG("up_l", up_l, WC_DBG_I32),
    WC_DBG("down_r", down_r, WC_DBG_I32),
    WC_DBG("up_r", up_r, WC_DBG_I32),
    WC_DBG("down_m", down_m, WC_DBG_I32),
    WC_DBG("up_m", up_m, WC_DBG_I32),
    WC_DBG("touch_mouse_events", touch_mouse_events, WC_DBG_I32),
    WC_DBG("finger_down", finger_down, WC_DBG_I32),
    WC_DBG("finger_up", finger_up, WC_DBG_I32),
    WC_DBG("finger_motion", finger_motion, WC_DBG_I32),
    WC_DBG("finger_x", finger_x, WC_DBG_I32),
    WC_DBG("finger_y", finger_y, WC_DBG_I32),
    WC_DBG("wheel_events", wheel_events, WC_DBG_I32),
    WC_DBG("wheel_y_int", wheel_y_int, WC_DBG_I32),
    WC_DBG("wheel_x_int", wheel_x_int, WC_DBG_I32),
    WC_DBG("wheel_py", wheel_py, WC_DBG_F32),
    WC_DBG("wheel_px", wheel_px, WC_DBG_F32)
)

/* The video backend references the gl4es bridge; this cart never makes a GL
 * context, so these are never called. */
void  gl4es_bridge_init(int w, int h) { (void)w; (void)h; }
void  gl4es_bridge_set_size(int w, int h) { (void)w; (void)h; }
void *wc_gl4es_GetProcAddress(const char *p) { (void)p; return NULL; }

__attribute__((export_name("wc_get_info")))
wc_info_t *wc_get_info(void)
{
    info.version = WC_ABI_VERSION;
    info.width = W;
    info.height = H;
    info.fb_ptr = (uint32_t)(uintptr_t)fb;
    info.flags = WC_FLAG_POINTER | WC_FLAG_DEBUG;
    info.pointer_ptr = (uint32_t)(uintptr_t)pointers;
    info.wheel_ptr = (uint32_t)(uintptr_t)&wheel;
    return &info;
}

__attribute__((export_name("wc_init")))
void wc_init(void)
{
    SDL_WASMCART_SetFramebuffer(fb, W, H);
    SDL_WASMCART_SetPointers(pointers);
    SDL_WASMCART_SetWheel(&wheel);
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    win = SDL_CreateWindow("input", 0, 0, W, H, 0);
}

static void tally(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_MOUSEMOTION:
        if (e->motion.which == SDL_TOUCH_MOUSEID) { touch_mouse_events++; break; }
        motions++; mouse_x = e->motion.x; mouse_y = e->motion.y;
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        if (e->button.which == SDL_TOUCH_MOUSEID) { touch_mouse_events++; break; }
        int d = e->type == SDL_MOUSEBUTTONDOWN;
        if (e->button.button == SDL_BUTTON_LEFT)   { if (d) down_l++; else up_l++; }
        if (e->button.button == SDL_BUTTON_RIGHT)  { if (d) down_r++; else up_r++; }
        if (e->button.button == SDL_BUTTON_MIDDLE) { if (d) down_m++; else up_m++; }
        break;
    }
    case SDL_FINGERDOWN:
        finger_down++;
        finger_x = (int32_t)(e->tfinger.x * (W - 1) + 0.5f);
        finger_y = (int32_t)(e->tfinger.y * (H - 1) + 0.5f);
        break;
    case SDL_FINGERMOTION:
        finger_motion++;
        finger_x = (int32_t)(e->tfinger.x * (W - 1) + 0.5f);
        finger_y = (int32_t)(e->tfinger.y * (H - 1) + 0.5f);
        break;
    case SDL_FINGERUP:
        finger_up++;
        break;
    case SDL_MOUSEWHEEL:
        wheel_events++;
        wheel_y_int += e->wheel.y;
        wheel_x_int += e->wheel.x;
        wheel_py += e->wheel.preciseY;
        wheel_px += e->wheel.preciseX;
        break;
    }
}

__attribute__((export_name("wc_render")))
void wc_render(void)
{
    SDL_Event e;
    /* Pump several times per frame on purpose: real games do (SDL_PollEvent
     * pumps whenever its queue drains), and a backend that re-reads per-frame
     * host state on every pump would multiply it. */
    for (int pass = 0; pass < 4; pass++) {
        SDL_PumpEvents();
        while (SDL_PollEvent(&e)) tally(&e);
    }
}
