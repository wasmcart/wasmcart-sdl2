# OpenArena → wasmcart Port Guide

> **STATUS: COMPLETE.** The working port is at `examples/openarena2/` (see its README.md).
> This document was the original planning/research guide. `examples/openarena/` is the old
> broken port; `examples/openarena2/` uses unmodified upstream ioquake3 with 0 engine patches.
> The GLES 3.0 path works out of the box — no engine FBO pipeline, no custom tonemap, just
> `USE_FAST_LIGHT` (r_normalMapping 0, r_specularMapping 0) for correct lighting.

## Overview

| Field | Value |
|-------|-------|
| **Game** | OpenArena — arena FPS, ioquake3 engine (id Tech 3) |
| **Engine** | ioquake3 (upstream, `git clone https://github.com/ioquake/ioq3.git`) |
| **Language** | C (~200K lines engine + ~100K lines game/cgame/ui/botlib) |
| **Renderer** | `renderergl2` — GLSL shaders, VBOs, FBOs, **built-in GLES2/3 path** |
| **Audio** | DMA mixer (S16 stereo ring buffer) — maps directly to wasmcart PCM |
| **Assets** | ~391 MB (pk3 files = ZIP archives, all GPLv2) |
| **License** | GPLv2 (engine + all assets — fully redistributable) |
| **Port dir** | `examples/openarena2/` |
| **Build** | 1.9 MB .wasm, 391 MB .wasc |
| **Strategy** | 8 platform files + 0 engine patches (unmodified upstream ioquake3) |

## Why This Port Is Feasible

1. **renderergl2 already has a battle-tested GLES path.** It detects `qglesMajorVersion`, adjusts texture formats, uses `glClearDepthf`/`glDepthRangef`, stubs `glDrawBuffer`/`glPolygonMode`. On Emscripten it targets WebGL 2.0 = GLES 3.0 — exactly what wasmcart provides.

2. **The DMA audio system is almost identical to wasmcart's model.** The engine's internal mixer writes S16 stereo PCM into a ring buffer. SDL's audio callback copies it out. Replace that callback with writes to `wc_audio_ring[]` and it works.

3. **The main loop is already frame-based.** ioquake3 has `#ifdef __EMSCRIPTEN__` support calling `Com_Frame()` as a callback. Our `wc_render()` does the same thing.

4. **100% of GL calls are already in gl_imports.js.** renderergl2 uses VBOs, shaders, FBOs, samplers, VAOs — all available. No GL compat layer needed (unlike Chromium B.S.U.).

5. **Assets are all GPL.** No need to "bring your own" game data like Doom/GZDoom.

## Source Repos

```bash
# Engine (mainline ioquake3)
git clone https://github.com/ioquake/ioq3.git /tmp/ioq3

# OpenArena assets (pk3 files)
# Download from https://sourceforge.net/projects/oarena/
# Or use the OpenArena game code repo for QVM source:
git clone https://github.com/OpenArena/gamecode.git /tmp/oa-gamecode
```

## Engine Architecture

