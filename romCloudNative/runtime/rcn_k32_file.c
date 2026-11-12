// RomCloud Native : kernel32, fichiers — CreateFile, ReadFile, WriteFile, position, taille,
// attributs, dossiers. Les chemins du jeu sont ramenés à son dossier (rcn_host_path) ; les fonctions
// en « W » convertissent leur chemin UTF-16 puis font comme celles en « A ».
//
// Pas encore fournis : énumération des dossiers (FindFirstFile), lecture asynchrone (OVERLAPPED),
// fichiers projetés en mémoire, verrouillage et partage (les modes de partage sont ignorés).
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#include "rcn_internal.h"

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#define open _open
#define read _read
#define write _write
#define lseek _lseeki64
#define unlink _unlink
#define rmdir _rmdir
#define fsync _commit
#define make_dir(path) _mkdir(path)
#define OPEN_FLAGS _O_BINARY
#else
#include <unistd.h>
#define make_dir(path) mkdir(path, 0777)
#define OPEN_FLAGS 0
#define _chsize_s ftruncate
#endif

enum {
    HANDLE_STDIN = 0x10,
    HANDLE_STDOUT = 0x11,
    HANDLE_STDERR = 0x12,
    INVALID_HANDLE_VALUE = -1,
    GENERIC_READ = 0x80000000u,
    GENERIC_WRITE = 0x40000000u,
    CREATE_NEW = 1,
    CREATE_ALWAYS = 2,
    OPEN_EXISTING = 3,
    OPEN_ALWAYS = 4,
    TRUNCATE_EXISTING = 5,
    FILE_ATTRIBUTE_DIRECTORY = 0x10,
    FILE_ATTRIBUTE_ARCHIVE = 0x20, // attribut d'un fichier ordinaire tout juste créé
    FILE_ATTRIBUTE_NORMAL = 0x80,
    INVALID_FILE_ATTRIBUTES = 0xFFFFFFFFu,
    INVALID_SET_FILE_POINTER = 0xFFFFFFFFu,
    FILE_TYPE_DISK = 1,
    FILE_TYPE_CHAR = 2,
    ERROR_FILE_NOT_FOUND = 2,
    ERROR_PATH_NOT_FOUND = 3,
    ERROR_ACCESS_DENIED = 5,
    ERROR_INVALID_HANDLE = 6,
    ERROR_NOT_ENOUGH_MEMORY = 8,
    ERROR_GEN_FAILURE = 31,
    ERROR_FILE_EXISTS = 80,
    ERROR_INVALID_PARAMETER = 87,
    ERROR_DISK_FULL = 112,
    ERROR_DIR_NOT_EMPTY = 145,
    ERROR_ALREADY_EXISTS = 183,
};

u32 rcn_win32_error(int host_errno) {
    switch (host_errno) {
    case 0: return 0;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EISDIR: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_FILE_EXISTS;
    case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
    case ENOSPC: return ERROR_DISK_FULL;
    case ENOTEMPTY: return ERROR_DIR_NOT_EMPTY;
    case EINVAL: return ERROR_INVALID_PARAMETER;
    case EBADF: return ERROR_INVALID_HANDLE;
    default: return ERROR_GEN_FAILURE;
    }
}

// Chemin de l'hôte d'un argument du jeu, en « A » ou en « W » (UTF-16). À libérer.
static char *arg_path(RcnCtx *ctx, u64 arg, int wide) {
    if (!arg) return NULL;
    if (!wide) return rcn_host_path(ctx, STR(arg));
    char *utf8 = rcn_utf16_to_utf8(PTR(arg), -1);
    char *path = utf8 ? rcn_host_path(ctx, utf8) : NULL;
    free(utf8);
    return path;
}

