# Wasmcart Game Development & Porting Guide

How to write new games for wasmcart or port existing C/SDL games to the
wasmcart ABI using emscripten.

## Overview

Wasmcart carts can be distributed in two formats:
- **`.wasm`** — standalone WASM file with all assets embedded (v1, still supported)
- **`.wasc`** — ZIP archive containing `manifest.json` + `cart.wasm` + `assets/` (v2+, recommended for games with assets)

The current ABI version is **3**, which adds optional networking (WebSocket, data channels) and extended input (pointer, keyboard) on top of v2's asset loading. All v3 features are opt-in — v2 carts work unchanged.

Both formats export three functions:
- `wc_get_info()` — returns a pointer to an info struct describing the cart's memory layout (must return a pointer to a live struct, not a copy — host re-reads after init)
- `wc_init()` — called once at startup (after save data is loaded)
- `wc_render()` — called every frame at 60fps by the host

The cart owns its own memory. It declares a framebuffer, audio ring buffer, input pads,
save blob, and timing struct — all as static globals. `wc_get_info()` tells the host
where these are in memory.

The host writes gamepad input and timing data, then calls `wc_render()`. The cart reads
input, updates game state, writes pixels to its framebuffer, and writes audio samples
to its ring buffer. The host reads pixels and audio after `wc_render()` returns.

## Prerequisites

