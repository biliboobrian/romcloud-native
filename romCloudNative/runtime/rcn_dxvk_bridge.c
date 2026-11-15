// RomCloud Native : Direct3D 11 et DXGI du jeu servis par DXVK (Vulkan), au lieu de la couche OpenGL ES
// (rcn_d3d11*.c). Android seulement, quand DXVK est livré dans l'application (RCN_HAS_DXVK) et choisi
// (RCN_D3D=dxvk).
//
// Chaque objet de DXVK est montré au jeu par un enveloppeur : sa table virtuelle (relais rcn_thunk vers
// les fonctions de rcn_dxvk_bridge_gen.h, qui lisent les arguments selon la convention Windows x64) et
// l'objet natif. Le jeu et DXVK partagent l'espace d'adresses : structures et tampons passent tels quels,
// seules les interfaces sont converties (et les chaînes larges : WCHAR fait 2 octets sous Windows, 4 ici).
#if defined(RCN_HAS_DXVK)
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <android/log.h>
#include <android/native_window.h>

#include <d3d11_4.h>
#include <dxgi1_6.h>

#include "rcn_gui.h"
#include "rcn_internal.h"
#include "rcn_vulkan.h"

typedef struct {
    const char *name;
    RcnFn fn;
} BrMethod;

enum { BR_MAX_ARRAY = 128, BR_FAM_DYNAMIC = -1 };
#define BR_MAGIC 0x314b5658444e4352ull // « RCNDXVK1 »

// Enveloppeur vu par le jeu : table virtuelle d'abord (convention COM).
typedef struct BrWrap {
    u64 vtable;
    u64 magic;
    IUnknown *native;
    int family;
    struct BrWrap *next; // même case de la table de hachage
} BrWrap;

// Journal du pont : sortie du jeu et journal d'Android (adb logcat -s rcn-dxvk).
#define br_log(...)                                                     \
    do {                                                                \
        fprintf(stderr, "[dxvk] " __VA_ARGS__);                         \
        __android_log_print(ANDROID_LOG_INFO, "rcn-dxvk", __VA_ARGS__); \
    } while (0)

static void br_unsupported(RcnCtx *ctx, const char *what) {
    static int told;
    if (told++ < 32) br_log("méthode non servie : %s\n", what);
    RET((u64)(i64)(i32)E_NOTIMPL);
}

// Échec rendu par DXVK (les premiers seulement, et jamais ceux d'une attente normale).
static void br_failed(const char *what, HRESULT r) {
    static int told;
    if ((u32)r == 0x887A000Au || (u32)r == 0x00000001u) return; // DXGI_ERROR_WAS_STILL_DRAWING, S_FALSE
    if (told++ < 64) br_log("%s : échec 0x%08x\n", what, (unsigned)r);
}

static void *br_unwrap(u64 guest) {
    if (!guest) return NULL;
    const BrWrap *w = (const BrWrap *)(uintptr_t)guest;
    // Objet qui n'est pas à nous (interface écrite par le jeu) : passé tel quel.
    return w->magic == BR_MAGIC ? w->native : (void *)(uintptr_t)guest;
}