```
code/
├── client/          # Client engine (rendering triggers, sound, prediction)
│   ├── snd_main.c       # Sound system entry (selects DMA or OpenAL)
│   ├── snd_dma.c        # DMA ring buffer mixer
│   ├── snd_mix.c        # Sample mixing
│   ├── snd_mem.c        # Sound memory/loading
│   ├── snd_codec.c      # Codec dispatcher
│   ├── snd_codec_wav.c  # WAV decoder
│   ├── snd_codec_ogg.c  # OGG Vorbis decoder
│   └── cl_cgame.c       # Client game VM dispatch
├── server/          # Server engine (game state, snapshots, bot dispatch)
│   └── sv_game.c        # Server game VM dispatch
├── qcommon/         # Shared code (filesystem, networking, VM, commands)
│   ├── files.c          # Virtual filesystem (pk3/ZIP loading)
│   ├── vm.c             # VM manager (VM_Create, VM_Call)
│   ├── vm_interpreted.c # QVM bytecode interpreter
│   ├── unzip.c          # Minizip (pk3 decompression)
│   └── ioapi.c          # Minizip I/O abstraction
├── renderergl2/     # GLES-compatible programmable renderer (28 .c files)
│   ├── tr_backend.c     # Draw call submission
│   ├── tr_glsl.c        # GLSL shader management (30 shaders)
│   ├── tr_vbo.c         # VBO management
│   ├── tr_fbo.c         # FBO management
│   ├── tr_image.c       # Texture loading (JPEG, TGA, BMP, PNG, PCX)
│   ├── tr_shade.c       # Surface shading (main draw path)
│   ├── tr_init.c        # Renderer init, mode table
│   └── glsl/            # 15 vertex/fragment shader pairs
├── renderercommon/  # Shared renderer code (image loaders, font, qgl)
│   └── qgl.h            # GL function pointer table (X-macro pattern)
├── sdl/             # Platform layer (5 files — THIS IS WHAT WE REPLACE)
│   ├── sdl_glimp.c      # GL context, function loader, GLES detection
│   ├── sdl_input.c      # Keyboard/mouse/joystick input
│   ├── sdl_snd.c        # DMA sound via SDL audio callback
│   ├── sdl_gamma.c      # Hardware gamma ramp
│   └── sdl_icon.h       # Window icon
├── sys/             # OS-level code (main, signals, dlopen)
│   ├── sys_main.c       # main(), Com_Init, main loop
│   └── sys_unix.c       # Sys_Milliseconds, Sys_Cwd, etc.
├── cgame/           # Client game logic (HUD, effects, prediction)
├── game/            # Server game logic (rules, physics, items)
├── q3_ui/           # Menu/UI system
├── botlib/          # Bot AI library
└── thirdparty/      # Bundled: zlib, libjpeg, libogg, libvorbis, libopus
```

## What We Replace

### Platform layer: 5 files → 5 new files

| Original | Replacement | Purpose |
|----------|-------------|---------|
| `code/sdl/sdl_glimp.c` | `wc_glimp.c` | GL init, function loader → assign qgl pointers directly |
| `code/sdl/sdl_input.c` | `wc_input.c` | Gamepad → Q3 event queue |
| `code/sdl/sdl_snd.c` | `wc_snd.c` | DMA ring buffer → wasmcart PCM ring buffer |
| `code/sdl/sdl_gamma.c` | `wc_gamma.c` | Stub (no gamma control) |
| `code/sys/sys_main.c` | `wc_main.c` | wasmcart entry point (wc_get_info/wc_init/wc_render) |

### QVM system: compile statically

Instead of loading `.qvm` bytecode files at runtime, compile `cgame/`, `game/`, `q3_ui/`, and `botlib/` directly into the WASM binary. This avoids the double-interpretation penalty (QVM interpreter running inside WASM interpreter).

Wire `VM_Create()` to call directly into the static `vmMain` functions instead of `dlopen` or QVM loading.

### Filesystem: pk3 → wasc assets

Two approaches, in order of preference:

**Option A: Flatten pk3 contents into .wasc** (recommended)
- Extract all pk3 contents into `assets/baseoa/`
- Replace `FS_LoadZipFile()` to scan `_filelist.txt` and load files via `wc_load_asset()`
- No minizip/zlib needed at runtime — simplest approach
- Downside: larger .wasc (no per-file compression)

**Option B: Keep pk3 files as assets**
- Load `pak0.pk3` through `pak6.pk3` via `wc_load_asset()` into memory
- Feed the memory buffers to minizip via a custom `ioapi` that reads from memory
- Keeps the original filesystem code mostly intact
- Needs zlib/minizip compiled in (already bundled in ioquake3)

### Audio: DMA → wasmcart PCM

