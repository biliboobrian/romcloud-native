// RomCloud Native : kernel32, fichiers — CreateFile, ReadFile, WriteFile, position, taille,
// attributs, dossiers. Les chemins du jeu sont ramenés à son dossier (rcn_host_path) ; les fonctions
// en « W » convertissent leur chemin UTF-16 puis font comme celles en « A ».
//
// Pas encore fournis : énumération des dossiers (FindFirstFile), lecture asynchrone (OVERLAPPED),
// fichiers projetés en mémoire, verrouillage et partage (les modes de partage sont ignorés).
#include <ctype.h>
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

// Crée un dossier et ses parents manquants (« mkdir -p ») : le jeu ne voit qu'un « C:\ » virtuel dont les
// dossiers n'existent pas encore (« Users\Player\Documents\My Games\… »), et il ne crée que le dernier.
static int make_dirs(const char *path) {
    size_t n = strlen(path);
    char *copy = malloc(n + 1);
    if (!copy) return -1;
    memcpy(copy, path, n + 1);
    // Barres finales retirées (mkdir de Windows les refuse).
    while (n > 1 && (copy[n - 1] == '/' || copy[n - 1] == '\\')) copy[--n] = 0;
    for (char *p = copy + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        char sep = *p;
        *p = 0;
        make_dir(copy); // parent : déjà présent ou créé ici, l'échec est sans conséquence
        *p = sep;
    }
    int failed = make_dir(copy) != 0;
    free(copy);
    return failed ? -1 : 0;
}

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

// Crée chez l'hôte le dossier d'un chemin Windows du jeu (dossier temporaire...), s'il manque.
void rcn_make_guest_dir(RcnCtx *ctx, const char *windows_path) {
    char *path = rcn_host_path(ctx, windows_path);
    if (path) make_dirs(path);
    free(path);
}

// Descripteur de l'hôte d'un handle de fichier, ou -1 (erreur notée).
static int file_fd(RcnCtx *ctx, u64 handle);
int rcn_file_fd_of(RcnCtx *ctx, u64 handle) { return file_fd(ctx, handle); }
static int file_fd(RcnCtx *ctx, u64 handle) {
    RcnProcess *proc = ctx->proc;
    pthread_mutex_lock(&proc->lock);
    RcnObject *object = rcn_handle_get(proc, handle, RCN_OBJ_FILE);
    int fd = object ? object->fd : -1;
    pthread_mutex_unlock(&proc->lock);
    if (fd < 0) LAST_ERROR(ctx) = ERROR_INVALID_HANDLE;
    return fd;
}

typedef struct {
    u32 next;     // entrées déjà rendues
    char path[];  // dossier de l'hôte
} DirScan;

