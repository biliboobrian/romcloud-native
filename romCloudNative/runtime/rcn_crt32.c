// RomCloud Native : bibliothèque C de Visual C++ (msvcr80/90/100…) pour les jeux x86 32 bits.
//
// Différences avec le 64 bits (rcn_crt.c) :
//   - arguments sur la pile (convention cdecl : l'appelant les retire), lus par A0, A1… ;
//   - un double occupe deux cases ; un résultat flottant est rendu au sommet de la pile x87 (ST(0)) ; un
//     entier de 64 bits dans edx:eax ;
//   - toute adresse donnée au jeu doit tenir sur 32 bits : FILE, struct tm, lconv, argv… sont rangés dans la
//     mémoire du jeu (sous 2 Go), jamais dans celle de l'hôte ;
//   - structures à la disposition de Windows 32 bits (FILE de 32 octets, _stat64…).
// Les exceptions (SEH et C++) ne sont pas encore gérées en 32 bits : leurs gestionnaires existent pour le
// chargement, et arrêtent le jeu en le disant s'ils sont appelés.
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <wctype.h>

#include "rcn_internal.h"

#if defined(_WIN32)
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#define mkdir_host(p) _mkdir(p)
#define localtime_r(t, tm) (localtime_s((tm), (t)) ? NULL : (tm))
#define gmtime_r(t, tm) (gmtime_s((tm), (t)) ? NULL : (tm))
#include <direct.h>
#else
#include <strings.h>
#include <unistd.h>
#define fseek64 fseeko
#define ftell64 ftello
#define mkdir_host(p) mkdir(p, 0777)
#endif

#define C3(name) static void c32_##name(RcnCtx *ctx)

// Argument flottant (double) commençant à la case n (deux cases de 4 octets).
static double arg_f64(RcnCtx *ctx, int n) { return rdf64((u32)(ctx->r[4] + 4 + 4 * (u32)n)); }
// Résultats : double au sommet de la pile x87, entier de 64 bits dans edx:eax.
static void ret_f64(RcnCtx *ctx, double v) { rcn_fpush(ctx, v); }
static void ret_u64(RcnCtx *ctx, u64 v) {
    ctx->r[0] = (u32)v;
    ctx->r[2] = (u32)(v >> 32);
}
static void sync_errno(RcnCtx *ctx) { THREAD(ctx)->crt_errno = errno; }

// --- Données de la bibliothèque dans la mémoire du jeu (adresses de 32 bits) ---
// FILE de Windows 32 bits : 32 octets. Les nôtres portent une marque et le numéro du FILE de l'hôte.
enum { FILE32 = 32, IOB_COUNT = 20, MAX_FILES = 512, FILE_MAGIC = 0x33464352 /* « RCF3 » */ };
typedef struct {
    u32 fmode, commode, adjust_fdiv;
    u32 acmdln, wcmdln;          // char *_acmdln, wchar_t *_wcmdln
    u32 argc, argv, envp, wargv; // __getmainargs
    u32 errno_slot;
    u8 iob[IOB_COUNT * FILE32];  // _iob : stdin, stdout, stderr, puis libres
    u8 tm[44];                   // struct tm de localtime / gmtime
    u8 lconv[56];                // struct lconv (pointeurs de 32 bits)
    char decimal[2], empty[1];
    char ctime_text[32];         // ctime
    u16 wctime_text[32];         // _wctime
    u32 mb_cur_max;              // __mb_cur_max (msvcrt.dll) : 1
    u32 pioinfo[64];             // __pioinfo (msvcrt.dll) : chaque bloc désigne badioinfo
    u8 badioinfo[32 * 8];        // __badioinfo : descripteurs vides
    u32 env_null;                // environnement vide : { NULL }
} Crt32Data;
static Crt32Data *data32;
static FILE *host_files[MAX_FILES];
static pthread_mutex_t files_lock = PTHREAD_MUTEX_INITIALIZER;

static void crt32_init(void);
static Crt32Data *crt32(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, crt32_init);
    return data32;
}
static void crt32_init(void) {
    data32 = rcn_low_alloc(sizeof(Crt32Data), 1);
    if (!data32) return;
    // stdin, stdout, stderr : FILE marqués, numéros 1 à 3 (0 : fichier de l'hôte).
    for (u32 i = 0; i < 3; i++) {
        wr32((u64)(uintptr_t)data32->iob + i * FILE32 + 24, FILE_MAGIC);
        wr32((u64)(uintptr_t)data32->iob + i * FILE32 + 28, 0x80000000u | (i + 1));
    }
    data32->decimal[0] = '.';
    // struct lconv (msvcr90) : decimal_point, thousands_sep, grouping, int_curr_symbol, currency_symbol,
    // mon_decimal_point, mon_thousands_sep, mon_grouping, positive_sign, negative_sign, puis des char.
    u32 dec = (u32)(uintptr_t)data32->decimal, empty = (u32)(uintptr_t)data32->empty;
    u64 l = (u64)(uintptr_t)data32->lconv;
    wr32(l, dec);
    for (int i = 1; i < 10; i++) wr32(l + 4 * (u64)i, empty);
    memset(data32->lconv + 40, 0x7F, 8); // CHAR_MAX : sans objet
    data32->envp = (u32)(uintptr_t)&data32->env_null;
    data32->mb_cur_max = 1;
    for (int i = 0; i < 64; i++) data32->pioinfo[i] = (u32)(uintptr_t)data32->badioinfo;
}

// Adresse des variables exportées par la bibliothèque (imports de données) ; 0 si inconnue.
u64 rcn_crt32_data(const char *name) {
    Crt32Data *d = crt32();
    if (!d) return 0;
    if (!strcmp(name, "_acmdln")) return (u64)(uintptr_t)&d->acmdln;
    if (!strcmp(name, "_wcmdln")) return (u64)(uintptr_t)&d->wcmdln;
    if (!strcmp(name, "_adjust_fdiv")) return (u64)(uintptr_t)&d->adjust_fdiv;
    if (!strcmp(name, "_iob")) return (u64)(uintptr_t)d->iob;
    if (!strcmp(name, "_fmode")) return (u64)(uintptr_t)&d->fmode;
    if (!strcmp(name, "_commode")) return (u64)(uintptr_t)&d->commode;
    if (!strcmp(name, "__initenv") || !strcmp(name, "_environ")) return (u64)(uintptr_t)&d->envp;
    if (!strcmp(name, "__mb_cur_max")) return (u64)(uintptr_t)&d->mb_cur_max;
    if (!strcmp(name, "__pioinfo")) return (u64)(uintptr_t)d->pioinfo;
    if (!strcmp(name, "__badioinfo")) return (u64)(uintptr_t)d->badioinfo;
    return 0;
}

// Ligne de commande : rangée dans _acmdln / _wcmdln au démarrage du CRT (avant que WinMain ne la lise).
static void fill_command_line(RcnCtx *ctx) {
    Crt32Data *d = crt32();
    if (!d || d->acmdln) return;
    d->acmdln = (u32)(uintptr_t)rcn_command_line(ctx);
    d->wcmdln = (u32)(uintptr_t)ctx->proc->command_line_w;
}

// --- FILE du jeu ↔ FILE de l'hôte ---
// 0 : pas un FILE à nous ; 1..3 : stdin, stdout, stderr ; sinon 0x100 + numéro de fichier de l'hôte.
static u32 file_id(u64 f) {
    if (!f || rd32(f + 24) != FILE_MAGIC) return 0;
    return rd32(f + 28);
}
static FILE *host_file(u64 f) {
    u32 id = file_id(f);
    if (!id || (id & 0x80000000u)) return NULL;
    return id - 1 < MAX_FILES ? host_files[id - 1] : NULL;
}
static int std_stream(u64 f) {
    u32 id = file_id(f);
    return id & 0x80000000u ? (int)(id & 0xFF) : 0;
}
static u64 wrap_file(FILE *f) {
    if (!f) return 0;
    pthread_mutex_lock(&files_lock);
    u32 slot = 0;
    while (slot < MAX_FILES && host_files[slot]) slot++;
    if (slot == MAX_FILES) {
        pthread_mutex_unlock(&files_lock);
        fclose(f);
        return 0;
    }
    host_files[slot] = f;
    pthread_mutex_unlock(&files_lock);
    u8 *g = rcn_low_alloc(FILE32, 1);
    if (!g) return 0;
    wr32((u64)(uintptr_t)g + 24, FILE_MAGIC);
    wr32((u64)(uintptr_t)g + 28, slot + 1);
    return (u64)(uintptr_t)g;
}
static size_t write_file(RcnCtx *ctx, u64 f, const void *buf, size_t len) {
    int std = std_stream(f);
    if (std == 2 || std == 3) {
        rcn_host_write(ctx, std == 3 ? RCN_STREAM_STDERR : RCN_STREAM_STDOUT, buf, len);
        return len;
    }
    FILE *h = host_file(f);
    return h ? fwrite(buf, 1, len, h) : 0;
}

// Mode d'ouverture : binaire toujours, options de Windows ignorées (« ccs=… », « t », « N »…).
static FILE *open_host(RcnCtx *ctx, const char *windows_path, const char *mode) {
    char host_mode[8];
    size_t n = 0;
    for (const char *m = mode; *m && *m != ','; m++)
        if (strchr("rwa+x", *m) && n < 4) host_mode[n++] = *m;
    host_mode[n++] = 'b';
    host_mode[n] = 0;
    char *path = rcn_host_path(ctx, windows_path);
    FILE *f = path ? fopen(path, host_mode) : NULL;
    if (!f) sync_errno(ctx);
    free(path);
    return f;
}
static char *utf8_of(u64 wide) { return wide ? rcn_utf16_to_utf8(PTR(wide), -1) : NULL; }

C3(fopen) {
    RET(wrap_file(open_host(ctx, STR(A0), STR(A1))));
}
// fopen_s(&file, path, mode) : 0 ou errno.
C3(fopen_s) {
    u64 f = wrap_file(open_host(ctx, STR(A1), STR(A2)));
    if (A0) wr32(A0, (u32)f);
    RET_I32(f ? 0 : (THREAD(ctx)->crt_errno ? THREAD(ctx)->crt_errno : ENOENT));
}
C3(_wfopen) {
    char *path = utf8_of(A0), *mode = utf8_of(A1);
    RET(path && mode ? wrap_file(open_host(ctx, path, mode)) : 0);
    free(path), free(mode);
}
C3(_wfopen_s) {
    char *path = utf8_of(A1), *mode = utf8_of(A2);
    u64 f = path && mode ? wrap_file(open_host(ctx, path, mode)) : 0;
    free(path), free(mode);
    if (A0) wr32(A0, (u32)f);
    RET_I32(f ? 0 : (THREAD(ctx)->crt_errno ? THREAD(ctx)->crt_errno : ENOENT));
}
C3(fclose) {
    u32 id = file_id(A0);
    if (!id || (id & 0x80000000u)) {
        RET_I32(id ? 0 : -1);
        return;
    }
    pthread_mutex_lock(&files_lock);
    FILE *h = id - 1 < MAX_FILES ? host_files[id - 1] : NULL;
    if (h) host_files[id - 1] = NULL;
    pthread_mutex_unlock(&files_lock);
    rcn_low_free(PTR(A0));
    RET_I32(h ? fclose(h) : -1);
}
C3(fflush) {
    FILE *h = host_file(A0);
    RET_I32(h ? fflush(h) : 0);
}
// fread(buffer, size, count, file)
C3(fread) {
    FILE *h = host_file(A3);
    RET(h ? fread(PTR(A0), (size_t)A1, (size_t)A2, h) : 0);
}
C3(fwrite) {
    size_t size = (size_t)A1, count = (size_t)A2;
    RET(size && count ? write_file(ctx, A3, PTR(A0), size * count) / size : 0);
}
// fgets(buffer, size, file)
C3(fgets) {
    FILE *h = host_file(A2);
    RET((uintptr_t)(h ? fgets(PTR(A0), (int)A1, h) : NULL));
}
C3(fgetc) {
    FILE *h = host_file(A0);
    RET_I32(h ? fgetc(h) : EOF);
}
C3(fputc) {
    char c = (char)A0;
    RET_I32(write_file(ctx, A1, &c, 1) == 1 ? (u8)c : EOF);
}
C3(fputs) {
    const char *s = STR(A0);
    size_t n = strlen(s);
    RET_I32(write_file(ctx, A1, s, n) == n ? 0 : EOF);
}
C3(fseek) {
    FILE *h = host_file(A0);
    RET_I32(h ? fseek64(h, (i32)A1, (int)A2) : -1);
}
C3(ftell) {
    FILE *h = host_file(A0);
    RET_I32(h ? (i32)ftell64(h) : -1);
}
C3(_fseeki64) {
    FILE *h = host_file(A0);
    i64 off = (i64)((u64)A1 | (u64)A2 << 32);
    RET_I32(h ? fseek64(h, off, (int)A3) : -1);
}
C3(_ftelli64) {
    FILE *h = host_file(A0);
    ret_u64(ctx, h ? (u64)(i64)ftell64(h) : (u64)-1);
}
C3(rewind) {
    FILE *h = host_file(A0);
    if (h) rewind(h);
}
C3(feof) {
    FILE *h = host_file(A0);
    RET_I32(h ? feof(h) : std_stream(A0) == 1);
}
C3(ferror) {
    FILE *h = host_file(A0);
    RET_I32(h ? ferror(h) : 0);
}
C3(__iob_func) { RET((uintptr_t)crt32()->iob); }

