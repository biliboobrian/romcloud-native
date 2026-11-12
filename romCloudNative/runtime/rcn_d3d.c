// RomCloud Native : Direct3D 7 (mode immédiat) en rendu logiciel, pour les jeux 32 bits.
//
// Le jeu envoie des triangles, des lignes ou des points (DrawPrimitive) dont les sommets sont déjà en
// coordonnées d'écran (XYZRHW) ou à transformer par les matrices monde, vue et projection. Chaque
// triangle est rempli point par point dans la surface cible : couleur interpolée entre les sommets,
// texture de l'étage 0, test d'opacité, mélange avec l'image selon les facteurs choisis.
//
// Pas fournis : tampon de profondeur (les primitives sont dessinées dans l'ordre reçu), éclairage,
// brouillard, plusieurs textures à la fois, filtrage des textures (point le plus proche), tampons de
// sommets, découpage des triangles qui traversent le plan proche.
#include <stdlib.h>

#include "rcn_gui.h"

enum {
    D3D_OK = 0,
    D3DERR_GENERIC = 0x80004005u,
    // Types de primitives.
    PT_POINTLIST = 1,
    PT_LINELIST = 2,
    PT_LINESTRIP = 3,
    PT_TRIANGLELIST = 4,
    PT_TRIANGLESTRIP = 5,
    PT_TRIANGLEFAN = 6,
    // Format des sommets.
    FVF_XYZ = 0x002,
    FVF_XYZRHW = 0x004,
    FVF_NORMAL = 0x010,
    FVF_DIFFUSE = 0x040,
    FVF_SPECULAR = 0x080,
    // États de rendu retenus.
    RS_ALPHATESTENABLE = 15,
    RS_SRCBLEND = 19,
    RS_DESTBLEND = 20,
    RS_CULLMODE = 22,
    RS_ALPHAREF = 24,
    RS_ALPHAFUNC = 25,
    RS_ALPHABLENDENABLE = 27,
    RS_COLORKEYENABLE = 41,
    RS_TEXTUREFACTOR = 60,
    RS_COUNT = 256,
    // États de l'étage de texture 0.
    TSS_COLOROP = 1,
    TSS_COLORARG1 = 2,
    TSS_COLORARG2 = 3,
    TSS_ALPHAOP = 4,
    TSS_ALPHAARG1 = 5,
    TSS_ALPHAARG2 = 6,
    TSS_ADDRESS = 12,
    TSS_ADDRESSU = 13,
    TSS_ADDRESSV = 14,
    TSS_COUNT = 32,
    TOP_DISABLE = 1,
    TOP_SELECTARG1 = 2,
    TOP_SELECTARG2 = 3,
    TOP_MODULATE = 4,
    TOP_MODULATE2X = 5,
    TOP_MODULATE4X = 6,
    TOP_ADD = 7,
    TA_DIFFUSE = 0,
    TA_CURRENT = 1,
    TA_TEXTURE = 2,
    TA_TFACTOR = 3,
    CLEAR_TARGET = 1,
};

#define U32(n) ((u32)rcn_arg(ctx, n))

typedef struct {
    u32 target;  // surface cible (objet COM)
    u32 texture; // surface de l'étage 0, ou 0
    u32 viewport[4]; // x, y, largeur, hauteur
    u32 render_state[RS_COUNT];
    u32 stage_state[TSS_COUNT];
    float world[16], view[16], projection[16];
} Device;

// --- Pixels ---

// Position et largeur des composantes d'un format, déduites de ses masques.
typedef struct {
    u8 shift[4], bits[4]; // rouge, vert, bleu, opacité
    u32 bpp;
} Layout;

