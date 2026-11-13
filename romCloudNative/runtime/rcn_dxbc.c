// RomCloud Native : nuanceurs DXBC (modèles 4.0 à 5.0) → GLSL (voir rcn_dxbc.h).
//
// Le conteneur DXBC porte des morceaux : signatures d'entrée (ISGN/ISG1) et de sortie (OSGN/OSG1/OSG5),
// code (SHDR/SHEX). Le code est une suite d'instructions de mots de 32 bits : un jeton d'opération (code,
// longueur, options), d'éventuels jetons étendus, puis les opérandes (type, composantes, indices pouvant
// eux-mêmes être des registres). Chaque instruction devient une ou quelques lignes de GLSL.
//
// Registres : r# (temporaires, uvec4 locaux), x#[] (tableaux), v[] (entrées), o[] (sorties), cb#[]
// (tampons de constantes : tableaux d'uvec4 dans un bloc std140), icb[] (constantes immédiates). Les
// opérations lisent leurs opérandes comme flottants (uintBitsToFloat), entiers (ivec) ou naturels selon
// leur nature, et écrivent des bits.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "rcn_dxbc.h"

void rcn_log(const char *fmt, ...);

// --- Texte ---

typedef struct {
    char *p;
    size_t n, cap;
} Str;
static void sputs(Str *s, const char *t) {
    size_t k = strlen(t);
    if (s->n + k + 1 > s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 4096;
        while (cap < s->n + k + 1) cap *= 2;
        char *p = realloc(s->p, cap);
        if (!p) return;
        s->p = p, s->cap = cap;
    }
    memcpy(s->p + s->n, t, k + 1);
    s->n += k;
}
static void sprintf_(Str *s, const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    sputs(s, buf);
}
#define SP sprintf_

// --- Instructions décodées ---

typedef struct Op Op;
struct Op {
    u32 type, ncomp, mode, mask;
    u8 swz[4];
    u32 dims;
    struct {
        u32 imm;
        Op *rel;
    } idx[3];
    u32 mod; // 1 négation, 2 valeur absolue
    u32 imm[4];
};

typedef struct {
    u32 opcode, ctrl;   // ctrl : bits 11 à 23 du jeton
    u32 sat, nz;
    i32 offset[3];      // décalages immédiats des échantillonnages
    u32 res_dim, res_ret; // jetons étendus
    u32 nops;
    Op op[6];
    u32 extra[4];       // mots qui suivent les opérandes des déclarations
} Ins;

typedef struct DxbcPriv {
    Ins *ins;
    u32 count;
    Op **rels;
    u32 rel_count, rel_cap;
    u32 temps;
    struct {
        u32 reg, size;
    } xt[32];
    u32 xt_count;
    u32 *icb;
    u32 icb_count; // mots
    u32 res_dim[128], res_ret[128], res_kind[128], res_stride[128]; // kind : 1 typée, 2 brute, 3 structurée
    u32 sampler_mode[16];
    u32 in_interp[32];  // par registre d'entrée (pixels)
    u32 in_sysval[32];  // déclarations dcl_input_siv/sgv
    u32 out_sysval[32];
    u32 cb_dynamic;
    int uses_depth_out;
} DxbcPriv;

enum {
    OP_ADD = 0, OP_AND, OP_BREAK, OP_BREAKC, OP_CALL, OP_CALLC, OP_CASE, OP_CONTINUE, OP_CONTINUEC, OP_CUT, OP_DEFAULT,
    OP_DERIV_RTX, OP_DERIV_RTY, OP_DISCARD, OP_DIV, OP_DP2, OP_DP3, OP_DP4, OP_ELSE, OP_EMIT, OP_EMITTHENCUT, OP_ENDIF,
    OP_ENDLOOP, OP_ENDSWITCH, OP_EQ, OP_EXP, OP_FRC, OP_FTOI, OP_FTOU, OP_GE, OP_IADD, OP_IF, OP_IEQ, OP_IGE, OP_ILT,
    OP_IMAD, OP_IMAX, OP_IMIN, OP_IMUL, OP_INE, OP_INEG, OP_ISHL, OP_ISHR, OP_ITOF, OP_LABEL, OP_LD, OP_LD_MS, OP_LOG,
    OP_LOOP, OP_LT, OP_MAD, OP_MIN, OP_MAX, OP_CUSTOMDATA, OP_MOV, OP_MOVC, OP_MUL, OP_NE, OP_NOP, OP_NOT, OP_OR,
    OP_RESINFO, OP_RET, OP_RETC, OP_ROUND_NE, OP_ROUND_NI, OP_ROUND_PI, OP_ROUND_Z, OP_RSQ, OP_SAMPLE, OP_SAMPLE_C,
    OP_SAMPLE_C_LZ, OP_SAMPLE_L, OP_SAMPLE_D, OP_SAMPLE_B, OP_SQRT, OP_SWITCH, OP_SINCOS, OP_UDIV, OP_ULT, OP_UGE,
    OP_UMUL, OP_UMAD, OP_UMAX, OP_UMIN, OP_USHR, OP_UTOF, OP_XOR, OP_DCL_RESOURCE, OP_DCL_CONSTANT_BUFFER,
    OP_DCL_SAMPLER, OP_DCL_INDEX_RANGE, OP_DCL_GS_OUTPUT_TOPOLOGY, OP_DCL_GS_INPUT_PRIMITIVE, OP_DCL_MAX_OUTPUT_VERTEX,
    OP_DCL_INPUT, OP_DCL_INPUT_SGV, OP_DCL_INPUT_SIV, OP_DCL_INPUT_PS, OP_DCL_INPUT_PS_SGV, OP_DCL_INPUT_PS_SIV,
    OP_DCL_OUTPUT, OP_DCL_OUTPUT_SGV, OP_DCL_OUTPUT_SIV, OP_DCL_TEMPS, OP_DCL_INDEXABLE_TEMP, OP_DCL_GLOBAL_FLAGS,
    OP_LOD = 108, OP_GATHER4 = 109, OP_SAMPLE_POS = 110, OP_SAMPLE_INFO = 111,
    OP_HS_DECLS = 113, OP_EMIT_STREAM = 117, OP_CUT_STREAM = 118, OP_BUFINFO = 121, OP_DERIV_RTX_COARSE = 122,
    OP_DERIV_RTX_FINE, OP_DERIV_RTY_COARSE, OP_DERIV_RTY_FINE, OP_GATHER4_C, OP_GATHER4_PO, OP_GATHER4_PO_C, OP_RCP,
    OP_F32TOF16, OP_F16TOF32, OP_UADDC, OP_USUBB, OP_COUNTBITS, OP_FIRSTBIT_HI, OP_FIRSTBIT_LO, OP_FIRSTBIT_SHI,
    OP_UBFE, OP_IBFE, OP_BFI, OP_BFREV, OP_SWAPC, OP_DCL_STREAM = 143, OP_DCL_THREAD_GROUP = 155,
    OP_DCL_UAV_TYPED = 156, OP_DCL_UAV_RAW, OP_DCL_UAV_STRUCTURED, OP_DCL_TGSM_RAW, OP_DCL_TGSM_STRUCTURED,
    OP_DCL_RESOURCE_RAW = 161, OP_DCL_RESOURCE_STRUCTURED = 162, OP_LD_UAV_TYPED = 163, OP_STORE_UAV_TYPED,
    OP_LD_RAW = 165, OP_STORE_RAW, OP_LD_STRUCTURED = 167, OP_STORE_STRUCTURED, OP_SYNC = 190, OP_DMOV = 199,
    OP_EVAL_CENTROID = 205,
};

enum {
    T_TEMP = 0, T_INPUT, T_OUTPUT, T_INDEXABLE_TEMP, T_IMM32, T_IMM64, T_SAMPLER, T_RESOURCE, T_CB, T_ICB, T_LABEL,
    T_PRIMITIVEID, T_ODEPTH, T_NULL, T_RASTERIZER, T_OMASK, T_STREAM, T_UAV = 30, T_TGSM = 31, T_THREAD_ID = 32,
    T_THREAD_GROUP_ID = 33, T_THREAD_ID_IN_GROUP = 34, T_COVERAGE = 35, T_THREAD_INDEX_IN_GROUP = 36,
    T_GS_INSTANCE_ID = 37, T_ODEPTH_GE = 38, T_ODEPTH_LE = 39,
};

// --- Lecture ---

typedef struct {
    const u32 *p, *end;
    DxbcPriv *pv;
    int bad;
} Reader;

static u32 next(Reader *r) {
    if (r->p >= r->end) {
        r->bad = 1;
        return 0;
    }
    return *r->p++;
}

static Op *rel_new(DxbcPriv *pv) {
    if (pv->rel_count == pv->rel_cap) {
        u32 cap = pv->rel_cap ? pv->rel_cap * 2 : 64;
        Op **rels = realloc(pv->rels, cap * sizeof *rels);
        if (!rels) return NULL;
        pv->rels = rels, pv->rel_cap = cap;
    }
    Op *o = calloc(1, sizeof *o);
    if (o) pv->rels[pv->rel_count++] = o;
    return o;
}

static void read_op(Reader *r, Op *o) {
    memset(o, 0, sizeof *o);
    u32 t = next(r);
    o->ncomp = t & 3;
    o->mode = (t >> 2) & 3;
    o->type = (t >> 12) & 0xFF;
    o->dims = (t >> 20) & 3;
    for (int i = 0; i < 4; i++) o->swz[i] = (u8)i;
    o->mask = 0xF;
    if (o->ncomp == 2) {
        if (o->mode == 0) o->mask = (t >> 4) & 0xF;
        else if (o->mode == 1) for (int i = 0; i < 4; i++) o->swz[i] = (t >> (4 + 2 * i)) & 3;
        else for (int i = 0; i < 4; i++) o->swz[i] = (t >> 4) & 3;
    } else if (o->ncomp == 1) {
        o->mask = 1;
        for (int i = 0; i < 4; i++) o->swz[i] = 0;
    } else {
        o->mask = 0;
    }
    u32 rep[3] = {(t >> 22) & 7, (t >> 25) & 7, (t >> 28) & 7};
    u32 ext = t & 0x80000000u;
    while (ext) {
        u32 e = next(r);
        if ((e & 0x3F) == 1) o->mod = (e >> 6) & 0xFF;
        ext = e & 0x80000000u;
    }
    if (o->type == T_IMM32) {
        u32 n = o->ncomp == 1 ? 1 : 4;
        for (u32 i = 0; i < n; i++) o->imm[i] = next(r);
        if (n == 1) o->imm[1] = o->imm[2] = o->imm[3] = o->imm[0];
    } else if (o->type == T_IMM64) {
        u32 n = o->ncomp == 1 ? 1 : 2;
        for (u32 i = 0; i < 2 * n; i++) o->imm[i < 4 ? i : 3] = next(r);
    }
    for (u32 d = 0; d < o->dims; d++) {
        switch (rep[d]) {
        case 0: o->idx[d].imm = next(r); break;
        case 1: next(r), o->idx[d].imm = next(r); break;
        case 2: o->idx[d].rel = rel_new(r->pv); if (o->idx[d].rel) read_op(r, o->idx[d].rel); break;
        case 3: o->idx[d].imm = next(r); o->idx[d].rel = rel_new(r->pv); if (o->idx[d].rel) read_op(r, o->idx[d].rel); break;
        case 4: next(r), o->idx[d].imm = next(r); o->idx[d].rel = rel_new(r->pv); if (o->idx[d].rel) read_op(r, o->idx[d].rel); break;
        default: r->bad = 1; break;
        }
    }
}