// Descripteur de l'hôte d'un handle de fichier, ou -1 (erreur notée).
static int file_fd(RcnCtx *ctx, u64 handle) {
    RcnProcess *proc = ctx->proc;
    pthread_mutex_lock(&proc->lock);
    RcnObject *object = rcn_handle_get(proc, handle, RCN_OBJ_FILE);
    int fd = object ? object->fd : -1;
    pthread_mutex_unlock(&proc->lock);
    if (fd < 0) LAST_ERROR(ctx) = ERROR_INVALID_HANDLE;
    return fd;
}

// CreateFile(path, access, share, security, disposition, flags, template)
static void create_file(RcnCtx *ctx, int wide) {
    RcnProcess *proc = ctx->proc;
    u32 access = (u32)A1, disposition = (u32)STK(4);
    int flags = OPEN_FLAGS;
    flags |= (access & GENERIC_WRITE) ? ((access & GENERIC_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    switch (disposition) {
    case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: flags |= O_CREAT; break;
    case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
    default: break;
    }
    char *path = arg_path(ctx, A0, wide);
    int fd = path ? open(path, flags, 0666) : -1;
    int error = errno;
    free(path);
    RcnObject *object = fd >= 0 ? calloc(1, sizeof *object) : NULL;
    if (!object) {
        if (fd >= 0) close(fd);
        LAST_ERROR(ctx) = fd >= 0 ? ERROR_NOT_ENOUGH_MEMORY : rcn_win32_error(error);
        RET(INVALID_HANDLE_VALUE);
        return;
    }
    object->type = RCN_OBJ_FILE;
    object->fd = fd;
    pthread_mutex_lock(&proc->lock);
    u64 handle = rcn_handle_new(proc, object);
    pthread_mutex_unlock(&proc->lock);
    // Windows signale ainsi qu'un fichier existant a été ouvert par CREATE_ALWAYS / OPEN_ALWAYS ; l'hôte
    // ne le dit pas : toujours 0.
    LAST_ERROR(ctx) = 0;
    RET(handle ? handle : (u64)INVALID_HANDLE_VALUE);
}
static void k32_CreateFileA(RcnCtx *ctx) { create_file(ctx, 0); }
static void k32_CreateFileW(RcnCtx *ctx) { create_file(ctx, 1); }

// ReadFile(handle, buffer, length, &read, overlapped)
static void k32_ReadFile(RcnCtx *ctx) {
    u64 count_out = A3;
    if (count_out) wr32(count_out, 0);
    if (A0 == HANDLE_STDIN) { // entrée standard : toujours vide
        RET(1);
        return;
    }
    int fd = file_fd(ctx, A0);
    if (fd < 0) {
        RET(0);
        return;
    }
    // Jusqu'à la longueur demandée ou la fin du fichier (une lecture de l'hôte peut être partielle).
    u8 *buffer = PTR(A1);
    u32 length = (u32)A2, total = 0;
    while (total < length) {
        long n = (long)read(fd, buffer + total, length - total);
        if (n < 0) {
            LAST_ERROR(ctx) = rcn_win32_error(errno);
            RET(0);
            return;
        }
        if (n == 0) break;
        total += (u32)n;
    }
    if (count_out) wr32(count_out, total);
    RET(1);
}

// WriteFile(handle, buffer, length, &written, overlapped) : console ou fichier.
static void k32_WriteFile(RcnCtx *ctx) {
    u64 handle = A0, count_out = A3;
    const u8 *buffer = PTR(A1);
    u32 length = (u32)A2, total = 0;
    if (handle == HANDLE_STDOUT || handle == HANDLE_STDERR) {
        rcn_host_write(ctx, handle == HANDLE_STDERR ? RCN_STREAM_STDERR : RCN_STREAM_STDOUT, buffer, length);
        total = length;
    } else {
        int fd = file_fd(ctx, handle);
        if (count_out) wr32(count_out, 0);
        if (fd < 0) {
            RET(0);
            return;
        }
        while (total < length) {
            long n = (long)write(fd, buffer + total, length - total);
            if (n <= 0) {
                LAST_ERROR(ctx) = rcn_win32_error(n < 0 ? errno : ENOSPC);
                RET(0);
                return;
            }
            total += (u32)n;
        }
    }
    if (count_out) wr32(count_out, total);
    RET(1);
}

// SetFilePointerEx(handle, distance, &new_position, method) ; method : 0 début, 1 position, 2 fin.
static void k32_SetFilePointerEx(RcnCtx *ctx) {
    int fd = file_fd(ctx, A0);
    i64 pos = fd >= 0 && (u32)A3 <= 2 ? (i64)lseek(fd, (i64)A1, (int)A3) : -1;
    if (pos < 0) {
        if (fd >= 0) LAST_ERROR(ctx) = ERROR_INVALID_PARAMETER;
        RET(0);
        return;
    }
    if (A2) wr64(A2, (u64)pos);
    RET(1);
}
// SetFilePointer(handle, distance_low, &distance_high, method) : renvoie les 32 bits bas de la position.
static void k32_SetFilePointer(RcnCtx *ctx) {
    int fd = file_fd(ctx, A0);
    i64 distance = A2 ? (i64)(((u64)rd32(A2) << 32) | (u32)A1) : (i64)(i32)A1;
    i64 pos = fd >= 0 && (u32)A3 <= 2 ? (i64)lseek(fd, distance, (int)A3) : -1;
    if (pos < 0) {
        if (fd >= 0) LAST_ERROR(ctx) = ERROR_INVALID_PARAMETER;
        RET(INVALID_SET_FILE_POINTER);
        return;
    }
    if (A2) wr32(A2, (u32)((u64)pos >> 32));
    LAST_ERROR(ctx) = 0; // distingue une position 0xFFFFFFFF d'une erreur
    RET((u32)pos);
}

static i64 file_size(int fd) {
    struct stat st;
    return fstat(fd, &st) == 0 ? (i64)st.st_size : -1;
}
// GetFileSizeEx(handle, &size)
static void k32_GetFileSizeEx(RcnCtx *ctx) {
    int fd = file_fd(ctx, A0);
    i64 size = fd >= 0 ? file_size(fd) : -1;
    if (size >= 0) wr64(A1, (u64)size);
    RET(size >= 0);
}
// GetFileSize(handle, &size_high)
static void k32_GetFileSize(RcnCtx *ctx) {
    int fd = file_fd(ctx, A0);
    i64 size = fd >= 0 ? file_size(fd) : -1;
    if (size < 0) {
        RET(0xFFFFFFFFu);
        return;
    }
    if (A1) wr32(A1, (u32)((u64)size >> 32));
    LAST_ERROR(ctx) = 0;
    RET((u32)size);
}
static void k32_FlushFileBuffers(RcnCtx *ctx) {
    if (A0 == HANDLE_STDOUT || A0 == HANDLE_STDERR) {
        RET(1);
        return;
    }
    int fd = file_fd(ctx, A0);
    RET(fd >= 0 && fsync(fd) == 0);
}
static void k32_GetFileType(RcnCtx *ctx) {
    if (A0 >= HANDLE_STDIN && A0 <= HANDLE_STDERR) {
        RET(FILE_TYPE_CHAR);
        return;
    }
    RET(file_fd(ctx, A0) >= 0 ? FILE_TYPE_DISK : 0);
}

// Fonctions à un chemin : résultat BOOL d'une fonction de l'hôte qui renvoie 0 en cas de succès.
#define PATH_BOOL(name, wide, call)                           \
    static void k32_##name(RcnCtx *ctx) {                     \
        char *path = arg_path(ctx, A0, wide);                 \
        int failed = path ? (call) != 0 : 1;                  \
        if (failed) LAST_ERROR(ctx) = rcn_win32_error(path ? errno : ENOMEM); \
        free(path);                                           \
        RET(!failed);                                         \
    }
PATH_BOOL(DeleteFileA, 0, unlink(path))
PATH_BOOL(DeleteFileW, 1, unlink(path))
PATH_BOOL(RemoveDirectoryA, 0, rmdir(path))
PATH_BOOL(RemoveDirectoryW, 1, rmdir(path))

// CreateDirectory(path, security) : un dossier déjà présent est une erreur (ERROR_ALREADY_EXISTS).
static void create_directory(RcnCtx *ctx, int wide) {
    char *path = arg_path(ctx, A0, wide);
    int failed = path ? make_dir(path) != 0 : 1;
    if (failed) LAST_ERROR(ctx) = errno == EEXIST ? ERROR_ALREADY_EXISTS : rcn_win32_error(path ? errno : ENOMEM);
    free(path);
    RET(!failed);
}
static void k32_CreateDirectoryA(RcnCtx *ctx) { create_directory(ctx, 0); }
static void k32_CreateDirectoryW(RcnCtx *ctx) { create_directory(ctx, 1); }

static void get_attributes(RcnCtx *ctx, int wide) {
    char *path = arg_path(ctx, A0, wide);
    struct stat st;
    if (!path || stat(path, &st) != 0) {
        LAST_ERROR(ctx) = rcn_win32_error(path ? errno : ENOMEM);
        RET(INVALID_FILE_ATTRIBUTES);
    } else {
        RET(S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE);
    }
    free(path);
}
static void k32_GetFileAttributesA(RcnCtx *ctx) { get_attributes(ctx, 0); }
static void k32_GetFileAttributesW(RcnCtx *ctx) { get_attributes(ctx, 1); }

// --- Dates ---

// Secondes depuis 1970 → FILETIME (centaines de nanosecondes depuis 1601).
static u64 filetime_of(i64 seconds) { return ((u64)seconds + 11644473600ull) * 10000000ull; }
// GetFileTime(handle, &creation, &access, &write) : la date de modification pour les trois.
static void k32_GetFileTime(RcnCtx *ctx) {
    int fd = file_fd(ctx, A0);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) {
        RET(0);
        return;
    }
    for (int i = 1; i <= 3; i++) {
        if (rcn_arg(ctx, i)) wr64(rcn_arg(ctx, i), filetime_of((i64)st.st_mtime));
    }
    RET(1);
}
// FileTimeToLocalFileTime(&in, &out) : l'heure locale est tenue pour l'heure universelle.
static void k32_FileTimeToLocalFileTime(RcnCtx *ctx) {
    wr64(A1, rd64(A0));
    RET(1);
}
// FileTimeToSystemTime(&filetime, &systemtime) : année, mois, jour de la semaine, jour, heure, minute,
// seconde, milliseconde (8 mots de 16 bits).
static void k32_FileTimeToSystemTime(RcnCtx *ctx) {
    u64 ticks = rd64(A0);
    time_t seconds = (time_t)(ticks / 10000000ull - 11644473600ull);
    struct tm *tm = gmtime(&seconds);
    if (!tm) {
        RET(0);
        return;
    }
    u16 fields[8] = {(u16)(tm->tm_year + 1900), (u16)(tm->tm_mon + 1), (u16)tm->tm_wday, (u16)tm->tm_mday,
                     (u16)tm->tm_hour, (u16)tm->tm_min, (u16)tm->tm_sec, (u16)(ticks / 10000 % 1000)};
    memcpy(PTR(A1), fields, sizeof fields);
    RET(1);
}

