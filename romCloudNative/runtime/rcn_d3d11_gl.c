// RomCloud Native : OpenGL sous Direct3D 11 — fonctions, contextes par thread, formats, présentation.
//
// Chaque thread du jeu qui appelle Direct3D reçoit son propre contexte OpenGL, qui partage ses objets
// (tampons, textures, programmes, échantillonneurs) avec les autres. Les framebuffers et tableaux de
// sommets, propres à un contexte, sont recréés par thread (D3dThreadGl).
#include <stdio.h>
#include <stdlib.h>

#include "rcn_d3d11.h"
#include "rcn_gui.h"

#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <android/native_window.h>
#include <dlfcn.h>
#elif defined(_WIN32)
__declspec(dllimport) void *__stdcall wglCreateContext(void *dc);
__declspec(dllimport) int __stdcall wglDeleteContext(void *context);
__declspec(dllimport) int __stdcall wglMakeCurrent(void *dc, void *context);
__declspec(dllimport) void *__stdcall wglGetProcAddress(const char *name);
__declspec(dllimport) void *__stdcall CreateWindowExA(unsigned long ex, const char *cls, const char *name, unsigned long style,
                                                        int x, int y, int w, int h, void *parent, void *menu, void *instance, void *param);
__declspec(dllimport) void *__stdcall GetDC(void *window);
__declspec(dllimport) int __stdcall ChoosePixelFormat(void *dc, const void *descriptor);
__declspec(dllimport) int __stdcall SetPixelFormat(void *dc, int format, const void *descriptor);
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *name);
__declspec(dllimport) void *__stdcall GetProcAddress(void *module, const char *name);
#endif

#define D3DGL_DEFINE(ret, name, params) ret(GLAPIENTRY *d3d_##name) params;
D3DGL_FUNCS(D3DGL_DEFINE)
#undef D3DGL_DEFINE

typedef struct {
    const char *name;
    void **slot;
} GlProc;
#define D3DGL_ENTRY(ret, name, params) {#name, (void **)&d3d_##name},
static const GlProc procs[] = {D3DGL_FUNCS(D3DGL_ENTRY)};

D3dGlCaps d3d_caps;

static struct {
    pthread_mutex_t lock;
    int state; // 0 : pas encore, 1 : prêt, -1 : indisponible
#if defined(__ANDROID__)
    EGLDisplay display;
    EGLConfig config;
    EGLContext main;
    EGLSurface window;
    u32 generation;
    int present_thread;
#elif defined(_WIN32)
    void *dc, *main;
    void *(__stdcall *create_attribs)(void *dc, void *share, const int *attribs);
    void *opengl32;
#endif
    u8 *pixels; // image relue (essais sans fenêtre)
    size_t pixels_size;
} g = {.lock = PTHREAD_MUTEX_INITIALIZER};

typedef struct {
    int ready;
    D3dThreadGl gl;
#if defined(__ANDROID__)
    EGLContext context;
    EGLSurface pbuffer;
#elif defined(_WIN32)
    void *context;
#endif
} ThreadGl;
static __thread ThreadGl tls;

D3dThreadGl *d3d_thread_gl(void) { return &tls.gl; }

// --- Plates-formes ---

#if defined(__ANDROID__)

static void *get_proc(const char *name) {
    void *p = (void *)eglGetProcAddress(name);
    return p ? p : dlsym(RTLD_DEFAULT, name);
}
static int platform_init(void) {
    static const EGLint config_attributes[] = {EGL_RENDERABLE_TYPE, 0x40 /* EGL_OPENGL_ES3_BIT */, EGL_SURFACE_TYPE,
                                               EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                               EGL_BLUE_SIZE, 8, EGL_NONE};
    static const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLint count = 0;
    g.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g.display == EGL_NO_DISPLAY || !eglInitialize(g.display, NULL, NULL)) return 0;
    if (!eglChooseConfig(g.display, config_attributes, &g.config, 1, &count) || count < 1) return 0;
    g.main = eglCreateContext(g.display, g.config, EGL_NO_CONTEXT, context_attributes);
    g.window = EGL_NO_SURFACE;
    return g.main != EGL_NO_CONTEXT;
}
static int platform_thread(void) {
    static const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    static const EGLint pbuffer_attributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    tls.context = eglCreateContext(g.display, g.config, g.main, context_attributes);
    tls.pbuffer = eglCreatePbufferSurface(g.display, g.config, pbuffer_attributes);
    if (tls.context == EGL_NO_CONTEXT) return 0;
    return eglMakeCurrent(g.display, tls.pbuffer, tls.pbuffer, tls.context);
}