The engine's `snd_dma.c` / `snd_mix.c` already does all mixing internally and writes final S16 stereo PCM to a ring buffer. The SDL backend just copies it out. We replace 5 functions:

```c
// wc_snd.c — replaces sdl_snd.c

qboolean SNDDMA_Init(void) {
    // Set dma struct to match wasmcart format:
    //   48000 Hz, 16-bit, stereo
    dma.speed = 48000;
    dma.samplebits = 16;
    dma.channels = 2;
    dma.samples = AUDIO_CAP * 2;  // match wc_audio_ring size
    dma.buffer = (byte *)wc_audio_ring;
    return qtrue;
}

int SNDDMA_GetDMAPos(void) {
    return wc_audio_write_cursor * 2;  // convert frames to samples
}

void SNDDMA_BeginPainting(void) { /* no-op, single-threaded */ }
void SNDDMA_Submit(void) { /* advance wc_audio_write_cursor */ }
void SNDDMA_Shutdown(void) { /* no-op */ }
```

The engine's mixer will write directly into `wc_audio_ring[]`. The host reads it out as usual.

### Networking: remove entirely

- Set `BUILD_SERVER=OFF` in CMake (no dedicated server)
- Disable `NET_Init()` or stub it
- The game still runs single-player with bots — the local client/server architecture works in-process

## Files to Create (in `examples/openarena/`)

| File | Purpose |
|------|---------|
| `wc_main.c` | wasmcart entry point: `wc_get_info`, `wc_init`, `wc_render` |
| `wc_glimp.c` | GL init: assign `qgl*` pointers to wasmcart GL imports |
| `wc_input.c` | Gamepad → Q3 event queue mapping |
| `wc_snd.c` | DMA sound → wasmcart PCM ring buffer |
| `wc_gamma.c` | Stub (no hardware gamma) |
| `wc_fs.c` | Filesystem adapter (pk3 contents via `wc_load_asset`) |
| `build.sh` | Build script |
| `pack.sh` | Asset packaging (pk3 → .wasc) |

## Files Excluded from Build

| File | Reason |
|------|--------|
| `code/sdl/sdl_glimp.c` | Replaced by `wc_glimp.c` |
| `code/sdl/sdl_input.c` | Replaced by `wc_input.c` |
| `code/sdl/sdl_snd.c` | Replaced by `wc_snd.c` |
| `code/sdl/sdl_gamma.c` | Replaced by `wc_gamma.c` |
| `code/sys/sys_main.c` | Replaced by `wc_main.c` |
| `code/sys/sys_unix.c` | Unix-specific, replaced by stubs in `wc_main.c` |
| `code/sys/con_tty.c` | No terminal |
| `code/sys/con_log.c` | No log files |
| `code/sys/sys_autoupdater.c` | No auto-update |
| `code/client/snd_openal.c` | No OpenAL — use DMA path |
| `code/qcommon/vm_x86.c` | No JIT in WASM |
| `code/qcommon/vm_armv7l.c` | No JIT in WASM |
| `code/qcommon/vm_interpreted.c` | Not needed — static linking |
| `code/renderergl1/` (all) | Use renderergl2 only |
| `code/null/` (all) | Headless stubs, not needed |

## GL Function Mapping

### QGL loader (qgl.h) → wasmcart gl_imports

ioquake3 loads GL functions via `SDL_GL_GetProcAddress()` into `qgl*` function pointers. For wasmcart, we assign them directly in `wc_glimp.c`:

