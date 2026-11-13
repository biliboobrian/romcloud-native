// RomCloud Native : contexte immédiat Direct3D 11 (voir rcn_d3d11.h) — liaisons, dessin, Map, copies.
//
// Les objets liés gardent une référence, comme sous Direct3D. Au dessin, l'état lié est appliqué à
// OpenGL : programme (paire de nuanceurs traduits, liée une fois), framebuffer des cibles, tampons de
// constantes, textures et échantillonneurs par paire, attributs de sommets, états fixes.
#include <stdio.h>
#include <stdlib.h>

#include "rcn_d3d11.h"
#include "rcn_dxbc.h"

#define SCRATCH_UNIT 47

// --- Références ---

static void set_ref(D3dObj **slot, D3dObj *o) {
    if (*slot == o) return;
    d3d_addref(o);
    D3dObj *old = *slot;
    *slot = o;
    d3d_release(old);
}
#define SET(slot, o) set_ref((D3dObj **)&(slot), (D3dObj *)(o))

static D3dContext *C(RcnCtx *ctx) { return THIS(D3dContext); }
#define LOCK(c) pthread_mutex_lock(&(c)->lock)
#define UNLOCK(c) pthread_mutex_unlock(&(c)->lock)

// --- Liaisons par étape ---

static void set_cbs(RcnCtx *ctx, u32 stage, u64 first_const, u64 num_consts) {
    D3dContext *c = C(ctx);
    u32 start = (u32)A1, n = (u32)A2;
    u64 list = A3;
    LOCK(c);
    for (u32 i = 0; i < n && start + i < D3D_CB_SLOTS; i++) {
        D3dRes *b = list ? (D3dRes *)OBJ(rd64(list + 8ull * i)) : NULL;
        SET(c->st[stage].cb[start + i], b);
        c->st[stage].cb_first[start + i] = first_const ? rd32(first_const + 4ull * i) : 0;
        c->st[stage].cb_count[start + i] = num_consts ? rd32(num_consts + 4ull * i) : 0;
    }
    UNLOCK(c);
}
static void set_srvs(RcnCtx *ctx, u32 stage) {
    D3dContext *c = C(ctx);
    u32 start = (u32)A1, n = (u32)A2;
    u64 list = A3;
    LOCK(c);
    for (u32 i = 0; i < n && start + i < D3D_SRV_SLOTS; i++) SET(c->st[stage].srv[start + i], list ? OBJ(rd64(list + 8ull * i)) : NULL);
    UNLOCK(c);
}
static void set_samplers(RcnCtx *ctx, u32 stage) {
    D3dContext *c = C(ctx);
    u32 start = (u32)A1, n = (u32)A2;
    u64 list = A3;
    LOCK(c);
    for (u32 i = 0; i < n && start + i < D3D_SAMPLER_SLOTS; i++) SET(c->st[stage].sampler[start + i], list ? OBJ(rd64(list + 8ull * i)) : NULL);
    UNLOCK(c);
}
static void set_shader(RcnCtx *ctx, u32 stage) {
    D3dContext *c = C(ctx);
    LOCK(c);
    SET(c->st[stage].shader, OBJ(A1));
    UNLOCK(c);
}
static void get_cbs(RcnCtx *ctx, u32 stage) {
    D3dContext *c = C(ctx);
    u32 start = (u32)A1, n = (u32)A2;
    for (u32 i = 0; i < n && A3; i++) {
        wr64(A3 + 8ull * i, 0);
        if (start + i < D3D_CB_SLOTS) d3d_out(A3 + 8ull * i, (D3dObj *)c->st[stage].cb[start + i]);
    }
}
static void get_srvs(RcnCtx *ctx, u32 stage) {
    D3dContext *c = C(ctx);
    u32 start = (u32)A1, n = (u32)A2;
    for (u32 i = 0; i < n && A3; i++) {
        wr64(A3 + 8ull * i, 0);
        if (start + i < D3D_SRV_SLOTS) d3d_out(A3 + 8ull * i, (D3dObj *)c->st[stage].srv[start + i]);
    }
}
static void get_samplers(RcnCtx *ctx, u32 stage) {
    D3dContext *c = C(ctx);
    u32 start = (u32)A1, n = (u32)A2;
    for (u32 i = 0; i < n && A3; i++) {
        wr64(A3 + 8ull * i, 0);
        if (start + i < D3D_SAMPLER_SLOTS) d3d_out(A3 + 8ull * i, (D3dObj *)c->st[stage].sampler[start + i]);
    }
}
static void get_shader(RcnCtx *ctx, u32 stage) {
    D3dContext *c = C(ctx);
    if (A1) {
        wr64(A1, 0);
        d3d_out(A1, (D3dObj *)c->st[stage].shader);
    }
    if (A3) wr32(A3, 0); // pas d'instances de classes
}

#define STAGE_FUNCS(P, n)                                                                                   \
    static void P##SetConstantBuffers(RcnCtx *ctx) { set_cbs(ctx, n, 0, 0); }                               \
    static void P##SetConstantBuffers1(RcnCtx *ctx) { set_cbs(ctx, n, STK(4), STK(5)); }                    \
    static void P##SetShaderResources(RcnCtx *ctx) { set_srvs(ctx, n); }                                    \
    static void P##SetSamplers(RcnCtx *ctx) { set_samplers(ctx, n); }                                       \
    static void P##SetShader(RcnCtx *ctx) { set_shader(ctx, n); }                                           \
    static void P##GetConstantBuffers(RcnCtx *ctx) { get_cbs(ctx, n); }                                     \
    static void P##GetShaderResources(RcnCtx *ctx) { get_srvs(ctx, n); }                                    \
    static void P##GetSamplers(RcnCtx *ctx) { get_samplers(ctx, n); }                                       \
    static void P##GetShader(RcnCtx *ctx) { get_shader(ctx, n); }
