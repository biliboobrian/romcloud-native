// RomCloud Native : chargement de l'exécutable d'origine (données, relocalisations, imports) et
// exécution du code traduit. L'image est chargée à une adresse quelconque de l'hôte : le code traduit
// adresse tout relativement à ctx->base, et les pointeurs absolus des données sont relocalisés.
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rcn_gui.h"
#include "rcn_internal.h"
#if defined(__ANDROID__)
#include <android/log.h>
#include <malloc.h>
#endif


// Pile du jeu : celle du thread principal, et celle par défaut des autres threads.
#define MAIN_STACK_SIZE ((size_t)8 << 20)
#define THREAD_STACK_SIZE ((size_t)1 << 20)
// Pile de l'hôte : chaque appel du jeu est un appel C, dont le cadre est plus gros que celui du jeu.
#define HOST_STACK_SIZE ((size_t)64 << 20)

// --- Mémoire alignée sur une page ---
#if defined(_WIN32)
#include <malloc.h>
void *rcn_page_alloc(size_t size) {
    void *p = _aligned_malloc(size, 4096);
    if (p) memset(p, 0, size);
    return p;
}
void rcn_page_free(void *p) { _aligned_free(p); }
#else
void *rcn_page_alloc(size_t size) {
    void *p = NULL;
    if (posix_memalign(&p, 4096, size) != 0) return NULL;
    memset(p, 0, size);
    return p;
}
void rcn_page_free(void *p) { free(p); }
#endif

// Message de suivi : sortie d'erreur, et journal du système sous Android (logcat, étiquette « rcn »).
void rcn_log(const char *fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    fprintf(stderr, "%s\n", line);
#if defined(__ANDROID__)
    __android_log_write(4, "rcn", line); // ANDROID_LOG_INFO
#endif
}

static void set_error(RcnProcess *proc, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(proc->result->error, sizeof proc->result->error, fmt, args);
    va_end(args);
}

// --- Appels ---

static RcnFn find_func(const RcnModule *m, u32 rva) {
    u32 lo = 0, hi = m->func_count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (m->funcs[mid].rva < rva) lo = mid + 1;
        else hi = mid;
    }
    return lo < m->func_count && m->funcs[lo].rva == rva ? m->funcs[lo].fn : NULL;
}

// Trampoline de .NET compilé d'avance (ThunksMapping.cpp, x86-64) : « 49 BA imm64 » (mov r10, imm64) puis
// « 41 FF 62 d8 » ou « 41 FF A2 d32 » (jmp [r10 + d]), écrit dans de la mémoire réservée par VirtualAlloc.
// 1 si imité.
static int thunk_jump(RcnCtx *ctx, u64 target) {
    u8 info[48] = {0};
    if (!rcn_vm_query(target, (u64)(uintptr_t)info)) return 0;
    u32 state;
    memcpy(&state, info + 32, 4);
    if (state != 0x1000) return 0; // MEM_COMMIT
    const u8 *p = PTR(target);
    if (p[0] != 0x49 || p[1] != 0xBA || p[10] != 0x41 || p[11] != 0xFF) return 0;
    i64 disp;
    if (p[12] == 0x62) {
        disp = (i8)p[13];
    } else if (p[12] == 0xA2) {
        i32 d32;
        memcpy(&d32, p + 13, 4);
        disp = d32;
    } else {
        return 0;
    }
    u64 data;
    memcpy(&data, p + 2, 8);
    if (!rcn_vm_query(data + (u64)disp, (u64)(uintptr_t)info)) return 0;
    ctx->r[10] = data;
    u64 next = rd64(data + (u64)disp);
    if (next == target) return 0;
    rcn_call(ctx, next);
    return 1;
}

// Appels indirects (méthodes virtuelles, pointeurs de fonctions) : cache par thread adresse → fonction
// traduite, devant la recherche dichotomique dans la table des fonctions (le code traduit ne change pas).
enum { CALL_CACHE = 4096 };
struct RcnCallSlot {
    u64 target;
    RcnFn fn;
};
static struct RcnCallSlot call_cache_none[CALL_CACHE]; // jamais rempli : thread dont le cache manque
static inline u32 call_slot(u64 target) { return (u32)((target >> 2) ^ (target >> 14)) & (CALL_CACHE - 1); }

// Nom qui contient l'un des mots de la liste (séparés par des virgules).
static int trace_match(const char *list, const char *name) {
    while (*list) {
        const char *end = strchr(list, ',');
        size_t n = end ? (size_t)(end - list) : strlen(list);
        for (const char *p = name; n && *p; p++) {
            if (strncmp(p, list, n) == 0) return 1;
        }
        if (!end) break;
        list = end + 1;
    }
    return 0;
}

void rcn_call(RcnCtx *ctx, u64 target) {
    struct RcnCallSlot *call_cache = THREAD(ctx)->call_cache;
    u32 slot = call_slot(target);
    if (call_cache && call_cache[slot].target == target && call_cache[slot].fn) {
        call_cache[slot].fn(ctx);
        return;
    }
    if (!call_cache) {
        call_cache = THREAD(ctx)->call_cache = calloc(CALL_CACHE, sizeof *call_cache);
        if (!call_cache) call_cache = call_cache_none;
    }
    RcnProcess *proc = ctx->proc;
    u64 rva = target - ctx->base;
    if (rva < ctx->image_size) {
        RcnFn fn = find_func(proc->module, (u32)rva);
        if (!fn) rcn_trap(ctx, rva, "appel d'une adresse qui n'a pas ete traduite");
        if (call_cache != call_cache_none) call_cache[slot].target = target, call_cache[slot].fn = fn;
        fn(ctx);
        return;
    }
    // DLL du jeu.
    for (u32 i = 1; i < proc->loaded_count; i++) {
        RcnLoaded *l = &proc->loaded[i];
        u64 r = target - (u64)(uintptr_t)l->image;
        if (r >= l->size) continue;
        RcnFn fn = find_func(l->code, (u32)r);
        if (!fn) rcn_fail(ctx, "appel d'une adresse qui n'a pas ete traduite (%s+0x%llx)", l->name, (unsigned long long)r);
        if (call_cache != call_cache_none) call_cache[slot].target = target, call_cache[slot].fn = fn;
        fn(ctx);
        return;
    }
    const RcnImport *imp = (const RcnImport *)(uintptr_t)target;
    if ((imp >= proc->imports && imp < proc->imports + proc->import_count) ||
        (imp >= proc->thunks && imp < proc->thunks + proc->thunk_count)) {
        THREAD(ctx)->current_import = imp;
        if (proc->trace || (proc->trace_filter && trace_match(proc->trace_filter, imp->name))) {
            fprintf(stderr, "[rcn %u] %s(%llx, %llx, %llx, %llx)", THREAD_ID(ctx), imp->name, (unsigned long long)rcn_arg(ctx, 0),
                    (unsigned long long)rcn_arg(ctx, 1), (unsigned long long)rcn_arg(ctx, 2), (unsigned long long)rcn_arg(ctx, 3));
            imp->fn(ctx);
            fprintf(stderr, " = %llx\n", (unsigned long long)ctx->r[0]);
        } else {
            imp->fn(ctx);
        }
        // longjmp (code machine traduit) : registres déjà rétablis, rien à dépiler.
        if (ctx->unwind) return;
        // Adresse de retour empilée par l'appelant ; en 32 bits (stdcall), la fonction retire aussi ses arguments.
        if (ctx->bits32) ctx->r[4] = (u32)(ctx->r[4] + 4 + imp->pop);
        else ctx->r[4] += 8;
        return;
    }
    // Trampoline écrit par le jeu dans sa propre mémoire (.NET compilé d'avance : pointeurs de fonction
    // C vers des délégués), « mov r10, données ; jmp [r10 + 8] » : imité (r10 posé, saut vers la cible).
    if (!ctx->bits32 && thunk_jump(ctx, target)) return;
    {
        u8 info[48];
        if (!ctx->bits32 && rcn_vm_query(target, (u64)(uintptr_t)info)) {
            const u8 *b = PTR(target);
            rcn_log("rcn : octets en 0x%llx : %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                    (unsigned long long)target, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13],
                    b[14], b[15], b[16], b[17], b[18], b[19]);
        }
    }
    // L'adresse de retour au sommet de la pile du jeu situe l'appel fautif.
    u64 ret = rdp(ctx, ctx->r[4]);
    if (proc->trace)
        for (int i = 0; i < 16; i++) fprintf(stderr, "  r%d = %llx\n", i, (unsigned long long)ctx->r[i]);
    if (ret - ctx->base < ctx->image_size)
        rcn_fail(ctx, "appel d'une adresse inconnue : 0x%llx (depuis %s+0x%llx)", (unsigned long long)target, proc->module->name,
                 (unsigned long long)(ret - ctx->base));
    RcnLoaded *from = rcn_loaded_at(proc, ret);
    if (from)
        rcn_fail(ctx, "appel d'une adresse inconnue : 0x%llx (depuis %s+0x%llx)", (unsigned long long)target, from->name,
                 (unsigned long long)(ret - (u64)(uintptr_t)from->image));
    rcn_fail(ctx, "appel d'une adresse inconnue : 0x%llx (retour prevu en 0x%llx)", (unsigned long long)target, (unsigned long long)ret);
}

_Noreturn void rcn_trap(RcnCtx *ctx, u64 rva, const char *what) {
    rcn_fail(ctx, "%s (%s+0x%llx)", what, ctx->proc->module->name, (unsigned long long)rva);
}

static _Noreturn void thread_exit_locked(RcnCtx *ctx, u32 code);

// Fin du jeu : le premier thread qui la déclare en fixe le résultat. Verrou du processus tenu.
static void finish_locked(RcnProcess *proc, int status, u32 exit_code, const char *error) {
    if (proc->finished) return;
    proc->finished = 1;
    proc->result->status = status;
    proc->result->exit_code = exit_code;
    if (error) snprintf(proc->result->error, sizeof proc->result->error, "%s", error);
    pthread_cond_broadcast(&proc->cond);
}

