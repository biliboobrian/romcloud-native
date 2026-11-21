// RomCloud Native : opérations appelées par le code machine traduit (recompiler/src/native.rs), pour ce
// qui serait long à écrire instruction par instruction : SSE sur 128 bits, conversions et fonctions
// mathématiques du x87, divisions. Elles reprennent les fonctions en ligne de rcn.h, utilisées par le
// générateur C, pour que les deux traductions se comportent de la même façon.
#include <math.h>
#include <stdlib.h>

#include "rcn_internal.h"

#define LOAD128(name, src)   \
    RcnXmm name;             \
    memcpy(&name, src, 16)

// --- SSE : dst = op(dst, src), dst = op(src), décalages, masques, mélanges ---

#define OP_BIN(n)                                           \
    void rcn_op_##n(RcnXmm *d, const void *s) {             \
        LOAD128(b, s);                                      \
        *d = rcn_##n(*d, b);                                \
    }
#define OP_UN(n)                                            \
    void rcn_op_##n(RcnXmm *d, const void *s) {             \
        LOAD128(a, s);                                      \
        *d = rcn_##n(a);                                    \
    }
// La source n'occupe que 64 bits.
#define OP_LOW(n)                                           \
    void rcn_op_##n(RcnXmm *d, const void *s) {             \
        u64 q;                                              \
        memcpy(&q, s, 8);                                   \
        *d = rcn_##n(rcn_xmm_q(q));                         \
    }
#define OP_SHIFT(n) \
    void rcn_op_##n(RcnXmm *d, u64 count) { *d = rcn_##n(*d, count); }
#define OP_MASK(n) \
    u32 rcn_op_##n(const RcnXmm *s) { return rcn_##n(*s); }
#define OP_IMM1(n)                                          \
    void rcn_op_##n(RcnXmm *d, const void *s, int imm) {    \
        LOAD128(a, s);                                      \
        *d = rcn_##n(a, imm);                               \
    }
#define OP_IMM2(n)                                          \
    void rcn_op_##n(RcnXmm *d, const void *s, int imm) {    \
        LOAD128(b, s);                                      \
        *d = rcn_##n(*d, b, imm);                           \
    }

