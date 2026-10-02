#!/bin/bash
#
# build_sdl2_wc.sh — Build SDL2 + SDL_ttf with wasmcart backends
#
# Produces: lib/libSDL2_wc.a, lib/libSDL2_ttf_wc.a
#

set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

# Emscripten SDK. Point EMSDK at your emsdk checkout, or have emsdk_env.sh
# already sourced in your shell (which exports EMSDK for us).
if [ -z "$EMSDK" ]; then
    echo "ERROR: set EMSDK to your emsdk checkout, e.g." >&2
    echo "  EMSDK=/path/to/emsdk $0" >&2
    echo "(or source /path/to/emsdk/emsdk_env.sh first)" >&2
    exit 1
fi
EMSDK_ROOT="$(cd "$EMSDK" && pwd)"
source "$EMSDK_ROOT/emsdk_env.sh" 2>/dev/null || true

# SDL2 source, from Emscripten's port cache.
#
# GLOBBED, not hardcoded: each emsdk release vendors a different SDL. Pinning a
# version here means anyone on a different emsdk gets "SDL2 source not found",
# which reads like a broken cache rather than a version mismatch. (It did exactly
# that: this script wanted SDL-release-2.32.8 while emsdk 4.0.3 ships 2.30.9.)
SDL2_PORTS="$EMSDK_ROOT/upstream/emscripten/cache/ports/sdl2"
SDL2_SRC="$(ls -d "$SDL2_PORTS"/SDL-release-*/ 2>/dev/null | head -1)"
SDL2_SRC="${SDL2_SRC%/}"

if [ -z "$SDL2_SRC" ]; then
    echo "SDL2 source not found. Populating Emscripten's port cache..."
    # embuilder is emscripten's own command for this and reports its errors,
    # unlike a throwaway compile with stderr redirected away.
    embuilder build sdl2 || true
    SDL2_SRC="$(ls -d "$SDL2_PORTS"/SDL-release-*/ 2>/dev/null | head -1)"
    SDL2_SRC="${SDL2_SRC%/}"
fi

if [ -z "$SDL2_SRC" ]; then
    echo "ERROR: no SDL-release-* directory under $SDL2_PORTS" >&2
    echo "       Contents:" >&2
    ls -A "$SDL2_PORTS" 2>/dev/null | sed 's/^/         /' >&2 || echo "         (missing)" >&2
    echo "       Try: embuilder build sdl2" >&2
    exit 1
fi

echo "SDL2 source: $SDL2_SRC"

# Output directories
mkdir -p lib obj/sdl2

# ============================================================================
# Build SDL2 with wasmcart backends
# ============================================================================

