// RomCloud Native : Direct3D 9 des jeux x86 32 bits, servi par le Direct3D 9 de l'hôte — DXVK sur Android
// (libdxvk_d3d9.so, quand DXVK est choisi pour le jeu : RCN_D3D=dxvk), celui de Windows sur PC (essais).
//
// Le jeu (32 bits) et l'hôte (64 bits) partagent l'espace d'adresses, mais pas la taille des pointeurs :
//   - chaque objet de l'hôte est montré au jeu par un enveloppeur rangé sous 2 Go { table virtuelle de 32
//     bits (relais rcn_thunk vers rcn_dxvk_d3d9_gen.h), objet natif } ; les interfaces rendues au jeu sont
//     écrites sur 4 octets ;
//   - les structures qui portent un pointeur ou un handle changent de disposition : D3DPRESENT_PARAMETERS
//     (hDeviceWindow), D3DDEVICE_CREATION_PARAMETERS, D3DLOCKED_RECT / D3DLOCKED_BOX (pBits) ;
//   - la mémoire rendue par Lock peut être au-delà de 4 Go : le jeu reçoit une copie sous 2 Go, recopiée
//     chez l'hôte à Unlock (tampons dynamiques : copie gardée, sans relecture).
#if defined(RCN_HAS_DXVK) || defined(_WIN32)
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <initguid.h> // identifiants des interfaces de Direct3D 9 définis ici
#else
#include <windows.h> // en-têtes de DXVK : identifiants définis par chaque fichier (static const)
// Les en-têtes de DXVK, faits pour le C++, laissent THIS_ vide : en C, les méthodes y perdraient leur
// premier paramètre. Définis comme ceux de MinGW (INTERFACE : posé par d3d9.h avant chaque interface).
#undef THIS_
#undef THIS
#define THIS_ INTERFACE *This,
#define THIS INTERFACE *This
#endif
#include <d3d9.h>

#include "rcn_gui.h"
#include "rcn_internal.h"

void *rcn_dxvk_d3d9_create(RcnCtx *ctx);
void *rcn_dxvk_hwnd(u64 guest);
void rcn_dxvk_before_present(RcnCtx *ctx);

enum { D9_FAM_DYNAMIC = -1, D9_MAGIC = 0x39445852 /* « RXD9 » */, D9_MONITOR = 0x30001 };

// Enveloppeur vu par le jeu (sous 2 Go) : table virtuelle de 32 bits d'abord (convention COM).
typedef struct D9Wrap {
    u32 vtable;
    u32 magic;
    IUnknown *native;
    int family;
    struct D9Wrap *next;
} D9Wrap;

#define d9_log(...) fprintf(stderr, "[d3d9] " __VA_ARGS__)

static void d9_unsupported(RcnCtx *ctx, const char *what) {
    static int told;
    if (told++ < 32) d9_log("méthode non servie : %s\n", what);
    RET((u32)E_NOTIMPL);
}
static void d9_failed(const char *what, HRESULT r) {
    static int told;
    if ((u32)r == 0x88760868u || (u32)r == 0x8876021Cu) return; // D3DERR_DEVICELOST / WASSTILLDRAWING : normaux
    if (told++ < 64) d9_log("%s : échec 0x%08x\n", what, (unsigned)r);
}
static void *d9_unwrap(u64 guest) {
    if (!guest) return NULL;
    const D9Wrap *w = (const D9Wrap *)(uintptr_t)guest;
    return w->magic == D9_MAGIC ? (void *)w->native : (void *)(uintptr_t)guest;
}
static float d9_float_arg(RcnCtx *ctx, int n) {
    u32 bits = (u32)rcn_arg(ctx, n);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
static HWND d9_hwnd(u64 guest) { return guest ? (HWND)rcn_dxvk_hwnd(guest) : NULL; }

static void d9_out(RcnCtx *ctx, u64 guest, void *native, int family);
static void d9_out_iid(RcnCtx *ctx, u64 guest, void *native, REFIID iid, HRESULT *r);

#include "rcn_dxvk_d3d9_gen.h"

// --- Enveloppeurs ---

static pthread_mutex_t d9_lock = PTHREAD_MUTEX_INITIALIZER;
static D9Wrap *d9_table[4096];
static u32 d9_vtables[D9_FAMILY_COUNT];

static size_t d9_hash(const void *native, int family) {
    return (size_t)((((uintptr_t)native >> 4) * 0x9E3779B97F4A7C15ull + (u64)family) >> 52);
}
// Famille d'un objet de l'hôte de type inconnu (GetTexture rend une IDirect3DBaseTexture9…).
static int d9_probe(IUnknown *native) {
    for (int i = 0; i < D9_FAMILY_COUNT; i++) {
        void *p = NULL;
        if (SUCCEEDED(native->lpVtbl->QueryInterface(native, d9_family_iid[i], &p)) && p) {
            ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);
            return i;
        }
    }
    return -1;
}
// Famille d'une interface demandée par le jeu ; D9_FAM_DYNAMIC pour une base (IUnknown, ressource, texture
// de base) ; -2 si le pont ne la sert pas (interfaces « Ex »).
static int d9_same_iid(REFIID a, const IID *b) { return !memcmp(a, b, sizeof *b); }
static int d9_family_of_iid(REFIID iid) {
    for (int i = 0; i < D9_FAMILY_COUNT; i++)
        if (d9_same_iid(iid, d9_family_iid[i])) return i;
    if (d9_same_iid(iid, &IID_IUnknown) || d9_same_iid(iid, &IID_IDirect3DResource9) ||
        d9_same_iid(iid, &IID_IDirect3DBaseTexture9))
        return D9_FAM_DYNAMIC;
    return -2;
}
static u32 d9_wrap(RcnCtx *ctx, void *native_ptr, int family) {
    IUnknown *native = native_ptr;
    if (!native) return 0;
    if (family == D9_FAM_DYNAMIC) family = d9_probe(native);
    if (family < 0) {
        d9_log("objet d'interface inconnue %p\n", native_ptr);
        return 0;
    }
    pthread_mutex_lock(&d9_lock);
    size_t h = d9_hash(native, family);
    D9Wrap *w = d9_table[h];
    while (w && !(w->native == native && w->family == family)) w = w->next;
    if (!w) {
        if (!d9_vtables[family]) {
            u32 n = d9_family_method_count[family];
            u32 *table = rcn_galloc(ctx, n * 4, 1);
            for (u32 i = 0; table && i < n; i++) {
                const D9Method *m = &d9_family_methods[family][i];
                table[i] = (u32)rcn_thunk(ctx, d9_family_name[family], m->name, m->fn, m->pop);
            }
            d9_vtables[family] = (u32)(uintptr_t)table;
        }
        w = rcn_low_alloc(sizeof *w, 1);
        if (w) {
            w->vtable = d9_vtables[family];
            w->magic = D9_MAGIC;
            w->native = native;
            w->family = family;
            w->next = d9_table[h];
            d9_table[h] = w;
        }
    }
    pthread_mutex_unlock(&d9_lock);
    return (u32)(uintptr_t)w;
}
static void d9_out(RcnCtx *ctx, u64 guest, void *native, int family) {
    u32 w = d9_wrap(ctx, native, family);
    if (!w && native) ((IUnknown *)native)->lpVtbl->Release((IUnknown *)native);
    wr32(guest, w);
}
static void d9_out_iid(RcnCtx *ctx, u64 guest, void *native, REFIID iid, HRESULT *r) {
    if (!native) {
        wr32(guest, 0);
        return;
    }
    int family = d9_family_of_iid(iid);
    if (family == -2) {
        d9_log("interface non servie {%08x-%04x-%04x}\n", (unsigned)iid->Data1, iid->Data2, iid->Data3);
        ((IUnknown *)native)->lpVtbl->Release((IUnknown *)native);
        wr32(guest, 0);
        if (r) *r = E_NOINTERFACE;
        return;
    }
    d9_out(ctx, guest, native, family);
}

