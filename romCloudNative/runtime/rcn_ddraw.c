// RomCloud Native : DirectDraw 7 et le texte de gdi32, pour les jeux 32 bits en rendu logiciel.
//
// Une surface est un tableau de pixels dans la mémoire du jeu (16 bits RGB565 ou 32 bits XRGB, selon le
// mode d'affichage demandé). Les copies entre surfaces (Blt, BltFast) sont faites par l'hôte ; la
// surface primaire est remise à l'application à chaque Flip, ou quand le jeu y dessine directement.
// Le texte (TextOutA sur le contexte d'une surface) est tracé avec une police fixe intégrée de 8 × 8
// points, agrandie à la hauteur de la police demandée.
//
// Pas fournis : modes à palette (8 bits), surfaces 3D et textures, recouvrements (overlays).
#include <stdlib.h>

#include "rcn_gl.h"
#include "rcn_gui.h"

enum {
    DD_OK = 0,
    DDERR_GENERIC = 0x80004005u,
    DDERR_INVALIDPARAMS = 0x80070057u,
    DDERR_UNSUPPORTED = 0x80004001u,
    DDERR_NOTFOUND = 0x887600FFu,
    E_NOINTERFACE = 0x80004002u,
    DDSD_CAPS = 0x1,
    DDSD_HEIGHT = 0x2,
    DDSD_WIDTH = 0x4,
    DDSD_PITCH = 0x8,
    DDSD_BACKBUFFERCOUNT = 0x20,
    DDSD_LPSURFACE = 0x800,
    DDSD_PIXELFORMAT = 0x1000,
    DDSD_REFRESHRATE = 0x40000,
    DDSCAPS_BACKBUFFER = 0x4,
    DDSCAPS_FLIP = 0x10,
    DDSCAPS_FRONTBUFFER = 0x20,
    DDSCAPS_PRIMARYSURFACE = 0x200,
    DDPF_RGB = 0x40,
    DDBLT_COLORFILL = 0x400,
    DDBLT_DDFX = 0x800,
    DDBLT_KEYSRC = 0x8000,
    DDBLT_KEYSRCOVERRIDE = 0x10000,
    DDBLTFX_MIRRORLEFTRIGHT = 0x2,
    DDBLTFX_MIRRORUPDOWN = 0x4,
    DDBLTFAST_SRCCOLORKEY = 0x1,
    DDCKEY_SRCBLT = 0x8,
    // DDSURFACEDESC2 (124 octets)
    DESC_SIZE = 124,
    DESC_FLAGS = 4,
    DESC_HEIGHT = 8,
    DESC_WIDTH = 12,
    DESC_PITCH = 16,
    DESC_BACKBUFFERS = 20,
    DESC_REFRESH = 24,
    DESC_SURFACE = 36,
    DESC_FORMAT = 72,
    DESC_CAPS = 104,
};

#define U32(n) ((u32)rcn_arg(ctx, n))

typedef RcnSurface Surface;

// Surfaces vivantes : un HDC rendu par GetDC est l'adresse de sa surface.
static u32 surfaces[256];
static pthread_mutex_t surfaces_lock = PTHREAD_MUTEX_INITIALIZER;

static void track(u32 object, int add) {
    pthread_mutex_lock(&surfaces_lock);
    for (int i = 0; i < 256; i++) {
        if (surfaces[i] == (add ? 0 : object)) {
            surfaces[i] = add ? object : 0;
            break;
        }
    }
    pthread_mutex_unlock(&surfaces_lock);
}
static Surface *surface_of_dc(u32 dc) {
    Surface *s = NULL;
    pthread_mutex_lock(&surfaces_lock);
    for (int i = 0; dc && i < 256 && !s; i++) {
        if (surfaces[i] == dc) s = COM_DATA(Surface, dc);
    }
    pthread_mutex_unlock(&surfaces_lock);
    return s;
}

static u8 *pixels_of(Surface *s) { return PTR(s->pixels); }

// La surface primaire a changé : l'image dessinée par le GPU, s'il y en a une (le jeu vient de la
// recopier de sa surface de rendu, par Blt ou Flip), sinon celle du rendu logiciel.
static void show(RcnCtx *ctx, Surface *s, int wait) {
    if (s->primary && !rcn_gl_present(ctx, wait)) rcn_present(ctx, pixels_of(s), s->width, s->height, s->pitch, s->bpp, wait);
}

