// RomCloud Native : fonctions système appelées par les programmes .NET compilés d'avance (bibliothèque
// de classes : System.Diagnostics.Process, TimeZoneInfo, Environment, Path, Registry, EventSource…).
// Le jeu est seul sur sa machine : un processus (le sien), pas de registre, l'heure UTC, une console
// absente. Les fonctions sans objet réussissent sans rien faire ou échouent comme Windows le ferait.
#include <stdlib.h>
#include <time.h>

#include "rcn_internal.h"

enum {
    ERROR_FILE_NOT_FOUND = 2,
    ERROR_INVALID_HANDLE = 6,
    ERROR_NOT_SUPPORTED = 50,
    ERROR_INVALID_PARAMETER = 87,
    ERROR_INSUFFICIENT_BUFFER = 122,
    ERROR_NO_MORE_ITEMS = 259,
    ERROR_NOT_FOUND = 1168,
    STILL_ACTIVE = 259,
    // Pseudo-handle rendu par OpenProcess pour le processus du jeu (fermé sans effet par CloseHandle).
    PROCESS_HANDLE = 0x7C,
};

static char *utf8(u64 text) { return text ? rcn_utf16_to_utf8(PTR(text), -1) : NULL; }

// Écrit `text` (UTF-8) en UTF-16 dans buffer (size caractères). Convention de GetTempPathW et
// semblables : longueur sans le zéro si elle tient, sinon taille nécessaire avec le zéro.
static u32 put_wide(u64 buffer, u32 size, const char *text) {
    size_t len = rcn_utf8_to_utf16(text, strlen(text), NULL);
    if (!buffer || size <= len) return (u32)len + 1;
    rcn_utf8_to_utf16(text, strlen(text), PTR(buffer));
    wr16(buffer + 2 * len, 0);
    return (u32)len;
}

// Texte UTF-16 dans un bloc alloué pour le jeu (libéré par CoTaskMemFree).
static u64 alloc_wide(RcnCtx *ctx, const char *text) {
    size_t len = rcn_utf8_to_utf16(text, strlen(text), NULL);
    u16 *p = rcn_galloc(ctx, 2 * (len + 1), 0);
    if (!p) return 0;
    rcn_utf8_to_utf16(text, strlen(text), p);
    p[len] = 0;
    return (u64)(uintptr_t)p;
}

// --- Processus ---

// OpenProcess(access, inherit, pid) : seul le processus du jeu existe.
static void k32_OpenProcess(RcnCtx *ctx) {
    if ((u32)A2 == 1000) {
        RET(PROCESS_HANDLE);
        return;
    }
    LAST_ERROR(ctx) = ERROR_INVALID_PARAMETER;
    RET(0);
}
static void k32_GetProcessId(RcnCtx *ctx) { RET(1000); }
static void k32_GetExitCodeProcess(RcnCtx *ctx) {
    wr32(A1, STILL_ACTIVE);
    RET(1);
}
static void k32_True(RcnCtx *ctx) { RET(1); }
// K32EnumProcesses(ids, size, &needed)
static void k32_K32EnumProcesses(RcnCtx *ctx) {
    if ((u32)A1 >= 4) wr32(A0, 1000);
    wr32(A2, 4);
    RET(1);
}
// SetThreadErrorMode(mode, &old)
static void k32_SetThreadErrorMode(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    RET(1);
}

// --- Heure ---

