// RomCloud Native : bibliothèque C de Windows (UCRT, msvcrt) fournie par l'hôte.
// La plupart des fonctions passent à la bibliothèque C de l'hôte : la mémoire du jeu est celle de
// l'hôte, ses pointeurs s'utilisent tels quels. Ce qui diffère de Windows est traité ici : tailles
// (« long » de 32 bits), fichiers (chemins, sortie standard vers l'application), printf (rcn_format.c),
// fonctions qui rappellent le jeu (qsort, atexit, _initterm).
//
// Limites connues : les fichiers sont toujours ouverts en mode binaire (pas de conversion des fins de
// ligne du mode texte de Windows) ; pas de paramètres régionaux ; l'environnement du jeu est vide.
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rcn_internal.h"

#if defined(_WIN32)
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#include <strings.h>
#include <unistd.h>
#define fseek64 fseeko
#define ftell64 ftello
#endif

#define CRT(name) static void crt_##name(RcnCtx *ctx)

// errno du jeu : mis à jour après une fonction de l'hôte qui peut échouer.
static void sync_errno(RcnCtx *ctx) { ctx->proc->crt_errno = errno; }

// --- Démarrage et arrêt ---

CRT(_set_app_type) { (void)ctx; }
CRT(__setusermatherr) { (void)ctx; }
CRT(_set_invalid_parameter_handler) { RET(0); }
CRT(_configthreadlocale) { RET(0); }
CRT(_set_new_mode) { RET(0); }
CRT(_seh_filter_exe) { RET(0); }
CRT(_configure_narrow_argv) { RET(0); }
CRT(_initialize_narrow_environment) { RET(0); }
CRT(__p___argc) { RET((uintptr_t)&ctx->proc->crt_argc); }
CRT(__p___argv) { RET((uintptr_t)&ctx->proc->crt_argv); }
CRT(__p__environ) { RET((uintptr_t)&ctx->proc->crt_environ); }
CRT(__p__fmode) { RET((uintptr_t)&ctx->proc->crt_fmode); }
CRT(__p__commode) { RET((uintptr_t)&ctx->proc->crt_commode); }
CRT(_errno) { RET((uintptr_t)&ctx->proc->crt_errno); }
CRT(getenv) { RET(0); }

// _initterm(first, last) : appelle chaque fonction d'initialisation du jeu (constructeurs globaux).
CRT(_initterm) {
    u64 last = A1;
    for (u64 p = A0; p < last; p += 8) {
        if (rd64(p)) rcn_call_guest(ctx, rd64(p), 0, 0, 0, 0);
    }
}
// _initterm_e : s'arrête à la première qui renvoie une erreur.
CRT(_initterm_e) {
    u64 last = A1;
    for (u64 p = A0; p < last; p += 8) {
        if (!rd64(p)) continue;
        u32 r = (u32)rcn_call_guest(ctx, rd64(p), 0, 0, 0, 0);
        if (r) {
            RET(r);
            return;
        }
    }
    RET(0);
}

// atexit : fonctions du jeu appelées à sa fin, dans l'ordre inverse de leur enregistrement.
CRT(_crt_atexit) {
    RcnProcess *proc = ctx->proc;
    if (proc->atexit_count == proc->atexit_cap) {
        u32 cap = proc->atexit_cap ? proc->atexit_cap * 2 : 16;
        u64 *fns = realloc(proc->atexit_fns, cap * sizeof(u64));
        if (!fns) {
            RET(-1);
            return;
        }
        proc->atexit_fns = fns;
        proc->atexit_cap = cap;
    }
    proc->atexit_fns[proc->atexit_count++] = A0;
    RET(0);
}
static void run_atexit(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    // Une fonction peut en enregistrer d'autres : le compteur est relu à chaque tour.
    while (proc->atexit_count) rcn_call_guest(ctx, proc->atexit_fns[--proc->atexit_count], 0, 0, 0, 0);
}
CRT(exit) {
    u32 code = (u32)A0;
    run_atexit(ctx);
    rcn_exit(ctx, code);
}
CRT(_exit) { rcn_exit(ctx, (u32)A0); }
CRT(_cexit) { run_atexit(ctx); }
CRT(_c_exit) { (void)ctx; }
CRT(abort) { rcn_fail(ctx, "le programme s'est interrompu (abort)"); }