OP_BIN(addps) OP_BIN(subps) OP_BIN(mulps) OP_BIN(divps) OP_BIN(minps) OP_BIN(maxps)
OP_BIN(addpd) OP_BIN(subpd) OP_BIN(mulpd) OP_BIN(divpd) OP_BIN(minpd) OP_BIN(maxpd)
OP_BIN(andps) OP_BIN(andpd) OP_BIN(pand) OP_BIN(orps) OP_BIN(orpd) OP_BIN(por) OP_BIN(xorps) OP_BIN(xorpd) OP_BIN(pxor)
OP_BIN(andnps) OP_BIN(andnpd) OP_BIN(pandn)
OP_BIN(paddb) OP_BIN(paddw) OP_BIN(paddd) OP_BIN(paddq) OP_BIN(psubb) OP_BIN(psubw) OP_BIN(psubd) OP_BIN(psubq)
OP_BIN(pcmpeqb) OP_BIN(pcmpeqw) OP_BIN(pcmpeqd) OP_BIN(pcmpgtb) OP_BIN(pcmpgtw) OP_BIN(pcmpgtd)
OP_BIN(pmullw) OP_BIN(pmulld) OP_BIN(pmuludq)
OP_BIN(punpcklbw) OP_BIN(punpcklwd) OP_BIN(punpckldq) OP_BIN(punpcklqdq)
OP_BIN(punpckhbw) OP_BIN(punpckhwd) OP_BIN(punpckhdq) OP_BIN(punpckhqdq)
OP_BIN(unpcklps) OP_BIN(unpckhps) OP_BIN(unpcklpd) OP_BIN(unpckhpd)
OP_BIN(pminub) OP_BIN(pmaxub) OP_BIN(pminsw) OP_BIN(pmaxsw)
OP_BIN(packuswb) OP_BIN(packsswb) OP_BIN(packssdw) OP_BIN(movlhps) OP_BIN(movhlps)
OP_BIN(paddusb) OP_BIN(psubusb) OP_BIN(pavgb) OP_BIN(pmaddwd) OP_BIN(haddps)
OP_BIN(addsubps) OP_BIN(addsubpd) OP_BIN(hsubps) OP_BIN(haddpd) OP_BIN(hsubpd) OP_BIN(pmaddubsw) OP_BIN(pshufb)
OP_BIN(phaddw) OP_BIN(phaddd) OP_BIN(phaddsw) OP_BIN(phsubw) OP_BIN(phsubd) OP_BIN(phsubsw) OP_BIN(pmulhrsw)
OP_BIN(psignb) OP_BIN(psignw) OP_BIN(psignd) OP_UN(pabsb) OP_UN(pabsw) OP_UN(pabsd) OP_IMM2(palignr)
OP_UN(sqrtps) OP_UN(sqrtpd) OP_UN(cvtdq2ps) OP_UN(cvtps2dq) OP_UN(cvttps2dq) OP_UN(cvtpd2ps) OP_UN(cvttpd2dq) OP_UN(cvtpd2dq) OP_UN(rsqrtps) OP_BIN(paddsb) OP_BIN(psubsb) OP_BIN(paddsw) OP_BIN(psubsw) OP_BIN(paddusw) OP_BIN(psubusw) OP_BIN(pmulhw) OP_BIN(pmulhuw) OP_BIN(pavgw) OP_BIN(psadbw) OP_UN(movsldup) OP_UN(movshdup) OP_UN(rcpps)
OP_LOW(cvtdq2pd) OP_LOW(cvtps2pd) OP_LOW(movddup)
OP_SHIFT(psllw) OP_SHIFT(pslld) OP_SHIFT(psllq) OP_SHIFT(psrlw) OP_SHIFT(psrld) OP_SHIFT(psrlq)
OP_SHIFT(psraw) OP_SHIFT(psrad) OP_SHIFT(pslldq) OP_SHIFT(psrldq)
OP_MASK(movmskps) OP_MASK(movmskpd) OP_MASK(pmovmskb)
OP_IMM1(pshufd) OP_IMM1(pshuflw) OP_IMM1(pshufhw)
OP_IMM2(shufps) OP_IMM2(shufpd) OP_IMM2(cmpps) OP_IMM2(cmppd)
// SSE4.1 : celles que le traducteur ne fait pas en ligne. blendv* : masque dans xmm0.
OP_BIN(pmuldq) OP_IMM2(dpps) OP_IMM2(dppd) OP_UN(phminposuw)
#define OP_BLENDV(n)                                                    void rcn_op_##n(RcnXmm *d, const void *s, const RcnXmm *m) {            LOAD128(b, s);                                                      *d = rcn_##n(*d, b, *m);                                        }
OP_BLENDV(blendvps) OP_BLENDV(blendvpd) OP_BLENDV(pblendvb)
// SSE4.2 : pcmp[ei]str[im] (rcn.h) ; lit rax / rdx, écrit ecx ou xmm0 et les drapeaux.
void rcn_op_pcmpstr(RcnCtx *ctx, const RcnXmm *a, const void *s, u32 info) {
    LOAD128(b, s);
    rcn_pcmpstr(ctx, *a, b, info);
}

