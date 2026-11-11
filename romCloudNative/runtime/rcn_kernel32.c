// RomCloud Native : fonctions de kernel32 fournies par l'hôte (console, tas, mémoire, temps).
// Chaque fonction lit ses arguments dans les registres du jeu (A0 … A3, STK) et rend son résultat
// dans rax (RET) : voir rcn_internal.h.
#include <stdlib.h>

#include "rcn_internal.h"

#if defined(_WIN32)
__declspec(dllimport) void __stdcall Sleep(unsigned long ms);
static void sleep_ms(u32 ms) { Sleep(ms); }
#else
#include <time.h>
static void sleep_ms(u32 ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
#endif

enum {
    HANDLE_STDIN = 0x10,
    HANDLE_STDOUT = 0x11,
    HANDLE_STDERR = 0x12,
    HANDLE_HEAP = 0x20,
    ERROR_INVALID_HANDLE = 6,
    HEAP_ZERO_MEMORY = 8,
    MEM_COMMIT = 0x1000,
    MEM_RESERVE = 0x2000,
    MEM_RELEASE = 0x8000,
    MEM_PRIVATE = 0x20000,
    PAGE_EXECUTE_READWRITE = 0x40,
};

static void k32_GetStdHandle(RcnCtx *ctx) {
    switch ((u32)A0) {
    case (u32)-10: RET(HANDLE_STDIN); break;
    case (u32)-11: RET(HANDLE_STDOUT); break;
    case (u32)-12: RET(HANDLE_STDERR); break;
    default: RET(~0ull); break;
    }
}

// WriteFile(handle, buffer, length, &written, overlapped) : console seulement pour l'instant.
static void k32_WriteFile(RcnCtx *ctx) {
    u64 handle = A0;
    u32 len = (u32)A2;
    if (handle != HANDLE_STDOUT && handle != HANDLE_STDERR) {
        ctx->proc->last_error = ERROR_INVALID_HANDLE;
        RET(0);
        return;
    }
    rcn_host_write(ctx, handle == HANDLE_STDERR ? RCN_STREAM_STDERR : RCN_STREAM_STDOUT, PTR(A1), len);
    if (A3) wr32(A3, len);
    RET(1);
}

static void k32_ExitProcess(RcnCtx *ctx) { rcn_exit(ctx, (u32)A0); }

static void k32_GetProcessHeap(RcnCtx *ctx) { RET(HANDLE_HEAP); }
// HeapAlloc(heap, flags, size)
static void k32_HeapAlloc(RcnCtx *ctx) {
    size_t size = A2 ? (size_t)A2 : 1;
    RET((uintptr_t)((u32)A1 & HEAP_ZERO_MEMORY ? calloc(1, size) : malloc(size)));
}
// HeapReAlloc(heap, flags, pointer, size) : sans HEAP_ZERO_MEMORY pour la partie ajoutée.
static void k32_HeapReAlloc(RcnCtx *ctx) { RET((uintptr_t)realloc(PTR(A2), A3 ? (size_t)A3 : 1)); }
static void k32_HeapFree(RcnCtx *ctx) {
    free(PTR(A2));
    RET(1);
}

// VirtualAlloc(address, size, type, protection) : la mémoire est réservée et engagée d'un coup ;
// engager une zone déjà réservée (adresse donnée) ne fait rien, réserver à une adresse imposée échoue.
static void k32_VirtualAlloc(RcnCtx *ctx) {
    if (A0) {
        RET(((u32)A2 & (MEM_COMMIT | MEM_RESERVE)) == MEM_COMMIT ? A0 : 0);
        return;
    }
    RET((uintptr_t)rcn_page_alloc(A1 ? (size_t)A1 : 1));
}
// VirtualFree(address, size, type) : seule la libération complète rend la mémoire.
static void k32_VirtualFree(RcnCtx *ctx) {
    if ((u32)A2 & MEM_RELEASE) rcn_page_free(PTR(A0));
    RET(1);
}
// VirtualProtect(address, size, protection, &old) : la mémoire du jeu est toujours lisible et modifiable.
static void k32_VirtualProtect(RcnCtx *ctx) {
    if (A3) wr32(A3, PAGE_EXECUTE_READWRITE);
    RET(1);
}
// VirtualQuery(address, &info, length) : MEMORY_BASIC_INFORMATION d'une page de l'image ou du tas.
static void k32_VirtualQuery(RcnCtx *ctx) {
    u64 info = A1;
    if (A2 < 48) {
        RET(0);
        return;
    }
    u64 page = A0 & ~0xFFFull;
    int in_image = A0 - ctx->base < ctx->image_size;
    memset(PTR(info), 0, 48);
    wr64(info + 0, page);                                  // BaseAddress
    wr64(info + 8, in_image ? ctx->base : page);           // AllocationBase
    wr32(info + 16, PAGE_EXECUTE_READWRITE);               // AllocationProtect
    wr64(info + 24, in_image ? ctx->base + ctx->image_size - page : 0x1000); // RegionSize
    wr32(info + 32, MEM_COMMIT);                           // State
    wr32(info + 36, PAGE_EXECUTE_READWRITE);               // Protect
    wr32(info + 40, MEM_PRIVATE);                          // Type
    RET(48);
}

static void k32_GetLastError(RcnCtx *ctx) { RET(ctx->proc->last_error); }
static void k32_SetLastError(RcnCtx *ctx) { ctx->proc->last_error = (u32)A0; }

static void k32_GetTickCount(RcnCtx *ctx) { RET((u32)(rcn_now_ns() / 1000000)); }
static void k32_GetTickCount64(RcnCtx *ctx) { RET(rcn_now_ns() / 1000000); }
static void k32_QueryPerformanceCounter(RcnCtx *ctx) {
    wr64(A0, rcn_now_ns());
    RET(1);
}
static void k32_QueryPerformanceFrequency(RcnCtx *ctx) {
    wr64(A0, 1000000000ull);
    RET(1);
}
static void k32_Sleep(RcnCtx *ctx) { sleep_ms((u32)A0); }

static void k32_GetCurrentProcessId(RcnCtx *ctx) { RET(1000); }
static void k32_GetCurrentThreadId(RcnCtx *ctx) { RET(1001); }
static void k32_IsDebuggerPresent(RcnCtx *ctx) { RET(0); }
// Aucun filtre d'exceptions n'est appelé : une faute du jeu arrête son processus.
static void k32_SetUnhandledExceptionFilter(RcnCtx *ctx) { RET(0); }
// GetModuleHandleA / W(NULL) : l'exécutable lui-même ; les autres modules sont inconnus.
static void k32_GetModuleHandle(RcnCtx *ctx) { RET(A0 ? 0 : ctx->base); }

const RcnShim rcn_kernel32[] = {
    {"ExitProcess", k32_ExitProcess},
    {"GetCurrentProcessId", k32_GetCurrentProcessId},
    {"GetCurrentThreadId", k32_GetCurrentThreadId},
    {"GetLastError", k32_GetLastError},
    {"GetModuleHandleA", k32_GetModuleHandle},
    {"GetModuleHandleW", k32_GetModuleHandle},
    {"GetProcessHeap", k32_GetProcessHeap},
    {"GetStdHandle", k32_GetStdHandle},
    {"GetTickCount", k32_GetTickCount},
    {"GetTickCount64", k32_GetTickCount64},
    {"HeapAlloc", k32_HeapAlloc},
    {"HeapFree", k32_HeapFree},
    {"HeapReAlloc", k32_HeapReAlloc},
    {"IsDebuggerPresent", k32_IsDebuggerPresent},
    {"QueryPerformanceCounter", k32_QueryPerformanceCounter},
    {"QueryPerformanceFrequency", k32_QueryPerformanceFrequency},
    {"SetLastError", k32_SetLastError},
    {"SetUnhandledExceptionFilter", k32_SetUnhandledExceptionFilter},
    {"Sleep", k32_Sleep},
    {"VirtualAlloc", k32_VirtualAlloc},
    {"VirtualFree", k32_VirtualFree},
    {"VirtualProtect", k32_VirtualProtect},
    {"VirtualQuery", k32_VirtualQuery},
    {"WriteFile", k32_WriteFile},
};
const size_t rcn_kernel32_count = sizeof rcn_kernel32 / sizeof rcn_kernel32[0];