static u64 filetime_now(void) {
    struct timespec ts;
#if defined(_WIN32)
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return ((u64)ts.tv_sec + 11644473600ull) * 10000000ull + (u64)ts.tv_nsec / 100;
}
static void k32_GetSystemTimePreciseAsFileTime(RcnCtx *ctx) { wr64(A0, filetime_now()); }
// QueryUnbiasedInterruptTime(&ticks) : temps écoulé, en centaines de nanosecondes.
static void k32_QueryUnbiasedInterruptTime(RcnCtx *ctx) {
    struct timespec ts;
#if defined(_WIN32)
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    wr64(A0, (u64)ts.tv_sec * 10000000ull + (u64)ts.tv_nsec / 100);
    RET(1);
}
// SYSTEMTIME : année, mois, jour de la semaine, jour, heure, minute, seconde, millisecondes (16 bits).
static void write_systemtime(u64 st, u64 ft) {
    time_t seconds = (time_t)(ft / 10000000ull - 11644473600ull);
    struct tm tm;
#if defined(_WIN32)
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    wr16(st, (u16)(tm.tm_year + 1900));
    wr16(st + 2, (u16)(tm.tm_mon + 1));
    wr16(st + 4, (u16)tm.tm_wday);
    wr16(st + 6, (u16)tm.tm_mday);
    wr16(st + 8, (u16)tm.tm_hour);
    wr16(st + 10, (u16)tm.tm_min);
    wr16(st + 12, (u16)tm.tm_sec);
    wr16(st + 14, (u16)(ft / 10000 % 1000));
}
static void k32_GetSystemTime(RcnCtx *ctx) { write_systemtime(A0, filetime_now()); }
// SystemTimeToFileTime(&systemtime, &filetime)
static void k32_SystemTimeToFileTime(RcnCtx *ctx) {
    u64 st = A0;
    i64 y = rd16(st), m = rd16(st + 2), d = rd16(st + 6);
    if (m < 1 || m > 12 || d < 1 || d > 31) {
        LAST_ERROR(ctx) = ERROR_INVALID_PARAMETER;
        RET(0);
        return;
    }
    // Jours depuis le 1er janvier 1601 (calendrier grégorien).
    if (m <= 2) {
        y--;
        m += 12;
    }
    i64 days = 365 * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + d - 1;
    i64 base = 365 * 1600 + 1600 / 4 - 1600 / 100 + 1600 / 400 + (153 * 10 + 2) / 5; // 1er janvier 1601
    u64 seconds = (u64)(days - base) * 86400 + rd16(st + 8) * 3600ull + rd16(st + 10) * 60ull + rd16(st + 12);
    wr64(A1, seconds * 10000000ull + rd16(st + 14) * 10000ull);
    RET(1);
}
// Fuseau horaire : UTC, sans heure d'été (TIME_ZONE_ID_UNKNOWN).
static void k32_GetTimeZoneInformation(RcnCtx *ctx) {
    memset(PTR(A0), 0, 172);
    RET(0);
}
static void k32_GetDynamicTimeZoneInformation(RcnCtx *ctx) {
    memset(PTR(A0), 0, 432);
    put_wide(A0 + 172, 128, "UTC"); // TimeZoneKeyName
    RET(0);
}
// TzSpecificLocalTimeToSystemTime(zone, &local, &universal) : heure locale = UTC.
static void k32_TzSpecificLocalTimeToSystemTime(RcnCtx *ctx) {
    memcpy(PTR(A2), PTR(A1), 16);
    RET(1);
}

// --- Chemins ---
// Le dossier du jeu est « C:\ » pour lui ; ses dossiers système et temporaires sont dessous.

