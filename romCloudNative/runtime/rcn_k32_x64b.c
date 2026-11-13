// RomCloud Native : kernel32, suite, pour les moteurs récents (Unity…) : fichiers projetés en mémoire,
// copie et déplacement de fichiers, temps local et formats de date, langue, mémoire Global / Local,
// informations du processus. Processus fils, tubes et instantanés du système : refusés proprement.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rcn_internal.h"

enum {
    ERROR_FILE_NOT_FOUND = 2,
    ERROR_ACCESS_DENIED = 5,
    ERROR_INVALID_PARAMETER = 87,
    ERROR_NOT_SUPPORTED = 50,
    ERROR_NO_MORE_FILES = 18,
    INVALID_HANDLE = -1,
};

static char *utf8(u64 text) { return text ? rcn_utf16_to_utf8(PTR(text), -1) : NULL; }
static u32 put_wide(u64 buffer, u32 size, const char *text) {
    size_t len = rcn_utf8_to_utf16(text, strlen(text), NULL);
    if (!buffer || size <= len) return (u32)len + 1;
    rcn_utf8_to_utf16(text, strlen(text), PTR(buffer));
    wr16(buffer + 2 * len, 0);
    return (u32)len;
}
static void k_True(RcnCtx *ctx) { RET(1); }
static void k_Zero(RcnCtx *ctx) { RET(0); }
static void k_Fail(RcnCtx *ctx) {
    LAST_ERROR(ctx) = ERROR_NOT_SUPPORTED;
    RET(0);
}
static void k_Invalid(RcnCtx *ctx) {
    LAST_ERROR(ctx) = ERROR_NOT_SUPPORTED;
    RET((u64)(i64)INVALID_HANDLE);
}

// --- Fichiers projetés : copie du fichier en mémoire (les écritures n'y retournent pas) ---

typedef struct {
    u32 magic;
    u8 *data;
    u64 size;
} Mapping;
#define MAPPING_MAGIC 0x4D415050u

// CreateFileMappingW(file, security, protect, size_high, size_low, name)
static void k_CreateFileMappingW(RcnCtx *ctx) {
    u64 file = A0, size = ((u64)(u32)A3 << 32) | (u32)STK(4);
    if (file != (u64)(i64)INVALID_HANDLE && !size) {
        u64 file_size = 0, t = 0;
        int dir = 0, ok = 0;
        rcn_file_info(ctx, file, &file_size, &dir, &t, &ok);
        if (!ok) {
            LAST_ERROR(ctx) = 6;
            RET(0);
            return;
        }
        size = file_size;
    }
    Mapping *m = calloc(1, sizeof *m);
    u8 *data = m ? calloc(1, size ? size : 1) : NULL;
    if (!m || !data) {
        free(m);
        LAST_ERROR(ctx) = 8; // ERROR_NOT_ENOUGH_MEMORY
        RET(0);
        return;
    }
    if (file != (u64)(i64)INVALID_HANDLE) rcn_file_read_at(ctx, file, 0, data, size);
    m->magic = MAPPING_MAGIC;
    m->data = data;
    m->size = size;
    RET((u64)(uintptr_t)m);
}
// MapViewOfFile(mapping, access, offset_high, offset_low, bytes)
static void k_MapViewOfFile(RcnCtx *ctx) {
    Mapping *m = PTR(A0);
    u64 offset = ((u64)(u32)A2 << 32) | (u32)A3;
    if (!m || m->magic != MAPPING_MAGIC || offset > m->size) {
        LAST_ERROR(ctx) = ERROR_INVALID_PARAMETER;
        RET(0);
        return;
    }
    RET((u64)(uintptr_t)(m->data + offset));
}

// --- Fichiers ---