// setjmp : le recompilateur traduit ses appels directs ; un appel par pointeur ne peut pas l'être.
CRT(setjmp) { rcn_fail(ctx, "setjmp appele par un pointeur de fonction"); }
CRT(longjmp) { rcn_longjmp(ctx, A0, (int)A1); }

// --- Tas ---

CRT(malloc) { RET((uintptr_t)malloc(A0 ? (size_t)A0 : 1)); }
CRT(calloc) { RET((uintptr_t)calloc(A0 ? (size_t)A0 : 1, A1 ? (size_t)A1 : 1)); }
CRT(realloc) { RET((uintptr_t)realloc(PTR(A0), A1 ? (size_t)A1 : 1)); }
CRT(free) { free(PTR(A0)); }

// --- Chaînes et mémoire ---

CRT(strlen) { RET(strlen(STR(A0))); }
CRT(strcmp) { RET_I32(strcmp(STR(A0), STR(A1))); }
CRT(strncmp) { RET_I32(strncmp(STR(A0), STR(A1), (size_t)A2)); }
CRT(_stricmp) { RET_I32(strcasecmp(STR(A0), STR(A1))); }
CRT(_strnicmp) { RET_I32(strncasecmp(STR(A0), STR(A1), (size_t)A2)); }
CRT(strcpy) { RET((uintptr_t)strcpy(PTR(A0), STR(A1))); }
CRT(strncpy) { RET((uintptr_t)strncpy(PTR(A0), STR(A1), (size_t)A2)); }
CRT(strcat) { RET((uintptr_t)strcat(PTR(A0), STR(A1))); }
CRT(strncat) { RET((uintptr_t)strncat(PTR(A0), STR(A1), (size_t)A2)); }
CRT(strchr) { RET((uintptr_t)strchr(STR(A0), (int)A1)); }
CRT(strrchr) { RET((uintptr_t)strrchr(STR(A0), (int)A1)); }
CRT(strstr) { RET((uintptr_t)strstr(STR(A0), STR(A1))); }
CRT(_strdup) {
    size_t n = strlen(STR(A0)) + 1;
    char *copy = malloc(n);
    if (copy) memcpy(copy, STR(A0), n);
    RET((uintptr_t)copy);
}
CRT(memcpy) { RET((uintptr_t)memcpy(PTR(A0), PTR(A1), (size_t)A2)); }
CRT(memmove) { RET((uintptr_t)memmove(PTR(A0), PTR(A1), (size_t)A2)); }
CRT(memset) { RET((uintptr_t)memset(PTR(A0), (int)A1, (size_t)A2)); }
CRT(memcmp) { RET_I32(memcmp(PTR(A0), PTR(A1), (size_t)A2)); }
CRT(memchr) { RET((uintptr_t)memchr(PTR(A0), (int)A1, (size_t)A2)); }
CRT(wcslen) {
    const u16 *s = PTR(A0);
    size_t n = 0;
    while (s[n]) n++;
    RET(n);
}

// Classes de caractères : celles de l'ASCII (paramètres régionaux « C »).
#define CTYPE(name) \
    CRT(name) { RET_I32((u32)A0 < 128 ? name((int)A0) : 0); }
CTYPE(isalpha)
CTYPE(isdigit)
CTYPE(isalnum)
CTYPE(isspace)
CTYPE(isupper)
CTYPE(islower)
CTYPE(isxdigit)
CTYPE(ispunct)
CTYPE(isprint)
CRT(tolower) { RET_I32((u32)A0 < 128 ? tolower((int)A0) : (int)A0); }
CRT(toupper) { RET_I32((u32)A0 < 128 ? toupper((int)A0) : (int)A0); }