// --- Divers ---

// SetEndOfFile(handle) : coupe (ou étend) le fichier à la position courante.
static void k32_SetEndOfFile(RcnCtx *ctx) {
    int fd = file_fd(ctx, A0);
    i64 pos = fd >= 0 ? (i64)lseek(fd, 0, 1) : -1;
    RET(pos >= 0 && _chsize_s(fd, pos) == 0);
}
// CopyFileA(source, destination, fail_if_exists)
static void k32_CopyFileA(RcnCtx *ctx) {
    char *from = arg_path(ctx, A0, 0), *to = arg_path(ctx, A1, 0);
    int in = from ? open(from, O_RDONLY | OPEN_FLAGS) : -1;
    int out = in >= 0 && to ? open(to, O_WRONLY | O_CREAT | (A2 ? O_EXCL : O_TRUNC) | OPEN_FLAGS, 0666) : -1;
    int ok = in >= 0 && out >= 0;
    if (!ok) LAST_ERROR(ctx) = rcn_win32_error(errno);
    char buffer[16384];
    for (long n; ok && (n = (long)read(in, buffer, sizeof buffer)) != 0;) {
        if (n < 0 || write(out, buffer, (unsigned)n) != n) ok = 0;
    }
    if (in >= 0) close(in);
    if (out >= 0) close(out);
    free(from);
    free(to);
    RET(ok);
}
// GetFullPathNameA(name, size, buffer, &file_part) : chemin complet sous « C:\ », le dossier du jeu.
static void k32_GetFullPathNameA(RcnCtx *ctx) {
    const char *name = STR(A0);
    u32 size = (u32)A1;
    u64 buffer = A2, part = A3;
    int has_drive = name[0] && name[1] == ':';
    while (!has_drive && (*name == '\\' || *name == '/')) name++;
    size_t len = strlen(name) + (has_drive ? 0 : 3);
    if (len + 1 > size) {
        RET(len + 1);
        return;
    }
    char *out = PTR(buffer);
    if (!has_drive) memcpy(out, "C:\\", 3);
    strcpy(out + (has_drive ? 0 : 3), name);
    char *last = out;
    for (char *p = out; *p; p++) {
        if (*p == '/') *p = '\\';
        if (*p == '\\') last = p + 1;
    }
    if (part) wrp(ctx, part, *last ? (u64)(uintptr_t)last : 0);
    RET(len);
}

