// RomCloud Native : DXGI — fabrique, adaptateur « RomCloud », son écran, chaîne d'échange, IDXGIDevice et
// IDXGISurface (voir rcn_d3d11.h).
//
// Un seul adaptateur et un seul écran (1920 × 1080 à 60 Hz, comme user32). La chaîne d'échange a un
// seul tampon arrière, une texture 2D ; Present l'affiche par d3d_gl_present.
#include <stdio.h>
#include <stdlib.h>

#include "rcn_d3d11.h"

#define DISPLAY_W 1920
#define DISPLAY_H 1080

typedef struct {
    D3dObj obj;
    D3dObj *output;
} Adapter;

typedef struct {
    D3dObj obj;
    u64 hwnd;
    u32 width, height, format, count, flags, swap_effect, usage, scaling, alpha;
    int windowed;
    D3dRes *buffer;     // référence tenue
    u32 presents;
    u64 latency_event;  // GetFrameLatencyWaitableObject
    u32 max_latency;
} SwapChain;

typedef struct {
    D3dObj obj;
} DxgiDevice;

static void put_wide(u64 at, u32 chars, const char *text) {
    u16 *w = PTR(at);
    u32 i = 0;
    for (; text[i] && i + 1 < chars; i++) w[i] = (u16)(u8)text[i];
    for (; i < chars; i++) w[i] = 0;
}

// --- Écran ---

// DXGI_MODE_DESC (28 octets) : Width, Height, RefreshRate {60, 1}, Format, ScanlineOrdering, Scaling.
static void put_mode(u64 at, u32 w, u32 h, u32 format) {
    u32 m[7] = {w, h, 60, 1, format, 1 /* progressif */, 0};
    memcpy(PTR(at), m, sizeof m);
}
static const u32 modes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}};
#define MODE_COUNT (sizeof modes / sizeof modes[0])

static void out_GetDesc(RcnCtx *ctx) {
    u64 d = A1;
    if (!d) {
        RET(E_INVALIDARG);
        return;
    }
    memset(PTR(d), 0, 96);
    put_wide(d, 32, "\\\\.\\DISPLAY1");
    i32 rect[4] = {0, 0, DISPLAY_W, DISPLAY_H};
    memcpy(PTR(d + 64), rect, 16);
    wr32(d + 80, 1);             // AttachedToDesktop
    wr32(d + 84, 1);             // DXGI_MODE_ROTATION_IDENTITY
    wr64(d + 88, 0x10001);       // HMONITOR (user32)
    RET(S_OK);
}
// GetDesc1 (DXGI_OUTPUT_DESC1) : GetDesc puis la colorimétrie (sRGB, 8 bits).
static void out_GetDesc1(RcnCtx *ctx) {
    u64 d = A1;
    if (!d) {
        RET(E_INVALIDARG);
        return;
    }
    memset(PTR(d), 0, 152);
    out_GetDesc(ctx);
    wr32(d + 96, 8);  // BitsPerColor
    wr32(d + 100, 0); // DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
    RET(S_OK);
}
// GetDisplayModeList(format, flags, *count, modes) ; GetDisplayModeList1 : DXGI_MODE_DESC1 (32 octets).
static void mode_list(RcnCtx *ctx, u32 size) {
    u32 format = (u32)A1;
    u64 count_at = A3, list = STK(4);
    if (!count_at) {
        RET(E_INVALIDARG);
        return;
    }
    if (!list) {
        wr32(count_at, MODE_COUNT);
        RET(S_OK);
        return;
    }
    u32 n = rd32(count_at) < MODE_COUNT ? rd32(count_at) : MODE_COUNT;
    for (u32 i = 0; i < n; i++) {
        memset(PTR(list + i * size), 0, size);
        put_mode(list + i * size, modes[i][0], modes[i][1], format);
    }
    wr32(count_at, n);
    RET(n < MODE_COUNT ? DXGI_ERROR_MORE_DATA : S_OK);
}
static void out_GetDisplayModeList(RcnCtx *ctx) { mode_list(ctx, 28); }
static void out_GetDisplayModeList1(RcnCtx *ctx) { mode_list(ctx, 32); }
// FindClosestMatchingMode(mode, closest, device) : le mode demandé s'il est dans la liste, sinon le plus grand.
static void closest_mode(RcnCtx *ctx, u32 size) {
    u64 want = A1, out = A2;
    if (!want || !out) {
        RET(E_INVALIDARG);
        return;
    }
    u32 w = rd32(want), h = rd32(want + 4), format = rd32(want + 16);
    u32 best = MODE_COUNT - 1;
    for (u32 i = 0; i < MODE_COUNT; i++) {
        if (modes[i][0] >= w && modes[i][1] >= h) {
            best = i;
            break;
        }
    }
    memset(PTR(out), 0, size);
    put_mode(out, modes[best][0], modes[best][1], format ? format : 28);
    RET(S_OK);
}
static void out_FindClosestMatchingMode(RcnCtx *ctx) { closest_mode(ctx, 28); }
static void out_FindClosestMatchingMode1(RcnCtx *ctx) { closest_mode(ctx, 32); }
static void out_WaitForVBlank(RcnCtx *ctx) {
    rcn_sleep_ms(16);
    RET(S_OK);
}
static void dx_Ok(RcnCtx *ctx) { RET(S_OK); }
static void dx_Unsupported(RcnCtx *ctx) { RET(DXGI_ERROR_UNSUPPORTED); }

