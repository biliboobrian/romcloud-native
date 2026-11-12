// RomCloud Native : rendu Direct3D 7 par le processeur graphique.
//
// Les primitives que rcn_d3d.c a préparées (sommets en points de l'image du jeu, état de rendu) sont
// dessinées par OpenGL : un seul programme de nuanceurs reproduit l'étage de texture 0, le test
// d'opacité et la couleur transparente ; les mélanges sont ceux du GPU. Les textures sont les surfaces
// DirectDraw du jeu, recopiées vers le GPU quand leur contenu a changé.
//
// Sous Android, le rendu va droit dans la fenêtre de l'application (EGL), à sa résolution : l'image du
// jeu (640 × 480 par exemple) est agrandie géométriquement, pas point par point.
// Sous Windows, pour les essais (RCN_GL=1), il va dans une image hors écran relue et remise à l'hôte
// comme une image du rendu logiciel, ce qui permet de comparer les deux rendus.
//
// Limite : ce que le jeu écrit dans la cible sans passer par Direct3D (copies DirectDraw, Lock, texte
// gdi32) n'apparaît pas. Si l'initialisation échoue, le rendu logiciel reprend.
#include <stdio.h>
#include <stdlib.h>

#include "rcn_gl.h"

#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
#define RCN_GL_AVAILABLE 1
#define SHADER_HEADER "#version 300 es\nprecision mediump float;\nprecision mediump int;\n"

#elif defined(_WIN32)
#define RCN_GL_AVAILABLE 1
#define SHADER_HEADER "#version 330\n"
// OpenGL sans les en-têtes de Windows (leurs noms entreraient en conflit avec ceux du runtime).
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLboolean;
typedef ptrdiff_t GLsizeiptr;
enum {
    GL_TRIANGLES = 4, GL_TRIANGLE_STRIP = 5, GL_TRIANGLE_FAN = 6, GL_POINTS = 0, GL_LINES = 1, GL_LINE_STRIP = 3,
    GL_ZERO = 0, GL_ONE = 1, GL_SRC_COLOR = 0x300, GL_ONE_MINUS_SRC_COLOR = 0x301, GL_SRC_ALPHA = 0x302,
    GL_ONE_MINUS_SRC_ALPHA = 0x303, GL_DST_ALPHA = 0x304, GL_ONE_MINUS_DST_ALPHA = 0x305, GL_DST_COLOR = 0x306,
    GL_ONE_MINUS_DST_COLOR = 0x307, GL_BLEND = 0xBE2, GL_SCISSOR_TEST = 0xC11, GL_CULL_FACE = 0xB44,
    GL_DEPTH_TEST = 0xB71, GL_FRONT = 0x404, GL_BACK = 0x405, GL_CCW = 0x901, GL_TEXTURE_2D = 0xDE1,
    GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401, GL_FLOAT = 0x1406, GL_NEAREST = 0x2600, GL_LINEAR = 0x2601,
    GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_WRAP_S = 0x2802,
    GL_TEXTURE_WRAP_T = 0x2803, GL_REPEAT = 0x2901, GL_CLAMP_TO_EDGE = 0x812F, GL_COLOR_BUFFER_BIT = 0x4000,
    GL_ARRAY_BUFFER = 0x8892, GL_STREAM_DRAW = 0x88E0, GL_FRAGMENT_SHADER = 0x8B30, GL_VERTEX_SHADER = 0x8B31,
    GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82, GL_TEXTURE0 = 0x84C0, GL_FRAMEBUFFER = 0x8D40,
    GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_FRAMEBUFFER_COMPLETE = 0x8CD5, GL_UNPACK_ALIGNMENT = 0xCF5,
    GL_PACK_ALIGNMENT = 0xD05, GL_RGBA8 = 0x8058,
};
#define GL11(ret, name, args) __declspec(dllimport) ret __stdcall name args;
GL11(void, glClear, (GLbitfield))
GL11(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))
GL11(void, glViewport, (GLint, GLint, GLsizei, GLsizei))
GL11(void, glScissor, (GLint, GLint, GLsizei, GLsizei))
GL11(void, glEnable, (GLenum))
GL11(void, glDisable, (GLenum))
GL11(void, glBlendFunc, (GLenum, GLenum))
GL11(void, glCullFace, (GLenum))
GL11(void, glFrontFace, (GLenum))
GL11(void, glLineWidth, (GLfloat))
GL11(void, glGenTextures, (GLsizei, GLuint *))
GL11(void, glDeleteTextures, (GLsizei, const GLuint *))
GL11(void, glBindTexture, (GLenum, GLuint))
GL11(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))
GL11(void, glTexParameteri, (GLenum, GLenum, GLint))
GL11(void, glDrawArrays, (GLenum, GLint, GLsizei))
GL11(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))
GL11(void, glPixelStorei, (GLenum, GLint))
// Fonctions d'OpenGL 2 et 3 : obtenues du pilote à l'initialisation.
#define GL_LATE(X)                                                                         \
    X(GLuint, glCreateShader, (GLenum))                                                    \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))       \
    X(void, glCompileShader, (GLuint))                                                     \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint *))                                      \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                    \
    X(GLuint, glCreateProgram, (void))                                                     \
    X(void, glAttachShader, (GLuint, GLuint))                                              \
    X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *))                        \
    X(void, glLinkProgram, (GLuint))                                                       \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint *))                                     \
    X(void, glUseProgram, (GLuint))                                                        \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar *))                               \
    X(void, glUniform1i, (GLint, GLint))                                                   \
    X(void, glUniform1f, (GLint, GLfloat))                                                 \
    X(void, glUniform2f, (GLint, GLfloat, GLfloat))                                        \
    X(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                      \
    X(void, glGenBuffers, (GLsizei, GLuint *))                                             \
    X(void, glBindBuffer, (GLenum, GLuint))                                                \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))                      \
    X(void, glEnableVertexAttribArray, (GLuint))                                           \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, glActiveTexture, (GLenum))                                                     \
    X(void, glGenFramebuffers, (GLsizei, GLuint *))                                        \
    X(void, glBindFramebuffer, (GLenum, GLuint))                                           \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))               \
    X(GLenum, glCheckFramebufferStatus, (GLenum))                                          \
    X(void, glGenVertexArrays, (GLsizei, GLuint *))                                        \
    X(void, glBindVertexArray, (GLuint))