```c
// wc_glimp.c — GLimp_GetProcAddresses replacement

// These are the actual WASM imports from gl_imports.js.
// Declared with import_module("gl") in wasmcart.h.
extern void glEnable(unsigned int cap);
extern void glDisable(unsigned int cap);
// ... etc

void GLimp_GetProcAddresses(void) {
    // QGL_1_1_PROCS
    qglBindTexture = glBindTexture;
    qglBlendFunc = glBlendFunc;
    qglClear = glClear;
    qglClearColor = glClearColor;
    qglColorMask = glColorMask;
    qglCullFace = glCullFace;
    qglDepthFunc = glDepthFunc;
    qglDepthMask = glDepthMask;
    qglDisable = glDisable;
    qglDrawArrays = glDrawArrays;
    qglDrawElements = glDrawElements;
    qglEnable = glEnable;
    qglFinish = glFinish;
    qglFrontFace = glFrontFace;
    qglGenTextures = glGenTextures;
    qglGetError = glGetError;
    qglGetIntegerv = glGetIntegerv;
    qglGetString = glGetString;
    qglLineWidth = glLineWidth;
    qglPolygonOffset = glPolygonOffset;
    qglReadPixels = glReadPixels;
    qglScissor = glScissor;
    qglStencilFunc = glStencilFunc;
    qglStencilMask = glStencilMask;
    qglStencilOp = glStencilOp;
    qglTexImage2D = glTexImage2D;
    qglTexParameterf = glTexParameterf;
    qglTexParameteri = glTexParameteri;
    qglTexSubImage2D = glTexSubImage2D;
    qglViewport = glViewport;
    qglDeleteTextures = glDeleteTextures;
    qglPixelStorei = glPixelStorei;

    // QGL_ES_1_1_PROCS (GLES-specific)
    qglClearDepthf = glClearDepthf;
    qglDepthRangef = glDepthRangef;

    // QGL_1_3_PROCS
    qglActiveTexture = glActiveTexture;
    qglCompressedTexImage2D = glCompressedTexImage2D;

    // QGL_1_5_PROCS (VBOs)
    qglGenBuffers = glGenBuffers;
    qglDeleteBuffers = glDeleteBuffers;
    qglBindBuffer = glBindBuffer;
    qglBufferData = glBufferData;
    qglBufferSubData = glBufferSubData;

    // QGL_2_0_PROCS (Shaders)
    qglAttachShader = glAttachShader;
    qglCompileShader = glCompileShader;
    qglCreateProgram = glCreateProgram;
    qglCreateShader = glCreateShader;
    qglDeleteProgram = glDeleteProgram;
    qglDeleteShader = glDeleteShader;
    qglDetachShader = glDetachShader;
    qglGetProgramInfoLog = glGetProgramInfoLog;
    qglGetProgramiv = glGetProgramiv;
    qglGetShaderInfoLog = glGetShaderInfoLog;
    qglGetShaderiv = glGetShaderiv;
    qglGetUniformLocation = glGetUniformLocation;
    qglLinkProgram = glLinkProgram;
    qglShaderSource = glShaderSource;
    qglUseProgram = glUseProgram;
    qglGetAttribLocation = glGetAttribLocation;
    qglBindAttribLocation = glBindAttribLocation;
    qglUniform1f = glUniform1f;
    qglUniform1i = glUniform1i;
    qglUniform2f = glUniform2f;
    qglUniform3f = glUniform3f;
    qglUniform4f = glUniform4f;
    qglUniform1fv = glUniform1fv;
    qglUniform4fv = glUniform4fv;
    qglUniformMatrix4fv = glUniformMatrix4fv;
    qglEnableVertexAttribArray = glEnableVertexAttribArray;
    qglDisableVertexAttribArray = glDisableVertexAttribArray;
    qglVertexAttribPointer = glVertexAttribPointer;
    qglValidateProgram = glValidateProgram;

    // QGL_3_0_PROCS
    qglGetStringi = glGetStringi;

    // QGL_ARB_framebuffer_object_PROCS
    qglGenFramebuffers = glGenFramebuffers;
    qglDeleteFramebuffers = glDeleteFramebuffers;
    qglBindFramebuffer = glBindFramebuffer;
    qglCheckFramebufferStatus = glCheckFramebufferStatus;
    qglFramebufferTexture2D = glFramebufferTexture2D;
    qglFramebufferRenderbuffer = glFramebufferRenderbuffer;
    qglGenRenderbuffers = glGenRenderbuffers;
    qglDeleteRenderbuffers = glDeleteRenderbuffers;
    qglBindRenderbuffer = glBindRenderbuffer;
    qglRenderbufferStorage = glRenderbufferStorage;
    qglGenerateMipmap = glGenerateMipmap;

    // QGL_ARB_vertex_array_object_PROCS
    qglGenVertexArrays = glGenVertexArrays;
    qglDeleteVertexArrays = glDeleteVertexArrays;
    qglBindVertexArray = glBindVertexArray;

    // Samplers (ES3)
    qglGenSamplers = glGenSamplers;
    qglDeleteSamplers = glDeleteSamplers;
    qglBindSampler = glBindSampler;
    qglSamplerParameteri = glSamplerParameteri;
    qglSamplerParameterf = glSamplerParameterf;

    // GLES shims for desktop-only functions
    qglClearDepth = GLimp_GLES_ClearDepth;    // wraps glClearDepthf
    qglDepthRange = GLimp_GLES_DepthRange;    // wraps glDepthRangef
    qglDrawBuffer = GLimp_GLES_DrawBuffer;    // no-op
    qglPolygonMode = GLimp_GLES_PolygonMode;  // no-op

    // Set GLES version so renderer uses ES path
    qglesMajorVersion = 3;
    qglesMinorVersion = 0;
}
```

