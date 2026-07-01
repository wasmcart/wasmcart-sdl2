# Full Games for Wasmcart GL Port

Complete, fully-free open-source games (engine AND assets) to port to wasmcart's GL ABI (OpenGL ES 3.0). All games listed here have freely redistributable assets and working or proven-feasible WASM compilation paths.

## Overview

| # | Game | Genre | Language | Assets | Assets License | WASM Exists? | GL API |
|---|------|-------|----------|--------|----------------|--------------|--------|
| 1 | OpenArena | Arena FPS | C | 256 MB | GPLv2 | YES (openarena.live) | GL1/GL2 shader |
| 2 | Freedoom + PrBoom | FPS (Doom) | C | 27 MB | BSD-3 | YES (Dwasm, webprboom) | Software + GL |
| 3 | Neverball/Neverputt | 3D puzzle / minigolf | C | 52 MB | GPLv2+ | YES | GL + gl4es |
| 4 | SuperTuxKart | Kart racer | C++ | 700 MB | GPLv3 + CC | YES (experimental) | GL3.3 / GLES |
| 5 | Endless Sky | Space trading/combat | C++ | 300 MB | GPLv3 + CC/PD | YES (endless-web) | GL3.0 |
| 6 | Teeworlds/DDNet | 2D multiplayer platformer | C/C++ | 50 MB | zlib + CC-BY-SA | YES (DDNet WASM) | GL (2D) |
| 7 | Chromium B.S.U. | Vertical scrolling shooter | C++ | 1.7 MB | Artistic + MIT | YES (midzer port) | GL1.x fixed-func |
| 8 | Extreme Tux Racer | Downhill racing | C++ | 50 MB | GPLv2 | **DONE** (wasmcart) | GL1.x via gl4es |

---

## 1. OpenArena — DONE

**Full free Quake III Arena replacement. Pure C, ioquake3 engine. Fully ported to wasmcart.**

- **Source:** ioquake3 from Ubuntu apt package (`/tmp/ioq3_apt/`, commit 526edd3)
- **Port:** `wasmcart/examples/openarena2/`
- **Build:** 1.9 MB .wasm, 391 MB .wasc (9 pk3 asset files)
- **Engine:** ioquake3 (id Tech 3), `renderergl2` with GLES 3.0 path
- **Language:** C (pure C engine, QVM gamecode compiled statically)
- **GL API:** `renderergl2` — GLSL shaders, VBOs, FBOs, built-in GLES2/3 path
- **License (engine):** GPLv2
- **License (assets):** GPLv2 — ALL assets are GPL, fully redistributable
- **Asset size:** ~391 MB (pk3 files loaded via wc_load_asset)
- **Gamepad:** Dual-stick FPS controls (left=move, right=look, triggers=fire)
- **Audio:** DMA mixer (S16 stereo ring buffer) mapped directly to wasmcart PCM
- **Game content:** Full arena FPS — bots, 50+ maps, all weapons, DM/CTF/tournament modes

### Porting approach (what actually worked)

Uses unmodified upstream ioquake3 as the engine base. `renderergl2` has a built-in GLES
path — we set `qglesMajorVersion = 3` and it handles texture formats, depth calls,
extension stubs automatically. **Zero engine source modifications required.**

**8 platform files** (replacing SDL/sys layer):
- `wc_main.c` — wasmcart entry point, Sys_* stubs, timing, file listing
- `wc_glimp.c` — GL init: assigns `qgl*` pointers to wasmcart GL imports
- `wc_input.c` — gamepad → Q3 event queue (dual-stick FPS mapping)
- `wc_snd.c` — DMA sound → wasmcart PCM ring buffer
- `wc_gamma.c` — stub (no hardware gamma)
- `wc_fs.c` — filesystem adapter (pk3 contents via wc_load_asset, in-memory minizip)
- `wc_vm.c` — static VM dispatch (cgame/game/ui compiled into binary)
- `wc_stubs.c` — miscellaneous stubs

**0 engine source changes.** The upstream GLES 3.0 path works as-is. Two GLSL shader
patches are applied via `glsl_patches/` (specular clamp, simplified tonemap) but no
C source files are modified.

### Key technical insight: no engine FBOs