#define DECLARE(ret, name, args) static ret(__stdcall *name) args;
GL_LATE(DECLARE)
__declspec(dllimport) void *__stdcall wglCreateContext(void *dc);
__declspec(dllimport) int __stdcall wglMakeCurrent(void *dc, void *context);
__declspec(dllimport) void *__stdcall wglGetProcAddress(const char *name);
__declspec(dllimport) void *__stdcall CreateWindowExA(unsigned long ex, const char *cls, const char *name, unsigned long style,
                                                        int x, int y, int w, int h, void *parent, void *menu, void *instance, void *param);
__declspec(dllimport) void *__stdcall GetDC(void *window);
__declspec(dllimport) int __stdcall ChoosePixelFormat(void *dc, const void *descriptor);
__declspec(dllimport) int __stdcall SetPixelFormat(void *dc, int format, const void *descriptor);

#else
#define RCN_GL_AVAILABLE 0
#endif

#if !RCN_GL_AVAILABLE

int rcn_gl_begin(RcnCtx *ctx, u32 width, u32 height) {
    (void)ctx;
    (void)width;
    (void)height;
    return RCN_GL_NONE;
}
void rcn_gl_clear(const i32 rect[4], u32 color) {
    (void)rect;
    (void)color;
}
void rcn_gl_draw(u32 type, const RcnVertex *vertices, u32 count, const RcnGlState *state) {
    (void)type;
    (void)vertices;
    (void)count;
    (void)state;
}
int rcn_gl_present(RcnCtx *ctx, int wait) {
    (void)ctx;
    (void)wait;
    return 0;
}
void rcn_gl_forget(u32 surface) { (void)surface; }