#elif defined(_WIN32)

static void *get_proc(const char *name) {
    void *p = wglGetProcAddress(name);
    if ((uintptr_t)p <= 3 || (intptr_t)p == -1) p = g.opengl32 ? GetProcAddress(g.opengl32, name) : NULL;
    return p;
}
static void *create_context(void) {
    static const int attribs[] = {0x2091, 4, 0x2092, 3, 0x9126, 2 /* compatibilité */, 0};
    return g.create_attribs ? g.create_attribs(g.dc, g.main, attribs) : NULL;
}
static int platform_init(void) {
    static const u8 descriptor[40] = {40, 0, 1, 0, 0x24, 0, 0, 0, 0, 32};
    void *window = CreateWindowExA(0, "STATIC", "rcn-d3d11", 0, 0, 0, 16, 16, NULL, NULL, NULL, NULL);
    g.dc = window ? GetDC(window) : NULL;
    int format = g.dc ? ChoosePixelFormat(g.dc, descriptor) : 0;
    if (!format || !SetPixelFormat(g.dc, format, descriptor)) return 0;
    g.opengl32 = LoadLibraryA("opengl32.dll");
    void *legacy = wglCreateContext(g.dc);
    if (!legacy || !wglMakeCurrent(g.dc, legacy)) return 0;
    g.create_attribs = (void *(__stdcall *)(void *, void *, const int *))get_proc("wglCreateContextAttribsARB");
    g.main = create_context();
    wglMakeCurrent(NULL, NULL);
    if (!g.main) {
        g.main = legacy; // pas de wglCreateContextAttribsARB : contexte classique
        return 1;
    }
    wglDeleteContext(legacy);
    return 1;
}
static int platform_thread(void) {
    tls.context = create_context();
    return tls.context && wglMakeCurrent(g.dc, tls.context);
}

#else

static void *get_proc(const char *name) { (void)name; return NULL; }
static int platform_init(void) { return 0; }
static int platform_thread(void) { return 0; }

#endif

static int has_ext(const char *name) {
    GLint count = 0;
    GL(glGetIntegerv)(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; i++) {
        const char *e = (const char *)GL(glGetStringi)(GL_EXTENSIONS, (GLuint)i);
        if (e && !strcmp(e, name)) return 1;
    }
    return 0;
}

static void load(void) {
    for (size_t i = 0; i < sizeof procs / sizeof procs[0]; i++) *procs[i].slot = get_proc(procs[i].name);
    const char *version = (const char *)GL(glGetString)(GL_VERSION);
    int major = 0, minor = 0;
    d3d_caps.es = version && strstr(version, "OpenGL ES") != NULL;
    if (version) sscanf(d3d_caps.es ? strstr(version, "ES") + 3 : version, "%d.%d", &major, &minor);
    d3d_caps.version = major * 10 + minor;
    d3d_caps.has_s3tc = has_ext("GL_EXT_texture_compression_s3tc") || has_ext("GL_EXT_texture_compression_dxt1");
    d3d_caps.has_rgtc = !d3d_caps.es || has_ext("GL_EXT_texture_compression_rgtc");
    d3d_caps.has_bptc = has_ext("GL_ARB_texture_compression_bptc") || has_ext("GL_EXT_texture_compression_bptc") ||
                        (!d3d_caps.es && d3d_caps.version >= 42);
    d3d_caps.has_base_instance = GL(glDrawElementsInstancedBaseVertexBaseInstance) != NULL;
    d3d_caps.has_copy_image = GL(glCopyImageSubData) != NULL;
    d3d_caps.max_anisotropy = has_ext("GL_EXT_texture_filter_anisotropic") || has_ext("GL_ARB_texture_filter_anisotropic") ? 16 : 0;
    rcn_log("d3d11 : OpenGL %s (%s, %s)%s%s%s", version ? version : "?", (const char *)GL(glGetString)(GL_RENDERER),
            (const char *)GL(glGetString)(GL_VENDOR), d3d_caps.has_s3tc ? " s3tc" : "", d3d_caps.has_rgtc ? " rgtc" : "",
            d3d_caps.has_bptc ? " bptc" : "");
}

