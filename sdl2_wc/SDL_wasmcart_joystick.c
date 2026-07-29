/*
 * SDL_wasmcart_joystick.c - SDL2 joystick backend for wasmcart
 *
 * Surfaces the wasmcart pad array (wc_pad_t[4]) as real SDL joystick devices,
 * so an SDL or pygame game reaches gamepads through the API it already knows
 * rather than a wasmcart-specific side channel.
 *
 * The video backend also translates pad state into SDL *keyboard* events, and
 * that stays: a lot of ported games only read the keyboard, and the two paths
 * describe the same physical pad without fighting over it. A game that opens a
 * joystick gets axes and buttons; a game that reads keys still gets keys.
 *
 * Rumble is the reason this driver exists at all. It is the one input feature
 * that runs cart-to-host, and the host exposes it as the wc_pad_rumble import.
 * Routing SDL's Rumble entry point at that import is what makes upstream code
 * -- pygame's Joystick.rumble(), SDL_GameControllerRumble() -- actually move a
 * motor instead of returning SDL_Unsupported().
 *
 * REUSABLE: this backend works for any SDL2 game targeting wasmcart.
 */

#include "../../SDL_internal.h"

#ifdef SDL_JOYSTICK_WASMCART

#include "SDL_joystick.h"
#include "SDL_events.h"
#include "SDL_timer.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"

#include <string.h>

/*---------------------------------------------------------------------------*/
/* wasmcart pad types - must match wasmcart.h */

typedef struct {
    Uint16 buttons;
    Sint16 left_x;
    Sint16 left_y;
    Sint16 right_x;
    Sint16 right_y;
    Uint8  left_trigger;
    Uint8  right_trigger;
    Uint8  connected;
    Uint8  _pad[3];
} wc_pad_t;

#define WC_MAX_PADS 4

/* Button bit masks - must match wasmcart.h */
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

/*---------------------------------------------------------------------------*/
/* Host imports.
 *
 * These are the wasmcart rumble ABI (see wasmcart.h). They are declared here
 * rather than pulled in from wasmcart.h because this file is compiled inside
 * the SDL2 source tree, where that header is not on the include path.
 *
 * They are OPTIONAL host imports. A wasm import that is never called is not
 * emitted into the binary, but one that IS called must be satisfied by the
 * host or instantiation fails. Every wasmcart host at ABI v3 provides them;
 * older hosts do not, which is why the rumble entry points below are the only
 * callers and a cart that never rumbles never reaches them.
 */
#ifdef __wasm__
__attribute__((import_module("env"), import_name("wc_pad_has_rumble")))
extern unsigned int wc_pad_has_rumble(unsigned int pad_id);

__attribute__((import_module("env"), import_name("wc_pad_rumble")))
extern void wc_pad_rumble(unsigned int pad_id, float low, float high,
                          unsigned int duration_ms);

__attribute__((import_module("env"), import_name("wc_pad_rumble_stop")))
extern void wc_pad_rumble_stop(unsigned int pad_id);
#else
static unsigned int wc_pad_has_rumble(unsigned int pad_id) { (void)pad_id; return 0; }
static void wc_pad_rumble(unsigned int pad_id, float low, float high,
                          unsigned int duration_ms) {
    (void)pad_id; (void)low; (void)high; (void)duration_ms;
}
static void wc_pad_rumble_stop(unsigned int pad_id) { (void)pad_id; }
#endif

/* How long each wc_pad_rumble call arms the host for.
 *
 * SDL does not hand the game's duration to a driver's Rumble entry point. It
 * keeps it as joystick->rumble_expiration and calls Rumble(0, 0) itself once
 * that passes, from SDL_JoystickUpdate -- which pygame drives from
 * event.get(), so in practice once a frame. Reading the field back here does
 * not help either: on a fresh effect SDL sets it AFTER this returns, so it
 * still holds the previous effect's deadline.
 *
 * So SDL owns when the effect ends and this owns only the safety net. The
 * window is deliberately short: it bounds how long the motors can keep running
 * if the game stops pumping events (or the cart dies) to roughly this value
 * rather than to the game's full requested duration. Every call re-arms, so a
 * long effect is a series of overlapping short ones and never stutters.
 *
 * The host caps at WC_RUMBLE_MAX_MS (5000) and stops the motors on its own
 * timer regardless, so this is the tighter of two independent guarantees. */