// Dossier temporaire, créé au besoin (.NET y teste la casse des noms de fichiers).
static void k32_GetTempPathW(RcnCtx *ctx) {
    rcn_make_guest_dir(ctx, "C:\\Temp");
    RET(put_wide(A1, (u32)A0, "C:\\Temp\\"));
}
static void k32_GetSystemDirectoryW(RcnCtx *ctx) { RET(put_wide(A0, (u32)A1, "C:\\Windows\\System32")); }
static void k32_GetComputerNameW(RcnCtx *ctx) {
    u32 size = rd32(A1);
    u32 n = put_wide(A0, size, "RCN");
    if (n >= size) {
        wr32(A1, n);
        LAST_ERROR(ctx) = 111; // ERROR_BUFFER_OVERFLOW
        RET(0);
        return;
    }
    wr32(A1, n);
    RET(1);
}
// GetFullPathNameW(name, size, buffer, &file_part) : chemin complet (rcn_full_path).
static void k32_GetFullPathNameW(RcnCtx *ctx) {
    char *name = utf8(A0);
    if (!name) {
        LAST_ERROR(ctx) = ERROR_INVALID_PARAMETER;
        RET(0);
        return;
    }
    char *full = rcn_full_path(ctx, name);
    if (!full) {
        free(name);
        RET(0);
        return;
    }
    u32 n = put_wide(A2, (u32)A1, full);
    if (A3 && n < (u32)A1) {
        // Dernier élément du chemin.
        char *last = strrchr(full, '\\');
        u64 at = last && last[1] ? A2 + 2 * rcn_utf8_to_utf16(full, (size_t)(last + 1 - full), NULL) : 0;
        wrp(ctx, A3, at);
    }
    free(name);
    free(full);
    RET(n);
}
// GetLongPathNameW(short, long, size) : les chemins sont déjà longs.
static void k32_GetLongPathNameW(RcnCtx *ctx) {
    char *name = utf8(A0);
    u32 n = name ? put_wide(A1, (u32)A2, name) : 0;
    free(name);
    RET(n);
}
// ExpandEnvironmentStringsW(source, destination, size) : aucune variable, texte inchangé ; renvoie
// le nombre de caractères avec le zéro.
static void k32_ExpandEnvironmentStringsW(RcnCtx *ctx) {
    char *text = utf8(A0);
    u32 n = text ? put_wide(A1, (u32)A2, text) : 0;
    u32 len = text ? (u32)rcn_utf8_to_utf16(text, strlen(text), NULL) : 0;
    free(text);
    RET(n == len ? len + 1 : n);
}

// --- Texte, langue, version ---

// CompareStringOrdinal(a, len_a, b, len_b, ignore_case) : CSTR_LESS_THAN 1, CSTR_EQUAL 2, CSTR_GREATER_THAN 3.
static void k32_CompareStringOrdinal(RcnCtx *ctx) {
    u64 a = A0, b = A2;
    i64 la = (i32)A1, lb = (i32)A3;
    int fold = (u32)rcn_arg(ctx, 4) != 0;
    if (la < 0) for (la = 0; rd16(a + 2 * la); la++) {}
    if (lb < 0) for (lb = 0; rd16(b + 2 * lb); lb++) {}
    for (i64 i = 0; i < la && i < lb; i++) {
        u16 x = rd16(a + 2 * i), y = rd16(b + 2 * i);
        if (fold) {
            if (x >= 'a' && x <= 'z') x -= 32;
            if (y >= 'a' && y <= 'z') y -= 32;
        }
        if (x != y) {
            RET(x < y ? 1 : 3);
            return;
        }
    }
    RET(la == lb ? 2 : la < lb ? 1 : 3);
}
static void k32_LocaleNameToLCID(RcnCtx *ctx) { RET(0x0409); }
// GetCPInfoExW(code_page, flags, &info) : CPINFOEXW (MaxCharSize, DefaultChar, LeadByte, UnicodeDefaultChar,
// CodePage, CodePageName).
static void k32_GetCPInfoExW(RcnCtx *ctx) {
    u64 info = A2;
    u32 cp = (u32)A0;
    memset(PTR(info), 0, 544);
    wr32(info, cp == 65001 ? 4 : 1);
    wr8(info + 4, '?');
    wr16(info + 18, '?');
    wr32(info + 20, cp ? cp : 1252);
    put_wide(info + 24, 260, cp == 65001 ? "65001 (UTF-8)" : "1252 (ANSI - Latin I)");
    RET(1);
}
// GetVersionExW(&info) : Windows 10 (10.0.19041).
static void k32_GetVersionExW(RcnCtx *ctx) {
    u64 info = A0;
    wr32(info + 4, 10);
    wr32(info + 8, 0);
    wr32(info + 12, 19041);
    wr32(info + 16, 2); // VER_PLATFORM_WIN32_NT
    wr16(info + 20, 0);
    RET(1);
}
// VerSetConditionMask(mask, type, condition) : combiné comme par Windows (3 bits par type).
static void k32_VerSetConditionMask(RcnCtx *ctx) {
    u64 mask = A0;
    u32 type = (u32)A1;
    u64 cond = A2 & 7;
    for (int i = 0; i < 8; i++) {
        if (type & (1u << i)) mask |= cond << (3 * i);
    }
    RET(mask);
}
// FormatMessageW(flags, source, id, langue, tampon, taille, arguments) : pour une erreur système
// (FORMAT_MESSAGE_FROM_SYSTEM 0x1000), le texte « Error <id> » (le jeu affiche ainsi le vrai code, pas celui
// d'un échec de FormatMessage) ; FORMAT_MESSAGE_ALLOCATE_BUFFER (0x100) : tampon alloué, rendu dans *tampon.
// Messages d'une bibliothèque ou d'un texte donné : aucun.
static void k32_FormatMessageW(RcnCtx *ctx) {
    u32 flags = (u32)A0, id = (u32)A2, size = (u32)STK(5);
    u64 buffer = STK(4);
    if (!(flags & 0x1000) || !buffer) {
        LAST_ERROR(ctx) = ERROR_NOT_FOUND;
        RET(0);
        return;
    }
    char text[32];
    int n = snprintf(text, sizeof text, "Error %u", id);
    u64 out = buffer;
    if (flags & 0x100) {
        out = (u64)(uintptr_t)rcn_galloc(ctx, (size_t)(n + 1) * 2, 0);
        if (!out) {
            RET(0);
            return;
        }
        wrp(ctx, buffer, out);
    } else if (size <= (u32)n) {
        LAST_ERROR(ctx) = 122; // ERROR_INSUFFICIENT_BUFFER
        RET(0);
        return;
    }
    for (int i = 0; i <= n; i++) wr16(out + 2 * (u64)i, (u8)text[i]);
    RET((u32)n);
}
static void k32_LoadStringW(RcnCtx *ctx) { RET(0); }
static void k32_GetFileMUIPath(RcnCtx *ctx) {
    LAST_ERROR(ctx) = ERROR_FILE_NOT_FOUND;
    RET(0);
}