### Legacy GL calls (stubs needed)

renderergl2 has a few `glBegin`/`glEnd` calls in debug/shadow paths. These are behind `QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS` which are **not loaded on GLES**. The renderer checks `qglesMajorVersion` and skips those code paths. No gl_compat layer needed.

## GLSL Shader Compatibility

renderergl2 includes 30 GLSL shaders (15 vertex/fragment pairs). The shader header is generated at runtime by `GLSL_GetShaderHeader()` in `tr_glsl.c`:

| Context | Header injected |
|---------|----------------|
| GLES 2.0 | `#version 100` + `precision mediump float;` |
| **GLES 3.0** | **`#version 300 es`** + `precision mediump float;` + `#define attribute in` / `#define varying out` |
| Desktop GL 1.30+ | `#version 130` or `#version 150` |

Since we set `qglesMajorVersion = 3`, the renderer will generate `#version 300 es` shaders — correct for our GLES 3.0 EGL context.

**GZDoom lesson:** GZDoom hit a crash because it detected desktop GL 3.3 and generated `#version 330` shaders on a GLES 3.0 context. ioquake3 avoids this because its GLES detection is separate from desktop GL detection — setting `qglesMajorVersion` is sufficient.

## GLES-Specific Behavior (already in renderergl2)

The engine checks `qglesMajorVersion` in ~25 places to handle GLES differences:

- **Index type**: Uses `GL_UNSIGNED_SHORT` for GLES2, `GL_UNSIGNED_INT` for GLES3+ (we get 32-bit indices)
- **HDR**: Disabled on GLES (`tr_bsp.c`)
- **Texture formats**: Restricted to GLES-supported formats (`tr_image.c`)
- **Stencil/depth read**: Fallback paths (`tr_cmds.c`, `tr_flares.c`)
- **Extension loading**: Uses `_EXT` suffixes for GLES2 extensions (`tr_extensions.c`)
- **DrawBuffer/PolygonMode**: Shimmed to no-ops
- **ClearDepth/DepthRange**: Uses `f` variants

## Entry Point

