/*
 * SDL_config_wasmcart.h — SDL2 configuration for wasmcart standalone WASM
 *
 * Based on SDL_config_emscripten.h but replaces Emscripten-specific
 * video/audio/joystick backends with wasmcart backends.
 * No EM_ASM, no browser dependencies.
 */

#ifndef SDL_config_wasmcart_h_
#define SDL_config_wasmcart_h_
#define SDL_config_h_

/* SDL_platform.h is NOT included here to avoid Emscripten's fake SDL headers.
 * Platform detection is not needed for standalone WASM. */

#ifdef __LP64__
#define SIZEOF_VOIDP 8
#else
#define SIZEOF_VOIDP 4
#endif
#define HAVE_GCC_ATOMICS 1

/* Useful headers */
#define STDC_HEADERS 1
#define HAVE_ALLOCA_H 1
#define HAVE_CTYPE_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_MALLOC_H 1
#define HAVE_MATH_H 1
#define HAVE_MEMORY_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_STDARG_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_WCHAR_H 1

/* C library functions */
#define HAVE_DLOPEN 1
#define HAVE_MALLOC 1
#define HAVE_CALLOC 1
#define HAVE_REALLOC 1
#define HAVE_FREE 1
#define HAVE_ALLOCA 1
#define HAVE_GETENV 1
#define HAVE_SETENV 1
#define HAVE_PUTENV 1
#define HAVE_UNSETENV 1
#define HAVE_QSORT 1
#define HAVE_BSEARCH 1
#define HAVE_ABS 1
#define HAVE_BCOPY 1
#define HAVE_MEMSET 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMCMP 1
#define HAVE_WCSLEN 1
#define HAVE_WCSDUP 1
#define HAVE_WCSSTR 1
#define HAVE_WCSCMP 1
#define HAVE_WCSNCMP 1
#define HAVE_WCSCASECMP 1
#define HAVE_WCSNCASECMP 1
#define HAVE_STRLEN 1
#define HAVE_STRLCPY 1
#define HAVE_STRLCAT 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRTOK_R 1
#define HAVE_STRTOL 1
#define HAVE_STRTOUL 1
#define HAVE_STRTOLL 1
#define HAVE_STRTOULL 1
#define HAVE_STRTOD 1
#define HAVE_ATOI 1
#define HAVE_ATOF 1
#define HAVE_STRCMP 1
#define HAVE_STRNCMP 1
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#define HAVE_SSCANF 1
#define HAVE_VSSCANF 1
#define HAVE_VSNPRINTF 1
#define HAVE_M_PI 1
#define HAVE_ACOS 1
#define HAVE_ACOSF 1
#define HAVE_ASIN 1
#define HAVE_ASINF 1
#define HAVE_ATAN 1
#define HAVE_ATANF 1
#define HAVE_ATAN2 1
#define HAVE_ATAN2F 1
#define HAVE_CEIL 1
#define HAVE_CEILF 1
#define HAVE_COPYSIGN 1
#define HAVE_COPYSIGNF 1
#define HAVE_COS 1
#define HAVE_COSF 1
#define HAVE_EXP 1
#define HAVE_EXPF 1
#define HAVE_FABS 1
#define HAVE_FABSF 1
#define HAVE_FLOOR 1
#define HAVE_FLOORF 1
#define HAVE_FMOD 1
#define HAVE_FMODF 1
#define HAVE_LOG 1
#define HAVE_LOGF 1
#define HAVE_LOG10 1
#define HAVE_LOG10F 1
#define HAVE_LROUND 1
#define HAVE_LROUNDF 1
#define HAVE_POW 1
#define HAVE_POWF 1
#define HAVE_ROUND 1
#define HAVE_ROUNDF 1
#define HAVE_SCALBN 1
#define HAVE_SCALBNF 1
#define HAVE_SIN 1
#define HAVE_SINF 1
#define HAVE_SQRT 1
#define HAVE_SQRTF 1
#define HAVE_TAN 1
#define HAVE_TANF 1
#define HAVE_TRUNC 1
#define HAVE_TRUNCF 1
#define HAVE_FSEEKO 1
#define HAVE_FSEEKO64 1
#define HAVE_SIGACTION 1
#define HAVE_SA_SIGACTION 1
#define HAVE_SETJMP 1
#define HAVE_NANOSLEEP 1
#define HAVE_SYSCONF 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_MPROTECT 1
#define HAVE_ICONV 1
#define HAVE_ICONV_H 1

/* SDL internal assertion support */
/* #undef SDL_DEFAULT_ASSERT_LEVEL */

#define SDL_CPUINFO_DISABLED 1
#define SDL_HAPTIC_DISABLED 1
#define SDL_HIDAPI_DISABLED 1
#define SDL_THREADS_DISABLED 1

/* Audio: wasmcart backend (ring buffer output) */
#define SDL_AUDIO_DRIVER_WASMCART 1
#define SDL_AUDIO_DRIVER_DUMMY 1

/* Joystick: disabled (input pushed via PumpEvents from wc_pads) */
#define SDL_JOYSTICK_DISABLED 1

/* Sensor: disabled */
#define SDL_SENSOR_DISABLED 1

/* Shared object loading */
#define SDL_LOADSO_DLOPEN 1

/* Timer: Unix (emscripten provides clock_gettime) */
#define SDL_TIMER_UNIX 1

/* Video: wasmcart backend (GL via gl4es bridge) */
#define SDL_VIDEO_DRIVER_WASMCART 1

/* OpenGL: GLES2 via gl4es */
#define SDL_VIDEO_OPENGL_ES2 1
/* No EGL — gl4es uses NOEGL mode with GetProcAddress */
/* #undef SDL_VIDEO_OPENGL_EGL */

/* Render: software + GLES2 (for SDL_Renderer if needed) */
#define SDL_VIDEO_RENDER_OGL_ES2 1

/* Filesystem: dummy (game uses its own FS layer) */
#define SDL_FILESYSTEM_DUMMY 1

/* Power: disabled */
/* #undef SDL_POWER_EMSCRIPTEN */

#endif /* SDL_config_wasmcart_h_ */