static float br_float_arg(RcnCtx *ctx, int n) {
    if (n < 4) return ctx->xmm[n].f[0];
    u32 bits = (u32)rcn_arg(ctx, n);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static void br_wrap_out(RcnCtx *ctx, u64 guest, void *native, int family);
static void br_out(RcnCtx *ctx, u64 guest, void *native, int family) { br_wrap_out(ctx, guest, native, family); }
static void br_out_iid(RcnCtx *ctx, u64 guest, void *native, const IID *iid, HRESULT *r);
static void br_before_present(RcnCtx *ctx);
static void br_after_present(RcnCtx *ctx, IDXGISwapChain *chain, UINT interval);

// Comptes d'appels par méthode (et de réponses S_FALSE : « pas encore prêt »), notés toutes les 5 s
// avec RCN_DXVK_CALLS=1. Comptés sans verrou : approximatifs entre threads, suffisants pour un essai.
static u32 br_calls[1024], br_false[1024];

#include "rcn_dxvk_bridge_gen.h"

static void br_dump_calls(void) {
    static int enabled = -1;
    static u64 last;
    if (enabled < 0) enabled = getenv("RCN_DXVK_CALLS") != NULL;
    if (!enabled) return;
    u64 now = rcn_now_ns();
    if (!last) last = now;
    if (now - last < 5000000000ull) return;
    last = now;
    char line[1024];
    size_t n = 0;
    // Les 24 méthodes les plus appelées depuis la dernière note.
    for (int k = 0; k < 24; k++) {
        int best = -1;
        for (int i = 0; i < BR_CALL_COUNT; i++)
            if (br_calls[i] && (best < 0 || br_calls[i] > br_calls[best])) best = i;
        if (best < 0) break;
        n += (size_t)snprintf(line + n, sizeof line - n, "%s %u%s%.0u ", br_call_names[best], br_calls[best],
                              br_false[best] ? "/faux:" : "", br_false[best]);
        br_calls[best] = 0;
        br_false[best] = 0;
        if (n >= sizeof line - 64) break;
    }
    memset(br_calls, 0, sizeof br_calls);
    memset(br_false, 0, sizeof br_false);
    br_log("appels (5 s) : %s\n", line);
}

// --- Enveloppeurs ---
static pthread_mutex_t br_lock = PTHREAD_MUTEX_INITIALIZER;
static BrWrap *br_table[4096];
static u64 br_vtables[BR_FAMILY_COUNT];

static size_t br_hash(const void *native, int family) {
    return (((uintptr_t)native >> 4) * 0x9E3779B97F4A7C15ull + (u64)family) >> 52;
}

static int br_family_of_iid(const IID *iid) {
    for (size_t i = 0; i < sizeof br_iid_family / sizeof br_iid_family[0]; i++)
        if (memcmp(br_iid_family[i].iid, iid, sizeof *iid) == 0) return br_iid_family[i].family;
    return memcmp(iid, &IID_IUnknown, sizeof *iid) == 0 ? BR_FAM_DYNAMIC : -2;
}

// Famille d'un objet dont l'interface n'est pas connue d'avance (IUnknown, ID3D11Resource…) : la première
// qui lui répond, dans l'ordre de FAMILIES.
static int br_probe_family(IUnknown *native) {
    for (int f = 0; f < BR_FAMILY_COUNT; f++) {
        void *p = NULL;
        if (SUCCEEDED(native->lpVtbl->QueryInterface(native, br_family_iid[f], &p)) && p) {
            ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);
            if (p == (void *)native) return f;
        }
    }
    return -1;
}

// Enveloppeur de l'objet natif (une référence passe au jeu : celle que DXVK vient de donner).
static u64 br_wrap(RcnCtx *ctx, void *native_ptr, int family) {
    IUnknown *native = native_ptr;
    if (!native) return 0;
    if (family == BR_FAM_DYNAMIC) family = br_probe_family(native);
    if (family < 0) {
        br_log("objet d'interface inconnue %p\n", native_ptr);
        return 0;
    }
    pthread_mutex_lock(&br_lock);
    size_t h = br_hash(native, family);
    BrWrap *w = br_table[h];
    while (w && !(w->native == native && w->family == family)) w = w->next;
    if (!w) {
        if (!br_vtables[family]) {
            u32 n = br_family_method_count[family];
            u64 *table = calloc(n, sizeof *table);
            for (u32 i = 0; i < n; i++)
                table[i] = rcn_thunk(ctx, br_family_name[family], br_family_methods[family][i].name, br_family_methods[family][i].fn, 0);
            br_vtables[family] = (u64)(uintptr_t)table;
        }
        w = calloc(1, sizeof *w);
        w->vtable = br_vtables[family];
        w->magic = BR_MAGIC;
        w->native = native;
        w->family = family;
        w->next = br_table[h];
        br_table[h] = w;
    }
    pthread_mutex_unlock(&br_lock);
    return (u64)(uintptr_t)w;
}

static void br_wrap_out(RcnCtx *ctx, u64 guest, void *native, int family) {
    u64 w = br_wrap(ctx, native, family);
    if (!w && native) ((IUnknown *)native)->lpVtbl->Release((IUnknown *)native);
    wr64(guest, w);
}

