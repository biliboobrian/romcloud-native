// RomCloud Native : kernel32, suite, pour les moteurs récents (Unity…) : fichiers projetés en mémoire,
// copie et déplacement de fichiers, temps local et formats de date, langue, mémoire Global / Local,
// informations du processus. Processus fils, tubes et instantanés du système : refusés proprement.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
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

// --- Fichiers projetés ---
// Projection d'un fichier : sous Android et Linux, vraie projection privée (mmap) de la partie demandée — pages
// lues à la demande, que le système peut reprendre, rien dans le tas natif ; les écritures n'y retournent pas au
// fichier. Auparavant le fichier entier était copié dans le tas natif à chaque CreateFileMapping, jamais libéré
// (Clair Obscur : tas natif à 1,6 Go en jeu). Projection sans fichier (mémoire partagée), et sur PC : copie en
// mémoire, partagée par toutes ses vues.

typedef struct {
    u32 magic;
    u8 *data; // copie (sans fichier, ou hôte Windows) ; NULL : projection du fichier `fd`
    u64 size;
    int fd;
} Mapping;
#define MAPPING_MAGIC 0x4D415050u

#if !defined(_WIN32)
#include <sys/mman.h>
#include <unistd.h>
int rcn_file_fd_of(RcnCtx *ctx, u64 handle);
// Vues projetées (adresse rendue → début et longueur du mmap), pour UnmapViewOfFile.
typedef struct {
    u64 view;
    void *base;
    size_t length;
} MappedView;
static MappedView *mapped_views;
static size_t mapped_count, mapped_cap;
static pthread_mutex_t mapped_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

