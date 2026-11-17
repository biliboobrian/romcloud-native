// RomCloud Native : bibliothèque C de Windows, suite (UCRT des jeux C++ récents et de msvcp140) :
// chaînes « sûres » (_s), caractères larges, printf larges, fichiers par chemin large, temps, paramètres
// régionaux « C », classification des flottants.
//
// Paramètres régionaux : toujours « C » ; un octet = un caractère (les caractères larges au-delà de 255
// deviennent « ? » en sens inverse).
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <wctype.h>

#include "rcn_internal.h"

#if defined(_WIN32)
#include <io.h>
#include <malloc.h>
#include <direct.h>
#define usable_size _msize
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#define timegm _mkgmtime
#define access _access
#define rmdir _rmdir
#define lseek _lseeki64
static struct tm *gmtime_r(const time_t *t, struct tm *out) { return gmtime_s(out, t) ? NULL : out; }
static struct tm *localtime_r(const time_t *t, struct tm *out) { return localtime_s(out, t) ? NULL : out; }

#else
#include <malloc.h>
#include <unistd.h>
#define usable_size malloc_usable_size
#define fseek64 fseeko
#define ftell64 ftello
#endif

#define CRT(name) static void crt_##name(RcnCtx *ctx)

int rcn_crt_std(RcnCtx *ctx, u64 file);
size_t rcn_crt_write(RcnCtx *ctx, u64 file, const void *data, size_t len);
const char *rcn_command_line(RcnCtx *ctx);

enum { EINVAL_W = 22, ERANGE_W = 34, STRUNCATE_W = 80 };
static const size_t TRUNCATE = (size_t)-1;

static void set_errno(RcnCtx *ctx, int e) { THREAD(ctx)->crt_errno = e; }

// --- Caractères larges (UTF-16 du jeu) ---

