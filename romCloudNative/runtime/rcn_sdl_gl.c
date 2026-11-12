// RomCloud Native : OpenGL pour les jeux qui passent par SDL (MonoGame « DesktopGL », FNA…).
//
// Le jeu obtient chaque fonction par SDL_GL_GetProcAddress : il reçoit un relais qui lit ses arguments
// selon la convention d'appel Windows x64 (entiers dans rcx, rdx, r8, r9, flottants dans xmm0 à xmm3
// selon leur position, puis la pile) et appelle la fonction du pilote de l'hôte.
//
// Le jeu croit dessiner dans le tampon de la fenêtre (framebuffer 0) : il dessine en fait dans une image
// de la taille qu'il a demandée, que SDL_GL_SwapWindow met à l'échelle de l'écran (Android) ou relit
// pour l'hôte (Windows, essais). Sous Android (OpenGL ES 3), les fonctions propres à OpenGL de bureau
// sont émulées, les nuanceurs GLSL adaptés et la version annoncée est celle qu'attend un jeu de bureau.
#include <stdio.h>
#include <stdlib.h>

#include "rcn_internal.h"
#include "rcn_sdl.h"

#if defined(_WIN32)
#define GLCALL __stdcall
#else
#define GLCALL
#endif

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat;
typedef double GLdouble;
typedef char GLchar;
typedef ptrdiff_t GLintptr, GLsizeiptr;
// Types rendus qui doivent former un seul mot dans la table des fonctions.
typedef const GLubyte *GLstring;
typedef void *GLvoidptr;

enum {
    GL_FRONT = 0x404, GL_BACK = 0x405, GL_FRONT_LEFT = 0x400, GL_BACK_LEFT = 0x402, GL_SCISSOR_TEST = 0xC11,
    GL_COLOR_WRITEMASK = 0xC23, GL_COLOR_CLEAR_VALUE = 0xC22, GL_PACK_ALIGNMENT = 0xD05, GL_VERSION = 0x1F02,
    GL_EXTENSIONS = 0x1F03, GL_SHADING_LANGUAGE_VERSION = 0x8B8C, GL_RGBA = 0x1908, GL_BGRA = 0x80E1,
    GL_UNSIGNED_BYTE = 0x1401, GL_COLOR_BUFFER_BIT = 0x4000, GL_LINEAR = 0x2601, GL_FRAMEBUFFER = 0x8D40,
    GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9, GL_FRAMEBUFFER_BINDING = 0x8CA6,
    GL_READ_FRAMEBUFFER_BINDING = 0x8CAA, GL_RENDERBUFFER = 0x8D41, GL_RGBA8 = 0x8058,
    GL_DEPTH24_STENCIL8 = 0x88F0, GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_DEPTH_STENCIL_ATTACHMENT = 0x821A,
    GL_BUFFER_SIZE = 0x8764, GL_MAP_READ_BIT = 1, GL_MAP_WRITE_BIT = 2, GL_READ_ONLY = 0x88B8, GL_WRITE_ONLY = 0x88B9,
    GL_PIXEL_PACK_BUFFER = 0x88EB, GL_PIXEL_PACK_BUFFER_BINDING = 0x88ED, GL_NUM_EXTENSIONS = 0x821D,
};

// --- Lecture des arguments (convention Windows x64) ---