// Nombre d'opérandes (destinations comprises) d'une instruction ordinaire ; -1 : déclaration ou cas à part.
static int operand_count(u32 op) {
    switch (op) {
    case OP_BREAK: case OP_CONTINUE: case OP_CUT: case OP_DEFAULT: case OP_ELSE: case OP_EMIT: case OP_EMITTHENCUT:
    case OP_ENDIF: case OP_ENDLOOP: case OP_ENDSWITCH: case OP_LOOP: case OP_NOP: case OP_RET: case OP_HS_DECLS:
    case OP_SYNC:
        return 0;
    case OP_BREAKC: case OP_CONTINUEC: case OP_DISCARD: case OP_IF: case OP_RETC: case OP_SWITCH: case OP_CASE:
    case OP_CALL: case OP_LABEL: case OP_EMIT_STREAM: case OP_CUT_STREAM:
        return 1;
    case OP_CALLC:
        return 2;
    case OP_DERIV_RTX: case OP_DERIV_RTY: case OP_EXP: case OP_FRC: case OP_FTOI: case OP_FTOU: case OP_INEG:
    case OP_ITOF: case OP_LOG: case OP_MOV: case OP_NOT: case OP_ROUND_NE: case OP_ROUND_NI: case OP_ROUND_PI:
    case OP_ROUND_Z: case OP_RSQ: case OP_SQRT: case OP_UTOF: case OP_DERIV_RTX_COARSE: case OP_DERIV_RTX_FINE:
    case OP_DERIV_RTY_COARSE: case OP_DERIV_RTY_FINE: case OP_RCP: case OP_F32TOF16: case OP_F16TOF32:
    case OP_COUNTBITS: case OP_FIRSTBIT_HI: case OP_FIRSTBIT_LO: case OP_FIRSTBIT_SHI: case OP_BFREV: case OP_DMOV:
    case OP_SAMPLE_INFO: case OP_BUFINFO: case OP_EVAL_CENTROID:
        return 2;
    case OP_ADD: case OP_AND: case OP_DIV: case OP_DP2: case OP_DP3: case OP_DP4: case OP_EQ: case OP_GE: case OP_IADD:
    case OP_IEQ: case OP_IGE: case OP_ILT: case OP_IMAX: case OP_IMIN: case OP_INE: case OP_ISHL: case OP_ISHR:
    case OP_LT: case OP_MIN: case OP_MAX: case OP_MUL: case OP_NE: case OP_OR: case OP_UGE: case OP_ULT: case OP_UMAX:
    case OP_UMIN: case OP_USHR: case OP_XOR: case OP_LD: case OP_RESINFO: case OP_SINCOS: case OP_SAMPLE_POS:
    case OP_LD_RAW: case OP_LD_UAV_TYPED: case OP_STORE_UAV_TYPED: case OP_STORE_RAW:
        return 3;
    case OP_MAD: case OP_MOVC: case OP_IMAD: case OP_UMAD: case OP_IMUL: case OP_UMUL: case OP_UDIV: case OP_LD_MS:
    case OP_SAMPLE: case OP_LOD: case OP_GATHER4: case OP_UBFE: case OP_IBFE: case OP_UADDC: case OP_USUBB:
    case OP_LD_STRUCTURED: case OP_STORE_STRUCTURED:
        return 4;
    case OP_SAMPLE_C: case OP_SAMPLE_C_LZ: case OP_SAMPLE_L: case OP_SAMPLE_B: case OP_GATHER4_C: case OP_GATHER4_PO:
    case OP_BFI: case OP_SWAPC:
        return 5;
    case OP_SAMPLE_D: case OP_GATHER4_PO_C:
        return 6;
    default:
        return -1;
    }
}

static int read_signature(const u8 *chunk, u32 size, u32 tag, DxbcSig *out, u32 *count) {
    if (size < 8) return 0;
    u32 n, stride = tag == 0x31475349 || tag == 0x3147534F ? 32 : tag == 0x3547534F ? 28 : 24; // ISG1 / OSG1 : 32, OSG5 : 28
    memcpy(&n, chunk, 4);
    u32 first = stride == 24 ? 0 : 4; // OSG5 / *SG1 : flux (stream) en premier
    for (u32 i = 0; i < n && *count < 32; i++) {
        const u8 *e = chunk + 8 + (size_t)i * stride;
        if (e + stride > chunk + size) return 0;
        u32 w[6];
        memcpy(w, e + first, 24);
        DxbcSig *s = &out[(*count)++];
        memset(s, 0, sizeof *s);
        if (w[0] < size) snprintf(s->name, sizeof s->name, "%s", (const char *)chunk + w[0]);
        s->index = w[1];
        s->sysval = w[2];
        s->type = w[3];
        s->reg = w[4];
        s->mask = w[5] & 0xF;
    }
    return 1;
}

static void priv_free(DxbcPriv *pv) {
    if (!pv) return;
    for (u32 i = 0; i < pv->rel_count; i++) free(pv->rels[i]);
    free(pv->rels);
    free(pv->ins);
    free(pv->icb);
    free(pv);
}

void dxbc_free(DxbcShader *dx) {
    if (!dx) return;
    priv_free(dx->priv);
    free(dx);
}

// Paire (texture, échantillonneur) : trouvée ou ajoutée.
static void add_pair(DxbcShader *dx, u32 t, i32 s, int shadow) {
    DxbcPriv *pv = dx->priv;
    for (u32 i = 0; i < dx->pair_count; i++) {
        if (dx->pairs[i].t == t && dx->pairs[i].s == s && dx->pairs[i].shadow == (u32)shadow) return;
    }
    if (dx->pair_count >= 32 || t >= 128) return;
    DxbcPair *p = &dx->pairs[dx->pair_count];
    p->t = t;
    p->s = s;
    p->shadow = (u32)shadow;
    p->dim = pv->res_dim[t];
    u32 ret = pv->res_ret[t] & 0xF;
    p->ret = ret == 3 ? 1 : ret == 4 ? 2 : 0;
    p->unit = D3D_UNIT_BASE(dx->stage) + dx->pair_count;
    dx->pair_count++;
}