The upstream GLES path in `tr_extensions.c` does NOT enable the engine's internal FBO
pipeline (`glRefConfig.framebufferObject` stays false). This is critical for compatibility
with retroemu's host FBO redirect:

- **With engine FBOs (broken)**: Engine creates render FBOs, renders 3D to them, then
  blits to FB 0. The host's FBO redirect intercepts FB 0 → host FBO. The engine's
  internal rendering to its own FBOs silently fails — 3D scene is black while
  menus/HUD still work (they're drawn after the blit).

- **Without engine FBOs (working)**: All rendering goes directly to FB 0. The host's
  FBO redirect captures everything cleanly. No conflict.

Previous builds forced FBOs on via 6 engine patches (`framebufferObject=qtrue`,
`framebufferBlit=qtrue`, Reinhard lightmaps, fog disable, format fixes, tonemap shader).
Removing all of that in favor of the upstream GLES path eliminated the FBO conflict and
all engine patches simultaneously.

### Key rendering details

- `r_hdr 0` — disables engine FBO pipeline (essential for host FBO redirect compat)
- `r_normalMapping 0` + `r_specularMapping 0` — forces `USE_FAST_LIGHT` shader path,
  avoids `lightColor /= max(surfNL, 0.25)` which amplifies by 4x and causes white walls
  without hardware gamma
- Q3's rendering: 99.9% of 3D goes through `glDrawElements(GL_TRIANGLES)`
- `renderergl2` avoids all fixed-function: no `glBegin`/`glEnd`, no texture combiners
- VM system: cgame/game/ui compiled statically into WASM binary (no QVM interpreter)
- pk3 files kept as-is in .wasc, loaded into memory, accessed via minizip in-memory ioapi
- ~310K lines C total (engine ~64K, renderer ~52K, game ~50K, client ~42K, bot AI ~40K)

---

## 2. Freedoom + PrBoom

**Complete free Doom replacement. Two full campaigns. Tiny assets.**

- **Source (assets):** https://github.com/freedoom/freedoom
- **Source (engine):** https://github.com/GMH-Code/Dwasm (PrBoom+ WASM) / https://github.com/raz0red/webprboom
- **Engine:** PrBoom+ / PrBoomX (id Tech 1 derivative)
- **Language:** C
- **GL API:** Software renderer primary, OpenGL optional. Dwasm targets WebGL.
- **License (engine):** GPLv2
- **License (assets):** BSD-3-Clause — freely redistributable, very permissive
- **Asset size:** ~27 MB per WAD (Freedoom2.wad)
- **Gamepad:** Yes via Emscripten Gamepad API in browser ports
- **WASM port:** YES — multiple working:
  - Dwasm: https://github.com/GMH-Code/Dwasm
  - WebPrBoom: https://github.com/raz0red/webprboom
  - webrcade-app-prboom: https://github.com/webrcade/webrcade-app-prboom
- **Game content:** Two complete campaigns (Freedoom Phase 1 = Doom 1 replacement, Phase 2 = Doom 2 replacement). Dozens of levels, all original monsters/weapons/textures.
- **Audio:** Custom mixing (PCM output from MUS/MIDI + SFX)

### Porting approach
PrBoom uses software rendering — convert framebuffer output to wasmcart's XRGB8888 framebuffer, or use PrBoom's GL renderer. WAD file loaded via `wc_load_asset` from .wasc archive. The simplest of all ports — Doom engine is the most-ported engine in history. Already have a doomgeneric port in wasmcart.

### Key technical details
- Can target either software rendering (framebuffer mode, like existing doom port) or GL rendering
- BSD-licensed assets mean no GPL copyleft concerns for distribution
- 27 MB WAD compresses well in .wasc ZIP archive
- Complete game with secret levels, boss fights, all weapons

---

## 3. Neverball / Neverputt

**3D ball-rolling puzzle game (like Super Monkey Ball) + miniature golf. 150+ levels.**

- **Source:** https://github.com/Neverball/neverball
- **Engine:** Custom C engine
- **Language:** C
- **GL API:** OpenGL (fixed-function GL1.x + some VBO usage). Web port uses gl4es for GLES translation.
- **License (engine):** GPLv2+
- **License (assets):** GPLv2+ — code AND media under GPL
- **Asset size:** ~52 MB total
- **Gamepad:** Tilt-based controls map well to analog sticks
- **WASM port:** YES — https://github.com/AtiLion/neverball-wasm-port (also enderandrew/neverball-emscripten)
- **Game content:** Neverball: 150+ levels across multiple level sets. Neverputt: 18-hole miniature golf courses. Both are complete, polished games.
- **Audio:** SDL_mixer (OGG music + WAV effects)

### Porting approach
Uses SDL + OpenGL — standard Emscripten target. Fixed-function GL needs gl4es or LEGACY_GL_EMULATION, or renderer rewrite to GLES3. The game's tilt mechanic maps perfectly to gamepad analog sticks. SOL level format loaded from assets.

### Key technical details
- Pure C, clean codebase
- 52 MB total is very manageable
- Ball physics + maze tilting = unique and fun gameplay
- Existing WASM port proves full feasibility
- Level editor exists — user-generated content possible

---

## 4. SuperTuxKart

**Full kart racing game (like Mario Kart). Story mode, 30+ tracks, online multiplayer.**

- **Source:** https://github.com/supertuxkart/stk-code
- **Engine:** Antarctica (custom)
- **Language:** C++
- **GL API:** OpenGL 3.3+ / OpenGL ES (has mobile GLES path)
- **License (engine):** GPLv3
- **License (assets):** Various open licenses (CC-BY-SA, etc.) — all freely redistributable
- **Asset size:** ~700 MB (stk-assets repo)
- **Gamepad:** YES — excellent gamepad support via SDL2, hotplugging, analog controls
- **WASM port:** YES (experimental) — https://supertuxkart.pages.dev/ by ading2210
  - Uses GLES2 mode, ~120 MB initial download, ~500 MB memory
- **Game content:** 30+ tracks, 20+ kart characters, story mode, battle mode, online multiplayer, items/powerups
- **Audio:** Custom engine with OpenAL

### Porting approach
Already has GLES path for mobile. C++ heavy but Emscripten compatible. Main challenge is 700 MB asset size — would need selective asset loading or reduced asset quality for .wasc distribution. GLES2 path maps to wasmcart GL ABI.

### Key technical details
- GLES2 mobile rendering path already exists and is maintained
- C++ with STL, Bullet physics, scripting — complex but modular
- 700 MB assets is large but individual tracks could be packaged separately
- One of the best free games — genuinely fun Mario Kart alternative
- Story mode with unlockable content

---

## 5. Endless Sky

**2D space trading/combat/exploration game. Hundreds of hours of content.**

- **Source:** https://github.com/endless-sky/endless-sky
- **Engine:** Custom C++ engine
- **Language:** C++
- **GL API:** OpenGL 3.0+
- **License (engine):** GPLv3
- **License (assets):** Public domain and Creative Commons — all freely redistributable
- **Asset size:** ~120-360 MB (depending on version)
- **Gamepad:** Not natively, easy to add
- **WASM port:** YES — https://play-endless-web.com/ (source: https://github.com/thomasballinger/endless-web)
- **Game content:** Open world, multiple story campaigns, hundreds of ships, full economy, factions, exploration. One of the best free games ever made.
- **Audio:** Custom (music + SFX)

### Porting approach
Has working browser port. C++ with OpenGL 3.0 — compatible with GLES3/wasmcart GL ABI. 2D rendering (sprites/particles in GL) is simpler than full 3D. Asset loading for ship sprites, backgrounds, etc. via .wasc archive.

### Key technical details
- 2D game with GL3 rendering — simpler GL surface than 3D games
- Extremely deep content (hundreds of hours)
- Asset licenses are very permissive (PD + CC)
- Working browser port validates WASM feasibility
- Data-driven design — ships, missions, etc. defined in text files

---

## 6. Teeworlds / DDraceNetwork (DDNet)

**2D retro multiplayer platformer/shooter. Tight physics, fast gameplay.**

- **Source:** https://github.com/teeworlds/teeworlds / https://github.com/ddnet/ddnet
- **Engine:** Custom
- **Language:** C/C++
- **GL API:** OpenGL (2D rendering)
- **License (engine):** zlib-like (Teeworlds), similar for DDNet
- **License (assets):** CC-BY-SA 3.0 (since 0.6.x) — freely redistributable
- **Asset size:** < 50 MB
- **Gamepad:** Basic, easy to map
- **WASM port:** YES — DDNet has official Emscripten build support. teewebs.net existed as browser version.
- **Game content:** Arena gameplay with grapple hook, weapons, physics puzzles (DDNet). Thousands of community maps.
- **Audio:** Custom mixer

### Porting approach
Small codebase, 2D GL rendering, DDNet already has Emscripten infrastructure. WebSocket networking for multiplayer. Simple GL surface — 2D quads with textures.

### Key technical details
- DDNet is the more active fork with massive community
- 2D rendering = simple GL surface
- Tight platformer physics with grapple hook — unique and fun
- Small assets, fast to load
- Multiplayer-focused but has single-player race/puzzle maps

---

## 7. Chromium B.S.U.

**Vertical scrolling space shooter. Classic arcade gameplay. Tiny footprint.**

- **Source:** https://sourceforge.net/p/chromium-bsu/code/ci/master/tree/
  - GitHub mirror: https://github.com/macton/chromium-bsu
  - Emscripten port: https://github.com/midzer/chromium-bsu
- **Engine:** Custom
- **Language:** C++ (simple, year-2000 style — no templates, no STL in game logic)
- **GL API:** OpenGL 1.x fixed-function only. `glBegin`/`glEnd`, `glVertex3f`, matrix stack, `gluPerspective`. Zero shaders.
- **License (engine):** Clarified Artistic License (OSI-approved)
- **License (assets):** MIT (sounds by Brian Redfern), Clarified Artistic (textures by Mark Allan) — all freely redistributable
- **Asset size:** 1.7 MB total (712 KB textures, 880 KB sounds)
- **Gamepad:** YES — SDL joystick API (`SDL_JoystickGetAxis` for movement, buttons for fire)
- **WASM port:** YES — https://midzer.de/wasm/chromium-bsu/ (uses `LEGACY_GL_EMULATION`)
  - Source: https://github.com/midzer/chromium-bsu
  - ~3 MB total download
- **Game content:** Endless scrolling shooter with enemy waves, powerups, boss patterns, multiple weapon types, difficulty scaling
- **Audio:** OpenAL + ALUT (primary) or SDL_mixer (alternative). 9 WAV files.

### Porting approach
Two options:
1. **Emscripten + LEGACY_GL_EMULATION** — already proven by midzer port. Quick but uses GL emulation layer.
2. **Rewrite renderer to GLES3** — the game only draws textured quads (`drawQuad()` helper). ~16K lines of real code. Simple enough to convert all `glBegin`/`glEnd` to VBO-based rendering.

Approach 2 is preferred for wasmcart since we have native GLES3. The game's rendering is trivial — it's all textured quads in 3D perspective (30° FOV). Replace `gluPerspective` with `mat4_perspective`, batch quads into a VBO, single shader program.

### Key technical details
- 16,700 lines of actual game code (rest is auto-generated text geometry)
- 1.7 MB total assets — smallest game on this list by far
- Rendering is 100% textured quads — perfect for a simple GLES3 batch renderer
- 800x600 default resolution, configurable
- All game state is single-threaded, frame-based — perfect for wasmcart's `wc_render()` callback
- Dual audio backend (OpenAL or SDL_mixer) — replace with PCM ring buffer
- 43 .cpp files, 47 .h files — very manageable

---

## 8. Extreme Tux Racer — DONE

**Downhill penguin racing through snowy terrain. 44 courses. Fully ported to wasmcart.**

- **Source:** https://github.com/drodin/extremetuxracer (mobile/GLES fork, unmodified)
- **Port:** `wasmcart/examples/etr/`
- **Build:** 1.1 MB .wasm, 50 MB .wasc (583 assets)
- **Engine:** Custom C++14
- **GL API:** GL1.x fixed-function → gl4es (GLES2) + ptitSeb/GLU (gluSphere, gluPerspective)
- **License:** GPLv2 (engine + assets)
- **Gamepad:** Analog stick steering, d-pad menus, A=select/jump, B=back/brake, X=space, Y=trick
- **Audio:** 10 WAV SFX via wc_pcm_mixer + 10 OGG music tracks via stb_vorbis, time-based mixing
- **Game content:** 44 courses, freeride + events mode, 3 racing music themes, Tux character with skeletal animation

### Porting approach (what actually worked)

Used drodin's mobile fork as the source (it has `#ifdef ANDROID` / `#ifdef MOBILE`
paths we re-use). Zero original files modified — all SFML dependencies replaced via
`-include etr_compat.h` which blocks SFML headers and provides compatible C++ class
stubs in `namespace sf`.