static Layout layout_of(const RcnSurface *s) {
    Layout l;
    l.bpp = s->bpp;
    for (int i = 0; i < 4; i++) {
        u32 mask = s->masks[i];
        l.shift[i] = 0;
        l.bits[i] = 0;
        if (!mask) continue;
        while (!(mask & 1)) {
            mask >>= 1;
            l.shift[i]++;
        }
        while (mask & 1) {
            mask >>= 1;
            l.bits[i]++;
        }
    }
    return l;
}
// Composante de `bits` bits étendue à 8 bits.
static inline u32 expand(u32 value, u32 bits) {
    if (bits >= 8) return value >> (bits - 8);
    if (bits == 0) return 255;
    value <<= 8 - bits;
    return value | (value >> bits);
}
static inline u32 load_pixel(const u8 *p, u32 bpp) { return bpp == 16 ? *(const u16 *)p : *(const u32 *)p; }
static inline void store_pixel(u8 *p, u32 bpp, u32 v) {
    if (bpp == 16) *(u16 *)p = (u16)v;
    else *(u32 *)p = v;
}
// Pixel → composantes de 8 bits (r, g, b, a).
static inline void unpack(const Layout *l, u32 pixel, u32 c[4]) {
    for (int i = 0; i < 4; i++) c[i] = l->bits[i] ? expand((pixel >> l->shift[i]) & ((1u << l->bits[i]) - 1), l->bits[i]) : 255;
}
static inline u32 pack(const Layout *l, const u32 c[4]) {
    u32 pixel = 0;
    for (int i = 0; i < 4; i++) {
        if (l->bits[i]) pixel |= (c[i] >> (8 - (l->bits[i] > 8 ? 8 : l->bits[i]))) << l->shift[i];
    }
    return pixel;
}
// Couleur Direct3D (0xAARRGGBB) → pixel de la surface.
static u32 pixel_of_color(const Layout *l, u32 color) {
    u32 c[4] = {(color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, color >> 24};
    return pack(l, c);
}

// --- Dessin ---

typedef struct {
    float x, y;
    float c[4]; // r, g, b, a : 0 à 255
    float u, v;
} Vertex;

// Tout ce qu'il faut pour remplir un point, préparé une fois par appel de dessin.
typedef struct {
    RcnSurface *target, *texture;
    Layout target_layout, texture_layout;
    i32 clip[4]; // gauche, haut, droite, bas
    u32 blend, src_factor, dst_factor;
    u32 alpha_test, alpha_ref, alpha_func;
    u32 color_op, color_arg1, color_arg2, alpha_op, alpha_arg1, alpha_arg2;
    u32 color_key, key, key_mask;
    u32 clamp_u, clamp_v;
    u32 factor[4];
    u32 cull;
} Draw;

static inline void blend_factor(u32 factor, const u32 src[4], const u32 dst[4], u32 out[4]) {
    switch (factor) {
    case 1: out[0] = out[1] = out[2] = out[3] = 0; break;                 // ZERO
    case 3: for (int i = 0; i < 4; i++) out[i] = src[i]; break;          // SRCCOLOR
    case 4: for (int i = 0; i < 4; i++) out[i] = 255 - src[i]; break;    // INVSRCCOLOR
    case 5: out[0] = out[1] = out[2] = out[3] = src[3]; break;            // SRCALPHA
    case 6: out[0] = out[1] = out[2] = out[3] = 255 - src[3]; break;      // INVSRCALPHA
    case 7: out[0] = out[1] = out[2] = out[3] = dst[3]; break;            // DESTALPHA
    case 8: out[0] = out[1] = out[2] = out[3] = 255 - dst[3]; break;      // INVDESTALPHA
    case 9: for (int i = 0; i < 4; i++) out[i] = dst[i]; break;          // DESTCOLOR
    case 10: for (int i = 0; i < 4; i++) out[i] = 255 - dst[i]; break;   // INVDESTCOLOR
    default: out[0] = out[1] = out[2] = out[3] = 255; break;              // ONE
    }
}

static inline u32 select_arg(u32 arg, int i, const u32 diffuse[4], const u32 texel[4], const u32 factor[4]) {
    switch (arg & 0xF) {
    case TA_TEXTURE: return texel[i];
    case TA_TFACTOR: return factor[i];
    default: return diffuse[i]; // DIFFUSE, et CURRENT à l'étage 0
    }
}
static inline u32 combine(u32 op, u32 a, u32 b) {
    u32 v;
    switch (op) {
    case TOP_SELECTARG2: return b;
    case TOP_MODULATE: return a * b / 255;
    case TOP_MODULATE2X: v = a * b * 2 / 255; break;
    case TOP_MODULATE4X: v = a * b * 4 / 255; break;
    case TOP_ADD: v = a + b; break;
    default: return a; // SELECTARG1
    }
    return v > 255 ? 255 : v;
}

// Couleur d'un point (diffuse interpolée, coordonnées de texture) → écrit dans la cible.
static inline void shade(const Draw *d, i32 x, i32 y, const u32 diffuse[4], float u, float v) {
    u32 texel[4] = {255, 255, 255, 255};
    if (d->texture) {
        const RcnSurface *t = d->texture;
        i32 tx = (i32)floorf(u * (float)t->width), ty = (i32)floorf(v * (float)t->height);
        if (d->clamp_u) tx = tx < 0 ? 0 : tx >= (i32)t->width ? (i32)t->width - 1 : tx;
        else tx = ((tx % (i32)t->width) + (i32)t->width) % (i32)t->width;
        if (d->clamp_v) ty = ty < 0 ? 0 : ty >= (i32)t->height ? (i32)t->height - 1 : ty;
        else ty = ((ty % (i32)t->height) + (i32)t->height) % (i32)t->height;
        u32 raw = load_pixel((const u8 *)PTR(t->pixels) + (size_t)ty * t->pitch + (size_t)tx * (t->bpp / 8), t->bpp);
        if (d->color_key && (raw & d->key_mask) == d->key) return;
        unpack(&d->texture_layout, raw, texel);
    }
    u32 src[4];
    if (d->color_op == TOP_DISABLE) {
        for (int i = 0; i < 4; i++) src[i] = diffuse[i];
    } else {
        for (int i = 0; i < 3; i++) {
            src[i] = combine(d->color_op, select_arg(d->color_arg1, i, diffuse, texel, d->factor),
                             select_arg(d->color_arg2, i, diffuse, texel, d->factor));
        }
        src[3] = d->alpha_op == TOP_DISABLE
                     ? diffuse[3]
                     : combine(d->alpha_op, select_arg(d->alpha_arg1, 3, diffuse, texel, d->factor),
                               select_arg(d->alpha_arg2, 3, diffuse, texel, d->factor));
    }
    if (d->alpha_test) {
        u32 a = src[3], ref = d->alpha_ref;
        int pass;
        switch (d->alpha_func) {
        case 1: pass = 0; break;         // NEVER
        case 2: pass = a < ref; break;   // LESS
        case 3: pass = a == ref; break;  // EQUAL
        case 4: pass = a <= ref; break;  // LESSEQUAL
        case 5: pass = a > ref; break;   // GREATER
        case 6: pass = a != ref; break;  // NOTEQUAL
        case 7: pass = a >= ref; break;  // GREATEREQUAL
        default: pass = 1; break;        // ALWAYS
        }
        if (!pass) return;
    }
    u8 *p = (u8 *)PTR(d->target->pixels) + (size_t)y * d->target->pitch + (size_t)x * (d->target->bpp / 8);
    if (d->blend) {
        u32 dst[4], fs[4], fd[4];
        unpack(&d->target_layout, load_pixel(p, d->target->bpp), dst);
        blend_factor(d->src_factor, src, dst, fs);
        blend_factor(d->dst_factor, src, dst, fd);
        for (int i = 0; i < 4; i++) {
            u32 value = (src[i] * fs[i] + dst[i] * fd[i]) / 255;
            src[i] = value > 255 ? 255 : value;
        }
    }
    store_pixel(p, d->target->bpp, pack(&d->target_layout, src));
}

static inline float edge(const Vertex *a, const Vertex *b, float x, float y) {
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}
// Un point exactement sur une arête n'appartient qu'à l'un des deux triangles qui la partagent :
// celui dont c'est une arête du haut ou de gauche (sinon les mélanges seraient appliqués deux fois).
static inline int top_left(const Vertex *a, const Vertex *b) {
    float dy = b->y - a->y;
    return dy < 0 || (dy == 0 && b->x - a->x > 0);
}

static void triangle(const Draw *d, const Vertex *a, const Vertex *b, const Vertex *c) {
    float area = edge(a, b, c->x, c->y);
    if (area == 0 || area != area) return;
    // Sens des sommets à l'écran : aire positive = sens horaire. D3DCULL_CW (2) écarte le sens horaire,
    // D3DCULL_CCW (3) l'autre.
    if ((d->cull == 2 && area > 0) || (d->cull == 3 && area < 0)) return;
    if (area < 0) {
        const Vertex *t = b;
        b = c;
        c = t;
        area = -area;
    }
    float min_x = fminf(a->x, fminf(b->x, c->x)), max_x = fmaxf(a->x, fmaxf(b->x, c->x));
    float min_y = fminf(a->y, fminf(b->y, c->y)), max_y = fmaxf(a->y, fmaxf(b->y, c->y));
    if (!(max_x >= (float)d->clip[0] && min_x <= (float)d->clip[2] && max_y >= (float)d->clip[1] && min_y <= (float)d->clip[3])) return;
    i32 x0 = (i32)floorf(min_x), x1 = (i32)ceilf(max_x), y0 = (i32)floorf(min_y), y1 = (i32)ceilf(max_y);
    if (x0 < d->clip[0]) x0 = d->clip[0];
    if (y0 < d->clip[1]) y0 = d->clip[1];
    if (x1 > d->clip[2]) x1 = d->clip[2];
    if (y1 > d->clip[3]) y1 = d->clip[3];
    int tl0 = top_left(b, c), tl1 = top_left(c, a), tl2 = top_left(a, b);
    float inv = 1.0f / area;
    for (i32 y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        for (i32 x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            float w0 = edge(b, c, px, py), w1 = edge(c, a, px, py), w2 = edge(a, b, px, py);
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            if ((w0 == 0 && !tl0) || (w1 == 0 && !tl1) || (w2 == 0 && !tl2)) continue;
            w0 *= inv;
            w1 *= inv;
            w2 *= inv;
            u32 diffuse[4];
            for (int i = 0; i < 4; i++) {
                float value = a->c[i] * w0 + b->c[i] * w1 + c->c[i] * w2;
                diffuse[i] = value <= 0 ? 0 : value >= 255 ? 255 : (u32)(value + 0.5f);
            }
            shade(d, x, y, diffuse, a->u * w0 + b->u * w1 + c->u * w2, a->v * w0 + b->v * w1 + c->v * w2);
        }
    }
}

static void line(const Draw *d, const Vertex *a, const Vertex *b) {
    float dx = b->x - a->x, dy = b->y - a->y;
    float length = fmaxf(fabsf(dx), fabsf(dy));
    if (!(length < 100000.0f)) return;
    i32 steps = (i32)length;
    for (i32 i = 0; i <= steps; i++) {
        float t = steps ? (float)i / (float)steps : 0;
        i32 x = (i32)floorf(a->x + dx * t), y = (i32)floorf(a->y + dy * t);
        if (x < d->clip[0] || x >= d->clip[2] || y < d->clip[1] || y >= d->clip[3]) continue;
        u32 diffuse[4];
        for (int k = 0; k < 4; k++) {
            float value = a->c[k] + (b->c[k] - a->c[k]) * t;
            diffuse[k] = value <= 0 ? 0 : value >= 255 ? 255 : (u32)(value + 0.5f);
        }
        shade(d, x, y, diffuse, a->u + (b->u - a->u) * t, a->v + (b->v - a->v) * t);
    }
}

static void point(const Draw *d, const Vertex *a) {
    i32 x = (i32)floorf(a->x), y = (i32)floorf(a->y);
    if (x < d->clip[0] || x >= d->clip[2] || y < d->clip[1] || y >= d->clip[3]) return;
    u32 diffuse[4];
    for (int k = 0; k < 4; k++) diffuse[k] = a->c[k] <= 0 ? 0 : a->c[k] >= 255 ? 255 : (u32)(a->c[k] + 0.5f);
    shade(d, x, y, diffuse, a->u, a->v);
}

// --- Sommets ---

static void multiply(const float a[16], const float b[16], float out[16]) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
        }
    }
}