```c
// wc_main.c

#include "wasmcart.h"

// wasmcart buffers
static wc_info_t wc_info;
static wc_host_info_t wc_host_info;
static wc_pad_t wc_pads[4];
static wc_time_t wc_time;
static int16_t wc_audio_ring[48000 * 2 * 2];  // 2 seconds stereo
static uint32_t wc_audio_write_cursor;

// No framebuffer needed — GL cart renders directly
#define DEFAULT_W 640
#define DEFAULT_H 480
#define MAX_W 1920
#define MAX_H 1080

WC_EXPORT wc_info_t *wc_get_info(void) {
    wc_info.abi_version = 2;
    wc_info.default_width = DEFAULT_W;
    wc_info.default_height = DEFAULT_H;
    wc_info.max_width = MAX_W;
    wc_info.max_height = MAX_H;
    wc_info.host_info_ptr = (uint32_t)(uintptr_t)&wc_host_info;
    wc_info.flags = WC_USE_GL | WC_USE_AUDIO;
    wc_info.audio_ptr = (uint32_t)(uintptr_t)wc_audio_ring;
    wc_info.audio_cap = sizeof(wc_audio_ring) / (2 * sizeof(int16_t));
    wc_info.audio_write_cursor_ptr = (uint32_t)(uintptr_t)&wc_audio_write_cursor;
    wc_info.pad_ptr = (uint32_t)(uintptr_t)wc_pads;
    wc_info.time_ptr = (uint32_t)(uintptr_t)&wc_time;
    return &wc_info;
}

static int initialized = 0;

WC_EXPORT void wc_init(void) {
    // Negotiate resolution from host
    uint32_t width = wc_host_info.preferred_width;
    uint32_t height = wc_host_info.preferred_height;
    if (width > MAX_W) width = MAX_W;
    if (height > MAX_H) height = MAX_H;
    if (width == 0) width = DEFAULT_W;
    if (height == 0) height = DEFAULT_H;

    // Set Q3 cvars for resolution
    // r_mode -1, r_customwidth, r_customheight

    // Call Com_Init() with args:
    //   +set r_mode -1
    //   +set r_customwidth <width>
    //   +set r_customheight <height>
    //   +set s_sdlSpeed 48000
    //   +set com_standalone 1
    //   +set fs_basegame baseoa
    //   +set vm_cgame 0  (native/static)
    //   +set vm_game 0
    //   +set vm_ui 0
    //   +set s_useOpenAL 0

    initialized = 1;
}

WC_EXPORT void wc_render(void) {
    if (!initialized) return;

    // Process gamepad input → inject into Q3 event queue
    wc_process_input(&wc_pads[0]);

    // Run one frame
    Com_Frame();
}
```

## Input Mapping

| Gamepad | Q3 Action | Implementation |
|---------|-----------|----------------|
| Left stick | Move (forward/back/strafe) | `+forward`/`+back`/`+moveleft`/`+moveright` |
| Right stick | Look (turn/pitch) | `cl.viewangles[YAW]` / `cl.viewangles[PITCH]` |
| D-pad up/down | Menu navigate | Key events `K_UPARROW`/`K_DOWNARROW` |
| D-pad left/right | Menu adjust / weapon switch | `K_LEFTARROW`/`K_RIGHTARROW` / `weapnext`/`weapprev` |
| A button | Fire / Menu select | `+attack` / `K_ENTER` |
| B button | Jump | `+moveup` |
| X button | Switch weapon | `weapnext` |
| Y button | Use item | `+button2` |
| Left trigger | Zoom | `+zoom` |
| Right trigger | Fire (alt) | `+attack` |
| Left bumper | Previous weapon | `weapprev` |
| Right bumper | Next weapon | `weapnext` |
| Start | Menu/Escape | `K_ESCAPE` |
| Select | Scoreboard | `+scores` |

FPS controls need analog input. Map stick axes to virtual mouse movement:
```c
// Analog stick → mouse delta for look
float rx = wc_pads[0].axes[2];  // right stick X
float ry = wc_pads[0].axes[3];  // right stick Y
if (abs(rx) > DEADZONE) inject_mouse_dx(rx * SENSITIVITY);
if (abs(ry) > DEADZONE) inject_mouse_dy(ry * SENSITIVITY);
```

## Asset Packaging

### pk3 file contents (OpenArena baseoa/)

