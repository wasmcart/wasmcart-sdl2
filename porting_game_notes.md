# Wasmcart Porting Notes

Lessons learned from porting games to the wasmcart ABI.

## Games Ported

| Game | Source | .wasm | .wasc | Audio | Issues |
|------|--------|-------|-------|-------|--------|
| hello | original | 2 KB | 2 KB | tone gen | none - demo |
| snake | original | 4 KB | 4 KB | tone gen | none |
| breakout | original | 8 KB | 8 KB | tone gen | none |
| platformer | original | 9 KB | 9 KB | tone gen | none |
| tetris | original | 11 KB | 11 KB | tone gen | none |
| invaders | original | 12 KB | 12 KB | tone gen | none |
| hello_gl | original (GL) | 44 KB | 89 KB | none | **First GL cart** — textured cube + triangle, stb_image, gamepad |
| angry_tirds | original | 246 KB | 245 KB | PCM | Box2D physics + MP3 sound via asset API |
| lmdave | [MaiZure/lmdave](https://github.com/MaiZure/lmdave) | 229 KB | 43 KB | none | assets loaded via wc_load_asset |
| zel | [superjer/tinyc.games](https://github.com/superjer/tinyc.games) | 311 KB | 61 KB | none | BMP sprites via asset API |
| ccleste | [lemon32767/ccleste](https://github.com/lemon32767/ccleste) | 2.5 MB | 2.3 MB | SDL_mixer (WAV+OGG) | assets via wc_load_asset |
| doom | [doomgeneric](https://github.com/ozkl/doomgeneric) | 28 MB | 11 MB | PCM mix | WAD loaded via asset API, huge size savings |
| flare | [flare-engine](https://github.com/flareteam/flare-engine) | 430 MB | 414 MB | custom | VFS layer over asset API, _filelist.txt for file discovery |
| chromium_bsu | [chromium-bsu](https://chromium-bsu.sourceforge.io/) | 376 KB | 1.6 MB | PCM mix | GL1.x→GLES3 compat layer, wrapper strategy, 0 original files modified |
| neverball | [Neverball](https://neverball.org) (GL) | 828 KB | 17 MB | stb_vorbis | gl4es (GL1.x→GLES2), 0 original files modified, pre-compiled SOL levels |
| neverputt | [Neverball](https://neverball.org) (GL) | 757 KB | 14 MB | stb_vorbis | Shares neverball's support files, separate entry point |
| etr | [Extreme Tux Racer](https://github.com/drodin/extremetuxracer) (GL) | 1.1 MB | 50 MB | stb_vorbis + wc_pcm_mixer | SFML C++→wasmcart, gl4es + GLU, 0 original files modified, 583 assets |

## Key Findings

### 1. Audio Is The Biggest Problem

Games written from scratch for wasmcart can easily generate audio by writing PCM samples
directly to the ring buffer (sine waves, square waves, etc). But ported games use audio
libraries:

- **SDL_mixer** (ccleste) — needs SDL2 audio subsystem, OGG Vorbis decoder, WAV loader,
  multi-channel mixing. Bringing this in via emscripten added ~2.3MB to the binary and
  required 200+ stub functions for emscripten JS/EGL/GL symbols that SDL2 references.
  Used `--wrap` linker flags to intercept SDL_OpenAudioDevice and route mixer output to
  the wasmcart ring buffer. This works but is awful.

- Most retro/indie games use SDL_mixer, FMOD, OpenAL, or similar. Every port will hit
  this same problem.

**Conclusion**: The raw PCM ring buffer is the right abstraction. The cart owns its
entire audio pipeline — SDL_mixer, custom tracker, raw synth, whatever — and writes
final mixed output to the ring buffer. The host just plays it. 2.5MB for SDL_mixer +
OGG decoder is nothing when real games with assets will be 50-200MB+. Don't over-
optimize for binary size. Keep the ABI simple.

### 2. stdio/WASI Is Always A Problem

Every non-trivial C codebase uses printf/fprintf/snprintf somewhere. Emscripten's libc
implements these via WASI fd_write/fd_close/fd_seek imports.

**Solutions tried**:
- `nostdio.h` force-included before everything — works but fragile, breaks when math.h
  or other system headers transitively include stdio
- `-Dprintf=stub_printf` compiler flags — conflicts with stdio.h declarations
- Just provide WASI stubs in CartHost.js — **this is what works**. The stubs are no-ops
  so they're safe (no real file access). CartHost now provides: fd_close, fd_write,
  fd_seek, fd_read, environ_get, environ_sizes_get, proc_exit, clock_time_get.

**Recommendation**: Accept that WASI stubs are necessary for emscripten-compiled carts.
The stubs are safe no-ops. The security boundary is that the cart can't do anything
real with them — no filesystem, no network, no environment variables leak through.

### 3. Frame Rate Mismatch

The host calls wc_render() at 60fps but games run at different speeds:
- PICO-8 games (Celeste): 30fps
- DOS games (Dave): 30fps
- Modern SDL games (Zel): 60fps

Carts currently handle this by skipping game logic every other frame. This works but
is fragile — the cart has to know the host's frame rate.

**Current approach**: The host writes `delta_ms` and `time_ms` to the time struct each frame.
Carts use `delta_ms` for variable timestep or use the frame counter for fixed timestep.
No separate FPS field is needed — `delta_ms` communicates the actual call rate.

### 4. Asset Loading

Games can load assets two ways:

- **Embedded in .wasm** (small carts): Convert assets to C byte arrays at build time
- **From .wasc archive** (recommended for games with assets): Use `wc_asset_size` / `wc_load_asset` API to load assets at runtime from the ZIP archive

The `.wasc` approach is now preferred for any cart with non-trivial assets. Benefits:
- No conversion scripts needed — keep original formats (BMP, WAV, OGG, WAD, etc.)
- ZIP compression reduces distribution size
- Faster dev iteration — change assets without recompiling
- The host generates a virtual `_filelist.txt` asset listing all paths in the archive,
  so carts with VFS layers can pre-register files for directory listing support

Games with virtual filesystems (doom's WAD loader, flare's VFS) benefit most — replace
the file I/O layer with `wc_asset_size`/`wc_load_asset` calls and the rest of the game
code stays unchanged.

### 5. Rendering Replacement

All ported games needed their rendering layer replaced:

| Game | Original | Ported |
|------|----------|--------|
| ccleste | Callback-based (SPR, RECTFILL, etc) → SDL2 surfaces | Same callbacks → 128x128 palette buffer → 2x scale to 320x240 |
| lmdave | SDL_RenderCopy with textures | Tile blitting directly to XRGB8888 framebuffer |
| zel | SDL3 SDL_RenderTexture | Palette-indexed sprite/tile blitting to framebuffer |

Common patterns:
- Replace SDL_RenderCopy/SDL_RenderTexture with a `blit_tile(fb, x, y, data, w, h)` function
- Handle transparency (color-keying: specific color = transparent, or alpha channel)
- Palette remapping
- Camera/scroll offset applied to all draws

### 6. Resolution Handling

Games use different native resolutions:
- Celeste: 128x128 (PICO-8) → scaled 2x to 256x256, centered in 320x240
- Dave: 320x200 → centered vertically in 320x240 (20px Y offset)
- Zel: 300x220 → centered in 320x240 (10px X, 10px Y offset)

The current ABI has the cart declare its framebuffer size in wc_get_info. The host
scales/renders however it wants. But the cart has no idea what the display looks like.

**Recommendation**: Host should communicate preferred/available resolution to the cart
at startup (see below for ABI changes).

### 7. Input Mapping

The gamepad input struct works well for all games tested. Patterns:
- D-pad + left analog stick → movement (with deadzone ~4000-16000)
- A/B → primary action (jump, fire, attack)
- X/Y → secondary action (dash, jetpack)
- Start/Select → menu/pause
- R → save state (in hello demo)

The W3C-style button layout maps naturally to retro game controls.

## Recommended ABI Changes (v2)

### 1. Host Info Struct (written by host before wc_init)

```c
typedef struct {
    uint32_t abi_version;        // host ABI version
    uint32_t preferred_width;    // host's preferred/native resolution
    uint32_t preferred_height;
    uint32_t _reserved0;         // (was host_fps — unused, carts use delta_ms)
    uint32_t audio_sample_rate;  // host audio rate (e.g. 48000)
    uint32_t flags;              // capability flags (stereo, etc)
} wc_host_info_t;
```

Cart reads this in wc_init() to decide its framebuffer size, frame timing, etc.
A cart targeting 128x128 can still use 128x128 — the host will scale it.
A cart that can adapt (e.g. a modern game) can match the preferred resolution.

### 2. Audio — Keep It Simple

The raw PCM ring buffer is the right level of abstraction. The cart brings its own
audio stack (SDL_mixer, custom engine, raw synth) and writes final mixed S16 stereo
to the ring buffer. The host just plays it. No host-side mixing needed.

Real games with full assets will be 50-200MB+. The 2.5MB overhead of SDL_mixer + OGG
decoder is insignificant. Don't add complexity to the ABI to save it.

### 3. Asset/Timing Considerations

Two asset approaches are supported:
- **Embedded .wasm** — assets baked into data sections as C arrays. Still works for small carts.
  Do NOT use emscripten's `--embed-file` (pulls in heavy virtual filesystem).
- **`.wasc` archive** (recommended) — ZIP containing `cart.wasm` + `assets/`. Cart loads assets
  at runtime via `wc_asset_size`/`wc_load_asset`. Use `wasmcart-pack` CLI to create archives.
  The host provides a virtual `_filelist.txt` asset for file discovery.

Carts using `ALLOW_MEMORY_GROWTH=1` with emscripten will automatically import
`emscripten_notify_memory_growth` — CartHost provides a no-op stub for this.

### 8. GL Carts (ABI v2)

The hello_gl example is the first GPU-accelerated cart. Key findings:

**WASM↔JS type mismatches**: WASM passes booleans as `i32` (0 or 1). N-API bindings
for native-gles expect JavaScript `boolean` values. Must coerce with `!!value` in
gl_imports.js for: `glVertexAttribPointer(normalized)`, `glDepthMask(flag)`,
`glColorMask(r,g,b,a)`, `glUniformMatrix*fv(transpose)`. Without this, you get
"A boolean was expected" TypeError at runtime.

**EGL/SDL context conflict**: SDL's `accelerated: true` renderer creates its own EGL
context on the same thread. This conflicts with native-gles's pbuffer context —
`eglMakeCurrent` fails silently and GL calls go to the wrong (or no) context.
Solution: create EGL context BEFORE SDL window, use `accelerated: false` for GL
carts, and call `makeCurrent()` after SDL init.

**stb_image for texture decoding**: Single-header JPEG/PNG decoder, no dependencies,
compiles cleanly into WASM. Must use `STBI_NO_STDIO` (no filesystem in WASM) and
`STBI_ONLY_JPEG` or similar to minimize code. stb_image uses `malloc` internally,
which is provided by emscripten's libc in the WASM binary.

**Asset API + GL textures**: Load compressed image files from .wasc archive, decode
in WASM memory with stb_image, upload to GPU with `glTexImage2D`. The gl_imports.js
wrapper creates a `Uint8Array` view of the decoded pixels in WASM memory and passes
it to the native GL call — zero copy from WASM to GPU.

**Readback pipeline**: The GL readback pipeline is:
`glFinish()` → `glReadPixels(GL_RGBA)` → vertical flip → SDL display.

**Mirrored glyphs ≠ display pipeline bug.** During the Chromium B.S.U. port,
mirrored text glyphs were misdiagnosed as a host display pipeline issue.
Hours were wasted adding shader hacks (`p.x = -p.x`) and changing SDL pixel
formats (`argb8888` → `rgba32`). The actual bug: `TextBitmap.cpp` used
`0x80 >> x` (MSB-left) but font8x8 data uses LSB-left convention (`1 << x`).
One-character fix. Check font/sprite rendering code FIRST when content
appears mirrored.

**Readback pipeline performance**: At 320x240 this costs copy=0.0ms + render=3.5ms on
AMD 890M — negligible. The two-path architecture (readback vs direct-to-screen) is validated.

**Bare .wasm vs .wasc**: Running a GL cart as bare `.wasm` (without .wasc packaging)
means `wc_load_asset` returns -1 for all assets. Carts should handle this gracefully
with fallback textures (e.g., solid magenta 1x1 pixel). Always distribute GL carts
with textures as `.wasc` archives.

### 9. Offline Build Tools Need Real Implementations

When a game has offline tools that pre-process assets (level compilers, texture
packers, sprite sheet generators, etc.), **stub implementations can silently
produce corrupt data**.

**Case study: Neverball's `mapc` level compiler**

Neverball compiles `.map` level files into `.sol` binary files using the `mapc`
tool. `mapc` needs to read texture image dimensions to compute UV coordinates
for each surface — it projects textures onto brush faces using the face plane
equation and the texture's pixel dimensions:

```c
// From mapclib.c — texture coordinate projection
size_image(ctx, material_name, &w, &h);  // get texture dimensions
v_scl(plane_u, plane_u, 64.f / w);       // scale by image width
v_scl(plane_v, plane_v, 64.f / h);       // scale by image height
// ...
uv[0] = v_dot(vertex_pos, plane_u);      // compute texcoord
uv[1] = v_dot(vertex_pos, plane_v);
```

The initial native `mapc` build used a stub `image_load()` that returned NULL
with width=0, height=0 (to avoid needing libpng/libjpeg). This caused
`64.f / 0 = +inf`, producing NaN texture coordinates. The compiled `.sol`
files appeared valid — correct material names, correct flags, correct geometry
— but every surface had NaN UVs, making all textures appear as flat
single-color fills.

**The fix**: Replace the stub with `stb_image.h` (single-header, no external
dependencies, compiles on any platform). The fixed `mapc` produced correct
texture coordinates and surfaces rendered with full texture detail.

**General lesson**: When porting a game that has offline asset processing
tools, those tools need working implementations of any functions they call,
not just stubs. Common examples:

- **Level compilers** that read image dimensions for UV generation
- **Texture packers** that read source images to compute atlas layouts
- **Mesh processors** that read material definitions for LOD/culling
- **Sound converters** that read audio headers for format detection

Stubs that return zero/NULL will compile and run without errors, but the
output data may be subtly wrong. The resulting bugs are hard to diagnose
because the data files look structurally valid — the corruption is in the
numeric values, not the format.

**Recommendation**: Use `stb_image.h` for any native tool that needs to
read image files. It handles PNG, JPEG, BMP, GIF, TGA, PSD, HDR, and PIC
with zero external dependencies. For audio, use `stb_vorbis.c` (OGG) or
`dr_wav.h` / `dr_mp3.h`. These single-header libraries eliminate the need
for system image/audio libraries while providing real, correct output.

### 10. The Zero-Modified-Files Strategy

Both Chromium B.S.U. and Neverball were ported without modifying a single
original source file. This is achieved with:

- **`-include compat_header.h`** — force-included before every file, provides
  SDL type stubs, function overrides, and `#define` guards that block real
  SDL/system headers
- **Replacement files** — `video_wc.c` replaces `video.c`, `audio_wc.c`
  replaces `audio.c`, etc. The build script simply lists the replacement
  file instead of the original
- **Linker-level interception** — for functions that can't be replaced at
  compile time, use `emscripten_set_main_loop_arg` override (define your
  own implementation that captures the callback pointer)

Benefits:
- Upstream updates can be pulled cleanly (git merge/rebase)
- Clear separation between port code and game code
- Easy to diff what the port actually changes
- Multiple ports can share the same source checkout

The key insight is that `-include` runs before any `#include` in the source
file. This means you can `#define SDL_video_h_` to prevent SDL's video
header from being included, then provide your own minimal type stubs. The
original code compiles against your stubs without knowing it.

### 11. GL Translation Layers (gl4es)

For games using OpenGL 1.x fixed-function pipeline (glBegin/glEnd,
glMaterialfv, GL_LIGHTING, display lists, etc.), writing a manual GLES2/3
translation is impractical. These games have hundreds of GL state
combinations that interact (lighting × texturing × fog × blending × etc).

**gl4es** is a proven GL1.x → GLES2 translator that handles all of this.
It compiles to a static library (~2MB in WASM) and routes its GLES2 output
through a GetProcAddress bridge to wasmcart's GL imports.

The bridge pattern:
```c
// gl4es_bridge.c
void *gl4es_GetProcAddress(const char *name) {
    if (strcmp(name, "glActiveTexture") == 0) return (void*)glActiveTexture;
    if (strcmp(name, "glBindBuffer") == 0) return (void*)glBindBuffer;
    // ... ~120 GLES2 functions mapped to wasmcart GL imports
}

void gl4es_bridge_init(int w, int h) {
    set_getprocaddress(gl4es_GetProcAddress);
    set_getmainfbsize(gl4es_getmainfbsize);
    initialize_gl4es();
}
```

**Pitfall**: gl4es calls GL functions during shader compilation
(`fill_program()`) that you might not expect. If a function is missing from
the bridge, you get "null function or function signature mismatch" at
runtime — often only when a specific material/texture combination triggers
a new shader variant. Start with the full wasmcart GL import list and log
warnings for unmapped functions.

### 12. Font Rendering Without SDL_ttf

SDL_ttf depends on FreeType2, which depends on zlib and has complex
initialization. In WASM with `STANDALONE_WASM=1`, FreeType2's
`FT_New_Memory_Face` can corrupt the heap (even with
`-sSUPPORT_LONGJMP=wasm`).

**stb_truetype.h** is a single-header TTF rasterizer with no dependencies
and no setjmp/longjmp usage. It works reliably in WASM. Implement minimal
`TTF_OpenFont` / `TTF_RenderUTF8_Blended` / `TTF_SizeUTF8` wrappers that
use stb_truetype internally, and the game's GUI code works unchanged.

### 13. Emscripten Main Loop Interception

Games with Emscripten build paths use `emscripten_set_main_loop_arg(fn, arg, fps, simulate)` to register a frame callback. For wasmcart, override
this function to capture the callback pointer:

```c
static void (*_em_loop_fn)(void *) = NULL;
static char _em_loop_arg_copy[64];  // static copy of stack-local arg

void emscripten_set_main_loop_arg(em_arg_callback_func fn, void *arg,
                                   int fps, bool simulate) {
    _em_loop_fn = fn;
    memcpy(_em_loop_arg_copy, arg, 64);  // arg is stack-local, copy it!
    _em_loop_arg = _em_loop_arg_copy;
}
```

Then call `_em_loop_fn(_em_loop_arg)` from `wc_render()`.

**Critical**: The `arg` pointer typically points to a stack-local struct in
`main()`. After `main()` returns, that memory is invalid. You MUST copy the
arg data to static storage before `main()` returns. Failure to do this
causes intermittent crashes from dangling pointer access.

### 14. Porting C++ / SFML Games (Extreme Tux Racer)

ETR is the first C++ game ported to wasmcart and the first SFML game. SFML
provides window management, input, audio, and 2D rendering as C++ classes
(sf::Music, sf::Sound, sf::Sprite, sf::Text, sf::RenderWindow). Unlike
SDL which uses C functions that can be individually stubbed or wrapped,
SFML's classes are deeply intertwined — you can't replace one without
replacing all of them.

**The compatibility header approach for C++ frameworks:**

```cpp
// etr_compat.h — force-included before everything
#define ANDROID 1           // re-use Android code paths for file loading
#define SFML_SYSTEM_HPP     // block SFML/System.hpp
#define SFML_WINDOW_HPP     // block SFML/Window.hpp
#define SFML_GRAPHICS_HPP   // block SFML/Graphics.hpp
#define SFML_AUDIO_HPP      // block SFML/Audio.hpp

namespace sf {
    class Music { ... };         // real implementations with stb_vorbis
    class SoundBuffer { ... };   // real implementations with wc_pcm_mixer
    class Sound { ... };         // wraps mixer channels
    class Sprite { ... };        // GL quad rendering
    class Text { ... };          // stb_truetype rendering
    // etc.
}
```

The `-include etr_compat.h` flag in the build script ensures this header
is processed before any `#include` in every source file. The `#define`
guards prevent SFML's real headers from being included, and the stub
namespace provides compatible class interfaces. Original ETR code compiles
unmodified against these stubs.

**Key differences from C/SDL porting:**
- C++ classes need full method signatures to match — can't just `#define` a function away
- Constructor/destructor semantics matter (RAII resource management)
- Template and operator overloads (sf::Vector2, sf::Color) need compatible types
- The `#define ANDROID 1` trick re-uses the game's existing Android code paths
  (which already use `sf::FileInputStream` for asset loading instead of `std::ifstream`)

**The replacement file set for ETR (7 files replacing 5 SFML-dependent modules):**
- `winsys_wc.cpp` → CWinsys class, gl4es init, pushGLStates/popGLStates
- `audio_wc.cpp` → CSound/CMusic with stb_vorbis + wc_pcm_mixer
- `textures_wc.cpp` → stb_image texture loading
- `font_wc.cpp` → stb_truetype font rendering, sf::Text/Sprite/RectangleShape draw
- `translation_wc.cpp` → i18n stub (English only)
- `states_wc.cpp` → frame-at-a-time state machine
- `etr_cart.cpp` → wasmcart entry points

### 15. Texture Vertical Flip Convention

Different frameworks have different texture coordinate conventions. Getting
this wrong makes every texture in the game render upside down.

| Framework | V=0 means | stb_image needs flip? |
|-----------|-----------|----------------------|
| SDL/OpenGL standard | Bottom of image | YES — flip before glTexImage2D |
| SFML | Top of image | NO — stb_image's top-first output matches |
| Raw GL (check code) | Depends on game's UV convention | Check the game |

**stb_image** always loads images top-to-bottom (row 0 = top). In standard
OpenGL, texture coordinate V=0 is at the bottom. So SDL-based games (which
use SDL_image with standard GL) need textures flipped before upload.

SFML stores textures top-first, with V=0 at the top. All ETR rendering
code (terrain texgen, tree billboards, skybox, HUD, fish sprites) uses
V=0 = top, V=1 = bottom. If you flip textures for an SFML game, every
texture in the game breaks — trees, characters, terrain, UI icons, everything.

**Rule**: Always check what V=0 means in the original framework's code
before deciding whether to flip. The default assumption of "just flip for
GL" is wrong for SFML games.

### 16. Client-Side Vertex Arrays in WASM (gl4es)

gl4es translates GL1.x immediate mode (glBegin/glEnd) into GLES2
glVertexAttribPointer + glDrawArrays calls. When no VBO is bound, gl4es
passes WASM linear memory offsets as "pointers" to glVertexAttribPointer.

In native code, these pointers are valid CPU addresses that the GPU driver
can read. But in WASM→native-gles, these "pointers" are WASM address space
offsets (e.g., 0x10000) — the GPU driver tries to read from address 0x10000
in the host process, which is invalid → segfault.

**Fix (in wasmcart core, gl_imports.js):** Track `GL_ARRAY_BUFFER` binding
state. When `glVertexAttribPointer` is called with no VBO bound, store the
WASM pointer info. At draw time (`glDrawArrays`/`glDrawElements`), copy
the data from WASM memory into temporary VBOs, bind them, then draw. This
fix is transparent to the cart and benefits all gl4es-based carts.

### 17. Time-Based Audio Mixing

When mixing audio in `wc_render()`, do NOT use a fixed sample count like
`host_rate / 60`. Frame timing is not perfectly uniform — menus, loading
screens, and complex scenes all have variable frame durations. A fixed
count causes choppy audio when frames take longer or shorter than 16.6ms.

**Use wall-clock time instead:**

```c
static unsigned int last_mix_ms = 0;

void audio_mix_frame(void) {
    unsigned int now_ms = time_info.time_ms;
    unsigned int delta_ms = now_ms - last_mix_ms;
    if (delta_ms == 0) return;
    if (delta_ms > 100) delta_ms = 100;  // cap to prevent huge decodes
    last_mix_ms = now_ms;

    int frames_to_mix = (int)((uint64_t)host_rate * delta_ms / 1000);
    // ... mix this many frames into ring buffer
}
```

This produces smooth audio regardless of frame timing variability. The
`wc_pcm_mixer.h` documentation includes this pattern.

### 18. GLU in WASM (ptitSeb/GLU)

Some GL1.x games use GLU functions — `gluPerspective`, `gluLookAt`,
`gluSphere`, `gluCylinder`, etc. GLU is not part of GLES and is not
provided by gl4es.

**ptitSeb/GLU** (https://github.com/nichmack/GLU) provides a standalone
GLU implementation that compiles to WASM. Key points:
- Builds as a static library with emcc (see `build_glu.sh`)
- Provides quadric objects (gluSphere, gluCylinder) needed for character
  models that use spherical body parts (ETR's Tux)
- `mgluErrorString` may not be exported — provide a stub if the linker
  complains about it
- Link with `-lglu` or include the .a in your build command

### 19. stb_truetype for SFML sf::Text Replacement

When replacing SFML's text rendering with stb_truetype, watch for
coordinate system differences:

- **stb_truetype**: `stbtt_GetBakedQuad` treats Y as the **baseline**.
  Glyphs extend ABOVE the Y coordinate.
- **SFML sf::Text**: `setPosition(x, y)` means Y = **top** of the text
  bounding box.

If you pass SFML Y positions directly to stbtt, text renders too high
(above where it should be). **Fix**: add the font's ascent to Y:

```cpp
int ascent, descent, lineGap;
stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
float scale = stbtt_ScaleForPixelHeight(&fontInfo, fontSize);
y += ascent * scale;  // offset from bounding box top to baseline
```

This is critical for GUI elements like combo boxes and labels where text
must appear inside a bordered frame at a specific position.

### 20. Lightweight Audio Without SDL_mixer

For games that use simple audio (WAV sound effects + OGG music), the
combination of **wc_pcm_mixer.h** (WAV SFX) and **stb_vorbis.c** (OGG
music) is far lighter than SDL_mixer:

| Approach | Binary overhead | Dependencies |
|----------|----------------|--------------|
| SDL_mixer via emscripten | ~2.3 MB | SDL2, libvorbis, 200+ stubs |
| wc_pcm_mixer + stb_vorbis | ~80 KB | None (STB-style single headers) |

The pattern:
1. **SFX**: `wc_mixer_load_wav()` to load WAV files into slots,
   `wc_mixer_play()` to start playback on a channel
2. **Music**: `stb_vorbis_open_memory()` to open OGG, decode per-frame
   with `stb_vorbis_get_samples_short_interleaved()`
3. **Mixing**: Each frame, decode music + iterate mixer channels into a
   temp buffer, then copy to the ring buffer

This handles ETR's 10 WAV SFX (~4MB, 16-bit stereo 44100Hz) and 10 OGG
music tracks (~13MB) with proper volume control, looping, and rate
conversion — all in ~80KB of WASM code vs 2.3MB for the SDL_mixer approach.

**Recommendation**: Use wc_pcm_mixer + stb_vorbis for new ports unless the
game requires SDL_mixer-specific features (MOD/MIDI/MP3). The binary
savings and reduced stub complexity are significant.

### 21. Emscripten as Compiler, Not Runtime

After porting 6 large games by hand (Neverball, GZDoom, OpenArena, Chromium BSU,
ETR, Flare), a key insight emerged: most of the porting work was reimplementing
what Emscripten already provides — SDL2 stubs, GL translation, audio bridges,
filesystem shims. Many of these games already had working Emscripten browser
builds. The hand-porting effort was weeks per game.

**The better approach**: Use Emscripten as a **compiler** with custom SDL2 backend
libraries that target wasmcart imports. The game compiles with `emcc` + SDL2/GL
as normal, but the SDL2 video backend writes to `wc_framebuffer`, the audio backend
writes to `wc_audio_ring`, and the input backend reads `wc_pads[]`. The output is
a standard wasmcart cart — no JS glue, no browser dependency.

Emscripten's WASM output is standard WebAssembly. The browser dependency comes
from the runtime libraries, not the compiler. Replace those libraries with
wasmcart-targeting backends and the WASM runs anywhere wasmcart runs.

**What this approach enables:**
- SDL2 GPU-accelerated 2D rendering (`SDL_Renderer` + `SDL_RENDERER_ACCELERATED`)
  — uses GL internally, routes through wasmcart's `gl.*` imports. Not possible
  with hand-porting (only software `wc_fb.h` or raw GL available today).
- Porting large games in hours instead of weeks
- Godot game support (reimplement ~150 `godot_js_*()` bridge functions in C
  instead of building a whole new `platform/wasmcart/` backend)
- Any Emscripten-targeting game engine becomes a potential wasmcart target

**What doesn't change**: The wasmcart ABI, CartHost.js, gl_imports.js, retroemu,
the .wasc format — all unchanged. This is purely a different porting approach
that produces identical carts.

**Binary size tradeoff**: Emscripten-compiled carts include the Emscripten libc
and SDL2 runtime (~3-10MB) vs hand-ported carts (~50KB-1MB code). This is
acceptable — asset-heavy games are dominated by asset size (ETR: 50MB assets,
OpenArena: 391MB assets), not code size.

See PORTING_GUIDE.md "Emscripten SDL2 Backend Approach" section for full details.

### 22. GL Host Implementation Gotchas (gl_imports.cpp / gl_imports.js)

When implementing the GL import bridge for a new host (native, libretro, etc.),
these are the non-obvious functions and behaviors that caused real bugs:

**`glGetInternalformativ` — required for Skia Ganesh GPU rendering:**
Ganesh queries `glGetInternalformativ(GL_RENDERBUFFER, GL_RGBA8, GL_SAMPLES, ...)`
to determine max MSAA sample count. If this function is missing (auto-stubbed to
no-op), it returns 0 samples → Ganesh can't create its render target → falls back
to software Skia (~60 FPS CPU-bound vs 700+ FPS GPU). This is an ES 3.0 function
that's easy to miss since most GL carts don't call it directly.

**Signed blit coordinates — Ganesh uses Y-inverted blits:**
Ganesh resolves its MSAA render target to the output FBO via `glBlitFramebuffer`
with Y-inverted source coordinates (srcY0 > srcY1). If the host tracks blit
dimensions as `uint32_t`, negative height wraps to ~4 billion → corrupts the
final blit-to-screen → black screen. Use `int32_t` and absolute values:
```c
int32_t sh = srcY1 - srcY0;
if (sh < 0) sh = -sh;
```

**GL_VERSION MUST be "OpenGL ES 3.0 wasmcart" — ALWAYS:**
The host MUST report `"OpenGL ES 3.0 wasmcart"` for `GL_VERSION` regardless of
the actual GPU driver or GL context type. This includes:
- Native EGL contexts (Mesa reports ES 3.2 — override to 3.0)
- RetroArch Core 3.3 contexts (GLX — still report ES 3.0, NOT 3.3)
- Any future host

Core 3.3 contexts accept ES 3.0 shaders (`#version 300 es`) via
`GL_ARB_ES3_compatibility`. There is NEVER a reason to report a desktop GL
version. Reporting "3.3" causes GPU engines (Ganesh) to request GL 3.3
functions not in the WASM import table. Reporting "ES 3.2" causes engines to
request ES 3.1+ functions. Only "ES 3.0" is safe.

**GL_EXTENSIONS — pass through real, DO NOT filter:**
Extensions must pass through from the real driver. Godot needs real extensions
for texture format detection (e.g., `GL_EXT_texture_compression_s3tc`).
Filtering extensions breaks Godot PNG loading. If a GPU engine (Ganesh) probes
extensions and fails, that's a cart-side problem — the cart must hide extensions
in its own getProcAddress callback (see section 23).

**GL_NUM_EXTENSIONS and glGetStringi — pass through real:**
Same as GL_EXTENSIONS. The host is a transparent passthrough for extension
queries. Only GL_VERSION is overridden.

**The complete list of "hidden required" GL functions:**
- `glGetInternalformativ` (Skia Ganesh MSAA)
- `glGetActiveAttrib` / `glGetActiveUniform` (gl4es shader linking)
- `glGetAttribLocation` / `glGetUniformLocation` (all shader-based carts)
- `glGetProgramInfoLog` / `glGetShaderInfoLog` (shader compile errors)
- `glIsBuffer` / `glIsTexture` / `glIsFramebuffer` etc. (state queries)
- `glGetBufferParameteriv` / `glGetRenderbufferParameteriv` (Neverball/gl4es)
- Boolean coercion for N-API hosts: `glVertexAttribPointer(normalized)`,
  `glDepthMask(flag)`, `glColorMask(r,g,b,a)`, `glUniformMatrix*fv(transpose)`
  — WASM passes `i32`, N-API expects `boolean`, must use `!!value`

### 23. wasmcart GL Surface Spec (CRITICAL for all GPU carts)

**The wasmcart GL surface is WebGL2 = OpenGL ES 3.0.** This is the ceiling for all
hosts (browser, Node.js, native, RetroArch). Every host MUST present the same
ES 3.0 surface. Every cart MUST work within ES 3.0.

#### What the host provides

| Query | Value |
|-------|-------|
| `GL_VERSION` | `"OpenGL ES 3.0 wasmcart"` — ALWAYS, on ALL context types |
| `GL_EXTENSIONS` | Real driver extensions (passthrough, NEVER filter) |
| `glGetStringi(GL_EXTENSIONS, i)` | Real driver extensions (passthrough) |
| `GL_NUM_EXTENSIONS` | Real count (passthrough) |
| `GL_SHADING_LANGUAGE_VERSION` | Real driver value (passthrough) |
| All ES 3.0 core functions | Real implementations |
| Extension functions the cart imports | Real if available, no-op stub if not |

**NEVER report a desktop GL version (3.3, 4.6, etc.) even on Core contexts.**
Core 3.3 accepts ES 3.0 shaders via `GL_ARB_ES3_compatibility`. Reporting
"3.3" causes GPU engines to request GL 3.3-specific functions not in the
WASM import table.

**NEVER filter GL_EXTENSIONS.** Godot and other engines rely on real extension
strings for texture format detection. If a GPU engine probes extensions and
fails, that's a cart-side problem (see getProcAddress pattern below).

#### What the cart MUST do

1. **Only call GL functions declared as WASM imports.** There is no
   `eglGetProcAddress` or runtime function discovery in WASM. If a function
   isn't in the cart's import table, it doesn't exist.

2. **Only use ES 3.0 core features.** The `GL_VERSION` says ES 3.0. Carts
   MUST NOT use ES 3.1+ core features (compute shaders, SSBO, image load/store,
   etc.) even if the underlying driver supports them.

3. **Extension functions require WASM imports.** If a cart wants to use an
   extension function (e.g., `glBlendBarrierKHR`), it must declare it as a WASM
   import. If the host doesn't provide it, the auto-stub returns 0/no-op.
   The cart should check `GL_EXTENSIONS` before calling extension functions.

4. **Do NOT probe function pointers at runtime.** Engines like Skia Ganesh use
   `getProcAddress` callbacks to build GL function tables. In WASM, this callback
   must return pointers to the cart's own wrapper functions (which call the WASM
   imports), NOT probe the host for function pointers. See below.

#### GPU engines with getProcAddress (Skia Ganesh, etc.)

Many GPU engines discover GL capabilities by:
1. Reading `GL_EXTENSIONS` string
2. Calling `getProcAddress("glSomeFunctionEXT")` for each extension
3. Failing validation if an advertised extension's function pointers are null

In WASM, the `getProcAddress` callback is cart-side code that maps function
names to WASM wrapper functions. The cart controls what this callback returns.

**The correct pattern:**

```cpp
// Cart-side getProcAddress callback
GrGLFuncPtr my_get_proc(void* ctx, const char name[]) {
    // Map to wrapper functions for GL imports the cart actually has
    if (strcmp(name, "glEnable") == 0) return (GrGLFuncPtr)w_glEnable;
    if (strcmp(name, "glDisable") == 0) return (GrGLFuncPtr)w_glDisable;
    // ... all ES 3.0 core functions ...

    // For GL queries: override to hide extensions from the engine
    // so it doesn't probe for function pointers we can't provide
    if (strcmp(name, "glGetString") == 0) return (GrGLFuncPtr)my_glGetString;
    if (strcmp(name, "glGetStringi") == 0) return (GrGLFuncPtr)my_glGetStringi;
    if (strcmp(name, "glGetIntegerv") == 0) return (GrGLFuncPtr)my_glGetIntegerv;

    return nullptr; // unknown function — engine will skip it
}

// Override glGetString to hide extensions from the engine
const unsigned char* my_glGetString(GLenum name) {
    if (name == GL_EXTENSIONS) return (const unsigned char*)""; // no extensions
    return glGetString(name); // real value for everything else
}

const unsigned char* my_glGetStringi(GLenum name, GLuint index) {
    if (name == GL_EXTENSIONS) return nullptr; // no extensions
    return glGetStringi(name, index);
}

void my_glGetIntegerv(GLenum pname, GLint* data) {
    if (pname == GL_NUM_EXTENSIONS) { *data = 0; return; }
    glGetIntegerv(pname, data);
}
```

This way:
- The engine sees ES 3.0 with zero extensions
- It only uses core ES 3.0 functions (all available as WASM imports)
- Validation passes because no extension function pointers are expected
- The real host extensions remain available to other parts of the cart
  (e.g., Godot's texture format detection uses the actual WASM imports,
  not the engine's getProcAddress callback)

#### Why this matters

Without this pattern, the same cart behaves differently on different hosts:
- **Browser (WebGL2):** extensions are WebGL2-only, engine works
- **Node.js host:** extensions are empty, engine works
- **Native host (Mesa):** real extensions with 100+ entries, engine probes
  for function pointers, gets null, validation fails, falls back to CPU

The wasmcart promise is: **same .wasc runs everywhere.** This requires carts
to stay within ES 3.0 and not rely on host-specific extension availability.

#### GLSL generation: use GL_SHADING_LANGUAGE_VERSION, not GL_VERSION

`GL_VERSION` is always `"OpenGL ES 3.0 wasmcart"` — it tells the cart which
**API features** are available (ES 3.0 core). But the **GLSL compiler** on the
underlying context may be desktop GLSL (RetroArch Core 3.3/GLX) or ES GLSL
(EGL GLES3, WebGL2).

`GL_SHADING_LANGUAGE_VERSION` is passed through real from the driver. Cart
engines that generate shaders MUST check this to determine the correct GLSL
dialect:

| Host | GL_VERSION (overridden) | GL_SHADING_LANGUAGE_VERSION (real) | Correct GLSL |
|------|------------------------|-----------------------------------|-------------|
| Browser (WebGL2) | OpenGL ES 3.0 wasmcart | OpenGL ES GLSL ES 3.00 | `#version 300 es` |
| Node.js (EGL GLES3) | OpenGL ES 3.0 wasmcart | OpenGL ES GLSL ES 3.20 | `#version 300 es` |
| wasmcart-native (EGL GLES3) | OpenGL ES 3.0 wasmcart | OpenGL ES GLSL ES 3.20 | `#version 300 es` |
| RetroArch (GLX Core 3.3) | OpenGL ES 3.0 wasmcart | 4.60 | `#version 330` (or higher) |

**Rule: if `GL_SHADING_LANGUAGE_VERSION` contains "ES", use ES GLSL. Otherwise
use desktop GLSL.** Three.js does this correctly. Skia Ganesh requires special
handling (see `wasmcart-jsgame/ganesh.md`).

Carts that only use ES 3.0 features in their shaders can use `#version 300 es`
on GLES contexts and `#version 330` on desktop contexts — the API calls are
identical, only the shader preamble differs.
