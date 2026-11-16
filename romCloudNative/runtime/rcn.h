// RomCloud Native : interface entre le code traduit par rcn-recomp et le runtime.
//
// Le code traduit manipule un processeur x86-64 « à plat » : registres dans RcnCtx, mémoire du jeu aux
// adresses de l'hôte (l'image de l'exe est chargée telle quelle, les pointeurs du jeu sont de vrais
// pointeurs). Ce fichier fournit les accès mémoire et les instructions trop longues pour être émises
// en ligne (SSE, divisions…). À compiler avec -fno-strict-aliasing -ffp-contract=off (résultats
// flottants identiques à ceux du x86 : pas de multiplication-addition fusionnée sur ARM64).
#ifndef RCN_H
#define RCN_H

#include <math.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef union {
    u64 q[2];
    u32 d[4];
    u16 w[8];
    u8 b[16];
    i32 sd[4];
    i16 sw[8];
    i8 sb[16];
    float f[4];
    double db[2];
} RcnXmm;

struct RcnProcess;

// État du processeur x86-64 du jeu. r[] dans l'ordre du codage x86 :
// rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8 … r15.
typedef struct RcnCtx {
    u64 r[16];
    RcnXmm xmm[16];
    u8 cf, pf, af, zf, sf, of, df;
    u32 mxcsr;
    u64 base;       // adresse de chargement de l'image
    u64 image_size;
    u64 fs_base, gs_base; // bloc d'environnement du thread (TEB) : gs en 64 bits, fs en 32 bits
    struct RcnProcess *proc;
    // Unité flottante x87 : pile de huit registres (sommet : fp_top), mots d'état et de contrôle.
    double st[8];
    u8 fp_top;
    u16 fp_sw, fp_cw;
    u8 bits32; // programme x86 32 bits : registres et adresses sur 32 bits
    // Code machine traduit (rcn_native.c) : longjmp en cours, remonté appel par appel jusqu'à la fonction
    // dont le jeton est `unwind`, qui reprend au point de setjmp numéro `unwind_site`.
    u64 unwind;
    u32 unwind_site;
    // Reprise non locale (unwind == RCN_UNWIND_RESUME) : la fonction dont le cadre contient resume_rsp y
    // reprend, à resume_rip (fin d'un catch, RtlUnwindEx, RtlRestoreContext).
    u64 resume_rsp, resume_rip;
} RcnCtx;
#define RCN_UNWIND_RESUME 1

// Le code machine traduit (recompiler/src/native.rs) accède à ces champs par leur position.
#define RCN_CHECK_OFFSET(field, offset) _Static_assert(offsetof(RcnCtx, field) == offset, "RcnCtx." #field)
RCN_CHECK_OFFSET(xmm, 128);
RCN_CHECK_OFFSET(cf, 384);
RCN_CHECK_OFFSET(df, 390);
RCN_CHECK_OFFSET(mxcsr, 392);
RCN_CHECK_OFFSET(base, 400);
RCN_CHECK_OFFSET(fs_base, 416);
RCN_CHECK_OFFSET(gs_base, 424);
RCN_CHECK_OFFSET(st, 440);
RCN_CHECK_OFFSET(fp_top, 504);
RCN_CHECK_OFFSET(fp_sw, 506);
RCN_CHECK_OFFSET(fp_cw, 508);
RCN_CHECK_OFFSET(unwind, 512);
RCN_CHECK_OFFSET(unwind_site, 520);
RCN_CHECK_OFFSET(resume_rsp, 528);
RCN_CHECK_OFFSET(resume_rip, 536);

#define RCN_ABI_VERSION 4

typedef void (*RcnFn)(RcnCtx *ctx);
typedef struct {
    u32 rva;
    RcnFn fn;
} RcnFuncEntry;

// Décrit le code traduit d'un exécutable (produit par rcn-recomp).
typedef struct {
    u32 abi;
    const char *name;
    u32 bits;       // 32 (x86) ou 64 (x86-64)
    u64 image_base; // adresse de chargement prévue par l'exe
    u32 image_size;
    u32 timestamp;
    u32 entry_rva;
    const RcnFuncEntry *funcs; // triées par RVA
    u32 func_count;
    // Code machine traduit (fichier .rcnx) : messages d'arrêt, chargés avec le code.
    u32 native;
    const char *const *messages;
    u32 message_count;
    // Code machine : adresse où le module est chargé, lue par chaque fonction traduite (symbole « @base »).
    u64 base_slot;
    // Toujours 0, lu juste après base_slot : valeur que le compilateur ne peut pas deviner (voir ctx_param
    // dans recompiler/src/native.rs).
    u64 zero_slot;
} RcnModule;

#if defined(_WIN32)
#define RCN_EXPORT
#else
#define RCN_EXPORT __attribute__((visibility("default")))
#endif

// Appel ou saut indirect : fonction traduite (adresse dans l'image) ou import fourni par l'hôte.
void rcn_call(RcnCtx *ctx, u64 target);
// Arrêt du programme sur une situation non gérée (instruction, import, adresse inconnue).
_Noreturn void rcn_trap(RcnCtx *ctx, u64 rva, const char *what);
// setjmp du jeu : enregistre ses registres et renvoie le point de reprise à donner au setjmp de l'hôte,
// appelé par le code traduit lui-même (il doit l'être dans la fonction qui reste active).
jmp_buf *rcn_setjmp(RcnCtx *ctx, u64 guest_buf);
void rcn_cpuid(RcnCtx *ctx);
void rcn_rdtsc(RcnCtx *ctx);
// cmpxchg16b [addr] : comparaison de rdx:rax avec 16 octets, échange avec rcx:rbx, résultat dans ZF.
void rcn_op_cmpxchg16b(RcnCtx *ctx, u64 addr);
// rcl / rcr : rotation de `value` (size octets) à travers CF ; info = taille | 0x100 pour rcl. CF et OF mis à jour.
u64 rcn_op_rcx(RcnCtx *ctx, u64 value, u64 count, u32 info);
// Opération atomique sur une adresse non alignée, sous verrou (ARM64).
u64 rcn_op_atomic_unaligned(u64 addr, u32 op, u32 size, u64 value, u64 expected);
// fxsave / fxrstor : zone de 512 octets (x87, MXCSR, xmm0 … xmm15).
void rcn_op_fxsave(RcnCtx *ctx, u64 addr);
void rcn_op_fxrstor(RcnCtx *ctx, u64 addr);
// Registre des drapeaux (eflags) recomposé à partir des drapeaux tenus séparément, et l'inverse.
u32 rcn_get_flags(RcnCtx *ctx);
void rcn_set_flags(RcnCtx *ctx, u64 flags);
// Flottants de 80 bits du x87 en mémoire ↔ double.
double rcn_ld80(u64 addr);
void rcn_st80(u64 addr, double value);
// fxam : classe de ST(0) dans le mot d'état.
void rcn_fxam(RcnCtx *ctx);
// Instructions sur chaînes ; rep : 0 aucun préfixe, 1 rep / repe, 2 repne.
void rcn_movs(RcnCtx *ctx, int size, int rep);
void rcn_stos(RcnCtx *ctx, int size, int rep);
void rcn_lods(RcnCtx *ctx, int size, int rep);
void rcn_cmps(RcnCtx *ctx, int size, int rep);
void rcn_scas(RcnCtx *ctx, int size, int rep);

