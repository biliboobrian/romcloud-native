// RomCloud Native : appareil Direct3D 11 — ressources, vues, nuanceurs, dispositions, états (voir rcn_d3d11.h).
//
// Textures : toute texture 1D ou 2D (sans multi-échantillonnage ni cube) est un tableau de textures 2D
// d'OpenGL, si bien qu'une même texture sert les vues Texture2D et Texture2DArray (le traducteur de
// nuanceurs emploie partout sampler2DArray). Tampons : un tampon OpenGL ; ceux de transfert (STAGING)
// ont en plus une copie du processeur, comme les textures de transfert.
#include <stdio.h>
#include <stdlib.h>

#include "rcn_bcn.h"
#include "rcn_d3d11.h"
#include "rcn_dxbc.h"

#define SCRATCH_UNIT 47 // unité de texture des envois (hors des liaisons du dessin)

u32 d3d_mip_size(u32 size, u32 mip) {
    u32 s = size >> mip;
    return s ? s : 1;
}
u32 d3d_sub_count(const D3dRes *r) { return r->dim == DIM_BUFFER ? 1 : r->mips * (r->dim == DIM_TEX3D ? 1 : r->layers); }
GLenum d3d_cube_face_target(u32 face) { return GL_TEXTURE_CUBE_MAP_POSITIVE_X + face; }

// Octets d'une ligne et nombre de lignes (de blocs) d'un niveau.
static void level_layout(const D3dFormat *f, u32 w, u32 h, u32 *row, u32 *rows) {
    if (f->block) {
        *row = ((w + 3) / 4) * f->bytes;
        *rows = (h + 3) / 4;
    } else {
        *row = w * f->bytes;
        *rows = h;
    }
}

// --- Destruction ---

static void res_destroy(D3dObj *o) {
    D3dRes *r = (D3dRes *)o;
    if (r->gl || r->tbo) {
        // Supprimés par n'importe quel contexte (objets partagés).
        if (r->dim == DIM_BUFFER) {
            if (GL(glDeleteBuffers) && r->gl) GL(glDeleteBuffers)(1, &r->gl);
            if (GL(glDeleteTextures) && r->tbo) GL(glDeleteTextures)(1, &r->tbo);
        } else if (GL(glDeleteTextures)) {
            GL(glDeleteTextures)(1, &r->gl);
        }
    }
    free(r->mem);
    free(r->sub_offset);
}

// --- Tampons ---

static void dev_CreateBuffer(RcnCtx *ctx);
static const D3dMethod buffer_methods[];
static const D3dMethod texture_methods[];

static D3dRes *buffer_new(RcnCtx *ctx, D3dDevice *dev, const u32 *d, u64 init) {
    D3dRes *r = d3d_new(ctx, K_BUFFER, sizeof(D3dRes), buffer_methods);
    if (!r) return NULL;
    r->obj.device = dev;
    r->obj.destroy = res_destroy;
    r->dim = DIM_BUFFER;
    r->size = d[0];
    r->usage = d[1];
    r->bind = d[2];
    r->cpu = d[3];
    r->misc = d[4];
    r->stride = d[5];
    const void *data = init ? PTR(rd64(init)) : NULL;
    if (r->usage == USAGE_STAGING) {
        r->mem = calloc(1, r->size ? r->size : 1);
        r->mem_size = r->size;
        if (data && r->mem) memcpy(r->mem, data, r->size);
    }
    if (r->usage != USAGE_STAGING && d3d_gl_current(ctx)) {
        GL(glGenBuffers)(1, &r->gl);
        GL(glBindBuffer)(GL_COPY_WRITE_BUFFER, r->gl);
        GLenum usage = r->usage == USAGE_DYNAMIC ? GL_DYNAMIC_DRAW : r->usage == USAGE_STAGING ? GL_STREAM_READ : GL_STATIC_DRAW;
        GL(glBufferData)(GL_COPY_WRITE_BUFFER, r->size, data, usage);
        GL(glBindBuffer)(GL_COPY_WRITE_BUFFER, 0);
        d3d_gl_publish();
        d3d_gl_check("CreateBuffer");
    }
    return r;
}

// CreateBuffer(D3D11_BUFFER_DESC *, D3D11_SUBRESOURCE_DATA *, **buffer)
static void dev_CreateBuffer(RcnCtx *ctx) {
    D3dDevice *dev = THIS(D3dDevice);
    u64 d = A1, init = A2, out = A3;
    if (!d) {
        RET(E_INVALIDARG);
        return;
    }
    if (!out) { // validation seule
        RET(S_FALSE);
        return;
    }
    wr64(out, 0);
    D3dRes *r = buffer_new(ctx, dev, PTR(d), init);
    if (!r) {
        RET(E_OUTOFMEMORY);
        return;
    }
    if (d3d_log_level > 1) d3d_log("CreateBuffer %u octets, usage %u, liaisons 0x%x", r->size, r->usage, r->bind);
    wr64(out, (u64)(uintptr_t)r);
    RET(S_OK);
}

static void buf_GetType(RcnCtx *ctx) {
    D3dRes *r = THIS(D3dRes);
    if (A1) wr32(A1, r->dim);
}
static void buf_GetDesc(RcnCtx *ctx) {
    D3dRes *r = THIS(D3dRes);
    if (A1) {
        u32 v[6] = {r->size, r->usage, r->bind, r->cpu, r->misc, r->stride};
        memcpy(PTR(A1), v, sizeof v);
    }
}
static void res_SetEvictionPriority(RcnCtx *ctx) { (void)ctx; }
static void res_GetEvictionPriority(RcnCtx *ctx) { RET(0); }

static const D3dMethod buffer_methods[] = {
    {"GetType", buf_GetType}, {"GetDesc", buf_GetDesc}, {"SetEvictionPriority", res_SetEvictionPriority},
    {"GetEvictionPriority", res_GetEvictionPriority}, {NULL, NULL},
};

// --- Textures ---

// Envoi d'une texture compressée que le GPU ne lit pas : blocs décompressés en RGBA 8 bits.
static void decode_upload(D3dRes *r, u32 mip, u32 layer, u32 x, u32 y, u32 z, u32 w, u32 h, u32 dd, const u8 *data,
                          u32 row_pitch, u32 slice_pitch) {
    if (!rcn_bcn_kind(r->format)) {
        static int told;
        if (!told++) d3d_log("format compressé %u (BC6H / BC7) pas encore décompressé : texture noire", r->format);
        return;
    }
    const D3dFormat *g = d3d_texture_format(r->format, 0);
    u32 bw = (w + 3) / 4, bh = (h + 3) / 4, pitch = bw * 16;
    u8 *tmp = malloc((size_t)pitch * bh * 4 * dd);
    if (!tmp) return;
    for (u32 k = 0; k < dd; k++) rcn_bcn_decode(r->format, data + (size_t)k * slice_pitch, row_pitch, bw, bh, tmp + (size_t)k * pitch * bh * 4, pitch);
    GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, (GLint)(bw * 4));
    GL(glPixelStorei)(GL_UNPACK_IMAGE_HEIGHT, (GLint)(bh * 4));
    GL(glActiveTexture)(GL_TEXTURE0 + SCRATCH_UNIT);
    GL(glBindTexture)(r->target, r->gl);
    switch (r->target) {
    case GL_TEXTURE_CUBE_MAP: GL(glTexSubImage2D)(d3d_cube_face_target(layer % 6), mip, x, y, w, h, g->format, g->type, tmp); break;
    case GL_TEXTURE_3D: GL(glTexSubImage3D)(r->target, mip, x, y, z, w, h, dd, g->format, g->type, tmp); break;
    case GL_TEXTURE_2D_MULTISAMPLE: break;
    default: GL(glTexSubImage3D)(r->target, mip, x, y, layer, w, h, 1, g->format, g->type, tmp); break;
    }
    GL(glBindTexture)(r->target, 0);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, 0);
    GL(glPixelStorei)(GL_UNPACK_IMAGE_HEIGHT, 0);
    free(tmp);
    r->version++;
    d3d_gl_check("envoi de texture décompressée");
}

