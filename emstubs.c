/*
 * emstubs.c - Stub implementations for emscripten/EGL/GL functions
 *
 * Reusable library for porting emscripten-compiled SDL2 games to wasmcart.
 *
 * PROBLEM:
 *   Emscripten's SDL2 library (libSDL2.a) is monolithic. Even if you only use
 *   SDL_INIT_AUDIO, the linker pulls in video, input, GL, EGL, and emscripten
 *   runtime functions. These are implemented as env:: imports that expect
 *   emscripten's JS glue. Since wasmcart uses STANDALONE_WASM with no JS glue,
 *   these imports would fail at instantiation time.
 *
 * SOLUTION:
 *   These no-op stubs provide definitions for all the functions SDL2 references.
 *   They satisfy the linker and produce zero env:: imports in the final .wasm.
 *   The functions are never called at runtime (we only use SDL for audio, and
 *   the audio device is intercepted by audio_bridge.c via --wrap flags).
 *
 * USAGE:
 *   Just compile this file alongside your cart source:
 *     emcc ... emstubs.c ... -o cart.wasm
 *
 * WHEN YOU NEED THIS:
 *   - Your build uses -s USE_SDL=2 (emscripten's SDL2 port)
 *   - Your build uses -s USE_SDL_MIXER=2 (SDL2_mixer)
 *   - You get linker errors about undefined emscripten_*, egl*, or gl* functions
 *   - You see env:: imports in your .wasm that CartHost rejects
 *
 * WHEN YOU DON'T NEED THIS:
 *   - Pure C games with no SDL dependency
 *   - Games that only use wasmcart.h directly (no emscripten SDL ports)
 */

#include <stdint.h>
#include <stddef.h>

/* ---- Emscripten runtime stubs ---- */

/* emscripten_asm_const_* are variadic in practice (the first arg is
   a pointer to inline JS code, followed by format args). SDL2 uses
   them for its emscripten audio/video/mouse drivers. Since we wrap
   away SDL audio and never use SDL video, these are never invoked. */
int emscripten_asm_const_int_sync_on_main_thread(const char *code, const char *sig, ...) { return 0; }
int emscripten_asm_const_int(const char *code, const char *sig, ...) { return 0; }
void *emscripten_asm_const_ptr_sync_on_main_thread(const char *code, const char *sig, ...) { return NULL; }

int emscripten_sample_gamepad_data(void) { return 0; }
int emscripten_get_num_gamepads(void) { return 0; }
int emscripten_get_gamepad_status(int index, void *state) { return -1; }
int emscripten_set_gamepadconnected_callback_on_thread(void *a, int b, void *c, int d) { return 0; }
int emscripten_set_gamepaddisconnected_callback_on_thread(void *a, int b, void *c, int d) { return 0; }
int emscripten_has_asyncify(void) { return 0; }
void emscripten_sleep(unsigned int ms) { (void)ms; }

void emscripten_get_screen_size(int *w, int *h) { if(w) *w = 320; if(h) *h = 240; }
int emscripten_request_pointerlock(const char *t, int d) { return -1; }
int emscripten_exit_pointerlock(void) { return -1; }
double emscripten_get_device_pixel_ratio(void) { return 1.0; }
int emscripten_set_canvas_element_size(const char *t, int w, int h) { return 0; }
int emscripten_get_element_css_size(const char *t, double *w, double *h) {
    if(w) *w = 320; if(h) *h = 240; return 0;
}
int emscripten_set_element_css_size(const char *t, double w, double h) { return 0; }
void emscripten_set_window_title(const char *t) {}
int emscripten_request_fullscreen_strategy(const char *t, int d, void *s) { return -1; }
int emscripten_exit_fullscreen(void) { return -1; }

/* All the set_*_callback_on_thread stubs */
int emscripten_set_mousemove_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_mousedown_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_mouseup_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_mouseenter_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_mouseleave_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_wheel_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_focus_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_blur_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_touchstart_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_touchend_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_touchmove_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_touchcancel_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_pointerlockchange_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_keydown_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_keyup_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_keypress_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_fullscreenchange_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_resize_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_visibilitychange_callback_on_thread(const char *t, void *u, int b, void *c, int d) { return 0; }
int emscripten_set_beforeunload_callback_on_thread(void *u, int b, void *c) { return 0; }