STAGE_FUNCS(VS, 0)
STAGE_FUNCS(PS, 1)
STAGE_FUNCS(GS, 2)
STAGE_FUNCS(HS, 3)
STAGE_FUNCS(DS, 4)
STAGE_FUNCS(CS, 5)
#define STAGE_ENTRIES(P)                                                                                    \
    {#P "SetConstantBuffers", P##SetConstantBuffers}, {#P "SetConstantBuffers1", P##SetConstantBuffers1},   \
        {#P "SetShaderResources", P##SetShaderResources}, {#P "SetSamplers", P##SetSamplers},               \
        {#P "SetShader", P##SetShader}, {#P "GetConstantBuffers", P##GetConstantBuffers},                   \
        {#P "GetConstantBuffers1", P##GetConstantBuffers}, {#P "GetShaderResources", P##GetShaderResources}, \
        {#P "GetSamplers", P##GetSamplers}, {#P "GetShader", P##GetShader},

// --- Assemblage ---

static void IASetInputLayout(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    LOCK(c);
    SET(c->layout, OBJ(A1));
    UNLOCK(c);
}
// IASetVertexBuffers(start, n, **buffers, *strides, *offsets)
static void IASetVertexBuffers(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    u32 start = (u32)A1, n = (u32)A2;
    u64 bufs = A3, strides = STK(4), offsets = STK(5);
    LOCK(c);
    for (u32 i = 0; i < n && start + i < 32; i++) {
        SET(c->vb[start + i], bufs ? OBJ(rd64(bufs + 8ull * i)) : NULL);
        c->vb_stride[start + i] = strides ? rd32(strides + 4ull * i) : 0;
        c->vb_offset[start + i] = offsets ? rd32(offsets + 4ull * i) : 0;
    }
    UNLOCK(c);
}
static void IASetIndexBuffer(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    LOCK(c);
    SET(c->ib, OBJ(A1));
    c->ib_format = (u32)A2;
    c->ib_offset = (u32)A3;
    UNLOCK(c);
}
static void IASetPrimitiveTopology(RcnCtx *ctx) { C(ctx)->topology = (u32)A1; }
static void IAGetInputLayout(RcnCtx *ctx) {
    if (A1) {
        wr64(A1, 0);
        d3d_out(A1, (D3dObj *)C(ctx)->layout);
    }
}
static void IAGetVertexBuffers(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    u32 start = (u32)A1, n = (u32)A2;
    for (u32 i = 0; i < n && start + i < 32; i++) {
        if (A3) {
            wr64(A3 + 8ull * i, 0);
            d3d_out(A3 + 8ull * i, (D3dObj *)c->vb[start + i]);
        }
        if (STK(4)) wr32(STK(4) + 4ull * i, c->vb_stride[start + i]);
        if (STK(5)) wr32(STK(5) + 4ull * i, c->vb_offset[start + i]);
    }
}
static void IAGetIndexBuffer(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    if (A1) {
        wr64(A1, 0);
        d3d_out(A1, (D3dObj *)c->ib);
    }
    if (A2) wr32(A2, c->ib_format);
    if (A3) wr32(A3, c->ib_offset);
}
static void IAGetPrimitiveTopology(RcnCtx *ctx) {
    if (A1) wr32(A1, C(ctx)->topology);
}

// --- Sortie ---

// OMSetRenderTargets(n, **rtv, dsv)
static void set_targets(D3dContext *c, u32 n, u64 rtvs, D3dView *dsv) {
    for (u32 i = 0; i < 8; i++) SET(c->rtv[i], i < n && rtvs ? OBJ(rd64(rtvs + 8ull * i)) : NULL);
    SET(c->dsv, dsv);
}
static void OMSetRenderTargets(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    LOCK(c);
    set_targets(c, (u32)A1, A2, (D3dView *)OBJ(A3));
    UNLOCK(c);
}
// OMSetRenderTargetsAndUnorderedAccessViews(n, rtvs, dsv, uav_start, n_uav, uavs, counts)
static void OMSetRenderTargetsAndUnorderedAccessViews(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    u32 n = (u32)A1, uav_start = (u32)A3 ? (u32)STK(4) : 0, n_uav = (u32)STK(5);
    (void)uav_start;
    LOCK(c);
    if (n != 0xFFFFFFFFu) set_targets(c, n, A2, (D3dView *)OBJ(A3));
    if (n_uav != 0xFFFFFFFFu) {
        u32 s = (u32)STK(4);
        u64 uavs = STK(6);
        for (u32 i = 0; i < n_uav && s + i < D3D_UAV_SLOTS; i++) SET(c->uav[s + i], uavs ? OBJ(rd64(uavs + 8ull * i)) : NULL);
    }
    UNLOCK(c);
}
static void OMSetBlendState(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    LOCK(c);
    SET(c->blend, OBJ(A1));
    if (A2) memcpy(c->blend_factor, PTR(A2), 16);
    else c->blend_factor[0] = c->blend_factor[1] = c->blend_factor[2] = c->blend_factor[3] = 1.0f;
    c->sample_mask = (u32)A3;
    UNLOCK(c);
}
static void OMSetDepthStencilState(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    LOCK(c);
    SET(c->depth, OBJ(A1));
    c->stencil_ref = (u32)A2;
    UNLOCK(c);
}
static void OMGetRenderTargets(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    u32 n = (u32)A1;
    for (u32 i = 0; i < n && A2; i++) {
        wr64(A2 + 8ull * i, 0);
        if (i < 8) d3d_out(A2 + 8ull * i, (D3dObj *)c->rtv[i]);
    }
    if (A3) {
        wr64(A3, 0);
        d3d_out(A3, (D3dObj *)c->dsv);
    }
}
static void OMGetBlendState(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    if (A1) {
        wr64(A1, 0);
        d3d_out(A1, (D3dObj *)c->blend);
    }
    if (A2) memcpy(PTR(A2), c->blend_factor, 16);
    if (A3) wr32(A3, c->sample_mask);
}
static void OMGetDepthStencilState(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    if (A1) {
        wr64(A1, 0);
        d3d_out(A1, (D3dObj *)c->depth);
    }
    if (A2) wr32(A2, c->stencil_ref);
}

// --- Rastérisation ---

static void RSSetState(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    LOCK(c);
    SET(c->raster, OBJ(A1));
    UNLOCK(c);
}
static void RSSetViewports(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    u32 n = (u32)A1;
    c->viewports = n > 16 ? 16 : n;
    for (u32 i = 0; i < c->viewports && A2; i++) memcpy(c->viewport[i], PTR(A2 + 24ull * i), 24);
}
static void RSSetScissorRects(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    u32 n = (u32)A1;
    c->scissors = n > 16 ? 16 : n;
    for (u32 i = 0; i < c->scissors && A2; i++) memcpy(c->scissor[i], PTR(A2 + 16ull * i), 16);
}
static void RSGetState(RcnCtx *ctx) {
    if (A1) {
        wr64(A1, 0);
        d3d_out(A1, (D3dObj *)C(ctx)->raster);
    }
}
static void RSGetViewports(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    if (!A1) return;
    if (A2) for (u32 i = 0; i < c->viewports && i < rd32(A1); i++) memcpy(PTR(A2 + 24ull * i), c->viewport[i], 24);
    wr32(A1, c->viewports);
}
static void RSGetScissorRects(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    if (!A1) return;
    if (A2) for (u32 i = 0; i < c->scissors && i < rd32(A1); i++) memcpy(PTR(A2 + 16ull * i), c->scissor[i], 16);
    wr32(A1, c->scissors);
}

// --- Framebuffers (par thread) ---

static volatile u32 fb_epoch_global = 1;
void d3d_fb_forget(D3dView *view) {
    // Une cible détruite : les framebuffers des threads seront refaits (objets propres à chaque contexte).
    if (view && (view->obj.kind == K_RTV || view->obj.kind == K_DSV)) __atomic_add_fetch(&fb_epoch_global, 1, __ATOMIC_RELEASE);
}

static u64 view_key(const D3dView *v) {
    if (!v || !v->res || !v->res->gl) return 0;
    return (u64)v->res->gl | (u64)(v->mip & 0xFF) << 32 | (u64)(v->layer & 0xFFFF) << 40 | (u64)(v->layers > 1) << 56 | 1ull << 63;
}
static void attach(GLenum att, const D3dView *v) {
    const D3dRes *r = v->res;
    switch (r->target) {
    case GL_TEXTURE_CUBE_MAP:
        GL(glFramebufferTexture2D)(GL_DRAW_FRAMEBUFFER, att, d3d_cube_face_target(v->layer % 6), r->gl, v->mip);
        break;
    case GL_TEXTURE_2D_MULTISAMPLE: GL(glFramebufferTexture2D)(GL_DRAW_FRAMEBUFFER, att, r->target, r->gl, 0); break;
    default:
        if (v->layers > 1 && GL(glFramebufferTexture) && r->target != GL_TEXTURE_3D) GL(glFramebufferTexture)(GL_DRAW_FRAMEBUFFER, att, r->gl, v->mip);
        else GL(glFramebufferTextureLayer)(GL_DRAW_FRAMEBUFFER, att, r->gl, v->mip, v->layer);
        break;
    }
}
GLuint d3d_framebuffer(D3dView *const *rtv, u32 count, D3dView *dsv) {
    D3dThreadGl *t = d3d_thread_gl();
    u32 epoch = __atomic_load_n(&fb_epoch_global, __ATOMIC_ACQUIRE);
    if (t->fb_epoch != epoch) {
        for (u32 i = 0; i < 64; i++) {
            if (t->fbs[i].fb) GL(glDeleteFramebuffers)(1, &t->fbs[i].fb);
            memset(&t->fbs[i], 0, sizeof t->fbs[i]);
        }
        t->fb_epoch = epoch;
    }
    u64 key[9] = {0};
    for (u32 i = 0; i < count && i < 8; i++) key[i] = view_key(rtv[i]);
    key[8] = view_key(dsv);
    for (u32 i = 0; i < 64; i++) {
        if (t->fbs[i].fb && !memcmp(t->fbs[i].key, key, sizeof key)) return t->fbs[i].fb;
    }
    u32 slot = t->fb_next++ % 64;
    if (t->fbs[slot].fb) GL(glDeleteFramebuffers)(1, &t->fbs[slot].fb);
    GLuint fb;
    GL(glGenFramebuffers)(1, &fb);
    GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, fb);
    GLenum bufs[8];
    u32 nb = 0;
    for (u32 i = 0; i < 8; i++) {
        if (key[i]) {
            attach(GL_COLOR_ATTACHMENT0 + i, rtv[i]);
            bufs[i] = GL_COLOR_ATTACHMENT0 + i;
            nb = i + 1;
        } else {
            bufs[i] = GL_NONE;
        }
    }
    if (key[8]) attach(d3d_format(d3d_storage_format(dsv->res->format, 1))->depth == 2 ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, dsv);
    GL(glDrawBuffers)(nb ? (GLsizei)nb : 1, bufs);
    GLenum status = GL(glCheckFramebufferStatus)(GL_DRAW_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) d3d_log("framebuffer incomplet (0x%x), %u cibles%s", status, nb, key[8] ? " + profondeur" : "");
    memcpy(t->fbs[slot].key, key, sizeof key);
    t->fbs[slot].fb = fb;
    return fb;
}

// --- État appliqué à OpenGL (par thread) ---

typedef struct {
    int valid;
    GLuint fb, program;
    const D3dBlend *blend;
    const D3dDepth *depth;
    const D3dRaster *raster;
    float blend_factor[4];
    u32 stencil_ref;
    int scissor_on;
} Applied;
static __thread Applied applied;
static void invalidate(void) { applied.valid = 0; }

static void apply_blend(D3dContext *c) {
    const D3dBlend *b = c->blend;
    if (applied.valid && applied.blend == b && !memcmp(applied.blend_factor, c->blend_factor, 16)) return;
    applied.blend = b;
    memcpy(applied.blend_factor, c->blend_factor, 16);
    int indexed = GL(glBlendFuncSeparatei) && GL(glEnablei) && GL(glColorMaski);
    for (u32 i = 0; i < (indexed ? 8u : 1u); i++) {
        u32 enable = b ? b->rt[i].enable : 0, mask = b ? b->rt[i].mask : 0xF;
        if (indexed) {
            if (enable) GL(glEnablei)(GL_BLEND, i);
            else GL(glDisablei)(GL_BLEND, i);
            if (enable) {
                GL(glBlendFuncSeparatei)(i, b->rt[i].src, b->rt[i].dst, b->rt[i].src_a, b->rt[i].dst_a);
                GL(glBlendEquationSeparatei)(i, b->rt[i].op, b->rt[i].op_a);
            }
            GL(glColorMaski)(i, mask & 1, (mask >> 1) & 1, (mask >> 2) & 1, (mask >> 3) & 1);
        } else {
            if (enable) GL(glEnable)(GL_BLEND);
            else GL(glDisable)(GL_BLEND);
            if (enable) {
                GL(glBlendFuncSeparate)(b->rt[0].src, b->rt[0].dst, b->rt[0].src_a, b->rt[0].dst_a);
                GL(glBlendEquationSeparate)(b->rt[0].op, b->rt[0].op_a);
            }
            GL(glColorMask)(mask & 1, (mask >> 1) & 1, (mask >> 2) & 1, (mask >> 3) & 1);
        }
    }
    if (b && b->alpha_to_coverage) GL(glEnable)(GL_SAMPLE_ALPHA_TO_COVERAGE);
    else GL(glDisable)(GL_SAMPLE_ALPHA_TO_COVERAGE);
    GL(glBlendColor)(c->blend_factor[0], c->blend_factor[1], c->blend_factor[2], c->blend_factor[3]);
}
static void apply_depth(D3dContext *c) {
    const D3dDepth *d = c->depth;
    if (applied.valid && applied.depth == d && applied.stencil_ref == c->stencil_ref) return;
    applied.depth = d;
    applied.stencil_ref = c->stencil_ref;
    // État par défaut : profondeur LESS écrite, pas de stencil.
    u32 enable = d ? d->depth_enable : 1, write = d ? d->depth_write : 1;
    if (enable) GL(glEnable)(GL_DEPTH_TEST);
    else GL(glDisable)(GL_DEPTH_TEST);
    GL(glDepthFunc)(d ? d->depth_func : GL_LESS);
    GL(glDepthMask)(enable && write ? 1 : 0);
    if (d && d->stencil_enable) {
        GL(glEnable)(GL_STENCIL_TEST);
        GL(glStencilFuncSeparate)(GL_FRONT, d->front[3], (GLint)c->stencil_ref, d->read_mask);
        GL(glStencilFuncSeparate)(GL_BACK, d->back[3], (GLint)c->stencil_ref, d->read_mask);
        GL(glStencilOpSeparate)(GL_FRONT, d->front[0], d->front[1], d->front[2]);
        GL(glStencilOpSeparate)(GL_BACK, d->back[0], d->back[1], d->back[2]);
        GL(glStencilMask)(d->write_mask);
    } else {
        GL(glDisable)(GL_STENCIL_TEST);
    }
}
static void apply_raster(D3dContext *c) {
    const D3dRaster *r = c->raster;
    if (applied.valid && applied.raster == r) return;
    applied.raster = r;
    u32 cull = r ? r->cull : 3;
    if (cull == 1) {
        GL(glDisable)(GL_CULL_FACE);
    } else {
        GL(glEnable)(GL_CULL_FACE);
        GL(glCullFace)(cull == 2 ? GL_FRONT : GL_BACK);
    }
    // Image retournée : le sens des faces s'inverse.
    GL(glFrontFace)(r && r->front_ccw ? GL_CW : GL_CCW);
    if (r && (r->bias || r->slope_bias != 0.0f)) {
        GL(glEnable)(GL_POLYGON_OFFSET_FILL);
        GL(glPolygonOffset)(r->slope_bias, (GLfloat)r->bias);
    } else {
        GL(glDisable)(GL_POLYGON_OFFSET_FILL);
    }
    if (!d3d_caps.es) {
        if (r && !r->depth_clip) GL(glEnable)(GL_DEPTH_CLAMP);
        else GL(glDisable)(GL_DEPTH_CLAMP);
        if (GL(glPolygonMode)) GL(glPolygonMode)(GL_FRONT_AND_BACK, r && r->fill == 2 ? GL_LINE : GL_FILL);
    }
    applied.scissor_on = r && r->scissor;
}
static void apply_viewport(D3dContext *c) {
    if (c->viewports) {
        const float *v = c->viewport[0];
        GL(glViewport)((GLint)v[0], (GLint)v[1], (GLsizei)(v[2] + 0.5f), (GLsizei)(v[3] + 0.5f));
        GL(glDepthRangef)(v[4], v[5]);
    }
    if (applied.scissor_on && c->scissors) {
        const i32 *s = c->scissor[0];
        GL(glEnable)(GL_SCISSOR_TEST);
        GL(glScissor)(s[0], s[1], s[2] - s[0] > 0 ? s[2] - s[0] : 0, s[3] - s[1] > 0 ? s[3] - s[1] : 0);
    } else {
        GL(glDisable)(GL_SCISSOR_TEST);
    }
}

// --- Programmes ---

struct D3dProgram {
    u64 vs, ps, gs; // empreintes des nuanceurs
    GLuint gl;
    int ok;
};
static struct {
    pthread_mutex_t lock;
    D3dProgram *list;
    u32 count, cap;
} programs = {.lock = PTHREAD_MUTEX_INITIALIZER};

static void dump_source(const char *what, const char *src, const char *log) {
    d3d_log("%s :\n%s", what, log);
    if (d3d_log_level > 1 && src) d3d_log("source :\n%s", src);
}

void d3d_shader_compile(D3dShader *s) {
    if (s->gl) return;
    s->gl = ~0u;
    if (!s->dx) s->dx = dxbc_translate(s->code, s->size, d3d_caps.es);
    if (!s->dx || !s->dx->glsl) return;
    static const GLenum types[] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, GL_GEOMETRY_SHADER, 0, 0, GL_COMPUTE_SHADER};
    if (!types[s->stage]) return;
    GLuint sh = GL(glCreateShader)(types[s->stage]);
    const GLchar *src = s->dx->glsl;
    GL(glShaderSource)(sh, 1, &src, NULL);
    GL(glCompileShader)(sh);
    GLint ok = 0;
    GL(glGetShaderiv)(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = "";
        GL(glGetShaderInfoLog)(sh, sizeof log, NULL, log);
        char what[96];
        snprintf(what, sizeof what, "nuanceur %016llx (étape %u) refusé", (unsigned long long)s->hash, s->stage);
        dump_source(what, s->dx->glsl, log);
        GL(glDeleteShader)(sh);
        return;
    }
    s->gl = sh;
}

static GLuint program_for(D3dShader *vs, D3dShader *ps, D3dShader *gs) {
    u64 kv = vs ? vs->hash : 0, kp = ps ? ps->hash : 0, kg = gs ? gs->hash : 0;
    pthread_mutex_lock(&programs.lock);
    for (u32 i = 0; i < programs.count; i++) {
        D3dProgram *p = &programs.list[i];
        if (p->vs == kv && p->ps == kp && p->gs == kg) {
            GLuint gl = p->ok ? p->gl : 0;
            pthread_mutex_unlock(&programs.lock);
            return gl;
        }
    }
    pthread_mutex_unlock(&programs.lock);
    D3dProgram p = {kv, kp, kg, 0, 0};
    if (vs) d3d_shader_compile(vs);
    if (ps) d3d_shader_compile(ps);
    if (gs) d3d_shader_compile(gs);
    int usable = vs && vs->gl && vs->gl != ~0u && (!ps || (ps->gl && ps->gl != ~0u)) && (!gs || (gs->gl && gs->gl != ~0u));
    if (usable) {
        p.gl = GL(glCreateProgram)();
        GL(glAttachShader)(p.gl, vs->gl);
        if (ps) GL(glAttachShader)(p.gl, ps->gl);
        if (gs) GL(glAttachShader)(p.gl, gs->gl);
        GL(glLinkProgram)(p.gl);
        GLint ok = 0;
        GL(glGetProgramiv)(p.gl, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048] = "";
            GL(glGetProgramInfoLog)(p.gl, sizeof log, NULL, log);
            d3d_log("programme %016llx + %016llx refusé à l'édition des liens :\n%s", (unsigned long long)kv, (unsigned long long)kp, log);
            GL(glDeleteProgram)(p.gl);
            p.gl = 0;
        } else {
            p.ok = 1;
        }
    }
    pthread_mutex_lock(&programs.lock);
    if (programs.count == programs.cap) {
        u32 cap = programs.cap ? programs.cap * 2 : 256;
        D3dProgram *list = realloc(programs.list, cap * sizeof *list);
        if (list) programs.list = list, programs.cap = cap;
    }
    if (programs.count < programs.cap) programs.list[programs.count++] = p;
    pthread_mutex_unlock(&programs.lock);
    return p.ok ? p.gl : 0;
}

// --- Ressources des nuanceurs ---

static GLenum view_target(const D3dView *v) { return v->res->target; }

static void bind_stage(D3dContext *c, u32 stage) {
    D3dStageState *st = &c->st[stage];
    D3dShader *s = st->shader;
    if (!s || !s->dx) return;
    DxbcShader *dx = s->dx;
    for (u32 slot = 0; slot < D3D_CB_SLOTS; slot++) {
        if (!(dx->cb_mask & (1u << slot))) continue;
        D3dRes *b = st->cb[slot];
        GLuint binding = D3D_CB_BINDING(stage, slot);
        if (!b || !b->gl) {
            GL(glBindBufferBase)(GL_UNIFORM_BUFFER, binding, 0);
            continue;
        }
        if (st->cb_count[slot]) {
            u32 off = st->cb_first[slot] * 16, size = st->cb_count[slot] * 16;
            if (off + size > b->size) size = b->size > off ? b->size - off : 0;
            GL(glBindBufferRange)(GL_UNIFORM_BUFFER, binding, b->gl, off, size);
        } else {
            GL(glBindBufferBase)(GL_UNIFORM_BUFFER, binding, b->gl);
        }
    }
    for (u32 i = 0; i < dx->pair_count; i++) {
        const DxbcPair *p = &dx->pairs[i];
        D3dView *v = p->t < D3D_SRV_SLOTS ? st->srv[p->t] : NULL;
        D3dSampler *smp = p->s >= 0 && p->s < D3D_SAMPLER_SLOTS ? st->sampler[p->s] : NULL;
        GL(glActiveTexture)(GL_TEXTURE0 + p->unit);
        if (v && v->res && v->res->dim == DIM_BUFFER) {
            D3dRes *b = v->res;
            if (!b->tbo && GL(glTexBuffer)) GL(glGenTextures)(1, &b->tbo);
            GL(glBindTexture)(GL_TEXTURE_BUFFER, b->tbo);
            const D3dFormat *f = d3d_format(v->format);
            if (b->tbo && f->internal && GL(glTexBufferRange)) {
                u32 off = v->first * f->bytes, size = v->count * f->bytes;
                if (off + size > b->size) size = b->size > off ? b->size - off : 0;
                GL(glTexBufferRange)(GL_TEXTURE_BUFFER, f->internal, b->gl, off, size);
            }
        } else if (v && v->res && v->res->gl) {
            GL(glBindTexture)(view_target(v), v->res->gl);
        }
        GL(glBindSampler)(p->unit, smp ? smp->gl : 0);
    }
}

// --- Attributs de sommets ---

static GLenum gl_topology(u32 t, int *ok) {
    *ok = 1;
    switch (t) {
    case 1: return GL_POINTS;
    case 2: return GL_LINES;
    case 3: return GL_LINE_STRIP;
    case 4: return GL_TRIANGLES;
    case 5: return GL_TRIANGLE_STRIP;
    case 10: return GL_LINES_ADJACENCY;
    case 11: return GL_LINE_STRIP_ADJACENCY;
    case 12: return GL_TRIANGLES_ADJACENCY;
    case 13: return GL_TRIANGLE_STRIP_ADJACENCY;
    default: *ok = 0; return GL_TRIANGLES;
    }
}

static __thread u32 attribs_enabled;

// Attributs : chaque élément de la disposition relié à l'entrée du nuanceur de même sémantique.
static void bind_attributes(D3dContext *c, const DxbcShader *vs, i32 base_vertex, u32 base_instance) {
    u32 enabled = 0;
    const D3dLayout *l = c->layout;
    for (u32 i = 0; l && i < l->count; i++) {
        const D3dElement *e = &l->el[i];
        const DxbcSig *in = NULL;
        for (u32 k = 0; k < vs->in_count; k++) {
            if (vs->in[k].index == e->index && !strcasecmp(vs->in[k].name, e->semantic) && !vs->in[k].sysval) {
                in = &vs->in[k];
                break;
            }
        }
        if (!in) continue;
        D3dRes *b = c->vb[e->slot];
        const D3dFormat *f = d3d_format(e->format);
        if (!b || !b->gl || !f->components) continue;
        u32 loc = in->reg;
        u32 stride = c->vb_stride[e->slot];
        i64 off = (i64)c->vb_offset[e->slot] + e->offset;
        off += e->instanced ? (i64)base_instance * stride / (e->step ? e->step : 1) : (i64)base_vertex * stride;
        GL(glBindBuffer)(GL_ARRAY_BUFFER, b->gl);
        GLint comps = f->components;
        GLenum type = f->type;
        if (type == GL_UNSIGNED_INT_2_10_10_10_REV || type == GL_UNSIGNED_INT_10F_11F_11F_REV) comps = 4;
        if (f->integer) GL(glVertexAttribIPointer)(loc, comps, type, (GLsizei)stride, (const void *)(uintptr_t)off);
        else GL(glVertexAttribPointer)(loc, comps, type, f->normalized, (GLsizei)stride, (const void *)(uintptr_t)off);
        GL(glVertexAttribDivisor)(loc, e->instanced ? (e->step ? e->step : 1) : 0);
        enabled |= 1u << loc;
    }
    for (u32 loc = 0; loc < 32; loc++) {
        u32 bit = 1u << loc;
        if ((enabled & bit) && !(attribs_enabled & bit)) GL(glEnableVertexAttribArray)(loc);
        if (!(enabled & bit) && (attribs_enabled & bit)) GL(glDisableVertexAttribArray)(loc);
    }
    attribs_enabled = enabled;
}

// Prépare un dessin : 0 s'il faut l'abandonner (nuanceur non traduit…).
static GLenum prepare(RcnCtx *ctx, D3dContext *c, i32 base_vertex, u32 base_instance, int emulate_base) {
    if (!d3d_gl_current(ctx)) return 0;
    d3d_gl_mark_draw();
    D3dShader *vs = c->st[0].shader, *ps = c->st[1].shader, *gs = c->st[2].shader;
    if (!vs) return 0;
    GLuint program = program_for(vs, ps, gs);
    if (!program) return 0;
    int ok;
    GLenum mode = gl_topology(c->topology, &ok);
    if (!ok) return 0;
    GLuint fb = d3d_framebuffer(c->rtv, 8, c->dsv);
    if (!applied.valid || applied.fb != fb) {
        GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, fb);
        applied.fb = fb;
    }
    if (!applied.valid || applied.program != program) {
        GL(glUseProgram)(program);
        applied.program = program;
    }
    apply_blend(c);
    apply_depth(c);
    apply_raster(c);
    applied.valid = 1;
    apply_viewport(c);
    bind_stage(c, 0);
    bind_stage(c, 1);
    if (gs) bind_stage(c, 2);
    bind_attributes(c, vs->dx, emulate_base ? base_vertex : 0, emulate_base ? base_instance : 0);
    return mode == 0 ? GL_POINTS + 0x10000 : mode; // GL_POINTS vaut 0 : distingué de l'abandon
}
#define MODE(m) ((m) & 0xFFFF)

static void draw(RcnCtx *ctx, u32 count, u32 instances, u32 start, u32 start_instance) {
    D3dContext *c = C(ctx);
    if (!count || !instances) return;
    LOCK(c);
    int native_base = d3d_caps.has_base_instance;
    GLenum mode = prepare(ctx, c, 0, start_instance, !native_base);
    if (mode) {
        if (start_instance && native_base) GL(glDrawArraysInstancedBaseInstance)(MODE(mode), (GLint)start, (GLsizei)count, (GLsizei)instances, start_instance);
        else GL(glDrawArraysInstanced)(MODE(mode), (GLint)start, (GLsizei)count, (GLsizei)instances);
        d3d_gl_check("Draw");
    }
    UNLOCK(c);
}
static void draw_indexed(RcnCtx *ctx, u32 count, u32 instances, u32 start, i32 base, u32 start_instance) {
    D3dContext *c = C(ctx);
    if (!count || !instances) return;
    LOCK(c);
    if (!c->ib || !c->ib->gl) {
        UNLOCK(c);
        return;
    }
    int native_base = d3d_caps.has_base_instance || (!start_instance && GL(glDrawElementsInstancedBaseVertex));
    GLenum mode = prepare(ctx, c, base, start_instance, !native_base);
    if (mode) {
        GL(glBindBuffer)(GL_ELEMENT_ARRAY_BUFFER, c->ib->gl);
        u32 isize = c->ib_format == 42 ? 4 : 2;
        GLenum type = isize == 4 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        const void *at = (const void *)(uintptr_t)(c->ib_offset + start * isize);
        if (native_base && start_instance)
            GL(glDrawElementsInstancedBaseVertexBaseInstance)(MODE(mode), (GLsizei)count, type, at, (GLsizei)instances, base, start_instance);
        else if (native_base)
            GL(glDrawElementsInstancedBaseVertex)(MODE(mode), (GLsizei)count, type, at, (GLsizei)instances, base);
        else
            GL(glDrawElementsInstanced)(MODE(mode), (GLsizei)count, type, at, (GLsizei)instances);
        d3d_gl_check("DrawIndexed");
    }
    UNLOCK(c);
}
static void Draw(RcnCtx *ctx) { draw(ctx, (u32)A1, 1, (u32)A2, 0); }
static void DrawInstanced(RcnCtx *ctx) { draw(ctx, (u32)A1, (u32)A2, (u32)A3, (u32)STK(4)); }
static void DrawIndexed(RcnCtx *ctx) { draw_indexed(ctx, (u32)A1, 1, (u32)A2, (i32)A3, 0); }
static void DrawIndexedInstanced(RcnCtx *ctx) { draw_indexed(ctx, (u32)A1, (u32)A2, (u32)A3, (i32)STK(4), (u32)STK(5)); }
// Dessins indirects : arguments lus dans le tampon (copie du GPU relue).
static void read_buffer(D3dRes *b, u32 offset, void *out, u32 size) {
    memset(out, 0, size);
    if (!b || !b->gl || offset + size > b->size) return;
    GL(glBindBuffer)(GL_COPY_READ_BUFFER, b->gl);
    void *p = GL(glMapBufferRange)(GL_COPY_READ_BUFFER, offset, size, GL_MAP_READ_BIT);
    if (p) {
        memcpy(out, p, size);
        GL(glUnmapBuffer)(GL_COPY_READ_BUFFER);
    }
    GL(glBindBuffer)(GL_COPY_READ_BUFFER, 0);
}
static void DrawInstancedIndirect(RcnCtx *ctx) {
    u32 a[4];
    if (!d3d_gl_current(ctx)) return;
    read_buffer((D3dRes *)OBJ(A1), (u32)A2, a, 16);
    draw(ctx, a[0], a[1], a[2], a[3]);
}
static void DrawIndexedInstancedIndirect(RcnCtx *ctx) {
    u32 a[5];
    if (!d3d_gl_current(ctx)) return;
    read_buffer((D3dRes *)OBJ(A1), (u32)A2, a, 20);
    draw_indexed(ctx, a[0], a[1], a[2], (i32)a[3], a[4]);
}
static void Dispatch(RcnCtx *ctx) {
    static int told;
    if (!told++) d3d_log("Dispatch (calcul) pas encore fourni");
}

// --- Map ---

enum { MAP_READ = 1, MAP_WRITE = 2, MAP_READ_WRITE = 3, MAP_WRITE_DISCARD = 4, MAP_WRITE_NO_OVERWRITE = 5 };

// Textures dynamiques : mémoire temporaire par sous-ressource envoyée à Unmap.
typedef struct {
    D3dRes *res;
    u32 sub;
    u8 *mem;
    u32 row, slice;
} TexMap;
static TexMap tex_maps[16];

// Map(resource, sub, type, flags, D3D11_MAPPED_SUBRESOURCE *)
static void Map(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    D3dRes *r = (D3dRes *)OBJ(A1);
    u32 sub = (u32)A2, type = (u32)A3;
    u64 out = STK(5);
    if (!r || !out) {
        RET(E_INVALIDARG);
        return;
    }
    memset(PTR(out), 0, 16);
    LOCK(c);
    u8 *p = NULL;
    u32 row = 0, slice = 0;
    if (r->dim == DIM_BUFFER) {
        if (r->mem) {
            p = r->mem;
        } else if (d3d_gl_current(ctx) && r->gl) {
            GLbitfield bits = GL_MAP_WRITE_BIT;
            if (type == MAP_WRITE_DISCARD) bits |= GL_MAP_INVALIDATE_BUFFER_BIT;
            else if (type == MAP_WRITE_NO_OVERWRITE) bits |= GL_MAP_UNSYNCHRONIZED_BIT;
            else if (type == MAP_READ || type == MAP_READ_WRITE) bits = GL_MAP_READ_BIT | (type == MAP_READ_WRITE ? GL_MAP_WRITE_BIT : 0);
            GL(glBindBuffer)(GL_COPY_WRITE_BUFFER, r->gl);
            p = GL(glMapBufferRange)(GL_COPY_WRITE_BUFFER, 0, r->size, bits);
            GL(glBindBuffer)(GL_COPY_WRITE_BUFFER, 0);
        }
        row = slice = r->size;
    } else {
        const D3dFormat *f = d3d_format(r->format);
        u32 mip = sub % r->mips, rows;
        u32 w = d3d_mip_size(r->width, mip), h = d3d_mip_size(r->height, mip);
        if (f->block) row = ((w + 3) / 4) * f->bytes, rows = (h + 3) / 4;
        else row = w * f->bytes, rows = h;
        slice = row * rows;
        if (r->mem && r->sub_offset) {
            p = r->mem + r->sub_offset[sub < d3d_sub_count(r) ? sub : 0];
        } else {
            for (int i = 0; i < 16; i++) {
                if (!tex_maps[i].res) {
                    u32 depth = r->dim == DIM_TEX3D ? d3d_mip_size(r->depth, mip) : 1;
                    tex_maps[i] = (TexMap){r, sub, malloc((size_t)slice * depth), row, slice};
                    p = tex_maps[i].mem;
                    break;
                }
            }
        }
    }
    UNLOCK(c);
    if (!p) {
        RET(E_OUTOFMEMORY);
        return;
    }
    wr64(out, (u64)(uintptr_t)p);
    wr32(out + 8, row);
    wr32(out + 12, slice);
    RET(S_OK);
}
static void Unmap(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    D3dRes *r = (D3dRes *)OBJ(A1);
    u32 sub = (u32)A2;
    if (!r) return;
    LOCK(c);
    if (r->dim == DIM_BUFFER) {
        if (!r->mem && r->gl && d3d_gl_current(ctx)) {
            GL(glBindBuffer)(GL_COPY_WRITE_BUFFER, r->gl);
            GL(glUnmapBuffer)(GL_COPY_WRITE_BUFFER);
            GL(glBindBuffer)(GL_COPY_WRITE_BUFFER, 0);
        }
        r->version++;
    } else {
        for (int i = 0; i < 16; i++) {
            if (tex_maps[i].res == r && tex_maps[i].sub == sub) {
                if (d3d_gl_current(ctx)) d3d_res_upload(r, sub, NULL, tex_maps[i].mem, tex_maps[i].row, tex_maps[i].slice);
                free(tex_maps[i].mem);
                memset(&tex_maps[i], 0, sizeof tex_maps[i]);
                break;
            }
        }
    }
    UNLOCK(c);
}

// UpdateSubresource(dst, sub, D3D11_BOX *, data, row_pitch, depth_pitch)
static void UpdateSubresource(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    D3dRes *r = (D3dRes *)OBJ(A1);
    u32 sub = (u32)A2;
    u64 box = A3, data = STK(4);
    u32 row = (u32)STK(5), slice = (u32)STK(6);
    if (!r || !data) return;
    LOCK(c);
    if (d3d_gl_current(ctx)) {
        if (r->dim == DIM_BUFFER) {
            u32 lo = box ? rd32(box) : 0, hi = box ? rd32(box + 12) : r->size;
            if (hi > r->size) hi = r->size;
            if (hi > lo) {
                if (r->mem) memcpy(r->mem + lo, PTR(data), hi - lo);
                if (r->gl) {
                    GL(glBindBuffer)(GL_COPY_WRITE_BUFFER, r->gl);
                    GL(glBufferSubData)(GL_COPY_WRITE_BUFFER, lo, hi - lo, PTR(data));
                    GL(glBindBuffer)(GL_COPY_WRITE_BUFFER, 0);
                }
            }
            r->version++;
        } else {
            d3d_res_upload(r, sub, box ? PTR(box) : NULL, PTR(data), row, slice);
        }
        d3d_gl_publish();
    }
    UNLOCK(c);
}

// --- Copies ---

// Relit un niveau (couche) d'une texture du GPU dans `out` (lignes serrées).
static void read_texture(D3dRes *r, u32 sub, u8 *out) {
    const D3dFormat *f = d3d_format(d3d_storage_format(r->format, (r->bind & BIND_DEPTH_STENCIL) != 0));
    if (!r->gl || f->block || f->depth) return;
    u32 mip = sub % r->mips, layer = sub / r->mips;
    u32 w = d3d_mip_size(r->width, mip), h = d3d_mip_size(r->height, mip);
    D3dThreadGl *t = d3d_thread_gl();
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, t->read_fb);
    if (r->target == GL_TEXTURE_CUBE_MAP) GL(glFramebufferTexture2D)(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, d3d_cube_face_target(layer % 6), r->gl, mip);
    else GL(glFramebufferTextureLayer)(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, r->gl, mip, layer);
    GL(glReadBuffer)(GL_COLOR_ATTACHMENT0);
    GL(glReadPixels)(0, 0, (GLsizei)w, (GLsizei)h, f->format, f->type, out);
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, 0);
    if (f->swap_rb == 1 || f->swap_rb == 3) {
        for (size_t i = 0; i + 3 < (size_t)w * h * 4; i += 4) {
            u8 x = out[i];
            out[i] = out[i + 2];
            out[i + 2] = x;
        }
    }
}