// --- Console absente ---

static void k32_NoConsole(RcnCtx *ctx) {
    LAST_ERROR(ctx) = ERROR_INVALID_HANDLE;
    RET(0);
}
static void k32_CancelIoEx(RcnCtx *ctx) {
    LAST_ERROR(ctx) = ERROR_NOT_FOUND;
    RET(0);
}
static void k32_GetFileInformationByHandleEx(RcnCtx *ctx) {
    LAST_ERROR(ctx) = ERROR_NOT_SUPPORTED;
    RET(0);
}

// CreateMutexExW(attributes, name, flags, access) : CREATE_MUTEX_INITIAL_OWNER = 1.
static void k32_CreateMutexExW(RcnCtx *ctx) { rcn_k32_create_mutex(ctx, ((u32)A2 & 1) != 0); }

static void k32_DecodePointer(RcnCtx *ctx) { RET(A0); }
// GetThreadTimes(thread, &creation, &exit, &kernel, &user) : temps nuls.
static void k32_GetThreadTimes(RcnCtx *ctx) {
    for (int i = 1; i <= 4; i++) {
        if (STK(i)) wr64(STK(i), 0);
    }
    RET(1);
}
static void k32_GetDriveTypeW(RcnCtx *ctx) { RET(3); } // DRIVE_FIXED
static void k32_GetSystemDirectoryA(RcnCtx *ctx) {
    static const char dir[] = "C:\\Windows\\System32";
    u32 n = sizeof dir - 1;
    if (A0 && (u32)A1 > n) {
        memcpy(PTR(A0), dir, n + 1);
        RET(n);
    } else {
        RET(n + 1);
    }
}
// OutputDebugStringW : sur la sortie d'erreur du jeu ; RCN_ODS_LOG=1, aussi dans le journal du runtime.
static void k32_OutputDebugStringW(RcnCtx *ctx) {
    char *text = utf8(A0);
    if (text) rcn_host_write(ctx, RCN_STREAM_STDERR, text, strlen(text));
    static int log = -1;
    if (log < 0) log = getenv("RCN_ODS_LOG") != NULL;
    if (log && text) rcn_log("OutputDebugString : %s", text);
    free(text);
}
static void k32_PeekNamedPipe(RcnCtx *ctx) {
    LAST_ERROR(ctx) = ERROR_INVALID_HANDLE;
    RET(0);
}
// GetFileInformationByHandle(handle, &info) : à partir de GetFileInformationByHandleEx (classe standard).
static void k32_GetFileInformationByHandle(RcnCtx *ctx) {
    u64 size = 0, t = 0;
    int dir = 0, ok = 0;
    rcn_file_info(ctx, A0, &size, &dir, &t, &ok);
    if (!ok) {
        LAST_ERROR(ctx) = ERROR_INVALID_HANDLE;
        RET(0);
        return;
    }
    u64 info = A1;
    memset(PTR(info), 0, 52);
    wr32(info, dir ? 0x10 : 0x20);
    for (int i = 0; i < 3; i++) wr64(info + 4 + 8 * (u64)i, t);
    wr32(info + 28, 1);                  // numéro de volume
    wr32(info + 32, (u32)(size >> 32));
    wr32(info + 36, (u32)size);
    wr32(info + 40, 1);                  // liens
    RET(1);
}
// RtlUnwindEx : déroulement d'exception (C++, SEH), pas encore fourni.
static void k32_RtlUnwindEx(RcnCtx *ctx) { rcn_fail(ctx, "exception du jeu (RtlUnwindEx) : pas encore geree"); }