#define WC_RUMBLE_ARM_MS 120

/*---------------------------------------------------------------------------*/
/* Device state */

static wc_pad_t *wc_pads_ptr = NULL;

/* Instance ids are handed out once at Init and never reused. Pads are a fixed
 * array that the host fills in, so a pad "arriving" is a connected flag
 * flipping rather than a device enumerating, and a stable id per slot keeps
 * SDL_JoystickFromInstanceID() honest across a disconnect. */
static SDL_JoystickID wc_instance_ids[WC_MAX_PADS];
static SDL_bool wc_was_connected[WC_MAX_PADS];

/* When each pad's current arming window lapses, so the per-frame update can
 * re-arm on the way out rather than on every single frame. A host import is
 * cheap but not free, and a 60fps game holding a long effect would otherwise
 * issue sixty identical calls a second for one rumble. */
static Uint32 wc_rumble_armed_until[WC_MAX_PADS];

/* Axis/button counts. The layout mirrors a standard game controller so the
 * default SDL mapping below lines up without a controller database entry. */
#define WC_NUM_AXES    6   /* leftx, lefty, rightx, righty, ltrigger, rtrigger */
#define WC_NUM_BUTTONS 8   /* a, b, x, y, l1, r1, start, select */
#define WC_NUM_HATS    1   /* dpad */

void SDL_WASMCART_SetJoystickPads(void *pads)
{
    wc_pads_ptr = (wc_pad_t *)pads;
}

static int pad_index_for_joystick(SDL_Joystick *joystick)
{
    int i;
    for (i = 0; i < WC_MAX_PADS; ++i) {
        if (wc_instance_ids[i] == joystick->instance_id) {
            return i;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------*/
/* Driver entry points */

static int WASMCART_JoystickInit(void)
{
    int i;
    for (i = 0; i < WC_MAX_PADS; ++i) {
        wc_instance_ids[i] = SDL_GetNextJoystickInstanceID();
        wc_was_connected[i] = SDL_FALSE;
        wc_rumble_armed_until[i] = 0;
    }
    return 0;
}

/* Report every pad slot the host has marked connected.
 *
 * Device indices are positional, not compacted: pad 2 connected alone still
 * reports a count of 3 with slots 0 and 1 dead. Compacting would be tidier for
 * SDL_NumJoysticks(), but it would also mean device_index 0 silently refers to
 * a different physical pad depending on who else is plugged in -- and the cart
 * side of the rumble ABI is keyed by wasmcart pad id, so the two numbering
 * schemes have to agree. */
static int WASMCART_JoystickGetCount(void)
{
    int i, count = 0;
    if (!wc_pads_ptr) {
        return 0;
    }
    for (i = 0; i < WC_MAX_PADS; ++i) {
        if (wc_pads_ptr[i].connected) {
            count = i + 1;
        }
    }
    return count;
}

/* Push connect/disconnect events for pads whose connected flag changed since
 * the last poll. SDL_PrivateJoystickAdded is what makes JOYDEVICEADDED reach
 * the event queue, which is how a game notices a pad plugged in mid-run. */
static void WASMCART_JoystickDetect(void)
{
    int i;
    if (!wc_pads_ptr) {
        return;
    }
    for (i = 0; i < WC_MAX_PADS; ++i) {
        SDL_bool now = wc_pads_ptr[i].connected ? SDL_TRUE : SDL_FALSE;
        if (now == wc_was_connected[i]) {
            continue;
        }
        wc_was_connected[i] = now;
        if (now) {
            SDL_PrivateJoystickAdded(wc_instance_ids[i]);
        } else {
            SDL_PrivateJoystickRemoved(wc_instance_ids[i]);
        }
    }
}

static const char *WASMCART_JoystickGetDeviceName(int device_index)
{
    static const char *const names[WC_MAX_PADS] = {
        "wasmcart Pad 1", "wasmcart Pad 2", "wasmcart Pad 3", "wasmcart Pad 4"
    };
    if (device_index < 0 || device_index >= WC_MAX_PADS) {
        return NULL;
    }
    return names[device_index];
}

static const char *WASMCART_JoystickGetDevicePath(int device_index)
{
    (void)device_index;
    return NULL;
}

static int WASMCART_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index)
{
    (void)device_index;
    return -1;
}

static int WASMCART_JoystickGetDevicePlayerIndex(int device_index)
{
    /* Player index IS the pad slot here: wasmcart pad ids are 0-based and
     * already ordered by player. */
    if (device_index < 0 || device_index >= WC_MAX_PADS) {
        return -1;
    }
    return device_index;
}

static void WASMCART_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
    /* The host owns the slot ordering; a game cannot renumber pads. */
    (void)device_index;
    (void)player_index;
}

static SDL_JoystickGUID WASMCART_JoystickGetDeviceGUID(int device_index)
{
    /* A per-slot GUID rather than one shared across pads, so a game that keys
     * its saved bindings on GUID does not confuse pad 1 with pad 3. */
    return SDL_CreateJoystickGUID(SDL_HARDWARE_BUS_VIRTUAL, 0x1209, 0x7763,
                                  (Uint16)(device_index + 1), "wasmcart",
                                  WASMCART_JoystickGetDeviceName(device_index),
                                  0, 0);
}

static SDL_JoystickID WASMCART_JoystickGetDeviceInstanceID(int device_index)
{
    if (device_index < 0 || device_index >= WC_MAX_PADS) {
        return -1;
    }
    return wc_instance_ids[device_index];
}

static int WASMCART_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    if (device_index < 0 || device_index >= WC_MAX_PADS) {
        return SDL_SetError("No such wasmcart pad: %d", device_index);
    }
    if (!wc_pads_ptr || !wc_pads_ptr[device_index].connected) {
        return SDL_SetError("wasmcart pad %d is not connected", device_index);
    }

    joystick->nbuttons = WC_NUM_BUTTONS;
    joystick->naxes = WC_NUM_AXES;
    joystick->nhats = WC_NUM_HATS;
    joystick->instance_id = wc_instance_ids[device_index];
    /* Player index is not set here: SDL derives it through the driver's
     * GetDevicePlayerIndex, which already answers with the pad slot. */

    return 0;
}