static const D3dMethod output_methods[] = {
    {"GetDesc", out_GetDesc}, {"GetDesc1", out_GetDesc1}, {"GetDisplayModeList", out_GetDisplayModeList},
    {"GetDisplayModeList1", out_GetDisplayModeList1}, {"FindClosestMatchingMode", out_FindClosestMatchingMode},
    {"FindClosestMatchingMode1", out_FindClosestMatchingMode1}, {"WaitForVBlank", out_WaitForVBlank},
    {"TakeOwnership", dx_Ok}, {"ReleaseOwnership", dx_Ok}, {"SetGammaControl", dx_Ok},
    {"GetGammaControlCapabilities", dx_Unsupported}, {"GetGammaControl", dx_Unsupported},
    {"GetFrameStatistics", dx_Unsupported}, {"DuplicateOutput", dx_Unsupported}, {"DuplicateOutput1", dx_Unsupported},
    {NULL, NULL},
};

// --- Adaptateur ---

// DXGI_ADAPTER_DESC (304 octets), DESC1 (+Flags), DESC2/3 (+préemption).
static void adapter_desc(RcnCtx *ctx, u32 size) {
    u64 d = A1;
    if (!d) {
        RET(E_INVALIDARG);
        return;
    }
    memset(PTR(d), 0, size);
    put_wide(d, 128, "RomCloud OpenGL");
    wr32(d + 256, 0x10de); // VendorId (NVIDIA : chemins les plus courants des jeux)
    wr32(d + 260, 0x1c82); // DeviceId
    wr64(d + 272, 2048ull << 20); // DedicatedVideoMemory
    wr64(d + 288, 2048ull << 20); // SharedSystemMemory
    wr32(d + 296, 0x1234);        // AdapterLuid
    RET(S_OK);
}
static void ad_GetDesc(RcnCtx *ctx) { adapter_desc(ctx, 304); }
static void ad_GetDesc1(RcnCtx *ctx) { adapter_desc(ctx, 312); }
static void ad_GetDesc2(RcnCtx *ctx) { adapter_desc(ctx, 320); }
static void ad_EnumOutputs(RcnCtx *ctx) {
    Adapter *a = THIS(Adapter);
    u64 out = A2;
    if (out) wr64(out, 0);
    if ((u32)A1 != 0) {
        RET(DXGI_ERROR_NOT_FOUND);
        return;
    }
    if (!a->output) {
        a->output = d3d_new(ctx, K_OUTPUT, sizeof(D3dObj), output_methods);
        if (a->output) {
            a->output->parent = &a->obj;
            d3d_addref(&a->obj);
        }
    }
    d3d_out(out, a->output);
    RET(a->output ? S_OK : E_OUTOFMEMORY);
}
// CheckInterfaceSupport(guid, *umd_version) : Direct3D 10 et 11 « pris en charge ».
static void ad_CheckInterfaceSupport(RcnCtx *ctx) {
    if (A2) wr64(A2, 0x0000001E000B0000ull);
    RET(S_OK);
}
// QueryVideoMemoryInfo(node, group, *info) : budget, utilisation, réservable, réservé.
static void ad_QueryVideoMemoryInfo(RcnCtx *ctx) {
    u64 info = A3;
    if (!info) {
        RET(E_INVALIDARG);
        return;
    }
    u64 v[4] = {2048ull << 20, 256ull << 20, 1024ull << 20, 0};
    memcpy(PTR(info), v, sizeof v);
    RET(S_OK);
}

