// RomCloud Native : exceptions matérielles d'un jeu 32 bits, comme sous Windows. Le code produit à
// l'exécution (traduction à la volée : code de Mono) vérifie avant chaque accès mémoire qu'il ne vise pas
// la page nulle ; s'il le fait, il appelle rcn_op_guest_fault avec ses registres rangés et l'adresse
// exacte de l'instruction. L'exception (EXCEPTION_ACCESS_VIOLATION) est alors remise aux gestionnaires du
// jeu : vectoriels (AddVectoredExceptionHandler), puis filtre des exceptions non gérées
// (SetUnhandledExceptionFilter). Mono y fait naître une NullReferenceException : son gestionnaire
// modifie le CONTEXT pour reprendre dans le bloc catch et demande de continuer — la reprise se fait par
// la remontée non locale du code traduit (RCN_UNWIND_RESUME).
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "rcn_internal.h"

#define MAX_VECTORED 16
static struct {
    pthread_mutex_t lock;
    u64 fn[MAX_VECTORED];
    u32 count;
    u64 filter; // SetUnhandledExceptionFilter
} handlers = {PTHREAD_MUTEX_INITIALIZER};

// AddVectoredExceptionHandler(premier, gestionnaire) : handle = adresse du gestionnaire (unique ici).
static void k32_AddVectoredExceptionHandler(RcnCtx *ctx) {
    pthread_mutex_lock(&handlers.lock);
    u64 fn = A1;
    if (fn && handlers.count < MAX_VECTORED) {
        if (A0) {
            memmove(&handlers.fn[1], &handlers.fn[0], handlers.count * sizeof handlers.fn[0]);
            handlers.fn[0] = fn;
        } else {
            handlers.fn[handlers.count] = fn;
        }
        handlers.count++;
    }
    pthread_mutex_unlock(&handlers.lock);
    RET(fn);
}
// RemoveVectoredExceptionHandler(handle)
static void k32_RemoveVectoredExceptionHandler(RcnCtx *ctx) {
    pthread_mutex_lock(&handlers.lock);
    u32 removed = 0;
    for (u32 i = 0; i < handlers.count; i++) {
        if (handlers.fn[i] != A0) continue;
        memmove(&handlers.fn[i], &handlers.fn[i + 1], (handlers.count - i - 1) * sizeof handlers.fn[0]);
        handlers.count--;
        removed = 1;
        break;
    }
    pthread_mutex_unlock(&handlers.lock);
    RET(removed);
}
// SetUnhandledExceptionFilter(filtre) : rend le précédent.
static void k32_SetUnhandledExceptionFilter(RcnCtx *ctx) {
    pthread_mutex_lock(&handlers.lock);
    u64 previous = handlers.filter;
    handlers.filter = A0;
    pthread_mutex_unlock(&handlers.lock);
    RET(previous);
}

// CONTEXT (x86, 0x2CC octets) : registres généraux et segments.
enum {
    C32_FLAGS = 0x00, C32_GS = 0x8C, C32_FS = 0x90, C32_ES = 0x94, C32_DS = 0x98, C32_EDI = 0x9C, C32_ESI = 0xA0,
    C32_EBX = 0xA4, C32_EDX = 0xA8, C32_ECX = 0xAC, C32_EAX = 0xB0, C32_EBP = 0xB4, C32_EIP = 0xB8, C32_CS = 0xBC,
    C32_EFLAGS = 0xC0, C32_ESP = 0xC4, C32_SS = 0xC8, C32_SIZE = 0x2CC,
};