static u32 vertex_size(u32 fvf) {
    u32 size = (fvf & FVF_XYZRHW) ? 16 : 12;
    if (fvf & FVF_NORMAL) size += 12;
    if (fvf & FVF_DIFFUSE) size += 4;
    if (fvf & FVF_SPECULAR) size += 4;
    return size + ((fvf >> 8) & 0xF) * 8;
}

// Lit un sommet du jeu ; `transform` : produit monde × vue × projection pour les sommets non transformés.
static void fetch(const Device *device, u32 fvf, u64 at, const float transform[16], Vertex *out) {
    float x = rdf32(at), y = rdf32(at + 4), z = rdf32(at + 8);
    u64 p = at + 12;
    if (fvf & FVF_XYZRHW) {
        out->x = x;
        out->y = y;
        p += 4;
    } else {
        // Vecteur ligne × matrice, division par w, puis passage à la fenêtre d'affichage.
        float tx = x * transform[0] + y * transform[4] + z * transform[8] + transform[12];
        float ty = x * transform[1] + y * transform[5] + z * transform[9] + transform[13];
        float tw = x * transform[3] + y * transform[7] + z * transform[11] + transform[15];
        if (tw == 0) tw = 1;
        out->x = (float)device->viewport[0] + (1.0f + tx / tw) * (float)device->viewport[2] * 0.5f;
        out->y = (float)device->viewport[1] + (1.0f - ty / tw) * (float)device->viewport[3] * 0.5f;
    }
    if (fvf & FVF_NORMAL) p += 12;
    u32 color = 0xFFFFFFFFu;
    if (fvf & FVF_DIFFUSE) {
        color = rd32(p);
        p += 4;
    }
    if (fvf & FVF_SPECULAR) p += 4;
    out->c[0] = (float)((color >> 16) & 0xFF);
    out->c[1] = (float)((color >> 8) & 0xFF);
    out->c[2] = (float)(color & 0xFF);
    out->c[3] = (float)(color >> 24);
    out->u = out->v = 0;
    if ((fvf >> 8) & 0xF) {
        out->u = rdf32(p);
        out->v = rdf32(p + 4);
    }
}

