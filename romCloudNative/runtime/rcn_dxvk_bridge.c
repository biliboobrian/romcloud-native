// RomCloud Native : Direct3D 11 et DXGI du jeu servis par DXVK (Vulkan), au lieu de la couche OpenGL ES
// (rcn_d3d11*.c). Android, quand DXVK est livré dans l'application (RCN_HAS_DXVK) et choisi (RCN_D3D=dxvk).
// Sur PC (hôte Windows, essais), le même pont sert le Direct3D 11 de Windows lui-même (RCN_D3D=dxvk) : rendu
// complet du jeu, dans une fenêtre de l'hôte, images enregistrées par RCN_FRAMES.
//
// Chaque objet de DXVK est montré au jeu par un enveloppeur : sa table virtuelle (relais rcn_thunk vers
// les fonctions de rcn_dxvk_bridge_gen.h, qui lisent les arguments selon la convention Windows x64) et
// l'objet natif. Le jeu et DXVK partagent l'espace d'adresses : structures et tampons passent tels quels,
// seules les interfaces sont converties (et les chaînes larges : WCHAR fait 2 octets sous Windows, 4 ici).
#if defined(RCN_HAS_DXVK) || defined(_WIN32)
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define BR_WINDOWS 1
#define COBJMACROS
#include <windows.h>
#include <initguid.h> // identifiants des interfaces définis ici (pas de dxguid)
#else
#include <dlfcn.h>
#include <android/log.h>
#include <android/native_window.h>
#endif

#include <d3d11_4.h>
#include <dxgi1_6.h>

#include "rcn_gui.h"
#include <stddef.h>

#include "rcn_internal.h"
#if !defined(BR_WINDOWS)
#include "rcn_vulkan.h"
#endif

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
#if defined(BR_WINDOWS)
#define br_log(...) fprintf(stderr, "[d3d11] " __VA_ARGS__)
#else
#define br_log(...)                                                     \
    do {                                                                \
        fprintf(stderr, "[dxvk] " __VA_ARGS__);                         \
        __android_log_print(ANDROID_LOG_INFO, "rcn-dxvk", __VA_ARGS__); \
    } while (0)
#endif

// Fenêtre d'un handle du jeu : sur Android, sans objet (DXVK dessine dans la surface de l'application) ;
// sur PC, la fenêtre de l'hôte créée pour lui (le jeu ne connaît que des handles imités).
static HWND br_hwnd(u64 guest);

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
// --- Mise au point : RCN_DRAW_PROBE=<n> (PC, D3D11 de Windows) ---
// Pendant l'image qui suit le n-ième Present, après chaque Draw* / Dispatch : la cible de rendu 0 (ou la vue
// UAV 0 du calcul) est relue et ses pixels non nuls comptés. Dit si une image noire est dessinée puis
// effacée, dessinée en noir, ou pas dessinée du tout.
static u64 br_present_count; // Present déjà faits (RCN_FRAMES, RCN_DRAW_PROBE)
#if defined(BR_WINDOWS)
static u32 br_probe_index;
static u32 br_format_bytes(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: return 16;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R32G32_FLOAT: return 8;
    case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R8G8_UNORM: return 2;
    case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT: return 1;
    default: return 4;
    }
}
static void br_probe_resource(ID3D11DeviceContext *context, ID3D11Resource *res, const char *what, const char *kind) {
    ID3D11Texture2D *tex = NULL;
    if (!res || FAILED(res->lpVtbl->QueryInterface(res, &IID_ID3D11Texture2D, (void **)&tex)) || !tex) {
        br_log("sonde %u %s : %s non 2D\n", br_probe_index, what, kind);
        return;
    }
    D3D11_TEXTURE2D_DESC d;
    tex->lpVtbl->GetDesc(tex, &d);
    if (d.SampleDesc.Count > 1) {
        br_log("sonde %u %s : %s %ux%u fmt %u multi-échantillons\n", br_probe_index, what, kind, d.Width, d.Height, d.Format);
        tex->lpVtbl->Release(tex);
        return;
    }
    ID3D11Device *device = NULL;
    context->lpVtbl->GetDevice(context, &device);
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Usage = D3D11_USAGE_STAGING, sd.BindFlags = 0, sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ, sd.MiscFlags = 0;
    sd.MipLevels = 1, sd.ArraySize = 1;
    ID3D11Texture2D *staging = NULL;
    if (SUCCEEDED(device->lpVtbl->CreateTexture2D(device, &sd, NULL, &staging))) {
        context->lpVtbl->CopySubresourceRegion(context, (ID3D11Resource *)staging, 0, 0, 0, 0, (ID3D11Resource *)tex, 0, NULL);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(context->lpVtbl->Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m))) {
            u32 bpp = br_format_bytes(d.Format), nonzero = 0, total = 0;
            u64 first = 0;
            for (u32 y = 0; y < d.Height; y += 7)
                for (u32 x = 0; x < d.Width; x += 7) {
                    const u8 *p = (const u8 *)m.pData + (size_t)y * m.RowPitch + (size_t)x * bpp;
                    u64 v = 0;
                    memcpy(&v, p, bpp < 8 ? bpp : 8);
                    total++;
                    if (v) {
                        if (!nonzero) first = v;
                        nonzero++;
                    }
                }
            br_log("sonde %u %s : %s %p %ux%u fmt %u : %u/%u non nuls (premier %llx)\n", br_probe_index, what, kind, (void *)tex,
                   d.Width, d.Height, d.Format, nonzero, total, (unsigned long long)first);
            context->lpVtbl->Unmap(context, (ID3D11Resource *)staging, 0);
        }
        staging->lpVtbl->Release(staging);
    }
    device->lpVtbl->Release(device);
    tex->lpVtbl->Release(tex);
}
static void br_probe(RcnCtx *ctx, const char *what) {
    static long long target = -2;
    if (target == -2) target = getenv("RCN_DRAW_PROBE") ? atoll(getenv("RCN_DRAW_PROBE")) : -1;
    if (target < 0 || (long long)br_present_count != target) return;
    ID3D11DeviceContext *context = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!context) return;
    br_probe_index++;
    if (!strcmp(what, "Dispatch") || !strcmp(what, "DispatchIndirect")) {
        ID3D11UnorderedAccessView *uav = NULL;
        context->lpVtbl->CSGetUnorderedAccessViews(context, 0, 1, &uav);
        if (!uav) return;
        ID3D11Resource *res = NULL;
        uav->lpVtbl->GetResource(uav, &res);
        br_probe_resource(context, res, what, "uav0");
        if (res) res->lpVtbl->Release(res);
        uav->lpVtbl->Release(uav);
        return;
    }
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11DepthStencilView *dsv = NULL;
    context->lpVtbl->OMGetRenderTargets(context, 1, &rtv, &dsv);
    if (dsv) dsv->lpVtbl->Release(dsv);
    if (!rtv) {
        br_log("sonde %u %s : pas de cible de rendu 0\n", br_probe_index, what);
        return;
    }
    ID3D11Resource *res = NULL;
    rtv->lpVtbl->GetResource(rtv, &res);
    br_probe_resource(context, res, what, "rt0");
    if (res) res->lpVtbl->Release(res);
    rtv->lpVtbl->Release(rtv);
}
#define BR_PROBE_FN(name)                                         \
    static RcnFn br_probe_orig_##name;                            \
    static void br_probe_##name(RcnCtx *ctx) {                    \
        u64 self = rcn_arg(ctx, 0);                               \
        br_probe_orig_##name(ctx);                                \
        u64 r0 = ctx->r[0];                                       \
        ctx->r[1] = self; /* premier argument de nouveau en place */ \
        br_probe(ctx, #name);                                     \
        ctx->r[0] = r0;                                           \
    }