int d3d_gl_current(RcnCtx *ctx) {
    (void)ctx;
    if (tls.ready) return tls.ready > 0;
    pthread_mutex_lock(&g.lock);
    if (!g.state) g.state = platform_init() ? 1 : -1;
    int ok = g.state > 0 && platform_thread();
    static int loaded;
    if (ok && !loaded) {
        load();
        loaded = 1;
    }
    pthread_mutex_unlock(&g.lock);
    tls.ready = ok ? 1 : -1;
    if (!ok) {
        d3d_log("contexte OpenGL indisponible");
        return 0;
    }
    GL(glGenVertexArrays)(1, &tls.gl.vao);
    GL(glBindVertexArray)(tls.gl.vao);
    GL(glGenFramebuffers)(1, &tls.gl.read_fb);
    GL(glGenFramebuffers)(1, &tls.gl.draw_fb);
    if (!d3d_caps.es) GL(glEnable)(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    GL(glEnable)(GL_PRIMITIVE_RESTART_FIXED_INDEX);
    GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 1);
    GL(glPixelStorei)(GL_PACK_ALIGNMENT, 1);
    return 1;
}

static __thread int draw_thread;
void d3d_gl_mark_draw(void) { draw_thread = 1; }
void d3d_gl_publish(void) {
    if (!draw_thread && GL(glFinish)) GL(glFinish)();
}

void d3d_gl_check(const char *where) {
    if (d3d_log_level < 2 || !GL(glGetError)) return;
    for (int i = 0; i < 4; i++) {
        GLenum e = GL(glGetError)();
        if (e == GL_NO_ERROR) break;
        d3d_log("erreur OpenGL 0x%x : %s", e, where);
    }
}

// --- Présentation ---

// Image relue et remise à l'hôte (essais sur PC, ou en ligne de commande sur l'appareil, sans fenêtre) :
// octets B, G, R, X, ligne 0 en haut (celle de la texture). OpenGL ES ne garantit que la lecture RGBA.
static void readback(RcnCtx *ctx, u32 w, u32 h, int interval) {
    size_t size = (size_t)w * h * 4;
    if (size > g.pixels_size) {
        free(g.pixels);
        g.pixels = malloc(size);
        g.pixels_size = g.pixels ? size : 0;
    }
    if (!g.pixels) return;
    GL(glBindBuffer)(GL_PIXEL_PACK_BUFFER, 0);
    if (d3d_caps.es) {
        GL(glReadPixels)(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, g.pixels);
        for (size_t i = 0; i < size; i += 4) {
            u8 r = g.pixels[i];
            g.pixels[i] = g.pixels[i + 2];
            g.pixels[i + 2] = r;
        }
    } else {
        GL(glReadPixels)(0, 0, (GLsizei)w, (GLsizei)h, GL_BGRA, GL_UNSIGNED_BYTE, g.pixels);
    }
    rcn_present(ctx, g.pixels, w, h, w * 4, 32, interval != 0);
}

void d3d_gl_present(RcnCtx *ctx, D3dRes *bb, int interval) {
    if (!bb || !d3d_gl_current(ctx)) return;
    u32 w = bb->width, h = bb->height;
    D3dThreadGl *t = &tls.gl;
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, t->read_fb);
    GL(glFramebufferTextureLayer)(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, bb->gl, 0, 0);
    GL(glReadBuffer)(GL_COLOR_ATTACHMENT0);