// --- printf ---
static void out_file(RcnCtx *ctx, u64 f, const RcnBuf *b) { write_file(ctx, f, b->data, b->len); }
// Copie dans un tampon de taille limitée : nombre de caractères, ou -1 s'il est trop petit (_vsnprintf).
static i32 copy_limited(const RcnBuf *b, u64 dst, size_t size) {
    if (!dst || !size) return b->len ? -1 : 0;
    size_t n = b->len < size ? b->len : size;
    memcpy(PTR(dst), b->data, n);
    if (b->len < size) {
        ((char *)PTR(dst))[n] = 0;
        return (i32)b->len;
    }
    return -1;
}
C3(printf) {
    RcnBuf b = {0};
    rcn_format32(&b, STR(A0), ctx->r[4] + 8);
    rcn_host_write(ctx, RCN_STREAM_STDOUT, b.data, b.len);
    RET_I32((i32)b.len);
    free(b.data);
}
C3(fprintf) {
    RcnBuf b = {0};
    rcn_format32(&b, STR(A1), ctx->r[4] + 12);
    out_file(ctx, A0, &b);
    RET_I32((i32)b.len);
    free(b.data);
}
C3(vfprintf) {
    RcnBuf b = {0};
    rcn_format32(&b, STR(A1), A2);
    out_file(ctx, A0, &b);
    RET_I32((i32)b.len);
    free(b.data);
}
C3(sprintf) {
    RcnBuf b = {0};
    rcn_format32(&b, STR(A1), ctx->r[4] + 12);
    memcpy(PTR(A0), b.data ? b.data : "", b.len + 1);
    RET_I32((i32)b.len);
    free(b.data);
}
C3(vsprintf) {
    RcnBuf b = {0};
    rcn_format32(&b, STR(A1), A2);
    memcpy(PTR(A0), b.data ? b.data : "", b.len + 1);
    RET_I32((i32)b.len);
    free(b.data);
}
// _snprintf(buffer, count, format, …) : sans zéro final si le résultat remplit le tampon.
C3(_snprintf) {
    RcnBuf b = {0};
    rcn_format32(&b, STR(A2), ctx->r[4] + 16);
    size_t size = (size_t)A1, n = b.len < size ? b.len : size;
    if (A0 && n) memcpy(PTR(A0), b.data, n);
    if (A0 && b.len < size) ((char *)PTR(A0))[b.len] = 0;
    RET_I32(b.len <= size ? (i32)b.len : -1);
    free(b.data);
}
C3(_vsnprintf) {
    RcnBuf b = {0};
    rcn_format32(&b, STR(A2), A3);
    size_t size = (size_t)A1, n = b.len < size ? b.len : size;
    if (A0 && n) memcpy(PTR(A0), b.data, n);
    if (A0 && b.len < size) ((char *)PTR(A0))[b.len] = 0;
    RET_I32(b.len <= size ? (i32)b.len : -1);
    free(b.data);
}
// _vsnprintf_s(buffer, size, count, format, va) : count = _TRUNCATE (-1) : tronqué, -1 rendu.
C3(_vsnprintf_s) {
    RcnBuf b = {0};
    rcn_format32(&b, STR(A3), STK(4));
    size_t size = (size_t)A1, count = (u32)A2 == 0xFFFFFFFFu ? size - 1 : (size_t)A2;
    size_t limit = count < size - 1 ? count : size - 1;
    size_t n = b.len < limit ? b.len : limit;
    if (A0 && size) {
        memcpy(PTR(A0), b.data ? b.data : "", n);
        ((char *)PTR(A0))[n] = 0;
    }
    RET_I32(b.len <= limit ? (i32)b.len : -1);
    free(b.data);
}
C3(sprintf_s) {
    RcnBuf b = {0};
    rcn_format32(&b, STR(A2), ctx->r[4] + 16);
    RET_I32(copy_limited(&b, A0, (size_t)A1));
    free(b.data);
}
C3(vsprintf_s) {
    RcnBuf b = {0};
    rcn_format32(&b, STR(A2), A3);
    RET_I32(copy_limited(&b, A0, (size_t)A1));
    free(b.data);
}

// printf large (wchar_t de 2 octets) : le format est passé en UTF-8. Dans ces fonctions, « %s » et « %c »
// désignent des chaînes larges et « %S » / « %C » des chaînes étroites : convertis en %ls / %hs.
static char *wide_format(u64 wfmt) {
    char *f = rcn_utf16_to_utf8(PTR(wfmt), -1);
    if (!f) return NULL;
    size_t len = strlen(f);
    char *out = malloc(len * 2 + 1);
    if (!out) {
        free(f);
        return NULL;
    }
    char *o = out;
    for (const char *p = f; *p;) {
        if (*p != '%') {
            *o++ = *p++;
            continue;
        }
        *o++ = *p++;
        if (*p == '%') {
            *o++ = *p++;
            continue;
        }
        while (*p && strchr("-+ #0123456789.*", *p)) *o++ = *p++;
        int explicit_len = (*p == 'h' || *p == 'l' || *p == 'w');
        if (!explicit_len) {
            if (*p == 's' || *p == 'c') *o++ = 'l';
            else if (*p == 'S' || *p == 'C') {
                *o++ = 'h';
                *o++ = (char)(*p == 'S' ? 's' : 'c');
                p++;
                continue;
            }
        }
    }
    *o = 0;
    free(f);
    return out;
}
// Texte UTF-8 → UTF-16 dans un tampon de `count` caractères larges ; nombre de caractères, -1 si trop petit.
static i32 put_wide(const RcnBuf *b, u64 dst, size_t count, int truncate) {
    size_t need = b->len + 1;
    u16 *tmp = malloc(need * sizeof(u16));
    if (!tmp) return -1;
    size_t n = rcn_utf8_to_utf16(b->data ? b->data : "", b->len, tmp);
    i32 r = (i32)n;
    if (!dst || !count) {
        free(tmp);
        return -1;
    }
    if (n >= count) {
        if (!truncate) {
            wr16(dst, 0);
            free(tmp);
            return -1;
        }
        n = count - 1;
        r = -1;
    }
    memcpy(PTR(dst), tmp, n * 2);
    wr16(dst + n * 2, 0);
    free(tmp);
    return r;
}
static void wide_printf(RcnCtx *ctx, RcnBuf *b, u64 wfmt, u64 va) {
    char *f = wide_format(wfmt);
    if (f) rcn_format32(b, f, va);
    free(f);
}
// vswprintf_s(buffer, count, format, va)
C3(vswprintf_s) {
    RcnBuf b = {0};
    wide_printf(ctx, &b, A2, A3);
    RET_I32(put_wide(&b, A0, (size_t)A1, 0));
    free(b.data);
}
C3(swprintf_s) {
    RcnBuf b = {0};
    wide_printf(ctx, &b, A2, ctx->r[4] + 16);
    RET_I32(put_wide(&b, A0, (size_t)A1, 0));
    free(b.data);
}
// _vsnwprintf_s(buffer, size, count, format, va)
C3(_vsnwprintf_s) {
    RcnBuf b = {0};
    wide_printf(ctx, &b, A3, STK(4));
    size_t size = (size_t)A1, count = (u32)A2 == 0xFFFFFFFFu ? size : (size_t)A2 + 1;
    RET_I32(put_wide(&b, A0, count < size ? count : size, 1));
    free(b.data);
}
C3(_vsnwprintf) {
    RcnBuf b = {0};
    wide_printf(ctx, &b, A2, A3);
    RET_I32(put_wide(&b, A0, (size_t)A1 + 1, 1));
    free(b.data);
}
// _vswprintf_c_l(buffer, count, format, locale, va)
C3(_vswprintf_c_l) {
    RcnBuf b = {0};
    wide_printf(ctx, &b, A2, STK(4));
    RET_I32(put_wide(&b, A0, (size_t)A1, 0));
    free(b.data);
}
C3(wprintf) {
    RcnBuf b = {0};
    wide_printf(ctx, &b, A0, ctx->r[4] + 8);
    rcn_host_write(ctx, RCN_STREAM_STDOUT, b.data, b.len);
    RET_I32((i32)b.len);
    free(b.data);
}
C3(fwprintf) {
    RcnBuf b = {0};
    wide_printf(ctx, &b, A1, ctx->r[4] + 12);
    out_file(ctx, A0, &b);
    RET_I32((i32)b.len);
    free(b.data);
}

// swscanf_s(texte, format, …) : entiers (d i u x, h l ll I64), flottants (e f g, l), chaînes et caractères
// (s c, suivis de leur taille en caractères). Rend le nombre de champs lus.
static int scan_wide(RcnCtx *ctx, const u16 *in, const u16 *fmt, int first_arg, int secure) {
    int arg = first_arg, fields = 0;
    while (*fmt) {
        if (*fmt == ' ' || (*fmt >= 9 && *fmt <= 13)) {
            while (*in == ' ' || (*in >= 9 && *in <= 13)) in++;
            fmt++;
            continue;
        }
        if (*fmt != '%') {
            if (*in != *fmt) return fields;
            in++, fmt++;
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            if (*in != '%') return fields;
            in++, fmt++;
            continue;
        }
        int skip = 0, width = 0, size = 4;
        if (*fmt == '*') skip = 1, fmt++;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (fmt[0] == 'I' && fmt[1] == '6' && fmt[2] == '4') size = 8, fmt += 3;
        else if (fmt[0] == 'l' && fmt[1] == 'l') size = 8, fmt += 2;
        else if (*fmt == 'l') size = 5, fmt++; // l : long (4) ou double
        else if (*fmt == 'h') size = 2, fmt++;
        u16 conv = *fmt++;
        if (!conv) break;
        if (conv != 'c') while (*in == ' ' || (*in >= 9 && *in <= 13)) in++;
        if (!*in) return fields ? fields : -1;
        char buf[128];
        int n = 0, limit = width ? width : 127;
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X') {
            while (n < limit && *in && *in < 128 && (isxdigit((int)*in) || *in == '-' || *in == '+' || *in == 'x' || *in == 'X')) {
                if (conv != 'x' && conv != 'X' && conv != 'i' && !(isdigit((int)*in) || ((*in == '-' || *in == '+') && n == 0))) break;
                buf[n++] = (char)*in++;
            }
            buf[n] = 0;
            if (!n) return fields;
            long long v = strtoll(buf, NULL, conv == 'x' || conv == 'X' ? 16 : conv == 'i' ? 0 : 10);
            if (!skip) {
                u64 at = STK(arg++);
                if (size == 8) wr64(at, (u64)v);
                else if (size == 2) wr16(at, (u16)v);
                else wr32(at, (u32)v);
                fields++;
            }
        } else if (conv == 'f' || conv == 'e' || conv == 'g' || conv == 'E' || conv == 'G') {
            while (n < limit && *in && *in < 128 && strchr("0123456789+-.eE", (char)*in)) buf[n++] = (char)*in++;
            buf[n] = 0;
            if (!n) return fields;
            double v = strtod(buf, NULL);
            if (!skip) {
                u64 at = STK(arg++);
                if (size == 5 || size == 8) wrf64(at, v);
                else wrf32(at, (float)v);
                fields++;
            }
        } else if (conv == 's' || conv == 'c') {
            u64 at = skip ? 0 : STK(arg++);
            u32 cap = secure && !skip ? (u32)STK(arg++) : 0xFFFFFFFFu;
            int count = 0, max = conv == 'c' ? (width ? width : 1) : (width ? width : 0x7FFFFFFF);
            while (*in && count < max && (conv == 'c' || !(*in == ' ' || (*in >= 9 && *in <= 13)))) {
                if (at && (u32)count < cap) wr16(at + 2 * (u64)count, *in);
                count++, in++;
            }
            if (at && conv == 's') {
                if ((u32)count < cap) wr16(at + 2 * (u64)count, 0);
                else if (cap) wr16(at, 0);
            }
            if (!skip) fields++;
        } else {
            return fields;
        }
    }
    return fields;
}
C3(swscanf_s) { RET_I32(scan_wide(ctx, PTR(A0), PTR(A1), 2, 1)); }
C3(swscanf) { RET_I32(scan_wide(ctx, PTR(A0), PTR(A1), 2, 0)); }