BR_PROBE_FN(Draw)
BR_PROBE_FN(DrawIndexed)
BR_PROBE_FN(DrawInstanced)
BR_PROBE_FN(DrawIndexedInstanced)
BR_PROBE_FN(Dispatch)
BR_PROBE_FN(DispatchIndirect)
BR_PROBE_FN(DrawIndexedInstancedIndirect)
BR_PROBE_FN(DrawInstancedIndirect)
static RcnFn br_probe_wrap(const char *family, const char *name, RcnFn fn) {
    if (!getenv("RCN_DRAW_PROBE") || strcmp(family, "ID3D11DeviceContext4")) return fn;
#define BR_PROBE_SUB(n) if (!strcmp(name, #n)) return br_probe_orig_##n = fn, br_probe_##n;
    BR_PROBE_SUB(Draw)
    BR_PROBE_SUB(DrawIndexed)
    BR_PROBE_SUB(DrawInstanced)
    BR_PROBE_SUB(DrawIndexedInstanced)
    BR_PROBE_SUB(Dispatch)
    BR_PROBE_SUB(DispatchIndirect)
    BR_PROBE_SUB(DrawIndexedInstancedIndirect)
    BR_PROBE_SUB(DrawInstancedIndirect)
    return fn;
}
#else
static RcnFn br_probe_wrap(const char *family, const char *name, RcnFn fn) {
    (void)family, (void)name;
    return fn;
}
#endif

// --- Mise au point : RCN_RES_LOG=1 ---
// Mémoire des textures et tampons que le jeu a créés et pas encore relâchés (compte public à 0), estimée
// d'après leur description : totaux par catégorie et les 24 plus grosses descriptions, notés toutes les 15 s.
// Dit ce qui remplit la mémoire du GPU (sur un téléphone, la mémoire vive).
void rcn_mem_report(void);
static int br_res_enabled = -1;
typedef struct {
    u32 kind, w, h, d, mips, array, format, samples, usage, bind, misc;
    u32 count;
    u64 bytes;
} BrResBucket;
typedef struct {
    void *native;
    u64 bytes;
    u32 bucket;
} BrResLive;
static BrResBucket br_res_buckets[16384];
static u32 br_res_bucket_count;
static BrResLive *br_res_live; // adressage ouvert, 1 << 21 cases
#define BR_RES_SLOTS (1u << 21)
static pthread_mutex_t br_res_lock = PTHREAD_MUTEX_INITIALIZER;

// Octets par bloc et taille du bloc (4 pour les formats compressés BC*).
static u32 br_res_format(u32 f, u32 *block) {
    *block = 1;
    if (f >= 1 && f <= 4) return 16;
    if (f >= 5 && f <= 8) return 12;
    if (f >= 9 && f <= 22) return 8;
    if (f >= 23 && f <= 47) return 4;
    if (f >= 48 && f <= 59) return 2;
    if (f >= 60 && f <= 65) return 1;
    if (f >= 67 && f <= 69) return 4;
    if ((f >= 70 && f <= 72) || (f >= 79 && f <= 81)) return *block = 4, 8;
    if ((f >= 73 && f <= 78) || (f >= 82 && f <= 84) || (f >= 94 && f <= 99)) return *block = 4, 16;
    if (f >= 85 && f <= 86) return 2;
    if (f >= 87 && f <= 93) return 4;
    if (f == 115) return 2; // B4G4R4A4
    return 4;
}

static u64 br_res_size(const BrResBucket *b) {
    if (b->kind == 0) return b->w;
    u32 block, unit = br_res_format(b->format, &block);
    u32 mips = b->mips;
    if (!mips) {
        u32 m = b->w > b->h ? b->w : b->h;
        if (b->d > m) m = b->d;
        while (m) mips++, m >>= 1;
    }
    u64 total = 0;
    for (u32 i = 0; i < mips && i < 16; i++) {
        u64 w = b->w >> i, h = b->h >> i, d = b->d >> i;
        if (!w) w = 1;
        if (!h) h = 1;
        if (!d) d = 1;
        total += ((w + block - 1) / block) * ((h + block - 1) / block) * d * unit;
    }
    return total * (b->array ? b->array : 1) * (b->samples ? b->samples : 1);
}

static void br_res_add(void *native, BrResBucket key) {
    if (!native) return;
    u64 bytes = br_res_size(&key);
    pthread_mutex_lock(&br_res_lock);
    if (!br_res_live) br_res_live = calloc(BR_RES_SLOTS, sizeof *br_res_live);
    u32 b = 0;
    while (b < br_res_bucket_count && memcmp(&br_res_buckets[b], &key, offsetof(BrResBucket, count))) b++;
    if (b == br_res_bucket_count && b < 16384) br_res_buckets[br_res_bucket_count++] = key;
    if (b < 16384 && br_res_live) {
        br_res_buckets[b].count++;
        br_res_buckets[b].bytes += bytes;
        size_t h = (((uintptr_t)native >> 4) * 0x9E3779B97F4A7C15ull) >> 43;
        for (u32 i = 0; i < BR_RES_SLOTS; i++) {
            BrResLive *s = &br_res_live[(h + i) & (BR_RES_SLOTS - 1)];
            if (!s->native || s->native == (void *)1 || s->native == native) {
                if (s->native == native) { // adresse réutilisée sans Release vu : l'ancienne sort du compte
                    br_res_buckets[s->bucket].count--;
                    br_res_buckets[s->bucket].bytes -= s->bytes;
                }
                s->native = native, s->bytes = bytes, s->bucket = b;
                break;
            }
        }
    }
    pthread_mutex_unlock(&br_res_lock);
}