DxbcShader *dxbc_parse(const u8 *code, size_t size) {
    if (size < 32 || memcmp(code, "DXBC", 4)) return NULL;
    u32 chunks;
    memcpy(&chunks, code + 28, 4);
    DxbcShader *dx = calloc(1, sizeof *dx);
    DxbcPriv *pv = calloc(1, sizeof *pv);
    if (!dx || !pv) {
        free(dx);
        free(pv);
        return NULL;
    }
    dx->priv = pv;
    const u32 *shdr = NULL;
    u32 shdr_words = 0;
    for (u32 i = 0; i < chunks && 32 + 4 * (size_t)i + 4 <= size; i++) {
        u32 off;
        memcpy(&off, code + 32 + 4 * i, 4);
        if (off + 8 > size) continue;
        u32 tag, len;
        memcpy(&tag, code + off, 4);
        memcpy(&len, code + off + 4, 4);
        if (off + 8 + (size_t)len > size) continue;
        const u8 *data = code + off + 8;
        switch (tag) {
        case 0x4E475349: case 0x31475349: read_signature(data, len, tag, dx->in, &dx->in_count); break;             // ISGN, ISG1
        case 0x4E47534F: case 0x3147534F: case 0x3547534F: read_signature(data, len, tag, dx->out, &dx->out_count); break; // OSGN, OSG1, OSG5
        case 0x52444853: case 0x58454853: shdr = (const u32 *)data, shdr_words = len / 4; break;                     // SHDR, SHEX
        }
    }
    if (!shdr || shdr_words < 2) {
        dxbc_free(dx);
        return NULL;
    }
    u32 version = shdr[0];
    u32 type = version >> 16;
    // Type de programme : 0 pixels, 1 sommets, 2 géométrie, 3 coque, 4 domaine, 5 calcul.
    static const u32 stages[] = {DXBC_PS, DXBC_VS, DXBC_GS, DXBC_HS, DXBC_DS, DXBC_CS};
    dx->stage = type < 6 ? stages[type] : DXBC_PS;
    dx->version = version & 0xFF;
    Reader r = {shdr + 2, shdr + (shdr[1] < shdr_words ? shdr[1] : shdr_words), pv, 0};
    u32 cap = 256;
    pv->ins = malloc(cap * sizeof(Ins));
    while (r.p < r.end && !r.bad && pv->ins) {
        const u32 *start = r.p;
        u32 tok = next(&r);
        u32 opcode = tok & 0x7FF;
        u32 len = (tok >> 24) & 0x7F;
        if (opcode == OP_CUSTOMDATA) {
            u32 cls = tok >> 11, words = next(&r);
            if (words < 2 || start + words > r.end) break;
            if (cls == 3) { // constantes immédiates
                pv->icb_count = words - 2;
                pv->icb = malloc(4 * (size_t)pv->icb_count + 16);
                if (pv->icb) memcpy(pv->icb, start + 2, 4 * (size_t)pv->icb_count);
            }
            r.p = start + words;
            continue;
        }
        if (!len || start + len > r.end) break;
        if (pv->count == cap) {
            cap *= 2;
            Ins *ins = realloc(pv->ins, cap * sizeof(Ins));
            if (!ins) break;
            pv->ins = ins;
        }
        Ins *in = &pv->ins[pv->count];
        memset(in, 0, sizeof *in);
        in->opcode = opcode;
        in->ctrl = (tok >> 11) & 0x1FFF;
        in->sat = (tok >> 13) & 1;
        in->nz = (tok >> 18) & 1;
        u32 ext = tok & 0x80000000u;
        while (ext) {
            u32 e = next(&r);
            u32 et = e & 0x3F;
            if (et == 1) {
                in->offset[0] = ((i32)(e << 19)) >> 28; // bits 9-12
                in->offset[1] = ((i32)(e << 15)) >> 28; // bits 13-16
                in->offset[2] = ((i32)(e << 11)) >> 28; // bits 17-20
            } else if (et == 2) {
                in->res_dim = (e >> 6) & 0x1F;
            } else if (et == 3) {
                in->res_ret = e >> 6;
            }
            ext = e & 0x80000000u;
        }
        int n = operand_count(opcode);
        const u32 *stop = start + len;
        if (n >= 0) {
            for (int k = 0; k < n && k < 6; k++) read_op(&r, &in->op[k]);
            in->nops = (u32)(n < 6 ? n : 6);
        } else {
            // Déclarations : un opérande au plus, puis des mots.
            switch (opcode) {
            case OP_DCL_TEMPS: pv->temps = next(&r); break;
            case OP_DCL_INDEXABLE_TEMP:
                if (pv->xt_count < 32) {
                    pv->xt[pv->xt_count].reg = next(&r);
                    pv->xt[pv->xt_count].size = next(&r);
                    pv->xt_count++;
                }
                break;
            case OP_DCL_GLOBAL_FLAGS: case OP_DCL_GS_OUTPUT_TOPOLOGY: case OP_DCL_GS_INPUT_PRIMITIVE:
            case OP_DCL_MAX_OUTPUT_VERTEX:
                break;
            case OP_DCL_THREAD_GROUP: dx->threads[0] = next(&r), dx->threads[1] = next(&r), dx->threads[2] = next(&r); break;
            default:
                if (r.p < stop) read_op(&r, &in->op[0]), in->nops = 1;
                for (int k = 0; k < 4 && r.p < stop; k++) in->extra[k] = next(&r);
                break;
            }
            Op *o = &in->op[0];
            switch (opcode) {
            case OP_DCL_RESOURCE:
                if (o->idx[0].imm < 128) {
                    pv->res_dim[o->idx[0].imm] = (tok >> 11) & 0x1F;
                    pv->res_ret[o->idx[0].imm] = in->extra[0];
                    pv->res_kind[o->idx[0].imm] = 1;
                }
                break;
            case OP_DCL_RESOURCE_RAW:
                if (o->idx[0].imm < 128) pv->res_kind[o->idx[0].imm] = 2, pv->res_dim[o->idx[0].imm] = 11;
                break;
            case OP_DCL_RESOURCE_STRUCTURED:
                if (o->idx[0].imm < 128) pv->res_kind[o->idx[0].imm] = 3, pv->res_stride[o->idx[0].imm] = in->extra[0], pv->res_dim[o->idx[0].imm] = 12;
                break;
            case OP_DCL_SAMPLER:
                if (o->idx[0].imm < 16) pv->sampler_mode[o->idx[0].imm] = (tok >> 11) & 0xF;
                break;
            case OP_DCL_CONSTANT_BUFFER:
                if (o->idx[0].imm < 14) {
                    dx->cb_mask |= 1u << o->idx[0].imm;
                    dx->cb_size[o->idx[0].imm] = o->idx[1].imm;
                    if ((tok >> 11) & 1) pv->cb_dynamic |= 1u << o->idx[0].imm;
                }
                break;
            case OP_DCL_INPUT_PS: case OP_DCL_INPUT_PS_SIV: case OP_DCL_INPUT_PS_SGV:
                if (o->idx[0].imm < 32) pv->in_interp[o->idx[0].imm] = (tok >> 11) & 0xF;
                if (opcode != OP_DCL_INPUT_PS && o->idx[0].imm < 32) pv->in_sysval[o->idx[0].imm] = in->extra[0];
                break;
            case OP_DCL_INPUT_SIV: case OP_DCL_INPUT_SGV:
                if (o->idx[0].imm < 32) pv->in_sysval[o->idx[0].imm] = in->extra[0];
                break;
            case OP_DCL_OUTPUT_SIV: case OP_DCL_OUTPUT_SGV:
                if (o->idx[0].imm < 32) pv->out_sysval[o->idx[0].imm] = in->extra[0];
                break;
            }
        }
        r.p = stop;
        pv->count++;
    }
    if (r.bad) {
        rcn_log("dxbc : nuanceur illisible");
        dxbc_free(dx);
        return NULL;
    }
    // Interpolation des entrées (pixels) rangée dans la signature.
    for (u32 i = 0; i < dx->in_count; i++) {
        if (dx->in[i].reg < 32) dx->in[i].interp = pv->in_interp[dx->in[i].reg];
    }
    // Paires (texture, échantillonneur) des instructions.
    for (u32 i = 0; i < pv->count; i++) {
        Ins *in = &pv->ins[i];
        switch (in->opcode) {
        case OP_SAMPLE: case OP_SAMPLE_L: case OP_SAMPLE_B: case OP_SAMPLE_D: case OP_GATHER4: case OP_LOD:
            add_pair(dx, in->op[2].idx[0].imm, (i32)in->op[3].idx[0].imm, 0);
            break;
        case OP_SAMPLE_C: case OP_SAMPLE_C_LZ: case OP_GATHER4_C:
            add_pair(dx, in->op[2].idx[0].imm, (i32)in->op[3].idx[0].imm, 1);
            break;
        case OP_GATHER4_PO: add_pair(dx, in->op[3].idx[0].imm, (i32)in->op[4].idx[0].imm, 0); break;
        case OP_GATHER4_PO_C: add_pair(dx, in->op[3].idx[0].imm, (i32)in->op[4].idx[0].imm, 1); break;
        case OP_LD: case OP_RESINFO: add_pair(dx, in->op[2].idx[0].imm, -1, 0); break;
        case OP_LD_MS: add_pair(dx, in->op[2].idx[0].imm, -1, 0); break;
        case OP_BUFINFO: if (in->op[1].type == T_RESOURCE) add_pair(dx, in->op[1].idx[0].imm, -1, 0); break;
        case OP_SAMPLE_INFO: if (in->op[1].type == T_RESOURCE) add_pair(dx, in->op[1].idx[0].imm, -1, 0); break;
        }
    }
    return dx;
}

// --- Écriture du GLSL ---

typedef struct {
    Str s;
    const DxbcShader *dx;
    const DxbcPriv *pv;
    const DxbcShader *prev, *next;
    int es;
    int depth;
    const char *pfx;
    int fail;
    char error[160];
} Gen;

enum { TF = 0, TI = 1, TU = 2 }; // flottant, entier, naturel

static const char *const comp_name = "xyzw";

static void indent(Gen *g) {
    for (int i = 0; i < g->depth + 1; i++) sputs(&g->s, "    ");
}
static void fail(Gen *g, const char *fmt, ...) {
    if (g->fail) return;
    g->fail = 1;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g->error, sizeof g->error, fmt, ap);
    va_end(ap);
}

static const char *vec_type(int type, int n) {
    static const char *const names[3][5] = {{"", "float", "vec2", "vec3", "vec4"},
                                            {"", "int", "ivec2", "ivec3", "ivec4"},
                                            {"", "uint", "uvec2", "uvec3", "uvec4"}};
    return names[type][n < 1 ? 1 : n > 4 ? 4 : n];
}

// Composantes écrites par une destination.
static int dest_comps(const Op *o, u8 *comps) {
    int n = 0;
    if (o->ncomp == 1) {
        comps[0] = 0;
        return 1;
    }
    for (int i = 0; i < 4; i++) {
        if (o->mask & (1u << i)) comps[n++] = (u8)i;
    }
    return n;
}

static void float_literal(Str *s, u32 bits) {
    float f;
    memcpy(&f, &bits, 4);
    char buf[48];
    if (isfinite(f) && (f == 0.0f || fabsf(f) >= 1e-30f)) {
        snprintf(buf, sizeof buf, "%.9g", f);
        float back = strtof(buf, NULL);
        u32 bb;
        memcpy(&bb, &back, 4);
        if (bb == bits) {
            sputs(s, buf);
            if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'n') && !strchr(buf, 'i')) sputs(s, ".0");
            return;
        }
    }
    SP(s, "uintBitsToFloat(0x%08xu)", bits);
}

static void src_expr(Gen *g, Str *s, const Op *o, const u8 *comps, int n, int type);

// Indice d'un tableau : immédiat, registre, ou registre plus immédiat.
static void index_expr(Gen *g, Str *s, const Op *o, int d) {
    if (o->idx[d].rel) {
        u8 c = 0;
        sputs(s, "int(");
        src_expr(g, s, o->idx[d].rel, &c, 1, TI);
        if (o->idx[d].imm) SP(s, " + %d", (int)o->idx[d].imm);
        sputs(s, ")");
    } else {
        SP(s, "%u", o->idx[d].imm);
    }
}

// Registre (uvec4) désigné par un opérande ; 0 si ce n'est pas un registre.
static int reg_base(Gen *g, Str *s, const Op *o) {
    switch (o->type) {
    case T_TEMP: SP(s, "r%u", o->idx[0].imm); return 1;
    case T_INPUT:
        if (o->dims == 2) { // entrée de géométrie [sommet][registre]
            sputs(s, "v[");
            index_expr(g, s, o, 1);
            sputs(s, "]");
            return 1;
        }
        sputs(s, "v[");
        index_expr(g, s, o, 0);
        sputs(s, "]");
        return 1;
    case T_OUTPUT: sputs(s, "o["); index_expr(g, s, o, 0); sputs(s, "]"); return 1;
    case T_INDEXABLE_TEMP: SP(s, "x%u[", o->idx[0].imm); index_expr(g, s, o, 1); sputs(s, "]"); return 1;
    case T_CB:
        SP(s, "%s_c%u[", g->pfx, o->idx[0].imm);
        index_expr(g, s, o, 1);
        sputs(s, "]");
        return 1;
    case T_ICB: sputs(s, "icb["); index_expr(g, s, o, 0); sputs(s, "]"); return 1;
    case T_PRIMITIVEID: sputs(s, "uvec4(uint(gl_PrimitiveID))"); return 1;
    case T_THREAD_ID: sputs(s, "uvec4(gl_GlobalInvocationID, 0u)"); return 1;
    case T_THREAD_GROUP_ID: sputs(s, "uvec4(gl_WorkGroupID, 0u)"); return 1;
    case T_THREAD_ID_IN_GROUP: sputs(s, "uvec4(gl_LocalInvocationID, 0u)"); return 1;
    case T_THREAD_INDEX_IN_GROUP: sputs(s, "uvec4(gl_LocalInvocationIndex)"); return 1;
    case T_COVERAGE: sputs(s, "uvec4(uint(gl_SampleMaskIn[0]))"); return 1;
    case T_GS_INSTANCE_ID: sputs(s, "uvec4(uint(gl_InvocationID))"); return 1;
    case T_ODEPTH: case T_ODEPTH_GE: case T_ODEPTH_LE: sputs(s, "uvec4(o_depth)"); return 1;
    default: return 0;
    }
}

