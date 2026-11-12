// RomCloud Native : fonctions système que demandent les jeux 64 bits récents (exécutables .NET
// compilés d'avance, Visual C++ récent) : kernel32 (bibliothèques, processeurs, mémoire, console,
// fichiers en « W », exceptions), advapi32 (privilèges, traces ETW), ole32 (COM), bcrypt (aléa).
// Les fonctions sans objet sur l'hôte réussissent sans rien faire ou échouent proprement.
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "rcn_internal.h"

#if defined(_WIN32)
__declspec(dllimport) unsigned long __stdcall GetActiveProcessorCount(unsigned short group);
__declspec(dllimport) int __stdcall GlobalMemoryStatusEx(void *status);
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

enum {
    ERROR_FILE_NOT_FOUND = 2,
    ERROR_NOT_SUPPORTED = 50,
    ERROR_INSUFFICIENT_BUFFER = 122,
    ERROR_MOD_NOT_FOUND = 126,
    ERROR_NO_MORE_FILES = 18,
    ERROR_ENVVAR_NOT_FOUND = 203,
    FILE_ATTRIBUTE_DIRECTORY = 0x10,
    FILE_ATTRIBUTE_ARCHIVE = 0x20,
    INVALID_HANDLE_VALUE = -1,
};

u32 rcn_cpu_count(void) {
#if defined(_WIN32)
    u32 n = (u32)GetActiveProcessorCount(0xFFFF);
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    return n < 1 ? 1 : n > 64 ? 64 : (u32)n;
}

void rcn_memory_status(u64 *total, u64 *available) {
#if defined(_WIN32)
    u8 status[64] = {0};
    *(u32 *)status = 64;
    GlobalMemoryStatusEx(status);
    memcpy(total, status + 8, 8);
    memcpy(available, status + 16, 8);
#else
    long page = sysconf(_SC_PAGESIZE);
    *total = (u64)sysconf(_SC_PHYS_PAGES) * (u64)page;
    *available = (u64)sysconf(_SC_AVPHYS_PAGES) * (u64)page;
#endif
}

// Texte UTF-16 du jeu → UTF-8 (à libérer), ou NULL.
static char *utf8(u64 text) { return text ? rcn_utf16_to_utf8(PTR(text), -1) : NULL; }

// --- Bibliothèques ---

// LoadLibraryExW(name, file, flags) : seules les bibliothèques fournies par l'hôte existent.
static void k32_LoadLibraryExW(RcnCtx *ctx) {
    char *name = utf8(A0);
    // Chemin éventuel retiré : seul le nom compte.
    const char *base = name ? name : "";
    for (const char *p = base; *p; p++) {
        if (*p == '\\' || *p == '/') base = p + 1;
    }
    u64 handle = name ? rcn_module_handle(ctx, base) : 0;
    if (ctx->proc->trace) fprintf(stderr, " [%s]", base);
    if (!handle) LAST_ERROR(ctx) = ERROR_MOD_NOT_FOUND;
    free(name);
    RET(handle);
}
static void k32_FreeLibrary(RcnCtx *ctx) { RET(1); }
// GetModuleHandleExW(flags, name, &module) : par adresse (drapeau 4) ou nom NULL : l'exécutable.
static void k32_GetModuleHandleExW(RcnCtx *ctx) {
    u32 flags = (u32)A0;
    u64 handle;
    if ((flags & 4) || !A1) {
        handle = ctx->base;
    } else {
        char *name = utf8(A1);
        handle = name ? rcn_module_handle(ctx, name) : 0;
        free(name);
    }
    if (A2) wrp(ctx, A2, handle);
    if (!handle) LAST_ERROR(ctx) = ERROR_MOD_NOT_FOUND;
    RET(handle != 0);
}

// --- Processeurs et mémoire ---