static void br_res_forget(void *native) {
    pthread_mutex_lock(&br_res_lock);
    if (br_res_live) {
        size_t h = (((uintptr_t)native >> 4) * 0x9E3779B97F4A7C15ull) >> 43;
        for (u32 i = 0; i < BR_RES_SLOTS; i++) {
            BrResLive *s = &br_res_live[(h + i) & (BR_RES_SLOTS - 1)];
            if (!s->native) break;
            if (s->native == native) {
                br_res_buckets[s->bucket].count--;
                br_res_buckets[s->bucket].bytes -= s->bytes;
                s->native = (void *)1; // case libérée (la recherche continue après elle)
                break;
            }
        }
    }
    pthread_mutex_unlock(&br_res_lock);
}

static void br_res_dump(void) {
    static u64 last;
    if (br_res_enabled <= 0) return;
    u64 now = rcn_now_ns();
    if (!last) last = now;
    if (now - last < 15000000000ull) return;
    last = now;
    pthread_mutex_lock(&br_res_lock);
    // Catégories : tampons, textures cibles de rendu / profondeur, UAV, autres textures (lues seulement).
    static const char *const names[] = {"tampons", "cibles de rendu/profondeur", "textures UAV", "textures lues", "textures dynamiques/lecture CPU", "textures lues sans mips"};
    u64 cat_bytes[6] = {0}, total = 0;
    u32 cat_count[6] = {0};
    for (u32 i = 0; i < br_res_bucket_count; i++) {
        const BrResBucket *b = &br_res_buckets[i];
        int c = b->kind == 0 ? 0 : (b->bind & 0x60) ? 1 : (b->bind & 0x80) ? 2 : (b->usage >= 2) ? 4 : b->mips == 1 ? 5 : 3;
        cat_bytes[c] += b->bytes, cat_count[c] += b->count, total += b->bytes;
    }
    br_log("ressources vivantes : %llu Mo\n", (unsigned long long)(total >> 20));
    rcn_mem_report();
    // Tampons par usage (sommets, index, constantes, structurés / lus, UAV) et par taille.
    {
        static const char *const kinds[] = {"sommets", "index", "constantes", "lus (SRV)", "UAV", "autres"};
        static const char *const sizes[] = {"<4 Ko", "<64 Ko", "<1 Mo", "<16 Mo", ">=16 Mo"};
        u64 kb[6][5] = {{0}};
        u32 kc[6][5] = {{0}};
        for (u32 i = 0; i < br_res_bucket_count; i++) {
            const BrResBucket *b = &br_res_buckets[i];
            if (b->kind != 0 || !b->count) continue;
            int k = (b->bind & 0x80) ? 4 : (b->bind & 1) ? 0 : (b->bind & 2) ? 1 : (b->bind & 4) ? 2 : (b->bind & 8) ? 3 : 5;
            int s = b->w < 4096 ? 0 : b->w < 65536 ? 1 : b->w < (1u << 20) ? 2 : b->w < (16u << 20) ? 3 : 4;
            kb[k][s] += b->bytes, kc[k][s] += b->count;
        }
        for (int k = 0; k < 6; k++)
            for (int s = 0; s < 5; s++)
                if (kc[k][s])
                    br_log("  tampons %-10s %-7s %6u  %5llu Mo\n", kinds[k], sizes[s], kc[k][s], (unsigned long long)(kb[k][s] >> 20));
    }
    for (int c = 0; c < 6; c++)
        br_log("  %-32s %6u  %6llu Mo\n", names[c], cat_count[c], (unsigned long long)(cat_bytes[c] >> 20));
    // Les 24 plus grosses descriptions.
    static u8 shown[16384];
    memset(shown, 0, sizeof shown);
    for (int k = 0; k < 24; k++) {
        int best = -1;
        for (u32 i = 0; i < br_res_bucket_count; i++)
            if (!shown[i] && br_res_buckets[i].bytes && (best < 0 || br_res_buckets[i].bytes > br_res_buckets[best].bytes)) best = (int)i;
        if (best < 0) break;
        shown[best] = 1;
        const BrResBucket *b = &br_res_buckets[best];
        if (b->kind == 0)
            br_log("  tampon %u o usage %u bind 0x%x misc 0x%x : %u, %llu Mo\n", b->w, b->usage, b->bind, b->misc, b->count,
                   (unsigned long long)(b->bytes >> 20));
        else
            br_log("  tex%uD %ux%ux%u mips %u tableau %u format %u ech %u usage %u bind 0x%x misc 0x%x : %u, %llu Mo\n", b->kind,
                   b->w, b->h, b->d, b->mips, b->array, b->format, b->samples, b->usage, b->bind, b->misc, b->count,
                   (unsigned long long)(b->bytes >> 20));
    }
    pthread_mutex_unlock(&br_res_lock);
}

// Créations du périphérique : (this, description, données initiales, sortie). La description est lue chez le
// jeu, la ressource native derrière l'enveloppe rendue.
static void *br_res_out(RcnCtx *ctx, u64 out) {
    (void)ctx;
    if (!out || (u32)ctx->r[0] != 0) return NULL;
    u64 w = rd64(out);
    return w ? ((BrWrap *)(uintptr_t)w)->native : NULL;
}
// Grosse ressource (32 Mo ou plus) : pile du jeu qui la crée, pour savoir quel système d'Unreal la demande.
static void br_res_big(RcnCtx *ctx, const BrResBucket *k, const char *name) {
    u64 size = br_res_size(k);
    if (size < (32u << 20)) return;
    char what[160];
    snprintf(what, sizeof what, "ressource de %llu Mo (%s %ux%ux%u format %u)", (unsigned long long)(size >> 20), name, k->w,
             k->h, k->d, k->format);
    // Pile complète (adresses du jeu trouvées sur sa pile) : les ressources passent toutes par les mêmes
    // fonctions du RHI, c'est l'appelant plus haut qui dit le système.
    static int shown;
    if (shown++ >= 64) return;
    char line[1024];
    int n = snprintf(line, sizeof line, "rcn : %s, pile :", what);
    for (int i = 0, found = 0; i < 1024 && found < 24 && n < (int)sizeof line - 64; i++) {
        u64 v = rd64(ctx->r[4] + 8 * (u64)i);
        RcnLoaded *l = rcn_loaded_at(ctx->proc, v);
        if (l) n += snprintf(line + n, sizeof line - (size_t)n, " +%llx", (unsigned long long)(v - (u64)(uintptr_t)l->image)), found++;
    }
    fprintf(stderr, "%s\n", line);
}
#define BR_RES_FN(name, fill)                                   \
    static RcnFn br_res_orig_##name;                              \
    static void br_res_##name(RcnCtx *ctx) {                      \
        u64 desc = rcn_arg(ctx, 1), out = rcn_arg(ctx, 3);        \
        br_res_orig_##name(ctx);                                  \
        void *native = br_res_out(ctx, out);                      \
        if (!native || !desc) return;                             \
        BrResBucket k;                                            \
        memset(&k, 0, sizeof k);                                  \
        fill;                                                     \
        br_res_add(native, k);                                    \
        br_res_big(ctx, &k, #name);                               \
    }
