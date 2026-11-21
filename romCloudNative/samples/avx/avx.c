// AVX, AVX2, FMA, F16C (code machine traduit avec RCN_AVX=1) : chaque opération sur des données fixes,
// résultat affiché en hexadécimal, à comparer entre le processeur du PC (exécution directe) et la
// traduction. Les fonctions sont gardées hors ligne pour que le compilateur garde les instructions.
#include <immintrin.h>
#include <stdio.h>
#include <string.h>

#define NOINLINE __attribute__((noinline))

static float fa[8] = {1.5f, -2.25f, 3.0f, 4.75f, -5.5f, 6.0f, 0.125f, -8.0f};
static float fb[8] = {0.5f, 2.0f, -1.0f, 3.25f, 7.0f, -0.75f, 9.5f, 1.0f};
static double da[4] = {1.5, -2.25, 3.0, 4.75};
static double db[4] = {0.5, 2.0, -1.0, 3.25};
static int ia[8] = {1, -2, 3, 0x7FFFFFFF, -5, 6, 7, -8};
static int ib[8] = {3, 1, 4, 1, 5, 9, 2, 6};

static void show256(const char *name, __m256i v) {
    unsigned u[8];
    memcpy(u, &v, 32);
    printf("%-14s", name);
    for (int i = 0; i < 8; i++) printf(" %08x", u[i]);
    printf("\n");
}
static void show128(const char *name, __m128i v) {
    unsigned u[4];
    memcpy(u, &v, 16);
    printf("%-14s", name);
    for (int i = 0; i < 4; i++) printf(" %08x", u[i]);
    printf("\n");
}
#define S256(name, v) show256(name, _mm256_castps_si256(v))
#define D256(name, v) show256(name, _mm256_castpd_si256(v))
#define I256(name, v) show256(name, v)
#define S128(name, v) show128(name, _mm_castps_si128(v))
#define I128(name, v) show128(name, v)