static void copy_buffer(D3dRes *dst, u32 doff, D3dRes *src, u32 soff, u32 size) {
    if (soff + size > src->size || doff + size > dst->size) return;
    if (src->mem && dst->mem) {
        memmove(dst->mem + doff, src->mem + soff, size);
    } else if (src->mem) { // transfert → GPU
        GL(glBindBuffer)(GL_COPY_WRITE_BUFFER, dst->gl);
        GL(glBufferSubData)(GL_COPY_WRITE_BUFFER, doff, size, src->mem + soff);
    } else if (dst->mem) { // GPU → transfert
        read_buffer(src, soff, dst->mem + doff, size);
    } else {
        GL(glBindBuffer)(GL_COPY_READ_BUFFER, src->gl);
        GL(glBindBuffer)(GL_COPY_WRITE_BUFFER, dst->gl);
        GL(glCopyBufferSubData)(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, soff, doff, size);
        GL(glBindBuffer)(GL_COPY_READ_BUFFER, 0);
    }
    GL(glBindBuffer)(GL_COPY_WRITE_BUFFER, 0);
    dst->version++;
}

static void copy_texture(D3dRes *dst, u32 dsub, u32 dx, u32 dy, u32 dz, D3dRes *src, u32 ssub, const u32 *box) {
    u32 smip = ssub % src->mips, slayer = ssub / src->mips, dmip = dsub % dst->mips, dlayer = dsub / dst->mips;
    u32 sx = 0, sy = 0, sz = 0, w = d3d_mip_size(src->width, smip), h = d3d_mip_size(src->height, smip),
        d = src->dim == DIM_TEX3D ? d3d_mip_size(src->depth, smip) : 1;
    if (box) sx = box[0], sy = box[1], sz = box[2], w = box[3] - box[0], h = box[4] - box[1], d = box[5] - box[2];
    if (!w || !h || !d) return;
    const D3dFormat *f = d3d_format(src->format);
    if (src->mem && src->sub_offset) { // transfert → GPU (ou transfert)
        u32 row = f->block ? ((d3d_mip_size(src->width, smip) + 3) / 4) * f->bytes : d3d_mip_size(src->width, smip) * f->bytes;
        u32 rows = f->block ? (d3d_mip_size(src->height, smip) + 3) / 4 : d3d_mip_size(src->height, smip);
        const u8 *p = src->mem + src->sub_offset[ssub] + (size_t)sz * row * rows + (size_t)(f->block ? sy / 4 : sy) * row +
                      (size_t)(f->block ? sx / 4 : sx) * f->bytes;
        if (dst->mem && dst->sub_offset) {
            u32 drow = f->block ? ((d3d_mip_size(dst->width, dmip) + 3) / 4) * f->bytes : d3d_mip_size(dst->width, dmip) * f->bytes;
            u8 *q = dst->mem + dst->sub_offset[dsub] + (size_t)(f->block ? dy / 4 : dy) * drow + (size_t)(f->block ? dx / 4 : dx) * f->bytes;
            u32 n = f->block ? (h + 3) / 4 : h, bytes = f->block ? ((w + 3) / 4) * f->bytes : w * f->bytes;
            for (u32 j = 0; j < n; j++) memcpy(q + (size_t)j * drow, p + (size_t)j * row, bytes);
        } else {
            u32 b[6] = {dx, dy, dz, dx + w, dy + h, dz + d};
            d3d_res_upload(dst, dsub, b, p, row, row * rows);
        }
        return;
    }
    if (dst->mem && dst->sub_offset) { // GPU → transfert : niveau entier relu
        u32 sw = d3d_mip_size(src->width, smip), sh = d3d_mip_size(src->height, smip);
        u8 *tmp = malloc((size_t)sw * sh * (f->bytes ? f->bytes : 4));
        if (!tmp) return;
        read_texture(src, ssub, tmp);
        u32 drow = d3d_mip_size(dst->width, dmip) * f->bytes;
        u8 *q = dst->mem + dst->sub_offset[dsub];
        for (u32 j = 0; j < h && dy + j < d3d_mip_size(dst->height, dmip); j++)
            memcpy(q + (size_t)(dy + j) * drow + (size_t)dx * f->bytes, tmp + ((size_t)(sy + j) * sw + sx) * f->bytes, (size_t)w * f->bytes);
        free(tmp);
        return;
    }
    if (!src->gl || !dst->gl) return;
    if (d3d_caps.has_copy_image) {
        GLenum st = src->target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP : src->target;
        GLenum dt = dst->target;
        GL(glCopyImageSubData)(src->gl, st, smip, sx, sy, src->dim == DIM_TEX3D ? sz : slayer, dst->gl, dt, dmip, dx, dy,
                               dst->dim == DIM_TEX3D ? dz : dlayer, w, h, d);
        return;
    }
    // Sans glCopyImageSubData : copie par framebuffers (couleur ou profondeur).
    D3dThreadGl *t = d3d_thread_gl();
    const D3dFormat *sf = d3d_format(d3d_storage_format(src->format, (src->bind & BIND_DEPTH_STENCIL) != 0));
    GLenum att = sf->depth == 2 ? GL_DEPTH_STENCIL_ATTACHMENT : sf->depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
    GLbitfield mask = sf->depth == 2 ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT : sf->depth ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT;
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, t->read_fb);
    GL(glFramebufferTextureLayer)(GL_READ_FRAMEBUFFER, att, src->gl, smip, slayer);
    GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, t->draw_fb);
    GL(glFramebufferTextureLayer)(GL_DRAW_FRAMEBUFFER, att, dst->gl, dmip, dlayer);
    GL(glDisable)(GL_SCISSOR_TEST);
    GL(glBlitFramebuffer)(sx, sy, sx + w, sy + h, dx, dy, dx + w, dy + h, mask, GL_NEAREST);
    GL(glFramebufferTextureLayer)(GL_READ_FRAMEBUFFER, att, 0, 0, 0);
    GL(glFramebufferTextureLayer)(GL_DRAW_FRAMEBUFFER, att, 0, 0, 0);
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, 0);
    invalidate();
}