// DDPIXELFORMAT (32 octets) : drapeaux, nombre de bits, masques du rouge, du vert, du bleu, de l'opacité.
void rcn_write_pixel_format(u64 at, u32 bpp, const u32 masks[4]) {
    memset(PTR(at), 0, 32);
    wr32(at, 32);
    wr32(at + 4, DDPF_RGB | (masks[3] ? 1 : 0)); // DDPF_ALPHAPIXELS
    wr32(at + 12, bpp);
    for (int i = 0; i < 4; i++) wr32(at + 16 + 4 * i, masks[i]);
}
// Masques du mode d'affichage : RGB565 en 16 bits, XRGB en 32 bits.
static void display_masks(u32 bpp, u32 masks[4]) {
    masks[0] = bpp == 16 ? 0xF800 : 0xFF0000;
    masks[1] = bpp == 16 ? 0x07E0 : 0x00FF00;
    masks[2] = bpp == 16 ? 0x001F : 0x0000FF;
    masks[3] = 0;
}
static void write_format(u64 at, u32 bpp) {
    u32 masks[4];
    display_masks(bpp, masks);
    rcn_write_pixel_format(at, bpp, masks);
}
static void write_desc(u64 desc, Surface *s) {
    memset(PTR(desc), 0, DESC_SIZE);
    wr32(desc, DESC_SIZE);
    wr32(desc + DESC_FLAGS, DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT);
    wr32(desc + DESC_HEIGHT, s->height);
    wr32(desc + DESC_WIDTH, s->width);
    wr32(desc + DESC_PITCH, s->pitch);
    rcn_write_pixel_format(desc + DESC_FORMAT, s->bpp, s->masks);
    wr32(desc + DESC_CAPS, s->caps);
}

// --- Copies de pixels ---

typedef struct {
    i32 left, top, right, bottom;
} Rect;

static Rect rect_arg(u64 at, Surface *s) {
    Rect r = {0, 0, (i32)s->width, (i32)s->height};
    if (at) {
        r.left = (i32)rd32(at);
        r.top = (i32)rd32(at + 4);
        r.right = (i32)rd32(at + 8);
        r.bottom = (i32)rd32(at + 12);
    }
    return r;
}

static void fill(Surface *s, Rect r, u32 color) {
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > (i32)s->width) r.right = (i32)s->width;
    if (r.bottom > (i32)s->height) r.bottom = (i32)s->height;
    for (i32 y = r.top; y < r.bottom; y++) {
        u8 *line = pixels_of(s) + (size_t)y * s->pitch;
        if (s->bpp == 16) {
            u16 *p = (u16 *)line;
            for (i32 x = r.left; x < r.right; x++) p[x] = (u16)color;
        } else {
            u32 *p = (u32 *)line;
            for (i32 x = r.left; x < r.right; x++) p[x] = color;
        }
    }
}

static u32 get_pixel(Surface *s, i32 x, i32 y) {
    const u8 *line = pixels_of(s) + (size_t)y * s->pitch;
    return s->bpp == 16 ? ((const u16 *)line)[x] : ((const u32 *)line)[x];
}
static void put_pixel(Surface *s, i32 x, i32 y, u32 color) {
    u8 *line = pixels_of(s) + (size_t)y * s->pitch;
    if (s->bpp == 16) ((u16 *)line)[x] = (u16)color;
    else ((u32 *)line)[x] = color;
}

// Copie src[sr] vers dst[dr], avec agrandissement si les tailles diffèrent, couleur transparente et
// miroirs éventuels. Les rectangles sont rognés aux surfaces.
static void blit(Surface *dst, Rect dr, Surface *src, Rect sr, int keyed, u32 key, int mirror_x, int mirror_y) {
    i32 dw = dr.right - dr.left, dh = dr.bottom - dr.top;
    i32 sw = sr.right - sr.left, sh = sr.bottom - sr.top;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || dst->bpp != src->bpp) return;
    u32 mask = dst->bpp == 16 ? 0xFFFF : 0x00FFFFFF;
    key &= mask;
    // Cas courant : même taille, sans miroir ni transparence, entièrement dans les surfaces.
    int inside = dr.left >= 0 && dr.top >= 0 && dr.right <= (i32)dst->width && dr.bottom <= (i32)dst->height &&
                 sr.left >= 0 && sr.top >= 0 && sr.right <= (i32)src->width && sr.bottom <= (i32)src->height;
    if (inside && dw == sw && dh == sh && !keyed && !mirror_x && !mirror_y) {
        size_t bytes = (size_t)dw * (dst->bpp / 8);
        for (i32 y = 0; y < dh; y++) {
            memmove(pixels_of(dst) + (size_t)(dr.top + y) * dst->pitch + (size_t)dr.left * (dst->bpp / 8),
                    pixels_of(src) + (size_t)(sr.top + y) * src->pitch + (size_t)sr.left * (src->bpp / 8), bytes);
        }
        return;
    }
    for (i32 y = 0; y < dh; y++) {
        i32 ty = dr.top + y;
        if (ty < 0 || ty >= (i32)dst->height) continue;
        i32 fy = sr.top + (mirror_y ? dh - 1 - y : y) * sh / dh;
        if (fy < 0 || fy >= (i32)src->height) continue;
        for (i32 x = 0; x < dw; x++) {
            i32 tx = dr.left + x;
            if (tx < 0 || tx >= (i32)dst->width) continue;
            i32 fx = sr.left + (mirror_x ? dw - 1 - x : x) * sw / dw;
            if (fx < 0 || fx >= (i32)src->width) continue;
            u32 color = get_pixel(src, fx, fy);
            if (keyed && (color & mask) == key) continue;
            put_pixel(dst, tx, ty, color);
        }
    }
}