/* ---- EGL stubs ---- */
int eglGetConfigAttrib(void *d, void *c, int a, int *v) { return 0; }
int eglChooseConfig(void *d, const int *a, void *c, int s, int *n) { return 0; }
int eglTerminate(void *d) { return 0; }
int eglInitialize(void *d, int *ma, int *mi) { return 0; }
void *eglGetDisplay(int n) { return NULL; }
int eglBindAPI(int a) { return 0; }
int eglWaitGL(void) { return 0; }
int eglWaitNative(int e) { return 0; }
int eglSwapInterval(void *d, int i) { return 0; }
int eglSwapBuffers(void *d, void *s) { return 0; }
int eglMakeCurrent(void *d, void *dr, void *rd, void *c) { return 0; }
int eglDestroySurface(void *d, void *s) { return 0; }
void *eglCreateWindowSurface(void *d, void *c, void *w, const int *a) { return NULL; }
int eglDestroyContext(void *d, void *c) { return 0; }
void *eglCreateContext(void *d, void *c, void *s, const int *a) { return NULL; }
int eglGetError(void) { return 0; }
const char *eglQueryString(void *d, int n) { return ""; }

/* ---- GL stubs ---- */
/* SDL2 video subsystem references all these GL functions. They are never
   called because we only use SDL_INIT_AUDIO. */

#define GL_VOID_STUB(name, ...) void emscripten_##name(__VA_ARGS__) {}
#define GL_INT_STUB(name, ...) int emscripten_##name(__VA_ARGS__) { return 0; }

/* Query objects */
GL_VOID_STUB(glGenQueriesEXT, int n, void *ids)
GL_VOID_STUB(glDeleteQueriesEXT, int n, const void *ids)
GL_INT_STUB(glIsQueryEXT, int id)
GL_VOID_STUB(glBeginQueryEXT, int target, int id)
GL_VOID_STUB(glEndQueryEXT, int target)
GL_VOID_STUB(glQueryCounterEXT, int id, int target)
GL_VOID_STUB(glGetQueryivEXT, int target, int pname, void *params)
GL_VOID_STUB(glGetQueryObjectivEXT, int id, int pname, void *params)
GL_VOID_STUB(glGetQueryObjectuivEXT, int id, int pname, void *params)
GL_VOID_STUB(glGetQueryObjecti64vEXT, int id, int pname, void *params)
GL_VOID_STUB(glGetQueryObjectui64vEXT, int id, int pname, void *params)

/* VAO */
GL_VOID_STUB(glBindVertexArrayOES, int a)
GL_VOID_STUB(glDeleteVertexArraysOES, int n, const void *a)
GL_VOID_STUB(glGenVertexArraysOES, int n, void *a)
GL_INT_STUB(glIsVertexArrayOES, int a)

/* Instancing / draw buffers */
GL_VOID_STUB(glDrawBuffersWEBGL, int n, const void *b)
GL_VOID_STUB(glDrawArraysInstancedANGLE, int m, int f, int c, int p)
GL_VOID_STUB(glDrawElementsInstancedANGLE, int m, int c, int t, const void *i, int p)
GL_VOID_STUB(glVertexAttribDivisorANGLE, int i, int d)