static size_t wlen(const u16 *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
static size_t wnlen(const u16 *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}
// Texte large → octets (un caractère par octet, « ? » au-delà de 255), à libérer.
static char *narrow(const u16 *s) {
    if (!s) return NULL;
    size_t n = wlen(s);
    char *out = malloc(n + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) out[i] = s[i] < 256 ? (char)s[i] : '?';
    out[n] = 0;
    return out;
}
// Chemin large du jeu → chemin de l'hôte (UTF-8), à libérer.
static char *host_wpath(RcnCtx *ctx, const u16 *path) {
    char *utf8 = path ? rcn_utf16_to_utf8(path, -1) : NULL;
    char *host = utf8 ? rcn_host_path(ctx, utf8) : NULL;
    free(utf8);
    return host;
}

// --- Fichiers ---

static char *host_mode(const char *mode, char out[8]) {
    size_t n = 0;
    for (const char *m = mode; *m && *m != ','; m++) {
        if (strchr("rwa+x", *m) && n < 4) out[n++] = *m;
    }
    out[n++] = 'b';
    out[n] = 0;
    return out;
}
static FILE *open_host(RcnCtx *ctx, char *path, const char *mode) {
    char m[8];
    FILE *f = path ? fopen(path, host_mode(mode, m)) : NULL;
    if (!f) set_errno(ctx, path ? errno : EINVAL_W);
    free(path);
    return f;
}
// _fsopen(chemin, mode, partage)
CRT(_fsopen) { RET((uintptr_t)open_host(ctx, rcn_host_path(ctx, STR(A0)), STR(A1))); }
// fopen_s(&fichier, chemin, mode)
CRT(fopen_s) {
    if (!A0) {
        RET(EINVAL_W);
        return;
    }
    FILE *f = open_host(ctx, rcn_host_path(ctx, STR(A1)), STR(A2));
    wr64(A0, (u64)(uintptr_t)f);
    RET(f ? 0 : THREAD(ctx)->crt_errno);
}
// _wfsopen(chemin, mode, partage) ; _wfopen_s(&fichier, chemin, mode)
CRT(_wfsopen) {
    char *mode = narrow(PTR(A1));
    RET((uintptr_t)(mode ? open_host(ctx, host_wpath(ctx, PTR(A0)), mode) : NULL));
    free(mode);
}
CRT(_wfopen_s) {
    if (!A0) {
        RET(EINVAL_W);
        return;
    }
    char *mode = narrow(PTR(A2));
    FILE *f = mode ? open_host(ctx, host_wpath(ctx, PTR(A1)), mode) : NULL;
    free(mode);
    wr64(A0, (u64)(uintptr_t)f);
    RET(f ? 0 : THREAD(ctx)->crt_errno);
}
// fgetpos / fsetpos : fpos_t de Windows, 64 bits.
CRT(fgetpos) {
    if (rcn_crt_std(ctx, A0)) {
        RET_I32(-1);
        return;
    }
    i64 pos = ftell64(PTR(A0));
    if (pos < 0) {
        RET_I32(-1);
        return;
    }
    wr64(A1, (u64)pos);
    RET(0);
}
CRT(fsetpos) { RET_I32(rcn_crt_std(ctx, A0) ? -1 : fseek64(PTR(A0), (i64)rd64(A1), SEEK_SET)); }
// Caractères larges d'un fichier : un octet par caractère (fichier ouvert sans mode Unicode).
CRT(fgetwc) {
    int c = rcn_crt_std(ctx, A0) ? EOF : fgetc(PTR(A0));
    RET(c == EOF ? 0xFFFF : (u16)(u8)c);
}
CRT(ungetwc) {
    int c = (u16)A0 == 0xFFFF || rcn_crt_std(ctx, A1) ? EOF : ungetc((u8)A0, PTR(A1));
    RET(c == EOF ? 0xFFFF : (u16)A0);
}
CRT(fputwc) {
    char c = (u16)A0 < 256 ? (char)A0 : '?';
    RET(rcn_crt_write(ctx, A1, &c, 1) == 1 ? (u16)A0 : 0xFFFF);
}
CRT(_lock_file) { (void)ctx; }
CRT(_unlock_file) { (void)ctx; }

// _get_stream_buffer_pointers(fichier, &base, &position, &reste) : msvcp140 (basic_filebuf) lit et écrit
// directement dans le tampon du FILE. Le FILE de l'hôte n'en montre pas : un tampon vide propre à chaque
// fichier (pointeurs nuls, compte nul), qui fait passer chaque lecture et écriture par fgetc, fputc…
typedef struct {
    u64 file;
    u64 base, ptr; // char *
    i32 count;
} StreamPointers;
static StreamPointers *streams;
static size_t stream_count, stream_cap;
static pthread_mutex_t stream_lock = PTHREAD_MUTEX_INITIALIZER;
CRT(_get_stream_buffer_pointers) {
    pthread_mutex_lock(&stream_lock);
    StreamPointers *s = NULL;
    for (size_t i = 0; i < stream_count; i++) {
        if (streams[i].file == A0) s = &streams[i];
    }
    if (!s) {
        // Les adresses rendues doivent rester valables : tableau jamais déplacé (taille fixe agrandie par
        // blocs chaînés serait plus juste ; 4096 fichiers ouverts par msvcp140 suffisent).
        if (!streams) {
            stream_cap = 4096;
            streams = calloc(stream_cap, sizeof *streams);
        }
        for (size_t i = 0; streams && i < stream_count; i++) {
            if (!streams[i].file) s = &streams[i];
        }
        if (!s && streams && stream_count < stream_cap) s = &streams[stream_count++];
        if (s) *s = (StreamPointers){.file = A0};
    }
    pthread_mutex_unlock(&stream_lock);
    if (!s) {
        RET(EINVAL_W);
        return;
    }
    if (A1) wr64(A1, (u64)(uintptr_t)&s->base);
    if (A2) wr64(A2, (u64)(uintptr_t)&s->ptr);
    if (A3) wr64(A3, (u64)(uintptr_t)&s->count);
    RET(0);
}
// Fichier fermé : sa case est rendue (appelé par fclose).
void rcn_crt_forget_file(u64 file) {
    pthread_mutex_lock(&stream_lock);
    for (size_t i = 0; i < stream_count; i++) {
        if (streams[i].file == file) streams[i] = (StreamPointers){0};
    }
    pthread_mutex_unlock(&stream_lock);
}

// --- Fichiers et dossiers par chemin ---

// _access(chemin, mode) : 0 existence, 2 écriture, 4 lecture, 6 les deux.
static int access_mode(u32 mode) { return (mode & 2 ? W_OK : 0) | (mode & 4 ? R_OK : 0); }
CRT(_access) {
    char *path = rcn_host_path(ctx, STR(A0));
    int r = path ? access(path, access_mode((u32)A1)) : -1;
    if (r) set_errno(ctx, errno);
    free(path);
    RET_I32(r);
}
// struct _stat64i32 (48 octets) et _stat64 (56 octets) de Windows.
static void put_stat(u64 out, const struct stat *s, int size64);
static int stat_to_guest(RcnCtx *ctx, char *path, u64 out, int size64) {
    struct stat st;
    int r = path ? stat(path, &st) : -1;
    free(path);
    if (r) {
        set_errno(ctx, 2); // ENOENT
        return -1;
    }
    put_stat(out, &st, size64);
    return 0;
}
static void put_stat(u64 out, const struct stat *s, int size64) {
    struct stat st = *s;
    u16 mode = S_ISDIR(st.st_mode) ? 0x4000 : 0x8000;
    mode |= 0x100 | ((st.st_mode & S_IWUSR) ? 0x80 : 0) | (S_ISDIR(st.st_mode) ? 0x40 : 0);
    mode |= (mode & 0x1C0) >> 3 | (mode & 0x1C0) >> 6;
    memset(PTR(out), 0, size64 ? 56 : 48);
    wr32(out, 2); // st_dev : lecteur C:
    wr16(out + 6, mode);
    wr16(out + 8, 1);
    wr32(out + 16, 2);
    if (size64) {
        wr64(out + 24, (u64)st.st_size);
        wr64(out + 32, (u64)st.st_atime), wr64(out + 40, (u64)st.st_mtime), wr64(out + 48, (u64)st.st_ctime);
    } else {
        wr32(out + 20, (u32)st.st_size);
        wr64(out + 24, (u64)st.st_atime), wr64(out + 32, (u64)st.st_mtime), wr64(out + 40, (u64)st.st_ctime);
    }
}

// --- Descripteurs de fichiers (_open, _read…) : ceux de l'hôte ---

// Options de _open de Windows → celles de l'hôte.
static int open_flags(u32 w) {
    int f = (w & 3) == 1 ? O_WRONLY : (w & 3) == 2 ? O_RDWR : O_RDONLY;
    if (w & 0x8) f |= O_APPEND;
    if (w & 0x100) f |= O_CREAT;
    if (w & 0x200) f |= O_TRUNC;
    if (w & 0x400) f |= O_EXCL;
#if defined(_WIN32)
    f |= _O_BINARY;
#endif
    return f;
}
static int fd_open(RcnCtx *ctx, char *path, u32 flags) {
    int fd = path ? open(path, open_flags(flags), 0666) : -1;
    if (fd < 0) set_errno(ctx, path ? errno : 2);
    free(path);
    return fd;
}
CRT(_open) { RET_I32(fd_open(ctx, rcn_host_path(ctx, STR(A0)), (u32)A1)); }
CRT(_wopen) { RET_I32(fd_open(ctx, host_wpath(ctx, PTR(A0)), (u32)A1)); }
CRT(_creat) { RET_I32(fd_open(ctx, rcn_host_path(ctx, STR(A0)), 0x301)); }
CRT(_wsopen_s) {
    int fd = fd_open(ctx, host_wpath(ctx, PTR(A1)), (u32)A2);
    if (A0) wr32(A0, (u32)fd);
    RET_I32(fd < 0 ? 2 : 0);
}
CRT(_read) {
    long n = (long)read((int)A0, PTR(A1), (u32)A2);
    if (n < 0) set_errno(ctx, errno);
    RET_I32(n);
}
CRT(_write) {
    int fd = (int)A0;
    if (fd == 1 || fd == 2) {
        rcn_host_write(ctx, fd == 1 ? RCN_STREAM_STDOUT : RCN_STREAM_STDERR, PTR(A1), (u32)A2);
        RET_I32((u32)A2);
        return;
    }
    long n = (long)write(fd, PTR(A1), (u32)A2);
    if (n < 0) set_errno(ctx, errno);
    RET_I32(n);
}
CRT(_close) {
    int r = (int)A0 > 2 ? close((int)A0) : 0;
    RET_I32(r);
}
CRT(_lseeki64) {
    i64 r = (i64)lseek((int)A0, (i64)A1, (int)A2);
    if (r < 0) set_errno(ctx, errno);
    RET((u64)r);
}
CRT(_lseek) {
    i64 r = (i64)lseek((int)A0, (i64)(i32)A1, (int)A2);
    RET_I32(r < 0 || r > INT32_MAX ? -1 : r);
}
CRT(_fstat64) {
    struct stat st;
    if (fstat((int)A0, &st)) {
        set_errno(ctx, 9); // EBADF
        RET_I32(-1);
        return;
    }
    put_stat(A1, &st, 1);
    RET_I32(0);
}
CRT(_fstat64i32) {
    struct stat st;
    if (fstat((int)A0, &st)) {
        set_errno(ctx, 9);
        RET_I32(-1);
        return;
    }
    put_stat(A1, &st, 0);
    RET_I32(0);
}
CRT(_locking) { RET_I32(0); }
CRT(_isatty) { RET_I32(0); }
CRT(_setmode) { RET_I32(0x8000); } // _O_BINARY
CRT(_commit) { RET_I32(0); }
CRT(_dup) { RET_I32(dup((int)A0)); }
CRT(_dup2) { RET_I32(dup2((int)A0, (int)A1) < 0 ? -1 : 0); }
// _fileno(FILE*) : 0, 1, 2 pour les flux standard, sinon le descripteur de l'hôte.
CRT(_fileno) {
    int std = rcn_crt_std(ctx, A0);
    RET_I32(std ? std - 1 : A0 ? fileno((FILE *)PTR(A0)) : -1);
}
// _open_osfhandle(handle, options) : descripteur d'un handle de fichier de kernel32.
int rcn_file_fd_of(RcnCtx *ctx, u64 handle);
CRT(_open_osfhandle) { RET_I32(rcn_file_fd_of(ctx, A0)); }
CRT(_unlink) {
    char *path = rcn_host_path(ctx, STR(A0));
    int r = path ? unlink(path) : -1;
    if (r) set_errno(ctx, errno);
    free(path);
    RET_I32(r);
}
CRT(_waccess) {
    char *path = host_wpath(ctx, PTR(A0));
    int r = path ? access(path, access_mode((u32)A1)) : -1;
    if (r) set_errno(ctx, errno);
    free(path);
    RET_I32(r);
}
CRT(_stat64i32) { RET_I32(stat_to_guest(ctx, rcn_host_path(ctx, STR(A0)), A1, 0)); }
CRT(_stat64) { RET_I32(stat_to_guest(ctx, rcn_host_path(ctx, STR(A0)), A1, 1)); }
CRT(_wstat64) { RET_I32(stat_to_guest(ctx, host_wpath(ctx, PTR(A0)), A1, 1)); }
CRT(_wstat64i32) { RET_I32(stat_to_guest(ctx, host_wpath(ctx, PTR(A0)), A1, 0)); }
CRT(_wremove) {
    char *path = host_wpath(ctx, PTR(A0));
    int r = path ? remove(path) : -1;
    if (r) set_errno(ctx, errno);
    free(path);
    RET_I32(r);
}
CRT(_wrename) {
    char *from = host_wpath(ctx, PTR(A0)), *to = host_wpath(ctx, PTR(A1));
    int r = from && to ? rename(from, to) : -1;
    if (r) set_errno(ctx, errno);
    free(from);
    free(to);
    RET_I32(r);
}
CRT(_wrmdir) {
    char *path = host_wpath(ctx, PTR(A0));
    int r = path ? rmdir(path) : -1;
    if (r) set_errno(ctx, errno);
    free(path);
    RET_I32(r);
}
// Dossier courant : la racine du jeu (« C:\ », comme GetCurrentDirectory) ; il ne change pas.
CRT(_wchdir) { RET(0); }
CRT(_getcwd) {
    u64 buf = A0;
    size_t size = (size_t)A1;
    if (!buf) {
        buf = (u64)(uintptr_t)rcn_galloc(ctx, size > 4 ? size : 4, 0);
        size = size > 4 ? size : 4;
    }
    if (!buf || size < 4) {
        set_errno(ctx, ERANGE_W);
        RET(0);
        return;
    }
    memcpy(PTR(buf), "C:\\", 4);
    RET(buf);
}
CRT(_wgetcwd) {
    u64 buf = A0;
    size_t size = (size_t)A1;
    if (!buf) {
        size = size > 4 ? size : 4;
        buf = (u64)(uintptr_t)rcn_galloc(ctx, size * 2, 0);
    }
    if (!buf || size < 4) {
        set_errno(ctx, ERANGE_W);
        RET(0);
        return;
    }
    wr16(buf, 'C'), wr16(buf + 2, ':'), wr16(buf + 4, '\\'), wr16(buf + 6, 0);
    RET(buf);
}
// _splitpath_s(chemin, lecteur, taille, dossier, taille, nom, taille, extension, taille)
static int put_part(u64 dest, size_t size, const char *from, size_t len) {
    if (!dest) return 0;
    if (len >= size) {
        if (size) wr8(dest, 0);
        return -1;
    }
    memcpy(PTR(dest), from, len);
    wr8(dest + len, 0);
    return 0;
}
CRT(_splitpath_s) {
    const char *path = STR(A0);
    const char *p = path;
    size_t drive = (p[0] && p[1] == ':') ? 2 : 0;
    p += drive;
    const char *slash = NULL;
    for (const char *q = p; *q; q++)
        if (*q == '\\' || *q == '/') slash = q;
    const char *name = slash ? slash + 1 : p;
    const char *dot = strrchr(name, '.');
    if (!dot) dot = name + strlen(name);
    int bad = put_part(A1, (size_t)A2, path, drive) | put_part(A3, (size_t)STK(4), p, (size_t)(name - p)) |
              put_part(STK(5), (size_t)STK(6), name, (size_t)(dot - name)) |
              put_part(STK(7), (size_t)STK(8), dot, strlen(dot));
    RET(bad ? ERANGE_W : 0);
}
// _makepath_s(destination, taille, lecteur, dossier, nom, extension)
CRT(_makepath_s) {
    char out[1024];
    size_t n = 0;
    const char *drive = STR(A2), *dir = STR(A3), *name = STR(STK(4)), *ext = STR(STK(5));
    if (drive && *drive) n += (size_t)snprintf(out + n, sizeof out - n, "%c:", drive[0]);
    if (dir && *dir) {
        n += (size_t)snprintf(out + n, sizeof out - n, "%s", dir);
        if (n && n < sizeof out - 1 && out[n - 1] != '\\' && out[n - 1] != '/') out[n++] = '\\', out[n] = 0;
    }
    if (name && *name) n += (size_t)snprintf(out + n, sizeof out - n, "%s", name);
    if (ext && *ext) n += (size_t)snprintf(out + n, sizeof out - n, "%s%s", ext[0] == '.' ? "" : ".", ext);
    if (n >= (size_t)A1 || n >= sizeof out) {
        if (A1) wr8(A0, 0);
        RET(ERANGE_W);
        return;
    }
    memcpy(PTR(A0), out, n + 1);
    RET(0);
}

// --- Tas ---

CRT(_msize) { RET(A0 ? usable_size(PTR(A0)) : (u64)-1); }
CRT(_callnewh) { RET(0); } // pas de gestionnaire : l'allocation échoue
CRT(_set_new_handler) { RET(0); }

// --- Démarrage, threads, erreurs ---

// _beginthread(fonction, pile, argument) : la fonction ne rend rien, le thread finit à son retour.
CRT(_beginthread) {
    u64 h = rcn_create_thread(ctx, A0, A2, A1, 0, 0);
    RET(h ? h : (u64)-1);
}
CRT(_endthread) { rcn_thread_exit(ctx, 0); }
CRT(_crt_at_quick_exit) { RET(0); }
CRT(_register_thread_local_exe_atexit_callback) { (void)ctx; }
// Ligne de commande sans le nom du programme (WinMain).
CRT(_get_narrow_winmain_command_line) {
    const char *p = rcn_command_line(ctx);
    if (!p) {
        RET(0);
        return;
    }
    if (*p == '"') {
        p++;
        while (*p && *p != '"') p++;
        if (*p) p++;
    } else {
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    while (*p == ' ' || *p == '\t') p++;
    RET((uintptr_t)p);
}
CRT(_invalid_parameter_noinfo) { (void)ctx; }
CRT(_invalid_parameter_noinfo_noreturn) { rcn_fail(ctx, "parametre invalide (bibliotheque C)"); }
CRT(_invoke_watson) { rcn_fail(ctx, "erreur fatale de la bibliotheque C (_invoke_watson)"); }
CRT(_wassert) {
    char *msg = rcn_utf16_to_utf8(PTR(A0), -1), *file = rcn_utf16_to_utf8(PTR(A1), -1);
    rcn_fail(ctx, "assertion : %s (%s:%u)", msg ? msg : "?", file ? file : "?", (u32)A2);
}
CRT(_set_fmode) {
    ctx->proc->crt_fmode = (int)A0;
    RET(0);
}

// --- Paramètres régionaux « C » ---

// Classes de caractères de Windows (_UPPER 1, _LOWER 2, _DIGIT 4, _SPACE 8, _PUNCT 0x10, _CONTROL 0x20,
// _BLANK 0x40, _HEX 0x80, _ALPHA 0x100) : table de 256 entrées, ASCII seulement.
static u16 ctype_table[257];
static pthread_once_t ctype_once = PTHREAD_ONCE_INIT;
static void ctype_init(void) {
    for (int c = 0; c < 128; c++) {
        u16 f = 0;
        if (isupper(c)) f |= 0x1 | 0x100;
        if (islower(c)) f |= 0x2 | 0x100;
        if (isdigit(c)) f |= 0x4;
        if (isspace(c)) f |= 0x8;
        if (ispunct(c)) f |= 0x10;
        if (iscntrl(c)) f |= 0x20;
        if (c == ' ' || c == '\t') f |= 0x40;
        if (isxdigit(c)) f |= 0x80;
        ctype_table[c + 1] = f;
    }
}
CRT(__pctype_func) {
    pthread_once(&ctype_once, ctype_init);
    RET((uintptr_t)&ctype_table[1]);
}
CRT(___lc_codepage_func) { RET(0); }
CRT(___lc_collate_cp_func) { RET(0); }
CRT(___mb_cur_max_func) { RET(1); }
CRT(___lc_locale_name_func) {
    static u64 names[6];
    RET((uintptr_t)names);
}
CRT(_lock_locales) { (void)ctx; }
CRT(_unlock_locales) { (void)ctx; }
// struct lconv de Windows : 10 chaînes, 8 octets, 8 chaînes larges.
static struct {
    u64 text[10];
    u8 values[8];
    u64 wide[8];
} lconv_c;
static char dot[] = ".", empty[] = "";
static u16 wdot[] = {'.', 0}, wempty[] = {0};
CRT(localeconv) {
    lconv_c.text[0] = (uintptr_t)dot;
    for (int i = 1; i < 10; i++) lconv_c.text[i] = (uintptr_t)empty;
    memset(lconv_c.values, 127, sizeof lconv_c.values); // CHAR_MAX : non précisé
    lconv_c.wide[0] = (uintptr_t)wdot;
    for (int i = 1; i < 8; i++) lconv_c.wide[i] = (uintptr_t)wempty;
    RET((uintptr_t)&lconv_c);
}

// --- Mathématiques ---

// _dclass / _fdtest / _ldclass : FP_INFINITE 1, FP_NAN 2, FP_NORMAL -1, FP_SUBNORMAL -2, FP_ZERO 0.
static int fp_class(double v) {
    switch (fpclassify(v)) {
    case FP_INFINITE: return 1;
    case FP_NAN: return 2;
    case FP_SUBNORMAL: return -2;
    case FP_ZERO: return 0;
    default: return -1;
    }
}
// Simple précision : classée telle quelle (un dénormal de float devient un double normal une fois élargi).
static int fp_classf(float v) {
    switch (fpclassify(v)) {
    case FP_INFINITE: return 1;
    case FP_NAN: return 2;
    case FP_SUBNORMAL: return -2;
    case FP_ZERO: return 0;
    default: return -1;
    }
}
CRT(_dclass) { RET_I32(fp_class(F64(0))); }
CRT(_ldclass) { RET_I32(fp_class(F64(0))); } // long double : double sous Windows
CRT(_fdtest) { RET_I32(fp_classf(*(const float *)PTR(A0))); }
CRT(_finite) { RET_I32(isfinite(F64(0)) ? 1 : 0); }
CRT(_isnan) {
    int nan = isnan(F64(0)) ? 1 : 0;
    // RCN_NAN_LOG=1 : appelant (adresse de retour, module et décalage) des valeurs NaN, pour remonter à la
    // fonction traduite qui les produit.
    static int log_nan = -1;
    if (log_nan < 0) log_nan = getenv("RCN_NAN_LOG") != NULL;
    if (log_nan && nan) {
        u64 ret = rd64(ctx->r[4]);
        RcnLoaded *l = rcn_loaded_at(ctx->proc, ret);
        static int told;
        if (told++ < 400) fprintf(stderr, "rcn : NaN vu par %s+0x%llx (valeur %016llx)\n", l ? l->name : "?",
                                  (unsigned long long)(l ? ret - (u64)(uintptr_t)l->image : ret), (unsigned long long)ctx->xmm[0].q[0]);
    }
    RET_I32(nan);
}
CRT(copysign) { F64(0) = copysign(F64(0), F64(1)); }
CRT(copysignf) { F32(0) = copysignf(F32(0), F32(1)); }
CRT(fminf) { F32(0) = fminf(F32(0), F32(1)); }
CRT(fmaxf) { F32(0) = fmaxf(F32(0), F32(1)); }
CRT(fmin) { F64(0) = fmin(F64(0), F64(1)); }
CRT(fmax) { F64(0) = fmax(F64(0), F64(1)); }
CRT(frexp) {
    int e = 0;
    F64(0) = frexp(F64(0), &e);
    wr32(A1, (u32)e);
}
CRT(ldexp) { F64(0) = ldexp(F64(0), (i32)A1); }
CRT(modf) {
    double whole;
    F64(0) = modf(F64(0), &whole);
    memcpy(PTR(A1), &whole, 8);
}
CRT(modff) {
    float whole;
    F32(0) = modff(F32(0), &whole);
    memcpy(PTR(A1), &whole, 4);
}

// --- Chaînes ---

CRT(strnlen) { RET(strnlen(STR(A0), (size_t)A1)); }
CRT(__strncnt) { RET(strnlen(STR(A0), (size_t)A1)); }
CRT(wcsnlen) { RET(wnlen(PTR(A0), (size_t)A1)); }
CRT(strcspn) { RET(strcspn(STR(A0), STR(A1))); }
CRT(strspn) { RET(strspn(STR(A0), STR(A1))); }
CRT(strpbrk) { RET((uintptr_t)strpbrk(STR(A0), STR(A1))); }
// strtok_s(texte, séparateurs, &contexte)
// Comme Windows : le contexte pointe après le séparateur, et sur le zéro final à la fin du texte (jamais
// NULL, contrairement à strtok_r de Bionic ; Skyrim lit le caractère pointé pour charger une sauvegarde).
CRT(strtok_s) {
    if (!A1 || !A2) {
        RET(0);
        return;
    }
    char *s = A0 ? (char *)PTR(A0) : (char *)PTR(rd64(A2));
    const char *delim = STR(A1);
    if (!s) {
        RET(0);
        return;
    }
    s += strspn(s, delim);
    if (!*s) {
        wr64(A2, (u64)(uintptr_t)s);
        RET(0);
        return;
    }
    char *end = s + strcspn(s, delim);
    if (*end) *end++ = 0;
    wr64(A2, (u64)(uintptr_t)end);
    RET((uintptr_t)s);
}
// strcat_s(destination, taille, source)
CRT(strcat_s) {
    size_t size = (size_t)A1;
    if (!A0 || !size || !A2) {
        RET(EINVAL_W);
        return;
    }
    size_t have = strnlen(STR(A0), size), add = strlen(STR(A2));
    if (have == size || have + add >= size) {
        wr8(A0, 0);
        RET(ERANGE_W);
        return;
    }
    memcpy((char *)PTR(A0) + have, STR(A2), add + 1);
    RET(0);
}
// strncpy_s(destination, taille, source, nombre) / strncat_s : nombre _TRUNCATE (-1) : tronqué sans erreur.
static int copy_n(u64 dest, size_t size, size_t at, const char *src, size_t count) {
    size_t n = strnlen(src, count == TRUNCATE ? (size_t)-1 : count);
    if (at + n >= size) {
        if (count == TRUNCATE) {
            n = size - at - 1;
            memcpy((char *)PTR(dest) + at, src, n);
            wr8(dest + at + n, 0);
            return STRUNCATE_W;
        }
        wr8(dest, 0);
        return ERANGE_W;
    }
    memcpy((char *)PTR(dest) + at, src, n);
    wr8(dest + at + n, 0);
    return 0;
}
CRT(strncpy_s) {
    if (!A0 || !A1) {
        RET(EINVAL_W);
        return;
    }
    if (!A2) {
        wr8(A0, 0);
        RET(A3 ? EINVAL_W : 0);
        return;
    }
    RET(copy_n(A0, (size_t)A1, 0, STR(A2), (size_t)A3));
}
CRT(strncat_s) {
    size_t size = (size_t)A1;
    if (!A0 || !size || !A2) {
        RET(EINVAL_W);
        return;
    }
    size_t have = strnlen(STR(A0), size);
    if (have == size) {
        wr8(A0, 0);
        RET(EINVAL_W);
        return;
    }
    RET(copy_n(A0, size, have, STR(A2), (size_t)A3));
}
static void change_case(RcnCtx *ctx, int upper) {
    if (!A0) {
        RET(EINVAL_W);
        return;
    }
    char *s = PTR(A0);
    size_t n = strnlen(s, (size_t)A1);
    if (n == (size_t)A1) {
        RET(EINVAL_W);
        return;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 128) s[i] = (char)(upper ? toupper(c) : tolower(c));
    }
    RET(0);
}
CRT(_strlwr_s) { change_case(ctx, 0); }
CRT(_strupr_s) { change_case(ctx, 1); }

// Caractères larges.
static u16 wlower(u16 c) { return c < 128 ? (u16)tolower(c) : c; }
static u16 wupper(u16 c) { return c < 128 ? (u16)toupper(c) : c; }
CRT(towlower) { RET(wlower((u16)A0)); }
CRT(towupper) { RET(wupper((u16)A0)); }
CRT(iswalnum) { RET_I32((u16)A0 < 128 && isalnum((int)(u16)A0)); }
CRT(iswalpha) { RET_I32((u16)A0 < 128 && isalpha((int)(u16)A0)); }
CRT(iswdigit) { RET_I32((u16)A0 < 128 && isdigit((int)(u16)A0)); }
CRT(iswspace) { RET_I32((u16)A0 < 128 && isspace((int)(u16)A0)); }
CRT(iswxdigit) { RET_I32((u16)A0 < 128 && isxdigit((int)(u16)A0)); }
CRT(iswupper) { RET_I32((u16)A0 < 128 && isupper((int)(u16)A0)); }
CRT(iswlower) { RET_I32((u16)A0 < 128 && islower((int)(u16)A0)); }
CRT(iswpunct) { RET_I32((u16)A0 < 128 && ispunct((int)(u16)A0)); }
static int wcompare(const u16 *a, const u16 *b, size_t n, int fold) {
    for (size_t i = 0; i < n; i++) {
        u16 x = fold ? wlower(a[i]) : a[i], y = fold ? wlower(b[i]) : b[i];
        if (x != y) return x < y ? -1 : 1;
        if (!x) break;
    }
    return 0;
}
CRT(_wcsicmp) { RET_I32(wcompare(PTR(A0), PTR(A1), (size_t)-1, 1)); }
CRT(_wcsnicmp) { RET_I32(wcompare(PTR(A0), PTR(A1), (size_t)A2, 1)); }
CRT(_wcsicoll) { RET_I32(wcompare(PTR(A0), PTR(A1), (size_t)-1, 1)); }
CRT(wcscoll) { RET_I32(wcompare(PTR(A0), PTR(A1), (size_t)-1, 0)); }
CRT(wcscmp) { RET_I32(wcompare(PTR(A0), PTR(A1), (size_t)-1, 0)); }
CRT(_wcsdup) {
    if (!A0) {
        RET(0);
        return;
    }
    size_t n = (wlen(PTR(A0)) + 1) * 2;
    void *copy = rcn_galloc(ctx, n, 0);
    if (copy) memcpy(copy, PTR(A0), n);
    RET((uintptr_t)copy);
}
CRT(wcschr) {
    const u16 *s = PTR(A0);
    u16 c = (u16)A1;
    for (;; s++) {
        if (*s == c) {
            RET((uintptr_t)s);
            return;
        }
        if (!*s) break;
    }
    RET(0);
}
CRT(wcsrchr) {
    const u16 *s = PTR(A0), *found = NULL;
    u16 c = (u16)A1;
    for (;; s++) {
        if (*s == c) found = s;
        if (!*s) break;
    }
    RET((uintptr_t)found);
}
CRT(wcsstr) {
    // Caractère par caractère : jamais au-delà de la fin du texte (memcmp de la longueur cherchée lisait
    // hors de la page près de la fin).
    const u16 *s = PTR(A0), *w = PTR(A1);
    if (!*w) {
        RET(A0);
        return;
    }
    for (; *s; s++) {
        size_t k = 0;
        while (w[k] && s[k] == w[k]) k++;
        if (!w[k]) {
            RET((uintptr_t)s);
            return;
        }
    }
    RET(0);
}
// wcscpy_s(destination, taille, source) / wcscat_s / wcsncpy_s(destination, taille, source, nombre)
static int wcopy(u64 dest, size_t size, size_t at, const u16 *src, size_t count) {
    size_t n = wnlen(src, count == TRUNCATE ? (size_t)-1 : count);
    if (at + n >= size) {
        if (count == TRUNCATE && size > at) {
            n = size - at - 1;
            memcpy((u16 *)PTR(dest) + at, src, n * 2);
            wr16(dest + 2 * (at + n), 0);
            return STRUNCATE_W;
        }
        wr16(dest, 0);
        return ERANGE_W;
    }
    memcpy((u16 *)PTR(dest) + at, src, n * 2);
    wr16(dest + 2 * (at + n), 0);
    return 0;
}
CRT(wcscpy_s) {
    if (!A0 || !A1 || !A2) {
        RET(EINVAL_W);
        return;
    }
    RET(wcopy(A0, (size_t)A1, 0, PTR(A2), (size_t)-2));
}
CRT(wcscat_s) {
    if (!A0 || !A1 || !A2) {
        RET(EINVAL_W);
        return;
    }
    size_t have = wnlen(PTR(A0), (size_t)A1);
    RET(have == (size_t)A1 ? EINVAL_W : wcopy(A0, (size_t)A1, have, PTR(A2), (size_t)-2));
}
CRT(wcsncpy_s) {
    if (!A0 || !A1) {
        RET(EINVAL_W);
        return;
    }
    if (!A2) {
        wr16(A0, 0);
        RET(A3 ? EINVAL_W : 0);
        return;
    }
    RET(wcopy(A0, (size_t)A1, 0, PTR(A2), (size_t)A3));
}

// --- Conversions ---

static int to_text(char *out, size_t size, i64 value, u32 radix, int is_signed) {
    char tmp[72];
    size_t n = 0;
    // Base 10 : signé ; autres bases : les 32 bits pris tels quels, comme sous Windows.
    int neg = is_signed && value < 0 && radix == 10;
    u64 v = neg ? (u64)0 - (u64)value : (u64)(u32)value;
    if (!is_signed) v = (u64)value;
    if (radix < 2 || radix > 36) return EINVAL_W;
    do {
        u32 d = (u32)(v % radix);
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= radix;
    } while (v);
    if (neg) tmp[n++] = '-';
    if (n + 1 > size) {
        if (size) out[0] = 0;
        return ERANGE_W;
    }
    for (size_t i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
    return 0;
}
// _itoa_s(valeur, tampon, taille, base) / _itow_s
CRT(_itoa_s) { RET(A1 ? to_text(PTR(A1), (size_t)A2, (i32)A0, (u32)A3, 1) : EINVAL_W); }
CRT(_itow_s) {
    char tmp[72];
    int r = to_text(tmp, sizeof tmp, (i32)A0, (u32)A3, 1);
    size_t n = strlen(tmp);
    if (!r && n + 1 > (size_t)A2) r = ERANGE_W;
    if (!A1) r = EINVAL_W;
    if (!r)
        for (size_t i = 0; i <= n; i++) wr16(A1 + 2 * i, (u8)tmp[i]);
    else if (A1 && A2)
        wr16(A1, 0);
    RET(r);
}
CRT(_ultoa) {
    to_text(PTR(A1), 72, (i64)(u32)A0, (u32)A2, 0);
    RET(A1);
}
CRT(atol) {
    long long v = strtoll(STR(A0), NULL, 10);
    RET_I32(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v);
}
CRT(strtof) {
    errno = 0;
    F32(0) = strtof(STR(A0), PTR(A1));
    if (errno) set_errno(ctx, errno);
}
CRT(_wtoi) {
    char *s = narrow(PTR(A0));
    long long v = s ? strtoll(s, NULL, 10) : 0;
    free(s);
    RET_I32(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v);
}
// wcsto* : nombre en ASCII ; la fin est rendue dans le texte large.
static u64 wide_number(RcnCtx *ctx, int is_signed) {
    char *s = narrow(PTR(A0));
    if (!s) return 0;
    char *end = s;
    errno = 0;
    u64 v = is_signed ? (u64)strtoll(s, &end, (int)A2) : (u64)strtoull(s, &end, (int)A2);
    if (errno) set_errno(ctx, errno);
    if (A1) wr64(A1, A0 + 2 * (u64)(end - s));
    free(s);
    return v;
}
CRT(wcstoll) { RET(wide_number(ctx, 1)); }
CRT(wcstoull) { RET(wide_number(ctx, 0)); }
CRT(wcstol) {
    i64 v = (i64)wide_number(ctx, 1);
    RET_I32(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v);
}
CRT(wcstoul) {
    u64 v = wide_number(ctx, 0);
    RET(v > UINT32_MAX ? UINT32_MAX : v);
}
CRT(wcstod) {
    char *s = narrow(PTR(A0));
    char *end = s;
    F64(0) = s ? strtod(s, &end) : 0;
    if (A1) wr64(A1, A0 + 2 * (u64)(s ? end - s : 0));
    free(s);
}
CRT(btowc) { RET((i32)A0 < 0 || (i32)A0 > 255 ? 0xFFFF : (u32)A0); }
CRT(wctob) { RET_I32((u16)A0 < 256 ? (i32)(u16)A0 : -1); }
// mbstowcs_s(&nombre, destination, taille, source, nombre_max) : un caractère par octet.
static int bytes_to_wide(u64 ret, u64 dest, size_t size, const u8 *src, size_t max) {
    size_t n = strnlen((const char *)src, max == TRUNCATE ? (size_t)-1 : max);
    if (dest) {
        if (n >= size) {
            if (max != TRUNCATE) {
                if (size) wr16(dest, 0);
                return ERANGE_W;
            }
            n = size ? size - 1 : 0;
        }
        for (size_t i = 0; i < n; i++) wr16(dest + 2 * i, src[i]);
        wr16(dest + 2 * n, 0);
    }
    if (ret) wr64(ret, n + 1);
    return 0;
}
CRT(mbstowcs_s) { RET(bytes_to_wide(A0, A1, (size_t)A2, PTR(A3), (size_t)STK(4))); }
// mbsrtowcs_s(&nombre, destination, taille, &source, nombre_max, état)
CRT(mbsrtowcs_s) {
    u64 src = rd64(A3);
    int r = bytes_to_wide(A0, A1, (size_t)A2, PTR(src), (size_t)STK(4));
    if (!r && A1) wr64(A3, 0);
    RET(r);
}
static int wide_to_bytes(u64 ret, u64 dest, size_t size, const u16 *src, size_t max) {
    size_t n = wnlen(src, max == TRUNCATE ? (size_t)-1 : max);
    if (dest) {
        if (n >= size) {
            if (max != TRUNCATE) {
                if (size) wr8(dest, 0);
                return ERANGE_W;
            }
            n = size ? size - 1 : 0;
        }
        for (size_t i = 0; i < n; i++) wr8(dest + i, src[i] < 256 ? (u8)src[i] : '?');
        wr8(dest + n, 0);
    }
    if (ret) wr64(ret, n + 1);
    return 0;
}
CRT(wcstombs_s) { RET(wide_to_bytes(A0, A1, (size_t)A2, PTR(A3), (size_t)STK(4))); }
CRT(wcsrtombs_s) {
    u64 src = rd64(A3);
    int r = wide_to_bytes(A0, A1, (size_t)A2, PTR(src), (size_t)STK(4));
    if (!r && A1) wr64(A3, 0);
    RET(r);
}
CRT(wcstombs) {
    const u16 *s = PTR(A1);
    size_t n = wlen(s), max = (size_t)A2;
    if (A0) {
        size_t i = 0;
        for (; i < n && i < max; i++) wr8(A0 + i, s[i] < 256 ? (u8)s[i] : '?');
        if (i < max) wr8(A0 + i, 0);
        n = i;
    }
    RET(n);
}
CRT(wctomb) {
    if (A0) wr8(A0, (u16)A1 < 256 ? (u8)A1 : '?');
    RET_I32(A0 ? 1 : 0);
}

// --- printf ---

// Format large → format de rcn_format : %s %c d'un format large désignent des textes larges (%S %C :
// étroits) ; la longueur explicite (h, l, w) est gardée.
static char *wide_format(const u16 *fmt) {
    size_t n = wlen(fmt);
    char *out = malloc(n * 2 + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        u16 c = fmt[i];
        out[o++] = c < 128 ? (char)c : '?';
        if (c != '%') continue;
        if (i + 1 < n && fmt[i + 1] == '%') {
            out[o++] = '%';
            i++;
            continue;
        }
        // Drapeaux, largeur, précision, puis longueur éventuelle.
        size_t j = i + 1;
        while (j < n && strchr("-+ #0123456789.*", fmt[j] < 128 ? (char)fmt[j] : '!')) out[o++] = (char)fmt[j++];
        int has_len = j < n && (fmt[j] == 'h' || fmt[j] == 'l' || fmt[j] == 'w' || fmt[j] == 'L' || fmt[j] == 'I' ||
                                fmt[j] == 'j' || fmt[j] == 'z' || fmt[j] == 't');
        if (!has_len && j < n) {
            if (fmt[j] == 's' || fmt[j] == 'c') out[o++] = 'l';
            else if (fmt[j] == 'S' || fmt[j] == 'C') out[o++] = 'h';
        }
        i = j - 1;
    }
    out[o] = 0;
    return out;
}
// Texte (UTF-8) → UTF-16 dans `dest` (count caractères au plus), comme __stdio_common_vs*printf.
static i64 put_wide_result(u64 options, u16 *dest, size_t count, const char *text, size_t len, int secure) {
    u16 *wide = malloc((len + 1) * 2);
    size_t n = wide ? rcn_utf8_to_utf16(text, len, wide) : 0;
    i64 result = (i64)n;
    if (dest && count) {
        if (n < count) {
            memcpy(dest, wide, n * 2);
            dest[n] = 0;
        } else if (secure) {
            dest[0] = 0;
            result = -1;
        } else if (options & 2) {
            memcpy(dest, wide, (count - 1) * 2);
            dest[count - 1] = 0;
        } else {
            memcpy(dest, wide, count * 2);
            if (options & 1) dest[count - 1] = 0;
            result = -1;
        }
    } else if (dest && !(options & 2)) {
        result = -1;
    }
    free(wide);
    return result;
}
static void format_wide(RcnCtx *ctx, RcnBuf *buf, u64 fmt, u64 va) {
    char *f = wide_format(PTR(fmt));
    if (!f) rcn_fail(ctx, "memoire epuisee (printf large)");
    rcn_format(buf, f, va);
    free(f);
}
// __stdio_common_vswprintf(options, tampon, nombre, format, locale, va_list) ; _p : même chose (arguments
// numérotés non gérés) ; _s : tampon vidé et -1 si le texte ne tient pas.
static void vswprintf_common(RcnCtx *ctx, int secure) {
    RcnBuf buf = {0};
    format_wide(ctx, &buf, A3, STK(5));
    // RCN_PRINTF_LOG=1 : textes formatés par le jeu (messages d'erreur avant un arrêt).
    static int log_text = -1;
    if (log_text < 0) log_text = getenv("RCN_PRINTF_LOG") != NULL;
    if (log_text) fprintf(stderr, "rcn : printf %.*s\n", (int)buf.len, buf.data ? buf.data : "");
    RET_I32(put_wide_result(A0, PTR(A1), (size_t)A2, buf.data ? buf.data : "", buf.len, secure));
    free(buf.data);
}
CRT(__stdio_common_vswprintf) { vswprintf_common(ctx, 0); }
// _vsnwprintf(tampon, nombre, format, va_list) (msvcrt) : -1 si tronqué, sans zéro final.
CRT(_vsnwprintf) {
    RcnBuf buf = {0};
    format_wide(ctx, &buf, A2, A3);
    RET_I32(put_wide_result(0, PTR(A0), (size_t)A1, buf.data ? buf.data : "", buf.len, 0));
    free(buf.data);
}
CRT(__stdio_common_vswprintf_p) { vswprintf_common(ctx, 0); }
CRT(__stdio_common_vswprintf_s) { vswprintf_common(ctx, 1); }
// __stdio_common_vsnwprintf_s(options, tampon, taille, nombre_max, format, locale, va_list)
CRT(__stdio_common_vsnwprintf_s) {
    RcnBuf buf = {0};
    format_wide(ctx, &buf, STK(4), STK(6));
    size_t size = (size_t)A2, max = (size_t)A3;
    u16 *dest = PTR(A1);
    u16 *wide = malloc((buf.len + 1) * 2);
    size_t n = wide ? rcn_utf8_to_utf16(buf.data ? buf.data : "", buf.len, wide) : 0;
    i64 r = (i64)n;
    if (dest && size) {
        size_t limit = max == TRUNCATE ? size - 1 : (max < size - 1 ? max : size - 1);
        if (n <= limit) {
            memcpy(dest, wide, n * 2);
            dest[n] = 0;
        } else if (max == TRUNCATE || max < size - 1 + 1) {
            memcpy(dest, wide, limit * 2);
            dest[limit] = 0;
            r = -1;
        } else {
            dest[0] = 0;
            r = -1;
        }
    }
    free(wide);
    free(buf.data);
    RET_I32(r);
}
// __stdio_common_vsprintf_s(options, tampon, nombre, format, locale, va_list)
static void vsprintf_s_common(RcnCtx *ctx, u64 fmt, u64 va, char *dest, size_t size, size_t max) {
    RcnBuf buf = {0};
    rcn_format(&buf, STR(fmt), va);
    i64 r = (i64)buf.len;
    if (dest && size) {
        size_t limit = max == TRUNCATE ? size - 1 : (max < size - 1 ? max : size - 1);
        if (buf.len <= limit) {
            memcpy(dest, buf.data ? buf.data : "", buf.len);
            dest[buf.len] = 0;
        } else if (max != (size_t)-2 && (max == TRUNCATE || max <= size - 1)) {
            memcpy(dest, buf.data, limit);
            dest[limit] = 0;
            r = -1;
        } else {
            dest[0] = 0;
            r = -1;
        }
    }
    free(buf.data);
    RET_I32(r);
}
CRT(__stdio_common_vsprintf_s) { vsprintf_s_common(ctx, A3, STK(5), PTR(A1), (size_t)A2, (size_t)-2); }
CRT(__stdio_common_vsnprintf_s) { vsprintf_s_common(ctx, STK(4), STK(6), PTR(A1), (size_t)A2, (size_t)A3); }
// __stdio_common_vsprintf_p : arguments numérotés non gérés, comme vsprintf.
CRT(__stdio_common_vsprintf_p) {
    RcnBuf buf = {0};
    rcn_format(&buf, STR(A3), STK(5));
    size_t count = (size_t)A2;
    char *dest = PTR(A1);
    i64 r = (i64)buf.len;
    if (dest && count) {
        size_t n = buf.len < count ? buf.len : count - 1;
        memcpy(dest, buf.data ? buf.data : "", n);
        dest[n] = 0;
        if (buf.len >= count) r = -1;
    }
    free(buf.data);
    RET_I32(r);
}
// __stdio_common_vfprintf_s / _p (options, fichier, format, locale, va_list)
static void fprint(RcnCtx *ctx, const char *fmt, u64 va) {
    RcnBuf buf = {0};
    rcn_format(&buf, fmt, va);
    size_t w = rcn_crt_write(ctx, A1, buf.data, buf.len);
    RET_I32(w == buf.len ? (i64)buf.len : -1);
    free(buf.data);
}
CRT(__stdio_common_vfprintf_s) { fprint(ctx, STR(A2), STK(4)); }
CRT(__stdio_common_vfprintf_p) { fprint(ctx, STR(A2), STK(4)); }
// __stdio_common_vfwprintf(options, fichier, format, locale, va_list) : texte écrit en octets.
static void fwprint(RcnCtx *ctx) {
    RcnBuf buf = {0};
    format_wide(ctx, &buf, A2, STK(4));
    size_t w = rcn_crt_write(ctx, A1, buf.data, buf.len);
    RET_I32(w == buf.len ? (i64)buf.len : -1);
    free(buf.data);
}
CRT(__stdio_common_vfwprintf) { fwprint(ctx); }
CRT(__stdio_common_vfwprintf_s) { fwprint(ctx); }
CRT(__stdio_common_vfwprintf_p) { fwprint(ctx); }

// --- scanf : fichier et texte large (nombres seulement pour le texte large) ---

static int scan_pointers(void *p[16], u64 va) {
    for (int i = 0; i < 16; i++) p[i] = PTR(rd64(va + 8 * (u64)i));
    return 0;
}
// __stdio_common_vfscanf(options, fichier, format, locale, va_list)
CRT(__stdio_common_vfscanf) {
    char *format = rcn_scan_format(STR(A2));
    if (!format) rcn_fail(ctx, "format de scanf non gere : %s", STR(A2));
    void *p[16];
    scan_pointers(p, STK(4));
    int r = rcn_crt_std(ctx, A1) ? EOF
                                 : fscanf(PTR(A1), format, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9],
                                          p[10], p[11], p[12], p[13], p[14], p[15]);
    free(format);
    RET_I32(r);
}
static void wide_scan(RcnCtx *ctx, const char *input, u64 fmt, u64 va) {
    char *f = narrow(PTR(fmt));
    if (!f) rcn_fail(ctx, "memoire epuisee (scanf large)");
    for (const char *q = f; *q; q++) {
        if (*q == '%' && q[1] != '%') {
            const char *e = q + 1;
            while (*e && strchr("*0123456789hlwILjzt", *e)) e++;
            if (*e == 's' || *e == 'c' || *e == '[' || *e == 'S' || *e == 'C') rcn_fail(ctx, "scanf large de texte non gere : %s", f);
        }
    }
    char *format = rcn_scan_format(f);
    if (!format) rcn_fail(ctx, "format de scanf non gere : %s", f);
    void *p[16];
    scan_pointers(p, va);
    int r = input ? sscanf(input, format, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11],
                           p[12], p[13], p[14], p[15])
                  : EOF;
    free(format);
    free(f);
    RET_I32(r);
}
// __stdio_common_vswscanf(options, texte, nombre, format, locale, va_list)
CRT(__stdio_common_vswscanf) {
    const u16 *w = PTR(A1);
    size_t n = (size_t)A2 == (size_t)-1 ? wlen(w) : wnlen(w, (size_t)A2);
    char *input = malloc(n + 1);
    if (input) {
        for (size_t i = 0; i < n; i++) input[i] = w[i] < 128 ? (char)w[i] : '?';
        input[n] = 0;
    }
    wide_scan(ctx, input, A3, STK(5));
    free(input);
}
CRT(__stdio_common_vfwscanf) { rcn_fail(ctx, "fwscanf non gere"); }