// --- MMX : registres de 64 bits, opérations de SSE2 faites sur la moitié basse ---
// Rang de l'opération : celui de MMX_BINARY / MMX_SHIFT dans le traducteur (native.rs), même ordre.
typedef RcnXmm (*MmxOp)(RcnXmm, RcnXmm);
static const MmxOp mmx_ops[] = {
    rcn_paddb, rcn_paddw, rcn_paddd, rcn_paddq, rcn_psubb, rcn_psubw, rcn_psubd, rcn_psubq,
    rcn_paddsb, rcn_paddsw, rcn_paddusb, rcn_paddusw, rcn_psubsb, rcn_psubsw, rcn_psubusb, rcn_psubusw,
    rcn_pmullw, rcn_pmulhw, rcn_pmulhuw, rcn_pmaddwd, rcn_pmuludq,
    rcn_pand, rcn_pandn, rcn_por, rcn_pxor,
    rcn_pcmpeqb, rcn_pcmpeqw, rcn_pcmpeqd, rcn_pcmpgtb, rcn_pcmpgtw, rcn_pcmpgtd,
    rcn_punpcklbw, rcn_punpcklwd, rcn_punpckldq, rcn_punpcklbw, rcn_punpcklwd, rcn_punpckldq,
    rcn_packsswb, rcn_packssdw, rcn_packuswb,
    rcn_pminub, rcn_pmaxub, rcn_pminsw, rcn_pmaxsw, rcn_pavgb, rcn_pavgw, rcn_psadbw,
    // SSSE3 sur registres MMX
    rcn_pmaddubsw, rcn_pshufb, rcn_pmulhrsw, rcn_psignb, rcn_psignw, rcn_psignd, rcn_phaddw, rcn_phaddd, rcn_phsubw, rcn_phsubd,
};
enum { MMX_UNPACK_HIGH = 34, MMX_PACK = 37, MMX_PSHUFB = 48, MMX_HADD = 53, MMX_COUNT = sizeof mmx_ops / sizeof mmx_ops[0] };
void rcn_op_mmx(u64 *d, const void *s, u32 op) {
    RcnXmm a = {{0}}, b = {{0}}, r;
    memcpy(&a.q[0], d, 8);
    memcpy(&b.q[0], s, 8);
    if (op >= MMX_COUNT) return;
    // punpckh* de MMX : moitiés hautes (32 bits) entrelacées = punpckl* des moitiés hautes descendues.
    if (op >= MMX_UNPACK_HIGH && op < MMX_PACK) a.q[0] >>= 32, b.q[0] >>= 32;
    // pshufb de MMX : indices de 3 bits (bit 7 : octet nul).
    if (op == MMX_PSHUFB) b.q[0] &= 0x8787878787878787ull;
    r = mmx_ops[op](a, b);
    u64 out = r.q[0];
    // pack*, phadd*, phsub* : la moitié tirée de la seconde source est en tête de la moitié haute.
    if ((op >= MMX_PACK && op < MMX_PACK + 3) || op >= MMX_HADD) out = (u64)r.d[0] | (u64)r.d[2] << 32;
    memcpy(d, &out, 8);
}
typedef RcnXmm (*MmxShift)(RcnXmm, u64);
static const MmxShift mmx_shifts[] = {rcn_psllw, rcn_pslld, rcn_psllq, rcn_psrlw, rcn_psrld, rcn_psrlq, rcn_psraw, rcn_psrad};
void rcn_op_mmx_shift(u64 *d, u64 count, u32 op) {
    if (op >= sizeof mmx_shifts / sizeof mmx_shifts[0]) return;
    RcnXmm a = {{0}};
    memcpy(&a.q[0], d, 8);
    RcnXmm r = mmx_shifts[op](a, count);
    memcpy(d, &r.q[0], 8);
}

// Conversion flottant → entier de cvt(t)ss2si / cvt(t)sd2si (la valeur d'un float est exacte en double).
i64 rcn_op_cvt(double v, u32 bits, u32 truncate) {
    if (bits == 64) return truncate ? rcn_cvtt_f64_i64(v) : rcn_cvt_f64_i64(v);
    return (u32)(truncate ? rcn_cvtt_f64_i32(v) : rcn_cvt_f64_i32(v));
}
u32 rcn_op_fcmp(double a, double b, int pred) { return (u32)rcn_cmp_f64(a, b, pred); }

// --- Divisions : rdx:rax / b (ou dx:ax, ah:al) ---