// --- Énumération d'un dossier (FindFirstFile) ---

enum { FIND_SLOTS = 16, HANDLE_FIND = 0x800, ERROR_NO_MORE_FILES = 18 };
static struct {
    DIR *dir;
    char *path;       // dossier de l'hôte
    char pattern[260]; // motif (« * », « ? »), sans le dossier
} finds[FIND_SLOTS];
static pthread_mutex_t finds_lock = PTHREAD_MUTEX_INITIALIZER;

// Le nom correspond-il au motif ? Sans distinction de casse ; « *.* » vaut « * ».
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
        if (*pattern != '?' && (*pattern | 0x20) != (*name | 0x20) && *pattern != *name) return 0;
    }
    return !*name;
}
// Entrée suivante qui correspond, dans un WIN32_FIND_DATAA (320 octets). 0 s'il n'y en a plus.
static int find_next(int slot, u64 data) {
    for (struct dirent *e; (e = readdir(finds[slot].dir));) {
        if (!wildcard(finds[slot].pattern, e->d_name) || strlen(e->d_name) >= 260) continue;
        char full[1024];
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", finds[slot].path, e->d_name);
        if (stat(full, &st) != 0) continue;
        memset(PTR(data), 0, 320);
        wr32(data, S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE);
        for (int i = 0; i < 3; i++) wr64(data + 4 + 8 * i, filetime_of((i64)st.st_mtime));
        wr32(data + 28, (u32)((u64)st.st_size >> 32));
        wr32(data + 32, (u32)st.st_size);
        strcpy(PTR(data + 44), e->d_name);
        return 1;
    }
    return 0;
}
// FindFirstFileA(pattern, &data)
static void k32_FindFirstFileA(RcnCtx *ctx) {
    u64 data = A1;
    char *path = arg_path(ctx, A0, 0);
    char *name = path ? strrchr(path, '/') : NULL;
    int slot = -1;
    if (name) {
        *name++ = 0; // path : le dossier ; name : le motif
        pthread_mutex_lock(&finds_lock);
        for (int i = 0; i < FIND_SLOTS && slot < 0; i++) {
            if (!finds[i].dir && strlen(name) < sizeof finds[i].pattern && (finds[i].dir = opendir(path))) {
                slot = i;
                finds[i].path = path;
                strcpy(finds[i].pattern, name);
            }
        }
        pthread_mutex_unlock(&finds_lock);
    }
    if (slot >= 0 && find_next(slot, data)) {
        RET(HANDLE_FIND + slot);
        return;
    }
    if (slot >= 0) {
        closedir(finds[slot].dir);
        finds[slot].dir = NULL;
    }
    free(path);
    LAST_ERROR(ctx) = ERROR_FILE_NOT_FOUND;
    RET(INVALID_HANDLE_VALUE);
}
static void k32_FindNextFileA(RcnCtx *ctx) {
    u32 slot = (u32)A0 - HANDLE_FIND;
    int found = slot < FIND_SLOTS && finds[slot].dir && find_next((int)slot, A1);
    if (!found) LAST_ERROR(ctx) = ERROR_NO_MORE_FILES;
    RET(found);
}
static void k32_FindClose(RcnCtx *ctx) {
    u32 slot = (u32)A0 - HANDLE_FIND;
    int ok = slot < FIND_SLOTS && finds[slot].dir;
    if (ok) {
        closedir(finds[slot].dir);
        free(finds[slot].path);
        finds[slot].dir = NULL;
        finds[slot].path = NULL;
    }
    RET(ok);
}