// Copie d'un fichier du jeu (chemins Windows) ; 0 si impossible.
static int copy_file(RcnCtx *ctx, u64 from_w, u64 to_w) {
    char *from = utf8(from_w), *to = utf8(to_w);
    char *src = from ? rcn_host_path(ctx, from) : NULL, *dst = to ? rcn_host_path(ctx, to) : NULL;
    free(from);
    free(to);
    FILE *in = src ? fopen(src, "rb") : NULL;
    FILE *out = in && dst ? fopen(dst, "wb") : NULL;
    int ok = in && out;
    char buffer[65536];
    size_t n;
    while (ok && (n = fread(buffer, 1, sizeof buffer, in)) > 0) ok = fwrite(buffer, 1, n, out) == n;
    if (in) fclose(in);
    if (out) fclose(out);
    free(src);
    free(dst);
    if (!ok) LAST_ERROR(ctx) = ERROR_FILE_NOT_FOUND;
    return ok;
}
static void k_CopyFileW(RcnCtx *ctx) { RET(copy_file(ctx, A0, A1)); }
static void k_CopyFileExW(RcnCtx *ctx) { RET(copy_file(ctx, A0, A1)); }
// MoveFileExW(from, to, flags) / ReplaceFileW(replaced, replacement, backup, …)
static int move_file(RcnCtx *ctx, u64 from_w, u64 to_w) {
    char *from = utf8(from_w), *to = utf8(to_w);
    char *src = from ? rcn_host_path(ctx, from) : NULL, *dst = to ? rcn_host_path(ctx, to) : NULL;
    free(from);
    free(to);
    int ok = src && dst;
    if (ok) {
        remove(dst);
        ok = rename(src, dst) == 0;
    }
    free(src);
    free(dst);
    if (!ok) LAST_ERROR(ctx) = ERROR_ACCESS_DENIED;
    return ok;
}
static void k_MoveFileExW(RcnCtx *ctx) { RET(move_file(ctx, A0, A1)); }
static void k_ReplaceFileW(RcnCtx *ctx) { RET(move_file(ctx, A1, A0)); }
// GetTempFileNameW(path, prefix, unique, &name)
static void k_GetTempFileNameW(RcnCtx *ctx) {
    static u32 counter = 0x100;
    char *dir = utf8(A0), *prefix = utf8(A1);
    char name[512];
    u32 unique = (u32)A2 ? (u32)A2 : ++counter;
    snprintf(name, sizeof name, "%s%s%.3s%04X.tmp", dir ? dir : "C:\\Temp", dir && dir[0] && dir[strlen(dir) - 1] == '\\' ? "" : "\\",
             prefix ? prefix : "", unique & 0xFFFF);
    free(dir);
    free(prefix);
    put_wide(A3, 260, name);
    RET(unique);
}
// GetDiskFreeSpaceExW(path, &free_caller, &total, &free) : 100 Go libres sur 200.
static void k_GetDiskFreeSpaceExW(RcnCtx *ctx) {
    if (A1) wr64(A1, 100ull << 30);
    if (A2) wr64(A2, 200ull << 30);
    if (A3) wr64(A3, 100ull << 30);
    RET(1);
}
static void k_GetLogicalDrives(RcnCtx *ctx) { RET(1u << 2); } // C:
static void k_GetWindowsDirectoryW(RcnCtx *ctx) { RET(put_wide(A0, (u32)A1, "C:\\Windows")); }
// GetOverlappedResult(file, &overlapped, &bytes, wait) : les lectures sont menées tout de suite.
static void k_GetOverlappedResult(RcnCtx *ctx) {
    if (A2) wr32(A2, (u32)rd64(A1 + 8));
    RET(1);
}

// --- Temps local (= temps universel) et formats ---