#define R(n) (ctx->r[n])
#define X(n) (ctx->xmm[n])
#define PUSH64(v) do { ctx->r[4] -= 8; wr64(ctx->r[4], (v)); } while (0)
#define PUSH32(v) do { ctx->r[4] = (u32)(ctx->r[4] - 4); wr32(ctx->r[4], (u32)(v)); } while (0)
// Registre x87 : ST(0) est le sommet de la pile.
#define ST(i) (ctx->st[(ctx->fp_top + (i)) & 7])

// --- Mémoire du jeu (accès non alignés permis, comme sur x86) ---
#define RCN_MEM(bits, type)                                                               \
    static inline type rd##bits(u64 a) { type v; memcpy(&v, (const void *)(uintptr_t)a, sizeof v); return v; } \
    static inline void wr##bits(u64 a, type v) { memcpy((void *)(uintptr_t)a, &v, sizeof v); }
RCN_MEM(8, u8)
RCN_MEM(16, u16)
RCN_MEM(32, u32)
RCN_MEM(64, u64)
RCN_MEM(f32, float)
RCN_MEM(f64, double)
RCN_MEM(x, RcnXmm)

static inline u8 rcn_parity(u8 v) { return !(__builtin_popcount(v) & 1); }

// --- Divisions (rdx:rax / opérande) ---
static inline void rcn_div64(RcnCtx *ctx, u64 b, u64 rva) {
    if (b == 0) rcn_trap(ctx, rva, "division par zero");
    if (ctx->r[2] == 0) {
        u64 a = ctx->r[0];
        ctx->r[0] = a / b;
        ctx->r[2] = a % b;
        return;
    }
    unsigned __int128 n = ((unsigned __int128)ctx->r[2] << 64) | ctx->r[0];
    unsigned __int128 q = n / b;
    if (q >> 64) rcn_trap(ctx, rva, "debordement de division");
    ctx->r[0] = (u64)q;
    ctx->r[2] = (u64)(n % b);
}
static inline void rcn_idiv64(RcnCtx *ctx, u64 ub, u64 rva) {
    i64 b = (i64)ub;
    if (b == 0) rcn_trap(ctx, rva, "division par zero");
    __int128 n = (__int128)(((unsigned __int128)ctx->r[2] << 64) | ctx->r[0]);
    if (n == (__int128)(i64)n) {
        i64 a = (i64)n;
        if (a == INT64_MIN && b == -1) rcn_trap(ctx, rva, "debordement de division");
        ctx->r[0] = (u64)(a / b);
        ctx->r[2] = (u64)(a % b);
        return;
    }
    __int128 q = n / b;
    if (q != (__int128)(i64)q) rcn_trap(ctx, rva, "debordement de division");
    ctx->r[0] = (u64)(i64)q;
    ctx->r[2] = (u64)(i64)(n % b);
}
static inline void rcn_div32(RcnCtx *ctx, u32 b, u64 rva) {
    if (b == 0) rcn_trap(ctx, rva, "division par zero");
    u64 n = ((u64)(u32)ctx->r[2] << 32) | (u32)ctx->r[0];
    u64 q = n / b;
    if (q >> 32) rcn_trap(ctx, rva, "debordement de division");
    ctx->r[0] = (u32)q;
    ctx->r[2] = (u32)(n % b);
}
static inline void rcn_idiv32(RcnCtx *ctx, u32 ub, u64 rva) {
    i64 b = (i32)ub;
    if (b == 0) rcn_trap(ctx, rva, "division par zero");
    i64 n = (i64)(((u64)(u32)ctx->r[2] << 32) | (u32)ctx->r[0]);
    i64 q = n / b;
    if (q != (i64)(i32)q) rcn_trap(ctx, rva, "debordement de division");
    ctx->r[0] = (u32)q;
    ctx->r[2] = (u32)(n % b);
}

// --- x87 ---
static inline void rcn_fpush(RcnCtx *ctx, double v) {
    ctx->fp_top = (ctx->fp_top - 1) & 7;
    ctx->st[ctx->fp_top] = v;
}
static inline void rcn_fpop(RcnCtx *ctx) { ctx->fp_top = (ctx->fp_top + 1) & 7; }
// Comparaison : C3 (bit 14), C2 (10), C0 (8) du mot d'état — plus petit : C0 ; égal : C3 ; non ordonné : les trois.
static inline void rcn_fcom(RcnCtx *ctx, double a, double b) {
    u16 sw = ctx->fp_sw & ~0x4500;
    if (a != a || b != b) sw |= 0x4500;
    else if (a < b) sw |= 0x0100;
    else if (a == b) sw |= 0x4000;
    ctx->fp_sw = sw;
}
static inline u16 rcn_fsw(RcnCtx *ctx) { return (u16)((ctx->fp_sw & ~0x3800) | (ctx->fp_top << 11)); }
// Arrondi selon le mot de contrôle (bits 10-11) : au plus proche, vers le bas, vers le haut, troncature.
static inline double rcn_fround(RcnCtx *ctx, double v) {
    switch ((ctx->fp_cw >> 10) & 3) {
    case 0: return rint(v);
    case 1: return floor(v);
    case 2: return ceil(v);
    default: return trunc(v);
    }
}
// fist / fisttp : hors plage ou NaN, « entier indéfini » (plus petit entier).
#define RCN_FIST(bits, lo, hi)                                                                 \
    static inline i##bits rcn_fist##bits(RcnCtx *ctx, double v) {                              \
        v = rcn_fround(ctx, v);                                                                \
        return (v >= lo && v < hi) ? (i##bits)v : INT##bits##_MIN;                             \
    }                                                                                          \
    static inline i##bits rcn_fistt##bits(RcnCtx *ctx, double v) {                             \
        (void)ctx;                                                                             \
        return (v > lo - 1.0 && v < hi) ? (i##bits)v : INT##bits##_MIN;                        \
    }