const RcnShim rcn_k32_x64[] = {
    {"AreFileApisANSI", k32_True, 0},
    {"DecodePointer", k32_DecodePointer, S32(1)},
    {"GetDriveTypeW", k32_GetDriveTypeW, 0},
    {"GetFileInformationByHandle", k32_GetFileInformationByHandle, 0},
    {"GetSystemDirectoryA", k32_GetSystemDirectoryA, S32(2)},
    {"OutputDebugStringW", k32_OutputDebugStringW, S32(1)},
    {"PeekNamedPipe", k32_PeekNamedPipe, 0},
    {"SetEnvironmentVariableA", k32_True, S32(2)},
    {"CancelIoEx", k32_CancelIoEx, 0},
    {"CompareStringOrdinal", k32_CompareStringOrdinal, 0},
    {"CreateMutexExW", k32_CreateMutexExW, 0},
    {"ExpandEnvironmentStringsW", k32_ExpandEnvironmentStringsW, 0},
    {"FormatMessageW", k32_FormatMessageW, S32(7)},
    {"GetCPInfoExW", k32_GetCPInfoExW, 0},
    {"GetComputerNameW", k32_GetComputerNameW, S32(2)},
    {"GetConsoleScreenBufferInfo", k32_NoConsole, S32(2)},
    {"GetDynamicTimeZoneInformation", k32_GetDynamicTimeZoneInformation, 0},
    {"GetExitCodeProcess", k32_GetExitCodeProcess, 0},
    {"GetFileMUIPath", k32_GetFileMUIPath, 0},
    {"GetFullPathNameW", k32_GetFullPathNameW, S32(4)},
    {"GetLongPathNameW", k32_GetLongPathNameW, 0},
    {"GetProcessId", k32_GetProcessId, 0},
    {"GetSystemDirectoryW", k32_GetSystemDirectoryW, 0},
    {"GetSystemTime", k32_GetSystemTime, S32(1)},
    {"GetSystemTimePreciseAsFileTime", k32_GetSystemTimePreciseAsFileTime, 0},
    {"GetTempPathW", k32_GetTempPathW, S32(2)},
    {"GetThreadTimes", k32_GetThreadTimes, S32(5)},
    {"GetTimeZoneInformation", k32_GetTimeZoneInformation, S32(1)},
    {"GetVersionExW", k32_GetVersionExW, S32(1)},
    {"K32EnumProcesses", k32_K32EnumProcesses, 0},
    {"LoadStringW", k32_LoadStringW, 0},
    {"LocaleNameToLCID", k32_LocaleNameToLCID, 0},
    {"OpenProcess", k32_OpenProcess, S32(3)},
    {"QueryUnbiasedInterruptTime", k32_QueryUnbiasedInterruptTime, 0},
    {"ReadConsoleW", k32_NoConsole, 0},
    {"SetConsoleTextAttribute", k32_True, S32(2)},
    {"SetConsoleTitleW", k32_True, S32(1)},
    {"SetConsoleTitleA", k32_True, 0},
    {"SetCurrentDirectoryW", k32_True, S32(1)},
    {"SetProcessWorkingSetSize", k32_True, 0},
    {"SetThreadErrorMode", k32_SetThreadErrorMode, 0},
    {"SystemTimeToFileTime", k32_SystemTimeToFileTime, S32(2)},
    {"TzSpecificLocalTimeToSystemTime", k32_TzSpecificLocalTimeToSystemTime, 0},
    {"VerSetConditionMask", k32_VerSetConditionMask, S32(4)},
    {"VerifyVersionInfoW", k32_True, S32(4)},
};
const size_t rcn_k32_x64_count = sizeof rcn_k32_x64 / sizeof rcn_k32_x64[0];