static const D3dMethod adapter_methods[] = {
    {"EnumOutputs", ad_EnumOutputs}, {"GetDesc", ad_GetDesc}, {"GetDesc1", ad_GetDesc1}, {"GetDesc2", ad_GetDesc2},
    {"GetDesc3", ad_GetDesc2}, {"CheckInterfaceSupport", ad_CheckInterfaceSupport},
    {"QueryVideoMemoryInfo", ad_QueryVideoMemoryInfo}, {"SetVideoMemoryReservation", dx_Ok},
    {"RegisterVideoMemoryBudgetChangeNotificationEvent", dx_Ok}, {"UnregisterVideoMemoryBudgetChangeNotification", dx_Ok},
    {NULL, NULL},
};

static D3dObj *factory_obj;
static D3dObj *factory(RcnCtx *ctx);
D3dObj *d3d_adapter_default(RcnCtx *ctx) {
    static D3dObj *adapter;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    if (!adapter) {
        adapter = d3d_new(ctx, K_ADAPTER, sizeof(Adapter), adapter_methods);
        if (adapter && factory_obj) {
            adapter->parent = factory_obj;
            d3d_addref(factory_obj);
        }
    }
    pthread_mutex_unlock(&lock);
    return adapter;
}

// --- Chaîne d'échange ---

static void sc_make_buffer(RcnCtx *ctx, SwapChain *s) {
    if (s->buffer) d3d_release(&s->buffer->obj);
    // Formats sRGB interdits pour le modèle « flip » ; la texture garde le format demandé.
    u32 desc[11] = {s->width, s->height, 1, 1, s->format, 1, 0, USAGE_DEFAULT,
                    BIND_RENDER_TARGET | BIND_SHADER_RESOURCE, 0, 0};
    s->buffer = d3d_texture_new(ctx, s->obj.device, DIM_TEX2D, desc, 0);
}