static u64 cpu_mask(void) {
    u32 n = rcn_cpu_count();
    return n >= 64 ? ~0ull : (1ull << n) - 1;
}
// IsProcessorFeaturePresent(feature) : jeux d'instructions que le traducteur gère (SSE, SSE2, SSE3, cmpxchg16b non).
static void k32_IsProcessorFeaturePresent(RcnCtx *ctx) {
    u32 f = (u32)A0;
    RET(f == 6 || f == 10 || f == 13 || f == 3 || f == 12 || f == 1);
}
static void k32_GetCurrentProcessorNumber(RcnCtx *ctx) { RET(0); }
static void k32_GetNumaHighestNodeNumber(RcnCtx *ctx) {
    wr32(A0, 0);
    RET(1);
}
static void k32_GetLargePageMinimum(RcnCtx *ctx) { RET(0); }
// GetProcessAffinityMask(process, &process_mask, &system_mask)
static void k32_GetProcessAffinityMask(RcnCtx *ctx) {
    wrp(ctx, A1, cpu_mask());
    wrp(ctx, A2, cpu_mask());
    RET(1);
}
// GetLogicalProcessorInformation(&buffer, &length) : un cœur par processeur (entrées de 32 octets).
static void k32_GetLogicalProcessorInformation(RcnCtx *ctx) {
    u32 n = rcn_cpu_count(), need = n * 32;
    u64 buffer = A0, length = A1;
    if (rd32(length) < need || !buffer) {
        wr32(length, need);
        LAST_ERROR(ctx) = ERROR_INSUFFICIENT_BUFFER;
        RET(0);
        return;
    }
    memset(PTR(buffer), 0, need);
    for (u32 i = 0; i < n; i++) {
        wr64(buffer + 32 * i, 1ull << i); // ProcessorMask
        wr32(buffer + 32 * i + 8, 0);    // RelationProcessorCore
    }
    wr32(length, need);
    RET(1);
}
// GetLogicalProcessorInformationEx(relation, &buffer, &length) : cœurs seulement (48 octets chacun).
static void k32_GetLogicalProcessorInformationEx(RcnCtx *ctx) {
    u32 relation = (u32)A0;
    u64 buffer = A1, length = A2;
    if (relation != 0 && relation != 0xFFFF) {
        LAST_ERROR(ctx) = ERROR_NOT_SUPPORTED;
        RET(0);
        return;
    }
    u32 n = rcn_cpu_count(), need = n * 48;
    if (rd32(length) < need || !buffer) {
        wr32(length, need);
        LAST_ERROR(ctx) = ERROR_INSUFFICIENT_BUFFER;
        RET(0);
        return;
    }
    memset(PTR(buffer), 0, need);
    for (u32 i = 0; i < n; i++) {
        u64 e = buffer + 48 * i;
        wr32(e, 0);                // Relationship : RelationProcessorCore
        wr32(e + 4, 48);           // Size
        wr16(e + 8 + 22, 1);       // GroupCount
        wr64(e + 8 + 24, 1ull << i); // GroupMask[0].Mask
    }
    wr32(length, need);
    RET(1);
}
// GlobalMemoryStatusEx(&status) : mémoire physique de l'hôte ; espace d'adresses de 128 To.
static void k32_GlobalMemoryStatusEx(RcnCtx *ctx) {
    u64 s = A0, total, available;
    rcn_memory_status(&total, &available);
    wr32(s + 4, total ? (u32)(100 - available * 100 / total) : 0);
    wr64(s + 8, total);
    wr64(s + 16, available);
    wr64(s + 24, total);
    wr64(s + 32, available);
    wr64(s + 40, 0x7FFFFFFE0000ull);
    wr64(s + 48, 0x7FFF00000000ull);
    wr64(s + 56, 0);
    RET(1);
}
static void k32_VirtualAllocExNuma(RcnCtx *ctx) { RET(rcn_vm_alloc(A1, A2, (u32)A3, (u32)STK(4))); }
static void k32_VirtualUnlock(RcnCtx *ctx) { RET(1); }
// GetWriteWatch(flags, base, size, &addresses, &count, &granularity) : les écritures ne sont pas suivies,
// toutes les pages sont données pour écrites (le ramasse-miettes les réexamine toutes).
static void k32_GetWriteWatch(RcnCtx *ctx) {
    u64 base = A1 & ~0xFFFull, size = A2, addresses = A3, count = STK(4), granularity = STK(5);
    u64 capacity = rd64(count), pages = (A1 + size - base + 0xFFF) / 0x1000, n = 0;
    for (; n < pages && n < capacity; n++) wr64(addresses + 8 * n, base + n * 0x1000);
    wr64(count, n);
    wr32(granularity, 0x1000);
    RET(0);
}
static void k32_ResetWriteWatch(RcnCtx *ctx) { RET(0); }
static void k32_FlushProcessWriteBuffers(RcnCtx *ctx) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static void k32_InitializeSListHead(RcnCtx *ctx) { memset(PTR(A0), 0, 16); }
static void k32_EncodePointer(RcnCtx *ctx) { RET(A0); }