**7 replacement files** (replacing 5 SFML-dependent modules):
- `etr_compat.h` — blocks SFML, provides sf::Music/Sound/Sprite/Text/etc with real implementations
- `winsys_wc.cpp` — CWinsys class, gl4es init, pushGLStates/popGLStates (Y-down ortho)
- `audio_wc.cpp` — CSound/CMusic with stb_vorbis (OGG streaming) + wc_pcm_mixer (WAV SFX)
- `textures_wc.cpp` — stb_image texture loading (no vertical flip — SFML convention)
- `font_wc.cpp` — stb_truetype font rendering, sf::Text/Sprite/RectangleShape draw ops
- `translation_wc.cpp` — i18n stub (English only)
- `states_wc.cpp` — frame-at-a-time state machine (wc_render calls Run() each frame)
- `etr_cart.cpp` — wc_get_info/wc_init/wc_render entry points

### Key technical challenges solved

1. **SFML C++ class replacement**: Unlike SDL (C functions), SFML uses C++ classes with
   constructors, destructors, and method signatures. The compat header provides full
   `namespace sf` with real implementations backed by stb_truetype, stb_vorbis, and wc_pcm_mixer.

2. **Client-side vertex arrays**: gl4es passes WASM memory offsets as "pointers" to
   glVertexAttribPointer when no VBO is bound. Fix in gl_imports.js: detect unbound VBO,
   copy data to temp VBOs at draw time. (Core wasmcart fix, benefits all gl4es carts.)