// CreateFileMappingW(file, security, protect, size_high, size_low, name)
static void k_CreateFileMappingW(RcnCtx *ctx) {
    u64 file = A0, size = ((u64)(u32)A3 << 32) | (u32)STK(4);
    u64 file_size = 0;
    if (file != (u64)(i64)INVALID_HANDLE) {
        u64 t = 0;
        int dir = 0, ok = 0;
        rcn_file_info(ctx, file, &file_size, &dir, &t, &ok);
        if (!ok) {
            LAST_ERROR(ctx) = 6;
            RET(0);
            return;
        }
        if (!size) size = file_size;
    }
    Mapping *m = calloc(1, sizeof *m);
    if (!m) {
        LAST_ERROR(ctx) = 8; // ERROR_NOT_ENOUGH_MEMORY
        RET(0);
        return;
    }
    m->magic = MAPPING_MAGIC;
    m->size = size;
    m->fd = -1;
#if !defined(_WIN32)
    // Fichier qui couvre toute la projection : projeté tel quel (descripteur gardé, la projection survit au
    // CloseHandle du fichier comme sous Windows).
    if (file != (u64)(i64)INVALID_HANDLE && size <= file_size) {
        int fd = rcn_file_fd_of(ctx, file);
        m->fd = fd >= 0 ? dup(fd) : -1;
        if (m->fd >= 0) {
            static int told;
            if (told++ < 16) fprintf(stderr, "rcn : fichier projete (%llu octets)\n", (unsigned long long)size);
            RET((u64)(uintptr_t)m);
            return;
        }
    }
#endif
    u8 *data = calloc(1, size ? size : 1);
    if (!data) {
        free(m);
        LAST_ERROR(ctx) = 8;
        RET(0);
        return;
    }
    if (file != (u64)(i64)INVALID_HANDLE) rcn_file_read_at(ctx, file, 0, data, size < file_size ? size : file_size);
    m->data = data;
    RET((u64)(uintptr_t)m);
}
// MapViewOfFile(mapping, access, offset_high, offset_low, bytes)
static void k_MapViewOfFile(RcnCtx *ctx) {
    Mapping *m = PTR(A0);
    u64 offset = ((u64)(u32)A2 << 32) | (u32)A3, bytes = STK(4);
    if (!m || m->magic != MAPPING_MAGIC || offset > m->size) {
        LAST_ERROR(ctx) = ERROR_INVALID_PARAMETER;
        RET(0);
        return;
    }
#if !defined(_WIN32)
    if (!m->data) {
        if (!bytes || offset + bytes > m->size) bytes = m->size - offset;
        u64 page = (u64)sysconf(_SC_PAGESIZE), start = offset & ~(page - 1);
        size_t length = (size_t)(offset - start + bytes);
        void *base = mmap(NULL, length ? length : 1, PROT_READ | PROT_WRITE, MAP_PRIVATE, m->fd, (off_t)start);
        if (base == MAP_FAILED) {
            LAST_ERROR(ctx) = 8;
            RET(0);
            return;
        }
        u64 view = (u64)(uintptr_t)base + (offset - start);
        pthread_mutex_lock(&mapped_lock);
        if (mapped_count == mapped_cap) {
            size_t cap = mapped_cap ? mapped_cap * 2 : 64;
            MappedView *grown = realloc(mapped_views, cap * sizeof *grown);
            if (grown) mapped_views = grown, mapped_cap = cap;
        }
        if (mapped_count < mapped_cap) mapped_views[mapped_count++] = (MappedView){view, base, length};
        pthread_mutex_unlock(&mapped_lock);
        RET(view);
        return;
    }
#endif
    (void)bytes;
    RET((u64)(uintptr_t)(m->data + offset));
}
// UnmapViewOfFile(adresse) : vue projetée rendue au système (les copies restent, partagées par leurs vues).
static void k_UnmapViewOfFile(RcnCtx *ctx) {
#if !defined(_WIN32)
    u64 view = A0;
    pthread_mutex_lock(&mapped_lock);
    for (size_t i = 0; i < mapped_count; i++) {
        if (mapped_views[i].view != view) continue;
        munmap(mapped_views[i].base, mapped_views[i].length);
        mapped_views[i] = mapped_views[--mapped_count];
        break;
    }
    pthread_mutex_unlock(&mapped_lock);
#endif
    RET(1);
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
// GetLocaleInfoW / GetLocaleInfoEx(locale, type, tampon, taille) : valeurs de en-US (globalisation de .NET,
// bibliothèque C). LOCALE_RETURN_NUMBER : la valeur en nombre (LOCALE_ILANGUAGE en hexadécimal).
static void k_GetLocaleInfoW(RcnCtx *ctx) {
    static const struct {
        u32 type;
        const char *text;
    } table[] = {
        {0x1, "0409"},
        {0x2, "English (United States)"},
        {0x3, "ENU"},
        {0x4, "English"},
        {0x5, "1"},
        {0x6, "United States"},
        {0x7, "USA"},
        {0x8, "United States"},
        {0x9, "0409"},
        {0xA, "1"},
        {0xB, "437"},
        {0xC, ","},
        {0xD, "1"},
        {0xE, "."},
        {0xF, ","},
        {0x10, "3;0"},
        {0x11, "2"},
        {0x12, "1"},
        {0x13, "0123456789"},
        {0x14, "$"},
        {0x15, "USD"},
        {0x16, "."},
        {0x17, ","},
        {0x18, "3;0"},
        {0x19, "2"},
        {0x1A, "2"},
        {0x1B, "0"},
        {0x1C, "0"},
        {0x1D, "/"},
        {0x1E, ":"},
        {0x1F, "M/d/yyyy"},
        {0x20, "dddd, MMMM d, yyyy"},
        {0x21, "0"},
        {0x22, "0"},
        {0x23, "0"},
        {0x24, "1"},
        {0x25, "0"},
        {0x26, "0"},
        {0x27, "0"},
        {0x28, "AM"},
        {0x29, "PM"},
        {0x2A, "Monday"},
        {0x2B, "Tuesday"},
        {0x2C, "Wednesday"},
        {0x2D, "Thursday"},
        {0x2E, "Friday"},
        {0x2F, "Saturday"},
        {0x30, "Sunday"},
        {0x31, "Mon"},
        {0x32, "Tue"},
        {0x33, "Wed"},
        {0x34, "Thu"},
        {0x35, "Fri"},
        {0x36, "Sat"},
        {0x37, "Sun"},
        {0x38, "January"},
        {0x39, "February"},
        {0x3A, "March"},
        {0x3B, "April"},
        {0x3C, "May"},
        {0x3D, "June"},
        {0x3E, "July"},
        {0x3F, "August"},
        {0x40, "September"},
        {0x41, "October"},
        {0x42, "November"},
        {0x43, "December"},
        {0x44, "Jan"},
        {0x45, "Feb"},
        {0x46, "Mar"},
        {0x47, "Apr"},
        {0x48, "May"},
        {0x49, "Jun"},
        {0x4A, "Jul"},
        {0x4B, "Aug"},
        {0x4C, "Sep"},
        {0x4D, "Oct"},
        {0x4E, "Nov"},
        {0x4F, "Dec"},
        {0x100E, ""},
        {0x100F, ""},
        {0x50, ""},
        {0x51, "-"},
        {0x52, "3"},
        {0x53, "0"},
        {0x54, "1"},
        {0x55, "0"},
        {0x56, "1"},
        {0x57, "0"},
        {0x59, "en"},
        {0x5A, "US"},
        {0x5B, "244"},
        {0x5C, "en-US"},
        {0x5D, "h:mm:ss"},
        {0x60, "Mo"},
        {0x61, "Tu"},
        {0x62, "We"},
        {0x63, "Th"},
        {0x64, "Fr"},
        {0x65, "Sa"},
        {0x66, "Su"},
        {0x67, "eng"},
        {0x68, "USA"},
        {0x69, "NaN"},
        {0x6A, "Infinity"},
        {0x6B, "-Infinity"},
        {0x6C, "Latn;"},
        {0x6D, "en"},
        {0x6E, "en-US"},
        {0x6F, "English"},
        {0x70, "0"},
        {0x71, "0"},
        {0x72, "English (United States)"},
        {0x73, "English (United States)"},
        {0x74, "1"},
        {0x75, "1"},
        {0x76, "%"},
        {0x77, "\xe2\x80\xb0"},
        {0x78, "MMMM d"},
        {0x79, "h:mm tt"},
        {0x7A, "dflt"},
        {0x7B, "en-US"},
        {0x7C, "dddd, MMMM d"},
        {0x7E, "a"},
        {0x7F, "p"},
        {0x1001, "English"},
        {0x1002, "United States"},
        {0x1003, "h:mm:ss tt"},
        {0x1004, "1252"},
        {0x1005, "0"},
        {0x1006, "MMMM yyyy"},
        {0x1007, "US Dollar"},
        {0x1008, "US Dollar"},
        {0x1009, "1"},
        {0x100A, "1"},
        {0x100B, "0"},
        {0x100C, "6"},
        {0x100D, "0"},
        {0x1010, "1"},
        {0x1011, "10000"},
        {0x1012, "037"},
        {0x1013, "Default"},
        {0x1014, "1"},
    };
    u32 type = (u32)A1 & 0x0FFFFFFF, number = ((u32)A1 & 0x20000000) != 0;
    const char *text = NULL;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        if (table[i].type == type) text = table[i].text;
    }
    if (!text) {
        LAST_ERROR(ctx) = 87; // ERROR_INVALID_PARAMETER : type inconnu
        RET(0);
        return;
    }
    if (number) {
        u32 value = (u32)strtoul(text, NULL, type == 0x01 || type == 0x09 ? 16 : 10);
        if (A2 && (u32)A3 >= 2) wr32(A2, value);
        RET(2);
        return;
    }
    u32 n = put_wide(A2, (u32)A3, text);
    RET(A2 && (u32)A3 > n ? n + 1 : n);
}
// Get{User,Thread,Process,System}PreferredUILanguages(flags, &count, buffer, &size) : « en-US »,
// liste terminée par deux zéros (7 caractères).
static void k_GetPreferredUILanguages(RcnCtx *ctx) {
    static const char list[] = "en-US";
    u64 count = A1, buffer = A2, size = A3;
    if (!size) {
        RET(0);
        return;
    }
    u32 need = (u32)sizeof list + 1;
    if (count) wr32(count, 1);
    if (buffer) {
        if (rd32(size) < need) {
            wr32(size, need);
            LAST_ERROR(ctx) = 122; // ERROR_INSUFFICIENT_BUFFER
            RET(0);
            return;
        }
        for (u32 i = 0; i < sizeof list; i++) wr16(buffer + 2 * i, (u8)list[i]);
        wr16(buffer + 2 * sizeof list, 0);
    }
    wr32(size, need);
    RET(1);
}
static void k_GetConsoleWindow(RcnCtx *ctx) { RET(0); }