// CopyResource(dst, src)
static void CopyResource(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    D3dRes *dst = (D3dRes *)OBJ(A1), *src = (D3dRes *)OBJ(A2);
    if (!dst || !src) return;
    LOCK(c);
    if (d3d_gl_current(ctx)) {
        if (dst->dim == DIM_BUFFER) copy_buffer(dst, 0, src, 0, dst->size < src->size ? dst->size : src->size);
        else
            for (u32 s = 0; s < d3d_sub_count(src) && s < d3d_sub_count(dst); s++) copy_texture(dst, s, 0, 0, 0, src, s, NULL);
    }
    UNLOCK(c);
}
// CopySubresourceRegion(dst, dsub, x, y, z, src, ssub, box) : dst rdx, dsub r8, x r9, puis la pile.
static void CopySubresourceRegion(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    D3dRes *dst = (D3dRes *)OBJ(A1), *src = (D3dRes *)OBJ(STK(6));
    u32 dsub = (u32)A2, x = (u32)A3, y = (u32)STK(4), z = (u32)STK(5), ssub = (u32)STK(7);
    u64 box = STK(8);
    if (!dst || !src) return;
    LOCK(c);
    if (d3d_gl_current(ctx)) {
        if (dst->dim == DIM_BUFFER) {
            u32 lo = box ? rd32(box) : 0, hi = box ? rd32(box + 12) : src->size;
            if (hi > lo) copy_buffer(dst, x, src, lo, hi - lo);
        } else {
            copy_texture(dst, dsub, x, y, z, src, ssub, box ? PTR(box) : NULL);
        }
    }
    UNLOCK(c);
}

