// RomCloud Native : chargement du code machine traduit (fichier .rcnx de rcn-recomp, voir
// recompiler/src/native.rs, write_rcnx) puis exécution du jeu comme pour le code traduit en C.
//
// Le code est copié dans de la mémoire exécutable ; les endroits notés dans le fichier reçoivent les
// adresses des autres fonctions traduites et des fonctions du runtime (rcn_helpers, rcn_ops.c).
#include <stdio.h>
#include <stdlib.h>

#include "rcn_internal.h"

#if defined(_WIN32)
__declspec(dllimport) void *__stdcall VirtualAlloc(void *address, size_t size, unsigned long type, unsigned long protect);
#define HOST_ARCH (sizeof(void *) == 8 ? 1 : 0)
#else
#include <sys/mman.h>
#if defined(__aarch64__)
#define HOST_ARCH 2
#elif defined(__x86_64__)
#define HOST_ARCH 1
#else
#define HOST_ARCH 0
#endif
#endif

enum { RELOC_ABS8 = 1, RELOC_PCREL4 = 2, RELOC_ARM64_CALL = 3, NS_FUNC = 0, NS_HELPER = 1 };

typedef struct {
    const u8 *data;
    size_t size, pos;
    int bad;
} Reader;

static u32 next32(Reader *r) {
    if (r->pos + 4 > r->size) {
        r->bad = 1;
        return 0;
    }
    u32 v;
    memcpy(&v, r->data + r->pos, 4);
    r->pos += 4;
    return v;
}
static u64 next64(Reader *r) {
    u64 lo = next32(r), hi = next32(r);
    return lo | hi << 32;
}
// Texte (longueur sur 32 bits puis octets), copié avec un zéro final. NULL si le fichier est tronqué.
static char *next_text(Reader *r) {
    u32 len = next32(r);
    if (r->bad || len > r->size - r->pos) {
        r->bad = 1;
        return NULL;
    }
    char *s = malloc(len + 1);
    if (!s) {
        r->bad = 1;
        return NULL;
    }
    memcpy(s, r->data + r->pos, len);
    s[len] = 0;
    r->pos += len;
    return s;
}

// --- Rapport de plantage : fonction traduite en cours, adresse fautive, registres du jeu ---

static const RcnModule *crash_module;
static u8 *crash_code;
static u32 crash_code_size;
_Thread_local RcnCtx *rcn_current_ctx;

static void crash_report(const char *what, u64 pc, u64 fault) {
    char line[512];
    int n = snprintf(line, sizeof line, "rcn : plantage (%s), adresse fautive 0x%llx", what, (unsigned long long)fault);
    const RcnModule *m = crash_module;
    if (m && pc >= (u64)(uintptr_t)crash_code && pc < (u64)(uintptr_t)crash_code + crash_code_size) {
        // Fonction traduite contenant pc : la dernière qui commence avant (rangées par adresse croissante).
        u32 lo = 0, hi = m->func_count;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2;
            if ((u64)(uintptr_t)m->funcs[mid].fn <= pc) lo = mid + 1;
            else hi = mid;
        }
        if (lo > 0) {
            n += snprintf(line + n, sizeof line - (size_t)n, " dans sub_%08X+0x%llx", m->funcs[lo - 1].rva,
                          (unsigned long long)(pc - (u64)(uintptr_t)m->funcs[lo - 1].fn));
        }
    } else {
        n += snprintf(line + n, sizeof line - (size_t)n, " hors du code traduit (pc 0x%llx)", (unsigned long long)pc);
    }
    fprintf(stderr, "%s\n", line);
    RcnCtx *ctx = rcn_current_ctx;
    if (ctx) {
        static const char *const names[16] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                              "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
        for (int i = 0; i < 16; i++) fprintf(stderr, "  %s=%llx%s", names[i], (unsigned long long)ctx->r[i], i % 4 == 3 ? "\n" : "");
        // Adresses de retour du jeu au sommet de sa pile (appels en cours).
        fprintf(stderr, "  pile :");
        for (int i = 0; i < 12; i++) {
            u64 v = rd64(ctx->r[4] + 8 * (u64)i);
            if (v - ctx->base < ctx->image_size) fprintf(stderr, " +0x%llx", (unsigned long long)(v - ctx->base));
        }
        fprintf(stderr, "\n");
    }
    fflush(stderr);
}