#define I(n) rcn_arg(ctx, n)
#define P(n) PTR(rcn_arg(ctx, n))
#define F(n) farg(ctx, n)
#define D(n) darg(ctx, n)
static float farg(RcnCtx *ctx, int n) {
    if (n < 4) return ctx->xmm[n].f[0];
    u32 bits = (u32)rcn_arg(ctx, n);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
static double darg(RcnCtx *ctx, int n) {
    if (n < 4) return ctx->xmm[n].db[0];
    u64 bits = rcn_arg(ctx, n);
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

// --- Fonctions ---
// X(nature, type rendu, nom, paramètres, arguments lus)
//   nature : D directe (fonction du pilote), E émulée (e_<nom>), H interne (chargée, jamais donnée au jeu).
#define GL_FUNCS(X)                                                                                              \
    X(D, void, glActiveTexture, (GLenum), (I(0)))                                                                \
    X(D, void, glAttachShader, (GLuint, GLuint), (I(0), I(1)))                                                   \
    X(D, void, glBeginQuery, (GLenum, GLuint), (I(0), I(1)))                                                     \
    X(D, void, glBindBuffer, (GLenum, GLuint), (I(0), I(1)))                                                     \
    X(E, void, glBindFramebuffer, (GLenum, GLuint), (I(0), I(1)))                                                \
    X(D, void, glBindRenderbuffer, (GLenum, GLuint), (I(0), I(1)))                                               \
    X(D, void, glBindTexture, (GLenum, GLuint), (I(0), I(1)))                                                    \
    X(D, void, glBlendColor, (GLfloat, GLfloat, GLfloat, GLfloat), (F(0), F(1), F(2), F(3)))                     \
    X(D, void, glBlendEquationSeparate, (GLenum, GLenum), (I(0), I(1)))                                          \
    X(D, void, glBlendEquationSeparatei, (GLuint, GLenum, GLenum), (I(0), I(1), I(2)))                           \
    X(D, void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum), (I(0), I(1), I(2), I(3)))                  \
    X(D, void, glBlendFuncSeparatei, (GLuint, GLenum, GLenum, GLenum, GLenum), (I(0), I(1), I(2), I(3), I(4)))   \
    X(D, void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum),  \
      (I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7), I(8), I(9)))                                              \
    X(D, void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum), (I(0), I(1), P(2), I(3)))               \
    X(D, void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *), (I(0), I(1), I(2), P(3)))          \
    X(D, GLenum, glCheckFramebufferStatus, (GLenum), (I(0)))                                                     \
    X(D, void, glClear, (GLbitfield), (I(0)))                                                                    \
    X(D, void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat), (F(0), F(1), F(2), F(3)))                     \
    X(E, void, glClearDepth, (GLdouble), (D(0)))                                                                 \
    X(D, void, glClearDepthf, (GLfloat), (F(0)))                                                                 \
    X(D, void, glClearStencil, (GLint), (I(0)))                                                                  \
    X(D, void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean), (I(0), I(1), I(2), I(3)))              \
    X(D, void, glCompileShader, (GLuint), (I(0)))                                                                \
    X(D, void, glCompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *),  \
      (I(0), I(1), I(2), I(3), I(4), I(5), I(6), P(7)))                                                          \
    X(D, void, glCompressedTexSubImage2D,                                                                        \
      (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *),                            \
      (I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7), P(8)))                                                    \
    X(D, GLuint, glCreateProgram, (void), ())                                                                    \
    X(D, GLuint, glCreateShader, (GLenum), (I(0)))                                                               \
    X(D, void, glCullFace, (GLenum), (I(0)))                                                                     \
    X(D, void, glDeleteBuffers, (GLsizei, const GLuint *), (I(0), P(1)))                                         \
    X(D, void, glDeleteFramebuffers, (GLsizei, const GLuint *), (I(0), P(1)))                                    \
    X(D, void, glDeleteProgram, (GLuint), (I(0)))                                                                \
    X(D, void, glDeleteQueries, (GLsizei, const GLuint *), (I(0), P(1)))                                         \
    X(D, void, glDeleteRenderbuffers, (GLsizei, const GLuint *), (I(0), P(1)))                                   \
    X(D, void, glDeleteShader, (GLuint), (I(0)))                                                                 \
    X(D, void, glDeleteTextures, (GLsizei, const GLuint *), (I(0), P(1)))                                        \
    X(D, void, glDepthFunc, (GLenum), (I(0)))                                                                    \
    X(D, void, glDepthMask, (GLboolean), (I(0)))                                                                 \
    X(E, void, glDepthRange, (GLdouble, GLdouble), (D(0), D(1)))                                                 \
    X(D, void, glDepthRangef, (GLfloat, GLfloat), (F(0), F(1)))                                                  \
    X(D, void, glDetachShader, (GLuint, GLuint), (I(0), I(1)))                                                   \
    X(D, void, glDisable, (GLenum), (I(0)))                                                                      \
    X(D, void, glDisableVertexAttribArray, (GLuint), (I(0)))                                                     \
    X(E, void, glDrawArrays, (GLenum, GLint, GLsizei), (I(0), I(1), I(2)))                                       \
    X(E, void, glDrawBuffer, (GLenum), (I(0)))                                                                   \
    X(D, void, glDrawBuffers, (GLsizei, const GLenum *), (I(0), P(1)))                                           \
    X(D, void, glDrawElements, (GLenum, GLsizei, GLenum, const void *), (I(0), I(1), I(2), P(3)))                \
    X(D, void, glDrawElementsInstanced, (GLenum, GLsizei, GLenum, const void *, GLsizei),                        \
      (I(0), I(1), I(2), P(3), I(4)))                                                                            \
    X(D, void, glEnable, (GLenum), (I(0)))                                                                       \
    X(D, void, glEnableVertexAttribArray, (GLuint), (I(0)))                                                      \
    X(D, void, glEndQuery, (GLenum), (I(0)))                                                                     \
    X(D, void, glFinish, (void), ())                                                                             \
    X(D, void, glFlush, (void), ())                                                                              \
    X(D, void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint), (I(0), I(1), I(2), I(3)))            \
    X(D, void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint), (I(0), I(1), I(2), I(3), I(4)))  \
    X(D, void, glFrontFace, (GLenum), (I(0)))                                                                    \
    X(D, void, glGenBuffers, (GLsizei, GLuint *), (I(0), P(1)))                                                  \
    X(D, void, glGenFramebuffers, (GLsizei, GLuint *), (I(0), P(1)))                                             \
    X(D, void, glGenQueries, (GLsizei, GLuint *), (I(0), P(1)))                                                  \
    X(D, void, glGenRenderbuffers, (GLsizei, GLuint *), (I(0), P(1)))                                            \
    X(D, void, glGenTextures, (GLsizei, GLuint *), (I(0), P(1)))                                                 \
    X(D, void, glGenerateMipmap, (GLenum), (I(0)))                                                               \
    X(D, GLint, glGetAttribLocation, (GLuint, const GLchar *), (I(0), P(1)))                                     \
    X(E, void, glGetCompressedTexImage, (GLenum, GLint, void *), (I(0), I(1), P(2)))                             \
    X(D, GLenum, glGetError, (void), ())                                                                         \
    X(E, void, glGetIntegerv, (GLenum, GLint *), (I(0), P(1)))                                                   \
    X(D, void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *), (I(0), I(1), P(2), P(3)))            \
    X(D, void, glGetProgramiv, (GLuint, GLenum, GLint *), (I(0), I(1), P(2)))                                    \
    X(E, void, glGetQueryObjectiv, (GLuint, GLenum, GLint *), (I(0), I(1), P(2)))                                \
    X(D, void, glGetQueryObjectuiv, (GLuint, GLenum, GLuint *), (I(0), I(1), P(2)))                              \
    X(D, void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *), (I(0), I(1), P(2), P(3)))             \
    X(D, void, glGetShaderiv, (GLuint, GLenum, GLint *), (I(0), I(1), P(2)))                                     \
    X(E, GLstring, glGetString, (GLenum), (I(0)))                                                         \
    X(E, void, glGetTexImage, (GLenum, GLint, GLenum, GLenum, void *), (I(0), I(1), I(2), I(3), P(4)))           \
    X(D, GLint, glGetUniformLocation, (GLuint, const GLchar *), (I(0), P(1)))                                    \
    X(D, GLboolean, glIsProgram, (GLuint), (I(0)))                                                               \
    X(D, GLboolean, glIsShader, (GLuint), (I(0)))                                                                \
    X(D, void, glLinkProgram, (GLuint), (I(0)))                                                                  \
    X(E, GLvoidptr, glMapBuffer, (GLenum, GLenum), (I(0), I(1)))                                                    \
    X(D, void, glPixelStorei, (GLenum, GLint), (I(0), I(1)))                                                     \
    X(E, void, glPolygonMode, (GLenum, GLenum), (I(0), I(1)))                                                    \
    X(D, void, glPolygonOffset, (GLfloat, GLfloat), (F(0), F(1)))                                                \
    X(E, void, glReadBuffer, (GLenum), (I(0)))                                                                   \
    X(D, void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *),                           \
      (I(0), I(1), I(2), I(3), I(4), I(5), P(6)))                                                                \
    X(D, void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei), (I(0), I(1), I(2), I(3)))              \
    X(D, void, glRenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei),                    \
      (I(0), I(1), I(2), I(3), I(4)))                                                                            \
    X(D, void, glScissor, (GLint, GLint, GLsizei, GLsizei), (I(0), I(1), I(2), I(3)))                            \
    X(E, void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *), (I(0), I(1), P(2), P(3))) \
    X(D, void, glStencilFunc, (GLenum, GLint, GLuint), (I(0), I(1), I(2)))                                       \
    X(D, void, glStencilFuncSeparate, (GLenum, GLenum, GLint, GLuint), (I(0), I(1), I(2), I(3)))                 \
    X(D, void, glStencilMask, (GLuint), (I(0)))                                                                  \
    X(D, void, glStencilOp, (GLenum, GLenum, GLenum), (I(0), I(1), I(2)))                                        \
    X(D, void, glStencilOpSeparate, (GLenum, GLenum, GLenum, GLenum), (I(0), I(1), I(2), I(3)))                  \
    X(D, void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *),      \
      (I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7), P(8)))                                                    \
    X(D, void, glTexImage3D,                                                                                     \
      (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *),                    \
      (I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7), I(8), P(9)))                                              \
    X(D, void, glTexParameterf, (GLenum, GLenum, GLfloat), (I(0), I(1), F(2)))                                   \
    X(D, void, glTexParameterfv, (GLenum, GLenum, const GLfloat *), (I(0), I(1), P(2)))                          \
    X(D, void, glTexParameteri, (GLenum, GLenum, GLint), (I(0), I(1), I(2)))                                     \
    X(D, void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *),   \
      (I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7), P(8)))                                                    \
    X(D, void, glTexSubImage3D,                                                                                  \
      (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *),             \
      (I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7), I(8), I(9), P(10)))                                       \
    X(D, void, glUniform1i, (GLint, GLint), (I(0), I(1)))                                                        \
    X(D, void, glUniform4fv, (GLint, GLsizei, const GLfloat *), (I(0), I(1), P(2)))                              \
    X(D, GLboolean, glUnmapBuffer, (GLenum), (I(0)))                                                             \
    X(D, void, glUseProgram, (GLuint), (I(0)))                                                                   \
    X(D, void, glVertexAttribDivisor, (GLuint, GLuint), (I(0), I(1)))                                            \
    X(D, void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *),                 \
      (I(0), I(1), I(2), I(3), I(4), P(5)))                                                                      \
    X(D, void, glViewport, (GLint, GLint, GLsizei, GLsizei), (I(0), I(1), I(2), I(3)))                           \
    X(H, void, glGetBooleanv, (GLenum, GLboolean *), (I(0), P(1)))                                               \
    X(H, void, glGetFloatv, (GLenum, GLfloat *), (I(0), P(1)))                                                   \
    X(H, GLboolean, glIsEnabled, (GLenum), (I(0)))                                                               \
    X(H, GLvoidptr, glMapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield), (I(0), I(1), I(2), I(3)))        \
    X(H, void, glGetBufferParameteriv, (GLenum, GLenum, GLint *), (I(0), I(1), P(2)))                            \
    X(H, GLstring, glGetStringi, (GLenum, GLuint), (I(0), I(1)))