// D3D11_BUFFER_DESC : ByteWidth, Usage, BindFlags, CPUAccessFlags, MiscFlags.
#define BR_FILL_BUFFER k.kind = 0, k.w = rd32(desc), k.usage = rd32(desc + 4), k.bind = rd32(desc + 8), k.misc = rd32(desc + 16)
// D3D11_TEXTURE1D_DESC : Width, MipLevels, ArraySize, Format, Usage, BindFlags, CPUAccessFlags, MiscFlags.
#define BR_FILL_TEX1D                                                                                            \
    k.kind = 1, k.w = rd32(desc), k.h = 1, k.d = 1, k.mips = rd32(desc + 4), k.array = rd32(desc + 8),           \
    k.format = rd32(desc + 12), k.samples = 1, k.usage = rd32(desc + 16), k.bind = rd32(desc + 20), k.misc = rd32(desc + 28)
// D3D11_TEXTURE2D_DESC : Width, Height, MipLevels, ArraySize, Format, SampleDesc (Count, Quality), Usage,
// BindFlags, CPUAccessFlags, MiscFlags.
#define BR_FILL_TEX2D                                                                                                \
    k.kind = 2, k.w = rd32(desc), k.h = rd32(desc + 4), k.d = 1, k.mips = rd32(desc + 8), k.array = rd32(desc + 12), \
    k.format = rd32(desc + 16), k.samples = rd32(desc + 20), k.usage = rd32(desc + 28), k.bind = rd32(desc + 32),     \
    k.misc = rd32(desc + 40)
// D3D11_TEXTURE3D_DESC : Width, Height, Depth, MipLevels, Format, Usage, BindFlags, CPUAccessFlags, MiscFlags.
#define BR_FILL_TEX3D                                                                                                 \
    k.kind = 3, k.w = rd32(desc), k.h = rd32(desc + 4), k.d = rd32(desc + 8), k.mips = rd32(desc + 12), k.array = 1, \
    k.format = rd32(desc + 16), k.samples = 1, k.usage = rd32(desc + 20), k.bind = rd32(desc + 24), k.misc = rd32(desc + 32)
BR_RES_FN(CreateBuffer, BR_FILL_BUFFER)
BR_RES_FN(CreateTexture1D, BR_FILL_TEX1D)
BR_RES_FN(CreateTexture2D, BR_FILL_TEX2D)
BR_RES_FN(CreateTexture2D1, BR_FILL_TEX2D)
BR_RES_FN(CreateTexture3D, BR_FILL_TEX3D)
BR_RES_FN(CreateTexture3D1, BR_FILL_TEX3D)
static RcnFn br_res_wrap(const char *family, const char *name, RcnFn fn) {
    if (br_res_enabled < 0) br_res_enabled = getenv("RCN_RES_LOG") != NULL;
    if (!br_res_enabled || strcmp(family, "ID3D11Device5")) return fn;
#define BR_RES_SUB(n) if (!strcmp(name, #n)) return br_res_orig_##n = fn, br_res_##n;
    BR_RES_SUB(CreateBuffer)
    BR_RES_SUB(CreateTexture1D)
    BR_RES_SUB(CreateTexture2D)
    BR_RES_SUB(CreateTexture2D1)
    BR_RES_SUB(CreateTexture3D)
    BR_RES_SUB(CreateTexture3D1)
    return fn;
}

// --- RCN_TEX_REDUCE=<côté minimal>:<décalage> (réglage du jeu « compression des textures ») ---
// Une texture 2D sans mips, lue seulement (SRV), dont le grand côté atteint le minimum, est créée 2^décalage
// fois plus petite (rcn_texreduce.c) : sur un téléphone, le processeur graphique partage la mémoire vive, et ces
// textures jamais diffusées par le moteur (interface, cartes…) en prennent des centaines de Mo. Ses pixels sont
// réduits à la création, ou plus tard quand le jeu les envoie (UpdateSubresource : Unreal crée ces textures
// vides puis les remplit) ; les textures réduites sont notées pour cela.
int rcn_tex_reduce(u32 format, u32 w, u32 h, const void *data, u32 pitch, u32 shift, u32 *nw, u32 *nh, void **out,
                   u32 *out_pitch);
u32 rcn_tex_shift(u32 format, u32 w, u32 h, u32 shift);
static int br_tex_min = -1, br_tex_shift;
static u64 br_tex_saved, br_tex_count, br_tex_copies;
static pthread_mutex_t br_tex_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    void *native; // NULL : libre ; (void *)1 : effacée
    u32 format, shift;
} BrTexReduced;
#define BR_TEX_SLOTS 16384u
static BrTexReduced br_tex_table[BR_TEX_SLOTS];

static size_t br_tex_hash(const void *p) { return (size_t)((((uintptr_t)p >> 4) * 0x9E3779B97F4A7C15ull) >> 50); }

static void br_tex_note(void *native, u32 format, u32 shift) {
    pthread_mutex_lock(&br_tex_lock);
    size_t h = br_tex_hash(native);
    for (u32 i = 0; i < BR_TEX_SLOTS; i++) {
        BrTexReduced *s = &br_tex_table[(h + i) % BR_TEX_SLOTS];
        if (!s->native || s->native == (void *)1 || s->native == native) {
            s->native = native, s->format = format, s->shift = shift;
            break;
        }
    }
    pthread_mutex_unlock(&br_tex_lock);
}

// Texture réduite : son décalage (0 sinon) ; `forget` : l'oublie (relâchée).
static u32 br_tex_find(const void *native, u32 *format, int forget) {
    u32 shift = 0;
    if (!native || br_tex_min <= 0) return 0;
    pthread_mutex_lock(&br_tex_lock);
    size_t h = br_tex_hash(native);
    for (u32 i = 0; i < BR_TEX_SLOTS; i++) {
        BrTexReduced *s = &br_tex_table[(h + i) % BR_TEX_SLOTS];
        if (!s->native) break;
        if (s->native == native) {
            shift = s->shift;
            if (format) *format = s->format;
            if (forget) s->native = (void *)1;
            break;
        }
    }
    pthread_mutex_unlock(&br_tex_lock);
    return shift;
}