void rcn_op_div(RcnCtx *ctx, u64 b, u32 bits, u32 is_signed, u64 rva) {
    switch (bits) {
    case 64:
        if (is_signed) rcn_idiv64(ctx, b, rva);
        else rcn_div64(ctx, b, rva);
        return;
    case 32:
        if (is_signed) rcn_idiv32(ctx, (u32)b, rva);
        else rcn_div32(ctx, (u32)b, rva);
        return;
    case 16: {
        u32 n = (u32)(u16)ctx->r[2] << 16 | (u16)ctx->r[0];
        if ((u16)b == 0) rcn_trap(ctx, rva, "division par zero");
        u32 q, r;
        if (is_signed) {
            i32 sn = (i32)n, sb = (i16)b;
            i32 sq = sn / sb;
            if (sq != (i16)sq) rcn_trap(ctx, rva, "debordement de division");
            q = (u32)sq;
            r = (u32)(sn % sb);
        } else {
            q = n / (u16)b;
            if (q > 0xFFFF) rcn_trap(ctx, rva, "debordement de division");
            r = n % (u16)b;
        }
        ctx->r[0] = (ctx->r[0] & ~0xFFFFull) | (u16)q;
        ctx->r[2] = (ctx->r[2] & ~0xFFFFull) | (u16)r;
        return;
    }
    default: { // 8 bits : ax / b → al (quotient), ah (reste)
        u32 n = (u16)ctx->r[0];
        if ((u8)b == 0) rcn_trap(ctx, rva, "division par zero");
        u32 q, r;
        if (is_signed) {
            i32 sn = (i16)n, sb = (i8)b;
            i32 sq = sn / sb;
            if (sq != (i8)sq) rcn_trap(ctx, rva, "debordement de division");
            q = (u32)sq;
            r = (u32)(sn % sb);
        } else {
            q = n / (u8)b;
            if (q > 0xFF) rcn_trap(ctx, rva, "debordement de division");
            r = n % (u8)b;
        }
        ctx->r[0] = (ctx->r[0] & ~0xFFFFull) | (u8)q | (u32)(u8)r << 8;
        return;
    }
    }
}

// --- cmpxchg16b ---
// Atomique si le compilateur sait faire une comparaison-échange de 16 octets (ARM64 : LDAXP/STLXP) ;
// sinon sous un verrou choisi d'après l'adresse.
void rcn_op_cmpxchg16b(RcnCtx *ctx, u64 addr) {
    u64 expected_lo = ctx->r[0], expected_hi = ctx->r[2];
    u64 desired_lo = ctx->r[3], desired_hi = ctx->r[1];
    u64 old_lo, old_hi;
    int equal;
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
    unsigned __int128 expected = (unsigned __int128)expected_hi << 64 | expected_lo;
    unsigned __int128 desired = (unsigned __int128)desired_hi << 64 | desired_lo;
    unsigned __int128 old = __sync_val_compare_and_swap((unsigned __int128 *)(uintptr_t)addr, expected, desired);
    old_lo = (u64)old;
    old_hi = (u64)(old >> 64);
    equal = old == expected;
#else
    static pthread_mutex_t locks[64] = {[0 ... 63] = PTHREAD_MUTEX_INITIALIZER};
    pthread_mutex_t *lock = &locks[(addr >> 4) & 63];
    pthread_mutex_lock(lock);
    old_lo = rd64(addr);
    old_hi = rd64(addr + 8);
    equal = old_lo == expected_lo && old_hi == expected_hi;
    if (equal) {
        wr64(addr, desired_lo);
        wr64(addr + 8, desired_hi);
    }
    pthread_mutex_unlock(lock);
#endif
    ctx->zf = (u8)equal;
    if (!equal) {
        ctx->r[0] = old_lo;
        ctx->r[2] = old_hi;
    }
}

// --- Drapeaux ---

// sahf : SF, ZF, AF, PF, CF depuis ah ; lahf : l'inverse.
void rcn_op_sahf(RcnCtx *ctx) { rcn_set_flags(ctx, (rcn_get_flags(ctx) & ~0xD5u) | ((ctx->r[0] >> 8) & 0xD5)); }
void rcn_op_lahf(RcnCtx *ctx) { ctx->r[0] = (ctx->r[0] & ~0xFF00ull) | (u64)((rcn_get_flags(ctx) & 0xD7) << 8); }

