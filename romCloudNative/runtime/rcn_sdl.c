// RomCloud Native : SDL2 pour les jeux 64 bits qui l'emploient (MonoGame « DesktopGL », FNA…). Le jeu
// charge « SDL2.dll » et y prend ses fonctions par GetProcAddress : ce sont celles-ci, sur l'hôte.
//
// Une seule fenêtre, plein écran, avec un contexte OpenGL (rcn_sdl_gl.c). La manette de l'hôte est une
// manette de jeu SDL (disposition Xbox) ; souris et clavier sont absents pour l'instant.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rcn_gui.h"
#include "rcn_sdl.h"

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

enum {
    SDL_QUIT = 0x100,
    SDL_JOYDEVICEADDED = 0x605,
    SDL_CONTROLLERDEVICEADDED = 0x653,
    SDL_EVENT_SIZE = 56,
    SDL_WINDOW_OPENGL = 0x2,
    SDL_WINDOW_SHOWN = 0x4,
    SDL_WINDOW_RESIZABLE = 0x20,
    SDL_WINDOW_INPUT_FOCUS = 0x200,
    SDL_WINDOW_MOUSE_FOCUS = 0x400,
    SDL_PIXELFORMAT_RGB888 = 0x16161804,
    // Objets rendus au jeu (adresses opaques qu'il ne lit jamais).
    WINDOW_ID = 1,
};

// Écran annoncé : celui de l'hôte s'il est connu.
#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080

static struct {
    pthread_mutex_t lock;
    char error[256];
    u64 window;                 // SDL_Window (bloc de mémoire du jeu)
    u64 context;                // SDL_GLContext
    u64 controller, joystick;   // SDL_GameController, SDL_Joystick
    u64 cursor, surface;
    u32 width, height;          // taille de la fenêtre (image du jeu)
    int swap_interval;
    int devices_announced, quit_sent;
    u8 queue[16][SDL_EVENT_SIZE];
    u32 queue_head, queue_count;
#if defined(__ANDROID__)
    EGLDisplay display;
    EGLConfig config;
    EGLContext egl;
    EGLSurface surface_egl, pbuffer;
    u32 generation;
#elif defined(_WIN32)
    void *dc, *wgl;
    u8 *pixels;
#endif
} sdl = {.lock = PTHREAD_MUTEX_INITIALIZER, .swap_interval = 1};

static void set_error(const char *text) { snprintf(sdl.error, sizeof sdl.error, "%s", text); }
void rcn_sdl_set_error(const char *text) { set_error(text); }

// Bloc de mémoire du jeu, rempli de zéros (objets opaques, chaînes).
static u64 block(RcnCtx *ctx, size_t size) { return (u64)(uintptr_t)rcn_galloc(ctx, size, 1); }
static u64 text(RcnCtx *ctx, const char *s) {
    char *p = rcn_galloc(ctx, strlen(s) + 1, 0);
    if (p) strcpy(p, s);
    return (u64)(uintptr_t)p;
}

static void push_event(u32 type, i32 which) {
    pthread_mutex_lock(&sdl.lock);
    if (sdl.queue_count < 16) {
        u8 *e = sdl.queue[(sdl.queue_head + sdl.queue_count++) % 16];
        memset(e, 0, SDL_EVENT_SIZE);
        memcpy(e, &type, 4);
        memcpy(e + 8, &which, 4);
    }
    pthread_mutex_unlock(&sdl.lock);
}

// SDL_WindowEvent : type 0x200, windowID (+8), event (+12), data1 (+16), data2 (+20). FNA ne met son
// image (back buffer) à la taille de la fenêtre qu'à réception de RESIZED / SIZE_CHANGED.
enum { SDL_WINDOWEVENT = 0x200, SDL_WINDOWEVENT_RESIZED = 5, SDL_WINDOWEVENT_SIZE_CHANGED = 6 };
static void push_window_event(u8 event, u32 data1, u32 data2) {
    pthread_mutex_lock(&sdl.lock);
    if (sdl.queue_count < 16) {
        u8 *e = sdl.queue[(sdl.queue_head + sdl.queue_count++) % 16];
        u32 type = SDL_WINDOWEVENT, id = WINDOW_ID;
        memset(e, 0, SDL_EVENT_SIZE);
        memcpy(e, &type, 4);
        memcpy(e + 8, &id, 4);
        e[12] = event;
        memcpy(e + 16, &data1, 4);
        memcpy(e + 20, &data2, 4);
    }
    pthread_mutex_unlock(&sdl.lock);
}
// Nouvelle taille de fenêtre : image de l'hôte ajustée et jeu prévenu.
static void window_resized(u32 width, u32 height) {
    if (width == sdl.width && height == sdl.height) return;
    sdl.width = width, sdl.height = height;
    if (sdl.context) rcn_sdlgl_resize(width, height);
    push_window_event(SDL_WINDOWEVENT_RESIZED, width, height);
    push_window_event(SDL_WINDOWEVENT_SIZE_CHANGED, width, height);
}

// --- Initialisation, erreurs, réglages ---

static void sdl_Zero(RcnCtx *ctx) { RET(0); }
static void sdl_One(RcnCtx *ctx) { RET(1); }
static void sdl_Nothing(RcnCtx *ctx) { (void)ctx; }
static void sdl_Null(RcnCtx *ctx) { RET(0); }
// SDL_GetVersion(&version) : 2.0.12.
static void sdl_GetVersion(RcnCtx *ctx) {
    wr8(A0, 2);
    wr8(A0 + 1, 0);
    wr8(A0 + 2, 12);
}
static void sdl_GetError(RcnCtx *ctx) {
    // Le texte d'erreur est recopié dans un bloc du jeu à chaque demande (il reste valide jusqu'à la suivante).
    static u64 copy;
    static size_t size;
    size_t n = strlen(sdl.error) + 1;
    if (n > size) {
        copy = block(ctx, n < 256 ? 256 : n);
        size = n < 256 ? 256 : n;
    }
    if (copy) strcpy(PTR(copy), sdl.error);
    RET(copy);
}
static void sdl_ClearError(RcnCtx *ctx) { sdl.error[0] = 0; }
// SDL_ShowSimpleMessageBox(flags, title, message, window) : vers le journal (pas de fenêtre).
static void sdl_ShowSimpleMessageBox(RcnCtx *ctx) {
    rcn_log("rcn : boite de message SDL « %s » : %s", A1 ? STR(A1) : "", A2 ? STR(A2) : "");
    RET(0);
}
// SDL_GetPlatform() : « Windows » (le jeu croit tourner sous Windows).
static void sdl_GetPlatform(RcnCtx *ctx) {
    static u64 name;
    if (!name) name = text(ctx, "Windows");
    RET(name);
}
// --- Compléments de FNA : chemins, temps, clavier, attributs GL ---