static void sc_Present(RcnCtx *ctx) {
    SwapChain *s = THIS(SwapChain);
    u32 interval = (u32)A1, flags = (u32)A2;
    if (flags & 1) { // DXGI_PRESENT_TEST
        RET(S_OK);
        return;
    }
    if (s->obj.device && s->obj.device->immediate) {
        D3dContext *c = s->obj.device->immediate;
        pthread_mutex_lock(&c->lock);
        d3d_gl_present(ctx, s->buffer, interval);
        pthread_mutex_unlock(&c->lock);
    }
    s->presents++;
    if (s->latency_event) rcn_semaphore_release(ctx->proc, s->latency_event, 1);
    RET(S_OK);
}
static void sc_Present1(RcnCtx *ctx) { sc_Present(ctx); }
// GetBuffer(index, riid, **surface)
static void sc_GetBuffer(RcnCtx *ctx) {
    SwapChain *s = THIS(SwapChain);
    u64 iid = A2, out = A3;
    if (out) wr64(out, 0);
    if (!s->buffer) {
        RET(E_FAIL);
        return;
    }
    D3dObj *b = &s->buffer->obj;
    extern D3dObj *d3d_query_extra(RcnCtx * ctx, D3dObj * o, u64 iid);
    D3dObj *found = d3d_has_iface(b, iid) ? b : d3d_query_extra(ctx, b, iid);
    if (!found) {
        RET(E_NOINTERFACE);
        return;
    }
    d3d_out(out, found);
    RET(S_OK);
}
static void sc_SetFullscreenState(RcnCtx *ctx) {
    THIS(SwapChain)->windowed = !(u32)A1;
    RET(S_OK);
}
static void sc_GetFullscreenState(RcnCtx *ctx) {
    SwapChain *s = THIS(SwapChain);
    if (A1) wr32(A1, !s->windowed);
    if (A2) wr64(A2, 0);
    RET(S_OK);
}
// DXGI_SWAP_CHAIN_DESC (72 octets).
static void sc_GetDesc(RcnCtx *ctx) {
    SwapChain *s = THIS(SwapChain);
    u64 d = A1;
    if (!d) {
        RET(E_INVALIDARG);
        return;
    }
    memset(PTR(d), 0, 72);
    put_mode(d, s->width, s->height, s->format);
    wr32(d + 28, 1);
    wr32(d + 36, s->usage);
    wr32(d + 40, s->count);
    wr64(d + 48, s->hwnd);
    wr32(d + 56, s->windowed);
    wr32(d + 60, s->swap_effect);
    wr32(d + 64, s->flags);
    RET(S_OK);
}
// DXGI_SWAP_CHAIN_DESC1 (48 octets).
static void sc_GetDesc1(RcnCtx *ctx) {
    SwapChain *s = THIS(SwapChain);
    u64 d = A1;
    if (!d) {
        RET(E_INVALIDARG);
        return;
    }
    u32 v[12] = {s->width, s->height, s->format, 0, 1, 0, s->usage, s->count, s->scaling, s->swap_effect, s->alpha, s->flags};
    memcpy(PTR(d), v, sizeof v);
    RET(S_OK);
}
// DXGI_SWAP_CHAIN_FULLSCREEN_DESC : RefreshRate, ScanlineOrdering, Scaling, Windowed.
static void sc_GetFullscreenDesc(RcnCtx *ctx) {
    SwapChain *s = THIS(SwapChain);
    if (A1) {
        u32 v[5] = {60, 1, 1, 0, (u32)s->windowed};
        memcpy(PTR(A1), v, sizeof v);
    }
    RET(S_OK);
}
static void sc_GetHwnd(RcnCtx *ctx) {
    if (A1) wr64(A1, THIS(SwapChain)->hwnd);
    RET(S_OK);
}
// ResizeBuffers(count, width, height, format, flags) ; 0 : valeur gardée.
static void sc_ResizeBuffers(RcnCtx *ctx) {
    SwapChain *s = THIS(SwapChain);
    u32 count = (u32)A1, w = (u32)A2, h = (u32)A3, format = (u32)STK(4), flags = (u32)STK(5);
    if (count) s->count = count;
    if (w) s->width = w;
    if (h) s->height = h;
    if (format) s->format = format;
    s->flags = flags;
    if (s->obj.device && s->obj.device->immediate) {
        D3dContext *c = s->obj.device->immediate;
        pthread_mutex_lock(&c->lock);
        d3d_gl_current(ctx);
        sc_make_buffer(ctx, s);
        pthread_mutex_unlock(&c->lock);
    }
    d3d_log("ResizeBuffers %ux%u format %u", s->width, s->height, s->format);
    RET(s->buffer ? S_OK : E_OUTOFMEMORY);
}
static void sc_ResizeBuffers1(RcnCtx *ctx) { sc_ResizeBuffers(ctx); }
static void sc_ResizeTarget(RcnCtx *ctx) { RET(S_OK); }
static void sc_GetContainingOutput(RcnCtx *ctx) {
    D3dObj *adapter = d3d_adapter_default(ctx);
    u64 out = A1;
    if (out) wr64(out, 0);
    // EnumOutputs(0, out) de l'adaptateur.
    u64 saved[3] = {ctx->r[1], ctx->r[2], ctx->r[8]};
    ctx->r[1] = (u64)(uintptr_t)adapter;
    ctx->r[2] = 0;
    ctx->r[8] = out;
    ad_EnumOutputs(ctx);
    ctx->r[1] = saved[0];
    ctx->r[2] = saved[1];
    ctx->r[8] = saved[2];
}
static void sc_GetLastPresentCount(RcnCtx *ctx) {
    if (A1) wr32(A1, THIS(SwapChain)->presents);
    RET(S_OK);
}
static void sc_GetCurrentBackBufferIndex(RcnCtx *ctx) { RET(0); }
// CheckColorSpaceSupport(space, *support) : sRGB seulement (présentation).
static void sc_CheckColorSpaceSupport(RcnCtx *ctx) {
    if (A2) wr32(A2, (u32)A1 == 0 ? 1 : 0);
    RET(S_OK);
}
static void sc_SetMaximumFrameLatency(RcnCtx *ctx) {
    THIS(SwapChain)->max_latency = (u32)A1;
    RET(S_OK);
}
static void sc_GetMaximumFrameLatency(RcnCtx *ctx) {
    if (A1) wr32(A1, THIS(SwapChain)->max_latency);
    RET(S_OK);
}
// Objet d'attente de la latence : sémaphore de compte initial la latence maximale, +1 à chaque Present.
static void sc_GetFrameLatencyWaitableObject(RcnCtx *ctx) {
    SwapChain *s = THIS(SwapChain);
    if (!s->latency_event) s->latency_event = rcn_semaphore_new(ctx, s->max_latency, s->max_latency);
    RET(s->latency_event);
}
static void sc_GetRotation(RcnCtx *ctx) {
    if (A1) wr32(A1, 1);
    RET(S_OK);
}
static void sc_GetSourceSize(RcnCtx *ctx) {
    SwapChain *s = THIS(SwapChain);
    if (A1) wr32(A1, s->width);
    if (A2) wr32(A2, s->height);
    RET(S_OK);
}
static void sc_GetFrameStatistics(RcnCtx *ctx) { RET(0x887A000Bu); } // DXGI_ERROR_FRAME_STATISTICS_DISJOINT
static void sc_IsTemporaryMonoSupported(RcnCtx *ctx) { RET(0); }