/* SDL passes 16-bit motor magnitudes; the wasmcart ABI takes 0..1 floats.
 * The host clamps out-of-range values, so the conversion only has to be
 * monotonic, but dividing by 0xFFFF keeps full intensity at exactly 1.0. */
static int WASMCART_JoystickRumble(SDL_Joystick *joystick,
                                   Uint16 low_frequency_rumble,
                                   Uint16 high_frequency_rumble)
{
    int pad = pad_index_for_joystick(joystick);
    if (pad < 0) {
        return SDL_Unsupported();
    }
    if (!wc_pad_has_rumble((unsigned int)pad)) {
        /* Report the honest answer rather than pretending. A game that checks
         * the return value can fall back to a visual cue; SDL_Unsupported()
         * is what every other driver returns for a motorless pad. */
        return SDL_Unsupported();
    }
    if (!low_frequency_rumble && !high_frequency_rumble) {
        wc_rumble_armed_until[pad] = 0;
        wc_pad_rumble_stop((unsigned int)pad);
        return 0;
    }

    wc_pad_rumble((unsigned int)pad,
                  (float)low_frequency_rumble / 65535.0f,
                  (float)high_frequency_rumble / 65535.0f,
                  WC_RUMBLE_ARM_MS);
    wc_rumble_armed_until[pad] = SDL_GetTicks() + WC_RUMBLE_ARM_MS;
    return 0;
}

static int WASMCART_JoystickRumbleTriggers(SDL_Joystick *joystick,
                                           Uint16 left_rumble, Uint16 right_rumble)
{
    /* The wasmcart ABI carries two motors, not four: there is no trigger
     * rumble to route this at. An Xbox 360 pad reports the same shape. */
    (void)joystick;
    (void)left_rumble;
    (void)right_rumble;
    return SDL_Unsupported();
}