// Prépare le dessin d'après l'état du périphérique. Renvoie 0 s'il n'y a pas de cible utilisable.
static int prepare(Device *device, Draw *d) {
    if (!device->target) return 0;
    memset(d, 0, sizeof *d);
    d->target = COM_DATA(RcnSurface, device->target);
    d->target_layout = layout_of(d->target);
    i32 right = (i32)(device->viewport[0] + device->viewport[2]), bottom = (i32)(device->viewport[1] + device->viewport[3]);
    d->clip[0] = (i32)device->viewport[0];
    d->clip[1] = (i32)device->viewport[1];
    d->clip[2] = right > (i32)d->target->width ? (i32)d->target->width : right;
    d->clip[3] = bottom > (i32)d->target->height ? (i32)d->target->height : bottom;
    const u32 *rs = device->render_state, *ts = device->stage_state;
    d->blend = rs[RS_ALPHABLENDENABLE];
    d->src_factor = rs[RS_SRCBLEND];
    d->dst_factor = rs[RS_DESTBLEND];
    d->alpha_test = rs[RS_ALPHATESTENABLE];
    d->alpha_ref = rs[RS_ALPHAREF] & 0xFF;
    d->alpha_func = rs[RS_ALPHAFUNC];
    d->cull = rs[RS_CULLMODE];
    u32 factor = rs[RS_TEXTUREFACTOR];
    d->factor[0] = (factor >> 16) & 0xFF;
    d->factor[1] = (factor >> 8) & 0xFF;
    d->factor[2] = factor & 0xFF;
    d->factor[3] = factor >> 24;
    d->color_op = ts[TSS_COLOROP];
    d->color_arg1 = ts[TSS_COLORARG1];
    d->color_arg2 = ts[TSS_COLORARG2];
    d->alpha_op = ts[TSS_ALPHAOP];
    d->alpha_arg1 = ts[TSS_ALPHAARG1];
    d->alpha_arg2 = ts[TSS_ALPHAARG2];
    d->clamp_u = ts[TSS_ADDRESSU] == 3;
    d->clamp_v = ts[TSS_ADDRESSV] == 3;
    if (device->texture) {
        d->texture = COM_DATA(RcnSurface, device->texture);
        d->texture_layout = layout_of(d->texture);
        d->color_key = rs[RS_COLORKEYENABLE] && d->texture->has_key;
        d->key_mask = d->texture->masks[0] | d->texture->masks[1] | d->texture->masks[2];
        d->key = d->texture->key & d->key_mask;
    }
    return 1;
}