#else

static const char VERTEX_SHADER[] = SHADER_HEADER
    "in vec2 a_position;\n"
    "in vec4 a_color;\n"
    "in vec2 a_uv;\n"
    "uniform vec2 u_size;\n"
    "out vec4 v_color;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_color = a_color / 255.0;\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = vec4(a_position.x / u_size.x * 2.0 - 1.0, 1.0 - a_position.y / u_size.y * 2.0, 0.0, 1.0);\n"
    "}\n";

// Étage de texture 0 de Direct3D : opérations et arguments numérotés comme D3DTOP_* et D3DTA_*.
static const char FRAGMENT_SHADER[] = SHADER_HEADER
    "in vec4 v_color;\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_texture;\n"
    "uniform int u_has_texture, u_key, u_color_op, u_color_arg1, u_color_arg2, u_alpha_op, u_alpha_arg1, u_alpha_arg2;\n"
    "uniform int u_alpha_test, u_alpha_func;\n"
    "uniform float u_alpha_ref;\n"
    "uniform vec4 u_factor;\n"
    "out vec4 o_color;\n"
    "vec4 argument(int a, vec4 texel) {\n"
    "    if (a == 2) return texel;\n"
    "    if (a == 3) return u_factor;\n"
    "    return v_color;\n"
    "}\n"
    "vec4 combine(int op, vec4 a, vec4 b) {\n"
    "    if (op == 3) return b;\n"
    "    if (op == 4) return a * b;\n"
    "    if (op == 5) return min(a * b * 2.0, 1.0);\n"
    "    if (op == 6) return min(a * b * 4.0, 1.0);\n"
    "    if (op == 7) return min(a + b, 1.0);\n"
    "    return a;\n"
    "}\n"
    "void main() {\n"
    "    vec4 texel = vec4(1.0);\n"
    "    if (u_has_texture == 1) {\n"
    "        texel = texture(u_texture, v_uv);\n"
    "        if (u_key == 1 && texel.a == 0.0) discard;\n"
    "    }\n"
    "    vec4 c = v_color;\n"
    "    if (u_color_op != 1) {\n"
    "        c.rgb = combine(u_color_op, argument(u_color_arg1, texel), argument(u_color_arg2, texel)).rgb;\n"
    "        if (u_alpha_op != 1) c.a = combine(u_alpha_op, argument(u_alpha_arg1, texel), argument(u_alpha_arg2, texel)).a;\n"
    "    }\n"
    "    if (u_alpha_test == 1) {\n"
    "        float a = floor(c.a * 255.0 + 0.5);\n"
    "        bool pass = true;\n"
    "        if (u_alpha_func == 1) pass = false;\n"
    "        else if (u_alpha_func == 2) pass = a < u_alpha_ref;\n"
    "        else if (u_alpha_func == 3) pass = a == u_alpha_ref;\n"
    "        else if (u_alpha_func == 4) pass = a <= u_alpha_ref;\n"
    "        else if (u_alpha_func == 5) pass = a > u_alpha_ref;\n"
    "        else if (u_alpha_func == 6) pass = a != u_alpha_ref;\n"
    "        else if (u_alpha_func == 7) pass = a >= u_alpha_ref;\n"
    "        if (!pass) discard;\n"
    "    }\n"
    "    o_color = c;\n"
    "}\n";

enum { STATE_UNTRIED, STATE_READY, STATE_FAILED, MAX_TEXTURES = 512, MAX_FORGOTTEN = 256 };

// Texture du GPU d'une surface du jeu, à jour pour une version de son contenu.
typedef struct {
    u32 surface;
    u32 version;
    u32 keyed;
    GLuint id;
} Texture;

