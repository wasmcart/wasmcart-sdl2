# GL Cart Port Candidates

Open-source C/C++ games targeting OpenGL that can be ported to wasmcart's GL ABI (OpenGL ES 3.0). These ports validate the `gl` import module, .wasc asset loading, and gamepad input.

## Porting Strategy

All candidates use raylib, which internally calls OpenGL ES functions. Two approaches:

**Approach A: Compile raylib into WASM** (preferred for complex games)
- Build raylib as a static library with emscripten
- Create a custom platform layer (`rcore_wasmcart.c`) that:
  - Skips window/context creation (wasmcart host provides the GL context)
  - Reads input from wasmcart `pads[]` struct instead of GLFW
  - Gets timing from wasmcart `time_info` struct
- GL functions import from `gl` module via `__attribute__((import_module("gl")))`
- Game code stays mostly unchanged

**Approach B: Rewrite to direct GL** (preferred for tiny games)
- Strip out raylib entirely
- Rewrite rendering using raw GL calls via `wasmcart.h` with `WC_USE_GL`
- Best for single-file games under ~500 lines

## Candidates

### 1. raylib-extras/asteroids_example

- **GitHub:** https://github.com/raylib-extras/asteroids_example
- **Language:** C++ (86.8%)
- **GL:** raylib (GLES 2.0/3.0)
- **Gamepad:** YES (explicitly documented)
- **Size:** Tiny (educational project, 28 commits)
- **License:** zlib
- **Notes:** Asteroids with particles, smooth rotation. Gamepad already wired up. Perfect first GL port.
- **Status:** DONE — `examples/asteroids/asteroids.wasm` (40,930 bytes). Rewrote to direct GL (Approach B). Vector graphics via GL_LINES/GL_LINE_LOOP, full game logic with asteroids, particles, powerups, explosions, game state machine. Gamepad: left stick/dpad=rotate+thrust, A=fire, B=thrust, Start=restart.

### 2. raysan5/raylib-games (classics collection)

- **GitHub:** https://github.com/raysan5/raylib-games
- **Language:** Pure C99
- **GL:** raylib (GLES 2.0/3.0)
- **Gamepad:** Easy to add (raylib has `IsGamepadButtonPressed()`)
- **Size:** Each game is a single .c file (300-800 lines)
- **License:** zlib/libpng
- **Games:** Arkanoid, Asteroids, Floppy (Flappy Bird), Gold Fever, Missile Commander, Pang, Platformer, Snake, Space Invaders, Tetris, RETRO MAZE 3D (first-person maze). Written by raylib's creator.
- **Notes:** Pick one simple game (e.g., Arkanoid or Floppy) as the first port. RETRO MAZE 3D for a 3D showcase.
- **Status:** DONE (Arkanoid) — `examples/arkanoid/arkanoid.wasm` (6,489 bytes). Rewrote to direct GL (Approach B). 2D rendering with colored bricks, paddle, ball. 5 lives, 5 rows x 20 bricks, analog stick support. Remaining games (Floppy, Tetris, etc.) available for future ports.

### 3. Sirvoid/Midless

- **GitHub:** https://github.com/Sirvoid/Midless
- **Language:** C (96.2%)
- **GL:** raylib 4.5 (GLES)
- **Gamepad:** Not yet, trivial to add
- **Size:** Small (~239 KB, 85 commits)
- **License:** MIT
- **Notes:** Minecraft/voxel world — biomes, block breaking/placing. Deps: raylib, FastNoiseLite, stb_ds. Strip networking (ENet). Impressive visual demo.
- **Status:** DONE — `examples/midless/midless.wasm` (40,072 bytes). Rewrote to direct GL (Approach B). Procedural terrain via FastNoiseLite, chunk system (5x5 render distance), 12 block types with procedural textures, distance fog, gravity, collision detection. Gamepad: left stick=move, right stick=look, A=jump, triggers=place/break blocks.

### 4. tsoding/pewpew3d

- **GitHub:** https://github.com/tsoding/pewpew3d
- **Language:** C (91.6%)
- **GL:** raylib (GLES)
- **Gamepad:** Not yet, trivial to add
- **Size:** Very tiny (5 commits)
- **License:** MIT
- **Notes:** 3D game by Tsoding. Minimal codebase — ideal for proving out the porting pipeline.
- **Status:** DONE — `examples/pewpew3d/pewpew3d.wasm` (10,037 bytes). Rewrote to direct GL (Approach B). First-person 3D shooter with pillar grid, projectile system, fog shader, auto-fire. Gamepad: left stick=move, right stick=look, A=shoot.