// Lecture d'un opérande sur les composantes `comps` (n), comme `type`.
static void src_expr(Gen *g, Str *s, const Op *o, const u8 *comps, int n, int type) {
    u8 sel[4];
    for (int i = 0; i < n; i++) sel[i] = o->ncomp == 1 ? 0 : o->swz[comps[i] & 3];
    Str inner = {0};
    if (o->type == T_IMM32) {
        if (n > 1) SP(&inner, "%s(", vec_type(type, n));
        for (int i = 0; i < n; i++) {
            u32 bits = o->imm[sel[i]];
            if (i) sputs(&inner, ", ");
            if (type == TF) float_literal(&inner, bits);
            else if (type == TI) SP(&inner, "%d", (i32)bits);
            else SP(&inner, "%uu", bits);
        }
        if (n > 1) sputs(&inner, ")");
    } else {
        Str base = {0};
        if (!reg_base(g, &base, o)) {
            fail(g, "opérande de type %u non géré", o->type);
            sputs(&base, "uvec4(0u)");
        }
        char swz[5] = {0};
        for (int i = 0; i < n; i++) swz[i] = comp_name[sel[i]];
        if (type == TF) SP(&inner, "uintBitsToFloat(%s.%s)", base.p, swz);
        else if (type == TI) SP(&inner, "%s(%s.%s)", vec_type(TI, n), base.p, swz);
        else SP(&inner, "%s.%s", base.p, swz);
        free(base.p);
    }
    if (o->mod & 2) {
        Str t = {0};
        SP(&t, "abs(%s)", inner.p);
        free(inner.p);
        inner = t;
    }
    if (o->mod & 1) {
        Str t = {0};
        SP(&t, "(-%s)", inner.p);
        free(inner.p);
        inner = t;
    }
    sputs(s, inner.p ? inner.p : "0");
    free(inner.p);
}

// Écrit `expr` (de type `type`, n composantes) dans la destination.
static void dst_write(Gen *g, const Ins *in, const Op *o, const u8 *comps, int n, int type, const char *expr) {
    if (o->type == T_NULL) return;
    indent(g);
    char swz[5] = {0};
    for (int i = 0; i < n; i++) swz[i] = comp_name[comps[i]];
    if (o->type == T_ODEPTH || o->type == T_ODEPTH_GE || o->type == T_ODEPTH_LE) {
        sputs(&g->s, "o_depth");
    } else {
        Str base = {0};
        if (!reg_base(g, &base, o)) fail(g, "destination de type %u non gérée", o->type);
        SP(&g->s, "%s.%s", base.p ? base.p : "r0", swz);
        free(base.p);
    }
    sputs(&g->s, " = ");
    if (type == TF) {
        if (in && in->sat) SP(&g->s, "floatBitsToUint(clamp(%s, 0.0, 1.0));\n", expr);
        else SP(&g->s, "floatBitsToUint(%s);\n", expr);
    } else if (type == TI) {
        SP(&g->s, "%s(%s);\n", vec_type(TU, n), expr);
    } else {
        SP(&g->s, "%s;\n", expr);
    }
}

// Opération composante par composante : `fmt` avec %0, %1, %2 pour les sources (lues comme `in_type`),
// résultat de type `out_type`.
static void component_op(Gen *g, const Ins *in, int in_type, int out_type, const char *fmt) {
    u8 comps[4];
    int n = dest_comps(&in->op[0], comps);
    if (!n) return;
    Str e = {0};
    for (const char *p = fmt; *p; p++) {
        if (*p == '%' && p[1] >= '0' && p[1] <= '4') {
            src_expr(g, &e, &in->op[1 + (p[1] - '0')], comps, n, in_type);
            p++;
        } else if (*p == '%' && p[1] == 'T') { // type du résultat
            sputs(&e, vec_type(out_type, n));
            p++;
        } else {
            char c[2] = {*p, 0};
            sputs(&e, c);
        }
    }
    dst_write(g, in, &in->op[0], comps, n, out_type, e.p ? e.p : "0");
    free(e.p);
}

// Comparaison composante par composante → masques 0xFFFFFFFF / 0.
static void compare_op(Gen *g, const Ins *in, int type, const char *vec_fn, const char *scalar_op) {
    u8 comps[4];
    int n = dest_comps(&in->op[0], comps);
    if (!n) return;
    Str a = {0}, b = {0}, e = {0};
    src_expr(g, &a, &in->op[1], comps, n, type);
    src_expr(g, &b, &in->op[2], comps, n, type);
    if (n == 1) SP(&e, "((%s %s %s) ? 0xFFFFFFFFu : 0u)", a.p, scalar_op, b.p);
    else SP(&e, "(%s(%s(%s, %s)) * 0xFFFFFFFFu)", vec_type(TU, n), vec_fn, a.p, b.p);
    dst_write(g, NULL, &in->op[0], comps, n, TU, e.p);
    free(a.p), free(b.p), free(e.p);
}

static void dot_op(Gen *g, const Ins *in, int k) {
    static const u8 all[4] = {0, 1, 2, 3};
    u8 comps[4];
    int n = dest_comps(&in->op[0], comps);
    if (!n) return;
    Str a = {0}, b = {0}, e = {0};
    src_expr(g, &a, &in->op[1], all, k, TF);
    src_expr(g, &b, &in->op[2], all, k, TF);
    if (n == 1) SP(&e, "dot(%s, %s)", a.p, b.p);
    else SP(&e, "%s(dot(%s, %s))", vec_type(TF, n), a.p, b.p);
    dst_write(g, in, &in->op[0], comps, n, TF, e.p);
    free(a.p), free(b.p), free(e.p);
}

// Condition d'un if / breakc… : composante unique comparée à zéro.
static void condition(Gen *g, Str *s, const Ins *in, const Op *o) {
    u8 c = 0;
    Str v = {0};
    src_expr(g, &v, o, &c, 1, TU);
    SP(s, "(%s %s 0u)", v.p, in->nz ? "!=" : "==");
    free(v.p);
}

// --- Textures ---

static const DxbcPair *pair_of(Gen *g, u32 t, i32 s, int shadow) {
    for (u32 i = 0; i < g->dx->pair_count; i++) {
        const DxbcPair *p = &g->dx->pairs[i];
        if (p->t == t && p->s == s && p->shadow == (u32)shadow) return p;
    }
    return NULL;
}
static void sampler_name(Gen *g, Str *s, const DxbcPair *p) {
    if (p->s >= 0) SP(s, "%s_t%u_s%d%s", g->pfx, p->t, p->s, p->shadow ? "c" : "");
    else SP(s, "%s_t%u", g->pfx, p->t);
}
static const char *sampler_type(const DxbcPair *p) {
    static char buf[48];
    const char *prefix = p->ret == 1 ? "i" : p->ret == 2 ? "u" : "";
    const char *base;
    switch (p->dim) {
    case 1: base = "samplerBuffer"; break;
    case 4: base = "sampler2DMS"; break;
    case 9: base = "sampler2DMSArray"; break;
    case 5: base = "sampler3D"; break;
    case 6: base = p->shadow ? "samplerCubeShadow" : "samplerCube"; break;
    case 10: base = p->shadow ? "samplerCubeArrayShadow" : "samplerCubeArray"; break;
    default: base = p->shadow ? "sampler2DArrayShadow" : "sampler2DArray"; break; // 1D, 2D et leurs tableaux
    }
    snprintf(buf, sizeof buf, "%s%s", p->shadow ? "" : prefix, base);
    return buf;
}

// Coordonnées d'échantillonnage (flottantes) selon la dimension : 1D et 2D deviennent des tableaux 2D.
static void sample_coord(Gen *g, Str *s, const Op *addr, u32 dim) {
    static const u8 xyzw[4] = {0, 1, 2, 3};
    Str a = {0};
    switch (dim) {
    case 2: src_expr(g, &a, addr, xyzw, 1, TF); SP(s, "vec3(%s, 0.5, 0.0)", a.p); break;
    case 7: {
        u8 c1 = 1;
        Str b = {0};
        src_expr(g, &a, addr, xyzw, 1, TF);
        src_expr(g, &b, addr, &c1, 1, TF);
        SP(s, "vec3(%s, 0.5, %s)", a.p, b.p);
        free(b.p);
        break;
    }
    case 3: src_expr(g, &a, addr, xyzw, 2, TF); SP(s, "vec3(%s, 0.0)", a.p); break;
    case 10: src_expr(g, &a, addr, xyzw, 4, TF); sputs(s, a.p); break;
    default: src_expr(g, &a, addr, xyzw, 3, TF); sputs(s, a.p); break; // 2D tableau, 3D, cube
    }
    free(a.p);
}
static int grad_comps(u32 dim) { return dim == 5 || dim == 6 || dim == 10 ? 3 : 2; }
static int has_offset(const Ins *in) { return in->offset[0] || in->offset[1] || in->offset[2]; }
static void offset_expr(Str *s, const Ins *in, u32 dim) {
    if (dim == 5) SP(s, "ivec3(%d, %d, %d)", in->offset[0], in->offset[1], in->offset[2]);
    else if (dim == 2 || dim == 7) SP(s, "ivec2(%d, 0)", in->offset[0]);
    else SP(s, "ivec2(%d, %d)", in->offset[0], in->offset[1]);
}

// Résultat vec4 d'une lecture de texture vers la destination, selon le mélange de l'opérande de ressource.
static void texture_result(Gen *g, const Ins *in, const Op *res, const char *expr, int ret) {
    u8 comps[4];
    int n = dest_comps(&in->op[0], comps);
    if (!n) return;
    Str e = {0};
    char swz[5] = {0};
    for (int i = 0; i < n; i++) swz[i] = comp_name[res->swz[comps[i]]];
    SP(&e, "(%s).%s", expr, swz);
    dst_write(g, ret == 0 ? in : NULL, &in->op[0], comps, n, ret == 0 ? TF : ret == 1 ? TI : TU, e.p);
    free(e.p);
}