// Dessine `count` sommets lus par `index` (NULL : dans l'ordre).
static void draw(RcnCtx *ctx, Device *device, u32 type, u32 fvf, u64 vertices, u32 count, const u16 *index) {
    Draw d;
    if (!prepare(device, &d) || count == 0 || count > 1000000) return;
    float world_view[16], transform[16];
    multiply(device->world, device->view, world_view);
    multiply(world_view, device->projection, transform);
    u32 size = vertex_size(fvf);
    Vertex *v = malloc(count * sizeof *v);
    if (!v) return;
    for (u32 i = 0; i < count; i++) fetch(device, fvf, vertices + (u64)(index ? index[i] : i) * size, transform, &v[i]);
    switch (type) {
    case PT_POINTLIST:
        for (u32 i = 0; i < count; i++) point(&d, &v[i]);
        break;
    case PT_LINELIST:
        for (u32 i = 0; i + 1 < count; i += 2) line(&d, &v[i], &v[i + 1]);
        break;
    case PT_LINESTRIP:
        for (u32 i = 0; i + 1 < count; i++) line(&d, &v[i], &v[i + 1]);
        break;
    case PT_TRIANGLELIST:
        for (u32 i = 0; i + 2 < count; i += 3) triangle(&d, &v[i], &v[i + 1], &v[i + 2]);
        break;
    case PT_TRIANGLESTRIP:
        // Un triangle sur deux a ses sommets dans l'ordre inverse : on les rétablit pour l'élimination des faces.
        for (u32 i = 0; i + 2 < count; i++) {
            if (i & 1) triangle(&d, &v[i + 1], &v[i], &v[i + 2]);
            else triangle(&d, &v[i], &v[i + 1], &v[i + 2]);
        }
        break;
    case PT_TRIANGLEFAN:
        for (u32 i = 1; i + 1 < count; i++) triangle(&d, &v[0], &v[i], &v[i + 1]);
        break;
    default:
        break;
    }
    free(v);
    (void)ctx;
}

// --- IDirect3DDevice7 ---

static void v_Ok(RcnCtx *ctx) { RET(D3D_OK); }
static void v_AddRef(RcnCtx *ctx) { RET(++COM_REFS(A0)); }
static void v_Release(RcnCtx *ctx) {
    u32 refs = --COM_REFS(A0);
    if (refs == 0) rcn_gfree(ctx, PTR(A0));
    RET(refs);
}
static void v_QueryInterface(RcnCtx *ctx) {
    COM_REFS(A0)++;
    wr32(A2, U32(0));
    RET(D3D_OK);
}

