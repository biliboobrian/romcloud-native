// Test jit32 : exécutable x86 32 bits sans bibliothèque C qui écrit du code machine dans un bloc de
// VirtualAlloc et l'appelle, comme un compilateur à la volée (Mono pour Unity). Le runtime ne connaît pas
// ce code d'avance : il le traduit au premier appel (runtime/rcn_jit.c, recompilateur librcn_recomp).
// Cas couverts : fonction simple ; appel direct (rel32) du code généré vers l'exécutable ; fonction écrite
// plus tard dans le même bloc ; fonction qui en appelle une autre du bloc ; saut terminal vers l'exécutable ;
// trampoline à la manière de Mono (argument empilé, puis « ret » vers l'adresse écrite à sa place) ;
// trampoline qui rend la main directement à l'appelant ; restauration de contexte vers un bloc de reprise
// (esp remis plus haut, puis « ret », comme Mono vers un catch) ; lecture à l'adresse 0 rattrapée par un
// gestionnaire vectoriel qui fait reprendre ailleurs (NullReferenceException de Mono).
#include <windows.h>

static HANDLE out;

static void put(const char *s) {
    DWORD written;
    size_t n = 0;
    while (s[n]) n++;
    if (n) WriteFile(out, s, (DWORD)n, &written, NULL);
}
static void put_dec(int v) {
    char text[13];
    int i = 12, neg = v < 0;
    unsigned u = neg ? 0u - (unsigned)v : (unsigned)v;
    text[i] = 0;
    do {
        text[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (neg) text[--i] = '-';
    put(text + i);
}
static void line(const char *label, int v) {
    put(label);
    put(" = ");
    put_dec(v);
    put("\n");
}

// Fonctions de l'exécutable appelées par le code généré.
__attribute__((noinline, cdecl)) int twice(int x) { return 2 * x; }
__attribute__((noinline, cdecl)) int plus_one(int x) { return x + 1; }

static unsigned char *emit_rel32(unsigned char *p, unsigned char op, const void *target) {
    *p++ = op;
    int rel = (int)((const unsigned char *)target - (p + 4));
    for (int i = 0; i < 4; i++) *p++ = (unsigned char)(rel >> (8 * i));
    return p;
}

// Contexte de la reprise : esp à rétablir, adresse du bloc.
static unsigned resume_ctx[2];

// Gestionnaire vectoriel : violation d'accès → reprise à recover_at (CONTEXT x86 : Eip à 0xB8).
static unsigned recover_at;
static LONG CALLBACK on_fault(EXCEPTION_POINTERS *e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !recover_at) return EXCEPTION_CONTINUE_SEARCH;
    e->ContextRecord->Eip = recover_at;
    return EXCEPTION_CONTINUE_EXECUTION;
}

typedef int(__cdecl *Fn2)(int, int);
typedef int(__cdecl *Fn1)(int);
typedef int(__cdecl *Fn0)(void);

void start(void) {
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    unsigned char *code = VirtualAlloc(NULL, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!code) {
        put("VirtualAlloc echec\n");
        ExitProcess(1);
    }
    // add(a, b) : mov eax, [esp+4] ; add eax, [esp+8] ; ret
    static const unsigned char add[] = {0x8B, 0x44, 0x24, 0x04, 0x03, 0x44, 0x24, 0x08, 0xC3};
    for (unsigned i = 0; i < sizeof add; i++) code[i] = add[i];
    line("add(20, 22)", ((Fn2)(void *)code)(20, 22));

    // call_twice() : push 21 ; call twice ; add esp, 4 ; ret
    unsigned char *f = code + 0x100, *p = f;
    *p++ = 0x6A, *p++ = 21;
    p = emit_rel32(p, 0xE8, (void *)twice);
    *p++ = 0x83, *p++ = 0xC4, *p++ = 0x04, *p++ = 0xC3;
    line("call_twice()", ((Fn0)(void *)f)());

    // Écrite après les premières traductions, dans le même bloc : chain(x) = plus_one(add(x, x)), la
    // seconde par un saut terminal (mov eax,[esp+4] ; push eax ; push eax ; call add ; add esp,8 ;
    // mov [esp+4], eax ; jmp plus_one).
    unsigned char *g = code + 0x200;
    p = g;
    *p++ = 0x8B, *p++ = 0x44, *p++ = 0x24, *p++ = 0x04;
    *p++ = 0x50, *p++ = 0x50;
    p = emit_rel32(p, 0xE8, code);
    *p++ = 0x83, *p++ = 0xC4, *p++ = 0x08;
    *p++ = 0x89, *p++ = 0x44, *p++ = 0x24, *p++ = 0x04;
    p = emit_rel32(p, 0xE9, (void *)plus_one);
    line("chain(10)", ((Fn1)(void *)g)(10));
    line("chain(-3)", ((Fn1)(void *)g)(-3));

    // Trampoline : push 0x1234 ; jmp suite — suite : mov dword [esp], plus_one ; ret (la valeur empilée,
    // remplacée par l'adresse de la fonction visée, sert d'adresse de « retour »).
    unsigned char *t = code + 0x300, *u = code + 0x320;
    p = t;
    *p++ = 0x68, *p++ = 0x34, *p++ = 0x12, *p++ = 0, *p++ = 0;
    p = emit_rel32(p, 0xE9, u);
    p = u;
    *p++ = 0xC7, *p++ = 0x04, *p++ = 0x24;
    unsigned target = (unsigned)(unsigned long)(void *)plus_one;
    for (int i = 0; i < 4; i++) *p++ = (unsigned char)(target >> (8 * i));
    *p++ = 0xC3;
    line("trampoline(41)", ((Fn1)(void *)t)(41));

    // Trampoline qui revient directement à l'appelant (initialisation de classe de Mono) : push 0x5678 ;
    // jmp suite — suite : add esp, 4 ; mov eax, 7 ; ret.
    unsigned char *v = code + 0x340, *w = code + 0x360;
    p = v;
    *p++ = 0x68, *p++ = 0x78, *p++ = 0x56, *p++ = 0, *p++ = 0;
    p = emit_rel32(p, 0xE9, w);
    p = w;
    *p++ = 0x83, *p++ = 0xC4, *p++ = 0x04;
    *p++ = 0xB8, *p++ = 7, *p++ = 0, *p++ = 0, *p++ = 0;
    *p++ = 0xC3;
    line("retour direct", ((Fn0)(void *)v)());

    // Reprise : c() = push ebp ; mov ebp, esp ; mov [ctx], esp ; push ctx ; call r ; mov eax, 1 ; leave ;
    // ret — bloc de reprise : mov eax, 99 ; leave ; ret. r : mov eax, [esp+4] ; mov esp, [eax] ;
    // push dword [eax+4] ; ret.
    unsigned char *c = code + 0x380, *r = code + 0x3C0, *x;
    unsigned ctx_at = (unsigned)(unsigned long)(void *)resume_ctx;
    p = c;
    *p++ = 0x55, *p++ = 0x8B, *p++ = 0xEC;
    *p++ = 0x89, *p++ = 0x25;
    for (int i = 0; i < 4; i++) *p++ = (unsigned char)(ctx_at >> (8 * i));
    *p++ = 0x68;
    for (int i = 0; i < 4; i++) *p++ = (unsigned char)(ctx_at >> (8 * i));
    p = emit_rel32(p, 0xE8, r);
    *p++ = 0xB8, *p++ = 1, *p++ = 0, *p++ = 0, *p++ = 0, *p++ = 0xC9, *p++ = 0xC3;
    x = p;
    *p++ = 0xB8, *p++ = 99, *p++ = 0, *p++ = 0, *p++ = 0, *p++ = 0xC9, *p++ = 0xC3;
    resume_ctx[1] = (unsigned)(unsigned long)(void *)x;
    p = r;
    *p++ = 0x8B, *p++ = 0x44, *p++ = 0x24, *p++ = 0x04;
    *p++ = 0x8B, *p++ = 0x20;
    *p++ = 0xFF, *p++ = 0x70, *p++ = 0x04;
    *p++ = 0xC3;
    line("reprise", ((Fn0)(void *)c)());

    // Faute : xor eax, eax ; mov eax, [eax] ; ret — secours : mov eax, 77 ; ret.
    unsigned char *n = code + 0x400, *s = code + 0x410;
    p = n;
    *p++ = 0x31, *p++ = 0xC0, *p++ = 0x8B, *p++ = 0x00, *p++ = 0xC3;
    p = s;
    *p++ = 0xB8, *p++ = 77, *p++ = 0, *p++ = 0, *p++ = 0, *p++ = 0xC3;
    recover_at = (unsigned)(unsigned long)(void *)s;
    AddVectoredExceptionHandler(1, on_fault);
    line("faute rattrapee", ((Fn0)(void *)n)());
    ExitProcess(0);
}