// --- Copies sous 2 Go de la mémoire verrouillée ---

// Zone verrouillée : `slices` tranches de `rows` lignes de `row_bytes` octets utiles, au pas `pitch` (lignes) et
// `slice_pitch` (tranches), les mêmes chez l'hôte et dans la copie. Seules les octets utiles sont copiés : le
// reste d'une ligne (hors du rectangle demandé) appartient à d'autres pixels.
typedef struct {
    u32 rows, row_bytes, pitch, slices, slice_pitch;
} D9Span;
typedef struct {
    void *native;
    u32 sub;       // niveau, face << 16 | niveau ; 0 pour une surface, un volume, un tampon
    u8 *host;      // mémoire de l'hôte (début de la zone verrouillée)
    u8 *copy;      // copie donnée au jeu (début de la même zone)
    D9Span span;
    u32 flags;
    int owned;     // copie libérée à Unlock (sinon copie gardée d'un tampon dynamique)
} D9Locked;

static size_t d9_span_size(const D9Span *s) {
    if (!s->rows || !s->slices) return 0;
    return (size_t)(s->slices - 1) * s->slice_pitch + (size_t)(s->rows - 1) * s->pitch + s->row_bytes;
}
static void d9_span_copy(u8 *dst, const u8 *src, const D9Span *s) {
    if (s->slices == 1 && (s->rows == 1 || s->row_bytes == s->pitch)) {
        memcpy(dst, src, d9_span_size(s));
        return;
    }
    for (u32 z = 0; z < s->slices; z++)
        for (u32 y = 0; y < s->rows; y++) {
            size_t at = (size_t)z * s->slice_pitch + (size_t)y * s->pitch;
            memcpy(dst + at, src + at, s->row_bytes);
        }
}
static D9Locked *d9_locks;
static u32 d9_lock_count, d9_lock_capacity;

// Copie gardée de chaque tampon dynamique (son contenu est celui que le jeu y a écrit).
typedef struct D9Shadow {
    void *native;
    u8 *copy;
    size_t size;
    struct D9Shadow *next;
} D9Shadow;
static D9Shadow *d9_shadows[1024];