| File | Size | Contents |
|------|------|----------|
| pak0.pk3 | 14.6 MB | Base game (maps, models, textures, sounds, game QVMs) |
| pak1.pk3 | 2.1 MB | Additional maps |
| pak2.pk3 | 2.4 MB | Player models |
| pak3.pk3 | ~0 MB | Music references |
| pak4.pk3 | 14.9 MB | Textures |
| pak5.pk3 | 0.4 MB | Team Arena |
| **Total** | **~34 MB** | |

### Approach A: Flatten into .wasc (recommended)

```bash
# Extract all pk3 files into a flat directory
mkdir -p /tmp/oa_assets/baseoa
cd /tmp/oa_assets/baseoa
for pk3 in /path/to/openarena/baseoa/pak*.pk3; do
    unzip -o "$pk3"
done

# Build .wasc
node tools/wasmcart-pack.js \
    examples/openarena/openarena.wasm \
    /tmp/oa_assets \
    -o openarena.wasc
```

The .wasc will contain:
```
openarena.wasc:
  manifest.json
  cart.wasm
  assets/
    baseoa/
      maps/        # BSP map files
      models/      # MD3 models
      textures/    # TGA/JPEG textures
      sound/       # WAV sound effects
      music/       # OGG music tracks
      scripts/     # Shader scripts
      gfx/         # UI graphics
      vm/          # QVM files (unused — compiled statically)
```

### Approach B: Keep pk3 files as-is

```
openarena.wasc:
  manifest.json
  cart.wasm
  assets/
    baseoa/
      pak0.pk3
      pak1.pk3
      ...
```

Load each pk3 into memory via `wc_load_asset()`, then wrap with a memory-based `ioapi` for minizip:

```c
// Custom ioapi that reads from in-memory buffer
voidpf ZCALLBACK mem_open(voidpf opaque, const char *filename, int mode) {
    return opaque;  // return the memory buffer pointer
}
uLong ZCALLBACK mem_read(voidpf opaque, voidpf stream, void *buf, uLong size) {
    // Read from memory buffer at current offset
}
long ZCALLBACK mem_seek(voidpf opaque, voidpf stream, uLong offset, int origin) {
    // Seek within memory buffer
}
```

## Build Configuration

### CMake approach (recommended for this codebase)

```bash
#!/bin/bash
# build.sh

IOQDIR=/tmp/ioq3
OUTDIR=/home/monteslu/code/cliemu/wasmcart/examples/openarena

# Cross-compile with emscripten
emcmake cmake -S "$IOQDIR" -B build_wasm \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_CLIENT=ON \
    -DBUILD_SERVER=OFF \
    -DBUILD_RENDERER_OPENGL1=OFF \
    -DBUILD_RENDERER_OPENGL2=ON \
    -DBUILD_GAME_QVM=OFF \
    -DBUILD_GAME_SO=OFF \
    -DUSE_OPENAL=OFF \
    -DUSE_CURL=OFF \
    -DUSE_CODEC_VORBIS=ON \
    -DUSE_INTERNAL_ZLIB=ON \
    -DUSE_INTERNAL_JPEG=ON \
    -DUSE_INTERNAL_OGG=ON \
    -DUSE_INTERNAL_VORBIS=ON \
    -DSTANDALONE=ON \
    -DBASEGAME=baseoa

emmake make -C build_wasm -j$(nproc)
```

### Emscripten linker flags

```
-s STANDALONE_WASM=1
-s ALLOW_MEMORY_GROWTH=1
-s TOTAL_MEMORY=128MB
-s ERROR_ON_UNDEFINED_SYMBOLS=0
-O2
```

### Build-time defines

```
-DSTANDALONE              # No CD key / authorize server
-DBASEGAME=\"baseoa\"     # OpenArena game dir
-DUSE_CODEC_VORBIS=1      # OGG Vorbis for music
-DUSE_OPENAL=0             # DMA mixer only
-DDEFAULT_BASEDIR=\"\"     # No default search path
```