RCN_FIST(16, -32768.0, 32768.0)
RCN_FIST(32, -2147483648.0, 2147483648.0)
RCN_FIST(64, -9223372036854775808.0, 9223372036854775808.0)

// --- Conversions flottant → entier (hors plage ou NaN : « entier indéfini » du x86) ---
#define RCN_CVT(name, ftype, itype, lo, hi, imin, round)                              \
    static inline itype name(ftype v) {                                               \
        v = round;                                                                    \
        return (v >= lo && v < hi) ? (itype)v : imin;                                 \
    }
RCN_CVT(rcn_cvtt_f32_i32, float, i32, -2147483648.0f, 2147483648.0f, INT32_MIN, v)
RCN_CVT(rcn_cvtt_f32_i64, float, i64, -9223372036854775808.0f, 9223372036854775808.0f, INT64_MIN, v)
RCN_CVT(rcn_cvtt_f64_i32, double, i32, -2147483648.0, 2147483648.0, INT32_MIN, v)
RCN_CVT(rcn_cvtt_f64_i64, double, i64, -9223372036854775808.0, 9223372036854775808.0, INT64_MIN, v)
RCN_CVT(rcn_cvt_f32_i32, float, i32, -2147483648.0f, 2147483648.0f, INT32_MIN, rintf(v))
RCN_CVT(rcn_cvt_f32_i64, float, i64, -9223372036854775808.0f, 9223372036854775808.0f, INT64_MIN, rintf(v))
RCN_CVT(rcn_cvt_f64_i32, double, i32, -2147483648.0, 2147483648.0, INT32_MIN, rint(v))
RCN_CVT(rcn_cvt_f64_i64, double, i64, -9223372036854775808.0, 9223372036854775808.0, INT64_MIN, rint(v))

// Prédicats de cmpss / cmpsd / cmpps / cmppd.
static inline int rcn_cmp_f64(double a, double b, int pred) {
    switch (pred & 7) {
    case 0: return a == b;
    case 1: return a < b;
    case 2: return a <= b;
    case 3: return (a != a) || (b != b);
    case 4: return !(a == b);
    case 5: return !(a < b);
    case 6: return !(a <= b);
    default: return (a == a) && (b == b);
    }
}