static Uint32 WASMCART_JoystickGetCapabilities(SDL_Joystick *joystick)
{
    int pad = pad_index_for_joystick(joystick);
    if (pad < 0) {
        return 0;
    }
    /* Capability is per-DEVICE and the host is the only one who knows, so ask
     * every time rather than caching at open: a pad can be swapped for one
     * with different hardware without the slot ever going empty. */
    return wc_pad_has_rumble((unsigned int)pad) ? SDL_JOYCAP_RUMBLE : 0;
}

static int WASMCART_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    (void)joystick; (void)red; (void)green; (void)blue;
    return SDL_Unsupported();
}

static int WASMCART_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size)
{
    (void)joystick; (void)data; (void)size;
    return SDL_Unsupported();
}

static int WASMCART_JoystickSetSensorsEnabled(SDL_Joystick *joystick, SDL_bool enabled)
{
    (void)joystick; (void)enabled;
    return SDL_Unsupported();
}

/* Push the current pad state. SDL_PrivateJoystick* only queue an event when
 * the value actually changed, so calling them unconditionally each poll is
 * both correct and cheap. */
static void WASMCART_JoystickUpdate(SDL_Joystick *joystick)
{
    int pad;
    const wc_pad_t *p;
    Uint16 buttons;
    Uint8 hat = SDL_HAT_CENTERED;

    if (!wc_pads_ptr) {
        return;
    }
    pad = pad_index_for_joystick(joystick);
    if (pad < 0) {
        return;
    }
    p = &wc_pads_ptr[pad];
    buttons = p->buttons;

    SDL_PrivateJoystickAxis(joystick, 0, p->left_x);
    SDL_PrivateJoystickAxis(joystick, 1, p->left_y);
    SDL_PrivateJoystickAxis(joystick, 2, p->right_x);
    SDL_PrivateJoystickAxis(joystick, 3, p->right_y);
    /* Triggers are 0..255 unsigned on the wasmcart side and -32768..32767 on
     * SDL's, where a released trigger reads as the minimum rather than zero. */
    SDL_PrivateJoystickAxis(joystick, 4,
        (Sint16)(((int)p->left_trigger * 257) - 32768));
    SDL_PrivateJoystickAxis(joystick, 5,
        (Sint16)(((int)p->right_trigger * 257) - 32768));

    SDL_PrivateJoystickButton(joystick, 0, (buttons & WC_BTN_A) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 1, (buttons & WC_BTN_B) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 2, (buttons & WC_BTN_X) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 3, (buttons & WC_BTN_Y) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 4, (buttons & WC_BTN_L1) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 5, (buttons & WC_BTN_R1) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 6, (buttons & WC_BTN_START) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 7, (buttons & WC_BTN_SELECT) ? SDL_PRESSED : SDL_RELEASED);

    if (buttons & WC_BTN_UP)    hat |= SDL_HAT_UP;
    if (buttons & WC_BTN_DOWN)  hat |= SDL_HAT_DOWN;
    if (buttons & WC_BTN_LEFT)  hat |= SDL_HAT_LEFT;
    if (buttons & WC_BTN_RIGHT) hat |= SDL_HAT_RIGHT;
    SDL_PrivateJoystickHat(joystick, 0, hat);

    /* Keep a live effect armed.
     *
     * The host stops the motors WC_RUMBLE_ARM_MS after the last call, which is
     * shorter than most effects a game asks for. SDL's own resend timer is two
     * seconds -- sized for hardware that forgets, not for a host watchdog --
     * so an effect longer than the arming window would go quiet mid-way and
     * come back. Re-arming from the per-frame update instead keeps the motor
     * continuous for exactly as long as SDL says the effect is still running,
     * and stops within one frame of its expiration.
     *
     * SDL_JoystickUpdate calls Update before it checks rumble_expiration, so a
     * finished effect would get one more arming window here and only then be
     * stopped by SDL. Checking the expiration first avoids that. */
    if (joystick->rumble_expiration &&
        (joystick->low_frequency_rumble || joystick->high_frequency_rumble)) {
        Uint32 now = SDL_GetTicks();
        int pad_slot = pad;
        if (!SDL_TICKS_PASSED(now, joystick->rumble_expiration) &&
            /* Re-arm on the way out of the current window, not every frame:
             * one host call per window is enough to keep the motor continuous,
             * and the half-window margin absorbs a slow frame. */
            SDL_TICKS_PASSED(now + (WC_RUMBLE_ARM_MS / 2),
                             wc_rumble_armed_until[pad_slot])) {
            WASMCART_JoystickRumble(joystick, joystick->low_frequency_rumble,
                                    joystick->high_frequency_rumble);
        }
    }
}