// --- Démarrage et arrêt ---

C3(__set_app_type) { fill_command_line(ctx); }
C3(__p__fmode) {
    fill_command_line(ctx);
    RET((uintptr_t)&crt32()->fmode);
}
C3(__p__commode) { RET((uintptr_t)&crt32()->commode); }
C3(__p___initenv) { RET((uintptr_t)&crt32()->envp); }
C3(__setusermatherr) { (void)ctx; }
C3(_configthreadlocale) { RET(0); }
C3(_set_app_type) { (void)ctx; }
C3(_set_error_mode) { RET(0); }
C3(_set_abort_behavior) { RET(0); }
C3(_set_invalid_parameter_handler) { RET(0); }
C3(_set_purecall_handler) { RET(0); }
C3(_crt_debugger_hook) { (void)ctx; }
C3(_encode_pointer) { RET(A0); }
C3(_decode_pointer) { RET(A0); }
C3(_controlfp_s) {
    // Mot de contrôle courant : précision 53 bits, arrondi au plus près, exceptions masquées.
    if (A0) wr32(A0, 0x0009001F);
    RET(0);
}
C3(_controlfp) { RET(0x0009001F); }
C3(_fpreset) { ctx->mxcsr = 0x1f80; }
C3(_errno) {
    Crt32Data *d = crt32();
    d->errno_slot = (u32)THREAD(ctx)->crt_errno;
    RET((uintptr_t)&d->errno_slot);
}

// _initterm(first, last) : tables de pointeurs de 4 octets.
C3(_initterm) {
    fill_command_line(ctx);
    for (u64 p = A0, last = A1; p < last; p += 4)
        if (rd32(p)) rcn_call_guest(ctx, rd32(p), 0, 0, 0, 0);
}
C3(_initterm_e) {
    fill_command_line(ctx);
    for (u64 p = A0, last = A1; p < last; p += 4) {
        if (!rd32(p)) continue;
        u32 r = (u32)rcn_call_guest(ctx, rd32(p), 0, 0, 0, 0);
        if (r) {
            RET(r);
            return;
        }
    }
    RET(0);
}

// argv de __getmainargs : ligne de commande découpée (guillemets compris), tableaux dans la mémoire du jeu.
static void split_args(RcnCtx *ctx) {
    Crt32Data *d = crt32();
    if (d->argv) return;
    const char *line = rcn_command_line(ctx);
    u32 *argv = rcn_low_alloc(64 * 4, 1);
    char *copy = rcn_low_alloc(strlen(line) + 1, 0);
    if (!argv || !copy) return;
    strcpy(copy, line);
    u32 argc = 0;
    char *p = copy;
    while (*p && argc < 63) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char *start = p, *o = p;
        int quoted = 0;
        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') quoted = !quoted, p++;
            else *o++ = *p++;
        }
        if (*p) p++;
        *o = 0;
        argv[argc++] = (u32)(uintptr_t)start;
    }
    d->argc = argc;
    d->argv = (u32)(uintptr_t)argv;
}
// __getmainargs(&argc, &argv, &envp, wildcard, startinfo)
C3(__getmainargs) {
    split_args(ctx);
    Crt32Data *d = crt32();
    if (A0) wr32(A0, d->argc);
    if (A1) wr32(A1, d->argv);
    if (A2) wr32(A2, d->envp);
    RET(0);
}
C3(__p___argc) {
    split_args(ctx);
    RET((uintptr_t)&crt32()->argc);
}
C3(__p___argv) {
    split_args(ctx);
    RET((uintptr_t)&crt32()->argv);
}

// Fonctions de fin (atexit, _onexit) : liste du processus, appelées par exit dans l'ordre inverse.
static void add_atexit(RcnCtx *ctx, u64 fn) {
    RcnProcess *proc = ctx->proc;
    pthread_mutex_lock(&proc->lock);
    if (proc->atexit_count == proc->atexit_cap) {
        u32 cap = proc->atexit_cap ? proc->atexit_cap * 2 : 16;
        u64 *fns = realloc(proc->atexit_fns, cap * sizeof(u64));
        if (fns) proc->atexit_fns = fns, proc->atexit_cap = cap;
    }
    if (proc->atexit_count < proc->atexit_cap) proc->atexit_fns[proc->atexit_count++] = fn;
    pthread_mutex_unlock(&proc->lock);
}
static void run_atexit(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    for (;;) {
        pthread_mutex_lock(&proc->lock);
        u64 fn = proc->atexit_count ? proc->atexit_fns[--proc->atexit_count] : 0;
        pthread_mutex_unlock(&proc->lock);
        if (!fn) break;
        rcn_call_guest(ctx, fn, 0, 0, 0, 0);
    }
}
C3(_onexit) {
    add_atexit(ctx, A0);
    RET(A0);
}
C3(atexit) {
    add_atexit(ctx, A0);
    RET(0);
}
// __dllonexit(fn, &begin, &end) : table propre à une DLL (pointeurs de 4 octets), agrandie au besoin.
C3(__dllonexit) {
    u64 pbegin = A1, pend = A2;
    if (!pbegin || !pend) {
        RET(0);
        return;
    }
    u32 begin = rd32(pbegin), end = rd32(pend);
    u32 count = begin ? (end - begin) / 4 : 0;
    u8 *grown = rcn_low_alloc((count + 1) * 4, 0);
    if (!grown) {
        RET(0);
        return;
    }
    if (count) memcpy(grown, PTR(begin), count * 4);
    wr32((u64)(uintptr_t)grown + count * 4, (u32)A0);
    wr32(pbegin, (u32)(uintptr_t)grown);
    wr32(pend, (u32)(uintptr_t)grown + (count + 1) * 4);
    RET(A0);
}
C3(exit) {
    run_atexit(ctx);
    rcn_exit(ctx, (u32)A0);
}
C3(_exit) { rcn_exit(ctx, (u32)A0); }
C3(_cexit) { run_atexit(ctx); }
C3(_c_exit) { (void)ctx; }
C3(abort) { rcn_fail(ctx, "le programme s'est interrompu (abort)"); }
C3(_amsg_exit) { rcn_fail(ctx, "erreur d'execution de la bibliotheque C (_amsg_exit %u)", (u32)A0); }
C3(_purecall) { rcn_fail(ctx, "appel d'une fonction virtuelle pure (_purecall)"); }
C3(_invalid_parameter_noinfo) { rcn_fail(ctx, "parametre invalide refuse par la bibliotheque C"); }
C3(_invoke_watson) { rcn_fail(ctx, "parametre invalide refuse par la bibliotheque C (_invoke_watson)"); }
C3(terminate) { rcn_fail(ctx, "std::terminate appele"); }
C3(type_info_dtor) { (void)ctx; }
// Verrous internes de la bibliothèque (_lock(n) / _unlock(n)) : un verrou récursif chacun.
static pthread_mutex_t crt_locks[64];
static void init_locks(void) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    for (int i = 0; i < 64; i++) pthread_mutex_init(&crt_locks[i], &a);
}
static pthread_once_t locks_once = PTHREAD_ONCE_INIT;
C3(_lock) {
    pthread_once(&locks_once, init_locks);
    pthread_mutex_lock(&crt_locks[(u32)A0 & 63]);
}
C3(_unlock) {
    pthread_once(&locks_once, init_locks);
    pthread_mutex_unlock(&crt_locks[(u32)A0 & 63]);
}

// --- Exceptions C++ des jeux 32 bits (SEH de x86 : chaîne de cadres en fs:[0]) ---
//
// Chaque fonction qui a des try ou des objets à détruire enregistre un nœud { suivant, gestionnaire, état }
// à ebp - 0x0C, et garde son esp à ebp - 0x10. Le gestionnaire est un relais du jeu (« mov eax, FuncInfo ;
// jmp __CxxFrameHandler3 ») : la fonction de la bibliothèque C, ici, reçoit FuncInfo dans eax.
// Lancer (_CxxThrowException) : la chaîne est parcourue, chaque gestionnaire appelé comme sous Windows. Le
// nôtre cherche un catch du type lancé pour l'état du cadre ; trouvé : remontée globale (destructeurs des
// cadres plus profonds), remontée locale jusqu'au try, copie de l'objet attrapé, appel du bloc catch (ebp
// du cadre), puis reprise à l'adresse qu'il rend, avec esp et ebp du cadre (reprise non locale du code
// traduit : ctx->unwind = RCN_UNWIND_RESUME).
// SEH (__try / __except / __finally, gestionnaires _except_handler3/4) : voir plus bas. Pas encore :
// exceptions matérielles (accès mémoire invalide…) rattrapées par __except ou catch(...).
enum {
    EH_CODE = 0xE06D7363u,      // « msc »
    EH_MAGIC = 0x19930520u,     // paramètre 0 de l'exception (et FuncInfo, 3 versions : …20 à …22)
    EH_NONCONTINUABLE = 1,
    EH_UNWINDING = 2,
    EH_EXIT_UNWIND = 4,
    DISP_CONTINUE_SEARCH = 1,
    DISP_RESUME = 0x52434E00u,  // nôtre : catch trouvé, reprise notée dans eh_resume
};
static _Thread_local struct {
    int pending;
    u32 cont, esp, ebp;
} eh_resume;
static _Thread_local u32 eh_cur_obj, eh_cur_info; // exception du bloc catch en cours (relance « throw; »)
// EXCEPTION_RECORD (80 octets, suivis des EXCEPTION_POINTERS { record, contexte } : EH_RECORD_SIZE) et
// CONTEXT, sous 2 Go.
enum { EH_RECORD_SIZE = 88 };
static _Thread_local u32 eh_record, eh_context;
static _Thread_local u32 eh_seh_record; // record de l'exception de RaiseException en cours

static u32 *eh_list(RcnCtx *ctx) { return (u32 *)THREAD(ctx)->teb; }

// Bloc du jeu appelé avec l'ebp de son cadre (bloc catch, destructeur d'une remontée) ; registres gardés
// rétablis ensuite. Rend eax.
static u32 eh_call_funclet(RcnCtx *ctx, u32 fn, u32 ebp) {
    u64 ebx = ctx->r[3], saved_ebp = ctx->r[5], esi = ctx->r[6], edi = ctx->r[7];
    ctx->r[5] = ebp;
    u32 r = (u32)rcn_call_guest(ctx, fn, 0, 0, 0, 0);
    // Exception lancée dans le bloc et rattrapée plus haut : la reprise a fixé ses registres (ebp du cadre
    // qui la rattrape) — surtout ne pas les écraser.
    if (!ctx->unwind) ctx->r[3] = ebx, ctx->r[5] = saved_ebp, ctx->r[6] = esi, ctx->r[7] = edi;
    return r;
}
// Méthode d'un objet (thiscall : this dans ecx).
static void eh_call_this(RcnCtx *ctx, u32 fn, u32 self, int nargs, u32 a0, u32 a1) {
    u64 ecx = ctx->r[1];
    ctx->r[1] = self;
    rcn_call_guest(ctx, fn, nargs > 0 ? a0 : 0, nargs > 1 ? a1 : 0, 0, 0);
    ctx->r[1] = ecx;
}

// Remontée locale d'un cadre jusqu'à l'état visé : destructeurs de la table de remontée (UnwindMapEntry
// { état suivant, action }).
static void eh_unwind_to(RcnCtx *ctx, u32 node, u32 funcinfo, i32 target) {
    u32 max = rd32(funcinfo + 4), map = rd32(funcinfo + 8);
    i32 state = (i32)rd32(node + 8);
    for (int guard = 0; state != target && state >= 0 && (u32)state < max && guard < 100000; guard++) {
        u32 entry = map + 8 * (u32)state;
        i32 next = (i32)rd32(entry);
        u32 action = rd32(entry + 4);
        wr32(node + 8, (u32)next);
        if (action) eh_call_funclet(ctx, action, node + 12);
        state = next;
    }
    wr32(node + 8, (u32)target);
}

