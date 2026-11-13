// RomCloud Native : objets COM de Direct3D 11 et DXGI (voir rcn_d3d11.h).
//
// Chaque genre d'objet sert la version la plus récente de son interface : sa table virtuelle suit
// l'ordre lu dans les en-têtes (rcn_d3d11_vt.h) et QueryInterface accepte toute interface de sa chaîne
// d'héritage. Une méthode non écrite nomme l'appel dans le journal et rend E_NOTIMPL.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "rcn_d3d11.h"
#include "rcn_d3d11_vt.h"

typedef struct {
    const char *name;
    const char *const *vt;
    u32 count;
    const char *const *chain;
} D3dClass;

#define CLASS(iface) {#iface, vt_##iface, VT_##iface##_COUNT, chain_##iface}
static const D3dClass classes[K_COUNT] = {
    [K_FACTORY] = CLASS(IDXGIFactory7),
    [K_ADAPTER] = CLASS(IDXGIAdapter4),
    [K_OUTPUT] = CLASS(IDXGIOutput6),
    [K_SWAPCHAIN] = CLASS(IDXGISwapChain4),
    [K_DXGIDEVICE] = CLASS(IDXGIDevice4),
    [K_SURFACE] = CLASS(IDXGISurface2),
    [K_DEVICE] = CLASS(ID3D11Device5),
    [K_CONTEXT] = CLASS(ID3D11DeviceContext4),
    [K_BUFFER] = CLASS(ID3D11Buffer),
    [K_TEX1D] = CLASS(ID3D11Texture1D),
    [K_TEX2D] = CLASS(ID3D11Texture2D1),
    [K_TEX3D] = CLASS(ID3D11Texture3D1),
    [K_SRV] = CLASS(ID3D11ShaderResourceView1),
    [K_RTV] = CLASS(ID3D11RenderTargetView1),
    [K_DSV] = CLASS(ID3D11DepthStencilView),
    [K_UAV] = CLASS(ID3D11UnorderedAccessView1),
    [K_VS] = CLASS(ID3D11VertexShader),
    [K_PS] = CLASS(ID3D11PixelShader),
    [K_GS] = CLASS(ID3D11GeometryShader),
    [K_HS] = CLASS(ID3D11HullShader),
    [K_DS] = CLASS(ID3D11DomainShader),
    [K_CS] = CLASS(ID3D11ComputeShader),
    [K_LAYOUT] = CLASS(ID3D11InputLayout),
    [K_BLEND] = CLASS(ID3D11BlendState1),
    [K_DEPTH] = CLASS(ID3D11DepthStencilState),
    [K_RASTER] = CLASS(ID3D11RasterizerState2),
    [K_SAMPLER] = CLASS(ID3D11SamplerState),
    [K_QUERY] = CLASS(ID3D11Query1),
    [K_MULTITHREAD] = CLASS(ID3D11Multithread),
    [K_ANNOTATION] = CLASS(ID3DUserDefinedAnnotation),
    [K_LINKAGE] = CLASS(ID3D11ClassLinkage),
};

int d3d_log_level = -1;
void d3d_log(const char *fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    rcn_log("d3d11 : %s", line);
}

// --- IID ---

static int iid_is(u64 iid, const RcnIid *k) {
    const u8 *p = PTR(iid);
    u32 a;
    u16 b, c;
    memcpy(&a, p, 4);
    memcpy(&b, p + 4, 2);
    memcpy(&c, p + 6, 2);
    return a == k->a && b == k->b && c == k->c && !memcmp(p + 8, k->d, 8);
}
const char *d3d_iid_name(u64 iid) {
    if (!iid) return "?";
    static const RcnIid unknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}, "IUnknown"};
    if (iid_is(iid, &unknown)) return unknown.name;
    for (size_t i = 0; i < sizeof rcn_d3d_iids / sizeof rcn_d3d_iids[0]; i++) {
        if (iid_is(iid, &rcn_d3d_iids[i])) return rcn_d3d_iids[i].name;
    }
    return NULL;
}
int d3d_has_iface(D3dObj *o, u64 iid) {
    const char *name = d3d_iid_name(iid);
    if (!name) return 0;
    if (!strcmp(name, "IUnknown")) return 1;
    for (const char *const *c = classes[o->kind].chain; *c; c++) {
        if (!strcmp(*c, name)) return 1;
    }
    // Familles proches servies par le même objet.
    if (o->kind == K_QUERY && (!strcmp(name, "ID3D11Predicate") || !strcmp(name, "ID3D11Counter"))) return 1;
    if ((o->kind == K_SWAPCHAIN || o->kind == K_SURFACE) && !strcmp(name, "IDXGIDeviceSubObject")) return 1;
    if (o->kind == K_SURFACE && (!strcmp(name, "IDXGIResource") || !strcmp(name, "IDXGIResource1"))) return 1;
    return 0;
}