// Pointeurs vers les fonctions de l'hôte (« h_ » : pas de collision avec ses en-têtes).
#define DECLARE(kind, ret, name, params, args) static ret(GLCALL *h_##name) params;
GL_FUNCS(DECLARE)

// Émulations (définies plus bas).
#define DECLARE_E_D(ret, name, params)
#define DECLARE_E_H(ret, name, params)
#define DECLARE_E_E(ret, name, params) static ret e_##name params;
#define DECLARE_E(kind, ret, name, params, args) DECLARE_E_##kind(ret, name, params)
GL_FUNCS(DECLARE_E)

// Résultat rendu au jeu, selon son type.
static void put_void(RcnCtx *ctx) { (void)ctx; }
static void put_GLenum(RcnCtx *ctx, GLenum v) { RET((u64)v); }
static void put_GLuint(RcnCtx *ctx, GLuint v) { RET((u64)v); }
static void put_GLint(RcnCtx *ctx, GLint v) { RET_I32(v); }
static void put_GLboolean(RcnCtx *ctx, GLboolean v) { RET((u64)v); }
static void put_GLstring(RcnCtx *ctx, GLstring v) { RET((u64)(uintptr_t)v); }
static void put_GLvoidptr(RcnCtx *ctx, GLvoidptr v) { RET((u64)(uintptr_t)v); }