// --- advapi32 : registre vide, traces ETW inactives ---

static void reg_NotFound(RcnCtx *ctx) { RET(ERROR_FILE_NOT_FOUND); }
static void reg_NoMoreItems(RcnCtx *ctx) { RET(ERROR_NO_MORE_ITEMS); }
static void reg_Success(RcnCtx *ctx) { RET(0); }
static void etw_NotSupported(RcnCtx *ctx) { RET(ERROR_NOT_SUPPORTED); }
const RcnShim rcn_advapi32_x64[] = {
    {"EnumerateTraceGuidsEx", etw_NotSupported, 0},
    {"EventActivityIdControl", reg_Success, 0},
    {"EventSetInformation", reg_Success, 0},
    {"EventWriteTransfer", reg_Success, 0},
    {"RegCloseKey", reg_Success, S32(1)},
    {"RegEnumKeyExA", reg_NoMoreItems, 0},
    {"RegOpenKeyExA", reg_NotFound, S32(5)},
    {"RegQueryInfoKeyA", reg_NotFound, 0},
    {"RegQueryValueExA", reg_NotFound, S32(6)},
    {"RegConnectRegistryW", reg_NotFound, 0},
    {"RegEnumKeyExW", reg_NoMoreItems, 0},
    {"RegEnumValueW", reg_NoMoreItems, S32(8)},
    {"RegOpenKeyExW", reg_NotFound, S32(5)},
    {"RegQueryValueExW", reg_NotFound, S32(6)},
    {"RegDeleteValueW", reg_NotFound, S32(2)},
    {"RegQueryInfoKeyW", reg_NotFound, S32(12)},
};
const size_t rcn_advapi32_x64_count = sizeof rcn_advapi32_x64 / sizeof rcn_advapi32_x64[0];

// --- ole32, oleaut32 : mémoire partagée avec le jeu, identifiants ---

