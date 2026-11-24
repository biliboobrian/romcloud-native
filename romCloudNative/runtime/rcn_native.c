// RomCloud Native : chargement du code machine traduit (fichier .rcnx de rcn-recomp, voir
// recompiler/src/native.rs, write_rcnx) puis exécution du jeu comme pour le code traduit en C.
//
// Format 2 : le code est définitif (références relatives, résolues par rcn-recomp) et suivi en mémoire
// d'une table des symboles (adresses des fonctions du runtime, rcn_helpers dans rcn_ops.c, et de la case
// « @base »), la seule chose que le chargeur remplit. Le code est projeté directement depuis le fichier
// quand le système le permet (pages partagées avec le cache des fichiers, que le système peut reprendre),
// sinon lu dans de la mémoire exécutable.
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <pthread.h>

#include "rcn_internal.h"

#if defined(_WIN32)
__declspec(dllimport) void *__stdcall VirtualAlloc(void *address, size_t size, unsigned long type, unsigned long protect);
#define HOST_ARCH (sizeof(void *) == 8 ? 1 : 0)
#else
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__aarch64__)
#define HOST_ARCH 2
#elif defined(__x86_64__)
#define HOST_ARCH 1
#else
#define HOST_ARCH 0
#endif
#endif

enum { RELOC_ABS8 = 1, NS_FUNC = 0, NS_HELPER = 1, CODE_ALIGN = 64 << 10, RCNX_VERSION = 2 };

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

// Code chargé de chaque module (exécutable, DLL).
static struct {
    const RcnModule *module;
    u8 *code;
    u32 size;
} crash_regions[RCN_MAX_LOADED];
static u32 crash_region_count;
_Thread_local RcnCtx *rcn_current_ctx;

const RcnModule *rcn_code_module(u64 pc) {
    for (u32 k = 0; k < crash_region_count; k++) {
        u64 code = (u64)(uintptr_t)crash_regions[k].code;
        if (pc >= code && pc < code + crash_regions[k].size) return crash_regions[k].module;
    }
    return NULL;
}

// Rapport écrit sur la sortie d'erreur et, sous Android, dans le journal du système (logcat).
static char report_text[4096];
static size_t report_len;
static void report_add(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(report_text + report_len, sizeof report_text - report_len, fmt, args);
    va_end(args);
    if (n > 0) report_len = report_len + (size_t)n < sizeof report_text ? report_len + (size_t)n : sizeof report_text - 1;
}
static void report_flush(void) {
    fputs(report_text, stderr);
    fflush(stderr);
#if defined(__ANDROID__)
    __android_log_write(6, "rcn", report_text); // ANDROID_LOG_ERROR
#endif
    report_len = 0;
    report_text[0] = 0;
}
// Fonction du jeu (module, RVA de son début) dont le code traduit contient `pc` : 0 hors du code traduit.
// Sert au profileur (RCN_PROFILE) et ressemble à la recherche du rapport de plantage.
int rcn_code_locate(u64 pc, const char **module, u32 *rva) {
    for (u32 k = 0; k < crash_region_count; k++) {
        const RcnModule *m = crash_regions[k].module;
        u64 code = (u64)(uintptr_t)crash_regions[k].code;
        if (pc < code || pc >= code + crash_regions[k].size) continue;
        u32 lo = 0, hi = m->func_count;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2;
            if ((u64)(uintptr_t)m->funcs[mid].fn <= pc) lo = mid + 1;
            else hi = mid;
        }
        if (!lo) return 0;
        *module = m->name;
        *rva = m->funcs[lo - 1].rva;
        return 1;
    }
    return 0;
}

