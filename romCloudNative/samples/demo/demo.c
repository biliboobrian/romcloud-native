// Programme de test du recompilateur : exe Windows x86-64 sans CRT (kernel32 seulement).
// Il exerce ce que le traducteur doit savoir faire avant de s'attaquer à un jeu : arithmétique
// entière et flottante, récursion, tables de sauts (switch), pointeurs de fonctions, tas, chaînes.
// Sa sortie doit être identique octet pour octet sous Windows et une fois recompilé pour ARM64.
//
// Compilation (llvm-mingw) :
//   x86_64-w64-mingw32-clang -O2 -ffreestanding -nostdlib -mno-stack-arg-probe -Wl,-e,start demo.c -lkernel32

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;
typedef __SIZE_TYPE__ usize; // taille d'une adresse : 4 octets en 32 bits, 8 en 64 bits

__declspec(dllimport) void *__stdcall GetStdHandle(u32 id);
__declspec(dllimport) int __stdcall WriteFile(void *h, const void *buf, u32 len, u32 *written, void *ov);
__declspec(dllimport) void __stdcall ExitProcess(u32 code);
__declspec(dllimport) void *__stdcall GetProcessHeap(void);
__declspec(dllimport) void *__stdcall HeapAlloc(void *heap, u32 flags, usize size);
__declspec(dllimport) int __stdcall HeapFree(void *heap, u32 flags, void *p);