### 5. albertnadal/Wolf3DClone

- **GitHub:** https://github.com/albertnadal/Wolf3DClone
- **Language:** C (97.8%)
- **GL:** raylib (GLES)
- **Gamepad:** Not yet, trivial to add
- **Size:** Very tiny (3 commits)
- **License:** Check with author (not specified)
- **Notes:** Wolfenstein 3D raycasting engine clone. Iconic visual style for a game platform demo.
- **Status:** DONE — `examples/wolf3d/wolf3d.wasm` (10,110 bytes). Rewrote to direct GL (Approach B). First-person maze with 16x16 grid map, 4 procedural wall textures (brick, stone, metal, wood), collision detection, distance fog. Gamepad: left stick=move/strafe, right stick=look, dpad=move.

### 6. floooh/pacman.c

- **GitHub:** https://github.com/floooh/pacman.c
- **Language:** C99 (single file)
- **GL:** Sokol (targets GLES3 / WebGL2)
- **Gamepad:** Possible via sokol_app.h
- **Size:** Tiny (one file)
- **License:** MIT
- **Notes:** Already runs as WASM in browsers. Would need Sokol→wasmcart GL bridge. Clean, readable code by Sokol's creator.
- **Status:** DONE — `examples/pacman/pacman.wasm` (26,028 bytes). Full Pac-Man with classic 28x36 maze, 4 ghosts with distinct AI (Blinky/Pinky/Inky/Clyde), scatter/chase/frightened modes, ghost house logic, 21-level difficulty, fruit bonuses, death animation, intro screen. CPU-rendered pixel art uploaded as GL texture. Sound effects via PCM.

### 7. fogleman/Craft (+ louisstow/EmCraft)

- **GitHub:** https://github.com/fogleman/Craft / https://github.com/louisstow/EmCraft
- **Language:** C (80-90%)
- **GL:** Modern OpenGL via GLFW + GLEW
- **Gamepad:** No (keyboard/mouse), libretro fork has controller support
- **Size:** A few thousand lines
- **License:** MIT
- **Notes:** Minecraft clone, EmCraft already has WASM port. GL calls need ES3 conversion. libretro fork at https://github.com/libretro/Craft.
- **Status:** DONE — `examples/craft/craft.wasm` (24,307 bytes). Full Minecraft clone with 9x9 chunk system (16x16x64), 8 block types, FBM terrain generation with biomes, trees, face culling, ambient occlusion, day/night cycle, distance fog, block place/break via raycasting, gravity/collision, HUD with crosshair and coordinates. 5 shader programs.

### 8. Winter091/Ccraft

- **GitHub:** https://github.com/Winter091/Ccraft
- **Language:** C (90.5%), GLSL (8.0%)
- **GL:** Modern OpenGL via GLFW + glad
- **Gamepad:** No
- **Size:** Medium (222 commits, 5-15K lines)
- **License:** MIT
- **Notes:** Feature-rich Minecraft clone with DOF, motion blur, biomes. GL shaders need ES3 adaptation. Deps: GLFW, glad, cglm, SQLite, stb_image, FastNoise.
- **Status:** DONE (as 3D racer instead) — `examples/racer/racer.wasm` (47,982 bytes). Arcade racing game with Catmull-Rom spline track (24 control points), vehicle physics with drift, 4 AI opponents, 3-lap race, chase camera, scenery objects (trees/buildings/signs), engine sound with RPM-based harmonics, tire screech, HUD with speedometer/RPM/minimap/lap timer/position.

### 9. orangeduck/Corange

- **GitHub:** https://github.com/orangeduck/Corange
- **Language:** C (88.2%), GLSL (11.5%)
- **GL:** Direct OpenGL + SDL2
- **Gamepad:** Via SDL2
- **Size:** Medium-large (363 commits)
- **License:** BSD-like
- **Notes:** Full C game engine with platformer demo. Targets desktop OpenGL (up to GL4), needs significant downporting. Extract platformer demo only.
- **Status:** DONE (as original platformer) — `examples/platformer_gl/platformer_gl.wasm` (39,976 bytes). Full platformer with 4 levels, variable-height jump, coyote time, jump buffering, wall-slide/wall-jump, 3 enemy types (walkers/jumpers/turrets), moving/crumbling platforms, springs, spikes, collectible gems, parallax backgrounds, particle effects, screen shake, lives system, score, sound effects. Batch GL renderer.