// --- x87 ---

void rcn_op_fcom(RcnCtx *ctx, double a, double b) { rcn_fcom(ctx, a, b); }
double rcn_op_fround(RcnCtx *ctx, double v) { return rcn_fround(ctx, v); }
u32 rcn_op_fsw(RcnCtx *ctx) { return rcn_fsw(ctx); }

i64 rcn_op_fist(RcnCtx *ctx, double v, u32 bits, u32 truncate) {
    switch (bits) {
    case 16: return truncate ? rcn_fistt16(ctx, v) : rcn_fist16(ctx, v);
    case 32: return truncate ? rcn_fistt32(ctx, v) : rcn_fist32(ctx, v);
    default: return truncate ? rcn_fistt64(ctx, v) : rcn_fist64(ctx, v);
    }
}

// Numérotées comme Math1 et Math2 dans native.rs.
double rcn_op_math1(double v, u32 which) {
    switch (which) {
    case 0: return sin(v);
    case 1: return cos(v);
    case 2: return tan(v);
    case 3: return log2(v);
    default: return exp2(v) - 1.0;
    }
}
double rcn_op_math2(double a, double b, u32 which) {
    switch (which) {
    case 0: return atan2(a, b);
    case 1: return fmod(a, b);
    case 2: return remainder(a, b);
    case 3: return scalbn(a, (int)rcn_fistt32(NULL, b));
    case 4: return a * log2(b);
    default: return a * log2(b + 1.0);
    }
}

// --- Fonctions que Cranelift appelle quand le processeur n'a pas l'instruction (libcall:…) ---

static float ceil_f32(float v) { return ceilf(v); }
static double ceil_f64(double v) { return ceil(v); }
static float floor_f32(float v) { return floorf(v); }
static double floor_f64(double v) { return floor(v); }
static float trunc_f32(float v) { return truncf(v); }
static double trunc_f64(double v) { return trunc(v); }
static float nearest_f32(float v) { return nearbyintf(v); }
static double nearest_f64(double v) { return nearbyint(v); }
static float fma_f32(float a, float b, float c) { return fmaf(a, b, c); }
static double fma_f64(double a, double b, double c) { return fma(a, b, c); }

// Arrêt du jeu sur une instruction ou un saut non gérés : message rangé avec le code.
// Opération atomique sur une adresse non alignée (refusée par le processeur ARM64) : sous un verrou global.
// op : 0 add, 1 sub, 2 and, 3 or, 4 xor, 5 xchg, 6 cmpxchg (expected). Rend l'ancienne valeur.
static pthread_mutex_t unaligned_lock = PTHREAD_MUTEX_INITIALIZER;
u64 rcn_op_atomic_unaligned(u64 addr, u32 op, u32 size, u64 value, u64 expected) {
    u64 mask = size >= 8 ? ~0ull : (1ull << (8 * size)) - 1, old = 0, now;
    pthread_mutex_lock(&unaligned_lock);
    memcpy(&old, PTR(addr), size);
    switch (op) {
    case 0: now = old + value; break;
    case 1: now = old - value; break;
    case 2: now = old & value; break;
    case 3: now = old | value; break;
    case 4: now = old ^ value; break;
    case 5: now = value; break;
    default: now = ((old ^ expected) & mask) == 0 ? value : old; break;
    }
    now &= mask;
    memcpy(PTR(addr), &now, size);
    pthread_mutex_unlock(&unaligned_lock);
    return old & mask;
}