// --- SSE / SSE2 sur 128 bits ---
static inline RcnXmm rcn_xmm_q(u64 low) {
    RcnXmm r;
    r.q[0] = low;
    r.q[1] = 0;
    return r;
}
// r.champ[i] = expr pour chaque voie.
static inline i8 rcn_sat8(int v) { return (i8)(v > 127 ? 127 : v < -128 ? -128 : v); }
static inline i16 rcn_sat16(int v) { return (i16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
#define RCN_LANES(name, field, n, expr)                    \
    static inline RcnXmm rcn_##name(RcnXmm a, RcnXmm b) {  \
        RcnXmm r;                                          \
        for (int i = 0; i < n; i++) r.field[i] = (expr);   \
        return r;                                          \
    }
RCN_LANES(addps, f, 4, a.f[i] + b.f[i])
RCN_LANES(subps, f, 4, a.f[i] - b.f[i])
RCN_LANES(mulps, f, 4, a.f[i] * b.f[i])
RCN_LANES(divps, f, 4, a.f[i] / b.f[i])
RCN_LANES(minps, f, 4, a.f[i] < b.f[i] ? a.f[i] : b.f[i])
RCN_LANES(maxps, f, 4, a.f[i] > b.f[i] ? a.f[i] : b.f[i])
RCN_LANES(addpd, db, 2, a.db[i] + b.db[i])
RCN_LANES(subpd, db, 2, a.db[i] - b.db[i])
RCN_LANES(mulpd, db, 2, a.db[i] * b.db[i])
RCN_LANES(divpd, db, 2, a.db[i] / b.db[i])
RCN_LANES(minpd, db, 2, a.db[i] < b.db[i] ? a.db[i] : b.db[i])
RCN_LANES(maxpd, db, 2, a.db[i] > b.db[i] ? a.db[i] : b.db[i])
RCN_LANES(andps, q, 2, a.q[i] & b.q[i])
RCN_LANES(andpd, q, 2, a.q[i] & b.q[i])
RCN_LANES(pand, q, 2, a.q[i] & b.q[i])
RCN_LANES(orps, q, 2, a.q[i] | b.q[i])
RCN_LANES(orpd, q, 2, a.q[i] | b.q[i])
RCN_LANES(por, q, 2, a.q[i] | b.q[i])
RCN_LANES(xorps, q, 2, a.q[i] ^ b.q[i])
RCN_LANES(xorpd, q, 2, a.q[i] ^ b.q[i])
RCN_LANES(pxor, q, 2, a.q[i] ^ b.q[i])
RCN_LANES(andnps, q, 2, ~a.q[i] & b.q[i])
RCN_LANES(andnpd, q, 2, ~a.q[i] & b.q[i])
RCN_LANES(pandn, q, 2, ~a.q[i] & b.q[i])
RCN_LANES(paddb, b, 16, a.b[i] + b.b[i])
RCN_LANES(paddw, w, 8, a.w[i] + b.w[i])
RCN_LANES(paddd, d, 4, a.d[i] + b.d[i])
RCN_LANES(paddq, q, 2, a.q[i] + b.q[i])
RCN_LANES(psubb, b, 16, a.b[i] - b.b[i])
RCN_LANES(psubw, w, 8, a.w[i] - b.w[i])
RCN_LANES(psubd, d, 4, a.d[i] - b.d[i])
RCN_LANES(psubq, q, 2, a.q[i] - b.q[i])
RCN_LANES(pcmpeqb, b, 16, a.b[i] == b.b[i] ? 0xFF : 0)
RCN_LANES(pcmpeqw, w, 8, a.w[i] == b.w[i] ? 0xFFFF : 0)
RCN_LANES(pcmpeqd, d, 4, a.d[i] == b.d[i] ? 0xFFFFFFFFu : 0)
RCN_LANES(pcmpgtb, sb, 16, a.sb[i] > b.sb[i] ? -1 : 0)
RCN_LANES(pcmpgtw, sw, 8, a.sw[i] > b.sw[i] ? -1 : 0)
RCN_LANES(pcmpgtd, sd, 4, a.sd[i] > b.sd[i] ? -1 : 0)
RCN_LANES(pmullw, w, 8, (u32)a.w[i] * b.w[i])
RCN_LANES(pmulld, d, 4, a.d[i] * b.d[i])
RCN_LANES(pmuludq, q, 2, (u64)a.d[i * 2] * b.d[i * 2])
RCN_LANES(pminub, b, 16, a.b[i] < b.b[i] ? a.b[i] : b.b[i])
RCN_LANES(pmaxub, b, 16, a.b[i] > b.b[i] ? a.b[i] : b.b[i])
RCN_LANES(pminsw, sw, 8, a.sw[i] < b.sw[i] ? a.sw[i] : b.sw[i])
RCN_LANES(pmaxsw, sw, 8, a.sw[i] > b.sw[i] ? a.sw[i] : b.sw[i])
RCN_LANES(paddsb, sb, 16, rcn_sat8(a.sb[i] + b.sb[i]))
RCN_LANES(psubsb, sb, 16, rcn_sat8(a.sb[i] - b.sb[i]))
RCN_LANES(paddsw, sw, 8, rcn_sat16(a.sw[i] + b.sw[i]))
RCN_LANES(psubsw, sw, 8, rcn_sat16(a.sw[i] - b.sw[i]))
RCN_LANES(paddusw, w, 8, a.w[i] + b.w[i] > 65535 ? 65535 : a.w[i] + b.w[i])
RCN_LANES(psubusw, w, 8, a.w[i] > b.w[i] ? a.w[i] - b.w[i] : 0)
RCN_LANES(pmulhw, sw, 8, (i16)((a.sw[i] * b.sw[i]) >> 16))
RCN_LANES(pmulhuw, w, 8, (u16)(((u32)a.w[i] * b.w[i]) >> 16))
RCN_LANES(pavgw, w, 8, (a.w[i] + b.w[i] + 1) >> 1)
// psadbw : somme des écarts absolus des octets, par moitié de registre.
static inline RcnXmm rcn_psadbw(RcnXmm a, RcnXmm b) {
    RcnXmm r = rcn_xmm_q(0);
    for (int h = 0; h < 2; h++) {
        u32 s = 0;
        for (int i = 0; i < 8; i++) s += a.b[8 * h + i] > b.b[8 * h + i] ? a.b[8 * h + i] - b.b[8 * h + i] : b.b[8 * h + i] - a.b[8 * h + i];
        r.q[h] = s;
    }
    return r;
}
RCN_LANES(paddusb, b, 16, a.b[i] + b.b[i] > 255 ? 255 : a.b[i] + b.b[i])
RCN_LANES(psubusb, b, 16, a.b[i] > b.b[i] ? a.b[i] - b.b[i] : 0)
RCN_LANES(pavgb, b, 16, (a.b[i] + b.b[i] + 1) >> 1)
RCN_LANES(pmaddwd, sd, 4, a.sw[i * 2] * b.sw[i * 2] + a.sw[i * 2 + 1] * b.sw[i * 2 + 1])
// Entrelacement des voies basses (off = 0) ou hautes de a et de b.
#define RCN_UNPACK(name, field, half, off)                 \
    static inline RcnXmm rcn_##name(RcnXmm a, RcnXmm b) {  \
        RcnXmm r;                                          \
        for (int i = 0; i < half; i++) {                   \
            r.field[i * 2] = a.field[off + i];             \
            r.field[i * 2 + 1] = b.field[off + i];         \
        }                                                  \
        return r;                                          \
    }
RCN_UNPACK(punpcklbw, b, 8, 0)
RCN_UNPACK(punpckhbw, b, 8, 8)
RCN_UNPACK(punpcklwd, w, 4, 0)
RCN_UNPACK(punpckhwd, w, 4, 4)
RCN_UNPACK(punpckldq, d, 2, 0)
RCN_UNPACK(punpckhdq, d, 2, 2)
RCN_UNPACK(punpcklqdq, q, 1, 0)
RCN_UNPACK(punpckhqdq, q, 1, 1)
RCN_UNPACK(unpcklps, d, 2, 0)
RCN_UNPACK(unpckhps, d, 2, 2)
RCN_UNPACK(unpcklpd, q, 1, 0)
RCN_UNPACK(unpckhpd, q, 1, 1)

