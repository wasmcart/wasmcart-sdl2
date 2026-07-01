/*
 * invoke_stubs.c — Stubs for Emscripten runtime functions and invoke_* wrappers
 *
 * Pre-built Emscripten port libraries (libpng, FreeType, etc.) use setjmp/longjmp
 * for error handling. Emscripten implements this via invoke_* wrappers
 * that call through the function table with exception handling.
 *
 * In standalone WASM (no JS glue), we provide simple call-through stubs.
 * Function pointers in Emscripten WASM are table indices, so casting
 * and calling works via call_indirect.
 *
 * Also provides setjmp/longjmp stubs and emscripten runtime stubs
 * that would otherwise become unresolvable env.* imports.
 */

#include <stdint.h>
#include <stddef.h>

/* ---- invoke_* stubs ---- */
/* Cover all signatures that pre-built port libraries might need.
 * First arg is always the function table index, rest are forwarded. */

typedef void (*fn_v)(void);
typedef int  (*fn_i)(void);
typedef int  (*fn_ii)(int);
typedef int  (*fn_iii)(int, int);
typedef int  (*fn_iiii)(int, int, int);
typedef int  (*fn_iiiii)(int, int, int, int);
typedef int  (*fn_iiiiii)(int, int, int, int, int);
typedef void (*fn_vi)(int);
typedef void (*fn_vii)(int, int);
typedef void (*fn_viii)(int, int, int);
typedef void (*fn_viiii)(int, int, int, int);
typedef void (*fn_viiiii)(int, int, int, int, int);
typedef void (*fn_viiiiii)(int, int, int, int, int, int);
typedef void (*fn_viiiiiii)(int, int, int, int, int, int, int);
typedef void (*fn_viiiiiiii)(int, int, int, int, int, int, int, int);
typedef void (*fn_viiiiiiiii)(int, int, int, int, int, int, int, int, int);

void invoke_v(int idx) { ((fn_v)idx)(); }
int  invoke_i(int idx) { return ((fn_i)idx)(); }
int  invoke_ii(int idx, int a) { return ((fn_ii)idx)(a); }
int  invoke_iii(int idx, int a, int b) { return ((fn_iii)idx)(a, b); }
int  invoke_iiii(int idx, int a, int b, int c) { return ((fn_iiii)idx)(a, b, c); }
int  invoke_iiiii(int idx, int a, int b, int c, int d) { return ((fn_iiiii)idx)(a, b, c, d); }
int  invoke_iiiiii(int idx, int a, int b, int c, int d, int e) { return ((fn_iiiiii)idx)(a, b, c, d, e); }
void invoke_vi(int idx, int a) { ((fn_vi)idx)(a); }
void invoke_vii(int idx, int a, int b) { ((fn_vii)idx)(a, b); }
void invoke_viii(int idx, int a, int b, int c) { ((fn_viii)idx)(a, b, c); }
void invoke_viiii(int idx, int a, int b, int c, int d) { ((fn_viiii)idx)(a, b, c, d); }
void invoke_viiiii(int idx, int a, int b, int c, int d, int e) { ((fn_viiiii)idx)(a, b, c, d, e); }
void invoke_viiiiii(int idx, int a, int b, int c, int d, int e, int f) { ((fn_viiiiii)idx)(a, b, c, d, e, f); }
void invoke_viiiiiii(int idx, int a, int b, int c, int d, int e, int f, int g) { ((fn_viiiiiii)idx)(a, b, c, d, e, f, g); }
void invoke_viiiiiiii(int idx, int a, int b, int c, int d, int e, int f, int g, int h) { ((fn_viiiiiiii)idx)(a, b, c, d, e, f, g, h); }
void invoke_viiiiiiiii(int idx, int a, int b, int c, int d, int e, int f, int g, int h, int j) { ((fn_viiiiiiiii)idx)(a, b, c, d, e, f, g, h, j); }

/* ---- setjmp/longjmp stubs ---- */
/* setjmp returns 0 (normal path). longjmp should never be called with valid assets. */

int setjmp(void *env)
{
    (void)env;
    return 0;
}

void longjmp(void *env, int val)
{
    (void)env; (void)val;
    __builtin_trap();
}

void _emscripten_throw_longjmp(void)
{
    __builtin_trap();
}

/* ---- Emscripten runtime stubs ---- */

int emscripten_asm_const_int_sync_on_main_thread(const char *code, const char *sig, ...)
{
    return 0;
}

int emscripten_has_asyncify(void)
{
    return 0;
}

void emscripten_sleep(unsigned int ms)
{
    (void)ms;
}

/* Emscripten DOM API stubs — neverball's loop() calls these under __EMSCRIPTEN__ */

int emscripten_get_element_css_size(const char *target, double *width, double *height)
{
    (void)target;
    if (width)  *width  = 0;
    if (height) *height = 0;
    return 0; /* EMSCRIPTEN_RESULT_SUCCESS */
}