static void sc_destroy(D3dObj *o) {
    SwapChain *s = (SwapChain *)o;
    if (s->buffer) d3d_release(&s->buffer->obj);
}

static const D3dMethod swapchain_methods[] = {
    {"Present", sc_Present}, {"Present1", sc_Present1}, {"GetBuffer", sc_GetBuffer},
    {"SetFullscreenState", sc_SetFullscreenState}, {"GetFullscreenState", sc_GetFullscreenState},
    {"GetDesc", sc_GetDesc}, {"GetDesc1", sc_GetDesc1}, {"GetFullscreenDesc", sc_GetFullscreenDesc},
    {"GetHwnd", sc_GetHwnd}, {"ResizeBuffers", sc_ResizeBuffers}, {"ResizeBuffers1", sc_ResizeBuffers1},
    {"ResizeTarget", sc_ResizeTarget}, {"GetContainingOutput", sc_GetContainingOutput},
    {"GetLastPresentCount", sc_GetLastPresentCount}, {"GetCurrentBackBufferIndex", sc_GetCurrentBackBufferIndex},
    {"CheckColorSpaceSupport", sc_CheckColorSpaceSupport}, {"SetColorSpace1", dx_Ok}, {"SetHDRMetaData", dx_Ok},
    {"SetMaximumFrameLatency", sc_SetMaximumFrameLatency}, {"GetMaximumFrameLatency", sc_GetMaximumFrameLatency},
    {"GetFrameLatencyWaitableObject", sc_GetFrameLatencyWaitableObject}, {"SetBackgroundColor", dx_Ok},
    {"SetRotation", dx_Ok}, {"GetRotation", sc_GetRotation}, {"SetSourceSize", dx_Ok}, {"GetSourceSize", sc_GetSourceSize},
    {"GetFrameStatistics", sc_GetFrameStatistics}, {"IsTemporaryMonoSupported", sc_IsTemporaryMonoSupported},
    {"SetMatrixTransform", dx_Ok},
    {NULL, NULL},
};

static D3dDevice *device_of(RcnCtx *ctx, u64 unknown) {
    D3dObj *o = OBJ(unknown);
    if (!o) return NULL;
    if (o->kind == K_DEVICE) return (D3dDevice *)o;
    if (o->kind == K_DXGIDEVICE || o->kind == K_CONTEXT) return o->device;
    (void)ctx;
    return NULL;
}