// --- IDirectDrawSurface7 ---

static u64 new_surface(RcnCtx *ctx, u32 width, u32 height, u32 bpp, u32 caps);

static void s_QueryInterface(RcnCtx *ctx) {
    COM_REFS(A0)++;
    wr32(A2, U32(0));
    RET(DD_OK);
}
// QueryInterface de DirectDraw : IDirect3D7 {F5049E77-…} donne l'objet Direct3D, le reste l'objet lui-même.
static void d_QueryInterface(RcnCtx *ctx) {
    if (A1 && rd32(A1) == 0xF5049E77u) {
        u64 d3d = rcn_d3d_new(ctx);
        wr32(A2, (u32)d3d);
        RET(d3d ? DD_OK : E_NOINTERFACE);
        return;
    }
    s_QueryInterface(ctx);
}
static void s_AddRef(RcnCtx *ctx) { RET(++COM_REFS(A0)); }
static void s_Release(RcnCtx *ctx) {
    u32 object = U32(0);
    u32 refs = --COM_REFS(object);
    if (refs == 0) {
        Surface *s = COM_DATA(Surface, object);
        track(object, 0);
        rcn_gl_forget(object);
        rcn_gfree(ctx, pixels_of(s));
        rcn_gfree(ctx, PTR(object));
    }
    RET(refs);
}
// Blt(this, &dst_rect, src, &src_rect, flags, &fx)
static void s_Blt(RcnCtx *ctx) {
    Surface *dst = COM_DATA(Surface, A0);
    u32 flags = U32(4);
    u64 fx = rcn_arg(ctx, 5);
    Rect dr = rect_arg(A1, dst);
    if (flags & DDBLT_COLORFILL) {
        fill(dst, dr, fx ? rd32(fx + 80) : 0); // DDBLTFX.dwFillColor
    } else if (A2) {
        Surface *src = COM_DATA(Surface, A2);
        int keyed = 0;
        u32 key = 0;
        if (flags & DDBLT_KEYSRCOVERRIDE) {
            keyed = 1;
            key = fx ? rd32(fx + 92) : 0; // DDBLTFX.ddckSrcColorkey
        } else if (flags & DDBLT_KEYSRC) {
            keyed = (int)src->has_key;
            key = src->key;
        }
        u32 effects = (flags & DDBLT_DDFX) && fx ? rd32(fx + 4) : 0;
        blit(dst, dr, src, rect_arg(A3, src), keyed, key, (effects & DDBLTFX_MIRRORLEFTRIGHT) != 0,
             (effects & DDBLTFX_MIRRORUPDOWN) != 0);
    }
    dst->version++;
    show(ctx, dst, 0);
    RET(DD_OK);
}
// BltFast(this, x, y, src, &src_rect, flags)
static void s_BltFast(RcnCtx *ctx) {
    Surface *dst = COM_DATA(Surface, A0);
    Surface *src = COM_DATA(Surface, A3);
    Rect sr = rect_arg(rcn_arg(ctx, 4), src);
    i32 x = (i32)U32(1), y = (i32)U32(2);
    Rect dr = {x, y, x + (sr.right - sr.left), y + (sr.bottom - sr.top)};
    blit(dst, dr, src, sr, (U32(5) & DDBLTFAST_SRCCOLORKEY) && src->has_key, src->key, 0, 0);
    dst->version++;
    show(ctx, dst, 0);
    RET(DD_OK);
}
// Flip(this, target, flags) : la surface arrière devient l'image affichée.
static void s_Flip(RcnCtx *ctx) {
    Surface *front = COM_DATA(Surface, A0);
    if (!front->back) {
        RET(DDERR_GENERIC);
        return;
    }
    Surface *back = COM_DATA(Surface, front->back);
    u32 pixels = front->pixels;
    front->pixels = back->pixels;
    back->pixels = pixels;
    show(ctx, front, 1);
    RET(DD_OK);
}
// GetAttachedSurface(this, &caps, &surface) : la surface arrière de la primaire.
static void s_GetAttachedSurface(RcnCtx *ctx) {
    Surface *s = COM_DATA(Surface, A0);
    if (!s->back) {
        RET(DDERR_NOTFOUND);
        return;
    }
    COM_REFS(s->back)++;
    wr32(A2, s->back);
    RET(DD_OK);
}
static void s_Ok(RcnCtx *ctx) { RET(DD_OK); }
static void s_GetCaps(RcnCtx *ctx) {
    memset(PTR(A1), 0, 16);
    wr32(A1, COM_DATA(Surface, A0)->caps);
    RET(DD_OK);
}
// GetColorKey / SetColorKey(this, flags, &key) : DDCOLORKEY { bas, haut }.
static void s_GetColorKey(RcnCtx *ctx) {
    Surface *s = COM_DATA(Surface, A0);
    wr32(A2, s->key);
    wr32(A2 + 4, s->key);
    RET(s->has_key ? DD_OK : DDERR_GENERIC);
}
static void s_SetColorKey(RcnCtx *ctx) {
    Surface *s = COM_DATA(Surface, A0);
    if (U32(1) & DDCKEY_SRCBLT) {
        s->has_key = A2 != 0;
        s->key = A2 ? rd32(A2) : 0;
    }
    RET(DD_OK);
}
// GetDC(this, &dc) : le contexte de dessin est l'adresse de la surface.
static void s_GetDC(RcnCtx *ctx) {
    Surface *s = COM_DATA(Surface, A0);
    s->text_color = 0;
    s->font_height = 16;
    s->opaque = 1;
    s->back_color = 0xFFFFFF;
    wr32(A1, U32(0));
    RET(DD_OK);
}
static void s_ReleaseDC(RcnCtx *ctx) {
    COM_DATA(Surface, A0)->version++;
    show(ctx, COM_DATA(Surface, A0), 0);
    RET(DD_OK);
}
static void s_GetPixelFormat(RcnCtx *ctx) {
    Surface *s = COM_DATA(Surface, A0);
    rcn_write_pixel_format(A1, s->bpp, s->masks);
    RET(DD_OK);
}
static void s_GetSurfaceDesc(RcnCtx *ctx) {
    write_desc(A1, COM_DATA(Surface, A0));
    RET(DD_OK);
}
// Lock(this, &rect, &desc, flags, event) : adresse et pas des pixels du rectangle demandé.
static void s_Lock(RcnCtx *ctx) {
    Surface *s = COM_DATA(Surface, A0);
    u64 desc = A2;
    Rect r = rect_arg(A1, s);
    write_desc(desc, s);
    wr32(desc + DESC_FLAGS, rd32(desc + DESC_FLAGS) | DDSD_LPSURFACE);
    wr32(desc + DESC_SURFACE, s->pixels + (u32)r.top * s->pitch + (u32)r.left * (s->bpp / 8));
    RET(DD_OK);
}
static void s_Unlock(RcnCtx *ctx) {
    COM_DATA(Surface, A0)->version++;
    show(ctx, COM_DATA(Surface, A0), 0);
    RET(DD_OK);
}