// Remontée globale jusqu'au cadre visé (exclu) : gestionnaire de chaque cadre plus profond appelé avec
// EXCEPTION_UNWINDING, puis retiré de la chaîne (RtlUnwind).
static void eh_global_unwind(RcnCtx *ctx, u32 record, u32 target) {
    u32 flags = rd32(record + 4);
    wr32(record + 4, flags | EH_UNWINDING);
    for (u32 n = *eh_list(ctx); n && n != 0xFFFFFFFFu && n != target; n = *eh_list(ctx)) {
        rcn_call_guest(ctx, rd32(n + 4), record, n, eh_context, 0);
        *eh_list(ctx) = rd32(n);
    }
    wr32(record + 4, flags);
}

// Type attrapé par un catch (HandlerType { adjectifs, type, décalage de l'objet, bloc }) : CatchableType
// du type lancé qui lui correspond, 1 pour catch(...), 0 sinon.
static u32 eh_match(u32 handler, u32 info) {
    u32 type = rd32(handler + 4);
    if (!type || !rd8(type + 8)) return 1;
    if (!info || !rd32(info + 12)) return 0;
    u32 array = rd32(info + 12), n = rd32(array);
    for (u32 i = 0; i < n && i < 64; i++) {
        u32 ct = rd32(array + 4 + 4 * i), td = ct ? rd32(ct + 4) : 0;
        if (td && (td == type || !strcmp(STR(td + 8), STR(type + 8)))) return ct;
    }
    return 0;
}
// Adresse de la partie attrapée d'un objet (PMD { mdisp, pdisp, vdisp }).
static u32 eh_adjust(u32 obj, u32 ct) {
    i32 mdisp = (i32)rd32(ct + 8), pdisp = (i32)rd32(ct + 12), vdisp = (i32)rd32(ct + 16);
    u32 p = obj;
    if (pdisp >= 0) p += (u32)pdisp + rd32(rd32(obj + (u32)pdisp) + (u32)vdisp);
    return p + (u32)mdisp;
}
// Objet attrapé copié dans le cadre du catch (ebp + décalage) : référence, type simple, ou constructeur
// de copie.
static void eh_copy(RcnCtx *ctx, u32 handler, u32 ct, u32 obj, u32 ebp) {
    i32 disp = (i32)rd32(handler + 8);
    if (!disp || ct <= 1 || !obj) return;
    u32 dst = ebp + (u32)disp, adjectives = rd32(handler), props = rd32(ct), size = rd32(ct + 20), copy = rd32(ct + 24);
    if (adjectives & 8) { // HT_IsReference
        wr32(dst, eh_adjust(obj, ct));
    } else if (props & 1) { // CT_IsSimpleType (pointeurs compris)
        memcpy(PTR(dst), PTR(obj), size);
        if (size == 4 && rd32(dst)) wr32(dst, eh_adjust(rd32(dst), ct));
    } else if (!copy) {
        memcpy(PTR(dst), PTR(eh_adjust(obj, ct)), size);
    } else {
        eh_call_this(ctx, copy, dst, props & 4 ? 2 : 1, eh_adjust(obj, ct), 1);
    }
}

// __CxxFrameHandler3(record, nœud, contexte, dispatcher) — FuncInfo dans eax.
C3(__CxxFrameHandler3) {
    u32 record = (u32)A0, node = (u32)A1, funcinfo = (u32)ctx->r[0];
    u32 magic = funcinfo ? rd32(funcinfo) & 0x1FFFFFFFu : 0;
    if (magic < EH_MAGIC || magic > EH_MAGIC + 2) {
        RET(DISP_CONTINUE_SEARCH);
        return;
    }
    u32 flags = rd32(record + 4);
    if (flags & (EH_UNWINDING | EH_EXIT_UNWIND)) {
        if (rd32(funcinfo + 4) && (i32)rd32(node + 8) != -1) eh_unwind_to(ctx, node, funcinfo, -1);
        RET(DISP_CONTINUE_SEARCH);
        return;
    }
    if (rd32(record) != EH_CODE || rd32(record + 16) < 3 || (rd32(record + 20) & 0x1FFFFFFFu) < EH_MAGIC) {
        RET(DISP_CONTINUE_SEARCH);
        return;
    }
    u32 obj = rd32(record + 24), info = rd32(record + 28);
    i32 state = (i32)rd32(node + 8);
    u32 ntry = rd32(funcinfo + 12), map = rd32(funcinfo + 16);
    for (u32 t = 0; t < ntry; t++) {
        u32 e = map + 20 * t;
        i32 low = (i32)rd32(e), high = (i32)rd32(e + 4);
        if (state < low || state > high) continue;
        u32 ncatch = rd32(e + 12), handlers = rd32(e + 16);
        for (u32 c = 0; c < ncatch; c++) {
            u32 h = handlers + 16 * c, ct = eh_match(h, info);
            if (!ct) continue;
            eh_global_unwind(ctx, record, node);
            eh_unwind_to(ctx, node, funcinfo, low);
            eh_copy(ctx, h, ct, obj, node + 12);
            wr32(node + 8, (u32)(high + 1));
            u32 prev_obj = eh_cur_obj, prev_info = eh_cur_info;
            eh_cur_obj = obj, eh_cur_info = info;
            u32 cont = eh_call_funclet(ctx, rd32(h + 12), node + 12);
            eh_cur_obj = prev_obj, eh_cur_info = prev_info;
            // Exception relancée (ou nouvelle) depuis le bloc catch, rattrapée plus haut : sa reprise est
            // déjà décidée (ctx->unwind), elle l'emporte.
            if (ctx->unwind) {
                RET(DISP_CONTINUE_SEARCH);
                return;
            }
            // Objet lancé détruit (ThrowInfo.pmfnUnwind) après le bloc catch.
            if (info && rd32(info + 4) && obj) eh_call_this(ctx, rd32(info + 4), obj, 0, 0, 0);
            eh_resume.pending = 1;
            eh_resume.cont = cont;
            eh_resume.esp = rd32(node - 4);
            eh_resume.ebp = node + 12;
            if (getenv("RCN_EH_LOG"))
                rcn_log("catch : cadre %08x (FuncInfo %08x), etat %d dans [%d, %d], bloc %08x, reprise %08x esp %08x ebp %08x",
                        node, funcinfo, state, low, high, rd32(h + 12), cont, eh_resume.esp, eh_resume.ebp);
            RET(DISP_RESUME);
            return;
        }
    }
    RET(DISP_CONTINUE_SEARCH);
}

// SEH de Visual C++ : le cadre d'une fonction qui a des __try enregistre un nœud { suivant, gestionnaire,
// table des portées, niveau } à ebp - 0x10 ; ebp - 0x14 reçoit les EXCEPTION_POINTERS lus par le filtre
// (GetExceptionInformation), ebp - 0x18 garde esp. Portée (12 octets) : { niveau englobant, filtre, bloc } ;
// sans filtre, le bloc est un __finally. _except_handler3 : table telle quelle, niveau « aucun » -1 ;
// _except_handler4 : table chiffrée par le cookie de sécurité, ses portées après 16 octets de cookies,
// niveau « aucun » -2.
// Filtre et __finally sont appelés avec l'ebp du cadre ; un filtre qui prend l'exception (> 0) : remontée
// globale, __finally des niveaux intérieurs, reprise au bloc __except (esp et ebp du cadre).

// __finally des niveaux du cadre jusqu'au niveau visé (exclu).
static void seh_local_unwind(RcnCtx *ctx, u32 node, u32 scope, i32 target, i32 none) {
    for (int guard = 0; guard < 4096; guard++) {
        i32 level = (i32)rd32(node + 12);
        if (level == target || level == none || level < 0) break;
        u32 e = scope + 12 * (u32)level;
        wr32(node + 12, rd32(e));
        if (!rd32(e + 4) && rd32(e + 8)) eh_call_funclet(ctx, rd32(e + 8), node + 16);
        if (ctx->unwind) break;
    }
}
static u32 seh_frame(RcnCtx *ctx, u32 record, u32 context, u32 node, u32 scope, i32 none) {
    u32 ebp = node + 16;
    if (rd32(record + 4) & (EH_UNWINDING | EH_EXIT_UNWIND)) {
        seh_local_unwind(ctx, node, scope, none, none);
        return DISP_CONTINUE_SEARCH;
    }
    // EXCEPTION_POINTERS : juste après le record de nos exceptions ; pour un record d'ailleurs, après le nôtre.
    u32 pointers = record == eh_record || (eh_seh_record && record == eh_seh_record) ? record + 80 : eh_record + 80;
    wr32(pointers, record);
    wr32(pointers + 4, context);
    wr32(node - 4, pointers);
    i32 level = (i32)rd32(node + 12);
    for (int guard = 0; level != none && level >= 0 && guard < 4096; guard++) {
        u32 e = scope + 12 * (u32)level;
        i32 enclosing = (i32)rd32(e);
        u32 filter = rd32(e + 4), handler = rd32(e + 8);
        if (filter) {
            i32 r = (i32)eh_call_funclet(ctx, filter, ebp);
            if (ctx->unwind) return DISP_CONTINUE_SEARCH; // exception lancée dans le filtre, reprise ailleurs
            if (r < 0) return 0;                            // EXCEPTION_CONTINUE_EXECUTION
            if (r > 0) {
                eh_global_unwind(ctx, record, node);
                seh_local_unwind(ctx, node, scope, level, none);
                if (ctx->unwind) return DISP_CONTINUE_SEARCH;
                wr32(node + 12, (u32)enclosing);
                eh_resume.pending = 1;
                eh_resume.cont = handler;
                eh_resume.esp = rd32(node - 8);
                eh_resume.ebp = ebp;
                if (getenv("RCN_EH_LOG"))
                    rcn_log("__except : exception %08x, cadre %08x, niveau %d, bloc %08x", rd32(record), node, level, handler);
                return DISP_RESUME;
            }
        }
        level = enclosing;
    }
    return DISP_CONTINUE_SEARCH;
}
// _except_handler3(record, nœud, contexte, dispatcher)
C3(_except_handler3) { RET(seh_frame(ctx, (u32)A0, (u32)A2, (u32)A1, rd32((u32)A1 + 8), -1)); }
// _except_handler4_common(&cookie, vérification du cookie, record, nœud, contexte, dispatcher)
C3(_except_handler4_common) {
    u32 node = (u32)A3, scope = rd32(node + 8) ^ rd32((u32)A0);
    RET(seh_frame(ctx, (u32)A2, (u32)STK(4), node, scope + 16, -2));
}

// Parcours de la chaîne des cadres pour une exception. Rend 1 : reprise décidée (catch, __except, ou
// exception imbriquée rattrapée : ctx->unwind) ; 2 : un gestionnaire a demandé de continuer l'exécution ;
// 0 : personne ne l'a prise.
static int eh_dispatch(RcnCtx *ctx, u32 record) {
    memset(PTR(eh_context), 0, 0x2CC);
    wr32(eh_context, 0x10007);
    wr32(eh_context + 0xB4, (u32)ctx->r[5]);
    wr32(eh_context + 0xC4, (u32)ctx->r[4]);
    for (u32 n = *eh_list(ctx); n && n != 0xFFFFFFFFu; n = rd32(n)) {
        eh_resume.pending = 0;
        u32 disp = (u32)rcn_call_guest(ctx, rd32(n + 4), record, n, eh_context, 0);
        // Reprise décidée par une exception imbriquée (lancée dans un bloc catch ou un destructeur) : gardée.
        if (ctx->unwind && !eh_resume.pending) return 1;
        if (eh_resume.pending) {
            eh_resume.pending = 0;
            ctx->r[5] = eh_resume.ebp;
            ctx->resume_rsp = eh_resume.esp;
            ctx->resume_rip = eh_resume.cont;
            ctx->unwind = RCN_UNWIND_RESUME;
            return 1;
        }
        if (disp == 0 && !(rd32(record + 4) & EH_NONCONTINUABLE)) return 2; // ExceptionContinueExecution
    }
    return 0;
}

static void eh_buffers(void) {
    if (!eh_record) eh_record = (u32)(uintptr_t)rcn_low_alloc(EH_RECORD_SIZE, 1);
    if (!eh_context) eh_context = (u32)(uintptr_t)rcn_low_alloc(0x2CC, 1);
}

