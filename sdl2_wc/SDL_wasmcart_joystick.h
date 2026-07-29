/*
 * SDL_wasmcart_joystick.h - Header for wasmcart joystick backend
 */

#ifndef SDL_wasmcart_joystick_h_
#define SDL_wasmcart_joystick_h_

/* Point the joystick backend at the cart's wc_pad_t[4] array.
 *
 * Separate from SDL_WASMCART_SetPads (the video backend's pad-to-keyboard
 * translation) because the two backends are independent compilation units in
 * SDL's tree and a cart may want one without the other. Call both; they read
 * the same array and do not interfere.
 *
 * Must be called BEFORE SDL_Init(SDL_INIT_JOYSTICK), since the driver
 * enumerates devices from this array at subsystem init. */
extern void SDL_WASMCART_SetJoystickPads(void *pads);

#endif