u64 rcn_op_rcx(RcnCtx *ctx, u64 value, u64 count, u32 info) {
    u32 size = info & 0xFF, bits = size * 8;
    int left = (info & 0x100) != 0;
    u64 mask = bits == 64 ? ~0ull : (1ull << bits) - 1;
    // Compte masqué comme le processeur, puis réduit modulo (bits + 1) pour 8 et 16 bits.
    u32 n = (u32)count & (bits == 64 ? 63 : 31);
    if (bits < 32) n %= bits + 1;
    value &= mask;
    if (n == 0) return value;
    u64 cf = ctx->cf & 1;
    for (u32 i = 0; i < n; i++) {
        if (left) {
            u64 out = value >> (bits - 1) & 1;
            value = ((value << 1) | cf) & mask;
            cf = out;
        } else {
            u64 out = value & 1;
            value = (value >> 1) | (cf << (bits - 1));
            cf = out;
        }
    }
    ctx->cf = (u8)cf;
    // OF (défini pour un compte de 1) : rcl : MSB xor CF ; rcr : xor des deux bits de poids fort.
    if (left) ctx->of = (u8)((value >> (bits - 1) & 1) ^ cf);
    else ctx->of = (u8)((value >> (bits - 1) ^ value >> (bits - 2)) & 1);
    return value;
}

// FXSAVE : FCW 0, FSW 2, FTW abrégé 4, MXCSR 24, MXCSR_MASK 28, ST(i) 32 + 16 i, XMM(i) 160 + 16 i.
void rcn_op_fxsave(RcnCtx *ctx, u64 a) {
    memset(PTR(a), 0, 512);
    wr16(a, ctx->fp_cw);
    wr16(a + 2, (u16)((ctx->fp_sw & ~0x3800u) | (u16)((ctx->fp_top & 7) << 11)));
    wr8(a + 4, 0xFF);
    wr32(a + 24, ctx->mxcsr);
    wr32(a + 28, 0xFFFF);
    for (int i = 0; i < 8; i++) rcn_st80(a + 32 + 16 * (u64)i, ST(i));
    for (int i = 0; i < 16; i++) memcpy(PTR(a + 160 + 16 * (u64)i), &ctx->xmm[i], 16);
}
void rcn_op_fxrstor(RcnCtx *ctx, u64 a) {
    ctx->fp_cw = rd16(a);
    u16 sw = rd16(a + 2);
    ctx->fp_top = (sw >> 11) & 7;
    ctx->fp_sw = sw;
    ctx->mxcsr = rd32(a + 24);
    for (int i = 0; i < 8; i++) ST(i) = rcn_ld80(a + 32 + 16 * (u64)i);
    for (int i = 0; i < 16; i++) memcpy(&ctx->xmm[i], PTR(a + 160 + 16 * (u64)i), 16);
}

// Environnement x87 au format 32 bits protégé : FCW 0, FSW 4, FTW 8 (2 bits par registre), FIP 12, FCS 16,
// FDP 20, FDS 24 (adresses des dernières instructions : non suivies, à zéro) ; fnsave ajoute ST0 … ST7 en
// 80 bits à partir de 28, puis réinitialise l'unité comme fninit. Les étiquettes ne sont pas suivies : toutes
// valides à l'enregistrement, ignorées au chargement.
void rcn_op_fenv(RcnCtx *ctx, u64 a, u32 which) {
    int save = which == 0 || which == 2, regs = which >= 2;
    if (save) {
        memset(PTR(a), 0, 28);
        wr16(a, ctx->fp_cw);
        wr16(a + 2, 0xFFFF);
        wr16(a + 4, (u16)((ctx->fp_sw & ~0x3800u) | (u16)((ctx->fp_top & 7) << 11)));
        wr16(a + 6, 0xFFFF);
        wr16(a + 10, 0xFFFF);
        if (regs) {
            for (int i = 0; i < 8; i++) rcn_st80(a + 28 + 10 * (u64)i, ST(i));
            ctx->fp_cw = 0x037F;
            ctx->fp_sw = 0;
            ctx->fp_top = 0;
        }
    } else {
        ctx->fp_cw = rd16(a);
        u16 sw = rd16(a + 4);
        ctx->fp_top = (sw >> 11) & 7;
        ctx->fp_sw = sw;
        if (regs)
            for (int i = 0; i < 8; i++) ST(i) = rcn_ld80(a + 28 + 10 * (u64)i);
    }
}