// --- Temps ---

// struct tm de Windows : les neuf champs entiers de C, sans les ajouts de l'hôte.
static void tm_to_guest(u64 out, const struct tm *t) {
    const int f[9] = {t->tm_sec, t->tm_min, t->tm_hour, t->tm_mday, t->tm_mon, t->tm_year, t->tm_wday, t->tm_yday, t->tm_isdst};
    for (int i = 0; i < 9; i++) wr32(out + 4 * (u64)i, (u32)f[i]);
}
static struct tm tm_from_guest(u64 in) {
    struct tm t;
    memset(&t, 0, sizeof t);
    int *f[9] = {&t.tm_sec, &t.tm_min, &t.tm_hour, &t.tm_mday, &t.tm_mon, &t.tm_year, &t.tm_wday, &t.tm_yday, &t.tm_isdst};
    for (int i = 0; i < 9; i++) *f[i] = (i32)rd32(in + 4 * (u64)i);
    return t;
}
static _Thread_local u32 tm_static[9];
static int time_convert(u64 out, u64 time_ptr, int local) {
    if (!out || !time_ptr) return EINVAL_W;
    time_t t = (time_t)(i64)rd64(time_ptr);
    struct tm r;
    if (!(local ? localtime_r(&t, &r) : gmtime_r(&t, &r))) return EINVAL_W;
    tm_to_guest(out, &r);
    return 0;
}
CRT(_gmtime64_s) { RET(time_convert(A0, A1, 0)); }
CRT(_localtime64_s) { RET(time_convert(A0, A1, 1)); }
CRT(_gmtime64) { RET(time_convert((uintptr_t)tm_static, A0, 0) ? 0 : (uintptr_t)tm_static); }
CRT(_localtime64) { RET(time_convert((uintptr_t)tm_static, A0, 1) ? 0 : (uintptr_t)tm_static); }
CRT(_mkgmtime64) {
    struct tm t = tm_from_guest(A0);
    time_t v = timegm(&t);
    tm_to_guest(A0, &t);
    RET((i64)v);
}
CRT(_mktime64) {
    struct tm t = tm_from_guest(A0);
    time_t v = mktime(&t);
    tm_to_guest(A0, &t);
    RET((i64)v);
}
// _ftime64(&timeb) : secondes, millisecondes, fuseau (minutes), heure d'été.
CRT(_ftime64) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    wr64(A0, (u64)ts.tv_sec);
    wr16(A0 + 8, (u16)(ts.tv_nsec / 1000000));
    wr16(A0 + 10, 0);
    wr16(A0 + 12, 0);
}
static size_t format_time(char *out, size_t max, const char *fmt, u64 tm) {
    struct tm t = tm_from_guest(tm);
    return max ? strftime(out, max, fmt, &t) : 0;
}
// strftime(tampon, taille, format, tm) ; _Strftime(…, données régionales) : même chose (« C »).
CRT(strftime) { RET(format_time(PTR(A0), (size_t)A1, STR(A2), A3)); }
CRT(_Strftime) { RET(format_time(PTR(A0), (size_t)A1, STR(A2), A3)); }
// _Wcsftime(tampon large, taille, format large, tm, données régionales)
CRT(_Wcsftime) {
    char *fmt = narrow(PTR(A2));
    size_t max = (size_t)A1;
    char *tmp = malloc(max ? max * 4 : 1);
    size_t n = fmt && tmp ? format_time(tmp, max, fmt, A3) : 0;
    for (size_t i = 0; i < n; i++) wr16(A0 + 2 * i, (u8)tmp[i]);
    if (max) wr16(A0 + 2 * (n < max ? n : max - 1), 0);
    free(fmt);
    free(tmp);
    RET(n);
}
CRT(wcsftime) { crt__Wcsftime(ctx); }
// Noms des jours et des mois (« :Sun:Sunday:Mon:Monday… »), à libérer par le jeu.
static const char days_text[] = ":Sun:Sunday:Mon:Monday:Tue:Tuesday:Wed:Wednesday:Thu:Thursday:Fri:Friday:Sat:Saturday";
static const char months_text[] = ":Jan:January:Feb:February:Mar:March:Apr:April:May:May:Jun:June:Jul:July:Aug:August"
                                  ":Sep:September:Oct:October:Nov:November:Dec:December";