#define M(name, fn, args) {#name, fn, args}
static const RcnMethod SURFACE_METHODS[] = {
    M(QueryInterface, s_QueryInterface, 3), M(AddRef, s_AddRef, 1), M(Release, s_Release, 1),
    M(AddAttachedSurface, s_Ok, 2), M(AddOverlayDirtyRect, NULL, 2), M(Blt, s_Blt, 6), M(BltBatch, NULL, 4),
    M(BltFast, s_BltFast, 6), M(DeleteAttachedSurface, NULL, 3), M(EnumAttachedSurfaces, NULL, 3),
    M(EnumOverlayZOrders, NULL, 4), M(Flip, s_Flip, 3), M(GetAttachedSurface, s_GetAttachedSurface, 3),
    M(GetBltStatus, s_Ok, 2), M(GetCaps, s_GetCaps, 2), M(GetClipper, NULL, 2), M(GetColorKey, s_GetColorKey, 3),
    M(GetDC, s_GetDC, 2), M(GetFlipStatus, s_Ok, 2), M(GetOverlayPosition, NULL, 3), M(GetPalette, NULL, 2),
    M(GetPixelFormat, s_GetPixelFormat, 2), M(GetSurfaceDesc, s_GetSurfaceDesc, 2), M(Initialize, NULL, 3),
    M(IsLost, s_Ok, 1), M(Lock, s_Lock, 5), M(ReleaseDC, s_ReleaseDC, 2), M(Restore, s_Ok, 1),
    M(SetClipper, s_Ok, 2), M(SetColorKey, s_SetColorKey, 3), M(SetOverlayPosition, NULL, 3), M(SetPalette, s_Ok, 2),
    M(Unlock, s_Unlock, 2), M(UpdateOverlay, NULL, 6), M(UpdateOverlayDisplay, NULL, 2),
    M(UpdateOverlayZOrder, NULL, 3), M(GetDDInterface, NULL, 2), M(PageLock, s_Ok, 2), M(PageUnlock, s_Ok, 2),
    M(SetSurfaceDesc, NULL, 3), M(SetPrivateData, NULL, 5), M(GetPrivateData, NULL, 4), M(FreePrivateData, NULL, 2),
    M(GetUniquenessValue, NULL, 2), M(ChangeUniquenessValue, NULL, 1), M(SetPriority, NULL, 2),
    M(GetPriority, NULL, 2), M(SetLOD, NULL, 2), M(GetLOD, NULL, 2),
};