// Tube nommé (« \\.\pipe\… ») : aucun autre processus ne le lit ; un handle sur le périphérique nul de
// l'hôte (écritures perdues, lectures vides) en tient lieu. Unreal Engine 3 y envoie sa console.
#if defined(_WIN32)
#define NULL_DEVICE "NUL"
#else
#define NULL_DEVICE "/dev/null"
#endif
static int is_pipe_path(u64 arg, int wide) {
    if (!arg) return 0;
    char head[10];
    for (int i = 0; i < 9; i++) {
        u32 c = wide ? rd16(arg + 2 * (u64)i) : rd8(arg + (u64)i);
        head[i] = (char)(c == '/' ? '\\' : c < 128 ? tolower((int)c) : '?');
        if (!c) return 0;
    }
    head[9] = 0;
    return !strcmp(head, "\\\\.\\pipe\\");
}
static u64 null_device_handle(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    int fd = open(NULL_DEVICE, O_RDWR);
    RcnObject *object = fd >= 0 ? calloc(1, sizeof *object) : NULL;
    if (!object) {
        if (fd >= 0) close(fd);
        return 0;
    }
    object->type = RCN_OBJ_FILE;
    object->fd = fd;
    pthread_mutex_lock(&proc->lock);
    u64 handle = rcn_handle_new(proc, object);
    pthread_mutex_unlock(&proc->lock);
    return handle;
}
// CreateNamedPipeW(nom, ouverture, mode, instances, sortie, entrée, délai, sécurité)
static void k32_CreateNamedPipeW(RcnCtx *ctx) {
    u64 handle = null_device_handle(ctx);
    LAST_ERROR(ctx) = 0;
    RET(handle ? handle : (u64)INVALID_HANDLE_VALUE);
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
    if (is_pipe_path(A0, wide)) {
        u64 handle = null_device_handle(ctx);
        LAST_ERROR(ctx) = handle ? 0 : ERROR_FILE_NOT_FOUND;
        RET(handle ? handle : (u64)INVALID_HANDLE_VALUE);
        return;
    }
    char *path = arg_path(ctx, A0, wide);
    // Dossier ouvert avec FILE_FLAG_BACKUP_SEMANTICS (énumération de .NET) : handle valide, descripteur en
    // lecture seule quand l'hôte le permet (pas sous Windows).
    struct stat st;
    int directory = path && (STK(5) & 0x02000000) && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    int fd = !path ? -1 : directory ? open(path, O_RDONLY) : open(path, flags, 0666);
    int error = errno;
    // RCN_OPEN_LOG=1 : chaque ouverture (chemin de l'hôte, réussite), pour la mise au point.
    static int open_log = -1;
    if (open_log < 0) open_log = getenv("RCN_OPEN_LOG") != NULL;
    if (open_log) rcn_log("CreateFile : %s %s", path ? path : "?", fd >= 0 || directory ? "ouvert" : "introuvable");
    if (fd < 0 && !directory && proc->trace) fprintf(stderr, "CreateFile : %s introuvable (errno %d)\n", path ? path : "?", error);
    RcnObject *object = fd >= 0 || directory ? calloc(1, sizeof *object) : NULL;
    // Dossier : son chemin et la position de l'énumération (NtQueryDirectoryFile).
    if (object && directory) {
        size_t n = strlen(path);
        DirScan *scan = calloc(1, sizeof *scan + n + 1);
        if (scan) memcpy(scan->path, path, n + 1);
        object->data = scan;
    }
    free(path);
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

// NtQueryDirectoryFile(dossier, événement, apc, contexte, &iosb, tampon, taille, classe, une seule,
// &motif, recommencer) : énumération de .NET (FileSystemEnumerator). Motif ignoré (.NET n'en passe pas).
void rcn_nt_QueryDirectoryFile(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    u64 iosb = STK(4), out = STK(5);
    u32 size = (u32)STK(6), kind = (u32)STK(7), single = (u8)STK(8), restart = (u8)STK(10);
    // Position du nom selon la classe : FileDirectory 1, FileFullDirectory 2, FileBothDirectory 3,
    // FileIdBothDirectory 37, FileIdFullDirectory 38.
    u32 name_at = kind == 1 ? 64 : kind == 2 ? 68 : kind == 3 ? 94 : kind == 37 ? 104 : kind == 38 ? 80 : 0;
    pthread_mutex_lock(&proc->lock);
    RcnObject *object = rcn_handle_get(proc, A0, RCN_OBJ_FILE);
    DirScan *scan = object ? object->data : NULL;
    char *dir_path = scan ? strdup(scan->path) : NULL;
    u32 skip = scan ? (restart ? 0 : scan->next) : 0;
    pthread_mutex_unlock(&proc->lock);
    u32 status;
    u64 used = 0;
    if (!dir_path) {
        status = 0xC0000008u; // STATUS_INVALID_HANDLE
    } else if (!name_at) {
        status = 0xC0000003u; // STATUS_INVALID_INFO_CLASS
    } else {
        DIR *dir = opendir(dir_path);
        u32 index = 0, taken = 0;
        u64 previous = 0;
        struct dirent *entry;
        while (dir && (entry = readdir(dir))) {
            if (index++ < skip) continue;
            size_t len = strlen(entry->d_name);
            size_t units = rcn_utf8_to_utf16(entry->d_name, len, NULL);
            u64 need = name_at + 2 * units, at = (used + 7) & ~7ull;
            if (at + need > size) break;
            char *full = malloc(strlen(dir_path) + len + 2);
            struct stat st;
            int known = 0;
            if (full) {
                sprintf(full, "%s/%s", dir_path, entry->d_name);
                known = stat(full, &st) == 0;
                free(full);
            }
            u8 *e = PTR(out + at);
            memset(e, 0, name_at);
            u64 time = known ? ((u64)st.st_mtime + 11644473600ull) * 10000000ull : 0;
            int is_dir = known && S_ISDIR(st.st_mode);
            for (int t = 0; t < 4; t++) wr64(out + at + 8 + 8 * t, time);
            u64 bytes = known && !is_dir ? (u64)st.st_size : 0;
            wr64(out + at + 40, bytes), wr64(out + at + 48, (bytes + 4095) & ~4095ull);
            wr32(out + at + 56, is_dir ? 0x10 : 0x20); // DIRECTORY / ARCHIVE
            wr32(out + at + 60, (u32)(2 * units));
            rcn_utf8_to_utf16(entry->d_name, len, (u16 *)(e + name_at));
            if (taken) wr32(previous, (u32)(out + at - previous));
            previous = out + at;
            used = at + need;
            taken++;
            if (single) break;
        }
        if (dir) closedir(dir);
        status = taken ? 0 : (skip || dir) ? 0x80000006u : 0xC000000Fu; // NO_MORE_FILES / NO_SUCH_FILE
        if (!taken && index > skip) status = 0x80000005u; // BUFFER_OVERFLOW : la première entrée ne tient pas
        pthread_mutex_lock(&proc->lock);
        if (object && rcn_handle_get(proc, A0, RCN_OBJ_FILE) == object) scan->next = skip + taken;
        pthread_mutex_unlock(&proc->lock);
    }
    free(dir_path);
    if (iosb) wr64(iosb, status), wr64(iosb + 8, used);
    RET(status);
}
static void k32_CreateFileW(RcnCtx *ctx) { create_file(ctx, 1); }

// Lecture ou écriture à une position, sans changer la position du fichier (pread / pwrite).
static long pos_io(int fd, u8 *buffer, u32 length, i64 at, int writing) {
#if defined(_WIN32)
    // Pas de pread : position et transfert sous un verrou, la position d'origine rendue ensuite.
    static pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&io_lock);
    i64 saved = lseek(fd, 0, SEEK_CUR);
    lseek(fd, at, SEEK_SET);
    long n = writing ? (long)write(fd, buffer, length) : (long)read(fd, buffer, length);
    lseek(fd, saved, SEEK_SET);
    pthread_mutex_unlock(&io_lock);
    return n;
#else
    return writing ? (long)pwrite(fd, buffer, length, (off_t)at) : (long)pread(fd, buffer, length, (off_t)at);
#endif
}
// ReadFile(handle, buffer, length, &read, overlapped)
// Opération OVERLAPPED terminée : état, octets transférés, événement signalé, et complétion déposée dans le
// port de complétion du fichier s'il y en a un (le jeu y attend la fin de ses lectures et écritures).
static void overlapped_done(RcnCtx *ctx, u64 handle, u64 overlapped, u32 total) {
    if (!overlapped) return;
    wr64(overlapped, 0);
    wr64(overlapped + 8, total);
    if (rd64(overlapped + 24)) rcn_event_signal(ctx->proc, rd64(overlapped + 24) & ~1ull);
    rcn_iocp_complete(ctx, handle, total, overlapped);
}
// Lecture à une position (fichiers projetés, rcn_k32_x64b.c) : octets lus, ou -1.
i64 rcn_file_read_at(RcnCtx *ctx, u64 handle, u64 offset, void *buffer, size_t size) {
    int fd = file_fd(ctx, handle);
    if (fd < 0) return -1;
    size_t total = 0;
    while (total < size) {
        u32 part = size - total > 0x40000000u ? 0x40000000u : (u32)(size - total);
        long n = pos_io(fd, (u8 *)buffer + total, part, (i64)(offset + total), 0);
        if (n <= 0) break;
        total += (size_t)n;
    }
    return (i64)total;
}
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
    // OVERLAPPED (lecture asynchrone, menée ici tout de suite) : position dans la structure, lecture à
    // cette position sans toucher à celle du fichier (plusieurs threads lisent le même fichier en même
    // temps : Unreal Engine 5 ; lseek puis read se mélangeaient entre eux).
    u64 overlapped = ctx->bits32 ? 0 : STK(4);
    i64 at = overlapped ? (i64)((u64)rd32(overlapped + 16) | (u64)rd32(overlapped + 20) << 32) : -1;
    // RCN_READ_LOG=1 : lectures (handle, position, taille), pour la mise au point.
    static int read_log = -1;
    if (read_log < 0) read_log = getenv("RCN_READ_LOG") != NULL;
    if (read_log) fprintf(stderr, "rcn : lecture %llx @%llx +%x (thread %u)\n", (unsigned long long)A0, (long long)at, (u32)A2, THREAD_ID(ctx));
    // Jusqu'à la longueur demandée ou la fin du fichier (une lecture de l'hôte peut être partielle).
    u8 *buffer = PTR(A1);
    u32 length = (u32)A2, total = 0;
    while (total < length) {
        long n = at >= 0 ? pos_io(fd, buffer + total, length - total, at + total, 0) : (long)read(fd, buffer + total, length - total);
        if (n < 0) {
            LAST_ERROR(ctx) = rcn_win32_error(errno);
            RET(0);
            return;
        }
        if (n == 0) break;
        total += (u32)n;
    }
    if (count_out) wr32(count_out, total);
    overlapped_done(ctx, A0, overlapped, total);
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
        u64 overlapped = ctx->bits32 ? 0 : STK(4);
        i64 at = overlapped ? (i64)((u64)rd32(overlapped + 16) | (u64)rd32(overlapped + 20) << 32) : -1;
        while (total < length) {
            long n = at >= 0 ? pos_io(fd, (u8 *)buffer + total, length - total, at + total, 1)
                             : (long)write(fd, buffer + total, length - total);
            if (n <= 0) {
                LAST_ERROR(ctx) = rcn_win32_error(n < 0 ? errno : ENOSPC);
                RET(0);
                return;
            }
            total += (u32)n;
        }
    }
    if (count_out) wr32(count_out, total);
    if (!ctx->bits32) overlapped_done(ctx, handle, STK(4), total);
    RET(1);
}