// Appel d'une autre fonction fournie (variante W, Ex…) avec les registres déjà préparés.
static void call_shim(RcnCtx *ctx, const char *dll, const char *name) {
    u32 pop = 0;
    RcnFn fn = rcn_win32_lookup(dll, name, 0, &pop);
    if (fn) fn(ctx);
    else RET(0);
}
// Texte ANSI du jeu copié en UTF-16 dans sa mémoire (à libérer par rcn_gfree).
static u64 wide_copy(RcnCtx *ctx, u64 text) {
    if (!text) return 0;
    size_t n = strlen(STR(text));
    u16 *w = rcn_galloc(ctx, 2 * (n + 1), 0);
    if (!w) return 0;
    for (size_t i = 0; i <= n; i++) w[i] = (u8)STR(text)[i];
    return (u64)(uintptr_t)w;
}
// GlobalMemoryStatus(MEMORYSTATUS *) : 56 octets (tailles de 64 bits).
static void k_GlobalMemoryStatus(RcnCtx *ctx) {
    u64 total = 0, avail = 0;
    rcn_memory_status(&total, &avail);
    u64 s = A0;
    if (!s) return;
    wr32(s, 56);
    wr32(s + 4, total ? (u32)(100 - avail * 100 / total) : 0);
    wr64(s + 8, total), wr64(s + 16, avail), wr64(s + 24, total), wr64(s + 32, avail);
    wr64(s + 40, 128ull << 40), wr64(s + 48, 127ull << 40);
}
// GetModuleHandleExA(flags, nom ou adresse, &module) : par la variante W.
static void k_GetModuleHandleExA(RcnCtx *ctx) {
    u64 name = A1, wide = 0;
    if (!((u32)A0 & 4)) { // GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS : adresse, pas un nom
        wide = wide_copy(ctx, name);
        ctx->r[2] = wide;
    }
    call_shim(ctx, "kernel32", "GetModuleHandleExW");
    if (wide) rcn_gfree(ctx, PTR(wide));
}
// CreateDirectoryExW(modèle, dossier, sécurité) : CreateDirectoryW(dossier, sécurité).
static void k_CreateDirectoryExW(RcnCtx *ctx) {
    ctx->r[1] = A1;
    ctx->r[2] = A2;
    call_shim(ctx, "kernel32", "CreateDirectoryW");
}
// GetStringTypeExW(langue, type, texte, longueur, sortie) : GetStringTypeW(type, texte, longueur, sortie).
static void k_GetStringTypeExW(RcnCtx *ctx) {
    u64 out = STK(4);
    ctx->r[1] = A1, ctx->r[2] = A2, ctx->r[8] = A3, ctx->r[9] = out;
    call_shim(ctx, "kernel32", "GetStringTypeW");
}
// GetStringTypeExA(langue, CT_CTYPE1, texte, longueur, sortie) : classes des caractères ASCII.
static void k_GetStringTypeExA(RcnCtx *ctx) {
    const u8 *s = PTR(A2);
    i32 n = (i32)A3;
    u64 out = STK(4);
    if (n < 0) n = (i32)strlen((const char *)s);
    for (i32 i = 0; i < n && out; i++) {
        u8 c = s[i];
        u16 t = 0;
        if (c >= 'A' && c <= 'Z') t |= 0x101; // C1_UPPER | C1_ALPHA
        if (c >= 'a' && c <= 'z') t |= 0x102; // C1_LOWER | C1_ALPHA
        if (c >= '0' && c <= '9') t |= 0x004; // C1_DIGIT
        if (c == ' ' || (c >= 9 && c <= 13)) t |= 0x008; // C1_SPACE
        if (c == ' ') t |= 0x040; // C1_BLANK
        if (c < 32 || c == 127) t |= 0x020; // C1_CNTRL
        if (c > 32 && c < 127 && !(t & 0x107)) t |= 0x010; // C1_PUNCT
        if (isxdigit(c)) t |= 0x080; // C1_XDIGIT
        wr16(out + 2ull * (u32)i, t);
    }
    RET(1);
}
// ExpandEnvironmentStringsA(source, destination, taille) : texte recopié (aucune variable connue).
static void k_ExpandEnvironmentStringsA(RcnCtx *ctx) {
    u32 n = A0 ? (u32)strlen(STR(A0)) + 1 : 1;
    if (A1 && (u32)A2 >= n) memcpy(PTR(A1), A0 ? STR(A0) : "", n);
    RET(n);
}
// QueryFullProcessImageNameA(processus, options, tampon, &taille) : « C:\<exe> ».
static void k_QueryFullProcessImageNameA(RcnCtx *ctx) {
    char path[300];
    snprintf(path, sizeof path, "C:\\%s", ctx->proc->exe_name ? ctx->proc->exe_name : "game.exe");
    u32 n = (u32)strlen(path);
    if (!A2 || !A3 || rd32(A3) <= n) {
        LAST_ERROR(ctx) = 122;
        RET(0);
        return;
    }
    memcpy(PTR(A2), path, n + 1);
    wr32(A3, n);
    RET(1);
}
static void k_NotSupported(RcnCtx *ctx) {
    LAST_ERROR(ctx) = 50; // ERROR_NOT_SUPPORTED
    RET(0);
}
static void k_NotFound(RcnCtx *ctx) {
    LAST_ERROR(ctx) = 2; // ERROR_FILE_NOT_FOUND
    RET(0);
}
static void k_DriveFixed(RcnCtx *ctx) { RET(3); } // DRIVE_FIXED
static void k_GeoUnitedStates(RcnCtx *ctx) { RET(244); }
static void k_FindFirstFileExA(RcnCtx *ctx) {
    LAST_ERROR(ctx) = 2;
    RET(~0ull); // INVALID_HANDLE_VALUE
}
// Console absente : pas d'événement à lire.
static void k_GetNumberOfConsoleInputEvents(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    RET(1);
}
static void k_PeekConsoleInputA(RcnCtx *ctx) {
    if (A3) wr32(A3, 0);
    RET(1);
}
// RtlAddFunctionTable / RtlDeleteFunctionTable (programmes de MinGW) : le dérouleur lit déjà .pdata.
static void k_RtlAddFunctionTable(RcnCtx *ctx) { RET(1); }
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