static u64 new_surface(RcnCtx *ctx, u32 width, u32 height, u32 bpp, u32 caps) {
    u64 object = rcn_com_new(ctx, "IDirectDrawSurface7", SURFACE_METHODS, sizeof SURFACE_METHODS / sizeof SURFACE_METHODS[0], sizeof(Surface));
    if (!object) return 0;
    Surface *s = COM_DATA(Surface, object);
    s->width = width;
    s->height = height;
    s->bpp = bpp;
    s->pitch = (width * (bpp / 8) + 3) & ~3u;
    s->caps = caps;
    display_masks(bpp, s->masks);
    s->pixels = (u32)(uintptr_t)rcn_galloc(ctx, (size_t)s->pitch * height + 16, 1);
    if (!s->pixels) {
        rcn_gfree(ctx, PTR(object));
        return 0;
    }
    track((u32)object, 1);
    return object;
}

// --- IDirectDrawClipper : accepté, sans effet (la fenêtre couvre l'écran) ---

static void c_AddRef(RcnCtx *ctx) { RET(++COM_REFS(A0)); }
static void c_Release(RcnCtx *ctx) {
    u32 refs = --COM_REFS(A0);
    if (refs == 0) rcn_gfree(ctx, PTR(A0));
    RET(refs);
}
static const RcnMethod CLIPPER_METHODS[] = {
    M(QueryInterface, NULL, 3), M(AddRef, c_AddRef, 1), M(Release, c_Release, 1), M(GetClipList, NULL, 4),
    M(GetHWnd, NULL, 2), M(Initialize, s_Ok, 3), M(IsClipListChanged, NULL, 2), M(SetClipList, s_Ok, 3),
    M(SetHWnd, s_Ok, 3),
};

// --- IDirectDraw7 ---

