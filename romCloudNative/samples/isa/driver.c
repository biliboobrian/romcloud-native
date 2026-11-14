// Programme de test des instructions (voir recompiler/examples/isagen.rs) : exécute chaque forme
// d'instruction relevée avec des états tirés au hasard et imprime l'état obtenu, une ligne par essai.
// Les valeurs qui pointent dans le tampon de travail sont imprimées relativement à lui.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long long u64;
typedef struct {
    u64 r[16];
    u64 flags;
    unsigned char x[16][16];
} State;
typedef struct {
    void (*fn)(State *);
    const char *name;
    unsigned masks, special, flags, rva;
} Test;

extern Test isa_table[];
extern int isa_count;

static unsigned char buf[65536] __attribute__((aligned(64)));
static u64 seed;

static u64 next(void) {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}
static u64 value(void) {
    static const u64 special[] = {0, 1, 2, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0x80000000, 0xffffffff,
                                  0x100000000ull, 0x7fffffffffffffffull, 0x8000000000000000ull, ~0ull};
    u64 k = next() % 4;
    if (k == 0) return special[next() % 16];
    if (k == 1) return next() % 64;
    return next();
}
static u64 rel(u64 v) {
    u64 base = (u64)(size_t)buf;
    return v - base < sizeof buf ? (v - base) | 0xB0F0000000000000ull : v;
}

int main(int argc, char **argv) {
    int first = 0, last = isa_count;
    if (argc > 1) sscanf(argv[1], "%d", &first);
    if (argc > 2) sscanf(argv[2], "%d", &last);
    // Arguments suivants : tests à sauter (ceux qui plantent sur le processeur avec ces états).
    for (int t = first; t < last && t < isa_count; t++) {
        int skip = 0;
        for (int a = 3; a < argc; a++) skip |= atoi(argv[a]) == t;
        if (skip) continue;
        Test *test = &isa_table[t];
        for (int k = 0; k < 4; k++) {
            seed = 0x9E3779B97F4A7C15ull ^ ((u64)t << 20) ^ (u64)k;
            for (int i = 0; i < 20; i++) next();
            State s;
            for (int i = 0; i < 16; i++) s.r[i] = value();
            for (int i = 0; i < 16; i++)
                for (int j = 0; j < 16; j += 8) {
                    u64 v = value();
                    memcpy(&s.x[i][j], &v, 8);
                }
            for (size_t i = 0; i < sizeof buf; i += 8) {
                u64 v = value();
                memcpy(buf + i, &v, 8);
            }
            s.flags = 2 | (next() & 0x8D5); // CF PF AF ZF SF OF
            for (int i = 0; i < 16; i++) {
                if (test->masks >> i & 1) s.r[i] = (u64)(size_t)(buf + 32768 + (next() % 4) * 16 + (test->special >> 8 & 15));
                if (test->masks >> (16 + i) & 1) s.r[i] = next() % 16;
            }
            if (test->special & 1) {
                s.r[2] = 0;
                s.r[0] &= 0x7fffffffffffffffull;
                if (test->special & 2) s.r[0] &= 0xff;
                if (test->special & 4) s.r[0] &= (1ull << (8 * (test->special >> 4 & 15) - 2)) - 1;
                if (test->special >> 16) s.r[(test->special >> 16) - 1] |= 1;
            }
            fprintf(stderr, "%d %s\n", t, test->name);
            test->fn(&s);
            u64 h = 1469598103934665603ull;
            for (size_t i = 0; i < sizeof buf; i++) h = (h ^ buf[i]) * 1099511628211ull;
            printf("%d.%d", t, k);
            for (int i = 0; i < 16; i++)
                if (i != 4) printf(" %llx", rel(s.r[i]));
            printf(" f%llx", s.flags & test->flags);
            for (int i = 0; i < 16; i++) {
                u64 a, b;
                memcpy(&a, &s.x[i][0], 8);
                memcpy(&b, &s.x[i][8], 8);
                printf(" %llx:%llx", a, b);
            }
            printf(" m%llx\n", h);
        }
    }
    fflush(stdout);
    return 0;
}