#if defined(__ANDROID__)
    const RcnHost *host = ctx->proc->host;
    u32 generation = 0;
    ANativeWindow *window = host->window ? host->window(host->user, &generation) : NULL;
    if (window) {
        if (g.window == EGL_NO_SURFACE || generation != g.generation) {
            if (g.window != EGL_NO_SURFACE) {
                eglMakeCurrent(g.display, tls.pbuffer, tls.pbuffer, tls.context);
                eglDestroySurface(g.display, g.window);
            }
            ANativeWindow_setBuffersGeometry(window, 0, 0, 0);
            g.window = eglCreateWindowSurface(g.display, g.config, window, NULL);
            g.generation = generation;
        }
        ANativeWindow_release(window);
    }
    if (g.window != EGL_NO_SURFACE && eglMakeCurrent(g.display, g.window, g.window, tls.context)) {
        eglSwapInterval(g.display, 0);
        EGLint ow = 0, oh = 0;
        eglQuerySurface(g.display, g.window, EGL_WIDTH, &ow);
        eglQuerySurface(g.display, g.window, EGL_HEIGHT, &oh);
        GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, 0);
        GL(glDisable)(GL_SCISSOR_TEST);
        GL(glColorMask)(1, 1, 1, 1);
        static const GLfloat black[4] = {0, 0, 0, 1};
        GL(glClearBufferfv)(GL_COLOR, 0, black);
        double sx = (double)ow / w, sy = (double)oh / h, s = sx < sy ? sx : sy;
        int dw = (int)(w * s), dh = (int)(h * s), x = (ow - dw) / 2, y = (oh - dh) / 2;
        // Ligne 0 de l'image en haut : copie retournée.
        GL(glBlitFramebuffer)(0, 0, (GLint)w, (GLint)h, x, y + dh, x + dw, y, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        if (!eglSwapBuffers(g.display, g.window)) g.generation = ~0u;
        eglMakeCurrent(g.display, tls.pbuffer, tls.pbuffer, tls.context);
        rcn_present(ctx, NULL, w, h, 0, 32, interval != 0);
    } else if (!host->window) {
        readback(ctx, w, h, interval); // ligne de commande (adb) : images relues
    } else {
        rcn_present(ctx, NULL, w, h, 0, 32, interval != 0); // fenêtre pas encore là
    }
#elif defined(_WIN32)
    // Les textures gardent toujours l'ordre logique RGBA (B8G8R8A8 échangé à l'envoi) : lecture BGRA directe.
    readback(ctx, w, h, interval);
#else
    (void)interval;
#endif
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, 0);
}

// --- Formats DXGI ---