static inline i32 rcn_sat(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline RcnXmm rcn_packuswb(RcnXmm a, RcnXmm b) {
    RcnXmm r;
    for (int i = 0; i < 8; i++) {
        r.b[i] = (u8)rcn_sat(a.sw[i], 0, 255);
        r.b[8 + i] = (u8)rcn_sat(b.sw[i], 0, 255);
    }
    return r;
}
static inline RcnXmm rcn_packsswb(RcnXmm a, RcnXmm b) {
    RcnXmm r;
    for (int i = 0; i < 8; i++) {
        r.sb[i] = (i8)rcn_sat(a.sw[i], -128, 127);
        r.sb[8 + i] = (i8)rcn_sat(b.sw[i], -128, 127);
    }
    return r;
}
static inline RcnXmm rcn_packssdw(RcnXmm a, RcnXmm b) {
    RcnXmm r;
    for (int i = 0; i < 4; i++) {
        r.sw[i] = (i16)rcn_sat(a.sd[i], -32768, 32767);
        r.sw[4 + i] = (i16)rcn_sat(b.sd[i], -32768, 32767);
    }
    return r;
}
static inline RcnXmm rcn_movlhps(RcnXmm a, RcnXmm b) { a.q[1] = b.q[0]; return a; }
static inline RcnXmm rcn_movhlps(RcnXmm a, RcnXmm b) { a.q[0] = b.q[1]; return a; }
static inline RcnXmm rcn_haddps(RcnXmm a, RcnXmm b) {
    RcnXmm r;
    r.f[0] = a.f[0] + a.f[1];
    r.f[1] = a.f[2] + a.f[3];
    r.f[2] = b.f[0] + b.f[1];
    r.f[3] = b.f[2] + b.f[3];
    return r;
}
// SSE3 : soustraction aux rangs pairs, addition aux rangs impairs (multiplications complexes, FFT).
static inline RcnXmm rcn_addsubps(RcnXmm a, RcnXmm b) {
    a.f[0] -= b.f[0], a.f[1] += b.f[1], a.f[2] -= b.f[2], a.f[3] += b.f[3];
    return a;
}
static inline RcnXmm rcn_addsubpd(RcnXmm a, RcnXmm b) {
    a.db[0] -= b.db[0], a.db[1] += b.db[1];
    return a;
}
static inline RcnXmm rcn_hsubps(RcnXmm a, RcnXmm b) {
    RcnXmm r;
    r.f[0] = a.f[0] - a.f[1];
    r.f[1] = a.f[2] - a.f[3];
    r.f[2] = b.f[0] - b.f[1];
    r.f[3] = b.f[2] - b.f[3];
    return r;
}
static inline RcnXmm rcn_haddpd(RcnXmm a, RcnXmm b) {
    RcnXmm r;
    r.db[0] = a.db[0] + a.db[1];
    r.db[1] = b.db[0] + b.db[1];
    return r;
}
static inline RcnXmm rcn_hsubpd(RcnXmm a, RcnXmm b) {
    RcnXmm r;
    r.db[0] = a.db[0] - a.db[1];
    r.db[1] = b.db[0] - b.db[1];
    return r;
}
// SSSE3 : octets non signés de a × octets signés de b, paires additionnées avec saturation sur 16 bits.
static inline RcnXmm rcn_pmaddubsw(RcnXmm a, RcnXmm b) {
    RcnXmm r;
    for (int i = 0; i < 8; i++) {
        i32 v = (i32)a.b[2 * i] * b.sb[2 * i] + (i32)a.b[2 * i + 1] * b.sb[2 * i + 1];
        r.sw[i] = (i16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    return r;
}
// SSSE3 : additions et soustractions horizontales (paires voisines de a, puis de b).
#define RCN_HORIZONTAL(name, field, n, expr)                                \
    static inline RcnXmm rcn_##name(RcnXmm a, RcnXmm b) {                   \
        RcnXmm r;                                                           \
        for (int i = 0; i < (n) / 2; i++) {                                 \
            { __typeof__(a.field[0]) x = a.field[2 * i], y = a.field[2 * i + 1]; r.field[i] = (expr); }             \
            { __typeof__(a.field[0]) x = b.field[2 * i], y = b.field[2 * i + 1]; r.field[(n) / 2 + i] = (expr); }   \
        }                                                                   \
        return r;                                                           \
    }
RCN_HORIZONTAL(phaddw, sw, 8, (i16)(x + y))
RCN_HORIZONTAL(phaddd, sd, 4, (i32)((u32)x + (u32)y))
RCN_HORIZONTAL(phaddsw, sw, 8, rcn_sat16((i32)x + y))
RCN_HORIZONTAL(phsubw, sw, 8, (i16)(x - y))
RCN_HORIZONTAL(phsubd, sd, 4, (i32)((u32)x - (u32)y))
RCN_HORIZONTAL(phsubsw, sw, 8, rcn_sat16((i32)x - y))
// SSSE3 : produit de mots signés arrondi, 16 bits hauts de (a*b + 0x4000) >> 15.
static inline RcnXmm rcn_pmulhrsw(RcnXmm a, RcnXmm b) {
    for (int i = 0; i < 8; i++) a.sw[i] = (i16)((((i32)a.sw[i] * b.sw[i]) + 0x4000) >> 15);
    return a;
}
// SSSE3 : signe de b appliqué à a (négatif : opposé, nul : zéro).
#define RCN_PSIGN(name, field, n)                                                        \
    static inline RcnXmm rcn_##name(RcnXmm a, RcnXmm b) {                                \
        for (int i = 0; i < (n); i++)                                                    \
            a.field[i] = b.field[i] < 0 ? (__typeof__(a.field[0]))(0 - a.field[i]) : b.field[i] == 0 ? 0 : a.field[i]; \
        return a;                                                                        \
    }
RCN_PSIGN(psignb, sb, 16)
RCN_PSIGN(psignw, sw, 8)
RCN_PSIGN(psignd, sd, 4)
// SSSE3 : valeurs absolues (opérande source seul).
static inline RcnXmm rcn_pabsb(RcnXmm a) {
    for (int i = 0; i < 16; i++) a.b[i] = (u8)(a.sb[i] < 0 ? -a.sb[i] : a.sb[i]);
    return a;
}
static inline RcnXmm rcn_pabsw(RcnXmm a) {
    for (int i = 0; i < 8; i++) a.w[i] = (u16)(a.sw[i] < 0 ? -a.sw[i] : a.sw[i]);
    return a;
}
static inline RcnXmm rcn_pabsd(RcnXmm a) {
    for (int i = 0; i < 4; i++) a.d[i] = a.sd[i] < 0 ? 0u - (u32)a.sd[i] : (u32)a.sd[i];
    return a;
}
// SSSE3 : palignr(a, b, n) : les 32 octets a:b décalés de n octets vers la droite, 16 octets bas gardés.
static inline RcnXmm rcn_palignr(RcnXmm a, RcnXmm b, int n) {
    u8 all[32];
    memcpy(all, b.b, 16);
    memcpy(all + 16, a.b, 16);
    RcnXmm r;
    for (int i = 0; i < 16; i++) r.b[i] = (u32)(n + i) < 32 ? all[n + i] : 0;
    return r;
}
// SSSE3 : octets de a choisis par ceux de b (bit 7 : zéro).
static inline RcnXmm rcn_pshufb(RcnXmm a, RcnXmm b) {
    RcnXmm r;
    for (int i = 0; i < 16; i++) r.b[i] = (b.b[i] & 0x80) ? 0 : a.b[b.b[i] & 15];
    return r;
}

// --- SSE4.1 / SSE4.2 ---

#define RCN_LANEWISE(name, field, n, expr)                                               \
    static inline RcnXmm rcn_##name(RcnXmm a, RcnXmm b) {                                \
        for (int i = 0; i < (n); i++) {                                                  \
            __typeof__(a.field[0]) x = a.field[i], y = b.field[i];                       \
            a.field[i] = (expr);                                                         \
        }                                                                                \
        return a;                                                                        \
    }
