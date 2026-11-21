// RomCloud Native : fonctions système que demandent les jeux 64 bits récents (exécutables .NET
// compilés d'avance, Visual C++ récent) : kernel32 (bibliothèques, processeurs, mémoire, console,
// fichiers en « W », exceptions), advapi32 (privilèges, traces ETW), ole32 (COM), bcrypt (aléa).
// Les fonctions sans objet sur l'hôte réussissent sans rien faire ou échouent proprement.
#include <errno.h>
#include <sched.h>
#include <stdatomic.h>
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
    // RCN_CPUS=<n> : nombre de processeurs annoncé (essais : reproduire sur PC le nombre de cœurs d'un téléphone).
    const char *forced = getenv("RCN_CPUS");
    if (forced && atoi(forced) > 0) n = atoi(forced);
    return n < 1 ? 1 : n > 64 ? 64 : (u32)n;
}

#if !defined(_WIN32)
// Valeur (Ko) d'une ligne de /proc/meminfo ; 0 si elle manque.
static int meminfo_kb(const char *key, u64 *kb) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[128];
    int found = 0;
    size_t n = strlen(key);
    while (!found && fgets(line, sizeof line, f)) {
        if (strncmp(line, key, n) == 0) {
            *kb = strtoull(line + n, NULL, 10);
            found = 1;
        }
    }
    fclose(f);
    return found;
}
#endif
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
    // Linux : MemAvailable (mémoire libre plus caches qu'on peut rendre) ; MemFree seul est minuscule
    // sur Android (tout sert de cache) et des jeux refusent alors de démarrer.
    u64 kb = 0;
    *available = meminfo_kb("MemAvailable:", &kb) ? kb * 1024 : (u64)sysconf(_SC_AVPHYS_PAGES) * (u64)page;
#endif
}
// Fichier d'échange : la mémoire physique plus le swap de l'hôte (zram sur Android).
void rcn_pagefile_status(u64 *total, u64 *available) {
    rcn_memory_status(total, available);
#if !defined(_WIN32)
    u64 swap_total = 0, swap_free = 0;
    if (meminfo_kb("SwapTotal:", &swap_total) && meminfo_kb("SwapFree:", &swap_free)) {
        *total += swap_total * 1024;
        *available += swap_free * 1024;
    }
#endif
}

// Texte UTF-16 du jeu → UTF-8 (à libérer), ou NULL.
static char *utf8(u64 text) { return text ? rcn_utf16_to_utf8(PTR(text), -1) : NULL; }

// --- Bibliothèques ---