static struct {
    int state;
    GLuint program, buffer;
    GLint u_size, u_has_texture, u_key, u_color_op, u_color_arg1, u_color_arg2, u_alpha_op, u_alpha_arg1, u_alpha_arg2;
    GLint u_alpha_test, u_alpha_func, u_alpha_ref, u_factor;
    u32 game_width, game_height; // image du jeu
    int out_width, out_height;   // cible du GPU
    int drawn;                   // quelque chose a été dessiné depuis la dernière image affichée
    int waiting;                 // le jeu a voulu dessiner alors que la fenêtre n'existait pas encore
    Texture textures[MAX_TEXTURES];
    u32 forgotten[MAX_FORGOTTEN];
    u32 forgotten_count;
    pthread_mutex_t forget_lock;
#if defined(__ANDROID__)
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface surface;
    u32 generation; // fenêtre de l'hôte pour laquelle la surface a été créée
#else
    GLuint framebuffer, color;
    u8 *readback;
#endif
} gl = {.forget_lock = PTHREAD_MUTEX_INITIALIZER};

static void report(RcnCtx *ctx, const char *what, const char *detail) {
    char line[600];
    int n = snprintf(line, sizeof line, "rendu GPU indisponible (%s%s%s) : rendu logiciel\n", what, detail[0] ? " : " : "", detail);
    rcn_host_write(ctx, RCN_STREAM_STDERR, line, (size_t)(n < (int)sizeof line ? n : (int)sizeof line - 1));
}

// --- Plateforme : contexte et cible du rendu ---

#if defined(__ANDROID__)