static void d9_lock_add(void *native, u32 sub, u8 *host, u8 *copy, D9Span span, u32 flags, int owned) {
    pthread_mutex_lock(&d9_lock);
    if (d9_lock_count == d9_lock_capacity) {
        u32 capacity = d9_lock_capacity ? d9_lock_capacity * 2 : 256;
        D9Locked *grown = realloc(d9_locks, capacity * sizeof *grown);
        if (grown) d9_locks = grown, d9_lock_capacity = capacity;
    }
    if (d9_lock_count < d9_lock_capacity) d9_locks[d9_lock_count++] = (D9Locked){native, sub, host, copy, span, flags, owned};
    pthread_mutex_unlock(&d9_lock);
}
// Fin d'un verrou (le plus récent de cet objet) : copie rendue à l'hôte, sauf D3DLOCK_READONLY.
static void d9_lock_end(void *native, u32 sub) {
    D9Locked l = {0};
    int found = 0;
    pthread_mutex_lock(&d9_lock);
    for (u32 i = d9_lock_count; i-- > 0;) {
        if (d9_locks[i].native != native || d9_locks[i].sub != sub) continue;
        l = d9_locks[i];
        d9_locks[i] = d9_locks[--d9_lock_count];
        found = 1;
        break;
    }
    pthread_mutex_unlock(&d9_lock);
    if (!found) return;
    if (!(l.flags & D3DLOCK_READONLY) && l.host && l.copy) d9_span_copy(l.host, l.copy, &l.span);
    if (l.owned) rcn_low_free(l.copy);
}
static u8 *d9_shadow_of(void *native, size_t size, int *created) {
    size_t h = ((uintptr_t)native >> 4) & 1023;
    pthread_mutex_lock(&d9_lock);
    D9Shadow *s = d9_shadows[h];
    while (s && s->native != native) s = s->next;
    *created = 0;
    if (!s) {
        s = calloc(1, sizeof *s);
        if (s) {
            s->native = native;
            s->copy = rcn_low_alloc(size ? size : 1, 1);
            s->size = size;
            s->next = d9_shadows[h];
            d9_shadows[h] = s;
            *created = 1;
        }
    }
    pthread_mutex_unlock(&d9_lock);
    return s ? s->copy : NULL;
}
static void d9_shadow_forget(void *native) {
    size_t h = ((uintptr_t)native >> 4) & 1023;
    pthread_mutex_lock(&d9_lock);
    for (D9Shadow **p = &d9_shadows[h]; *p; p = &(*p)->next) {
        if ((*p)->native != native) continue;
        D9Shadow *s = *p;
        *p = s->next;
        rcn_low_free(s->copy);
        free(s);
        break;
    }
    pthread_mutex_unlock(&d9_lock);
}