// --- Processus, environnement, console ---

static void k32_GetStartupInfoW(RcnCtx *ctx) {
    u32 size = ctx->bits32 ? 68 : 104;
    memset(PTR(A0), 0, size);
    wr32(A0, size);
}
// L'environnement du jeu est vide.
// GetEnvironmentVariableW(name, buffer, size) : seuls les réglages des runtimes .NET (RH_, DOTNET_,
// COMPlus_) viennent de l'environnement de l'hôte, pour la mise au point ; le reste est absent.
static void k32_GetEnvironmentVariableW(RcnCtx *ctx) {
    char *name = utf8(A0);
    if (ctx->proc->trace) fprintf(stderr, " [%s]", name ? name : "?");
    const char *value = NULL;
    if (name && (!strncmp(name, "RH_", 3) || !strncmp(name, "DOTNET_", 7) || !strncmp(name, "COMPlus_", 8))) value = getenv(name);
    free(name);
    if (value) {
        size_t len = rcn_utf8_to_utf16(value, strlen(value), NULL);
        if (A1 && A2 > len) {
            rcn_utf8_to_utf16(value, strlen(value), PTR(A1));
            wr16(A1 + 2 * len, 0);
            RET(len);
        } else {
            RET(len + 1);
        }
        return;
    }
    LAST_ERROR(ctx) = ERROR_ENVVAR_NOT_FOUND;
    RET(0);
}
static void k32_SetEnvironmentVariableW(RcnCtx *ctx) { RET(1); }
static void k32_SetThreadPriority(RcnCtx *ctx) { RET(1); }
static void k32_GetThreadPriority(RcnCtx *ctx) { RET(0); } // THREAD_PRIORITY_NORMAL
// SleepEx(milliseconds, alertable) : aucun appel asynchrone n'est jamais en attente.
static void k32_SleepEx(RcnCtx *ctx) {
    rcn_sleep_ms((u32)A0);
    RET(0);
}
static void k32_GetConsoleCP(RcnCtx *ctx) { RET(65001); }
static void k32_GetConsoleMode(RcnCtx *ctx) { RET(0); }
// WriteConsoleW(handle, text, length, &written, reserved)
static void k32_WriteConsoleW(RcnCtx *ctx) {
    u32 length = (u32)A2;
    char *text = rcn_utf16_to_utf8(PTR(A1), length);
    if (text) rcn_host_write(ctx, A0 == 0x12 ? RCN_STREAM_STDERR : RCN_STREAM_STDOUT, text, strlen(text));
    free(text);
    if (A3) wr32(A3, length);
    RET(1);
}
static void k32_IsValidCodePage(RcnCtx *ctx) { RET(1); }
// CompareStringW(locale, flags, a, a_length, b, b_length) : comparaison ordinale (sans la casse avec
// NORM_IGNORECASE) ; 1 : inférieur, 2 : égal, 3 : supérieur.
static void k32_CompareStringW(RcnCtx *ctx) {
    u32 flags = (u32)A1;
    const u16 *a = PTR(A2), *b = PTR(STK(4));
    i32 na = (i32)A3, nb = (i32)STK(5);
    if (na < 0) for (na = 0; a[na]; na++) {}
    if (nb < 0) for (nb = 0; b[nb]; nb++) {}
    int result = 0;
    for (i32 i = 0; !result && i < na && i < nb; i++) {
        u16 x = a[i], y = b[i];
        if (flags & 1) {
            if (x >= 'a' && x <= 'z') x -= 32;
            if (y >= 'a' && y <= 'z') y -= 32;
        }
        result = (x > y) - (x < y);
    }
    if (!result) result = (na > nb) - (na < nb);
    RET(result + 2);
}
static void k32_DebugBreak(RcnCtx *ctx) { rcn_fail(ctx, "le jeu s'est arrete (DebugBreak)"); }
static void k32_RaiseFailFastException(RcnCtx *ctx) { rcn_fail(ctx, "le jeu s'est arrete sur une erreur fatale (fail fast)"); }
static void k32_UnhandledExceptionFilter(RcnCtx *ctx) { RET(1); }
// RaiseException(code, flags, count, arguments) : pas encore de traitement des exceptions.
static void k32_RaiseException(RcnCtx *ctx) {
    rcn_fail(ctx, "exception 0x%08x levee par le jeu (exceptions non gerees pour l'instant)", (u32)A0);
}
// Gestionnaires d'exceptions : notés, jamais appelés pour l'instant.
static void k32_AddVectoredExceptionHandler(RcnCtx *ctx) { RET(A1 ? A1 : 1); }