// RaiseException d'un jeu 32 bits : l'exception parcourt la chaîne SEH. Rend 1 : reprise décidée ;
// 2 : l'exécution continue après RaiseException ; 0 : personne ne l'a prise.
int rcn_eh32_raise(RcnCtx *ctx, u32 code, u32 flags, u32 count, u32 args) {
    eh_buffers();
    u32 r = (u32)(uintptr_t)rcn_low_alloc(EH_RECORD_SIZE, 1);
    if (!r) return 0;
    if (count > 15) count = 15;
    wr32(r, code);
    wr32(r + 4, flags & EH_NONCONTINUABLE);
    wr32(r + 16, count);
    for (u32 i = 0; i < count && args; i++) wr32(r + 20 + 4 * i, rd32(args + 4 * i));
    u32 outer = eh_seh_record;
    eh_seh_record = r;
    int result = eh_dispatch(ctx, r);
    eh_seh_record = outer;
    rcn_low_free(PTR(r));
    return result;
}

// _CxxThrowException(objet, ThrowInfo) ; (NULL, NULL) : relance de l'exception du catch en cours.
C3(_CxxThrowException) {
    u32 obj = (u32)A0, info = (u32)A1;
    if (!obj && !info) {
        obj = eh_cur_obj, info = eh_cur_info;
        if (!info) rcn_fail(ctx, "throw; hors d'un bloc catch (jeu 32 bits)");
    }
    const char *type = "?";
    if (info && rd32(info + 12) && rd32(rd32(info + 12))) {
        u32 ct = rd32(rd32(info + 12) + 4);
        if (ct && rd32(ct + 4)) type = STR(rd32(ct + 4) + 8);
    }
    // RCN_EH_LOG=1 : exceptions lancées, avec le texte des chaînes (messages d'Unreal Engine 3).
    static int log = -1;
    if (log < 0) log = getenv("RCN_EH_LOG") != NULL;
    if (log) {
        char *text = NULL;
        if (obj && rd32(obj) && (!strcmp(type, ".PA_W") || !strcmp(type, ".PB_W"))) text = rcn_utf16_to_utf8(PTR(rd32(obj)), -1);
        rcn_log("exception C++ %s%s%s", type, text ? " : " : "", text ? text : "");
        free(text);
        // Qui l'a lancée : l'adresse de retour dans le jeu (validation d'un paquet, VM de script…).
        rcn_guest_stack_once(ctx, "exception C++");
    }
    eh_buffers();
    u32 r = eh_record;
    memset(PTR(r), 0, 80);
    wr32(r, EH_CODE);
    wr32(r + 4, EH_NONCONTINUABLE);
    wr32(r + 16, 3);
    wr32(r + 20, EH_MAGIC);
    wr32(r + 24, obj);
    wr32(r + 28, info);
    if (eh_dispatch(ctx, r)) return;
    rcn_guest_stack_once(ctx, type);
    rcn_fail(ctx, "exception C++ non rattrapee dans un jeu 32 bits (%s)", type);
}

// RtlUnwind(cadre visé, adresse, record, valeur) des jeux 32 bits : remontée globale jusqu'au cadre (les
// copies de la bibliothèque C liées dans les DLL du jeu l'appellent depuis leur __CxxFrameHandler).
void rcn_eh32_RtlUnwind(RcnCtx *ctx) {
    eh_buffers();
    u32 target = (u32)A0, record = (u32)A2;
    if (!record) {
        record = eh_record;
        memset(PTR(record), 0, 80);
        wr32(record, 0xC0000027u); // STATUS_UNWIND
    }
    eh_global_unwind(ctx, record, target ? target : 0xFFFFFFFFu);
    RET(A3);
}

// _XcptFilter : filtre du démarrage — laisse passer l'exception (EXCEPTION_CONTINUE_SEARCH).
C3(_XcptFilter) { RET(0); }
C3(longjmp) {
    if (ctx->proc->module->native) rcn_longjmp_native(ctx, A0, (int)A1);
    else rcn_longjmp(ctx, A0, (int)A1);
}
C3(_setjmp3) { rcn_fail(ctx, "_setjmp3 appele par un pointeur de fonction"); }

// --- Tas ---
C3(malloc) { RET((uintptr_t)rcn_low_alloc((size_t)A0 ? (size_t)A0 : 1, 0)); }
C3(calloc) {
    u64 n = (u64)(u32)A0 * (u32)A1;
    RET(n > 0x7FFFFFFF ? 0 : (uintptr_t)rcn_low_alloc(n ? (size_t)n : 1, 1));
}
C3(realloc) { RET((uintptr_t)rcn_low_realloc(PTR(A0), (size_t)A1 ? (size_t)A1 : 1)); }
C3(free) {
    if (A0) rcn_low_free(PTR(A0));
}
C3(_aligned_malloc) {
    u32 align = (u32)A1 < 8 ? 8 : (u32)A1;
    u64 raw = (u64)(uintptr_t)rcn_low_alloc((size_t)A0 + align + 4, 0);
    if (!raw) {
        RET(0);
        return;
    }
    u64 p = (raw + 4 + align - 1) & ~(u64)(align - 1);
    wr32(p - 4, (u32)raw);
    RET(p);
}
C3(_aligned_free) {
    if (A0) rcn_low_free(PTR(rd32(A0 - 4)));
}
// _aligned_realloc(bloc, taille, alignement) : nouveau bloc, contenu recopié (taille du bloc d'origine).
C3(_aligned_realloc) {
    u64 old = A0;
    size_t size = (size_t)A1;
    if (!old) {
        c32__aligned_malloc(ctx);
        return;
    }
    if (!size) {
        rcn_low_free(PTR(rd32(old - 4)));
        RET(0);
        return;
    }
    u64 raw_old = rd32(old - 4);
    size_t had = rcn_low_size(PTR(raw_old)) - (size_t)(old - raw_old);
    u32 align = (u32)A2 < 8 ? 8 : (u32)A2;
    u64 raw = (u64)(uintptr_t)rcn_low_alloc(size + align + 4, 0);
    if (!raw) {
        RET(0);
        return;
    }
    u64 p = (raw + 4 + align - 1) & ~(u64)(align - 1);
    wr32(p - 4, (u32)raw);
    memcpy(PTR(p), PTR(old), had < size ? had : size);
    rcn_low_free(PTR(raw_old));
    RET(p);
}
// Filtre des exceptions C++ de la bibliothèque (__CppXcptFilter) : EXCEPTION_CONTINUE_SEARCH.
C3(__CppXcptFilter) { RET(0); }
C3(__clean_type_info_names_internal) { (void)ctx; }
// _encoded_null : EncodePointer(NULL), sans codage ici.
C3(_encoded_null) { RET(0); }
// type_info::_name_internal_method(&liste) (thiscall : this dans ecx) : nom décoré, rangé après la table
// virtuelle et le champ _M_data.
static void c32_type_info_name(RcnCtx *ctx) { RET(ctx->r[1] + 9); }
// strncat_s(dest, taille, src, nombre) : 0, ou ERANGE (34) si la destination est trop petite.
C3(strncat_s) {
    char *d = PTR(A0);
    const char *s = STR(A2);
    size_t size = (size_t)(u32)A1, count = (size_t)(u32)A3, n = strnlen(d, size);
    if (!d || n == size) {
        RET(22);
        return;
    }
    size_t add = strlen(s);
    if (count != (size_t)0xFFFFFFFFu && add > count) add = count;
    if (n + add >= size) {
        d[0] = 0;
        RET(34);
        return;
    }
    memcpy(d + n, s, add);
    d[n + add] = 0;
    RET(0);
}