#if defined(_WIN32)
typedef struct {
    u32 code, flags;
    void *record, *address;
    u32 count;
    u64 info[15];
} WinRecord;
typedef struct {
    WinRecord *record;
    u8 *context;
} WinPointers;
__declspec(dllimport) void *__stdcall AddVectoredExceptionHandler(unsigned long first, long(__stdcall *handler)(WinPointers *));
static long __stdcall crash_handler(WinPointers *p) {
    u32 code = p->record->code;
    if (code != 0xC0000005u && code != 0xC000001Du && code != 0xC0000094u && code != 0xC00000FDu) return 0;
    u64 pc = *(u64 *)(p->context + 0xF8); // CONTEXT.Rip
    crash_report(code == 0xC0000005u ? "acces memoire" : "exception du processeur", pc, p->record->count > 1 ? p->record->info[1] : 0);
    return 0;
}
static void install_crash_handler(void) { AddVectoredExceptionHandler(1, crash_handler); }
#else
#include <signal.h>
#include <ucontext.h>
static void crash_signal(int sig, siginfo_t *info, void *uc) {
    u64 pc = 0;
#if defined(__aarch64__)
    pc = ((ucontext_t *)uc)->uc_mcontext.pc;
#elif defined(__x86_64__)
    pc = (u64)((ucontext_t *)uc)->uc_mcontext.gregs[REG_RIP];
#endif
    crash_report(sig == SIGSEGV ? "acces memoire" : sig == SIGBUS ? "acces mal aligne" : "instruction illegale", pc, (u64)(uintptr_t)info->si_addr);
    signal(sig, SIG_DFL);
    raise(sig);
}
static void install_crash_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crash_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
}
#endif

static void *find_helper(const char *name) {
    for (size_t i = 0; i < rcn_helper_count; i++) {
        if (strcmp(rcn_helpers[i].name, name) == 0) return rcn_helpers[i].fn;
    }
    return NULL;
}

// Mémoire exécutable : écrite d'abord, puis rendue exécutable (rcn_code_protect).
static u8 *code_alloc(size_t size) {
#if defined(_WIN32)
    return VirtualAlloc(NULL, size, 0x3000, 0x40); // MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE
#else
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
#endif
}
static int code_protect(u8 *code, size_t size) {
#if defined(_WIN32)
    (void)code;
    (void)size;
    return 0;
#else
    __builtin___clear_cache((char *)code, (char *)code + size);
    return mprotect(code, size, PROT_READ | PROT_EXEC);
#endif
}

static int fail(RcnResult *result, const char *fmt, const char *detail) {
    memset(result, 0, sizeof *result);
    snprintf(result->error, sizeof result->error, fmt, detail ? detail : "");
    return result->status = RCN_ERR_LOAD;
}

static u8 *read_all(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    u8 *data = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long len = ftell(f);
        if (len > 0 && fseek(f, 0, SEEK_SET) == 0 && (data = malloc((size_t)len))) {
            if (fread(data, 1, (size_t)len, f) != (size_t)len) {
                free(data);
                data = NULL;
            }
            *size = (size_t)len;
        }
    }
    fclose(f);
    return data;
}

