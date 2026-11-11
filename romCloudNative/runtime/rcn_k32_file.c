// RomCloud Native : kernel32, fichiers — CreateFile, ReadFile, WriteFile, position, taille,
// attributs, dossiers. Les chemins du jeu sont ramenés à son dossier (rcn_host_path) ; les fonctions
// en « W » convertissent leur chemin UTF-16 puis font comme celles en « A ».
//
// Pas encore fournis : énumération des dossiers (FindFirstFile), lecture asynchrone (OVERLAPPED),
// fichiers projetés en mémoire, verrouillage et partage (les modes de partage sont ignorés).
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

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

const RcnShim rcn_k32_file[] = {
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