static void br_out_iid(RcnCtx *ctx, u64 guest, void *native, const IID *iid, HRESULT *r) {
    if (!native) {
        wr64(guest, 0);
        return;
    }
    int family = br_family_of_iid(iid);
    if (family == -2) {
        // Interface que le pont ne sert pas : refusée au jeu plutôt que donnée sans conversion.
        br_log("interface non servie {%08x-%04x-%04x}\n", (unsigned)iid->Data1, iid->Data2, iid->Data3);
        ((IUnknown *)native)->lpVtbl->Release((IUnknown *)native);
        wr64(guest, 0);
        if (r) *r = E_NOINTERFACE;
        return;
    }
    br_wrap_out(ctx, guest, native, family);
}

// L'objet natif disparaît : ses enveloppeurs sortent de la table. Ceux des objets que DXVK garde en vie
// (appareil, contexte, chaîne d'images…) restent valides en mémoire, au cas où le jeu les utiliserait
// après sa dernière référence ; les autres sont libérés.
static void br_forget(IUnknown *native) {
    pthread_mutex_lock(&br_lock);
    for (int f = 0; f < BR_FAMILY_COUNT; f++) {
        BrWrap **link = &br_table[br_hash(native, f)];
        while (*link) {
            BrWrap *w = *link;
            if (w->native == native) {
                *link = w->next;
                w->magic = 0;
                if (f >= 10) free(w); // à partir de ID3D11Buffer : ressources, vues, états, shaders
            } else {
                link = &w->next;
            }
        }
    }
    pthread_mutex_unlock(&br_lock);
}

// --- IUnknown ---
static void br_special_QueryInterface(RcnCtx *ctx) {
    IUnknown *self = br_unwrap(A0);
    u64 out = A2;
    if (!self || !out) {
        RET((u64)(i64)(i32)E_POINTER);
        return;
    }
    const IID *iid = (const IID *)(uintptr_t)A1;
    void *p = NULL;
    HRESULT r = self->lpVtbl->QueryInterface(self, iid, &p);
    if (SUCCEEDED(r)) br_out_iid(ctx, out, p, iid, &r);
    else {
        wr64(out, 0);
        br_log("QueryInterface {%08x-%04x-%04x} : 0x%08x\n", (unsigned)iid->Data1, iid->Data2, iid->Data3, (unsigned)r);
    }
    RET((u64)(i64)r);
}

static void br_special_AddRef(RcnCtx *ctx) {
    IUnknown *self = br_unwrap(A0);
    RET(self ? self->lpVtbl->AddRef(self) : 0);
}

static void br_special_Release(RcnCtx *ctx) {
    IUnknown *self = br_unwrap(A0);
    if (!self) {
        RET(0);
        return;
    }
    ULONG n = self->lpVtbl->Release(self);
    if (n == 0) br_forget(self);
    RET(n);
}

// --- Chaînes larges ---
// Descriptions qui commencent par un tableau de WCHAR : copié en 2 octets par caractère, le reste de la
// structure (même disposition des deux côtés, alignements compris) recopié derrière.
static void br_wide_desc(u64 guest, const void *native, size_t native_size, size_t chars) {
    const wchar_t *s = native;
    u16 *g = (u16 *)(uintptr_t)guest;
    for (size_t i = 0; i < chars; i++) g[i] = (u16)s[i];
    memcpy((u8 *)g + 2 * chars, (const u8 *)native + sizeof(wchar_t) * chars, native_size - sizeof(wchar_t) * chars);
}

#define WIDE_DESC(fn, iface, method, type, field)                                                 \
    static void fn(RcnCtx *ctx) {                                                                  \
        iface *self = br_unwrap(A0);                                                               \
        if (!self || !A1) {                                                                        \
            RET((u64)(i64)(i32)(self ? DXGI_ERROR_INVALID_CALL : E_POINTER));                      \
            return;                                                                                \
        }                                                                                          \
        type d;                                                                                    \
        memset(&d, 0, sizeof d);                                                                   \
        HRESULT r = self->lpVtbl->method(self, &d);                                                \
        if (SUCCEEDED(r)) br_wide_desc(A1, &d, sizeof d, sizeof d.field / sizeof d.field[0]);      \
        RET((u64)(i64)r);                                                                          \
    }
