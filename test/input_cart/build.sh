#!/bin/bash
#
# build.sh - build the input test cart against sdl2_wc/lib/libSDL2_wc.a
#
#   EMSDK=/path/to/emsdk WASMCART=/path/to/wasmcart ./build.sh
#
# WASMCART defaults to `npm install` in this directory, else a sibling checkout
# (../../../wasmcart); either supplies wasmcart.h and wc_cart.h. Output:
# build/cart.wasm.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
if [ -z "$WASMCART" ]; then
    # an npm install here (what CI uses), else a sibling checkout
    if [ -d "$HERE/node_modules/wasmcart" ]; then WASMCART="$HERE/node_modules/wasmcart"
    else WASMCART="$ROOT/../wasmcart"; fi
fi

if [ -z "$EMSDK" ]; then
    echo "ERROR: set EMSDK to your emsdk checkout" >&2
    exit 1
fi
source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1 || true

LIB="$ROOT/sdl2_wc/lib/libSDL2_wc.a"
if [ ! -f "$LIB" ]; then
    echo "ERROR: $LIB missing; run sdl2_wc/build_sdl2_wc.sh first" >&2
    exit 1
fi

mkdir -p "$HERE/build"
emcc -O2 -sUSE_SDL=2 \
  -I"$WASMCART/include" -I"$ROOT/sdl2_wc" \
  "$HERE/input_cart.c" "$ROOT/emstubs.c" "$LIB" \
  -sSTANDALONE_WASM=1 -sERROR_ON_UNDEFINED_SYMBOLS=0 \
  -sALLOW_MEMORY_GROWTH=1 --no-entry \
  -o "$HERE/build/cart.wasm"
cp "$HERE/manifest.json" "$HERE/build/manifest.json"
echo "built $HERE/build/cart.wasm"