// --- Références ---

static int immortal(u32 kind) {
    return kind == K_FACTORY || kind == K_ADAPTER || kind == K_OUTPUT || kind == K_DEVICE || kind == K_CONTEXT ||
           kind == K_DXGIDEVICE || kind == K_MULTITHREAD || kind == K_ANNOTATION;
}
void d3d_addref(D3dObj *o) {
    if (o) __atomic_add_fetch(&o->refs, 1, __ATOMIC_ACQ_REL);
}
void d3d_release(D3dObj *o) {
    if (!o) return;
    u32 left = __atomic_sub_fetch(&o->refs, 1, __ATOMIC_ACQ_REL);
    if (left || immortal(o->kind)) return;
    if (o->destroy) o->destroy(o);
    for (D3dPriv *p = o->priv, *next; p; p = next) {
        next = p->next;
        free(p);
    }
    if (o->kind == K_SURFACE && o->parent) o->parent->alias = NULL;
    d3d_release(o->parent);
    free(o);
}
void d3d_out(u64 slot, D3dObj *o) {
    if (!slot) return;
    d3d_addref(o);
    wr64(slot, (u64)(uintptr_t)o);
}

// --- Méthodes communes ---

// Autres interfaces d'un objet (IDXGIDevice d'un appareil…) : rcn_d3d11.c, rcn_dxgi.c.
D3dObj *d3d_query_extra(RcnCtx *ctx, D3dObj *o, u64 iid);

static void com_QueryInterface(RcnCtx *ctx) {
    D3dObj *o = OBJ(A0);
    u64 iid = A1, out = A2;
    if (!out) {
        RET(E_POINTER);
        return;
    }
    D3dObj *found = d3d_has_iface(o, iid) ? o : d3d_query_extra(ctx, o, iid);
    if (!found) {
        wr64(out, 0);
        const char *name = d3d_iid_name(iid);
        if (d3d_log_level > 0 || !name) {
            const u8 *g = PTR(iid);
            d3d_log("%s::QueryInterface(%s %08x-%02x%02x) refusée", classes[o->kind].name, name ? name : "?", *(const u32 *)g, g[4], g[5]);
        }
        RET(E_NOINTERFACE);
        return;
    }
    d3d_out(out, found);
    RET(S_OK);
}
static void com_AddRef(RcnCtx *ctx) {
    D3dObj *o = OBJ(A0);
    RET(__atomic_add_fetch(&o->refs, 1, __ATOMIC_ACQ_REL));
}
static void com_Release(RcnCtx *ctx) {
    D3dObj *o = OBJ(A0);
    u32 before = __atomic_load_n(&o->refs, __ATOMIC_ACQUIRE);
    d3d_release(o);
    RET(before ? before - 1 : 0);
}