// --- Conversions ---

// « long » de Windows : 32 bits. Le résultat de l'hôte est borné comme le ferait Windows.
CRT(strtol) {
    errno = 0;
    long long v = strtoll(STR(A0), PTR(A1), (int)A2);
    if (v > INT32_MAX) v = INT32_MAX, errno = ERANGE;
    if (v < INT32_MIN) v = INT32_MIN, errno = ERANGE;
    if (errno) sync_errno(ctx);
    RET_I32(v);
}
CRT(strtoul) {
    errno = 0;
    const char *s = STR(A0);
    unsigned long long v = strtoull(s, PTR(A1), (int)A2);
    // Un nombre négatif est accepté et replié sur 32 bits, comme sous Windows.
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') v = (u32)v;
    else if (v > UINT32_MAX) v = UINT32_MAX, errno = ERANGE;
    if (errno) sync_errno(ctx);
    RET((u32)v);
}
CRT(strtoll) {
    errno = 0;
    RET(strtoll(STR(A0), PTR(A1), (int)A2));
    if (errno) sync_errno(ctx);
}
CRT(strtoull) {
    errno = 0;
    RET(strtoull(STR(A0), PTR(A1), (int)A2));
    if (errno) sync_errno(ctx);
}
CRT(atoi) {
    long long v = strtoll(STR(A0), NULL, 10);
    RET_I32(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v);
}
CRT(strtod) {
    errno = 0;
    F64(0) = strtod(STR(A0), PTR(A1));
    if (errno) sync_errno(ctx);
}
CRT(atof) { F64(0) = strtod(STR(A0), NULL); }

// --- Mathématiques : arguments et résultat dans xmm0, xmm1 ---