// Relais : arguments lus, fonction appelée, résultat rendu.
#define CALL_D(name, args) h_##name args
#define CALL_H(name, args) h_##name args
#define CALL_E(name, args) e_##name args
#define WRAP_void(kind, name, args) CALL_##kind(name, args);
#define WRAP_GLenum(kind, name, args) put_GLenum(ctx, CALL_##kind(name, args));
#define WRAP_GLuint(kind, name, args) put_GLuint(ctx, CALL_##kind(name, args));
#define WRAP_GLint(kind, name, args) put_GLint(ctx, CALL_##kind(name, args));
#define WRAP_GLboolean(kind, name, args) put_GLboolean(ctx, CALL_##kind(name, args));
#define WRAP_GLstring(kind, name, args) put_GLstring(ctx, CALL_##kind(name, args));
#define WRAP_GLvoidptr(kind, name, args) put_GLvoidptr(ctx, CALL_##kind(name, args));
#define WRAP(kind, ret, name, params, args) \
    static void w_##name(RcnCtx *ctx) { WRAP_##ret(kind, name, args) }
GL_FUNCS(WRAP)

// Table : nom, relais, pointeur de l'hôte, nature.
typedef struct {
    const char *name;
    RcnFn wrapper;
    void **host;
    char kind;
    u64 thunk;
} GlEntry;
#define ENTRY(kind, ret, name, params, args) {#name, w_##name, (void **)&h_##name, #kind[0], 0},
static GlEntry gl_table[] = {GL_FUNCS(ENTRY)};
#define GL_COUNT (sizeof gl_table / sizeof gl_table[0])