static void br_tex_count_saved(u32 format, u32 w, u32 h, u32 shift) {
    // Octets par pixel (formats bruts) ou par bloc de 4 × 4 (BC : 8 pour BC1 / BC4, 16 sinon).
    u64 unit = format >= 70 ? ((format <= 72 || (format >= 79 && format <= 81)) ? 8 : 16) : 0;
    u64 bpp = (format >= 27 && format <= 30) || format >= 87 ? 4 : (format >= 48 && format <= 50) ? 2 : 1;
    u64 before = unit ? (u64)(w / 4) * (h / 4) * unit : (u64)w * h * bpp;
    u64 after = before >> (2 * shift);
    pthread_mutex_lock(&br_tex_lock);
    br_tex_saved += before - after;
    u64 count = ++br_tex_count, saved = br_tex_saved;
    pthread_mutex_unlock(&br_tex_lock);
    if (count <= 8 || count % 50 == 0)
        br_log("textures reduites : %llu (%ux%u format %u, /%u), %llu Mo gagnes\n", (unsigned long long)count, w, h, format, 1u << shift,
               (unsigned long long)(saved >> 20));
}

// CreateTexture2D(1)(périphérique, description, données, sortie).
static void br_tex_create(RcnCtx *ctx, RcnFn orig, u32 desc_size) {
    u64 desc = A1, init = A2, out = A3;
    u32 w = desc ? rd32(desc) : 0, h = desc ? rd32(desc + 4) : 0;
    // Texture « staging » écrite par le processeur : rendue lisible aussi, pour que ses copies vers une texture
    // réduite puissent être relues (br_tex_copy).
    if (!ctx->bits32 && desc && rd32(desc + 28) == 3 && (rd32(desc + 36) & 0x10000) && !(rd32(desc + 36) & 0x20000)) {
        u8 staging[48];
        memcpy(staging, (const void *)(uintptr_t)desc, desc_size);
        u32 cpu = rd32(desc + 36) | 0x20000;
        memcpy(staging + 36, &cpu, 4);
        ctx->r[2] = (u64)(uintptr_t)staging;
        orig(ctx);
        return;
    }
    if (ctx->bits32 || !desc || (w > h ? w : h) < (u32)br_tex_min || rd32(desc + 8) != 1 || rd32(desc + 12) != 1 ||
        rd32(desc + 20) != 1 || rd32(desc + 28) > 1 || rd32(desc + 32) != 0x8 || rd32(desc + 36) || rd32(desc + 40)) {
        orig(ctx);
        return;
    }
    u32 format = rd32(desc + 16), shift = rcn_tex_shift(format, w, h, (u32)br_tex_shift), nw = w >> shift, nh = h >> shift, pitch = 0;
    void *pixels = NULL;
    u64 data = init ? rd64(init) : 0;
    if (!shift || (init && (!data || !rcn_tex_reduce(format, w, h, (const void *)(uintptr_t)data, rd32(init + 8), shift, &nw, &nh,
                                                     &pixels, &pitch)))) {
        orig(ctx);
        return;
    }
    // Description (et données) remplacées le temps de l'appel (rdx, r8 : volatils pour l'appelant).
    u8 new_desc[48];
    memcpy(new_desc, (const void *)(uintptr_t)desc, desc_size);
    memcpy(new_desc, &nw, 4);
    memcpy(new_desc + 4, &nh, 4);
    struct {
        u64 mem;
        u32 pitch, slice;
    } new_init = {(u64)(uintptr_t)pixels, pitch, 0};
    ctx->r[2] = (u64)(uintptr_t)new_desc;
    ctx->r[8] = init ? (u64)(uintptr_t)&new_init : 0;
    orig(ctx);
    free(pixels);
    if ((u32)ctx->r[0] != 0 || !out || !rd64(out)) return;
    br_tex_note(((BrWrap *)(uintptr_t)rd64(out))->native, format, shift);
    br_tex_count_saved(format, w, h, shift);
}

// Pixels envoyés dans une texture réduite (sous-ressource 0, seule existante) : réduits à leur tour. Région
// donnée (boîte) : alignée sur 2^décalage pixels (4 · 2^décalage pour un format BC), sinon ignorée.
static int br_tex_update(RcnCtx *ctx, ID3D11DeviceContext1 *self, ID3D11Resource *dst, UINT sub, const D3D11_BOX *box,
                         const void *src, UINT row_pitch, UINT depth_pitch, UINT flags, int v1) {
    u32 format = 0, shift = br_tex_find(dst, &format, 0);
    if (!shift) return 0;
    (void)depth_pitch;
    D3D11_TEXTURE2D_DESC d;
    ((ID3D11Texture2D *)dst)->lpVtbl->GetDesc((ID3D11Texture2D *)dst, &d);
    u32 f = 1u << shift, align = format >= 70 ? 4 * f : f;
    u32 left = 0, top = 0, w = d.Width << shift, h = d.Height << shift;
    if (box) {
        if (box->left % align || box->top % align || (box->right - box->left) % align || (box->bottom - box->top) % align) {
            static int shown;
            if (shown++ < 8) br_log("envoi partiel non aligne dans une texture reduite, ignore\n");
            return 1;
        }
        left = box->left, top = box->top, w = box->right - box->left, h = box->bottom - box->top;
    }
    void *pixels = NULL;
    u32 nw, nh, pitch;
    if (!src || rcn_tex_shift(format, w, h, shift) != shift || !rcn_tex_reduce(format, w, h, src, row_pitch, shift, &nw, &nh, &pixels, &pitch))
        return 1;
    D3D11_BOX nb = {left >> shift, top >> shift, 0, (left >> shift) + nw, (top >> shift) + nh, 1};
    u32 rows = format >= 70 ? nh / 4 : nh;
    if (v1) self->lpVtbl->UpdateSubresource1(self, dst, sub, &nb, pixels, pitch, pitch * rows, flags);
    else self->lpVtbl->UpdateSubresource(self, dst, sub, &nb, pixels, pitch, pitch * rows);
    free(pixels);
    return 1;
}

static RcnFn br_tex_orig_UpdateSubresource, br_tex_orig_UpdateSubresource1, br_tex_orig_CopyResource,
    br_tex_orig_CopySubresourceRegion, br_tex_orig_CopySubresourceRegion1;