RCN_LANEWISE(pminsb, sb, 16, x < y ? x : y)
RCN_LANEWISE(pmaxsb, sb, 16, x > y ? x : y)
RCN_LANEWISE(pminuw, w, 8, x < y ? x : y)
RCN_LANEWISE(pmaxuw, w, 8, x > y ? x : y)
RCN_LANEWISE(pminsd, sd, 4, x < y ? x : y)
RCN_LANEWISE(pmaxsd, sd, 4, x > y ? x : y)
RCN_LANEWISE(pminud, d, 4, x < y ? x : y)
RCN_LANEWISE(pmaxud, d, 4, x > y ? x : y)
RCN_LANEWISE(pcmpeqq, q, 2, x == y ? ~0ULL : 0)
RCN_LANEWISE(pcmpgtq, q, 2, (i64)x > (i64)y ? ~0ULL : 0)
// Produit signé des doubles mots pairs (0 et 2), sur 64 bits.
static inline RcnXmm rcn_pmuldq(RcnXmm a, RcnXmm b) {
    RcnXmm r;
    r.q[0] = (u64)((i64)a.sd[0] * (i64)b.sd[0]);
    r.q[1] = (u64)((i64)a.sd[2] * (i64)b.sd[2]);
    return r;
}
static inline RcnXmm rcn_packusdw(RcnXmm a, RcnXmm b) {
    RcnXmm r;
    for (int i = 0; i < 4; i++) {
        r.w[i] = (u16)(a.sd[i] < 0 ? 0 : a.sd[i] > 65535 ? 65535 : a.sd[i]);
        r.w[4 + i] = (u16)(b.sd[i] < 0 ? 0 : b.sd[i] > 65535 ? 65535 : b.sd[i]);
    }
    return r;
}
// Mélanges : élément de b là où le bit de l'immédiat (ou le bit de signe de m) est à 1.
static inline RcnXmm rcn_blendps(RcnXmm a, RcnXmm b, int imm) {
    for (int i = 0; i < 4; i++) if (imm >> i & 1) a.d[i] = b.d[i];
    return a;
}
static inline RcnXmm rcn_blendpd(RcnXmm a, RcnXmm b, int imm) {
    for (int i = 0; i < 2; i++) if (imm >> i & 1) a.q[i] = b.q[i];
    return a;
}
static inline RcnXmm rcn_pblendw(RcnXmm a, RcnXmm b, int imm) {
    for (int i = 0; i < 8; i++) if (imm >> i & 1) a.w[i] = b.w[i];
    return a;
}
static inline RcnXmm rcn_blendvps(RcnXmm a, RcnXmm b, RcnXmm m) {
    for (int i = 0; i < 4; i++) if (m.sd[i] < 0) a.d[i] = b.d[i];
    return a;
}
static inline RcnXmm rcn_blendvpd(RcnXmm a, RcnXmm b, RcnXmm m) {
    for (int i = 0; i < 2; i++) if ((i64)m.q[i] < 0) a.q[i] = b.q[i];
    return a;
}
static inline RcnXmm rcn_pblendvb(RcnXmm a, RcnXmm b, RcnXmm m) {
    for (int i = 0; i < 16; i++) if (m.sb[i] < 0) a.b[i] = b.b[i];
    return a;
}
// Arrondis : bits 0-1 de l'immédiat (au plus près, vers -inf, vers +inf, vers zéro) ; bit 2 : mode de
// MXCSR, au plus près ici.
static inline double rcn_round_f64(double v, int imm) {
    switch (imm & 4 ? 0 : imm & 3) {
    case 0: return __builtin_rint(v);
    case 1: return __builtin_floor(v);
    case 2: return __builtin_ceil(v);
    default: return __builtin_trunc(v);
    }
}
static inline float rcn_round_f32(float v, int imm) {
    switch (imm & 4 ? 0 : imm & 3) {
    case 0: return __builtin_rintf(v);
    case 1: return __builtin_floorf(v);
    case 2: return __builtin_ceilf(v);
    default: return __builtin_truncf(v);
    }
}
static inline RcnXmm rcn_roundps(RcnXmm a, int imm) {
    for (int i = 0; i < 4; i++) a.f[i] = rcn_round_f32(a.f[i], imm);
    return a;
}
static inline RcnXmm rcn_roundpd(RcnXmm a, int imm) {
    for (int i = 0; i < 2; i++) a.db[i] = rcn_round_f64(a.db[i], imm);
    return a;
}
// Produits scalaires : bits 4-7 (4-5) de l'immédiat, éléments multipliés ; bits 0-3 (0-1), éléments
// qui reçoivent la somme (les autres à zéro).
static inline RcnXmm rcn_dpps(RcnXmm a, RcnXmm b, int imm) {
    float p[4];
    for (int i = 0; i < 4; i++) p[i] = (imm >> (4 + i) & 1) ? a.f[i] * b.f[i] : 0.0f;
    float sum = (p[0] + p[1]) + (p[2] + p[3]);
    for (int i = 0; i < 4; i++) a.f[i] = (imm >> i & 1) ? sum : 0.0f;
    return a;
}
static inline RcnXmm rcn_dppd(RcnXmm a, RcnXmm b, int imm) {
    double p0 = (imm >> 4 & 1) ? a.db[0] * b.db[0] : 0.0, p1 = (imm >> 5 & 1) ? a.db[1] * b.db[1] : 0.0;
    double sum = p0 + p1;
    for (int i = 0; i < 2; i++) a.db[i] = (imm >> i & 1) ? sum : 0.0;
    return a;
}
// insertps : élément (bits 6-7) de b, ou le flottant v lu en mémoire, mis à l'élément (bits 4-5) de a ;
// bits 0-3 : éléments mis à zéro.
static inline RcnXmm rcn_insertps(RcnXmm a, u32 v, int imm) {
    a.d[(imm >> 4) & 3] = v;
    for (int i = 0; i < 4; i++) if (imm >> i & 1) a.d[i] = 0;
    return a;
}
// Extensions : éléments bas de a élargis (signe ou zéros).
#define RCN_WIDEN(name, from, to, n)                                                     \
    static inline RcnXmm rcn_##name(RcnXmm a) {                                          \
        RcnXmm r;                                                                        \
        for (int i = 0; i < (n); i++) r.to[i] = a.from[i];                               \
        return r;                                                                        \
    }