// Appelé par le code traduit (accès à la page nulle). Ne revient qu'avec une reprise décidée
// (ctx->unwind) ; personne ne prend l'exception : arrêt du jeu.
void rcn_op_guest_fault(RcnCtx *ctx, u64 eip, u64 address, u32 write) {
    if (!ctx->bits32) rcn_fail(ctx, "acces a l'adresse 0x%llx (en 0x%llx)", (unsigned long long)address, (unsigned long long)eip);
    static int log = -1;
    if (log < 0) log = getenv("RCN_FAULT_LOG") != NULL;
    if (log) rcn_log("rcn : faute d'acces en 0x%llx (adresse 0x%llx, %s)", (unsigned long long)eip, (unsigned long long)address, write ? "ecriture" : "lecture");
    // EXCEPTION_RECORD (80 octets), CONTEXT, puis EXCEPTION_POINTERS : un bloc dans la mémoire du jeu.
    u8 *block = rcn_low_alloc(80 + C32_SIZE + 8, 1);
    if (!block) rcn_fail(ctx, "memoire insuffisante (exception)");
    u64 record = (u64)(uintptr_t)block, context = record + 80, pointers = context + C32_SIZE;
    wr32(record, 0xC0000005u); // EXCEPTION_ACCESS_VIOLATION
    wr32(record + 12, (u32)eip);
    wr32(record + 16, 2);
    wr32(record + 20, write);
    wr32(record + 24, (u32)address);
    wr32(context + C32_FLAGS, 0x10007); // CONTEXT_FULL
    wr32(context + C32_EDI, (u32)ctx->r[7]), wr32(context + C32_ESI, (u32)ctx->r[6]), wr32(context + C32_EBX, (u32)ctx->r[3]);
    wr32(context + C32_EDX, (u32)ctx->r[2]), wr32(context + C32_ECX, (u32)ctx->r[1]), wr32(context + C32_EAX, (u32)ctx->r[0]);
    wr32(context + C32_EBP, (u32)ctx->r[5]), wr32(context + C32_EIP, (u32)eip), wr32(context + C32_ESP, (u32)ctx->r[4]);
    wr32(context + C32_EFLAGS, rcn_get_flags(ctx));
    wr32(context + C32_CS, 0x23), wr32(context + C32_SS, 0x2B), wr32(context + C32_DS, 0x2B), wr32(context + C32_ES, 0x2B);
    wr32(context + C32_FS, 0x53), wr32(context + C32_GS, 0x2B);
    wr32(pointers, (u32)record);
    wr32(pointers + 4, (u32)context);
    pthread_mutex_lock(&handlers.lock);
    u64 list[MAX_VECTORED + 1];
    u32 n = handlers.count;
    memcpy(list, handlers.fn, n * sizeof list[0]);
    if (handlers.filter) list[n++] = handlers.filter;
    pthread_mutex_unlock(&handlers.lock);
    for (u32 i = 0; i < n; i++) {
        i32 r = (i32)(u32)rcn_call_guest(ctx, list[i], pointers, 0, 0, 0);
        // Le gestionnaire a lui-même repris ailleurs (restauration de contexte de Mono).
        if (ctx->unwind) return;
        if (r != -1) continue; // EXCEPTION_CONTINUE_SEARCH (vectoriel), EXECUTE_HANDLER (filtre)
        // EXCEPTION_CONTINUE_EXECUTION : reprise avec le CONTEXT, peut-être modifié.
        ctx->r[7] = rd32(context + C32_EDI), ctx->r[6] = rd32(context + C32_ESI), ctx->r[3] = rd32(context + C32_EBX);
        ctx->r[2] = rd32(context + C32_EDX), ctx->r[1] = rd32(context + C32_ECX), ctx->r[0] = rd32(context + C32_EAX);
        ctx->r[5] = rd32(context + C32_EBP);
        rcn_set_flags(ctx, rd32(context + C32_EFLAGS));
        ctx->resume_rsp = rd32(context + C32_ESP);
        ctx->resume_rip = rd32(context + C32_EIP);
        ctx->unwind = RCN_UNWIND_RESUME;
        if (log) rcn_log("rcn : reprise en 0x%llx (esp 0x%llx)", (unsigned long long)ctx->resume_rip, (unsigned long long)ctx->resume_rsp);
        return;
    }
    rcn_fail(ctx, "acces a l'adresse 0x%llx en 0x%llx, rattrape par aucun gestionnaire", (unsigned long long)address, (unsigned long long)eip);
}

const RcnShim rcn_fault32[] = {
    {"AddVectoredExceptionHandler", k32_AddVectoredExceptionHandler, S32(2)},
    {"RemoveVectoredExceptionHandler", k32_RemoveVectoredExceptionHandler, S32(1)},
    {"SetUnhandledExceptionFilter", k32_SetUnhandledExceptionFilter, S32(1)},
};
const size_t rcn_fault32_count = sizeof rcn_fault32 / sizeof rcn_fault32[0];

// Mise au point (RCN_RESUME_LOG=1) : chaque reprise non locale du code traduit (kind 0 : la fonction dont
// le cadre commence en `entry` reprend à resume_rip ; 1 : simple retour « à travers » de cette fonction).
void rcn_op_resume_note(RcnCtx *ctx, u64 entry, u32 kind) {
    static int log = -1;
    if (log < 0) log = getenv("RCN_RESUME_LOG") != NULL;
    if (!log) return;
    RcnLoaded *l = rcn_loaded_at(ctx->proc, ctx->resume_rip);
    rcn_log("rcn : [%u] %s : cadre 0x%llx, adresse 0x%llx%s%s, rsp 0x%llx", THREAD_ID(ctx), kind ? "retour a travers" : "reprise",
            (unsigned long long)entry, (unsigned long long)ctx->resume_rip, l ? " " : "", l ? l->name : "", (unsigned long long)ctx->resume_rsp);
}