// UpdateSubresource(contexte, ressource, sous-ressource, boîte, données, pas, pas de tranche).
static void br_tex_UpdateSubresource(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = br_unwrap(A0);
    if (!self || !br_tex_update(ctx, self, br_unwrap(A1), (UINT)A2, (const D3D11_BOX *)(uintptr_t)A3,
                                (const void *)(uintptr_t)rcn_arg(ctx, 4), (UINT)rcn_arg(ctx, 5), (UINT)rcn_arg(ctx, 6), 0, 0))
        br_tex_orig_UpdateSubresource(ctx);
}
static void br_tex_UpdateSubresource1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = br_unwrap(A0);
    if (!self || !br_tex_update(ctx, self, br_unwrap(A1), (UINT)A2, (const D3D11_BOX *)(uintptr_t)A3,
                                (const void *)(uintptr_t)rcn_arg(ctx, 4), (UINT)rcn_arg(ctx, 5), (UINT)rcn_arg(ctx, 6),
                                (UINT)rcn_arg(ctx, 7), 1))
        br_tex_orig_UpdateSubresource1(ctx);
}
// Copie vers une texture réduite depuis une texture « staging » (qu'Unreal remplit par Map puis copie, par
// tuiles pour un atlas) : la région source est relue (les textures staging sont créées lisibles, voir
// br_tex_create), réduite et envoyée à la position réduite. Région non alignée, source d'un autre genre ou
// copie depuis une texture réduite : sautée (notée), la texture n'en est que partiellement remplie.
static void br_tex_skip(const char *what, const char *why) {
    u64 n = __atomic_add_fetch(&br_tex_copies, 1, __ATOMIC_RELAXED);
    if (n <= 12 || n % 200 == 0) br_log("%s avec une texture reduite sautee (%s, %llu fois)\n", what, why, (unsigned long long)n);
}

static int br_tex_copy(RcnCtx *ctx, const char *what, int whole) {
    ID3D11DeviceContext *self = br_unwrap(A0);
    ID3D11Resource *dst = br_unwrap(A1), *src = br_unwrap(whole ? A2 : rcn_arg(ctx, 6));
    u32 format = 0, shift = br_tex_find(dst, &format, 0);
    if (!shift) {
        if (!br_tex_find(src, NULL, 0)) return 0;
        br_tex_skip(what, "source reduite");
        return 1;
    }
    if (!self || !src) return 1;
    UINT dst_sub = whole ? 0 : (UINT)A2, dx = whole ? 0 : (UINT)A3, dy = whole ? 0 : (UINT)rcn_arg(ctx, 4);
    UINT src_sub = whole ? 0 : (UINT)rcn_arg(ctx, 7);
    const u32 *box = whole ? NULL : (const u32 *)(uintptr_t)rcn_arg(ctx, 8);
    D3D11_RESOURCE_DIMENSION dim;
    src->lpVtbl->GetType(src, &dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        br_tex_skip(what, "source pas 2D");
        return 1;
    }
    D3D11_TEXTURE2D_DESC sd;
    ((ID3D11Texture2D *)src)->lpVtbl->GetDesc((ID3D11Texture2D *)src, &sd);
    if (sd.Usage != D3D11_USAGE_STAGING || !(sd.CPUAccessFlags & D3D11_CPU_ACCESS_READ)) {
        br_tex_skip(what, "source pas lisible");
        return 1;
    }
    u32 mip = src_sub % (sd.MipLevels ? sd.MipLevels : 1);
    u32 sw = sd.Width >> mip, sh = sd.Height >> mip;
    if (!sw) sw = 1;
    if (!sh) sh = 1;
    u32 left = box ? box[0] : 0, top = box ? box[1] : 0, w = box ? box[3] - box[0] : sw, h = box ? box[4] - box[1] : sh;
    u32 f = 1u << shift, align = format >= 70 ? 4 * f : f;
    if (dx % align || dy % align || w % align || h % align || rcn_tex_shift(format, w, h, shift) != shift) {
        br_tex_skip(what, "region non alignee");
        return 1;
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(self->lpVtbl->Map(self, src, src_sub, D3D11_MAP_READ, 0, &m)) || !m.pData) {
        br_tex_skip(what, "lecture impossible");
        return 1;
    }
    // Début de la région : en blocs de 4 × 4 (BC) ou en pixels.
    u32 bc_unit = (format <= 72 || (format >= 79 && format <= 81)) ? 8 : 16;
    u32 bpp = (format >= 27 && format <= 30) || format >= 87 ? 4 : (format >= 48 && format <= 50) ? 2 : 1;
    const u8 *start = (const u8 *)m.pData + (format >= 70 ? (size_t)(top / 4) * m.RowPitch + (size_t)(left / 4) * bc_unit
                                                          : (size_t)top * m.RowPitch + (size_t)left * bpp);
    void *pixels = NULL;
    u32 nw, nh, pitch;
    int ok = rcn_tex_reduce(format, w, h, start, m.RowPitch, shift, &nw, &nh, &pixels, &pitch);
    self->lpVtbl->Unmap(self, src, src_sub);
    if (!ok) {
        br_tex_skip(what, "reduction impossible");
        return 1;
    }
    D3D11_BOX nb = {dx >> shift, dy >> shift, 0, (dx >> shift) + nw, (dy >> shift) + nh, 1};
    u32 rows = format >= 70 ? nh / 4 : nh;
    self->lpVtbl->UpdateSubresource(self, dst, dst_sub, &nb, pixels, pitch, pitch * rows);
    free(pixels);
    return 1;
}
static void br_tex_CopyResource(RcnCtx *ctx) {
    if (!br_tex_copy(ctx, "CopyResource", 1)) br_tex_orig_CopyResource(ctx);
}
static void br_tex_CopySubresourceRegion(RcnCtx *ctx) {
    if (!br_tex_copy(ctx, "CopySubresourceRegion", 0)) br_tex_orig_CopySubresourceRegion(ctx);
}
static void br_tex_CopySubresourceRegion1(RcnCtx *ctx) {
    if (!br_tex_copy(ctx, "CopySubresourceRegion1", 0)) br_tex_orig_CopySubresourceRegion1(ctx);
}

static RcnFn br_tex_orig_CreateTexture2D, br_tex_orig_CreateTexture2D1;
static void br_tex_CreateTexture2D(RcnCtx *ctx) { br_tex_create(ctx, br_tex_orig_CreateTexture2D, 44); }
static void br_tex_CreateTexture2D1(RcnCtx *ctx) { br_tex_create(ctx, br_tex_orig_CreateTexture2D1, 48); }
static RcnFn br_tex_wrap(const char *family, const char *name, RcnFn fn) {
    if (br_tex_min < 0) {
        const char *v = getenv("RCN_TEX_REDUCE");
        unsigned min = 0, shift = 0;
        br_tex_min = v && sscanf(v, "%u:%u", &min, &shift) == 2 && shift >= 1 && shift <= 3 && min >= 64 ? (int)min : 0;
        br_tex_shift = (int)shift;
        if (br_tex_min) br_log("textures sans mips de %d pixels ou plus reduites de %d fois\n", br_tex_min, 1 << br_tex_shift);
    }
    if (!br_tex_min) return fn;
#define BR_TEX_SUB(fam, n) if (!strcmp(family, fam) && !strcmp(name, #n)) return br_tex_orig_##n = fn, br_tex_##n;
    BR_TEX_SUB("ID3D11Device5", CreateTexture2D)
    BR_TEX_SUB("ID3D11Device5", CreateTexture2D1)
    BR_TEX_SUB("ID3D11DeviceContext4", UpdateSubresource)
    BR_TEX_SUB("ID3D11DeviceContext4", UpdateSubresource1)
    BR_TEX_SUB("ID3D11DeviceContext4", CopyResource)
    BR_TEX_SUB("ID3D11DeviceContext4", CopySubresourceRegion)
    BR_TEX_SUB("ID3D11DeviceContext4", CopySubresourceRegion1)
    return fn;
}

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
                table[i] = rcn_thunk(ctx, br_family_name[family], br_family_methods[family][i].name,
                                     br_tex_wrap(br_family_name[family], br_family_methods[family][i].name,
                                                 br_res_wrap(br_family_name[family], br_family_methods[family][i].name,
                                                             br_probe_wrap(br_family_name[family], br_family_methods[family][i].name,
                                                                           br_family_methods[family][i].fn))),
                                     0);
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