typedef union {
    i64 sq[2];
    u64 q[2];
} RcnXmmQ;
RCN_WIDEN(pmovsxbw, sb, sw, 8)
RCN_WIDEN(pmovsxwd, sw, sd, 4)
RCN_WIDEN(pmovsxbd, sb, sd, 4)
RCN_WIDEN(pmovzxbw, b, w, 8)
RCN_WIDEN(pmovzxwd, w, d, 4)
RCN_WIDEN(pmovzxbd, b, d, 4)
RCN_WIDEN(pmovzxbq, b, q, 2)
RCN_WIDEN(pmovzxwq, w, q, 2)
RCN_WIDEN(pmovzxdq, d, q, 2)
static inline RcnXmm rcn_pmovsxbq(RcnXmm a) { RcnXmm r; for (int i = 0; i < 2; i++) r.q[i] = (u64)(i64)a.sb[i]; return r; }
static inline RcnXmm rcn_pmovsxwq(RcnXmm a) { RcnXmm r; for (int i = 0; i < 2; i++) r.q[i] = (u64)(i64)a.sw[i]; return r; }
static inline RcnXmm rcn_pmovsxdq(RcnXmm a) { RcnXmm r; for (int i = 0; i < 2; i++) r.q[i] = (u64)(i64)a.sd[i]; return r; }
// ptest : bit 0, (a & b) nul (ZF) ; bit 1, (~a & b) nul (CF).
static inline u32 rcn_ptest(RcnXmm a, RcnXmm b) {
    u32 zf = ((a.q[0] & b.q[0]) | (a.q[1] & b.q[1])) == 0;
    u32 cf = ((~a.q[0] & b.q[0]) | (~a.q[1] & b.q[1])) == 0;
    return zf | cf << 1;
}
// SSE4.2 : comparaisons de chaînes pcmp[ei]str[im]. info : immédiat (bits 0-7), longueurs explicites
// (bit 8 : rax et rdx), masque dans xmm0 (bit 9) plutôt qu'un rang dans ecx, longueurs sur 64 bits (bit 10).
// Drapeaux : CF résultat non nul, ZF fin de b, SF fin de a, OF bit 0 du résultat ; AF et PF à zéro.
static inline void rcn_pcmpstr(struct RcnCtx *ctx, RcnXmm a, RcnXmm b, u32 info);
static inline int rcn_pcmpstr_elem(const RcnXmm *v, int i, int fmt) {
    switch (fmt) {
    case 0: return v->b[i];
    case 1: return v->w[i];
    case 2: return v->sb[i];
    default: return v->sw[i];
    }
}
static inline u32 rcn_pcmpstr_result(RcnXmm a, RcnXmm b, int imm, int la, int lb, int n) {
    int fmt = imm & 3, agg = (imm >> 2) & 3;
    u32 res = 0;
    for (int j = 0; j < n; j++) {
        int bj = rcn_pcmpstr_elem(&b, j, fmt), b_ok = j < lb, bit = 0;
        switch (agg) {
        case 0: // l'un des éléments de a
            for (int i = 0; i < la && !bit; i++) bit = b_ok && rcn_pcmpstr_elem(&a, i, fmt) == bj;
            break;
        case 1: // dans l'un des intervalles [a(2k), a(2k+1)]
            for (int i = 0; i + 1 < n && !bit; i += 2) {
                if (i + 1 >= la || !b_ok) break;
                bit = rcn_pcmpstr_elem(&a, i, fmt) <= bj && bj <= rcn_pcmpstr_elem(&a, i + 1, fmt);
            }
            break;
        case 2: { // égalité élément par élément
            int a_ok = j < la;
            bit = (!a_ok && !b_ok) ? 1 : (a_ok != b_ok) ? 0 : rcn_pcmpstr_elem(&a, j, fmt) == bj;
            break;
        }
        default: // a présent tel quel à partir de j
            bit = 1;
            for (int i = 0; i < n - j && bit; i++) {
                int k = i + j;
                if (i >= la) break;              // fin de a : accepté
                if (k >= lb) { bit = 0; break; } // fin de b avant celle de a
                bit = rcn_pcmpstr_elem(&a, i, fmt) == rcn_pcmpstr_elem(&b, k, fmt);
            }
            break;
        }
        res |= (u32)bit << j;
    }
    u32 all = n == 16 ? 0xFFFFu : 0xFFu;
    switch ((imm >> 4) & 3) {
    case 1: res = ~res & all; break;
    case 3: res ^= lb >= n ? all : ((1u << lb) - 1); break; // négation des seuls éléments valides de b
    default: break;
    }
    return res;
}
static inline int rcn_pcmpstr_len(RcnXmm v, int n, int words) {
    for (int i = 0; i < n; i++)
        if (words ? v.w[i] == 0 : v.b[i] == 0) return i;
    return n;
}
static inline void rcn_pcmpstr(struct RcnCtx *ctx, RcnXmm a, RcnXmm b, u32 info) {
    int imm = info & 0xFF, words = imm & 1, n = words ? 8 : 16, la, lb;
    if (info & 0x100) {
        i64 ra = info & 0x400 ? (i64)ctx->r[0] : (i64)(i32)ctx->r[0];
        i64 rb = info & 0x400 ? (i64)ctx->r[2] : (i64)(i32)ctx->r[2];
        if (ra < 0) ra = -ra;
        if (rb < 0) rb = -rb;
        la = ra > n || ra < 0 ? n : (int)ra;
        lb = rb > n || rb < 0 ? n : (int)rb;
    } else {
        la = rcn_pcmpstr_len(a, n, words);
        lb = rcn_pcmpstr_len(b, n, words);
    }
    u32 res = rcn_pcmpstr_result(a, b, imm, la, lb, n);
    if (info & 0x200) {
        RcnXmm m = {{0, 0}};
        if (imm & 0x40) {
            for (int i = 0; i < n; i++) {
                if (!(res >> i & 1)) continue;
                if (words) m.w[i] = 0xFFFF;
                else m.b[i] = 0xFF;
            }
        } else {
            m.w[0] = (u16)res;
        }
        ctx->xmm[0] = m;
    } else {
        int index = n;
        if (res) index = imm & 0x40 ? 31 - __builtin_clz(res) : __builtin_ctz(res);
        ctx->r[1] = (u64)(u32)index;
    }
    ctx->cf = res != 0;
    ctx->zf = lb < n;
    ctx->sf = la < n;
    ctx->of = res & 1;
    ctx->af = 0;
    ctx->pf = 0;
}