// Formats : côté d'un bloc (1 ou 4) et octets par bloc (par pixel si le bloc fait 1).
static void d9_format_unit(D3DFORMAT f, u32 *block, u32 *bytes) {
    *block = 1;
    switch ((u32)f) {
    case MAKEFOURCC('D', 'X', 'T', '1'):
    case MAKEFOURCC('A', 'T', 'I', '1'):
    case MAKEFOURCC('B', 'C', '4', 'U'):
    case MAKEFOURCC('B', 'C', '4', 'S'): *block = 4, *bytes = 8; return;
    case MAKEFOURCC('D', 'X', 'T', '2'):
    case MAKEFOURCC('D', 'X', 'T', '3'):
    case MAKEFOURCC('D', 'X', 'T', '4'):
    case MAKEFOURCC('D', 'X', 'T', '5'):
    case MAKEFOURCC('A', 'T', 'I', '2'):
    case MAKEFOURCC('B', 'C', '5', 'U'):
    case MAKEFOURCC('B', 'C', '5', 'S'): *block = 4, *bytes = 16; return;
    case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_P8: case D3DFMT_A4L4: case D3DFMT_R3G3B2: case D3DFMT_S8_LOCKABLE:
        *bytes = 1; return;
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
    case D3DFMT_A8R3G3B2: case D3DFMT_A8L8: case D3DFMT_A8P8: case D3DFMT_L16: case D3DFMT_V8U8: case D3DFMT_L6V5U5:
    case D3DFMT_R16F: case D3DFMT_D16: case D3DFMT_D16_LOCKABLE: case D3DFMT_D15S1:
        *bytes = 2; return;
    case D3DFMT_R8G8B8: *bytes = 3; return;
    case D3DFMT_A16B16G16R16: case D3DFMT_A16B16G16R16F: case D3DFMT_G32R32F: case D3DFMT_Q16W16V16U16:
        *bytes = 8; return;
    case D3DFMT_A32B32G32R32F: *bytes = 16; return;
    default: *bytes = 4; return;
    }
}
// Rectangle (ou boîte) demandé par le jeu, ou toute la surface : largeur, hauteur, profondeur.
static void d9_rect_dims(u64 guest_rect, u32 width, u32 height, u32 *w, u32 *h) {
    *w = width, *h = height;
    if (!guest_rect) return;
    i32 left = (i32)rd32(guest_rect), top = (i32)rd32(guest_rect + 4);
    i32 right = (i32)rd32(guest_rect + 8), bottom = (i32)rd32(guest_rect + 12);
    *w = right > left ? (u32)(right - left) : 0;
    *h = bottom > top ? (u32)(bottom - top) : 0;
}
// Copie sous 2 Go d'une zone verrouillée chez l'hôte (déjà réduite au rectangle demandé) : seulement ses lignes
// (sauf D3DLOCK_DISCARD : contenu indéfini). Rend la copie, ou NULL (journal).
static u8 *d9_copy_span(void *native, u32 sub, u8 *host, D9Span span, u32 flags, const char *what) {
    size_t size = d9_span_size(&span);
    u8 *copy = rcn_low_alloc(size ? size : 1, 0);
    if (!copy) {
        rcn_log("[d3d9] %s : pas de place sous 2 Go pour %zu octets (%u verrous ouverts)", what, size, d9_lock_count);
        return NULL;
    }
    if (!(flags & D3DLOCK_DISCARD) && host) d9_span_copy(copy, host, &span);
    d9_lock_add(native, sub, host, copy, span, flags, 1);
    return copy;
}
// Verrou d'un rectangle : l'hôte verrouille le rectangle demandé (D3DLOCKED_RECT { pas, début du rectangle }) ;
// le jeu reçoit { pas, copie du rectangle }. Sans place pour la copie : verrou de l'hôte rendu, E_OUTOFMEMORY.
static void d9_give_rect(RcnCtx *ctx, HRESULT r, void *native, u32 sub, const D3DLOCKED_RECT *lr, D3DFORMAT format,
                         u32 width, u32 height, u64 guest_lr, u32 flags, void (*unlock)(void *, u32)) {
    if (FAILED(r)) {
        d9_failed("LockRect", r);
        RET((u32)r);
        return;
    }
    u32 block, bytes;
    d9_format_unit(format, &block, &bytes);
    D9Span span = {(height + block - 1) / block, (width + block - 1) / block * bytes, (u32)lr->Pitch, 1, 0};
    if (span.row_bytes > span.pitch) span.row_bytes = span.pitch;
    u8 *copy = d9_copy_span(native, sub, lr->pBits, span, flags, "LockRect");
    if (!copy) {
        unlock(native, sub);
        RET((u32)E_OUTOFMEMORY);
        return;
    }
    wr32(guest_lr, (u32)lr->Pitch);
    wr32(guest_lr + 4, (u32)(uintptr_t)copy);
    RET(0);
}
static void d9_give_box(RcnCtx *ctx, HRESULT r, void *native, u32 sub, const D3DLOCKED_BOX *lb, D3DFORMAT format,
                        u32 width, u32 height, u32 depth, u64 guest_lb, u32 flags, void (*unlock)(void *, u32)) {
    if (FAILED(r)) {
        d9_failed("LockBox", r);
        RET((u32)r);
        return;
    }
    u32 block, bytes;
    d9_format_unit(format, &block, &bytes);
    D9Span span = {(height + block - 1) / block, (width + block - 1) / block * bytes, (u32)lb->RowPitch, depth ? depth : 1,
                   (u32)lb->SlicePitch};
    if (span.row_bytes > span.pitch) span.row_bytes = span.pitch;
    u8 *copy = d9_copy_span(native, sub, lb->pBits, span, flags, "LockBox");
    if (!copy) {
        unlock(native, sub);
        RET((u32)E_OUTOFMEMORY);
        return;
    }
    wr32(guest_lb, (u32)lb->RowPitch);
    wr32(guest_lb + 4, (u32)lb->SlicePitch);
    wr32(guest_lb + 8, (u32)(uintptr_t)copy);
    RET(0);
}
// Verrous de l'hôte rendus quand la copie manque.
static void d9_unlock_texture(void *n, u32 sub) { ((IDirect3DTexture9 *)n)->lpVtbl->UnlockRect(n, sub); }
static void d9_unlock_cube(void *n, u32 sub) {
    ((IDirect3DCubeTexture9 *)n)->lpVtbl->UnlockRect(n, (D3DCUBEMAP_FACES)(sub >> 16), sub & 0xFFFF);
}
static void d9_unlock_surface(void *n, u32 sub) { (void)sub, ((IDirect3DSurface9 *)n)->lpVtbl->UnlockRect(n); }
static void d9_unlock_volume_texture(void *n, u32 sub) { ((IDirect3DVolumeTexture9 *)n)->lpVtbl->UnlockBox(n, sub); }
static void d9_unlock_volume(void *n, u32 sub) { (void)sub, ((IDirect3DVolume9 *)n)->lpVtbl->UnlockBox(n); }
// D3DBOX { Left, Top, Right, Bottom, Front, Back } : largeur, hauteur, profondeur.
static void d9_box_dims(u64 box, u32 width, u32 height, u32 depth, u32 *w, u32 *h, u32 *dp) {
    *w = width, *h = height, *dp = depth;
    if (!box) return;
    u32 l = rd32(box), tp = rd32(box + 4), r = rd32(box + 8), bt = rd32(box + 12), f = rd32(box + 16), bk = rd32(box + 20);
    *w = r > l ? r - l : 0, *h = bt > tp ? bt - tp : 0, *dp = bk > f ? bk - f : 0;
}