void d3d_res_upload(D3dRes *r, u32 sub, const u32 *box, const u8 *data, u32 row_pitch, u32 slice_pitch) {
    if (!r->gl || !data) return;
    u32 mip = sub % r->mips, layer = sub / r->mips;
    u32 mw = d3d_mip_size(r->width, mip), mh = d3d_mip_size(r->height, mip), md = r->dim == DIM_TEX3D ? d3d_mip_size(r->depth, mip) : 1;
    u32 x = 0, y = 0, z = 0, w = mw, h = mh, dd = md;
    if (box) {
        x = box[0], y = box[1], z = box[2];
        w = box[3] - box[0], h = box[4] - box[1], dd = box[5] - box[2];
        if (!w || !h || !dd) return;
    }
    const D3dFormat *f = d3d_format(d3d_storage_format(r->format, (r->bind & BIND_DEPTH_STENCIL) != 0));
    u32 row, rows;
    level_layout(f, w, h, &row, &rows);
    if (!row_pitch) row_pitch = row;
    if (!slice_pitch) slice_pitch = row_pitch * rows;
    if (d3d_bcn_decoded(r->format)) {
        decode_upload(r, mip, layer, x, y, z, w, h, dd, data, row_pitch, slice_pitch);
        return;
    }
    // Copie serrée (et octets B/R échangés) si les lignes du jeu ne le sont pas.
    u8 *tmp = NULL;
    const u8 *src = data;
    int swap = f->swap_rb == 1 || f->swap_rb == 3;
    if (row_pitch != row || slice_pitch != row * rows || swap) {
        tmp = malloc((size_t)row * rows * dd);
        if (!tmp) return;
        for (u32 k = 0; k < dd; k++) {
            for (u32 j = 0; j < rows; j++) {
                u8 *o = tmp + ((size_t)k * rows + j) * row;
                memcpy(o, data + (size_t)k * slice_pitch + (size_t)j * row_pitch, row);
                if (swap) {
                    for (u32 i = 0; i + 3 < row; i += 4) {
                        u8 t = o[i];
                        o[i] = o[i + 2];
                        o[i + 2] = t;
                    }
                }
            }
        }
        src = tmp;
    }
    GL(glActiveTexture)(GL_TEXTURE0 + SCRATCH_UNIT);
    GL(glBindTexture)(r->target, r->gl);
    GLsizei bytes = (GLsizei)((size_t)row * rows * dd);
    switch (r->target) {
    case GL_TEXTURE_CUBE_MAP:
        if (f->block) GL(glCompressedTexSubImage2D)(d3d_cube_face_target(layer % 6), mip, x, y, w, h, f->internal, bytes, src);
        else GL(glTexSubImage2D)(d3d_cube_face_target(layer % 6), mip, x, y, w, h, f->format, f->type, src);
        break;
    case GL_TEXTURE_3D:
        if (f->block) GL(glCompressedTexSubImage3D)(r->target, mip, x, y, z, w, h, dd, f->internal, bytes, src);
        else GL(glTexSubImage3D)(r->target, mip, x, y, z, w, h, dd, f->format, f->type, src);
        break;
    case GL_TEXTURE_2D_MULTISAMPLE:
        break;
    default: // tableaux 2D (et cubes en tableau) : une couche
        if (f->block) GL(glCompressedTexSubImage3D)(r->target, mip, x, y, layer, w, h, 1, f->internal, bytes, src);
        else GL(glTexSubImage3D)(r->target, mip, x, y, layer, w, h, 1, f->format, f->type, src);
        break;
    }
    GL(glBindTexture)(r->target, 0);
    GL(glActiveTexture)(GL_TEXTURE0);
    free(tmp);
    r->version++;
    d3d_gl_check("envoi de texture");
}

