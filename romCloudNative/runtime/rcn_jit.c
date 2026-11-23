// RomCloud Native : traduction à la volée du code que le jeu produit pendant qu'il tourne (méthodes C#
// compilées par Mono pour Unity…). Quand le code traduit appelle une adresse qui n'appartient à aucun
// module, mais à de la mémoire du jeu, le bloc alloué qui la contient est copié et traduit par le
// recompilateur (librcn_recomp, celui de l'application) comme une petite image d'une seule section, depuis
// cette adresse : les fonctions qu'elle appelle dans le bloc sont traduites avec elle, les appels qui en
// sortent passent par rcn_call. Le .rcnx obtenu est chargé comme celui d'une DLL, à l'adresse du bloc.
//
// Le code déjà traduit n'est pas retraduit si le jeu le réécrit ensuite (Mono corrige ses sites d'appel
// une fois la méthode visée compilée : l'ancien chemin, par le trampoline, reste valable).
// RCN_JIT=0 : traduction à la volée désactivée. RCN_JIT_LOG=1 : chaque traduction notée.
// RCN_JIT_DUMP=1 : le bloc traduit est écrit (jit-<n>-<base>-<entrée>.bin dans le dossier du code), tel
// que traduit, et rcn_jit_dump l'écrit de nouveau à la demande (appel inconnu depuis ce code).
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rcn_internal.h"

#if defined(_WIN32)
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *name);
__declspec(dllimport) void *__stdcall GetProcAddress(void *module, const char *name);
#else
#include <dlfcn.h>
#endif

typedef int (*RecompileCodeFn)(const u8 *code, size_t size, u64 base, u32 bits, const u32 *entries, u32 count,
                               const u32 *done, u32 done_count, const char *output, char *message, size_t message_size);

// Recompilateur : librcn_recomp.so (Android, bibliothèque de l'application), rcn_recomp.dll (PC, à côté du
// lanceur ou désignée par RCN_RECOMP_LIB).
static RecompileCodeFn recompiler(void) {
    static RecompileCodeFn fn;
    static int tried;
    if (tried) return fn;
    tried = 1;
    const char *name = getenv("RCN_RECOMP_LIB");
#if defined(_WIN32)
    void *lib = LoadLibraryA(name ? name : "rcn_recomp.dll");
    if (lib) fn = (RecompileCodeFn)GetProcAddress(lib, "rcn_recompile_code");
#else
    void *lib = dlopen(name ? name : "librcn_recomp.so", RTLD_NOW | RTLD_LOCAL);
    if (lib) fn = (RecompileCodeFn)dlsym(lib, "rcn_recompile_code");
#endif
    if (!fn) rcn_log("rcn : traduction a la volee impossible : recompilateur introuvable (%s)", name ? name : "librcn_recomp");
    return fn;
}

// Fonctions traduites à la volée, par adresse (adressage ouvert, agrandi au besoin).
typedef struct {
    u64 address;
    RcnFn fn;
} JitEntry;
static struct {
    pthread_mutex_t lock;
    JitEntry *table;
    u32 cap, count;
    u32 translations;
    u64 bytes;
} jit = {PTHREAD_MUTEX_INITIALIZER};

static u32 jit_slot(u64 address, u32 cap) { return (u32)((address * 0x9E3779B97F4A7C15ull) >> 32) & (cap - 1); }
static RcnFn jit_find_locked(u64 address) {
    if (!jit.cap) return NULL;
    for (u32 i = jit_slot(address, jit.cap);; i = (i + 1) & (jit.cap - 1)) {
        if (!jit.table[i].address) return NULL;
        if (jit.table[i].address == address) return jit.table[i].fn;
    }
}
static void jit_insert_locked(u64 address, RcnFn fn) {
    if ((jit.count + 1) * 2 > jit.cap) {
        u32 cap = jit.cap ? jit.cap * 2 : 4096;
        JitEntry *table = calloc(cap, sizeof *table);
        if (!table) return;
        for (u32 i = 0; i < jit.cap; i++) {
            if (!jit.table[i].address) continue;
            u32 k = jit_slot(jit.table[i].address, cap);
            while (table[k].address) k = (k + 1) & (cap - 1);
            table[k] = jit.table[i];
        }
        free(jit.table);
        jit.table = table, jit.cap = cap;
    }
    u32 k = jit_slot(address, jit.cap);
    while (jit.table[k].address && jit.table[k].address != address) k = (k + 1) & (jit.cap - 1);
    if (!jit.table[k].address) jit.count++;
    jit.table[k].address = address, jit.table[k].fn = fn;
}

// Bloc de mémoire du jeu qui contient `address` : [*start, *start + *size). 0 si ce n'en est pas.
static int jit_block(RcnCtx *ctx, u64 address, u64 *start, u64 *size) {
    if (ctx->bits32) {
        size_t n = 0;
        void *at = NULL;
        if (!rcn_low_block_range((void *)(uintptr_t)address, &at, &n)) return 0;
        *start = (u64)(uintptr_t)at, *size = n;
        return 1;
    }
    u8 info[48];
    if (!rcn_vm_query(address, (u64)(uintptr_t)info)) return 0;
    u64 base, alloc, region;
    memcpy(&base, info, 8), memcpy(&alloc, info + 8, 8), memcpy(&region, info + 24, 8);
    u32 state;
    memcpy(&state, info + 32, 4);
    if (state != 0x1000 || !alloc) return 0; // MEM_COMMIT
    *start = alloc, *size = base + region - alloc;
    return 1;
}