static void sample_op(Gen *g, const Ins *in) {
    const Op *addr = &in->op[1], *res = &in->op[2], *smp = &in->op[3];
    int shadow = in->opcode == OP_SAMPLE_C || in->opcode == OP_SAMPLE_C_LZ;
    const DxbcPair *p = pair_of(g, res->idx[0].imm, (i32)smp->idx[0].imm, shadow);
    if (!p) {
        fail(g, "paire de texture absente");
        return;
    }
    Str name = {0}, coord = {0}, e = {0};
    sampler_name(g, &name, p);
    sample_coord(g, &coord, addr, p->dim);
    u8 c0 = 0;
    Str extra = {0};
    int off = has_offset(in) && p->dim != 6 && p->dim != 10;
    Str offs = {0};
    if (off) offset_expr(&offs, in, p->dim);
    switch (in->opcode) {
    case OP_SAMPLE:
        if (off) SP(&e, "textureOffset(%s, %s, %s)", name.p, coord.p, offs.p);
        else SP(&e, "texture(%s, %s)", name.p, coord.p);
        break;
    case OP_SAMPLE_B:
        src_expr(g, &extra, &in->op[4], &c0, 1, TF);
        if (off) SP(&e, "textureOffset(%s, %s, %s, %s)", name.p, coord.p, offs.p, extra.p);
        else SP(&e, "texture(%s, %s, %s)", name.p, coord.p, extra.p);
        break;
    case OP_SAMPLE_L:
        src_expr(g, &extra, &in->op[4], &c0, 1, TF);
        if (off) SP(&e, "textureLodOffset(%s, %s, %s, %s)", name.p, coord.p, extra.p, offs.p);
        else SP(&e, "textureLod(%s, %s, %s)", name.p, coord.p, extra.p);
        break;
    case OP_SAMPLE_D: {
        static const u8 xyz[3] = {0, 1, 2};
        Str dx = {0}, dy = {0};
        int k = grad_comps(p->dim);
        src_expr(g, &dx, &in->op[4], xyz, k, TF);
        src_expr(g, &dy, &in->op[5], xyz, k, TF);
        if (off) SP(&e, "textureGradOffset(%s, %s, %s, %s, %s)", name.p, coord.p, dx.p, dy.p, offs.p);
        else SP(&e, "textureGrad(%s, %s, %s, %s)", name.p, coord.p, dx.p, dy.p);
        free(dx.p), free(dy.p);
        break;
    }
    case OP_SAMPLE_C:
    case OP_SAMPLE_C_LZ: {
        src_expr(g, &extra, &in->op[4], &c0, 1, TF);
        Str full = {0};
        if (p->dim == 10) SP(&full, "%s, %s", coord.p, extra.p); // cube en tableau : référence à part
        else SP(&full, "vec4(%s, %s)", coord.p, extra.p);
        if (in->opcode == OP_SAMPLE_C_LZ && p->dim != 6 && p->dim != 10) {
            // Niveau 0 : textureGrad à dérivées nulles (textureLod n'existe pas pour sampler2DArrayShadow).
            if (off) SP(&e, "vec4(textureGradOffset(%s, %s, vec2(0.0), vec2(0.0), %s))", name.p, full.p, offs.p);
            else SP(&e, "vec4(textureGrad(%s, %s, vec2(0.0), vec2(0.0)))", name.p, full.p);
        } else if (in->opcode == OP_SAMPLE_C_LZ) {
            SP(&e, "vec4(textureGrad(%s, %s, vec3(0.0), vec3(0.0)))", name.p, full.p);
        } else {
            if (off) SP(&e, "vec4(textureOffset(%s, %s, %s))", name.p, full.p, offs.p);
            else SP(&e, "vec4(texture(%s, %s))", name.p, full.p);
        }
        free(full.p);
        break;
    }
    }
    texture_result(g, in, res, e.p, shadow ? 0 : (int)p->ret);
    free(name.p), free(coord.p), free(e.p), free(extra.p), free(offs.p);
}

static void gather_op(Gen *g, const Ins *in) {
    int po = in->opcode == OP_GATHER4_PO || in->opcode == OP_GATHER4_PO_C;
    int cmp = in->opcode == OP_GATHER4_C || in->opcode == OP_GATHER4_PO_C;
    const Op *addr = &in->op[1], *res = &in->op[po ? 3 : 2], *smp = &in->op[po ? 4 : 3];
    const DxbcPair *p = pair_of(g, res->idx[0].imm, (i32)smp->idx[0].imm, cmp);
    if (!p) {
        fail(g, "paire de texture absente");
        return;
    }
    Str name = {0}, coord = {0}, e = {0}, offs = {0}, ref = {0};
    sampler_name(g, &name, p);
    sample_coord(g, &coord, addr, p->dim);
    static const u8 xy[2] = {0, 1};
    if (po) src_expr(g, &offs, &in->op[2], xy, 2, TI);
    else if (has_offset(in)) offset_expr(&offs, in, p->dim);
    u8 c0 = 0;
    if (cmp) src_expr(g, &ref, &in->op[po ? 5 : 4], &c0, 1, TF);
    int comp = smp->swz[0];
    if (cmp) {
        if (offs.p) SP(&e, "textureGatherOffset(%s, %s, %s, %s)", name.p, coord.p, ref.p, offs.p);
        else SP(&e, "textureGather(%s, %s, %s)", name.p, coord.p, ref.p);
    } else {
        if (offs.p) SP(&e, "textureGatherOffset(%s, %s, %s, %d)", name.p, coord.p, offs.p, comp);
        else SP(&e, "textureGather(%s, %s, %d)", name.p, coord.p, comp);
    }
    texture_result(g, in, res, e.p, cmp ? 0 : (int)p->ret);
    free(name.p), free(coord.p), free(e.p), free(offs.p), free(ref.p);
}

// ld : texelFetch, adresse entière (texel, couche, niveau selon la dimension).
static void ld_op(Gen *g, const Ins *in) {
    const Op *addr = &in->op[1], *res = &in->op[2];
    const DxbcPair *p = pair_of(g, res->idx[0].imm, -1, 0);
    if (!p) {
        fail(g, "texture absente");
        return;
    }
    Str name = {0}, e = {0}, a = {0}, lod = {0};
    sampler_name(g, &name, p);
    static const u8 x[1] = {0}, xy[2] = {0, 1}, xyz[3] = {0, 1, 2};
    u8 c;
    switch (p->dim) {
    case 1: src_expr(g, &a, addr, x, 1, TI); SP(&e, "texelFetch(%s, %s)", name.p, a.p); break;
    case 2: src_expr(g, &a, addr, x, 1, TI); c = 1; src_expr(g, &lod, addr, &c, 1, TI); SP(&e, "texelFetch(%s, ivec3(%s, 0, 0), %s)", name.p, a.p, lod.p); break;
    case 7: src_expr(g, &a, addr, xy, 2, TI); c = 2; src_expr(g, &lod, addr, &c, 1, TI); SP(&e, "texelFetch(%s, ivec3(%s.x, 0, %s.y), %s)", name.p, a.p, a.p, lod.p); break;
    case 3: src_expr(g, &a, addr, xy, 2, TI); c = 2; src_expr(g, &lod, addr, &c, 1, TI); SP(&e, "texelFetch(%s, ivec3(%s, 0), %s)", name.p, a.p, lod.p); break;
    case 4: src_expr(g, &a, addr, xy, 2, TI); src_expr(g, &lod, &in->op[3], x, 1, TI); SP(&e, "texelFetch(%s, %s, %s)", name.p, a.p, lod.p); break;
    case 9: src_expr(g, &a, addr, xyz, 3, TI); src_expr(g, &lod, &in->op[3], x, 1, TI); SP(&e, "texelFetch(%s, %s, %s)", name.p, a.p, lod.p); break;
    default: // 2D tableau, 3D
        src_expr(g, &a, addr, xyz, 3, TI);
        c = 3;
        src_expr(g, &lod, addr, &c, 1, TI);
        SP(&e, "texelFetch(%s, %s, %s)", name.p, a.p, lod.p);
        break;
    }
    if (has_offset(in) && p->dim == 3) { // décalage : ajouté à l'adresse
        free(e.p);
        e.p = NULL, e.n = e.cap = 0;
        SP(&e, "texelFetch(%s, ivec3(%s + ivec2(%d, %d), 0), %s)", name.p, a.p, in->offset[0], in->offset[1], lod.p);
    }
    texture_result(g, in, res, e.p, (int)p->ret);
    free(name.p), free(e.p), free(a.p), free(lod.p);
}

// resinfo : taille du niveau, puis nombre de niveaux (flottants, inverses ou naturels).
static void resinfo_op(Gen *g, const Ins *in) {
    const Op *res = &in->op[2];
    const DxbcPair *p = pair_of(g, res->idx[0].imm, -1, 0);
    if (!p) {
        fail(g, "texture absente");
        return;
    }
    Str name = {0}, lod = {0}, e = {0}, levels = {0};
    sampler_name(g, &name, p);
    u8 c0 = 0;
    src_expr(g, &lod, &in->op[1], &c0, 1, TI);
    if (g->es) SP(&levels, "1u");
    else SP(&levels, "uint(textureQueryLevels(%s))", name.p);
    switch (p->dim) {
    case 2: SP(&e, "uvec4(uint(textureSize(%s, %s).x), 0u, 0u, %s)", name.p, lod.p, levels.p); break;
    case 7: SP(&e, "uvec4(uvec3(textureSize(%s, %s)).xz, 0u, %s)", name.p, lod.p, levels.p); break;
    case 3: SP(&e, "uvec4(uvec2(textureSize(%s, %s).xy), 0u, %s)", name.p, lod.p, levels.p); break;
    case 6: SP(&e, "uvec4(uvec2(textureSize(%s, %s)), 0u, %s)", name.p, lod.p, levels.p); break;
    case 4: SP(&e, "uvec4(uvec2(textureSize(%s)), 0u, 1u)", name.p); break;
    case 1: SP(&e, "uvec4(uint(textureSize(%s)), 0u, 0u, 1u)", name.p); break;
    default: SP(&e, "uvec4(uvec3(textureSize(%s, %s)), %s)", name.p, lod.p, levels.p); break;
    }
    u32 kind = in->ctrl & 3; // 0 flottant, 1 inverse flottant, 2 naturel
    Str v = {0};
    if (kind == 2) SP(&v, "%s", e.p);
    else if (kind == 1) SP(&v, "floatBitsToUint(vec4(1.0) / vec4(%s))", e.p);
    else SP(&v, "floatBitsToUint(vec4(%s))", e.p);
    // Mélange de l'opérande de ressource puis destination (bits).
    u8 comps[4];
    int n = dest_comps(&in->op[0], comps);
    char swz[5] = {0};
    for (int i = 0; i < n; i++) swz[i] = comp_name[res->swz[comps[i]]];
    Str w = {0};
    SP(&w, "(%s).%s", v.p, swz);
    dst_write(g, NULL, &in->op[0], comps, n, TU, w.p);
    free(name.p), free(lod.p), free(e.p), free(levels.p), free(v.p), free(w.p);
}

// --- Instructions ---