static SwapChain *swapchain_new(RcnCtx *ctx, D3dDevice *device, u64 hwnd, u32 w, u32 h, u32 format, u32 count,
                                u32 usage, u32 effect, u32 flags, int windowed) {
    SwapChain *s = d3d_new(ctx, K_SWAPCHAIN, sizeof(SwapChain), swapchain_methods);
    if (!s) return NULL;
    s->obj.device = device;
    s->obj.destroy = sc_destroy;
    s->obj.parent = factory_obj;
    d3d_addref(factory_obj);
    s->hwnd = hwnd;
    // Taille 0 : celle de la fenêtre (l'écran entier ici).
    s->width = w ? w : DISPLAY_W;
    s->height = h ? h : DISPLAY_H;
    s->format = format;
    s->count = count;
    s->usage = usage;
    s->swap_effect = effect;
    s->flags = flags;
    s->windowed = windowed;
    s->max_latency = 1; // valeur de DXGI pour une chaîne « waitable »
    D3dContext *c = device->immediate;
    pthread_mutex_lock(&c->lock);
    if (d3d_gl_current(ctx)) sc_make_buffer(ctx, s);
    pthread_mutex_unlock(&c->lock);
    d3d_log("chaîne d'échange %ux%u format %u, %u tampons, effet %u, options 0x%x", s->width, s->height, format, count,
            effect, flags);
    if (!s->buffer) {
        d3d_release(&s->obj);
        return NULL;
    }
    return s;
}

// --- Fabrique ---

static void fa_EnumAdapters(RcnCtx *ctx) {
    u64 out = A2;
    if (out) wr64(out, 0);
    if ((u32)A1 != 0) {
        RET(DXGI_ERROR_NOT_FOUND);
        return;
    }
    d3d_out(out, d3d_adapter_default(ctx));
    RET(S_OK);
}
// EnumAdapterByLuid(luid, riid, **adapter) ; EnumAdapterByGpuPreference(index, preference, riid, **adapter).
static void give_adapter(RcnCtx *ctx, u64 iid, u64 out) {
    D3dObj *a = d3d_adapter_default(ctx);
    if (out) wr64(out, 0);
    if (!d3d_has_iface(a, iid)) {
        RET(E_NOINTERFACE);
        return;
    }
    d3d_out(out, a);
    RET(S_OK);
}
static void fa_EnumAdapterByLuid(RcnCtx *ctx) { give_adapter(ctx, A2, A3); }
static void fa_EnumAdapterByGpuPreference(RcnCtx *ctx) {
    if ((u32)A1 != 0) {
        if (STK(4)) wr64(STK(4), 0);
        RET(DXGI_ERROR_NOT_FOUND);
        return;
    }
    give_adapter(ctx, A3, STK(4));
}
static void fa_MakeWindowAssociation(RcnCtx *ctx) { RET(S_OK); }
static void fa_GetWindowAssociation(RcnCtx *ctx) {
    if (A1) wr64(A1, 0);
    RET(S_OK);
}
static void fa_IsCurrent(RcnCtx *ctx) { RET(1); }
static void fa_False(RcnCtx *ctx) { RET(0); }
// Chaîne d'échange d'un DXGI_SWAP_CHAIN_DESC (aussi pour D3D11CreateDeviceAndSwapChain).
u32 d3d_dxgi_create_swapchain(RcnCtx *ctx, D3dDevice *device, u64 d, u64 out) {
    factory(ctx);
    if (out) wr64(out, 0);
    if (!device || !d || !out) return E_INVALIDARG;
    SwapChain *s = swapchain_new(ctx, device, rd64(d + 48), rd32(d), rd32(d + 4), rd32(d + 16), rd32(d + 40), rd32(d + 36),
                                 rd32(d + 60), rd32(d + 64), (int)rd32(d + 56));
    if (!s) return E_FAIL;
    wr64(out, (u64)(uintptr_t)s);
    return S_OK;
}
// CreateSwapChain(device, DXGI_SWAP_CHAIN_DESC *, **swapchain)
static void fa_CreateSwapChain(RcnCtx *ctx) { RET(d3d_dxgi_create_swapchain(ctx, device_of(ctx, A1), A2, A3)); }
// CreateSwapChainForHwnd(device, hwnd, DXGI_SWAP_CHAIN_DESC1 *, fullscreen_desc, restrict, **swapchain)
static void fa_CreateSwapChainForHwnd(RcnCtx *ctx) {
    D3dDevice *device = device_of(ctx, A1);
    u64 hwnd = A2, d = A3, fs = STK(4), out = STK(6);
    if (out) wr64(out, 0);
    if (!device || !d) {
        RET(E_INVALIDARG);
        return;
    }
    const u32 *v = PTR(d);
    SwapChain *s = swapchain_new(ctx, device, hwnd, v[0], v[1], v[2], v[7], v[6], v[9], v[11], fs ? (int)rd32(fs + 16) : 1);
    if (!s) {
        RET(E_FAIL);
        return;
    }
    s->scaling = v[8];
    s->alpha = v[10];
    wr64(out, (u64)(uintptr_t)s);
    RET(S_OK);
}
// CheckFeatureSupport(feature, data, size) : rien (ni « tearing »).
static void fa_CheckFeatureSupport(RcnCtx *ctx) {
    if (A2 && A3) memset(PTR(A2), 0, (size_t)A3);
    RET(S_OK);
}
static void fa_GetCreationFlags(RcnCtx *ctx) { RET(0); }