WIDE_DESC(br_special_IDXGIAdapter_GetDesc, IDXGIAdapter, GetDesc, DXGI_ADAPTER_DESC, Description)
WIDE_DESC(br_special_IDXGIAdapter1_GetDesc1, IDXGIAdapter1, GetDesc1, DXGI_ADAPTER_DESC1, Description)
WIDE_DESC(br_special_IDXGIAdapter2_GetDesc2, IDXGIAdapter2, GetDesc2, DXGI_ADAPTER_DESC2, Description)
WIDE_DESC(br_special_IDXGIAdapter4_GetDesc3, IDXGIAdapter4, GetDesc3, DXGI_ADAPTER_DESC3, Description)
WIDE_DESC(br_special_IDXGIOutput_GetDesc, IDXGIOutput, GetDesc, DXGI_OUTPUT_DESC, DeviceName)
WIDE_DESC(br_special_IDXGIOutput6_GetDesc1, IDXGIOutput6, GetDesc1, DXGI_OUTPUT_DESC1, DeviceName)

static wchar_t *br_wide_string(u64 guest, wchar_t *buf, size_t n) {
    if (!guest) return NULL;
    const u16 *s = (const u16 *)(uintptr_t)guest;
    size_t i = 0;
    for (; i + 1 < n && s[i]; i++) buf[i] = s[i];
    buf[i] = 0;
    return buf;
}

static void br_special_ID3DUserDefinedAnnotation_BeginEvent(RcnCtx *ctx) {
    ID3DUserDefinedAnnotation *self = br_unwrap(A0);
    wchar_t name[256];
    RET((u64)(i64)(self ? self->lpVtbl->BeginEvent(self, br_wide_string(A1, name, 256)) : -1));
}

static void br_special_ID3DUserDefinedAnnotation_SetMarker(RcnCtx *ctx) {
    ID3DUserDefinedAnnotation *self = br_unwrap(A0);
    wchar_t name[256];
    if (self) self->lpVtbl->SetMarker(self, br_wide_string(A1, name, 256));
}

// Statistiques d'images : celles de DXVK sont datées par son horloge, pas par le QueryPerformanceCounter
// du jeu (CNTVCT sur ARM64) ; Unity en tirait une durée d'image fausse et son temps n'avançait plus.
// Toujours « disjointes », comme la couche OpenGL ES : le jeu mesure lui-même.
static void br_special_IDXGISwapChain_GetFrameStatistics(RcnCtx *ctx) { RET(0x887A000Bu); }
static void br_special_IDXGIOutput_GetFrameStatistics(RcnCtx *ctx) { RET(0x887A000Bu); }

// --- Bibliothèques de DXVK et fenêtre ---
typedef HRESULT (*PFN_CreateDevice)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT,
                                    ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (*PFN_CreateDeviceAndSwapChain)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT,
                                                const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **,
                                                D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (*PFN_CreateFactory)(REFIID, void **);
typedef HRESULT (*PFN_CreateFactory2)(UINT, REFIID, void **);
typedef void (*PFN_SetLoader)(PFN_vkGetInstanceProcAddr);
typedef void (*PFN_SetWindow)(ANativeWindow *, uint32_t, uint32_t);

static struct {
    int state; // 0 : pas essayé, 1 : prêt, -1 : échec
    void *dxgi, *d3d11;
    RcnVulkan vk;
    PFN_CreateDevice create_device;
    PFN_CreateDeviceAndSwapChain create_device_sc;
    PFN_CreateFactory create_factory, create_factory1;
    PFN_CreateFactory2 create_factory2;
    PFN_SetWindow set_window[2];
    ANativeWindow *window;
    u32 generation;
} dx;

static void br_update_window(RcnCtx *ctx) {
    const RcnHost *host = ctx->proc->host;
    if (!host->window) return;
    u32 generation = 0;
    ANativeWindow *window = host->window(host->user, &generation);
    if (!window) {
        if (!dx.window) br_log("pas encore de fenêtre\n");
        return;
    }
    if (window == dx.window && generation == dx.generation) {
        ANativeWindow_release(window);
        return;
    }
    // La référence rendue par l'hôte est gardée tant que DXVK dessine dans cette fenêtre.
    if (dx.window) ANativeWindow_release(dx.window);
    dx.window = window;
    dx.generation = generation;
    u32 w = (u32)ANativeWindow_getWidth(window), h = (u32)ANativeWindow_getHeight(window);
    br_log("fenêtre %ux%u (génération %u)\n", w, h, generation);
    for (int i = 0; i < 2; i++)
        if (dx.set_window[i]) dx.set_window[i](window, w, h);
}