static struct {
    int es;                    // OpenGL ES 3 (Android)
    GLuint framebuffer;        // image du jeu (son « framebuffer 0 »)
    GLuint color, depth;       // ses tampons
    u32 width, height;
    GLuint draw, read;         // framebuffers que le jeu croit liés (0 : le sien)
    char *extensions;          // extensions annoncées (OpenGL ES)
    pthread_mutex_t lock;
} gl = {.lock = PTHREAD_MUTEX_INITIALIZER};

static GLuint real_fb(GLuint id) { return id ? id : gl.framebuffer; }

// Liaisons de framebuffers du jeu rétablies (après une copie de l'image).
static void restore_bindings(void) {
    h_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, real_fb(gl.draw));
    h_glBindFramebuffer(GL_READ_FRAMEBUFFER, real_fb(gl.read));
}

// --- Émulations ---

static void e_glBindFramebuffer(GLenum target, GLuint id) {
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) gl.draw = id;
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) gl.read = id;
    h_glBindFramebuffer(target, real_fb(id));
}
// Le framebuffer 0 du jeu reste 0 à ses yeux.
static void e_glGetIntegerv(GLenum pname, GLint *v) {
    h_glGetIntegerv(pname, v);
    if ((pname == GL_FRAMEBUFFER_BINDING || pname == GL_READ_FRAMEBUFFER_BINDING) && (GLuint)*v == gl.framebuffer) *v = 0;
}
// GL_BACK / GL_FRONT désignent l'image du jeu : sa première couleur.
static GLenum color_buffer(GLenum mode, GLuint bound) {
    if (!bound && (mode == GL_BACK || mode == GL_FRONT || mode == GL_BACK_LEFT || mode == GL_FRONT_LEFT)) return GL_COLOR_ATTACHMENT0;
    return mode;
}
static void e_glDrawBuffer(GLenum mode) {
    GLenum m = color_buffer(mode, gl.draw);
    if (h_glDrawBuffers) h_glDrawBuffers(1, &m);
}
static void e_glReadBuffer(GLenum mode) { h_glReadBuffer(color_buffer(mode, gl.read)); }
static void e_glClearDepth(GLdouble d) {
    if (h_glClearDepthf) h_glClearDepthf((GLfloat)d);
    else if (h_glClearDepth) h_glClearDepth(d);
}
static void e_glDepthRange(GLdouble n, GLdouble f) {
    if (h_glDepthRangef) h_glDepthRangef((GLfloat)n, (GLfloat)f);
    else if (h_glDepthRange) h_glDepthRange(n, f);
}
static void e_glPolygonMode(GLenum face, GLenum mode) {
    if (h_glPolygonMode && !gl.es) h_glPolygonMode(face, mode);
}
static void e_glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels) {
    if (h_glGetTexImage && !gl.es) h_glGetTexImage(target, level, format, type, pixels);
}
static void e_glGetCompressedTexImage(GLenum target, GLint level, void *pixels) {
    if (h_glGetCompressedTexImage && !gl.es) h_glGetCompressedTexImage(target, level, pixels);
}
static void e_glGetQueryObjectiv(GLuint id, GLenum pname, GLint *v) {
    if (h_glGetQueryObjectiv && !gl.es) h_glGetQueryObjectiv(id, pname, v);
    else h_glGetQueryObjectuiv(id, pname, (GLuint *)v);
}
static GLvoidptr e_glMapBuffer(GLenum target, GLenum access) {
    if (h_glMapBuffer && !gl.es) return h_glMapBuffer(target, access);
    GLint size = 0;
    h_glGetBufferParameteriv(target, GL_BUFFER_SIZE, &size);
    GLbitfield bits = access == GL_READ_ONLY ? GL_MAP_READ_BIT : access == GL_WRITE_ONLY ? GL_MAP_WRITE_BIT : GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
    return h_glMapBufferRange(target, 0, size, bits);
}
// Appels de dessin aberrants (compte négatif ou démesuré) : notés et ignorés.
static void e_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (count < 0 || count > 16000000 || first < 0) {
        static int told;
        if (told++ < 8) fprintf(stderr, "rcn : glDrawArrays(%u, %d, %d) ignore\n", mode, first, count);
        return;
    }
    if (getenv("RCN_GL_LOG")) fprintf(stderr, "glDrawArrays(%u, %d, %d)\n", mode, first, count);
    h_glDrawArrays(mode, first, count);
}
// Version : celle d'OpenGL de bureau que le jeu attend (MonoGame lit « majeur.mineur » au début).
static GLstring e_glGetString(GLenum name) {
    if (!gl.es) return h_glGetString(name);
    switch (name) {
    case GL_VERSION: return (GLstring) "3.0 RomCloud (OpenGL ES 3)";
    case GL_SHADING_LANGUAGE_VERSION: return (GLstring) "1.30";
    case GL_EXTENSIONS: return (GLstring)gl.extensions;
    default: return h_glGetString(name);
    }
}
// Nuanceurs GLSL de bureau (versions 1.10 à 1.40, sans précision) → GLSL ES 1.00.
static void e_glShaderSource(GLuint shader, GLsizei count, const GLchar *const *strings, const GLint *lengths) {
    if (!gl.es) {
        h_glShaderSource(shader, count, strings, lengths);
        return;
    }
    size_t total = 0;
    for (GLsizei i = 0; i < count; i++) total += lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
    char *text = malloc(total + 64), *out = malloc(total + 128);
    if (!text || !out) {
        free(text);
        free(out);
        return;
    }
    size_t at = 0;
    for (GLsizei i = 0; i < count; i++) {
        size_t n = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
        memcpy(text + at, strings[i], n);
        at += n;
    }
    text[at] = 0;
    // « #version 1x0 » retiré : GLSL ES 1.00 par défaut ; précision par défaut ajoutée.
    const char *body = text;
    char *version = strstr(text, "#version");
    if (version) {
        char *eol = strchr(version, '\n');
        memset(version, ' ', eol ? (size_t)(eol - version) : strlen(version));
    }
    snprintf(out, total + 128, "precision highp float;\nprecision mediump int;\n%s", body);
    const GLchar *list[1] = {out};
    h_glShaderSource(shader, 1, list, NULL);
    free(text);
    free(out);
}