// --- Effacements ---

static void ClearRenderTargetView(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    D3dView *v = (D3dView *)OBJ(A1);
    if (!v || !A2) return;
    LOCK(c);
    if (d3d_gl_current(ctx) && v->res && v->res->gl) {
        d3d_gl_mark_draw();
        D3dView *list[1] = {v};
        GLuint fb = d3d_framebuffer(list, 1, NULL);
        GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, fb);
        GL(glDisable)(GL_SCISSOR_TEST);
        if (GL(glColorMaski)) GL(glColorMaski)(0, 1, 1, 1, 1);
        else GL(glColorMask)(1, 1, 1, 1);
        float color[4];
        memcpy(color, PTR(A2), 16);
        const D3dFormat *f = d3d_format(v->format);
        if (f->integer) {
            GLuint u[4] = {(GLuint)color[0], (GLuint)color[1], (GLuint)color[2], (GLuint)color[3]};
            GL(glClearBufferuiv)(GL_COLOR, 0, u);
        } else {
            GL(glClearBufferfv)(GL_COLOR, 0, color);
        }
        invalidate();
        d3d_gl_check("ClearRenderTargetView");
    }
    UNLOCK(c);
}
// ClearDepthStencilView(dsv, flags, depth (xmm2), stencil)
static void ClearDepthStencilView(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    D3dView *v = (D3dView *)OBJ(A1);
    u32 flags = (u32)A2;
    float depth = ctx->xmm[2].f[0];
    u8 stencil = (u8)A3;
    if (!v) return;
    LOCK(c);
    if (d3d_gl_current(ctx) && v->res && v->res->gl) {
        d3d_gl_mark_draw();
        GLuint fb = d3d_framebuffer(NULL, 0, v);
        GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, fb);
        GL(glDisable)(GL_SCISSOR_TEST);
        GL(glDepthMask)(1);
        GL(glStencilMask)(0xFF);
        int has_stencil = d3d_format(d3d_storage_format(v->res->format, 1))->depth == 2;
        if ((flags & 3) == 3 && has_stencil) {
            GL(glClearBufferfi)(GL_DEPTH_STENCIL, 0, depth, stencil);
        } else {
            if (flags & 1) GL(glClearBufferfv)(GL_DEPTH, 0, &depth);
            if ((flags & 2) && has_stencil) {
                GLint s = stencil;
                GL(glClearBufferuiv)(GL_STENCIL, 0, (const GLuint *)&s);
            }
        }
        invalidate();
        d3d_gl_check("ClearDepthStencilView");
    }
    UNLOCK(c);
}
static void ClearUnorderedAccessView(RcnCtx *ctx) { (void)ctx; }
static void GenerateMips(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    D3dView *v = (D3dView *)OBJ(A1);
    if (!v || !v->res || !v->res->gl) return;
    LOCK(c);
    if (d3d_gl_current(ctx)) {
        GL(glActiveTexture)(GL_TEXTURE0 + SCRATCH_UNIT);
        GL(glBindTexture)(v->res->target, v->res->gl);
        GL(glGenerateMipmap)(v->res->target);
        GL(glBindTexture)(v->res->target, 0);
        GL(glActiveTexture)(GL_TEXTURE0);
    }
    UNLOCK(c);
}
// ResolveSubresource(dst, dsub, src, ssub, format) : copie par framebuffers.
static void ResolveSubresource(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    D3dRes *dst = (D3dRes *)OBJ(A1), *src = (D3dRes *)OBJ(A3);
    if (!dst || !src) return;
    LOCK(c);
    if (d3d_gl_current(ctx)) copy_texture(dst, (u32)A2, 0, 0, 0, src, (u32)STK(4), NULL);
    UNLOCK(c);
}