// Crée (ou recrée) la surface EGL sur la fenêtre actuelle de l'hôte. 0 s'il n'y a pas de fenêtre.
static int attach_window(RcnCtx *ctx) {
    const RcnHost *host = ctx->proc->host;
    u32 generation = 0;
    ANativeWindow *window = host->window(host->user, &generation);
    if (!window) return 0;
    if (gl.surface != EGL_NO_SURFACE && generation == gl.generation) {
        ANativeWindow_release(window);
        return 1;
    }
    if (gl.surface != EGL_NO_SURFACE) {
        eglMakeCurrent(gl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(gl.display, gl.surface);
    }
    // Tampons à la taille de la vue (le rendu logiciel a pu en imposer une autre).
    ANativeWindow_setBuffersGeometry(window, 0, 0, 0);
    gl.surface = eglCreateWindowSurface(gl.display, gl.config, window, NULL);
    ANativeWindow_release(window);
    gl.generation = generation;
    if (gl.surface == EGL_NO_SURFACE || !eglMakeCurrent(gl.display, gl.surface, gl.surface, gl.context)) return 0;
    eglSwapInterval(gl.display, 0); // la cadence est tenue par le jeu (rcn_present), pas par l'écran
    return 1;
}

static int platform_init(RcnCtx *ctx, const char **why) {
    static const EGLint config_attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                               EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    static const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLint count = 0;
    gl.surface = EGL_NO_SURFACE;
    gl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    *why = "EGL";
    if (gl.display == EGL_NO_DISPLAY || !eglInitialize(gl.display, NULL, NULL)) return 0;
    if (!eglChooseConfig(gl.display, config_attributes, &gl.config, 1, &count) || count < 1) return 0;
    gl.context = eglCreateContext(gl.display, gl.config, EGL_NO_CONTEXT, context_attributes);
    *why = "contexte OpenGL ES 3";
    if (gl.context == EGL_NO_CONTEXT) return 0;
    *why = "surface de la fenetre";
    return attach_window(ctx);
}

// Cible prête pour une image ? Met à jour sa taille.
static int platform_begin(RcnCtx *ctx) {
    if (!attach_window(ctx)) return 0;
    EGLint width = 0, height = 0;
    eglQuerySurface(gl.display, gl.surface, EGL_WIDTH, &width);
    eglQuerySurface(gl.display, gl.surface, EGL_HEIGHT, &height);
    gl.out_width = width;
    gl.out_height = height;
    return width > 0 && height > 0;
}

static void platform_present(RcnCtx *ctx, int wait) {
    if (!eglSwapBuffers(gl.display, gl.surface)) gl.generation = ~0u; // surface perdue : recréée à l'image suivante
    // Annonce la taille de l'image à l'application (sans pixels) et tient la cadence.
    rcn_present(ctx, NULL, gl.game_width, gl.game_height, 0, 32, wait);
}

#else // Windows : image hors écran, relue

static int platform_init(RcnCtx *ctx, const char **why) {
    (void)ctx;
    // PIXELFORMATDESCRIPTOR (40 octets) : fenêtre, OpenGL, 32 bits.
    static const u8 descriptor[40] = {40, 0, 1, 0, 0x24, 0, 0, 0, 0, 32};
    *why = "fenetre OpenGL";
    void *window = CreateWindowExA(0, "STATIC", "rcn-gl", 0, 0, 0, 16, 16, NULL, NULL, NULL, NULL);
    void *dc = window ? GetDC(window) : NULL;
    int format = dc ? ChoosePixelFormat(dc, descriptor) : 0;
    if (!format || !SetPixelFormat(dc, format, descriptor)) return 0;
    void *context = wglCreateContext(dc);
    if (!context || !wglMakeCurrent(dc, context)) return 0;
    *why = "OpenGL 3.3";
#define LOAD(ret, name, args)                           \
    name = (ret(__stdcall *) args)wglGetProcAddress(#name); \
    if (!name) return 0;
    GL_LATE(LOAD)
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    return 1;
}

static int platform_begin(RcnCtx *ctx) {
    (void)ctx;
    if ((int)gl.game_width == gl.out_width && (int)gl.game_height == gl.out_height) return 1;
    gl.out_width = (int)gl.game_width;
    gl.out_height = (int)gl.game_height;
    if (!gl.framebuffer) {
        glGenFramebuffers(1, &gl.framebuffer);
        glGenTextures(1, &gl.color);
    }
    glBindTexture(GL_TEXTURE_2D, gl.color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gl.out_width, gl.out_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, gl.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.color, 0);
    free(gl.readback);
    gl.readback = malloc((size_t)gl.out_width * (size_t)gl.out_height * 8);
    return gl.readback && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

static void platform_present(RcnCtx *ctx, int wait) {
    // Relit l'image (du bas vers le haut, en RVBA) et la remet à l'hôte comme une image logicielle (BVRX).
    size_t row = (size_t)gl.out_width * 4, size = row * (size_t)gl.out_height;
    u8 *raw = gl.readback, *image = gl.readback + size;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, gl.out_width, gl.out_height, GL_RGBA, GL_UNSIGNED_BYTE, raw);
    for (int y = 0; y < gl.out_height; y++) {
        const u8 *src = raw + (size_t)(gl.out_height - 1 - y) * row;
        u8 *dst = image + (size_t)y * row;
        for (int x = 0; x < gl.out_width; x++) {
            dst[x * 4] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4];
            dst[x * 4 + 3] = 0;
        }
    }
    rcn_present(ctx, image, (u32)gl.out_width, (u32)gl.out_height, (u32)row, 32, wait);
}

#endif

// --- Programme de nuanceurs ---

static GLuint compile(RcnCtx *ctx, GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[400] = "";
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        report(ctx, "nuanceur", log);
        return 0;
    }
    return shader;
}