// GetData(contexte, requête, données, taille, options) : avec D3D11_ASYNC_GETDATA_DONOTFLUSH, DXVK n'envoie
// rien au GPU et la requête peut ne jamais aboutir ; les pilotes de Windows, eux, finissent par envoyer
// leurs commandes. Skyrim attend ainsi (GetData puis Sleep(1) en boucle) : sans réponse, le contexte est
// vidé, comme le ferait un pilote.
static void br_special_ID3D11DeviceContext_GetData(RcnCtx *ctx) {
    ID3D11DeviceContext *self = br_unwrap(A0);
    if (!self) {
        RET((u64)(i64)(i32)E_POINTER);
        return;
    }
    ID3D11Asynchronous *query = br_unwrap(A1);
    UINT flags = (UINT)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->GetData(self, query, (void *)(uintptr_t)A2, (UINT)A3, flags);
    if (r == S_FALSE && (flags & D3D11_ASYNC_GETDATA_DONOTFLUSH)) self->lpVtbl->Flush(self);
    if (FAILED(r)) br_failed("ID3D11DeviceContext::GetData", r);
#if defined(BR_WINDOWS)
    // RCN_DRAW_PROBE : résultats des requêtes (occlusion : pixels visibles) pendant l'image sondée.
    if (getenv("RCN_DRAW_PROBE") && (long long)br_present_count == atoll(getenv("RCN_DRAW_PROBE")) && A2 && A3 >= 4) {
        D3D11_QUERY_DESC qd = {0};
        ID3D11Query *q = NULL;
        if (query && SUCCEEDED(query->lpVtbl->QueryInterface(query, &IID_ID3D11Query, (void **)&q)) && q) {
            q->lpVtbl->GetDesc(q, &qd);
            q->lpVtbl->Release(q);
        }
        br_log("GetData requête type %u taille %u : 0x%x valeur %llx\n", qd.Query, (UINT)A3, (unsigned)r,
               (unsigned long long)(A3 >= 8 ? *(u64 *)(uintptr_t)A2 : *(u32 *)(uintptr_t)A2));
    }
#endif
    RET((u64)(i64)r);
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
    // Compte public à 0 : l'objet de DXVK peut vivre encore (une vue, le contexte le référencent), et le jeu
    // s'en sert parfois comme sous Windows. L'enveloppe reste donc liée à lui ; une nouvelle ressource à la
    // même adresse la retrouve (recherche par adresse et famille).
    ULONG n = self->lpVtbl->Release(self);
    if (!n && br_res_enabled > 0) br_res_forget(self);
    if (!n) br_tex_find(self, NULL, 1);
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
#if defined(BR_WINDOWS)
static struct {
    int state;
    HMODULE dxgi, d3d11;
    PFN_CreateDevice create_device;
    PFN_CreateDeviceAndSwapChain create_device_sc;
    PFN_CreateFactory create_factory, create_factory1;
    PFN_CreateFactory2 create_factory2;
    HWND window;
} dx;

// IUnknown : déclaré par windows.h avant initguid.h, défini ici (sans libuuid).
const IID IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static HWND br_hwnd(u64 guest) {
    if (!guest) return NULL;
    if (!dx.window) {
        // Fenêtre cachée de 1920 × 1080 : la chaîne d'images y présente ; le jeu fixe sa taille (ResizeBuffers).
        dx.window = CreateWindowExA(0, "STATIC", "rcn-d3d11", WS_POPUP, 0, 0, 1920, 1080, NULL, NULL, NULL, NULL);
        br_log("fenêtre de l'hôte %p\n", (void *)dx.window);
    }
    return dx.window;
}
static void br_update_window(RcnCtx *ctx) { (void)ctx; }
static int br_load(RcnCtx *ctx) {
    (void)ctx;
    if (dx.state) return dx.state > 0;
    dx.state = -1;
    dx.dxgi = LoadLibraryA("dxgi.dll");
    dx.d3d11 = dx.dxgi ? LoadLibraryA("d3d11.dll") : NULL;
    if (!dx.d3d11) {
        br_log("dxgi.dll / d3d11.dll introuvables\n");
        return 0;
    }
    dx.create_device = (PFN_CreateDevice)(void *)GetProcAddress(dx.d3d11, "D3D11CreateDevice");
    dx.create_device_sc = (PFN_CreateDeviceAndSwapChain)(void *)GetProcAddress(dx.d3d11, "D3D11CreateDeviceAndSwapChain");
    dx.create_factory = (PFN_CreateFactory)(void *)GetProcAddress(dx.dxgi, "CreateDXGIFactory");
    dx.create_factory1 = (PFN_CreateFactory)(void *)GetProcAddress(dx.dxgi, "CreateDXGIFactory1");
    dx.create_factory2 = (PFN_CreateFactory2)(void *)GetProcAddress(dx.dxgi, "CreateDXGIFactory2");
    dx.state = 1;
    br_log("prêt (Direct3D 11 de Windows)\n");
    return 1;
}

// Image de la chaîne relue (RCN_FRAMES) : copie dans une texture lisible, convertie en XRGB8888.
static void br_capture(RcnCtx *ctx, IDXGISwapChain *chain, u32 *w_out, u32 *h_out, u8 **pixels_out) {
    *pixels_out = NULL;
    ID3D11Texture2D *back = NULL;
    if (FAILED(chain->lpVtbl->GetBuffer(chain, 0, &IID_ID3D11Texture2D, (void **)&back)) || !back) return;
    D3D11_TEXTURE2D_DESC d;
    back->lpVtbl->GetDesc(back, &d);
    ID3D11Device *device = NULL;
    back->lpVtbl->GetDevice(back, &device);
    ID3D11DeviceContext *context = NULL;
    device->lpVtbl->GetImmediateContext(device, &context);
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Usage = D3D11_USAGE_STAGING, sd.BindFlags = 0, sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ, sd.MiscFlags = 0;
    sd.SampleDesc.Count = 1, sd.SampleDesc.Quality = 0, sd.MipLevels = 1, sd.ArraySize = 1;
    ID3D11Texture2D *staging = NULL;
    if (SUCCEEDED(device->lpVtbl->CreateTexture2D(device, &sd, NULL, &staging))) {
        context->lpVtbl->CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)back);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(context->lpVtbl->Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m))) {
            u8 *out = malloc((size_t)d.Width * d.Height * 4);
            for (u32 y = 0; out && y < d.Height; y++) {
                const u32 *src = (const u32 *)((const u8 *)m.pData + (size_t)y * m.RowPitch);
                u32 *dst = (u32 *)(out + (size_t)y * d.Width * 4);
                for (u32 x = 0; x < d.Width; x++) {
                    u32 v = src[x], r, g, b;
                    if (d.Format == DXGI_FORMAT_R10G10B10A2_UNORM) r = (v >> 2) & 255, g = (v >> 12) & 255, b = (v >> 22) & 255;
                    else if (d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                        b = v & 255, g = (v >> 8) & 255, r = (v >> 16) & 255;
                    else r = v & 255, g = (v >> 8) & 255, b = (v >> 16) & 255;
                    dst[x] = r << 16 | g << 8 | b;
                }
            }
            context->lpVtbl->Unmap(context, (ID3D11Resource *)staging, 0);
            *pixels_out = out, *w_out = d.Width, *h_out = d.Height;
        }
        staging->lpVtbl->Release(staging);
    }
    context->lpVtbl->Release(context);
    device->lpVtbl->Release(device);
    back->lpVtbl->Release(back);
    (void)ctx;
}
// Images enregistrées (RCN_FRAMES) : relues juste avant Present (après, le tampon d'une chaîne « flip » est
// indéfini), remises à l'hôte juste après.
static u8 *br_pending;
static u32 br_pending_w, br_pending_h;
static void br_before_present(RcnCtx *ctx) {
    const char *every = getenv("RCN_FRAME_EVERY");
    u64 step = every ? (u64)atoll(every) : 60;
    if (!getenv("RCN_FRAMES") || !step || (br_present_count + 1) % step != 0) return;
    IUnknown *u = br_unwrap(A0);
    IDXGISwapChain *chain = NULL;
    if (!u || FAILED(u->lpVtbl->QueryInterface(u, &IID_IDXGISwapChain, (void **)&chain)) || !chain) return;
    free(br_pending);
    br_pending = NULL;
    br_capture(ctx, chain, &br_pending_w, &br_pending_h, &br_pending);
    chain->lpVtbl->Release(chain);
}
static void br_after_present(RcnCtx *ctx, IDXGISwapChain *chain, UINT interval) {
    DXGI_SWAP_CHAIN_DESC d;
    memset(&d, 0, sizeof d);
    chain->lpVtbl->GetDesc(chain, &d);
    br_present_count++;
    if (br_pending) {
        rcn_present(ctx, br_pending, br_pending_w, br_pending_h, br_pending_w * 4, 32, 0);
        free(br_pending);
        br_pending = NULL;
        br_dump_calls();
        br_res_dump();
        return;
    }
    rcn_present(ctx, NULL, d.BufferDesc.Width, d.BufferDesc.Height, 0, 32, 0);
    (void)interval;
    br_dump_calls();
    br_res_dump();
}
#else
typedef void (*PFN_SetLoader)(PFN_vkGetInstanceProcAddr);
typedef void (*PFN_SetWindow)(ANativeWindow *, uint32_t, uint32_t);