// --- Fichiers en « W » ---

static void fill_attributes(RcnCtx *ctx, u64 at, const struct stat *st) {
    (void)ctx;
    u64 time = ((u64)st->st_mtime + 11644473600ull) * 10000000ull;
    wr32(at, S_ISDIR(st->st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE);
    wr64(at + 4, time);
    wr64(at + 12, time);
    wr64(at + 20, time);
    wr32(at + 28, (u32)((u64)st->st_size >> 32));
    wr32(at + 32, (u32)st->st_size);
}
// GetFileAttributesExW(name, level, &data) : WIN32_FILE_ATTRIBUTE_DATA (36 octets).
static void k32_GetFileAttributesExW(RcnCtx *ctx) {
    char *name = utf8(A0);
    char *path = name ? rcn_host_path(ctx, name) : NULL;
    struct stat st;
    int ok = path && stat(path, &st) == 0;
    if (ok) fill_attributes(ctx, A2, &st);
    else LAST_ERROR(ctx) = ERROR_FILE_NOT_FOUND;
    free(path);
    free(name);
    RET(ok);
}

#if !defined(_WIN32)
// Recherche de fichiers : dossier ouvert et motif, par handle.
typedef struct {
    DIR *dir;
    char *path;
    char pattern[260];
} Find;
static Find finds[16];
static pthread_mutex_t finds_lock = PTHREAD_MUTEX_INITIALIZER;
#define HANDLE_FINDW 0x900

static int wildcard(const char *pattern, const char *name) {
    if (strcmp(pattern, "*.*") == 0) return 1;
    for (; *pattern; pattern++, name++) {
        if (*pattern == '*') {
            for (const char *rest = name;; rest++) {
                if (wildcard(pattern + 1, rest)) return 1;
                if (!*rest) return 0;
            }
        }
        if (!*name) return 0;
        if (*pattern != '?' && (*pattern | 0x20) != (*name | 0x20)) return 0;
    }
    return !*name;
}
// Entrée suivante dans un WIN32_FIND_DATAW (592 octets).
static int find_next(RcnCtx *ctx, Find *f, u64 data) {
    for (struct dirent *e; (e = readdir(f->dir));) {
        if (!wildcard(f->pattern, e->d_name)) continue;
        char full[1024];
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", f->path, e->d_name);
        if (stat(full, &st) != 0) continue;
        memset(PTR(data), 0, 592);
        fill_attributes(ctx, data, &st);
        size_t n = rcn_utf8_to_utf16(e->d_name, strlen(e->d_name), NULL);
        if (n >= 260) continue;
        rcn_utf8_to_utf16(e->d_name, strlen(e->d_name), PTR(data + 44));
        return 1;
    }
    return 0;
}
// FindFirstFileExW(pattern, level, &data, search, filter, flags)
static void k32_FindFirstFileExW(RcnCtx *ctx) {
    char *name = utf8(A0);
    char *path = name ? rcn_host_path(ctx, name) : NULL;
    free(name);
    char *slash = path ? strrchr(path, '/') : NULL;
    int slot = -1;
    if (slash) {
        *slash = 0;
        pthread_mutex_lock(&finds_lock);
        for (int i = 0; i < 16 && slot < 0; i++) {
            if (!finds[i].dir && strlen(slash + 1) < sizeof finds[i].pattern && (finds[i].dir = opendir(path))) {
                slot = i;
                finds[i].path = path;
                strcpy(finds[i].pattern, slash + 1);
            }
        }
        pthread_mutex_unlock(&finds_lock);
    }
    if (slot >= 0 && find_next(ctx, &finds[slot], A2)) {
        RET(HANDLE_FINDW + slot);
        return;
    }
    if (slot >= 0) {
        closedir(finds[slot].dir);
        finds[slot].dir = NULL;
    }
    free(path);
    LAST_ERROR(ctx) = ERROR_FILE_NOT_FOUND;
    RET((u64)(i64)INVALID_HANDLE_VALUE);
}
static void k32_FindNextFileW(RcnCtx *ctx) {
    u32 slot = (u32)A0 - HANDLE_FINDW;
    int found = slot < 16 && finds[slot].dir && find_next(ctx, &finds[slot], A1);
    if (!found) LAST_ERROR(ctx) = ERROR_NO_MORE_FILES;
    RET(found);
}
#else
// Sous Windows (essais), la recherche passe par les fonctions du système, chemin ramené au dossier du jeu.
__declspec(dllimport) void *__stdcall FindFirstFileExW(const u16 *name, int level, void *data, int search, void *filter, unsigned long flags);
__declspec(dllimport) int __stdcall FindNextFileW(void *find, void *data);
static void k32_FindFirstFileExW(RcnCtx *ctx) {
    char *name = utf8(A0);
    char *path = name ? rcn_host_path(ctx, name) : NULL;
    free(name);
    u16 wide[1024];
    void *h = (void *)(intptr_t)-1;
    if (path && strlen(path) < 1000) {
        wide[rcn_utf8_to_utf16(path, strlen(path), wide)] = 0;
        h = FindFirstFileExW(wide, 0, PTR(A2), 0, NULL, 0);
    }
    free(path);
    if (h == (void *)(intptr_t)-1) LAST_ERROR(ctx) = ERROR_FILE_NOT_FOUND;
    RET((uintptr_t)h);
}
static void k32_FindNextFileW(RcnCtx *ctx) {
    int ok = FindNextFileW(PTR(A0), PTR(A1));
    if (!ok) LAST_ERROR(ctx) = ERROR_NO_MORE_FILES;
    RET(ok);
}
#endif

const RcnShim rcn_k32_more[] = {
    {"AddVectoredExceptionHandler", k32_AddVectoredExceptionHandler, 0},
    {"CompareStringW", k32_CompareStringW, 0},
    {"DebugBreak", k32_DebugBreak, 0},
    {"EncodePointer", k32_EncodePointer, 0},
    {"FindFirstFileExW", k32_FindFirstFileExW, 0},
    {"FindNextFileW", k32_FindNextFileW, 0},
    {"FlushProcessWriteBuffers", k32_FlushProcessWriteBuffers, 0},
    {"FreeLibrary", k32_FreeLibrary, S32(1)},
    {"GetConsoleCP", k32_GetConsoleCP, 0},
    {"GetConsoleMode", k32_GetConsoleMode, 0},
    {"GetConsoleOutputCP", k32_GetConsoleCP, 0},
    {"GetThreadPriority", k32_GetThreadPriority, 0},
    {"GetCurrentProcessorNumber", k32_GetCurrentProcessorNumber, 0},
    {"GetEnvironmentVariableW", k32_GetEnvironmentVariableW, 0},
    {"GetFileAttributesExW", k32_GetFileAttributesExW, 0},
    {"GetLargePageMinimum", k32_GetLargePageMinimum, 0},
    {"GetLogicalProcessorInformation", k32_GetLogicalProcessorInformation, 0},
    {"GetLogicalProcessorInformationEx", k32_GetLogicalProcessorInformationEx, 0},
    {"GetModuleHandleExW", k32_GetModuleHandleExW, 0},
    {"GetNumaHighestNodeNumber", k32_GetNumaHighestNodeNumber, 0},
    {"GetProcessAffinityMask", k32_GetProcessAffinityMask, 0},
    {"GetStartupInfoW", k32_GetStartupInfoW, 0},
    {"GetWriteWatch", k32_GetWriteWatch, 0},
    {"GlobalMemoryStatusEx", k32_GlobalMemoryStatusEx, 0},
    {"InitializeSListHead", k32_InitializeSListHead, 0},
    {"IsProcessorFeaturePresent", k32_IsProcessorFeaturePresent, 0},
    {"IsValidCodePage", k32_IsValidCodePage, 0},
    {"LoadLibraryExW", k32_LoadLibraryExW, 0},
    {"LoadLibraryW", k32_LoadLibraryExW, 0},
    {"RaiseException", k32_RaiseException, 0},
    {"RaiseFailFastException", k32_RaiseFailFastException, 0},
    {"ResetWriteWatch", k32_ResetWriteWatch, 0},
    {"SetEnvironmentVariableW", k32_SetEnvironmentVariableW, 0},
    {"SetThreadPriority", k32_SetThreadPriority, 0},
    {"SleepEx", k32_SleepEx, 0},
    {"UnhandledExceptionFilter", k32_UnhandledExceptionFilter, 0},
    {"VirtualAllocExNuma", k32_VirtualAllocExNuma, 0},
    {"VirtualUnlock", k32_VirtualUnlock, 0},
    {"WriteConsoleW", k32_WriteConsoleW, 0},
};
const size_t rcn_k32_more_count = sizeof rcn_k32_more / sizeof rcn_k32_more[0];

// --- advapi32 : privilèges refusés (pages larges), traces ETW désactivées ---

static void ret0(RcnCtx *ctx) { RET(0); }
static void ret1(RcnCtx *ctx) { RET(1); }
// EventRegister(guid, callback, context, &handle) : réussit, handle nul.
static void adv_EventRegister(RcnCtx *ctx) {
    if (A3) wr64(A3, 0);
    RET(0);
}
const RcnShim rcn_advapi32[] = {
    {"AdjustTokenPrivileges", ret0, 0},
    {"EventEnabled", ret0, 0},
    {"EventRegister", adv_EventRegister, 0},
    {"EventUnregister", ret0, 0},
    {"EventWrite", ret0, 0},
    {"LookupPrivilegeValueW", ret0, 0},
    {"OpenProcessToken", ret0, 0},
};
const size_t rcn_advapi32_count = sizeof rcn_advapi32 / sizeof rcn_advapi32[0];

// --- ole32 : COM initialisé, aucun composant disponible ---

static void ole_CoGetApartmentType(RcnCtx *ctx) {
    wr32(A0, 1); // APTTYPE_MTA
    wr32(A1, 0);
    RET(0);
}
static void ole_CoCreateInstance(RcnCtx *ctx) {
    if (ctx->proc->trace && A0) {
        u64 g = A0;
        fprintf(stderr, " [{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}]", rd32(g), rd16(g + 4), rd16(g + 6), rd8(g + 8),
                rd8(g + 9), rd8(g + 10), rd8(g + 11), rd8(g + 12), rd8(g + 13), rd8(g + 14), rd8(g + 15));
    }
    if (STK(4)) wrp(ctx, STK(4), 0);
    RET(0x80040154u); // REGDB_E_CLASSNOTREG
}
// CoWaitForMultipleHandles(flags, timeout, count, handles, &index) : par WaitForMultipleObjects.
static void ole_CoWaitForMultipleHandles(RcnCtx *ctx) {
    rcn_fail(ctx, "CoWaitForMultipleHandles non geree");
}
const RcnShim rcn_ole32[] = {
    {"CoCreateInstance", ole_CoCreateInstance, 0},
    {"CoGetApartmentType", ole_CoGetApartmentType, 0},
    {"CoInitializeEx", ret0, 0},
    {"CoUninitialize", ret0, 0},
    {"CoWaitForMultipleHandles", ole_CoWaitForMultipleHandles, 0},
};
const size_t rcn_ole32_count = sizeof rcn_ole32 / sizeof rcn_ole32[0];

// --- bcrypt : octets aléatoires ---

// BCryptGenRandom(algorithm, buffer, length, flags)
static void bc_BCryptGenRandom(RcnCtx *ctx) {
    u8 *out = PTR(A1);
    u32 length = (u32)A2;
#if !defined(_WIN32)
    int fd = open("/dev/urandom", O_RDONLY);
    u32 done = 0;
    while (fd >= 0 && done < length) {
        ssize_t n = read(fd, out + done, length - done);
        if (n <= 0) break;
        done += (u32)n;
    }
    if (fd >= 0) close(fd);
    if (done == length) {
        RET(0);
        return;
    }
#endif
    static u64 state;
    if (!state) state = rcn_now_ns() | 1;
    for (u32 i = 0; i < length; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        out[i] = (u8)state;
    }
    RET(0);
}
const RcnShim rcn_bcrypt[] = {
    {"BCryptGenRandom", bc_BCryptGenRandom, 0},
};
const size_t rcn_bcrypt_count = sizeof rcn_bcrypt / sizeof rcn_bcrypt[0];

// --- user32 (jeux 64 bits) : ce qu'appellent les programmes qui passent par SDL ---
const RcnShim rcn_user32_x64[] = {
    {"SetProcessDPIAware", ret1, 0},
};
const size_t rcn_user32_x64_count = sizeof rcn_user32_x64 / sizeof rcn_user32_x64[0];