// --- Calendriers (globalisation de .NET) : le seul calendrier grégorien, valeurs de en-US ---
// Valeur CAL_* du calendrier grégorien en texte, ou NULL.
static const char *calendar_info(u32 type) {
    static const char *const days[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
    static const char *const months[] = {"January", "February", "March", "April", "May", "June", "July",
                                         "August", "September", "October", "November", "December", ""};
    static char abbrev[8];
    switch (type) {
    case 1: return "1";                  // CAL_ICALINTVALUE : CAL_GREGORIAN
    case 2: return "Gregorian Calendar"; // CAL_SCALNAME
    case 3: return "0";                  // CAL_IYEAROFFSETRANGE
    case 4: return "A.D.";               // CAL_SERASTRING
    case 5: return "M/d/yyyy";           // CAL_SSHORTDATE
    case 6: return "dddd, MMMM d, yyyy"; // CAL_SLONGDATE
    case 47: return "MMMM yyyy";         // CAL_SYEARMONTH
    case 48: return "2049";              // CAL_ITWODIGITYEARMAX
    case 56: return "MMMM d";            // CAL_SMONTHDAY
    case 57: return "AD";                // CAL_SABBREVERASTRING
    case 58: return "dddd, MMMM d, yyyy"; // CAL_SRELATIVELONGDATE
    case 59: return "A.D.";              // CAL_SENGLISHERANAME
    case 60: return "AD";                // CAL_SENGLISHABBREVERANAME
    }
    if (type >= 7 && type <= 13) return days[type - 7];   // CAL_SDAYNAME1… (lundi d'abord)
    if (type >= 21 && type <= 33) return months[type - 21]; // CAL_SMONTHNAME1…13
    if ((type >= 14 && type <= 20) || (type >= 49 && type <= 55) || (type >= 34 && type <= 46)) {
        const char *full = type <= 20 ? days[type - 14] : type >= 49 ? days[type - 49] : months[type - 34];
        size_t n = type >= 49 ? 2 : 3; // jours les plus courts : 2 lettres
        snprintf(abbrev, sizeof abbrev, "%.*s", (int)n, full);
        return abbrev;
    }
    return NULL;
}
// GetCalendarInfoEx(locale, calendrier, réservé, type, tampon, taille, &valeur)
static void k_GetCalendarInfoEx(RcnCtx *ctx) {
    u32 type = (u32)STK(3), size = (u32)STK(5);
    u64 out = STK(4), value = STK(6);
    const char *text = calendar_info(type & 0xFFFF);
    if (!text) {
        LAST_ERROR(ctx) = 87; // ERROR_INVALID_PARAMETER
        RET(0);
        return;
    }
    if (type & 0x20000000u) { // CAL_RETURN_NUMBER
        if (value) wr32(value, (u32)strtoul(text, NULL, 10));
        RET(2);
        return;
    }
    u32 n = (u32)strlen(text) + 1;
    if (size == 0) {
        RET(n);
        return;
    }
    if (size < n) {
        LAST_ERROR(ctx) = 122; // ERROR_INSUFFICIENT_BUFFER
        RET(0);
        return;
    }
    for (u32 i = 0; i < n; i++) wr16(out + 2ull * i, (u8)text[i]);
    RET(n);
}
// Chaque format de la liste (terminée par NULL) donné à la fonction du jeu, tant qu'elle rend vrai.
// with_calendar : fonction(texte, calendrier, lParam) ; sinon fonction(texte, lParam).
static void enum_formats(RcnCtx *ctx, u64 proc, u64 lparam, const char *const *list, int with_calendar) {
    for (; proc && *list; list++) {
        size_t n = strlen(*list) + 1;
        u16 *w = rcn_galloc(ctx, 2 * n, 0);
        if (!w) break;
        for (size_t i = 0; i < n; i++) w[i] = (u8)(*list)[i];
        u64 r = with_calendar ? rcn_call_guest(ctx, proc, (u64)(uintptr_t)w, 1, lparam, 0)
                              : rcn_call_guest(ctx, proc, (u64)(uintptr_t)w, lparam, 0, 0);
        rcn_gfree(ctx, w);
        if (!(u32)r) break;
    }
}
// EnumTimeFormatsEx(fonction, locale, flags, lParam) : formats d'heure de en-US (TIME_NOSECONDS : 2).
static void k_EnumTimeFormatsEx(RcnCtx *ctx) {
    static const char *const full[] = {"h:mm:ss tt", "hh:mm:ss tt", "H:mm:ss", "HH:mm:ss", NULL};
    static const char *const short_[] = {"h:mm tt", "hh:mm tt", "H:mm", "HH:mm", NULL};
    enum_formats(ctx, A0, A3, ((u32)A2 & 2) ? short_ : full, 0);
    RET(1);
}
// EnumDateFormatsExEx(fonction, locale, flags, lParam) : formats de date de en-US.
static void k_EnumDateFormatsExEx(RcnCtx *ctx) {
    static const char *const short_[] = {"M/d/yyyy", "M/d/yy", "MM/dd/yy", "MM/dd/yyyy", "yy/MM/dd", "yyyy-MM-dd", "dd-MMM-yy", NULL};
    static const char *const long_[] = {"dddd, MMMM d, yyyy", "MMMM d, yyyy", "dddd, d MMMM, yyyy", "d MMMM, yyyy", NULL};
    static const char *const year_month[] = {"MMMM yyyy", "MMMM, yyyy", NULL};
    static const char *const month_day[] = {"MMMM d", NULL};
    u32 flags = (u32)A2;
    const char *const *list = (flags & 2) ? long_ : (flags & 8) ? year_month : (flags & 0x80) ? month_day : short_;
    enum_formats(ctx, A0, A3, list, 1);
    RET(1);
}
// EnumCalendarInfoExEx(fonction, locale, calendrier, réservé, type, lParam) : fonction(texte, calendrier,
// réservé, lParam) appelée pour le calendrier grégorien.
static void k_EnumCalendarInfoExEx(RcnCtx *ctx) {
    u64 proc = A0, lparam = STK(5);
    u32 calendar = (u32)A2, type = (u32)STK(4);
    const char *text = calendar_info(type & 0xFFFF);
    if (!proc || !text || (calendar != 1 && calendar != 0xFFFFFFFFu)) {
        RET(text ? 1 : 0);
        return;
    }
    size_t n = strlen(text) + 1;
    u16 *w = rcn_galloc(ctx, 2 * n, 0);
    if (!w) {
        RET(0);
        return;
    }
    for (size_t i = 0; i < n; i++) w[i] = (u8)text[i];
    rcn_call_guest(ctx, proc, (u64)(uintptr_t)w, 1, 0, lparam);
    rcn_gfree(ctx, w);
    RET(1);
}


// --- Unreal Engine 5 : processeurs (un seul nœud NUMA, un seul groupe), versions, processus ---

u32 rcn_cpu_count(void);
static u64 all_cpus(void) {
    u32 n = rcn_cpu_count();
    return n >= 64 ? ~0ULL : (1ULL << n) - 1;
}
// GetVersion : Windows 8 (6.2), ce que rend Windows 10 à un programme sans manifeste.
// SetDllDirectoryW(dossier) / AddDllDirectory(dossier) : dossiers où chercher les DLL (rcn_runtime.c).
static void add_dll_dir(RcnCtx *ctx) {
    if (!A0) return;
    size_t n = 0;
    while (rd16(A0 + 2 * n)) n++;
    char *dir = rcn_utf16_to_utf8(PTR(A0), (int)n);
    rcn_dll_dir_add(ctx, dir);
    free(dir);
}
static void k_SetDllDirectoryW(RcnCtx *ctx) {
    add_dll_dir(ctx);
    RET(1);
}
static void k_AddDllDirectory(RcnCtx *ctx) {
    static u32 cookie;
    add_dll_dir(ctx);
    RET(A0 ? 0xDD000000u + ++cookie : 0);
}
// lstrcmpW(a, b) : -1, 0 ou 1.
static void k_lstrcmpW(RcnCtx *ctx) {
    const u16 *a = PTR(A0), *b = PTR(A1);
    if (!a || !b) {
        RET_I32(a == b ? 0 : a ? 1 : -1);
        return;
    }
    while (*a && *a == *b) a++, b++;
    RET_I32(*a < *b ? -1 : *a > *b ? 1 : 0);
}
static void k_GetVersion(RcnCtx *ctx) { RET(0x23F00206u); }
// GetNumaProcessorNode(processeur, &nœud)
static void k_GetNumaProcessorNode(RcnCtx *ctx) {
    if (A1) *(u8 *)PTR(A1) = 0;
    RET(1);
}
// GetNumaProcessorNodeEx(&PROCESSOR_NUMBER, &nœud u16)
static void k_GetNumaProcessorNodeEx(RcnCtx *ctx) {
    if (A1) wr16(A1, 0);
    RET(1);
}
// GetNumaNodeProcessorMask(nœud, &masque)
static void k_GetNumaNodeProcessorMask(RcnCtx *ctx) {
    if (A1) wr64(A1, (u8)A0 == 0 ? all_cpus() : 0);
    RET((u8)A0 == 0);
}
// GetNumaNodeProcessorMaskEx(nœud, &GROUP_AFFINITY { masque, groupe, réservé[3] })
static void k_GetNumaNodeProcessorMaskEx(RcnCtx *ctx) {
    if (A1) {
        memset(PTR(A1), 0, 16);
        wr64(A1, (u16)A0 == 0 ? all_cpus() : 0);
    }
    RET((u16)A0 == 0);
}
// GetThreadIdealProcessorEx(thread, &PROCESSOR_NUMBER { groupe u16, numéro u8, réservé u8 })
static void k_GetThreadIdealProcessorEx(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    RET(1);
}
// GetProcessHandleCount(processus, &nombre)
static void k_GetProcessHandleCount(RcnCtx *ctx) {
    if (A1) wr32(A1, 100);
    RET(1);
}
// GetProcessTimes(processus, &création, &fin, &noyau, &utilisateur) : temps du processus de l'hôte.
static void k_GetProcessTimes(RcnCtx *ctx) {
    u64 user = (u64)clock() * (10000000ULL / CLOCKS_PER_SEC);
    if (A1) wr64(A1, 0);
    if (A2) wr64(A2, 0);
    if (A3) wr64(A3, 0);
    if (STK(4)) wr64(STK(4), user);
    RET(1);
}
// IsProcessInJob(processus, job, &résultat)
static void k_IsProcessInJob(RcnCtx *ctx) {
    if (A2) wr32(A2, 0);
    RET(1);
}


// --- WaitOnAddress / WakeByAddress : attente tant que la valeur à l'adresse n'a pas changé ---
// Une seule file pour toutes les adresses : un réveil réveille tout le monde, chacun relit sa valeur.
static pthread_mutex_t address_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t address_cond = PTHREAD_COND_INITIALIZER;
static u64 address_wakes;
static int address_same(u64 address, u64 compare, u32 size) {
    switch (size) {
    case 1: return __atomic_load_n((u8 *)PTR(address), __ATOMIC_SEQ_CST) == rd8(compare);
    case 2: return __atomic_load_n((u16 *)PTR(address), __ATOMIC_SEQ_CST) == rd16(compare);
    case 4: return __atomic_load_n((u32 *)PTR(address), __ATOMIC_SEQ_CST) == rd32(compare);
    default: return __atomic_load_n((u64 *)PTR(address), __ATOMIC_SEQ_CST) == rd64(compare);
    }
}
// WaitOnAddress(adresse, &valeur comparée, taille, délai ms)
static void k_WaitOnAddress(RcnCtx *ctx) {
    u64 address = A0, compare = A1;
    u32 size = (u32)A2, ms = (u32)A3;
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    if (ms != 0xFFFFFFFFu) {
        deadline.tv_sec += ms / 1000;
        deadline.tv_nsec += (long)(ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) deadline.tv_sec++, deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&address_lock);
    int timed_out = 0;
    while (address_same(address, compare, size)) {
        if (ms == 0) {
            timed_out = 1;
            break;
        }
        // Tranches d'une milliseconde : un réveil manqué (valeur changée sans Wake) ne bloque pas.
        struct timespec slice;
        clock_gettime(CLOCK_REALTIME, &slice);
        slice.tv_nsec += 1000000L;
        if (slice.tv_nsec >= 1000000000L) slice.tv_sec++, slice.tv_nsec -= 1000000000L;
        if (ms != 0xFFFFFFFFu && (slice.tv_sec > deadline.tv_sec || (slice.tv_sec == deadline.tv_sec && slice.tv_nsec > deadline.tv_nsec)))
            slice = deadline;
        pthread_cond_timedwait(&address_cond, &address_lock, &slice);
        if (ms != 0xFFFFFFFFu) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                timed_out = address_same(address, compare, size);
                break;
            }
        }
    }
    pthread_mutex_unlock(&address_lock);
    if (timed_out) LAST_ERROR(ctx) = 1460; // ERROR_TIMEOUT
    RET(!timed_out);
}
static void k_WakeByAddress(RcnCtx *ctx) {
    (void)ctx;
    pthread_mutex_lock(&address_lock);
    address_wakes++;
    pthread_cond_broadcast(&address_cond);
    pthread_mutex_unlock(&address_lock);
}