_Noreturn void rcn_fail(RcnCtx *ctx, const char *fmt, ...) {
    RcnProcess *proc = ctx->proc;
    char error[sizeof proc->result->error];
    va_list args;
    va_start(args, fmt);
    vsnprintf(error, sizeof error, fmt, args);
    va_end(args);
    // Noté aussi dans le journal : dans l'application, un thread qui s'arrête ainsi sans que le processus
    // finisse (les autres threads attendent) ne laissait aucune trace.
    rcn_log("rcn : arret : %s", error);
    pthread_mutex_lock(&proc->lock);
    finish_locked(proc, RCN_ERR_TRAP, 0, error);
    thread_exit_locked(ctx, 0);
}

_Noreturn void rcn_exit(RcnCtx *ctx, u32 code) {
    RcnProcess *proc = ctx->proc;
    pthread_mutex_lock(&proc->lock);
    finish_locked(proc, RCN_OK, code, NULL);
    thread_exit_locked(ctx, code);
}

u64 rcn_call_guest(RcnCtx *ctx, u64 fn, u64 a0, u64 a1, u64 a2, u64 a3) {
    u64 rsp = ctx->r[4];
    if (ctx->bits32) {
        // 32 bits : arguments sur la pile. La fonction appelée en retire ou non selon sa convention ;
        // la pile est rétablie ici dans les deux cas.
        ctx->r[4] = (u32)(rsp - 0x40) & ~0xFu;
        PUSH32(a3);
        PUSH32(a2);
        PUSH32(a1);
        PUSH32(a0);
        PUSH32(RCN_RETURN_SENTINEL32);
        rcn_call(ctx, fn);
        ctx->r[4] = rsp;
        return (u32)ctx->r[0];
    }
    // Convention Windows x64 : pile alignée sur 16 octets avant l'appel, 32 octets réservés à l'appelé.
    ctx->r[4] = ((rsp - 0x40) & ~0xFull);
    ctx->r[1] = a0;
    ctx->r[2] = a1;
    ctx->r[8] = a2;
    ctx->r[9] = a3;
    PUSH64(RCN_RETURN_SENTINEL);
    rcn_call(ctx, fn);
    ctx->r[4] = rsp;
    return ctx->r[0];
}

u64 rcn_call_guest_n(RcnCtx *ctx, u64 fn, int n, const u64 *args) {
    if (!ctx->bits32) return rcn_call_guest(ctx, fn, n > 0 ? args[0] : 0, n > 1 ? args[1] : 0, n > 2 ? args[2] : 0, n > 3 ? args[3] : 0);
    u64 sp = ctx->r[4];
    ctx->r[4] = (u32)(sp - 0x40 - 4 * (u32)n) & ~0xFu;
    for (int i = n - 1; i >= 0; i--) PUSH32(args[i]);
    PUSH32(RCN_RETURN_SENTINEL32);
    rcn_call(ctx, fn);
    ctx->r[4] = sp;
    return (u32)ctx->r[0];
}

u64 rcn_thunk(RcnCtx *ctx, const char *dll, const char *name, RcnFn fn, u32 pop) {
    RcnProcess *proc = ctx->proc;
    RcnImport *imp = NULL;
    pthread_mutex_lock(&proc->lock);
    if (!proc->thunks) proc->thunks = rcn_galloc(ctx, RCN_MAX_THUNKS * sizeof(RcnImport), 1);
    if (proc->thunks && proc->thunk_count < RCN_MAX_THUNKS) {
        imp = &proc->thunks[proc->thunk_count++];
        imp->fn = fn;
        imp->dll = dll;
        imp->name = name;
        imp->pop = pop;
    }
    pthread_mutex_unlock(&proc->lock);
    return (u64)(uintptr_t)imp;
}

// Méthode d'un objet COM que l'hôte ne fournit pas encore.
static void missing_method(RcnCtx *ctx) {
    const RcnImport *imp = THREAD(ctx)->current_import;
    rcn_fail(ctx, "methode non fournie : %s::%s", imp->dll, imp->name);
}

u64 rcn_com_new(RcnCtx *ctx, const char *interface_name, const RcnMethod *methods, u32 count, size_t data_size) {
    // Tables virtuelles déjà construites, par interface (quelques-unes par processus).
    static struct {
        const RcnMethod *methods;
        u32 *table;
    } tables[32];
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    u32 *table = NULL;
    pthread_mutex_lock(&lock);
    int slot = 0;
    while (slot < 32 && tables[slot].methods && tables[slot].methods != methods) slot++;
    if (slot < 32 && !tables[slot].methods) {
        u32 *built = rcn_galloc(ctx, count * 4, 1);
        for (u32 i = 0; built && i < count; i++) {
            RcnFn fn = methods[i].fn ? methods[i].fn : missing_method;
            built[i] = (u32)rcn_thunk(ctx, interface_name, methods[i].name, fn, methods[i].args * 4u);
        }
        tables[slot].methods = methods;
        tables[slot].table = built;
    }
    if (slot < 32) table = tables[slot].table;
    pthread_mutex_unlock(&lock);
    u8 *object = table ? rcn_galloc(ctx, 8 + data_size, 1) : NULL;
    if (!object) return 0;
    wr32((u64)(uintptr_t)object, (u32)(uintptr_t)table);
    wr32((u64)(uintptr_t)object + 4, 1);
    return (u64)(uintptr_t)object;
}

void rcn_host_write(RcnCtx *ctx, int stream, const void *data, size_t len) {
    const RcnHost *host = ctx->proc->host;
    if (len) host->write(host->user, stream, data, len);
}