static int br_load(RcnCtx *ctx) {
    if (dx.state) return dx.state > 0;
    dx.state = -1;
    char error[512];
    if (!rcn_vk_open_configured(&dx.vk, error, sizeof error)) {
        br_log("pilote Vulkan : %s\n", error);
        return 0;
    }
    dx.dxgi = dlopen("libdxvk_dxgi.so", RTLD_NOW | RTLD_GLOBAL);
    dx.d3d11 = dx.dxgi ? dlopen("libdxvk_d3d11.so", RTLD_NOW | RTLD_GLOBAL) : NULL;
    if (!dx.d3d11) {
        br_log("bibliothèques absentes : %s\n", dlerror());
        return 0;
    }
    void *libs[2] = {dx.dxgi, dx.d3d11};
    for (int i = 0; i < 2; i++) {
        // Chaque bibliothèque a sa copie du chargeur Vulkan et de l'intégration au système de DXVK.
        PFN_SetLoader set = (PFN_SetLoader)dlsym(libs[i], "dxvk_android_set_vulkan_loader");
        if (set) set(dx.vk.get_instance_proc);
        dx.set_window[i] = (PFN_SetWindow)dlsym(libs[i], "dxvk_android_set_window");
    }
    dx.create_device = (PFN_CreateDevice)dlsym(dx.d3d11, "D3D11CreateDevice");
    dx.create_device_sc = (PFN_CreateDeviceAndSwapChain)dlsym(dx.d3d11, "D3D11CreateDeviceAndSwapChain");
    dx.create_factory = (PFN_CreateFactory)dlsym(dx.dxgi, "CreateDXGIFactory");
    dx.create_factory1 = (PFN_CreateFactory)dlsym(dx.dxgi, "CreateDXGIFactory1");
    dx.create_factory2 = (PFN_CreateFactory2)dlsym(dx.dxgi, "CreateDXGIFactory2");
    br_update_window(ctx);
    dx.state = 1;
    br_log("prêt (%s)\n", dx.vk.path);
    return 1;
}

// Avant une chaîne d'images ou une image : la fenêtre d'Android doit exister. L'application ne crée la
// surface du jeu qu'à l'annonce de sa première image : annoncée ici (sans pixels), puis attendue.
static void br_before_present(RcnCtx *ctx) {
    br_update_window(ctx);
    const RcnHost *host = ctx->proc->host;
    if (dx.window || !host->window) return;
    if (host->present) host->present(host->user, NULL, 1920, 1080, 0, RCN_PIXEL_XRGB8888);
    for (int i = 0; i < 250 && !dx.window; i++) {
        rcn_sleep_ms(20);
        br_update_window(ctx);
    }
}

static void br_after_present(RcnCtx *ctx, IDXGISwapChain *chain, UINT interval) {
    DXGI_SWAP_CHAIN_DESC d;
    memset(&d, 0, sizeof d);
    chain->lpVtbl->GetDesc(chain, &d);
    // Rythme des images et entrées du runtime (rien à copier : l'image est déjà dans la fenêtre).
    rcn_present(ctx, NULL, d.BufferDesc.Width, d.BufferDesc.Height, 0, 32, interval != 0);
    br_dump_calls();
}

// --- Fonctions exportées ---
enum { FAM_DEVICE = 8, FAM_CONTEXT = 9, FAM_SWAPCHAIN = 3 };

// D3D11CreateDevice(adapter, type, software, flags, levels, count, sdk, **device, *level, **context)
static void br_D3D11CreateDevice(RcnCtx *ctx) {
    if (!br_load(ctx) || !dx.create_device) {
        RET((u64)(i64)(i32)E_FAIL);
        return;
    }
    u64 out_device = STK(7), out_context = STK(9);
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    HRESULT r = dx.create_device(br_unwrap(A0), (D3D_DRIVER_TYPE)A1, NULL, (UINT)A3, (const D3D_FEATURE_LEVEL *)(uintptr_t)STK(4),
                                 (UINT)STK(5), (UINT)STK(6), out_device ? &device : NULL, (D3D_FEATURE_LEVEL *)(uintptr_t)STK(8),
                                 out_context ? &context : NULL);
    if (out_device) br_out(ctx, out_device, SUCCEEDED(r) ? device : NULL, FAM_DEVICE);
    if (out_context) br_out(ctx, out_context, SUCCEEDED(r) ? context : NULL, FAM_CONTEXT);
    br_log("D3D11CreateDevice : 0x%08x\n", (unsigned)r);
    RET((u64)(i64)r);
}

