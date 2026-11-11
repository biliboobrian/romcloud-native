// RomCloud Native : kernel32, généralités — processus, tas, mémoire, temps, texte.
// Fichiers : rcn_k32_file.c ; threads et synchronisation : rcn_k32_sync.c.
// Chaque fonction lit ses arguments dans les registres du jeu (A0 … A3, STK) et rend son résultat
// dans rax (RET) : voir rcn_internal.h.
#include <stdlib.h>
#include <time.h>

#include "rcn_internal.h"

#if defined(_WIN32)
__declspec(dllimport) void __stdcall Sleep(unsigned long ms);
static void sleep_ms(u32 ms) { Sleep(ms); }
#else
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
    HEAP_ZERO_MEMORY = 8,
    MEM_COMMIT = 0x1000,
    MEM_RESERVE = 0x2000,
    MEM_RELEASE = 0x8000,
    MEM_PRIVATE = 0x20000,
    PAGE_EXECUTE_READWRITE = 0x40,
    ERROR_INSUFFICIENT_BUFFER = 122,
};

// --- Processus ---

static void k32_GetStdHandle(RcnCtx *ctx) {
    switch ((u32)A0) {
    case (u32)-10: RET(HANDLE_STDIN); break;
    case (u32)-11: RET(HANDLE_STDOUT); break;
    case (u32)-12: RET(HANDLE_STDERR); break;
    default: RET(~0ull); break;
    }
}
static void k32_ExitProcess(RcnCtx *ctx) { rcn_exit(ctx, (u32)A0); }
static void k32_GetLastError(RcnCtx *ctx) { RET(LAST_ERROR(ctx)); }
static void k32_SetLastError(RcnCtx *ctx) { LAST_ERROR(ctx) = (u32)A0; }
static void k32_GetCurrentProcessId(RcnCtx *ctx) { RET(1000); }
static void k32_IsDebuggerPresent(RcnCtx *ctx) { RET(0); }
// Aucun filtre d'exceptions n'est appelé : une faute du jeu arrête son processus.
static void k32_SetUnhandledExceptionFilter(RcnCtx *ctx) { RET(0); }
// GetModuleHandleA / W(NULL) : l'exécutable lui-même ; les autres modules sont inconnus.
static void k32_GetModuleHandle(RcnCtx *ctx) { RET(A0 ? 0 : ctx->base); }
// OutputDebugStringA : vers la sortie d'erreur.
static void k32_OutputDebugStringA(RcnCtx *ctx) {
    if (A0) rcn_host_write(ctx, RCN_STREAM_STDERR, STR(A0), strlen(STR(A0)));
}

// Le dossier du jeu est vu par lui comme « C:\ » : son exécutable est « C:\<nom>.exe ».
static char *exe_path(RcnProcess *proc) {
    size_t n = strlen(proc->exe_name);
    char *path = malloc(n + 4);
    if (path) {
        memcpy(path, "C:\\", 3);
        memcpy(path + 3, proc->exe_name, n + 1);
    }
    return path;
}
// Copie un texte dans un tampon du jeu de `size` caractères (octets, ou unités UTF-16 si wide) ;
// renvoie le nombre de caractères copiés, zéro final non compris (texte tronqué s'il ne tient pas).
static u32 copy_text(RcnCtx *ctx, const char *text, u64 buffer, u32 size, int wide) {
    if (!text || !size) return 0;
    size_t len = strlen(text);
    if (!wide) {
        u32 n = len < size ? (u32)len : size - 1;
        memcpy(PTR(buffer), text, n);
        wr8(buffer + n, 0);
        if (len >= size) LAST_ERROR(ctx) = ERROR_INSUFFICIENT_BUFFER;
        return n;
    }
    size_t count = rcn_utf8_to_utf16(text, len, NULL);
    u16 *wide_text = malloc((count + 1) * 2);
    if (!wide_text) return 0;
    rcn_utf8_to_utf16(text, len, wide_text);
    u32 n = count < size ? (u32)count : size - 1;
    memcpy(PTR(buffer), wide_text, (size_t)n * 2);
    wr16(buffer + (u64)n * 2, 0);
    free(wide_text);
    if (count >= size) LAST_ERROR(ctx) = ERROR_INSUFFICIENT_BUFFER;
    return n;
}
// GetModuleFileNameA / W(module, buffer, size)
static void module_file_name(RcnCtx *ctx, int wide) {
    char *path = exe_path(ctx->proc);
    RET(copy_text(ctx, path, A1, (u32)A2, wide));
    free(path);
}
static void k32_GetModuleFileNameA(RcnCtx *ctx) { module_file_name(ctx, 0); }
static void k32_GetModuleFileNameW(RcnCtx *ctx) { module_file_name(ctx, 1); }
// GetCurrentDirectoryA / W(size, buffer)
static void k32_GetCurrentDirectoryA(RcnCtx *ctx) { RET((u32)A0 < 4 ? 4 : copy_text(ctx, "C:\\", A1, (u32)A0, 0)); }
static void k32_GetCurrentDirectoryW(RcnCtx *ctx) { RET((u32)A0 < 4 ? 4 : copy_text(ctx, "C:\\", A1, (u32)A0, 1)); }