// --- Fichiers de réglages (.ini) : [section] puis lignes « clé=valeur » ---

static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = size >= 0 ? malloc((size_t)size + 1) : NULL;
    if (text) text[fread(text, 1, (size_t)size, f)] = 0;
    fclose(f);
    return text;
}
static int same_text(const char *a, size_t a_len, const char *b) {
    if (strlen(b) != a_len) return 0;
    for (size_t i = 0; i < a_len; i++) {
        if ((a[i] | 0x20) != (b[i] | 0x20) && a[i] != b[i]) return 0;
    }
    return 1;
}
// Cherche « clé » dans [section]. Renvoie le début de sa valeur (longueur dans *len), ou NULL ;
// *insert reçoit l'endroit où ajouter la clé (fin de la section), ou NULL si la section manque.
static char *ini_find(char *text, const char *section, const char *key, size_t *len, char **insert) {
    int inside = 0;
    *insert = NULL;
    for (char *line = text; *line;) {
        char *end = line + strcspn(line, "\r\n");
        char *start = line + strspn(line, " \t");
        if (*start == '[') {
            if (inside) return NULL;
            char *close = memchr(start, ']', (size_t)(end - start));
            inside = close && same_text(start + 1, (size_t)(close - start - 1), section);
        } else if (inside) {
            char *equal = memchr(start, '=', (size_t)(end - start));
            if (equal) {
                char *key_end = equal;
                while (key_end > start && (key_end[-1] == ' ' || key_end[-1] == '\t')) key_end--;
                if (same_text(start, (size_t)(key_end - start), key)) {
                    char *value = equal + 1 + strspn(equal + 1, " \t");
                    if (value > end) value = end;
                    *len = (size_t)(end - value);
                    return value;
                }
            }
        }
        line = end + strspn(end, "\r\n");
        if (inside) *insert = line;
    }
    return NULL;
}
// GetPrivateProfileStringA(section, key, default, buffer, size, file)
static void k32_GetPrivateProfileStringA(RcnCtx *ctx) {
    const char *fallback = A2 ? STR(A2) : "";
    char *out = PTR(A3);
    u32 size = (u32)STK(4);
    char *path = arg_path(ctx, STK(5), 0);
    char *text = path ? read_text(path) : NULL;
    size_t len = 0;
    char *insert;
    const char *value = text && A0 && A1 ? ini_find(text, STR(A0), STR(A1), &len, &insert) : NULL;
    if (!value) {
        value = fallback;
        len = strlen(fallback);
    }
    if (size) {
        if (len > size - 1) len = size - 1;
        memcpy(out, value, len);
        out[len] = 0;
    }
    free(text);
    free(path);
    RET(size ? len : 0);
}
// WritePrivateProfileStringA(section, key, value, file) : remplace la valeur, ou ajoute la clé (et la
// section) si elles manquent. Supprimer une clé ou une section (pointeur nul) n'est pas géré.
static void k32_WritePrivateProfileStringA(RcnCtx *ctx) {
    if (!A0 || !A1 || !A2) {
        RET(0);
        return;
    }
    const char *section = STR(A0), *key = STR(A1), *value = STR(A2);
    char *path = arg_path(ctx, A3, 0);
    char *text = path ? read_text(path) : NULL;
    const char *old = text ? text : "";
    size_t len = 0;
    char *insert = NULL;
    char *found = text ? ini_find(text, section, key, &len, &insert) : NULL;
    FILE *f = path ? fopen(path, "wb") : NULL;
    if (f) {
        if (found) { // avant la valeur, la nouvelle valeur, après
            fwrite(old, 1, (size_t)(found - old), f);
            fputs(value, f);
            fputs(found + len, f);
        } else if (insert) { // à la fin de la section
            fwrite(old, 1, (size_t)(insert - old), f);
            if (insert > old && insert[-1] != '\n') fputs("\r\n", f);
            fprintf(f, "%s=%s\r\n", key, value);
            fputs(insert, f);
        } else {
            fputs(old, f);
            if (*old && old[strlen(old) - 1] != '\n') fputs("\r\n", f);
            fprintf(f, "[%s]\r\n%s=%s\r\n", section, key, value);
        }
        fclose(f);
    }
    free(text);
    free(path);
    RET(f != NULL);
}