static void k_GetLocalTime(RcnCtx *ctx) {
    struct timespec ts;
#if defined(_WIN32)
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    time_t seconds = ts.tv_sec;
    struct tm tm;
#if defined(_WIN32)
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    u64 st = A0;
    wr16(st, (u16)(tm.tm_year + 1900));
    wr16(st + 2, (u16)(tm.tm_mon + 1));
    wr16(st + 4, (u16)tm.tm_wday);
    wr16(st + 6, (u16)tm.tm_mday);
    wr16(st + 8, (u16)tm.tm_hour);
    wr16(st + 10, (u16)tm.tm_min);
    wr16(st + 12, (u16)tm.tm_sec);
    wr16(st + 14, (u16)(ts.tv_nsec / 1000000));
}
static void k_SystemTimeToTzSpecificLocalTime(RcnCtx *ctx) {
    memcpy(PTR(A2), PTR(A1), 16);
    RET(1);
}
// GetDateFormatW(locale, flags, &systemtime, format, buffer, size) : « MM/dd/yyyy ».
static void k_GetDateFormatW(RcnCtx *ctx) {
    u64 st = A2;
    char text[32];
    if (st) snprintf(text, sizeof text, "%02u/%02u/%04u", rd16(st + 2), rd16(st + 6), rd16(st));
    else snprintf(text, sizeof text, "01/01/2026");
    u32 n = put_wide(STK(4), (u32)STK(5), text);
    RET(STK(4) && (u32)STK(5) ? n + 1 : n);
}
// GetTimeFormatW(locale, flags, &systemtime, format, buffer, size) : « HH:mm:ss ».
static void k_GetTimeFormatW(RcnCtx *ctx) {
    u64 st = A2;
    char text[32];
    if (st) snprintf(text, sizeof text, "%02u:%02u:%02u", rd16(st + 8), rd16(st + 10), rd16(st + 12));
    else snprintf(text, sizeof text, "00:00:00");
    u32 n = put_wide(STK(4), (u32)STK(5), text);
    RET(STK(4) && (u32)STK(5) ? n + 1 : n);
}

// --- Langue : anglais des États-Unis ---

static void k_Lcid(RcnCtx *ctx) { RET(0x0409); }
static void k_GetUserDefaultLocaleName(RcnCtx *ctx) {
    u32 n = put_wide(A0, (u32)A1, "en-US");
    RET(A0 && (u32)A1 > n ? n + 1 : 0);
}
// GetLocaleInfoW(lcid, type, buffer, size) : les champs usuels ; avec LOCALE_RETURN_NUMBER, un nombre.
static void k_GetLocaleInfoW(RcnCtx *ctx) {
    u32 type = (u32)A1 & 0x0FFFFFFF, number = ((u32)A1 & 0x20000000) != 0;
    const char *text = "";
    u32 value = 0;
    switch (type) {
    case 0x59: text = "en"; break;                  // LOCALE_SISO639LANGNAME
    case 0x5A: text = "US"; break;                  // LOCALE_SISO3166CTRYNAME
    case 0x5C: text = "en-US"; break;               // LOCALE_SNAME
    case 0x0E: text = "."; break;                   // LOCALE_SDECIMAL
    case 0x0F: text = ","; break;                   // LOCALE_STHOUSAND
    case 0x1D: text = "/"; break;                   // LOCALE_SDATE
    case 0x1E: text = ":"; break;                   // LOCALE_STIME
    case 0x1F: text = "M/d/yyyy"; break;            // LOCALE_SSHORTDATE
    case 0x1001: text = "English"; break;           // LOCALE_SENGLANGUAGE
    case 0x1002: text = "United States"; break;     // LOCALE_SENGCOUNTRY
    case 0x14: text = "$"; break;                   // LOCALE_SCURRENCY
    case 0x1004: text = "1252"; value = 1252; break; // LOCALE_IDEFAULTANSICODEPAGE
    case 0x0B: text = "437"; value = 437; break;    // LOCALE_IDEFAULTCODEPAGE
    case 0x01: text = "0409"; value = 0x409; break; // LOCALE_ILANGUAGE
    case 0x05: text = "1"; value = 1; break;        // LOCALE_ICOUNTRY
    default: break;
    }
    if (number) {
        if (A2 && (u32)A3 >= 2) wr32(A2, value);
        RET(2);
        return;
    }
    u32 n = put_wide(A2, (u32)A3, text);
    RET(A2 && (u32)A3 > n ? n + 1 : n);
}
static void k_EnumSystemLocalesW(RcnCtx *ctx) {
    static u16 name[] = {'0', '0', '0', '0', '0', '4', '0', '9', 0};
    if (A0) rcn_call_guest(ctx, A0, (u64)(uintptr_t)name, 0, 0, 0);
    RET(1);
}