// CreateClipper(this, flags, &clipper, outer)
static void d_CreateClipper(RcnCtx *ctx) {
    u64 object = rcn_com_new(ctx, "IDirectDrawClipper", CLIPPER_METHODS, sizeof CLIPPER_METHODS / sizeof CLIPPER_METHODS[0], 4);
    wr32(A2, (u32)object);
    RET(object ? DD_OK : DDERR_GENERIC);
}
// CreateSurface(this, &desc, &surface, outer)
static void d_CreateSurface(RcnCtx *ctx) {
    RcnGui *gui = rcn_gui(ctx);
    u64 desc = A1;
    u32 flags = rd32(desc + DESC_FLAGS);
    u32 caps = (flags & DDSD_CAPS) ? rd32(desc + DESC_CAPS) : 0;
    int primary = (caps & DDSCAPS_PRIMARYSURFACE) != 0;
    u32 width = !primary && (flags & DDSD_WIDTH) ? rd32(desc + DESC_WIDTH) : gui->width;
    u32 height = !primary && (flags & DDSD_HEIGHT) ? rd32(desc + DESC_HEIGHT) : gui->height;
    u32 bpp = gui->bpp;
    if (flags & DDSD_PIXELFORMAT) {
        u32 asked = rd32(desc + DESC_FORMAT + 12);
        if (asked != 16 && asked != 32) { // palette, 24 bits : non gérés
            RET(DDERR_UNSUPPORTED);
            return;
        }
        bpp = asked;
    }
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        RET(DDERR_INVALIDPARAMS);
        return;
    }
    u64 object = new_surface(ctx, width, height, bpp, caps);
    if (!object) {
        RET(DDERR_GENERIC);
        return;
    }
    Surface *s = COM_DATA(Surface, object);
    s->primary = (u32)primary;
    // Format demandé (textures : 4444, 1555…) : ses masques remplacent ceux de l'écran.
    if ((flags & DDSD_PIXELFORMAT) && rd32(desc + DESC_FORMAT + 16)) {
        for (int i = 0; i < 4; i++) s->masks[i] = rd32(desc + DESC_FORMAT + 16 + 4 * i);
        if (!(rd32(desc + DESC_FORMAT + 4) & 1)) s->masks[3] = 0; // sans DDPF_ALPHAPIXELS : pas d'opacité
    }
    // Chaîne d'échange : une surface arrière suffit, quel que soit le nombre demandé.
    if (primary && (flags & DDSD_BACKBUFFERCOUNT) && rd32(desc + DESC_BACKBUFFERS) > 0) {
        s->caps |= DDSCAPS_FRONTBUFFER | DDSCAPS_FLIP;
        s->back = (u32)new_surface(ctx, width, height, bpp, DDSCAPS_BACKBUFFER | DDSCAPS_FLIP);
    }
    wr32(A2, (u32)object);
    RET(DD_OK);
}
// Mode d'affichage : 640 × 480 par défaut, en 16 ou 32 bits.
static void write_mode(RcnCtx *ctx, u64 desc, u32 width, u32 height, u32 bpp) {
    (void)ctx;
    memset(PTR(desc), 0, DESC_SIZE);
    wr32(desc, DESC_SIZE);
    wr32(desc + DESC_FLAGS, DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE);
    wr32(desc + DESC_HEIGHT, height);
    wr32(desc + DESC_WIDTH, width);
    wr32(desc + DESC_PITCH, width * (bpp / 8));
    wr32(desc + DESC_REFRESH, 60);
    write_format(desc + DESC_FORMAT, bpp);
}
// EnumDisplayModes(this, flags, &filter, context, callback) : la fonction du jeu reçoit chaque mode.
static void d_EnumDisplayModes(RcnCtx *ctx) {
    static const u32 modes[][3] = {{640, 480, 16}, {640, 480, 32}, {800, 600, 16}, {800, 600, 32}, {1024, 768, 16}, {1024, 768, 32}};
    u32 context = U32(3), callback = U32(4);
    u64 desc = (u64)(uintptr_t)rcn_galloc(ctx, DESC_SIZE, 1);
    for (size_t i = 0; desc && i < sizeof modes / sizeof modes[0]; i++) {
        write_mode(ctx, desc, modes[i][0], modes[i][1], modes[i][2]);
        u64 args[2] = {desc, context};
        if (rcn_call_guest_n(ctx, callback, 2, args) == 0) break; // DDENUMRET_CANCEL
    }
    rcn_gfree(ctx, PTR(desc));
    RET(DD_OK);
}
// GetCaps(this, &driver, &emulation) : DDCAPS — copies, agrandissement, couleur transparente,
// remplissage, 3D ; et, à partir de DirectX 6, les types de surfaces (décalage 0x16C).
static void d_GetCaps(RcnCtx *ctx) {
    for (int i = 1; i <= 2; i++) {
        u64 caps = rcn_arg(ctx, i);
        if (!caps) continue;
        u32 size = rd32(caps);
        if (size < 16 || size > 1024) continue;
        memset(PTR(caps + 4), 0, size - 4);
        wr32(caps + 4, 0x1 | 0x40 | 0x200 | 0x400000 | 0x4000000); // 3D, BLT, BLTSTRETCH, COLORKEY, BLTCOLORFILL
        if (size >= 0x17C) wr32(caps + 0x16C, 0x4 | 0x10 | 0x40 | 0x200 | 0x1000 | 0x2000 | 0x4000 | 0x20000);
    }
    RET(DD_OK);
}
static void d_GetDisplayMode(RcnCtx *ctx) {
    RcnGui *gui = rcn_gui(ctx);
    write_mode(ctx, A1, gui->width, gui->height, gui->bpp);
    RET(DD_OK);
}
// SetDisplayMode(this, width, height, bpp, refresh, flags)
static void d_SetDisplayMode(RcnCtx *ctx) {
    RcnGui *gui = rcn_gui(ctx);
    u32 bpp = U32(3);
    if (bpp != 16 && bpp != 32) {
        RET(DDERR_UNSUPPORTED);
        return;
    }
    gui->width = U32(1);
    gui->height = U32(2);
    gui->bpp = bpp;
    RET(DD_OK);
}
// WaitForVerticalBlank : la cadence est tenue par Flip.
static void d_WaitForVerticalBlank(RcnCtx *ctx) {
    rcn_sleep_ms(1);
    RET(DD_OK);
}
// GetAvailableVidMem(this, &caps, &total, &free)
static void d_GetAvailableVidMem(RcnCtx *ctx) {
    if (A2) wr32(A2, 64 << 20);
    if (A3) wr32(A3, 64 << 20);
    RET(DD_OK);
}
static void d_GetMonitorFrequency(RcnCtx *ctx) {
    wr32(A1, 60);
    RET(DD_OK);
}
static void d_Release(RcnCtx *ctx) {
    u32 refs = COM_REFS(A0);
    if (refs) COM_REFS(A0) = --refs;
    RET(refs);
}