static void crash_report(const char *what, u64 pc, u64 fault) {
    char line[512];
    int n = snprintf(line, sizeof line, "rcn : plantage (%s), adresse fautive 0x%llx", what, (unsigned long long)fault);
    int found = 0;
    for (u32 k = 0; k < crash_region_count && !found; k++) {
        const RcnModule *m = crash_regions[k].module;
        u64 code = (u64)(uintptr_t)crash_regions[k].code;
        if (pc < code || pc >= code + crash_regions[k].size) continue;
        found = 1;
        // Fonction traduite contenant pc : la dernière qui commence avant (rangées par adresse croissante).
        u32 lo = 0, hi = m->func_count;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2;
            if ((u64)(uintptr_t)m->funcs[mid].fn <= pc) lo = mid + 1;
            else hi = mid;
        }
        if (lo > 0) {
            n += snprintf(line + n, sizeof line - (size_t)n, " dans %s:sub_%08X+0x%llx", m->name, m->funcs[lo - 1].rva,
                          (unsigned long long)(pc - (u64)(uintptr_t)m->funcs[lo - 1].fn));
        }
    }
    if (!found) {
        n += snprintf(line + n, sizeof line - (size_t)n, " hors du code traduit (pc 0x%llx)", (unsigned long long)pc);
#if !defined(_WIN32)
        // Bibliothèque de l'hôte (runtime, DXVK, pilote, libc…) et symbole le plus proche.
        Dl_info info;
        if (dladdr((void *)(uintptr_t)pc, &info) && info.dli_fname) {
            const char *base = strrchr(info.dli_fname, '/');
            n += snprintf(line + n, sizeof line - (size_t)n, " %s+0x%llx", base ? base + 1 : info.dli_fname,
                          (unsigned long long)(pc - (u64)(uintptr_t)info.dli_fbase));
            if (info.dli_sname)
                n += snprintf(line + n, sizeof line - (size_t)n, " (%s+0x%llx)", info.dli_sname,
                              (unsigned long long)(pc - (u64)(uintptr_t)info.dli_saddr));
        }
#endif
    }
    report_add("%s\n", line);
    RcnCtx *ctx = rcn_current_ctx;
    // Adresse fautive dans l'image d'un module (données statiques) ?
    RcnLoaded *in_image = ctx ? rcn_loaded_at(ctx->proc, fault) : NULL;
    // Plantage dans le runtime : la fonction Windows en cours d'appel.
    const RcnImport *imp = ctx && !found ? THREAD(ctx)->current_import : NULL;
    if (imp) report_add("  (dans %s!%s)\n", imp->dll ? imp->dll : "?", imp->name);
    if (in_image) report_add("  (adresse dans l'image de %s, +0x%llx)\n", in_image->name, (unsigned long long)(fault - (u64)(uintptr_t)in_image->image));
    if (ctx) {
        static const char *const names[16] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                              "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
        for (int i = 0; i < 16; i++) report_add("  %s=%llx%s", names[i], (unsigned long long)ctx->r[i], i % 4 == 3 ? "\n" : "");
        // Adresses de retour du jeu au sommet de sa pile (appels en cours), par module.
        // 32 bits : mots de 4 octets, sur une plus grande hauteur (les cadres sont plus serrés).
        report_add("  pile :");
        u32 step = ctx->bits32 ? 4 : 8, words = ctx->bits32 ? 128 : 16;
        for (u32 i = 0; i < words; i++) {
            u64 v = ctx->bits32 ? rd32(ctx->r[4] + step * (u64)i) : rd64(ctx->r[4] + step * (u64)i);
            RcnLoaded *l = rcn_loaded_at(ctx->proc, v);
            if (l) report_add(" %s+0x%llx", l->name, (unsigned long long)(v - (u64)(uintptr_t)l->image));
        }
        report_add("\n");
        // Chaîne des cadres (32 bits : ebp -> [ebp], retour à [ebp+4]) : les appels en cours, du plus
        // récent au plus ancien. Les cadres restent dans la pile du jeu (autour de esp), donc lisibles ici.
        if (ctx->bits32) {
            u64 esp = (u32)ctx->r[4], ebp = (u32)ctx->r[5], seen = 0;
            report_add("  cadres :");
            for (int i = 0; i < 16 && ebp >= esp && ebp - esp < 0x40000; i++) {
                u64 next = rd32(ebp), ret = rd32(ebp + 4);
                RcnLoaded *l = rcn_loaded_at(ctx->proc, ret);
                if (!l) break;
                report_add(" %s+0x%llx", l->name, (unsigned long long)(ret - (u64)(uintptr_t)l->image));
                seen++;
                if (!next || next <= ebp) break;
                ebp = next;
            }
            report_add("%s\n", seen ? "" : " (aucun)");
            // Cadre courant en clair (esp -> ebp) : les registres que la fonction a empilés (ebx, esi, edi)
            // s'y trouvent, et montrent si un appelant les avait déjà perdus.
            u64 frame = (u32)ctx->r[5] - (u32)ctx->r[4];
            if ((u32)ctx->r[5] > (u32)ctx->r[4] && frame <= 0x10000) {
                for (u64 at = (u32)ctx->r[4]; at < (u32)ctx->r[5] && at - (u32)ctx->r[4] < 0x200; at += 32) {
                    report_add("  %08llx :", (unsigned long long)at);
                    for (u64 k = at; k < at + 32 && k < (u32)ctx->r[5]; k += 4)
                        report_add(" %08x", rd32(k));
                    report_add("\n");
                }
            }
        }
        // Dernières méthodes des interfaces Steam appelées : le sous-système en ligne du jeu est bâti
        // dessus, et une table d'interface du mauvais SDK y laisse des pointeurs faux.
        if (rcn_steam_call(0)) {
            report_add("  Steam :");
            for (unsigned i = 0; i < 12; i++) {
                const RcnImport *imp = rcn_steam_call(i);
                if (!imp) break;
                report_add(" %s::%s", imp->dll, imp->name);
            }
            report_add("\n");
        }
    }
    report_flush();
    // Mise au point (RCN_RECOMP_WATCH) : dernières écritures sur la case surveillée.
    void rcn_store_hits_dump(RcnCtx *ctx);
    rcn_store_hits_dump(ctx);
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
static struct sigaction old_segv, old_bus, old_ill;
static void crash_signal(int sig, siginfo_t *info, void *uc) {
    u64 pc = 0;
#if defined(__aarch64__)
    pc = ((ucontext_t *)uc)->uc_mcontext.pc;
#elif defined(__x86_64__)
    pc = (u64)((ucontext_t *)uc)->uc_mcontext.gregs[REG_RIP];
#endif
    crash_report(sig == SIGSEGV ? "acces memoire" : sig == SIGBUS ? "acces mal aligne" : "instruction illegale", pc, (u64)(uintptr_t)info->si_addr);
    // Gestionnaire précédent (Android : debuggerd, qui écrit la pile de l'hôte dans le journal) : la faute
    // se reproduit au retour de celui-ci.
    struct sigaction *old = sig == SIGSEGV ? &old_segv : sig == SIGBUS ? &old_bus : &old_ill;
    sigaction(sig, old, NULL);
}
static void install_crash_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crash_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, &old_segv);
    sigaction(SIGBUS, &sa, &old_bus);
    sigaction(SIGILL, &sa, &old_ill);
}
#endif