static u64 copy_names(RcnCtx *ctx, const char *text, int wide) {
    size_t n = strlen(text) + 1;
    u8 *out = rcn_galloc(ctx, wide ? n * 2 : n, 0);
    if (!out) return 0;
    for (size_t i = 0; i < n; i++) {
        if (wide) wr16((u64)(uintptr_t)out + 2 * i, (u8)text[i]);
        else out[i] = (u8)text[i];
    }
    return (u64)(uintptr_t)out;
}
CRT(_Getdays) { RET(copy_names(ctx, days_text, 0)); }
CRT(_Getmonths) { RET(copy_names(ctx, months_text, 0)); }
CRT(_W_Getdays) { RET(copy_names(ctx, days_text, 1)); }
CRT(_W_Getmonths) { RET(copy_names(ctx, months_text, 1)); }
// _Gettnames : données régionales du temps, opaques (rendues à _Strftime, qui les ignore).
CRT(_Gettnames) { RET((uintptr_t)rcn_galloc(ctx, 1024, 1)); }
CRT(_W_Gettnames) { RET((uintptr_t)rcn_galloc(ctx, 1024, 1)); }

// --- Divers ---

// rand / srand : le générateur congruentiel de la bibliothèque C de Windows (graine par thread).
static _Thread_local u32 rand_seed = 1;
CRT(srand) { rand_seed = (u32)A0; }
CRT(rand) {
    rand_seed = rand_seed * 214013u + 2531011u;
    RET((rand_seed >> 16) & 0x7FFF);
}
CRT(rand_s) {
    static _Atomic u64 state = 0x9E3779B97F4A7C15ull;
    u64 x = state += 0x9E3779B97F4A7C15ull ^ rcn_now_ns();
    x ^= x >> 30, x *= 0xBF58476D1CE4E5B9ull, x ^= x >> 27, x *= 0x94D049BB133111EBull, x ^= x >> 31;
    if (A0) wr32(A0, (u32)x);
    RET(A0 ? 0 : EINVAL_W);
}
// bsearch_s(clé, base, nombre, taille, comparaison, contexte) : comparaison(contexte, a, b).
CRT(bsearch_s) {
    u64 key = A0, base = A1, count = A2, size = A3, fn = STK(4), context = STK(5);
    u64 lo = 0, hi = count;
    while (lo < hi) {
        u64 mid = lo + (hi - lo) / 2, at = base + mid * size;
        i32 c = (i32)rcn_call_guest(ctx, fn, context, key, at, 0);
        if (c == 0) {
            RET(at);
            return;
        }
        if (c < 0) hi = mid;
        else lo = mid + 1;
    }
    RET(0);
}
// qsort_s(base, nombre, taille, comparaison, contexte) : tri par insertion binaire sur place (stable,
// sans mémoire de l'hôte à rappeler).
CRT(qsort_s) {
    u64 base = A0, count = A1, size = A2, fn = A3, context = STK(4);
    u8 *tmp = malloc((size_t)size);
    if (!tmp) return;
    for (u64 i = 1; i < count; i++) {
        memcpy(tmp, PTR(base + i * size), (size_t)size);
        u64 lo = 0, hi = i;
        while (lo < hi) {
            u64 mid = lo + (hi - lo) / 2;
            if ((i32)rcn_call_guest(ctx, fn, context, (u64)(uintptr_t)tmp, base + mid * size, 0) < 0) hi = mid;
            else lo = mid + 1;
        }
        if (lo < i) {
            memmove(PTR(base + (lo + 1) * size), PTR(base + lo * size), (size_t)((i - lo) * size));
            memcpy(PTR(base + lo * size), tmp, (size_t)size);
        }
    }
    free(tmp);
}