- **Emscripten SDK** (emcc) — install from https://emscripten.org
- **wasmcart.h** — the ABI header, plus the cart-author SDK (`wc_cart.h`, `wc_fb.h`,
  `wc_gl.h`, math/mixer helpers) from the main
  [wasmcart](https://github.com/wasmcart/wasmcart) repo's `include/`
- **wasmcart** — the CLI, for packing and running carts: `npx wasmcart`

## Choosing a Format

| | Bare `.wasm` | `.wasc` Archive |
|---|---|---|
| **Best for** | Small carts (< 1MB), no external assets | Games with textures, levels, audio, data files |
| **Asset handling** | Embed as C arrays in headers | Keep original files, load at runtime |
| **Build complexity** | Convert assets → C headers → compile | Compile code, then `wasmcart-pack` |
| **Max practical size** | ~100MB (WASM linear memory) | ~4GB (ZIP), assets loaded on demand |
| **Dev iteration** | Recompile for any asset change | Swap files and repack |

**Rule of thumb**: If your game has no assets or trivial assets (< 100KB), use bare `.wasm`.
If it has textures, levels, WADs, sprites, audio files — use `.wasc`.

## Quick Start

### 1. New Game with Shared Libraries

Using the shared headers, a new cart is minimal:

```c
#define WC_USE_GL               // or omit for 2D software-rendered cart
#include "wasmcart.h"
#include "wc_cart.h"            // buffer declarations + wc_get_info macro
#include "wc_math.h"            // sin, cos, sqrt — no libm needed
#include "wc_fb.h"              // 2D drawing (fill, blit, alpha blend)

#define DEFAULT_WIDTH  320
#define DEFAULT_HEIGHT 240
#define MAX_WIDTH  320
#define MAX_HEIGHT 240
#define AUDIO_CAP 4096

WC_CART_BUFFERS;                // declares all standard globals

WC_EXPORT wc_info_t *wc_get_info(void) {
    WC_FILL_INFO(WC_USE_AUDIO);
    return &wc_info;
}

WC_EXPORT_INIT void wc_init(void) {
    wc_audio_write_cursor = 0;
    // game init
}

WC_EXPORT_RENDER void wc_render(void) {
    // Read input from wc_pads[0]
    // Read timing from wc_time

    wc_fb_clear(wc_framebuffer, DEFAULT_WIDTH, DEFAULT_HEIGHT, 0x000000);
    wc_fb_fill(wc_framebuffer, DEFAULT_WIDTH, DEFAULT_HEIGHT,
               10, 10, 50, 30, WC_RGB(255, 0, 0));
}
```

### 2. Cross-Platform Game Structure

The portable shared libraries (`wc_math.h`, `wc_mat4.h`, `wc_fb.h`,
`wc_pcm_mixer.h`) have no wasmcart dependency. You can write game logic
that compiles to **both** wasmcart and native:

```
my_game/
├── game.h              # Game state, uses wc_math.h, wc_fb.h
├── game.c              # Game logic — pure C, no platform deps
├── platform_wasmcart.c # wasmcart entry (wc_get_info, wc_init, wc_render)
├── platform_sdl.c      # Native entry (SDL window, main loop, input)
├── build_wasm.sh       # emcc → game.wasm
└── build_native.sh     # gcc + SDL2 → game native binary
```

The game code writes to a `uint32_t*` framebuffer and an `int16_t*` audio
ring buffer. The platform layer provides those buffers and calls the game:

```c
// game.h — platform-independent
void game_init(void);
void game_update(float dt, uint16_t buttons, int stick_x, int stick_y);
void game_render(uint32_t *fb, int w, int h);
void game_audio(int16_t *ring, uint32_t cap, uint32_t *write_cur, int frames);

// platform_wasmcart.c — wasmcart target
WC_EXPORT_RENDER void wc_render(void) {
    float dt = wc_time.delta_ms / 1000.0f;
    game_update(dt, wc_pads[0].buttons, wc_pads[0].left_x, wc_pads[0].left_y);
    game_render(wc_framebuffer, wc_cur_width, wc_cur_height);
    game_audio(wc_audio_ring, AUDIO_CAP, &wc_audio_write_cursor, 800);
}

// platform_sdl.c — native target
int main() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    // create window, renderer...
    game_init();
    while (running) {
        // poll SDL events → buttons, stick_x, stick_y
        game_update(dt, buttons, stick_x, stick_y);
        game_render(pixels, width, height);
        // SDL_UpdateTexture + SDL_RenderCopy
    }
}
```

### 3. Minimal Cart (without shared libraries)

If you prefer to see all the boilerplate explicitly:

```c
#include "wasmcart.h"
#include <string.h>

#define WIDTH  320
#define HEIGHT 240

static uint32_t framebuffer[WIDTH * HEIGHT];
static int16_t  audio_ring[4096 * 2];  // stereo ring buffer
static uint32_t audio_write_cursor;
static wc_pad_t pads[4];
static wc_time_t time_info;
static wc_info_t info;
static uint8_t  save_data[64];

__attribute__((export_name("wc_get_info")))
wc_info_t *wc_get_info(void) {
    info.version        = WC_ABI_VERSION;
    info.width          = WIDTH;
    info.height         = HEIGHT;
    info.fb_ptr         = (uint32_t)framebuffer;
    info.audio_ptr      = (uint32_t)audio_ring;
    info.audio_cap      = 4096;
    info.audio_write_ptr = (uint32_t)&audio_write_cursor;
    info.input_ptr      = (uint32_t)pads;
    info.save_ptr       = (uint32_t)save_data;
    info.save_size      = 64;
    info.time_ptr       = (uint32_t)&time_info;
    return &info;
}

__attribute__((export_name("wc_init")))
void wc_init(void) {
    // Initialize game state here
    // save_data[] is already populated with persisted save if it exists
}

__attribute__((export_name("wc_render")))
void wc_render(void) {
    // Read input from pads[0]
    // Read timing from time_info
    // Update game logic
    // Write pixels to framebuffer[] (XRGB8888 format)
    // Write audio to audio_ring[] (S16 stereo, 48000Hz)
}
```

### 2. Build (no SDL)

```bash
emcc \
    -O2 \
    -s STANDALONE_WASM=1 \
    -s EXPORTED_FUNCTIONS="['_wc_init','_wc_render','_wc_get_info']" \
    -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
    -s TOTAL_MEMORY=4194304 \
    -s ALLOW_MEMORY_GROWTH=0 \
    --no-entry \
    cart.c \
    -o cart.wasm
```

### 3. Build (with SDL2_mixer audio)

```bash
PORTING_DIR="path/to/wasmcart/porting"

WRAP_FLAGS="\
    -Wl,--wrap=SDL_OpenAudioDevice \
    -Wl,--wrap=SDL_CloseAudioDevice \
    -Wl,--wrap=SDL_LockAudioDevice \
    -Wl,--wrap=SDL_UnlockAudioDevice \
    -Wl,--wrap=SDL_PauseAudioDevice \
    -Wl,--wrap=SDL_OpenAudio \
    -Wl,--wrap=SDL_CloseAudio \
    -Wl,--wrap=SDL_LockAudio \
    -Wl,--wrap=SDL_UnlockAudio \
    -Wl,--wrap=SDL_PauseAudio"

emcc \
    -O2 \
    -msimd128 \
    -s STANDALONE_WASM=1 \
    -s EXPORTED_FUNCTIONS="['_wc_init','_wc_render','_wc_get_info']" \
    -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
    -s TOTAL_MEMORY=16777216 \
    -s ALLOW_MEMORY_GROWTH=0 \
    -s USE_SDL=2 \
    -s USE_SDL_MIXER=2 \
    -s SDL2_MIXER_FORMATS='["ogg"]' \
    --no-entry \
    ${WRAP_FLAGS} \
    cart.c \
    "${PORTING_DIR}/audio_bridge.c" \
    "${PORTING_DIR}/emstubs.c" \
    -o cart.wasm
```

### 4. Package as .wasc (for games with assets)

```bash
# After building cart.wasm, package with assets:
wasmcart-pack --wasm cart.wasm --assets assets/ --name "My Game" -o mygame.wasc
```

The `.wasc` file is a ZIP containing:
```
mygame.wasc (ZIP)
├── manifest.json       # { name, version, abi, entry, assets }
├── cart.wasm           # compiled code (no embedded assets)
└── assets/
    ├── textures/
    │   └── player.png
    ├── levels/
    │   └── level1.dat
    └── audio/
        └── music.ogg
```

## Asset API (ABI v2)

For `.wasc` carts, the host provides two functions to load assets at runtime:

```c
#include "wasmcart.h"

// Query asset size (returns -1 if not found)
int size = WC_ASSET_SIZE("levels/level1.dat");

// Load asset into a buffer
static uint8_t level_buf[65536];
int loaded = WC_LOAD_ASSET("levels/level1.dat", level_buf, sizeof(level_buf));
if (loaded > 0) {
    // level_buf now contains the file data
    parse_level(level_buf, loaded);
}
```

### How It Works

- `wc_asset_size(path, path_len)` — returns the uncompressed size of an asset, or -1 if not found
- `wc_load_asset(path, path_len, dest, max_size)` — loads an asset into cart memory at `dest`, returns bytes copied or -1

Both are synchronous. The cart calls them like `fopen`/`fread` — the host decompresses from the ZIP on demand.

### File Listing (`_filelist.txt`)

The host automatically generates a virtual asset called `_filelist.txt` containing a newline-separated list of all asset paths in the archive. Carts can load this at init time to discover available files without hardcoding paths:

```c
int list_size = wc_asset_size("_filelist.txt", strlen("_filelist.txt"));
if (list_size > 0) {
    char *list = malloc(list_size + 1);
    wc_load_asset("_filelist.txt", strlen("_filelist.txt"), list, list_size);
    list[list_size] = '\0';
    // Parse newline-separated paths...
    free(list);
}
```

This is essential for games with virtual filesystems (like FLARE's VFS) that need to pre-register all available files at startup for directory listing to work.

**Important**: Always pass the correct string length to `wc_asset_size` and `wc_load_asset`. Using `strlen()` is recommended over hardcoded lengths to avoid off-by-one bugs.

### Asset Paths

Asset paths are relative to the `assets/` directory in the `.wasc` archive:
- Archive entry: `assets/textures/player.png`
- Cart requests: `textures/player.png`

Both with and without the `assets/` prefix work.

### Porting File I/O to Asset API

Games that use `fopen`/`fread` can be ported by replacing the file operations:

```c
// Original game code:
FILE *f = fopen("data/level1.dat", "rb");
fread(buf, 1, size, f);
fclose(f);

// Wasmcart equivalent:
int size = WC_ASSET_SIZE("data/level1.dat");
if (size > 0) {
    WC_LOAD_ASSET("data/level1.dat", buf, size);
}
```

For games with many file operations, create a shim layer:
```c
// Redirect fopen/fread to asset API
typedef struct { char path[256]; int path_len; } WCFILE;

WCFILE* wc_fopen(const char* path) {
    int size = wc_asset_size(path, strlen(path));
    if (size < 0) return NULL;
    WCFILE* f = malloc(sizeof(WCFILE));
    strncpy(f->path, path, 255);
    f->path_len = strlen(path);
    return f;
}

int wc_fread(WCFILE* f, void* buf, int size) {
    return wc_load_asset(f->path, f->path_len, buf, size);
}
```

### Example: Doom WAD Loading

See `examples/doom/w_file_asset.c` — loads the 28MB WAD from the .wasc archive
instead of a 145MB embedded C header:

```c
static unsigned char *wad_buffer = NULL;

static wad_file_t *W_Asset_OpenFile(char *path) {
    if (!wad_buffer) {
        int size = WC_ASSET_SIZE("freedoom1.wad");
        wad_buffer = malloc(size);
        WC_LOAD_ASSET("freedoom1.wad", wad_buffer, size);
    }
    // ... use wad_buffer as memory-mapped WAD
}
```

### Dev Mode: Directory Loading

During development, CartHost can load from a directory instead of a .wasc file:

```bash
# Run from a directory (no need to repack after every change)
retroemu path/to/my-game/

# my-game/ contains:
#   manifest.json
#   cart.wasm
#   assets/
#     textures/...
#     levels/...
```

## Detailed Porting Steps

### Step 1: Understand the Original Game

Before touching any code:
1. Clone and build the original game natively to verify it works
2. Identify the main loop structure (SDL_PollEvent / SDL_RenderPresent cycle)
3. Identify audio usage (SDL_mixer? OpenAL? Custom?)
4. Identify asset loading (files on disk? Embedded resources?)
5. Note the game's native resolution and target FPS
6. **Check for offline build tools** — level compilers, texture packers, mesh
   processors, or any tool that pre-processes assets into binary formats. These
   tools need to be built natively and may need real library implementations
   (see "Offline Build Tools" below)

### Step 2: Replace the Main Loop

The original game has a main loop like:
```c
while (running) {
    SDL_PollEvent(&event);   // <- replaced by reading pads[]
    update_game(dt);         // <- keep this
    render_game(renderer);   // <- replace with framebuffer writes
    SDL_RenderPresent(renderer); // <- remove
    SDL_Delay(16);           // <- remove (host controls timing)
}
```

In wasmcart, the host calls `wc_render()` at 60fps. Your job is to:
1. Remove the main loop
2. Move per-frame logic into `wc_render()`
3. Move one-time init into `wc_init()`

### Step 3: Replace Rendering

Replace all SDL rendering calls with direct framebuffer writes:

| SDL Call | Wasmcart Replacement |
|----------|---------------------|
| `SDL_RenderCopy(renderer, tex, src, dst)` | `blit_tile(framebuffer, dst_x, dst_y, pixels, w, h)` |
| `SDL_RenderClear(renderer)` | `memset(framebuffer, 0, sizeof(framebuffer))` |
| `SDL_SetRenderDrawColor` + `SDL_RenderFillRect` | Write XRGB8888 color directly to fb region |
| `SDL_RenderPresent(renderer)` | Remove (host reads fb after wc_render returns) |

**Framebuffer format**: XRGB8888 — each pixel is a `uint32_t`:
```c
// 0x00RRGGBB
uint32_t red   = 0x00FF0000;
uint32_t green = 0x0000FF00;
uint32_t blue  = 0x000000FF;
uint32_t white = 0x00FFFFFF;
```

**Blit helper** (copy a rectangular sprite to the framebuffer):
```c
static void blit_tile(uint32_t *fb, int fb_w, int fb_h,
                      int dx, int dy,
                      const uint32_t *pixels, int tw, int th,
                      uint32_t transparent_color)
{
    for (int y = 0; y < th; y++) {
        int fy = dy + y;
        if (fy < 0 || fy >= fb_h) continue;
        for (int x = 0; x < tw; x++) {
            int fx = dx + x;
            if (fx < 0 || fx >= fb_w) continue;
            uint32_t pix = pixels[y * tw + x];
            if (pix != transparent_color) {
                fb[fy * fb_w + fx] = pix;
            }
        }
    }
}
```

### Step 4: Replace Input

Replace SDL event polling with reading the wasmcart pad struct:

```c
wc_pad_t *pad = &pads[0];

// D-pad
int left  = (pad->buttons & WC_BTN_LEFT)  != 0;
int right = (pad->buttons & WC_BTN_RIGHT) != 0;
int up    = (pad->buttons & WC_BTN_UP)    != 0;
int down  = (pad->buttons & WC_BTN_DOWN)  != 0;

// Face buttons (W3C gamepad layout)
int btn_a = (pad->buttons & WC_BTN_A) != 0;  // bottom face button
int btn_b = (pad->buttons & WC_BTN_B) != 0;  // right face button
int btn_x = (pad->buttons & WC_BTN_X) != 0;  // left face button
int btn_y = (pad->buttons & WC_BTN_Y) != 0;  // top face button

// Analog sticks (int16_t, -32768 to 32767)
int stick_left_x = pad->left_x;   // negative = left
int stick_left_y = pad->left_y;   // negative = up
// Apply deadzone: if (abs(stick_left_x) < 8000) stick_left_x = 0;

// Shoulders and triggers
int l_shoulder = (pad->buttons & WC_BTN_L) != 0;
int r_shoulder = (pad->buttons & WC_BTN_R) != 0;
int l_trigger  = pad->left_trigger;   // 0-255
int r_trigger  = pad->right_trigger;  // 0-255
```

#### Pointer Input (ABI v3, opt-in)

For games that need mouse/touch (RTS, drawing, editors), declare `"pointer": true` in the manifest and set `WC_FLAG_POINTER` in your info flags. The host writes unified pointer state to `wc_pointer_t[10]` every frame — mouse is pointer 0, touch fingers fill slots 1+.

```c
// In wc_get_info():
WC_FILL_INFO(WC_FLAG_POINTER);

// In wc_render() — poll pointer state:
if (wc_pointers[0].active) {
    int mx = wc_pointers[0].x;
    int my = wc_pointers[0].y;
    int clicking = wc_pointers[0].buttons & 1;  // primary button
}

// Or use event callbacks (optional exports):
WC_EXPORT_NAME("wc_ptr_on_down")
void wc_ptr_on_down(uint32_t id, int16_t x, int16_t y, uint8_t button) {
    // button: 0=primary, 1=secondary, 2=middle
}
```

#### Keyboard Input (ABI v3, opt-in)

For games that need raw keyboard (typing, hotkeys, RTS), declare `"keyboard": true` in the manifest and set `WC_FLAG_KEYBOARD`. The host writes a 256-bit bitmask using USB HID scancodes.

```c
// In wc_get_info():
WC_FILL_INFO(WC_FLAG_KEYBOARD);

// In wc_render() — poll key state:
if (WC_KEY_IS_DOWN(wc_keys, WC_KEY_W)) { /* W held */ }
if (WC_KEY_IS_DOWN(wc_keys, WC_KEY_SPACE)) { /* space held */ }

// Or use event callbacks (optional exports):
WC_EXPORT_NAME("wc_kb_on_down")
void wc_kb_on_down(uint8_t keycode, uint8_t modifiers) {
    if (keycode == WC_KEY_ESCAPE) pause_game();
}
```

**Important:** When `"keyboard": true` is set, the host delivers raw key events to the cart instead of mapping keyboard to gamepad buttons. Gamepad input is still always available.

### Step 5: Handle Assets

**Option A: Embed in .wasm (small carts)**

Convert assets to C byte arrays. Do NOT use emscripten's `--embed-file`
(it pulls in a virtual filesystem that adds ~200KB+ and complexity).

```js
// BMP sprite sheet -> C header (Node.js)
const fs = require('fs');
const bmp = fs.readFileSync('sprites.bmp');
const offset = bmp.readUInt32LE(10);
const width = bmp.readInt32LE(18);
const height = bmp.readInt32LE(22);
// ... convert to uint32_t XRGB8888 array ...
```

**Option B: Load from .wasc (recommended for games with assets)**

Keep assets in their original format. Load at runtime:

```c
// In wc_init() or on first use:
static uint8_t sprite_data[SPRITE_SIZE];
int loaded = WC_LOAD_ASSET("sprites.bmp", sprite_data, sizeof(sprite_data));

// Parse BMP in memory and use the pixel data
```

Build the code-only .wasm, then pack:
```bash
emcc cart.c -o cart.wasm  # code only, no embedded assets
wasmcart-pack --wasm cart.wasm --assets assets/ -o mygame.wasc
```

### Step 6: Handle Audio

**Option A: No audio / Simple tone generation**
Write raw PCM samples directly to the ring buffer. See `examples/hello/hello.c`
for sine wave tone generation.

**Option B: SDL2_mixer via audio_bridge (hand-port, lightweight)**
Use the audio_bridge library from `wasmcart/porting/`. This intercepts
`SDL_OpenAudioDevice` via `--wrap` linker flags and resamples the mixer
output to the host rate. Best for hand-ports where you don't want the
full SDL2 wasmcart backend.

1. In your `wc_init()`:
```c
#include "audio_bridge.h"

void wc_init(void) {
    // Initialize SDL2 audio with dummy driver
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    Mix_Init(MIX_INIT_OGG);  // or 0 for WAV-only

    // Open mixer -- the --wrap overrides intercept this
    Mix_OpenAudio(22050, AUDIO_S16SYS, 1, 1024);

    // Initialize the resampler
    audio_bridge_init();

    // Load your audio assets from embedded byte arrays
    SDL_RWops *rw = SDL_RWFromConstMem(snd_data, snd_data_size);
    Mix_Chunk *sfx = Mix_LoadWAV_RW(rw, 1);
    // ...
}
```

2. In your `wc_render()`:
```c
#define SAMPLES_PER_FRAME 800  // 48000Hz / 60fps

void wc_render(void) {
    // Pump audio FIRST (even on skip frames)
    audio_bridge_pump(audio_ring, AUDIO_CAP, &audio_write_cursor, SAMPLES_PER_FRAME);

    // ... game logic and rendering ...
}
```

3. Add to your build.sh: `audio_bridge.c`, `emstubs.c`, `--wrap` flags,
   `-s USE_SDL=2`, `-s USE_SDL_MIXER=2`.

**Option B2: SDL2_mixer via wasmcart SDL2 backend (ES approach, recommended)**
Use the full SDL2 wasmcart backend (`porting/sdl2_wc/`). This compiles real
SDL2 with custom video/audio/input backends that target wasmcart's ABI.
The audio backend does **zero conversion and zero resampling** — it writes
the callback's native format/rate directly to the ring buffer. The host
adapts using native SDL or WebAudio (essentially free). See "Emscripten
SDL2 Backend Approach" below for details.

```c
// SDL_Init, Mix_OpenAudio, etc. work normally — no setup needed
SDL_InitSubSystem(SDL_INIT_AUDIO);
Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024);

// After audio opens, query the actual spec and update wc_info for the host:
extern void wasmcart_audio_get_spec(int *rate, int *channels, int *is_float);
int rate = 0, channels = 0, is_float = 0;
wasmcart_audio_get_spec(&rate, &channels, &is_float);
if (rate > 0) {
    audio_rate = rate;
    wc_info.audio_sample_rate = rate;
    if (is_float) wc_info.flags |= WC_FLAG_AUDIO_F32;
    else          wc_info.flags &= ~WC_FLAG_AUDIO_F32;
}

// In wc_render():
extern void wasmcart_audio_pump(void *ring, uint32_t cap, uint32_t *write_cur,
                                int rate, uint32_t delta_ms);
wasmcart_audio_pump(wc_audio_ring, AUDIO_CAP, &wc_audio_write_cursor, audio_rate, delta_ms);
```

The audio backend accepts whatever format/rate the game requests. The pump
invokes the SDL callback and writes its native output directly to the ring
buffer (with mono→stereo expansion if needed). The cart declares its actual
format and rate via `wc_info.flags` and `wc_info.audio_sample_rate`, and the
host opens its audio device to match.

**Important:** `wc_get_info()` must return a pointer to the live info struct
(e.g. `return &wc_info`), not copy it. The host re-reads the struct after
`wc_init()` returns to pick up the audio format/rate set during init. If
you copy the struct (e.g. `*info_out = wc_info`), the host will read stale
values and open audio in the wrong format.

**Option C: wc_pcm_mixer + stb_vorbis (WAV SFX + OGG music, lightweight)**

For games with WAV sound effects and OGG music, this approach adds only ~80KB
to the binary (vs ~2.3MB for SDL_mixer). Used by neverball and ETR ports.

```c
// In audio_wc.c — include implementations
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

#define WC_PCM_MIXER_IMPLEMENTATION
#include "wc_pcm_mixer.h"

// Load WAV sound effects
void load_sounds(void) {
    wc_mixer_init();
    unsigned char *data = load_asset("sounds/click.wav", &size);
    int slot = wc_mixer_load_wav(data, size);
    free(data);
}

// Play a sound effect
wc_mixer_play(slot, 0.8f /* volume */, 0 /* no loop */);

// Open OGG music for streaming
unsigned char *ogg_data = load_asset("music/theme.ogg", &size);
stb_vorbis *v = stb_vorbis_open_memory(ogg_data, size, &err, NULL);

// Per-frame mixing (TIME-BASED — do NOT use fixed rate/60!)
static unsigned int last_ms = 0;
void audio_mix_frame(void) {
    unsigned int now = time_info.time_ms;
    unsigned int delta = now - last_ms;
    if (delta == 0) return;
    if (delta > 100) delta = 100;
    last_ms = now;

    int frames = (int)((uint64_t)host_rate * delta / 1000);
    float temp[2048 * 2];
    memset(temp, 0, frames * 2 * sizeof(float));

    // Decode OGG music into temp (stb_vorbis_get_samples_float_interleaved)
    // Mix SFX channels on top

    // Write to ring buffer — CLAMP after mixing!
    for (int i = 0; i < frames; i++) {
        float L = temp[i * 2];
        float R = temp[i * 2 + 1];
        if (L >  1.0f) L =  1.0f;
        if (L < -1.0f) L = -1.0f;
        if (R >  1.0f) R =  1.0f;
        if (R < -1.0f) R = -1.0f;
        ring_buf[(w % cap) * 2]     = L;
        ring_buf[(w % cap) * 2 + 1] = R;
        w++;
    }
    *write_cursor = w;
}
```

**Important: Use time-based frame counts, not fixed.** `host_rate / 60`
assumes perfectly uniform 16.6ms frames. In practice, menu screens, loading,
and complex scenes have variable timing. Using `host_rate * delta_ms / 1000`
produces smooth audio regardless of frame timing. See `wc_pcm_mixer.h` docs.

**Important: Clamp after mixing.** When mixing multiple audio sources
additively (music + SFX + voice), the sum can exceed [-1.0, 1.0]. Unlike
Int16 (which has implicit type clamping), float values out of range cause
distortion/static. Always clamp to [-1.0, 1.0] before writing to the ring
buffer. `wc_mixer_mix_f32()` handles this automatically.

**Audio format: Float32 (default) vs Int16 (legacy)**

The audio ring buffer defaults to **Float32** stereo interleaved, with samples
normalized to [-1.0, 1.0]. This matches modern audio engines (WebAudio, SDL_mixer,
PulseAudio, CoreAudio, WASAPI) and avoids lossy format conversions.

When using `wc_cart.h`, the ring buffer is `float` by default and `WC_FLAG_AUDIO_F32`
is set automatically in the cart's flags:
```c
#include "wasmcart.h"
#include "wc_cart.h"
// wc_audio_ring is float[], WC_FILL_INFO sets WC_FLAG_AUDIO_F32
```

Write normalized float samples [-1.0, 1.0] to the ring buffer:
```c
void write_audio(float left, float right) {
    uint32_t idx = (audio_write_cursor % AUDIO_CAP) * 2;
    wc_audio_ring[idx]     = left;
    wc_audio_ring[idx + 1] = right;
    audio_write_cursor++;
}
```

`wc_pcm_mixer.h` provides `wc_mixer_mix_f32()` for Float32 output:
```c
wc_mixer_mix_f32(wc_audio_ring, AUDIO_CAP, &audio_write_cursor, frames);
```

**Legacy Int16 mode:** For carts that produce Int16 PCM directly (simple tone
generators, 8-bit retro audio), define `WC_AUDIO_FORMAT_I16` before including
`wc_cart.h`:
```c
#define WC_AUDIO_FORMAT_I16   // opt into legacy int16 ring buffer
#include "wasmcart.h"
#include "wc_cart.h"
// wc_audio_ring is int16_t[], WC_FLAG_AUDIO_F32 is NOT set
```
Use `wc_mixer_mix()` (the original I16 variant) with this mode.

### Step 7: Handle Frame Rate

The host calls `wc_render()` at 60fps. If your game runs at 30fps, skip game logic
on alternate frames:

```c
static int frame_counter = 0;

void wc_render(void) {
    // Always pump audio (every frame)
    audio_bridge_pump(audio_ring, AUDIO_CAP, &audio_write_cursor, SAMPLES_PER_FRAME);

    frame_counter++;
    if (frame_counter % 2 != 0) {
        return;  // skip game logic, keep last frame's pixels
    }

    // Game logic runs at 30fps from here
    update_game();
    render_game();
}
```

For variable-timestep games, use `time_info.delta_ms`:
```c
void wc_render(void) {
    float dt = (float)time_info.delta_ms / 1000.0f;
    update_game(dt);  // pass seconds since last frame
    render_game();
}
```

### Step 8: Handle Resolution

The host tells the cart its preferred resolution via `wc_host_info_t`. The cart decides its actual rendering resolution and returns it via `wc_info_t.width` and `wc_info_t.height`. The host is responsible for scaling the output to fit its display with letterboxing. See the Resolution Negotiation section in the main wasmcart README for the full spec.

For GL carts, use `wc_negotiate_resolution()` from `wc_gl.h` to read the host preference and clamp to your max:

```c
wc_negotiate_resolution(&wc_host_info, &wc_cur_width, &wc_cur_height, MAX_WIDTH, MAX_HEIGHT);
```

For 2D framebuffer carts at fixed resolutions:

| Game Resolution | Strategy |
|----------------|----------|
| 320x240 | Direct -- use as-is |
| 320x200 | Letterbox -- offset Y by 20px |
| 128x128 | Scale 2x to 256x256, center in 320x240 (32px X offset) |
| 300x220 | Center -- 10px X offset, 10px Y offset |

For scaling, use an intermediate buffer at the game's native resolution, then
scale to the output framebuffer:

```c
#define GAME_W 128
#define GAME_H 128
#define OUT_W  320
#define OUT_H  240
#define SCALE  2

static uint8_t game_screen[GAME_W * GAME_H];  // palette-indexed
static uint32_t framebuffer[OUT_W * OUT_H];

static void scale_to_framebuffer(void) {
    memset(framebuffer, 0, sizeof(framebuffer));
    int off_x = (OUT_W - GAME_W * SCALE) / 2;

    for (int y = 0; y < GAME_H; y++) {
        for (int s = 0; s < SCALE; s++) {
            int dy = y * SCALE + s;
            if (dy >= OUT_H) break;
            for (int x = 0; x < GAME_W; x++) {
                uint32_t color = palette[game_screen[y * GAME_W + x]];
                for (int sx = 0; sx < SCALE; sx++) {
                    framebuffer[dy * OUT_W + off_x + x * SCALE + sx] = color;
                }
            }
        }
    }
}
```

## Networking (ABI v3)

Carts can open WebSocket connections and communicate peer-to-peer via data channels. All networking is opt-in via manifest fields.

### WebSocket

Declare allowed domains in the manifest:
```json
{ "net": { "websocket": ["api.mygame.com", "leaderboard.example.com"] } }
```

Cart imports (calls into host):
```c
#define WC_USE_NET_WS
#include "wasmcart.h"

// Open, send, close — all synchronous calls
int32_t conn = wc_ws_open("wss://api.mygame.com/game", 34);
wc_ws_send_text(conn, json_buf, json_len);
wc_ws_close(conn, 1000);
```

Cart exports (host calls into cart — all optional):
```c
void wc_ws_on_open(int32_t conn_id) { /* connected */ }
void wc_ws_on_message(int32_t conn_id, const void* data, uint32_t len) { /* binary */ }
void wc_ws_on_message_text(int32_t conn_id, const char* str, uint32_t len) { /* text */ }
void wc_ws_on_close(int32_t conn_id, uint32_t code) { /* closed */ }
void wc_ws_on_error(int32_t conn_id) { /* error */ }
```

Events are delivered at the start of each frame, before `wc_render()`. The host validates URLs against the manifest allowlist — connections to non-listed domains are rejected.

### Data Channels (Peer-to-Peer)

Declare in the manifest:
```json
{ "net": { "data-channel": true } }
```

The host manages connections (WebRTC, TCP relay, etc.) and exposes them as peer IDs:
```c
#define WC_USE_NET_DC
#include "wasmcart.h"

int32_t count = wc_dc_peer_count();
wc_dc_send(peer_id, data, len);
wc_dc_broadcast(data, len);  // send to all peers
```

Cart exports (optional):
```c
void wc_dc_on_connect(int32_t peer_id, const char* label, uint32_t label_len) {}
void wc_dc_on_message(int32_t peer_id, const void* data, uint32_t len) {}
void wc_dc_on_disconnect(int32_t peer_id) {}
```

## OpenGL ES 3.0 Carts

Wasmcart supports GPU-accelerated rendering via OpenGL ES 3.0. GL carts issue standard
GLES3 draw calls — the host provides real GPU rendering on all platforms (native EGL on
Linux, ANGLE on macOS/Windows, WebGL2 in browsers).

### Enabling GL

Define `WC_USE_GL` before including `wasmcart.h`:

```c
#define WC_USE_GL
#include "wasmcart.h"
```

This adds `~100` GL function declarations as WASM imports from the `gl` module. Your cart
code uses standard `glBindBuffer()`, `glDrawArrays()`, etc. — identical to any GLES3 C code.

### GL Cart Structure

A GL cart still exports `wc_get_info`, `wc_init`, `wc_render`. The framebuffer in
`wc_get_info` is used by the host for readback (terminal mode) but the cart does NOT
need to write pixels to it — the host reads pixels from the GPU via `glReadPixels`.

```c
#define WC_USE_GL
#include "wasmcart.h"

#define WIDTH  320
#define HEIGHT 240

static uint32_t framebuffer[WIDTH * HEIGHT];  // host uses for readback
static wc_pad_t pads[4];
static wc_time_t time_info;
static wc_info_t info;
// ... audio ring buffer, etc.

static GLuint program, vao, vbo;

void wc_init(void) {
    // Compile shaders, create buffers, load textures — standard GL setup
    program = link_program(vertex_src, fragment_src);
    // ...
    glViewport(0, 0, WIDTH, HEIGHT);
    glEnable(GL_DEPTH_TEST);
}

void wc_render(void) {
    float t = (float)(time_info.time_ms / 1000.0);

    glClearColor(0.1, 0.1, 0.12, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(program);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    // Host calls glReadPixels after this returns (if needed for terminal display)
}
```

### Loading Textures from .wasc

Use the asset API + a single-header image decoder like [stb_image](https://github.com/nothings/stb):

```c
#define WC_USE_GL
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG      // only include JPEG decoder (saves ~200KB)
#define STBI_NO_STDIO       // no FILE* usage (WASM has no filesystem)
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "wasmcart.h"
#include "stb_image.h"

#define MAX_TEX_FILE (256 * 1024)
static unsigned char tex_file_buf[MAX_TEX_FILE];

void wc_init(void) {
    // ... shader setup ...

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int file_size = WC_LOAD_ASSET("texture.jpg", tex_file_buf, MAX_TEX_FILE);
    if (file_size > 0) {
        int tw, th, channels;
        unsigned char* pixels = stbi_load_from_memory(tex_file_buf, file_size, &tw, &th, &channels, 4);
        if (pixels) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
            stbi_image_free(pixels);
        }
    }
}
```

**Key points:**
- Use `STBI_NO_STDIO` — WASM has no filesystem, stb_image must decode from memory
- Use `STBI_ONLY_JPEG` (or `STBI_ONLY_PNG`, etc.) to minimize binary size
- Load JPEG/PNG from .wasc archive at runtime instead of embedding raw pixels
- This approach: 44KB WASM + 46KB JPEG = 89KB .wasc. Embedding raw pixels: 268KB WASM.

### Build Flags for GL Carts

```bash
emcc hello_gl.c -O2 \
    -s STANDALONE_WASM=1 \
    -s EXPORTED_FUNCTIONS='["_wc_init","_wc_render","_wc_get_info"]' \
    -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
    -s ALLOW_MEMORY_GROWTH=1 \
    --no-entry \
    -o hello_gl.wasm
```

`ERROR_ON_UNDEFINED_SYMBOLS=0` is required — GL functions are imported from the host at
runtime, not linked at compile time. `ALLOW_MEMORY_GROWTH=1` is recommended for GL carts
that use stb_image or other allocating libraries.

### Minimal Math Without libm

WASM standalone mode doesn't always include libm. For basic 3D carts, implement your own
sin/cos approximation:

```c
static float my_sin(float x) {
    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318530f;
    x = x - (int)(x / TWO_PI) * TWO_PI;
    if (x > PI) x -= TWO_PI;
    if (x < -PI) x += TWO_PI;
    float abs_x = x < 0 ? -x : x;
    return 16.0f * x * (PI - abs_x) /
           (5.0f * PI * PI - 4.0f * x * (PI - abs_x));
}

static float my_cos(float x) { return my_sin(x + 1.5707963f); }
```

This avoids pulling in the full math library and keeps the WASM binary small.

### Gamepad Input in GL Carts

GL carts read input the same way as software carts — from the `pads[]` struct:

```c
wc_pad_t *pad = &pads[0];
float dt = (float)(time_info.delta_ms / 1000.0);
float speed = 3.0f * dt;

// D-pad / left stick: move object
if (pad->buttons & WC_BTN_LEFT)  obj_x -= speed;
if (pad->buttons & WC_BTN_RIGHT) obj_x += speed;
if (pad->left_x > 4000 || pad->left_x < -4000)
    obj_x += (float)pad->left_x / 32767.0f * speed * 2.0f;

// Right stick: rotate
if (pad->right_x > 4000 || pad->right_x < -4000)
    rot_y += (float)pad->right_x / 32767.0f * dt * 3.0f;
```

### Running GL Carts

```bash
# SDL window (recommended for GL carts)
retroemu --video sdl examples/hello_gl/hello_gl.wasc

# Terminal mode (uses glReadPixels readback → chafa)
retroemu examples/hello_gl/hello_gl.wasc

# Bare .wasm (no assets — textures will use fallback)
retroemu --video sdl examples/hello_gl/hello_gl.wasm
```

## Common Problems

### GL Cart Display: Horizontally Mirrored Text / Glyphs

If text glyphs appear horizontally mirrored but the overall scene layout looks
correct, the problem is almost certainly in the **font rendering code**, not
the display pipeline.

**Most likely cause**: Wrong bit order when reading bitmap font data. The
font8x8 data commonly used in retro/embedded projects uses LSB-left convention:

```c
// CORRECT — LSB-left (font8x8 convention):
if (bits & (1 << x))

// WRONG — MSB-left (causes mirrored glyphs):
if (bits & (0x80 >> x))
```

**Do NOT** try to fix mirrored glyphs by:
- Adding `p.x = -p.x` to vertex shaders — this flips vertex positions but
  not UV coordinates, so every textured quad shows mirrored content while
  the scene layout appears correct
- Changing the host display pixel format
- Adding horizontal flip to the readback pipeline

These hacks may appear to fix one symptom while creating new problems.
Check the font/sprite rendering code first — the simplest explanation is
usually right.

### "Cart imports unknown module" / "Cart imports unknown function"

CartHost allows `env`, `wasi_snapshot_preview1`, and `gl` module imports. If your
WASM has other imports, you need stubs. The `emstubs.c` file handles the common
emscripten/EGL/GL stubs. If you get new undefined symbols, add no-op stubs.

The allowed `env` imports are: `wc_log`, `wc_asset_size`, `wc_load_asset`,
`emscripten_notify_memory_growth` (no-op, added automatically by emscripten when
`ALLOW_MEMORY_GROWTH=1` is set).

The `gl` module imports are allowed for GL carts (any GLES3 function declared
with `WC_USE_GL`). The host provides these via native-gles or WebGL2.

### "Cart uses GL but no glBackend was provided"

The cart imports GL functions but the host wasn't configured with a GL backend.
When using retroemu, GL detection is automatic. When using CartHost directly:

```javascript
import gl from 'native-gles';
gl.createContext(320, 240);  // create EGL pbuffer
const cart = new CartHost();
await cart.load('cart.wasc', { glBackend: gl });
```

### Emscripten's printf/snprintf pulls in WASI

This is expected. Emscripten's libc implements stdio via WASI fd_write/fd_seek/fd_close.
CartHost provides safe no-op stubs for all WASI imports. No action needed from the
cart side — just don't try to actually read/write files.

### "memory access out of bounds" at runtime

Usually means:
- TOTAL_MEMORY is too low — increase it (8MB, 16MB, etc.)
- A pointer calculation overflowed — check your buffer sizes
- An asset array is too large for the memory space

### SDL_mixer not finding audio format

Make sure you specified the format in the build:
- WAV only: no special flags needed
- OGG Vorbis: `-s SDL2_MIXER_FORMATS='["ogg"]'`
- MP3: `-s SDL2_MIXER_FORMATS='["mp3"]'`

### Game runs at 2x speed

Your game probably runs at 30fps but the host calls at 60fps. Add frame skipping
(see Step 7 above).

### Audio is choppy, clicks, or screeches

- Make sure you call audio mixing every frame (including skip frames)
- Ring buffer too small: increase AUDIO_CAP (4096 is ~85ms at 48000Hz)
- **Format mismatch**: If `wc_get_info()` copies the info struct instead of
  returning a pointer, the host will read stale audio flags after `wc_init()`.
  The host re-reads the info struct after init to pick up runtime-set values
  like `audio_sample_rate` and `flags`. Always use `return &wc_info;` — never
  `*info_out = wc_info;`.
- **Missing `wasmcart_audio_get_spec()`**: ES carts must call this after SDL
  audio opens to set `wc_info.audio_sample_rate` and clear/set `WC_FLAG_AUDIO_F32`
  based on the actual callback format. Without it, the host may open audio in
  the wrong format.
- **Most common cause**: Using fixed `sample_rate / 60` for samples per frame.
  Frame timing is NOT perfectly uniform — menus and loading screens have variable
  frame durations. Use time-based mixing instead: `(uint64_t)rate * delta_ms / 1000`
  (see Step 6 Option C above)

### wc_load_asset returns -1

- Check the asset path matches what's in the .wasc archive (case-sensitive)
- Asset paths are relative to `assets/` in the archive
- Verify you're passing the correct path length — use `strlen()` rather than hardcoded values
- Run with `retroemu` to see [cart] log messages for debugging

### VFS / directory listing not finding files

If your game uses a virtual filesystem that needs to list directory contents, pre-register
all files at init time by loading `_filelist.txt` (see "File Listing" section above).
Without pre-registration, only files explicitly requested by exact path will be found.

### Link fails: undefined `wc_sdl_gl_blit` / `gl4es_bridge_set_size` / `wc_gl4es_GetProcAddress`

The video backend references these unconditionally at link time even though it
only *calls* them behind runtime guards, so they must resolve even for a cart
that never touches GL.

- `wc_sdl_gl_blit` is provided by `sdl2_wc/sdl2_gl_blit.c` — compile and link
  that file alongside your cart.
- The two `gl4es_*` symbols come from gl4es, which only GL1.x ports link. A
  2D-only cart can satisfy them with no-op stubs:

```c
/* gl4es_stub.c — a 2D-only cart never reaches these. */
void  gl4es_bridge_set_size(int w, int h) { (void)w; (void)h; }
void *wc_gl4es_GetProcAddress(const char *p) { (void)p; return 0; }
```

### `emcc` can't find `SDL.h`

`-sUSE_SDL=0` is a *link*-time flag. If you pass it while compiling, Emscripten
substitutes its `fakesdl` headers and every SDL type goes undeclared. Compile
with `-sUSE_SDL=2` (real headers) and only switch to `-sUSE_SDL=0` on the link
step, where it stops Emscripten's own SDL2 from competing with `libSDL2_wc.a`.
Doing both in one `emcc` invocation does not work — split it into a compile
step and a link step.

## Shared Libraries

The `porting/include/` directory contains reusable single-header C libraries for
game development. Most have **no wasmcart dependency** — they're plain C that works
anywhere. You can use them to:

- **Write a new game from scratch** that compiles to both wasmcart and native
- **Port an existing game** to wasmcart without reinventing math, rendering, and audio
- **Build cross-platform** — same game code compiles with emscripten (wasmcart target)
  or gcc/clang (native target with SDL/GL)

| Header | Portable? | Dependencies |
|--------|-----------|-------------|
| `wc_math.h` | Yes — pure C math | None |
| `wc_mat4.h` | Yes — matrix ops | wc_math.h |
| `wc_vec3.h` | Yes — vector ops | wc_math.h |
| `wc_fb.h` | Yes — software rendering to any `uint32_t*` buffer | stdint.h |
| `wc_pcm_mixer.h` | Yes — writes to any S16 ring buffer | stdint.h, stdlib.h |
| `wc_gl.h` | Yes — works with any GLES3/GL3 headers | GL types |
| `wc_cart.h` | **wasmcart only** — cart boilerplate | wasmcart.h |
| `wc_sdl_stubs.h` | **wasmcart only** — replaces SDL types | None |
| `stb_image.h` | Yes — image decoder (PNG, JPEG, etc.) | None |
| `stb_truetype.h` | Yes — TTF font rasterizer | None |
| `stb_vorbis.c` | Yes — OGG Vorbis audio decoder | None |

For a cross-platform game, use the portable headers for all game logic, then
swap only the platform layer: `wc_cart.h` for wasmcart, SDL for native.

### Directory Layout

```
porting/
├── include/              # Shared headers (add -I porting/include to your build)
│   ├── wc_cart.h         # Cart boilerplate (buffer decls, wc_get_info macro)
│   ├── wc_fb.h           # 2D framebuffer drawing (fill, blit, alpha blend)
│   ├── wc_gl.h           # GL utilities (shader compile/link, texture, VAO/VBO, RNG)
│   ├── wc_math.h         # Math without libm (sin, cos, sqrt, atan2, etc.)
│   ├── wc_mat4.h         # 4x4 column-major matrix ops for GL carts
│   ├── wc_vec3.h         # 3D vector operations
│   ├── wc_pcm_mixer.h    # Multi-channel PCM audio mixer + WAV parser
│   ├── wc_sdl_stubs.h    # Minimal SDL2 type stubs for porting SDL games
│   ├── stb_image.h       # Image decoder — PNG, JPEG, BMP, etc. (nothings/stb)
│   ├── stb_truetype.h    # TTF font rasterizer (nothings/stb)
│   └── stb_vorbis.c      # OGG Vorbis audio decoder (nothings/stb)
├── audio_bridge.h        # SDL2_mixer → ring buffer bridge
├── audio_bridge.c        # SDL2_mixer bridge implementation
└── emstubs.c             # ~200 no-op stubs for emscripten/EGL/GL symbols
```

### Using Shared Headers

Add `-I /path/to/wasmcart/porting/include` to your build flags:

```bash
FLAGS="-O2 -s STANDALONE_WASM=1 -I ../../porting/include"
```

### `wc_math.h` — Math without libm

Provides trig, sqrt, and utility functions that don't require linking libm.
All functions are `static inline` — just include the header, no .c file needed.

```c
#include "wc_math.h"

float angle = wc_sinf(WC_PI * 0.5f);   // ≈ 1.0
float dist = wc_sqrtf(x*x + y*y);
float a = wc_atan2f(dy, dx);
float v = wc_clampf(speed, 0.0f, 10.0f);
float t = wc_lerpf(start, end, 0.5f);
```

Functions: `wc_sinf`, `wc_cosf`, `wc_tanf`, `wc_sqrtf`, `wc_atan2f`,
`wc_fabsf`, `wc_fmodf`, `wc_floorf`, `wc_clampf`, `wc_lerpf`, `wc_signf`,
`wc_minf`, `wc_maxf`.

Constants: `WC_PI`, `WC_TWO_PI`, `WC_HALF_PI`, `WC_DEG2RAD`, `WC_RAD2DEG`.

### `wc_mat4.h` — 4x4 Matrix Operations (requires wc_math.h)

Column-major 4x4 matrices for OpenGL-style rendering. Follows GL conventions.

```c
#include "wc_math.h"
#include "wc_mat4.h"

wc_mat4 proj, view, model;
wc_mat4_perspective_deg(proj, 45.0f, aspect, 0.1f, 100.0f);
wc_mat4_look_at(view, 0,5,10, 0,0,0, 0,1,0);
wc_mat4_identity(model);
wc_mat4_translate(model, x, y, z);
wc_mat4_rotate(model, angle_deg, 0, 1, 0);  // matches glRotatef

wc_mat4 mvp;
wc_mat4_multiply(mvp, proj, view);
wc_mat4_multiply(mvp, mvp, model);  // safe to alias
```

Functions: `wc_mat4_identity`, `wc_mat4_copy`, `wc_mat4_multiply`,
`wc_mat4_translate`, `wc_mat4_scale`, `wc_mat4_rotate` (degrees, axis-angle),
`wc_mat4_perspective` (radians), `wc_mat4_perspective_deg` (degrees),
`wc_mat4_ortho`, `wc_mat4_look_at`,
`wc_mat4_rotate_x/y/z` (radians, write from scratch),
`wc_mat4_from_translation`, `wc_mat4_from_scale`, `wc_mat4_is_identity`.

### `wc_vec3.h` — 3D Vector Operations (requires wc_math.h)

```c
#include "wc_math.h"
#include "wc_vec3.h"

wc_vec3 pos = wc_v3(1, 2, 3);
wc_vec3 dir = wc_v3_normalize(wc_v3_sub(target, pos));
float d = wc_v3_dot(dir, normal);
wc_vec3 right = wc_v3_cross(forward, up);
```

Functions: `wc_v3`, `wc_v3_add`, `wc_v3_sub`, `wc_v3_scale`, `wc_v3_neg`,
`wc_v3_dot`, `wc_v3_cross`, `wc_v3_length`, `wc_v3_length_sq`,
`wc_v3_normalize`, `wc_v3_lerp`, `wc_v3_distance`.

### `wc_gl.h` — GL Utilities (requires wasmcart.h with WC_USE_GL)

Common OpenGL ES 3.0 boilerplate shared across 10+ GL carts.

```c
#define WC_USE_GL
#include "wasmcart.h"
#include "wc_gl.h"

// Shader compilation + linking (was copy-pasted in 10 carts):
GLuint prog = wc_link_program(vs_source, fs_source);

// Resolution negotiation (was copy-pasted in 10+ carts):
wc_negotiate_resolution(&host_info, &cur_width, &cur_height, MAX_WIDTH, MAX_HEIGHT);

// Common GL state:
wc_gl_setup_2d(cur_width, cur_height);  // viewport + blend + no depth
wc_gl_setup_3d(cur_width, cur_height);  // viewport + blend + depth test

// VAO/VBO creation:
GLuint vao, vbo;
vbo = wc_create_dynamic_vbo(&vao, MAX_VERTS * stride);
vbo = wc_create_static_vbo(&vao, vertex_data, sizeof(vertex_data));

// Texture upload from decoded pixels:
GLuint tex = wc_create_texture_rgba(pixels, w, h);    // linear filtering
GLuint tex = wc_create_texture_nearest(pixels, w, h); // pixel art

// RNG (xorshift32, was duplicated in 5+ carts):
wc_srand(time_info.time_ms);
float r = wc_randf();                    // [0, 1)
float speed = wc_randf_range(1.0f, 5.0f); // [1, 5)
int idx = wc_rand_range(10);             // [0, 10)
```

### `wc_cart.h` — Cart Boilerplate Macros (requires wasmcart.h)

Reduces the ~40 lines of buffer declarations + `wc_get_info()` that
every cart copy-pastes.

```c
#include "wasmcart.h"
#include "wc_cart.h"

#define DEFAULT_WIDTH  640
#define DEFAULT_HEIGHT 480
#define MAX_WIDTH  1920
#define MAX_HEIGHT 1080
#define AUDIO_CAP 4096

// One macro declares all standard globals:
//   wc_cur_width, wc_cur_height, wc_framebuffer[],
//   wc_audio_ring[], wc_audio_write_cursor,
//   wc_pads[4], wc_time, wc_info, wc_host_info,
//   wc_pointers[10], wc_keys[32]
WC_CART_BUFFERS;

// wc_get_info in ~3 lines instead of ~15:
// IMPORTANT: must return &wc_info (pointer to live struct), NOT copy.
// The host re-reads after wc_init() to pick up runtime audio format/rate.
WC_EXPORT wc_info_t *wc_get_info(void) {
    WC_FILL_INFO(WC_USE_GL | WC_USE_AUDIO);
    return &wc_info;
}

// For save data, set save_ptr/save_size after WC_FILL_INFO:
WC_EXPORT wc_info_t *wc_get_info(void) {
    WC_FILL_INFO(WC_USE_AUDIO);
    wc_info.save_ptr  = (uint32_t)(uintptr_t)save_data;
    wc_info.save_size = sizeof(save_data);
    return &wc_info;
}

// For pointer + keyboard input (ABI v3):
WC_EXPORT wc_info_t *wc_get_info(void) {
    WC_FILL_INFO(WC_FLAG_POINTER | WC_FLAG_KEYBOARD);
    return &wc_info;
}
```

Also provides `WC_EXPORT_INIT` and `WC_EXPORT_RENDER` attribute shorthands.

### `wc_fb.h` — 2D Framebuffer Drawing Primitives

Software rendering functions that replace SDL_RenderCopy, SDL_FillRect, and
SDL_BlitSurface for 2D carts. All operate on the `uint32_t` XRGB8888 framebuffer.

```c
#include "wc_fb.h"

// Fill rect (solid):
wc_fb_fill(framebuffer, WIDTH, HEIGHT, x, y, w, h, WC_RGB(255, 0, 0));

// Fill rect with alpha blending:
wc_fb_fill_alpha(framebuffer, WIDTH, HEIGHT, x, y, w, h, color, 128);

// Clear entire screen:
wc_fb_clear(framebuffer, WIDTH, HEIGHT, 0x000000);

// Blit indexed sprite with palette (index 0 = transparent):
wc_fb_blit_indexed(framebuffer, WIDTH, HEIGHT,
                   sprite_data, sprite_stride, palette,
                   sx, sy, sw, sh, dx, dy);

// Blit XRGB pixels with color key transparency:
wc_fb_blit(framebuffer, WIDTH, HEIGHT, src, src_w,
           sx, sy, sw, sh, dx, dy, 0xFF00FF);  // magenta = transparent

// Blit RGBA pixels with per-pixel alpha:
wc_fb_blit_rgba(framebuffer, WIDTH, HEIGHT, src, src_w,
                sx, sy, sw, sh, dx, dy);

// Additive blending (particles, effects):
wc_fb_blit_add(framebuffer, WIDTH, HEIGHT, src, src_w,
               sx, sy, sw, sh, dx, dy);

// Lines and outlines:
wc_fb_hline(framebuffer, WIDTH, HEIGHT, x, y, w, color);
wc_fb_vline(framebuffer, WIDTH, HEIGHT, x, y, h, color);
wc_fb_rect(framebuffer, WIDTH, HEIGHT, x, y, w, h, color);  // outline
```

Color helpers: `WC_RGB(r,g,b)`, `WC_RGBA(r,g,b,a)`, `WC_R(c)`, `WC_G(c)`,
`WC_B(c)`, `WC_A(c)`.

### `wc_sdl_stubs.h` — SDL2 Type Stubs for Porting

Minimal SDL2 type definitions and no-op function stubs for porting games that
reference SDL types throughout their codebase. Provides the types needed to
compile without linking actual SDL2.

```c
// Option A: Include directly
#include "wc_sdl_stubs.h"

// Option B: Symlink or copy as "SDL.h" so existing #include "SDL.h" works
```

Provides: `Uint8/16/32/64`, `Sint8/16/32/64`, `SDL_Rect`, `SDL_Color`,
`SDL_Point`, `SDL_bool`, `SDL_mutex/SDL_cond` stubs, `SDL_Init`, `SDL_Quit`,
`SDL_GetTicks`, `SDL_Delay`, `SDL_Log*` (all no-ops), byte order macros.

Does NOT provide: `SDL_Surface`, `SDL_Renderer`, `SDL_Window`, `SDL_Texture`,
`SDL_Event` — these are what you're replacing with wasmcart equivalents.

### `wc_pcm_mixer.h` — Multi-Channel PCM Audio Mixer

STB-style single-header library. Define `WC_PCM_MIXER_IMPLEMENTATION` in exactly
one .c file. Includes WAV file parser and ring buffer writer.

```c
// In one .c file:
#define WC_PCM_MIXER_IMPLEMENTATION
#include "wc_pcm_mixer.h"

// Load sounds from .wasc assets:
wc_mixer_init();
unsigned char buf[512000];
int len = wc_load_asset("sfx/boom.wav", 12, buf, sizeof(buf));
int snd_boom = wc_mixer_load_wav(buf, len);

// Play:
wc_mixer_play(snd_boom, 1.0f, 0);          // one-shot
int music = wc_mixer_play(snd_music, 0.5f, 1);  // looping
wc_mixer_set_volume(music, 0.3f);

// Each frame:
wc_mixer_mix(ring, cap, &write_cursor, 800);  // 800 frames @ 48kHz/60fps
```

Configurable via defines (before including):
- `WC_MIXER_MAX_SOUNDS` — max loaded sounds (default 32)
- `WC_MIXER_MAX_CHANNELS` — max simultaneous voices (default 16)
- `WC_MIXER_RATE` — output sample rate (default 48000)

Supports: 8-bit unsigned and 16-bit signed WAV (mono/stereo), resampling via
16.16 fixed-point stepping, per-channel volume and stereo panning, looping.

### `stb_image.h` — Image Decoder

Canonical copy in `porting/include/`. Example carts symlink to it.
Recommended defines for WASM:

```c
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO          // required — no filesystem in WASM
#define STBI_ONLY_PNG          // or STBI_ONLY_JPEG — reduces code size
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"
```

### `audio_bridge.h/c` — SDL2_mixer → Ring Buffer Bridge

For games using SDL2_mixer. Intercepts `SDL_OpenAudioDevice` via `--wrap` linker
flags to capture the mixer callback. See header for required linker flags.

### `emstubs.c` — Emscripten Runtime Stubs

~200 no-op stubs for emscripten runtime, EGL, and GL symbols that SDL2
references but are never called at runtime. Needed whenever building with
`-s USE_SDL=2`.

### Planned (Not Yet Extracted)

| Library | Purpose | Priority |
|---------|---------|----------|
| `wc_asset_utils.h` | `wc_asset_load_alloc()` — malloc + load in one call | LOW |

## File Reference

| File | Purpose | When to use |
|------|---------|-------------|
| `wasmcart.h` | ABI header (structs, constants, button masks, asset API) | Always |
| `porting/include/wc_cart.h` | Cart boilerplate (buffer declarations, wc_get_info macro) | All carts |
| `porting/include/wc_fb.h` | 2D framebuffer drawing (fill, blit, alpha blend, indexed palette) | 2D/software-rendered carts |
| `porting/include/wc_gl.h` | GL utilities (shader compile/link, texture, VAO, RNG) | GL carts |
| `porting/include/wc_math.h` | Math functions without libm (sin, cos, sqrt, atan2) | GL carts, any cart avoiding libm |
| `porting/include/wc_mat4.h` | 4x4 matrix operations (requires wc_math.h) | GL carts |
| `porting/include/wc_vec3.h` | 3D vector operations (requires wc_math.h) | GL carts |
| `porting/include/wc_pcm_mixer.h` | Multi-channel PCM mixer + WAV parser | Any cart with sound effects |
| `porting/include/wc_sdl_stubs.h` | SDL2 type definitions + no-op function stubs | Porting SDL2 games |
| `porting/include/stb_image.h` | PNG/JPEG image decoder | GL carts with textures |
| `porting/audio_bridge.h` | Audio bridge header | When using SDL2_mixer |
| `porting/audio_bridge.c` | SDL_mixer → ring buffer bridge with resampler | When using SDL2_mixer |
| `porting/emstubs.c` | 200+ no-op stubs for emscripten/EGL/GL symbols | When using emscripten's SDL2 port |

## wasmcart-pack CLI

```bash
# Basic usage
wasmcart-pack --wasm cart.wasm --assets assets/ -o game.wasc

# With metadata
wasmcart-pack --wasm cart.wasm --assets assets/ --name "My Game" --version "1.0.0" -o game.wasc

# No assets (bare wasm wrapped in .wasc for consistency)
wasmcart-pack --wasm cart.wasm -o game.wasc

# ABI v3 features
wasmcart-pack --wasm cart.wasm -o game.wasc --pointer          # enable pointer input
wasmcart-pack --wasm cart.wasm -o game.wasc --keyboard         # enable raw keyboard input
wasmcart-pack --wasm cart.wasm -o game.wasc --players 4        # local multiplayer (1-4)
wasmcart-pack --wasm cart.wasm -o game.wasc --ws api.example.com  # WebSocket allowlist (repeatable)
wasmcart-pack --wasm cart.wasm -o game.wasc --data-channel     # enable peer-to-peer data channels
```

## Examples

Working ports in `wasmcart/examples/`:

| Example | Type | Audio | .wasm Size | .wasc | Notes |
|---------|------|-------|-----------|-------|-------|
| `hello/` | Original | Tone gen | ~2KB | — | Simplest possible cart |
| `snake/` | Original | Tone gen | ~4KB | — | Basic game loop |
| `breakout/` | Original | — | ~8KB | — | Paddle/ball game |
| `invaders/` | Original | — | ~12KB | — | Space invaders |
| `platformer/` | Original | — | ~9KB | — | Side-scroller |
| `tetris/` | Original | — | ~11KB | — | Tetris clone |
| `hello_gl/` | Original (GL) | None | ~44KB | 89KB | **GL cart**: textured cube + triangle, stb_image JPEG decoder, gamepad controls |
| `zel/` | Port (SDL3) | None | ~311KB | `pack.sh` | BMP sprites/tiles |
| `lmdave/` | Port (DOS) | None | ~229KB | `pack.sh` | Extracted from DAVE.EXE |
| `angry_tirds/` | Original | PCM | ~246KB | `pack.sh` | Box2D physics + sound |
| `ccleste/` | Port (PICO-8) | SDL_mixer | ~2.5MB | `pack.sh` | Full audio bridge |
| `doom/` | Port (Doom) | PCM mix | ~28MB / ~1MB | `pack.sh` + `build_wasc.sh` | WAD via asset API |
| `flare/` | Port (RPG) | Custom | ~430MB / ~1.5MB | `pack.sh` + `build_wasc.sh` | VFS via asset API, uses `_filelist.txt` |
| `chromium_bsu/` | Port (GL1.x) | PCM mix | ~376KB | `build.sh` | Legacy GL1.x→GLES3 compat layer, stb_image, bitmap font |

## Porting Legacy GL1.x Games (Fixed-Function Pipeline)

Games using OpenGL 1.x immediate mode (glBegin/glEnd, display lists, fixed-function
matrices) require a compatibility layer to run on wasmcart's GLES3 backend. This section
covers the approach used for Chromium B.S.U. — applicable to any GL1.x game.

### Architecture: Wrapper Strategy

Keep the original source files **unmodified**. Create wrapper files that redirect API calls:

1. **`gl_compat.h` / `gl_compat.cpp`** — GL1.x → GLES3 batch renderer
2. **`chromium_compat.h`** — Force-included master header (`-include compat.h`)
3. **Replacement files** for platform-specific code (Image loader, Audio, Text, etc.)
4. **`chromium_cart.cpp`** — wasmcart entry point (replaces `main.cpp`)

The key principle: **the game is just a build target and some shims**. Don't modify
the original game source. Use `-include` to inject compatibility macros before every
translation unit.

### gl_compat: Immediate Mode → GLES3 Batching

The compat layer translates GL1.x calls into GLES3:

| GL1.x Call | gl_compat Implementation |
|-----------|--------------------------|
| `glBegin(GL_QUADS)` / `glEnd()` | Batch vertices, triangulate quads → 2 triangles |
| `glVertex2f/3f()` | Accumulate vertex + current color + current texcoord into batch |
| `glTexCoord2f()` | Set current texcoord (applied to next glVertex) |
| `glColor3f/4f()` | Set current color |
| `glMatrixMode/glLoadIdentity/glPushMatrix/glPopMatrix` | Maintain modelview + projection stacks |
| `glTranslatef/glRotatef/glScalef` | Apply to current modelview matrix |
| `gluPerspective/gluOrtho2D` | Build projection matrix |
| `glBindTexture` | Flush batch on texture change, bind real GLES3 texture |
| `glAlphaFunc` | Stub (use discard in fragment shader if needed) |
| Display lists (`glGenLists/glNewList/glCallList`) | Stub — call geometry directly instead |
| `glEnable/glDisable(GL_TEXTURE_2D)` | Track state, pass GL_BLEND/GL_DEPTH_TEST to real GLES3 |
| `GL_CLAMP` | Map to `GL_CLAMP_TO_EDGE` (GLES3 doesn't have GL_CLAMP) |
| `GL_LUMINANCE` | Map to `GL_RED` (GLES3 doesn't have GL_LUMINANCE) |

The batch renderer uses a simple vertex shader (`uMVP * position`) and fragment
shader (texture * color, with optional texture enable). Modelview is applied on the
CPU when emitting vertices; projection is uploaded as a uniform.

**Critical**: The vertex shader should be straightforward — `gl_Position = uMVP * vec4(aPos, 1.0)`.
Do NOT add coordinate hacks like `p.x = -p.x` in the shader. If text or sprites
appear mirrored, check the font/sprite rendering code first (e.g. bit order for
bitmap fonts). See "Horizontally Mirrored Text" under Common Problems.

### Force-Include Compat Header

Compile original source files with `-include compat.h` to inject stubs:

```bash
# gl_compat.cpp is compiled WITHOUT the force-include (it calls real GL)
em++ $FLAGS -c gl_compat.cpp -o gl_compat.o

# Everything else compiled WITH the force-include
em++ $FLAGS -include compat.h \
  cart.cpp original_src/*.cpp gl_compat.o \
  -o game.wasm
```

The compat header should contain:
- Config defines (`HAVE_CONFIG_H`, `USE_SDL 0`, etc.)
- GL macro redirects (`glColor4fv(v)` → `compat_glColor4f(v[0]...)`)
- Display list stubs (`glGenLists` → dummy, `glCallList` → no-op)
- SDL stubs (`SDL_Delay` → no-op, `SDL_GetTicks` → frame counter)
- System stubs (`getenv` → NULL, file I/O → no-ops)
- gettext stubs (`_(x)` → `(x)`)

### Replacement Files

Instead of modifying original source, create replacement files:

| Original | Replacement | Purpose |
|----------|------------|---------|
| `main.cpp` | `cart.cpp` | wasmcart entry point (wc_get_info/wc_init/wc_render) |
| `Image.cpp` | `ImageWC.cpp` | stb_image texture loader via asset API |
| `AudioOpenAL.cpp` | `AudioWasmcart.cpp` | PCM mixer writing to ring buffer |
| `TextFTGL.cpp` | `TextBitmap.cpp` | Bitmap font renderer (no FreeType dependency) |
| `MainSDL.cpp` | Excluded | Not needed — wasmcart hosts the window |
| `Config.cpp` | `Config_wc.cpp` | Skip file I/O, hardcode sensible defaults |

Exclude platform-specific files from the build entirely. Don't stub them — just
don't compile them.

### Build Script Structure

```bash
#!/bin/bash
set -e
SRC=/path/to/original/src
HERE="$(cd "$(dirname "$0")" && pwd)"

FLAGS="-O2 -s STANDALONE_WASM=1 -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
  -I $SRC -I $HERE -DWASM_CART -DWC_USE_GL \
  -fno-exceptions -Wno-writable-strings"

# gl_compat.cpp WITHOUT force-include (calls real GL directly)
em++ $FLAGS -c "$HERE/gl_compat.cpp" -o /tmp/gl_compat.o

# Everything else WITH force-include
em++ $FLAGS --no-entry \
  -include "$HERE/compat.h" \
  "$HERE/cart.cpp" \
  "$HERE/ImageWC.cpp" \
  "$HERE/AudioWasmcart.cpp" \
  "$HERE/TextBitmap.cpp" \
  "$SRC/GameLogic.cpp" \
  "$SRC/Rendering.cpp" \
  ... \
  /tmp/gl_compat.o \
  -o "$HERE/game.wasm"
```

### Screenshot Tool for Visual Debugging

The player captures cart output headlessly, without needing a display:

```bash
npx wasmcart game.wasc --frames 120 --shot screenshot.png
```

This runs the cart with no window, steps N frames, reads pixels back, and saves a
PNG. Around 120 frames (~2 seconds) lets menu animations settle. Add `--wav out.wav`
to dump audio at the same time.

Because the run is deterministic, adding `--seed` makes it a regression test — the
same seed produces a byte-identical PNG, so a shell loop plus `cmp` catches visual
regressions:

```bash
npx wasmcart game.wasc --seed 7 --frames 60 --shot now.png
cmp golden.png now.png || echo "render changed"
```

The screenshot tool reads directly from the GL framebuffer — it shows the cart's
actual rendered output, bypassing any host display pipeline issues.

## Offline Build Tools

Many games have native tools that pre-process assets into optimized binary
formats before they can be packaged. These tools need to be built natively
(not as WASM) and run on the build machine.

### Why Stubs Don't Work

When building native tools, it's tempting to stub out dependencies like
image loading (libpng/libjpeg) to simplify the build. **This produces
silently corrupt output** if the tool uses those libraries for more than
just display.

Example: Neverball's `mapc` level compiler reads image dimensions to compute
texture UV coordinates. A stub `image_load()` returning `width=0, height=0`
caused division by zero in the UV math, producing NaN texture coordinates in
every compiled level file. The output files were structurally valid (correct
headers, correct material names) but had garbage UV data. Every surface
rendered as a flat single-color fill instead of showing texture detail.

### Use stb Libraries for Native Tools

The `stb` single-header libraries provide dependency-free implementations
that work on any platform:

| Library | Purpose | Header |
|---------|---------|--------|
| stb_image.h | Read PNG/JPEG/BMP/GIF dimensions and pixels | `porting/include/stb_image.h` |
| stb_vorbis.c | Decode OGG audio | — |
| stb_truetype.h | Rasterize TTF fonts | `porting/include/stb_truetype.h` |

For a native tool that only needs image dimensions:

```c
// base_image_stb.c — drop-in replacement for image stubs
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

void *image_load(const char *filename, int *width, int *height, int *bytes) {
    int w, h, channels;
    unsigned char *pixels = stbi_load(filename, &w, &h, &channels, 0);
    if (!pixels) return NULL;
    // ... flip, set outputs ...
    return pixels;
}
```

Build with `-I path/to/porting/include` to find the header.

### Verifying Tool Output

When a build tool processes assets, verify the output is correct before
packaging. Check for:

- **NaN/Inf values** in floating-point data (texture coords, vertices, normals)
- **Zero counts** where non-zero is expected (e.g., 0 texture coordinates in a textured level)
- **Truncated strings** or misaligned struct fields (indicates serialization mismatch)

A quick binary inspection script can catch these:

```python
import struct, math

with open("level.sol", "rb") as f:
    data = f.read()
    floats = struct.unpack(f"<{len(data)//4}f", data[:len(data)//4*4])
    nan_count = sum(1 for v in floats if math.isnan(v))
    if nan_count > 0:
        print(f"WARNING: {nan_count} NaN values found!")
```

### Running Native Tools in the Build Pipeline

Structure your build scripts to:
1. Build native tools first (if not already built)
2. Process assets with native tools
3. Compile WASM cart
4. Package into .wasc

```bash
# Example: Neverball build pipeline
bash build_mapc.sh              # Build native level compiler (once)
cd ports/neverball/data
for map in map-putt/*.map; do   # Compile all levels
    /tmp/mapc "$map" .
done
cd -
bash build.sh                   # Compile WASM cart
bash pack.sh                    # Package .wasc with compiled levels
```

## Porting C++ / SFML Games

Games using SFML (Simple and Fast Multimedia Library) require a different approach
than SDL games. SFML uses C++ classes (`sf::Music`, `sf::Sound`, `sf::Sprite`,
`sf::Text`, `sf::RenderWindow`) that can't be individually stubbed with `#define`.
Instead, replace the entire namespace.

### The Compatibility Header Pattern (C++ Frameworks)

Create a compatibility header that blocks SFML's real headers and provides
stub classes with real wasmcart-backed implementations:

```cpp
// game_compat.h — force-included with -include game_compat.h

// Block all SFML headers
#define SFML_SYSTEM_HPP
#define SFML_WINDOW_HPP
#define SFML_GRAPHICS_HPP
#define SFML_AUDIO_HPP

// Enable Android code paths for file loading via sf::FileInputStream
#define ANDROID 1

// Provide compatible types in namespace sf
namespace sf {
    // Minimal stubs for types the game code references
    struct Color { uint8_t r, g, b, a; /* constructors, operators */ };
    struct Vector2f { float x, y; };
    struct Vector2i { int x, y; };
    typedef std::string String;

    // Real implementations backed by wasmcart libraries
    class Music {
        void* vorbis_;      // stb_vorbis handle
        // ... streaming state
    public:
        bool openFromFile(const std::string& path);  // wc_load_asset + stb_vorbis
        void play();
        void stop();
        void setLoop(bool l);
        void setVolume(float v);
        int decodeFrames(short* out, int frames);     // called by mixer
    };

    class SoundBuffer {
        int mixer_slot_;    // wc_pcm_mixer slot
    public:
        bool loadFromFile(const std::string& path);   // wc_load_asset + wc_mixer_load_wav
    };

    class Sound {
        const SoundBuffer* buffer_;
        int channel_;       // wc_pcm_mixer channel
    public:
        void setBuffer(const SoundBuffer& b);
        void play();                                   // wc_mixer_play
    };

    class Sprite { /* GL quad rendering in Y-down ortho */ };
    class Text { /* stb_truetype rendering */ };
    class RectangleShape { /* GL filled rect */ };

    // File loading backed by wc_load_asset
    class FileInputStream {
        unsigned char* data_;
        int size_, pos_;
    public:
        bool open(const std::string& path);   // wc_load_asset
        int64_t read(void* buf, int64_t n);
        int64_t getSize();
    };
}
```

Build with: `em++ -include game_compat.h -std=c++14 ...`

### Key Differences from SDL Porting

| Aspect | SDL Game | SFML Game |
|--------|----------|-----------|
| Header blocking | `#define SDL_video_h_` | `#define SFML_GRAPHICS_HPP` |
| Function stubs | `#define SDL_Init(x) 0` | Full class implementations needed |
| Audio | `--wrap SDL_OpenAudioDevice` | Replace sf::Music/Sound classes |
| File loading | `SDL_RWFromFile` → wc_load_asset | `sf::FileInputStream` → wc_load_asset |
| Text rendering | SDL_ttf / bitmap fonts | sf::Text → stb_truetype |
| Android trick | N/A | `#define ANDROID 1` re-uses mobile code paths |

### Texture Vertical Flip Convention

**This is critical.** Different frameworks have different texture coordinate conventions:

| Framework | V=0 means | Flip before glTexImage2D? |
|-----------|-----------|--------------------------|
| SDL + standard GL | Bottom of image | YES |
| SFML | Top of image | **NO** |

stb_image loads images top-to-bottom. SDL-based games expect V=0 at the bottom
(standard GL), so they flip textures before upload. SFML-based games expect V=0 at
the top, so stb_image's output matches directly — do NOT flip.

If you flip textures for an SFML game, every texture in the game will render upside
down — terrain, trees, characters, skybox, HUD, everything. Always check what V=0
means in the original framework.

### stb_truetype Baseline vs SFML Top

When replacing sf::Text with stb_truetype:
- `stbtt_GetBakedQuad` treats Y as the **baseline** (glyphs extend above)
- SFML `sf::Text::setPosition(x, y)` means y = **top** of the text bounding box

Fix: add font ascent offset to Y before rendering:
```cpp
int ascent, descent, lineGap;
stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
float scale = stbtt_ScaleForPixelHeight(&fontInfo, fontSize);
y += ascent * scale;
```

Without this, text in GUI elements (combo boxes, labels, buttons) renders above
its intended position.

### Example: Extreme Tux Racer

See `examples/etr/` for a complete SFML C++ game port. Key files:
- `etr_compat.h` — full SFML replacement with real audio/sprite/text implementations
- `audio_wc.cpp` — stb_vorbis + wc_pcm_mixer audio with time-based mixing
- `font_wc.cpp` — stb_truetype font rendering with Y-down ortho support
- `build.sh` — two-stage build (C for gl4es bridge, C++14 for everything else)

Result: 1.1 MB .wasm, 50 MB .wasc (583 assets), 0 original source files modified.

## Emscripten SDL2 Backend Approach (Proven)

Validated on: **neverball_es**, **neverputt_es**, **ccleste_es**, **flare_es**.
Produces identical rendering to hand-ports with 50-75% less game-specific code.

The hand-porting approach described above works but requires significant effort per game:
custom compat headers, GL translation layers, audio bridges, and file I/O shims.
Many of these games already had working Emscripten/browser builds — the porting work
was essentially reimplementing what Emscripten already provides.

A better approach: use **Emscripten as a compiler** with custom SDL2/GL backend
libraries that target wasmcart's ABI directly. The output is a standard wasmcart cart
(`.wasm` exporting `wc_render()` / `wc_get_info()`), but the game's SDL2/GL calls are
compiled against backends that write to `wc_framebuffer`, `wc_audio_ring`, and `wc_pads[]`.

### Why This Works

Emscripten produces standard WebAssembly. The WASM bytecode has no idea it's
"supposed" to run in a browser — it just imports functions. Whoever provides those
imports wins. The browser dependency comes from Emscripten's **runtime libraries**,
not the compiler itself.

If we replace Emscripten's browser-targeting backends with wasmcart-targeting
backends, the game compiles with `emcc` but the output is a self-contained wasmcart
cart. No JS glue needed. No browser required.

### Architecture

```
Game code (C/C++)
    |
SDL2 / OpenGL (Emscripten's C-side ports)
    |
Custom wasmcart backend (replaces Emscripten's browser JS backend)
    | calls
wc_framebuffer, wc_audio_ring, wc_pads[], gl.* imports
    |
retroemu (existing host, unchanged)
```

The key: Emscripten's SDL2 port is implemented partly in C (compiled into WASM)
and partly in JS (browser backend). We replace only the JS backend part with C
implementations that use wasmcart imports. Everything else — SDL2's
platform-independent code, Emscripten's libc, math library, GL translation —
stays as-is and compiles into the WASM.

### What Changes (and What Doesn't)

**Nothing changes in wasmcart:**
- ABI — unchanged
- CartHost.js — unchanged
- gl_imports.js — unchanged
- retroemu — unchanged
- .wasc format — unchanged

**What's new — porting libraries:**

```
porting/sdl2_wc/
    SDL_wasmcart_video.c/.h  # SDL2 video backend -> wc_framebuffer / GL
    SDL_wasmcart_audio.c/.h  # SDL2 audio backend -> wc_audio_ring (native format)
    SDL_config_wasmcart.h    # SDL2 config for wasmcart targets
    build_sdl2_wc.sh         # Builds libSDL2_wc.a + libSDL2_ttf_wc.a
    invoke_stubs.c           # Stubs for missing emscripten runtime symbols
```

The audio backend does **no conversion or resampling**. It accepts whatever
format/rate the game opens (e.g. 22050Hz S16 mono) and writes the callback's
native output directly to the ring buffer (expanding mono to stereo). The cart
calls `wasmcart_audio_get_spec()` after audio opens to query the actual
format/rate, then sets `wc_info.audio_sample_rate` and `wc_info.flags` so
the host can adapt.

### SDL2 Backend Mapping

| SDL2 Subsystem | Browser Backend (current) | wasmcart Backend (new) |
|----------------|--------------------------|----------------------|
| Video | Canvas / WebGL2 context | Write to `wc_framebuffer` (2D) or use `gl.*` imports (GL) |
| Audio | Web Audio API / AudioWorklet | Write to `wc_audio_ring` (native format stereo ring buffer, host adapts to cart's declared rate/format) |
| Input | DOM keyboard/mouse/touch events | Read from `wc_pads[]` struct |
| Filesystem | Emscripten MEMFS / IDBFS | `wc_load_asset()` / `wc_asset_size()` |
| Timer | `performance.now()` / `Date.now()` | Read from `wc_time.time_ms` / `wc_time.delta_ms` |
| Window | HTML Canvas element | No-op (host manages display) |

### SDL2 GPU-Accelerated 2D Rendering

This approach unlocks something not possible with hand-porting: **SDL2's hardware-
accelerated 2D renderer** (`SDL_Renderer` with `SDL_RENDERER_ACCELERATED`).

SDL2's accelerated renderer uses OpenGL internally for `SDL_RenderCopy`,
`SDL_RenderDrawRect`, texture rotation, scaling, and alpha blending. With the
wasmcart backend, these GL calls route through the `gl.*` import module — giving
2D games GPU acceleration without writing any GL code.

Current wasmcart options for 2D games:
- `wc_fb.h` — CPU software rendering to `wc_framebuffer`
- Raw GL calls — powerful but requires writing shaders

With Emscripten SDL2 backend:
- `SDL_RenderCopy` / `SDL_RenderPresent` — GPU-accelerated 2D, familiar API

### Main Loop Inversion

Games compiled with Emscripten expect to own their main loop. wasmcart requires
the host to call `wc_render()` per frame. The `main_loop_wrapper.c` bridges this:

```c
// The game's original main() calls SDL_Init, creates window, enters loop.
// We intercept at the loop boundary:

static void (*_frame_callback)(void) = NULL;

// Emscripten's emscripten_set_main_loop replacement:
void emscripten_set_main_loop(void (*func)(void), int fps, int infinite) {
    _frame_callback = func;
    // Don't actually loop — just store the callback
}

// wasmcart entry points:
WC_EXPORT void wc_init(void) {
    // Call the game's main() — it will set up SDL, load assets,
    // and register its frame callback via emscripten_set_main_loop
    main(0, NULL);
}

WC_EXPORT void wc_render(void) {
    // Call the game's per-frame callback
    if (_frame_callback) _frame_callback();
}
```

For games using `emscripten_set_main_loop_arg` (with user data), the wrapper
stores the arg pointer and invokes accordingly.

### Porting a Game with the Emscripten Backend

With the backend libraries built (`libSDL2_wc.a`), porting an SDL2 game becomes:

1. Write a cart entry point (`game_cart.c`) that:
   - Declares cart buffers (`WC_CART_BUFFERS`)
   - `wc_get_info()` returns `&wc_info` (pointer, not copy — host re-reads after init)
   - Calls the game's `main()` from `wc_init()`
   - Intercepts `emscripten_set_main_loop_arg` to capture the frame callback
   - After audio opens, calls `wasmcart_audio_get_spec()` to set `wc_info.audio_sample_rate` and flags
   - Calls `wasmcart_audio_pump()` each frame in `wc_render()`

2. Build with emcc, linking against `libSDL2_wc.a`:
```bash
# Compile: use -sUSE_SDL=2 for headers, link against our lib (not Emscripten's)
emcc -O2 -sSTANDALONE_WASM=1 -sUSE_SDL=2 \
    game_cart.c game_source/*.c \
    -L lib -lSDL2_wc \
    -sUSE_SDL=0 -sUSE_SDL_MIXER=2 -sUSE_SDL_IMAGE=2 \
    -o cart.wasm

# Pack with assets
wasmcart-pack --wasm cart.wasm --assets assets/ -o game.wasc
```

Note the build trick: `-sUSE_SDL=2` at compile time (for headers), then
`-sUSE_SDL=0` at link time (use our `libSDL2_wc.a` instead of Emscripten's).

A complete, minimal build that is known to work end-to-end (compile → link →
pack → run), useful as a smoke test that your `libSDL2_wc.a` is good:

```bash
# 1. compile — real SDL2 headers
emcc -O2 -sUSE_SDL=2 -I. -c game_cart.c     -o game_cart.o
emcc -O2 -sUSE_SDL=2 -I. -c sdl2_wc/sdl2_gl_blit.c -o blit.o
emcc -O2                  -c gl4es_stub.c   -o stub.o   # 2D-only carts; see Common Problems

# 2. link — our SDL2, not Emscripten's
emcc -O2 -sSTANDALONE_WASM=1 -sALLOW_MEMORY_GROWTH=1 --no-entry \
    game_cart.o blit.o stub.o \
    -L sdl2_wc/lib -lSDL2_wc -sUSE_SDL=0 \
    -o cart.wasm

# 3. pack + run (headless; writes a PNG you can eyeball)
npx wasmcart pack --wasm cart.wasm --name "My Game" -o game.wasc
npx wasmcart game.wasc --frames 40 --shot out.png
```

The cart calls `SDL_WASMCART_SetFramebuffer(fb, w, h)` in `wc_init()` before
`SDL_Init`, then uses ordinary `SDL_CreateRenderer` / `SDL_RenderFillRect` /
`SDL_RenderPresent` — the backend routes the result into the wasmcart
framebuffer.

Compare this to the hand-porting approach which requires weeks of:
- Writing SDL/SFML compat headers
- Building GL1.x → GLES3 translation layers
- Implementing audio bridges
- Creating file I/O shims
- Debugging texture flip conventions

### Porting Godot Games

Godot's web export uses Emscripten with a custom platform layer (`platform/web/`).
The C++ code calls ~150 `godot_js_*()` extern functions, currently implemented in
JavaScript (13 `library_godot_*.js` files).

With the Emscripten backend approach, porting a Godot game means reimplementing
those ~150 bridge functions **in C** instead of JS:

```
porting/
  godot/
    godot_js_display.c    # godot_js_display_*() -> wc_framebuffer / gl.* imports
    godot_js_audio.c      # godot_js_audio_*() -> wc_audio_ring
    godot_js_input.c      # godot_js_input_*() -> reads wc_pads[]
    godot_js_os.c         # godot_js_os_*() -> wc_load_asset for .pck loading
```

The Godot engine's platform/web/ C++ code stays **unchanged**. The GL rendering
path already works through Emscripten's GLES3 translation. The output is a
standard wasmcart cart:

```bash
# Build Godot engine as a wasmcart cart
scons platform=web target=template_release \
    custom_modules=path/to/wasmcart_backend

# Bundle with game's .pck
wasmcart-pack --wasm godot.wasm --assets game.pck -o game.wasc

# Run with retroemu
retroemu --video sdl game.wasc
```

This is dramatically less work than building a full `platform/wasmcart/` from
scratch (new OS, DisplayServer, AudioDriver classes = months of work).

### Binary Size Considerations

Emscripten-compiled carts are larger than hand-ported carts because they include
Emscripten's libc, SDL2 implementation, and potentially the game engine runtime:

| Approach | Typical .wasm size | Notes |
|----------|-------------------|-------|
| Hand-ported (no SDL) | 2KB - 400KB | Minimal, only game code |
| Hand-ported (with SDL_mixer) | 1-3 MB | Includes SDL_mixer + OGG decoder |
| Emscripten SDL2 backend | 3-10 MB | Includes Emscripten libc + SDL2 |
| Godot 3.x engine | ~15-20 MB | Full game engine runtime |
| Godot 4.x engine | ~33-40 MB | Full engine (can strip to ~15MB) |

These sizes are acceptable. Asset-heavy games (ETR: 50MB, OpenArena: 391MB)
are dominated by asset size, not code size. And carts that would take weeks to
hand-port can be ready in hours.

### Hand-Porting vs Emscripten Backend

Both approaches produce identical wasmcart carts. Choose based on the game:

| Factor | Hand-Port | Emscripten Backend |
|--------|-----------|-------------------|
| **Binary size** | Smallest possible | Larger (includes Emscripten runtime) |
| **Porting effort** | Days to weeks per game | Hours (once backend exists) |
| **SDL2 GPU 2D** | Not available | Yes (SDL_Renderer accelerated) |
| **Game engine support** | N/A | Godot, other Emscripten-targeting engines |
| **Control** | Full control over every byte | Less control, more abstraction |
| **Best for** | Small/simple games, learning | Large SDL2/GL games, Godot games, rapid porting |

The two approaches coexist. Small original games and educational carts benefit
from hand-porting's simplicity. Large existing games benefit from Emscripten's
existing SDL2/GL/audio support.

## Threading (WASI Threads)

Carts can spawn background threads using standard pthreads. Useful for background computation, audio decoding, physics simulation, or asset loading without blocking the render loop.

### Prerequisites

Threading requires **wasi-sdk** (not Emscripten). Emscripten's `-sSTANDALONE_WASM=1` is incompatible with its pthread implementation. wasmcart uses the WASI threads model instead — one import, one export, all pthread logic handled by wasi-libc inside the WASM module.

Install wasi-sdk from https://github.com/WebAssembly/wasi-sdk/releases

### How It Works

When a cart calls `pthread_create()`:

1. wasi-libc calls the host-provided `wasi.thread-spawn(start_arg)` import
2. The host assigns a thread ID and spawns a worker (Node.js `worker_thread` or browser `Web Worker`)
3. The worker instantiates the same WASM module with the same shared memory
4. The worker calls the cart's exported `wasi_thread_start(tid, start_arg)`
5. All pthread synchronization (mutexes, condvars, TLS) runs inside wasi-libc — the host just spawns and calls

Non-threaded carts don't import `thread-spawn`, don't use shared memory, and follow the exact same code path as before. Zero impact.

### Compilation

```bash
WASI_SDK=$HOME/wasi-sdk-25.0-x86_64-linux

$WASI_SDK/bin/clang \
  --target=wasm32-wasip1-threads \
  --sysroot=$WASI_SDK/share/wasi-sysroot \
  -pthread \
  -O2 \
  -o cart.wasm cart.c \
  -Wl,--import-memory \
  -Wl,--shared-memory \
  -Wl,--max-memory=67108864 \
  -Wl,--export=wc_get_info \
  -Wl,--export=wc_init \
  -Wl,--export=wc_render \
  -Wl,--no-entry \
  -nostartfiles
```

Key flags:
- `--target=wasm32-wasip1-threads` — WASI threads target
- `-pthread` — enables pthreads in the compiler and links wasi-libc's pthread implementation
- `--import-memory` — memory is imported from the host (required for shared memory)
- `--shared-memory` — marks memory as shared (`SharedArrayBuffer` on the host side)
- `--max-memory=N` — required for shared memory (memory must have a maximum); 64MB is a reasonable default
- `--no-entry` / `-nostartfiles` — no `_start` entrypoint (wasmcart uses `wc_init`)

Non-threaded carts compile exactly as before with Emscripten. No flags change, no wasi-sdk needed.

### Example

```c
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <string.h>

/* wasmcart ABI header */
#include "wasmcart.h"

static uint32_t framebuffer[320 * 240];
static wc_info_t info;
static wc_time_t time_info;

/* Shared state between main thread and worker */
static atomic_int result = 0;
static atomic_int done = 0;

void* background_work(void* arg) {
    /* Heavy computation on a background thread */
    int sum = 0;
    for (int i = 0; i < 1000000; i++) sum += i;
    atomic_store(&result, sum);
    atomic_store(&done, 1);
    return NULL;
}

__attribute__((export_name("wc_get_info")))
wc_info_t* wc_get_info(void) {
    info.version = 2;
    info.width = 320;
    info.height = 240;
    info.fb_ptr = (uint32_t)(uintptr_t)framebuffer;
    info.time_ptr = (uint32_t)(uintptr_t)&time_info;
    return &info;
}

static pthread_t worker;
static int spawned = 0;

__attribute__((export_name("wc_init")))
void wc_init(void) {
    memset(framebuffer, 0, sizeof(framebuffer));
}

__attribute__((export_name("wc_render")))
void wc_render(void) {
    if (!spawned) {
        pthread_create(&worker, NULL, background_work, NULL);
        spawned = 1;
    }

    /* Read result (non-blocking) */
    int is_done = atomic_load(&done);
    int value = atomic_load(&result);

    /* Draw progress: green if done, yellow if working */
    uint32_t color = is_done ? 0xFF00FF00 : 0xFFFFFF00;
    for (int i = 0; i < 320 * 240; i++)
        framebuffer[i] = 0xFF1A1A40;
    for (int y = 100; y < 140; y++)
        for (int x = 20; x < 300; x++)
            framebuffer[y * 320 + x] = color;

    /* Join when done (safe — thread already finished) */
    if (is_done && spawned == 1) {
        pthread_join(worker, NULL);
        spawned = 2;
    }
}
```

See [`examples/hello_threads/`](../examples/hello_threads/) for the full working example with build script.

### Constraints

**GL calls are main-thread only.** Worker threads receive trapping stubs for all GL functions. If a worker calls any GL function, it throws an error. This matches native OpenGL's thread-ownership model. Use threads for compute, not rendering.

**`pthread_join` blocks in the browser.** `Atomics.wait()` (used by pthread_join) throws on the browser main thread. If your cart does `pthread_join` inside `wc_render()`, it will work in Node.js but fail in the browser. Workarounds:
- Use `atomic_load` to poll for completion (non-blocking)
- Use `pthread_detach` instead of join
- Only join when you know the thread has already finished

**Browser requires COOP/COEP headers for threaded carts.** Threaded carts use `SharedArrayBuffer`, which requires cross-origin isolation:
```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```
Non-threaded carts are completely unaffected — they use regular `ArrayBuffer` and need no special headers.

**wasi-sdk only, not Emscripten.** Emscripten's `-sSTANDALONE_WASM=1` flag is incompatible with Emscripten's pthread implementation (which requires JS glue). wasmcart's threading uses the WASI threads model, which only wasi-sdk supports. Non-threaded carts can still use Emscripten.

### Asset Access from Threads

Worker threads can access `.wasc` assets. The host automatically gives each worker its own file descriptor to the `.wasc` ZIP file, so asset reads from threads don't require IPC or synchronization with the main thread.

### What the Host Provides

The threading ABI adds exactly two things:

| Direction | Name | Signature |
|-----------|------|-----------|
| Host provides (import) | `wasi.thread-spawn` | `(start_arg: i32) → i32 (tid)` |
| Cart provides (export) | `wasi_thread_start` | `(tid: i32, start_arg: i32) → void` |

Everything else — mutexes, condition variables, TLS, thread-local storage — is handled by wasi-libc inside the WASM module. The host just spawns workers and calls the entry point.