static const D3dMethod factory_methods[] = {
    {"EnumAdapters", fa_EnumAdapters}, {"EnumAdapters1", fa_EnumAdapters}, {"EnumAdapterByLuid", fa_EnumAdapterByLuid},
    {"EnumAdapterByGpuPreference", fa_EnumAdapterByGpuPreference}, {"EnumWarpAdapter", dx_Unsupported},
    {"MakeWindowAssociation", fa_MakeWindowAssociation}, {"GetWindowAssociation", fa_GetWindowAssociation},
    {"CreateSwapChain", fa_CreateSwapChain}, {"CreateSwapChainForHwnd", fa_CreateSwapChainForHwnd},
    {"IsCurrent", fa_IsCurrent}, {"IsWindowedStereoEnabled", fa_False}, {"CheckFeatureSupport", fa_CheckFeatureSupport},
    {"GetCreationFlags", fa_GetCreationFlags}, {"RegisterOcclusionStatusWindow", dx_Ok},
    {"RegisterOcclusionStatusEvent", dx_Ok}, {"UnregisterOcclusionStatus", dx_Ok}, {"RegisterStereoStatusWindow", dx_Ok},
    {"RegisterStereoStatusEvent", dx_Ok}, {"UnregisterStereoStatus", dx_Ok}, {"RegisterAdaptersChangedEvent", dx_Ok},
    {"UnregisterAdaptersChangedNotification", dx_Ok},
    {NULL, NULL},
};

static D3dObj *factory(RcnCtx *ctx) {
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    if (!factory_obj) factory_obj = d3d_new(ctx, K_FACTORY, sizeof(D3dObj), factory_methods);
    pthread_mutex_unlock(&lock);
    return factory_obj;
}

// CreateDXGIFactory / CreateDXGIFactory1(riid, **factory) ; CreateDXGIFactory2(flags, riid, **factory).
static void give_factory(RcnCtx *ctx, u64 iid, u64 out) {
    D3dObj *f = factory(ctx);
    if (out) wr64(out, 0);
    if (!f || !d3d_has_iface(f, iid)) {
        RET(E_NOINTERFACE);
        return;
    }
    d3d_out(out, f);
    RET(S_OK);
}
static void dxgi_CreateDXGIFactory(RcnCtx *ctx) { give_factory(ctx, A0, A1); }
static void dxgi_CreateDXGIFactory2(RcnCtx *ctx) { give_factory(ctx, A1, A2); }
static void dxgi_GetDebugInterface(RcnCtx *ctx) {
    if (A2) wr64(A2, 0);
    RET(E_NOINTERFACE);
}
static void dxgi_Ok(RcnCtx *ctx) { RET(S_OK); }