// --- Chargement ---

// Extensions de bureau que l'on fournit sous OpenGL ES 3 (fonctions de base de la version 3).
static void build_extensions(void) {
    static const char *const base =
        "GL_ARB_framebuffer_object GL_EXT_framebuffer_object GL_EXT_framebuffer_blit GL_EXT_framebuffer_multisample "
        "GL_ARB_texture_non_power_of_two GL_ARB_instanced_arrays GL_ARB_draw_instanced GL_ARB_occlusion_query "
        "GL_ARB_vertex_array_object GL_ARB_map_buffer_range GL_EXT_draw_buffers2";
    size_t size = strlen(base) + 1;
    GLint count = 0;
    h_glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count && h_glGetStringi; i++) size += strlen((const char *)h_glGetStringi(GL_EXTENSIONS, (GLuint)i)) + 1;
    free(gl.extensions);
    gl.extensions = malloc(size + 1);
    if (!gl.extensions) return;
    strcpy(gl.extensions, base);
    for (GLint i = 0; i < count && h_glGetStringi; i++) {
        const char *e = (const char *)h_glGetStringi(GL_EXTENSIONS, (GLuint)i);
        // Compression S3TC, filtrage anisotrope : seulement si le pilote les a.
        strcat(gl.extensions, " ");
        strcat(gl.extensions, e);
    }
}

