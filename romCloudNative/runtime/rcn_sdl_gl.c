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
#if defined(__ANDROID__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE // cpu_set_t, sched_setaffinity (thread de rendu)
#endif
#include <ctype.h>
#include <errno.h>
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
    X(E, void, glCompileShader, (GLuint), (I(0)))                                                                \
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
    X(D, void, glDrawRangeElements, (GLenum, GLuint, GLuint, GLsizei, GLenum, const void *),                     \
      (I(0), I(1), I(2), I(3), I(4), P(5)))                                                                      \
    X(D, void, glDrawRangeElementsBaseVertex, (GLenum, GLuint, GLuint, GLsizei, GLenum, const void *, GLint),    \
      (I(0), I(1), I(2), I(3), I(4), P(5), I(6)))                                                                \
    X(E, void, glGetBufferSubData, (GLenum, GLintptr, GLsizeiptr, void *), (I(0), I(1), I(2), P(3)))             \
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
    X(E, void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *),   \
      (I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7), P(8)))                                                    \
    X(D, void, glTexSubImage3D,                                                                                  \
      (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *),             \
      (I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7), I(8), I(9), P(10)))                                       \
    X(D, void, glUniform1i, (GLint, GLint), (I(0), I(1)))                                                        \
    X(D, void, glUniform1iv, (GLint, GLsizei, const GLint *), (I(0), I(1), P(2)))                                \
    X(D, void, glUniform1f, (GLint, GLfloat), (I(0), F(1)))                                                      \
    X(D, void, glUniform4iv, (GLint, GLsizei, const GLint *), (I(0), I(1), P(2)))                                \
    X(D, void, glColorMaski, (GLuint, GLboolean, GLboolean, GLboolean, GLboolean), (I(0), I(1), I(2), I(3), I(4)))\
    X(D, void, glDrawElementsInstancedBaseVertex, (GLenum, GLsizei, GLenum, const void *, GLsizei, GLint),       \
      (I(0), I(1), I(2), P(3), I(4), I(5)))                                                                      \
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
    X(H, void, glGetShaderSource, (GLuint, GLsizei, GLsizei *, GLchar *), (I(0), I(1), P(2), P(3)))              \
    X(D, GLstring, glGetStringi, (GLenum, GLuint), (I(0), I(1)))

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
// Comptes d'appels par fonction (RCN_GL_CALLS=1 : moyenne par image notée toutes les 300 images).
#define INDEX(kind, ret, name, params, args) GLI_##name,
enum { GL_FUNCS(INDEX) GLI_COUNT };
static u32 gl_calls[GLI_COUNT];
static int gl_calls_on; // RCN_GL_CALLS, connu après la première image
#define WRAP(kind, ret, name, params, args) \
    static void w_##name(RcnCtx *ctx) { gl_calls[GLI_##name]++; WRAP_##ret(kind, name, args) }
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

// --- Thread de rendu (Android ; RCN_GL_THREAD=0 le désactive) ---
// Le contexte OpenGL vit sur un thread à lui : le jeu met ses appels dans une file (anneau d'octets, un
// producteur, un consommateur) et continue ; le pilote les exécute sur un autre cœur. Les appels fréquents
// sans valeur rendue sont mis en file (données pointées recopiées : le jeu peut les réutiliser aussitôt) ;
// tous les autres s'exécutent sur le thread de rendu pendant que le jeu attend (relais d'origine, qui lit
// ses arguments dans le contexte du jeu, immobile). Au changement d'image, le jeu garde au plus une image
// d'avance.
#if defined(__ANDROID__)
#include <sched.h>
#include <stdatomic.h>

typedef struct GltCmd {
    void (*fn)(struct GltCmd *c); // NULL : bourrage jusqu'à la fin de l'anneau
    u32 size;                     // octets de l'enregistrement, données comprises (multiple de 16)
    u32 data_size;
    u64 a[9];
} GltCmd;

enum { GLT_RING = 16u << 20, GLT_SPIN = 2000 };
static struct {
    int on;
    pthread_t thread;
    u8 *ring;
    _Atomic u64 head; // écrit par le jeu
    _Atomic u64 tail; // écrit par le thread de rendu
    _Atomic int consumer_waiting, producer_waiting;
    pthread_mutex_t m;
    pthread_cond_t work, done;
    // Plusieurs threads du jeu peuvent appeler OpenGL (ou les fonctions OpenGL de SDL) : un enregistrement
    // à la fois (glt_begin … glt_end).
    pthread_mutex_t producer;
    u64 last_swap_end;
    // Liaisons vues par le jeu (décident si un pointeur est un décalage dans un tampon).
    GLuint array_binding, element_binding;
} glt = {.m = PTHREAD_MUTEX_INITIALIZER, .work = PTHREAD_COND_INITIALIZER, .done = PTHREAD_COND_INITIALIZER,
         .producer = PTHREAD_MUTEX_INITIALIZER};

static GLuint glt_unpack_buffer; // GL_PIXEL_UNPACK_BUFFER lié, vu par le jeu
static inline void *glt_data(GltCmd *c) { return c + 1; }

static cpu_set_t render_cpus;
static int render_pinned;
static void *glt_main(void *arg) {
    (void)arg;
    if (render_pinned) sched_setaffinity(0, sizeof render_cpus, &render_cpus);
    else rcn_thread_default_affinity();
    u64 tail = atomic_load(&glt.tail);
    for (;;) {
        u64 head = atomic_load_explicit(&glt.head, memory_order_acquire);
        for (int i = 0; head == tail && i < GLT_SPIN; i++) head = atomic_load_explicit(&glt.head, memory_order_acquire);
        if (head == tail) {
            pthread_mutex_lock(&glt.m);
            atomic_store(&glt.consumer_waiting, 1);
            while ((head = atomic_load(&glt.head)) == tail) pthread_cond_wait(&glt.work, &glt.m);
            atomic_store(&glt.consumer_waiting, 0);
            pthread_mutex_unlock(&glt.m);
        }
        while (tail != head) {
            GltCmd *c = (GltCmd *)(glt.ring + tail % GLT_RING);
            if (c->fn) c->fn(c);
            tail += c->size;
            // Ordre séquentiel (pas seulement « release ») : l'écriture de tail doit précéder la lecture de
            // producer_waiting, sinon le jeu peut s'endormir sur une position déjà atteinte.
            atomic_store(&glt.tail, tail);
            if (atomic_load(&glt.producer_waiting)) {
                pthread_mutex_lock(&glt.m);
                pthread_cond_broadcast(&glt.done);
                pthread_mutex_unlock(&glt.m);
            }
        }
    }
    return NULL;
}

// Attente (jeu) que le thread de rendu ait passé la position `pos`.
static u64 glt_wait_ns, glt_waits, glt_swap_wait_ns; // attentes du jeu (RCN_GL_CALLS)
static u64 glt_wait_pos, glt_last_size, glt_last_at; // mise au point (rcn_sdlgl_debug)
static void glt_wait(u64 pos) {
    glt_wait_pos = pos;
    for (int i = 0; i < GLT_SPIN; i++)
        if (atomic_load_explicit(&glt.tail, memory_order_acquire) >= pos) return;
    u64 t0 = rcn_now_ns();
    glt_waits++;
    pthread_mutex_lock(&glt.m);
    atomic_fetch_add(&glt.producer_waiting, 1); // compteur : plusieurs threads du jeu peuvent attendre
    while (atomic_load(&glt.tail) < pos) pthread_cond_wait(&glt.done, &glt.m);
    atomic_fetch_sub(&glt.producer_waiting, 1);
    pthread_mutex_unlock(&glt.m);
    glt_wait_ns += rcn_now_ns() - t0;
}