// --- Chaînes et mémoire ---
C3(memcpy) { RET((uintptr_t)memcpy(PTR(A0), PTR(A1), (size_t)A2)); }
C3(memmove) { RET((uintptr_t)memmove(PTR(A0), PTR(A1), (size_t)A2)); }
C3(memset) { RET((uintptr_t)memset(PTR(A0), (int)A1, (size_t)A2)); }
C3(memcmp) { RET_I32(memcmp(PTR(A0), PTR(A1), (size_t)A2)); }
C3(memchr) { RET((uintptr_t)memchr(PTR(A0), (int)A1, (size_t)A2)); }
// memmove_s(dest, size, src, count) : 0, ou ERANGE si la destination est trop petite.
C3(memmove_s) {
    if ((u32)A3 > (u32)A1) {
        RET_I32(ERANGE);
        return;
    }
    memmove(PTR(A0), PTR(A2), (size_t)A3);
    RET(0);
}
C3(memcpy_s) {
    if ((u32)A3 > (u32)A1) {
        RET_I32(ERANGE);
        return;
    }
    memcpy(PTR(A0), PTR(A2), (size_t)A3);
    RET(0);
}
C3(strlen) { RET(strlen(STR(A0))); }
C3(strnlen) { RET(strnlen(STR(A0), (size_t)A1)); }
C3(strcmp) {
    int r = strcmp(STR(A0), STR(A1));
    RET_I32((r > 0) - (r < 0));
}
C3(strncmp) { RET_I32(strncmp(STR(A0), STR(A1), (size_t)A2)); }
C3(_stricmp) { RET_I32(strcasecmp(STR(A0), STR(A1))); }
C3(_strnicmp) { RET_I32(strncasecmp(STR(A0), STR(A1), (size_t)A2)); }
C3(strcpy) { RET((uintptr_t)strcpy(PTR(A0), STR(A1))); }
C3(strncpy) { RET((uintptr_t)strncpy(PTR(A0), STR(A1), (size_t)A2)); }
C3(strcat) { RET((uintptr_t)strcat(PTR(A0), STR(A1))); }
C3(strncat) { RET((uintptr_t)strncat(PTR(A0), STR(A1), (size_t)A2)); }
C3(strchr) { RET((uintptr_t)strchr(STR(A0), (int)A1)); }
C3(strrchr) { RET((uintptr_t)strrchr(STR(A0), (int)A1)); }
C3(strstr) { RET((uintptr_t)strstr(STR(A0), STR(A1))); }
C3(strcspn) { RET(strcspn(STR(A0), STR(A1))); }
C3(strspn) { RET(strspn(STR(A0), STR(A1))); }
C3(strpbrk) { RET((uintptr_t)strpbrk(STR(A0), STR(A1))); }
C3(_strdup) {
    size_t n = strlen(STR(A0)) + 1;
    char *copy = rcn_low_alloc(n, 0);
    if (copy) memcpy(copy, STR(A0), n);
    RET((uintptr_t)copy);
}
C3(_strlwr) {
    for (char *p = PTR(A0); *p; p++) *p = (char)tolower((u8)*p);
    RET(A0);
}
C3(_strupr) {
    for (char *p = PTR(A0); *p; p++) *p = (char)toupper((u8)*p);
    RET(A0);
}
// strcpy_s(dest, size, src) / strcat_s / strncpy_s(dest, size, src, count) : 0, ou ERANGE (destination vidée).
C3(strcpy_s) {
    size_t n = strlen(STR(A2));
    if (!A0 || n >= (u32)A1) {
        if (A0 && A1) wr8(A0, 0);
        RET_I32(ERANGE);
        return;
    }
    memcpy(PTR(A0), STR(A2), n + 1);
    RET(0);
}
C3(strcat_s) {
    size_t have = strnlen(STR(A0), (size_t)A1), n = strlen(STR(A2));
    if (have + n >= (u32)A1) {
        if (A1) wr8(A0, 0);
        RET_I32(ERANGE);
        return;
    }
    memcpy((char *)PTR(A0) + have, STR(A2), n + 1);
    RET(0);
}
C3(strncpy_s) {
    size_t size = (u32)A1, count = (u32)A3, n = strnlen(STR(A2), count == 0xFFFFFFFFu ? size : count);
    if (n >= size) {
        if (count == 0xFFFFFFFFu && size) { // _TRUNCATE
            memcpy(PTR(A0), STR(A2), size - 1);
            wr8(A0 + size - 1, 0);
            RET_I32(80); // STRUNCATE
            return;
        }
        if (size) wr8(A0, 0);
        RET_I32(ERANGE);
        return;
    }
    memcpy(PTR(A0), STR(A2), n);
    wr8(A0 + n, 0);
    RET(0);
}
C3(_itoa_s) {
    char buf[40];
    u32 v = (u32)A0, radix = (u32)A3;
    int neg = radix == 10 && (i32)v < 0;
    u32 u = neg ? 0u - v : v;
    int n = 0;
    do {
        u32 d = u % radix;
        buf[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        u /= radix;
    } while (u && n < 34);
    if (neg) buf[n++] = '-';
    if ((u32)n + 1 > (u32)A2) {
        if (A2) wr8(A1, 0);
        RET_I32(ERANGE);
        return;
    }
    for (int i = 0; i < n; i++) wr8(A1 + i, (u8)buf[n - 1 - i]);
    wr8(A1 + n, 0);
    RET(0);
}
C3(_itoa) {
    char tmp[16];
    snprintf(tmp, sizeof tmp, (u32)A2 == 16 ? "%x" : "%d", (i32)A0);
    strcpy(PTR(A1), tmp);
    RET(A1);
}

#define CTYPE(name) \
    C3(name) { RET_I32((u32)A0 < 128 ? name((int)A0) : 0); }
CTYPE(isalpha)
CTYPE(isdigit)
CTYPE(isalnum)
CTYPE(isspace)
CTYPE(isupper)
CTYPE(islower)
CTYPE(isxdigit)
CTYPE(ispunct)
CTYPE(isprint)
CTYPE(iscntrl)
CTYPE(isgraph)
C3(tolower) { RET_I32((u32)A0 < 128 ? tolower((int)A0) : (int)A0); }
C3(toupper) { RET_I32((u32)A0 < 128 ? toupper((int)A0) : (int)A0); }
C3(_ismbblead) { RET(0); }
C3(localeconv) { RET((uintptr_t)crt32()->lconv); }

// --- Conversions ---
C3(atoi) { RET_I32(atoi(STR(A0))); }
C3(atol) { RET_I32((i32)atol(STR(A0))); }
C3(_atoi64) { ret_u64(ctx, (u64)strtoll(STR(A0), NULL, 10)); }
C3(atof) { ret_f64(ctx, strtod(STR(A0), NULL)); }
static void store_end(u64 endp, u64 start, const char *s, const char *end) {
    if (endp) wr32(endp, (u32)(start + (u64)(end - s)));
}
C3(strtod) {
    const char *s = STR(A0);
    char *end;
    double v = strtod(s, &end);
    store_end(A1, A0, s, end);
    ret_f64(ctx, v);
}
// strtol / strtoul : « long » de 32 bits, saturé comme sous Windows.
C3(strtol) {
    const char *s = STR(A0);
    char *end;
    long long v = strtoll(s, &end, (int)A2);
    store_end(A1, A0, s, end);
    if (v > 0x7FFFFFFFLL) v = 0x7FFFFFFFLL;
    if (v < -0x80000000LL) v = -0x80000000LL;
    RET_I32((i32)v);
}
C3(strtoul) {
    const char *s = STR(A0);
    char *end;
    unsigned long long v = strtoull(s, &end, (int)A2);
    store_end(A1, A0, s, end);
    RET(v > 0xFFFFFFFFull && s[strspn(s, " \t")] != '-' ? 0xFFFFFFFFu : (u32)v);
}
C3(_strtoi64) {
    const char *s = STR(A0);
    char *end;
    long long v = strtoll(s, &end, (int)A2);
    store_end(A1, A0, s, end);
    ret_u64(ctx, (u64)v);
}
C3(_strtoui64) {
    const char *s = STR(A0);
    char *end;
    unsigned long long v = strtoull(s, &end, (int)A2);
    store_end(A1, A0, s, end);
    ret_u64(ctx, v);
}
C3(abs) { RET_I32((i32)A0 < 0 ? 0 - (u32)A0 : (u32)A0); }
C3(labs) { RET_I32((i32)A0 < 0 ? 0 - (u32)A0 : (u32)A0); }
static u32 rand_state = 1;
C3(srand) { rand_state = (u32)A0; }
C3(rand) {
    rand_state = rand_state * 214013u + 2531011u; // générateur de la bibliothèque de Visual C++
    RET((rand_state >> 16) & 0x7FFF);
}

// --- Chaînes larges (wchar_t de 2 octets) ---
static size_t wlen(const u16 *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
static u16 wlower(u16 c) { return c < 128 ? (u16)tolower(c) : (c >= 0xC0 && c <= 0xDE && c != 0xD7 ? (u16)(c + 32) : c); }
C3(wcslen) { RET(wlen(PTR(A0))); }
C3(wcsnlen) {
    const u16 *s = PTR(A0);
    size_t n = 0, max = (u32)A1;
    while (n < max && s[n]) n++;
    RET(n);
}
C3(wcscmp) {
    const u16 *a = PTR(A0), *b = PTR(A1);
    while (*a && *a == *b) a++, b++;
    RET_I32((*a > *b) - (*a < *b));
}
C3(wcsncmp) {
    const u16 *a = PTR(A0), *b = PTR(A1);
    size_t n = (u32)A2;
    while (n && *a && *a == *b) a++, b++, n--;
    RET_I32(n ? (*a > *b) - (*a < *b) : 0);
}
static int wicmp(const u16 *a, const u16 *b, size_t n) {
    for (; n; a++, b++, n--) {
        u16 x = wlower(*a), y = wlower(*b);
        if (x != y || !x) return (x > y) - (x < y);
    }
    return 0;
}
C3(_wcsicmp) { RET_I32(wicmp(PTR(A0), PTR(A1), (size_t)-1)); }
C3(_wcsnicmp) { RET_I32(wicmp(PTR(A0), PTR(A1), (u32)A2)); }
C3(wcscoll) {
    const u16 *a = PTR(A0), *b = PTR(A1);
    while (*a && *a == *b) a++, b++;
    RET_I32((*a > *b) - (*a < *b));
}
C3(_wcsicoll) { RET_I32(wicmp(PTR(A0), PTR(A1), (size_t)-1)); }
C3(wcschr) {
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
C3(wcsrchr) {
    const u16 *s = PTR(A0), *found = NULL;
    u16 c = (u16)A1;
    for (;; s++) {
        if (*s == c) found = s;
        if (!*s) break;
    }
    RET((uintptr_t)found);
}
C3(wcsstr) {
    const u16 *h = PTR(A0), *n = PTR(A1);
    size_t nl = wlen(n);
    if (!nl) {
        RET(A0);
        return;
    }
    for (; *h; h++)
        if (!memcmp(h, n, nl * 2)) {
            RET((uintptr_t)h);
            return;
        }
    RET(0);
}
C3(wcscpy) {
    memcpy(PTR(A0), PTR(A1), (wlen(PTR(A1)) + 1) * 2);
    RET(A0);
}
C3(wcscat) {
    u16 *d = PTR(A0);
    memcpy(d + wlen(d), PTR(A1), (wlen(PTR(A1)) + 1) * 2);
    RET(A0);
}
C3(wcsncpy) {
    u16 *d = PTR(A0);
    const u16 *s = PTR(A1);
    size_t n = (u32)A2, i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    RET(A0);
}
// wcscpy_s(dest, count, src), wcscat_s, wcsncpy_s(dest, count, src, n), wcsncat_s : comme leurs versions étroites.
C3(wcscpy_s) {
    size_t n = wlen(PTR(A2));
    if (!A0 || n >= (u32)A1) {
        if (A0 && A1) wr16(A0, 0);
        RET_I32(ERANGE);
        return;
    }
    memcpy(PTR(A0), PTR(A2), (n + 1) * 2);
    RET(0);
}
C3(wcscat_s) {
    u16 *d = PTR(A0);
    size_t cap = (u32)A1, have = 0, n = wlen(PTR(A2));
    while (have < cap && d[have]) have++;
    if (have + n >= cap) {
        if (cap) d[0] = 0;
        RET_I32(ERANGE);
        return;
    }
    memcpy(d + have, PTR(A2), (n + 1) * 2);
    RET(0);
}
static int wcopy_n(u64 dst, size_t cap, const u16 *src, size_t limit, size_t at) {
    size_t n = 0;
    while (n < limit && src[n]) n++;
    int truncate = limit == 0xFFFFFFFFu;
    if (at + n >= cap) {
        if (!truncate || !cap) {
            if (cap) wr16(dst, 0);
            return ERANGE;
        }
        n = cap - 1 - at;
        memcpy((u16 *)PTR(dst) + at, src, n * 2);
        wr16(dst + 2 * (at + n), 0);
        return 80; // STRUNCATE
    }
    memcpy((u16 *)PTR(dst) + at, src, n * 2);
    wr16(dst + 2 * (at + n), 0);
    return 0;
}
C3(wcsncpy_s) { RET_I32(wcopy_n(A0, (u32)A1, PTR(A2), (u32)A3, 0)); }
C3(wcsncat_s) {
    const u16 *d = PTR(A0);
    size_t cap = (u32)A1, have = 0;
    while (have < cap && d[have]) have++;
    RET_I32(have >= cap ? ERANGE : wcopy_n(A0, cap, PTR(A2), (u32)A3, have));
}
C3(_wcsdup) {
    size_t n = (wlen(PTR(A0)) + 1) * 2;
    u16 *copy = rcn_low_alloc(n, 0);
    if (copy) memcpy(copy, PTR(A0), n);
    RET((uintptr_t)copy);
}
C3(_wcslwr) {
    for (u16 *p = PTR(A0); *p; p++) *p = wlower(*p);
    RET(A0);
}
C3(_wcsupr) {
    for (u16 *p = PTR(A0); *p; p++)
        if (*p < 128) *p = (u16)toupper(*p);
    RET(A0);
}
// Nombre écrit en caractères larges → texte ASCII (les chiffres le sont toujours).
static char *narrow(const u16 *s, const u16 **end_out, char *buf, size_t cap) {
    size_t n = 0;
    while (s[n] && s[n] < 128 && n + 1 < cap) {
        buf[n] = (char)s[n];
        n++;
    }
    buf[n] = 0;
    if (end_out) *end_out = s;
    return buf;
}
C3(_wtoi) {
    char buf[64];
    RET_I32(atoi(narrow(PTR(A0), NULL, buf, sizeof buf)));
}
C3(_wtol) {
    char buf[64];
    RET_I32((i32)atol(narrow(PTR(A0), NULL, buf, sizeof buf)));
}
C3(_wtoi64) {
    char buf[64];
    ret_u64(ctx, (u64)strtoll(narrow(PTR(A0), NULL, buf, sizeof buf), NULL, 10));
}
C3(_wtof) {
    char buf[128];
    ret_f64(ctx, strtod(narrow(PTR(A0), NULL, buf, sizeof buf), NULL));
}
C3(wcstod) {
    char buf[128];
    char *end;
    double v = strtod(narrow(PTR(A0), NULL, buf, sizeof buf), &end);
    if (A1) wr32(A1, (u32)(A0 + 2 * (u64)(end - buf)));
    ret_f64(ctx, v);
}
C3(wcstol) {
    char buf[128];
    char *end;
    long long v = strtoll(narrow(PTR(A0), NULL, buf, sizeof buf), &end, (int)A2);
    if (A1) wr32(A1, (u32)(A0 + 2 * (u64)(end - buf)));
    if (v > 0x7FFFFFFFLL) v = 0x7FFFFFFFLL;
    if (v < -0x80000000LL) v = -0x80000000LL;
    RET_I32((i32)v);
}
C3(wcstoul) {
    char buf[128];
    char *end;
    unsigned long long v = strtoull(narrow(PTR(A0), NULL, buf, sizeof buf), &end, (int)A2);
    if (A1) wr32(A1, (u32)(A0 + 2 * (u64)(end - buf)));
    RET(v > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)v);
}
C3(wctob) { RET_I32((u32)A0 < 128 ? (i32)A0 : -1); }
C3(btowc) { RET((u8)A0 < 128 ? (u32)(u8)A0 : 0xFFFF); }
C3(iswspace) { RET_I32((u16)A0 == ' ' || ((u16)A0 >= 9 && (u16)A0 <= 13) || (u16)A0 == 0xA0 || (u16)A0 == 0x3000); }
C3(iswpunct) { RET_I32((u16)A0 < 128 ? ispunct((int)(u16)A0) != 0 : 0); }
C3(iswdigit) { RET_I32((u16)A0 >= '0' && (u16)A0 <= '9'); }
C3(iswalpha) { RET_I32((u16)A0 < 128 ? isalpha((int)(u16)A0) != 0 : (u16)A0 >= 0xC0); }
C3(iswalnum) { RET_I32((u16)A0 < 128 ? isalnum((int)(u16)A0) != 0 : (u16)A0 >= 0xC0); }
C3(iswupper) { RET_I32((u16)A0 < 128 ? isupper((int)(u16)A0) != 0 : 0); }
C3(iswlower) { RET_I32((u16)A0 < 128 ? islower((int)(u16)A0) != 0 : 0); }
C3(iswxdigit) { RET_I32((u16)A0 < 128 ? isxdigit((int)(u16)A0) != 0 : 0); }
C3(towlower) { RET(wlower((u16)A0)); }
C3(towupper) { RET((u16)A0 < 128 ? (u32)toupper((int)(u16)A0) : (u32)(u16)A0); }

// --- Fichiers par nom ---
C3(_wmkdir) {
    char *p = utf8_of(A0);
    char *host = p ? rcn_host_path(ctx, p) : NULL;
    int r = host ? mkdir_host(host) : -1;
    if (r) sync_errno(ctx);
    free(p), free(host);
    RET_I32(r ? -1 : 0);
}
C3(_mkdir) {
    char *host = rcn_host_path(ctx, STR(A0));
    int r = host ? mkdir_host(host) : -1;
    if (r) sync_errno(ctx);
    free(host);
    RET_I32(r ? -1 : 0);
}
C3(_wutime64) { RET(0); }
C3(_utime64) { RET(0); }
// _wstat64 / _wstat64i32 (chemin large) et _stat64 : disposition de Windows 32 bits.
static int stat_path(RcnCtx *ctx, const char *windows_path, struct stat *st) {
    char *host = rcn_host_path(ctx, windows_path);
    int r = host ? stat(host, st) : -1;
    if (r) sync_errno(ctx);
    free(host);
    return r;
}
static void put_stat(u64 out, const struct stat *st, int size32) {
    memset(PTR(out), 0, size32 ? 48 : 56);
    u16 mode = S_ISDIR(st->st_mode) ? 0x4000 | 0x1FF : 0x8000 | 0x1B6;
    wr16(out + 6, mode);
    wr16(out + 8, 1);
    if (size32) {
        wr32(out + 20, (u32)st->st_size);
        wr64(out + 24, (u64)st->st_atime), wr64(out + 32, (u64)st->st_mtime), wr64(out + 40, (u64)st->st_ctime);
    } else {
        wr64(out + 24, (u64)st->st_size);
        wr64(out + 32, (u64)st->st_atime), wr64(out + 40, (u64)st->st_mtime), wr64(out + 48, (u64)st->st_ctime);
    }
}
C3(_wstat64) {
    char *p = utf8_of(A0);
    struct stat st;
    int r = p ? stat_path(ctx, p, &st) : -1;
    free(p);
    if (!r && A1) put_stat(A1, &st, 0);
    RET_I32(r ? -1 : 0);
}
C3(_wstat64i32) {
    char *p = utf8_of(A0);
    struct stat st;
    int r = p ? stat_path(ctx, p, &st) : -1;
    free(p);
    if (!r && A1) put_stat(A1, &st, 1);
    RET_I32(r ? -1 : 0);
}
C3(_stat64) {
    struct stat st;
    int r = stat_path(ctx, STR(A0), &st);
    if (!r && A1) put_stat(A1, &st, 0);
    RET_I32(r ? -1 : 0);
}
C3(_stat64i32) {
    struct stat st;
    int r = stat_path(ctx, STR(A0), &st);
    if (!r && A1) put_stat(A1, &st, 1);
    RET_I32(r ? -1 : 0);
}
C3(remove) {
    char *host = rcn_host_path(ctx, STR(A0));
    int r = host ? remove(host) : -1;
    free(host);
    RET_I32(r);
}
C3(_wremove) {
    char *p = utf8_of(A0), *host = p ? rcn_host_path(ctx, p) : NULL;
    int r = host ? remove(host) : -1;
    free(p), free(host);
    RET_I32(r);
}

// --- Temps ---
static void put_tm(u64 out, const struct tm *t) {
    i32 v[9] = {t->tm_sec, t->tm_min, t->tm_hour, t->tm_mday, t->tm_mon, t->tm_year, t->tm_wday, t->tm_yday, t->tm_isdst};
    memcpy(PTR(out), v, sizeof v);
}
C3(_time64) {
    u64 t = (u64)time(NULL);
    if (A0) wr64(A0, t);
    ret_u64(ctx, t);
}
C3(_time32) {
    u32 t = (u32)time(NULL);
    if (A0) wr32(A0, t);
    RET(t);
}
C3(time) {
    u64 t = (u64)time(NULL);
    if (A0) wr64(A0, t);
    ret_u64(ctx, t);
}
// _ftime64(&timeb) : { __time64_t time ; unsigned short millitm ; short timezone ; short dstflag }.
C3(_ftime64) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    u64 out = A0;
    wr64(out, (u64)ts.tv_sec);
    wr16(out + 8, (u16)(ts.tv_nsec / 1000000));
    wr16(out + 10, 0);
    wr16(out + 12, 0);
}
C3(_localtime64) {
    time_t t = (time_t)rd64(A0);
    struct tm tm;
    localtime_r(&t, &tm);
    put_tm((u64)(uintptr_t)crt32()->tm, &tm);
    RET((uintptr_t)crt32()->tm);
}
C3(_gmtime64) {
    time_t t = (time_t)rd64(A0);
    struct tm tm;
    gmtime_r(&t, &tm);
    put_tm((u64)(uintptr_t)crt32()->tm, &tm);
    RET((uintptr_t)crt32()->tm);
}
// _gmtime64_s(&tm, &time) / _localtime64_s : 0.
C3(_gmtime64_s) {
    time_t t = (time_t)rd64(A1);
    struct tm tm;
    gmtime_r(&t, &tm);
    put_tm(A0, &tm);
    RET(0);
}
C3(_localtime64_s) {
    time_t t = (time_t)rd64(A1);
    struct tm tm;
    localtime_r(&t, &tm);
    put_tm(A0, &tm);
    RET(0);
}
C3(clock) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    RET_I32((i32)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000));
}
// _wstrdate_s(buffer, count) « MM/JJ/AA » ; _wstrtime_s « HH:MM:SS ».
static void put_wide_ascii(u64 dst, const char *s) {
    for (size_t i = 0;; i++) {
        wr16(dst + 2 * i, (u8)s[i]);
        if (!s[i]) break;
    }
}
C3(_wstrdate_s) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char s[16];
    snprintf(s, sizeof s, "%02d/%02d/%02d", tm.tm_mon + 1, tm.tm_mday, tm.tm_year % 100);
    if ((u32)A1 < 9) {
        RET_I32(ERANGE);
        return;
    }
    put_wide_ascii(A0, s);
    RET(0);
}
C3(_wstrtime_s) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char s[16];
    snprintf(s, sizeof s, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    if ((u32)A1 < 9) {
        RET_I32(ERANGE);
        return;
    }
    put_wide_ascii(A0, s);
    RET(0);
}