// IDirect3DTexture9::LockRect(niveau, &verrou, rect, options) / UnlockRect(niveau)
static void d9_special_IDirect3DTexture9_LockRect(RcnCtx *ctx) {
    IDirect3DTexture9 *self = d9_unwrap(A0);
    if (!self || !A2) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    UINT level = (UINT)A1;
    u32 flags = (u32)STK(4);
    D3DSURFACE_DESC d;
    HRESULT r = self->lpVtbl->GetLevelDesc(self, level, &d);
    D3DLOCKED_RECT lr = {0};
    if (SUCCEEDED(r)) r = self->lpVtbl->LockRect(self, level, &lr, A3 ? (const RECT *)PTR(A3) : NULL, flags);
    u32 w, h;
    d9_rect_dims(A3, d.Width, d.Height, &w, &h);
    d9_give_rect(ctx, r, self, level, &lr, d.Format, w, h, A2, flags, d9_unlock_texture);
}
static void d9_special_IDirect3DTexture9_UnlockRect(RcnCtx *ctx) {
    IDirect3DTexture9 *self = d9_unwrap(A0);
    if (!self) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    d9_lock_end(self, (u32)A1);
    RET((u32)self->lpVtbl->UnlockRect(self, (UINT)A1));
}
// IDirect3DCubeTexture9::LockRect(face, niveau, &verrou, rect, options) / UnlockRect(face, niveau)
static void d9_special_IDirect3DCubeTexture9_LockRect(RcnCtx *ctx) {
    IDirect3DCubeTexture9 *self = d9_unwrap(A0);
    if (!self || !A3) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    D3DCUBEMAP_FACES face = (D3DCUBEMAP_FACES)(u32)A1;
    UINT level = (UINT)A2;
    u32 flags = (u32)STK(5);
    D3DSURFACE_DESC d;
    HRESULT r = self->lpVtbl->GetLevelDesc(self, level, &d);
    D3DLOCKED_RECT lr = {0};
    u64 rect = STK(4);
    if (SUCCEEDED(r)) r = self->lpVtbl->LockRect(self, face, level, &lr, rect ? (const RECT *)PTR(rect) : NULL, flags);
    u32 w, h;
    d9_rect_dims(rect, d.Width, d.Height, &w, &h);
    d9_give_rect(ctx, r, self, (u32)face << 16 | level, &lr, d.Format, w, h, A3, flags, d9_unlock_cube);
}
static void d9_special_IDirect3DCubeTexture9_UnlockRect(RcnCtx *ctx) {
    IDirect3DCubeTexture9 *self = d9_unwrap(A0);
    if (!self) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    d9_lock_end(self, (u32)A1 << 16 | (u32)A2);
    RET((u32)self->lpVtbl->UnlockRect(self, (D3DCUBEMAP_FACES)(u32)A1, (UINT)A2));
}
// IDirect3DSurface9::LockRect(&verrou, rect, options) / UnlockRect()
static void d9_special_IDirect3DSurface9_LockRect(RcnCtx *ctx) {
    IDirect3DSurface9 *self = d9_unwrap(A0);
    if (!self || !A1) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    u32 flags = (u32)A3;
    D3DSURFACE_DESC d;
    HRESULT r = self->lpVtbl->GetDesc(self, &d);
    D3DLOCKED_RECT lr = {0};
    if (SUCCEEDED(r)) r = self->lpVtbl->LockRect(self, &lr, A2 ? (const RECT *)PTR(A2) : NULL, flags);
    u32 w, h;
    d9_rect_dims(A2, d.Width, d.Height, &w, &h);
    d9_give_rect(ctx, r, self, 0, &lr, d.Format, w, h, A1, flags, d9_unlock_surface);
}
static void d9_special_IDirect3DSurface9_UnlockRect(RcnCtx *ctx) {
    IDirect3DSurface9 *self = d9_unwrap(A0);
    if (!self) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    d9_lock_end(self, 0);
    RET((u32)self->lpVtbl->UnlockRect(self));
}
// IDirect3DVolumeTexture9::LockBox(niveau, &verrou, boîte, options) / UnlockBox(niveau)
static void d9_special_IDirect3DVolumeTexture9_LockBox(RcnCtx *ctx) {
    IDirect3DVolumeTexture9 *self = d9_unwrap(A0);
    if (!self || !A2) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    UINT level = (UINT)A1;
    u32 flags = (u32)STK(4);
    D3DVOLUME_DESC d;
    HRESULT r = self->lpVtbl->GetLevelDesc(self, level, &d);
    D3DLOCKED_BOX lb = {0};
    if (SUCCEEDED(r)) r = self->lpVtbl->LockBox(self, level, &lb, A3 ? (const D3DBOX *)PTR(A3) : NULL, flags);
    u32 w, h, dp;
    d9_box_dims(A3, d.Width, d.Height, d.Depth, &w, &h, &dp);
    d9_give_box(ctx, r, self, level, &lb, d.Format, w, h, dp, A2, flags, d9_unlock_volume_texture);
}
static void d9_special_IDirect3DVolumeTexture9_UnlockBox(RcnCtx *ctx) {
    IDirect3DVolumeTexture9 *self = d9_unwrap(A0);
    if (!self) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    d9_lock_end(self, (u32)A1);
    RET((u32)self->lpVtbl->UnlockBox(self, (UINT)A1));
}
// IDirect3DVolume9::LockBox(&verrou, boîte, options) / UnlockBox()
static void d9_special_IDirect3DVolume9_LockBox(RcnCtx *ctx) {
    IDirect3DVolume9 *self = d9_unwrap(A0);
    if (!self || !A1) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    u32 flags = (u32)A3;
    D3DVOLUME_DESC d;
    HRESULT r = self->lpVtbl->GetDesc(self, &d);
    D3DLOCKED_BOX lb = {0};
    if (SUCCEEDED(r)) r = self->lpVtbl->LockBox(self, &lb, A2 ? (const D3DBOX *)PTR(A2) : NULL, flags);
    u32 w, h, dp;
    d9_box_dims(A2, d.Width, d.Height, d.Depth, &w, &h, &dp);
    d9_give_box(ctx, r, self, 0, &lb, d.Format, w, h, dp, A1, flags, d9_unlock_volume);
}
static void d9_special_IDirect3DVolume9_UnlockBox(RcnCtx *ctx) {
    IDirect3DVolume9 *self = d9_unwrap(A0);
    if (!self) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    d9_lock_end(self, 0);
    RET((u32)self->lpVtbl->UnlockBox(self));
}