const RcnShim rcn_k32_file[] = {
    {"CopyFileA", k32_CopyFileA, S32(3)},
    {"FileTimeToLocalFileTime", k32_FileTimeToLocalFileTime, S32(2)},
    {"FileTimeToSystemTime", k32_FileTimeToSystemTime, S32(2)},
    {"FindClose", k32_FindClose, S32(1)},
    {"FindFirstFileA", k32_FindFirstFileA, S32(2)},
    {"FindNextFileA", k32_FindNextFileA, S32(2)},
    {"GetFileTime", k32_GetFileTime, S32(4)},
    {"GetFullPathNameA", k32_GetFullPathNameA, S32(4)},
    {"GetPrivateProfileStringA", k32_GetPrivateProfileStringA, S32(6)},
    {"SetEndOfFile", k32_SetEndOfFile, S32(1)},
    {"WritePrivateProfileStringA", k32_WritePrivateProfileStringA, S32(4)},
    {"CreateDirectoryA", k32_CreateDirectoryA, S32(2)},
    {"CreateDirectoryW", k32_CreateDirectoryW},
    {"CreateFileA", k32_CreateFileA, S32(7)},
    {"CreateFileW", k32_CreateFileW},
    {"DeleteFileA", k32_DeleteFileA, S32(1)},
    {"DeleteFileW", k32_DeleteFileW},
    {"FlushFileBuffers", k32_FlushFileBuffers, S32(1)},
    {"GetFileAttributesA", k32_GetFileAttributesA, S32(1)},
    {"GetFileAttributesW", k32_GetFileAttributesW},
    {"GetFileSize", k32_GetFileSize, S32(2)},
    {"GetFileSizeEx", k32_GetFileSizeEx},
    {"GetFileType", k32_GetFileType, S32(1)},
    {"ReadFile", k32_ReadFile, S32(5)},
    {"RemoveDirectoryA", k32_RemoveDirectoryA, S32(1)},
    {"RemoveDirectoryW", k32_RemoveDirectoryW},
    {"SetFilePointer", k32_SetFilePointer, S32(4)},
    {"SetFilePointerEx", k32_SetFilePointerEx},
    {"WriteFile", k32_WriteFile, S32(5)},
};
const size_t rcn_k32_file_count = sizeof rcn_k32_file / sizeof rcn_k32_file[0];