static HWND br_hwnd(u64 guest) { return (HWND)(uintptr_t)guest; }

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
    // RCN_DXVK_LOWMEM=1 : DXVK économe en mémoire (téléphones de 8 Go) — pipelines compilés seulement au premier
    // dessin (sans bibliothèques de pipelines, qu'il compile sinon pour chacun des milliers de shaders d'Unreal
    // dès leur création) et 2 fils de compilation au lieu d'un par cœur : le tas natif montait à 1,3 Go dans
    // Clair Obscur. Réglages ajoutés à DXVK_CONFIG (que DXVK lit au chargement).
    if (getenv("RCN_DXVK_LOWMEM") && atoi(getenv("RCN_DXVK_LOWMEM")) > 0) {
        const char *old = getenv("DXVK_CONFIG");
        char conf[512];
        snprintf(conf, sizeof conf, "%s%sdxvk.enableGraphicsPipelineLibrary=False;dxvk.numCompilerThreads=2", old ? old : "",
                 old && *old ? ";" : "");
        setenv("DXVK_CONFIG", conf, 1);
        br_log("DXVK econome en memoire : %s\n", conf);
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
    br_res_dump();
}
#endif

// IDXGIFactory::CreateSwapChain(device, &desc, &chain) : la fenêtre de la description passe par br_hwnd.
static void br_special_IDXGIFactory_CreateSwapChain(RcnCtx *ctx) {
    IDXGIFactory *self = (IDXGIFactory *)br_unwrap(A0);
    if (!self || !A2) {
        RET((u64)(i64)(i32)(self ? DXGI_ERROR_INVALID_CALL : E_POINTER));
        return;
    }
    DXGI_SWAP_CHAIN_DESC desc = *(const DXGI_SWAP_CHAIN_DESC *)(uintptr_t)A2;
    desc.OutputWindow = br_hwnd((u64)(uintptr_t)desc.OutputWindow);
    br_before_present(ctx);
    IDXGISwapChain *chain = NULL;
    HRESULT r = self->lpVtbl->CreateSwapChain(self, (IUnknown *)br_unwrap(A1), &desc, A3 ? &chain : NULL);
    if (A3) br_out(ctx, A3, chain, 3);
    if (FAILED(r)) br_failed("IDXGIFactory::CreateSwapChain", r);
    RET((u64)(i64)r);
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
    // La chaîne d'images est créée avec l'appareil : fenêtre attendue d'abord (sinon E_FAIL).
    if (out_chain && STK(7)) br_before_present(ctx);
    IDXGISwapChain *chain = NULL;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    DXGI_SWAP_CHAIN_DESC chain_desc, *chain_desc_ptr = NULL;
    if (STK(7)) {
        chain_desc = *(const DXGI_SWAP_CHAIN_DESC *)(uintptr_t)STK(7);
        chain_desc.OutputWindow = br_hwnd((u64)(uintptr_t)chain_desc.OutputWindow);
        chain_desc_ptr = &chain_desc;
    }
    HRESULT r = dx.create_device_sc(br_unwrap(A0), (D3D_DRIVER_TYPE)A1, NULL, (UINT)A3, (const D3D_FEATURE_LEVEL *)(uintptr_t)STK(4),
                                    (UINT)STK(5), (UINT)STK(6), chain_desc_ptr,
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