#define F(i, f, t, b, ...) {i, f, t, b, __VA_ARGS__}
//                                        internal format type bytes block depth swap int comps norm srgb
static const D3dFormat formats[] = {
    [1] = F(GL_RGBA32F, GL_RGBA, GL_FLOAT, 16, 0, 0, 0, 0, 4, 0, 0),
    [2] = F(GL_RGBA32F, GL_RGBA, GL_FLOAT, 16, 0, 0, 0, 0, 4, 0, 0),
    [3] = F(GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, 16, 0, 0, 0, 1, 4, 0, 0),
    [4] = F(GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, 16, 0, 0, 0, 1, 4, 0, 0),
    [5] = F(GL_RGB32F, GL_RGB, GL_FLOAT, 12, 0, 0, 0, 0, 3, 0, 0),
    [6] = F(GL_RGB32F, GL_RGB, GL_FLOAT, 12, 0, 0, 0, 0, 3, 0, 0),
    [7] = F(GL_RGB32UI, GL_RGB_INTEGER, GL_UNSIGNED_INT, 12, 0, 0, 0, 1, 3, 0, 0),
    [8] = F(GL_RGB32I, GL_RGB_INTEGER, GL_INT, 12, 0, 0, 0, 1, 3, 0, 0),
    [9] = F(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8, 0, 0, 0, 0, 4, 0, 0),
    [10] = F(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8, 0, 0, 0, 0, 4, 0, 0),
    [11] = F(GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, 8, 0, 0, 0, 0, 4, 1, 0),
    [12] = F(GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, 8, 0, 0, 0, 1, 4, 0, 0),
    [13] = F(GL_RGBA16_SNORM, GL_RGBA, GL_SHORT, 8, 0, 0, 0, 0, 4, 1, 0),
    [14] = F(GL_RGBA16I, GL_RGBA_INTEGER, GL_SHORT, 8, 0, 0, 0, 1, 4, 0, 0),
    [15] = F(GL_RG32F, GL_RG, GL_FLOAT, 8, 0, 0, 0, 0, 2, 0, 0),
    [16] = F(GL_RG32F, GL_RG, GL_FLOAT, 8, 0, 0, 0, 0, 2, 0, 0),
    [17] = F(GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT, 8, 0, 0, 0, 1, 2, 0, 0),
    [18] = F(GL_RG32I, GL_RG_INTEGER, GL_INT, 8, 0, 0, 0, 1, 2, 0, 0),
    [19] = F(GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 8, 0, 2, 0, 0, 0, 0, 0),
    [20] = F(GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 8, 0, 2, 0, 0, 0, 0, 0),
    [21] = F(GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 8, 0, 2, 0, 0, 0, 0, 0),
    [22] = F(GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 8, 0, 2, 0, 0, 0, 0, 0),
    [23] = F(GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, 4, 0, 0, 0, 0, 4, 1, 0),
    [24] = F(GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, 4, 0, 0, 0, 0, 4, 1, 0),
    [25] = F(GL_RGB10_A2UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV, 4, 0, 0, 0, 1, 4, 0, 0),
    [26] = F(GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, 4, 0, 0, 0, 0, 3, 0, 0),
    [27] = F(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0, 0, 0, 0, 4, 1, 0),
    [28] = F(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0, 0, 0, 0, 4, 1, 0),
    [29] = F(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0, 0, 0, 0, 4, 1, 1),
    [30] = F(GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, 4, 0, 0, 0, 1, 4, 0, 0),
    [31] = F(GL_RGBA8_SNORM, GL_RGBA, GL_BYTE, 4, 0, 0, 0, 0, 4, 1, 0),
    [32] = F(GL_RGBA8I, GL_RGBA_INTEGER, GL_BYTE, 4, 0, 0, 0, 1, 4, 0, 0),
    [33] = F(GL_RG16F, GL_RG, GL_HALF_FLOAT, 4, 0, 0, 0, 0, 2, 0, 0),
    [34] = F(GL_RG16F, GL_RG, GL_HALF_FLOAT, 4, 0, 0, 0, 0, 2, 0, 0),
    [35] = F(GL_RG16, GL_RG, GL_UNSIGNED_SHORT, 4, 0, 0, 0, 0, 2, 1, 0),
    [36] = F(GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, 4, 0, 0, 0, 1, 2, 0, 0),
    [37] = F(GL_RG16_SNORM, GL_RG, GL_SHORT, 4, 0, 0, 0, 0, 2, 1, 0),
    [38] = F(GL_RG16I, GL_RG_INTEGER, GL_SHORT, 4, 0, 0, 0, 1, 2, 0, 0),
    [39] = F(GL_R32F, GL_RED, GL_FLOAT, 4, 0, 0, 0, 0, 1, 0, 0),
    [40] = F(GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 4, 0, 1, 0, 0, 0, 0, 0),
    [41] = F(GL_R32F, GL_RED, GL_FLOAT, 4, 0, 0, 0, 0, 1, 0, 0),
    [42] = F(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 4, 0, 0, 0, 1, 1, 0, 0),
    [43] = F(GL_R32I, GL_RED_INTEGER, GL_INT, 4, 0, 0, 0, 1, 1, 0, 0),
    [44] = F(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 4, 0, 2, 0, 0, 0, 0, 0),
    [45] = F(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 4, 0, 2, 0, 0, 0, 0, 0),
    [46] = F(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 4, 0, 2, 0, 0, 0, 0, 0),
    [47] = F(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 4, 0, 2, 0, 0, 0, 0, 0),
    [48] = F(GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2, 0, 0, 0, 0, 2, 1, 0),
    [49] = F(GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2, 0, 0, 0, 0, 2, 1, 0),
    [50] = F(GL_RG8UI, GL_RG_INTEGER, GL_UNSIGNED_BYTE, 2, 0, 0, 0, 1, 2, 0, 0),
    [51] = F(GL_RG8_SNORM, GL_RG, GL_BYTE, 2, 0, 0, 0, 0, 2, 1, 0),
    [52] = F(GL_RG8I, GL_RG_INTEGER, GL_BYTE, 2, 0, 0, 0, 1, 2, 0, 0),
    [53] = F(GL_R16F, GL_RED, GL_HALF_FLOAT, 2, 0, 0, 0, 0, 1, 0, 0),
    [54] = F(GL_R16F, GL_RED, GL_HALF_FLOAT, 2, 0, 0, 0, 0, 1, 0, 0),
    [55] = F(GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 2, 0, 1, 0, 0, 0, 0, 0),
    [56] = F(GL_R16, GL_RED, GL_UNSIGNED_SHORT, 2, 0, 0, 0, 0, 1, 1, 0),
    [57] = F(GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, 2, 0, 0, 0, 1, 1, 0, 0),
    [58] = F(GL_R16_SNORM, GL_RED, GL_SHORT, 2, 0, 0, 0, 0, 1, 1, 0),
    [59] = F(GL_R16I, GL_RED_INTEGER, GL_SHORT, 2, 0, 0, 0, 1, 1, 0, 0),
    [60] = F(GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1, 0, 0, 0, 0, 1, 1, 0),
    [61] = F(GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1, 0, 0, 0, 0, 1, 1, 0),
    [62] = F(GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, 1, 0, 0, 0, 1, 1, 0, 0),
    [63] = F(GL_R8_SNORM, GL_RED, GL_BYTE, 1, 0, 0, 0, 0, 1, 1, 0),
    [64] = F(GL_R8I, GL_RED_INTEGER, GL_BYTE, 1, 0, 0, 0, 1, 1, 0, 0),
    [65] = F(GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1, 0, 0, 2, 0, 1, 1, 0), // A8 : rouge lu comme alpha
    [67] = F(GL_RGB9_E5, GL_RGB, GL_UNSIGNED_INT_5_9_9_9_REV, 4, 0, 0, 0, 0, 3, 0, 0),
    [70] = F(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, 0, 8, 1, 0, 0, 0, 0, 0, 0),
    [71] = F(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, 0, 8, 1, 0, 0, 0, 0, 0, 0),
    [72] = F(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT, 0, 0, 8, 1, 0, 0, 0, 0, 0, 1),
    [73] = F(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, 0, 16, 1, 0, 0, 0, 0, 0, 0),
    [74] = F(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, 0, 16, 1, 0, 0, 0, 0, 0, 0),
    [75] = F(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT, 0, 0, 16, 1, 0, 0, 0, 0, 0, 1),
    [76] = F(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0, 16, 1, 0, 0, 0, 0, 0, 0),
    [77] = F(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0, 16, 1, 0, 0, 0, 0, 0, 0),
    [78] = F(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 0, 0, 16, 1, 0, 0, 0, 0, 0, 1),
    [79] = F(GL_COMPRESSED_RED_RGTC1, 0, 0, 8, 1, 0, 0, 0, 0, 0, 0),
    [80] = F(GL_COMPRESSED_RED_RGTC1, 0, 0, 8, 1, 0, 0, 0, 0, 0, 0),
    [81] = F(GL_COMPRESSED_SIGNED_RED_RGTC1, 0, 0, 8, 1, 0, 0, 0, 0, 0, 0),
    [82] = F(GL_COMPRESSED_RG_RGTC2, 0, 0, 16, 1, 0, 0, 0, 0, 0, 0),
    [83] = F(GL_COMPRESSED_RG_RGTC2, 0, 0, 16, 1, 0, 0, 0, 0, 0, 0),
    [84] = F(GL_COMPRESSED_SIGNED_RG_RGTC2, 0, 0, 16, 1, 0, 0, 0, 0, 0, 0),
    [85] = F(GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 2, 0, 0, 0, 0, 3, 1, 0),
    [87] = F(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0, 0, 1, 0, 4, 1, 0),
    [88] = F(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0, 0, 3, 0, 4, 1, 0), // B8G8R8X8 : alpha lu comme 1
    [90] = F(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0, 0, 1, 0, 4, 1, 0),
    [91] = F(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0, 0, 1, 0, 4, 1, 1),
    [92] = F(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0, 0, 3, 0, 4, 1, 0),
    [93] = F(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0, 0, 3, 0, 4, 1, 1),
    [94] = F(GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT, 0, 0, 16, 1, 0, 0, 0, 0, 0, 0),
    [95] = F(GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT, 0, 0, 16, 1, 0, 0, 0, 0, 0, 0),
    [96] = F(GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT, 0, 0, 16, 1, 0, 0, 0, 0, 0, 0),
    [97] = F(GL_COMPRESSED_RGBA_BPTC_UNORM, 0, 0, 16, 1, 0, 0, 0, 0, 0, 0),
    [98] = F(GL_COMPRESSED_RGBA_BPTC_UNORM, 0, 0, 16, 1, 0, 0, 0, 0, 0, 0),
    [99] = F(GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM, 0, 0, 16, 1, 0, 0, 0, 0, 0, 1),
};