static void glt_publish(u64 head) {
    // Ordre séquentiel : voir glt_main (sinon le thread de rendu peut s'endormir avec du travail en attente).
    atomic_store(&glt.head, head);
    if (atomic_load(&glt.consumer_waiting)) {
        pthread_mutex_lock(&glt.m);
        pthread_cond_signal(&glt.work);
        pthread_mutex_unlock(&glt.m);
    }
}

// Enregistrement de `data_size` octets de données à remplir, puis glt_end. Jamais plus grand que
// l'anneau (les gros envois passent par le chemin synchrone).
static GltCmd *glt_begin(void (*fn)(GltCmd *), u32 data_size) {
    u32 size = (u32)((sizeof(GltCmd) + data_size + 15) & ~15u);
    pthread_mutex_lock(&glt.producer);
    u64 head = atomic_load_explicit(&glt.head, memory_order_relaxed);
    u32 at = (u32)(head % GLT_RING);
    glt_last_size = size;
    glt_last_at = at;
    if (at + size > GLT_RING) {
        // Pas la place jusqu'au bout : bourrage, l'enregistrement part au début de l'anneau.
        // La place du bourrage d'abord (le thread de rendu ne peut atteindre le début de l'anneau qu'après
        // l'avoir lu : attendre ici la place de l'enregistrement entier bloquerait les deux threads).
        u32 pad = GLT_RING - at;
        if (head + pad > GLT_RING) glt_wait(head + pad - GLT_RING);
        GltCmd *p = (GltCmd *)(glt.ring + at);
        p->fn = NULL;
        p->size = pad;
        head += pad;
        glt_publish(head);
        at = 0;
    }
    if (head + size > GLT_RING) glt_wait(head + size - GLT_RING);
    GltCmd *c = (GltCmd *)(glt.ring + at);
    c->fn = fn;
    c->size = size;
    c->data_size = data_size;
    return c;
}
static u64 glt_end(GltCmd *c) {
    u64 head = atomic_load_explicit(&glt.head, memory_order_relaxed) + c->size;
    glt_publish(head);
    pthread_mutex_unlock(&glt.producer);
    return head;
}

static int glt_is_render_thread(void) { return glt.on && pthread_equal(pthread_self(), glt.thread); }

// Appel synchrone : `fn(ctx)` exécuté sur le thread de rendu, le jeu attend.
static void x_sync(GltCmd *c) { ((RcnFn)(uintptr_t)c->a[0])((RcnCtx *)(uintptr_t)c->a[1]); }
// Tableaux de sommets côté client (glVertexAttribPointer sans tampon lié), suivis côté jeu : le pointeur
// n'est donné à OpenGL qu'au tracé, les sommets recopiés dans l'enregistrement du tracé (le jeu peut réécrire
// sa mémoire dès le retour de l'appel). `stale` : indices dont OpenGL n'a pas le pointeur du jeu.
enum { GLT_ATTRIBS = 16 };
static struct {
    u32 mask, enabled, divisor, stale;
    struct {
        GLint size;
        GLenum type;
        GLboolean norm;
        GLsizei stride;
        const u8 *ptr;
    } a[GLT_ATTRIBS];
} glt_client;
static void x_client_vap(GltCmd *c);
static u64 glt_end(GltCmd *c);
// Avant un appel exécuté sur place (tracé synchrone…) : OpenGL reçoit les pointeurs du jeu.
static void glt_client_restore(void) {
    u32 m = glt_client.stale & glt_client.mask;
    while (m) {
        int i = __builtin_ctz(m);
        m &= m - 1;
        GltCmd *c = glt_begin(x_client_vap, 0);
        c->a[0] = (u64)i;
        c->a[1] = (u64)glt_client.a[i].size;
        c->a[2] = glt_client.a[i].type;
        c->a[3] = glt_client.a[i].norm;
        c->a[4] = (u64)(u32)glt_client.a[i].stride;
        c->a[5] = (u64)(uintptr_t)glt_client.a[i].ptr;
        glt_end(c);
    }
    glt_client.stale &= ~glt_client.mask;
}

// Appels synchrones par nom (RCN_GL_CALLS) : qui fait attendre le jeu.
static struct { const char *name; u64 n, ns; } glt_sync_stats[48];
static void glt_sync_n(RcnFn fn, RcnCtx *ctx, const char *name) {
    if (!glt.on || glt_is_render_thread()) {
        fn(ctx);
        return;
    }
    u64 t0 = gl_calls_on ? rcn_now_ns() : 0;
    if (glt_client.stale & glt_client.mask) glt_client_restore();
    GltCmd *c = glt_begin(x_sync, 0);
    c->a[0] = (u64)(uintptr_t)fn;
    c->a[1] = (u64)(uintptr_t)ctx;
    glt_wait(glt_end(c));
    if (gl_calls_on) {
        for (int i = 0; i < 48; i++)
            if (!glt_sync_stats[i].name || glt_sync_stats[i].name == name) {
                glt_sync_stats[i].name = name;
                glt_sync_stats[i].n++;
                glt_sync_stats[i].ns += rcn_now_ns() - t0;
                break;
            }
    }
}
#define glt_sync(fn, ctx) glt_sync_n(fn, ctx, #fn)