static void write_mode(u64 mode);
// SDL_GetBasePath() : le dossier du jeu, vu par lui comme « C:\ ».
static void sdl_GetBasePath(RcnCtx *ctx) { RET(text(ctx, "C:\\")); }
// SDL_GetPrefPath(organisation, application) : dossier de réglages, créé au besoin.
static void sdl_GetPrefPath(RcnCtx *ctx) {
    char path[600];
    int n = snprintf(path, sizeof path, "C:\\Users\\Player\\AppData\\Roaming");
    rcn_make_guest_dir(ctx, path);
    for (int i = 0; i < 2; i++) {
        u64 part = i ? A1 : A0;
        if (!part || !*(const char *)STR(part)) continue;
        n += snprintf(path + n, sizeof path - (size_t)n, "\\%s", (const char *)STR(part));
        rcn_make_guest_dir(ctx, path);
    }
    snprintf(path + n, sizeof path - (size_t)n, "\\");
    RET(text(ctx, path));
}
static u64 now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}
static u64 start_ns;
static void sdl_GetTicks(RcnCtx *ctx) {
    if (!start_ns) start_ns = now_ns();
    RET((u32)((now_ns() - start_ns) / 1000000ull));
}
static void sdl_GetPerformanceCounter(RcnCtx *ctx) { RET(now_ns()); }
static void sdl_GetPerformanceFrequency(RcnCtx *ctx) { RET(1000000000ull); }
static void sdl_Delay(RcnCtx *ctx) {
    u32 ms = (u32)A0;
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
static void sdl_GetCPUCount(RcnCtx *ctx) { RET(8); }
static void sdl_GetSystemRAM(RcnCtx *ctx) { RET(8192); }
// SDL_WasInit(flags) : tout est prêt.
static void sdl_WasInit(RcnCtx *ctx) { RET(A0 ? (u32)A0 : 0xF231u); }
static void sdl_GetHintBoolean(RcnCtx *ctx) { RET((u32)A1 != 0); }
static void sdl_GetCurrentVideoDriver(RcnCtx *ctx) {
    static u64 name;
    if (!name) name = text(ctx, "windows");
    RET(name);
}
static void sdl_GetRevision(RcnCtx *ctx) {
    static u64 name;
    if (!name) name = text(ctx, "rcn");
    RET(name);
}
// SDL_GetKeyboardState(&nombre) : aucune touche enfoncée (512 codes).
static void sdl_GetKeyboardState(RcnCtx *ctx) {
    static u64 keys;
    if (!keys) keys = block(ctx, 512);
    if (A0) wr32(A0, 512);
    RET(keys);
}
// Codes de touches SDL : lettres et chiffres en ASCII, les autres d'après leur code physique.
static u32 key_from_scancode(u32 sc) {
    if (sc >= 4 && sc <= 29) return 'a' + (sc - 4);
    if (sc >= 30 && sc <= 38) return '1' + (sc - 30);
    static const u8 ascii[] = {'0', '\r', 27, 8, '\t', ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/'};
    if (sc >= 39 && sc < 39 + sizeof ascii && ascii[sc - 39]) return ascii[sc - 39];
    return sc | 0x40000000u;
}
static void sdl_GetKeyFromScancode(RcnCtx *ctx) { RET(key_from_scancode((u32)A0)); }
static void sdl_GetScancodeFromKey(RcnCtx *ctx) {
    u32 key = (u32)A0;
    if (key & 0x40000000u) {
        RET(key & 0x1FF);
        return;
    }
    for (u32 sc = 4; sc < 64; sc++) {
        if (key_from_scancode(sc) == key) {
            RET(sc);
            return;
        }
    }
    RET(0);
}
static void sdl_Window(RcnCtx *ctx) { RET(sdl.window); }
// SDL_GL_GetAttribute(attribut, &valeur) : tampons RGBA8, profondeur 24, gabarit 8, double tampon.
static void sdl_GL_GetAttribute(RcnCtx *ctx) {
    static const i32 values[] = {8, 8, 8, 8, 32, 1, 24, 8};
    u32 attr = (u32)A0;
    i32 value = attr < 8 ? values[attr] : 0;
    if (attr == 17) value = 3; // SDL_GL_CONTEXT_MAJOR_VERSION
    if (A1) wr32(A1, (u32)value);
    RET(0);
}
// SDL_GL_GetDrawableSize(window, &w, &h) : taille de la fenêtre.
static void sdl_GL_GetDrawableSize(RcnCtx *ctx) {
    if (A1) wr32(A1, sdl.width);
    if (A2) wr32(A2, sdl.height);
}
static void sdl_GetWindowDisplayMode(RcnCtx *ctx) {
    write_mode(A1);
    RET(0);
}
// SDL_GetDisplayDPI(display, &diagonale, &horizontale, &verticale) : 96 points par pouce.
static void sdl_GetDisplayDPI(RcnCtx *ctx) {
    for (int i = 0; i < 3; i++) {
        u64 at = i == 0 ? A1 : i == 1 ? A2 : A3;
        if (at) wr32(at, 0x42C00000u); // 96.0f
    }
    RET(0);
}
static void sdl_GetWindowTitle(RcnCtx *ctx) {
    static u64 name;
    if (!name) name = text(ctx, "");
    RET(name);
}
static void sdl_RegisterEvents(RcnCtx *ctx) { RET(0x8000); }

// SDL_GameControllerAddMappingsFromRW(rw, freesrc) : la manette de l'hôte a déjà sa disposition.
static void sdl_AddMappingsFromRW(RcnCtx *ctx) { RET(0); }

// --- Écran ---

static void display_size(u32 *w, u32 *h) {
    *w = DISPLAY_WIDTH;
    *h = DISPLAY_HEIGHT;
}
// SDL_DisplayMode : format, w, h, refresh_rate, driverdata.
static void write_mode(u64 mode) {
    u32 w, h;
    display_size(&w, &h);
    wr32(mode, SDL_PIXELFORMAT_RGB888);
    wr32(mode + 4, w);
    wr32(mode + 8, h);
    wr32(mode + 12, 60);
    wr64(mode + 16, 0);
}
static void sdl_GetDisplayMode(RcnCtx *ctx) { // (display, index, &mode)
    write_mode(A2);
    RET(0);
}
static void sdl_GetCurrentDisplayMode(RcnCtx *ctx) { // (display, &mode)
    write_mode(A1);
    RET(0);
}
// SDL_GetClosestDisplayMode(display, &wanted, &closest) : le seul mode.
static void sdl_GetClosestDisplayMode(RcnCtx *ctx) {
    write_mode(A2);
    RET(A2);
}
static void sdl_GetDisplayBounds(RcnCtx *ctx) { // (display, &rect)
    u32 w, h;
    display_size(&w, &h);
    wr32(A1, 0);
    wr32(A1 + 4, 0);
    wr32(A1 + 8, w);
    wr32(A1 + 12, h);
    RET(0);
}
static void sdl_GetDisplayName(RcnCtx *ctx) {
    static u64 name;
    if (!name) name = text(ctx, "RomCloud");
    RET(name);
}

// --- Fenêtre ---

// SDL_CreateWindow(title, x, y, w, h, flags)
static int fullscreen, resizable; // état de la fenêtre
static void sdl_CreateWindow(RcnCtx *ctx) {
    if (!sdl.window) sdl.window = block(ctx, 64);
    sdl.width = (u32)STK(3) ? (u32)STK(3) : 1280;
    sdl.height = (u32)STK(4) ? (u32)STK(4) : 720;
    resizable = ((u32)STK(5) & SDL_WINDOW_RESIZABLE) != 0;
    if (ctx->proc->trace) fprintf(stderr, "SDL_CreateWindow %ux%u flags %x\n", sdl.width, sdl.height, (u32)STK(5));
    if (sdl.context) rcn_sdlgl_resize(sdl.width, sdl.height);
    RET(sdl.window);
}
static void sdl_GetWindowSize(RcnCtx *ctx) { // (window, &w, &h)
    if (A1) wr32(A1, sdl.width);
    if (A2) wr32(A2, sdl.height);
}
static void sdl_SetWindowSize(RcnCtx *ctx) { // (window, w, h)
    if ((i32)A1 > 0 && (i32)A2 > 0 && !fullscreen) window_resized((u32)A1, (u32)A2);
}
// SDL_SetWindowFullscreen(window, flags) : plein écran (SDL_WINDOW_FULLSCREEN 1, …_DESKTOP 0x1001) :
// la fenêtre prend la taille de l'écran, comme sous Windows.
static void sdl_SetWindowFullscreen(RcnCtx *ctx) {
    fullscreen = ((u32)A1 & 1) != 0;
    if (fullscreen) {
        u32 w, h;
        display_size(&w, &h);
        window_resized(w, h);
    }
    RET(0);
}
static void sdl_GetWindowPosition(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    if (A2) wr32(A2, 0);
}
static void sdl_GetWindowFlags(RcnCtx *ctx) {
    RET(SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS | (fullscreen ? 0x1001 : 0) |
        (resizable ? SDL_WINDOW_RESIZABLE : 0));
}
// SDL_SetWindowResizable(window, oui) : FNA ne suit les changements de taille que d'une fenêtre redimensionnable.
static void sdl_SetWindowResizable(RcnCtx *ctx) { resizable = (u32)A1 != 0; }
static void sdl_GetWindowID(RcnCtx *ctx) { RET(WINDOW_ID); }
// SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right) : sans bords.
static void sdl_GetWindowBordersSize(RcnCtx *ctx) {
    for (int i = 1; i <= 4; i++) {
        if (STK(i)) wr32(STK(i), 0);
    }
    RET(0);
}
// SDL_GetWindowWMInfo(window, &info) : fenêtre Windows (sous-système 1), poignée fictive.
static void sdl_GetWindowWMInfo(RcnCtx *ctx) {
    u64 info = A1;
    wr32(info + 4, 1);
    wr64(info + 8, 0x10001);
    wr64(info + 16, 0);
    wr64(info + 24, 0);
    RET(1);
}

// --- OpenGL : contexte de l'hôte ---

#if defined(__ANDROID__)

static void *gl_proc(const char *name) {
    void *p = (void *)eglGetProcAddress(name);
    return p ? p : dlsym(RTLD_DEFAULT, name);
}
static int platform_create(RcnCtx *ctx) {
    static const EGLint config_attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                                               EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    static const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    static const EGLint pbuffer_attributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLint count = 0;
    (void)ctx;
    sdl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (sdl.display == EGL_NO_DISPLAY || !eglInitialize(sdl.display, NULL, NULL)) return 0;
    if (!eglChooseConfig(sdl.display, config_attributes, &sdl.config, 1, &count) || count < 1) return 0;
    sdl.egl = eglCreateContext(sdl.display, sdl.config, EGL_NO_CONTEXT, context_attributes);
    if (sdl.egl == EGL_NO_CONTEXT) return 0;
    // Surface d'attente tant que la fenêtre de l'application n'est pas là.
    sdl.pbuffer = eglCreatePbufferSurface(sdl.display, sdl.config, pbuffer_attributes);
    sdl.surface_egl = EGL_NO_SURFACE;
    return eglMakeCurrent(sdl.display, sdl.pbuffer, sdl.pbuffer, sdl.egl);
}
static void platform_make_current(void) {
    EGLSurface s = sdl.surface_egl != EGL_NO_SURFACE ? sdl.surface_egl : sdl.pbuffer;
    eglMakeCurrent(sdl.display, s, s, sdl.egl);
}
// Surface sur la fenêtre actuelle de l'application ; 0 si elle n'existe pas.
static int attach_window(RcnCtx *ctx) {
    const RcnHost *host = ctx->proc->host;
    u32 generation = 0;
    ANativeWindow *window = host->window ? host->window(host->user, &generation) : NULL;
    if (!window) return 0;
    if (sdl.surface_egl != EGL_NO_SURFACE && generation == sdl.generation) {
        ANativeWindow_release(window);
        return 1;
    }
    if (sdl.surface_egl != EGL_NO_SURFACE) {
        eglMakeCurrent(sdl.display, sdl.pbuffer, sdl.pbuffer, sdl.egl);
        eglDestroySurface(sdl.display, sdl.surface_egl);
    }
    ANativeWindow_setBuffersGeometry(window, 0, 0, 0);
    sdl.surface_egl = eglCreateWindowSurface(sdl.display, sdl.config, window, NULL);
    ANativeWindow_release(window);
    sdl.generation = generation;
    if (sdl.surface_egl == EGL_NO_SURFACE) return 0;
    platform_make_current();
    eglSwapInterval(sdl.display, 0); // cadence tenue par rcn_present
    return 1;
}
static void platform_swap(RcnCtx *ctx) {
    if (attach_window(ctx)) {
        EGLint w = 0, h = 0;
        eglQuerySurface(sdl.display, sdl.surface_egl, EGL_WIDTH, &w);
        eglQuerySurface(sdl.display, sdl.surface_egl, EGL_HEIGHT, &h);
        rcn_sdlgl_blit(w, h);
        if (!eglSwapBuffers(sdl.display, sdl.surface_egl)) sdl.generation = ~0u;
    }
    // Essais en ligne de commande (RCN_FRAMES) : image relue aux numéros enregistrés par l'hôte par défaut.
    static u64 count;
    static u8 *pixels;
    const char *every = getenv("RCN_FRAME_EVERY");
    u64 step = every ? (u64)atoll(every) : 60;
    u32 w = 0, h = 0;
    rcn_sdlgl_size(&w, &h);
    if (getenv("RCN_FRAMES") && step && ++count % step == 0 && w && h) {
        free(pixels);
        pixels = malloc((size_t)w * h * 4);
        if (pixels) rcn_sdlgl_read(pixels);
        rcn_present(ctx, pixels, w, h, w * 4, 32, sdl.swap_interval != 0);
        return;
    }
    rcn_present(ctx, NULL, sdl.width, sdl.height, 0, 32, sdl.swap_interval != 0);
}

#elif defined(_WIN32)

static void *opengl32;
static void *gl_proc(const char *name) {
    void *p = wglGetProcAddress(name);
    // wglGetProcAddress rend parfois 1, 2, 3 ou -1 au lieu de NULL.
    if ((uintptr_t)p <= 3 || (intptr_t)p == -1) p = opengl32 ? GetProcAddress(opengl32, name) : NULL;
    return p;
}
static int platform_create(RcnCtx *ctx) {
    (void)ctx;
    static const u8 descriptor[40] = {40, 0, 1, 0, 0x24, 0, 0, 0, 0, 32};
    void *window = CreateWindowExA(0, "STATIC", "rcn-sdl", 0, 0, 0, 16, 16, NULL, NULL, NULL, NULL);
    sdl.dc = window ? GetDC(window) : NULL;
    int format = sdl.dc ? ChoosePixelFormat(sdl.dc, descriptor) : 0;
    if (!format || !SetPixelFormat(sdl.dc, format, descriptor)) return 0;
    sdl.wgl = wglCreateContext(sdl.dc);
    opengl32 = LoadLibraryA("opengl32.dll");
    return sdl.wgl && wglMakeCurrent(sdl.dc, sdl.wgl);
}
static void platform_make_current(void) { wglMakeCurrent(sdl.dc, sdl.wgl); }
// Essais sur PC : l'image est relue et remise à l'hôte (fichiers BMP, voir rcn_gui.c).
static void platform_swap(RcnCtx *ctx) {
    u32 w, h;
    rcn_sdlgl_size(&w, &h);
    if (!w || !h) return;
    static size_t size;
    if ((size_t)w * h * 4 > size) {
        free(sdl.pixels);
        size = (size_t)w * h * 4;
        sdl.pixels = malloc(size);
    }
    if (!sdl.pixels) return;
    rcn_sdlgl_read(sdl.pixels);
    rcn_present(ctx, sdl.pixels, w, h, w * 4, 32, sdl.swap_interval != 0);
}

#else

static void *gl_proc(const char *name) { (void)name; return NULL; }
static int platform_create(RcnCtx *ctx) { (void)ctx; return 0; }
static void platform_make_current(void) {}
static void platform_swap(RcnCtx *ctx) { rcn_present(ctx, NULL, sdl.width, sdl.height, 0, 32, 1); }

#endif

// SDL_GL_CreateContext(window) : le seul contexte, courant sur le thread appelant.
static void sdl_GL_CreateContext(RcnCtx *ctx) {
    if (!sdl.context) {
#if defined(__ANDROID__)
        int es = 1;
#else
        int es = 0;
#endif
        if (!platform_create(ctx) || !rcn_sdlgl_load(gl_proc, es)) {
            set_error("OpenGL indisponible");
            RET(0);
            return;
        }
        sdl.context = block(ctx, 16);
        rcn_sdlgl_resize(sdl.width ? sdl.width : 1280, sdl.height ? sdl.height : 720);
    } else {
        platform_make_current();
    }
    RET(sdl.context);
}
// SDL_GL_MakeCurrent(window, context)
static void sdl_GL_MakeCurrent(RcnCtx *ctx) {
    if (A1 && sdl.context) platform_make_current();
    RET(0);
}
// SDL_GL_ExtensionSupported(nom) : d'après le contexte courant.
static void sdl_GL_ExtensionSupported(RcnCtx *ctx) {
    int yes = sdl.context && A0 && rcn_sdlgl_extension(STR(A0));
    if (ctx->proc->trace) fprintf(stderr, "SDL_GL_ExtensionSupported(%s) = %d\n", A0 ? STR(A0) : "", yes);
    RET(yes);
}
static void sdl_GL_GetCurrentContext(RcnCtx *ctx) { RET(sdl.context); }
static void sdl_GL_GetProcAddress(RcnCtx *ctx) {
    u64 p = A0 && sdl.context ? rcn_sdlgl_proc(ctx, STR(A0)) : 0;
    if (ctx->proc->trace) fprintf(stderr, " [%s]", A0 ? STR(A0) : "?");
    RET(p);
}
static void sdl_GL_SetSwapInterval(RcnCtx *ctx) {
    sdl.swap_interval = (i32)A0;
    RET(0);
}
static void sdl_GL_GetSwapInterval(RcnCtx *ctx) { RET_I32(sdl.swap_interval); }
static void sdl_GL_SwapWindow(RcnCtx *ctx) {
    rcn_sdlgl_frame_stats();
    if (sdl.context) platform_swap(ctx);
}

// --- Événements ---

// SDL_PollEvent(&event) : 1 si un événement a été copié.
static void sdl_PollEvent(RcnCtx *ctx) {
    const RcnHost *host = ctx->proc->host;
    if (!sdl.devices_announced && host->pad) {
        sdl.devices_announced = 1;
        push_event(SDL_JOYDEVICEADDED, 0);
        push_event(SDL_CONTROLLERDEVICEADDED, 0);
    }
    if (!sdl.quit_sent && (rcn_pad(ctx) & RCN_PAD_QUIT)) {
        sdl.quit_sent = 1;
        push_event(SDL_QUIT, 0);
    }
    pthread_mutex_lock(&sdl.lock);
    int got = sdl.queue_count > 0;
    if (got) {
        if (A0) memcpy(PTR(A0), sdl.queue[sdl.queue_head], SDL_EVENT_SIZE);
        sdl.queue_head = (sdl.queue_head + 1) % 16;
        sdl.queue_count--;
    }
    pthread_mutex_unlock(&sdl.lock);
    RET(got);
}

// --- Souris ---

static void sdl_GetMouseState(RcnCtx *ctx) { // (&x, &y) → boutons
    if (A0) wr32(A0, 0);
    if (A1) wr32(A1, 0);
    RET(0);
}
static void sdl_Cursor(RcnCtx *ctx) {
    if (!sdl.cursor) sdl.cursor = block(ctx, 16);
    RET(sdl.cursor);
}

// --- Manette (disposition Xbox de SDL) ---

static int has_pad(RcnCtx *ctx) { return ctx->proc->host->pad != NULL; }
// Détails de la manette : celle de l'hôte, présentée comme une manette Xbox 360 (Microsoft 045e:028e).
static void sdl_ControllerAttached(RcnCtx *ctx) { RET(has_pad(ctx) ? 1 : 0); }
static void sdl_GameControllerFromInstanceID(RcnCtx *ctx) { RET((i32)A0 == 0 ? sdl.controller : 0); }
static u64 pad_name(RcnCtx *ctx) {
    static u64 name;
    if (!name) name = text(ctx, "Xbox 360 Controller");
    return name;
}
static void sdl_ControllerNameForIndex(RcnCtx *ctx) { RET((i32)A0 == 0 && has_pad(ctx) ? pad_name(ctx) : 0); }
static void sdl_JoystickName(RcnCtx *ctx) { RET(pad_name(ctx)); }
static void sdl_Vendor(RcnCtx *ctx) { RET(0x045E); }
static void sdl_Product(RcnCtx *ctx) { RET(0x028E); }
static void sdl_DeviceInstanceID(RcnCtx *ctx) { RET_I32((i32)A0 == 0 && has_pad(ctx) ? 0 : -1); }
static void sdl_NumJoysticks(RcnCtx *ctx) { RET(has_pad(ctx) ? 1 : 0); }
static void sdl_IsGameController(RcnCtx *ctx) { RET((i32)A0 == 0 && has_pad(ctx)); }
static void sdl_GameControllerOpen(RcnCtx *ctx) {
    if ((i32)A0 != 0 || !has_pad(ctx)) {
        RET(0);
        return;
    }
    if (!sdl.controller) sdl.controller = block(ctx, 16);
    if (!sdl.joystick) sdl.joystick = block(ctx, 16);
    RET(sdl.controller);
}
static void sdl_JoystickOpen(RcnCtx *ctx) {
    if (!sdl.joystick) sdl.joystick = block(ctx, 16);
    RET((i32)A0 == 0 && has_pad(ctx) ? sdl.joystick : 0);
}
static void sdl_GameControllerGetJoystick(RcnCtx *ctx) {
    if (!sdl.joystick) sdl.joystick = block(ctx, 16);
    RET(sdl.joystick);
}
static void sdl_JoystickFromInstanceID(RcnCtx *ctx) { RET((i32)A0 == 0 ? sdl.joystick : 0); }
static void sdl_GameControllerName(RcnCtx *ctx) {
    static u64 name;
    if (!name) name = text(ctx, "Xbox Controller");
    RET(name);
}
static void sdl_GameControllerMapping(RcnCtx *ctx) {
    RET(text(ctx, "00000000000000000000000000000000,Xbox Controller,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,"
                  "leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,"
                  "dpright:b14,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,"));
}
// SDL_JoystickGetGUID(joystick) : structure de 16 octets rendue par un pointeur caché (rcx).
static void sdl_JoystickGetGUID(RcnCtx *ctx) {
    u64 out = A0;
    memset(PTR(out), 0, 16);
    wr8(out, 3); // bus USB
    RET(out);
}
// SDL_JoystickGetGUIDString(guid, tampon, taille) : guid (16 octets) passé par adresse, en hexadécimal.
static void sdl_JoystickGetGUIDString(RcnCtx *ctx) {
    u64 guid = A0, out = A1;
    i32 size = (i32)A2;
    if (!out || size <= 0) return;
    char text[33];
    for (int i = 0; i < 16; i++) snprintf(text + 2 * i, 3, "%02x", guid ? rd8(guid + (u64)i) : 0);
    i32 n = size - 1 < 32 ? size - 1 : 32;
    memcpy(PTR(out), text, (size_t)n);
    wr8(out + (u64)n, 0);
}
// SDL_GameControllerGetBindForButton / ForAxis(controller, n) : SDL_GameControllerButtonBind (12 octets,
// pointeur caché en rcx) : type (1 bouton, 2 axe), puis le numéro — celui de la disposition annoncée.
static void sdl_GameControllerGetBindForButton(RcnCtx *ctx) {
    u64 out = A0;
    i32 button = (i32)A2;
    int ok = button >= 0 && button < 15;
    wr32(out, ok ? 1 : 0), wr32(out + 4, ok ? (u32)button : 0), wr32(out + 8, 0);
    RET(out);
}
static void sdl_GameControllerGetBindForAxis(RcnCtx *ctx) {
    u64 out = A0;
    i32 axis = (i32)A2;
    int ok = axis >= 0 && axis < 6;
    wr32(out, ok ? 2 : 0), wr32(out + 4, ok ? (u32)axis : 0), wr32(out + 8, 0);
    RET(out);
}
// Boutons SDL_CONTROLLER_BUTTON_* : A, B, X, Y, BACK, GUIDE, START, LEFTSTICK, RIGHTSTICK, LEFTSHOULDER,
// RIGHTSHOULDER, DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT. La croix est la vraie croix (pas le stick).
static void sdl_GameControllerGetButton(RcnCtx *ctx) {
    static const u32 bits[15] = {RCN_PAD_A, RCN_PAD_B, RCN_PAD_X, RCN_PAD_Y, RCN_PAD_SELECT, 0, RCN_PAD_START, RCN_PAD_L3,
                                 RCN_PAD_R3, RCN_PAD_L, RCN_PAD_R, RCN_PAD_UP, RCN_PAD_DOWN, RCN_PAD_LEFT, RCN_PAD_RIGHT};
    u32 button = (u32)A1;
    RET(button < 15 && bits[button] && (rcn_pad_buttons(ctx) & bits[button]) ? 1 : 0);
}
// Axes : LEFTX, LEFTY, RIGHTX, RIGHTY, TRIGGERLEFT, TRIGGERRIGHT (ceux de l'hôte, dans le même ordre).
static void sdl_GameControllerGetAxis(RcnCtx *ctx) {
    i16 axes[6];
    rcn_pad_axes(ctx, axes);
    u32 axis = (u32)A1;
    RET((u64)(u16)(axis < 6 ? axes[axis] : 0));
}static void sdl_JoystickNumButtons(RcnCtx *ctx) { RET(15); }
static void sdl_JoystickNumAxes(RcnCtx *ctx) { RET(6); }
static void sdl_JoystickGetButton(RcnCtx *ctx) { sdl_GameControllerGetButton(ctx); }
static void sdl_JoystickGetAxis(RcnCtx *ctx) { sdl_GameControllerGetAxis(ctx); }
static void sdl_MinusOne(RcnCtx *ctx) { RET_I32(-1); }

const RcnShim rcn_sdl2[] = {
    {"SDL_ClearError", sdl_ClearError, 0},
    {"SDL_CreateColorCursor", sdl_Cursor, 0},
    {"SDL_CreateSystemCursor", sdl_Cursor, 0},
    {"SDL_CreateWindow", sdl_CreateWindow, 0},
    {"SDL_DestroyWindow", sdl_Nothing, 0},
    {"SDL_DisableScreenSaver", sdl_Nothing, 0},
    {"SDL_FreeCursor", sdl_Nothing, 0},
    {"SDL_GL_CreateContext", sdl_GL_CreateContext, 0},
    {"SDL_GL_DeleteContext", sdl_Nothing, 0},
    {"SDL_GL_GetCurrentContext", sdl_GL_GetCurrentContext, 0},
    {"SDL_GL_GetProcAddress", sdl_GL_GetProcAddress, 0},
    {"SDL_GL_GetSwapInterval", sdl_GL_GetSwapInterval, 0},
    {"SDL_GL_MakeCurrent", sdl_GL_MakeCurrent, 0},
    {"SDL_GL_SetAttribute", sdl_Zero, 0},
    {"SDL_GL_SetSwapInterval", sdl_GL_SetSwapInterval, 0},
    {"SDL_GL_SwapWindow", sdl_GL_SwapWindow, 0},
    {"SDL_GameControllerAddMapping", sdl_One, 0},
    {"SDL_GameControllerAddMappingsFromRW", sdl_AddMappingsFromRW, 0},
    {"SDL_GameControllerClose", sdl_Nothing, 0},
    {"SDL_GameControllerGetAxis", sdl_GameControllerGetAxis, 0},
    {"SDL_GameControllerGetButton", sdl_GameControllerGetButton, 0},
    {"SDL_GameControllerGetJoystick", sdl_GameControllerGetJoystick, 0},
    {"SDL_GameControllerMapping", sdl_GameControllerMapping, 0},
    {"SDL_GameControllerName", sdl_GameControllerName, 0},
    {"SDL_GameControllerOpen", sdl_GameControllerOpen, 0},
    {"SDL_GetClosestDisplayMode", sdl_GetClosestDisplayMode, 0},
    {"SDL_GetCurrentDisplayMode", sdl_GetCurrentDisplayMode, 0},
    {"SDL_GetDesktopDisplayMode", sdl_GetCurrentDisplayMode, 0},
    {"SDL_GetDisplayBounds", sdl_GetDisplayBounds, 0},
    {"SDL_GetDisplayMode", sdl_GetDisplayMode, 0},
    {"SDL_GetDisplayName", sdl_GetDisplayName, 0},
    {"SDL_GetError", sdl_GetError, 0},
    {"SDL_GetPlatform", sdl_GetPlatform, 0},
    {"SDL_SetMainReady", sdl_Zero, 0},
    {"SDL_GetGlobalMouseState", sdl_GetMouseState, 0},
    {"SDL_GetHint", sdl_Null, 0},
    {"SDL_GetModState", sdl_Zero, 0},
    {"SDL_GetMouseState", sdl_GetMouseState, 0},
    {"SDL_GetNumDisplayModes", sdl_One, 0},
    {"SDL_GetNumVideoDisplays", sdl_One, 0},
    {"SDL_GetVersion", sdl_GetVersion, 0},
    {"SDL_GetWindowBordersSize", sdl_GetWindowBordersSize, 0},
    {"SDL_GetWindowDisplayIndex", sdl_Zero, 0},
    {"SDL_GetWindowFlags", sdl_GetWindowFlags, 0},
    {"SDL_GetWindowID", sdl_GetWindowID, 0},
    {"SDL_GetWindowPosition", sdl_GetWindowPosition, 0},
    {"SDL_GetWindowSize", sdl_GetWindowSize, 0},
    {"SDL_GetWindowWMInfo", sdl_GetWindowWMInfo, 0},
    {"SDL_HapticClose", sdl_Nothing, 0},
    {"SDL_HapticEffectSupported", sdl_Zero, 0},
    {"SDL_HapticNewEffect", sdl_MinusOne, 0},
    {"SDL_HapticOpen", sdl_Null, 0},
    {"SDL_HapticOpenFromJoystick", sdl_Null, 0},
    {"SDL_HapticRumbleInit", sdl_MinusOne, 0},
    {"SDL_HapticRumblePlay", sdl_MinusOne, 0},
    {"SDL_HapticRumbleSupported", sdl_Zero, 0},
    {"SDL_HapticRunEffect", sdl_MinusOne, 0},
    {"SDL_HapticStopAll", sdl_MinusOne, 0},
    {"SDL_HapticUpdateEffect", sdl_MinusOne, 0},
    {"SDL_Init", sdl_Zero, 0},
    {"SDL_IsGameController", sdl_IsGameController, 0},
    {"SDL_JoystickClose", sdl_Nothing, 0},
    {"SDL_JoystickFromInstanceID", sdl_JoystickFromInstanceID, 0},
    {"SDL_JoystickGetAxis", sdl_JoystickGetAxis, 0},
    {"SDL_JoystickGetButton", sdl_JoystickGetButton, 0},
    {"SDL_JoystickGetGUID", sdl_JoystickGetGUID, 0},
    {"SDL_JoystickGetHat", sdl_Zero, 0},
    {"SDL_JoystickInstanceID", sdl_Zero, 0},
    {"SDL_JoystickIsHaptic", sdl_Zero, 0},
    {"SDL_JoystickNumAxes", sdl_JoystickNumAxes, 0},
    {"SDL_JoystickNumButtons", sdl_JoystickNumButtons, 0},
    {"SDL_JoystickNumHats", sdl_Zero, 0},
    {"SDL_JoystickOpen", sdl_JoystickOpen, 0},
    {"SDL_LoadBMP_RW", sdl_Null, 0},
    {"SDL_NumJoysticks", sdl_NumJoysticks, 0},
    {"SDL_PollEvent", sdl_PollEvent, 0},
    {"SDL_PumpEvents", sdl_Nothing, 0},
    {"SDL_Quit", sdl_Nothing, 0},
    {"SDL_SetCursor", sdl_Nothing, 0},
    {"SDL_SetHint", sdl_One, 0},
    {"SDL_SetRelativeMouseMode", sdl_Zero, 0},
    {"SDL_SetWindowBordered", sdl_Nothing, 0},
    {"SDL_SetWindowFullscreen", sdl_SetWindowFullscreen, 0},
    {"SDL_SetWindowIcon", sdl_Nothing, 0},
    {"SDL_SetWindowPosition", sdl_Nothing, 0},
    {"SDL_SetWindowResizable", sdl_SetWindowResizable, 0},
    {"SDL_SetWindowSize", sdl_SetWindowSize, 0},
    {"SDL_SetWindowTitle", sdl_Nothing, 0},
    {"SDL_ShowCursor", sdl_Zero, 0},
    {"SDL_ShowSimpleMessageBox", sdl_ShowSimpleMessageBox, 0},
    {"SDL_ShowWindow", sdl_Nothing, 0},
    {"SDL_StartTextInput", sdl_Nothing, 0},
    {"SDL_StopTextInput", sdl_Nothing, 0},
    {"SDL_WarpMouseInWindow", sdl_Nothing, 0},
    {"SDL_AddEventWatch", sdl_Nothing, 0},
    {"SDL_CaptureMouse", sdl_Zero, 0},
    {"SDL_DelEventWatch", sdl_Nothing, 0},
    {"SDL_Delay", sdl_Delay, 0},
    {"SDL_EnableScreenSaver", sdl_Nothing, 0},
    {"SDL_EventState", sdl_One, 0},
    {"SDL_FlushEvent", sdl_Nothing, 0},
    {"SDL_FlushEvents", sdl_Nothing, 0},
    {"SDL_GL_ExtensionSupported", sdl_GL_ExtensionSupported, 0},
    {"SDL_GL_GetAttribute", sdl_GL_GetAttribute, 0},
    {"SDL_GL_GetDrawableSize", sdl_GL_GetDrawableSize, 0},
    {"SDL_GL_ResetAttributes", sdl_Nothing, 0},
    {"SDL_GameControllerEventState", sdl_One, 0},
    {"SDL_GameControllerFromInstanceID", sdl_GameControllerFromInstanceID, 0},
    {"SDL_GameControllerGetAttached", sdl_ControllerAttached, 0},
    {"SDL_GameControllerGetProduct", sdl_Product, 0},
    {"SDL_GameControllerGetProductVersion", sdl_Zero, 0},
    {"SDL_GameControllerGetVendor", sdl_Vendor, 0},
    {"SDL_GameControllerNameForIndex", sdl_ControllerNameForIndex, 0},
    {"SDL_GameControllerRumble", sdl_Zero, 0},
    {"SDL_GameControllerUpdate", sdl_Nothing, 0},
    {"SDL_GetBasePath", sdl_GetBasePath, 0},
    {"SDL_GetCPUCount", sdl_GetCPUCount, 0},
    {"SDL_GetCurrentVideoDriver", sdl_GetCurrentVideoDriver, 0},
    {"SDL_GetDisplayDPI", sdl_GetDisplayDPI, 0},
    {"SDL_GetDisplayUsableBounds", sdl_GetDisplayBounds, 0},
    {"SDL_GetEventFilter", sdl_Zero, 0},
    {"SDL_GetHintBoolean", sdl_GetHintBoolean, 0},
    {"SDL_GetKeyFromScancode", sdl_GetKeyFromScancode, 0},
    {"SDL_GetKeyboardFocus", sdl_Window, 0},
    {"SDL_GetKeyboardState", sdl_GetKeyboardState, 0},
    {"SDL_GetMouseFocus", sdl_Window, 0},
    {"SDL_GetNumTouchDevices", sdl_Zero, 0},
    {"SDL_GetPerformanceCounter", sdl_GetPerformanceCounter, 0},
    {"SDL_GetPerformanceFrequency", sdl_GetPerformanceFrequency, 0},
    {"SDL_GetPowerInfo", sdl_Zero, 0},
    {"SDL_GetPrefPath", sdl_GetPrefPath, 0},
    {"SDL_GetRelativeMouseMode", sdl_Zero, 0},
    {"SDL_GetRelativeMouseState", sdl_GetMouseState, 0},
    {"SDL_GetRevision", sdl_GetRevision, 0},
    {"SDL_GetScancodeFromKey", sdl_GetScancodeFromKey, 0},
    {"SDL_GetSystemRAM", sdl_GetSystemRAM, 0},
    {"SDL_GetTicks", sdl_GetTicks, 0},
    {"SDL_GetWindowDisplayMode", sdl_GetWindowDisplayMode, 0},
    {"SDL_GetWindowFromID", sdl_Window, 0},
    {"SDL_GetWindowGrab", sdl_Zero, 0},
    {"SDL_GetWindowTitle", sdl_GetWindowTitle, 0},
    {"SDL_HasEvent", sdl_Zero, 0},
    {"SDL_HasEvents", sdl_Zero, 0},
    {"SDL_HideWindow", sdl_Nothing, 0},
    {"SDL_InitSubSystem", sdl_Zero, 0},
    {"SDL_IsScreenKeyboardShown", sdl_Zero, 0},
    {"SDL_IsTextInputActive", sdl_Zero, 0},
    {"SDL_JoystickEventState", sdl_One, 0},
    {"SDL_JoystickGetAttached", sdl_ControllerAttached, 0},
    {"SDL_JoystickGetDeviceInstanceID", sdl_DeviceInstanceID, 0},
    {"SDL_JoystickGetDeviceProduct", sdl_Product, 0},
    {"SDL_JoystickGetDeviceProductVersion", sdl_Zero, 0},
    {"SDL_JoystickGetDeviceVendor", sdl_Vendor, 0},
    {"SDL_JoystickGetProduct", sdl_Product, 0},
    {"SDL_JoystickGetProductVersion", sdl_Zero, 0},
    {"SDL_JoystickGetVendor", sdl_Vendor, 0},
    {"SDL_JoystickGetType", sdl_One, 0},          // SDL_JOYSTICK_TYPE_GAMECONTROLLER
    {"SDL_JoystickGetDeviceType", sdl_One, 0},
    {"SDL_JoystickCurrentPowerLevel", sdl_MinusOne, 0}, // SDL_JOYSTICK_POWER_UNKNOWN
    {"SDL_JoystickGetGUIDString", sdl_JoystickGetGUIDString, 0},
    {"SDL_JoystickGetDeviceGUID", sdl_JoystickGetGUID, 0},
    {"SDL_GameControllerGetBindForButton", sdl_GameControllerGetBindForButton, 0},
    {"SDL_GameControllerGetBindForAxis", sdl_GameControllerGetBindForAxis, 0},
    {"SDL_GameControllerMappingForGUID", sdl_GameControllerMapping, 0},
    {"SDL_GameControllerMappingForDeviceIndex", sdl_GameControllerMapping, 0},
    {"SDL_JoystickName", sdl_JoystickName, 0},
    {"SDL_JoystickNameForIndex", sdl_ControllerNameForIndex, 0},
    {"SDL_JoystickRumble", sdl_Zero, 0},
    {"SDL_JoystickUpdate", sdl_Nothing, 0},
    {"SDL_Log", sdl_Nothing, 0},
    {"SDL_LogSetAllPriority", sdl_Nothing, 0},
    {"SDL_LogSetOutputFunction", sdl_Nothing, 0},
    {"SDL_MaximizeWindow", sdl_Nothing, 0},
    {"SDL_MinimizeWindow", sdl_Nothing, 0},
    {"SDL_NumHaptics", sdl_Zero, 0},
    {"SDL_NumSensors", sdl_Zero, 0},
    {"SDL_PeepEvents", sdl_Zero, 0},
    {"SDL_PushEvent", sdl_One, 0},
    {"SDL_QuitSubSystem", sdl_Nothing, 0},
    {"SDL_RaiseWindow", sdl_Nothing, 0},
    {"SDL_RegisterEvents", sdl_RegisterEvents, 0},
    {"SDL_RestoreWindow", sdl_Nothing, 0},
    {"SDL_SetEventFilter", sdl_Nothing, 0},
    {"SDL_SetHintWithPriority", sdl_One, 0},
    {"SDL_SetModState", sdl_Nothing, 0},
    {"SDL_SetTextInputRect", sdl_Nothing, 0},
    {"SDL_SetWindowGrab", sdl_Nothing, 0},
    {"SDL_SetWindowInputFocus", sdl_Zero, 0},
    {"SDL_SetWindowMaximumSize", sdl_Nothing, 0},
    {"SDL_SetWindowMinimumSize", sdl_Nothing, 0},
    {"SDL_SetWindowsMessageHook", sdl_Nothing, 0},
    {"SDL_WasInit", sdl_WasInit, 0},
};
const size_t rcn_sdl2_count = sizeof rcn_sdl2 / sizeof rcn_sdl2[0];