int rcn_run_native(const char *code_path, const char *exe_path, const RcnHost *host, RcnResult *result) {
    size_t size = 0;
    u8 *file = read_all(code_path, &size);
    if (!file) return fail(result, "code traduit illisible : %s", code_path);
    Reader r = {file, size, 0, 0};
    int status = RCN_ERR_LOAD;
    if (size < 8 || memcmp(file, "RCNX", 4) != 0) {
        status = fail(result, "ce n'est pas du code traduit par rcn-recomp : %s", code_path);
        goto done;
    }
    r.pos = 4;
    u32 version = next32(&r), arch = next32(&r), bits = next32(&r);
    if (version != 1) {
        status = fail(result, "code traduit par une autre version de rcn-recomp%s", "");
        goto done;
    }
    if (arch != HOST_ARCH) {
        status = fail(result, "code traduit pour un autre processeur%s", "");
        goto done;
    }
    RcnModule *m = calloc(1, sizeof *m);
    if (!m) goto memory;
    m->abi = RCN_ABI_VERSION;
    m->native = 1;
    m->bits = bits;
    m->image_base = next64(&r);
    m->image_size = next32(&r);
    m->timestamp = next32(&r);
    m->entry_rva = next32(&r);
    u32 func_count = next32(&r), helper_count = next32(&r), message_count = next32(&r);
    u32 code_size = next32(&r), reloc_count = next32(&r);
    m->name = next_text(&r);
    if (r.bad) goto truncated;
    void **helpers = calloc(helper_count + 1, sizeof *helpers);
    char **messages = calloc(message_count + 1, sizeof *messages);
    RcnFuncEntry *funcs = calloc(func_count + 1, sizeof *funcs);
    u8 *code = code_size ? code_alloc(code_size) : NULL;
    if (!helpers || !messages || !funcs || (code_size && !code)) goto memory;
    for (u32 i = 0; i < helper_count; i++) {
        char *name = next_text(&r);
        if (r.bad) goto truncated;
        helpers[i] = find_helper(name);
        if (!helpers[i]) {
            status = fail(result, "fonction du runtime absente : %s (runtime trop ancien pour ce code)", name);
            free(name);
            goto done;
        }
        free(name);
    }
    for (u32 i = 0; i < message_count; i++) messages[i] = next_text(&r);
    u32 *offsets = calloc(func_count + 1, sizeof *offsets);
    if (!offsets) goto memory;
    for (u32 i = 0; i < func_count; i++) {
        funcs[i].rva = next32(&r);
        offsets[i] = next32(&r);
        if (offsets[i] >= code_size) r.bad = 1;
    }
    size_t relocs_at = r.pos;
    r.pos += (size_t)reloc_count * 20;
    if (r.bad || r.pos > size || code_size > size - r.pos) goto truncated;
    memcpy(code, file + r.pos, code_size);
    for (u32 i = 0; i < func_count; i++) funcs[i].fn = (RcnFn)(void *)(code + offsets[i]);

    // Relocalisations : décalage, type, espace de noms, indice, ajout.
    Reader rr = {file, size, relocs_at, 0};
    for (u32 i = 0; i < reloc_count; i++) {
        u32 at = next32(&rr);
        u32 kind_ns = next32(&rr);
        u32 index = next32(&rr);
        i64 addend = (i64)next64(&rr);
        u8 kind = (u8)kind_ns, ns = (u8)(kind_ns >> 8);
        u64 target;
        if (ns == NS_HELPER && index < helper_count) {
            target = (u64)(uintptr_t)helpers[index];
        } else if (ns == NS_FUNC) {
            // Fonction traduite, par son RVA (tableau trié).
            u32 lo = 0, hi = func_count;
            while (lo < hi) {
                u32 mid = lo + (hi - lo) / 2;
                if (funcs[mid].rva < index) lo = mid + 1;
                else hi = mid;
            }
            if (lo == func_count || funcs[lo].rva != index) {
                status = fail(result, "code traduit incoherent (fonction manquante)%s", "");
                goto done;
            }
            target = (u64)(uintptr_t)funcs[lo].fn;
        } else {
            goto truncated;
        }
        if (at + 8 > code_size) goto truncated;
        u8 *p = code + at;
        u64 value = target + (u64)addend;
        if (kind == RELOC_ABS8) {
            memcpy(p, &value, 8);
        } else if (kind == RELOC_PCREL4) {
            i64 delta = (i64)(value - (u64)(uintptr_t)p);
            if (delta != (i32)delta) {
                status = fail(result, "code traduit trop dispersé en memoire%s", "");
                goto done;
            }
            i32 d = (i32)delta;
            memcpy(p, &d, 4);
        } else if (kind == RELOC_ARM64_CALL) {
            i64 delta = (i64)(value - (u64)(uintptr_t)p);
            if ((delta & 3) || delta >= (1ll << 27) || delta < -(1ll << 27)) {
                status = fail(result, "code traduit trop grand (appel hors de portee)%s", "");
                goto done;
            }
            u32 ins;
            memcpy(&ins, p, 4);
            ins = (ins & 0xFC000000u) | ((u32)(delta >> 2) & 0x03FFFFFFu);
            memcpy(p, &ins, 4);
        } else {
            goto truncated;
        }
    }
    if (code_protect(code, code_size) != 0) {
        status = fail(result, "memoire executable refusee par le systeme%s", "");
        goto done;
    }
    m->funcs = funcs;
    m->func_count = func_count;
    m->messages = (const char *const *)messages;
    m->message_count = message_count;
    free(file);
    file = NULL;
    crash_module = m;
    crash_code = code;
    crash_code_size = code_size;
    install_crash_handler();
    // Le code et la description restent en place jusqu'à la fin du processus : des threads du jeu
    // peuvent encore tourner après le retour de rcn_run_module.
    return rcn_run_module(m, exe_path, host, result);

memory:
    status = fail(result, "memoire insuffisante pour le code traduit%s", "");
    goto done;
truncated:
    status = fail(result, "code traduit tronque ou abime : %s", code_path);
done:
    free(file);
    return status;
}