// Lock(début, taille, &données, options) d'un tampon de sommets ou d'index (taille 0 : jusqu'à la fin).
// Tampon dynamique : copie gardée (le jeu y écrit à chaque image, souvent en D3DLOCK_NOOVERWRITE), relue
// chez l'hôte seulement à sa création. Autre tampon : copie le temps du verrou.
static void d9_buffer_lock(RcnCtx *ctx, void *native, HRESULT r, u8 *host, u32 offset, u32 size, u32 total, u32 usage,
                           u64 guest_out, u32 flags) {
    if (FAILED(r) || !host) {
        if (FAILED(r)) d9_failed("Lock", r);
        RET((u32)(FAILED(r) ? r : D3DERR_INVALIDCALL));
        return;
    }
    size_t n = size ? size : (offset < total ? total - offset : 0);
    u8 *copy;
    int owned = 0;
    if (usage & D3DUSAGE_DYNAMIC) {
        int created;
        u8 *shadow = d9_shadow_of(native, total, &created);
        copy = shadow ? shadow + offset : NULL;
        if (copy && created && !(flags & D3DLOCK_DISCARD)) memcpy(copy, host, n);
    } else {
        copy = rcn_low_alloc(n ? n : 1, 0);
        owned = 1;
        if (copy && !(flags & D3DLOCK_DISCARD)) memcpy(copy, host, n);
    }
    if (!copy) {
        RET((u32)E_OUTOFMEMORY);
        return;
    }
    d9_lock_add(native, 0, host, copy, (D9Span){1, (u32)n, (u32)n, 1, 0}, flags, owned);
    wr32(guest_out, (u32)(uintptr_t)copy);
    RET(0);
}
static void d9_special_IDirect3DVertexBuffer9_Lock(RcnCtx *ctx) {
    IDirect3DVertexBuffer9 *self = d9_unwrap(A0);
    if (!self || !STK(3)) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    D3DVERTEXBUFFER_DESC d;
    HRESULT r = self->lpVtbl->GetDesc(self, &d);
    void *p = NULL;
    u32 flags = (u32)STK(4);
    if (SUCCEEDED(r)) r = self->lpVtbl->Lock(self, (UINT)A1, (UINT)A2, &p, flags);
    d9_buffer_lock(ctx, self, r, p, (u32)A1, (u32)A2, d.Size, d.Usage, STK(3), flags);
}
static void d9_special_IDirect3DVertexBuffer9_Unlock(RcnCtx *ctx) {
    IDirect3DVertexBuffer9 *self = d9_unwrap(A0);
    if (!self) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    d9_lock_end(self, 0);
    RET((u32)self->lpVtbl->Unlock(self));
}
static void d9_special_IDirect3DIndexBuffer9_Lock(RcnCtx *ctx) {
    IDirect3DIndexBuffer9 *self = d9_unwrap(A0);
    if (!self || !STK(3)) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    D3DINDEXBUFFER_DESC d;
    HRESULT r = self->lpVtbl->GetDesc(self, &d);
    void *p = NULL;
    u32 flags = (u32)STK(4);
    if (SUCCEEDED(r)) r = self->lpVtbl->Lock(self, (UINT)A1, (UINT)A2, &p, flags);
    d9_buffer_lock(ctx, self, r, p, (u32)A1, (u32)A2, d.Size, d.Usage, STK(3), flags);
}
static void d9_special_IDirect3DIndexBuffer9_Unlock(RcnCtx *ctx) {
    IDirect3DIndexBuffer9 *self = d9_unwrap(A0);
    if (!self) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    d9_lock_end(self, 0);
    RET((u32)self->lpVtbl->Unlock(self));
}

// --- IUnknown ---

static void d9_special_QueryInterface(RcnCtx *ctx) {
    IUnknown *self = d9_unwrap(A0);
    u64 out = A2;
    if (!self || !out) {
        RET((u32)E_POINTER);
        return;
    }
    REFIID iid = (REFIID)(uintptr_t)A1;
    void *p = NULL;
    HRESULT r = self->lpVtbl->QueryInterface(self, iid, &p);
    if (SUCCEEDED(r)) d9_out_iid(ctx, out, p, iid, &r);
    else wr32(out, 0);
    RET((u32)r);
}
static void d9_special_AddRef(RcnCtx *ctx) {
    IUnknown *self = d9_unwrap(A0);
    RET(self ? self->lpVtbl->AddRef(self) : 0);
}
// L'enveloppeur reste lié à l'objet (retrouvé par adresse et famille si l'hôte réutilise l'adresse).
static void d9_special_Release(RcnCtx *ctx) {
    IUnknown *self = d9_unwrap(A0);
    if (!self) {
        RET(0);
        return;
    }
    ULONG n = self->lpVtbl->Release(self);
    if (!n) d9_shadow_forget(self);
    RET(n);
}

// --- Paramètres de présentation (hDeviceWindow : 4 octets chez le jeu) ---
// D3DPRESENT_PARAMETERS de 32 bits : 7 champs, hDeviceWindow (+28), puis 5 champs (56 octets).
static u64 d9_guest_window;     // fenêtre du jeu (rendue telle quelle par GetPresentParameters)
static u64 d9_guest_focus;      // fenêtre de CreateDevice (GetCreationParameters)
static u32 d9_width = 1920, d9_height = 1080, d9_interval;