/* Core GL */
GL_VOID_STUB(glActiveTexture, int t)
GL_VOID_STUB(glAttachShader, int p, int s)
GL_VOID_STUB(glBindAttribLocation, int p, int i, const char *n)
GL_VOID_STUB(glBindBuffer, int t, int b)
GL_VOID_STUB(glBindFramebuffer, int t, int f)
GL_VOID_STUB(glBindRenderbuffer, int t, int r)
GL_VOID_STUB(glBindTexture, int t, int x)
GL_VOID_STUB(glBlendColor, float r, float g, float b, float a)
GL_VOID_STUB(glBlendEquation, int m)
GL_VOID_STUB(glBlendEquationSeparate, int r, int a)
GL_VOID_STUB(glBlendFunc, int s, int d)
GL_VOID_STUB(glBlendFuncSeparate, int sr, int dr, int sa, int da)
GL_VOID_STUB(glBufferData, int t, int s, const void *d, int u)
GL_VOID_STUB(glBufferSubData, int t, int o, int s, const void *d)
GL_INT_STUB(glCheckFramebufferStatus, int t)
GL_VOID_STUB(glClear, int m)
GL_VOID_STUB(glClearColor, float r, float g, float b, float a)
GL_VOID_STUB(glClearDepthf, float d)
GL_VOID_STUB(glClearStencil, int s)
GL_VOID_STUB(glColorMask, int r, int g, int b, int a)
GL_VOID_STUB(glCompileShader, int s)
GL_VOID_STUB(glCompressedTexImage2D, int t, int l, int f, int w, int h, int b, int s, const void *d)
GL_VOID_STUB(glCompressedTexSubImage2D, int t, int l, int xo, int yo, int w, int h, int f, int s, const void *d)
GL_VOID_STUB(glCopyTexImage2D, int t, int l, int f, int x, int y, int w, int h, int b)
GL_VOID_STUB(glCopyTexSubImage2D, int t, int l, int xo, int yo, int x, int y, int w, int h)
GL_INT_STUB(glCreateProgram, void)
GL_INT_STUB(glCreateShader, int t)
GL_VOID_STUB(glCullFace, int m)
GL_VOID_STUB(glDeleteBuffers, int n, const void *b)
GL_VOID_STUB(glDeleteFramebuffers, int n, const void *f)
GL_VOID_STUB(glDeleteProgram, int p)
GL_VOID_STUB(glDeleteRenderbuffers, int n, const void *r)
GL_VOID_STUB(glDeleteShader, int s)
GL_VOID_STUB(glDeleteTextures, int n, const void *t)
GL_VOID_STUB(glDepthFunc, int f)
GL_VOID_STUB(glDepthMask, int m)
GL_VOID_STUB(glDepthRangef, float n, float f)
GL_VOID_STUB(glDetachShader, int p, int s)
GL_VOID_STUB(glDisable, int c)
GL_VOID_STUB(glDisableVertexAttribArray, int i)
GL_VOID_STUB(glDrawArrays, int m, int f, int c)
GL_VOID_STUB(glDrawElements, int m, int c, int t, const void *i)
GL_VOID_STUB(glEnable, int c)
GL_VOID_STUB(glEnableVertexAttribArray, int i)
GL_VOID_STUB(glFinish, void)
GL_VOID_STUB(glFlush, void)
GL_VOID_STUB(glFramebufferRenderbuffer, int t, int a, int rt, int r)
GL_VOID_STUB(glFramebufferTexture2D, int t, int a, int tt, int tx, int l)
GL_VOID_STUB(glFrontFace, int m)
GL_VOID_STUB(glGenBuffers, int n, void *b)
GL_VOID_STUB(glGenerateMipmap, int t)
GL_VOID_STUB(glGenFramebuffers, int n, void *f)
GL_VOID_STUB(glGenRenderbuffers, int n, void *r)
GL_VOID_STUB(glGenTextures, int n, void *t)
GL_VOID_STUB(glGetActiveAttrib, int p, int i, int bs, void *l, void *sz, void *tp, void *nm)
GL_VOID_STUB(glGetActiveUniform, int p, int i, int bs, void *l, void *sz, void *tp, void *nm)
GL_VOID_STUB(glGetAttachedShaders, int p, int ms, void *c, void *s)
GL_INT_STUB(glGetAttribLocation, int p, const char *n)
GL_VOID_STUB(glGetBooleanv, int p, void *d)
GL_VOID_STUB(glGetBufferParameteriv, int t, int p, void *d)
GL_INT_STUB(glGetError, void)
GL_VOID_STUB(glGetFloatv, int p, void *d)
GL_VOID_STUB(glGetFramebufferAttachmentParameteriv, int t, int a, int p, void *d)
GL_VOID_STUB(glGetIntegerv, int p, void *d)
GL_VOID_STUB(glGetProgramiv, int p, int pn, void *d)
GL_VOID_STUB(glGetProgramInfoLog, int p, int ms, void *l, void *il)
GL_VOID_STUB(glGetRenderbufferParameteriv, int t, int p, void *d)
GL_VOID_STUB(glGetShaderiv, int s, int p, void *d)
GL_VOID_STUB(glGetShaderInfoLog, int s, int ms, void *l, void *il)
GL_VOID_STUB(glGetShaderPrecisionFormat, int st, int pt, void *r, void *p)
GL_VOID_STUB(glGetShaderSource, int s, int bs, void *l, void *src)
GL_INT_STUB(glGetString, int n)
GL_VOID_STUB(glGetTexParameterfv, int t, int p, void *d)
GL_VOID_STUB(glGetTexParameteriv, int t, int p, void *d)
GL_VOID_STUB(glGetUniformfv, int p, int l, void *d)
GL_VOID_STUB(glGetUniformiv, int p, int l, void *d)
GL_INT_STUB(glGetUniformLocation, int p, const char *n)
GL_VOID_STUB(glGetVertexAttribfv, int i, int p, void *d)
GL_VOID_STUB(glGetVertexAttribiv, int i, int p, void *d)
GL_VOID_STUB(glGetVertexAttribPointerv, int i, int p, void **d)
GL_VOID_STUB(glHint, int t, int m)
GL_INT_STUB(glIsBuffer, int b)
GL_INT_STUB(glIsEnabled, int c)
GL_INT_STUB(glIsFramebuffer, int f)
GL_INT_STUB(glIsProgram, int p)
GL_INT_STUB(glIsRenderbuffer, int r)
GL_INT_STUB(glIsShader, int s)
GL_INT_STUB(glIsTexture, int t)
GL_VOID_STUB(glLineWidth, float w)
GL_VOID_STUB(glLinkProgram, int p)
GL_VOID_STUB(glPixelStorei, int p, int v)
GL_VOID_STUB(glPolygonOffset, float f, float u)
GL_VOID_STUB(glReadPixels, int x, int y, int w, int h, int f, int t, void *d)
GL_VOID_STUB(glReleaseShaderCompiler, void)
GL_VOID_STUB(glRenderbufferStorage, int t, int f, int w, int h)
GL_VOID_STUB(glSampleCoverage, float v, int i)
GL_VOID_STUB(glScissor, int x, int y, int w, int h)
GL_VOID_STUB(glShaderBinary, int c, const void *s, int bf, const void *b, int l)
GL_VOID_STUB(glShaderSource, int s, int c, const void *str, const void *l)
GL_VOID_STUB(glStencilFunc, int f, int r, int m)
GL_VOID_STUB(glStencilFuncSeparate, int fc, int f, int r, int m)
GL_VOID_STUB(glStencilMask, int m)
GL_VOID_STUB(glStencilMaskSeparate, int fc, int m)
GL_VOID_STUB(glStencilOp, int sf, int df, int dp)
GL_VOID_STUB(glStencilOpSeparate, int fc, int sf, int df, int dp)
GL_VOID_STUB(glTexImage2D, int t, int l, int f, int w, int h, int b, int fmt, int tp, const void *d)
GL_VOID_STUB(glTexParameterf, int t, int p, float v)
GL_VOID_STUB(glTexParameterfv, int t, int p, const void *v)
GL_VOID_STUB(glTexParameteri, int t, int p, int v)
GL_VOID_STUB(glTexParameteriv, int t, int p, const void *v)
GL_VOID_STUB(glTexSubImage2D, int t, int l, int xo, int yo, int w, int h, int f, int tp, const void *d)
GL_VOID_STUB(glUniform1f, int l, float v)
GL_VOID_STUB(glUniform1fv, int l, int c, const void *v)
GL_VOID_STUB(glUniform1i, int l, int v)
GL_VOID_STUB(glUniform1iv, int l, int c, const void *v)
GL_VOID_STUB(glUniform2f, int l, float a, float b)
GL_VOID_STUB(glUniform2fv, int l, int c, const void *v)
GL_VOID_STUB(glUniform2i, int l, int a, int b)
GL_VOID_STUB(glUniform2iv, int l, int c, const void *v)
GL_VOID_STUB(glUniform3f, int l, float a, float b, float cc)
GL_VOID_STUB(glUniform3fv, int l, int c, const void *v)
GL_VOID_STUB(glUniform3i, int l, int a, int b, int cc)
GL_VOID_STUB(glUniform3iv, int l, int c, const void *v)
GL_VOID_STUB(glUniform4f, int l, float a, float b, float cc, float d)
GL_VOID_STUB(glUniform4fv, int l, int c, const void *v)
GL_VOID_STUB(glUniform4i, int l, int a, int b, int cc, int d)
GL_VOID_STUB(glUniform4iv, int l, int c, const void *v)
GL_VOID_STUB(glUniformMatrix2fv, int l, int c, int t, const void *v)
GL_VOID_STUB(glUniformMatrix3fv, int l, int c, int t, const void *v)
GL_VOID_STUB(glUniformMatrix4fv, int l, int c, int t, const void *v)
GL_VOID_STUB(glUseProgram, int p)
GL_VOID_STUB(glValidateProgram, int p)
GL_VOID_STUB(glVertexAttrib1f, int i, float a)
GL_VOID_STUB(glVertexAttrib1fv, int i, const void *v)
GL_VOID_STUB(glVertexAttrib2f, int i, float a, float b)
GL_VOID_STUB(glVertexAttrib2fv, int i, const void *v)
GL_VOID_STUB(glVertexAttrib3f, int i, float a, float b, float c)
GL_VOID_STUB(glVertexAttrib3fv, int i, const void *v)
GL_VOID_STUB(glVertexAttrib4f, int i, float a, float b, float c, float d)
GL_VOID_STUB(glVertexAttrib4fv, int i, const void *v)
GL_VOID_STUB(glVertexAttribPointer, int i, int s, int t, int n, int st, const void *p)
GL_VOID_STUB(glViewport, int x, int y, int w, int h)
GL_VOID_STUB(glPolygonOffsetClampEXT, float f, float u, float c)
GL_VOID_STUB(glClipControlEXT, int o, int d)
GL_VOID_STUB(glPolygonModeWEBGL, int f, int m)