static int make_program(RcnCtx *ctx) {
    GLuint vertex = compile(ctx, GL_VERTEX_SHADER, VERTEX_SHADER);
    GLuint fragment = vertex ? compile(ctx, GL_FRAGMENT_SHADER, FRAGMENT_SHADER) : 0;
    if (!fragment) return 0;
    gl.program = glCreateProgram();
    glAttachShader(gl.program, vertex);
    glAttachShader(gl.program, fragment);
    glBindAttribLocation(gl.program, 0, "a_position");
    glBindAttribLocation(gl.program, 1, "a_color");
    glBindAttribLocation(gl.program, 2, "a_uv");
    glLinkProgram(gl.program);
    GLint ok = 0;
    glGetProgramiv(gl.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        report(ctx, "edition des liens des nuanceurs", "");
        return 0;
    }
    glUseProgram(gl.program);
#define UNIFORM(name) gl.name = glGetUniformLocation(gl.program, #name)
    UNIFORM(u_size);
    UNIFORM(u_has_texture);
    UNIFORM(u_key);
    UNIFORM(u_color_op);
    UNIFORM(u_color_arg1);
    UNIFORM(u_color_arg2);
    UNIFORM(u_alpha_op);
    UNIFORM(u_alpha_arg1);
    UNIFORM(u_alpha_arg2);
    UNIFORM(u_alpha_test);
    UNIFORM(u_alpha_func);
    UNIFORM(u_alpha_ref);
    UNIFORM(u_factor);
    glUniform1i(glGetUniformLocation(gl.program, "u_texture"), 0);
    glGenBuffers(1, &gl.buffer);
    return 1;
}

// GPU demandé pour ce jeu ? Sous Android : dès que l'hôte fournit une fenêtre. Sous Windows : RCN_GL.
static int wanted(RcnCtx *ctx) {
#if defined(__ANDROID__)
    return ctx->proc->host->window != NULL;
#else
    (void)ctx;
    return getenv("RCN_GL") != NULL;
#endif
}

int rcn_gl_begin(RcnCtx *ctx, u32 width, u32 height) {
    if (gl.state == STATE_FAILED || !wanted(ctx)) return RCN_GL_NONE;
    gl.game_width = width;
    gl.game_height = height;
    if (gl.state == STATE_UNTRIED) {
#if defined(__ANDROID__)
        // La fenêtre n'existe qu'après la première image annoncée à l'application : on l'annonce, sans rien dessiner.
        const RcnHost *host = ctx->proc->host;
        u32 generation;
        ANativeWindow *window = host->window(host->user, &generation);
        if (!window) {
            gl.waiting = 1;
            return RCN_GL_WAIT;
        }
        ANativeWindow_release(window);
#endif
        const char *why = "";
        if (!platform_init(ctx, &why)) {
            report(ctx, why, "");
            gl.state = STATE_FAILED;
            return RCN_GL_NONE;
        }
        if (!make_program(ctx)) {
            gl.state = STATE_FAILED;
            return RCN_GL_NONE;
        }
        gl.state = STATE_READY;
    }
    if (!platform_begin(ctx)) {
        gl.waiting = 1;
        return RCN_GL_WAIT;
    }
    if (!gl.drawn) {
        // Début d'une image : états fixes, textures abandonnées supprimées.
        glViewport(0, 0, gl.out_width, gl.out_height);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(gl.program);
        glUniform2f(gl.u_size, (float)width, (float)height);
        pthread_mutex_lock(&gl.forget_lock);
        for (u32 i = 0; i < gl.forgotten_count; i++) {
            for (int t = 0; t < MAX_TEXTURES; t++) {
                if (gl.textures[t].surface == gl.forgotten[i]) {
                    glDeleteTextures(1, &gl.textures[t].id);
                    gl.textures[t].surface = 0;
                }
            }
        }
        gl.forgotten_count = 0;
        pthread_mutex_unlock(&gl.forget_lock);
    }
    gl.drawn = 1;
    return RCN_GL_READY;
}

// Rectangle du jeu (origine en haut à gauche) → rectangle de découpe du GPU (origine en bas à gauche).
static void scissor(const i32 rect[4]) {
    float sx = (float)gl.out_width / (float)gl.game_width, sy = (float)gl.out_height / (float)gl.game_height;
    i32 left = (i32)((float)rect[0] * sx), right = (i32)((float)rect[2] * sx + 0.5f);
    i32 top = (i32)((float)rect[1] * sy), bottom = (i32)((float)rect[3] * sy + 0.5f);
    glEnable(GL_SCISSOR_TEST);
    glScissor(left, gl.out_height - bottom, right > left ? right - left : 0, bottom > top ? bottom - top : 0);
}

