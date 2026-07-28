# wasmcart-sdl2

**SDL2 backend + porting toolkit for [wasmcart](https://github.com/wasmcart/wasmcart).**

This is how you take an existing **C/SDL2 game** and turn it into a wasmcart cart.
It provides a wasmcart-native SDL2 backend (video + audio implemented against the
wasmcart framebuffer/audio-ring instead of a real windowing system), the stub
headers and helper decoders games need, and the full porting guide.

It is **not** part of the wasmcart spec — the spec + reference hosts live in the
main [wasmcart](https://github.com/wasmcart/wasmcart) repo. This is a build toolkit
for one class of cart author (people porting existing SDL2 games).

## Contents

```
sdl2_wc/              wasmcart-native SDL2 backend (video + audio + GL blit)
  SDL_wasmcart_video.c    SDL2 video backend → wasmcart framebuffer / GL
  SDL_wasmcart_audio.c    SDL2 audio backend → wasmcart audio ring
  SDL_config_wasmcart.h   SDL2 build config for the wasmcart target
  build_sdl2_wc.sh        builds libSDL2_wc.a against wasi-sdk / emscripten
include/
  wc_sdl_stubs.h          minimal SDL2 type shims for lighter ports
  stb_image.h / stb_truetype.h / stb_vorbis.c   3rd-party decoders (public domain / MIT)
audio_bridge.{c,h}    PCM bridge helpers
emstubs.c             emscripten runtime stubs
PORTING_GUIDE.md      ★ comprehensive guide: writing new carts + porting existing games
OPENARENA_PORT.md     worked example (ioquake3 → wasmcart)
GL_FULL_GAMES.md      feasibility notes for GL game ports
GL_PORT_CANDIDATES.md
porting_game_notes.md lessons from porting 16+ games
```

## Build

```bash
# builds libSDL2_wc.a (+ libSDL2_ttf_wc.a); point EMSDK at your emsdk checkout
EMSDK=/path/to/emsdk ./sdl2_wc/build_sdl2_wc.sh
```

Compiled objects and static libs are **not** committed (see `.gitignore`) — build
them locally. The build pulls SDL2 from Emscripten's own port cache, so the only
prerequisite is a working emsdk.

## The cart contract

The C-side of the wasmcart contract (`wc_cart.h`) and the lightweight cart-author
SDK (`wc_fb.h`, `wc_gl.h`, math/mixer helpers) live in the main
[wasmcart](https://github.com/wasmcart/wasmcart) repo's `include/`. This repo
depends on that contract; it does not redefine it.

> **Note:** `wc_cart.h` includes `wasmcart.h` (the raw ABI header with the
> `wc_info_t` / `wc_pad_t` struct definitions), which is not currently published
> in either repo — it travels with existing ports. Until it ships in the main
> repo's `include/`, copy it from a port you have, or work from the struct
> layouts in [SPEC.md](https://github.com/wasmcart/wasmcart/blob/main/SPEC.md),
> which are normative.

## Third-party

`stb_image.h`, `stb_truetype.h`, `stb_vorbis.c` are Sean Barrett's stb libraries
(public domain / MIT). SDL2 backend sources derive from SDL2 (zlib license).

## License

MIT (the wasmcart-specific code). Bundled third-party retains its own license.