RcnFn rcn_jit_function(RcnCtx *ctx, u64 target) {
    static int enabled = -1, log = -1;
    if (enabled < 0) enabled = !getenv("RCN_JIT") || strcmp(getenv("RCN_JIT"), "0") != 0;
    if (log < 0) log = getenv("RCN_JIT_LOG") != NULL;
    if (!enabled) return NULL;
    pthread_mutex_lock(&jit.lock);
    RcnFn fn = jit_find_locked(target);
    if (fn) {
        pthread_mutex_unlock(&jit.lock);
        return fn;
    }
    u64 start = 0, size = 0;
    RecompileCodeFn recompile = recompiler();
    if (!recompile || !jit_block(ctx, target, &start, &size) || target < start || target >= start + size || size > ((u64)256 << 20)) {
        pthread_mutex_unlock(&jit.lock);
        return NULL;
    }
    // Copie du bloc : le jeu peut l'écrire pendant la traduction (autres threads).
    u8 *copy = malloc((size_t)size);
    const char *dir = rcn_code_dir();
    char path[1024];
    snprintf(path, sizeof path, "%s/jit-%llx.rcnx", dir ? dir : ".", (unsigned long long)target);
    if (!copy) {
        pthread_mutex_unlock(&jit.lock);
        return NULL;
    }
    memcpy(copy, (void *)(uintptr_t)start, (size_t)size);
    u32 entry = (u32)(target - start);
    if (getenv("RCN_JIT_DUMP")) {
        char dump[1100];
        snprintf(dump, sizeof dump, "%s/jit-%u-%llx-%llx.bin", dir ? dir : ".", jit.translations, (unsigned long long)start,
                 (unsigned long long)target);
        FILE *f = fopen(dump, "wb");
        if (f) fwrite(copy, 1, (size_t)size, f), fclose(f);
    }
    // Fonctions du bloc déjà traduites : ni décodées ni traduites de nouveau, appelées par leur adresse
    // (sinon chaque traduction refait toutes les méthodes que la nouvelle atteint).
    u32 *done = malloc(((size_t)jit.count + 1) * sizeof *done), done_count = 0;
    for (u32 i = 0; done && i < jit.cap; i++) {
        u64 a = jit.table[i].address;
        if (a && a >= start && a < start + size) done[done_count++] = (u32)(a - start);
    }
    char message[512];
    int status = recompile(copy, (size_t)size, start, ctx->bits32 ? 32 : 64, &entry, 1, done, done_count, path, message, sizeof message);
    free(done);
    free(copy);
    if (status != 0) {
        rcn_log("rcn : traduction a la volee de 0x%llx impossible : %s", (unsigned long long)target, message);
        pthread_mutex_unlock(&jit.lock);
        return NULL;
    }
    char error[256];
    const RcnModule *m = rcn_load_rcnx(path, error, sizeof error);
    remove(path);
    if (!m) {
        rcn_log("rcn : code traduit a la volee illisible (0x%llx) : %s", (unsigned long long)target, error);
        pthread_mutex_unlock(&jit.lock);
        return NULL;
    }
    ((RcnModule *)m)->base_slot = start;
    // Toutes les fonctions découvertes, pas seulement l'entrée : leurs adresses sont désormais connues.
    for (u32 i = 0; i < m->func_count; i++) {
        u64 address = start + m->funcs[i].rva;
        if (!jit_find_locked(address)) jit_insert_locked(address, m->funcs[i].fn);
    }
    fn = jit_find_locked(target);
    jit.translations++;
    jit.bytes += size;
    if (log)
        rcn_log("rcn : traduit a la volee : 0x%llx (bloc 0x%llx, %llu octets, %u fonctions ; %u traductions)", (unsigned long long)target,
                (unsigned long long)start, (unsigned long long)size, m->func_count, jit.translations);
    pthread_mutex_unlock(&jit.lock);
    return fn;
}

// Mise au point (RCN_JIT_DUMP) : bloc de mémoire du jeu qui contient `address`, écrit tel qu'il est
// maintenant (jit-now-<base>-<adresse>.bin).
void rcn_jit_dump(RcnCtx *ctx, u64 address) {
    if (!getenv("RCN_JIT_DUMP")) return;
    u64 start = 0, size = 0;
    if (!jit_block(ctx, address, &start, &size)) return;
    const char *dir = rcn_code_dir();
    char dump[1100];
    snprintf(dump, sizeof dump, "%s/jit-now-%llx-%llx.bin", dir ? dir : ".", (unsigned long long)start, (unsigned long long)address);
    FILE *f = fopen(dump, "wb");
    if (f) fwrite((void *)(uintptr_t)start, 1, (size_t)size, f), fclose(f);
    rcn_log("rcn : bloc 0x%llx ecrit dans %s", (unsigned long long)start, dump);
}