# Core SDL2 sources (from Emscripten's port script, minus emscripten-specific files)
SDL2_CORE_SRCS="
SDL.c SDL_assert.c SDL_dataqueue.c SDL_error.c SDL_guid.c SDL_hints.c SDL_list.c SDL_log.c
SDL_utils.c
atomic/SDL_atomic.c atomic/SDL_spinlock.c
audio/SDL_audio.c audio/SDL_audiocvt.c audio/SDL_audiodev.c audio/SDL_audiotypecvt.c
audio/SDL_mixer.c audio/SDL_wave.c
cpuinfo/SDL_cpuinfo.c
dynapi/SDL_dynapi.c
events/SDL_clipboardevents.c events/SDL_displayevents.c events/SDL_dropevents.c
events/SDL_events.c events/SDL_gesture.c events/SDL_keyboard.c events/SDL_keysym_to_scancode.c
events/SDL_scancode_tables.c events/SDL_mouse.c events/SDL_quit.c
events/SDL_touch.c events/SDL_windowevents.c
file/SDL_rwops.c
haptic/SDL_haptic.c
joystick/controller_type.c joystick/SDL_gamecontroller.c joystick/SDL_joystick.c
joystick/SDL_steam_virtual_gamepad.c
power/SDL_power.c
render/SDL_d3dmath.c render/SDL_render.c render/SDL_yuv_sw.c
render/direct3d/SDL_render_d3d.c render/direct3d11/SDL_render_d3d11.c
render/opengl/SDL_render_gl.c render/opengl/SDL_shaders_gl.c
render/opengles/SDL_render_gles.c
render/opengles2/SDL_render_gles2.c render/opengles2/SDL_shaders_gles2.c
render/psp/SDL_render_psp.c
render/software/SDL_blendfillrect.c render/software/SDL_blendline.c
render/software/SDL_blendpoint.c render/software/SDL_drawline.c render/software/SDL_drawpoint.c
render/software/SDL_render_sw.c render/software/SDL_rotate.c render/software/SDL_triangle.c
sensor/SDL_sensor.c sensor/dummy/SDL_dummysensor.c
stdlib/SDL_crc16.c stdlib/SDL_crc32.c stdlib/SDL_getenv.c stdlib/SDL_iconv.c stdlib/SDL_malloc.c
stdlib/SDL_qsort.c stdlib/SDL_stdlib.c stdlib/SDL_string.c stdlib/SDL_strtokr.c
thread/SDL_thread.c
thread/generic/SDL_syscond.c thread/generic/SDL_sysmutex.c thread/generic/SDL_syssem.c
thread/generic/SDL_systhread.c thread/generic/SDL_systls.c
timer/SDL_timer.c timer/unix/SDL_systimer.c
video/SDL_RLEaccel.c video/SDL_blit.c video/SDL_blit_0.c video/SDL_blit_1.c video/SDL_blit_A.c
video/SDL_blit_N.c video/SDL_blit_auto.c video/SDL_blit_copy.c video/SDL_blit_slow.c
video/SDL_bmp.c video/SDL_clipboard.c video/SDL_egl.c video/SDL_fillrect.c video/SDL_pixels.c
video/SDL_rect.c video/SDL_shape.c video/SDL_stretch.c video/SDL_surface.c video/SDL_video.c
video/SDL_yuv.c video/yuv2rgb/yuv_rgb_std.c
video/dummy/SDL_nullevents.c video/dummy/SDL_nullframebuffer.c video/dummy/SDL_nullvideo.c
audio/dummy/SDL_dummyaudio.c
loadso/dlopen/SDL_sysloadso.c
haptic/dummy/SDL_syshaptic.c
main/dummy/SDL_dummy_main.c
locale/SDL_locale.c
misc/SDL_url.c
joystick/dummy/SDL_sysjoystick.c
"

# Note: we intentionally EXCLUDE:
#   video/emscripten/* (replaced by our wasmcart video backend)
#   audio/emscripten/* (replaced by our wasmcart audio backend)
#   joystick/emscripten/* (replaced by our wasmcart joystick backend)
#   power/emscripten/* (not needed)
#   filesystem/emscripten/* (game has its own FS)
#   locale/emscripten/* (not needed)
#   misc/emscripten/* (not needed)
#   audio/disk/* (not needed)

CFLAGS="-O2 -sUSE_SDL=0 -fwrapv-pointer \
  -I$SDL2_SRC/include -I$SDL2_SRC/src \
  -include $HERE/SDL_config_wasmcart.h"

# ============================================================================
# Patch SDL_video.c and SDL_audio.c to register wasmcart backends
# ============================================================================
echo "Patching SDL2 for wasmcart backend registration..."

# We patch IN the SDL2 source tree (temporary, reverted after compile)
SDL_VIDEO_C="$SDL2_SRC/src/video/SDL_video.c"
SDL_AUDIO_C="$SDL2_SRC/src/audio/SDL_audio.c"
SDL_JOYSTICK_C="$SDL2_SRC/src/joystick/SDL_joystick.c"

# Backup originals
cp "$SDL_VIDEO_C" "$SDL_VIDEO_C.orig"
cp "$SDL_AUDIO_C" "$SDL_AUDIO_C.orig"
cp "$SDL_JOYSTICK_C" "$SDL_JOYSTICK_C.orig"