// desc : D3D11_TEXTURE{1,2,3}D_DESC (mots de 32 bits), selon dim.
D3dRes *d3d_texture_new(RcnCtx *ctx, D3dDevice *dev, u32 dim, const u32 *d, u64 init) {
    D3dKind kind = dim == DIM_TEX1D ? K_TEX1D : dim == DIM_TEX2D ? K_TEX2D : K_TEX3D;
    D3dRes *r = d3d_new(ctx, kind, sizeof(D3dRes), texture_methods);
    if (!r) return NULL;
    r->obj.device = dev;
    r->obj.destroy = res_destroy;
    r->dim = dim;
    r->height = r->depth = r->layers = r->samples = 1;
    if (dim == DIM_TEX1D) {
        r->width = d[0], r->mips = d[1], r->layers = d[2], r->format = d[3];
        r->usage = d[4], r->bind = d[5], r->cpu = d[6], r->misc = d[7];
    } else if (dim == DIM_TEX2D) {
        r->width = d[0], r->height = d[1], r->mips = d[2], r->layers = d[3], r->format = d[4], r->samples = d[5];
        r->usage = d[7], r->bind = d[8], r->cpu = d[9], r->misc = d[10];
    } else {
        r->width = d[0], r->height = d[1], r->depth = d[2], r->mips = d[3], r->format = d[4];
        r->usage = d[5], r->bind = d[6], r->cpu = d[7], r->misc = d[8];
    }
    if (!r->samples) r->samples = 1;
    if (!r->layers) r->layers = 1;
    if (!r->mips) { // chaîne complète
        u32 m = r->width > r->height ? r->width : r->height;
        if (r->depth > m) m = r->depth;
        r->mips = 1;
        while (m > 1) m >>= 1, r->mips++;
    }
    int depth_bound = (r->bind & BIND_DEPTH_STENCIL) != 0;
    const D3dFormat *f = d3d_format(d3d_storage_format(r->format, depth_bound));     // données du jeu
    const D3dFormat *gl_format = d3d_texture_format(r->format, depth_bound);           // stockage OpenGL
    if (!f->internal) d3d_log("format DXGI %u non géré (texture %ux%u)", r->format, r->width, r->height);
    u32 subs = d3d_sub_count(r);
    // Transfert : copie du processeur, sous-ressources à la suite.
    if (r->usage == USAGE_STAGING) {
        r->sub_offset = calloc(subs + 1, 4);
        size_t total = 0;
        for (u32 s = 0; r->sub_offset && s < subs; s++) {
            u32 mip = s % r->mips, row, rows;
            level_layout(f, d3d_mip_size(r->width, mip), d3d_mip_size(r->height, mip), &row, &rows);
            r->sub_offset[s] = (u32)total;
            total += (size_t)row * rows * (dim == DIM_TEX3D ? d3d_mip_size(r->depth, mip) : 1);
            total = (total + 15) & ~(size_t)15;
        }
        r->mem = calloc(1, total ? total : 1);
        r->mem_size = total;
    }
    if (r->usage == USAGE_STAGING || !d3d_gl_current(ctx) || !f->internal) return r;
    if (dim == DIM_TEX3D) r->target = GL_TEXTURE_3D;
    else if (r->samples > 1) r->target = GL_TEXTURE_2D_MULTISAMPLE;
    else if (r->misc & MISC_TEXTURECUBE) r->target = r->layers > 6 ? GL_TEXTURE_CUBE_MAP_ARRAY : GL_TEXTURE_CUBE_MAP;
    else r->target = GL_TEXTURE_2D_ARRAY;
    GL(glGenTextures)(1, &r->gl);
    GL(glActiveTexture)(GL_TEXTURE0 + SCRATCH_UNIT);
    GL(glBindTexture)(r->target, r->gl);
    switch (r->target) {
    case GL_TEXTURE_3D: GL(glTexStorage3D)(r->target, r->mips, gl_format->internal, r->width, r->height, r->depth); break;
    case GL_TEXTURE_2D_MULTISAMPLE:
        GL(glTexStorage2DMultisample)(r->target, r->samples, gl_format->internal, r->width, r->height, 1);
        break;
    case GL_TEXTURE_CUBE_MAP: GL(glTexStorage2D)(r->target, r->mips, gl_format->internal, r->width, r->height); break;
    default: GL(glTexStorage3D)(r->target, r->mips, gl_format->internal, r->width, r->height, r->layers); break;
    }
    if (r->target != GL_TEXTURE_2D_MULTISAMPLE) {
        GL(glTexParameteri)(r->target, GL_TEXTURE_MAX_LEVEL, (GLint)r->mips - 1);
        GL(glTexParameteri)(r->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        GL(glTexParameteri)(r->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (f->swap_rb == 2) { // A8 : rouge rangé, lu comme alpha
            GL(glTexParameteri)(r->target, GL_TEXTURE_SWIZZLE_R, GL_ZERO);
            GL(glTexParameteri)(r->target, GL_TEXTURE_SWIZZLE_G, GL_ZERO);
            GL(glTexParameteri)(r->target, GL_TEXTURE_SWIZZLE_B, GL_ZERO);
            GL(glTexParameteri)(r->target, GL_TEXTURE_SWIZZLE_A, GL_RED);
        } else if (f->swap_rb == 3) {
            GL(glTexParameteri)(r->target, GL_TEXTURE_SWIZZLE_A, GL_ONE);
        }
    }
    GL(glBindTexture)(r->target, 0);
    GL(glActiveTexture)(GL_TEXTURE0);
    if (init) {
        for (u32 s = 0; s < subs; s++) {
            u64 sd = init + 16ull * s;
            d3d_res_upload(r, s, NULL, PTR(rd64(sd)), rd32(sd + 8), rd32(sd + 12));
        }
    }
    d3d_gl_publish();
    d3d_gl_check("CreateTexture");
    return r;
}

static void create_texture(RcnCtx *ctx, u32 dim) {
    D3dDevice *dev = THIS(D3dDevice);
    u64 d = A1, init = A2, out = A3;
    if (!d) {
        RET(E_INVALIDARG);
        return;
    }
    if (!out) {
        RET(S_FALSE);
        return;
    }
    wr64(out, 0);
    D3dRes *r = d3d_texture_new(ctx, dev, dim, PTR(d), init);
    if (!r) {
        RET(E_OUTOFMEMORY);
        return;
    }
    if (d3d_log_level > 1)
        d3d_log("CreateTexture%uD %ux%ux%u, %u niveaux, %u couches, format %u, liaisons 0x%x, usage %u", dim - 1, r->width,
                r->height, r->depth, r->mips, r->layers, r->format, r->bind, r->usage);
    wr64(out, (u64)(uintptr_t)r);
    RET(S_OK);
}
static void dev_CreateTexture1D(RcnCtx *ctx) { create_texture(ctx, DIM_TEX1D); }
static void dev_CreateTexture2D(RcnCtx *ctx) { create_texture(ctx, DIM_TEX2D); }
static void dev_CreateTexture3D(RcnCtx *ctx) { create_texture(ctx, DIM_TEX3D); }

static void tex_GetType(RcnCtx *ctx) {
    if (A1) wr32(A1, THIS(D3dRes)->dim);
}
// GetDesc / GetDesc1 : D3D11_TEXTURE{1,2,3}D_DESC(1).
static void tex_GetDesc(RcnCtx *ctx) {
    D3dRes *r = THIS(D3dRes);
    u64 d = A1;
    if (!d) return;
    if (r->dim == DIM_TEX1D) {
        u32 v[8] = {r->width, r->mips, r->layers, r->format, r->usage, r->bind, r->cpu, r->misc};
        memcpy(PTR(d), v, sizeof v);
    } else if (r->dim == DIM_TEX2D) {
        u32 v[11] = {r->width, r->height, r->mips, r->layers, r->format, r->samples, 0, r->usage, r->bind, r->cpu, r->misc};
        memcpy(PTR(d), v, sizeof v);
    } else {
        u32 v[9] = {r->width, r->height, r->depth, r->mips, r->format, r->usage, r->bind, r->cpu, r->misc};
        memcpy(PTR(d), v, sizeof v);
    }
}
static void tex_GetDesc1(RcnCtx *ctx) {
    D3dRes *r = THIS(D3dRes);
    tex_GetDesc(ctx);
    if (A1) wr32(A1 + (r->dim == DIM_TEX2D ? 44 : 36), 0); // TextureLayout
}

static const D3dMethod texture_methods[] = {
    {"GetType", tex_GetType}, {"GetDesc", tex_GetDesc}, {"GetDesc1", tex_GetDesc1},
    {"SetEvictionPriority", res_SetEvictionPriority}, {"GetEvictionPriority", res_GetEvictionPriority}, {NULL, NULL},
};

// --- Vues ---

static void view_destroy(D3dObj *o) {
    D3dView *v = (D3dView *)o;
    d3d_fb_forget(v);
    if (v->res) d3d_release(&v->res->obj);
}
static void view_GetResource(RcnCtx *ctx) {
    D3dView *v = THIS(D3dView);
    if (A1) wr64(A1, 0);
    d3d_out(A1, v->res ? &v->res->obj : NULL);
}
// GetDesc : D3D11_*_VIEW_DESC (format, dimension, puis l'union selon la dimension).
static void view_GetDesc(RcnCtx *ctx) {
    D3dView *v = THIS(D3dView);
    u64 d = A1;
    if (!d) return;
    u32 w[6] = {v->format, v->dim, 0, 0, 0, 0};
    switch (v->obj.kind) {
    case K_SRV:
        if (v->dim == 1 || v->dim == 11) w[2] = v->first, w[3] = v->count;
        else w[2] = v->mip, w[3] = v->mips, w[4] = v->layer, w[5] = v->layers;
        break;
    case K_DSV: w[2] = v->flags, w[3] = v->mip, w[4] = v->layer, w[5] = v->layers; break;
    default:
        if (v->dim == 1) w[2] = v->first, w[3] = v->count;
        else w[2] = v->mip, w[3] = v->layer, w[4] = v->layers;
        break;
    }
    memcpy(PTR(d), w, v->obj.kind == K_DSV ? 24 : 20);
}
static const D3dMethod view_methods[] = {
    {"GetResource", view_GetResource}, {"GetDesc", view_GetDesc}, {"GetDesc1", view_GetDesc}, {NULL, NULL},
};

// Dimension de vue par défaut d'une ressource (desc NULL), selon le genre de vue.
static u32 default_dim(const D3dRes *r, D3dKind kind) {
    int array = r->layers > 1;
    if (r->dim == DIM_BUFFER) return kind == K_DSV ? 0 : 1;
    if (r->dim == DIM_TEX1D) return kind == K_DSV ? (array ? 2 : 1) : (array ? 3 : 2);
    if (r->dim == DIM_TEX3D) return 8;
    if (kind == K_SRV && (r->misc & MISC_TEXTURECUBE)) return r->layers > 6 ? 10 : 9;
    if (r->samples > 1) return kind == K_DSV ? (array ? 6 : 5) : (array ? 7 : 6);
    if (kind == K_DSV) return array ? 4 : 3;
    return array ? 5 : 4;
}

// Lit l'union de la description selon le genre de vue et sa dimension.
static void parse_view(D3dView *v, const u32 *u) {
    D3dRes *r = v->res;
    v->mip = 0, v->mips = r->mips, v->layer = 0, v->layers = r->dim == DIM_TEX3D ? r->depth : r->layers;
    if (v->obj.kind == K_SRV) {
        switch (v->dim) {
        case 1: v->first = u[0], v->count = u[1]; break;            // BUFFER
        case 11: v->first = u[0], v->count = u[1], v->flags = u[2]; break; // BUFFEREX
        case 2: case 4: case 8: case 9: v->mip = u[0], v->mips = u[1], v->layers = 1; break;
        case 3: case 5: v->mip = u[0], v->mips = u[1], v->layer = u[2], v->layers = u[3]; break;
        case 10: v->mip = u[0], v->mips = u[1], v->layer = u[2], v->layers = u[3] * 6; break; // cubes : First2DArrayFace, NumCubes
        case 6: v->layers = 1; break;
        case 7: v->layer = u[0], v->layers = u[1]; break;
        }
        if (v->dim == 9) v->layers = 6;
        if (v->mips == 0xFFFFFFFFu) v->mips = r->mips - v->mip;
    } else if (v->obj.kind == K_DSV) {
        v->flags = u[0];
        u += 1;
        switch (v->dim) {
        case 1: case 3: v->mip = u[0], v->layers = 1; break;
        case 2: case 4: v->mip = u[0], v->layer = u[1], v->layers = u[2]; break;
        case 5: v->layers = 1; break;
        case 6: v->layer = u[0], v->layers = u[1]; break;
        }
        v->mips = 1;
    } else { // RTV, UAV
        switch (v->dim) {
        case 1: v->first = u[0], v->count = u[1], v->flags = v->obj.kind == K_UAV ? u[2] : 0; break;
        case 2: case 4: v->mip = u[0], v->layers = 1; break;
        case 3: case 5: v->mip = u[0], v->layer = u[1], v->layers = u[2]; break;
        case 6: v->layers = 1; break;
        case 7: v->layer = u[0], v->layers = u[1]; break;
        case 8: v->mip = u[0], v->layer = u[1], v->layers = u[2]; break; // FirstWSlice, WSize
        }
        v->mips = 1;
    }
    if (v->layers == 0xFFFFFFFFu || v->layers == 0) v->layers = (r->dim == DIM_TEX3D ? d3d_mip_size(r->depth, v->mip) : r->layers) - v->layer;
}

static void create_view(RcnCtx *ctx, D3dKind kind) {
    D3dDevice *dev = THIS(D3dDevice);
    D3dRes *r = (D3dRes *)OBJ(A1);
    u64 d = A2, out = A3;
    if (!r) {
        RET(E_INVALIDARG);
        return;
    }
    if (!out) {
        RET(S_FALSE);
        return;
    }
    wr64(out, 0);
    D3dView *v = d3d_new(ctx, kind, sizeof(D3dView), view_methods);
    if (!v) {
        RET(E_OUTOFMEMORY);
        return;
    }
    v->obj.device = dev;
    v->obj.destroy = view_destroy;
    v->res = r;
    d3d_addref(&r->obj);
    if (d) {
        const u32 *w = PTR(d);
        v->format = w[0] ? w[0] : r->format;
        v->dim = w[1] ? w[1] : default_dim(r, kind);
        parse_view(v, w + 2);
    } else {
        static const u32 zero[6];
        v->format = r->format;
        v->dim = default_dim(r, kind);
        parse_view(v, zero);
        if (r->dim == DIM_BUFFER) v->first = 0, v->count = r->stride ? r->size / r->stride : r->size / 4;
    }
    if (d3d_log_level > 1) d3d_log("vue %u de la ressource %p : format %u, dimension %u", kind, (void *)r, v->format, v->dim);
    wr64(out, (u64)(uintptr_t)v);
    RET(S_OK);
}
static void dev_CreateShaderResourceView(RcnCtx *ctx) { create_view(ctx, K_SRV); }
static void dev_CreateRenderTargetView(RcnCtx *ctx) { create_view(ctx, K_RTV); }
static void dev_CreateDepthStencilView(RcnCtx *ctx) { create_view(ctx, K_DSV); }
static void dev_CreateUnorderedAccessView(RcnCtx *ctx) { create_view(ctx, K_UAV); }

// --- Nuanceurs ---

static void shader_destroy(D3dObj *o) {
    D3dShader *s = (D3dShader *)o;
    if (s->dx) dxbc_free(s->dx);
    if (s->gl && s->gl != ~0u && GL(glDeleteShader)) GL(glDeleteShader)(s->gl);
    free(s->code);
}
static const D3dMethod shader_methods[] = {{NULL, NULL}};

static u64 fnv(const u8 *p, size_t n) {
    u64 h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

// Vérification (RCN_DXBC_ALL=1) : chaque nuanceur traduit et compilé seul dès sa création, pour
// couvrir ceux du jeu qui ne sont pas encore dessinés. RCN_GLSL_ES=1 : en GLSL ES.
static void check_shader(RcnCtx *ctx, D3dShader *s) {
    static int mode = -1;
    static u32 total, untranslated, rejected;
    if (mode < 0) mode = getenv("RCN_DXBC_ALL") ? atoi(getenv("RCN_DXBC_ALL")) : 0;
    if (!mode || !s->dx || (s->stage != 0 && s->stage != 1) || !d3d_gl_current(ctx)) return;
    int es = d3d_caps.es || (getenv("RCN_GLSL_ES") && atoi(getenv("RCN_GLSL_ES")));
    total++;
    char *src = dxbc_glsl(s->dx, NULL, NULL, es);
    if (!src) {
        untranslated++;
        d3d_log("vérification : nuanceur %016llx non traduit : %s", (unsigned long long)s->hash, s->dx->error);
    } else {
        GLuint sh = GL(glCreateShader)(s->stage == 0 ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
        const GLchar *text = src;
        GL(glShaderSource)(sh, 1, &text, NULL);
        GL(glCompileShader)(sh);
        GLint ok = 0;
        GL(glGetShaderiv)(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = "";
            GL(glGetShaderInfoLog)(sh, sizeof log, NULL, log);
            rejected++;
            d3d_log("vérification : nuanceur %016llx refusé :\n%s", (unsigned long long)s->hash, log);
            if (mode > 1) d3d_log("source :\n%s", src);
        }
        GL(glDeleteShader)(sh);
        free(src);
    }
    if (total % 20 == 0) d3d_log("vérification : %u nuanceurs, %u non traduits, %u refusés", total, untranslated, rejected);
}

static void create_shader(RcnCtx *ctx, D3dKind kind, u32 stage, u64 out) {
    D3dDevice *dev = THIS(D3dDevice);
    u64 code = A1, size = A2;
    if (!code || !size) {
        RET(E_INVALIDARG);
        return;
    }
    if (!out) {
        RET(S_FALSE);
        return;
    }
    wr64(out, 0);
    D3dShader *s = d3d_new(ctx, kind, sizeof(D3dShader), shader_methods);
    if (!s) {
        RET(E_OUTOFMEMORY);
        return;
    }
    s->obj.device = dev;
    s->obj.destroy = shader_destroy;
    s->stage = stage;
    s->size = (size_t)size;
    s->code = malloc(s->size);
    if (s->code) memcpy(s->code, PTR(code), s->size);
    s->hash = s->code ? fnv(s->code, s->size) : 0;
    s->dx = s->code ? dxbc_parse(s->code, s->size) : NULL;
    if (!s->dx) d3d_log("nuanceur %016llx illisible", (unsigned long long)s->hash);
    check_shader(ctx, s);
    if (d3d_log_level > 1) d3d_log("nuanceur %u : %zu octets, %016llx", stage, s->size, (unsigned long long)s->hash);
    wr64(out, (u64)(uintptr_t)s);
    RET(S_OK);
}
// Create*Shader(bytecode, length, linkage, **shader)
static void dev_CreateVertexShader(RcnCtx *ctx) { create_shader(ctx, K_VS, 0, STK(4)); }
static void dev_CreatePixelShader(RcnCtx *ctx) { create_shader(ctx, K_PS, 1, STK(4)); }
static void dev_CreateGeometryShader(RcnCtx *ctx) { create_shader(ctx, K_GS, 2, STK(4)); }
static void dev_CreateHullShader(RcnCtx *ctx) { create_shader(ctx, K_HS, 3, STK(4)); }
static void dev_CreateDomainShader(RcnCtx *ctx) { create_shader(ctx, K_DS, 4, STK(4)); }
static void dev_CreateComputeShader(RcnCtx *ctx) { create_shader(ctx, K_CS, 5, STK(4)); }
// CreateGeometryShaderWithStreamOutput(code, size, decl, n, strides, n, stream, linkage, **shader)
static void dev_CreateGeometryShaderWithStreamOutput(RcnCtx *ctx) { create_shader(ctx, K_GS, 2, STK(8)); }

static void dev_CreateClassLinkage(RcnCtx *ctx) {
    D3dObj *o = d3d_new(ctx, K_LINKAGE, sizeof(D3dObj), shader_methods);
    if (o) o->device = THIS(D3dDevice);
    if (A1) wr64(A1, (u64)(uintptr_t)o);
    RET(o ? S_OK : E_OUTOFMEMORY);
}

// --- Disposition des sommets ---

// CreateInputLayout(D3D11_INPUT_ELEMENT_DESC *, count, signature, size, **layout)
static void dev_CreateInputLayout(RcnCtx *ctx) {
    D3dDevice *dev = THIS(D3dDevice);
    u64 el = A1, out = STK(5);
    u32 count = (u32)A2;
    if (!out) {
        RET(S_FALSE);
        return;
    }
    wr64(out, 0);
    D3dLayout *l = d3d_new(ctx, K_LAYOUT, sizeof(D3dLayout), shader_methods);
    if (!l) {
        RET(E_OUTOFMEMORY);
        return;
    }
    l->obj.device = dev;
    u32 offsets[32] = {0};
    for (u32 i = 0; i < count && i < 32; i++) {
        u64 e = el + 32ull * i;
        D3dElement *x = &l->el[l->count++];
        snprintf(x->semantic, sizeof x->semantic, "%s", STR(rd64(e)));
        x->index = rd32(e + 8);
        x->format = rd32(e + 12);
        x->slot = rd32(e + 16) & 31;
        x->offset = rd32(e + 20);
        x->instanced = rd32(e + 24);
        x->step = rd32(e + 28);
        if (x->offset == 0xFFFFFFFFu) x->offset = offsets[x->slot]; // D3D11_APPEND_ALIGNED_ELEMENT
        offsets[x->slot] = x->offset + d3d_format(x->format)->bytes;
    }
    wr64(out, (u64)(uintptr_t)l);
    RET(S_OK);
}

// --- États ---

static void state_GetDesc(RcnCtx *ctx);
static const D3dMethod state_methods[] = {{"GetDesc", state_GetDesc}, {"GetDesc1", state_GetDesc}, {"GetDesc2", state_GetDesc}, {NULL, NULL}};

// GetDesc : les états gardent leur description d'origine juste après eux (octets).
typedef struct {
    u32 size;
    u8 bytes[300];
} RawDesc;
static RawDesc *raw_of(D3dObj *o) {
    switch (o->kind) {
    case K_BLEND: return (RawDesc *)((u8 *)o + sizeof(D3dBlend));
    case K_DEPTH: return (RawDesc *)((u8 *)o + sizeof(D3dDepth));
    case K_RASTER: return (RawDesc *)((u8 *)o + sizeof(D3dRaster));
    case K_SAMPLER: return (RawDesc *)((u8 *)o + sizeof(D3dSampler));
    default: return NULL;
    }
}
static void state_GetDesc(RcnCtx *ctx) {
    RawDesc *raw = raw_of(THIS(D3dObj));
    if (A1 && raw) memcpy(PTR(A1), raw->bytes, raw->size);
}
static void *state_new(RcnCtx *ctx, D3dKind kind, size_t size, u64 desc, u32 desc_size) {
    D3dObj *o = d3d_new(ctx, kind, size + sizeof(RawDesc), state_methods);
    if (!o) return NULL;
    o->device = THIS(D3dDevice);
    RawDesc *raw = raw_of(o);
    raw->size = desc_size;
    memcpy(raw->bytes, PTR(desc), desc_size);
    return o;
}

static GLenum blend_factor(u32 b) {
    static const GLenum map[] = {0, GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                 GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA_SATURATE,
                                 0, 0, GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR,
                                 GL_SRC1_ALPHA, GL_ONE_MINUS_SRC1_ALPHA};
    return b < sizeof map / sizeof map[0] && (map[b] || b == 1) ? map[b] : GL_ONE;
}
static GLenum blend_op(u32 op) {
    static const GLenum map[] = {GL_FUNC_ADD, GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX};
    return op < 6 ? map[op] : GL_FUNC_ADD;
}

// CreateBlendState(D3D11_BLEND_DESC *, **) ; CreateBlendState1 : cibles de 40 octets (opérations logiques).
static void blend_state(RcnCtx *ctx, int v1) {
    u64 d = A1, out = A2;
    if (!d) {
        RET(E_INVALIDARG);
        return;
    }
    if (!out) {
        RET(S_FALSE);
        return;
    }
    u32 rt_size = v1 ? 40 : 32;
    D3dBlend *b = state_new(ctx, K_BLEND, sizeof(D3dBlend), d, 8 + 8 * rt_size);
    if (!b) {
        RET(E_OUTOFMEMORY);
        return;
    }
    b->alpha_to_coverage = rd32(d);
    b->independent = rd32(d + 4);
    for (int i = 0; i < 8; i++) {
        u64 rt = d + 8 + rt_size * (u64)(b->independent ? i : 0);
        const u32 *w = PTR(rt);
        int k = v1 ? 2 : 1; // BlendEnable, [LogicOpEnable], Src, Dest, Op, SrcA, DestA, OpA, [LogicOp], Mask
        b->rt[i].enable = w[0];
        b->rt[i].src = blend_factor(w[k]);
        b->rt[i].dst = blend_factor(w[k + 1]);
        b->rt[i].op = blend_op(w[k + 2]);
        b->rt[i].src_a = blend_factor(w[k + 3]);
        b->rt[i].dst_a = blend_factor(w[k + 4]);
        b->rt[i].op_a = blend_op(w[k + 5]);
        b->rt[i].mask = w[k + 6 + (v1 ? 1 : 0)] & 0xF;
    }
    wr64(out, (u64)(uintptr_t)b);
    RET(S_OK);
}
static void dev_CreateBlendState(RcnCtx *ctx) { blend_state(ctx, 0); }
static void dev_CreateBlendState1(RcnCtx *ctx) { blend_state(ctx, 1); }

static GLenum compare_func(u32 f) {
    static const GLenum map[] = {GL_ALWAYS, GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
    return f < 9 ? map[f] : GL_ALWAYS;
}
static GLenum stencil_op(u32 op) {
    static const GLenum map[] = {GL_KEEP, GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP};
    return op < 9 ? map[op] : GL_KEEP;
}

// CreateDepthStencilState(D3D11_DEPTH_STENCIL_DESC *, **) (52 octets).
static void dev_CreateDepthStencilState(RcnCtx *ctx) {
    u64 d = A1, out = A2;
    if (!d) {
        RET(E_INVALIDARG);
        return;
    }
    if (!out) {
        RET(S_FALSE);
        return;
    }
    D3dDepth *s = state_new(ctx, K_DEPTH, sizeof(D3dDepth), d, 52);
    if (!s) {
        RET(E_OUTOFMEMORY);
        return;
    }
    const u32 *w = PTR(d);
    s->depth_enable = w[0];
    s->depth_write = w[1];
    s->depth_func = compare_func(w[2]);
    s->stencil_enable = w[3];
    s->read_mask = *(u8 *)PTR(d + 16);
    s->write_mask = *(u8 *)PTR(d + 17);
    for (int i = 0; i < 3; i++) s->front[i] = stencil_op(w[5 + i]), s->back[i] = stencil_op(w[9 + i]);
    s->front[3] = compare_func(w[8]);
    s->back[3] = compare_func(w[12]);
    wr64(out, (u64)(uintptr_t)s);
    RET(S_OK);
}

// CreateRasterizerState(D3D11_RASTERIZER_DESC *, **) (40 octets ; 44 et 48 pour les versions 1 et 2).
static void raster_state(RcnCtx *ctx, u32 size) {
    u64 d = A1, out = A2;
    if (!d) {
        RET(E_INVALIDARG);
        return;
    }
    if (!out) {
        RET(S_FALSE);
        return;
    }
    D3dRaster *s = state_new(ctx, K_RASTER, sizeof(D3dRaster), d, size);
    if (!s) {
        RET(E_OUTOFMEMORY);
        return;
    }
    const u32 *w = PTR(d);
    s->fill = w[0];
    s->cull = w[1];
    s->front_ccw = w[2];
    s->bias = (i32)w[3];
    memcpy(&s->bias_clamp, &w[4], 4);
    memcpy(&s->slope_bias, &w[5], 4);
    s->depth_clip = w[6];
    s->scissor = w[7];
    s->multisample = w[8];
    s->line_aa = w[9];
    wr64(out, (u64)(uintptr_t)s);
    RET(S_OK);
}
static void dev_CreateRasterizerState(RcnCtx *ctx) { raster_state(ctx, 40); }
static void dev_CreateRasterizerState1(RcnCtx *ctx) { raster_state(ctx, 44); }
static void dev_CreateRasterizerState2(RcnCtx *ctx) { raster_state(ctx, 48); }

static GLenum address_mode(u32 a) {
    static const GLenum map[] = {GL_REPEAT, GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_BORDER, GL_MIRROR_CLAMP_TO_EDGE};
    GLenum m = a < 6 ? map[a] : GL_REPEAT;
    // OpenGL ES 3.1 : ni bordure ni miroir borné sans extension.
    if (d3d_caps.es && d3d_caps.version < 32 && m == GL_CLAMP_TO_BORDER) m = GL_CLAMP_TO_EDGE;
    if (d3d_caps.es && m == GL_MIRROR_CLAMP_TO_EDGE) m = GL_MIRRORED_REPEAT;
    return m;
}
static void sampler_destroy(D3dObj *o) {
    D3dSampler *s = (D3dSampler *)o;
    if (s->gl && GL(glDeleteSamplers)) GL(glDeleteSamplers)(1, &s->gl);
}

// CreateSamplerState(D3D11_SAMPLER_DESC *, **) (52 octets).
static void dev_CreateSamplerState(RcnCtx *ctx) {
    u64 d = A1, out = A2;
    if (!d) {
        RET(E_INVALIDARG);
        return;
    }
    if (!out) {
        RET(S_FALSE);
        return;
    }
    D3dSampler *s = state_new(ctx, K_SAMPLER, sizeof(D3dSampler), d, 52);
    if (!s) {
        RET(E_OUTOFMEMORY);
        return;
    }
    s->obj.destroy = sampler_destroy;
    const u32 *w = PTR(d);
    s->filter = w[0];
    s->address[0] = w[1], s->address[1] = w[2], s->address[2] = w[3];
    memcpy(&s->lod_bias, &w[4], 4);
    s->max_aniso = w[5];
    s->compare = w[6];
    memcpy(s->border, &w[7], 16);
    memcpy(&s->min_lod, &w[11], 4);
    memcpy(&s->max_lod, &w[12], 4);
    if (d3d_gl_current(ctx)) {
        GL(glGenSamplers)(1, &s->gl);
        // D3D11_FILTER : bits 0 (mip linéaire), 2 (agrandissement), 4 (réduction), 6 (anisotrope), 7 (comparaison).
        u32 f = s->filter;
        int aniso = (f & 0x40) != 0, mip = (f & 1) || aniso, mag = (f & 4) || aniso, min = (f & 0x10) || aniso;
        GLenum min_filter = min ? (mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST) : (mip ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
        GL(glSamplerParameteri)(s->gl, GL_TEXTURE_MIN_FILTER, (GLint)min_filter);
        GL(glSamplerParameteri)(s->gl, GL_TEXTURE_MAG_FILTER, mag ? GL_LINEAR : GL_NEAREST);
        GL(glSamplerParameteri)(s->gl, GL_TEXTURE_WRAP_S, (GLint)address_mode(w[1]));
        GL(glSamplerParameteri)(s->gl, GL_TEXTURE_WRAP_T, (GLint)address_mode(w[2]));
        GL(glSamplerParameteri)(s->gl, GL_TEXTURE_WRAP_R, (GLint)address_mode(w[3]));
        GL(glSamplerParameterf)(s->gl, GL_TEXTURE_MIN_LOD, s->min_lod);
        GL(glSamplerParameterf)(s->gl, GL_TEXTURE_MAX_LOD, s->max_lod > 1000.0f ? 1000.0f : s->max_lod);
        if (!d3d_caps.es) GL(glSamplerParameterf)(s->gl, GL_TEXTURE_LOD_BIAS, s->lod_bias);
        if (!d3d_caps.es || d3d_caps.version >= 32) GL(glSamplerParameterfv)(s->gl, GL_TEXTURE_BORDER_COLOR, s->border);
        if (aniso && d3d_caps.max_anisotropy)
            GL(glSamplerParameterf)(s->gl, GL_TEXTURE_MAX_ANISOTROPY, (GLfloat)(s->max_aniso > 16 ? 16 : s->max_aniso ? s->max_aniso : 1));
        if ((f & 0x180) == 0x80) { // comparaison
            GL(glSamplerParameteri)(s->gl, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            GL(glSamplerParameteri)(s->gl, GL_TEXTURE_COMPARE_FUNC, (GLint)compare_func(s->compare));
        }
        d3d_gl_publish();
        d3d_gl_check("CreateSamplerState");
    }
    wr64(out, (u64)(uintptr_t)s);
    RET(S_OK);
}

// --- Requêtes ---

static void query_destroy(D3dObj *o) {
    D3dQuery *q = (D3dQuery *)o;
    if (q->gl && GL(glDeleteQueries)) GL(glDeleteQueries)(1, &q->gl);
    if (q->fence && GL(glDeleteSync)) GL(glDeleteSync)(q->fence);
}
static void query_GetDataSize(RcnCtx *ctx) {
    static const u32 sizes[] = {0, 8, 16, 8, 8, 16, 32, 8, 8, 8, 16, 16, 16, 16, 16, 16, 16, 16};
    u32 t = THIS(D3dQuery)->type;
    RET(t == 0 ? 0 : t < sizeof sizes / sizeof sizes[0] ? sizes[t] : 8);
}
static void query_GetDesc(RcnCtx *ctx) {
    D3dQuery *q = THIS(D3dQuery);
    if (A1) {
        wr32(A1, q->type);
        wr32(A1 + 4, q->misc);
    }
}
static const D3dMethod query_methods[] = {
    {"GetDataSize", query_GetDataSize}, {"GetDesc", query_GetDesc}, {"GetDesc1", query_GetDesc}, {NULL, NULL},
};
// CreateQuery / CreatePredicate(D3D11_QUERY_DESC *, **)
static void dev_CreateQuery(RcnCtx *ctx) {
    u64 d = A1, out = A2;
    if (!d) {
        RET(E_INVALIDARG);
        return;
    }
    if (!out) {
        RET(S_FALSE);
        return;
    }
    D3dQuery *q = d3d_new(ctx, K_QUERY, sizeof(D3dQuery), query_methods);
    if (!q) {
        RET(E_OUTOFMEMORY);
        return;
    }
    q->obj.device = THIS(D3dDevice);
    q->obj.destroy = query_destroy;
    q->type = rd32(d);
    q->misc = rd32(d + 4);
    wr64(out, (u64)(uintptr_t)q);
    RET(S_OK);
}

// --- Appareil : divers ---

// D3D11_FORMAT_SUPPORT d'un format.
static u32 format_support(u32 dxgi) {
    const D3dFormat *f = d3d_format(dxgi);
    if (!f->internal) {
        // Formats d'index et de sommets seuls.
        return dxgi == 42 || dxgi == 57 ? 0x7 : 0;
    }
    u32 s = 0x1 | 0x2 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x1000 | 0x20000 | 0x100000 | 0x800000;
    if (f->depth) s = 0x20 | 0x80 | 0x10000 | 0x100 | 0x200 | 0x400 | 0x4000000;
    else if (!f->block) {
        s |= 0x4000 | 0x2000 | 0x80000 | 0x40000 | 0x200000;
        if (!f->integer) s |= 0x8000;
        if (f->bytes >= 4 && !f->integer) s |= 0x2000000;
    }
    if (dxgi == 42 || dxgi == 57) s |= 0x4; // index
    return s;
}
// CheckFormatSupport(format, *support)
static void dev_CheckFormatSupport(RcnCtx *ctx) {
    u32 s = format_support((u32)A1);
    if (A2) wr32(A2, s);
    RET(s ? S_OK : E_FAIL);
}
// CheckMultisampleQualityLevels(format, count, *levels) : pas de multi-échantillonnage pour l'instant.
static void dev_CheckMultisampleQualityLevels(RcnCtx *ctx) {
    if (A3) wr32(A3, (u32)A2 == 1 && d3d_format((u32)A1)->internal ? 1 : 0);
    RET(S_OK);
}
static void dev_CheckMultisampleQualityLevels1(RcnCtx *ctx) {
    if (STK(4)) wr32(STK(4), (u32)A2 == 1 && d3d_format((u32)A1)->internal ? 1 : 0);
    RET(S_OK);
}
// CheckFeatureSupport(feature, data, size)
static void dev_CheckFeatureSupport(RcnCtx *ctx) {
    u32 feature = (u32)A1, size = (u32)A3;
    u64 data = A2;
    if (!data) {
        RET(E_INVALIDARG);
        return;
    }
    switch (feature) {
    case 0: // THREADING : créations simultanées, pas de listes de commandes
        memset(PTR(data), 0, size);
        if (size >= 4) wr32(data, 1);
        break;
    case 2: // FORMAT_SUPPORT
        if (size >= 8) wr32(data + 4, format_support(rd32(data)));
        break;
    case 3: // FORMAT_SUPPORT2
        if (size >= 8) wr32(data + 4, 0);
        break;
    case 7: case 9: case 11: // D3D9_OPTIONS, D3D9_SHADOW, D3D9_SIMPLE_INSTANCING
        memset(PTR(data), 0, size);
        if (size >= 4) wr32(data, 1);
        break;
    case 13: // D3D9_OPTIONS1
        for (u32 i = 0; i + 4 <= size; i += 4) wr32(data + i, 1);
        break;
    default: memset(PTR(data), 0, size); break;
    }
    RET(S_OK);
}
static void dev_GetFeatureLevel(RcnCtx *ctx) { RET(THIS(D3dDevice)->feature_level); }
static void dev_GetCreationFlags(RcnCtx *ctx) { RET(THIS(D3dDevice)->flags); }
static void dev_Ok(RcnCtx *ctx) { RET(S_OK); }
static void dev_Zero(RcnCtx *ctx) { RET(0); }
static void dev_GetImmediateContext(RcnCtx *ctx) {
    D3dDevice *d = THIS(D3dDevice);
    if (A1) wr64(A1, 0);
    d3d_out(A1, &d->immediate->obj);
}
static void dev_CheckCounterInfo(RcnCtx *ctx) {
    if (A1) memset(PTR(A1), 0, 12);
}
static void dev_Unsupported(RcnCtx *ctx) { RET(E_NOTIMPL); }
static void dev_RegisterDeviceRemovedEvent(RcnCtx *ctx) {
    if (A2) wr32(A2, 1);
    RET(S_OK);
}
static void dev_UnregisterDeviceRemoved(RcnCtx *ctx) { (void)ctx; }

static const D3dMethod device_methods[] = {
    {"CreateBuffer", dev_CreateBuffer}, {"CreateTexture1D", dev_CreateTexture1D}, {"CreateTexture2D", dev_CreateTexture2D},
    {"CreateTexture3D", dev_CreateTexture3D}, {"CreateTexture2D1", dev_CreateTexture2D}, {"CreateTexture3D1", dev_CreateTexture3D},
    {"CreateShaderResourceView", dev_CreateShaderResourceView}, {"CreateShaderResourceView1", dev_CreateShaderResourceView},
    {"CreateUnorderedAccessView", dev_CreateUnorderedAccessView}, {"CreateUnorderedAccessView1", dev_CreateUnorderedAccessView},
    {"CreateRenderTargetView", dev_CreateRenderTargetView}, {"CreateRenderTargetView1", dev_CreateRenderTargetView},
    {"CreateDepthStencilView", dev_CreateDepthStencilView}, {"CreateInputLayout", dev_CreateInputLayout},
    {"CreateVertexShader", dev_CreateVertexShader}, {"CreateGeometryShader", dev_CreateGeometryShader},
    {"CreateGeometryShaderWithStreamOutput", dev_CreateGeometryShaderWithStreamOutput},
    {"CreatePixelShader", dev_CreatePixelShader}, {"CreateHullShader", dev_CreateHullShader},
    {"CreateDomainShader", dev_CreateDomainShader}, {"CreateComputeShader", dev_CreateComputeShader},
    {"CreateClassLinkage", dev_CreateClassLinkage}, {"CreateBlendState", dev_CreateBlendState},
    {"CreateBlendState1", dev_CreateBlendState1}, {"CreateDepthStencilState", dev_CreateDepthStencilState},
    {"CreateRasterizerState", dev_CreateRasterizerState}, {"CreateRasterizerState1", dev_CreateRasterizerState1},
    {"CreateRasterizerState2", dev_CreateRasterizerState2}, {"CreateSamplerState", dev_CreateSamplerState},
    {"CreateQuery", dev_CreateQuery}, {"CreateQuery1", dev_CreateQuery}, {"CreatePredicate", dev_CreateQuery},
    {"CreateCounter", dev_Unsupported}, {"CreateDeferredContext", dev_Unsupported},
    {"CreateDeferredContext1", dev_Unsupported}, {"CreateDeferredContext2", dev_Unsupported},
    {"CreateDeferredContext3", dev_Unsupported}, {"OpenSharedResource", dev_Unsupported},
    {"CheckFormatSupport", dev_CheckFormatSupport}, {"CheckMultisampleQualityLevels", dev_CheckMultisampleQualityLevels},
    {"CheckMultisampleQualityLevels1", dev_CheckMultisampleQualityLevels1}, {"CheckCounterInfo", dev_CheckCounterInfo},
    {"CheckFeatureSupport", dev_CheckFeatureSupport}, {"GetFeatureLevel", dev_GetFeatureLevel},
    {"GetCreationFlags", dev_GetCreationFlags}, {"GetDeviceRemovedReason", dev_Ok},
    {"GetImmediateContext", dev_GetImmediateContext}, {"GetImmediateContext1", dev_GetImmediateContext},
    {"GetImmediateContext2", dev_GetImmediateContext}, {"GetImmediateContext3", dev_GetImmediateContext},
    {"SetExceptionMode", dev_Ok}, {"GetExceptionMode", dev_Zero},
    {"RegisterDeviceRemovedEvent", dev_RegisterDeviceRemovedEvent}, {"UnregisterDeviceRemoved", dev_UnregisterDeviceRemoved},
    {NULL, NULL},
};

// --- ID3D11Multithread, ID3DUserDefinedAnnotation ---

static void mt_Enter(RcnCtx *ctx) {
    D3dDevice *d = THIS(D3dObj)->device;
    if (d && d->immediate) pthread_mutex_lock(&d->immediate->lock);
}
static void mt_Leave(RcnCtx *ctx) {
    D3dDevice *d = THIS(D3dObj)->device;
    if (d && d->immediate) pthread_mutex_unlock(&d->immediate->lock);
}
static void mt_SetMultithreadProtected(RcnCtx *ctx) { RET(1); }
static void mt_GetMultithreadProtected(RcnCtx *ctx) { RET(1); }
static const D3dMethod multithread_methods[] = {
    {"Enter", mt_Enter}, {"Leave", mt_Leave}, {"SetMultithreadProtected", mt_SetMultithreadProtected},
    {"GetMultithreadProtected", mt_GetMultithreadProtected}, {NULL, NULL},
};
static void an_Event(RcnCtx *ctx) { RET(0xFFFFFFFFu); }
static void an_SetMarker(RcnCtx *ctx) { (void)ctx; }
static void an_GetStatus(RcnCtx *ctx) { RET(0); }
static const D3dMethod annotation_methods[] = {
    {"BeginEvent", an_Event}, {"EndEvent", an_Event}, {"SetMarker", an_SetMarker}, {"GetStatus", an_GetStatus}, {NULL, NULL},
};

// Interfaces servies par un objet compagnon.
D3dObj *d3d_query_extra(RcnCtx *ctx, D3dObj *o, u64 iid) {
    const char *name = d3d_iid_name(iid);
    if (!name) return NULL;
    D3dDevice *dev = o->device ? o->device : o->kind == K_DEVICE ? (D3dDevice *)o : NULL;
    if ((o->kind == K_DEVICE || o->kind == K_CONTEXT) && dev) {
        if (!strncmp(name, "IDXGIDevice", 11) || !strcmp(name, "IDXGIObject")) return dev->dxgi_device;
        if (!strcmp(name, "ID3D11Multithread")) {
            if (!dev->multithread) {
                dev->multithread = d3d_new(ctx, K_MULTITHREAD, sizeof(D3dObj), multithread_methods);
                if (dev->multithread) dev->multithread->device = dev;
            }
            return dev->multithread;
        }
    }
    if (o->kind == K_CONTEXT && !strcmp(name, "ID3DUserDefinedAnnotation")) {
        static D3dObj *annotation;
        if (!annotation) annotation = d3d_new(ctx, K_ANNOTATION, sizeof(D3dObj), annotation_methods);
        return annotation;
    }
    if (o->kind == K_DXGIDEVICE && dev && !strncmp(name, "ID3D11Device", 12)) return &dev->obj;
    if ((o->kind == K_TEX2D || o->kind == K_TEX1D || o->kind == K_TEX3D || o->kind == K_BUFFER) &&
        (!strncmp(name, "IDXGISurface", 12) || !strncmp(name, "IDXGIResource", 13))) {
        if (!o->alias) {
            o->alias = d3d_surface_new(ctx, (D3dRes *)o);
            if (o->alias) o->alias->refs = 0; // la référence vient de QueryInterface
        }
        return o->alias;
    }
    if (o->kind == K_SURFACE && o->parent && d3d_has_iface(o->parent, iid)) return o->parent;
    return NULL;
}

D3dDevice *d3d_device_create(RcnCtx *ctx, D3dObj *adapter, u32 flags, u32 level) {
    D3dDevice *d = d3d_new(ctx, K_DEVICE, sizeof(D3dDevice), device_methods);
    if (!d) return NULL;
    d->obj.device = d;
    d->adapter = adapter;
    d->flags = flags;
    d->feature_level = level;
    d->immediate = d3d_context_create(ctx, d);
    d->dxgi_device = d3d_dxgi_device_new(ctx, d);
    if (!d->immediate || !d->dxgi_device || !d3d_gl_current(ctx)) return NULL;
    return d;
}

// --- Fonctions de d3d11.dll ---

// Niveau rendu : le plus élevé demandé jusqu'à 11_0.
static u32 pick_level(u64 levels, u32 count) {
    if (!levels || !count) return 0xb000;
    u32 best = 0;
    for (u32 i = 0; i < count; i++) {
        u32 l = rd32(levels + 4ull * i);
        if (l <= 0xb000 && l > best) best = l;
    }
    return best;
}

static D3dDevice *the_device;

static int make_device(RcnCtx *ctx, u64 adapter, u32 flags, u64 levels, u32 count, u64 out_device, u64 out_level, u64 out_context) {
    u32 level = pick_level(levels, count);
    if (out_device) wr64(out_device, 0);
    if (out_context) wr64(out_context, 0);
    if (!level) return 0;
    if (out_level) wr32(out_level, level);
    if (!out_device && !out_context) return 1; // vérification seule
    D3dDevice *d = d3d_device_create(ctx, adapter ? OBJ(adapter) : d3d_adapter_default(ctx), flags, level);
    if (!d) return -1;
    if (!the_device) the_device = d;
    if (out_device) wr64(out_device, (u64)(uintptr_t)d);
    if (out_context) d3d_out(out_context, &d->immediate->obj);
    d3d_log("appareil créé : niveau 0x%x, options 0x%x", level, flags);
    return 1;
}

// D3D11CreateDevice(adapter, type, software, flags, levels, count, sdk, **device, *level, **context)
static void d3d_D3D11CreateDevice(RcnCtx *ctx) {
    int r = make_device(ctx, A0, (u32)A3, STK(4), (u32)STK(5), STK(7), STK(8), STK(9));
    RET(r > 0 ? S_OK : r == 0 ? E_INVALIDARG : E_FAIL);
}
// D3D11CreateDeviceAndSwapChain(adapter, type, software, flags, levels, count, sdk, swapdesc, **swapchain,
//                               **device, *level, **context)
static void d3d_D3D11CreateDeviceAndSwapChain(RcnCtx *ctx) {
    u64 out_device = STK(9), out_sc = STK(8), desc = STK(7);
    u64 local = 0;
    int r = make_device(ctx, A0, (u32)A3, STK(4), (u32)STK(5), out_device ? out_device : (u64)(uintptr_t)&local, STK(10), STK(11));
    if (r <= 0) {
        RET(r == 0 ? E_INVALIDARG : E_FAIL);
        return;
    }
    u64 device = out_device ? rd64(out_device) : local;
    if (out_sc && desc) {
        u32 hr = d3d_dxgi_create_swapchain(ctx, (D3dDevice *)OBJ(device), desc, out_sc);
        if (hr != S_OK) {
            RET(hr);
            return;
        }
    }
    if (!out_device) d3d_release(OBJ(local));
    RET(S_OK);
}

const RcnShim rcn_d3d11_exports[] = {
    {"D3D11CreateDevice", d3d_D3D11CreateDevice, 0},
    {"D3D11CreateDeviceAndSwapChain", d3d_D3D11CreateDeviceAndSwapChain, 0},
};
const size_t rcn_d3d11_exports_count = sizeof rcn_d3d11_exports / sizeof rcn_d3d11_exports[0];