// Mise au point (code traduit avec RCN_RECOMP_CALLCHECK=1) : l'appel direct de `site` vers `target` a
// rendu ebp changé (avant : `before`). Noté les 32 premières fois.
// RCN_CALLCHECK_SKIP=rva,rva… (hexadécimal, relatifs au module de la cible) : fonctions qui changent ebp
// par nature (__SEH_prolog4 / __SEH_epilog4 de Visual C++), ignorées.
void rcn_op_callcheck(RcnCtx *ctx, u64 site, u64 target, u64 before) {
    static int told;
    if (told >= 32) return;
    RcnLoaded *l = rcn_loaded_at(ctx->proc, site);
    u64 base = l ? (u64)(uintptr_t)l->image : 0;
    const char *skip = getenv("RCN_CALLCHECK_SKIP");
    for (const char *p = skip; p && *p;) {
        char *end;
        unsigned long long rva = strtoull(p, &end, 16);
        if (end == p) break;
        if (rva == target - base) return;
        p = *end == ',' ? end + 1 : end;
    }
    // Seulement les valeurs suspectes : hors de la pile (sous 0x10000, ou à plus de 16 Mo de esp). Les
    // fonctions d'aide de Visual C++ (__SEH_prolog4, _EH_prolog3…) changent ebp pour un cadre de la pile.
    u64 ebp = (u32)ctx->r[5], esp = (u32)ctx->r[4];
    if (ebp >= 0x10000 && (ebp > esp ? ebp - esp : esp - ebp) < ((u64)16 << 20)) return;
    told++;
    rcn_log("rcn : [%u] ebp change par l'appel %s+0x%llx -> +0x%llx : %llx -> %llx (esp %llx)", THREAD_ID(ctx), l ? l->name : "?",
            (unsigned long long)(site - base), (unsigned long long)(target - base), (unsigned long long)before,
            (unsigned long long)ctx->r[5], (unsigned long long)ctx->r[4]);
}

// Mise au point (code traduit avec RCN_RECOMP_CALLCHECK=1) : case de 4 octets surveillée (RCN_WATCH=adresse
// hexadécimale), lue avant chaque appel direct ; un appel qui la change est noté (les 64 premiers).
static u64 watch_address(void) {
    static u64 at = 1;
    if (at == 1) {
        const char *v = getenv("RCN_WATCH");
        at = v ? strtoull(v, NULL, 16) : 0;
    }
    return at;
}
u64 rcn_op_watch_get(RcnCtx *ctx) {
    (void)ctx;
    u64 at = watch_address();
    return at ? rd32(at) : 0;
}
void rcn_op_watch_check(RcnCtx *ctx, u64 site, u64 target, u64 before) {
    u64 at = watch_address();
    if (!at || rd32(at) == (u32)before) return;
    // RCN_WATCH_THREAD=n : seulement les appels de ce thread (celui à qui appartient la case).
    static long only = -2;
    if (only == -2) only = getenv("RCN_WATCH_THREAD") ? strtol(getenv("RCN_WATCH_THREAD"), NULL, 10) : -1;
    if (only >= 0 && (long)THREAD_ID(ctx) != only) return;
    static int told;
    if (told++ >= 64) return;
    RcnLoaded *l = rcn_loaded_at(ctx->proc, site);
    u64 base = l ? (u64)(uintptr_t)l->image : 0;
    rcn_log("rcn : [%u] case %llx changee par l'appel %s+0x%llx -> +0x%llx : %llx -> %x (esp %llx)", THREAD_ID(ctx),
            (unsigned long long)at, l ? l->name : "?", (unsigned long long)(site - base), (unsigned long long)(target - base),
            (unsigned long long)before, rd32(at), (unsigned long long)ctx->r[4]);
}

// Mise au point (code traduit avec RCN_RECOMP_WATCH=adresse) : écriture du code traduit sur la case
// surveillée (thread, instruction, adresse visée). Les 64 premières.
// Les 32 dernières écritures sont gardées (anneau) et données par rcn_store_hits_dump, au plantage.
#define HITS 32
static struct {
    u32 thread;
    u64 eip, esp, ebp, value_before;
} hits[HITS];
static u32 hit_next;
void rcn_op_store_hit(RcnCtx *ctx, u64 eip, u64 addr) {
    u32 k = __atomic_fetch_add(&hit_next, 1, __ATOMIC_RELAXED) % HITS;
    hits[k].thread = THREAD_ID(ctx), hits[k].eip = eip, hits[k].esp = ctx->r[4], hits[k].ebp = ctx->r[5];
    hits[k].value_before = rd32(addr & ~3ull);
}
void rcn_store_hits_dump(RcnCtx *ctx) {
    u32 n = hit_next < HITS ? hit_next : HITS;
    for (u32 i = 0; i < n; i++) {
        u32 k = (hit_next - n + i) % HITS;
        RcnLoaded *l = ctx ? rcn_loaded_at(ctx->proc, hits[k].eip) : NULL;
        rcn_log("  ecriture %u : [%u] %s+0x%llx (esp %llx, ebp %llx, avant %llx)", i, hits[k].thread, l ? l->name : "code",
                (unsigned long long)(hits[k].eip - (l ? (u64)(uintptr_t)l->image : 0)), (unsigned long long)hits[k].esp,
                (unsigned long long)hits[k].ebp, (unsigned long long)hits[k].value_before);
    }
}