void rcn_gl_clear(const i32 rect[4], u32 color) {
    scissor(rect);
    glClearColor((float)((color >> 16) & 0xFF) / 255.0f, (float)((color >> 8) & 0xFF) / 255.0f, (float)(color & 0xFF) / 255.0f,
                 (float)(color >> 24) / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

// Texture du GPU d'une surface, recopiée si son contenu a changé. Le texel de la couleur transparente
// devient entièrement transparent (opacité nulle), ce que le nuanceur écarte.
static GLuint texture_of(u32 object, int keyed) {
    RcnSurface *s = COM_DATA(RcnSurface, object);
    Texture *slot = NULL;
    for (int i = 0; i < MAX_TEXTURES; i++) {
        if (gl.textures[i].surface == object) {
            slot = &gl.textures[i];
            break;
        }
        if (!slot && !gl.textures[i].surface) slot = &gl.textures[i];
    }
    if (!slot) return 0;
    if (slot->surface == object && slot->version == s->version && slot->keyed == (u32)keyed) return slot->id;
    if (slot->surface != object) {
        glGenTextures(1, &slot->id);
        slot->surface = object;
    }
    slot->version = s->version;
    slot->keyed = (u32)keyed;
    u8 *rgba = malloc((size_t)s->width * s->height * 4);
    if (!rgba) return slot->id;
    // Position et largeur de chaque composante d'après les masques du format.
    u32 shift[4], bits[4];
    for (int c = 0; c < 4; c++) {
        u32 mask = s->masks[c];
        shift[c] = bits[c] = 0;
        while (mask && !(mask & 1)) {
            mask >>= 1;
            shift[c]++;
        }
        while (mask & 1) {
            mask >>= 1;
            bits[c]++;
        }
    }
    u32 key_mask = s->masks[0] | s->masks[1] | s->masks[2], key = s->key & key_mask;
    for (u32 y = 0; y < s->height; y++) {
        const u8 *line = (const u8 *)PTR(s->pixels) + (size_t)y * s->pitch;
        u8 *out = rgba + (size_t)y * s->width * 4;
        for (u32 x = 0; x < s->width; x++) {
            u32 pixel = s->bpp == 16 ? ((const u16 *)line)[x] : ((const u32 *)line)[x];
            for (int c = 0; c < 4; c++) {
                u32 v = 255;
                if (bits[c]) {
                    // Composante ramenée à 8 bits : le maximum du format donne 255.
                    u32 max = (1u << bits[c]) - 1;
                    v = (pixel >> shift[c]) & max;
                    v = bits[c] >= 8 ? v >> (bits[c] - 8) : v * 255 / max;
                }
                out[x * 4 + c] = (u8)v;
            }
            if (keyed && (pixel & key_mask) == key) out[x * 4] = out[x * 4 + 1] = out[x * 4 + 2] = out[x * 4 + 3] = 0;
        }
    }
    glBindTexture(GL_TEXTURE_2D, slot->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)s->width, (GLsizei)s->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    return slot->id;
}

static GLenum blend_factor(u32 factor) {
    switch (factor) {
    case 1: return GL_ZERO;
    case 3: return GL_SRC_COLOR;
    case 4: return GL_ONE_MINUS_SRC_COLOR;
    case 5: return GL_SRC_ALPHA;
    case 6: return GL_ONE_MINUS_SRC_ALPHA;
    case 7: return GL_DST_ALPHA;
    case 8: return GL_ONE_MINUS_DST_ALPHA;
    case 9: return GL_DST_COLOR;
    case 10: return GL_ONE_MINUS_DST_COLOR;
    default: return GL_ONE;
    }
}

void rcn_gl_draw(u32 type, const RcnVertex *vertices, u32 count, const RcnGlState *state) {
    static const GLenum modes[] = {0, GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN};
    if (type < 1 || type > 6 || count == 0) return;
    scissor(state->clip);
    if (state->blend) {
        glEnable(GL_BLEND);
        glBlendFunc(blend_factor(state->src_factor), blend_factor(state->dst_factor));
    } else {
        glDisable(GL_BLEND);
    }
    // Le sens des sommets est jugé à l'écran, où l'axe vertical est inversé par rapport à celui du GPU :
    // un triangle « horaire » pour Direct3D est antihoraire pour OpenGL.
    if (state->cull == 2 || state->cull == 3) {
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        glCullFace(state->cull == 2 ? GL_FRONT : GL_BACK);
    } else {
        glDisable(GL_CULL_FACE);
    }
    GLuint texture = state->texture ? texture_of(state->texture, (int)state->color_key) : 0;
    glUniform1i(gl.u_has_texture, texture != 0);
    glUniform1i(gl.u_key, state->color_key != 0);
    if (texture) {
        // Une texture à couleur transparente n'est pas lissée : le lissage ferait baver la couleur écartée.
        GLint filter = state->linear && !state->color_key ? GL_LINEAR : GL_NEAREST;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, state->clamp_u ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, state->clamp_v ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    }
    glUniform1i(gl.u_color_op, (GLint)state->color_op);
    glUniform1i(gl.u_color_arg1, (GLint)(state->color_arg1 & 0xF));
    glUniform1i(gl.u_color_arg2, (GLint)(state->color_arg2 & 0xF));
    glUniform1i(gl.u_alpha_op, (GLint)state->alpha_op);
    glUniform1i(gl.u_alpha_arg1, (GLint)(state->alpha_arg1 & 0xF));
    glUniform1i(gl.u_alpha_arg2, (GLint)(state->alpha_arg2 & 0xF));
    glUniform1i(gl.u_alpha_test, state->alpha_test != 0);
    glUniform1i(gl.u_alpha_func, (GLint)state->alpha_func);
    glUniform1f(gl.u_alpha_ref, (float)state->alpha_ref);
    glUniform4f(gl.u_factor, (float)((state->factor >> 16) & 0xFF) / 255.0f, (float)((state->factor >> 8) & 0xFF) / 255.0f,
                (float)(state->factor & 0xFF) / 255.0f, (float)(state->factor >> 24) / 255.0f);
    // Lignes de l'épaisseur d'un point du jeu, quelle que soit la résolution de la cible.
    if (type == 2 || type == 3) {
        float width = (float)gl.out_height / (float)gl.game_height;
        glLineWidth(width < 1 ? 1 : width);
    }
    // Sommets : position, couleur de 0 à 255 (ramenée à 0-1 par le nuanceur), coordonnées de texture.
    glBindBuffer(GL_ARRAY_BUFFER, gl.buffer);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof *vertices), vertices, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof *vertices, (const void *)offsetof(RcnVertex, x));
    glVertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof *vertices, (const void *)offsetof(RcnVertex, c));
    glVertexAttribPointer(2, 2, GL_FLOAT, 0, sizeof *vertices, (const void *)offsetof(RcnVertex, u));
    glDrawArrays(modes[type], 0, (GLsizei)count);
}

int rcn_gl_present(RcnCtx *ctx, int wait) {
    if (gl.waiting) {
        // Pas encore de fenêtre : la taille de l'image est annoncée à l'application (qui crée alors la
        // fenêtre), sans pixels — une image logicielle occuperait la fenêtre à la place du GPU.
        gl.waiting = 0;
        rcn_present(ctx, NULL, gl.game_width, gl.game_height, 0, 32, wait);
        return 1;
    }
    if (gl.state != STATE_READY || !gl.drawn) return 0;
    gl.drawn = 0;
    glDisable(GL_SCISSOR_TEST);
    platform_present(ctx, wait);
    return 1;
}

void rcn_gl_forget(u32 surface) {
    pthread_mutex_lock(&gl.forget_lock);
    if (gl.forgotten_count < MAX_FORGOTTEN) gl.forgotten[gl.forgotten_count++] = surface;
    pthread_mutex_unlock(&gl.forget_lock);
}

#endif