int rcn_sdlgl_load(void *(*get_proc)(const char *name), int es) {
    gl.es = es;
    for (size_t i = 0; i < GL_COUNT; i++) *gl_table[i].host = get_proc(gl_table[i].name);
    if (!h_glBindFramebuffer || !h_glGenFramebuffers || !h_glGetIntegerv || !h_glGetString || !h_glShaderSource) return 0;
    if (es) build_extensions();
    return 1;
}

void rcn_sdlgl_resize(u32 width, u32 height) {
    if (width == gl.width && height == gl.height && gl.framebuffer) return;
    if (!gl.framebuffer) {
        h_glGenFramebuffers(1, &gl.framebuffer);
        h_glGenRenderbuffers(1, &gl.color);
        h_glGenRenderbuffers(1, &gl.depth);
    }
    gl.width = width;
    gl.height = height;
    h_glBindRenderbuffer(GL_RENDERBUFFER, gl.color);
    h_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, (GLsizei)width, (GLsizei)height);
    h_glBindRenderbuffer(GL_RENDERBUFFER, gl.depth);
    h_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)width, (GLsizei)height);
    h_glBindRenderbuffer(GL_RENDERBUFFER, 0);
    h_glBindFramebuffer(GL_FRAMEBUFFER, gl.framebuffer);
    h_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, gl.color);
    h_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, gl.depth);
    if (h_glDrawBuffers) {
        GLenum attachment = GL_COLOR_ATTACHMENT0;
        h_glDrawBuffers(1, &attachment);
    }
    h_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, real_fb(gl.draw));
    h_glBindFramebuffer(GL_READ_FRAMEBUFFER, real_fb(gl.read));
    if (!gl.draw) h_glViewport(0, 0, (GLsizei)width, (GLsizei)height);
}