u64 rcn_now_ns(void) {
    struct timespec ts;
#if defined(_WIN32)
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

// --- Chemins de fichiers ---

#if !defined(_WIN32)
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
// Windows ne distingue pas majuscules et minuscules dans les noms de fichiers : si `name` n'existe pas
// dans `dir`, le remplace par l'entrée du dossier qui n'en diffère que par la casse.
static void match_case(const char *dir, char *name) {
    DIR *d = opendir(dir);
    if (!d) return;
    for (struct dirent *e; (e = readdir(d));) {
        if (strcasecmp(e->d_name, name) == 0) {
            memcpy(name, e->d_name, strlen(name));
            break;
        }
    }
    closedir(d);
}
#endif

// Dossier du jeu tel que le jeu le voit (GetModuleFileName) : le chemin de l'hôte, en « X:\… » (lecteur C:
// pour un hôte sans lecteur). Les moteurs situent leurs données par rapport à lui (Unreal : « ../../../Projet »),
// ce qui demande de vrais dossiers parents. NULL si le dossier du jeu n'est pas absolu. À libérer.
char *rcn_guest_dir(RcnProcess *proc) {
    const char *d = proc->game_dir;
    int drive = d[0] && d[1] == ':';
    if (!drive && d[0] != '/') return NULL;
    size_t n = strlen(d);
    char *out = malloc(n + 3);
    if (!out) return NULL;
    if (drive) {
        memcpy(out, d, n + 1);
    } else {
        out[0] = 'C', out[1] = ':';
        memcpy(out + 2, d, n + 1);
    }
    for (char *p = out; *p; p++) {
        if (*p == '/') *p = '\\';
    }
    for (size_t len = strlen(out); len > 3 && out[len - 1] == '\\';) out[--len] = 0;
    return out;
}
// Longueur du dossier de l'hôte sous lequel un chemin absolu du jeu désigne le vrai fichier : trois niveaux
// au-dessus du dossier du jeu (« Racine/Projet/Binaries/Win64 »), sans remonter à la racine. 0 : aucun.
static size_t real_root_len(const char *game_dir) {
    size_t len = strlen(game_dir);
    while (len > 1 && game_dir[len - 1] == '/') len--;
    size_t parts = 0;
    for (size_t i = 0; i < len; i++) parts += game_dir[i] == '/' && i + 1 < len && game_dir[i + 1] != '/';
    for (int up = 0; up < 3 && parts > 2; up++, parts--) {
        while (len > 0 && game_dir[len - 1] != '/') len--;
        while (len > 1 && game_dir[len - 1] == '/') len--;
    }
    return len;
}

char *rcn_host_path(RcnCtx *ctx, const char *path) {
    const char *root = ctx->proc->game_dir;
    // Préfixe des chemins longs (« \\?\C:\… », .NET) retiré.
    if ((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/') && (path[2] == '?' || path[2] == '.') &&
        (path[3] == '\\' || path[3] == '/'))
        path += 4;
    size_t root_len = strlen(root), len = strlen(path);
    char *out = malloc(root_len + len + 3);
    if (!out) return NULL;
    // Chemin absolu sous le dossier du jeu ou ses parents proches (rapporté par GetModuleFileName) : le
    // vrai chemin de l'hôte.
    size_t real = path[0] && path[1] == ':' && (path[2] == '\\' || path[2] == '/') ? real_root_len(root) : 0;
    if (real) {
        int host_drive = root[0] && root[1] == ':';
        const char *p = host_drive ? path : path + 2;
        size_t k = 0;
        for (; k < real && p[k]; k++) {
            char a = p[k] == '\\' ? '/' : p[k], b = root[k];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (k == real && (p[k] == '\\' || p[k] == '/' || !p[k])) {
            memcpy(out, root, real);
            size_t rest = strlen(p + real);
            memcpy(out + real, p + real, rest + 1);
            for (char *q = out + real; *q; q++) {
                if (*q == '\\') *q = '/';
            }
            root_len = real;
            goto cased;
        }
    }
    // Sinon : lettre de lecteur et barres initiales retirées, chemin relatif au dossier du jeu.
    if (path[0] && path[1] == ':') path += 2;
    while (*path == '\\' || *path == '/') path++;
    len = strlen(path);
    memcpy(out, root, root_len);
    out[root_len] = '/';
    memcpy(out + root_len + 1, path, len + 1);
    for (char *p = out + root_len + 1; *p; p++) {
        if (*p == '\\') *p = '/';
    }
cased:
#if !defined(_WIN32)
    // Composant par composant, tant que le chemin existe.
    for (char *name = out + root_len + (out[root_len] ? 1 : 0); *name;) {
        char *end = strchr(name, '/');
        if (end) *end = 0;
        if (access(out, F_OK) != 0) {
            name[-1] = 0;
            match_case(out, name);
            name[-1] = '/';
        }
        if (!end) break;
        *end = '/';
        name = end + 1;
    }
#endif
    // RCN_PATH_LOG=1 : chemins demandés par le jeu.
    static int log_paths = -1;
    if (log_paths < 0) log_paths = getenv("RCN_PATH_LOG") != NULL;
    if (log_paths) fprintf(stderr, "rcn : chemin %s\n", out);
    return out;
}

// --- setjmp / longjmp du jeu ---
// Le code traduit appelle le setjmp de l'hôte dans la fonction C qui correspond à la fonction du jeu
// (voir le recompilateur) : longjmp déroule alors la pile de l'hôte jusqu'à elle, comme dans le jeu.

jmp_buf *rcn_setjmp(RcnCtx *ctx, u64 guest_buf) {
    RcnThread *thread = THREAD(ctx); // les points de reprise sont propres au thread
    RcnJmp *slot = &thread->jmps[0];
    for (int i = 0; i < RCN_JMP_SLOTS; i++) {
        RcnJmp *j = &thread->jmps[i];
        if (j->guest_buf == guest_buf) {
            slot = j;
            break;
        }
        if (j->used < slot->used) slot = j;
    }
    slot->guest_buf = guest_buf;
    slot->used = ++thread->jmp_clock;
    memcpy(slot->gpr, ctx->r, sizeof slot->gpr);
    memcpy(slot->xmm, &ctx->xmm[6], sizeof slot->xmm);
    return &slot->host;
}

// Code machine traduit : setjmp enregistre les registres avec le jeton de la fonction appelante et le
// numéro du point d'appel ; longjmp les rétablit et demande la remontée jusqu'à elle (native.rs).
void rcn_setjmp_native(RcnCtx *ctx, u64 guest_buf, u64 token, u32 site) {
    RcnThread *thread = THREAD(ctx);
    RcnJmp *slot = &thread->jmps[0];
    for (int i = 0; i < RCN_JMP_SLOTS; i++) {
        RcnJmp *j = &thread->jmps[i];
        if (j->guest_buf == guest_buf) {
            slot = j;
            break;
        }
        if (j->used < slot->used) slot = j;
    }
    slot->guest_buf = guest_buf;
    slot->used = ++thread->jmp_clock;
    slot->token = token;
    slot->site = site;
    ctx->r[0] = 0;
    memcpy(slot->gpr, ctx->r, sizeof slot->gpr);
    memcpy(slot->xmm, &ctx->xmm[6], sizeof slot->xmm);
}

void rcn_longjmp_native(RcnCtx *ctx, u64 guest_buf, int value) {
    RcnThread *thread = THREAD(ctx);
    for (int i = 0; i < RCN_JMP_SLOTS; i++) {
        RcnJmp *j = &thread->jmps[i];
        if (j->used && j->guest_buf == guest_buf) {
            memcpy(ctx->r, j->gpr, sizeof j->gpr);
            memcpy(&ctx->xmm[6], j->xmm, sizeof j->xmm);
            ctx->r[0] = (u32)(value ? value : 1);
            ctx->unwind = j->token;
            ctx->unwind_site = j->site;
            return;
        }
    }
    rcn_fail(ctx, "longjmp vers un setjmp inconnu");
}
_Noreturn void rcn_longjmp(RcnCtx *ctx, u64 guest_buf, int value) {
    RcnThread *thread = THREAD(ctx);
    for (int i = 0; i < RCN_JMP_SLOTS; i++) {
        RcnJmp *j = &thread->jmps[i];
        if (j->used && j->guest_buf == guest_buf) {
            // Registres que la convention d'appel préserve, pile comprise ; rax : valeur de retour de setjmp.
            memcpy(ctx->r, j->gpr, sizeof j->gpr);
            memcpy(&ctx->xmm[6], j->xmm, sizeof j->xmm);
            ctx->r[0] = (u32)(value ? value : 1);
            longjmp(j->host, 1);
        }
    }
    rcn_fail(ctx, "longjmp vers un setjmp inconnu");
}

// Import que l'hôte ne fournit pas : le programme s'arrête en le nommant s'il l'appelle.
static void missing_import(RcnCtx *ctx) {
    const RcnImport *imp = THREAD(ctx)->current_import;
    rcn_fail(ctx, "fonction Windows non fournie : %s!%s", imp->dll, imp->name);
}

// --- Instructions fournies par le runtime ---

void rcn_cpuid(RcnCtx *ctx) {
    u32 leaf = (u32)ctx->r[0];
    u32 a = 0, b = 0, c = 0, d = 0;
    switch (leaf) {
    case 0:
        a = 1;
        b = 0x756e6547; // « GenuineIntel »
        d = 0x49656e69;
        c = 0x6c65746e;
        break;
    case 1:
        a = 0x000306a9;
        // Seules les extensions que le traducteur gère sont annoncées : MMX, SSE, SSE2 (edx), SSE3, SSSE3, POPCNT
        // (ecx). SSE3 et SSSE3 : tous les processeurs x86-64 de jeu les ont, et des bibliothèques n'ont que des
        // chemins faux sans eux (son des vidéos Bink : bruit).
        d = (1u << 0) | (1u << 4) | (1u << 8) | (1u << 15) | (1u << 23) | (1u << 24) | (1u << 25) | (1u << 26);
        // SSE4.1 et SSE4.2 (bits 19, 20) : exigés par les moteurs récents (Unreal Engine 5).
        c = (1u << 23) | 1u | (1u << 9) | (1u << 19) | (1u << 20);
        break;
    case 0x80000000u:
        a = 0x80000001u;
        break;
    case 0x80000001u:
        d = 1u << 29; // mode 64 bits
        c = 1u << 5;  // LZCNT
        break;
    }
    ctx->r[0] = a;
    ctx->r[3] = b;
    ctx->r[1] = c;
    ctx->r[2] = d;
    if (getenv("RCN_CPUID_LOG")) {
        const RcnModule *m = rcn_code_module((u64)(uintptr_t)__builtin_return_address(0));
        fprintf(stderr, "rcn : cpuid %x (%s) = %x %x %x %x\n", leaf, m ? m->name : "?", a, b, c, d);
    }
}

void rcn_rdtsc(RcnCtx *ctx) {
    struct timespec ts;
#if defined(_WIN32)
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    u64 t = (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
    ctx->r[0] = (u32)t;
    ctx->r[2] = (u32)(t >> 32);
}

static u64 load(u64 a, int size) {
    switch (size) {
    case 1: return rd8(a);
    case 2: return rd16(a);
    case 4: return rd32(a);
    default: return rd64(a);
    }
}
static void store(u64 a, int size, u64 v) {
    switch (size) {
    case 1: wr8(a, (u8)v); break;
    case 2: wr16(a, (u16)v); break;
    case 4: wr32(a, (u32)v); break;
    default: wr64(a, v); break;
    }
}
// Drapeaux de a - b sur `size` octets (cmps, scas).
static void flags_sub(RcnCtx *ctx, u64 a, u64 b, int size) {
    int top = size * 8 - 1;
    u64 mask = size == 8 ? ~0ull : (1ull << (size * 8)) - 1;
    u64 r = (a - b) & mask;
    ctx->cf = a < b;
    ctx->zf = r == 0;
    ctx->sf = (r >> top) & 1;
    ctx->of = (((a ^ b) & (a ^ r)) >> top) & 1;
    ctx->af = ((a ^ b ^ r) >> 4) & 1;
    ctx->pf = rcn_parity((u8)r);
}
static void set_acc(RcnCtx *ctx, int size, u64 v) {
    switch (size) {
    case 1: ctx->r[0] = (ctx->r[0] & ~0xFFull) | (u8)v; break;
    case 2: ctx->r[0] = (ctx->r[0] & ~0xFFFFull) | (u16)v; break;
    case 4: ctx->r[0] = (u32)v; break;
    default: ctx->r[0] = v; break;
    }
}

// En 32 bits, esi et edi restent sur 32 bits.
static void wrap32(RcnCtx *ctx) {
    if (ctx->bits32) {
        ctx->r[6] = (u32)ctx->r[6];
        ctx->r[7] = (u32)ctx->r[7];
    }
}

// rep movs / rep stos en avant (DF = 0) d'un jeu 64 bits : d'un bloc. Le runtime .NET met ainsi à zéro
// chaque allocation (rep stosb/stosq) ; élément par élément, c'était ≈ 7 % du thread principal de SoR4.
// rep movs garde sa sémantique d'avancée élément par élément si la destination recouvre la source
// devant elle (motif répété) : boucle ordinaire dans ce cas.
void rcn_movs(RcnCtx *ctx, int size, int rep) {
    if (rep && !ctx->df && !ctx->bits32 && ctx->r[1]) {
        u64 bytes = ctx->r[1] * (u64)size, dst = ctx->r[7], src = ctx->r[6];
        if (dst <= src || dst >= src + bytes) {
            memmove((void *)(uintptr_t)dst, (const void *)(uintptr_t)src, (size_t)bytes);
            ctx->r[6] = src + bytes;
            ctx->r[7] = dst + bytes;
            ctx->r[1] = 0;
            return;
        }
    }
    i64 step = ctx->df ? -size : size;
    for (u64 n = rep ? ctx->r[1] : 1; n; n--) {
        store(ctx->r[7], size, load(ctx->r[6], size));
        ctx->r[6] += step;
        ctx->r[7] += step;
        wrap32(ctx);
    }
    if (rep) ctx->r[1] = 0;
}
void rcn_stos(RcnCtx *ctx, int size, int rep) {
    if (rep && !ctx->df && !ctx->bits32 && ctx->r[1]) {
        u64 n = ctx->r[1], dst = ctx->r[7], v = ctx->r[0];
        u8 *p = (u8 *)(uintptr_t)dst;
        u64 pattern = size == 1 ? (u8)v * 0x0101010101010101ull : size == 2 ? (u16)v * 0x0001000100010001ull
                    : size == 4 ? (u32)v * 0x0000000100000001ull : v;
        if (pattern == 0 || size == 1) {
            memset(p, (u8)pattern, (size_t)(n * (u64)size));
        } else {
            for (u64 i = 0; i < n; i++) memcpy(p + i * (u64)size, &pattern, (size_t)size);
        }
        ctx->r[7] = dst + n * (u64)size;
        ctx->r[1] = 0;
        return;
    }
    i64 step = ctx->df ? -size : size;
    for (u64 n = rep ? ctx->r[1] : 1; n; n--) {
        store(ctx->r[7], size, ctx->r[0]);
        ctx->r[7] += step;
        wrap32(ctx);
    }
    if (rep) ctx->r[1] = 0;
}
void rcn_lods(RcnCtx *ctx, int size, int rep) {
    i64 step = ctx->df ? -size : size;
    for (u64 n = rep ? ctx->r[1] : 1; n; n--) {
        set_acc(ctx, size, load(ctx->r[6], size));
        ctx->r[6] += step;
        wrap32(ctx);
    }
    if (rep) ctx->r[1] = 0;
}
// repe : tant que les éléments sont égaux ; repne : tant qu'ils diffèrent.
void rcn_cmps(RcnCtx *ctx, int size, int rep) {
    i64 step = ctx->df ? -size : size;
    do {
        if (rep && ctx->r[1] == 0) return;
        flags_sub(ctx, load(ctx->r[6], size), load(ctx->r[7], size), size);
        ctx->r[6] += step;
        ctx->r[7] += step;
        wrap32(ctx);
        if (rep) ctx->r[1]--;
    } while ((rep == 1 && ctx->zf) || (rep == 2 && !ctx->zf));
}
void rcn_scas(RcnCtx *ctx, int size, int rep) {
    i64 step = ctx->df ? -size : size;
    u64 mask = size == 8 ? ~0ull : (1ull << (size * 8)) - 1;
    do {
        if (rep && ctx->r[1] == 0) return;
        flags_sub(ctx, ctx->r[0] & mask, load(ctx->r[7], size), size);
        ctx->r[7] += step;
        wrap32(ctx);
        if (rep) ctx->r[1]--;
    } while ((rep == 1 && ctx->zf) || (rep == 2 && !ctx->zf));
}

// Registre des drapeaux : CF 0, PF 2, AF 4, ZF 6, SF 7, DF 10, OF 11 ; bits 1 et 9 (interruptions) à 1.
u32 rcn_get_flags(RcnCtx *ctx) {
    return 0x202u | ctx->cf | ctx->pf << 2 | ctx->af << 4 | ctx->zf << 6 | ctx->sf << 7 | ctx->df << 10 | ctx->of << 11;
}
void rcn_set_flags(RcnCtx *ctx, u64 flags) {
    ctx->cf = flags & 1;
    ctx->pf = (flags >> 2) & 1;
    ctx->af = (flags >> 4) & 1;
    ctx->zf = (flags >> 6) & 1;
    ctx->sf = (flags >> 7) & 1;
    ctx->df = (flags >> 10) & 1;
    ctx->of = (flags >> 11) & 1;
}

// Flottant de 80 bits : mantisse de 64 bits (bit entier explicite), exposant de 15 bits, signe.
double rcn_ld80(u64 addr) {
    u64 mantissa = rd64(addr);
    u16 high = rd16(addr + 8);
    int exponent = high & 0x7FFF;
    double v;
    if (exponent == 0x7FFF) v = (mantissa << 1) ? NAN : INFINITY;
    else v = ldexp((double)mantissa, exponent - 16383 - 63);
    return (high & 0x8000) ? -v : v;
}
void rcn_st80(u64 addr, double value) {
    u16 sign = signbit(value) ? 0x8000 : 0;
    u64 mantissa = 0;
    u16 exponent = 0;
    if (value != value) {
        mantissa = 0xC000000000000000ull;
        exponent = 0x7FFF;
    } else if (isinf(value)) {
        mantissa = 0x8000000000000000ull;
        exponent = 0x7FFF;
    } else if (value != 0) {
        int e;
        double m = frexp(fabs(value), &e); // m dans [0,5 ; 1[
        mantissa = (u64)ldexp(m, 64);
        exponent = (u16)(e - 1 + 16383);
    }
    wr64(addr, mantissa);
    wr16(addr + 8, sign | exponent);
}
// fxam : C1 = signe ; zéro : C3 ; nombre : C2 ; infini : C2 et C0 ; NaN : C0.
void rcn_fxam(RcnCtx *ctx) {
    double v = ST(0);
    u16 sw = ctx->fp_sw & ~0x4700;
    if (signbit(v)) sw |= 0x0200;
    if (v != v) sw |= 0x0100;
    else if (isinf(v)) sw |= 0x0500;
    else if (v == 0) sw |= 0x4000;
    else sw |= 0x0400;
    ctx->fp_sw = sw;
}

// --- Chargement de l'exécutable ---

typedef struct {
    const u8 *data;
    size_t size;
} Bytes;

static int in_range(size_t off, size_t len, size_t size) { return off <= size && len <= size - off; }
static u32 get32(Bytes b, size_t off) { return in_range(off, 4, b.size) ? rd32((u64)(uintptr_t)(b.data + off)) : 0; }
static u16 get16(Bytes b, size_t off) { return in_range(off, 2, b.size) ? rd16((u64)(uintptr_t)(b.data + off)) : 0; }
static u64 get64(Bytes b, size_t off) { return in_range(off, 8, b.size) ? rd64((u64)(uintptr_t)(b.data + off)) : 0; }

static u8 *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    u8 *data = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long len = ftell(f);
        if (len > 0 && fseek(f, 0, SEEK_SET) == 0) {
            data = malloc((size_t)len);
            if (data && fread(data, 1, (size_t)len, f) != (size_t)len) {
                free(data);
                data = NULL;
            }
            *size = (size_t)len;
        }
    }
    fclose(f);
    return data;
}

// Chaîne terminée par un zéro dans l'image, ou "" si elle en sort.
static const char *image_str(Bytes img, u32 rva) {
    if (rva >= img.size || !memchr(img.data + rva, 0, img.size - rva)) return "";
    return (const char *)img.data + rva;
}

// --- Modules du jeu ---

static char *code_dir_global;
void rcn_set_code_dir(const char *code_path) {
    const char *name = code_path;
    for (const char *p = code_path; *p; p++) {
        if (*p == '/' || *p == '\\') name = p + 1;
    }
    size_t n = (size_t)(name - code_path);
    free(code_dir_global);
    code_dir_global = malloc(n + 2);
    if (!code_dir_global) return;
    if (n == 0) strcpy(code_dir_global, ".");
    else {
        memcpy(code_dir_global, code_path, n - 1);
        code_dir_global[n - 1] = 0;
    }
}

RcnLoaded *rcn_loaded_at(RcnProcess *proc, u64 address) {
    if (!proc) return NULL;
    for (u32 i = 0; i < proc->loaded_count; i++) {
        RcnLoaded *l = &proc->loaded[i];
        if (address - (u64)(uintptr_t)l->image < l->size) return l;
    }
    return NULL;
}

// Nom de fichier sans le chemin.
static const char *base_name(const char *path) {
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') name = p + 1;
    }
    return name;
}
static int same_name(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}
// « fmod64 » désigne aussi « fmod64.dll ».
static int same_module(const char *a, const char *b) {
    if (same_name(a, b)) return 1;
    char with[300];
    snprintf(with, sizeof with, "%s.dll", b);
    return same_name(a, with);
}

RcnLoaded *rcn_loaded_named(RcnProcess *proc, const char *name) {
    for (u32 i = 0; i < proc->loaded_count; i++) {
        if (same_module(proc->loaded[i].name, name)) return &proc->loaded[i];
    }
    return NULL;
}

u64 rcn_module_export(RcnLoaded *l, const char *name) {
    Bytes img = {l->image, l->size};
    u32 nt = get32(img, 0x3c), opt = nt + 24;
    u32 dirs = opt + 108;
    u32 dir = get32(img, dirs + 4), dir_size = get32(img, dirs + 8);
    if (!dir) return 0;
    u32 ordinal_base = get32(img, dir + 16), funcs_count = get32(img, dir + 20), names_count = get32(img, dir + 24);
    u32 funcs = get32(img, dir + 28), names = get32(img, dir + 32), ordinals = get32(img, dir + 36);
    u32 index = ~0u;
    if (name[0] == '#') {
        index = (u32)atoi(name + 1) - ordinal_base;
    } else {
        // Noms triés : recherche dichotomique.
        u32 lo = 0, hi = names_count;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2;
            int c = strcmp(image_str(img, get32(img, names + 4 * mid)), name);
            if (c == 0) {
                index = get16(img, ordinals + 2 * mid);
                break;
            }
            if (c < 0) lo = mid + 1;
            else hi = mid;
        }
    }
    if (index >= funcs_count) return 0;
    u32 rva = get32(img, funcs + 4 * index);
    // Renvoi vers une autre bibliothèque (« DLL.fonction ») : non géré.
    if (!rva || (rva >= dir && rva < dir + dir_size)) return 0;
    return (u64)(uintptr_t)l->image + rva;
}

