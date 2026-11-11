// État d'un jeu en cours d'exécution, partagé par le runtime et les fonctions Windows de l'hôte.
#ifndef RCN_INTERNAL_H
#define RCN_INTERNAL_H

#include <pthread.h>
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

struct RcnThread;

// Objet du noyau désigné par un handle du jeu. Son état est protégé par le verrou du processus.
typedef enum { RCN_OBJ_FILE = 1, RCN_OBJ_THREAD, RCN_OBJ_EVENT, RCN_OBJ_MUTEX, RCN_OBJ_SEMAPHORE } RcnObjType;
typedef struct RcnObject {
    int type;
    int refs;     // handles ouverts, plus le thread lui-même tant qu'il tourne
    int fd;       // fichier de l'hôte
    int signaled; // événement signalé, thread terminé
    int manual;   // événement à remise à zéro manuelle
    u32 exit_code;
    u32 thread_id;
    struct RcnThread *thread; // thread créé suspendu, pas encore lancé
    u32 owner;                // mutex : identifiant du thread qui le tient (0 : libre)
    u32 count, max;           // mutex : prises imbriquées ; sémaphore : compteur et son maximum
} RcnObject;

typedef struct RcnProcess {
    const RcnModule *module;
    const RcnHost *host;
    RcnResult *result;
    u8 *image;
    RcnImport *imports;
    u32 import_count;
    u64 start_ns;

    // Verrou du processus : handles, objets, threads, fin du jeu. `cond` est signalée à chaque
    // changement d'état d'un objet (toutes les attentes la partagent).
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int finished;      // le jeu est terminé (résultat écrit)
    u32 threads_alive; // threads du jeu en cours
    u32 next_thread_id;
    RcnObject **handles;
    u32 handle_cap;
    u64 tls_used; // cases de TlsAlloc prises (64 au plus)

    // Dossier du jeu (celui de son exécutable) : racine de ses chemins de fichiers.
    char *game_dir;
    char *exe_name;
    // Ligne de commande vue par le jeu, créée à la demande (GetCommandLineA / W).
    char *command_line;
    u16 *command_line_w;

    // Bibliothèque C.
    int crt_fmode, crt_commode;
    int crt_argc;
    char *crt_argv_data[2];
    char **crt_argv;
    char *crt_environ_data[1];
    char **crt_environ;
    u8 crt_iob[3]; // stdin, stdout, stderr : seule leur adresse compte (FILE * du jeu)
    u64 *atexit_fns;
    u32 atexit_count, atexit_cap;
} RcnProcess;

// Thread du jeu : son processeur (en premier : un RcnCtx * est aussi un RcnThread *), sa pile, son
// bloc d'environnement (TEB, lu par gs) et ses variables locales.
typedef struct RcnThread {
    RcnCtx ctx;
    u8 *stack;
    u8 *teb;
    u8 *tls_block;
    u64 tls_slots[1]; // tableau lu par gs:[0x58] : un seul module, l'exécutable
    u64 tls_callbacks;
    RcnJmp *jmps;
    u64 jmp_clock;
    int crt_errno;
    const RcnImport *current_import; // import en cours d'appel
    int is_main;
    u64 start_fn, start_arg;
    RcnObject *object; // objet désigné par le handle du thread (NULL pour le thread principal)
} RcnThread;

#define THREAD(ctx) ((RcnThread *)(ctx))

// Champs du bloc d'environnement du thread (mêmes décalages que sous Windows : du code les lit par gs).
enum {
    TEB_STACK_BASE = 0x08,
    TEB_STACK_LIMIT = 0x10,
    TEB_SELF = 0x30,
    TEB_PROCESS_ID = 0x40,
    TEB_THREAD_ID = 0x48,
    TEB_TLS_POINTER = 0x58,
    TEB_PEB = 0x60,
    TEB_LAST_ERROR = 0x68,
    TEB_TLS_SLOTS = 0x1480, // 64 cases de TlsAlloc
    TEB_SIZE = 0x3000,      // le bloc du processus (PEB) suit, à 0x2000
};
#define LAST_ERROR(ctx) (*(u32 *)(THREAD(ctx)->teb + TEB_LAST_ERROR))
#define THREAD_ID(ctx) (*(u32 *)(THREAD(ctx)->teb + TEB_THREAD_ID))

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

// Le jeu se termine (ExitProcess, exit) : aucun de ses threads ne reprend la main.
_Noreturn void rcn_exit(RcnCtx *ctx, u32 code);
// Arrêt du jeu sur une erreur décrite par un message.
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

// --- Threads du jeu (rcn_runtime.c) ---
// Crée un thread (pile du jeu de stack_size octets, 0 : taille par défaut) sans le lancer.
RcnThread *rcn_thread_new(RcnProcess *proc, size_t stack_size);
// Lance fn(arg) sur un thread de l'hôte. Renvoie 0 en cas de succès.
int rcn_thread_start(RcnThread *thread, u64 fn, u64 arg);
// Le thread appelant se termine.
_Noreturn void rcn_thread_exit(RcnCtx *ctx, u32 code);

// --- Handles (rcn_k32_sync.c) : à appeler avec le verrou du processus tenu ---
// Nouveau handle sur un objet alloué par calloc (une référence). 0 si la mémoire manque.
u64 rcn_handle_new(RcnProcess *proc, RcnObject *object);
// Objet d'un handle, du type demandé (0 : n'importe lequel), ou NULL.
RcnObject *rcn_handle_get(RcnProcess *proc, u64 handle, int type);
// Rend une référence ; l'objet est détruit avec la dernière.
void rcn_object_unref(RcnObject *object);
// Ferme tous les handles (fin du processus).
void rcn_handles_free(RcnProcess *proc);

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
// UTF-8 (len octets) → UTF-16 dans out (NULL : compte seulement). Renvoie le nombre d'unités UTF-16.
size_t rcn_utf8_to_utf16(const char *text, size_t len, u16 *out);
// Code d'erreur Windows correspondant à l'errno de l'hôte.
u32 rcn_win32_error(int host_errno);
// Crée un thread du jeu ; renvoie son handle (0 en cas d'échec). thread_id : adresse où ranger son identifiant, ou 0.
u64 rcn_create_thread(RcnCtx *ctx, u64 fn, u64 arg, u64 stack_size, int suspended, u64 thread_id);

// Tables de fonctions, par bibliothèque et par domaine.
extern const RcnShim rcn_kernel32[], rcn_k32_file[], rcn_k32_sync[], rcn_crt[];
extern const size_t rcn_kernel32_count, rcn_k32_file_count, rcn_k32_sync_count, rcn_crt_count;

// Adresse de retour de la fonction d'entrée d'un thread.
#define RCN_RETURN_SENTINEL 0xdead0000beef0000ULL

#endif
