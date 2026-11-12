// RomCloud Native : SDL2 pour les jeux 64 bits qui l'emploient (MonoGame « DesktopGL », FNA…). Le jeu
// charge « SDL2.dll » et y prend ses fonctions par GetProcAddress : ce sont celles-ci, sur l'hôte.
//
// Une seule fenêtre, plein écran, avec un contexte OpenGL (rcn_sdl_gl.c). La manette de l'hôte est une
// manette de jeu SDL (disposition Xbox) ; souris et clavier sont absents pour l'instant.
#include <stdio.h>
#include <stdlib.h>

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
// SDL_RWFromMem(mem, size) : flux en mémoire ; seuls le début et la taille sont gardés.
static void sdl_RWFromMem(RcnCtx *ctx) {
    u64 rw = block(ctx, 64);
    if (rw) {
        wr64(rw + 32, A0);
        wr64(rw + 40, A1);
    }
    RET(rw);
}
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
static void sdl_CreateWindow(RcnCtx *ctx) {
    if (!sdl.window) sdl.window = block(ctx, 64);
    sdl.width = (u32)STK(3) ? (u32)STK(3) : 1280;
    sdl.height = (u32)STK(4) ? (u32)STK(4) : 720;
    if (sdl.context) rcn_sdlgl_resize(sdl.width, sdl.height);
    RET(sdl.window);
}
static void sdl_GetWindowSize(RcnCtx *ctx) { // (window, &w, &h)
    if (A1) wr32(A1, sdl.width);
    if (A2) wr32(A2, sdl.height);
}
static void sdl_SetWindowSize(RcnCtx *ctx) { // (window, w, h)
    if ((i32)A1 > 0 && (i32)A2 > 0) {
        sdl.width = (u32)A1;
        sdl.height = (u32)A2;
        if (sdl.context) rcn_sdlgl_resize(sdl.width, sdl.height);
    }
}
static void sdl_GetWindowPosition(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    if (A2) wr32(A2, 0);
}
static void sdl_GetWindowFlags(RcnCtx *ctx) {
    RET(SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS);
}
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
static void sdl_Surface(RcnCtx *ctx) {
    if (!sdl.surface) sdl.surface = block(ctx, 96);
    RET(sdl.surface);
}

// --- Manette (disposition Xbox de SDL) ---

static int has_pad(RcnCtx *ctx) { return ctx->proc->host->pad != NULL; }
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
// Boutons SDL_CONTROLLER_BUTTON_* : A, B, X, Y, BACK, GUIDE, START, LEFTSTICK, RIGHTSTICK, LEFTSHOULDER,
// RIGHTSHOULDER, DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT.
static void sdl_GameControllerGetButton(RcnCtx *ctx) {
    static const u32 bits[15] = {RCN_PAD_A, RCN_PAD_B, RCN_PAD_X, RCN_PAD_Y, RCN_PAD_SELECT, 0, RCN_PAD_START, 0, 0,
                                 RCN_PAD_L, RCN_PAD_R, RCN_PAD_UP, RCN_PAD_DOWN, RCN_PAD_LEFT, RCN_PAD_RIGHT};
    u32 button = (u32)A1;
    RET(button < 15 && bits[button] && (rcn_pad(ctx) & bits[button]) ? 1 : 0);
}
// Axes : LEFTX, LEFTY, RIGHTX, RIGHTY, TRIGGERLEFT, TRIGGERRIGHT. La croix guide aussi le stick gauche.
static void sdl_GameControllerGetAxis(RcnCtx *ctx) {
    u32 pad = rcn_pad(ctx), axis = (u32)A1;
    i32 v = 0;
    if (axis == 0) v = (pad & RCN_PAD_LEFT) ? -32768 : (pad & RCN_PAD_RIGHT) ? 32767 : 0;
    if (axis == 1) v = (pad & RCN_PAD_UP) ? -32768 : (pad & RCN_PAD_DOWN) ? 32767 : 0;
    RET((u64)(u16)(i16)v);
}
static void sdl_JoystickNumButtons(RcnCtx *ctx) { RET(15); }
static void sdl_JoystickNumAxes(RcnCtx *ctx) { RET(6); }
static void sdl_JoystickGetButton(RcnCtx *ctx) { sdl_GameControllerGetButton(ctx); }
static void sdl_JoystickGetAxis(RcnCtx *ctx) { sdl_GameControllerGetAxis(ctx); }
static void sdl_MinusOne(RcnCtx *ctx) { RET_I32(-1); }

const RcnShim rcn_sdl2[] = {
    {"SDL_ClearError", sdl_ClearError, 0},
    {"SDL_CreateColorCursor", sdl_Cursor, 0},
    {"SDL_CreateRGBSurfaceFrom", sdl_Surface, 0},
    {"SDL_CreateSystemCursor", sdl_Cursor, 0},
    {"SDL_CreateWindow", sdl_CreateWindow, 0},
    {"SDL_DestroyWindow", sdl_Nothing, 0},
    {"SDL_DisableScreenSaver", sdl_Nothing, 0},
    {"SDL_FreeCursor", sdl_Nothing, 0},
    {"SDL_FreeSurface", sdl_Nothing, 0},
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
    {"SDL_RWFromMem", sdl_RWFromMem, 0},
    {"SDL_SetCursor", sdl_Nothing, 0},
    {"SDL_SetHint", sdl_One, 0},
    {"SDL_SetRelativeMouseMode", sdl_Zero, 0},
    {"SDL_SetWindowBordered", sdl_Nothing, 0},
    {"SDL_SetWindowFullscreen", sdl_Zero, 0},
    {"SDL_SetWindowIcon", sdl_Nothing, 0},
    {"SDL_SetWindowPosition", sdl_Nothing, 0},
    {"SDL_SetWindowResizable", sdl_Nothing, 0},
    {"SDL_SetWindowSize", sdl_SetWindowSize, 0},
    {"SDL_SetWindowTitle", sdl_Nothing, 0},
    {"SDL_ShowCursor", sdl_Zero, 0},
    {"SDL_ShowSimpleMessageBox", sdl_Zero, 0},
    {"SDL_ShowWindow", sdl_Nothing, 0},
    {"SDL_StartTextInput", sdl_Nothing, 0},
    {"SDL_StopTextInput", sdl_Nothing, 0},
    {"SDL_WarpMouseInWindow", sdl_Nothing, 0},
    {"SDL_free", sdl_Nothing, 0},
};
const size_t rcn_sdl2_count = sizeof rcn_sdl2 / sizeof rcn_sdl2[0];
