# Emscripten Exception Handling for wasmcart

## The Rule

All Emscripten-compiled carts that use C++ exceptions or setjmp/longjmp MUST use these flags at **both compile and link time**:

```bash
-fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0 -sSUPPORT_LONGJMP=wasm
```

## Why

Emscripten has two exception instruction formats:
- **Legacy** (`try`/`catch`/`catch_all`, opcode 0x06): Works in V8 (browsers, Node.js) but NOT in wasmtime, wasmer, or any standalone WASM runtime
- **Standard** (`try_table`/`throw`/`throw_ref`): Works in wasmtime AND V8 (with `--experimental-wasm-exnref` in Node.js)

wasmcart carts must use standard instructions so the same `.wasc` runs on all hosts.

## Flags Explained

| Flag | What it does |
|------|-------------|
| `-fwasm-exceptions` | Tell clang to emit standard WASM EH instructions (not JS-based) |
| `-sWASM_LEGACY_EXCEPTIONS=0` | Tell Emscripten runtime libs to use standard format |
| `-sSUPPORT_LONGJMP=wasm` | Implement setjmp/longjmp via WASM exceptions (needed by ioquake3, GZDoom, etc.) |

## First-Time Setup

The first build with these flags will trigger Emscripten to rebuild its cached runtime libraries with the new exception format. This is normal and only happens once:

```
cache:INFO: generating system library: libc++-wasmexcept.a...
cache:INFO: generating system library: libc++abi-wasmexcept.a...
```

If you get legacy exceptions in the output despite using the flags, clear the cache:
```bash
emcc --clear-cache
```

## Node.js Compatibility

Node.js needs the `--experimental-wasm-exnref` flag to run carts with standard exception instructions:

```bash
node --experimental-wasm-exnref your-script.js
```

This flag will become default in a future Node.js version as the WASM spec standardizes.

## Carts Without Exceptions

Pure C carts that don't use setjmp/longjmp or C++ exceptions don't need any of these flags. Most simple wasmcart examples (hello, snake, breakout, etc.) have no exception instructions and work everywhere without changes.

## Verified

- **OpenArena3** (ioquake3): 0 legacy, 8 standard try_table — works in wasmtime + Node.js
- **GZDoom2**: 0 legacy, 12331 standard try_table — compiles in wasmtime

## Emscripten Version

Requires Emscripten 5.0+ (has `WASM_LEGACY_EXCEPTIONS` setting).