static const RcnMethod DDRAW_METHODS[] = {
    M(QueryInterface, d_QueryInterface, 3), M(AddRef, s_AddRef, 1), M(Release, d_Release, 1), M(Compact, s_Ok, 1),
    M(CreateClipper, d_CreateClipper, 4), M(CreatePalette, NULL, 5), M(CreateSurface, d_CreateSurface, 4),
    M(DuplicateSurface, NULL, 3), M(EnumDisplayModes, d_EnumDisplayModes, 5), M(EnumSurfaces, NULL, 5),
    M(FlipToGDISurface, s_Ok, 1), M(GetCaps, d_GetCaps, 3), M(GetDisplayMode, d_GetDisplayMode, 2),
    M(GetFourCCCodes, NULL, 3), M(GetGDISurface, NULL, 2), M(GetMonitorFrequency, d_GetMonitorFrequency, 2),
    M(GetScanLine, NULL, 2), M(GetVerticalBlankStatus, NULL, 2), M(Initialize, s_Ok, 2),
    M(RestoreDisplayMode, s_Ok, 1), M(SetCooperativeLevel, s_Ok, 3), M(SetDisplayMode, d_SetDisplayMode, 6),
    M(WaitForVerticalBlank, d_WaitForVerticalBlank, 3), M(GetAvailableVidMem, d_GetAvailableVidMem, 4),
    M(GetSurfaceFromDC, NULL, 3), M(RestoreAllSurfaces, s_Ok, 1), M(TestCooperativeLevel, s_Ok, 1),
    M(GetDeviceIdentifier, NULL, 3), M(StartModeTest, NULL, 4), M(EvaluateMode, NULL, 3),
};

// DirectDrawCreateEx(guid, &ddraw, iid, outer)
static void dd_DirectDrawCreateEx(RcnCtx *ctx) {
    u64 object = rcn_com_new(ctx, "IDirectDraw7", DDRAW_METHODS, sizeof DDRAW_METHODS / sizeof DDRAW_METHODS[0], 4);
    wr32(A1, (u32)object);
    RET(object ? DD_OK : DDERR_GENERIC);
}
// DirectDrawEnumerateExA(callback, context, flags) : un seul écran, celui par défaut.
// La fonction du jeu reçoit (guid, description, nom, contexte, moniteur).
static void dd_DirectDrawEnumerateExA(RcnCtx *ctx) {
    u32 callback = U32(0), context = U32(1);
    char *text = rcn_galloc(ctx, 64, 1);
    if (text) {
        strcpy(text, "Primary Display Driver");
        strcpy(text + 32, "display");
        u64 args[5] = {0, (u64)(uintptr_t)text, (u64)(uintptr_t)text + 32, context, 0};
        rcn_call_guest_n(ctx, callback, 5, args);
        rcn_gfree(ctx, text);
    }
    RET(DD_OK);
}

const RcnShim rcn_ddraw[] = {
    {"DirectDrawCreateEx", dd_DirectDrawCreateEx, S32(4)},
    {"DirectDrawEnumerateExA", dd_DirectDrawEnumerateExA, S32(3)},
};
const size_t rcn_ddraw_count = sizeof rcn_ddraw / sizeof rcn_ddraw[0];

// --- gdi32 : texte sur une surface ---

// Police fixe de 8 × 8 points, caractères 0x20 à 0x7F ; bit de poids faible : point de gauche.
// (font8x8_basic de Daniel Hepper, domaine public.)
static const u8 FONT[96][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},
    {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00}, {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00},
    {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00}, {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

enum {
    HANDLE_FONT = 0x00050000u, // CreateFontA : HANDLE_FONT | hauteur
    HANDLE_STOCK = 0x00060000u,
    TRANSPARENT = 1,
};

// Couleur de gdi32 (0x00BBVVRR) vers un pixel de la surface.
static u32 color_of(Surface *s, u32 colorref) {
    u32 r = colorref & 0xFF, g = (colorref >> 8) & 0xFF, b = (colorref >> 16) & 0xFF;
    return s->bpp == 16 ? ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) : (r << 16) | (g << 8) | b;
}
static u32 char_width(Surface *s) { return (s->font_height + 1) / 2; }