NOINLINE static void floats(void) {
    __m256 a = _mm256_loadu_ps(fa), b = _mm256_loadu_ps(fb);
    S256("add", _mm256_add_ps(a, b));
    S256("sub", _mm256_sub_ps(a, b));
    S256("sub b-a", _mm256_sub_ps(b, a));
    S256("mul", _mm256_mul_ps(a, b));
    S256("div", _mm256_div_ps(a, b));
    S256("min", _mm256_min_ps(a, b));
    S256("max", _mm256_max_ps(a, b));
    S256("sqrt", _mm256_sqrt_ps(_mm256_mul_ps(a, a)));
    S256("and", _mm256_and_ps(a, b));
    S256("andnot", _mm256_andnot_ps(a, b));
    S256("xor", _mm256_xor_ps(a, b));
    S256("unpacklo", _mm256_unpacklo_ps(a, b));
    S256("unpackhi", _mm256_unpackhi_ps(a, b));
    S256("shuffle", _mm256_shuffle_ps(a, b, 0x1B));
    S256("blend", _mm256_blend_ps(a, b, 0xA5));
    S256("blendv", _mm256_blendv_ps(a, b, a));
    S256("hadd", _mm256_hadd_ps(a, b));
    S256("cmp lt", _mm256_cmp_ps(a, b, _CMP_LT_OQ));
    S256("cmp ge", _mm256_cmp_ps(a, b, _CMP_GE_OQ));
    S256("cmp neq_uq", _mm256_cmp_ps(a, b, _CMP_NEQ_UQ));
    S256("round", _mm256_round_ps(a, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
    S256("floor", _mm256_floor_ps(a));
    S256("permute", _mm256_permute_ps(a, 0x4E));
    S256("permutevar", _mm256_permutevar_ps(a, _mm256_loadu_si256((const __m256i *)ib)));
    S256("perm2f128", _mm256_permute2f128_ps(a, b, 0x21));
    S256("insertf128", _mm256_insertf128_ps(a, _mm256_castps256_ps128(b), 1));
    S128("extractf128", _mm256_extractf128_ps(a, 1));
    S256("broadcast", _mm256_broadcast_ss(&fb[3]));
    S256("movehdup", _mm256_movehdup_ps(a));
    S256("dp", _mm256_dp_ps(a, b, 0xF1));
    I256("cvtps2dq", _mm256_cvtps_epi32(a));
    I256("cvttps2dq", _mm256_cvttps_epi32(a));
    S256("cvtdq2ps", _mm256_cvtepi32_ps(_mm256_loadu_si256((const __m256i *)ia)));
    printf("movmsk        %x\n", _mm256_movemask_ps(a));
    printf("testz         %d %d\n", _mm256_testz_ps(a, b), _mm256_testc_ps(a, b));
    // 128 bits (VEX) : trois opérandes, moitié haute remise à zéro.
    __m128 x = _mm_loadu_ps(fa), y = _mm_loadu_ps(fb);
    S128("add128", _mm_add_ps(x, y));
    S128("sub128 b-a", _mm_sub_ps(y, x));
    S128("addss", _mm_add_ss(x, y));
    S128("cmp128 gt", _mm_cmp_ps(x, y, _CMP_GT_OQ));
    S128("rcp-ish", _mm_div_ps(_mm_set1_ps(1.0f), y));
    S128("insert", _mm_insert_ps(x, y, 0x50));
}

NOINLINE static void doubles(void) {
    __m256d a = _mm256_loadu_pd(da), b = _mm256_loadu_pd(db);
    D256("addpd", _mm256_add_pd(a, b));
    D256("divpd", _mm256_div_pd(a, b));
    D256("shufflepd", _mm256_shuffle_pd(a, b, 0x5));
    D256("blendpd", _mm256_blend_pd(a, b, 0x6));
    D256("unpacklopd", _mm256_unpacklo_pd(a, b));
    D256("cvtps2pd", _mm256_cvtps_pd(_mm_loadu_ps(fa)));
    S128("cvtpd2ps", _mm256_cvtpd_ps(a));
    I128("cvttpd2dq", _mm256_cvttpd_epi32(a));
    D256("cvtdq2pd", _mm256_cvtepi32_pd(_mm_loadu_si128((const __m128i *)ia)));
    D256("permute4x64", _mm256_permute4x64_pd(a, 0x1B));
    D256("broadcastsd", _mm256_broadcast_sd(&db[2]));
    D256("permutepd", _mm256_permute_pd(a, 0x5));
    printf("movmskpd      %x\n", _mm256_movemask_pd(a));
}

NOINLINE static void fma(void) {
    __m256 a = _mm256_loadu_ps(fa), b = _mm256_loadu_ps(fb), c = _mm256_set1_ps(0.1f);
    S256("fmadd", _mm256_fmadd_ps(a, b, c));
    S256("fmsub", _mm256_fmsub_ps(a, b, c));
    S256("fnmadd", _mm256_fnmadd_ps(a, b, c));
    S256("fnmsub", _mm256_fnmsub_ps(a, b, c));
    S256("fmaddsub", _mm256_fmaddsub_ps(a, b, c));
    __m256d x = _mm256_loadu_pd(da), y = _mm256_loadu_pd(db), z = _mm256_set1_pd(0.1);
    D256("fmaddpd", _mm256_fmadd_pd(x, y, z));
    __m128 s = _mm_loadu_ps(fa), t = _mm_loadu_ps(fb);
    S128("fmaddss", _mm_fmadd_ss(s, t, _mm_set1_ps(0.1f)));
    // Une somme accumulée : forme 231 dans une boucle.
    __m256 acc = _mm256_setzero_ps();
    for (int i = 0; i < 5; i++) acc = _mm256_fmadd_ps(a, b, acc);
    S256("fma loop", acc);
}

NOINLINE static void integers(void) {
    __m256i a = _mm256_loadu_si256((const __m256i *)ia), b = _mm256_loadu_si256((const __m256i *)ib);
    I256("paddd", _mm256_add_epi32(a, b));
    I256("psubd", _mm256_sub_epi32(a, b));
    I256("pmulld", _mm256_mullo_epi32(a, b));
    I256("pmuludq", _mm256_mul_epu32(a, b));
    I256("pminsd", _mm256_min_epi32(a, b));
    I256("pmaxud", _mm256_max_epu32(a, b));
    I256("pcmpgtd", _mm256_cmpgt_epi32(a, b));
    I256("pcmpeqd", _mm256_cmpeq_epi32(a, a));
    I256("pand", _mm256_and_si256(a, b));
    I256("pandn", _mm256_andnot_si256(a, b));
    I256("por", _mm256_or_si256(a, b));
    I256("pslld", _mm256_slli_epi32(a, 3));
    I256("psrad", _mm256_srai_epi32(a, 2));
    I256("psrlq xmm", _mm256_srl_epi64(a, _mm_set_epi64x(0, 5)));
    I256("pslldq", _mm256_slli_si256(a, 3));
    I256("psllvd", _mm256_sllv_epi32(a, b));
    I256("psravd", _mm256_srav_epi32(a, b));
    I256("psrlvq", _mm256_srlv_epi64(a, b));
    I256("pshufd", _mm256_shuffle_epi32(a, 0x39));
    I256("pshufb", _mm256_shuffle_epi8(a, b));
    I256("punpcklbw", _mm256_unpacklo_epi8(a, b));
    I256("packssdw", _mm256_packs_epi32(a, b));
    I256("packusdw", _mm256_packus_epi32(a, b));
    I256("palignr", _mm256_alignr_epi8(a, b, 5));
    I256("pabsd", _mm256_abs_epi32(a));
    I256("phaddd", _mm256_hadd_epi32(a, b));
    I256("pmaddwd", _mm256_madd_epi16(a, b));
    I256("pavgb", _mm256_avg_epu8(a, b));
    I256("psadbw", _mm256_sad_epu8(a, b));
    I256("permd", _mm256_permutevar8x32_epi32(a, b));
    I256("permq", _mm256_permute4x64_epi64(a, 0x93));
    I256("perm2i128", _mm256_permute2x128_si256(a, b, 0x30));
    I256("inserti128", _mm256_inserti128_si256(a, _mm256_castsi256_si128(b), 1));
    I128("extracti128", _mm256_extracti128_si256(a, 1));
    I256("pblendd", _mm256_blend_epi32(a, b, 0x5A));
    I256("pblendw", _mm256_blend_epi16(a, b, 0x33));
    I256("pblendvb", _mm256_blendv_epi8(a, b, a));
    I256("pbroadcastd", _mm256_broadcastd_epi32(_mm256_castsi256_si128(b)));
    I256("pbroadcastb", _mm256_set1_epi8((char)ia[3]));
    I256("pmovzxbd", _mm256_cvtepu8_epi32(_mm_loadu_si128((const __m128i *)ia)));
    I256("pmovsxwd", _mm256_cvtepi16_epi32(_mm_loadu_si128((const __m128i *)ia)));
    I256("pmovsxdq", _mm256_cvtepi32_epi64(_mm_loadu_si128((const __m128i *)ia)));
    I256("maskload", _mm256_maskload_epi32(ib, a));
    printf("pmovmskb      %08x\n", (unsigned)_mm256_movemask_epi8(a));
    printf("ptest         %d %d\n", _mm256_testz_si256(a, b), _mm256_testc_si256(a, a));
    int out[8] = {0};
    _mm256_maskstore_epi32(out, a, b);
    I256("maskstore", _mm256_loadu_si256((const __m256i *)out));
    // 128 bits (VEX).
    __m128i x = _mm_loadu_si128((const __m128i *)ia), y = _mm_loadu_si128((const __m128i *)ib);
    I128("paddd128", _mm_add_epi32(x, y));
    I128("psubd128 b-a", _mm_sub_epi32(y, x));
    I128("pshufb128", _mm_shuffle_epi8(x, y));
    I128("psllvd128", _mm_sllv_epi32(x, y));
}

NOINLINE static void halves(void) {
    // F16C.
    __m128i h = _mm_cvtps_ph(_mm_loadu_ps(fa), _MM_FROUND_TO_NEAREST_INT);
    I128("cvtps2ph", h);
    S256("cvtph2ps", _mm256_cvtph_ps(_mm_set_epi16(0x7C00, (short)0xFC00, 0x3C00, (short)0xC000, 0x0001, 0x3555, 0x7BFF, 0x0400)));
    // Boucle vectorisée par le compilateur (-O2 -mavx2) : somme de produits.
    static float p[64], q[64];
    for (int i = 0; i < 64; i++) p[i] = (float)i * 0.5f, q[i] = (float)(64 - i);
    float sum = 0;
    for (int i = 0; i < 64; i++) sum += p[i] * q[i];
    printf("sum           %08x\n", *(unsigned *)&sum);
}

int main(void) {
    floats();
    doubles();
    fma();
    integers();
    halves();
    return 0;
}