// Le compilateur peut appeler memcpy / memset même sans CRT.
void *memcpy(void *d, const void *s, usize n) {
    u8 *dd = d;
    const u8 *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void *memset(void *d, int c, usize n) {
    u8 *dd = d;
    while (n--) *dd++ = (u8)c;
    return d;
}

static char out[8192];
static u32 outLen;

static void flush(void) {
    u32 written;
    WriteFile(GetStdHandle((u32)-11), out, outLen, &written, 0);
    outLen = 0;
}
static void putc_(char c) {
    if (outLen == sizeof(out)) flush();
    out[outLen++] = c;
}
static void puts_(const char *s) {
    while (*s) putc_(*s++);
}
static void putu(u64 v, u32 base) {
    char tmp[65];
    int n = 0;
    do {
        u32 d = (u32)(v % base);
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v);
    while (n) putc_(tmp[--n]);
}
static void puti(i64 v) {
    if (v < 0) {
        putc_('-');
        putu((u64)(-v), 10);
    } else {
        putu((u64)v, 10);
    }
}
// Nombre flottant avec 6 décimales (troncature).
static void putf(double v) {
    if (v < 0) {
        putc_('-');
        v = -v;
    }
    u64 ip = (u64)v;
    putu(ip, 10);
    putc_('.');
    double frac = v - (double)ip;
    for (int i = 0; i < 6; i++) {
        frac *= 10.0;
        int d = (int)frac;
        putc_((char)('0' + d));
        frac -= d;
    }
}

// --- Récursion ---
__attribute__((noinline)) static u64 fib(u32 n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

__attribute__((noinline)) static u64 ackermann(u64 m, u64 n) {
    if (m == 0) return n + 1;
    if (n == 0) return ackermann(m - 1, 1);
    return ackermann(m - 1, ackermann(m, n - 1));
}

// --- Table de sauts ---
__attribute__((noinline)) static const char *opName(int op) {
    switch (op) {
    case 0: return "add";
    case 1: return "sub";
    case 2: return "mul";
    case 3: return "div";
    case 4: return "mod";
    case 5: return "and";
    case 6: return "or";
    case 7: return "xor";
    case 8: return "shl";
    case 9: return "shr";
    default: return "?";
    }
}
__attribute__((noinline)) static i64 apply(int op, i64 a, i64 b) {
    switch (op) {
    case 0: return a + b;
    case 1: return a - b;
    case 2: return a * b;
    case 3: return b ? a / b : 0;
    case 4: return b ? a % b : 0;
    case 5: return a & b;
    case 6: return a | b;
    case 7: return a ^ b;
    case 8: return a << (b & 63);
    case 9: return (i64)((u64)a >> (b & 63));
    default: return -1;
    }
}

// --- Machine à pile : interpréteur à table de sauts indirects ---
enum { OP_PUSH, OP_ADD, OP_SUB, OP_MUL, OP_DUP, OP_SWAP, OP_DROP, OP_OVER, OP_JNZ, OP_DEC, OP_SHR, OP_AND, OP_PRINT, OP_HALT };
__attribute__((noinline)) static i64 vmRun(const signed char *code) {
    i64 stack[32];
    int sp = 0, pc = 0;
    for (;;) {
        switch (code[pc++]) {
        case OP_PUSH: stack[sp++] = code[pc++]; break;
        case OP_ADD: sp--; stack[sp - 1] += stack[sp]; break;
        case OP_SUB: sp--; stack[sp - 1] -= stack[sp]; break;
        case OP_MUL: sp--; stack[sp - 1] *= stack[sp]; break;
        case OP_DUP: stack[sp] = stack[sp - 1]; sp++; break;
        case OP_SWAP: { i64 t = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = t; break; }
        case OP_DROP: sp--; break;
        case OP_OVER: stack[sp] = stack[sp - 2]; sp++; break;
        case OP_JNZ: { int off = code[pc++]; if (stack[--sp]) pc += off; break; }
        case OP_DEC: stack[sp - 1]--; break;
        case OP_SHR: stack[sp - 1] = (i64)((u64)stack[sp - 1] >> 1); break;
        case OP_AND: sp--; stack[sp - 1] &= stack[sp]; break;
        case OP_PRINT: puti(stack[sp - 1]); putc_(' '); break;
        case OP_HALT: return stack[sp - 1];
        default: return -1;
        }
    }
}
// Factorielle de 15 par accumulation, en affichant les produits intermédiaires.
static const signed char vmProgram[] = {
    OP_PUSH, 1, OP_PUSH, 15,          // acc n
    OP_SWAP, OP_OVER, OP_MUL, OP_PRINT, // n acc*n
    OP_SWAP, OP_DEC, OP_DUP, OP_JNZ, -9,// acc n-1 ; boucle tant que n != 0
    OP_DROP, OP_DUP, OP_SHR, OP_PUSH, 127, OP_AND, OP_ADD, OP_HALT,
};

// --- Pointeurs de fonctions, tri ---
typedef int (*Cmp)(const void *, const void *);
static int cmpAsc(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}
static int cmpDesc(const void *a, const void *b) { return cmpAsc(b, a); }
static int cmpLowBits(const void *a, const void *b) {
    int x = *(const int *)a & 0xff, y = *(const int *)b & 0xff;
    return x != y ? x - y : cmpAsc(a, b);
}
__attribute__((noinline)) static void sortInts(int *v, int n, Cmp cmp) {
    for (int i = 1; i < n; i++) {
        int x = v[i], j = i - 1;
        while (j >= 0 && cmp(&v[j], &x) > 0) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
}

// --- Générateur pseudo-aléatoire, CRC32, hachage 64 bits ---
static u64 rngState = 0x853c49e6748fea9bULL;
static u32 rng(void) {
    u64 old = rngState;
    rngState = old * 6364136223846793005ULL + 1442695040888963407ULL;
    u32 x = (u32)(((old >> 18) ^ old) >> 27);
    u32 rot = (u32)(old >> 59);
    return (x >> rot) | (x << ((32 - rot) & 31));
}
__attribute__((noinline)) static u32 crc32(const u8 *p, u64 n) {
    u32 c = 0xffffffffu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}
__attribute__((noinline)) static u64 fnv1a(const char *s) {
    u64 h = 0xcbf29ce484222325ULL;
    while (*s) {
        h ^= (u8)*s++;
        h *= 0x100000001b3ULL;
    }
    return h;
}
#ifdef __SIZEOF_INT128__
__attribute__((noinline)) static u64 mulhi(u64 a, u64 b) { return (u64)(((unsigned __int128)a * b) >> 64); }
__attribute__((noinline)) static i64 smulhi(i64 a, i64 b) { return (i64)(((__int128)a * b) >> 64); }
#else
// 32 bits : pas d'entiers de 128 bits, produit par moitiés de 32 bits.
__attribute__((noinline)) static u64 mulhi(u64 a, u64 b) {
    u64 al = (u32)a, ah = a >> 32, bl = (u32)b, bh = b >> 32;
    u64 mid = ah * bl + ((al * bl) >> 32);
    u64 mid2 = al * bh + (u32)mid;
    return ah * bh + (mid >> 32) + (mid2 >> 32);
}
__attribute__((noinline)) static i64 smulhi(i64 a, i64 b) {
    u64 high = mulhi((u64)a, (u64)b);
    if (a < 0) high -= (u64)b;
    if (b < 0) high -= (u64)a;
    return (i64)high;
}
#endif

// --- Flottants ---
__attribute__((noinline)) static double sqrtNewton(double x) {
    double r = x > 1 ? x / 2 : 1;
    for (int i = 0; i < 40; i++) r = 0.5 * (r + x / r);
    return r;
}
__attribute__((noinline)) static double piLeibniz(int terms) {
    double s = 0, sign = 1;
    for (int i = 0; i < terms; i++) {
        s += sign / (2.0 * i + 1.0);
        sign = -sign;
    }
    return 4 * s;
}
__attribute__((noinline)) static float dot(const float *a, const float *b, int n) {
    float s = 0;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}
__attribute__((noinline)) static void mandelbrot(void) {
    static const char shades[] = " .:-=+*#%@";
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 56; x++) {
            double cr = -2.2 + x * (3.2 / 56), ci = -1.2 + y * (2.4 / 16);
            double zr = 0, zi = 0;
            int it = 0;
            while (it < 90 && zr * zr + zi * zi < 4.0) {
                double t = zr * zr - zi * zi + cr;
                zi = 2 * zr * zi + ci;
                zr = t;
                it++;
            }
            putc_(shades[it / 10]);
        }
        putc_('\n');
    }
}

// --- Tas : liste chaînée ---
typedef struct Node {
    struct Node *next;
    u64 value;
} Node;
__attribute__((noinline)) static u64 listTest(int n) {
    void *heap = GetProcessHeap();
    Node *head = 0;
    for (int i = 0; i < n; i++) {
        Node *node = HeapAlloc(heap, 8, sizeof(Node));
        node->value = (u64)i * i + node->value; // HEAP_ZERO_MEMORY : value vaut 0
        node->next = head;
        head = node;
    }
    u64 sum = 0;
    while (head) {
        Node *next = head->next;
        sum += head->value;
        HeapFree(heap, 0, head);
        head = next;
    }
    return sum;
}

// --- Objets à méthodes virtuelles (table de pointeurs dans les données) ---
typedef struct Shape Shape;
typedef struct {
    const char *name;
    double (*area)(const Shape *);
} ShapeVt;
struct Shape {
    const ShapeVt *vt;
    double a, b;
};
static double rectArea(const Shape *s) { return s->a * s->b; }
static double triArea(const Shape *s) { return s->a * s->b / 2; }
static double circleArea(const Shape *s) { return 3.141592653589793 * s->a * s->a; }
static const ShapeVt rectVt = {"rectangle", rectArea};
static const ShapeVt triVt = {"triangle", triArea};
static const ShapeVt circleVt = {"cercle", circleArea};

void start(void) {
    puts_("RomCloud Native - programme de test\n");

    puts_("fib(27) = ");
    putu(fib(27), 10);
    puts_("\nackermann(2, 3) = ");
    putu(ackermann(2, 3), 10);
    putc_('\n');

    for (int op = 0; op <= 10; op++) {
        puts_(opName(op));
        puts_("(-1234567, 89) = ");
        puti(apply(op, -1234567, 89));
        putc_('\n');
    }

    puts_("vm : ");
    i64 vm = vmRun(vmProgram);
    puts_("= ");
    puti(vm);
    putc_('\n');

    int v[24];
    for (int i = 0; i < 24; i++) v[i] = (int)(rng() % 2000) - 1000;
    Cmp cmps[3] = {cmpAsc, cmpDesc, cmpLowBits};
    for (int k = 0; k < 3; k++) {
        sortInts(v, 24, cmps[k]);
        puts_("tri ");
        putu((u64)k, 10);
        putc_(':');
        for (int i = 0; i < 24; i++) {
            putc_(' ');
            puti(v[i]);
        }
        putc_('\n');
    }

    const char *text = "Les jeux PC transposes en applications Android natives";
    puts_("crc32 = 0x");
    putu(crc32((const u8 *)text, 54), 16);
    puts_("\nfnv1a = 0x");
    putu(fnv1a(text), 16);
    puts_("\nmulhi = 0x");
    putu(mulhi(0xfedcba9876543210ULL, 0x0123456789abcdefULL), 16);
    puts_("\nsmulhi = ");
    puti(smulhi(-0x123456789abcdefLL, 0x7edcba9876543210LL));
    puts_("\nrng = ");
    for (int i = 0; i < 4; i++) {
        putu(rng(), 10);
        putc_(' ');
    }

    puts_("\nsqrt(2) = ");
    putf(sqrtNewton(2.0));
    puts_("\npi = ");
    putf(piLeibniz(200000));
    float fa[19], fb[19];
    for (int i = 0; i < 19; i++) {
        fa[i] = (float)i * 0.25f;
        fb[i] = 3.0f - (float)i * 0.125f;
    }
    puts_("\ndot = ");
    putf(dot(fa, fb, 19));
    putc_('\n');
    mandelbrot();

    puts_("liste = ");
    putu(listTest(1000), 10);
    putc_('\n');

    Shape shapes[3] = {{&rectVt, 3, 4.5}, {&triVt, 6, 7}, {&circleVt, 1.5, 0}};
    for (int i = 0; i < 3; i++) {
        puts_(shapes[i].vt->name);
        puts_(" : ");
        putf(shapes[i].vt->area(&shapes[i]));
        putc_('\n');
    }

    u8 block[300];
    memset(block, 0xa5, sizeof(block));
    memcpy(block + 100, text, 54);
    puts_("bloc = 0x");
    putu(crc32(block, sizeof(block)), 16);
    puts_("\nfin\n");
    flush();
    ExitProcess(42);
}