// --- Mémoire Global / Local (adresses directes) ---

// GlobalAlloc / LocalAlloc(flags, size) : GMEM_ZEROINIT / LMEM_ZEROINIT 0x40.
static void k_GlobalAlloc(RcnCtx *ctx) { RET((u64)(uintptr_t)rcn_galloc(ctx, (size_t)A1, ((u32)A0 & 0x40) != 0)); }
static void k_GlobalFree(RcnCtx *ctx) {
    rcn_gfree(ctx, PTR(A0));
    RET(0);
}
static void k_GlobalLock(RcnCtx *ctx) { RET(A0); }

// --- Processus ---

static void k_K32GetModuleBaseNameW(RcnCtx *ctx) { // (process, module, buffer, size)
    RcnLoaded *l = A1 ? rcn_loaded_at(ctx->proc, A1) : &ctx->proc->loaded[0];
    u32 n = put_wide(A2, (u32)A3, l && l->name ? l->name : "game.exe");
    RET(A2 && (u32)A3 > n ? n : 0);
}
// K32GetProcessMemoryInfo(process, &counters, size) : quelques centaines de Mo.
static void k_K32GetProcessMemoryInfo(RcnCtx *ctx) {
    u64 c = A1;
    u32 size = (u32)A2;
    memset(PTR(c + 4), 0, size > 4 ? size - 4 : 0);
    if (size >= 24) {
        wr64(c + 16, 512ull << 20); // WorkingSetSize
        wr64(c + 8, 512ull << 20);  // PeakWorkingSetSize (champ précédent : PageFaultCount)
    }
    RET(1);
}
// GetSystemPowerStatus(&status) : sur secteur.
static void k_GetSystemPowerStatus(RcnCtx *ctx) {
    wr8(A0, 1);       // ACLineStatus
    wr8(A0 + 1, 128); // BatteryFlag : pas de batterie
    wr8(A0 + 2, 255);
    wr8(A0 + 3, 0);
    wr32(A0 + 4, 0xFFFFFFFFu);
    wr32(A0 + 8, 0xFFFFFFFFu);
    RET(1);
}
static void k_GetSystemTimes(RcnCtx *ctx) {
    for (int i = 0; i < 3; i++) {
        u64 p = rcn_arg(ctx, i);
        if (p) wr64(p, 0);
    }
    RET(1);
}
static void k_ReadProcessMemory(RcnCtx *ctx) { // (process, address, buffer, size, &read)
    memcpy(PTR(A2), PTR(A1), (size_t)A3);
    if (STK(4)) wr64(STK(4), A3);
    RET(1);
}
// RtlCaptureStackBackTrace(skip, count, &frames, &hash) : aucune adresse.
static void k_RtlCaptureStackBackTrace(RcnCtx *ctx) {
    if (STK(3)) wr32(STK(3), 0);
    RET(0);
}
static void k_SetThreadAffinityMask(RcnCtx *ctx) { RET(1); }
static void k_FreeLibraryAndExitThread(RcnCtx *ctx) { rcn_thread_exit(ctx, (u32)A1); }
static void k_InitializeProcThreadAttributeList(RcnCtx *ctx) {
    if (A3) wr64(A3, 64);
    LAST_ERROR(ctx) = 122; // ERROR_INSUFFICIENT_BUFFER : la taille est demandée d'abord
    RET(A0 != 0);
}
static void k_OpenEventA(RcnCtx *ctx) {
    LAST_ERROR(ctx) = ERROR_FILE_NOT_FOUND;
    RET(0);
}