// D3DPRIMCAPS (56 octets) : tout est permis, textures de toutes tailles.
static void write_prim_caps(u64 at) {
    wr32(at, 56);
    for (int i = 1; i < 12; i++) wr32(at + 4 * i, 0xFFFFFFFFu);
    wr32(at + 32, 0x1 | 0x4 | 0x8); // textures : perspective, opacité, transparence — sans contrainte de taille
    wr32(at + 48, 0);
    wr32(at + 52, 0);
}
// D3DDEVICEDESC7 (236 octets).
static void write_device_desc(u64 desc) {
    static const u8 hal[16] = {0xE0, 0x3D, 0xE6, 0x84, 0xAA, 0x46, 0xCF, 0x11, 0x81, 0x6F, 0x00, 0x00, 0xC0, 0x20, 0x15, 0x6E};
    memset(PTR(desc), 0, 236);
    wr32(desc, 0x1 | 0x10 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x2000 | 0x8000 | 0x80000); // sommets déjà transformés, DrawPrimitive, rendu « matériel »
    write_prim_caps(desc + 4);
    write_prim_caps(desc + 60);
    wr32(desc + 116, 0x400 | 0x100); // cibles de 16 et 32 bits
    wr32(desc + 120, 0x400);         // profondeur de 16 bits
    wr32(desc + 124, 1);
    wr32(desc + 128, 1);
    wr32(desc + 132, 2048);
    wr32(desc + 136, 2048);
    wr32(desc + 140, 32768);
    wr32(desc + 144, 2048);
    wr32(desc + 148, 1);
    wr32(desc + 176, 8);           // huit jeux de coordonnées de texture
    wr32(desc + 180, 0xFFFFFFFFu); // opérations de texture
    wr16(desc + 184, 1);
    wr16(desc + 186, 1);
    wrf32(desc + 192, 1.0e10f);
    memcpy(PTR(desc + 196), hal, 16);
}
static void v_GetCaps(RcnCtx *ctx) {
    write_device_desc(A1);
    RET(D3D_OK);
}
// EnumTextureFormats(this, callback, context) : formats de 16 et 32 bits, avec ou sans opacité.
static void v_EnumTextureFormats(RcnCtx *ctx) {
    static const u32 formats[][5] = {
        {16, 0xF800, 0x07E0, 0x001F, 0},          {16, 0x7C00, 0x03E0, 0x001F, 0x8000},
        {16, 0x0F00, 0x00F0, 0x000F, 0xF000},     {32, 0xFF0000, 0x00FF00, 0x0000FF, 0},
        {32, 0xFF0000, 0x00FF00, 0x0000FF, 0xFF000000u},
    };
    u32 callback = U32(1), context = U32(2);
    u64 format = (u64)(uintptr_t)rcn_galloc(ctx, 32, 1);
    for (size_t i = 0; format && i < sizeof formats / sizeof formats[0]; i++) {
        rcn_write_pixel_format(format, formats[i][0], &formats[i][1]);
        u64 args[2] = {format, context};
        if (rcn_call_guest_n(ctx, callback, 2, args) == 0) break;
    }
    rcn_gfree(ctx, PTR(format));
    RET(D3D_OK);
}
// SetRenderTarget(this, surface, flags)
static void v_SetRenderTarget(RcnCtx *ctx) {
    COM_DATA(Device, A0)->target = U32(1);
    RET(D3D_OK);
}
static void v_GetRenderTarget(RcnCtx *ctx) {
    u32 target = COM_DATA(Device, A0)->target;
    if (target) COM_REFS(target)++;
    wr32(A1, target);
    RET(D3D_OK);
}
// Clear(this, count, &rects, flags, color, z, stencil) : rectangles D3DRECT, ou toute la fenêtre d'affichage.
static void v_Clear(RcnCtx *ctx) {
    Device *device = COM_DATA(Device, A0);
    u32 count = U32(1), flags = U32(3);
    u64 rects = A2;
    if (!(flags & CLEAR_TARGET) || !device->target) {
        RET(D3D_OK);
        return;
    }
    RcnSurface *s = COM_DATA(RcnSurface, device->target);
    Layout layout = layout_of(s);
    u32 pixel = pixel_of_color(&layout, U32(4));
    for (u32 i = 0; i < (count && rects ? count : 1); i++) {
        i32 r[4] = {(i32)device->viewport[0], (i32)device->viewport[1], (i32)(device->viewport[0] + device->viewport[2]),
                    (i32)(device->viewport[1] + device->viewport[3])};
        if (count && rects) {
            for (int k = 0; k < 4; k++) r[k] = (i32)rd32(rects + 16 * i + 4 * (u32)k);
        }
        if (r[0] < 0) r[0] = 0;
        if (r[1] < 0) r[1] = 0;
        if (r[2] > (i32)s->width) r[2] = (i32)s->width;
        if (r[3] > (i32)s->height) r[3] = (i32)s->height;
        for (i32 y = r[1]; y < r[3]; y++) {
            u8 *p = (u8 *)PTR(s->pixels) + (size_t)y * s->pitch + (size_t)r[0] * (s->bpp / 8);
            for (i32 x = r[0]; x < r[2]; x++, p += s->bpp / 8) store_pixel(p, s->bpp, pixel);
        }
    }
    RET(D3D_OK);
}
// SetTransform / GetTransform(this, which, &matrix) : monde (1), vue (2), projection (3).
static float *matrix_of(Device *device, u32 which) {
    return which == 1 ? device->world : which == 2 ? device->view : which == 3 ? device->projection : NULL;
}
static void v_SetTransform(RcnCtx *ctx) {
    float *m = matrix_of(COM_DATA(Device, A0), U32(1));
    if (m && A2) memcpy(m, PTR(A2), 64);
    RET(D3D_OK);
}
static void v_GetTransform(RcnCtx *ctx) {
    float *m = matrix_of(COM_DATA(Device, A0), U32(1));
    if (m && A2) memcpy(PTR(A2), m, 64);
    RET(m ? D3D_OK : D3DERR_GENERIC);
}
static void v_MultiplyTransform(RcnCtx *ctx) {
    float *m = matrix_of(COM_DATA(Device, A0), U32(1));
    if (m && A2) {
        float by[16], product[16];
        memcpy(by, PTR(A2), 64);
        multiply(by, m, product);
        memcpy(m, product, 64);
    }
    RET(D3D_OK);
}
// SetViewport / GetViewport(this, &viewport) : D3DVIEWPORT7 { x, y, largeur, hauteur, zmin, zmax }.
static void v_SetViewport(RcnCtx *ctx) {
    memcpy(COM_DATA(Device, A0)->viewport, PTR(A1), 16);
    RET(D3D_OK);
}
static void v_GetViewport(RcnCtx *ctx) {
    memcpy(PTR(A1), COM_DATA(Device, A0)->viewport, 16);
    wrf32(A1 + 16, 0.0f);
    wrf32(A1 + 20, 1.0f);
    RET(D3D_OK);
}
// SetRenderState / GetRenderState(this, state, value)
static void v_SetRenderState(RcnCtx *ctx) {
    u32 state = U32(1);
    if (state < RS_COUNT) COM_DATA(Device, A0)->render_state[state] = U32(2);
    RET(D3D_OK);
}
static void v_GetRenderState(RcnCtx *ctx) {
    u32 state = U32(1);
    wr32(A2, state < RS_COUNT ? COM_DATA(Device, A0)->render_state[state] : 0);
    RET(D3D_OK);
}
// SetTexture / GetTexture(this, stage, surface) : seul l'étage 0 est rendu.
static void v_SetTexture(RcnCtx *ctx) {
    if (U32(1) == 0) COM_DATA(Device, A0)->texture = U32(2);
    RET(D3D_OK);
}
static void v_GetTexture(RcnCtx *ctx) {
    u32 texture = U32(1) == 0 ? COM_DATA(Device, A0)->texture : 0;
    if (texture) COM_REFS(texture)++;
    wr32(A2, texture);
    RET(D3D_OK);
}
// SetTextureStageState / GetTextureStageState(this, stage, state, value)
static void v_SetTextureStageState(RcnCtx *ctx) {
    Device *device = COM_DATA(Device, A0);
    u32 state = U32(2), value = U32(3);
    if (U32(1) == 0 && state < TSS_COUNT) {
        device->stage_state[state] = value;
        if (state == TSS_ADDRESS) device->stage_state[TSS_ADDRESSU] = device->stage_state[TSS_ADDRESSV] = value;
    }
    RET(D3D_OK);
}
static void v_GetTextureStageState(RcnCtx *ctx) {
    u32 state = U32(2);
    wr32(A3, U32(1) == 0 && state < TSS_COUNT ? COM_DATA(Device, A0)->stage_state[state] : 0);
    RET(D3D_OK);
}
// DrawPrimitive(this, type, fvf, &vertices, count, flags)
static void v_DrawPrimitive(RcnCtx *ctx) {
    draw(ctx, COM_DATA(Device, A0), U32(1), U32(2), A3, U32(4), NULL);
    RET(D3D_OK);
}
// DrawIndexedPrimitive(this, type, fvf, &vertices, vertex_count, &indices, index_count, flags)
static void v_DrawIndexedPrimitive(RcnCtx *ctx) {
    draw(ctx, COM_DATA(Device, A0), U32(1), U32(2), A3, U32(6), PTR(rcn_arg(ctx, 5)));
    RET(D3D_OK);
}
// ValidateDevice(this, &passes) : une passe.
static void v_ValidateDevice(RcnCtx *ctx) {
    if (A1) wr32(A1, 1);
    RET(D3D_OK);
}

