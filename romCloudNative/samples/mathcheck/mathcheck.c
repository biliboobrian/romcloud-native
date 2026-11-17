// Fonctions mathématiques du CRT appelées par les jeux (liste de Clair Obscur) sur des valeurs limites :
// bits du résultat, à comparer entre l'UCRT de Windows (exécution directe) et les fonctions du runtime
// (exécution traduite). Un écart sur un NaN, un infini ou un zéro signé change le chemin d'un jeu.
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// UCRT (api-ms-win-crt-math), absentes des en-têtes de mingw.
short __cdecl _dclass(double);
int __cdecl _dsign(double);
short __cdecl _fdclass(float);

static const double D[] = {0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 1.0000000000000002, -1.0000000000000002,
                           3.141592653589793, 1e-310, -1e-310, 1e300, -1e300, 710.0, -745.5, 0.1, 100.0, -3.5};
static const float F[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.0f, -2.0f, 1.0000001f, -1.0000001f,
                          3.1415927f, 1e-40f, -1e-40f, 1e38f, -1e38f, 89.0f, -104.0f, 0.1f, 100.0f, -3.5f};
enum { ND = sizeof D / sizeof D[0], NF = sizeof F / sizeof F[0] };

static volatile double inf_d, nan_d;
static volatile float inf_f, nan_f;

static unsigned long long bd(double v) {
    unsigned long long b;
    memcpy(&b, &v, 8);
    return b;
}
static unsigned bf(float v) {
    unsigned b;
    memcpy(&b, &v, 4);
    return b;
}

#define DLIST(x) for (int i = 0; i < ND + 3; i++) { double x = i < ND ? D[i] : i == ND ? inf_d : i == ND + 1 ? -inf_d : nan_d;
#define FLIST(x) for (int i = 0; i < NF + 3; i++) { float x = i < NF ? F[i] : i == NF ? inf_f : i == NF + 1 ? -inf_f : nan_f;
#define D1(fn) DLIST(x) printf(#fn " %d %016llx\n", i, bd(fn(x))); }
#define F1(fn) FLIST(x) printf(#fn " %d %08x\n", i, bf(fn(x))); }
#define D2(fn) DLIST(x) for (int j = 0; j < ND + 3; j++) { double y = j < ND ? D[j] : j == ND ? inf_d : j == ND + 1 ? -inf_d : nan_d; \
    printf(#fn " %d %d %016llx\n", i, j, bd(fn(x, y))); } }
#define F2(fn) FLIST(x) for (int j = 0; j < NF + 3; j++) { float y = j < NF ? F[j] : j == NF ? inf_f : j == NF + 1 ? -inf_f : nan_f; \
    printf(#fn " %d %d %08x\n", i, j, bf(fn(x, y))); } }

int main(void) {
    inf_d = HUGE_VAL, nan_d = inf_d - inf_d, inf_f = HUGE_VALF, nan_f = inf_f - inf_f;
    D1(sin) D1(cos) D1(tan) D1(asin) D1(acos) D1(atan) D1(exp) D1(log) D1(log10) D1(log2) D1(exp2) D1(sqrt)
    D1(floor) D1(ceil) D1(trunc) D1(round) D1(fabs) D1(cbrt)
    F1(sinf) F1(cosf) F1(tanf) F1(asinf) F1(acosf) F1(atanf) F1(expf) F1(logf) F1(log2f) F1(sqrtf) F1(floorf)
    F1(ceilf) F1(truncf) F1(roundf) F1(tanhf) F1(sinhf) F1(logbf)
    D2(atan2) D2(pow) D2(fmod) D2(hypot)
    F2(atan2f) F2(powf) F2(fmodf) F2(nextafterf)
    FLIST(x) printf("lroundf %d %ld\n", i, i < NF ? lroundf(x) : 0); }
    DLIST(x) int e; double m = frexp(x, &e); printf("frexp %d %016llx %d\n", i, bd(m), e); }
    DLIST(x) double ip; double fr = modf(x, &ip); printf("modf %d %016llx %016llx\n", i, bd(fr), bd(ip)); }
    FLIST(x) float ip; float fr = modff(x, &ip); printf("modff %d %08x %08x\n", i, bf(fr), bf(ip)); }
    DLIST(x) printf("ldexp %d %016llx\n", i, bd(ldexp(x, 3))); }
    DLIST(x) printf("_isnan %d %d _finite %d _dclass %d _dsign %d\n", i, _isnan(x), _finite(x), _dclass(x), _dsign(x)); }
    FLIST(x) printf("_fdclass %d %d\n", i, _fdclass(x)); }
    return 0;
}
