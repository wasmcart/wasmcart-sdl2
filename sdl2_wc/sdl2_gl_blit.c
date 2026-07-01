/*
 * sdl2_gl_blit.c — GL blit implementation for sdl2_wc backend
 *
 * Include this file in your cart to enable GPU-accelerated SDL2 display.
 * SDL's software renderer draws pixels, this uploads them as a GL texture.
 *
 * USAGE in your cart:
 *
 *   // In wc_get_info():
 *   info.gpu_api = 1;
 *
 *   // In wc_init(), after SDL_Init:
 *   SDL_WASMCART_SetGLBlit(1);
 *
 *   // Link with: sdl2_gl_blit.c
 *   // (provides wc_sdl_gl_blit() that sdl2_wc video backend calls)
 */

#define WC_USE_GL
#include "wasmcart.h"

#define WC_GL_BLIT_IMPLEMENTATION
#include "wc_gl_blit.h"

/* Called by sdl2_wc video backend during UpdateWindowFramebuffer */
void wc_sdl_gl_blit(const void *pixels, int w, int h) {
    wc_gl_blit(pixels, w, h);
}