#define M(name, fn, args) {#name, fn, args}
static const RcnMethod DEVICE_METHODS[] = {
    M(QueryInterface, v_QueryInterface, 3), M(AddRef, v_AddRef, 1), M(Release, v_Release, 1), M(GetCaps, v_GetCaps, 2),
    M(EnumTextureFormats, v_EnumTextureFormats, 3), M(BeginScene, v_Ok, 1), M(EndScene, v_Ok, 1),
    M(GetDirect3D, NULL, 2), M(SetRenderTarget, v_SetRenderTarget, 3), M(GetRenderTarget, v_GetRenderTarget, 2),
    M(Clear, v_Clear, 7), M(SetTransform, v_SetTransform, 3), M(GetTransform, v_GetTransform, 3),
    M(SetViewport, v_SetViewport, 2), M(MultiplyTransform, v_MultiplyTransform, 3), M(GetViewport, v_GetViewport, 2),
    M(SetMaterial, v_Ok, 2), M(GetMaterial, NULL, 2), M(SetLight, v_Ok, 3), M(GetLight, NULL, 3),
    M(SetRenderState, v_SetRenderState, 3), M(GetRenderState, v_GetRenderState, 3), M(BeginStateBlock, NULL, 1),
    M(EndStateBlock, NULL, 2), M(PreLoad, v_Ok, 2), M(DrawPrimitive, v_DrawPrimitive, 6),
    M(DrawIndexedPrimitive, v_DrawIndexedPrimitive, 8), M(SetClipStatus, v_Ok, 2), M(GetClipStatus, NULL, 2),
    M(DrawPrimitiveStrided, NULL, 6), M(DrawIndexedPrimitiveStrided, NULL, 8), M(DrawPrimitiveVB, NULL, 6),
    M(DrawIndexedPrimitiveVB, NULL, 8), M(ComputeSphereVisibility, NULL, 6), M(GetTexture, v_GetTexture, 3),
    M(SetTexture, v_SetTexture, 3), M(GetTextureStageState, v_GetTextureStageState, 4),
    M(SetTextureStageState, v_SetTextureStageState, 4), M(ValidateDevice, v_ValidateDevice, 2),
    M(ApplyStateBlock, NULL, 2), M(CaptureStateBlock, NULL, 2), M(DeleteStateBlock, NULL, 2),
    M(CreateStateBlock, NULL, 3), M(Load, NULL, 6), M(LightEnable, v_Ok, 3), M(GetLightEnable, NULL, 3),
    M(SetClipPlane, v_Ok, 3), M(GetClipPlane, NULL, 3), M(GetInfo, NULL, 4),
};