// LoadLibraryExW(name, file, flags) : seules les bibliothèques fournies par l'hôte existent.
static void k32_LoadLibraryExW(RcnCtx *ctx) {
    char *name = utf8(A0);
    if (ctx->proc->trace && name) fprintf(stderr, " [%s]", name);
    u64 handle = name ? rcn_load_library(ctx, name) : 0;
    free(name);
    RET(handle);
}
static void k32_FreeLibrary(RcnCtx *ctx) { RET(1); }
// GetModuleHandleExW(flags, name, &module) : par adresse (drapeau 4) ou nom NULL : l'exécutable.
static void k32_GetModuleHandleExW(RcnCtx *ctx) {
    u32 flags = (u32)A0;
    u64 handle;
    if (flags & 4) {
        RcnLoaded *l = rcn_loaded_at(ctx->proc, A1);
        handle = l ? (u64)(uintptr_t)l->image : 0;
    } else if (!A1) {
        handle = ctx->base;
    } else {
        char *name = utf8(A1);
        handle = name ? rcn_module_handle(ctx, name) : 0;
        if (!handle && name) {
            RcnLoaded *l = rcn_loaded_named(ctx->proc, name);
            if (l) handle = (u64)(uintptr_t)l->image;
        }
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
// IsProcessorFeaturePresent(feature) : jeux d'instructions que le traducteur gère (SSE, SSE2, SSE3, SSSE3 ;
// cmpxchg16b non).
static void k32_IsProcessorFeaturePresent(RcnCtx *ctx) {
    u32 f = (u32)A0;
    RET(f == 6 || f == 10 || f == 13 || f == 36 || f == 3 || f == 12 || f == 1);
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
    u64 s = A0, total, available, page_total, page_available;
    rcn_memory_status(&total, &available);
    rcn_pagefile_status(&page_total, &page_available);
    wr32(s + 4, total ? (u32)(100 - available * 100 / total) : 0);
    wr64(s + 8, total);
    wr64(s + 16, available);
    wr64(s + 24, page_total);
    wr64(s + 32, page_available);
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
static void k32_InitializeSListHead(RcnCtx *ctx) { memset(PTR(A0), 0, ctx->bits32 ? 8 : 16); }
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
// GetConsoleOutputCP : celle de la console, UTF-8.
static void k32_GetConsoleOutputCP(RcnCtx *ctx) { RET(65001); }
// WriteConsoleA(handle, texte, longueur, &écrits, réservé) : sur la sortie du jeu.
static void k32_WriteConsoleA(RcnCtx *ctx) {
    u32 length = (u32)A2;
    rcn_host_write(ctx, A0 == 0x12 ? RCN_STREAM_STDERR : RCN_STREAM_STDOUT, PTR(A1), length);
    if (A3) wr32(A3, length);
    RET(1);
}
// CompareStringA(locale, flags, a, a_length, b, b_length) : ordinale, comme CompareStringW.
static void k32_CompareStringA(RcnCtx *ctx) {
    u32 flags = (u32)A1;
    const u8 *a = PTR(A2), *b = PTR(STK(4));
    i32 na = (i32)A3, nb = (i32)STK(5);
    if (na < 0) for (na = 0; a[na]; na++) {}
    if (nb < 0) for (nb = 0; b[nb]; nb++) {}
    int result = 0;
    for (i32 i = 0; !result && i < na && i < nb; i++) {
        u8 x = a[i], y = b[i];
        if (flags & 0x11) {
            if (x >= 'a' && x <= 'z') x -= 32;
            if (y >= 'a' && y <= 'z') y -= 32;
        }
        result = x < y ? -1 : x > y;
    }
    if (!result) result = na < nb ? -1 : na > nb;
    RET((u32)(2 + result));
}
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
        if (flags & 0x11) { // NORM_IGNORECASE, LINGUISTIC_IGNORECASE
            if (x >= 'a' && x <= 'z') x -= 32;
            if (y >= 'a' && y <= 'z') y -= 32;
        }
        result = (x > y) - (x < y);
    }
    if (!result) result = (na > nb) - (na < nb);
    RET(result + 2);
}
// FindNLSStringEx(locale, flags, source, source_len, value, value_len, &found_len, …) : recherche
// ordinale (sans la casse avec NORM/LINGUISTIC_IGNORECASE) ; position trouvée ou -1.
static u16 fold(u16 c, int ignore_case) { return ignore_case && c >= 'a' && c <= 'z' ? (u16)(c - 32) : c; }
static void k32_FindNLSStringEx(RcnCtx *ctx) {
    u32 flags = (u32)A1;
    const u16 *s = PTR(A2), *v = PTR(STK(4));
    i32 ns = (i32)A3, nv = (i32)STK(5);
    if (ns < 0) for (ns = 0; s[ns]; ns++) {}
    if (nv < 0) for (nv = 0; v[nv]; nv++) {}
    int ic = (flags & 0x11) != 0, from_end = (flags & 0x00A00000) != 0; // FIND_FROMEND, FIND_ENDSWITH
    i32 first = 0, last = ns - nv, found = -1;
    if (flags & 0x00100000) last = last < 0 ? last : 0;  // FIND_STARTSWITH
    if (flags & 0x00200000) first = last;                 // FIND_ENDSWITH
    for (i32 k = 0; last >= first && k <= last - first; k++) {
        i32 i = from_end ? last - k : first + k, j = 0;
        while (j < nv && fold(s[i + j], ic) == fold(v[j], ic)) j++;
        if (j == nv) {
            found = i;
            break;
        }
    }
    if (STK(6) && found >= 0) wr32(STK(6), (u32)nv);
    if (found < 0) LAST_ERROR(ctx) = 0;
    RET((u64)(i64)found);
}
static void k32_DebugBreak(RcnCtx *ctx) { rcn_fail(ctx, "le jeu s'est arrete (DebugBreak)"); }
static void k32_RaiseFailFastException(RcnCtx *ctx) { rcn_fail(ctx, "le jeu s'est arrete sur une erreur fatale (fail fast)"); }
static void k32_UnhandledExceptionFilter(RcnCtx *ctx) { RET(1); }
// RaiseException(code, flags, count, arguments)
static void k32_RaiseException(RcnCtx *ctx) {
    // Jeu 32 bits : l'exception parcourt la chaîne SEH (__except, __finally, catch(...)). Le nommage d'un thread
    // pour le débogueur (0x406D1388) se contente de rendre la main : sous Windows sans débogueur, son __except ne
    // fait rien. Une exception que personne ne prend arrête le jeu en donnant son code.
    if (ctx->bits32) {
        if ((u32)A0 == 0x406D1388u) return;
        // Trace avant le parcours : un __except qui prend l'exception peut mener à la sortie du jeu (rapport
        // d'erreur d'Unreal Engine 3) sans autre message.
        rcn_log("RaiseException %08x (jeu 32 bits)", (u32)A0);
        rcn_guest_stack_once(ctx, "RaiseException");
        // RCN_FAIL_WSTR=<adresse hexadécimale> : texte UTF-16 du jeu à cette adresse (son message d'erreur,
        // GErrorHist d'Unreal Engine 3…).
        const char *at = getenv("RCN_FAIL_WSTR");
        if (at) {
            u64 address = strtoull(at, NULL, 16);
            char *text = address ? rcn_utf16_to_utf8(PTR(address), -1) : NULL;
            if (text && *text) rcn_log("texte en %s : %.2000s", at, text);
            free(text);
        }
        if (rcn_eh32_raise(ctx, (u32)A0, (u32)A1, (u32)A2, (u32)A3)) return;
        rcn_fail(ctx, "exception %08x levee par un jeu 32 bits, rattrapee par aucun __except", (u32)A0);
    }
    rcn_raise_exception(ctx);
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

// GetFileAttributesExA(nom, niveau, &données)
static void k32_GetFileAttributesExA(RcnCtx *ctx) {
    char *path = A0 ? rcn_host_path(ctx, STR(A0)) : NULL;
    struct stat st;
    int ok = path && stat(path, &st) == 0;
    if (ok) fill_attributes(ctx, A2, &st);
    else LAST_ERROR(ctx) = ERROR_FILE_NOT_FOUND;
    free(path);
    RET(ok);
}
// MoveFileA(ancien, nouveau)
static void k32_MoveFileA(RcnCtx *ctx) {
    char *from = A0 ? rcn_host_path(ctx, STR(A0)) : NULL, *to = A1 ? rcn_host_path(ctx, STR(A1)) : NULL;
    int ok = from && to && rename(from, to) == 0;
    if (!ok) LAST_ERROR(ctx) = ERROR_FILE_NOT_FOUND;
    free(from);
    free(to);
    RET(ok);
}
// CompareFileTime(&a, &b) : -1, 0 ou 1.
static void k32_CompareFileTime(RcnCtx *ctx) {
    u64 a = rd64(A0), b = rd64(A1);
    RET_I32(a < b ? -1 : a > b);
}
// LocalFileTimeToFileTime : heure locale = temps universel (comme FileTimeToLocalFileTime).
static void k32_LocalFileTimeToFileTime(RcnCtx *ctx) {
    wr64(A1, rd64(A0));
    RET(1);
}
// MulDiv(a, b, c) : a * b / c arrondi, sur 64 bits ; -1 si c est nul.
static void k32_MulDiv(RcnCtx *ctx) {
    i64 a = (i32)A0, b = (i32)A1, c = (i32)A2;
    if (!c) {
        RET_I32(-1);
        return;
    }
    i64 p = a * b;
    i64 r = (p >= 0) == (c > 0) ? (p + (c > 0 ? c : -c) / 2) / c : (p - (c > 0 ? c : -c) / 2) / c;
    RET_I32(r > INT32_MAX || r < INT32_MIN ? -1 : r);
}
// GetComputerNameA(tampon, &taille)
static void k32_GetComputerNameA(RcnCtx *ctx) {
    static const char name[] = "ROMCLOUD";
    u32 size = A1 ? rd32(A1) : 0;
    if (!A0 || size < sizeof name) {
        if (A1) wr32(A1, sizeof name);
        LAST_ERROR(ctx) = 111; // ERROR_BUFFER_OVERFLOW
        RET(0);
        return;
    }
    memcpy(PTR(A0), name, sizeof name);
    wr32(A1, sizeof name - 1);
    RET(1);
}
// GetLogicalDriveStringsA(taille, tampon) : « C:\ ».
static void k32_GetLogicalDriveStringsA(RcnCtx *ctx) {
    if ((u32)A0 >= 5 && A1) memcpy(PTR(A1), "C:\\\0", 5); // « C:\ » puis le texte vide qui finit la liste
    RET(4);
}
// GetWindowsDirectoryA(tampon, taille)
static void k32_GetWindowsDirectoryA(RcnCtx *ctx) {
    static const char dir[] = "C:\\Windows";
    if (!A0 || (u32)A1 < sizeof dir) {
        RET(sizeof dir);
        return;
    }
    memcpy(PTR(A0), dir, sizeof dir);
    RET(sizeof dir - 1);
}
// GetDiskFreeSpaceExA(dossier, &libre, &total, &total_libre) : 64 Go libres sur 128.
static void k32_GetDiskFreeSpaceExA(RcnCtx *ctx) {
    if (A1) wr64(A1, 64ull << 30);
    if (A2) wr64(A2, 128ull << 30);
    if (A3) wr64(A3, 64ull << 30);
    RET(1);
}
// InitOnceExecuteOnce(&init_once, fonction, paramètre, &contexte) : un seul appel de la fonction, les
// autres threads attendent qu'il soit fini. INIT_ONCE : 0 jamais, 1 en cours, 2 fait (bit 1 : réussi).
static void k32_InitOnceExecuteOnce(RcnCtx *ctx) {
    _Atomic u64 *once = PTR(A0);
    for (;;) {
        u64 state = atomic_load(once);
        if (state == 2) {
            RET(1);
            return;
        }
        u64 expected = 0;
        if (state == 0 && atomic_compare_exchange_strong(once, &expected, 1)) {
            u64 ok = rcn_call_guest(ctx, A1, A0, A2, A3, 0) & 0xFFFFFFFF;
            atomic_store(once, ok ? 2 : 0);
            RET(ok ? 1 : 0);
            return;
        }
        sched_yield();
    }
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
static void find_first(RcnCtx *ctx, u64 pattern, u64 data);
static void k32_FindFirstFileExW(RcnCtx *ctx) { find_first(ctx, A0, A2); }
static void find_first(RcnCtx *ctx, u64 pattern, u64 data) {
    char *name = utf8(pattern);
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
    if (slot >= 0 && find_next(ctx, &finds[slot], data)) {
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
static void find_first(RcnCtx *ctx, u64 pattern, u64 data) {
    char *name = utf8(pattern);
    char *path = name ? rcn_host_path(ctx, name) : NULL;
    free(name);
    u16 wide[1024];
    void *h = (void *)(intptr_t)-1;
    if (path && strlen(path) < 1000) {
        wide[rcn_utf8_to_utf16(path, strlen(path), wide)] = 0;
        h = FindFirstFileExW(wide, 0, PTR(data), 0, NULL, 0);
    }
    free(path);
    if (h == (void *)(intptr_t)-1) LAST_ERROR(ctx) = ERROR_FILE_NOT_FOUND;
    RET((uintptr_t)h);
}
static void k32_FindFirstFileExW(RcnCtx *ctx) { find_first(ctx, A0, A2); }
static void k32_FindNextFileW(RcnCtx *ctx) {
    int ok = FindNextFileW(PTR(A0), PTR(A1));
    if (!ok) LAST_ERROR(ctx) = ERROR_NO_MORE_FILES;
    RET(ok);
}
#endif

// FindFirstFileW(pattern, &data) : FindFirstFileExW dont la structure est le deuxième argument (lu par
// rcn_arg : registre en 64 bits, pile en 32 bits — ne pas déplacer de registre, la pile d'un jeu 32 bits
// n'a pas de troisième argument).
static void k32_FindFirstFileW(RcnCtx *ctx) { find_first(ctx, A0, A1); }

const RcnShim rcn_k32_more[] = {
    {"AddVectoredExceptionHandler", k32_AddVectoredExceptionHandler, 0},
    {"RemoveVectoredExceptionHandler", k32_AddVectoredExceptionHandler, 0},
    {"GetFileAttributesExA", k32_GetFileAttributesExA, 0},
    {"MoveFileA", k32_MoveFileA, 0},
    {"CompareFileTime", k32_CompareFileTime, 0},
    {"LocalFileTimeToFileTime", k32_LocalFileTimeToFileTime, 0},
    {"MulDiv", k32_MulDiv, 0},
    {"GetComputerNameA", k32_GetComputerNameA, 0},
    {"GetLogicalDriveStringsA", k32_GetLogicalDriveStringsA, 0},
    {"GetWindowsDirectoryA", k32_GetWindowsDirectoryA, S32(2)},
    {"GetDiskFreeSpaceExA", k32_GetDiskFreeSpaceExA, 0},
    {"InitOnceExecuteOnce", k32_InitOnceExecuteOnce, 0},
    {"CompareStringW", k32_CompareStringW, S32(6)},
    {"CompareStringA", k32_CompareStringA, S32(6)},
    {"GetConsoleOutputCP", k32_GetConsoleOutputCP, S32(0)},
    {"WriteConsoleA", k32_WriteConsoleA, S32(5)},
    {"CompareStringEx", k32_CompareStringW, 0},
    {"FindNLSStringEx", k32_FindNLSStringEx, 0},
    {"DebugBreak", k32_DebugBreak, 0},
    {"EncodePointer", k32_EncodePointer, S32(1)},
    {"FindFirstFileExW", k32_FindFirstFileExW, 0},
    {"FindFirstFileW", k32_FindFirstFileW, S32(2)},
    {"FindNextFileW", k32_FindNextFileW, S32(2)},
    {"FlushProcessWriteBuffers", k32_FlushProcessWriteBuffers, 0},
    {"FreeLibrary", k32_FreeLibrary, S32(1)},
    {"GetConsoleCP", k32_GetConsoleCP, S32(0)},
    {"GetConsoleMode", k32_GetConsoleMode, S32(2)},
    {"GetConsoleOutputCP", k32_GetConsoleCP, 0},
    {"GetThreadPriority", k32_GetThreadPriority, S32(1)},
    {"GetCurrentProcessorNumber", k32_GetCurrentProcessorNumber, 0},
    {"GetEnvironmentVariableW", k32_GetEnvironmentVariableW, 0},
    {"GetFileAttributesExW", k32_GetFileAttributesExW, 0},
    {"GetLargePageMinimum", k32_GetLargePageMinimum, 0},
    {"GetLogicalProcessorInformation", k32_GetLogicalProcessorInformation, 0},
    {"GetLogicalProcessorInformationEx", k32_GetLogicalProcessorInformationEx, 0},
    {"GetModuleHandleExW", k32_GetModuleHandleExW, 0},
    {"GetNumaHighestNodeNumber", k32_GetNumaHighestNodeNumber, 0},
    {"GetProcessAffinityMask", k32_GetProcessAffinityMask, S32(3)},
    {"GetStartupInfoW", k32_GetStartupInfoW, 0},
    {"GetWriteWatch", k32_GetWriteWatch, 0},
    {"GlobalMemoryStatusEx", k32_GlobalMemoryStatusEx, S32(1)},
    {"InitializeSListHead", k32_InitializeSListHead, S32(1)},
    {"IsProcessorFeaturePresent", k32_IsProcessorFeaturePresent, S32(1)},
    {"IsValidCodePage", k32_IsValidCodePage, S32(1)},
    {"LoadLibraryExW", k32_LoadLibraryExW, 0},
    {"LoadLibraryW", k32_LoadLibraryExW, S32(1)},
    {"RaiseException", k32_RaiseException, S32(4)},
    {"RaiseFailFastException", k32_RaiseFailFastException, 0},
    {"ResetWriteWatch", k32_ResetWriteWatch, 0},
    {"SetEnvironmentVariableW", k32_SetEnvironmentVariableW, 0},
    {"SetThreadPriority", k32_SetThreadPriority, S32(2)},
    {"SleepEx", k32_SleepEx, S32(2)},
    {"UnhandledExceptionFilter", k32_UnhandledExceptionFilter, S32(1)},
    {"VirtualAllocExNuma", k32_VirtualAllocExNuma, 0},
    {"VirtualUnlock", k32_VirtualUnlock, 0},
    {"WriteConsoleW", k32_WriteConsoleW, S32(5)},
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
// Composants COM en DLL (serveurs « in-process ») distribués avec DirectX : leur DLL traduite (fournie
// avec la traduction du jeu, comme msvcp140) crée l'objet par DllGetClassObject puis IClassFactory.
static const struct {
    u32 d1;
    u16 d2, d3;
    const char *dll;
} servers[] = {
    {0x5a508685, 0xa254, 0x4fba, "xaudio2_7.dll"}, // XAudio2 2.7
    {0xdb05ea35, 0x0329, 0x4d4b, "xaudio2_7.dll"}, // XAudio2 2.7 (débogage)
    {0x3eda9b49, 0x2085, 0x498b, "xaudio2_6.dll"}, // XAudio2 2.6
    {0x4c5e637a, 0x16c7, 0x4de3, "xaudio2_5.dll"}, // XAudio2 2.5
    {0x6a93130e, 0x1d53, 0x41d1, "xaudio2_7.dll"}, // AudioReverb 2.7 (XAudio2CreateReverb)
    {0xcac1105f, 0x619b, 0x4d04, "xaudio2_7.dll"}, // AudioVolumeMeter 2.7
};
// Objet créé dans `out` et HRESULT rendu ; 0 si la classe n'a pas de DLL traduite.
static int in_process_server(RcnCtx *ctx, u64 clsid, u64 iid, u64 out) {
    const char *dll = NULL;
    for (size_t i = 0; i < sizeof servers / sizeof servers[0]; i++) {
        if (rd32(clsid) == servers[i].d1 && rd16(clsid + 4) == servers[i].d2 && rd16(clsid + 6) == servers[i].d3) dll = servers[i].dll;
    }
    if (!dll || !rcn_load_dll(ctx, dll)) return 0;
    RcnLoaded *l = rcn_loaded_named(ctx->proc, dll);
    u64 get_class = l ? rcn_module_export(l, "DllGetClassObject") : 0;
    if (!get_class) return 0;
    // IID_IClassFactory {00000001-0000-0000-C000-000000000046} et le pointeur rendu, dans la mémoire du jeu.
    u8 *tmp = rcn_galloc(ctx, 24, 1);
    if (!tmp) return 0;
    static const u8 iid_factory[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
    memcpy(tmp, iid_factory, 16);
    u64 at = (u64)(uintptr_t)tmp;
    u32 hr = (u32)rcn_call_guest(ctx, get_class, clsid, at, at + 16, 0);
    u64 factory = rd64(at + 16);
    rcn_gfree(ctx, tmp);
    if (hr || !factory) {
        if (out) wr64(out, 0);
        RET(hr ? hr : 0x80004005u);
        return 1;
    }
    u64 vt = rd64(factory);
    // IClassFactory::CreateInstance(this, extérieur, iid, &objet), puis Release.
    u64 args[4] = {factory, 0, iid, out};
    hr = (u32)rcn_call_guest_n(ctx, rd64(vt + 3 * 8), 4, args);
    rcn_call_guest(ctx, rd64(vt + 2 * 8), factory, 0, 0, 0);
    if (hr) rcn_log("CoCreateInstance : %s : erreur 0x%08x", dll, hr);
    RET(hr);
    return 1;
}
static void ole_CoCreateInstance(RcnCtx *ctx) {
    if (ctx->proc->trace && A0) {
        u64 g = A0;
        fprintf(stderr, " [{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}]", rd32(g), rd16(g + 4), rd16(g + 6), rd8(g + 8),
                rd8(g + 9), rd8(g + 10), rd8(g + 11), rd8(g + 12), rd8(g + 13), rd8(g + 14), rd8(g + 15));
    }
    u64 object = !ctx->bits32 && A0 ? rcn_wasapi_create(ctx, rd32(A0)) : 0;
    if (object) {
        if (STK(4)) wr64(STK(4), object);
        RET(0);
        return;
    }
    // CLSID_DirectSound8 {3901CC3F-84B5-4FA4-BA35-AA8172B8A09B}, CLSID_DirectSound {47D4D946-62E8-11CF-93BC-444553540000}
    // (Wwise, quand WASAPI manque) : l'objet de rcn_dsound.c.
    if (A0 && ((rd32(A0) == 0x3901CC3Fu && rd32(A0 + 4) == 0x4FA484B5u) || (rd32(A0) == 0x47D4D946u && rd32(A0 + 4) == 0x11CF62E8u))) {
        object = rcn_dsound_new(ctx);
        if (STK(4)) wrp(ctx, STK(4), object);
        RET(object ? 0 : 0x88780078u); // DSERR_NODRIVER
        return;
    }
    if (!ctx->bits32 && A0 && in_process_server(ctx, A0, A3, STK(4))) return;
    static int told;
    // CLSID_GameExplorer {9A5EA990-3034-4D6F-9128-01F3C61022BC} : inscription du jeu dans l'explorateur de jeux
    // de Windows, facultative (Dishonored) : refusée sans bruit.
    int quiet = A0 && rd32(A0) == 0x9A5EA990u && rd32(A0 + 4) == 0x4D6F3034u;
    if (A0 && !quiet && told++ < 32) {
        u64 g = A0;
        rcn_log("CoCreateInstance : classe inconnue {%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", rd32(g), rd16(g + 4), rd16(g + 6),
                rd8(g + 8), rd8(g + 9), rd8(g + 10), rd8(g + 11), rd8(g + 12), rd8(g + 13), rd8(g + 14), rd8(g + 15));
    }
    if (STK(4)) wrp(ctx, STK(4), 0);
    RET(0x80040154u); // REGDB_E_CLASSNOTREG
}
// CoWaitForMultipleHandles(flags, timeout, count, handles, &index) : par WaitForMultipleObjects.
static void ole_CoWaitForMultipleHandles(RcnCtx *ctx) {
    rcn_fail(ctx, "CoWaitForMultipleHandles non geree");
}
const RcnShim rcn_ole32[] = {
    {"CoCreateInstance", ole_CoCreateInstance, S32(5)},
    {"CoGetApartmentType", ole_CoGetApartmentType, 0},
    {"CoInitializeEx", ret0, S32(2)},
    {"CoUninitialize", ret0, S32(0)},
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


