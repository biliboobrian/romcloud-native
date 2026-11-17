// RomCloud Native : mise au point sur PC seulement (hôte Windows, RCN_NATIVE_SYNC=1) — les verrous légers
// (SRWLOCK), variables de condition et attentes sur adresse du jeu confiés aux vraies fonctions de Windows.
// La mémoire du jeu est celle de l'hôte et ces objets n'y sont qu'un mot : le résultat dit si un blocage
// vient de leur imitation par le runtime (rcn_k32_sync.c, rcn_k32_x64b.c) ou d'ailleurs.
#include "rcn_internal.h"

#if defined(_WIN32)
#include <stdlib.h>

#define WINAPI __stdcall
__declspec(dllimport) void WINAPI AcquireSRWLockExclusive(void *lock);
__declspec(dllimport) void WINAPI AcquireSRWLockShared(void *lock);
__declspec(dllimport) void WINAPI ReleaseSRWLockExclusive(void *lock);
__declspec(dllimport) void WINAPI ReleaseSRWLockShared(void *lock);
__declspec(dllimport) unsigned char WINAPI TryAcquireSRWLockExclusive(void *lock);
__declspec(dllimport) unsigned char WINAPI TryAcquireSRWLockShared(void *lock);
__declspec(dllimport) int WINAPI SleepConditionVariableSRW(void *cv, void *lock, unsigned long ms, unsigned long flags);
__declspec(dllimport) void WINAPI WakeConditionVariable(void *cv);
__declspec(dllimport) void WINAPI WakeAllConditionVariable(void *cv);
__declspec(dllimport) unsigned long WINAPI GetLastError(void);
__declspec(dllimport) void *WINAPI GetModuleHandleA(const char *name);
__declspec(dllimport) void *WINAPI GetProcAddress(void *module, const char *name);

typedef int(WINAPI *WaitOnAddressFn)(volatile void *, void *, size_t, unsigned long);
typedef void(WINAPI *WakeFn)(void *);
static WaitOnAddressFn wait_on_address;
static WakeFn wake_single, wake_all;
static void load_address_functions(void) {
    if (wait_on_address) return;
    void *base = GetModuleHandleA("kernelbase.dll");
    wait_on_address = (WaitOnAddressFn)GetProcAddress(base, "WaitOnAddress");
    wake_single = (WakeFn)GetProcAddress(base, "WakeByAddressSingle");
    wake_all = (WakeFn)GetProcAddress(base, "WakeByAddressAll");
}

static void n_Init(RcnCtx *ctx) { wr64(A0, 0); }
static void n_AcquireExclusive(RcnCtx *ctx) { AcquireSRWLockExclusive(PTR(A0)); }
static void n_AcquireShared(RcnCtx *ctx) { AcquireSRWLockShared(PTR(A0)); }
static void n_ReleaseExclusive(RcnCtx *ctx) { ReleaseSRWLockExclusive(PTR(A0)); }
static void n_ReleaseShared(RcnCtx *ctx) { ReleaseSRWLockShared(PTR(A0)); }
static void n_TryExclusive(RcnCtx *ctx) { RET(TryAcquireSRWLockExclusive(PTR(A0))); }
static void n_TryShared(RcnCtx *ctx) { RET(TryAcquireSRWLockShared(PTR(A0))); }
static void n_SleepCV(RcnCtx *ctx) {
    int ok = SleepConditionVariableSRW(PTR(A0), PTR(A1), (unsigned long)(u32)A2, (unsigned long)(u32)A3);
    if (!ok) LAST_ERROR(ctx) = (u32)GetLastError();
    RET(ok);
}
static void n_WakeCV(RcnCtx *ctx) { WakeConditionVariable(PTR(A0)); }
static void n_WakeAllCV(RcnCtx *ctx) { WakeAllConditionVariable(PTR(A0)); }
static void n_WaitOnAddress(RcnCtx *ctx) {
    load_address_functions();
    int ok = wait_on_address(PTR(A0), PTR(A1), (size_t)A2, (unsigned long)(u32)A3);
    if (!ok) LAST_ERROR(ctx) = (u32)GetLastError();
    RET(ok);
}
static void n_WakeSingle(RcnCtx *ctx) {
    load_address_functions();
    wake_single(PTR(A0));
}
static void n_WakeAll(RcnCtx *ctx) {
    load_address_functions();
    wake_all(PTR(A0));
}

static const RcnShim native_sync[] = {
    {"InitializeSRWLock", n_Init, 0},
    {"AcquireSRWLockExclusive", n_AcquireExclusive, 0},
    {"AcquireSRWLockShared", n_AcquireShared, 0},
    {"ReleaseSRWLockExclusive", n_ReleaseExclusive, 0},
    {"ReleaseSRWLockShared", n_ReleaseShared, 0},
    {"TryAcquireSRWLockExclusive", n_TryExclusive, 0},
    {"TryAcquireSRWLockShared", n_TryShared, 0},
    {"InitializeConditionVariable", n_Init, 0},
    {"SleepConditionVariableSRW", n_SleepCV, 0},
    {"WakeConditionVariable", n_WakeCV, 0},
    {"WakeAllConditionVariable", n_WakeAllCV, 0},
    {"WaitOnAddress", n_WaitOnAddress, 0},
    {"WakeByAddressSingle", n_WakeSingle, 0},
    {"WakeByAddressAll", n_WakeAll, 0},
};

RcnFn rcn_native_sync_lookup(const char *name) {
    static int on = -1;
    if (on < 0) on = getenv("RCN_NATIVE_SYNC") != NULL;
    if (!on) return NULL;
    for (size_t i = 0; i < sizeof native_sync / sizeof native_sync[0]; i++) {
        if (strcmp(native_sync[i].name, name) == 0) return native_sync[i].fn;
    }
    return NULL;
}
#else
RcnFn rcn_native_sync_lookup(const char *name) {
    (void)name;
    return NULL;
}
#endif