_Noreturn void rcn_trap_index(RcnCtx *ctx, u64 rva, u32 index) {
    // Module du code traduit qui s'arrête : celui qui contient l'adresse de retour (exécutable ou DLL).
    const RcnModule *m = rcn_code_module((u64)(uintptr_t)__builtin_return_address(0));
    if (!m) m = ctx->proc->module;
    rcn_fail(ctx, "%s (%s+0x%llx)", index < m->message_count ? m->messages[index] : "arret", m->name, (unsigned long long)rva);
}

void rcn_setjmp_native(RcnCtx *ctx, u64 guest_buf, u64 token, u32 site);

// Code traduit avec --nan-probe : une opération flottante vient de produire un NaN à partir d'opérandes qui
// n'en étaient pas (0/0, inf-inf, racine d'un négatif…). Noté une fois par instruction, avec la pile du jeu.
void rcn_op_nan_born(RcnCtx *ctx, u64 rva) {
    const RcnModule *m = rcn_code_module((u64)(uintptr_t)__builtin_return_address(0));
    if (!m) m = ctx->proc->module;
    static struct {
        const RcnModule *m;
        u64 rva;
    } seen[1024];
    static int count;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    int known = 0;
    for (int i = 0; i < count && !known; i++) known = seen[i].m == m && seen[i].rva == rva;
    if (!known && count < 1024) seen[count].m = m, seen[count].rva = rva, count++;
    pthread_mutex_unlock(&lock);
    if (known) return;
    char what[96];
    snprintf(what, sizeof what, "%s né en %s+0x%llx", rva >> 63 ? "infini" : "NaN", m->name, (unsigned long long)(rva & ~(1ull << 63)));
    rcn_guest_stack_once(ctx, what);
}

// Code traduit avec --probe-fn : entrée d'une fonction surveillée, notée une fois (avec la pile du jeu),
// puis comptée ; le compte de chacune est écrit toutes les 2000 entrées.
void rcn_op_probe(RcnCtx *ctx, u64 rva) {
    static struct {
        u64 rva, count;
    } seen[64];
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    int k = 0;
    while (k < 64 && seen[k].rva && seen[k].rva != rva) k++;
    u64 n = 0;
    if (k < 64) {
        seen[k].rva = rva;
        n = ++seen[k].count;
    }
    pthread_mutex_unlock(&lock);
    if (n == 1) {
        char what[64];
        snprintf(what, sizeof what, "fonction surveillée 0x%llx", (unsigned long long)rva);
        rcn_guest_stack_once(ctx, what);
    } else if (n % 2000 == 0) {
        rcn_log("fonction surveillée 0x%llx : %llu entrées", (unsigned long long)rva, (unsigned long long)n);
    }
}