// --- Requêtes ---

static void Begin(RcnCtx *ctx) {
    D3dQuery *q = (D3dQuery *)OBJ(A1);
    if (q && q->obj.kind == K_QUERY) q->active = 1, q->ended = 0;
}
static void End(RcnCtx *ctx) {
    D3dQuery *q = (D3dQuery *)OBJ(A1);
    if (!q || q->obj.kind != K_QUERY) return;
    q->active = 0;
    q->ended = 1;
    if (q->type == 0 && d3d_gl_current(ctx)) { // EVENT : barrière
        if (q->fence) GL(glDeleteSync)(q->fence);
        q->fence = GL(glFenceSync)(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        GL(glFlush)();
    }
}
// GetData(async, data, size, flags) : résultats immédiats (horloges, compteurs plausibles).
static void GetData(RcnCtx *ctx) {
    D3dQuery *q = (D3dQuery *)OBJ(A1);
    u64 data = A2;
    u32 size = (u32)A3;
    if (!q) {
        RET(E_INVALIDARG);
        return;
    }
    if (q->type == 0 && q->fence && d3d_gl_current(ctx)) {
        GLenum r = GL(glClientWaitSync)(q->fence, (u32)STK(4) & 1 ? 0 : GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        if (r == 0x911B /* GL_TIMEOUT_EXPIRED */) {
            RET(S_FALSE);
            return;
        }
    }
    if (data && size) {
        memset(PTR(data), 0, size);
        switch (q->type) {
        case 0: wr32(data, 1); break;                        // EVENT : fini
        case 1: if (size >= 8) wr64(data, 1000); break;      // OCCLUSION : visible
        case 2: if (size >= 8) wr64(data, rcn_now_ns()); break; // TIMESTAMP (ns)
        case 3: if (size >= 16) wr64(data, 1000000000ull), wr32(data + 8, 0); break; // TIMESTAMP_DISJOINT : 1 GHz, cohérent
        case 5: case 7: wr32(data, 1); break;                // OCCLUSION_PREDICATE, SO_OVERFLOW
        default: break;
        }
    }
    RET(S_OK);
}
static void SetPredication(RcnCtx *ctx) { (void)ctx; }

// --- Divers ---

static void ClearState(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    LOCK(c);
    for (u32 s = 0; s < D3D_STAGES; s++) {
        SET(c->st[s].shader, NULL);
        for (u32 i = 0; i < D3D_CB_SLOTS; i++) SET(c->st[s].cb[i], NULL), c->st[s].cb_first[i] = c->st[s].cb_count[i] = 0;
        for (u32 i = 0; i < D3D_SRV_SLOTS; i++) SET(c->st[s].srv[i], NULL);
        for (u32 i = 0; i < D3D_SAMPLER_SLOTS; i++) SET(c->st[s].sampler[i], NULL);
    }
    SET(c->layout, NULL);
    for (u32 i = 0; i < 32; i++) SET(c->vb[i], NULL), c->vb_stride[i] = c->vb_offset[i] = 0;
    SET(c->ib, NULL);
    for (u32 i = 0; i < 8; i++) SET(c->rtv[i], NULL);
    SET(c->dsv, NULL);
    for (u32 i = 0; i < D3D_UAV_SLOTS; i++) SET(c->uav[i], NULL), SET(c->cs_uav[i], NULL);
    SET(c->blend, NULL);
    SET(c->depth, NULL);
    SET(c->raster, NULL);
    c->topology = 0;
    c->viewports = c->scissors = 0;
    c->sample_mask = 0xFFFFFFFFu;
    c->blend_factor[0] = c->blend_factor[1] = c->blend_factor[2] = c->blend_factor[3] = 1.0f;
    UNLOCK(c);
}
static void Flush(RcnCtx *ctx) {
    if (d3d_gl_current(ctx)) GL(glFlush)();
}
static void GetType(RcnCtx *ctx) { RET(0); } // D3D11_DEVICE_CONTEXT_IMMEDIATE
static void GetContextFlags(RcnCtx *ctx) { RET(0); }
static void Nothing(RcnCtx *ctx) { (void)ctx; }
static void NotImpl(RcnCtx *ctx) { RET(E_NOTIMPL); }
static void CSSetUnorderedAccessViews(RcnCtx *ctx) {
    D3dContext *c = C(ctx);
    u32 start = (u32)A1, n = (u32)A2;
    LOCK(c);
    for (u32 i = 0; i < n && start + i < D3D_UAV_SLOTS; i++) SET(c->cs_uav[start + i], A3 ? OBJ(rd64(A3 + 8ull * i)) : NULL);
    UNLOCK(c);
}
static void CopySubresourceRegion1(RcnCtx *ctx) { CopySubresourceRegion(ctx); }
static void UpdateSubresource1(RcnCtx *ctx) { UpdateSubresource(ctx); }
static void an_Event(RcnCtx *ctx) { (void)ctx; }

static const D3dMethod context_methods[] = {
    STAGE_ENTRIES(VS) STAGE_ENTRIES(PS) STAGE_ENTRIES(GS) STAGE_ENTRIES(HS) STAGE_ENTRIES(DS) STAGE_ENTRIES(CS)
    {"IASetInputLayout", IASetInputLayout}, {"IASetVertexBuffers", IASetVertexBuffers},
    {"IASetIndexBuffer", IASetIndexBuffer}, {"IASetPrimitiveTopology", IASetPrimitiveTopology},
    {"IAGetInputLayout", IAGetInputLayout}, {"IAGetVertexBuffers", IAGetVertexBuffers},
    {"IAGetIndexBuffer", IAGetIndexBuffer}, {"IAGetPrimitiveTopology", IAGetPrimitiveTopology},
    {"OMSetRenderTargets", OMSetRenderTargets},
    {"OMSetRenderTargetsAndUnorderedAccessViews", OMSetRenderTargetsAndUnorderedAccessViews},
    {"OMSetBlendState", OMSetBlendState}, {"OMSetDepthStencilState", OMSetDepthStencilState},
    {"OMGetRenderTargets", OMGetRenderTargets}, {"OMGetBlendState", OMGetBlendState},
    {"OMGetDepthStencilState", OMGetDepthStencilState}, {"RSSetState", RSSetState}, {"RSSetViewports", RSSetViewports},
    {"RSSetScissorRects", RSSetScissorRects}, {"RSGetState", RSGetState}, {"RSGetViewports", RSGetViewports},
    {"RSGetScissorRects", RSGetScissorRects}, {"Draw", Draw}, {"DrawIndexed", DrawIndexed},
    {"DrawInstanced", DrawInstanced}, {"DrawIndexedInstanced", DrawIndexedInstanced},
    {"DrawInstancedIndirect", DrawInstancedIndirect}, {"DrawIndexedInstancedIndirect", DrawIndexedInstancedIndirect},
    {"DrawAuto", Nothing}, {"Dispatch", Dispatch}, {"DispatchIndirect", Dispatch}, {"Map", Map}, {"Unmap", Unmap},
    {"UpdateSubresource", UpdateSubresource}, {"UpdateSubresource1", UpdateSubresource1},
    {"CopyResource", CopyResource}, {"CopySubresourceRegion", CopySubresourceRegion},
    {"CopySubresourceRegion1", CopySubresourceRegion1}, {"CopyStructureCount", Nothing},
    {"ClearRenderTargetView", ClearRenderTargetView}, {"ClearDepthStencilView", ClearDepthStencilView},
    {"ClearUnorderedAccessViewUint", ClearUnorderedAccessView}, {"ClearUnorderedAccessViewFloat", ClearUnorderedAccessView},
    {"ClearView", Nothing}, {"GenerateMips", GenerateMips}, {"SetResourceMinLOD", Nothing},
    {"ResolveSubresource", ResolveSubresource}, {"Begin", Begin}, {"End", End}, {"GetData", GetData},
    {"SetPredication", SetPredication}, {"SOSetTargets", Nothing}, {"ClearState", ClearState}, {"Flush", Flush},
    {"Flush1", Flush}, {"GetType", GetType}, {"GetContextFlags", GetContextFlags}, {"FinishCommandList", NotImpl},
    {"ExecuteCommandList", Nothing}, {"CSSetUnorderedAccessViews", CSSetUnorderedAccessViews},
    {"DiscardResource", Nothing}, {"DiscardView", Nothing}, {"DiscardView1", Nothing},
    {"BeginEventInt", an_Event}, {"EndEvent", an_Event}, {"SetMarkerInt", an_Event},
    {NULL, NULL},
};

D3dContext *d3d_context_create(RcnCtx *ctx, D3dDevice *device) {
    D3dContext *c = d3d_new(ctx, K_CONTEXT, sizeof(D3dContext), context_methods);
    if (!c) return NULL;
    c->obj.device = device;
    c->device = device;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&c->lock, &attr);
    c->sample_mask = 0xFFFFFFFFu;
    c->blend_factor[0] = c->blend_factor[1] = c->blend_factor[2] = c->blend_factor[3] = 1.0f;
    return c;
}