// SetFilePointerEx(handle, distance, &new_position, method) ; method : 0 début, 1 position, 2 fin.
// 32 bits : la distance (LARGE_INTEGER) occupe deux cases de la pile, les arguments suivants sont décalés.
static void k32_SetFilePointerEx(RcnCtx *ctx) {
    int fd = file_fd(ctx, A0);
    i64 distance = ctx->bits32 ? (i64)(A1 | (A2 << 32)) : (i64)A1;
    u64 out = ctx->bits32 ? A3 : A2;
    u32 method = (u32)(ctx->bits32 ? STK(4) : A3);
    i64 pos = fd >= 0 && method <= 2 ? (i64)lseek(fd, distance, (int)method) : -1;
    if (pos < 0) {
        if (fd >= 0) LAST_ERROR(ctx) = ERROR_INVALID_PARAMETER;
        RET(0);
        return;
    }
    if (out) wr64(out, (u64)pos);
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
static u64 filetime_of(i64 seconds);
// GetFileInformationByHandleEx(handle, class, &info, size) : FileBasicInfo (0), FileStandardInfo (1).
static void k32_GetFileInformationByHandleEx(RcnCtx *ctx) {
    int fd = file_fd(ctx, A0);
    u32 cls = (u32)A1, size = (u32)A3;
    u64 info = A2;
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) {
        LAST_ERROR(ctx) = 6; // ERROR_INVALID_HANDLE
        RET(0);
        return;
    }
    int dir = S_ISDIR(st.st_mode);
    if (cls == 0 && size >= 40) {
        u64 t = filetime_of((i64)st.st_mtime);
        for (int i = 0; i < 4; i++) wr64(info + 8 * (u64)i, t);
        wr32(info + 32, dir ? 0x10 : 0x20);
        RET(1);
    } else if (cls == 1 && size >= 24) {
        wr64(info, (u64)st.st_size);
        wr64(info + 8, (u64)st.st_size);
        wr32(info + 16, 1);
        wr8(info + 20, 0);
        wr8(info + 21, (u8)dir);
        RET(1);
    } else {
        LAST_ERROR(ctx) = 50; // ERROR_NOT_SUPPORTED
        RET(0);
    }
}
// GetFileInformationByHandle(handle, &info) : BY_HANDLE_FILE_INFORMATION (52 octets, même disposition en
// 32 et 64 bits).
static void k32_GetFileInformationByHandle(RcnCtx *ctx) {
    int fd = file_fd(ctx, A0);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0 || !A1) {
        LAST_ERROR(ctx) = 6; // ERROR_INVALID_HANDLE
        RET(0);
        return;
    }
    u64 t = filetime_of((i64)st.st_mtime);
    memset(PTR(A1), 0, 52);
    wr32(A1, S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE);
    for (int i = 0; i < 3; i++) wr64(A1 + 4 + 8 * (u64)i, t);
    wr32(A1 + 28, 0x1234ABCDu); // numéro de volume
    wr32(A1 + 32, (u32)((u64)st.st_size >> 32));
    wr32(A1 + 36, (u32)st.st_size);
    wr32(A1 + 40, 1); // liens
    wr32(A1 + 44, (u32)((u64)st.st_ino >> 32));
    wr32(A1 + 48, (u32)st.st_ino);
    RET(1);
}
// Taille, nature et date d'un fichier ouvert (pour rcn_k32_x64.c).
void rcn_file_info(RcnCtx *ctx, u64 handle, u64 *size, int *directory, u64 *time, int *ok) {
    int fd = file_fd(ctx, handle);
    struct stat st;
    *ok = fd >= 0 && fstat(fd, &st) == 0;
    if (!*ok) return;
    *size = (u64)st.st_size;
    *directory = S_ISDIR(st.st_mode);
    *time = filetime_of((i64)st.st_mtime);
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
    int failed = path ? make_dirs(path) != 0 : 1;
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
    // Recherche de FindFirstFileW (rcn_k32_more.c) : sa table à elle.
    if (rcn_findw_close(A0)) {
        RET(1);
        return;
    }
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
// GetPrivateProfileIntA(section, key, default, file) : nombre décimal en tête de la valeur.
static void k32_GetPrivateProfileIntA(RcnCtx *ctx) {
    char *path = arg_path(ctx, A3, 0);
    char *text = path ? read_text(path) : NULL;
    size_t len = 0;
    char *insert;
    const char *value = text && A0 && A1 ? ini_find(text, STR(A0), STR(A1), &len, &insert) : NULL;
    i64 v = (i32)A2;
    if (value && len) {
        char number[32];
        size_t n = len < sizeof number - 1 ? len : sizeof number - 1;
        memcpy(number, value, n);
        number[n] = 0;
        v = strtol(number, NULL, 10);
    }
    free(text);
    free(path);
    RET((u32)v);
}
// Écrit une valeur dans un fichier INI : remplace la valeur existante, ou ajoute la clé (et la section).
static int ini_write(RcnCtx *ctx, const char *section, const char *key, const char *value, u64 file) {
    char *path = arg_path(ctx, file, 0);
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
    return f != NULL;
}
// WritePrivateProfileStringA(section, key, value, file)
static void k32_WritePrivateProfileStringA(RcnCtx *ctx) {
    if (!A0 || !A1 || !A2) {
        RET(0);
        return;
    }
    RET(ini_write(ctx, STR(A0), STR(A1), STR(A2), A3));
}
// Valeur hexadécimale d'un chiffre, ou -1.
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
// GetPrivateProfileStructA(section, clé, données, taille, fichier) : données binaires rangées en
// hexadécimal, comme sous Windows (les réglages de NieR: Automata).
static void k32_GetPrivateProfileStructA(RcnCtx *ctx) {
    char *path = arg_path(ctx, STK(4), 0);
    char *text = path ? read_text(path) : NULL;
    size_t len = 0;
    char *insert;
    const char *value = text && A0 && A1 ? ini_find(text, STR(A0), STR(A1), &len, &insert) : NULL;
    u8 *out = PTR(A2);
    u32 size = (u32)A3;
    int ok = 0;
    if (value && out && strlen(value) >= (size_t)size * 2) {
        ok = 1;
        for (u32 i = 0; i < size && ok; i++) {
            int hi = hex_digit(value[2 * i]), lo = hex_digit(value[2 * i + 1]);
            if (hi < 0 || lo < 0) ok = 0;
            else out[i] = (u8)(hi << 4 | lo);
        }
    }
    free(text);
    free(path);
    RET(ok);
}
// WritePrivateProfileStructA(section, clé, données, taille, fichier)
static void k32_WritePrivateProfileStructA(RcnCtx *ctx) {
    const u8 *data = PTR(A2);
    u32 size = (u32)A3;
    if (!A0 || !A1 || !data) {
        RET(0);
        return;
    }
    char *value = malloc((size_t)size * 2 + 1);
    if (!value) {
        RET(0);
        return;
    }
    for (u32 i = 0; i < size; i++) snprintf(value + 2 * (size_t)i, 3, "%02X", data[i]);
    RET(ini_write(ctx, STR(A0), STR(A1), value, STK(4)));
    free(value);
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
    {"GetPrivateProfileIntA", k32_GetPrivateProfileIntA, S32(4)},
    {"GetPrivateProfileStructA", k32_GetPrivateProfileStructA, S32(5)},
    {"SetEndOfFile", k32_SetEndOfFile, S32(1)},
    {"WritePrivateProfileStringA", k32_WritePrivateProfileStringA, S32(4)},
    {"WritePrivateProfileStructA", k32_WritePrivateProfileStructA, S32(5)},
    {"CreateDirectoryA", k32_CreateDirectoryA, S32(2)},
    {"CreateDirectoryW", k32_CreateDirectoryW, S32(2)},
    {"CreateFileA", k32_CreateFileA, S32(7)},
    {"CreateFileW", k32_CreateFileW, S32(7)},
    {"CreateNamedPipeW", k32_CreateNamedPipeW, S32(8)},
    {"DeleteFileA", k32_DeleteFileA, S32(1)},
    {"DeleteFileW", k32_DeleteFileW, S32(1)},
    {"FlushFileBuffers", k32_FlushFileBuffers, S32(1)},
    {"GetFileAttributesA", k32_GetFileAttributesA, S32(1)},
    {"GetFileAttributesW", k32_GetFileAttributesW, S32(1)},
    {"GetFileSize", k32_GetFileSize, S32(2)},
    {"GetFileInformationByHandleEx", k32_GetFileInformationByHandleEx}, {"GetFileInformationByHandle", k32_GetFileInformationByHandle, S32(2)},
    {"GetFileSizeEx", k32_GetFileSizeEx, S32(2)},
    {"GetFileType", k32_GetFileType, S32(1)},
    {"ReadFile", k32_ReadFile, S32(5)},
    {"RemoveDirectoryA", k32_RemoveDirectoryA, S32(1)},
    {"RemoveDirectoryW", k32_RemoveDirectoryW, S32(1)},
    {"SetFilePointer", k32_SetFilePointer, S32(4)},
    {"SetFilePointerEx", k32_SetFilePointerEx, S32(5)},
    {"WriteFile", k32_WriteFile, S32(5)},
};
const size_t rcn_k32_file_count = sizeof rcn_k32_file / sizeof rcn_k32_file[0];
