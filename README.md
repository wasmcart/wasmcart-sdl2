# wasmcart-sdl2

**SDL2 backend + porting toolkit for [wasmcart](https://github.com/wasmcart/wasmcart).**

This is how you take an existing **C/SDL2 game** and turn it into a wasmcart cart.
It provides a wasmcart-native [SDL2](https://www.libsdl.org) backend (video + audio
implemented against the wasmcart framebuffer/audio-ring instead of a real windowing
system), the stub headers and helper decoders games need, and the full porting guide.

It is **not** part of the wasmcart spec — the spec + reference hosts live in the
main [wasmcart](https://github.com/wasmcart/wasmcart) repo. This is a build toolkit
for one class of cart author (people porting existing SDL2 games).

## Documentation

| Doc | What's in it |
|-----|--------------|
| [**PORTING_GUIDE.md**](PORTING_GUIDE.md) | ★ The comprehensive guide — writing new carts, porting existing games, the asset API, GL ES 3.0, legacy GL1.x, C++/SFML, threading, and common problems |
| [**porting_game_notes.md**](porting_game_notes.md) | Hard-won lessons from 9 upstream game ports (plus 8 original carts) — audio, stdio/WASI, frame-rate mismatch, texture flips, the zero-modified-files strategy |
| [**OPENARENA_PORT.md**](OPENARENA_PORT.md) | Worked example: [ioquake3](https://github.com/ioquake/ioq3) → wasmcart |
| [**GL_FULL_GAMES.md**](GL_FULL_GAMES.md) | Feasibility notes for full-size GL game ports |
| [**GL_PORT_CANDIDATES.md**](GL_PORT_CANDIDATES.md) | Survey of candidate games and their GL requirements |
| [**PORTING_EXCEPTIONS.md**](PORTING_EXCEPTIONS.md) | Cases where the standard approach doesn't apply |

## Contents

```
sdl2_wc/              wasmcart-native SDL2 backend (video + audio + GL blit)
  SDL_wasmcart_video.c    SDL2 video backend → wasmcart framebuffer / GL
  SDL_wasmcart_audio.c    SDL2 audio backend → wasmcart audio ring
  SDL_config_wasmcart.h   SDL2 build config for the wasmcart target
  sdl2_gl_blit.c          uploads the software-rendered surface as a GL texture
  build_sdl2_wc.sh        builds libSDL2_wc.a with Emscripten
include/
  wc_sdl_stubs.h          minimal SDL2 type shims for lighter ports
  stb_image.h / stb_truetype.h / stb_vorbis.c   3rd-party decoders (public domain / MIT)
audio_bridge.{c,h}    PCM bridge helpers
emstubs.c             emscripten runtime stubs
```

## Build

Requires a working [emsdk](https://github.com/emscripten-core/emsdk); SDL2 and
SDL_ttf are pulled from [Emscripten](https://emscripten.org)'s own port cache, so
there is nothing else to fetch.

```bash
# builds libSDL2_wc.a (+ libSDL2_ttf_wc.a); point EMSDK at your emsdk checkout
EMSDK=/path/to/emsdk ./sdl2_wc/build_sdl2_wc.sh
```

Compiled objects and static libs are **not** committed (see `.gitignore`) — build
them locally.

## Quick check

Once the libraries are built, this is the shortest path from source to a running
cart (see [PORTING_GUIDE.md](PORTING_GUIDE.md) for the full version):

`WC=` is a checkout of the main [wasmcart](https://github.com/wasmcart/wasmcart)
repo — its `include/` supplies `wasmcart.h` and `wc_gl_blit.h`.

```bash
WC=/path/to/wasmcart

# 1. compile with real SDL2 headers
emcc -O2 -sUSE_SDL=2 -I. -I$WC/include -c game_cart.c            -o game_cart.o
emcc -O2 -sUSE_SDL=2 -I. -I$WC/include -c sdl2_wc/sdl2_gl_blit.c -o blit.o
emcc -O2                               -c gl4es_stub.c           -o stub.o

# 2. link against ours, not Emscripten's
emcc -O2 -sSTANDALONE_WASM=1 -sALLOW_MEMORY_GROWTH=1 --no-entry \
    game_cart.o blit.o stub.o \
    -L sdl2_wc/lib -lSDL2_wc -sUSE_SDL=0 -o cart.wasm

# 3. pack and run headlessly — writes a PNG you can eyeball
npx wasmcart pack --wasm cart.wasm --name "My Game" -o game.wasc
npx wasmcart game.wasc --frames 40 --shot out.png
```

Two traps this recipe steps around, both covered in
[Common Problems](PORTING_GUIDE.md#common-problems):

- **`-sUSE_SDL=0` is link-only.** Passing it while compiling swaps in
  Emscripten's `fakesdl` headers and every SDL type goes undeclared, so compile
  and link must be separate steps.
- **The video backend needs three GL symbols at link time** even when nothing
  calls them. `wc_sdl_gl_blit` comes from `sdl2_wc/sdl2_gl_blit.c`; a 2D-only
  cart satisfies the other two with a four-line `gl4es_stub.c`:

  ```c
  void  gl4es_bridge_set_size(int w, int h) { (void)w; (void)h; }
  void *wc_gl4es_GetProcAddress(const char *p) { (void)p; return 0; }
  ```

## The cart contract

The C-side of the wasmcart contract lives in the main
[wasmcart](https://github.com/wasmcart/wasmcart) repo's
[`include/`](https://github.com/wasmcart/wasmcart/tree/main/include) — the
`wasmcart.h` ABI header plus the cart-author SDK (`wc_cart.h`, `wc_fb.h`,
`wc_gl.h`, math/mixer helpers). This repo depends on that contract; it does not
redefine it. The normative layouts are in
[SPEC.md](https://github.com/wasmcart/wasmcart/blob/main/SPEC.md).

Include `wasmcart.h` (structs, flags, GL + host imports) first, then `wc_cart.h`
for the boilerplate macros. Note the three export macros are not
interchangeable — `WC_EXPORT` is for `wc_get_info` only, with `WC_EXPORT_INIT` and
`WC_EXPORT_RENDER` for the other two.

## Third-party

`stb_image.h`, `stb_truetype.h`, `stb_vorbis.c` are Sean Barrett's
[stb](https://github.com/nothings/stb) libraries (public domain / MIT). SDL2
backend sources derive from [SDL2](https://www.libsdl.org) (zlib license).
GL 1.x ports use [gl4es](https://github.com/ptitSeb/gl4es) for translation to
GLES2.

## License

MIT (the wasmcart-specific code). Bundled third-party retains its own license.