static int map_module(RcnProcess *proc, const char *path, const RcnModule *m, RcnLoaded *out);

// Copie du modèle TLS d'une DLL pour un thread, rangée dans sa case du tableau TLS du thread.
static void dll_tls_block(RcnThread *thread, const RcnLoaded *l) {
    if (!l->tls_index || !thread->teb) return;
    size_t size = (size_t)(l->tls_end - l->tls_start);
    u8 *block = calloc(1, size + l->tls_zero + 16);
    if (!block) return;
    memcpy(block, PTR(l->tls_start), size);
    wr64((u64)(uintptr_t)thread->teb + TEB_TLS_ARRAY + 8 * (u64)l->tls_index, (u64)(uintptr_t)block);
}
static void tls_for_running_threads(const RcnLoaded *l);
// Fonctions TLS d'une DLL (raison : 1 attachement au processus, 2 / 3 début / fin de thread).
static void dll_tls_callbacks(RcnCtx *ctx, const RcnLoaded *l, u32 reason) {
    for (u64 p = l->tls_callbacks; p && rd64(p); p += 8) rcn_call_guest(ctx, rd64(p), (u64)(uintptr_t)l->image, reason, 0, 0);
}

// Charge une DLL du jeu et ses dépendances, sans appeler DllMain. NULL si elle n'existe pas.
// Chemin d'un greffon natif de Unity (dossiers « *_Data/Plugins[/x86_64] » du jeu), ou NULL. À libérer.
static char *plugin_path(const char *game_dir, const char *file) {
    DIR *dir = opendir(game_dir);
    if (!dir) return NULL;
    char *found = NULL;
    struct dirent *e;
    while (!found && (e = readdir(dir))) {
        size_t n = strlen(e->d_name);
        if (n < 6 || strcmp(e->d_name + n - 5, "_Data")) continue;
        static const char *const subdirs[] = {"Plugins/x86_64", "Plugins"};
        for (int i = 0; i < 2 && !found; i++) {
            size_t len = strlen(game_dir) + n + strlen(subdirs[i]) + strlen(file) + 4;
            char *path = malloc(len);
            if (!path) break;
            snprintf(path, len, "%s/%s/%s/%s", game_dir, e->d_name, subdirs[i], file);
            FILE *f = fopen(path, "rb");
            if (f) {
                fclose(f);
                found = path;
            } else {
                free(path);
            }
        }
    }
    closedir(dir);
    return found;
}