3. **Y-down ortho for GUI**: SFML's pushGLStates uses Y-down coordinate system
   (glOrtho(0,W,H,0,-1,1)). Font atlas and sprite rendering must respect this convention.

4. **Texture flip convention**: SFML uses V=0 = top of image. Do NOT flip textures
   (stb_image's top-first output matches SFML). Flipping breaks every texture in the game.

5. **stb_truetype baseline vs SFML top**: stbtt treats Y as baseline (glyphs above),
   SFML setPosition Y = top of bounding box. Fix: add `ascent * scale` to Y.

6. **GLU for character rendering**: Tux's body is ~40 nodes of gluSphere/gluCylinder.
   ptitSeb/GLU compiled to WASM static library provides these.

7. **Time-based audio mixing**: Fixed `rate/60` causes choppy menu music (variable frame
   timing). Fix: use `(uint64_t)host_rate * delta_ms / 1000` for smooth audio at any framerate.

---

## Porting Priority

Based on wasmcart GL ABI compatibility, asset size, code complexity, and game quality:

### Phase 1 — Easiest wins (C, small assets, WASM proven)
1. **Chromium B.S.U.** — 1.7 MB, simplest renderer (textured quads), working WASM port
2. **Freedoom + PrBoom** — 27 MB, already have doom port infrastructure in wasmcart

### Phase 2 — Medium effort (C, moderate assets, WASM proven)
3. ~~**Neverball**~~ — **DONE** (795 KB .wasm, 16 MB .wasc, gl4es, 785 assets)
4. ~~**OpenArena**~~ — **DONE** (1.9 MB .wasm, 391 MB .wasc, renderergl2 native GLES3, 0 engine patches)

### Phase 3 — Larger effort (C++, bigger assets, renderer work needed)
5. ~~**Extreme Tux Racer**~~ — **DONE** (1.1 MB .wasm, 50 MB .wasc, gl4es + GLU, 0 files modified)
6. **Teeworlds/DDNet** — 50 MB, C/C++ mix, 2D GL, WASM infrastructure exists
7. **Endless Sky** — 300 MB, C++, GL3 compatible, working browser port

### Phase 4 — Ambitious (C++, large assets)
8. **SuperTuxKart** — 700 MB, C++, has GLES path, biggest/best game on list