// Pointeur vers la fonction appelée par chaque relais (pilote, ou émulation e_<nom>).
#define CPTR_D(name) h_##name
#define CPTR_H(name) h_##name
#define CPTR_E(name) e_##name
#define CPTR(kind, ret, name, params, args) \
    static inline ret (*c_##name(void)) params { return CPTR_##kind(name); }
GL_FUNCS(CPTR)

static inline u64 fbits(float f) {
    u32 b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float bitsf(u64 v) {
    u32 b = (u32)v;
    float f;
    memcpy(&f, &b, 4);
    return f;
}
#define CAP_I(n) c->a[n] = I(n)
#define CAP_F(n) c->a[n] = fbits(F(n))
#define ARG_I(n) c->a[n]
#define ARG_F(n) bitsf(c->a[n])
#define ASYNC_HEAD(name) static void x_##name(GltCmd *c)
#define ASYNC_T(name) static void t_##name(RcnCtx *ctx)
#define ASYNC1(name, t0)                                                            \
    ASYNC_HEAD(name) { c_##name()(ARG_##t0(0)); }                                   \
    ASYNC_T(name) {                                                                 \
        gl_calls[GLI_##name]++;                                                     \
        GltCmd *c = glt_begin(x_##name, 0);                                         \
        CAP_##t0(0);                                                                \
        glt_end(c);                                                                 \
    }
#define ASYNC2(name, t0, t1)                                                        \
    ASYNC_HEAD(name) { c_##name()(ARG_##t0(0), ARG_##t1(1)); }                      \
    ASYNC_T(name) {                                                                 \
        gl_calls[GLI_##name]++;                                                     \
        GltCmd *c = glt_begin(x_##name, 0);                                         \
        CAP_##t0(0);                                                                \
        CAP_##t1(1);                                                                \
        glt_end(c);                                                                 \
    }
#define ASYNC3(name, t0, t1, t2)                                                    \
    ASYNC_HEAD(name) { c_##name()(ARG_##t0(0), ARG_##t1(1), ARG_##t2(2)); }         \
    ASYNC_T(name) {                                                                 \
        gl_calls[GLI_##name]++;                                                     \
        GltCmd *c = glt_begin(x_##name, 0);                                         \
        CAP_##t0(0);                                                                \
        CAP_##t1(1);                                                                \
        CAP_##t2(2);                                                                \
        glt_end(c);                                                                 \
    }
#define ASYNC4(name, t0, t1, t2, t3)                                                     \
    ASYNC_HEAD(name) { c_##name()(ARG_##t0(0), ARG_##t1(1), ARG_##t2(2), ARG_##t3(3)); } \
    ASYNC_T(name) {                                                                      \
        gl_calls[GLI_##name]++;                                                          \
        GltCmd *c = glt_begin(x_##name, 0);                                              \
        CAP_##t0(0);                                                                     \
        CAP_##t1(1);                                                                     \
        CAP_##t2(2);                                                                     \
        CAP_##t3(3);                                                                     \
        glt_end(c);                                                                      \
    }
#define ASYNC5(name, t0, t1, t2, t3, t4)                                                              \
    ASYNC_HEAD(name) { c_##name()(ARG_##t0(0), ARG_##t1(1), ARG_##t2(2), ARG_##t3(3), ARG_##t4(4)); } \
    ASYNC_T(name) {                                                                                   \
        gl_calls[GLI_##name]++;                                                                       \
        GltCmd *c = glt_begin(x_##name, 0);                                                           \
        CAP_##t0(0);                                                                                  \
        CAP_##t1(1);                                                                                  \
        CAP_##t2(2);                                                                                  \
        CAP_##t3(3);                                                                                  \
        CAP_##t4(4);                                                                                  \
        glt_end(c);                                                                                   \
    }

// Appels sans pointeur ni valeur rendue.
ASYNC1(glActiveTexture, I)
ASYNC2(glBindTexture, I, I)
ASYNC4(glBlendColor, F, F, F, F)
ASYNC2(glBlendEquationSeparate, I, I)
ASYNC4(glBlendFuncSeparate, I, I, I, I)
ASYNC1(glClear, I)
ASYNC4(glClearColor, F, F, F, F)
ASYNC1(glClearDepthf, F)
ASYNC1(glClearStencil, I)
ASYNC4(glColorMask, I, I, I, I)
ASYNC1(glCullFace, I)
ASYNC1(glDepthFunc, I)
ASYNC1(glDepthMask, I)
ASYNC2(glDepthRangef, F, F)
ASYNC1(glDisable, I)
ASYNC1(glEnable, I)
ASYNC5(glFramebufferTexture2D, I, I, I, I, I)
ASYNC1(glFrontFace, I)
ASYNC2(glPolygonOffset, F, F)
ASYNC4(glScissor, I, I, I, I)
ASYNC3(glStencilFunc, I, I, I)
ASYNC4(glStencilFuncSeparate, I, I, I, I)
ASYNC1(glStencilMask, I)
ASYNC3(glStencilOp, I, I, I)
ASYNC4(glStencilOpSeparate, I, I, I, I)
ASYNC3(glTexParameteri, I, I, I)
ASYNC2(glUniform1i, I, I)
ASYNC2(glUniform1f, I, F)
ASYNC1(glUseProgram, I)
ASYNC4(glViewport, I, I, I, I)
ASYNC2(glBindFramebuffer, I, I)

// glBindBuffer : liaison suivie côté jeu.
ASYNC_HEAD(glBindBuffer) { c_glBindBuffer()((GLenum)c->a[0], (GLuint)c->a[1]); }
ASYNC_T(glBindBuffer) {
    gl_calls[GLI_glBindBuffer]++;
    GLenum target = (GLenum)I(0);
    GLuint buffer = (GLuint)I(1);
    if (target == 0x8892) glt.array_binding = buffer;
    if (target == 0x8893) glt.element_binding = buffer;
    if (target == 0x88EC) glt_unpack_buffer = buffer;
    GltCmd *c = glt_begin(x_glBindBuffer, 0);
    c->a[0] = target;
    c->a[1] = buffer;
    glt_end(c);
}
// glDeleteBuffers : synchrone ; une liaison supprimée revient à 0.
ASYNC_T(glDeleteBuffers) {
    GLsizei n = (GLsizei)I(0);
    const GLuint *ids = P(1);
    for (GLsizei i = 0; ids && i < n; i++) {
        if (ids[i] == glt.array_binding) glt.array_binding = 0;
        if (ids[i] == glt.element_binding) glt.element_binding = 0;
    }
    glt_sync(w_glDeleteBuffers, ctx);
}

// Uniformes en tableau : valeurs recopiées.
#define ASYNC_UNIFORMV(name, type, per)                                            \
    ASYNC_HEAD(name) { c_##name()((GLint)c->a[0], (GLsizei)c->a[1], glt_data(c)); } \
    ASYNC_T(name) {                                                                \
        GLsizei count = (GLsizei)I(1);                                             \
        const void *v = P(2);                                                      \
        u64 bytes = (u64)(count > 0 ? count : 0) * (per) * sizeof(type);           \
        if (!v || bytes > (1u << 20)) {                                            \
            glt_sync(w_##name, ctx);                                               \
            return;                                                                \
        }                                                                          \
        gl_calls[GLI_##name]++;                                                    \
        GltCmd *c = glt_begin(x_##name, (u32)bytes);                               \
        c->a[0] = I(0);                                                            \
        c->a[1] = (u64)count;                                                      \
        memcpy(glt_data(c), v, (size_t)bytes);                                     \
        glt_end(c);                                                                \
    }
ASYNC_UNIFORMV(glUniform4fv, GLfloat, 4)
ASYNC_UNIFORMV(glUniform1iv, GLint, 1)
ASYNC_UNIFORMV(glUniform4iv, GLint, 4)

// Envois dans un tampon : données recopiées.
ASYNC_HEAD(glBufferSubData) { c_glBufferSubData()((GLenum)c->a[0], (GLintptr)c->a[1], (GLsizeiptr)c->a[2], glt_data(c)); }
ASYNC_T(glBufferSubData) {
    GLsizeiptr size = (GLsizeiptr)I(2);
    const void *data = P(3);
    if (!data || size <= 0 || size > (4 << 20)) {
        glt_sync(w_glBufferSubData, ctx);
        return;
    }
    gl_calls[GLI_glBufferSubData]++;
    GltCmd *c = glt_begin(x_glBufferSubData, (u32)size);
    c->a[0] = I(0);
    c->a[1] = I(1);
    c->a[2] = (u64)size;
    memcpy(glt_data(c), data, (size_t)size);
    glt_end(c);
}
ASYNC_HEAD(glBufferData) {
    c_glBufferData()((GLenum)c->a[0], (GLsizeiptr)c->a[1], c->a[3] ? glt_data(c) : NULL, (GLenum)c->a[2]);
}
ASYNC_T(glBufferData) {
    GLsizeiptr size = (GLsizeiptr)I(1);
    const void *data = P(2);
    u64 copy = data ? (u64)size : 0;
    if (size < 0 || copy > (4 << 20)) {
        glt_sync(w_glBufferData, ctx);
        return;
    }
    gl_calls[GLI_glBufferData]++;
    GltCmd *c = glt_begin(x_glBufferData, (u32)copy);
    c->a[0] = I(0);
    c->a[1] = (u64)size;
    c->a[2] = I(3);
    c->a[3] = data != NULL;
    if (copy) memcpy(glt_data(c), data, (size_t)copy);
    glt_end(c);
}

// Attributs : état des tableaux côté client tenu à jour.
#define ASYNC_ATTRIB(name, update)                    \
    ASYNC_HEAD(name) { c_##name()((GLuint)c->a[0]); } \
    ASYNC_T(name) {                                   \
        gl_calls[GLI_##name]++;                       \
        u32 i = (u32)I(0);                            \
        if (i < GLT_ATTRIBS) update;                  \
        GltCmd *c = glt_begin(x_##name, 0);           \
        c->a[0] = i;                                  \
        glt_end(c);                                   \
    }
ASYNC_ATTRIB(glEnableVertexAttribArray, glt_client.enabled |= 1u << i)
ASYNC_ATTRIB(glDisableVertexAttribArray, glt_client.enabled &= ~(1u << i))
ASYNC_HEAD(glVertexAttribDivisor) { c_glVertexAttribDivisor()((GLuint)c->a[0], (GLuint)c->a[1]); }
ASYNC_T(glVertexAttribDivisor) {
    gl_calls[GLI_glVertexAttribDivisor]++;
    u32 i = (u32)I(0), d = (u32)I(1);
    if (i < GLT_ATTRIBS) glt_client.divisor = d ? glt_client.divisor | 1u << i : glt_client.divisor & ~(1u << i);
    GltCmd *c = glt_begin(x_glVertexAttribDivisor, 0);
    c->a[0] = i;
    c->a[1] = d;
    glt_end(c);
}

static void x_client_vap(GltCmd *c) {
    c_glVertexAttribPointer()((GLuint)c->a[0], (GLint)c->a[1], (GLenum)c->a[2], (GLboolean)c->a[3], (GLsizei)c->a[4],
                              (const void *)(uintptr_t)c->a[5]);
}
static u32 glt_type_bytes(GLenum type, GLint size) {
    if (size < 1 || size > 4) return 0;
    switch (type) {
    case 0x1400: case 0x1401: return (u32)size;                                // BYTE, UNSIGNED_BYTE
    case 0x1402: case 0x1403: case 0x140B: return 2u * (u32)size;              // SHORT, UNSIGNED_SHORT, HALF_FLOAT
    case 0x1404: case 0x1405: case 0x1406: case 0x140C: return 4u * (u32)size; // INT, UINT, FLOAT, FIXED
    case 0x8D9F: case 0x8368: return 4;                                        // (UNSIGNED_)INT_2_10_10_10_REV
    default: return 0;
    }
}
// Tracé avec des tableaux côté client actifs : sommets [lo, hi] recopiés dans l'enregistrement, qui donne à
// OpenGL des pointeurs vers sa copie puis trace. Données : n entrées GltClientAttr puis les sommets.
typedef struct {
    u32 index, size, type, norm, stride, offset;
    int64_t base; // lo * stride : le pointeur donné à OpenGL est la copie moins ce décalage
} GltClientAttr;
static void x_client_draw(GltCmd *c) {
    u8 *data = glt_data(c);
    u32 n = (u32)c->a[8];
    const GltClientAttr *e = (const GltClientAttr *)data;
    for (u32 k = 0; k < n; k++)
        c_glVertexAttribPointer()(e[k].index, (GLint)e[k].size, e[k].type, (GLboolean)e[k].norm, (GLsizei)e[k].stride,
                                  (const void *)((uintptr_t)(data + e[k].offset) - (uintptr_t)e[k].base));
    switch (c->a[7]) {
    case 0: c_glDrawArrays()((GLenum)c->a[0], (GLint)c->a[1], (GLsizei)c->a[2]); break;
    case 1:
        c_glDrawRangeElements()((GLenum)c->a[0], (GLuint)c->a[1], (GLuint)c->a[2], (GLsizei)c->a[3], (GLenum)c->a[4],
                                (const void *)(uintptr_t)c->a[5]);
        break;
    default:
        c_glDrawRangeElementsBaseVertex()((GLenum)c->a[0], (GLuint)c->a[1], (GLuint)c->a[2], (GLsizei)c->a[3],
                                          (GLenum)c->a[4], (const void *)(uintptr_t)c->a[5], (GLint)c->a[6]);
        break;
    }
}
// Enregistrement commencé (a[7], a[8] et données remplis, glt_end à faire), ou NULL : le tracé doit passer par
// le chemin synchrone (qui redonne d'abord à OpenGL les pointeurs du jeu). Sommets lo..hi (hi compris).
static GltCmd *glt_client_draw(int64_t lo, int64_t hi, u64 kind) {
    u32 active = glt_client.mask & glt_client.enabled;
    if ((glt_client.divisor & active) || lo < 0 || hi < lo) return NULL;
    GltClientAttr e[GLT_ATTRIBS];
    u32 n = 0;
    u64 bytes = 0;
    for (u32 m = active; m; m &= m - 1) {
        u32 i = (u32)__builtin_ctz(m);
        u32 elem = glt_type_bytes(glt_client.a[i].type, glt_client.a[i].size);
        if (!elem || !glt_client.a[i].ptr || glt_client.a[i].stride < 0) return NULL;
        u32 stride = glt_client.a[i].stride ? (u32)glt_client.a[i].stride : elem;
        e[n] = (GltClientAttr){i, (u32)glt_client.a[i].size, glt_client.a[i].type, glt_client.a[i].norm, stride, 0,
                               lo * (int64_t)stride};
        bytes += (u64)(hi - lo) * stride + elem + 16;
        n++;
        if (bytes > (4u << 20)) return NULL;
    }
    u64 at = ((u64)n * sizeof(GltClientAttr) + 15) & ~15ull;
    GltCmd *c = glt_begin(x_client_draw, (u32)(at + bytes));
    u8 *data = glt_data(c);
    for (u32 k = 0; k < n; k++) {
        u32 i = e[k].index;
        u32 elem = glt_type_bytes(glt_client.a[i].type, glt_client.a[i].size);
        u64 len = (u64)(hi - lo) * e[k].stride + elem;
        e[k].offset = (u32)at;
        memcpy(data + at, glt_client.a[i].ptr + lo * (int64_t)e[k].stride, (size_t)len);
        at = (at + len + 15) & ~15ull;
    }
    memcpy(data, e, n * sizeof(GltClientAttr));
    c->a[7] = kind;
    c->a[8] = n;
    glt_client.stale |= active; // OpenGL pointe maintenant vers la copie
    return c;
}

ASYNC_HEAD(glDrawArrays) { c_glDrawArrays()((GLenum)c->a[0], (GLint)c->a[1], (GLsizei)c->a[2]); }
ASYNC_T(glDrawArrays) {
    GLint first = (GLint)I(1);
    GLsizei count = (GLsizei)I(2);
    GltCmd *c;
    if (glt_client.mask & glt_client.enabled) {
        if (count <= 0) return; // rien à tracer
        c = glt_client_draw(first, (int64_t)first + count - 1, 0);
        if (!c) {
            glt_sync(w_glDrawArrays, ctx);
            return;
        }
    } else
        c = glt_begin(x_glDrawArrays, 0);
    gl_calls[GLI_glDrawArrays]++;
    c->a[0] = I(0);
    c->a[1] = (u64)(u32)first;
    c->a[2] = (u64)(u32)count;
    glt_end(c);
}

// Pointeurs qui sont des décalages dans le tampon lié ; sans tampon lié, tableau côté client suivi (ci-dessus).
ASYNC_HEAD(glVertexAttribPointer) {
    c_glVertexAttribPointer()((GLuint)c->a[0], (GLint)c->a[1], (GLenum)c->a[2], (GLboolean)c->a[3], (GLsizei)c->a[4],
                              (const void *)(uintptr_t)c->a[5]);
}
ASYNC_T(glVertexAttribPointer) {
    u32 index = (u32)I(0);
    if (!glt.array_binding && index < GLT_ATTRIBS) {
        gl_calls[GLI_glVertexAttribPointer]++;
        glt_client.a[index].size = (GLint)I(1);
        glt_client.a[index].type = (GLenum)I(2);
        glt_client.a[index].norm = (GLboolean)I(3);
        glt_client.a[index].stride = (GLsizei)I(4);
        glt_client.a[index].ptr = P(5);
        glt_client.mask |= 1u << index;
        glt_client.stale |= 1u << index;
        return;
    }
    if (!glt.array_binding) {
        glt_sync(w_glVertexAttribPointer, ctx);
        return;
    }
    if (index < GLT_ATTRIBS) {
        glt_client.mask &= ~(1u << index);
        glt_client.stale &= ~(1u << index);
    }
    gl_calls[GLI_glVertexAttribPointer]++;
    GltCmd *c = glt_begin(x_glVertexAttribPointer, 0);
    for (int i = 0; i < 5; i++) c->a[i] = I(i);
    c->a[5] = (u64)(uintptr_t)P(5);
    glt_end(c);
}
ASYNC_HEAD(glDrawElements) {
    c_glDrawElements()((GLenum)c->a[0], (GLsizei)c->a[1], (GLenum)c->a[2], (const void *)(uintptr_t)c->a[3]);
}
ASYNC_T(glDrawElements) {
    if (!glt.element_binding || (glt_client.mask & glt_client.enabled)) {
        glt_sync(w_glDrawElements, ctx);
        return;
    }
    gl_calls[GLI_glDrawElements]++;
    GltCmd *c = glt_begin(x_glDrawElements, 0);
    for (int i = 0; i < 3; i++) c->a[i] = I(i);
    c->a[3] = (u64)(uintptr_t)P(3);
    glt_end(c);
}
ASYNC_HEAD(glDrawRangeElements) {
    c_glDrawRangeElements()((GLenum)c->a[0], (GLuint)c->a[1], (GLuint)c->a[2], (GLsizei)c->a[3], (GLenum)c->a[4],
                            (const void *)(uintptr_t)c->a[5]);
}
ASYNC_T(glDrawRangeElements) {
    if (!glt.element_binding) {
        glt_sync(w_glDrawRangeElements, ctx);
        return;
    }
    GltCmd *c;
    if (glt_client.mask & glt_client.enabled) {
        int64_t bv = 0;
        c = glt_client_draw((int64_t)(GLuint)I(1) + bv, (int64_t)(GLuint)I(2) + bv, 1);
        if (!c) {
            glt_sync(w_glDrawRangeElements, ctx);
            return;
        }
    } else
        c = glt_begin(x_glDrawRangeElements, 0);
    gl_calls[GLI_glDrawRangeElements]++;
    for (int i = 0; i < 5; i++) c->a[i] = I(i);
    c->a[5] = (u64)(uintptr_t)P(5);
    glt_end(c);
}
ASYNC_HEAD(glDrawRangeElementsBaseVertex) {
    c_glDrawRangeElementsBaseVertex()((GLenum)c->a[0], (GLuint)c->a[1], (GLuint)c->a[2], (GLsizei)c->a[3], (GLenum)c->a[4],
                                      (const void *)(uintptr_t)c->a[5], (GLint)c->a[6]);
}
ASYNC_T(glDrawRangeElementsBaseVertex) {
    if (!glt.element_binding) {
        glt_sync(w_glDrawRangeElementsBaseVertex, ctx);
        return;
    }
    GltCmd *c;
    if (glt_client.mask & glt_client.enabled) {
        int64_t bv = (int64_t)(GLint)I(6);
        c = glt_client_draw((int64_t)(GLuint)I(1) + bv, (int64_t)(GLuint)I(2) + bv, 2);
        if (!c) {
            glt_sync(w_glDrawRangeElementsBaseVertex, ctx);
            return;
        }
    } else
        c = glt_begin(x_glDrawRangeElementsBaseVertex, 0);
    gl_calls[GLI_glDrawRangeElementsBaseVertex]++;
    for (int i = 0; i < 5; i++) c->a[i] = I(i);
    c->a[5] = (u64)(uintptr_t)P(5);
    c->a[6] = I(6);
    glt_end(c);
}

// Envois de textures : pixels recopiés selon les réglages de glPixelStorei (suivis côté jeu). Tampon de
// pixels lié (GL_PIXEL_UNPACK_BUFFER), format inconnu ou image démesurée : synchrone.
static u32 pixel_bytes(GLenum format, GLenum type);
static struct {
    GLint align, row, skip_rows, skip_pixels;
} glt_unpack = {4, 0, 0, 0};
ASYNC_HEAD(glPixelStorei) { c_glPixelStorei()((GLenum)c->a[0], (GLint)c->a[1]); }
ASYNC_T(glPixelStorei) {
    gl_calls[GLI_glPixelStorei]++;
    GLenum pname = (GLenum)I(0);
    GLint v = (GLint)I(1);
    if (pname == 0xCF5) glt_unpack.align = v;
    if (pname == 0xCF2) glt_unpack.row = v;
    if (pname == 0xCF3) glt_unpack.skip_rows = v;
    if (pname == 0xCF4) glt_unpack.skip_pixels = v;
    GltCmd *c = glt_begin(x_glPixelStorei, 0);
    c->a[0] = pname;
    c->a[1] = (u64)(i64)v;
    glt_end(c);
}
// Octets lus par le pilote pour une image w × h (0 : inconnu).
static u64 glt_image_bytes(GLsizei w, GLsizei h, GLenum format, GLenum type) {
    u32 bpp = pixel_bytes(format, type);
    if (!bpp || w <= 0 || h <= 0) return 0;
    u64 stride = (u64)(glt_unpack.row > 0 ? glt_unpack.row : w) * bpp;
    u64 align = glt_unpack.align > 1 ? (u64)glt_unpack.align : 1;
    stride = (stride + align - 1) / align * align;
    return ((u64)glt_unpack.skip_rows + (u64)h - 1) * stride + ((u64)glt_unpack.skip_pixels + (u64)w) * bpp;
}
enum { GLT_MAX_IMAGE = 12u << 20 };
ASYNC_HEAD(glTexSubImage2D) {
    c_glTexSubImage2D()((GLenum)c->a[0], (GLint)c->a[1], (GLint)c->a[2], (GLint)c->a[3], (GLsizei)c->a[4], (GLsizei)c->a[5],
                        (GLenum)c->a[6], (GLenum)c->a[7], glt_data(c));
}
ASYNC_T(glTexSubImage2D) {
    const void *pixels = P(8);
    u64 bytes = glt_unpack_buffer || !pixels ? 0 : glt_image_bytes((GLsizei)I(4), (GLsizei)I(5), (GLenum)I(6), (GLenum)I(7));
    if (!bytes || bytes > GLT_MAX_IMAGE) {
        glt_sync(w_glTexSubImage2D, ctx);
        return;
    }
    GltCmd *c = glt_begin(x_glTexSubImage2D, (u32)bytes);
    for (int i = 0; i < 8; i++) c->a[i] = I(i);
    memcpy(glt_data(c), pixels, (size_t)bytes);
    glt_end(c);
}
ASYNC_HEAD(glTexImage2D) {
    c_glTexImage2D()((GLenum)c->a[0], (GLint)c->a[1], (GLint)c->a[2], (GLsizei)c->a[3], (GLsizei)c->a[4], (GLint)c->a[5],
                     (GLenum)c->a[6], (GLenum)c->a[7], c->a[8] ? glt_data(c) : NULL);
}
ASYNC_T(glTexImage2D) {
    gl_calls[GLI_glTexImage2D]++;
    const void *pixels = P(8);
    u64 bytes = 0;
    if (pixels) {
        bytes = glt_unpack_buffer ? 0 : glt_image_bytes((GLsizei)I(3), (GLsizei)I(4), (GLenum)I(6), (GLenum)I(7));
        if (!bytes || bytes > GLT_MAX_IMAGE) {
            glt_sync(w_glTexImage2D, ctx);
            return;
        }
    }
    GltCmd *c = glt_begin(x_glTexImage2D, (u32)bytes);
    for (int i = 0; i < 8; i++) c->a[i] = I(i);
    c->a[8] = pixels != NULL;
    if (bytes) memcpy(glt_data(c), pixels, (size_t)bytes);
    glt_end(c);
}

// Relais donné au jeu en mode thread : version en file s'il y en a une, sinon appel synchrone.
#define SYNC_WRAP(kind, ret, name, params, args) \
    static void s_##name(RcnCtx *ctx) { glt_sync(w_##name, ctx); }
GL_FUNCS(SYNC_WRAP)
static const struct {
    const char *name;
    RcnFn fn;
} glt_async[] = {
#define AS(name) {#name, t_##name},
    AS(glActiveTexture) AS(glBindTexture) AS(glBlendColor) AS(glBlendEquationSeparate) AS(glBlendFuncSeparate) AS(glClear)
    AS(glClearColor) AS(glClearDepthf) AS(glClearStencil) AS(glColorMask) AS(glCullFace) AS(glDepthFunc) AS(glDepthMask)
    AS(glDepthRangef) AS(glDisable) AS(glEnable) AS(glFramebufferTexture2D) AS(glFrontFace) AS(glPolygonOffset) AS(glScissor)
    AS(glStencilFunc) AS(glStencilFuncSeparate) AS(glStencilMask) AS(glStencilOp) AS(glStencilOpSeparate) AS(glTexParameteri)
    AS(glUniform1i) AS(glUniform1f) AS(glUseProgram) AS(glVertexAttribDivisor) AS(glViewport) AS(glEnableVertexAttribArray)
    AS(glDisableVertexAttribArray) AS(glDrawArrays) AS(glBindFramebuffer) AS(glBindBuffer) AS(glDeleteBuffers)
    AS(glUniform4fv) AS(glUniform1iv) AS(glUniform4iv) AS(glBufferSubData) AS(glBufferData) AS(glVertexAttribPointer)
    AS(glDrawElements) AS(glDrawRangeElements) AS(glDrawRangeElementsBaseVertex) AS(glPixelStorei) AS(glTexSubImage2D)
    AS(glTexImage2D)
#undef AS
};
#define SYNC_ENTRY(kind, ret, name, params, args) s_##name,
static const RcnFn glt_sync_wrappers[] = {GL_FUNCS(SYNC_ENTRY)};

static RcnFn glt_wrapper(size_t index) {
    for (size_t i = 0; i < sizeof glt_async / sizeof glt_async[0]; i++)
        if (strcmp(glt_async[i].name, gl_table[index].name) == 0) return glt_async[i].fn;
    return glt_sync_wrappers[index];
}

// Cœurs du processeur rangés par fréquence maximale : `fastest` reçoit ceux de la plus haute, `next` ceux
// du niveau suivant (téléphones : 1 cœur « prime », 3 « gold », 4 petits). 0 si inconnu.
static int cpu_tiers(cpu_set_t *fastest, cpu_set_t *next) {
    u32 freq[64] = {0}, top = 0, second = 0;
    int count = 0;
    for (int i = 0; i < 64; i++) {
        char path[96];
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (fscanf(f, "%u", &freq[i]) == 1) count++;
        fclose(f);
        if (freq[i] > top) top = freq[i];
    }
    for (int i = 0; i < 64; i++)
        if (freq[i] < top && freq[i] > second) second = freq[i];
    if (count < 2 || !second) return 0;
    CPU_ZERO(fastest);
    CPU_ZERO(next);
    for (int i = 0; i < 64; i++) {
        if (freq[i] == top) CPU_SET(i, fastest);
        if (freq[i] == second) CPU_SET(i, next);
    }
    return 1;
}
// Thread du jeu (celui qui crée le contexte) sur le cœur le plus rapide, thread de rendu sur les suivants :
// RCN_CPU_PIN=1 seulement : en mesure, le chargement et la vidéo devenaient très lents une fois les threads placés,
// et le combat tient 60 images/s sans.
static cpu_set_t default_cpus;
static int default_saved;
void rcn_thread_default_affinity(void) {
    if (default_saved) sched_setaffinity(0, sizeof default_cpus, &default_cpus);
    rcn_big_cores();
}
// RCN_BIG_CORES=1 : threads du jeu sur les grands cœurs (les deux niveaux les plus rapides, jamais les petits).
// L'ordonnanceur d'Android met un thread qui attend souvent (passages de relais entre threads) sur un petit
// cœur, où chaque relais coûte bien plus (Skyrim : thread principal mesuré sur un Cortex-A55).
void rcn_big_cores(void) {
    static int state = -1; // -1 : à décider ; 0 : non ; 1 : oui
    static cpu_set_t big;
    if (state < 0) {
        const char *v = getenv("RCN_BIG_CORES");
        cpu_set_t fastest, next;
        state = v && strcmp(v, "1") == 0 && cpu_tiers(&fastest, &next);
        if (state) {
            CPU_OR(&big, &fastest, &next);
            fprintf(stderr, "rcn : threads du jeu sur les grands coeurs (%d)\n", CPU_COUNT(&big));
        }
    }
    if (state) sched_setaffinity(0, sizeof big, &big);
}
static void pin_threads(void) {
    const char *v = getenv("RCN_CPU_PIN");
    if (!v || strcmp(v, "1") != 0) return;
    default_saved = sched_getaffinity(0, sizeof default_cpus, &default_cpus) == 0;
    cpu_set_t fastest;
    if (!cpu_tiers(&fastest, &render_cpus)) {
        fprintf(stderr, "rcn : fréquences des cœurs illisibles, threads non placés\n");
        return;
    }
    // Cœurs interdits au processus (processus lancé par adb sous MIUI : cœurs 0-5) : le niveau suivant, et
    // le thread de rendu laissé à l'ordonnanceur.
    if (sched_setaffinity(0, sizeof fastest, &fastest) == 0) {
        fprintf(stderr, "rcn : thread du jeu sur le coeur le plus rapide, rendu sur les suivants\n");
        render_pinned = 1;
    } else if (sched_setaffinity(0, sizeof render_cpus, &render_cpus) == 0) {
        fprintf(stderr, "rcn : coeur le plus rapide interdit (%d), thread du jeu sur les suivants\n", errno);
    }
}

// Démarrage (avant la création du contexte, qui se fera sur le thread de rendu).
int rcn_sdlgl_thread_start(void) {
    if (glt.on) return 1;
    pin_threads();
    const char *v = getenv("RCN_GL_THREAD");
    if (v && strcmp(v, "0") == 0) return 0; // actif par défaut (RCN_GL_THREAD=0 : OpenGL sur le thread du jeu)
    glt.ring = malloc(GLT_RING);
    if (!glt.ring) return 0;
    glt.on = 1;
    if (pthread_create(&glt.thread, NULL, glt_main, NULL) != 0) {
        glt.on = 0;
        return 0;
    }
    fprintf(stderr, "rcn : OpenGL sur un thread de rendu\n");
    return 1;
}
// Appel synchrone sur le thread de rendu (fonctions SDL qui touchent à OpenGL).
void rcn_sdlgl_run(RcnFn fn, RcnCtx *ctx) { glt_sync(fn, ctx); }
// Changement d'image mis en file : `fn(ctx)` sur le thread de rendu. Le jeu attend d'abord que le
// changement précédent soit fait (une image d'avance au plus).
static void x_swap(GltCmd *c) { ((RcnFn)(uintptr_t)c->a[0])((RcnCtx *)(uintptr_t)c->a[1]); }
int rcn_sdlgl_post_swap(RcnFn fn, RcnCtx *ctx) {
    if (!glt.on) return 0;
    u64 before = glt_wait_ns;
    glt_wait(glt.last_swap_end);
    glt_swap_wait_ns += glt_wait_ns - before;
    GltCmd *c = glt_begin(x_swap, 0);
    c->a[0] = (u64)(uintptr_t)fn;
    c->a[1] = (u64)(uintptr_t)ctx;
    glt.last_swap_end = glt_end(c);
    return 1;
}
// Redimensionnement de l'image du jeu (appelé du thread du jeu) : mis en file.
static void resize_now(u32 width, u32 height);
static void x_resize(GltCmd *c) { resize_now((u32)c->a[0], (u32)c->a[1]); }
static int glt_post_resize(u32 width, u32 height) {
    if (!glt.on || glt_is_render_thread()) return 0;
    GltCmd *c = glt_begin(x_resize, 0);
    c->a[0] = width;
    c->a[1] = height;
    glt_end(c);
    return 1;
}
void rcn_sdlgl_debug(void) {
    if (!glt.on) return;
    fprintf(stderr, "thread de rendu : file %llu / %llu, rendu endormi %d, jeu en attente %d, dernier changement d'image %llu\n",
            (unsigned long long)atomic_load(&glt.tail), (unsigned long long)atomic_load(&glt.head), atomic_load(&glt.consumer_waiting),
            atomic_load(&glt.producer_waiting), (unsigned long long)glt.last_swap_end);
    fprintf(stderr, "  attente de %llu, dernier enregistrement %llu octets à %llu\n", (unsigned long long)glt_wait_pos,
            (unsigned long long)glt_last_size, (unsigned long long)glt_last_at);
}
#else
void rcn_sdlgl_debug(void) {}
void rcn_thread_default_affinity(void) {}
void rcn_big_cores(void) {}
int rcn_sdlgl_thread_start(void) { return 0; }
void rcn_sdlgl_run(RcnFn fn, RcnCtx *ctx) { fn(ctx); }
int rcn_sdlgl_post_swap(RcnFn fn, RcnCtx *ctx) {
    (void)fn, (void)ctx;
    return 0;
}
#endif

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
// glGetBufferSubData : absente d'OpenGL ES, lue par une projection en lecture.
static void e_glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void *data) {
    if (h_glGetBufferSubData && !gl.es) {
        h_glGetBufferSubData(target, offset, size, data);
        return;
    }
    void *p = size > 0 ? h_glMapBufferRange(target, offset, size, GL_MAP_READ_BIT) : NULL;
    if (!p) return;
    memcpy(data, p, (size_t)size);
    h_glUnmapBuffer(target);
}
// Appels de dessin aberrants (compte négatif ou démesuré) : notés et ignorés.
static void e_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (count < 0 || count > 16000000 || first < 0) {
        static int told;
        if (told++ < 8) fprintf(stderr, "rcn : glDrawArrays(%u, %d, %d) ignore\n", mode, first, count);
        return;
    }
    static int log = -1; // getenv une fois, pas à chaque dessin
    if (log < 0) log = getenv("RCN_GL_LOG") != NULL;
    if (log) fprintf(stderr, "glDrawArrays(%u, %d, %d)\n", mode, first, count);
    h_glDrawArrays(mode, first, count);
}
// Version : celle d'OpenGL de bureau que le jeu attend (MonoGame lit « majeur.mineur » au début).
static GLstring e_glGetString(GLenum name) {
    if (!gl.es) return h_glGetString(name);
    switch (name) {
    // Sans « OpenGL ES » : FNA et MojoShader prendraient leur chemin ES (profil glsles), alors que les
    // nuanceurs de bureau sont convertis ici.
    case GL_VERSION: return (GLstring) "3.0 RomCloud";
    case GL_SHADING_LANGUAGE_VERSION: return (GLstring) "1.30";
    case GL_EXTENSIONS: return (GLstring)gl.extensions;
    default: return h_glGetString(name);
    }
}
// Envois de textures (RCN_GL_STATS) : comptés par image ; une image qui y passe plus de 2 ms est notée
// (rcn_sdlgl_frame_stats, à chaque fin d'image).
static int tex_stats = -1;
static u64 tex_calls, tex_bytes, tex_ns;
static GLsizei tex_max_w, tex_max_h;
static GLenum tex_max_format, tex_max_type;
// Octets par pixel des formats non compressés courants (0 : inconnu).
static u32 pixel_bytes(GLenum format, GLenum type) {
    u32 comps = format == 0x1908 || format == GL_BGRA ? 4 : format == 0x1907 ? 3 : format == 0x190A || format == 0x8227 ? 2
              : format == 0x1909 || format == 0x1906 || format == 0x1903 ? 1 : 0;
    if (type == GL_UNSIGNED_BYTE) return comps;
    if (type == 0x8033 || type == 0x8034 || type == 0x8363) return 2; // 4444, 5551, 565 : un mot par pixel
    if (type == 0x1406) return comps * 4;                              // GL_FLOAT
    if (type == 0x140B) return comps * 2;                              // GL_HALF_FLOAT
    return 0;
}
// Envoi d'une texture sous OpenGL ES : les pixels du jeu sont recopiés dans un tampon de l'hôte
// (GL_PIXEL_UNPACK_BUFFER) d'où le pilote les prend lui-même ; le pilote Adreno convertit sinon l'image sur
// le processeur, pendant l'appel (≈200 Mo/s). Petits envois et cas particuliers : envoi direct.
enum { GL_PIXEL_UNPACK_BUFFER = 0x88EC, GL_PIXEL_UNPACK_BUFFER_BINDING = 0x88EF, GL_UNPACK_ALIGNMENT = 0xCF5,
       GL_UNPACK_ROW_LENGTH = 0xCF2, GL_UNPACK_SKIP_ROWS = 0xCF3, GL_UNPACK_SKIP_PIXELS = 0xCF4, GL_STREAM_DRAW = 0x88E0 };
static int upload_through_buffer(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                                 GLenum type, const void *pixels) {
    static int enabled = -1;
    if (enabled < 0) enabled = !getenv("RCN_GL_NO_PBO");
    u32 bpp = pixel_bytes(format, type);
    // Images RGBA seulement : les plans de vidéo (1 octet par pixel) sont plus rapides en envoi direct.
    if (!enabled || !gl.es || !pixels || bpp != 4 || (u64)w * h * bpp < 256 * 1024) return 0;
    GLint bound = 0, align = 4, row = 0, skip_rows = 0, skip_pixels = 0;
    h_glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &bound);
    if (bound) return 0; // le jeu envoie déjà depuis un tampon
    h_glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    h_glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row);
    h_glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
    h_glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
    u64 stride = (u64)(row > 0 ? row : w) * bpp;
    if (align > 1) stride = (stride + (u64)align - 1) / (u64)align * (u64)align;
    u64 size = ((u64)skip_rows + (u64)h - 1) * stride + ((u64)skip_pixels + (u64)w) * bpp;
    static GLuint buffer;
    if (!buffer) h_glGenBuffers(1, &buffer);
    if (!buffer) return 0;
    h_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
    h_glBufferData(GL_PIXEL_UNPACK_BUFFER, (GLsizeiptr)size, pixels, GL_STREAM_DRAW); // nouveau stockage à chaque envoi
    h_glTexSubImage2D(target, level, x, y, w, h, format, type, (const void *)0);
    h_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    return 1;
}
static void e_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                              const void *pixels) {
    if (tex_stats < 0) tex_stats = getenv("RCN_GL_STATS") != NULL;
    if (!tex_stats) {
        if (!upload_through_buffer(target, level, x, y, w, h, format, type, pixels))
            h_glTexSubImage2D(target, level, x, y, w, h, format, type, pixels);
        return;
    }
    u64 t0 = rcn_now_ns();
    if (!upload_through_buffer(target, level, x, y, w, h, format, type, pixels))
        h_glTexSubImage2D(target, level, x, y, w, h, format, type, pixels);
    tex_ns += rcn_now_ns() - t0;
    tex_calls++, tex_bytes += (u64)w * (u64)h * 4;
    if ((u64)w * h > (u64)tex_max_w * tex_max_h) tex_max_w = w, tex_max_h = h, tex_max_format = format, tex_max_type = type;
}
static void dump_calls(u64 frame) {
    static int enabled = -1;
    if (enabled < 0) enabled = gl_calls_on = getenv("RCN_GL_CALLS") != NULL;
    if (!enabled || frame % 300) return;
    u64 total = 0;
    for (int i = 0; i < GLI_COUNT; i++) total += gl_calls[i];
    char line[1024];
    int n = snprintf(line, sizeof line, "rcn : image %llu, appels OpenGL par image : %.0f ;", (unsigned long long)frame, total / 300.0);
    for (int k = 0; k < 24 && n < (int)sizeof line - 64; k++) {
        int best = -1;
        for (int i = 0; i < GLI_COUNT; i++)
            if (gl_calls[i] && (best < 0 || gl_calls[i] > gl_calls[best])) best = i;
        if (best < 0) break;
        n += snprintf(line + n, sizeof line - (size_t)n, " %s %.1f", gl_table[best].name, gl_calls[best] / 300.0);
        gl_calls[best] = 0;
    }
    fprintf(stderr, "%s\n", line);
#if defined(__ANDROID__)
    if (glt.on) {
        fprintf(stderr, "rcn : thread de rendu : jeu en attente %.1f ms par image (dont changement d'image %.1f ms), %llu attentes\n",
                glt_wait_ns / 300.0 / 1e6, glt_swap_wait_ns / 300.0 / 1e6, (unsigned long long)glt_waits);
        glt_wait_ns = glt_swap_wait_ns = glt_waits = 0;
        for (int i = 0; i < 48 && glt_sync_stats[i].name; i++) {
            if (glt_sync_stats[i].n)
                fprintf(stderr, "rcn :   synchrone %s : %.1f par image, %.2f ms par image\n", glt_sync_stats[i].name,
                        glt_sync_stats[i].n / 300.0, glt_sync_stats[i].ns / 300.0 / 1e6);
            glt_sync_stats[i].n = glt_sync_stats[i].ns = 0;
        }
    }
#endif
    memset(gl_calls, 0, sizeof gl_calls);
}
void rcn_sdlgl_frame_stats(void) {
    static u64 frame;
    frame++;
    dump_calls(frame);
    if (tex_stats > 0 && tex_ns > 2000000)
        fprintf(stderr, "rcn : image %llu : glTexSubImage2D %llu appels, %.1f Mo, %.1f ms, plus grand %dx%d (format %x, type %x)\n",
                (unsigned long long)frame, (unsigned long long)tex_calls, tex_bytes / 1048576.0, tex_ns / 1e6, tex_max_w, tex_max_h,
                tex_max_format, tex_max_type);
    tex_calls = tex_bytes = tex_ns = 0, tex_max_w = tex_max_h = 0;
}
// Compilation d'un nuanceur ; en cas d'échec (les premières fois), journal du pilote et début du source.
enum { GL_COMPILE_STATUS = 0x8B81, GL_SHADER_SOURCE_LENGTH = 0x8B88 };
static void e_glCompileShader(GLuint shader) {
    h_glCompileShader(shader);
    static int told;
    GLint ok = 1;
    h_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok || told >= 4) return;
    told++;
    char log[2048] = {0};
    h_glGetShaderInfoLog(shader, sizeof log - 1, NULL, log);
    fprintf(stderr, "rcn : nuanceur %u refusé par le pilote :\n%s\n", shader, log);
    if (h_glGetShaderSource) {
        char src[1500] = {0};
        h_glGetShaderSource(shader, sizeof src - 1, NULL, src);
        fprintf(stderr, "--- source ---\n%s\n---\n", src);
    }
}
// Variables du pipeline fixe de GLSL de bureau (MojoShader : gl_FrontColor, gl_TexCoord[]…) absentes de
// GLSL ES : remplacées par des « varying » déclarés de même dans les deux nuanceurs. Remplacement par mot
// entier ; rend une copie (à libérer), ou NULL si rien n'a changé.
static char *replace_word(const char *text, const char *word, const char *by) {
    size_t wn = strlen(word), bn = strlen(by), count = 0;
    for (const char *p = text; (p = strstr(p, word)); p += wn) count++;
    if (!count) return NULL;
    char *out = malloc(strlen(text) + count * (bn > wn ? bn - wn : 0) + 1), *o = out;
    if (!out) return NULL;
    for (const char *p = text;;) {
        const char *q = strstr(p, word);
        if (!q) {
            strcpy(o, p);
            break;
        }
        int before = q > text && (isalnum((unsigned char)q[-1]) || q[-1] == '_');
        int after = isalnum((unsigned char)q[wn]) || q[wn] == '_';
        memcpy(o, p, (size_t)(q - p));
        o += q - p;
        if (before || after) {
            memcpy(o, q, wn), o += wn;
        } else {
            memcpy(o, by, bn), o += bn;
        }
        p = q + wn;
    }
    return out;
}
static char *fixed_function_varyings(char *text, int *used) {
    static const char *const pairs[][2] = {
        {"gl_FrontColor", "rcn_Color"}, {"gl_FrontSecondaryColor", "rcn_SecondaryColor"}, {"gl_Color", "rcn_Color"},
        {"gl_SecondaryColor", "rcn_SecondaryColor"}, {"gl_TexCoord", "rcn_TexCoord"},
    };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        char *r = replace_word(text, pairs[i][0], pairs[i][1]);
        if (r) {
            free(text);
            text = r;
            *used = 1;
        }
    }
    return text;
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
    int used = 0;
    char *converted = fixed_function_varyings(strdup(body), &used);
    if (!converted) {
        free(text);
        free(out);
        return;
    }
    free(out);
    size_t size = strlen(converted) + 256;
    out = malloc(size);
    if (out) {
        snprintf(out, size, "precision highp float;\nprecision mediump int;\n%s%s",
                 used ? "varying vec4 rcn_Color;\nvarying vec4 rcn_SecondaryColor;\nvarying vec4 rcn_TexCoord[8];\n" : "", converted);
        const GLchar *list[1] = {out};
        h_glShaderSource(shader, 1, list, NULL);
    }
    free(converted);
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

// Extension annoncée au jeu (SDL_GL_ExtensionSupported) : liste construite (OpenGL ES) ou du pilote.
static int has_word(const char *list, const char *word) {
    size_t n = strlen(word);
    for (const char *p = list; p && (p = strstr(p, word)); p += n) {
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0)) return 1;
    }
    return 0;
}
int rcn_sdlgl_extension(const char *name) {
    if (!name || !*name) return 0;
    if (gl.es) return has_word(gl.extensions, name);
    GLint count = 0;
    h_glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count && h_glGetStringi; i++) {
        const char *e = (const char *)h_glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (e && strcmp(e, name) == 0) return 1;
    }
    return has_word((const char *)h_glGetString(GL_EXTENSIONS), name);
}

int rcn_sdlgl_load(void *(*get_proc)(const char *name), int es) {
    gl.es = es;
    for (size_t i = 0; i < GL_COUNT; i++) *gl_table[i].host = get_proc(gl_table[i].name);
    if (!h_glBindFramebuffer || !h_glGenFramebuffers || !h_glGetIntegerv || !h_glGetString || !h_glShaderSource) return 0;
    if (es) build_extensions();
    return 1;
}

static void resize_now(u32 width, u32 height);
void rcn_sdlgl_resize(u32 width, u32 height) {
#if defined(__ANDROID__)
    if (glt_post_resize(width, height)) return;
#endif
    resize_now(width, height);
}
static void resize_now(u32 width, u32 height) {
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
        RcnFn wrapper = e->wrapper;
#if defined(__ANDROID__)
        if (glt.on) wrapper = glt_wrapper(i);
#endif
        if (!e->thunk) e->thunk = rcn_thunk(ctx, "opengl32.dll", e->name, wrapper, 0);
        u64 thunk = e->thunk;
        pthread_mutex_unlock(&gl.lock);
        return thunk;
    }
    return 0;
}