// --- Mathématiques ---
// Fonctions à argument double sur la pile, résultat dans ST(0).
#define MATH1(name) \
    C3(name) { ret_f64(ctx, name(arg_f64(ctx, 0))); }
#define MATH2(name) \
    C3(name) { ret_f64(ctx, name(arg_f64(ctx, 0), arg_f64(ctx, 2))); }
MATH1(ceil)
MATH1(floor)
MATH1(sqrt)
MATH1(sin)
MATH1(cos)
MATH1(tan)
MATH1(asin)
MATH1(acos)
MATH1(atan)
MATH1(exp)
MATH1(log)
MATH1(log10)
MATH1(fabs)
MATH1(sinh)
MATH1(cosh)
MATH1(tanh)
MATH2(pow)
MATH2(atan2)
MATH2(fmod)
C3(ldexp) { ret_f64(ctx, ldexp(arg_f64(ctx, 0), (int)STK(2))); }
C3(modf) {
    double ip;
    double r = modf(arg_f64(ctx, 0), &ip);
    if (STK(2)) wrf64(STK(2), ip);
    ret_f64(ctx, r);
}
// frexp(x, &exposant)
C3(frexp) {
    int e;
    double r = frexp(arg_f64(ctx, 0), &e);
    if (STK(2)) wr32(STK(2), (u32)e);
    ret_f64(ctx, r);
}
C3(_finite) { RET_I32(isfinite(arg_f64(ctx, 0)) ? 1 : 0); }
C3(_isnan) { RET_I32(isnan(arg_f64(ctx, 0)) ? 1 : 0); }
// _fpclass : classes de Windows (_FPCLASS_SNAN 1 … _FPCLASS_PINF 0x200).
C3(_fpclass) {
    double v = arg_f64(ctx, 0);
    int neg = signbit(v) != 0;
    u32 r;
    switch (fpclassify(v)) {
    case FP_NAN: r = 2; break;
    case FP_INFINITE: r = neg ? 4 : 0x200; break;
    case FP_ZERO: r = neg ? 0x20 : 0x40; break;
    case FP_SUBNORMAL: r = neg ? 0x10 : 0x80; break;
    default: r = neg ? 8 : 0x100; break;
    }
    RET(r);
}
// _CIxxx : arguments et résultat sur la pile x87 (ST(0), et ST(1) pour les fonctions à deux arguments :
// ST(1) premier opérande, ST(0) second ; tous deux retirés).
#define CI1(name, fn) \
    C3(name) { ST(0) = fn(ST(0)); }
#define CI2(name, fn)                     \
    C3(name) {                            \
        double b = ST(0), a = ST(1);      \
        rcn_fpop(ctx);                    \
        ST(0) = fn(a, b);                 \
    }
CI1(_CIsin, sin)
CI1(_CIcos, cos)
CI1(_CItan, tan)
CI1(_CIasin, asin)
CI1(_CIacos, acos)
CI1(_CIatan, atan)
CI1(_CIexp, exp)
CI1(_CIlog, log)
CI1(_CIlog10, log10)
CI1(_CIsqrt, sqrt)
CI1(_CIsinh, sinh)
CI1(_CIcosh, cosh)
CI1(_CItanh, tanh)
CI2(_CIpow, pow)
CI2(_CIatan2, atan2)
CI2(_CIfmod, fmod)
// _ftol / _ftol2 / _ftol2_sse : ST(0) tronqué en entier de 64 bits dans edx:eax, retiré de la pile.
C3(_ftol) {
    double v = ST(0);
    rcn_fpop(ctx);
    ret_u64(ctx, (u64)rcn_fistt64(ctx, v));
}

// --- Tri ---
static _Thread_local RcnCtx *cmp_ctx;
static _Thread_local u64 cmp_fn;
static int cmp_guest(const void *a, const void *b) {
    return (i32)rcn_call_guest(cmp_ctx, cmp_fn, (u64)(uintptr_t)a, (u64)(uintptr_t)b, 0, 0);
}
C3(qsort) {
    RcnCtx *saved_ctx = cmp_ctx;
    u64 saved_fn = cmp_fn;
    cmp_ctx = ctx, cmp_fn = A3;
    qsort(PTR(A0), (size_t)A1, (size_t)A2, cmp_guest);
    cmp_ctx = saved_ctx, cmp_fn = saved_fn;
}
C3(bsearch) {
    RcnCtx *saved_ctx = cmp_ctx;
    u64 saved_fn = cmp_fn;
    cmp_ctx = ctx, cmp_fn = STK(4);
    void *r = bsearch(PTR(A0), PTR(A1), (size_t)A2, (size_t)A3, cmp_guest);
    cmp_ctx = saved_ctx, cmp_fn = saved_fn;
    RET((uintptr_t)r);
}
C3(getenv) { RET(0); }
C3(_wgetenv) { RET(0); }
C3(signal) { RET(0); }

// --- Threads ---
// _beginthreadex(security, stack_size, start, argument, flags, &thread_id) : comme CreateThread.
C3(_beginthreadex) { RET(rcn_create_thread(ctx, A2, A3, A1, ((u32)STK(4) & 4) != 0, STK(5))); }
C3(_endthreadex) { rcn_thread_exit(ctx, (u32)A0); }