static void emit(Gen *g, const Ins *in) {
    Str t = {0};
    switch (in->opcode) {
    case OP_MOV: case OP_DMOV:
        if (in->sat) component_op(g, in, TF, TF, "%0"); // mov_sat : flottant borné
        else component_op(g, in, TU, TU, "%0");
        break;
    case OP_ADD: component_op(g, in, TF, TF, "(%0 + %1)"); break;
    case OP_MUL: component_op(g, in, TF, TF, "(%0 * %1)"); break;
    case OP_DIV: component_op(g, in, TF, TF, "(%0 / %1)"); break;
    case OP_MAD: component_op(g, in, TF, TF, "(%0 * %1 + %2)"); break;
    case OP_MIN: component_op(g, in, TF, TF, "min(%0, %1)"); break;
    case OP_MAX: component_op(g, in, TF, TF, "max(%0, %1)"); break;
    case OP_SQRT: component_op(g, in, TF, TF, "sqrt(%0)"); break;
    case OP_RSQ: component_op(g, in, TF, TF, "inversesqrt(%0)"); break;
    case OP_RCP: component_op(g, in, TF, TF, "(1.0 / %0)"); break;
    case OP_EXP: component_op(g, in, TF, TF, "exp2(%0)"); break;
    case OP_LOG: component_op(g, in, TF, TF, "log2(%0)"); break;
    case OP_FRC: component_op(g, in, TF, TF, "fract(%0)"); break;
    case OP_ROUND_NE: component_op(g, in, TF, TF, "roundEven(%0)"); break;
    case OP_ROUND_NI: component_op(g, in, TF, TF, "floor(%0)"); break;
    case OP_ROUND_PI: component_op(g, in, TF, TF, "ceil(%0)"); break;
    case OP_ROUND_Z: component_op(g, in, TF, TF, "trunc(%0)"); break;
    case OP_DERIV_RTX: case OP_DERIV_RTX_COARSE: case OP_DERIV_RTX_FINE:
        component_op(g, in, TF, TF, g->dx->stage == DXBC_PS ? "dFdx(%0)" : "(%0 * 0.0)");
        break;
    case OP_DERIV_RTY: case OP_DERIV_RTY_COARSE: case OP_DERIV_RTY_FINE:
        component_op(g, in, TF, TF, g->dx->stage == DXBC_PS ? "dFdy(%0)" : "(%0 * 0.0)");
        break;
    case OP_DP2: dot_op(g, in, 2); break;
    case OP_DP3: dot_op(g, in, 3); break;
    case OP_DP4: dot_op(g, in, 4); break;
    case OP_EQ: compare_op(g, in, TF, "equal", "=="); break;
    case OP_NE: compare_op(g, in, TF, "notEqual", "!="); break;
    case OP_LT: compare_op(g, in, TF, "lessThan", "<"); break;
    case OP_GE: compare_op(g, in, TF, "greaterThanEqual", ">="); break;
    case OP_IEQ: compare_op(g, in, TI, "equal", "=="); break;
    case OP_INE: compare_op(g, in, TI, "notEqual", "!="); break;
    case OP_ILT: compare_op(g, in, TI, "lessThan", "<"); break;
    case OP_IGE: compare_op(g, in, TI, "greaterThanEqual", ">="); break;
    case OP_ULT: compare_op(g, in, TU, "lessThan", "<"); break;
    case OP_UGE: compare_op(g, in, TU, "greaterThanEqual", ">="); break;
    case OP_IADD: component_op(g, in, TI, TI, "(%0 + %1)"); break;
    case OP_IMAD: component_op(g, in, TI, TI, "(%0 * %1 + %2)"); break;
    case OP_UMAD: component_op(g, in, TU, TU, "(%0 * %1 + %2)"); break;
    case OP_IMAX: component_op(g, in, TI, TI, "max(%0, %1)"); break;
    case OP_IMIN: component_op(g, in, TI, TI, "min(%0, %1)"); break;
    case OP_UMAX: component_op(g, in, TU, TU, "max(%0, %1)"); break;
    case OP_UMIN: component_op(g, in, TU, TU, "min(%0, %1)"); break;
    case OP_INEG: component_op(g, in, TI, TI, "(-%0)"); break;
    case OP_AND: component_op(g, in, TU, TU, "(%0 & %1)"); break;
    case OP_OR: component_op(g, in, TU, TU, "(%0 | %1)"); break;
    case OP_XOR: component_op(g, in, TU, TU, "(%0 ^ %1)"); break;
    case OP_NOT: component_op(g, in, TU, TU, "(~%0)"); break;
    case OP_ISHL: component_op(g, in, TU, TU, "(%0 << (%1 & 31u))"); break;
    case OP_USHR: component_op(g, in, TU, TU, "(%0 >> (%1 & 31u))"); break;
    case OP_ISHR: {
        // Décalage arithmétique : opérande entier, quantité naturelle.
        u8 comps[4];
        int n = dest_comps(&in->op[0], comps);
        if (!n) break;
        Str a = {0}, b = {0}, e = {0};
        src_expr(g, &a, &in->op[1], comps, n, TI);
        src_expr(g, &b, &in->op[2], comps, n, TU);
        SP(&e, "(%s >> %s(%s & 31u))", a.p, vec_type(TI, n), b.p);
        dst_write(g, NULL, &in->op[0], comps, n, TI, e.p);
        free(a.p), free(b.p), free(e.p);
        break;
    }
    case OP_FTOI: component_op(g, in, TF, TI, "%T(%0)"); break;
    case OP_FTOU: component_op(g, in, TF, TU, "%T(%0)"); break;
    case OP_ITOF: component_op(g, in, TI, TF, "%T(%0)"); break;
    case OP_UTOF: component_op(g, in, TU, TF, "%T(%0)"); break;
    case OP_COUNTBITS: component_op(g, in, TU, TI, "bitCount(%0)"); break;
    case OP_FIRSTBIT_LO: component_op(g, in, TU, TI, "findLSB(%0)"); break;
    case OP_FIRSTBIT_HI: {
        // D3D compte depuis le bit de poids fort ; GLSL depuis le bit 0.
        u8 comps[4];
        int n = dest_comps(&in->op[0], comps);
        Str a = {0}, e = {0};
        src_expr(g, &a, &in->op[1], comps, n, TU);
        if (n == 1) SP(&e, "(%s == 0u ? -1 : 31 - findMSB(%s))", a.p, a.p);
        else SP(&e, "mix(%s(31) - findMSB(%s), %s(-1), equal(%s, %s(0u)))", vec_type(TI, n), a.p, vec_type(TI, n), a.p, vec_type(TU, n));
        dst_write(g, NULL, &in->op[0], comps, n, TI, e.p);
        free(a.p), free(e.p);
        break;
    }
    case OP_FIRSTBIT_SHI: {
        u8 comps[4];
        int n = dest_comps(&in->op[0], comps);
        Str a = {0}, e = {0};
        src_expr(g, &a, &in->op[1], comps, n, TI);
        if (n == 1) SP(&e, "(findMSB(%s) < 0 ? -1 : 31 - findMSB(%s))", a.p, a.p);
        else SP(&e, "mix(%s(31) - findMSB(%s), %s(-1), lessThan(findMSB(%s), %s(0)))", vec_type(TI, n), a.p, vec_type(TI, n), a.p, vec_type(TI, n));
        dst_write(g, NULL, &in->op[0], comps, n, TI, e.p);
        free(a.p), free(e.p);
        break;
    }
    case OP_BFREV: component_op(g, in, TU, TU, "bitfieldReverse(%0)"); break;
    case OP_UBFE: component_op(g, in, TU, TU, "bitfieldExtract(%2, int(%1 & 31u), int(%0 & 31u))"); break;
    case OP_IBFE: {
        u8 comps[4];
        int n = dest_comps(&in->op[0], comps);
        if (!n) break;
        Str w = {0}, o = {0}, v = {0}, e = {0};
        src_expr(g, &w, &in->op[1], comps, n, TU);
        src_expr(g, &o, &in->op[2], comps, n, TU);
        src_expr(g, &v, &in->op[3], comps, n, TI);
        if (n == 1) SP(&e, "bitfieldExtract(%s, int(%s & 31u), int(%s & 31u))", v.p, o.p, w.p);
        else {
            // bitfieldExtract prend des décalages scalaires : composante par composante.
            SP(&e, "%s(", vec_type(TI, n));
            for (int i = 0; i < n; i++) {
                Str wi = {0}, oi = {0}, vi = {0};
                src_expr(g, &wi, &in->op[1], &comps[i], 1, TU);
                src_expr(g, &oi, &in->op[2], &comps[i], 1, TU);
                src_expr(g, &vi, &in->op[3], &comps[i], 1, TI);
                SP(&e, "%sbitfieldExtract(%s, int(%s & 31u), int(%s & 31u))", i ? ", " : "", vi.p, oi.p, wi.p);
                free(wi.p), free(oi.p), free(vi.p);
            }
            sputs(&e, ")");
        }
        dst_write(g, NULL, &in->op[0], comps, n, TI, e.p);
        free(w.p), free(o.p), free(v.p), free(e.p);
        break;
    }
    case OP_BFI: {
        // bfi dst, largeur, décalage, source, base : composante par composante.
        u8 comps[4];
        int n = dest_comps(&in->op[0], comps);
        if (!n) break;
        Str e = {0};
        if (n > 1) SP(&e, "%s(", vec_type(TU, n));
        for (int i = 0; i < n; i++) {
            Str w = {0}, o = {0}, ins = {0}, base = {0};
            src_expr(g, &w, &in->op[1], &comps[i], 1, TU);
            src_expr(g, &o, &in->op[2], &comps[i], 1, TU);
            src_expr(g, &ins, &in->op[3], &comps[i], 1, TU);
            src_expr(g, &base, &in->op[4], &comps[i], 1, TU);
            SP(&e, "%sbitfieldInsert(%s, %s, int(%s & 31u), int(%s & 31u))", i ? ", " : "", base.p, ins.p, o.p, w.p);
            free(w.p), free(o.p), free(ins.p), free(base.p);
        }
        if (n > 1) sputs(&e, ")");
        dst_write(g, NULL, &in->op[0], comps, n, TU, e.p);
        free(e.p);
        break;
    }
    case OP_F32TOF16: case OP_F16TOF32: {
        // Fonctions scalaires (packHalf2x16 / unpackHalf2x16) : composante par composante.
        int to16 = in->opcode == OP_F32TOF16;
        u8 comps[4];
        int n = dest_comps(&in->op[0], comps);
        if (!n) break;
        Str e = {0};
        if (n > 1) SP(&e, "%s(", vec_type(to16 ? TU : TF, n));
        for (int i = 0; i < n; i++) {
            Str a = {0};
            src_expr(g, &a, &in->op[1], &comps[i], 1, to16 ? TF : TU);
            if (to16) SP(&e, "%s(packHalf2x16(vec2(%s, 0.0)) & 0xFFFFu)", i ? ", " : "", a.p);
            else SP(&e, "%sunpackHalf2x16(%s & 0xFFFFu).x", i ? ", " : "", a.p);
            free(a.p);
        }
        if (n > 1) sputs(&e, ")");
        dst_write(g, NULL, &in->op[0], comps, n, to16 ? TU : TF, e.p);
        free(e.p);
        break;
    }
    case OP_MOVC: {
        u8 comps[4];
        int n = dest_comps(&in->op[0], comps);
        if (!n) break;
        Str e = {0};
        if (n > 1) SP(&e, "%s(", vec_type(TU, n));
        for (int i = 0; i < n; i++) {
            Str c = {0}, a = {0}, b = {0};
            src_expr(g, &c, &in->op[1], &comps[i], 1, TU);
            src_expr(g, &a, &in->op[2], &comps[i], 1, TU);
            src_expr(g, &b, &in->op[3], &comps[i], 1, TU);
            SP(&e, "%s(%s != 0u ? %s : %s)", i ? ", " : "", c.p, a.p, b.p);
            free(c.p), free(a.p), free(b.p);
        }
        if (n > 1) sputs(&e, ")");
        // movc_sat : saturation des flottants.
        if (in->sat) {
            Str f = {0};
            SP(&f, "uintBitsToFloat(%s)", e.p);
            dst_write(g, in, &in->op[0], comps, n, TF, f.p);
            free(f.p);
        } else {
            dst_write(g, NULL, &in->op[0], comps, n, TU, e.p);
        }
        free(e.p);
        break;
    }
    case OP_SWAPC: {
        // swapc d0, d1, cond, a, b : d0 = cond ? b : a ; d1 = cond ? a : b.
        for (int k = 0; k < 2; k++) {
            u8 comps[4];
            int n = dest_comps(&in->op[k], comps);
            if (!n || in->op[k].type == T_NULL) continue;
            Str e = {0};
            if (n > 1) SP(&e, "%s(", vec_type(TU, n));
            for (int i = 0; i < n; i++) {
                Str c = {0}, a = {0}, b = {0};
                src_expr(g, &c, &in->op[2], &comps[i], 1, TU);
                src_expr(g, &a, &in->op[3], &comps[i], 1, TU);
                src_expr(g, &b, &in->op[4], &comps[i], 1, TU);
                SP(&e, "%s(%s != 0u ? %s : %s)", i ? ", " : "", c.p, k == 0 ? b.p : a.p, k == 0 ? a.p : b.p);
                free(c.p), free(a.p), free(b.p);
            }
            if (n > 1) sputs(&e, ")");
            dst_write(g, NULL, &in->op[k], comps, n, TU, e.p);
            free(e.p);
        }
        break;
    }
    case OP_SINCOS: {
        // sincos sin, cos, source : destinations facultatives (null).
        for (int k = 0; k < 2; k++) {
            if (in->op[k].type == T_NULL) continue;
            u8 comps[4];
            int n = dest_comps(&in->op[k], comps);
            Str a = {0}, e = {0};
            src_expr(g, &a, &in->op[2], comps, n, TF);
            SP(&e, "%s(%s)", k == 0 ? "sin" : "cos", a.p);
            dst_write(g, in, &in->op[k], comps, n, TF, e.p);
            free(a.p), free(e.p);
        }
        break;
    }
    case OP_IMUL: case OP_UMUL: {
        // imul hi, lo, a, b : poids fort (rarement employé) et poids faible.
        int type = in->opcode == OP_IMUL ? TI : TU;
        for (int k = 0; k < 2; k++) {
            if (in->op[k].type == T_NULL) continue;
            u8 comps[4];
            int n = dest_comps(&in->op[k], comps);
            Str a = {0}, b = {0}, e = {0};
            src_expr(g, &a, &in->op[2], comps, n, type);
            src_expr(g, &b, &in->op[3], comps, n, type);
            if (k == 1) {
                SP(&e, "(%s * %s)", a.p, b.p);
            } else {
                // Poids fort : imulExtended / umulExtended n'existent qu'en instruction ; approximation par 64 bits absente en ES.
                SP(&e, "%s(0)", vec_type(type, n));
            }
            dst_write(g, NULL, &in->op[k], comps, n, type, e.p);
            free(a.p), free(b.p), free(e.p);
        }
        break;
    }
    case OP_UDIV: {
        // udiv quotient, reste, a, b (division par zéro : 0xFFFFFFFF, comme Direct3D).
        for (int k = 0; k < 2; k++) {
            if (in->op[k].type == T_NULL) continue;
            u8 comps[4];
            int n = dest_comps(&in->op[k], comps);
            Str e = {0};
            if (n > 1) SP(&e, "%s(", vec_type(TU, n));
            for (int i = 0; i < n; i++) {
                Str a = {0}, b = {0};
                src_expr(g, &a, &in->op[2], &comps[i], 1, TU);
                src_expr(g, &b, &in->op[3], &comps[i], 1, TU);
                SP(&e, "%s(%s == 0u ? 0xFFFFFFFFu : %s %s %s)", i ? ", " : "", b.p, a.p, k == 0 ? "/" : "%", b.p);
                free(a.p), free(b.p);
            }
            if (n > 1) sputs(&e, ")");
            dst_write(g, NULL, &in->op[k], comps, n, TU, e.p);
            free(e.p);
        }
        break;
    }
    case OP_UADDC: case OP_USUBB: {
        for (int k = 0; k < 2; k++) {
            if (in->op[k].type == T_NULL) continue;
            u8 comps[4];
            int n = dest_comps(&in->op[k], comps);
            Str a = {0}, b = {0}, e = {0};
            src_expr(g, &a, &in->op[2], comps, n, TU);
            src_expr(g, &b, &in->op[3], comps, n, TU);
            int add = in->opcode == OP_UADDC;
            if (k == 0) SP(&e, "(%s %s %s)", a.p, add ? "+" : "-", b.p);
            else if (n == 1) SP(&e, add ? "((%s + %s) < %s ? 1u : 0u)" : "(%s < %s ? 1u : 0u)", a.p, b.p, a.p);
            else SP(&e, add ? "%s(lessThan(%s + %s, %s))" : "%s(lessThan(%s, %s))", vec_type(TU, n), a.p, b.p, a.p);
            dst_write(g, NULL, &in->op[k], comps, n, TU, e.p);
            free(a.p), free(b.p), free(e.p);
        }
        break;
    }
    // --- Flot de contrôle ---
    case OP_IF:
        condition(g, &t, in, &in->op[0]);
        indent(g);
        SP(&g->s, "if %s {\n", t.p);
        g->depth++;
        break;
    case OP_ELSE:
        g->depth--;
        indent(g);
        sputs(&g->s, "} else {\n");
        g->depth++;
        break;
    case OP_ENDIF: case OP_ENDLOOP: case OP_ENDSWITCH:
        g->depth--;
        indent(g);
        sputs(&g->s, "}\n");
        break;
    case OP_LOOP:
        indent(g);
        sputs(&g->s, "while (true) {\n");
        g->depth++;
        break;
    case OP_BREAK: indent(g); sputs(&g->s, "break;\n"); break;
    case OP_CONTINUE: indent(g); sputs(&g->s, "continue;\n"); break;
    case OP_BREAKC: case OP_CONTINUEC: case OP_RETC: case OP_DISCARD:
        condition(g, &t, in, &in->op[0]);
        indent(g);
        SP(&g->s, "if %s %s;\n", t.p, in->opcode == OP_BREAKC ? "break" : in->opcode == OP_CONTINUEC ? "continue" : in->opcode == OP_RETC ? "return" : "discard");
        break;
    case OP_RET:
        indent(g);
        sputs(&g->s, "return;\n");
        break;
    case OP_SWITCH: {
        u8 c = 0;
        src_expr(g, &t, &in->op[0], &c, 1, TI);
        indent(g);
        SP(&g->s, "switch (%s) {\n", t.p);
        g->depth++;
        break;
    }
    case OP_CASE:
        indent(g);
        SP(&g->s, "case %d:\n", (i32)in->op[0].imm[0]);
        break;
    case OP_DEFAULT:
        indent(g);
        sputs(&g->s, "default:\n");
        break;
    case OP_NOP: case OP_SYNC: case OP_HS_DECLS: break;
    // --- Textures ---
    case OP_SAMPLE: case OP_SAMPLE_L: case OP_SAMPLE_B: case OP_SAMPLE_D: case OP_SAMPLE_C: case OP_SAMPLE_C_LZ:
        sample_op(g, in);
        break;
    case OP_GATHER4: case OP_GATHER4_C: case OP_GATHER4_PO: case OP_GATHER4_PO_C: gather_op(g, in); break;
    case OP_LD: case OP_LD_MS: ld_op(g, in); break;
    case OP_RESINFO: resinfo_op(g, in); break;
    case OP_EVAL_CENTROID: component_op(g, in, TF, TF, "%0"); break;
    default:
        if (operand_count(in->opcode) >= 0) fail(g, "instruction %u non traduite", in->opcode);
        break;
    }
    free(t.p);
}