// Fonctions que le code machine traduit peut appeler, par leur nom (fichier .rcnx).
#define H(name) {#name, (void *)name}
#define HOP(name) {"rcn_op_" #name, (void *)rcn_op_##name}
const RcnHelper rcn_helpers[] = {
    H(rcn_call), H(rcn_trap_index), HOP(nan_born), HOP(probe), H(rcn_cpuid), H(rcn_rdtsc), H(rcn_movs), H(rcn_stos), H(rcn_lods), H(rcn_cmps),
    H(rcn_scas), H(rcn_get_flags), H(rcn_set_flags), H(rcn_setjmp_native), H(rcn_ld80), H(rcn_st80), H(rcn_fxam),
    HOP(mmx), HOP(mmx_shift), HOP(div), HOP(cmpxchg16b), HOP(fxsave), HOP(fxrstor), HOP(fenv), HOP(rcx), HOP(atomic_unaligned), HOP(sahf), HOP(lahf), HOP(cvt), HOP(fcmp), HOP(fcom), HOP(fround), HOP(fsw), HOP(fist), HOP(math1), HOP(math2),
    HOP(addps), HOP(subps), HOP(mulps), HOP(divps), HOP(minps), HOP(maxps),
    HOP(addpd), HOP(subpd), HOP(mulpd), HOP(divpd), HOP(minpd), HOP(maxpd),
    HOP(andps), HOP(andpd), HOP(pand), HOP(orps), HOP(orpd), HOP(por), HOP(xorps), HOP(xorpd), HOP(pxor),
    HOP(andnps), HOP(andnpd), HOP(pandn),
    HOP(paddb), HOP(paddw), HOP(paddd), HOP(paddq), HOP(psubb), HOP(psubw), HOP(psubd), HOP(psubq),
    HOP(pcmpeqb), HOP(pcmpeqw), HOP(pcmpeqd), HOP(pcmpgtb), HOP(pcmpgtw), HOP(pcmpgtd),
    HOP(pmullw), HOP(pmulld), HOP(pmuludq),
    HOP(punpcklbw), HOP(punpcklwd), HOP(punpckldq), HOP(punpcklqdq),
    HOP(punpckhbw), HOP(punpckhwd), HOP(punpckhdq), HOP(punpckhqdq),
    HOP(unpcklps), HOP(unpckhps), HOP(unpcklpd), HOP(unpckhpd),
    HOP(pminub), HOP(pmaxub), HOP(pminsw), HOP(pmaxsw),
    HOP(packuswb), HOP(packsswb), HOP(packssdw), HOP(movlhps), HOP(movhlps),
    HOP(paddusb), HOP(psubusb), HOP(pavgb), HOP(pmaddwd), HOP(haddps),
    HOP(addsubps), HOP(addsubpd), HOP(hsubps), HOP(haddpd), HOP(hsubpd), HOP(pmaddubsw), HOP(pshufb),
    HOP(phaddw), HOP(phaddd), HOP(phaddsw), HOP(phsubw), HOP(phsubd), HOP(phsubsw), HOP(pmulhrsw),
    HOP(psignb), HOP(psignw), HOP(psignd), HOP(pabsb), HOP(pabsw), HOP(pabsd), HOP(palignr),
    HOP(sqrtps), HOP(sqrtpd), HOP(cvtdq2ps), HOP(cvtps2dq), HOP(cvttps2dq), HOP(cvtpd2ps), HOP(cvttpd2dq), HOP(cvtpd2dq), HOP(rsqrtps), HOP(paddsb), HOP(psubsb), HOP(paddsw), HOP(psubsw), HOP(paddusw), HOP(psubusw), HOP(pmulhw), HOP(pmulhuw), HOP(pavgw), HOP(psadbw), HOP(movsldup), HOP(movshdup), HOP(rcpps),
    HOP(cvtdq2pd), HOP(cvtps2pd), HOP(movddup),
    HOP(psllw), HOP(pslld), HOP(psllq), HOP(psrlw), HOP(psrld), HOP(psrlq), HOP(psraw), HOP(psrad), HOP(pslldq), HOP(psrldq),
    HOP(movmskps), HOP(movmskpd), HOP(pmovmskb),
    HOP(pshufd), HOP(pshuflw), HOP(pshufhw), HOP(shufps), HOP(shufpd), HOP(cmpps), HOP(cmppd),
    HOP(pmuldq), HOP(dpps), HOP(dppd), HOP(phminposuw), HOP(blendvps), HOP(blendvpd), HOP(pblendvb), HOP(pcmpstr),
    {"libcall:CeilF32", (void *)ceil_f32}, {"libcall:CeilF64", (void *)ceil_f64},
    {"libcall:FloorF32", (void *)floor_f32}, {"libcall:FloorF64", (void *)floor_f64},
    {"libcall:TruncF32", (void *)trunc_f32}, {"libcall:TruncF64", (void *)trunc_f64},
    {"libcall:NearestF32", (void *)nearest_f32}, {"libcall:NearestF64", (void *)nearest_f64},
    {"libcall:FmaF32", (void *)fma_f32}, {"libcall:FmaF64", (void *)fma_f64},
    {"libcall:Memcpy", (void *)memcpy}, {"libcall:Memset", (void *)memset}, {"libcall:Memmove", (void *)memmove},
};
const size_t rcn_helper_count = sizeof rcn_helpers / sizeof rcn_helpers[0];