const D3dFormat *d3d_format(u32 dxgi) {
    static const D3dFormat none;
    return dxgi < sizeof formats / sizeof formats[0] ? &formats[dxgi] : &none;
}

// Textures compressées (BC1 à BC7) que le GPU ne lit pas : rangées en RGBA 8 bits (sRGB ou signé selon
// le format). BC1 à BC5 sont décompressés à l'envoi ; BC6H / BC7 pas encore (texture noire).
// RCN_FORCE_BCN=1 force la décompression (essais sur PC).
int d3d_bcn_decoded(u32 dxgi) {
    static int forced = -1;
    if (forced < 0) forced = getenv("RCN_FORCE_BCN") && atoi(getenv("RCN_FORCE_BCN"));
    if (dxgi >= 70 && dxgi <= 78) return forced || !d3d_caps.has_s3tc;
    if (dxgi >= 79 && dxgi <= 84) return forced || !d3d_caps.has_rgtc;
    if (dxgi >= 94 && dxgi <= 99) return forced || !d3d_caps.has_bptc;
    return 0;
}
const D3dFormat *d3d_texture_format(u32 dxgi, int depth_bound) {
    static const D3dFormat rgba8 = F(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0, 0, 0, 0, 4, 1, 0),
                           srgb8 = F(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0, 0, 0, 0, 4, 1, 1),
                           snorm8 = F(GL_RGBA8_SNORM, GL_RGBA, GL_BYTE, 4, 0, 0, 0, 0, 4, 1, 0);
    if (d3d_bcn_decoded(dxgi)) {
        if (dxgi == 81 || dxgi == 84) return &snorm8;
        return d3d_format(dxgi)->srgb ? &srgb8 : &rgba8;
    }
    return d3d_format(d3d_storage_format(dxgi, depth_bound));
}

u32 d3d_storage_format(u32 dxgi, int depth_bound) {
    if (!depth_bound) return dxgi;
    switch (dxgi) {
    case 39: return 40; // R32_TYPELESS → D32_FLOAT
    case 53: return 55; // R16_TYPELESS → D16_UNORM
    case 44: return 45; // R24G8_TYPELESS → D24_UNORM_S8_UINT
    case 19: return 20;
    default: return dxgi;
    }
}