static void ole_CoTaskMemAlloc(RcnCtx *ctx) { RET((u64)(uintptr_t)rcn_galloc(ctx, (size_t)A0, 0)); }
static void ole_CoTaskMemFree(RcnCtx *ctx) { rcn_gfree(ctx, PTR(A0)); }
// SysFreeString : la chaîne est précédée de sa longueur (4 octets).
static void ole_SysFreeString(RcnCtx *ctx) {
    if (A0) rcn_gfree(ctx, PTR(A0 - 4));
}
// CoCreateGuid(&guid) : version 4, aléatoire.
static void ole_CoCreateGuid(RcnCtx *ctx) {
    for (int i = 0; i < 16; i++) wr8(A0 + i, (u8)(rand() >> 4));
    wr8(A0 + 7, (u8)((rd8(A0 + 7) & 0x0F) | 0x40));
    wr8(A0 + 8, (u8)((rd8(A0 + 8) & 0x3F) | 0x80));
    RET(0);
}
// CLSIDFromString(text, &clsid) : « {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} ».
static int hex_digit(u16 c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static void ole_CLSIDFromString(RcnCtx *ctx) {
    u64 s = A0, out = A1;
    // Positions des chiffres (deux par octet) dans le texte, octets dans l'ordre de la structure GUID.
    static const u8 pos[16] = {7, 5, 3, 1, 12, 10, 17, 15, 20, 22, 25, 27, 29, 31, 33, 35};
    if (!s || rd16(s) != '{') {
        RET(0x800401F3u); // CO_E_CLASSSTRING
        return;
    }
    for (int i = 0; i < 16; i++) {
        int hi = hex_digit(rd16(s + 2 * (u64)pos[i])), lo = hex_digit(rd16(s + 2 * (u64)pos[i] + 2));
        if (hi < 0 || lo < 0) {
            RET(0x800401F3u);
            return;
        }
        wr8(out + i, (u8)(hi << 4 | lo));
    }
    RET(0);
}
static void ole_PropVariantClear(RcnCtx *ctx) {
    if (A0) memset(PTR(A0), 0, 24);
    RET(0);
}
const RcnShim rcn_ole32_x64[] = {
    {"CLSIDFromString", ole_CLSIDFromString, 0},
    {"PropVariantClear", ole_PropVariantClear, 0},
    {"CoCreateGuid", ole_CoCreateGuid, S32(1)},
    {"CoTaskMemAlloc", ole_CoTaskMemAlloc, 0},
    {"CoTaskMemFree", ole_CoTaskMemFree, 0},
    {"SysFreeString", ole_SysFreeString, 0},
};
const size_t rcn_ole32_x64_count = sizeof rcn_ole32_x64 / sizeof rcn_ole32_x64[0];

// --- shell32 : dossiers connus, sous « C:\Users\Player » ---

// SHGetKnownFolderPath(&folder_id, flags, token, &path)
static void sh_SHGetKnownFolderPath(RcnCtx *ctx) {
    static const struct {
        u32 data1;
        const char *path;
    } folders[] = {
        {0x3EB685DB, "C:\\Users\\Player\\AppData\\Roaming"},       // RoamingAppData
        {0xF1B32785, "C:\\Users\\Player\\AppData\\Local"},         // LocalAppData
        {0xFDD39AD0, "C:\\Users\\Player\\Documents"},              // Documents
        {0x4C5C32FF, "C:\\Users\\Player\\Saved Games"},            // SavedGames
        {0x5E6C858F, "C:\\Users\\Player"},                         // Profile
        {0x62AB5D82, "C:\\ProgramData"},                           // ProgramData
        {0xB4BFCC3A, "C:\\Users\\Player\\Desktop"},                // Desktop
    };
    u32 id = A0 ? rd32(A0) : 0;
    const char *path = NULL;
    for (size_t i = 0; i < sizeof folders / sizeof folders[0]; i++) {
        if (folders[i].data1 == id) path = folders[i].path;
    }
    if (!path) path = "C:\\Users\\Player";
    u64 text = alloc_wide(ctx, path);
    wr64(A3, text);
    RET(text ? 0 : 0x8007000Eu); // E_OUTOFMEMORY
}
const RcnShim rcn_shell32_x64[] = {
    {"SHGetKnownFolderPath", sh_SHGetKnownFolderPath, 0},
};
const size_t rcn_shell32_x64_count = sizeof rcn_shell32_x64 / sizeof rcn_shell32_x64[0];

// --- ntdll ---

static void nt_NotImplemented(RcnCtx *ctx) { RET(0xC0000002u); } // STATUS_NOT_IMPLEMENTED
const RcnShim rcn_ntdll_x64[] = {
    {"NtQuerySystemInformation", nt_NotImplemented, 0},
    {"NtQueryDirectoryFile", rcn_nt_QueryDirectoryFile, 0},
};
const size_t rcn_ntdll_x64_count = sizeof rcn_ntdll_x64 / sizeof rcn_ntdll_x64[0];