static void d9_pp_in(u64 g, D3DPRESENT_PARAMETERS *pp) {
    memset(pp, 0, sizeof *pp);
    pp->BackBufferWidth = rd32(g);
    pp->BackBufferHeight = rd32(g + 4);
    pp->BackBufferFormat = (D3DFORMAT)rd32(g + 8);
    pp->BackBufferCount = rd32(g + 12);
    pp->MultiSampleType = (D3DMULTISAMPLE_TYPE)rd32(g + 16);
    pp->MultiSampleQuality = rd32(g + 20);
    pp->SwapEffect = (D3DSWAPEFFECT)rd32(g + 24);
    if (rd32(g + 28)) d9_guest_window = rd32(g + 28);
    pp->hDeviceWindow = d9_hwnd(rd32(g + 28));
    pp->Windowed = (BOOL)rd32(g + 32);
    pp->EnableAutoDepthStencil = (BOOL)rd32(g + 36);
    pp->AutoDepthStencilFormat = (D3DFORMAT)rd32(g + 40);
    pp->Flags = rd32(g + 44);
    pp->FullScreen_RefreshRateInHz = rd32(g + 48);
    pp->PresentationInterval = rd32(g + 52);
}
static void d9_pp_out(u64 g, const D3DPRESENT_PARAMETERS *pp, u64 guest_window) {
    wr32(g, pp->BackBufferWidth);
    wr32(g + 4, pp->BackBufferHeight);
    wr32(g + 8, (u32)pp->BackBufferFormat);
    wr32(g + 12, pp->BackBufferCount);
    wr32(g + 16, (u32)pp->MultiSampleType);
    wr32(g + 20, pp->MultiSampleQuality);
    wr32(g + 24, (u32)pp->SwapEffect);
    wr32(g + 28, (u32)guest_window);
    wr32(g + 32, (u32)pp->Windowed);
    wr32(g + 36, (u32)pp->EnableAutoDepthStencil);
    wr32(g + 40, (u32)pp->AutoDepthStencilFormat);
    wr32(g + 44, pp->Flags);
    wr32(g + 48, pp->FullScreen_RefreshRateInHz);
    wr32(g + 52, pp->PresentationInterval);
}
static void d9_pp_note(const D3DPRESENT_PARAMETERS *pp) {
    if (pp->BackBufferWidth) d9_width = pp->BackBufferWidth;
    if (pp->BackBufferHeight) d9_height = pp->BackBufferHeight;
    d9_interval = pp->PresentationInterval;
}

// IDirect3D9::CreateDevice(adaptateur, type, fenêtre, options, &paramètres, &appareil)
static void d9_special_IDirect3D9_CreateDevice(RcnCtx *ctx) {
    IDirect3D9 *self = d9_unwrap(A0);
    u64 g_pp = STK(5), g_out = STK(6);
    if (!self || !g_pp || !g_out) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    D3DPRESENT_PARAMETERS pp;
    d9_pp_in(g_pp, &pp);
    d9_guest_focus = A3;
    u64 guest_window = rd32(g_pp + 28) ? rd32(g_pp + 28) : A3;
    if (!pp.hDeviceWindow) pp.hDeviceWindow = d9_hwnd(A3);
    rcn_dxvk_before_present(ctx); // Android : la fenêtre doit exister avant la chaîne d'images
    IDirect3DDevice9 *device = NULL;
    HRESULT r = self->lpVtbl->CreateDevice(self, (UINT)A1, (D3DDEVTYPE)(u32)A2, d9_hwnd(A3), (DWORD)STK(4), &pp, &device);
    d9_pp_out(g_pp, &pp, guest_window);
    d9_pp_note(&pp);
    d9_out(ctx, g_out, SUCCEEDED(r) ? device : NULL, 1);
    d9_log("CreateDevice %ux%u : 0x%08x\n", pp.BackBufferWidth, pp.BackBufferHeight, (unsigned)r);
    RET((u32)r);
}
// IDirect3DDevice9::Reset(&paramètres)
static void d9_special_IDirect3DDevice9_Reset(RcnCtx *ctx) {
    IDirect3DDevice9 *self = d9_unwrap(A0);
    if (!self || !A1) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    D3DPRESENT_PARAMETERS pp;
    d9_pp_in(A1, &pp);
    u64 guest_window = rd32(A1 + 28);
    if (!pp.hDeviceWindow) pp.hDeviceWindow = d9_hwnd(d9_guest_window ? d9_guest_window : d9_guest_focus);
    HRESULT r = self->lpVtbl->Reset(self, &pp);
    d9_pp_out(A1, &pp, guest_window);
    d9_pp_note(&pp);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::Reset", r);
    RET((u32)r);
}
// IDirect3DDevice9::CreateAdditionalSwapChain(&paramètres, &chaîne)
static void d9_special_IDirect3DDevice9_CreateAdditionalSwapChain(RcnCtx *ctx) {
    IDirect3DDevice9 *self = d9_unwrap(A0);
    if (!self || !A1 || !A2) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    D3DPRESENT_PARAMETERS pp;
    d9_pp_in(A1, &pp);
    u64 guest_window = rd32(A1 + 28);
    IDirect3DSwapChain9 *chain = NULL;
    HRESULT r = self->lpVtbl->CreateAdditionalSwapChain(self, &pp, &chain);
    d9_pp_out(A1, &pp, guest_window);
    d9_out(ctx, A2, SUCCEEDED(r) ? chain : NULL, 2);
    RET((u32)r);
}
// IDirect3DSwapChain9::GetPresentParameters(&paramètres)
static void d9_special_IDirect3DSwapChain9_GetPresentParameters(RcnCtx *ctx) {
    IDirect3DSwapChain9 *self = d9_unwrap(A0);
    if (!self || !A1) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof pp);
    HRESULT r = self->lpVtbl->GetPresentParameters(self, &pp);
    if (SUCCEEDED(r)) d9_pp_out(A1, &pp, d9_guest_window ? d9_guest_window : d9_guest_focus);
    RET((u32)r);
}
// IDirect3DDevice9::GetCreationParameters(&paramètres) : adaptateur, type, fenêtre (+8, 4 octets), options.
static void d9_special_IDirect3DDevice9_GetCreationParameters(RcnCtx *ctx) {
    IDirect3DDevice9 *self = d9_unwrap(A0);
    if (!self || !A1) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    D3DDEVICE_CREATION_PARAMETERS p;
    HRESULT r = self->lpVtbl->GetCreationParameters(self, &p);
    if (SUCCEEDED(r)) {
        wr32(A1, p.AdapterOrdinal);
        wr32(A1 + 4, (u32)p.DeviceType);
        wr32(A1 + 8, (u32)d9_guest_focus);
        wr32(A1 + 12, p.BehaviorFlags);
    }
    RET((u32)r);
}
// IDirect3D9::GetAdapterMonitor(adaptateur) : l'écran unique du user32 du runtime.
static void d9_special_IDirect3D9_GetAdapterMonitor(RcnCtx *ctx) { RET(D9_MONITOR); }