// --- Assemblage ---

static int is_sv(const DxbcSig *s, const char *name) {
    return !strncasecmp(s->name, name, strlen(name)) && strlen(s->name) == strlen(name);
}
static int comp_count(u32 mask) { return (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1) + ((mask >> 3) & 1); }
static int first_comp(u32 mask) {
    for (int i = 0; i < 4; i++) if (mask & (1u << i)) return i;
    return 0;
}
static int sig_type(const DxbcSig *s) { return s->type == 1 ? TU : s->type == 2 ? TI : TF; }
static void varying_name(Str *s, const DxbcSig *e) {
    sputs(s, "v_");
    for (const char *p = e->name; *p; p++) {
        char c[2] = {(*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p, 0};
        if (!((c[0] >= 'A' && c[0] <= 'Z') || (c[0] >= '0' && c[0] <= '9') || c[0] == '_')) c[0] = '_';
        sputs(s, c);
    }
    SP(s, "%u", e->index);
}
static const DxbcSig *find_sig(const DxbcSig *list, u32 count, const DxbcSig *e) {
    for (u32 i = 0; list && i < count; i++) {
        if (list[i].index == e->index && !strcasecmp(list[i].name, e->name)) return &list[i];
    }
    return NULL;
}
// Bits d'une valeur d'un type vers uint (et l'inverse).
static const char *to_bits(int type) { return type == TF ? "floatBitsToUint" : type == TI ? "uvec4" : ""; }
static const char *from_bits(int type) { return type == TF ? "uintBitsToFloat" : type == TI ? "ivec4" : ""; }

static void interp_qualifier(Str *s, const DxbcSig *ps_in, int type, int es) {
    if (type != TF || (ps_in && ps_in->interp == 1)) {
        sputs(s, "flat ");
        return;
    }
    if (ps_in && (ps_in->interp == 3 || ps_in->interp == 5)) sputs(s, "centroid ");
    if (!es && ps_in && (ps_in->interp == 4 || ps_in->interp == 5)) sputs(s, "noperspective ");
}

char *dxbc_glsl(const DxbcShader *dx, const DxbcShader *prev, const DxbcShader *next_stage, int es) {
    Gen g = {0};
    g.dx = dx;
    g.pv = dx->priv;
    g.prev = prev;
    g.next = next_stage;
    g.es = es;
    g.pfx = dx->stage == DXBC_VS ? "vs" : dx->stage == DXBC_PS ? "ps" : dx->stage == DXBC_GS ? "gs" : "cs";
    const DxbcPriv *pv = dx->priv;
    Str *s = &g.s;
    if (dx->stage != DXBC_VS && dx->stage != DXBC_PS) {
        snprintf(((DxbcShader *)dx)->error, sizeof dx->error, "étape %u non traduite", dx->stage);
        return NULL;
    }
    // En-tête.
    if (es) {
        sputs(s, "#version 310 es\n");
        int buffers = 0, cube_arrays = 0;
        for (u32 i = 0; i < dx->pair_count; i++) buffers |= dx->pairs[i].dim == 1, cube_arrays |= dx->pairs[i].dim == 10;
        if (buffers) sputs(s, "#extension GL_EXT_texture_buffer : enable\n");
        if (cube_arrays) sputs(s, "#extension GL_EXT_texture_cube_map_array : enable\n");
        sputs(s, "precision highp float;\nprecision highp int;\n");
    } else {
        sputs(s, "#version 430\n");
    }
    // Tampons de constantes.
    for (u32 slot = 0; slot < 14; slot++) {
        if (!(dx->cb_mask & (1u << slot))) continue;
        u32 size = dx->cb_size[slot] ? dx->cb_size[slot] : 1;
        if (size > 4096) size = 4096;
        if (es && size > 1024) size = 1024;
        SP(s, "layout(std140, binding = %u) uniform CB%u_%u { uvec4 %s_c%u[%u]; };\n", D3D_CB_BINDING(dx->stage, slot), dx->stage, slot, g.pfx, slot, size);
    }
    // Textures.
    for (u32 i = 0; i < dx->pair_count; i++) {
        const DxbcPair *p = &dx->pairs[i];
        Str name = {0};
        sampler_name(&g, &name, p);
        SP(s, "layout(binding = %u) uniform highp %s %s;\n", p->unit, sampler_type(p), name.p);
        free(name.p);
    }
    // Constantes immédiates.
    if (pv->icb_count >= 4) {
        u32 n = pv->icb_count / 4;
        SP(s, "const uvec4 icb[%u] = uvec4[%u](", n, n);
        for (u32 i = 0; i < n; i++)
            SP(s, "%suvec4(0x%08xu, 0x%08xu, 0x%08xu, 0x%08xu)", i ? ", " : "", pv->icb[4 * i], pv->icb[4 * i + 1], pv->icb[4 * i + 2], pv->icb[4 * i + 3]);
        sputs(s, ");\n");
    }
    // Entrées.
    u32 nv = 1, no = 1;
    for (u32 i = 0; i < dx->in_count; i++) if (dx->in[i].reg < 32 && dx->in[i].reg + 1 > nv) nv = dx->in[i].reg + 1;
    for (u32 i = 0; i < dx->out_count; i++) if (dx->out[i].reg < 32 && dx->out[i].reg + 1 > no) no = dx->out[i].reg + 1;
    for (u32 i = 0; i < 32; i++) {
        if (pv->in_sysval[i] && i + 1 > nv) nv = i + 1;
        if (pv->out_sysval[i] && i + 1 > no) no = i + 1;
    }
    for (u32 i = 0; i < dx->in_count; i++) {
        const DxbcSig *e = &dx->in[i];
        int type = sig_type(e);
        if (dx->stage == DXBC_VS) {
            if (e->sysval) continue;
            SP(s, "layout(location = %u) in %s a%u;\n", e->reg, vec_type(type, 4), e->reg);
        } else {
            if (e->sysval || !strncasecmp(e->name, "SV_", 3)) continue;
            if (prev && !find_sig(prev->out, prev->out_count, e)) continue; // non écrite : zéro
            Str name = {0};
            varying_name(&name, e);
            interp_qualifier(s, e, type, es);
            SP(s, "in %s %s;\n", vec_type(type, 4), name.p);
            free(name.p);
        }
    }
    // Sorties.
    for (u32 i = 0; i < dx->out_count; i++) {
        const DxbcSig *e = &dx->out[i];
        int type = sig_type(e);
        if (dx->stage == DXBC_VS) {
            if (e->sysval || !strncasecmp(e->name, "SV_", 3)) continue;
            Str name = {0};
            varying_name(&name, e);
            const DxbcSig *ps_in = next_stage ? find_sig(next_stage->in, next_stage->in_count, e) : NULL;
            interp_qualifier(s, ps_in, type, es);
            SP(s, "out %s %s;\n", vec_type(type, 4), name.p);
            free(name.p);
        } else if (is_sv(e, "SV_Target")) {
            SP(s, "layout(location = %u) out %s o_target%u;\n", e->index, vec_type(type, 4), e->index);
        }
    }
    SP(s, "uvec4 v[%u];\nuvec4 o[%u];\nuint o_depth;\n", nv, no);
    // Corps.
    sputs(s, "void shader_main() {\n");
    for (u32 i = 0; i < pv->temps; i++) SP(s, "    uvec4 r%u = uvec4(0u);\n", i);
    for (u32 i = 0; i < pv->xt_count; i++) SP(s, "    uvec4 x%u[%u];\n", pv->xt[i].reg, pv->xt[i].size ? pv->xt[i].size : 1);
    for (u32 i = 0; i < pv->count && !g.fail; i++) {
        const Ins *in = &pv->ins[i];
        if (operand_count(in->opcode) < 0) continue; // déclarations
        emit(&g, in);
    }
    sputs(s, "}\n");
    // main : entrées, corps, sorties.
    sputs(s, "void main() {\n");
    for (u32 i = 0; i < nv; i++) SP(s, "    v[%u] = uvec4(0u);\n", i);
    for (u32 i = 0; i < no; i++) SP(s, "    o[%u] = uvec4(0u);\n", i);
    sputs(s, "    o_depth = 0u;\n");
    for (u32 i = 0; i < dx->in_count; i++) {
        const DxbcSig *e = &dx->in[i];
        int type = sig_type(e), k = comp_count(e->mask), f = first_comp(e->mask);
        char dst[5] = {0}, src[5] = {0};
        for (int c = 0; c < k; c++) dst[c] = comp_name[f + c], src[c] = comp_name[c];
        if (dx->stage == DXBC_VS) {
            if (e->sysval == 6) SP(s, "    v[%u].%c = uint(gl_VertexID);\n", e->reg, comp_name[f]);
            else if (e->sysval == 8) SP(s, "    v[%u].%c = uint(gl_InstanceID);\n", e->reg, comp_name[f]);
            else if (!e->sysval) SP(s, "    v[%u].%s = %s(a%u).%s;\n", e->reg, dst, to_bits(type), e->reg, dst);
            continue;
        }
        // Pixels.
        if (e->sysval == 1 || is_sv(e, "SV_Position")) {
            SP(s, "    v[%u].%s = floatBitsToUint(vec4(gl_FragCoord.xyz, 1.0 / gl_FragCoord.w)).%s;\n", e->reg, dst, dst);
        } else if (e->sysval == 9 || is_sv(e, "SV_IsFrontFace")) {
            SP(s, "    v[%u].%c = gl_FrontFacing ? 0xFFFFFFFFu : 0u;\n", e->reg, comp_name[f]);
        } else if (e->sysval == 7 || is_sv(e, "SV_PrimitiveID")) {
            if (!es) SP(s, "    v[%u].%c = uint(gl_PrimitiveID);\n", e->reg, comp_name[f]);
        } else if (e->sysval == 10 || is_sv(e, "SV_SampleIndex")) {
            SP(s, "    v[%u].%c = uint(gl_SampleID);\n", e->reg, comp_name[f]);
        } else if (!strncasecmp(e->name, "SV_", 3)) {
            // Autres valeurs système : zéro.
        } else if (!prev || find_sig(prev->out, prev->out_count, e)) {
            Str name = {0};
            varying_name(&name, e);
            SP(s, "    v[%u].%s = %s(%s).%s;\n", e->reg, dst, to_bits(type), name.p, src);
            free(name.p);
        }
    }
    sputs(s, "    shader_main();\n");
    for (u32 i = 0; i < dx->out_count; i++) {
        const DxbcSig *e = &dx->out[i];
        int type = sig_type(e), k = comp_count(e->mask), f = first_comp(e->mask);
        char dst[5] = {0}, src[5] = {0};
        for (int c = 0; c < k; c++) dst[c] = comp_name[c], src[c] = comp_name[f + c];
        if (dx->stage == DXBC_VS) {
            if (e->sysval == 1 || is_sv(e, "SV_Position")) {
                SP(s, "    gl_Position = uintBitsToFloat(o[%u]);\n", e->reg);
                sputs(s, "    gl_Position.y = -gl_Position.y;\n    gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;\n");
            } else if (!e->sysval && strncasecmp(e->name, "SV_", 3)) {
                Str name = {0};
                varying_name(&name, e);
                SP(s, "    %s = %s(0);\n", name.p, vec_type(type, 4));
                SP(s, "    %s.%s = %s(o[%u].%s);\n", name.p, dst, type == TF ? "uintBitsToFloat" : type == TI ? vec_type(TI, k) : "", e->reg, src);
                free(name.p);
            }
        } else if (is_sv(e, "SV_Target")) {
            SP(s, "    o_target%u = %s(o[%u]);\n", e->index, from_bits(type), e->reg);
        } else if (!strncasecmp(e->name, "SV_Depth", 8)) {
            sputs(s, "    gl_FragDepth = uintBitsToFloat(o_depth);\n");
        }
    }
    sputs(s, "}\n");
    if (g.fail) {
        snprintf(((DxbcShader *)dx)->error, sizeof dx->error, "%s", g.error);
        free(g.s.p);
        return NULL;
    }
    return g.s.p;
}