## Known Challenges

### 1. Filesystem replacement is the biggest task

The `files.c` VFS is ~3500 lines with pk3 search paths, hashing, caching, and multiple I/O modes. Two approaches:

- **Approach A (flatten)**: Replace `FS_LoadZipFile()` and related functions to scan `_filelist.txt` and use `wc_load_asset()`. This is a significant rewrite of `files.c` but eliminates zlib/minizip dependency.
- **Approach B (pk3 in memory)**: Smaller change — just replace the `ioapi` layer so minizip reads from memory buffers loaded via `wc_load_asset()`. Keep the rest of `files.c` intact.

Approach B is probably less work since the filesystem code is complex and well-tested.

### 2. Static game module linking

The engine expects to `dlopen()` game modules or load QVM bytecode. For WASM we need to link everything statically. The Emscripten port (jdarpinian/ioq3) has already solved this — reference their approach for `VM_Create` changes.

Key files: `code/qcommon/vm.c` (VM_Create), `code/client/cl_cgame.c`, `code/server/sv_game.c`, `code/client/cl_ui.c`.

### 3. Sys_Milliseconds and timing

The engine uses `Sys_Milliseconds()` (usually `clock_gettime` or `gettimeofday`) for frame timing. Replace with wasmcart's `wc_time.time_ms`.

### 4. Console/cvar system

The engine's console command system expects keyboard input for typing commands. With gamepad-only input, we need to ensure the game starts directly into the menu without needing console interaction. Set initial cvars via `Com_Init()` argument string.

### 5. Asset size

~34 MB of assets is significant but manageable. For a smaller initial test, use only `pak0.pk3` (14.6 MB) which has enough content for single-player with bots on several maps.

## Implementation Order

1. **Clone and explore** — Clone ioquake3, build natively to understand the codebase
2. **Study jdarpinian/ioq3** — The Emscripten fork has already solved static linking, GLES detection, and frame-based main loop
3. **Create wc_glimp.c** — GL function pointer assignment (most mechanical, tests GL path)
4. **Create wc_main.c** — Entry point with `Com_Init` / `Com_Frame`
5. **Create wc_snd.c** — DMA ring buffer → wasmcart PCM (simplest replacement)
6. **Create wc_gamma.c** — Stub
7. **Create wc_input.c** — Gamepad mapping
8. **Handle filesystem** — Implement pk3 loading via wc_load_asset
9. **Static link game modules** — Compile cgame/game/ui into binary
10. **Build and test** — Initial build targeting menu display
11. **Package and iterate** — Create .wasc, test in retroemu

## Reference: jdarpinian/ioq3 Emscripten Fork

The [jdarpinian/ioq3](https://github.com/jdarpinian/ioq3) fork is the most complete reference:
- Uses `renderergl2` targeting WebGL 2.0
- Has `emscripten_set_main_loop(Com_Frame)` for frame-based rendering
- Compiles game modules statically (no dlopen on Emscripten)
- Added Web Gamepad API support
- Uses Emscripten virtual filesystem for assets

We can lift the VM static linking changes directly. The main difference is we replace Emscripten's filesystem/audio/input with wasmcart's native APIs.

## Estimated Binary Size

| Component | Estimate |
|-----------|----------|
| Engine core (client, qcommon, renderer) | ~2 MB |
| renderergl2 (shaders, VBO, FBO, image) | ~500 KB |
| Game logic (cgame + game + ui + botlib) | ~1.5 MB |
| libjpeg (bundled) | ~100 KB |
| libogg + libvorbis (bundled) | ~200 KB |
| zlib (if keeping pk3 loading) | ~50 KB |
| **Total .wasm** | **~4-5 MB** |
| **Total .wasc** (with full assets) | **~35-40 MB** |
| **Total .wasc** (pak0 only) | **~18-20 MB** |
