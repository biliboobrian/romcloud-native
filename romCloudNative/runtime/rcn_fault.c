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