static RcnLoaded *load_dll_locked(RcnProcess *proc, const char *name) {
    RcnLoaded *l = rcn_loaded_named(proc, name);
    if (l) return l;
    if (proc->module->bits != 64 || proc->loaded_count >= RCN_MAX_LOADED || !code_dir_global || !proc->game_dir) return NULL;
    char file[300];
    snprintf(file, sizeof file, "%s", base_name(name));
    if (!strchr(file, '.')) strcat(file, ".dll");
    // Code traduit : « <dossier du code>/<nom en minuscules>.rcnx ».
    char lower[300];
    for (size_t i = 0;; i++) {
        char c = file[i];
        lower[i] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
        if (!c) break;
    }
    size_t code_len = strlen(code_dir_global) + strlen(lower) + 8;
    char *code_path = malloc(code_len);
    char *dll_path = malloc(strlen(proc->game_dir) + strlen(file) + 2);
    if (!code_path || !dll_path) {
        free(code_path);
        free(dll_path);
        return NULL;
    }
    snprintf(code_path, code_len, "%s/%s.rcnx", code_dir_global, lower);
    sprintf(dll_path, "%s/%s", proc->game_dir, file);
    // Greffons natifs de Unity : « <jeu>_Data/Plugins/x86_64 » ou « <jeu>_Data/Plugins ».
    FILE *image = fopen(dll_path, "rb");
#if !defined(_WIN32)
    // Nom demandé dans une autre casse que celle du fichier (« mojoshader.dll » pour MojoShader.dll).
    if (!image) {
        match_case(proc->game_dir, dll_path + strlen(proc->game_dir) + 1);
        image = fopen(dll_path, "rb");
    }
#endif
    if (image) {
        fclose(image);
    } else {
        char *found = plugin_path(proc->game_dir, file);
        if (!found) {
            // DLL du système fournie avec la traduction (msvcp140.dll…) : son image à côté de son code.
            char *beside = malloc(strlen(code_dir_global) + strlen(lower) + 2);
            if (beside) {
                sprintf(beside, "%s/%s", code_dir_global, lower);
                FILE *f = fopen(beside, "rb");
                if (f) {
                    fclose(f);
                    found = beside;
                } else {
                    free(beside);
                }
            }
        }
        if (found) {
            free(dll_path);
            dll_path = found;
        }
    }
    FILE *probe = fopen(code_path, "rb");
    if (probe) fclose(probe);
    char error[512];
    const RcnModule *m = probe ? rcn_load_rcnx(code_path, error, sizeof error) : NULL;
    if (probe && !m) fprintf(stderr, "rcn : %s : %s\n", file, error);
    free(code_path);
    if (!m) {
        free(dll_path);
        return NULL;
    }
    RcnLoaded tmp = {0};
    int status = map_module(proc, dll_path, m, &tmp);
    free(dll_path);
    if (status != 0) {
        fprintf(stderr, "rcn : %s : %s\n", file, proc->result->error);
        return NULL;
    }
    tmp.name = malloc(strlen(file) + 1);
    if (tmp.name) strcpy(tmp.name, file);
    // Ajouté après ses dépendances : l'ordre de la liste est celui des DllMain.
    l = &proc->loaded[proc->loaded_count++];
    *l = tmp;
    // Variables locales au thread : une case du tableau TLS, son numéro écrit dans la DLL (_tls_index).
    Bytes img = {l->image, l->size};
    u32 dirs = get32(img, 0x3c) + 24 + 108;
    u32 dir = get32(img, dirs) > 9 ? get32(img, dirs + 4 + 9 * 8) : 0;
    if (dir && in_range(dir, 40, l->size)) {
        u64 base = (u64)(uintptr_t)l->image, at = base + dir;
        u64 start = rd64(at), end = rd64(at + 8), index = rd64(at + 16);
        if (start >= base && end >= start && end - base <= l->size && index - base < l->size) {
            u32 used = 0;
            for (u32 i = 1; i + 1 < proc->loaded_count; i++) used = proc->loaded[i].tls_index > used ? proc->loaded[i].tls_index : used;
            l->tls_index = used + 1;
            l->tls_start = start;
            l->tls_end = end;
            l->tls_callbacks = rd64(at + 24) - base < l->size ? rd64(at + 24) : 0;
            l->tls_zero = rd32(at + 32);
            wr32(index, l->tls_index);
            tls_for_running_threads(l);
        }
    }
    return l;
}

// DllMain(module, DLL_PROCESS_ATTACH, 0) des DLL chargées qui ne l'ont pas encore reçu, dans l'ordre.
static void attach_dlls(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    for (u32 i = 1; i < proc->loaded_count; i++) {
        RcnLoaded *l = &proc->loaded[i];
        if (l->attached) continue;
        l->attached = 1;
        dll_tls_callbacks(ctx, l, 1);
        if (l->code->entry_rva) {
            u64 base = (u64)(uintptr_t)l->image;
            if (proc->trace) fprintf(stderr, "[rcn %u] DllMain(%s)\n", THREAD_ID(ctx), l->name);
            rcn_call_guest(ctx, base + l->code->entry_rva, base, 1, 0, 0);
        }
    }
}

u64 rcn_load_dll(RcnCtx *ctx, const char *name) {
    RcnProcess *proc = ctx->proc;
    pthread_mutex_lock(&proc->lock);
    RcnLoaded *l = load_dll_locked(proc, name);
    u64 base = l ? (u64)(uintptr_t)l->image : 0;
    pthread_mutex_unlock(&proc->lock);
    if (base) attach_dlls(ctx);
    return base;
}