# Patch SDL_video.c: add wasmcart extern + bootstrap entry
sed -i '/extern VideoBootStrap Emscripten_bootstrap;/a extern VideoBootStrap WASMCART_bootstrap;' "$SDL_VIDEO_C"
sed -i '/#ifdef SDL_VIDEO_DRIVER_DUMMY/i #ifdef SDL_VIDEO_DRIVER_WASMCART\n    \&WASMCART_bootstrap,\n#endif' "$SDL_VIDEO_C"

# Patch SDL_audio.c: add wasmcart extern + bootstrap entry
sed -i '/extern AudioBootStrap EMSCRIPTENAUDIO_bootstrap;/a extern AudioBootStrap WASMCARTAUDIO_bootstrap;' "$SDL_AUDIO_C"
sed -i '/#ifdef SDL_AUDIO_DRIVER_DUMMY/i #ifdef SDL_AUDIO_DRIVER_WASMCART\n    \&WASMCARTAUDIO_bootstrap,\n#endif' "$SDL_AUDIO_C"

# Patch SDL_joystick.c: put our driver in the driver table.
#
# It goes FIRST rather than beside the dummy entry. The table is scanned in
# order and the dummy driver is the "no joysticks exist" fallback -- behind it,
# ours would still be reached, but the ordering would say the fallback is the
# real backend. With SDL_JOYSTICK_WASMCART defined and SDL_JOYSTICK_DISABLED
# not, the dummy entry compiles out entirely and this is the only driver.
sed -i '/^static SDL_JoystickDriver \*SDL_joystick_drivers\[\] = {/a #ifdef SDL_JOYSTICK_WASMCART\n    \&SDL_WASMCART_JoystickDriver,\n#endif' "$SDL_JOYSTICK_C"

# Also need to patch SDL_sysvideo.h, SDL_sysaudio.h and SDL_sysjoystick.h for
# extern declarations
SDL_SYSVIDEO_H="$SDL2_SRC/src/video/SDL_sysvideo.h"
SDL_SYSAUDIO_H="$SDL2_SRC/src/audio/SDL_sysaudio.h"
SDL_SYSJOYSTICK_H="$SDL2_SRC/src/joystick/SDL_sysjoystick.h"
cp "$SDL_SYSVIDEO_H" "$SDL_SYSVIDEO_H.orig"
cp "$SDL_SYSAUDIO_H" "$SDL_SYSAUDIO_H.orig"
cp "$SDL_SYSJOYSTICK_H" "$SDL_SYSJOYSTICK_H.orig"
sed -i '/extern VideoBootStrap Emscripten_bootstrap;/a extern VideoBootStrap WASMCART_bootstrap;' "$SDL_SYSVIDEO_H"
sed -i '/extern AudioBootStrap EMSCRIPTENAUDIO_bootstrap;/a extern AudioBootStrap WASMCARTAUDIO_bootstrap;' "$SDL_SYSAUDIO_H"
sed -i '/extern SDL_JoystickDriver SDL_EMSCRIPTEN_JoystickDriver;/a extern SDL_JoystickDriver SDL_WASMCART_JoystickDriver;' "$SDL_SYSJOYSTICK_H"

echo "Compiling SDL2 core files..."

for src in $SDL2_CORE_SRCS; do
    src_path="$SDL2_SRC/src/$src"
    if [ ! -f "$src_path" ]; then
        echo "  SKIP (not found): $src"
        continue
    fi
    obj_name="$(echo "$src" | tr '/' '_' | sed 's/\.c$/.o/')"
    emcc $CFLAGS -c "$src_path" -o "obj/sdl2/$obj_name" 2>/dev/null || \
        echo "  WARN: failed to compile $src"
done

# Restore original SDL2 sources
mv "$SDL_VIDEO_C.orig" "$SDL_VIDEO_C"
mv "$SDL_AUDIO_C.orig" "$SDL_AUDIO_C"
mv "$SDL_JOYSTICK_C.orig" "$SDL_JOYSTICK_C"
mv "$SDL_SYSVIDEO_H.orig" "$SDL_SYSVIDEO_H"
mv "$SDL_SYSAUDIO_H.orig" "$SDL_SYSAUDIO_H"
mv "$SDL_SYSJOYSTICK_H.orig" "$SDL_SYSJOYSTICK_H"