// --- Unreal Engine 5 : compléments ---

CRT(__initialize_lconv_for_unsigned_char) { RET_I32(0); }
CRT(_dsign) { RET_I32(signbit(F64(0)) ? 1 : 0); }
CRT(_fdsign) { RET_I32(signbit(F32(0)) ? 1 : 0); }
CRT(_ldsign) { RET_I32(signbit(F64(0)) ? 1 : 0); }
// _dupenv_s(&texte, &taille, nom) : environnement vide.
CRT(_dupenv_s) {
    if (A0) wr64(A0, 0);
    if (A1) wr64(A1, 0);
    RET_I32(0);
}
CRT(_dtest) { RET_I32(fp_class(*(const double *)PTR(A0))); }
CRT(_fdclass) { RET_I32(fp_classf(F32(0))); }
CRT(cbrt) { F64(0) = cbrt(F64(0)); }
CRT(exp2) { F64(0) = exp2(F64(0)); }
CRT(exp2f) { F32(0) = exp2f(F32(0)); }
CRT(hypot) { F64(0) = hypot(F64(0), F64(1)); }
CRT(hypotf) { F32(0) = hypotf(F32(0), F32(1)); }
CRT(logbf) { F32(0) = logbf(F32(0)); }
CRT(lroundf) { RET_I32(lroundf(F32(0))); }
CRT(lround) { RET_I32(lround(F64(0))); }
CRT(nextafterf) { F32(0) = nextafterf(F32(0), F32(1)); }
CRT(nextafter) { F64(0) = nextafter(F64(0), F64(1)); }
CRT(iscntrl) { RET_I32((u32)A0 < 256 ? iscntrl((int)A0) != 0 : 0); }
CRT(wcsncpy) {
    u16 *d = PTR(A0);
    const u16 *src = PTR(A1);
    size_t n = (size_t)A2, i = 0;
    for (; i < n && src[i]; i++) d[i] = src[i];
    for (; i < n; i++) d[i] = 0;
    RET(A0);
}
// div : div_t { quot, rem } rendu dans rax.
CRT(div) {
    i32 a = (i32)A0, b = (i32)A1;
    i32 q = b ? a / b : 0, r = b ? a % b : 0;
    RET((u64)(u32)q | (u64)(u32)r << 32);
}
CRT(_getpid) { RET_I32(1000); } // comme GetCurrentProcessId
CRT(raise) { RET_I32(0); }
CRT(strerror) { RET((uintptr_t)strerror((int)A0)); }
CRT(strerror_s) {
    if (A0 && A1) snprintf(PTR(A0), (size_t)A1, "%s", strerror((int)A2));
    RET_I32(0);
}
// Environnement du jeu : vide.
CRT(getenv_s) {
    if (A0) wr64(A0, 0);
    if (A1 && A2) *(char *)PTR(A1) = 0;
    RET_I32(0);
}
CRT(_wgetenv) { RET(0); }
CRT(_wgetenv_s) {
    if (A0) wr64(A0, 0);
    if (A1 && A2) *(u16 *)PTR(A1) = 0;
    RET_I32(0);
}
CRT(_putenv_s) { RET_I32(0); }
CRT(_wputenv_s) { RET_I32(0); }
// Blocs de _aligned_malloc : adresse d'origine rangée juste avant (rcn_crt.c).
CRT(_aligned_msize) {
    if (!A0) {
        RET((u64)-1);
        return;
    }
    u64 raw = rd64(A0 - 8);
    RET(usable_size(PTR(raw)) - (A0 - raw));
}
CRT(_aligned_realloc) {
    u64 old = A0, size = A1, align = A2 < 16 ? 16 : A2;
    if (!size) {
        if (old) rcn_gfree(ctx, PTR(rd64(old - 8)));
        RET(0);
        return;
    }
    u64 raw = (u64)(uintptr_t)rcn_galloc(ctx, (size_t)(size + align + 8), 0);
    if (!raw) {
        RET(0);
        return;
    }
    u64 p = (raw + 8 + align - 1) & ~(align - 1);
    wr64(p - 8, raw);
    if (old) {
        u64 oraw = rd64(old - 8);
        u64 osize = usable_size(PTR(oraw)) - (old - oraw);
        memcpy(PTR(p), PTR(old), (size_t)(osize < size ? osize : size));
        rcn_gfree(ctx, PTR(oraw));
    }
    RET(p);
}
CRT(_get_heap_handle) { RET(0x10000); }
CRT(_heapchk) { RET_I32(-2); } // _HEAPOK
CRT(_wtof) {
    char *t = narrow(PTR(A0));
    F64(0) = t ? strtod(t, NULL) : 0.0;
    free(t);
}
CRT(_wtoi64) {
    char *t = narrow(PTR(A0));
    RET(t ? (u64)strtoll(t, NULL, 10) : 0);
    free(t);
}
CRT(_wcstoi64) { RET(wide_number(ctx, 1)); }
CRT(_wcstoui64) { RET(wide_number(ctx, 0)); }
CRT(_strtol_l) {
    errno = 0;
    long long v = strtoll(STR(A0), PTR(A1), (int)A2);
    if (errno || v > INT32_MAX || v < INT32_MIN) set_errno(ctx, 34);
    RET_I32(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v);
}
CRT(_strtof_l) {
    errno = 0;
    F32(0) = strtof(STR(A0), PTR(A1));
    if (errno) set_errno(ctx, errno);
}
// _wfullpath(tampon, chemin, taille) : chemin complet (rcn_full_path) ; tampon NULL : alloué.
CRT(_wfullpath) {
    char *name = A1 ? rcn_utf16_to_utf8(PTR(A1), -1) : NULL;
    char *full = name ? rcn_full_path(ctx, name) : NULL;
    free(name);
    if (!full) {
        RET(0);
        return;
    }
    size_t n = strlen(full);
    u16 *wide = malloc((n + 1) * 2);
    size_t w = wide ? rcn_utf8_to_utf16(full, n, wide) : 0;
    free(full);
    u64 out = A0;
    if (!out) out = (u64)(uintptr_t)rcn_galloc(ctx, (w + 1) * 2, 0);
    else if (w + 1 > (size_t)A2) out = 0;
    if (out && wide) {
        memcpy(PTR(out), wide, w * 2);
        wr16(out + 2 * w, 0);
    }
    free(wide);
    RET(out);
}
CRT(_mbspbrk) { RET((uintptr_t)strpbrk(STR(A0), STR(A1))); }
// Fuseau horaire : UTC (comme localtime des autres fonctions du runtime).
static i32 tz_seconds, tz_daylight;
static char tz_std[] = "UTC", tz_dst[] = "UTC";
static u64 tz_names[2];
CRT(_tzset) { (void)ctx; }
CRT(__timezone) { RET((uintptr_t)&tz_seconds); }
CRT(__daylight) { RET((uintptr_t)&tz_daylight); }
CRT(__dstbias) { RET((uintptr_t)&tz_daylight); }
CRT(__tzname) {
    tz_names[0] = (uintptr_t)tz_std, tz_names[1] = (uintptr_t)tz_dst;
    RET((uintptr_t)tz_names);
}
CRT(_get_timezone) {
    if (A0) wr32(A0, 0);
    RET_I32(0);
}
CRT(_get_daylight) {
    if (A0) wr32(A0, 0);
    RET_I32(0);
}
CRT(_cputs) {
    fputs(STR(A0), stderr);
    RET_I32(0);
}