// Charge l'exécutable ou une DLL : sections, relocalisations, imports. Renvoie 0 en cas de succès.
// 64 bits : l'image va à une adresse quelconque et ses pointeurs absolus sont relocalisés.
// 32 bits : elle va à son adresse d'origine (ces exécutables n'ont souvent pas de relocalisations).
static int map_module(RcnProcess *proc, const char *path, const RcnModule *m, RcnLoaded *out) {
    int bits32 = m->bits == 32;
    size_t file_size = 0;
    u8 *file_data = read_file(path, &file_size);
    if (!file_data) {
        set_error(proc, "executable illisible : %s", path);
        return -1;
    }
    Bytes file = {file_data, file_size};
    int status = -1;
    u32 nt = get32(file, 0x3c);
    u32 opt = nt + 24;
    u32 psz = bits32 ? 4 : 8;
    if (get16(file, 0) != 0x5a4d || get32(file, nt) != 0x4550 || get16(file, nt + 4) != (bits32 ? 0x14c : 0x8664) ||
        get16(file, opt) != (bits32 ? 0x10b : 0x20b)) {
        set_error(proc, "ce n'est pas un executable Windows %s : %s", bits32 ? "x86" : "x86-64", path);
        goto done;
    }
    // Répertoires de données : 16 octets plus tôt dans l'en-tête 32 bits.
    u32 dirs = opt + (bits32 ? 92 : 108);
    u32 image_size = get32(file, opt + 56);
    u64 image_base = bits32 ? get32(file, opt + 28) : get64(file, opt + 24);
    // Le code traduit ne vaut que pour l'exécutable dont il vient.
    if (image_size != m->image_size || get32(file, nt + 8) != m->timestamp || get32(file, opt + 16) != m->entry_rva ||
        image_base != m->image_base) {
        set_error(proc, "cet executable n'est pas celui qui a ete traduit (%s)", m->name);
        goto done;
    }
    u8 *image = bits32 ? rcn_map_fixed(image_base, image_size) : rcn_page_alloc(image_size);
    if (!image) {
        if (bits32) set_error(proc, "adresse de chargement 0x%llx indisponible", (unsigned long long)image_base);
        else set_error(proc, "memoire insuffisante pour l'image (%u octets)", image_size);
        goto done;
    }
    out->image = image;
    out->size = image_size;
    out->code = m;
    if (m->native) ((RcnModule *)m)->base_slot = (u64)(uintptr_t)image;
    Bytes img = {image, image_size};

    u32 headers = get32(file, opt + 60);
    if (headers > file_size) headers = (u32)file_size;
    if (headers > image_size) headers = image_size;
    memcpy(image, file_data, headers);
    u32 section_count = get16(file, nt + 6);
    u32 table = opt + get16(file, nt + 20);
    for (u32 i = 0; i < section_count; i++) {
        u32 s = table + i * 40;
        u32 vsize = get32(file, s + 8), rva = get32(file, s + 12);
        u32 raw_size = get32(file, s + 16), raw_off = get32(file, s + 20);
        u32 n = vsize && vsize < raw_size ? vsize : raw_size;
        if (!in_range(raw_off, n, file_size) || !in_range(rva, n, image_size)) {
            set_error(proc, "section %u hors de l'executable", i);
            goto done;
        }
        memcpy(image + rva, file_data + raw_off, n);
    }

    u32 dir_count = get32(file, dirs);
    if (!bits32) {
        // Relocalisations : pointeurs absolus des données, décalés de l'écart avec l'adresse prévue.
        u64 delta = (u64)(uintptr_t)image - m->image_base;
        u32 reloc = dir_count > 5 ? get32(file, dirs + 4 + 5 * 8) : 0;
        u32 reloc_size = dir_count > 5 ? get32(file, dirs + 4 + 5 * 8 + 4) : 0;
        if (!reloc) {
            set_error(proc, "executable sans table de relocalisations : il ne peut pas etre charge");
            goto done;
        }
        for (u32 off = 0; off + 8 <= reloc_size;) {
            u32 page = get32(img, reloc + off), block = get32(img, reloc + off + 4);
            if (block < 8) break;
            for (u32 e = 8; e + 2 <= block && off + e + 2 <= reloc_size; e += 2) {
                u16 entry = get16(img, reloc + off + e);
                u32 at = page + (entry & 0xfff);
                if ((entry >> 12) == 10 && in_range(at, 8, image_size)) wr64((u64)(uintptr_t)(image + at), get64(img, at) + delta);
            }
            off += block;
        }
    }

    // Imports : chaque case de la table reçoit l'adresse d'un RcnImport (reconnue par rcn_call), ou celle
    // de la fonction exportée par une autre DLL du jeu.
    u32 imp_dir = dir_count > 1 ? get32(file, dirs + 4 + 8) : 0;
    u32 count = 0;
    RcnImport *imports = NULL;
    for (int pass = 0; pass < 2 && imp_dir; pass++) {
        if (pass == 1) {
            // En 32 bits, l'adresse d'un import doit tenir dans une case de 4 octets.
            size_t bytes = (count ? count : 1) * sizeof(RcnImport);
            imports = bits32 ? rcn_low_alloc(bytes, 1) : calloc(1, bytes);
            if (!imports) {
                set_error(proc, bits32 ? "pas de memoire libre sous 2 Go pour ce jeu 32 bits" : "memoire insuffisante");
                goto done;
            }
            // Les imports de l'exécutable sont reconnus par rcn_call ; ceux d'une DLL passent par les relais.
            if (!proc->imports) {
                proc->imports = imports;
                proc->import_count = count;
            }
        }
        u32 k = 0;
        for (u32 d = imp_dir; in_range(d, 20, image_size); d += 20) {
            u32 ilt = get32(img, d), name_rva = get32(img, d + 12), iat = get32(img, d + 16);
            if (!name_rva && !iat) break;
            const char *dll = image_str(img, name_rva);
            for (u32 i = 0;; i++, k++) {
                u32 at = (ilt ? ilt : iat) + i * psz;
                u64 entry = bits32 ? get32(img, at) : get64(img, at);
                if (!entry || !in_range(iat + i * psz, psz, image_size)) break;
                if (pass == 0) continue;
                RcnImport *imp = &imports[k];
                int by_ordinal = (entry >> (psz * 8 - 1)) & 1;
                imp->dll = dll;
                // Import par numéro d'ordre : nommé « #numéro » (texte gardé pour la durée du processus).
                if (by_ordinal) {
                    char *ordinal = malloc(12);
                    if (ordinal) snprintf(ordinal, 12, "#%u", (u32)(entry & 0xFFFF));
                    imp->name = ordinal ? ordinal : "#?";
                } else {
                    imp->name = image_str(img, (u32)entry + 2);
                }
                u64 slot = (u64)(uintptr_t)(image + iat + i * psz);
                imp->fn = rcn_win32_lookup(dll, imp->name, bits32, &imp->pop);
                if (!imp->fn) {
                    // Fonction d'une autre DLL du jeu, traduite elle aussi.
                    RcnLoaded *other = bits32 ? NULL : load_dll_locked(proc, dll);
                    u64 address = other ? rcn_module_export(other, imp->name) : 0;
                    if (address) {
                        wr64(slot, address);
                        continue;
                    }
                    imp->fn = missing_import;
                    // RCN_IMPORT_LOG=1 : imports non fournis dès le chargement (sinon nommés au premier appel).
                    static int log_missing = -1;
                    if (log_missing < 0) log_missing = getenv("RCN_IMPORT_LOG") != NULL;
                    if (log_missing) fprintf(stderr, "rcn : import non fourni : %s!%s\n", dll, imp->name);
                }
                u64 target = (u64)(uintptr_t)imp;
                if (imports != proc->imports) {
                    // Relais du processus (reconnu par rcn_call), une seule fois par fonction.
                    if (!proc->thunks) proc->thunks = calloc(RCN_MAX_THUNKS, sizeof(RcnImport));
                    if (proc->thunks && proc->thunk_count < RCN_MAX_THUNKS) {
                        proc->thunks[proc->thunk_count] = *imp;
                        target = (u64)(uintptr_t)&proc->thunks[proc->thunk_count++];
                    }
                }
                if (bits32) wr32(slot, (u32)target);
                else wr64(slot, target);
            }
        }
        count = k;
    }
    status = 0;
done:
    free(file_data);
    return status;
}

static int load_image(RcnProcess *proc, const char *path) {
    RcnLoaded *exe = &proc->loaded[0];
    proc->loaded_count = 1;
    exe->name = proc->exe_name;
    exe->attached = 1;
    exe->code = proc->module;
    int status = map_module(proc, path, proc->module, exe);
    proc->image = exe->image;
    return status;
}
// Dossier et nom de l'exécutable (séparateurs « / » ou « \ »).
static int setup_paths(RcnProcess *proc, const char *exe_path) {
    const char *name = exe_path;
    for (const char *p = exe_path; *p; p++) {
        if (*p == '/' || *p == '\\') name = p + 1;
    }
    size_t dir_len = (size_t)(name - exe_path);
    proc->exe_name = malloc(strlen(name) + 1);
    proc->game_dir = malloc(dir_len + 2);
    if (!proc->exe_name || !proc->game_dir) return -1;
    strcpy(proc->exe_name, name);
    if (dir_len == 0) {
        strcpy(proc->game_dir, ".");
    } else {
        // Sans la barre finale (sauf pour la racine).
        memcpy(proc->game_dir, exe_path, dir_len);
        proc->game_dir[dir_len > 1 ? dir_len - 1 : dir_len] = 0;
    }
    proc->crt_argv_data[0] = proc->exe_name;
    proc->crt_argv = proc->crt_argv_data;
    proc->crt_argc = 1;
    // Arguments donnés au jeu pour les essais : RCN_ARGS, séparés par des espaces.
    const char *args = getenv("RCN_ARGS");
    char *copy = args ? strdup(args) : NULL;
    for (char *save = NULL, *a = copy ? strtok_r(copy, " ", &save) : NULL; a && proc->crt_argc < 17; a = strtok_r(NULL, " ", &save))
        proc->crt_argv_data[proc->crt_argc++] = a;
    proc->crt_argv_data[proc->crt_argc] = NULL;
    proc->crt_environ = proc->crt_environ_data;
    return 0;
}

static void default_write(void *user, int stream, const char *data, size_t len) {
    (void)user;
    fwrite(data, 1, len, stream == RCN_STREAM_STDERR ? stderr : stdout);
}


// --- Threads du jeu ---

// Mémoire propre à un thread, que le jeu voit (pile, bloc d'environnement, variables locales).
static void *thread_alloc(int bits32, size_t size) { return bits32 ? rcn_low_alloc(size, 1) : rcn_page_alloc(size); }
static void thread_mem_free(int bits32, void *p) {
    if (bits32) rcn_low_free(p);
    else rcn_page_free(p);
}

// Variables locales au thread : copie du modèle de l'exe, rangée dans le tableau que le code lit par
// le bloc d'environnement du thread. Note l'adresse de la liste des fonctions à appeler au début du thread.
static int setup_tls(RcnThread *thread) {
    RcnCtx *ctx = &thread->ctx;
    RcnProcess *proc = ctx->proc;
    Bytes img = {proc->image, proc->module->image_size};
    u32 psz = PTR_SIZE(ctx);
    u32 dirs = get32(img, 0x3c) + 24 + (ctx->bits32 ? 92 : 108);
    u32 dir = get32(img, dirs) > 9 ? get32(img, dirs + 4 + 9 * 8) : 0;
    u64 array = (u64)(uintptr_t)thread->teb + TEB_TLS_ARRAY;
    wrp(ctx, (u64)(uintptr_t)rcn_teb(ctx, TEB_TLS_POINTER), array);
    if (!dir || !in_range(dir, 5 * psz, img.size)) return 0;
    // Adresses absolues (relocalisées en 64 bits) : modèle [start, end), puis une zone à zéro.
    u64 base = (u64)(uintptr_t)proc->image;
    u64 at = base + dir;
    u64 start = rdp(ctx, at), end = rdp(ctx, at + psz), index = rdp(ctx, at + 2 * psz);
    u64 callbacks = rdp(ctx, at + 3 * psz);
    u32 zero_fill = rd32(at + 4 * psz);
    if (start < base || end < start || end - base > img.size) return 0;
    size_t size = (size_t)(end - start);
    thread->tls_block = thread_alloc(ctx->bits32, size + zero_fill + 16);
    if (!thread->tls_block) return -1;
    memcpy(thread->tls_block, PTR(start), size);
    wrp(ctx, array, (u64)(uintptr_t)thread->tls_block);
    if (index - base < img.size) wr32(index, 0);
    thread->tls_callbacks = callbacks - base < img.size ? callbacks : 0;
    return 0;
}