static void *find_helper(const char *name) {
    for (size_t i = 0; i < rcn_helper_count; i++) {
        if (strcmp(rcn_helpers[i].name, name) == 0) return rcn_helpers[i].fn;
    }
    return NULL;
}

// Lit `size` octets à la position `at` du fichier (0 si incomplet).
static int read_at(FILE *f, u64 at, void *out, size_t size) {
#if defined(_WIN32)
    if (_fseeki64(f, (long long)at, SEEK_SET) != 0) return 0;
#else
    if (fseeko(f, (off_t)at, SEEK_SET) != 0) return 0;
#endif
    return fread(out, 1, size, f) == size;
}

// Mémoire du module : code (projeté depuis le fichier ou lu) puis table, à code_span du début.
// Renvoie le début, ou NULL ; *mapped : code projeté depuis le fichier.
static u8 *map_code(FILE *f, const char *path, u64 code_at, size_t code_size, size_t code_span, size_t table_size, int *mapped) {
    *mapped = 0;
    size_t total = code_span + (table_size ? (table_size + 0xFFFF) & ~(size_t)0xFFFF : 0);
#if defined(_WIN32)
    (void)path;
    u8 *base = VirtualAlloc(NULL, total, 0x3000, 0x40); // MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE
    if (!base) return NULL;
    if (code_size && !read_at(f, code_at, base, code_size)) return NULL;
    return base;
#else
    u8 *base = mmap(NULL, total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return NULL;
    // Projection directe : refusée aux applications récentes d'Android pour leurs propres fichiers
    // (exécution depuis leur dossier de données) ; le code est alors lu.
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0 && code_size && !getenv("RCN_NO_MAP") &&
        mmap(base, code_size, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_FIXED, fd, (off_t)code_at) != MAP_FAILED) {
        *mapped = 1;
    } else if (code_size) {
        if (mmap(base, code_span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED ||
            !read_at(f, code_at, base, code_size)) {
            if (fd >= 0) close(fd);
            return NULL;
        }
        __builtin___clear_cache((char *)base, (char *)base + code_size);
        if (mprotect(base, code_span, PROT_READ | PROT_EXEC) != 0) {
            if (fd >= 0) close(fd);
            return NULL;
        }
    }
    if (fd >= 0) close(fd);
    if (table_size && mmap(base + code_span, total - code_span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
        return NULL;
    return base;
#endif
}

static int install_once;

// Lit un fichier .rcnx (format 2) : en-tête et tables lus, code projeté ou lu, table des symboles remplie.
static const RcnModule *rcn_load_rcnx_file(const char *code_path, char *error, size_t error_size);
// Code déjà chargé, par fichier : un même .rcnx demandé plusieurs fois (DLL dont le chargement échoue puis
// est retenté, Clair Obscur : OVRLipSync.dll, 102 Mo à chaque essai) rend le même module, jamais relu.
const RcnModule *rcn_load_rcnx(const char *code_path, char *error, size_t error_size) {
    static struct {
        char *path;
        const RcnModule *module;
    } loaded[256];
    static int count;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < count; i++)
        if (!strcmp(loaded[i].path, code_path)) {
            const RcnModule *m = loaded[i].module;
            pthread_mutex_unlock(&lock);
            return m;
        }
    const RcnModule *m = rcn_load_rcnx_file(code_path, error, error_size);
    if (m && count < 256) {
        loaded[count].path = strdup(code_path);
        if (loaded[count].path) loaded[count++].module = m;
    }
    pthread_mutex_unlock(&lock);
    return m;
}

static const RcnModule *rcn_load_rcnx_file(const char *code_path, char *error, size_t error_size) {
    FILE *f = fopen(code_path, "rb");
    u8 *head = NULL;
    RcnModule *m = NULL;
    if (!f) {
        snprintf(error, error_size, "code traduit illisible : %s", code_path);
        return NULL;
    }
    // En-tête et tables (avant le code, aligné) : lus par morceaux grandissants jusqu'à les avoir en entier.
    Reader r = {0};
    u32 version = 0, arch = 0, bits = 0, func_count = 0, helper_count = 0, message_count = 0, code_size = 0, reloc_count = 0, table_count = 0;
    size_t want = 1 << 16;
    for (;;) {
        u8 *grown = realloc(head, want);
        if (!grown) goto memory;
        head = grown;
        size_t got = 0;
        if (read_at(f, 0, head, 8)) {
#if defined(_WIN32)
            _fseeki64(f, 0, SEEK_SET);
#else
            fseeko(f, 0, SEEK_SET);
#endif
            got = fread(head, 1, want, f);
        }
        r = (Reader){head, got, 0, 0};
        if (got < 8 || memcmp(head, "RCNX", 4) != 0) {
            snprintf(error, error_size, "ce n'est pas du code traduit par rcn-recomp : %s", code_path);
            goto fail;
        }
        r.pos = 4;
        version = next32(&r), arch = next32(&r), bits = next32(&r);
        if (version != RCNX_VERSION) {
            snprintf(error, error_size, "code traduit par une autre version de rcn-recomp");
            goto fail;
        }
        r.pos += 8 + 4 + 4 + 4;
        func_count = next32(&r), helper_count = next32(&r), message_count = next32(&r);
        code_size = next32(&r), reloc_count = next32(&r), table_count = next32(&r);
        size_t need = r.pos + 4 + 1024 + (size_t)func_count * 8 + (size_t)reloc_count * 20;
        for (u32 i = 0; !r.bad && i < helper_count + message_count + 1; i++) {
            u32 n = next32(&r);
            r.pos += n;
            need += 4 + n;
        }
        if (!r.bad && r.pos + (size_t)func_count * 8 + (size_t)reloc_count * 20 <= got) break;
        if (got < want) goto truncated; // fichier plus court que ses tables
        want = need > want * 2 ? need : want * 2;
    }
    if (arch != HOST_ARCH) {
        snprintf(error, error_size, "code traduit pour un autre processeur");
        goto fail;
    }
    m = calloc(1, sizeof *m);
    if (!m) goto memory;
    r.pos = 16;
    m->abi = RCN_ABI_VERSION;
    m->native = 1;
    m->bits = bits;
    m->image_base = next64(&r);
    m->image_size = next32(&r);
    m->timestamp = next32(&r);
    m->entry_rva = next32(&r);
    r.pos += 6 * 4;
    m->name = next_text(&r);
    void **helpers = calloc(helper_count + 1, sizeof *helpers);
    char **messages = calloc(message_count + 1, sizeof *messages);
    RcnFuncEntry *funcs = calloc(func_count + 1, sizeof *funcs);
    if (!helpers || !messages || !funcs) goto memory;
    for (u32 i = 0; i < helper_count; i++) {
        char *name = next_text(&r);
        if (r.bad) goto truncated;
        // « @base » : la case du module où le chargeur écrit son adresse.
        helpers[i] = strcmp(name, "@base") == 0 ? (void *)&m->base_slot : find_helper(name);
        if (!helpers[i]) {
            snprintf(error, error_size, "fonction du runtime absente : %s (runtime trop ancien pour ce code)", name);
            free(name);
            goto fail;
        }
        free(name);
    }
    for (u32 i = 0; i < message_count; i++) messages[i] = next_text(&r);
    size_t funcs_at = r.pos, relocs_at = funcs_at + (size_t)func_count * 8;
    u64 code_at = ((u64)relocs_at + (u64)reloc_count * 20 + CODE_ALIGN - 1) & ~(u64)(CODE_ALIGN - 1);
    size_t code_span = ((size_t)code_size + CODE_ALIGN - 1) & ~(size_t)(CODE_ALIGN - 1);
    int mapped = 0;
    u8 *code = map_code(f, code_path, code_at, code_size, code_span, (size_t)table_count * 8, &mapped);
    if (!code) goto memory;
    u8 *table = code + code_span;
    for (u32 i = 0; i < func_count; i++) {
        funcs[i].rva = next32(&r);
        u32 offset = next32(&r);
        if (offset >= code_size) r.bad = 1;
        funcs[i].fn = (RcnFn)(void *)(code + offset);
    }
    // Table des symboles : décalage dans la table, type, espace de noms, indice, ajout.
    const char *why = NULL;
    for (u32 i = 0; i < reloc_count && !why; i++) {
        u32 at = next32(&r), kind_ns = next32(&r), index = next32(&r);
        i64 addend = (i64)next64(&r);
        u8 kind = (u8)kind_ns, ns = (u8)(kind_ns >> 8);
        u64 target = 0;
        if (kind != RELOC_ABS8 || (u64)at + 8 > (u64)table_count * 8) goto truncated;
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
            if (lo == func_count || funcs[lo].rva != index) why = "code traduit incoherent (fonction manquante)";
            else target = (u64)(uintptr_t)funcs[lo].fn;
        } else {
            goto truncated;
        }
        u64 value = target + (u64)addend;
        memcpy(table + at, &value, 8);
    }
    if (r.bad) goto truncated;
    if (why) {
        snprintf(error, error_size, "%s", why);
        goto fail;
    }
    rcn_log("rcn : %s : %u Mo de code %s", m->name, code_size >> 20, mapped ? "projete depuis le fichier" : "lu en memoire");
    m->funcs = funcs;
    m->func_count = func_count;
    m->messages = (const char *const *)messages;
    m->message_count = message_count;
    free(helpers);
    free(head);
    fclose(f);
    if (crash_region_count < RCN_MAX_LOADED) {
        crash_regions[crash_region_count].module = m;
        crash_regions[crash_region_count].code = code;
        crash_regions[crash_region_count].size = code_size;
        crash_region_count++;
    }
    if (!install_once) {
        install_once = 1;
        install_crash_handler();
    }
    // Le code et la description restent en place jusqu'à la fin du processus.
    return m;

memory:
    snprintf(error, error_size, "memoire insuffisante pour le code traduit");
    goto fail;
truncated:
    snprintf(error, error_size, "code traduit tronque ou abime : %s", code_path);
fail:
    free(head);
    fclose(f);
    return NULL;
}

int rcn_run_native(const char *code_path, const char *exe_path, const RcnHost *host, RcnResult *result) {
    memset(result, 0, sizeof *result);
    const RcnModule *m = rcn_load_rcnx(code_path, result->error, sizeof result->error);
    if (!m) return result->status = RCN_ERR_LOAD;
    rcn_set_code_dir(code_path);
    return rcn_run_module(m, exe_path, host, result);
}