// --- IDirect3D7 ---

// CreateDevice(this, &class_id, surface, &device) : états de rendu à leurs valeurs par défaut.
static void d_CreateDevice(RcnCtx *ctx) {
    u64 object = rcn_com_new(ctx, "IDirect3DDevice7", DEVICE_METHODS, sizeof DEVICE_METHODS / sizeof DEVICE_METHODS[0], sizeof(Device));
    if (!object) {
        RET(D3DERR_GENERIC);
        return;
    }
    Device *device = COM_DATA(Device, object);
    device->target = U32(2);
    if (device->target) {
        RcnSurface *s = COM_DATA(RcnSurface, device->target);
        device->viewport[2] = s->width;
        device->viewport[3] = s->height;
    }
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    memcpy(device->world, identity, sizeof identity);
    memcpy(device->view, identity, sizeof identity);
    memcpy(device->projection, identity, sizeof identity);
    device->render_state[RS_SRCBLEND] = 2;  // ONE
    device->render_state[RS_DESTBLEND] = 1; // ZERO
    device->render_state[RS_CULLMODE] = 3;  // faces à sommets en sens antihoraire écartées
    device->render_state[RS_ALPHAFUNC] = 8; // ALWAYS
    device->render_state[RS_TEXTUREFACTOR] = 0xFFFFFFFFu;
    device->stage_state[TSS_COLOROP] = TOP_MODULATE;
    device->stage_state[TSS_COLORARG1] = TA_TEXTURE;
    device->stage_state[TSS_COLORARG2] = TA_CURRENT;
    device->stage_state[TSS_ALPHAOP] = TOP_SELECTARG1;
    device->stage_state[TSS_ALPHAARG1] = TA_TEXTURE;
    device->stage_state[TSS_ALPHAARG2] = TA_CURRENT;
    device->stage_state[TSS_ADDRESSU] = device->stage_state[TSS_ADDRESSV] = 1; // répétition
    wr32(A3, (u32)object);
    RET(D3D_OK);
}
// EnumDevices(this, callback, context) : un périphérique ; la fonction du jeu reçoit
// (description, nom, &D3DDEVICEDESC7, contexte).
static void d_EnumDevices(RcnCtx *ctx) {
    u32 callback = U32(1), context = U32(2);
    u8 *block = rcn_galloc(ctx, 236 + 64, 1);
    if (block) {
        u64 desc = (u64)(uintptr_t)block, text = desc + 236;
        write_device_desc(desc);
        strcpy(PTR(text), "RomCloud Native");
        strcpy(PTR(text + 32), "Direct3D HAL");
        u64 args[4] = {text, text + 32, desc, context};
        rcn_call_guest_n(ctx, callback, 4, args);
        rcn_gfree(ctx, block);
    }
    RET(D3D_OK);
}
// EnumZBufferFormats(this, &class_id, callback, context) : profondeur de 16 bits (acceptée, sans effet).
static void d_EnumZBufferFormats(RcnCtx *ctx) {
    u32 callback = U32(2), context = U32(3);
    u64 format = (u64)(uintptr_t)rcn_galloc(ctx, 32, 1);
    if (format) {
        wr32(format, 32);
        wr32(format + 4, 0x400); // DDPF_ZBUFFER
        wr32(format + 12, 16);
        wr32(format + 20, 0xFFFF);
        u64 args[2] = {format, context};
        rcn_call_guest_n(ctx, callback, 2, args);
        rcn_gfree(ctx, PTR(format));
    }
    RET(D3D_OK);
}

static const RcnMethod D3D_METHODS[] = {
    M(QueryInterface, v_QueryInterface, 3), M(AddRef, v_AddRef, 1), M(Release, v_Release, 1),
    M(EnumDevices, d_EnumDevices, 3), M(CreateDevice, d_CreateDevice, 4), M(CreateVertexBuffer, NULL, 4),
    M(EnumZBufferFormats, d_EnumZBufferFormats, 4), M(EvictManagedTextures, v_Ok, 1),
};

u64 rcn_d3d_new(RcnCtx *ctx) {
    return rcn_com_new(ctx, "IDirect3D7", D3D_METHODS, sizeof D3D_METHODS / sizeof D3D_METHODS[0], 4);
}