static void live_remove(RcnThread *t);
static void thread_free(RcnThread *thread) {
    live_remove(thread);
    int bits32 = thread->ctx.bits32;
    thread_mem_free(bits32, thread->stack);
    thread_mem_free(bits32, thread->teb);
    thread_mem_free(bits32, thread->tls_block);
    free(thread->jmps);
    if (thread->call_cache != call_cache_none) free(thread->call_cache);
    free(thread);
}

RcnThread *rcn_thread_new(RcnProcess *proc, size_t stack_size) {
    RcnThread *thread = calloc(1, sizeof *thread);
    if (!thread) return NULL;
    if (stack_size < THREAD_STACK_SIZE) stack_size = THREAD_STACK_SIZE;
    stack_size = (stack_size + 0xFFFF) & ~(size_t)0xFFFF;
    RcnCtx *ctx = &thread->ctx;
    int bits32 = proc->module->bits == 32;
    ctx->proc = proc;
    ctx->bits32 = (u8)bits32;
    ctx->base = (u64)(uintptr_t)proc->image;
    ctx->image_size = proc->module->image_size;
    ctx->mxcsr = 0x1f80;
    ctx->fp_cw = 0x027F; // x87 : précision double, réglage par défaut des programmes Windows
    thread->stack = thread_alloc(bits32, stack_size);
    thread->teb = thread_alloc(bits32, TEB_SIZE);
    thread->jmps = calloc(RCN_JMP_SLOTS, sizeof(RcnJmp));
    if (!thread->stack || !thread->teb || !thread->jmps || setup_tls(thread) != 0) {
        thread_free(thread);
        return NULL;
    }
    // Variables locales au thread des DLL du jeu déjà chargées.
    for (u32 i = 1; i < proc->loaded_count; i++) dll_tls_block(thread, &proc->loaded[i]);
    // Bloc d'environnement du thread : limites de la pile, lui-même, identifiants, bloc du processus.
    u64 teb = (u64)(uintptr_t)thread->teb, peb = teb + TEB_PEB_OFFSET;
    u64 stack_top = (u64)(uintptr_t)thread->stack + stack_size;
#define TEB_SET(field, value) wrp(ctx, (u64)(uintptr_t)rcn_teb(ctx, field), value)
    TEB_SET(TEB_STACK_BASE, stack_top);
    TEB_SET(TEB_STACK_LIMIT, (u64)(uintptr_t)thread->stack);
    TEB_SET(TEB_SELF, teb);
    TEB_SET(TEB_PROCESS_ID, 1000);
    TEB_SET(TEB_PEB, peb);
    pthread_mutex_lock(&proc->lock);
    TEB_SET(TEB_THREAD_ID, ++proc->next_thread_id);
    pthread_mutex_unlock(&proc->lock);
#undef TEB_SET
    if (bits32) {
        wr32(teb, 0xFFFFFFFFu);    // fin de la liste des gestionnaires d'exceptions (fs:[0])
        wr32(peb + 0x08, (u32)ctx->base); // adresse de l'image
        ctx->fs_base = teb;
    } else {
        wr64(peb + 0x10, ctx->base);
        // Données du chargeur (PEB.Ldr) : l'exécutable seul, dans les trois listes de modules, que
        // certains programmes parcourent eux-mêmes (exécutables .NET compilés d'avance).
        u64 ldr = peb + 0x400, entry = peb + 0x500, name = peb + 0x600;
        wr64(peb + 0x18, ldr);
        wr32(ldr, 0x58);
        wr8(ldr + 4, 1);
        for (u32 list = 0; list < 3; list++) {
            u64 head = ldr + 0x10 + 16 * list, links = entry + 16 * list;
            wr64(head, links);
            wr64(head + 8, links);
            wr64(links, head);
            wr64(links + 8, head);
        }
        wr64(entry + 0x30, ctx->base);                                  // DllBase
        wr64(entry + 0x38, ctx->base + proc->module->entry_rva);        // EntryPoint
        wr32(entry + 0x40, proc->module->image_size);                   // SizeOfImage
        // FullDllName « C:\<exe> » et BaseDllName « <exe> » (UTF-16).
        const char *exe = proc->exe_name ? proc->exe_name : "game.exe";
        char full[300];
        snprintf(full, sizeof full, "C:\\%s", exe);
        size_t units = rcn_utf8_to_utf16(full, strlen(full), NULL);
        if (units < 250) {
            rcn_utf8_to_utf16(full, strlen(full), PTR(name));
            wr16(name + 2 * units, 0);
            wr16(entry + 0x48, (u16)(units * 2));
            wr16(entry + 0x4A, (u16)(units * 2 + 2));
            wr64(entry + 0x50, name);
            wr16(entry + 0x58, (u16)((units - 3) * 2));
            wr16(entry + 0x5A, (u16)((units - 3) * 2 + 2));
            wr64(entry + 0x60, name + 6);
        }
        // Paramètres du processus (PEB.ProcessParameters), lus par la bibliothèque C de Visual C++ :
        // drapeaux, handles standard, nom de l'image et ligne de commande, environnement vide.
        u64 params = peb + 0x900;
        wr64(peb + 0x20, params);
        wr32(params, 0x400);            // MaximumLength
        wr32(params + 4, 0x400);        // Length
        wr32(params + 8, 1);            // Flags : PROCESS_PARAMETERS_NORMALIZED
        wr64(params + 0x20, 0x10);      // StandardInput
        wr64(params + 0x28, 0x11);      // StandardOutput
        wr64(params + 0x30, 0x12);      // StandardError
        for (u32 s = 0; s < 2; s++) {   // ImagePathName (+0x60), CommandLine (+0x70)
            u64 us = params + 0x60 + 0x10 * s;
            wr16(us, rd16(entry + 0x48));
            wr16(us + 2, rd16(entry + 0x4A));
            wr64(us + 8, rd64(entry + 0x50));
        }
        wr64(params + 0x80, params + 0x3F0); // Environment : bloc vide (deux zéros UTF-16)
        ctx->gs_base = teb;
    }
    ctx->r[4] = stack_top - 0x100;
    return thread;
}
_Noreturn void rcn_thread_exit(RcnCtx *ctx, u32 code) {
    if (getenv("RCN_THREAD_LOG")) fprintf(stderr, "[rcn] thread %u fin\n", THREAD_ID(ctx));
    // Comme Windows : DLL_THREAD_DETACH aux fonctions TLS de l'exécutable, puis fin des cases FLS.
    RcnThread *self = THREAD(ctx);
    if (!self->is_main) {
        for (u64 p = self->tls_callbacks; p && rd64(p); p += 8) rcn_call_guest(ctx, rd64(p), ctx->base, 3, 0, 0);
        for (u32 i = 1; i < ctx->proc->loaded_count; i++) dll_tls_callbacks(ctx, &ctx->proc->loaded[i], 3);
    }
    rcn_fls_thread_end(ctx);
    const char *ts = getenv("RCN_THREAD_LOG") ? getenv("RCN_TS") : NULL;
    if (ts) {
        u64 store = rd64((u64)(uintptr_t)ctx->proc->loaded[0].image + strtoull(ts, NULL, 16));
        u64 list = store ? rd64(store) : 0, t = list ? rd64(list) : 0;
        fprintf(stderr, "[rcn] liste apres fin de %u :", THREAD_ID(ctx));
        for (int n = 0; t && n < 40; n++, t = rd64(t + 0x58)) fprintf(stderr, " %llx", (unsigned long long)t);
        fprintf(stderr, "\n");
    }
    pthread_mutex_lock(&ctx->proc->lock);
    thread_exit_locked(ctx, code);
}

// Fin du thread appelant, verrou du processus tenu (rendu ici) : la fin du jeu et celle du thread
// qui la déclare sont ainsi vues ensemble par rcn_port_run.
static _Noreturn void thread_exit_locked(RcnCtx *ctx, u32 code) {
    RcnThread *thread = THREAD(ctx);
    RcnProcess *proc = ctx->proc;
    if (thread->object) {
        thread->object->signaled = 1;
        thread->object->exit_code = code;
        rcn_object_unref(thread->object);
    }
    // Dernier thread : le jeu est terminé, même sans ExitProcess.
    if (--proc->threads_alive == 0) finish_locked(proc, RCN_OK, code, NULL);
    pthread_cond_broadcast(&proc->cond);
    pthread_mutex_unlock(&proc->lock);
    // Plus aucun accès au processus à partir d'ici : rcn_run_module peut le libérer.
    thread_free(thread);
#if defined(_WIN32)
    // pthread_exit de winpthreads déroule la pile par les exceptions de Windows, qui ne savent pas
    // traverser le code machine traduit : le thread se termine directement.
    {
        __declspec(dllimport) void __stdcall ExitThread(unsigned long code);
        ExitThread(0);
    }
#endif
    pthread_exit(NULL);
}

extern _Thread_local RcnCtx *rcn_current_ctx;

// --- Mise au point : RCN_HANG_DUMP=<secondes> affiche régulièrement l'état de chaque thread du jeu ---

static pthread_mutex_t live_lock = PTHREAD_MUTEX_INITIALIZER;
static RcnThread *live_threads[256];

static void live_add(RcnThread *t) {
    pthread_mutex_lock(&live_lock);
    for (int i = 0; i < 256; i++) {
        if (!live_threads[i]) {
            live_threads[i] = t;
            break;
        }
    }
    pthread_mutex_unlock(&live_lock);
}
static void live_remove(RcnThread *t) {
    pthread_mutex_lock(&live_lock);
    for (int i = 0; i < 256; i++) {
        if (live_threads[i] == t) live_threads[i] = NULL;
    }
    pthread_mutex_unlock(&live_lock);
}
static void tls_for_running_threads(const RcnLoaded *l) {
    pthread_mutex_lock(&live_lock);
    for (int i = 0; i < 256; i++) {
        if (live_threads[i]) dll_tls_block(live_threads[i], l);
    }
    pthread_mutex_unlock(&live_lock);
}