// --- Présentation : fenêtre d'Android à jour avant, rythme des images et entrées du runtime après ---
static u64 d9_presents;
static void d9_after_present(RcnCtx *ctx) {
    d9_presents++;
    rcn_present(ctx, NULL, d9_width, d9_height, 0, 32, d9_interval != D3DPRESENT_INTERVAL_IMMEDIATE);
}
// IDirect3DDevice9::Present(source, destination, fenêtre, région)
static void d9_special_IDirect3DDevice9_Present(RcnCtx *ctx) {
    IDirect3DDevice9 *self = d9_unwrap(A0);
    if (!self) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    rcn_dxvk_before_present(ctx);
    HRESULT r = self->lpVtbl->Present(self, (const RECT *)(uintptr_t)A1, (const RECT *)(uintptr_t)A2, d9_hwnd(A3),
                                      (const RGNDATA *)(uintptr_t)STK(4));
    if (FAILED(r)) d9_failed("IDirect3DDevice9::Present", r);
    d9_after_present(ctx);
    RET((u32)r);
}
// IDirect3DSwapChain9::Present(source, destination, fenêtre, région, options)
static void d9_special_IDirect3DSwapChain9_Present(RcnCtx *ctx) {
    IDirect3DSwapChain9 *self = d9_unwrap(A0);
    if (!self) {
        RET((u32)D3DERR_INVALIDCALL);
        return;
    }
    rcn_dxvk_before_present(ctx);
    HRESULT r = self->lpVtbl->Present(self, (const RECT *)(uintptr_t)A1, (const RECT *)(uintptr_t)A2, d9_hwnd(A3),
                                      (const RGNDATA *)(uintptr_t)STK(4), (DWORD)STK(5));
    if (FAILED(r)) d9_failed("IDirect3DSwapChain9::Present", r);
    d9_after_present(ctx);
    RET((u32)r);
}

// --- Fonctions exportées par d3d9.dll ---

typedef IDirect3D9 *(WINAPI *D9Create)(UINT);
// Direct3DCreate9(version du SDK)
static void d9_Direct3DCreate9(RcnCtx *ctx) {
    D9Create create = (D9Create)rcn_dxvk_d3d9_create(ctx);
    IDirect3D9 *d3d = create ? create((UINT)A0) : NULL;
    d9_log("Direct3DCreate9(%u) : %p\n", (unsigned)(u32)A0, (void *)d3d);
    RET(d3d ? d9_wrap(ctx, d3d, 0) : 0);
}
// Direct3DCreate9Ex : interfaces « Ex » non servies (le jeu se replie sur Direct3DCreate9).
static void d9_Direct3DCreate9Ex(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    RET((u32)D3DERR_NOTAVAILABLE);
}
static void d9_Zero(RcnCtx *ctx) { RET(0); }

const RcnShim rcn_dxvk_d3d9_exports[] = {
    {"Direct3DCreate9", d9_Direct3DCreate9, S32(1)},
    {"Direct3DCreate9Ex", d9_Direct3DCreate9Ex, S32(2)},
    {"D3DPERF_BeginEvent", d9_Zero, S32(2)},
    {"D3DPERF_EndEvent", d9_Zero, S32(0)},
    {"D3DPERF_SetMarker", d9_Zero, S32(2)},
    {"D3DPERF_SetRegion", d9_Zero, S32(2)},
    {"D3DPERF_QueryRepeatFrame", d9_Zero, S32(0)},
    {"D3DPERF_SetOptions", d9_Zero, S32(1)},
    {"D3DPERF_GetStatus", d9_Zero, S32(0)},
};
const size_t rcn_dxvk_d3d9_exports_count = sizeof rcn_dxvk_d3d9_exports / sizeof rcn_dxvk_d3d9_exports[0];
#endif