// TextOutA(dc, x, y, text, length)
static void gdi_TextOutA(RcnCtx *ctx) {
    Surface *s = surface_of_dc(U32(0));
    if (!s) { // contexte de la fenêtre : rien à l'écran
        RET(1);
        return;
    }
    i32 x0 = (i32)U32(1), y0 = (i32)U32(2);
    const u8 *text = PTR(A3);
    u32 length = U32(4);
    u32 h = s->font_height, w = char_width(s);
    u32 fore = color_of(s, s->text_color), back = color_of(s, s->back_color);
    for (u32 i = 0; i < length; i++) {
        u8 c = text[i];
        const u8 *glyph = FONT[c >= 0x20 && c < 0x80 ? c - 0x20 : '?' - 0x20];
        for (u32 y = 0; y < h; y++) {
            i32 py = y0 + (i32)y;
            if (py < 0 || py >= (i32)s->height) continue;
            u8 bits = glyph[y * 8 / h];
            for (u32 x = 0; x < w; x++) {
                i32 px = x0 + (i32)(i * w + x);
                if (px < 0 || px >= (i32)s->width) continue;
                if (bits >> (x * 8 / w) & 1) put_pixel(s, px, py, fore);
                else if (s->opaque) put_pixel(s, px, py, back);
            }
        }
    }
    RET(1);
}
// GetTextExtentPoint32A(dc, text, length, &size)
static void gdi_GetTextExtentPoint32A(RcnCtx *ctx) {
    Surface *s = surface_of_dc(U32(0));
    u32 h = s ? s->font_height : 16;
    wr32(A3, U32(2) * ((h + 1) / 2));
    wr32(A3 + 4, h);
    RET(1);
}
// CreateFontA(height, …) : seule la hauteur compte (police fixe).
static void gdi_CreateFontA(RcnCtx *ctx) {
    i32 height = (i32)U32(0);
    if (height < 0) height = -height;
    if (height == 0) height = 16;
    if (height > 255) height = 255;
    RET(HANDLE_FONT | (u32)height);
}
// SelectObject(dc, object) : choisir une police fixe la hauteur du texte.
static void gdi_SelectObject(RcnCtx *ctx) {
    Surface *s = surface_of_dc(U32(0));
    u32 object = U32(1);
    u32 previous = HANDLE_STOCK;
    if (s && (object & 0xFFFF0000u) == HANDLE_FONT) {
        previous = HANDLE_FONT | s->font_height;
        s->font_height = object & 0xFF;
    }
    RET(previous);
}
static void gdi_SetTextColor(RcnCtx *ctx) {
    Surface *s = surface_of_dc(U32(0));
    u32 previous = s ? s->text_color : 0;
    if (s) s->text_color = U32(1);
    RET(previous);
}
static void gdi_SetBkMode(RcnCtx *ctx) {
    Surface *s = surface_of_dc(U32(0));
    u32 previous = s && !s->opaque ? TRANSPARENT : 2;
    if (s) s->opaque = U32(1) != TRANSPARENT;
    RET(previous);
}
static void gdi_GetStockObject(RcnCtx *ctx) { RET(HANDLE_STOCK | (U32(0) & 0xFF)); }
static void gdi_DeleteObject(RcnCtx *ctx) { RET(1); }
// GetDeviceCaps(dc, index) : taille et profondeur de l'écran.
static void gdi_GetDeviceCaps(RcnCtx *ctx) {
    RcnGui *gui = rcn_gui(ctx);
    switch (U32(1)) {
    case 8: RET(gui->width); break;   // HORZRES
    case 10: RET(gui->height); break; // VERTRES
    case 12: RET(gui->bpp); break;    // BITSPIXEL
    case 14: RET(1); break;           // PLANES
    case 88:                          // LOGPIXELSX
    case 90: RET(96); break;          // LOGPIXELSY
    case 116: RET(60); break;         // VREFRESH
    default: RET(0); break;
    }
}

const RcnShim rcn_gdi32[] = {
    {"CreateFontA", gdi_CreateFontA, S32(14)},
    {"DeleteObject", gdi_DeleteObject, S32(1)},
    {"GetDeviceCaps", gdi_GetDeviceCaps, S32(2)},
    {"GetStockObject", gdi_GetStockObject, S32(1)},
    {"GetTextExtentPoint32A", gdi_GetTextExtentPoint32A, S32(4)},
    {"SelectObject", gdi_SelectObject, S32(2)},
    {"SetBkMode", gdi_SetBkMode, S32(2)},
    {"SetTextColor", gdi_SetTextColor, S32(2)},
    {"TextOutA", gdi_TextOutA, S32(5)},
};
const size_t rcn_gdi32_count = sizeof rcn_gdi32 / sizeof rcn_gdi32[0];