static void *hang_dump(void *arg) {
    RcnProcess *proc = arg;
    int seconds = atoi(getenv("RCN_HANG_DUMP"));
    if (seconds < 1) seconds = 10;
    for (;;) {
        rcn_sleep_ms((u32)seconds * 1000);
        pthread_mutex_lock(&live_lock);
        fprintf(stderr, "=== threads du jeu ===\n");
        rcn_sdlgl_debug();
        for (int i = 0; i < 256; i++) {
            RcnThread *t = live_threads[i];
            if (!t) continue;
            RcnCtx *ctx = &t->ctx;
            const RcnImport *imp = t->current_import;
            fprintf(stderr, "[%u] rsp=%llx rax=%llx rcx=%llx rdx=%llx rbx=%llx rsi=%llx rdi=%llx dernier import %s\n  pile :", THREAD_ID(ctx),
                    (unsigned long long)ctx->r[4], (unsigned long long)ctx->r[0], (unsigned long long)ctx->r[1],
                    (unsigned long long)ctx->r[2], (unsigned long long)ctx->r[3], (unsigned long long)ctx->r[6],
                    (unsigned long long)ctx->r[7], imp ? imp->name : "-");
            for (int k = 0, shown = 0; k < 256 && shown < 14; k++) {
                u64 v = rd64(ctx->r[4] + 8 * (u64)k);
                RcnLoaded *l = rcn_loaded_at(proc, v);
                if (l) {
                    fprintf(stderr, " %s+%llx", l->name, (unsigned long long)(v - (u64)(uintptr_t)l->image));
                    shown++;
                }
            }
            fprintf(stderr, "\n");
            // RCN_TLS=<rva hexadécimal> : indice TLS de l'exécutable (_tls_index) ; objet Thread .NET à +0x20 de
            // son bloc, cadre de transition à +0x40 (0 : le thread exécute du code managé).
            const char *tls = getenv("RCN_TLS");
            if (tls && t->teb) {
                u32 index = rd32((u64)(uintptr_t)proc->loaded[0].image + strtoull(tls, NULL, 16));
                u64 array = rd64((u64)(uintptr_t)t->teb + 0x58);
                u64 block = array ? rd64(array + 8 * (u64)index) : 0;
                if (block) fprintf(stderr, "  Thread .NET %llx cadre %llx\n", (unsigned long long)(block + 0x20), (unsigned long long)rd64(block + 0x60));
            }
        }
        // RCN_TS=<rva hexadécimal> : case de l'exécutable qui pointe sur la liste des threads d'un
        // runtime .NET compilé d'avance (ThreadStore) ; chaque Thread : suivant +0x58, cadre de transition
        // +0x40, handle +0x60, numéro du thread système cherché parmi ses premiers champs.
        const char *ts = getenv("RCN_TS");
        if (ts && proc->loaded_count) {
            u64 store = rd64((u64)(uintptr_t)proc->loaded[0].image + strtoull(ts, NULL, 16));
            u64 list = store ? rd64(store) : 0;
            u64 t = list ? rd64(list) : 0;
            for (int n = 0; t && n < 64; n++, t = rd64(t + 0x58)) {
                fprintf(stderr, "  Thread %llx : cadre %llx cache %llx handle %llx +70 %llx +38 %x\n", (unsigned long long)t,
                        (unsigned long long)rd64(t + 0x40), (unsigned long long)rd64(t + 0x50), (unsigned long long)rd64(t + 0x60),
                        (unsigned long long)rd64(t + 0x70), rd32(t + 0x38));
            }
        }
        fflush(stderr);
        pthread_mutex_unlock(&live_lock);
    }
    return NULL;
}

static void *thread_main(void *arg) {
    RcnThread *thread = arg;
    RcnCtx *ctx = &thread->ctx;
    rcn_current_ctx = ctx;
    rcn_thread_default_affinity();
    live_add(thread);
    if (getenv("RCN_THREAD_LOG")) fprintf(stderr, "[rcn] thread %u debut, tls %p, depart %llx\n", THREAD_ID(ctx), (void *)thread->tls_block, (unsigned long long)thread->start_fn);
    // Fonctions d'initialisation des variables locales au thread :
    // (image, DLL_PROCESS_ATTACH pour le thread principal ou DLL_THREAD_ATTACH, 0).
    for (u64 p = thread->tls_callbacks; p && rd64(p); p += 8) {
        rcn_call_guest(ctx, rd64(p), ctx->base, thread->is_main ? 1 : 2, 0, 0);
    }
    // DLL du jeu importées par l'exécutable : leur DllMain avant le programme. Autres threads : fonctions
    // TLS des DLL (DLL_THREAD_ATTACH).
    if (thread->is_main) attach_dlls(ctx);
    else for (u32 i = 1; i < ctx->proc->loaded_count; i++) dll_tls_callbacks(ctx, &ctx->proc->loaded[i], 2);
    u32 code = (u32)rcn_call_guest(ctx, thread->start_fn, thread->start_arg, 0, 0, 0);
    // La fonction d'entrée du programme est revenue : son résultat est le code de sortie.
    if (thread->is_main) rcn_exit(ctx, code);
    rcn_thread_exit(ctx, code);
}

int rcn_thread_start(RcnThread *thread, u64 fn, u64 arg) {
    RcnProcess *proc = thread->ctx.proc;
    thread->start_fn = fn;
    thread->start_arg = arg;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, HOST_STACK_SIZE);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_mutex_lock(&proc->lock);
    proc->threads_alive++;
    pthread_mutex_unlock(&proc->lock);
    pthread_t id;
    int r = pthread_create(&id, &attr, thread_main, thread);
    pthread_attr_destroy(&attr);
    if (r != 0) {
        pthread_mutex_lock(&proc->lock);
        proc->threads_alive--;
        pthread_mutex_unlock(&proc->lock);
    }
    return r;
}

int rcn_run_module(const RcnModule *module, const char *exe_path, const RcnHost *host, RcnResult *result) {
    static const RcnHost default_host = {RCN_HOST_ABI, NULL, default_write, rcn_default_present, rcn_default_pad, rcn_default_audio_open};
    memset(result, 0, sizeof *result);
    if (!host) host = &default_host;
#if defined(__ANDROID__)
    // Pointeurs marqués de Bionic (octet haut 0xb4…) : le code x86 du jeu les manipule comme des adresses
    // ordinaires (comparaisons, bits hauts) ; marquage du tas désactivé (M_BIONIC_SET_HEAP_TAGGING_LEVEL,
    // M_HEAP_TAGGING_LEVEL_NONE ; Android 11+, sans effet ailleurs).
    if (!getenv("RCN_HEAP_TAGS")) mallopt(-204, 0);
#endif
    if (host->abi != RCN_HOST_ABI || module->abi != RCN_ABI_VERSION) {
        snprintf(result->error, sizeof result->error, "version de l'application incompatible avec ce jeu");
        return result->status = RCN_ERR_HOST;
    }
    RcnProcess *proc = calloc(1, sizeof *proc);
    if (!proc) {
        snprintf(result->error, sizeof result->error, "memoire insuffisante");
        return result->status = RCN_ERR_LOAD;
    }
    proc->module = module;
    proc->host = host;
    proc->result = result;
    proc->start_ns = rcn_now_ns();
    proc->next_thread_id = 1000;
    const char *trace = getenv("RCN_TRACE");
    // RCN_TRACE=1 : tout ; sinon une liste de mots qui choisit les fonctions notées.
    proc->trace = trace && strcmp(trace, "1") == 0;
    proc->trace_filter = trace && !proc->trace && *trace ? trace : NULL;
    setvbuf(stderr, NULL, _IONBF, 0); // messages de mise au point gardés même si le processus est tué
    if (getenv("RCN_HANG_DUMP")) {
        pthread_t watchdog;
        if (pthread_create(&watchdog, NULL, hang_dump, proc) == 0) pthread_detach(watchdog);
    }
    pthread_mutex_init(&proc->lock, NULL);
    pthread_cond_init(&proc->cond, NULL);
    int leaked = 0;
    if (setup_paths(proc, exe_path) != 0 || load_image(proc, exe_path) != 0) {
        result->status = RCN_ERR_LOAD;
        goto done;
    }
    RcnThread *thread = rcn_thread_new(proc, MAIN_STACK_SIZE);
    if (!thread) {
        snprintf(result->error, sizeof result->error,
                 proc->module->bits == 32 ? "pas de memoire libre sous 2 Go pour ce jeu 32 bits" : "memoire insuffisante");
        result->status = RCN_ERR_LOAD;
        goto done;
    }
    // Le jeu tourne sur ses propres threads ; celui-ci attend sa fin (ExitProcess, erreur, ou fin de
    // son dernier thread).
    thread->is_main = 1;
    if (rcn_thread_start(thread, (u64)(uintptr_t)proc->image + proc->module->entry_rva, 0) != 0) {
        thread_free(thread);
        snprintf(result->error, sizeof result->error, "creation du thread principal impossible");
        result->status = RCN_ERR_LOAD;
        goto done;
    }
    pthread_mutex_lock(&proc->lock);
    while (!proc->finished) pthread_cond_wait(&proc->cond, &proc->lock);
    // Des threads du jeu tournent encore (ExitProcess depuis l'un d'eux) : ils ne peuvent pas être
    // arrêtés proprement, la mémoire du jeu leur est laissée. Le processus hôte est censé se terminer.
    leaked = proc->threads_alive != 0;
    pthread_mutex_unlock(&proc->lock);
done:
    if (!leaked) {
        rcn_handles_free(proc);
        free(proc->gui);
        if (proc->module->bits == 32) {
            rcn_unmap_fixed(proc->image, proc->module->image_size);
            rcn_low_free(proc->imports);
            rcn_low_free(proc->command_line);
            rcn_low_free(proc->command_line_w);
        } else {
            rcn_page_free(proc->image);
            free(proc->imports);
            free(proc->command_line);
            free(proc->command_line_w);
        }
        free(proc->game_dir);
        free(proc->exe_name);
        free(proc->atexit_fns);
        pthread_mutex_destroy(&proc->lock);
        pthread_cond_destroy(&proc->cond);
        free(proc);
    }
    return result->status;
}