static void WASMCART_JoystickClose(SDL_Joystick *joystick)
{
    /* Leave the motors quiet behind a game that closes the pad mid-effect. */
    int pad = pad_index_for_joystick(joystick);
    if (pad >= 0) {
        wc_rumble_armed_until[pad] = 0;
        wc_pad_rumble_stop((unsigned int)pad);
    }
}

static void WASMCART_JoystickQuit(void)
{
    int i;
    for (i = 0; i < WC_MAX_PADS; ++i) {
        wc_was_connected[i] = SDL_FALSE;
    }
}

/* Hand SDL a built-in mapping so SDL_IsGameController() is true and
 * SDL_GameControllerOpen() works without a gamecontrollerdb entry. The layout
 * matches the one WASMCART_JoystickUpdate pushes above. */
static SDL_bool WASMCART_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    if (device_index < 0 || device_index >= WC_MAX_PADS) {
        return SDL_FALSE;
    }

    out->a.kind = EMappingKind_Button;      out->a.target = 0;
    out->b.kind = EMappingKind_Button;      out->b.target = 1;
    out->x.kind = EMappingKind_Button;      out->x.target = 2;
    out->y.kind = EMappingKind_Button;      out->y.target = 3;
    out->leftshoulder.kind = EMappingKind_Button;  out->leftshoulder.target = 4;
    out->rightshoulder.kind = EMappingKind_Button; out->rightshoulder.target = 5;
    out->start.kind = EMappingKind_Button;  out->start.target = 6;
    out->back.kind = EMappingKind_Button;   out->back.target = 7;

    out->dpup.kind = EMappingKind_Hat;      out->dpup.target = (0 << 4) | SDL_HAT_UP;
    out->dpdown.kind = EMappingKind_Hat;    out->dpdown.target = (0 << 4) | SDL_HAT_DOWN;
    out->dpleft.kind = EMappingKind_Hat;    out->dpleft.target = (0 << 4) | SDL_HAT_LEFT;
    out->dpright.kind = EMappingKind_Hat;   out->dpright.target = (0 << 4) | SDL_HAT_RIGHT;

    out->leftx.kind = EMappingKind_Axis;    out->leftx.target = 0;
    out->lefty.kind = EMappingKind_Axis;    out->lefty.target = 1;
    out->rightx.kind = EMappingKind_Axis;   out->rightx.target = 2;
    out->righty.kind = EMappingKind_Axis;   out->righty.target = 3;
    out->lefttrigger.kind = EMappingKind_Axis;  out->lefttrigger.target = 4;
    out->righttrigger.kind = EMappingKind_Axis; out->righttrigger.target = 5;

    return SDL_TRUE;
}

SDL_JoystickDriver SDL_WASMCART_JoystickDriver = {
    WASMCART_JoystickInit,
    WASMCART_JoystickGetCount,
    WASMCART_JoystickDetect,
    WASMCART_JoystickGetDeviceName,
    WASMCART_JoystickGetDevicePath,
    WASMCART_JoystickGetDeviceSteamVirtualGamepadSlot,
    WASMCART_JoystickGetDevicePlayerIndex,
    WASMCART_JoystickSetDevicePlayerIndex,
    WASMCART_JoystickGetDeviceGUID,
    WASMCART_JoystickGetDeviceInstanceID,
    WASMCART_JoystickOpen,
    WASMCART_JoystickRumble,
    WASMCART_JoystickRumbleTriggers,
    WASMCART_JoystickGetCapabilities,
    WASMCART_JoystickSetLED,
    WASMCART_JoystickSendEffect,
    WASMCART_JoystickSetSensorsEnabled,
    WASMCART_JoystickUpdate,
    WASMCART_JoystickClose,
    WASMCART_JoystickQuit,
    WASMCART_JoystickGetGamepadMapping
};

#endif /* SDL_JOYSTICK_WASMCART */

/* vi: set ts=4 sw=4 expandtab: */