// --- InitOnceBeginInitialize / InitOnceComplete : initialisation unique en deux temps ---
// Mot INIT_ONCE : 0 pas commencée, 1 en cours (un thread initialise), sinon terminée : contexte | 2.
static void k_InitOnceBeginInitialize(RcnCtx *ctx) {
    u64 *word = PTR(A0);
    u32 flags = (u32)A1;
    for (;;) {
        u64 v = __atomic_load_n(word, __ATOMIC_ACQUIRE);
        if (v & 2) { // terminée
            if (A2) wr32(A2, 0);
            if (A3) wr64(A3, v & ~3ull);
            RET(1);
            return;
        }
        if (flags & 1) { // INIT_ONCE_CHECK_ONLY : pas encore terminée
            LAST_ERROR(ctx) = 31; // ERROR_GEN_FAILURE
            RET(0);
            return;
        }
        if (v == 0) {
            u64 expected = 0;
            if (__atomic_compare_exchange_n(word, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                if (A2) wr32(A2, 1); // à ce thread d'initialiser
                RET(1);
                return;
            }
            continue;
        }
        if (flags & 2) { // INIT_ONCE_ASYNC : pas d'attente
            if (A2) wr32(A2, 1);
            RET(1);
            return;
        }
        rcn_sleep_ms(0); // un autre thread initialise : attente
    }
}
static void k_InitOnceComplete(RcnCtx *ctx) {
    u64 *word = PTR(A0);
    u32 flags = (u32)A1;
    if (flags & 4) __atomic_store_n(word, 0, __ATOMIC_RELEASE); // INIT_ONCE_INIT_FAILED : à refaire
    else __atomic_store_n(word, (A2 & ~3ull) | 2, __ATOMIC_RELEASE);
    RET(1);
}

const RcnShim rcn_k32_x64b[] = {
    {"InitOnceBeginInitialize", k_InitOnceBeginInitialize, 0},
    {"InitOnceComplete", k_InitOnceComplete, 0},
    {"WaitOnAddress", k_WaitOnAddress, 0},
    {"SetThreadDescription", k_Zero, 0},
    {"CancelWaitableTimer", k_True, 0},
    {"lstrcmpW", k_lstrcmpW, 0},
    {"WakeByAddressSingle", k_WakeByAddress, 0},
    {"WakeByAddressAll", k_WakeByAddress, 0},
    {"SetThreadStackGuarantee", k_True, 0},
    {"WerRegisterRuntimeExceptionModule", k_Zero, 0},
    {"GetVersion", k_GetVersion, 0},
    {"AllocConsole", k_True, 0},
    {"FreeConsole", k_True, 0},
    {"CreateJobObjectW", k_Fail, 0},
    {"AssignProcessToJobObject", k_True, 0},
    {"SetInformationJobObject", k_True, 0},
    {"QueryInformationJobObject", k_Fail, 0},
    {"IsProcessInJob", k_IsProcessInJob, 0},
    {"CreateMemoryResourceNotification", k_Fail, 0},
    {"QueryMemoryResourceNotification", k_Fail, 0},
    {"CreateNamedPipeW", k_Invalid, 0},
    {"WaitNamedPipeW", k_Fail, 0},
    {"SetNamedPipeHandleState", k_Fail, 0},
    {"TransactNamedPipe", k_Fail, 0},
    {"GetDllDirectoryW", k_Zero, 0},
    {"GetNumaProcessorNode", k_GetNumaProcessorNode, 0},
    {"GetNumaProcessorNodeEx", k_GetNumaProcessorNodeEx, 0},
    {"GetNumaNodeProcessorMask", k_GetNumaNodeProcessorMask, 0},
    {"GetNumaNodeProcessorMaskEx", k_GetNumaNodeProcessorMaskEx, 0},
    {"GetThreadIdealProcessorEx", k_GetThreadIdealProcessorEx, 0},
    {"SetThreadGroupAffinity", k_True, 0},
    {"GetProcessHandleCount", k_GetProcessHandleCount, 0},
    {"GetProcessTimes", k_GetProcessTimes, 0},
    {"LockFileEx", k_True, 0},
    {"UnlockFileEx", k_True, 0},
    {"FlushViewOfFile", k_True, 0},
    {"K32EnumProcessModules", k_Fail, 0},
    {"K32EnumProcessModulesEx", k_Fail, 0},
    {"K32GetModuleFileNameExW", k_Zero, 0},
    {"Module32FirstW", k_Fail, 0},
    {"Module32NextW", k_Fail, 0},
    {"Process32FirstW", k_Fail, 0},
    {"Process32NextW", k_Fail, 0},
    {"SetConsoleScreenBufferSize", k_True, 0},
    {"SetConsoleWindowInfo", k_True, 0},
    {"FindResourceW", k_Zero, 0},
    {"LoadResource", k_Zero, 0},
    {"LockResource", k_Zero, 0},
    {"SizeofResource", k_Zero, 0},
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
    {"GetLocaleInfoEx", k_GetLocaleInfoW, 0},
    {"GetCalendarInfoEx", k_GetCalendarInfoEx, 0},
    {"EnumCalendarInfoExEx", k_EnumCalendarInfoExEx, 0},
    {"EnumTimeFormatsEx", k_EnumTimeFormatsEx, 0},
    {"EnumDateFormatsExEx", k_EnumDateFormatsExEx, 0}, // (nom de langue, type, tampon, taille) : mêmes réponses
    {"GetUserPreferredUILanguages", k_GetPreferredUILanguages, 0},
    {"GetThreadPreferredUILanguages", k_GetPreferredUILanguages, 0},
    {"GetProcessPreferredUILanguages", k_GetPreferredUILanguages, 0},
    {"GetSystemPreferredUILanguages", k_GetPreferredUILanguages, 0},
    {"GetConsoleWindow", k_GetConsoleWindow, 0},
    {"GlobalMemoryStatus", k_GlobalMemoryStatus, 0},
    {"DisableThreadLibraryCalls", k_True, 0},
    {"GetModuleHandleExA", k_GetModuleHandleExA, 0},
    {"CreateDirectoryExW", k_CreateDirectoryExW, 0},
    {"GetStringTypeExW", k_GetStringTypeExW, 0},
    {"GetStringTypeExA", k_GetStringTypeExA, 0},
    {"ExpandEnvironmentStringsA", k_ExpandEnvironmentStringsA, 0},
    {"QueryFullProcessImageNameA", k_QueryFullProcessImageNameA, 0},
    {"VerifyVersionInfoA", k_True, 0},
    {"SetCurrentDirectoryA", k_True, 0},
    {"SetProcessAffinityMask", k_True, 0},
    {"TerminateThread", k_NotSupported, 0},
    {"QueueUserAPC", k_NotSupported, 0},
    {"DeviceIoControl", k_NotSupported, 0},
    {"CreateProcessA", k_NotFound, 0},
    {"FindFirstFileExA", k_FindFirstFileExA, 0},
    {"FlushConsoleInputBuffer", k_True, 0},
    {"GetNumberOfConsoleInputEvents", k_GetNumberOfConsoleInputEvents, 0},
    {"PeekConsoleInputA", k_PeekConsoleInputA, 0},
    {"ReadConsoleInputA", k_Zero, 0},
    {"SetConsoleMode", k_True, 0},
    {"RtlAddFunctionTable", k_RtlAddFunctionTable, 0},
    {"RtlDeleteFunctionTable", k_RtlAddFunctionTable, 0},
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
    {"SetConsoleCtrlHandler", k_True, 0},
    {"SetDllDirectoryW", k_SetDllDirectoryW, 0},
    {"AddDllDirectory", k_AddDllDirectory, 0},
    {"RemoveDllDirectory", k_True, 0},
    {"SetErrorMode", k_Zero, 0},
    {"SetFileAttributesW", k_True, 0},
    {"SetFileInformationByHandle", k_True, 0},
    {"SetFileTime", k_True, 0},
    {"SetHandleInformation", k_True, 0},
    {"SetThreadAffinityMask", k_SetThreadAffinityMask, 0},
    {"SystemTimeToTzSpecificLocalTime", k_SystemTimeToTzSpecificLocalTime, 0},
    {"Thread32First", k_Fail, 0},
    {"Thread32Next", k_Fail, 0},
    {"UnmapViewOfFile", k_UnmapViewOfFile, 0},
    {"UpdateProcThreadAttribute", k_True, 0},
    {"CreateHardLinkW", k_Fail, 0},
    {"CreateSymbolicLinkW", k_Fail, 0},
    {"FreeLibraryWhenCallbackReturns", k_Zero, 0},
    {"GetDriveTypeA", k_DriveFixed, 0},
    {"GetGeoInfoW", k_Fail, 0},
    {"GetUserGeoID", k_GeoUnitedStates, 0},
    {"HeapSetInformation", k_True, 0},
    {"OpenMutexA", k_NotFound, 0},
    {"ReadFileEx", k_Fail, 0},
    {"WriteFileEx", k_Fail, 0},
    {"SetFileAttributesA", k_True, 0},
    {"SetThreadIdealProcessor", k_Zero, 0},
    {"WaitForThreadpoolTimerCallbacks", k_Zero, 0},
};
const size_t rcn_k32_x64b_count = sizeof rcn_k32_x64b / sizeof rcn_k32_x64b[0];