void rcn_sdlgl_size(u32 *width, u32 *height) {
    *width = gl.width;
    *height = gl.height;
}

void rcn_sdlgl_blit(int out_width, int out_height) {
    if (!gl.framebuffer || out_width <= 0 || out_height <= 0) return;
    GLboolean scissor = h_glIsEnabled(GL_SCISSOR_TEST), mask[4];
    GLfloat clear[4];
    h_glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    h_glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    h_glDisable(GL_SCISSOR_TEST);
    h_glColorMask(1, 1, 1, 1);
    h_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    h_glClearColor(0, 0, 0, 1);
    h_glClear(GL_COLOR_BUFFER_BIT);
    // Image entière, proportions gardées, centrée.
    double sx = (double)out_width / gl.width, sy = (double)out_height / gl.height, s = sx < sy ? sx : sy;
    int w = (int)(gl.width * s), h = (int)(gl.height * s), x = (out_width - w) / 2, y = (out_height - h) / 2;
    h_glBindFramebuffer(GL_READ_FRAMEBUFFER, gl.framebuffer);
    h_glBlitFramebuffer(0, 0, (GLint)gl.width, (GLint)gl.height, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    h_glClearColor(clear[0], clear[1], clear[2], clear[3]);
    h_glColorMask(mask[0], mask[1], mask[2], mask[3]);
    if (scissor) h_glEnable(GL_SCISSOR_TEST);
    restore_bindings();
}

void rcn_sdlgl_read(u8 *pixels) {
    if (!gl.framebuffer) return;
    GLint pack = 4, pack_buffer = 0;
    h_glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
    h_glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
    if (pack_buffer) h_glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    h_glPixelStorei(GL_PACK_ALIGNMENT, 4);
    h_glBindFramebuffer(GL_READ_FRAMEBUFFER, gl.framebuffer);
    size_t row = (size_t)gl.width * 4;
    h_glReadPixels(0, 0, (GLsizei)gl.width, (GLsizei)gl.height, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    // OpenGL lit de bas en haut : lignes retournées.
    u8 *line = malloc(row);
    for (u32 y = 0; line && y < gl.height / 2; y++) {
        u8 *a = pixels + y * row, *b = pixels + (gl.height - 1 - y) * row;
        memcpy(line, a, row);
        memcpy(a, b, row);
        memcpy(b, line, row);
    }
    free(line);
    h_glPixelStorei(GL_PACK_ALIGNMENT, pack);
    if (pack_buffer) h_glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)pack_buffer);
    restore_bindings();
}

// Variantes de bureau (EXT, ARB) d'une fonction de base : la fonction de base.
u64 rcn_sdlgl_proc(RcnCtx *ctx, const char *name) {
    char base[96];
    snprintf(base, sizeof base, "%s", name);
    size_t n = strlen(base);
    static const char *const suffixes[] = {"EXT", "ARB"};
    for (size_t s = 0; s < 2; s++) {
        size_t k = strlen(suffixes[s]);
        if (n > k && strcmp(base + n - k, suffixes[s]) == 0) base[n - k] = 0;
    }
    for (size_t i = 0; i < GL_COUNT; i++) {
        GlEntry *e = &gl_table[i];
        if (e->kind == 'H' || strcmp(e->name, base) != 0) continue;
        if (e->kind == 'D' && !*e->host) return 0;
        pthread_mutex_lock(&gl.lock);
        if (!e->thunk) e->thunk = rcn_thunk(ctx, "opengl32.dll", e->name, e->wrapper, 0);
        u64 thunk = e->thunk;
        pthread_mutex_unlock(&gl.lock);
        return thunk;
    }
    return 0;
}