// phminposuw : plus petit mot non signé (et son rang le plus bas) dans les deux mots bas.
static inline RcnXmm rcn_phminposuw(RcnXmm a) {
    int k = 0;
    for (int i = 1; i < 8; i++) if (a.w[i] < a.w[k]) k = i;
    RcnXmm r = {{0, 0}};
    r.w[0] = a.w[k];
    r.w[1] = (u16)k;
    return r;
}

// r.champ[i] = expr, le reste du registre à zéro.
#define RCN_CONVERT(name, field, n, expr)                  \
    static inline RcnXmm rcn_##name(RcnXmm a) {            \
        RcnXmm r = rcn_xmm_q(0);                           \
        for (int i = 0; i < n; i++) r.field[i] = (expr);   \
        return r;                                          \
    }
RCN_CONVERT(sqrtps, f, 4, sqrtf(a.f[i]))
RCN_CONVERT(sqrtpd, db, 2, sqrt(a.db[i]))
RCN_CONVERT(cvtdq2ps, f, 4, (float)a.sd[i])
RCN_CONVERT(cvtps2dq, sd, 4, rcn_cvt_f32_i32(a.f[i]))
RCN_CONVERT(cvttps2dq, sd, 4, rcn_cvtt_f32_i32(a.f[i]))
RCN_CONVERT(cvtpd2ps, f, 2, (float)a.db[i])
RCN_CONVERT(cvttpd2dq, sd, 2, rcn_cvtt_f64_i32(a.db[i]))
RCN_CONVERT(cvtpd2dq, sd, 2, rcn_cvt_f64_i32(a.db[i]))
RCN_CONVERT(movsldup, f, 4, a.f[i & ~1])
RCN_CONVERT(movshdup, f, 4, a.f[i | 1])
RCN_CONVERT(rcpps, f, 4, 1.0f / a.f[i])
RCN_CONVERT(rsqrtps, f, 4, 1.0f / sqrtf(a.f[i]))
RCN_CONVERT(cvtdq2pd, db, 2, (double)a.sd[i])
RCN_CONVERT(cvtps2pd, db, 2, (double)a.f[i])
RCN_CONVERT(movddup, q, 2, a.q[0])

// Décalages : un décalage au-delà de la largeur de la voie donne zéro (ou le signe pour psra*).
#define RCN_SHIFT(name, field, n, bits, expr, over)        \
    static inline RcnXmm rcn_##name(RcnXmm a, u64 c) {     \
        RcnXmm r;                                          \
        for (int i = 0; i < n; i++) r.field[i] = c < bits ? (expr) : (over); \
        return r;                                          \
    }
RCN_SHIFT(psllw, w, 8, 16, a.w[i] << c, 0)
RCN_SHIFT(pslld, d, 4, 32, a.d[i] << c, 0)
RCN_SHIFT(psllq, q, 2, 64, a.q[i] << c, 0)
RCN_SHIFT(psrlw, w, 8, 16, a.w[i] >> c, 0)
RCN_SHIFT(psrld, d, 4, 32, a.d[i] >> c, 0)
RCN_SHIFT(psrlq, q, 2, 64, a.q[i] >> c, 0)
RCN_SHIFT(psraw, sw, 8, 16, a.sw[i] >> c, a.sw[i] >> 15)
RCN_SHIFT(psrad, sd, 4, 32, a.sd[i] >> c, a.sd[i] >> 31)
RCN_SHIFT(pslldq, b, 16, 16, (u64)i >= c ? a.b[i - c] : 0, 0)
RCN_SHIFT(psrldq, b, 16, 16, i + c < 16 ? a.b[i + c] : 0, 0)

static inline u32 rcn_movmskps(RcnXmm a) {
    return (a.d[0] >> 31) | (a.d[1] >> 31) << 1 | (a.d[2] >> 31) << 2 | (a.d[3] >> 31) << 3;
}
static inline u32 rcn_movmskpd(RcnXmm a) { return (u32)(a.q[0] >> 63) | (u32)(a.q[1] >> 63) << 1; }
static inline u32 rcn_pmovmskb(RcnXmm a) {
    u32 m = 0;
    for (int i = 0; i < 16; i++) m |= (u32)(a.b[i] >> 7) << i;
    return m;
}
static inline RcnXmm rcn_pshufd(RcnXmm a, int imm) {
    RcnXmm r;
    for (int i = 0; i < 4; i++) r.d[i] = a.d[(imm >> (i * 2)) & 3];
    return r;
}
static inline RcnXmm rcn_pshuflw(RcnXmm a, int imm) {
    RcnXmm r = a;
    for (int i = 0; i < 4; i++) r.w[i] = a.w[(imm >> (i * 2)) & 3];
    return r;
}
static inline RcnXmm rcn_pshufhw(RcnXmm a, int imm) {
    RcnXmm r = a;
    for (int i = 0; i < 4; i++) r.w[4 + i] = a.w[4 + ((imm >> (i * 2)) & 3)];
    return r;
}
static inline RcnXmm rcn_shufps(RcnXmm a, RcnXmm b, int imm) {
    RcnXmm r;
    r.d[0] = a.d[imm & 3];
    r.d[1] = a.d[(imm >> 2) & 3];
    r.d[2] = b.d[(imm >> 4) & 3];
    r.d[3] = b.d[(imm >> 6) & 3];
    return r;
}
static inline RcnXmm rcn_shufpd(RcnXmm a, RcnXmm b, int imm) {
    RcnXmm r;
    r.q[0] = a.q[imm & 1];
    r.q[1] = b.q[(imm >> 1) & 1];
    return r;
}
static inline RcnXmm rcn_cmpps(RcnXmm a, RcnXmm b, int imm) {
    RcnXmm r;
    for (int i = 0; i < 4; i++) r.d[i] = rcn_cmp_f64(a.f[i], b.f[i], imm) ? 0xFFFFFFFFu : 0;
    return r;
}
static inline RcnXmm rcn_cmppd(RcnXmm a, RcnXmm b, int imm) {
    RcnXmm r;
    for (int i = 0; i < 2; i++) r.q[i] = rcn_cmp_f64(a.db[i], b.db[i], imm) ? ~0ULL : 0;
    return r;
}

#endif