static D3dPriv **priv_find(D3dObj *o, const u8 *guid) {
    D3dPriv **p = &o->priv;
    while (*p && memcmp((*p)->guid, guid, 16)) p = &(*p)->next;
    return p;
}
// GetPrivateData(guid, *size, data)
static void com_GetPrivateData(RcnCtx *ctx) {
    D3dObj *o = OBJ(A0);
    u64 size_at = A2, data = A3;
    D3dPriv *p = *priv_find(o, PTR(A1));
    if (!size_at) {
        RET(E_INVALIDARG);
        return;
    }
    if (!p) {
        wr32(size_at, 0);
        RET(DXGI_ERROR_NOT_FOUND);
        return;
    }
    u32 size = p->iface ? 8 : p->size;
    if (!data) {
        wr32(size_at, size);
        RET(S_OK);
        return;
    }
    if (rd32(size_at) < size) {
        wr32(size_at, size);
        RET(DXGI_ERROR_MORE_DATA);
        return;
    }
    if (p->iface) {
        wr64(data, p->iface);
        rcn_call_guest(ctx, rd64(rd64(p->iface) + 8), p->iface, 0, 0, 0); // AddRef
    } else {
        memcpy(PTR(data), p->data, size);
    }
    wr32(size_at, size);
    RET(S_OK);
}
static void set_private(RcnCtx *ctx, D3dObj *o, const u8 *guid, u32 size, const void *data, u64 iface) {
    D3dPriv **at = priv_find(o, guid), *old = *at;
    if (old) {
        *at = old->next;
        if (old->iface) rcn_call_guest(ctx, rd64(rd64(old->iface) + 16), old->iface, 0, 0, 0); // Release
        free(old);
    }
    if (!data && !iface) return;
    D3dPriv *p = calloc(1, sizeof *p + size);
    if (!p) return;
    memcpy(p->guid, guid, 16);
    p->size = size;
    p->iface = iface;
    if (data) memcpy(p->data, data, size);
    if (iface) rcn_call_guest(ctx, rd64(rd64(iface) + 8), iface, 0, 0, 0); // AddRef
    p->next = o->priv;
    o->priv = p;
}
static void com_SetPrivateData(RcnCtx *ctx) {
    set_private(ctx, OBJ(A0), PTR(A1), (u32)A2, A3 ? PTR(A3) : NULL, 0);
    RET(S_OK);
}
static void com_SetPrivateDataInterface(RcnCtx *ctx) {
    set_private(ctx, OBJ(A0), PTR(A1), 0, NULL, A2);
    RET(S_OK);
}
// ID3D11DeviceChild::GetDevice(**device) ; IDXGIDeviceSubObject::GetDevice(riid, **device).
static void com_GetDevice(RcnCtx *ctx) {
    D3dObj *o = OBJ(A0);
    D3dObj *device = o->device ? &o->device->obj : NULL;
    if (o->kind == K_SWAPCHAIN || o->kind == K_SURFACE || o->kind == K_DXGIDEVICE) {
        u64 iid = A1, out = A2;
        D3dObj *found = device && d3d_has_iface(device, iid) ? device : device ? d3d_query_extra(ctx, device, iid) : NULL;
        if (out) wr64(out, 0);
        if (!found) {
            RET(E_NOINTERFACE);
            return;
        }
        d3d_out(out, found);
        RET(S_OK);
        return;
    }
    if (A1) wr64(A1, 0);
    d3d_out(A1, device);
}
// IDXGIObject::GetParent(riid, **parent)
static void com_GetParent(RcnCtx *ctx) {
    D3dObj *o = OBJ(A0), *p = o->parent;
    u64 iid = A1, out = A2;
    if (out) wr64(out, 0);
    D3dObj *found = p && d3d_has_iface(p, iid) ? p : p ? d3d_query_extra(ctx, p, iid) : NULL;
    if (!found) {
        RET(E_NOINTERFACE);
        return;
    }
    d3d_out(out, found);
    RET(S_OK);
}

static const D3dMethod common[] = {
    {"QueryInterface", com_QueryInterface}, {"AddRef", com_AddRef}, {"Release", com_Release},
    {"GetPrivateData", com_GetPrivateData}, {"SetPrivateData", com_SetPrivateData},
    {"SetPrivateDataInterface", com_SetPrivateDataInterface}, {"GetDevice", com_GetDevice},
    {"GetParent", com_GetParent}, {NULL, NULL},
};

// Méthode non écrite : nommée (une fois) et E_NOTIMPL.
static void com_missing(RcnCtx *ctx) {
    const RcnImport *imp = THREAD(ctx)->current_import;
    static struct {
        const char *iface, *name;
    } told[256];
    static int told_count;
    int seen = 0;
    for (int i = 0; i < told_count; i++) seen |= told[i].iface == imp->dll && told[i].name == imp->name;
    if (!seen) {
        if (told_count < 256) {
            told[told_count].iface = imp->dll;
            told[told_count].name = imp->name;
            told_count++;
        }
        d3d_log("methode non fournie : %s::%s", imp->dll, imp->name);
    }
    RET(E_NOTIMPL);
}

static RcnFn find_method(const D3dMethod *list, const char *name) {
    for (; list && list->name; list++) {
        if (!strcmp(list->name, name)) return list->fn;
    }
    return NULL;
}

void *d3d_new(RcnCtx *ctx, D3dKind kind, size_t size, const D3dMethod *methods) {
    static u64 tables[K_COUNT];
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    if (d3d_log_level < 0) {
        const char *e = getenv("RCN_D3D_LOG");
        d3d_log_level = e ? atoi(e) : 0;
    }
    const D3dClass *c = &classes[kind];
    pthread_mutex_lock(&lock);
    if (!tables[kind]) {
        u64 *table = rcn_galloc(ctx, c->count * 8, 1);
        for (u32 i = 0; table && i < c->count; i++) {
            RcnFn fn = find_method(methods, c->vt[i]);
            if (!fn) fn = find_method(common, c->vt[i]);
            if (!fn) fn = com_missing;
            table[i] = rcn_thunk(ctx, c->name, c->vt[i], fn, 0);
        }
        tables[kind] = (u64)(uintptr_t)table;
    }
    pthread_mutex_unlock(&lock);
    D3dObj *o = calloc(1, size);
    if (!o) return NULL;
    o->vtable = tables[kind];
    o->refs = 1;
    o->kind = kind;
    return o;
}
