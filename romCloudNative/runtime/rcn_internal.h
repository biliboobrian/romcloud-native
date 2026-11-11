// État d'un jeu en cours d'exécution, partagé par le runtime et les fonctions Windows de l'hôte.
#ifndef RCN_INTERNAL_H
#define RCN_INTERNAL_H

#include <setjmp.h>
#include <stdio.h>

#include "rcn.h"
#include "rcn_host.h"

// Fonction importée par le jeu (case de sa table d'imports) : fournie par l'hôte, ou absente.
typedef struct {
    RcnFn fn;
    const char *dll;
    const char *name;
} RcnImport;

// setjmp du jeu : point de reprise de l'hôte et registres du jeu à restaurer par longjmp.
typedef struct {
    u64 guest_buf; // adresse du jmp_buf du jeu (clé)
    u64 used;      // dernière utilisation (le plus ancien est remplacé)
    jmp_buf host;
    u64 gpr[16];
    RcnXmm xmm[10]; // xmm6 … xmm15
} RcnJmp;

#define RCN_JMP_SLOTS 64

typedef struct RcnProcess {
    const RcnModule *module;
    const RcnHost *host;
    RcnResult *result;
    u8 *image;
    RcnImport *imports;
    u32 import_count;
    const RcnImport *current_import; // import en cours d'appel
    u8 *stack;
    u8 *teb;
    jmp_buf exit_jmp;
    u32 last_error;
    u64 start_ns;

    // Variables locales aux threads du jeu (un seul thread pour l'instant).
    u8 *tls_block;
    u64 tls_slots[1];

    // Dossier du jeu (celui de son exécutable) : racine de ses chemins de fichiers.
    char *game_dir;
    char *exe_name;

    // Bibliothèque C.
    int crt_errno;
    int crt_fmode, crt_commode;
    int crt_argc;
    char *crt_argv_data[2];
    char **crt_argv;
    char *crt_environ_data[1];
    char **crt_environ;
    u8 crt_iob[3]; // stdin, stdout, stderr : seule leur adresse compte (FILE * du jeu)
    u64 *atexit_fns;
    u32 atexit_count, atexit_cap;
    RcnJmp *jmps;
    u64 jmp_clock;
} RcnProcess;

enum {
    RCN_JMP_EXIT = 1,
    RCN_JMP_TRAP = 2,
};

// Table de fonctions Windows d'une bibliothèque.
typedef struct {
    const char *name;
    RcnFn fn;
} RcnShim;

// --- Écriture des fonctions Windows : arguments (convention Windows x64) et résultat ---
#define A0 (ctx->r[1])
#define A1 (ctx->r[2])
#define A2 (ctx->r[8])
#define A3 (ctx->r[9])
// Argument n (à partir de 4) : sur la pile, après l'adresse de retour et les 32 octets réservés.
#define STK(n) rd64(ctx->r[4] + 8 + 8 * (n))
#define RET(v) (ctx->r[0] = (u64)(v))
// Résultat entier signé de 32 bits (int, long de Windows).
#define RET_I32(v) (ctx->r[0] = (u64)(i64)(i32)(v))
#define PTR(v) ((void *)(uintptr_t)(v))
#define STR(v) ((const char *)(uintptr_t)(v))
// Arguments et résultat flottants : xmm0 … xmm3 selon la position de l'argument.
#define F64(n) (ctx->xmm[n].db[0])
#define F32(n) (ctx->xmm[n].f[0])

// Le jeu se termine (ExitProcess, exit).
_Noreturn void rcn_exit(RcnCtx *ctx, u32 code);
// Arrêt sur une erreur décrite par un message.
_Noreturn void rcn_fail(RcnCtx *ctx, const char *fmt, ...);
// longjmp du jeu vers un point enregistré par rcn_setjmp.
_Noreturn void rcn_longjmp(RcnCtx *ctx, u64 guest_buf, int value);
// Fonction Windows fournie par l'hôte, ou NULL.
RcnFn rcn_win32_lookup(const char *dll, const char *name);
// L'hôte appelle une fonction du jeu (jusqu'à 4 arguments entiers) ; renvoie rax.
u64 rcn_call_guest(RcnCtx *ctx, u64 fn, u64 a0, u64 a1, u64 a2, u64 a3);
// Texte du jeu vers l'application (RCN_STREAM_STDOUT / RCN_STREAM_STDERR).
void rcn_host_write(RcnCtx *ctx, int stream, const void *data, size_t len);
// Chemin Windows du jeu → chemin de l'hôte, sous le dossier du jeu. À libérer par free.
char *rcn_host_path(RcnCtx *ctx, const char *windows_path);
u64 rcn_now_ns(void);
void *rcn_page_alloc(size_t size);
void rcn_page_free(void *p);

// --- Mise en forme à la printf (rcn_format.c) ---
typedef struct {
    char *data;
    size_t len, cap;
} RcnBuf;
void rcn_buf_put(RcnBuf *buf, const char *data, size_t len);
// Met en forme `fmt` avec les arguments d'un va_list du jeu (cases de 8 octets).
void rcn_format(RcnBuf *out, const char *fmt, u64 va);
// Réécrit un format de scanf de Windows pour l'hôte (long de 32 bits…). NULL si non géré. À libérer.
char *rcn_scan_format(const char *fmt);
// Texte UTF-16 du jeu → UTF-8 ; max : nombre de caractères au plus (-1 : jusqu'au zéro). À libérer.
char *rcn_utf16_to_utf8(const u16 *text, i64 max);

extern const RcnShim rcn_kernel32[];
extern const size_t rcn_kernel32_count;
extern const RcnShim rcn_crt[];
extern const size_t rcn_crt_count;

// Adresse de retour de la fonction d'entrée : y revenir termine le programme.
#define RCN_RETURN_SENTINEL 0xdead0000beef0000ULL

#endif