// --- Entrées et sorties de bas niveau (msvcrt.dll : dbghelp…) : descripteurs de l'hôte ; 0, 1 et 2 sont
// l'entrée et les sorties du jeu. ---
#if !defined(_WIN32)
#include <fcntl.h>
#endif
// _wsopen(chemin, drapeaux, partage, droits) : drapeaux de Windows (_O_WRONLY 1, _O_RDWR 2, _O_APPEND 8,
// _O_CREAT 0x100, _O_TRUNC 0x200, _O_EXCL 0x400).
static int open_low(RcnCtx *ctx, const char *windows_path, u32 flags) {
#if defined(_WIN32)
    (void)ctx, (void)windows_path, (void)flags;
    return -1;
#else
    int host = (flags & 3) == 1 ? O_WRONLY : (flags & 3) == 2 ? O_RDWR : O_RDONLY;
    if (flags & 8) host |= O_APPEND;
    if (flags & 0x100) host |= O_CREAT;
    if (flags & 0x200) host |= O_TRUNC;
    if (flags & 0x400) host |= O_EXCL;
    char *path = rcn_host_path(ctx, windows_path);
    int fd = path ? open(path, host, 0666) : -1;
    if (fd < 0) sync_errno(ctx);
    free(path);
    return fd;
#endif
}
C3(_wsopen) {
    char *path = utf8_of(A0);
    RET_I32(path ? open_low(ctx, path, (u32)A1) : -1);
    free(path);
}
C3(_read) {
#if defined(_WIN32)
    RET_I32(-1);
#else
    int fd = (int)A0;
    RET_I32(fd > 2 ? (int)read(fd, PTR(A1), (size_t)(u32)A2) : 0);
#endif
}
C3(_write) {
    int fd = (int)A0;
    if (fd == 1 || fd == 2) {
        rcn_host_write(ctx, fd == 2 ? RCN_STREAM_STDERR : RCN_STREAM_STDOUT, PTR(A1), (size_t)(u32)A2);
        RET_I32((i32)A2);
        return;
    }
#if defined(_WIN32)
    RET_I32(-1);
#else
    RET_I32(fd > 2 ? (int)write(fd, PTR(A1), (size_t)(u32)A2) : -1);
#endif
}
C3(_close) {
#if defined(_WIN32)
    RET_I32(-1);
#else
    RET_I32((int)A0 > 2 ? close((int)A0) : 0);
#endif
}
// _lseeki64(fd, décalage de 64 bits, origine) : position de 64 bits dans edx:eax.
C3(_lseeki64) {
#if defined(_WIN32)
    ret_u64(ctx, (u64)-1);
#else
    i64 off = (i64)((u64)A1 | (u64)A2 << 32);
    ret_u64(ctx, (int)A0 > 2 ? (u64)(i64)lseek((int)A0, (off_t)off, (int)A3) : (u64)-1);
#endif
}
C3(_chsize) {
#if defined(_WIN32)
    RET_I32(-1);
#else
    RET_I32((int)A0 > 2 ? ftruncate((int)A0, (off_t)(i32)A1) : -1);
#endif
}
C3(_isatty) { RET(0); }
C3(_fileno) {
    int std = std_stream(A0);
    FILE *h = host_file(A0);
#if defined(_WIN32)
    RET_I32(std ? std - 1 : -1);
    (void)h;
#else
    RET_I32(std ? std - 1 : h ? fileno(h) : -1);
#endif
}
// Pas de handle de Windows derrière nos descripteurs.
C3(_get_osfhandle) { RET_I32(-1); }
C3(_open_osfhandle) { RET_I32(-1); }
// _wfsopen(chemin, mode, partage)
C3(_wfsopen) { c32__wfopen(ctx); }
// _wfullpath(tampon, chemin, taille) : chemin rendu tel quel (le jeu donne des chemins complets).
C3(_wfullpath) {
    u16 *path = PTR(A1);
    size_t n = 0;
    while (path && path[n]) n++;
    u64 out = A0;
    if (!out) out = (u64)(uintptr_t)rcn_low_alloc((n + 1) * 2, 0);
    else if ((u32)A2 <= n) out = 0;
    if (out) memcpy(PTR(out), path, n * 2), wr16(out + 2 * n, 0);
    RET(out);
}
C3(_memicmp) {
    const u8 *a = PTR(A0), *b = PTR(A1);
    for (size_t i = 0; i < (size_t)(u32)A2; i++) {
        int d = tolower(a[i]) - tolower(b[i]);
        if (d) {
            RET_I32(d);
            return;
        }
    }
    RET(0);
}
C3(_mbscmp) { RET_I32(strcmp(STR(A0), STR(A1))); }
C3(isleadbyte) { RET(0); }
C3(iswprint) { RET_I32((u16)A0 >= 0x20 && (u16)A0 != 0x7F); }
// wctomb(&octet, caractère) : 1 octet (« ? » hors de l'ASCII).
C3(wctomb) {
    if (!A0) {
        RET(0);
        return;
    }
    wr8(A0, (u16)A1 < 128 ? (u8)A1 : '?');
    RET(1);
}
// mbtowc(&caractère, texte, n)
C3(mbtowc) {
    if (!A1) {
        RET(0);
        return;
    }
    if (!(u32)A2) {
        RET_I32(-1);
        return;
    }
    u8 c = rd8(A1);
    if (A0) wr16(A0, c);
    RET(c != 0);
}
// ctime(&t) / _wctime(&t) (msvcrt.dll : time_t de 32 bits).
static void ctime_text(u64 t_ptr, char *out) {
    time_t t = (time_t)(i32)rd32(t_ptr);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, 32, "%a %b %d %H:%M:%S %Y\n", &tm);
}
C3(ctime) {
    ctime_text(A0, crt32()->ctime_text);
    RET((uintptr_t)crt32()->ctime_text);
}
C3(_wctime) {
    char text[32];
    ctime_text(A0, text);
    u16 *w = crt32()->wctime_text;
    for (int i = 0; i < 32; i++) w[i] = (u8)text[i];
    RET((uintptr_t)w);
}
// __unDName : pas de noms décorés lisibles (dbghelp garde le nom brut).
C3(__unDName) { RET(0); }

#define F(name) {#name, c32_##name, C32}
const RcnShim rcn_crt32[] = {
    // Démarrage, arrêt, erreurs
    F(__set_app_type), F(__p__fmode), F(__p__commode), F(__p___initenv), F(__setusermatherr), F(_configthreadlocale),
    F(_set_app_type), F(_set_error_mode), F(_set_abort_behavior), F(_set_invalid_parameter_handler),
    F(_set_purecall_handler), F(_crt_debugger_hook), F(_encode_pointer), F(_decode_pointer), F(_controlfp_s),
    F(_controlfp), F(_fpreset), F(_errno), F(_initterm), F(_initterm_e), F(__getmainargs), F(__p___argc), F(__p___argv),
    F(_onexit), F(atexit), F(__dllonexit), F(exit), F(_exit), F(_cexit), F(_c_exit), F(abort), F(_amsg_exit),
    F(_purecall), F(_invalid_parameter_noinfo), F(_invoke_watson), F(_lock), F(_unlock),
    F(_except_handler3), F(_except_handler4_common), F(__CxxFrameHandler3), F(_CxxThrowException), F(_XcptFilter),
    F(longjmp), F(_setjmp3), F(signal),
    {"?terminate@@YAXXZ", c32_terminate, C32},
    {"?_type_info_dtor_internal_method@type_info@@QAEXXZ", c32_type_info_dtor, C32},
    // Tas
    F(malloc), F(calloc), F(realloc), F(free), F(_aligned_malloc), F(_aligned_free),
    // Chaînes, mémoire, caractères
    F(memcpy), F(memmove), F(memset), F(memcmp), F(memchr), F(memmove_s), F(memcpy_s), F(strlen), F(strnlen), F(strcmp),
    F(strncmp), F(_stricmp), F(_strnicmp), F(strcpy), F(strncpy), F(strcat), F(strncat), F(strchr), F(strrchr),
    F(strstr), F(strcspn), F(strspn), F(strpbrk), F(_strdup), F(_strlwr), F(_strupr), F(strcpy_s), F(strcat_s),
    F(strncpy_s), F(_itoa_s), F(_itoa), F(isalpha), F(isdigit), F(isalnum), F(isspace), F(isupper), F(islower),
    F(isxdigit), F(ispunct), F(isprint), F(iscntrl), F(isgraph), F(tolower), F(toupper), F(_ismbblead), F(localeconv),
    // Conversions
    F(atoi), F(atol), F(_atoi64), F(atof), F(strtod), F(strtol), F(strtoul), F(_strtoi64), F(_strtoui64), F(abs),
    F(labs), F(srand), F(rand),
    // Chaînes larges
    F(wcslen), F(wcsnlen), F(wcscmp), F(wcsncmp), F(_wcsicmp), F(_wcsnicmp), F(wcscoll), F(_wcsicoll), F(wcschr),
    F(wcsrchr), F(wcsstr), F(wcscpy), F(wcscat), F(wcsncpy), F(wcscpy_s), F(wcscat_s), F(wcsncpy_s), F(wcsncat_s),
    F(_wcsdup), F(_wcslwr), F(_wcsupr), F(_wtoi), F(_wtol), F(_wtoi64), F(_wtof), F(wcstod), F(wcstol), F(wcstoul),
    F(wctob), F(btowc), F(iswspace), F(iswpunct), F(iswdigit), F(iswalpha), F(iswalnum), F(iswupper), F(iswlower),
    F(iswxdigit), F(towlower), F(towupper),
    // Fichiers
    F(fopen), F(fopen_s), F(_wfopen), F(_wfopen_s), F(fclose), F(fflush), F(fread), F(fwrite), F(fgets), F(fgetc),
    F(fputc), F(fputs), F(fseek), F(ftell), F(_fseeki64), F(_ftelli64), F(rewind), F(feof), F(ferror), F(__iob_func),
    F(_wmkdir), F(_mkdir), F(_wutime64), F(_utime64), F(_wstat64), F(_wstat64i32), F(_stat64), F(_stat64i32),
    F(remove), F(_wremove),
    // printf, scanf
    F(printf), F(fprintf), F(vfprintf), F(sprintf), F(vsprintf), F(_snprintf), F(_vsnprintf), F(_vsnprintf_s),
    F(sprintf_s), F(vsprintf_s), F(vswprintf_s), F(swprintf_s), F(_vsnwprintf_s), F(_vsnwprintf), F(_vswprintf_c_l),
    F(wprintf), F(fwprintf), F(swscanf_s), F(swscanf),
    // Temps
    F(_time64), F(_time32), F(time), F(_ftime64), F(_localtime64), F(_gmtime64), F(_gmtime64_s), F(_localtime64_s),
    F(clock), F(_wstrdate_s), F(_wstrtime_s),
    // Mathématiques
    F(ceil), F(floor), F(sqrt), F(sin), F(cos), F(tan), F(asin), F(acos), F(atan), F(exp), F(log), F(log10), F(fabs),
    F(sinh), F(cosh), F(tanh), F(pow), F(atan2), F(fmod), F(ldexp), F(modf), F(frexp), F(_finite), F(_isnan),
    F(_fpclass), F(_CIsin), F(_CIcos), F(_CItan), F(_CIasin), F(_CIacos), F(_CIatan), F(_CIexp), F(_CIlog), F(_CIlog10),
    F(_CIsqrt), F(_CIsinh), F(_CIcosh), F(_CItanh), F(_CIpow), F(_CIatan2), F(_CIfmod), F(_ftol),
    {"_ftol2", c32__ftol, C32}, {"_ftol2_sse", c32__ftol, C32},
    // Divers
    F(qsort), F(bsearch), F(getenv), F(_wgetenv), F(_beginthreadex), F(_endthreadex),
    // msvcrt.dll (dbghelp…)
    F(_wsopen), F(_read), F(_write), F(_close), F(_lseeki64), F(_chsize), F(_isatty), F(_fileno), F(_get_osfhandle),
    F(_open_osfhandle), F(_wfsopen), F(_wfullpath), F(_memicmp), F(_mbscmp), F(isleadbyte), F(iswprint), F(wctomb),
    F(mbtowc), F(ctime), F(_wctime), F(__unDName),
    {"_ltoa", c32__itoa, C32},
    {"??2@YAPAXI@Z", c32_malloc, C32},
    {"??3@YAXPAX@Z", c32_free, C32},
    {"??1type_info@@UAE@XZ", c32_type_info_dtor, C32},
    {"__CxxFrameHandler", c32___CxxFrameHandler3, C32},
    F(_aligned_realloc), F(__CppXcptFilter), F(__clean_type_info_names_internal), F(_encoded_null), F(strncat_s),
    {"_malloc_crt", c32_malloc, C32},
    {"??_U@YAPAXI@Z", c32_malloc, C32},
    {"??_V@YAXPAX@Z", c32_free, C32},
    {"?_name_internal_method@type_info@@QBEPBDPAU__type_info_node@@@Z", c32_type_info_name, S32(1)},
};
const size_t rcn_crt32_count = sizeof rcn_crt32 / sizeof rcn_crt32[0];