// D3D11CreateDeviceAndSwapChain(adapter, type, software, flags, levels, count, sdk, swapdesc, **swapchain,
//                               **device, *level, **context)
static void br_D3D11CreateDeviceAndSwapChain(RcnCtx *ctx) {
    if (!br_load(ctx) || !dx.create_device_sc) {
        RET((u64)(i64)(i32)E_FAIL);
        return;
    }
    u64 out_chain = STK(8), out_device = STK(9), out_context = STK(11);
    IDXGISwapChain *chain = NULL;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    HRESULT r = dx.create_device_sc(br_unwrap(A0), (D3D_DRIVER_TYPE)A1, NULL, (UINT)A3, (const D3D_FEATURE_LEVEL *)(uintptr_t)STK(4),
                                    (UINT)STK(5), (UINT)STK(6), (const DXGI_SWAP_CHAIN_DESC *)(uintptr_t)STK(7),
                                    out_chain ? &chain : NULL, out_device ? &device : NULL,
                                    (D3D_FEATURE_LEVEL *)(uintptr_t)STK(10), out_context ? &context : NULL);
    if (out_chain) br_out(ctx, out_chain, SUCCEEDED(r) ? chain : NULL, FAM_SWAPCHAIN);
    if (out_device) br_out(ctx, out_device, SUCCEEDED(r) ? device : NULL, FAM_DEVICE);
    if (out_context) br_out(ctx, out_context, SUCCEEDED(r) ? context : NULL, FAM_CONTEXT);
    br_log("D3D11CreateDeviceAndSwapChain : 0x%08x\n", (unsigned)r);
    RET((u64)(i64)r);
}

static void br_give_factory(RcnCtx *ctx, PFN_CreateFactory create, UINT flags, u64 riid, u64 out) {
    if (!br_load(ctx) || (!create && !dx.create_factory2) || !out) {
        RET((u64)(i64)(i32)(out ? E_FAIL : E_POINTER));
        return;
    }
    const IID *iid = (const IID *)(uintptr_t)riid;
    void *f = NULL;
    HRESULT r = create ? create(iid, &f) : dx.create_factory2(flags, iid, &f);
    if (SUCCEEDED(r)) br_out_iid(ctx, out, f, iid, &r);
    else wr64(out, 0);
    RET((u64)(i64)r);
}
static void br_CreateDXGIFactory(RcnCtx *ctx) { br_give_factory(ctx, dx.create_factory, 0, A0, A1); }
static void br_CreateDXGIFactory1(RcnCtx *ctx) { br_give_factory(ctx, dx.create_factory1, 0, A0, A1); }
static void br_CreateDXGIFactory2(RcnCtx *ctx) { br_give_factory(ctx, NULL, (UINT)A0, A1, A2); }

const RcnShim rcn_dxvk_d3d11_exports[] = {
    {"D3D11CreateDevice", br_D3D11CreateDevice, 0},
    {"D3D11CreateDeviceAndSwapChain", br_D3D11CreateDeviceAndSwapChain, 0},
};
const size_t rcn_dxvk_d3d11_exports_count = sizeof rcn_dxvk_d3d11_exports / sizeof rcn_dxvk_d3d11_exports[0];
const RcnShim rcn_dxvk_dxgi_exports[] = {
    {"CreateDXGIFactory", br_CreateDXGIFactory, 0},
    {"CreateDXGIFactory1", br_CreateDXGIFactory1, 0},
    {"CreateDXGIFactory2", br_CreateDXGIFactory2, 0},
};
const size_t rcn_dxvk_dxgi_exports_count = sizeof rcn_dxvk_dxgi_exports / sizeof rcn_dxvk_dxgi_exports[0];

// DXVK choisi pour ce jeu (RCN_D3D=dxvk, posé par l'application).
int rcn_dxvk_enabled(void) {
    const char *v = getenv("RCN_D3D");
    return v && strcmp(v, "dxvk") == 0;
}
#endif