const RcnShim rcn_k32_x64b[] = {
    {"AttachConsole", k_Zero, 0},
    {"CancelIo", k_True, 0},
    {"CopyFileExW", k_CopyFileExW, 0},
    {"CopyFileW", k_CopyFileW, 0},
    {"CreateFileMappingW", k_CreateFileMappingW, 0},
    {"CreatePipe", k_Fail, 0},
    {"CreateProcessW", k_Fail, 0},
    {"CreateToolhelp32Snapshot", k_Invalid, 0},
    {"DeleteProcThreadAttributeList", k_Zero, 0},
    {"DeleteVolumeMountPointW", k_Fail, 0},
    {"EnumSystemLocalesW", k_EnumSystemLocalesW, 0},
    {"FormatMessageA", k_Fail, 0},
    {"FreeLibraryAndExitThread", k_FreeLibraryAndExitThread, 0},
    {"GetDateFormatW", k_GetDateFormatW, 0},
    {"GetDiskFreeSpaceExW", k_GetDiskFreeSpaceExW, 0},
    {"GetEnvironmentVariableA", k_Zero, 0},
    {"GetErrorMode", k_Zero, 0},
    {"GetLocalTime", k_GetLocalTime, 0},
    {"GetLocaleInfoW", k_GetLocaleInfoW, 0},
    {"GetLogicalDrives", k_GetLogicalDrives, 0},
    {"GetOverlappedResult", k_GetOverlappedResult, 0},
    {"GetSystemPowerStatus", k_GetSystemPowerStatus, 0},
    {"GetSystemTimes", k_GetSystemTimes, 0},
    {"GetTempFileNameW", k_GetTempFileNameW, 0},
    {"GetThreadLocale", k_Lcid, 0},
    {"GetTimeFormatW", k_GetTimeFormatW, 0},
    {"GetUserDefaultLCID", k_Lcid, 0},
    {"GetUserDefaultLocaleName", k_GetUserDefaultLocaleName, 0},
    {"GetWindowsDirectoryW", k_GetWindowsDirectoryW, 0},
    {"GlobalAlloc", k_GlobalAlloc, 0},
    {"GlobalFree", k_GlobalFree, 0},
    {"GlobalLock", k_GlobalLock, 0},
    {"GlobalUnlock", k_True, 0},
    {"HeapQueryInformation", k_Fail, 0},
    {"InitializeProcThreadAttributeList", k_InitializeProcThreadAttributeList, 0},
    {"IsValidLocale", k_True, 0},
    {"K32GetModuleBaseNameW", k_K32GetModuleBaseNameW, 0},
    {"K32GetProcessMemoryInfo", k_K32GetProcessMemoryInfo, 0},
    {"LocalAlloc", k_GlobalAlloc, 0},
    {"LocalFree", k_GlobalFree, 0},
    {"MapViewOfFile", k_MapViewOfFile, 0},
    {"MoveFileExW", k_MoveFileExW, 0},
    {"OpenEventA", k_OpenEventA, 0},
    {"OpenFileMappingW", k_Fail, 0},
    {"OpenThread", k_Fail, 0},
    {"ReadProcessMemory", k_ReadProcessMemory, 0},
    {"ReplaceFileW", k_ReplaceFileW, 0},
    {"RtlCaptureStackBackTrace", k_RtlCaptureStackBackTrace, 0},
    {"SetConsoleCtrlHandler", k_True, 0},
    {"SetDllDirectoryW", k_True, 0},
    {"SetErrorMode", k_Zero, 0},
    {"SetFileAttributesW", k_True, 0},
    {"SetFileInformationByHandle", k_True, 0},
    {"SetFileTime", k_True, 0},
    {"SetHandleInformation", k_True, 0},
    {"SetThreadAffinityMask", k_SetThreadAffinityMask, 0},
    {"SystemTimeToTzSpecificLocalTime", k_SystemTimeToTzSpecificLocalTime, 0},
    {"Thread32First", k_Fail, 0},
    {"Thread32Next", k_Fail, 0},
    {"UnmapViewOfFile", k_True, 0},
    {"UpdateProcThreadAttribute", k_True, 0},
};
const size_t rcn_k32_x64b_count = sizeof rcn_k32_x64b / sizeof rcn_k32_x64b[0];