const RcnShim rcn_dxgi_exports[] = {
    {"CreateDXGIFactory", dxgi_CreateDXGIFactory, 0},
    {"CreateDXGIFactory1", dxgi_CreateDXGIFactory, 0},
    {"CreateDXGIFactory2", dxgi_CreateDXGIFactory2, 0},
    {"DXGIGetDebugInterface1", dxgi_GetDebugInterface, 0},
    {"DXGIDeclareAdapterRemovalSupport", dxgi_Ok, 0},
};
const size_t rcn_dxgi_exports_count = sizeof rcn_dxgi_exports / sizeof rcn_dxgi_exports[0];

// --- IDXGIDevice d'un appareil ---

static void dd_GetAdapter(RcnCtx *ctx) {
    d3d_out(A1, d3d_adapter_default(ctx));
    RET(S_OK);
}
static void dd_GetGPUThreadPriority(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    RET(S_OK);
}
static void dd_GetMaximumFrameLatency(RcnCtx *ctx) {
    if (A1) wr32(A1, 3);
    RET(S_OK);
}
// EnqueueSetEvent(event) : le GPU a tout fini (glFinish fait au besoin par l'appelant).
static void dd_EnqueueSetEvent(RcnCtx *ctx) {
    rcn_event_signal(ctx->proc, A1);
    RET(S_OK);
}
static void dd_Trim(RcnCtx *ctx) { (void)ctx; }

static const D3dMethod dxgi_device_methods[] = {
    {"GetAdapter", dd_GetAdapter}, {"SetGPUThreadPriority", dx_Ok}, {"GetGPUThreadPriority", dd_GetGPUThreadPriority},
    {"SetMaximumFrameLatency", dx_Ok}, {"GetMaximumFrameLatency", dd_GetMaximumFrameLatency},
    {"EnqueueSetEvent", dd_EnqueueSetEvent}, {"Trim", dd_Trim}, {"OfferResources", dx_Ok}, {"OfferResources1", dx_Ok},
    {"ReclaimResources", dx_Ok}, {"ReclaimResources1", dx_Ok},
    {NULL, NULL},
};

D3dObj *d3d_dxgi_device_new(RcnCtx *ctx, D3dDevice *device) {
    D3dObj *o = d3d_new(ctx, K_DXGIDEVICE, sizeof(DxgiDevice), dxgi_device_methods);
    if (!o) return NULL;
    o->device = device;
    o->parent = d3d_adapter_default(ctx);
    d3d_addref(o->parent);
    return o;
}

// --- IDXGISurface d'une texture ---

// GetDesc(DXGI_SURFACE_DESC *) : Width, Height, Format, SampleDesc.
static void su_GetDesc(RcnCtx *ctx) {
    D3dRes *t = (D3dRes *)THIS(D3dObj)->parent;
    if (A1 && t) {
        u32 v[5] = {t->width, t->height, t->format, t->samples ? t->samples : 1, 0};
        memcpy(PTR(A1), v, sizeof v);
    }
    RET(S_OK);
}
// GetResource(riid, **resource, *subresource)
static void su_GetResource(RcnCtx *ctx) {
    D3dObj *t = THIS(D3dObj)->parent;
    if (A3) wr32(A3, 0);
    if (A2) wr64(A2, 0);
    if (!t || !d3d_has_iface(t, A1)) {
        RET(E_NOINTERFACE);
        return;
    }
    d3d_out(A2, t);
    RET(S_OK);
}

static const D3dMethod surface_methods[] = {
    {"GetDesc", su_GetDesc}, {"GetResource", su_GetResource}, {"Map", dx_Unsupported}, {"Unmap", dx_Ok},
    {"GetDC", dx_Unsupported}, {"ReleaseDC", dx_Ok},
    {NULL, NULL},
};

D3dObj *d3d_surface_new(RcnCtx *ctx, D3dRes *texture) {
    D3dObj *o = d3d_new(ctx, K_SURFACE, sizeof(D3dObj), surface_methods);
    if (!o) return NULL;
    o->device = texture->obj.device;
    o->parent = &texture->obj;
    d3d_addref(&texture->obj);
    return o;
}