#define MATH1(name) \
    CRT(name) { F64(0) = name(F64(0)); } \
    CRT(name##f) { F32(0) = name##f(F32(0)); }
#define MATH2(name) \
    CRT(name) { F64(0) = name(F64(0), F64(1)); } \
    CRT(name##f) { F32(0) = name##f(F32(0), F32(1)); }
MATH1(sin)
MATH1(cos)
MATH1(tan)
MATH1(asin)
MATH1(acos)
MATH1(atan)
MATH1(sinh)
MATH1(cosh)
MATH1(tanh)
MATH1(exp)
MATH1(log)
MATH1(log10)
MATH1(log2)
MATH1(sqrt)
MATH1(floor)
MATH1(ceil)
MATH1(fabs)
MATH1(round)
MATH1(trunc)
MATH2(atan2)
MATH2(pow)
MATH2(fmod)
CRT(abs) { RET_I32((i32)A0 < 0 ? 0 - (u32)A0 : (u32)A0); }

// --- Tri et recherche : l'hôte rappelle la fonction de comparaison du jeu ---

static _Thread_local RcnCtx *compare_ctx;
static _Thread_local u64 compare_fn;
static int compare_guest(const void *a, const void *b) {
    return (i32)rcn_call_guest(compare_ctx, compare_fn, (u64)(uintptr_t)a, (u64)(uintptr_t)b, 0, 0);
}
// Les comparaisons peuvent s'imbriquer (une comparaison qui trie à son tour) : contexte rétabli après.
#define WITH_COMPARE(fn, call)                    \
    do {                                          \
        RcnCtx *saved_ctx = compare_ctx;          \
        u64 saved_fn = compare_fn;                \
        compare_ctx = ctx;                        \
        compare_fn = (fn);                        \
        call;                                     \
        compare_ctx = saved_ctx;                  \
        compare_fn = saved_fn;                    \
    } while (0)
// qsort(base, count, size, compare)
CRT(qsort) {
    u64 base = A0, count = A1, size = A2;
    WITH_COMPARE(A3, qsort(PTR(base), (size_t)count, (size_t)size, compare_guest));
}
// bsearch(key, base, count, size, compare)
CRT(bsearch) {
    u64 key = A0, base = A1, count = A2, size = A3;
    void *found;
    WITH_COMPARE(STK(4), found = bsearch(PTR(key), PTR(base), (size_t)count, (size_t)size, compare_guest));
    RET((uintptr_t)found);
}

// --- Fichiers ---
// FILE * du jeu : celui de l'hôte, sauf stdin, stdout et stderr, désignés par l'adresse d'une case
// de crt_iob et dirigés vers l'application.

// 0 : fichier ordinaire ; sinon 1 (stdin), RCN_STREAM_STDOUT + 1 ou RCN_STREAM_STDERR + 1.
static int std_index(RcnCtx *ctx, u64 file) {
    u64 first = (u64)(uintptr_t)ctx->proc->crt_iob;
    return file - first < 3 ? (int)(file - first) + 1 : 0;
}
// Écrit dans un fichier du jeu ; renvoie le nombre d'octets écrits.
static size_t file_write(RcnCtx *ctx, u64 file, const void *data, size_t len) {
    int std = std_index(ctx, file);
    if (std == 0) return fwrite(data, 1, len, PTR(file));
    if (std == 1) return 0;
    rcn_host_write(ctx, std == 3 ? RCN_STREAM_STDERR : RCN_STREAM_STDOUT, data, len);
    return len;
}
static u64 std_file(RcnCtx *ctx, int index) { return (u64)(uintptr_t)&ctx->proc->crt_iob[index]; }

CRT(__acrt_iob_func) { RET((u32)A0 < 3 ? std_file(ctx, (int)A0) : 0); }

// fopen(path, mode) : toujours en binaire ; les options propres à Windows du mode sont ignorées.
CRT(fopen) {
    const char *mode = STR(A1);
    char host_mode[8];
    size_t n = 0;
    for (const char *m = mode; *m && *m != ','; m++) {
        if (strchr("rwa+x", *m) && n < 4) host_mode[n++] = *m;
    }
    host_mode[n++] = 'b';
    host_mode[n] = 0;
    char *path = rcn_host_path(ctx, STR(A0));
    FILE *f = path ? fopen(path, host_mode) : NULL;
    if (!f) sync_errno(ctx);
    free(path);
    RET((uintptr_t)f);
}
CRT(fclose) { RET_I32(std_index(ctx, A0) ? 0 : fclose(PTR(A0))); }
CRT(fflush) { RET_I32(!A0 || std_index(ctx, A0) ? 0 : fflush(PTR(A0))); }
// fread(buffer, size, count, file)
CRT(fread) { RET(std_index(ctx, A3) ? 0 : fread(PTR(A0), (size_t)A1, (size_t)A2, PTR(A3))); }
// fwrite(buffer, size, count, file)
CRT(fwrite) {
    size_t size = (size_t)A1, count = (size_t)A2;
    if (!size || !count) {
        RET(0);
        return;
    }
    RET(file_write(ctx, A3, PTR(A0), size * count) / size);
}
// fseek(file, offset, origin) : décalage « long » de 32 bits ; _fseeki64 : 64 bits.
CRT(fseek) { RET_I32(std_index(ctx, A0) ? -1 : fseek64(PTR(A0), (i32)A1, (int)A2)); }
CRT(_fseeki64) { RET_I32(std_index(ctx, A0) ? -1 : fseek64(PTR(A0), (i64)A1, (int)A2)); }
CRT(ftell) { RET_I32(std_index(ctx, A0) ? -1 : ftell64(PTR(A0))); }
CRT(_ftelli64) { RET(std_index(ctx, A0) ? -1 : (i64)ftell64(PTR(A0))); }
CRT(rewind) {
    if (!std_index(ctx, A0)) rewind(PTR(A0));
}
CRT(feof) { RET_I32(std_index(ctx, A0) ? A0 == std_file(ctx, 0) : feof(PTR(A0))); }
CRT(ferror) { RET_I32(std_index(ctx, A0) ? 0 : ferror(PTR(A0))); }
CRT(fgetc) { RET_I32(std_index(ctx, A0) ? EOF : fgetc(PTR(A0))); }
// fgets(buffer, size, file)
CRT(fgets) { RET((uintptr_t)(std_index(ctx, A2) ? NULL : fgets(PTR(A0), (int)A1, PTR(A2)))); }
CRT(ungetc) { RET_I32(std_index(ctx, A1) ? EOF : ungetc((int)A0, PTR(A1))); }
// fputc(character, file)
CRT(fputc) {
    char c = (char)A0;
    RET_I32(file_write(ctx, A1, &c, 1) == 1 ? (u8)c : EOF);
}
// fputs(text, file)
CRT(fputs) {
    size_t len = strlen(STR(A0));
    RET_I32(file_write(ctx, A1, STR(A0), len) == len ? 0 : EOF);
}
CRT(putchar) {
    char c = (char)A0;
    rcn_host_write(ctx, RCN_STREAM_STDOUT, &c, 1);
    RET_I32((u8)c);
}
CRT(puts) {
    rcn_host_write(ctx, RCN_STREAM_STDOUT, STR(A0), strlen(STR(A0)));
    rcn_host_write(ctx, RCN_STREAM_STDOUT, "\n", 1);
    RET(0);
}
CRT(setvbuf) { RET(0); }
CRT(remove) {
    char *path = rcn_host_path(ctx, STR(A0));
    int r = path ? remove(path) : -1;
    if (r) sync_errno(ctx);
    free(path);
    RET_I32(r);
}
CRT(rename) {
    char *from = rcn_host_path(ctx, STR(A0)), *to = rcn_host_path(ctx, STR(A1));
    int r = from && to ? rename(from, to) : -1;
    if (r) sync_errno(ctx);
    free(from);
    free(to);
    RET_I32(r);
}

// --- printf et scanf (UCRT : une fonction commune par famille) ---

enum {
    // Options de __stdio_common_v*printf.
    PRINTF_LEGACY_NULL_TERMINATION = 1, // vsnprintf d'avant C99 : zéro final ajouté seulement s'il tient
    PRINTF_STANDARD_SNPRINTF = 2,       // snprintf de C99 : longueur complète renvoyée, zéro final garanti
};

// __stdio_common_vfprintf(options, file, format, locale, va_list)
CRT(__stdio_common_vfprintf) {
    RcnBuf buf = {0};
    rcn_format(&buf, STR(A2), STK(4));
    size_t written = file_write(ctx, A1, buf.data, buf.len);
    RET_I32(written == buf.len ? (i64)buf.len : -1);
    free(buf.data);
}
// __stdio_common_vsprintf(options, buffer, count, format, locale, va_list)
CRT(__stdio_common_vsprintf) {
    u64 options = A0;
    char *dest = PTR(A1);
    size_t count = (size_t)A2;
    RcnBuf buf = {0};
    rcn_format(&buf, STR(A3), STK(5));
    i64 result = (i64)buf.len;
    if (dest && count) {
        if (buf.len < count) {
            memcpy(dest, buf.data, buf.len + 1);
        } else if (options & PRINTF_STANDARD_SNPRINTF) {
            memcpy(dest, buf.data, count - 1);
            dest[count - 1] = 0;
        } else {
            // Tronqué : -1, sans zéro final sauf si l'option le demande.
            memcpy(dest, buf.data, count);
            if (options & PRINTF_LEGACY_NULL_TERMINATION) dest[count - 1] = 0;
            result = -1;
        }
    } else if (!(options & PRINTF_STANDARD_SNPRINTF) && dest) {
        result = -1;
    }
    free(buf.data);
    RET_I32(result);
}
// __stdio_common_vsscanf(options, buffer, count, format, locale, va_list) : les arguments sont des
// pointeurs, passés tels quels au sscanf de l'hôte avec un format aux tailles de Windows.
CRT(__stdio_common_vsscanf) {
    const char *input = STR(A1);
    size_t count = (size_t)A2;
    u64 va = STK(5);
    char *format = rcn_scan_format(STR(A3));
    if (!format) rcn_fail(ctx, "format de scanf non gere : %s", STR(A3));
    // Le texte n'est pas forcément terminé par un zéro quand sa longueur est donnée.
    char *copy = NULL;
    if (count != (size_t)-1) {
        copy = malloc(count + 1);
        if (copy) {
            memcpy(copy, input, count);
            copy[count] = 0;
            input = copy;
        }
    }
    void *p[16];
    for (int i = 0; i < 16; i++) p[i] = PTR(rd64(va + 8 * i));
    int r = sscanf(input, format, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13],
                   p[14], p[15]);
    free(copy);
    free(format);
    RET_I32(r);
}

// --- Temps ---

CRT(_time64) {
    i64 now = (i64)time(NULL);
    if (A0) wr64(A0, (u64)now);
    RET(now);
}
// clock : millisecondes depuis le lancement (CLOCKS_PER_SEC vaut 1000 sous Windows).
CRT(clock) { RET_I32((rcn_now_ns() - ctx->proc->start_ns) / 1000000); }

#define E(name) {#name, crt_##name}
#define E2(name) E(name), E(name##f)
const RcnShim rcn_crt[] = {
    E(__acrt_iob_func), E(__p___argc), E(__p___argv), E(__p__commode), E(__p__environ), E(__p__fmode),
    E(__setusermatherr), E(__stdio_common_vfprintf), E(__stdio_common_vsprintf), E(__stdio_common_vsscanf),
    E(_c_exit), E(_cexit), E(_configthreadlocale), E(_configure_narrow_argv), E(_crt_atexit), E(_errno), E(_exit),
    E(_fseeki64), E(_ftelli64), E(_initialize_narrow_environment), E(_initterm), E(_initterm_e), E(_seh_filter_exe),
    E(_set_app_type), E(_set_invalid_parameter_handler), E(_set_new_mode), E(_strdup), E(_stricmp), E(_strnicmp),
    E(_time64), E(abort), E(abs), E(atof), E(atoi), E(bsearch), E(calloc), E(clock), E(exit), E(fclose), E(feof),
    E(ferror), E(fflush), E(fgetc), E(fgets), E(fopen), E(fputc), E(fputs), E(fread), E(free), E(fseek), E(ftell),
    E(fwrite), E(getenv), E(isalnum), E(isalpha), E(isdigit), E(islower), E(isprint), E(ispunct), E(isspace),
    E(isupper), E(isxdigit), E(longjmp), E(malloc), E(memchr), E(memcmp), E(memcpy), E(memmove), E(memset),
    E(putchar), E(puts), E(qsort), E(realloc), E(remove), E(rename), E(rewind), E(setvbuf), E(strcat), E(strchr),
    E(strcmp), E(strcpy), E(strlen), E(strncat), E(strncmp), E(strncpy), E(strrchr), E(strstr), E(strtod),
    E(strtol), E(strtoll), E(strtoul), E(strtoull), E(tolower), E(toupper), E(ungetc), E(wcslen),
    E2(acos), E2(asin), E2(atan), E2(atan2), E2(ceil), E2(cos), E2(cosh), E2(exp), E2(fabs), E2(floor), E2(fmod),
    E2(log), E2(log10), E2(log2), E2(pow), E2(round), E2(sin), E2(sinh), E2(sqrt), E2(tan), E2(tanh), E2(trunc),
    // Autres noms des mêmes fonctions.
    {"strdup", crt__strdup},
    {"_strtoi64", crt_strtoll},
    {"_strtoui64", crt_strtoull},
    {"_setjmp", crt_setjmp},
    {"_setjmpex", crt_setjmp},
    {"__intrinsic_setjmp", crt_setjmp},
    {"__intrinsic_setjmpex", crt_setjmp},
};
const size_t rcn_crt_count = sizeof rcn_crt / sizeof rcn_crt[0];
