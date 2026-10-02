/*
 * SDL_wasmcart_video.h — Header for wasmcart video backend
 */

#ifndef SDL_wasmcart_video_h_
#define SDL_wasmcart_video_h_

/* Set the pointer to wc_pads for input translation */
extern void SDL_WASMCART_SetPads(void *pads);

/* Set the wasmcart framebuffer for 2D software rendering.
 * Used by CreateWindowFramebuffer/UpdateWindowFramebuffer to push pixels
 * from SDL's software renderer to the wasmcart framebuffer. */
extern void SDL_WASMCART_SetFramebuffer(uint32_t *fb, int w, int h);

/* Set the wasmcart keyboard state bitmask for raw keyboard passthrough.
 * The bitmask is 32 bytes (256 bits), indexed by USB HID scancode. */
extern void SDL_WASMCART_SetKeys(uint8_t *keys);

/* Set the wasmcart pointer array (wc_pointer_t[10], the same one in
 * wc_info_t.pointer_ptr; the cart must also set WC_FLAG_POINTER).
 * Slot 0 (mouse) becomes SDL mouse motion/buttons; slots 1-9 (touch)
 * become SDL finger events, which SDL also maps to the mouse by default. */
extern void SDL_WASMCART_SetPointers(void *pointers);

/* Set the wasmcart scroll wheel (wc_wheel_t, the same one in
 * wc_info_t.wheel_ptr; gated by WC_FLAG_POINTER). Becomes SDL_MOUSEWHEEL
 * in notches. The backend zeroes the struct as it consumes it. */
extern void SDL_WASMCART_SetWheel(void *wheel);

/* Enable GL blit mode: SDL surface pixels are uploaded as a GL texture
 * instead of being copied to wc_framebuffer. All display goes through GPU.
 * Call with enable=1 after GL context is available. */
extern void SDL_WASMCART_SetGLBlit(int enable);

#endif