// Ligne de commande : le chemin de l'exécutable entre guillemets, sans argument.
static void make_command_line(RcnProcess *proc) {
    pthread_mutex_lock(&proc->lock);
    if (!proc->command_line) {
        char *path = exe_path(proc);
        size_t n = path ? strlen(path) : 0;
        char *line = malloc(n + 3);
        u16 *wide = malloc((n * 2 + 3) * 2);
        if (path && line && wide) {
            line[0] = '"';
            memcpy(line + 1, path, n);
            line[n + 1] = '"';
            line[n + 2] = 0;
            wide[rcn_utf8_to_utf16(line, n + 2, wide)] = 0;
            proc->command_line = line;
            proc->command_line_w = wide;
        } else {
            free(line);
            free(wide);
        }
        free(path);
    }
    pthread_mutex_unlock(&proc->lock);
}
static void k32_GetCommandLineA(RcnCtx *ctx) {
    make_command_line(ctx->proc);
    RET((uintptr_t)ctx->proc->command_line);
}
static void k32_GetCommandLineW(RcnCtx *ctx) {
    make_command_line(ctx->proc);
    RET((uintptr_t)ctx->proc->command_line_w);
}

// --- Tas et mémoire ---

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

// --- Temps ---

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
// GetSystemTimeAsFileTime(&filetime) : centaines de nanosecondes depuis le 1er janvier 1601.
static void k32_GetSystemTimeAsFileTime(RcnCtx *ctx) {
    wr64(A0, ((u64)time(NULL) + 11644473600ull) * 10000000ull);
}

// --- Texte ---
// Toutes les pages de codes sont traitées comme de l'UTF-8 (identique pour l'ASCII).

static void k32_lstrlenA(RcnCtx *ctx) { RET(A0 ? strlen(STR(A0)) : 0); }
static void k32_lstrlenW(RcnCtx *ctx) {
    const u16 *s = PTR(A0);
    u32 n = 0;
    while (s && s[n]) n++;
    RET(n);
}
// MultiByteToWideChar(code_page, flags, source, source_length, destination, destination_size)
// Longueur -1 : jusqu'au zéro final, compris dans le résultat. Taille 0 : seulement compter.
static void k32_MultiByteToWideChar(RcnCtx *ctx) {
    const char *source = STR(A2);
    i32 source_length = (i32)A3;
    u64 destination = STK(4);
    u32 size = (u32)STK(5);
    size_t len = source_length < 0 ? strlen(source) + 1 : (size_t)source_length;
    size_t count = rcn_utf8_to_utf16(source, len, NULL);
    if (size == 0) {
        RET(count);
        return;
    }
    if (count > size) {
        LAST_ERROR(ctx) = ERROR_INSUFFICIENT_BUFFER;
        RET(0);
        return;
    }
    rcn_utf8_to_utf16(source, len, PTR(destination));
    RET(count);
}
// WideCharToMultiByte(code_page, flags, source, source_length, destination, destination_size, default, &used)
static void k32_WideCharToMultiByte(RcnCtx *ctx) {
    const u16 *source = PTR(A2);
    i32 source_length = (i32)A3;
    u64 destination = STK(4);
    u32 size = (u32)STK(5);
    if (STK(7)) wr32(STK(7), 0);
    char *text = rcn_utf16_to_utf8(source, source_length);
    if (!text) {
        RET(0);
        return;
    }
    size_t count = strlen(text) + (source_length < 0 ? 1 : 0);
    if (size == 0) {
        RET(count);
    } else if (count > size) {
        LAST_ERROR(ctx) = ERROR_INSUFFICIENT_BUFFER;
        RET(0);
    } else {
        memcpy(PTR(destination), text, count);
        RET(count);
    }
    free(text);
}

const RcnShim rcn_kernel32[] = {
    {"ExitProcess", k32_ExitProcess},
    {"GetCommandLineA", k32_GetCommandLineA},
    {"GetCommandLineW", k32_GetCommandLineW},
    {"GetCurrentDirectoryA", k32_GetCurrentDirectoryA},
    {"GetCurrentDirectoryW", k32_GetCurrentDirectoryW},
    {"GetCurrentProcessId", k32_GetCurrentProcessId},
    {"GetLastError", k32_GetLastError},
    {"GetModuleFileNameA", k32_GetModuleFileNameA},
    {"GetModuleFileNameW", k32_GetModuleFileNameW},
    {"GetModuleHandleA", k32_GetModuleHandle},
    {"GetModuleHandleW", k32_GetModuleHandle},
    {"GetProcessHeap", k32_GetProcessHeap},
    {"GetStdHandle", k32_GetStdHandle},
    {"GetSystemTimeAsFileTime", k32_GetSystemTimeAsFileTime},
    {"GetTickCount", k32_GetTickCount},
    {"GetTickCount64", k32_GetTickCount64},
    {"HeapAlloc", k32_HeapAlloc},
    {"HeapFree", k32_HeapFree},
    {"HeapReAlloc", k32_HeapReAlloc},
    {"IsDebuggerPresent", k32_IsDebuggerPresent},
    {"MultiByteToWideChar", k32_MultiByteToWideChar},
    {"OutputDebugStringA", k32_OutputDebugStringA},
    {"QueryPerformanceCounter", k32_QueryPerformanceCounter},
    {"QueryPerformanceFrequency", k32_QueryPerformanceFrequency},
    {"SetLastError", k32_SetLastError},
    {"SetUnhandledExceptionFilter", k32_SetUnhandledExceptionFilter},
    {"Sleep", k32_Sleep},
    {"VirtualAlloc", k32_VirtualAlloc},
    {"VirtualFree", k32_VirtualFree},
    {"VirtualProtect", k32_VirtualProtect},
    {"VirtualQuery", k32_VirtualQuery},
    {"WideCharToMultiByte", k32_WideCharToMultiByte},
    {"lstrlenA", k32_lstrlenA},
    {"lstrlenW", k32_lstrlenW},
};
const size_t rcn_kernel32_count = sizeof rcn_kernel32 / sizeof rcn_kernel32[0];