#define E(name) {#name, crt_##name}
const RcnShim rcn_crt_more[] = {
    {"_wfopen", crt__wfsopen}, E(_ldsign), E(_dupenv_s), E(_wfullpath), E(_open), E(_wopen), E(_creat), E(_wsopen_s), E(_read), E(_write), E(_close), E(_lseeki64), E(_lseek), E(_fstat64), E(_fstat64i32), E(_locking), E(_isatty), E(_setmode), E(_commit), E(_dup), E(_dup2), E(_fileno), E(_open_osfhandle), E(_unlink), E(_waccess), E(_tzset), E(__timezone), E(__daylight), E(__dstbias), E(__tzname), E(_get_timezone), E(_get_daylight),
    E(__initialize_lconv_for_unsigned_char), E(_dsign), E(_fdsign), E(_dtest), E(_fdclass), E(cbrt), E(exp2), E(exp2f), E(hypot), E(hypotf), E(logbf), E(lroundf), E(lround), E(nextafterf), E(nextafter), E(iscntrl), E(wcsncpy), E(div), E(_getpid), E(raise), E(strerror), E(strerror_s), E(getenv_s), E(_wgetenv), E(_wgetenv_s), E(_putenv_s), E(_wputenv_s), E(_aligned_msize), E(_aligned_realloc), E(_get_heap_handle), E(_heapchk), E(_wtof), E(_wtoi64), E(_wcstoi64), E(_wcstoui64), E(_strtol_l), E(_strtof_l), E(_mbspbrk), E(_cputs),
    E(_fsopen), E(fopen_s), E(_wfsopen), E(_wfopen_s), E(fgetpos), E(fsetpos), E(fgetwc), E(ungetwc), E(fputwc),
    E(_lock_file), E(_unlock_file), E(_get_stream_buffer_pointers), E(_access), E(_stat64i32), E(_stat64),
    E(_wstat64), E(_wstat64i32), E(_wremove), E(_wrename), E(_wrmdir), E(_wchdir), E(_getcwd), E(_wgetcwd),
    E(_splitpath_s), E(_makepath_s), E(_msize), E(_callnewh), E(_set_new_handler), E(_beginthread), E(_endthread),
    E(_crt_at_quick_exit), E(_register_thread_local_exe_atexit_callback), E(_get_narrow_winmain_command_line),
    E(_invalid_parameter_noinfo), E(_invalid_parameter_noinfo_noreturn), E(_invoke_watson), E(_wassert), E(_set_fmode),
    E(__pctype_func), E(___lc_codepage_func), E(___lc_collate_cp_func), E(___mb_cur_max_func), E(___lc_locale_name_func),
    E(_lock_locales), E(_unlock_locales), E(localeconv), E(_dclass), E(_ldclass), E(_fdtest), E(_finite), E(_isnan),
    E(copysign), E(copysignf), E(fminf), E(fmaxf), E(fmin), E(fmax), E(frexp), E(ldexp), E(modf), E(modff),
    E(strnlen), E(__strncnt), E(wcsnlen), E(strcspn), E(strspn), E(strpbrk), E(strtok_s), E(strcat_s), E(strncpy_s),
    E(strncat_s), E(_strlwr_s), E(_strupr_s), E(towlower), E(towupper), E(iswalnum), E(iswalpha), E(iswdigit),
    E(iswspace), E(iswxdigit), E(iswupper), E(iswlower), E(iswpunct), E(_wcsicmp), E(_wcsnicmp), E(_wcsicoll),
    E(wcscoll), E(wcscmp), E(_wcsdup), E(wcschr), E(wcsrchr), E(wcsstr), E(wcscpy_s), E(wcscat_s), E(wcsncpy_s),
    E(_itoa_s), E(_itow_s), E(_ultoa), E(atol), E(strtof), E(_wtoi), E(wcstoll), E(wcstoull), E(wcstol), E(wcstoul),
    E(wcstod), E(btowc), E(wctob), E(mbstowcs_s), E(mbsrtowcs_s), E(wcstombs_s), E(wcsrtombs_s), E(wcstombs), E(wctomb),
    E(__stdio_common_vswprintf), E(__stdio_common_vswprintf_p), E(__stdio_common_vswprintf_s), E(_vsnwprintf),
    E(__stdio_common_vsnwprintf_s), E(__stdio_common_vsprintf_s), E(__stdio_common_vsnprintf_s),
    E(__stdio_common_vsprintf_p), E(__stdio_common_vfprintf_s), E(__stdio_common_vfprintf_p),
    E(__stdio_common_vfwprintf), E(__stdio_common_vfwprintf_s), E(__stdio_common_vfwprintf_p),
    E(__stdio_common_vfscanf), E(__stdio_common_vswscanf), E(__stdio_common_vfwscanf), E(_gmtime64_s),
    E(_localtime64_s), E(_gmtime64), E(_localtime64), E(_mkgmtime64), E(_mktime64), E(_ftime64), E(strftime),
    E(_Strftime), E(_Wcsftime), E(wcsftime), E(_Getdays), E(_Getmonths), E(_W_Getdays), E(_W_Getmonths),
    E(_Gettnames), E(_W_Gettnames), E(srand), E(rand), E(rand_s), E(bsearch_s), E(qsort_s),
    {"_wcsicmp_l", crt__wcsicmp},
};
const size_t rcn_crt_more_count = sizeof rcn_crt_more / sizeof rcn_crt_more[0];