# Compile our wasmcart backends (copy into SDL2 source tree so relative includes work)
echo "Compiling wasmcart video backend..."
mkdir -p "$SDL2_SRC/src/video/wasmcart"
cp "$HERE/SDL_wasmcart_video.c" "$SDL2_SRC/src/video/wasmcart/"
emcc $CFLAGS -c "$SDL2_SRC/src/video/wasmcart/SDL_wasmcart_video.c" -o "obj/sdl2/SDL_wasmcart_video.o"
rm -rf "$SDL2_SRC/src/video/wasmcart"

echo "Compiling wasmcart joystick backend..."
mkdir -p "$SDL2_SRC/src/joystick/wasmcart"
cp "$HERE/SDL_wasmcart_joystick.c" "$SDL2_SRC/src/joystick/wasmcart/"
emcc $CFLAGS -c "$SDL2_SRC/src/joystick/wasmcart/SDL_wasmcart_joystick.c" -o "obj/sdl2/SDL_wasmcart_joystick.o"
rm -rf "$SDL2_SRC/src/joystick/wasmcart"

echo "Compiling wasmcart audio backend..."
mkdir -p "$SDL2_SRC/src/audio/wasmcart"
cp "$HERE/SDL_wasmcart_audio.c" "$SDL2_SRC/src/audio/wasmcart/"
cp "$HERE/SDL_wasmcartaudio.h" "$SDL2_SRC/src/audio/wasmcart/"
emcc $CFLAGS -c "$SDL2_SRC/src/audio/wasmcart/SDL_wasmcart_audio.c" -o "obj/sdl2/SDL_wasmcart_audio.o"
rm -rf "$SDL2_SRC/src/audio/wasmcart"

# Archive
echo "Creating libSDL2_wc.a..."
emar rcs lib/libSDL2_wc.a obj/sdl2/*.o

echo "SDL2 build complete: lib/libSDL2_wc.a ($(wc -c < lib/libSDL2_wc.a) bytes)"

# ============================================================================
# Build SDL_ttf
# ============================================================================

# Trigger SDL_ttf port download if needed
SDL_TTF_SRC="$EMSDK_ROOT/upstream/emscripten/cache/ports/sdl2_ttf"
if [ ! -d "$SDL_TTF_SRC" ]; then
    echo "Triggering SDL_ttf port download..."
    echo '#include <SDL_ttf.h>' > /tmp/_ttf_trigger.c
    emcc -sUSE_SDL=2 -sUSE_SDL_TTF=2 -sUSE_FREETYPE=1 -c /tmp/_ttf_trigger.c -o /dev/null 2>/dev/null || true
    rm -f /tmp/_ttf_trigger.c
fi

# Find SDL_ttf source
TTF_SRC_DIR=""
for d in "$SDL_TTF_SRC"/SDL_ttf-release-*; do
    if [ -f "$d/SDL_ttf.c" ]; then
        TTF_SRC_DIR="$d"
        break
    fi
done

if [ -n "$TTF_SRC_DIR" ]; then
    echo "Building SDL_ttf from $TTF_SRC_DIR..."
    mkdir -p obj/ttf

    emcc -O2 -sUSE_SDL=0 -sUSE_FREETYPE=1 \
        -I"$SDL2_SRC/include" \
        -include "$HERE/SDL_config_wasmcart.h" \
        -c "$TTF_SRC_DIR/SDL_ttf.c" -o obj/ttf/SDL_ttf.o

    emar rcs lib/libSDL2_ttf_wc.a obj/ttf/SDL_ttf.o
    echo "SDL_ttf build complete: lib/libSDL2_ttf_wc.a"
else
    echo "WARNING: SDL_ttf source not found. Font rendering will not work."
fi

echo "=== SDL2 wasmcart build complete ==="
