// État d'un jeu en cours d'exécution, partagé par le runtime et les fonctions Windows de l'hôte.
#ifndef RCN_INTERNAL_H
#define RCN_INTERNAL_H

#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>

#include "rcn.h"
#include "rcn_host.h"

#if defined(_WIN32)
// Hôte Windows (essais sur PC) : stat 64 bits, sinon les fichiers de plus de 2 Go sont introuvables
// (conteneurs d'Unreal Engine). Android a déjà des tailles sur 64 bits.
#include <sys/stat.h>
#define stat _stat64
#define fstat _fstat64
#endif

// Fonction importée par le jeu (case de sa table d'imports) : fournie par l'hôte, ou absente.
typedef struct {
    RcnFn fn;
    const char *dll;
    const char *name;
    u32 pop; // 32 bits, convention stdcall : octets d'arguments que la fonction retire de la pile
} RcnImport;

// setjmp du jeu : point de reprise de l'hôte et registres du jeu à restaurer par longjmp.
typedef struct {
    u64 guest_buf; // adresse du jmp_buf du jeu (clé)
    u64 used;      // dernière utilisation (le plus ancien est remplacé)
    jmp_buf host;
    u64 gpr[16];
    RcnXmm xmm[10]; // xmm6 … xmm15
    u64 token;      // code machine traduit : jeton de la fonction et numéro du point de setjmp
    u32 site;
} RcnJmp;

#define RCN_JMP_SLOTS 64
#define RCN_MAX_THUNKS 8192
#define RCN_MAX_MODULES 32
#define RCN_MAX_LOADED 16

// Module du jeu chargé : l'exécutable (le premier), puis ses DLL traduites (« fmod64.dll » et
// « fmod64.dll.rcnx » à côté du code de l'exécutable).
typedef struct {
    char *name;      // nom du fichier
    u8 *image;
    u32 size;
    const RcnModule *code;
    int attached;    // DllMain appelée (DLL_PROCESS_ATTACH)
    // Variables locales au thread de la DLL (__declspec(thread)) : case du tableau TLS, modèle, fonctions.
    u32 tls_index;   // 0 : pas de TLS (la case 0 est celle de l'exécutable)
    u64 tls_start, tls_end, tls_callbacks;
    u32 tls_zero;
} RcnLoaded;

struct RcnThread;

// Objet du noyau désigné par un handle du jeu. Son état est protégé par le verrou du processus.
typedef enum { RCN_OBJ_FILE = 1, RCN_OBJ_THREAD, RCN_OBJ_EVENT, RCN_OBJ_MUTEX, RCN_OBJ_SEMAPHORE, RCN_OBJ_IOCP } RcnObjType;
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
    void *data;               // port de complétion : sa file (libérée avec l'objet)
    // Threads qui attendent cet objet seul (WaitForSingleObject) : ils dorment sur `cond`, que seuls les
    // changements de cet objet réveillent. Liste des objets attendus (proc->waited) pour les réveils
    // généraux. Le tout sous le verrou du processus.
    int waiters, cond_ready;
    u32 gen; // signalements (événement à remise à zéro manuelle : libère ceux qui attendaient, comme Windows)
    pthread_cond_t cond;
    struct RcnObject *wnext, *wprev;
} RcnObject;

typedef struct RcnProcess {
    const RcnModule *module;
    const RcnHost *host;
    RcnResult *result;
    u8 *image;
    RcnImport *imports;
    u32 import_count;
    // Fonctions de l'hôte que le jeu appelle par une adresse obtenue à l'exécution (GetProcAddress, méthodes
    // des objets COM), et bibliothèques demandées par leur nom (leur handle est l'adresse de ce nom).
    RcnImport *thunks;
    u32 thunk_count;
    char *modules[RCN_MAX_MODULES];
    RcnLoaded loaded[RCN_MAX_LOADED];
    u32 loaded_count;
    struct RcnGui *gui; // fenêtre, messages, affichage (rcn_gui.h), créé à la demande
    u64 start_ns;
    int trace; // variable d'environnement RCN_TRACE : chaque appel d'une fonction Windows est noté sur la sortie d'erreur
    // RCN_TRACE=mot1,mot2 : seulement les fonctions (et méthodes COM) dont le nom contient l'un des mots.
    const char *trace_filter;

    // Verrou du processus : handles, objets, threads, fin du jeu. `cond` est signalée à chaque
    // changement d'état d'un objet (toutes les attentes la partagent).
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int finished;
    RcnObject *waited;  // objets qu'un thread attend seul (rcn_k32_sync.c)
    int multi_waiters;  // threads dans une attente de plusieurs objets (sur `cond`)      // le jeu est terminé (résultat écrit)
    u32 threads_alive; // threads du jeu en cours
    u32 next_thread_id;
    RcnObject **handles;
    u32 handle_cap;
    u64 tls_used; // cases de TlsAlloc prises (64 au plus)
    u64 fls_callbacks[64]; // FlsAlloc : fonction du jeu appelée à la fin de chaque thread (valeur non nulle)

    // Dossier du jeu (celui de son exécutable) : racine de ses chemins de fichiers.
    char *game_dir;
    char *exe_name;
    // Ligne de commande vue par le jeu, créée à la demande (GetCommandLineA / W).
    char *command_line;
    u16 *command_line_w;

    // Bibliothèque C.
    int crt_fmode, crt_commode;
    int crt_argc;
    char *crt_argv_data[18]; // exécutable, arguments de RCN_ARGS (essais), NULL
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
    u32 host_tid; // identifiant du thread chez l'hôte (mise au point : mesures du système)
    u64 tls_callbacks;
    RcnJmp *jmps;
    u64 jmp_clock;
    int crt_errno;
    const RcnImport *current_import; // import en cours d'appel
    int is_main;
    u64 start_fn, start_arg;
    RcnObject *object; // objet désigné par le handle du thread (NULL pour le thread principal)
    // Cache des appels indirects (rcn_call), alloué au premier appel : ici plutôt qu'en _Thread_local,
    // émulé sur Android avant la version 29 (__emutls_get_address à chaque appel).
    struct RcnCallSlot *call_cache;
} RcnThread;

#define THREAD(ctx) ((RcnThread *)(ctx))

// Champs du bloc d'environnement du thread (TEB), aux mêmes décalages que sous Windows — du code du
// jeu les lit directement par gs (64 bits) ou fs (32 bits).
typedef enum {
    TEB_STACK_BASE,
    TEB_STACK_LIMIT,
    TEB_SELF,
    TEB_PROCESS_ID,
    TEB_THREAD_ID,
    TEB_TLS_POINTER,
    TEB_PEB,
    TEB_LAST_ERROR,
    TEB_TLS_SLOTS, // 64 cases de TlsAlloc
} RcnTebField;
enum {
    TEB_TLS_ARRAY = 0x1800, // tableau désigné par TEB_TLS_POINTER : case 0 l'exécutable, puis ses DLL
    TEB_PEB_OFFSET = 0x2000, // le bloc du processus (PEB) suit le TEB
    TEB_SIZE = 0x3000,
};
static inline u8 *rcn_teb(RcnCtx *ctx, RcnTebField field) {
    static const u16 offsets64[] = {0x08, 0x10, 0x30, 0x40, 0x48, 0x58, 0x60, 0x68, 0x1480};
    static const u16 offsets32[] = {0x04, 0x08, 0x18, 0x20, 0x24, 0x2C, 0x30, 0x34, 0xE10};
    return THREAD(ctx)->teb + (ctx->bits32 ? offsets32 : offsets64)[field];
}
#define LAST_ERROR(ctx) (*(u32 *)rcn_teb(ctx, TEB_LAST_ERROR))
#define THREAD_ID(ctx) (*(u32 *)rcn_teb(ctx, TEB_THREAD_ID))

// Valeur de la taille d'une adresse du jeu (4 ou 8 octets) en mémoire.
static inline u64 rdp(RcnCtx *ctx, u64 a) { return ctx->bits32 ? rd32(a) : rd64(a); }
static inline void wrp(RcnCtx *ctx, u64 a, u64 v) {
    if (ctx->bits32) wr32(a, (u32)v);
    else wr64(a, v);
}
#define PTR_SIZE(ctx) ((ctx)->bits32 ? 4u : 8u)

// Table de fonctions Windows d'une bibliothèque. args32 dit si la fonction est prête pour un jeu
// 32 bits (pointeurs et structures de 4 octets vérifiés) et combien d'arguments elle retire de la pile :
// 0 : pas prête ; S32(n) : convention stdcall à n arguments ; C32 : convention cdecl (l'appelant les retire).
typedef struct {
    const char *name;
    RcnFn fn;
    u8 args32;
} RcnShim;
#define S32(n) ((n) + 1)
#define C32 0xFF

// --- Écriture des fonctions Windows : arguments et résultat ---
// Argument entier ou pointeur numéro n. 64 bits : rcx, rdx, r8, r9, puis la pile après l'adresse de
// retour et les 32 octets réservés. 32 bits : tous sur la pile, 4 octets chacun.
static inline u64 rcn_arg(RcnCtx *ctx, int n) {
    if (ctx->bits32) return rd32((u32)(ctx->r[4] + 4 + 4 * (u32)n));
    switch (n) {
    case 0: return ctx->r[1];
    case 1: return ctx->r[2];
    case 2: return ctx->r[8];
    case 3: return ctx->r[9];
    default: return rd64(ctx->r[4] + 8 + 8 * (u64)n);
    }
}
#define A0 rcn_arg(ctx, 0)
#define A1 rcn_arg(ctx, 1)
#define A2 rcn_arg(ctx, 2)
#define A3 rcn_arg(ctx, 3)
#define STK(n) rcn_arg(ctx, n)
// Résultat : rax, ou eax en 32 bits (les registres d'un jeu 32 bits ne dépassent jamais 32 bits).
#define RET(v) (ctx->r[0] = ctx->bits32 ? (u64)(u32)(v) : (u64)(v))
// Résultat entier signé de 32 bits (int, long de Windows).
#define RET_I32(v) (ctx->r[0] = ctx->bits32 ? (u64)(u32)(i32)(v) : (u64)(i64)(i32)(v))
// Résultat de 64 bits d'une fonction Windows : rax, ou edx:eax pour un jeu 32 bits (CSteamID de Steam,
// GetTickCount64, taille de fichier).
static inline void rcn_ret64(RcnCtx *ctx, u64 v) {
    if (ctx->bits32) {
        ctx->r[0] = (u64)(u32)v;
        ctx->r[2] = (u64)(u32)(v >> 32);
    } else {
        ctx->r[0] = v;
    }
}
#define RET64(v) rcn_ret64(ctx, (u64)(v))
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
// Code machine traduit : longjmp sans longjmp de l'hôte (registres rétablis, remontée demandée).
void rcn_longjmp_native(RcnCtx *ctx, u64 guest_buf, int value);
// Exécute un jeu dont le code traduit est décrit par `module`.
int rcn_run_module(const RcnModule *module, const char *exe_path, const RcnHost *host, RcnResult *result);
// Fonction Windows fournie par l'hôte, ou NULL.
// *pop : octets d'arguments retirés de la pile (stdcall, 32 bits).
RcnFn rcn_win32_lookup(const char *dll, const char *name, int bits32, u32 *pop);
// L'hôte appelle une fonction du jeu (jusqu'à 4 arguments entiers) ; renvoie rax.
u64 rcn_call_guest(RcnCtx *ctx, u64 fn, u64 a0, u64 a1, u64 a2, u64 a3);
// Idem avec n arguments (32 bits seulement au-delà de 4).
u64 rcn_call_guest_n(RcnCtx *ctx, u64 fn, int n, const u64 *args);
// --- Modules du jeu (rcn_runtime.c) ---
// Module qui contient cette adresse, ou NULL.
RcnLoaded *rcn_loaded_at(RcnProcess *proc, u64 address);
// Module déjà chargé de ce nom (« fmod64 » ou « fmod64.dll », sans la casse), ou NULL.
RcnLoaded *rcn_loaded_named(RcnProcess *proc, const char *name);
// DLL du jeu de ce nom (chemin éventuel ignoré, sans la casse) : chargée au besoin avec son code traduit,
// DllMain appelée sur ce thread. Rend son adresse (HMODULE), ou 0 si elle n'existe pas.
u64 rcn_load_dll(RcnCtx *ctx, const char *name);
// LoadLibrary : bibliothèque système connue, sinon DLL du jeu (chargée au besoin). 0 si absente.
u64 rcn_load_library(RcnCtx *ctx, const char *name);
void rcn_guest_stack_once(RcnCtx *ctx, const char *what);
// Adresse d'une fonction exportée par un module (`name`, ou « #numéro » d'ordre), ou 0.
u64 rcn_module_export(RcnLoaded *module, const char *name);
// Code traduit d'un module (fichier .rcnx, rcn_native.c). NULL et `error` rempli en cas d'échec.
const RcnModule *rcn_load_rcnx(const char *path, char *error, size_t error_size);
// RaiseException(code, flags, count, arguments) : répartition aux gestionnaires du jeu (rcn_k32_unwind.c).
void rcn_raise_exception(RcnCtx *ctx);
// Module dont le code machine traduit contient cette adresse de l'hôte, ou NULL.
const RcnModule *rcn_code_module(u64 pc);
// Dossier du fichier .rcnx de l'exécutable (pour y trouver ceux des DLL).
void rcn_set_code_dir(const char *code_path);

// Adresse appelable par le jeu d'une fonction de l'hôte ; pop : octets d'arguments qu'elle retire de la
// pile (stdcall, 32 bits). `name` doit rester valide. 0 si la réserve est épuisée.
u64 rcn_thunk(RcnCtx *ctx, const char *dll, const char *name, RcnFn fn, u32 pop);
// Thread créé par le runtime : cœurs d'origine du processus (un thread hérite de ceux du thread qui le
// crée, et le thread du jeu peut avoir été placé sur le plus rapide, voir rcn_sdl_gl.c). Sans effet ailleurs.
void rcn_thread_default_affinity(void);
// Taille de l'écran imité (largeur, ou hauteur si height) : 1920 × 1080 ou RCN_SCREEN (rcn_user32_x64.c).
int rcn_screen_size(int height);
void rcn_big_cores(void);
// État du thread de rendu d'OpenGL (journal du chien de garde RCN_HANG_DUMP).
void rcn_sdlgl_debug(void);

// --- Objets COM fournis au jeu (DirectDraw, DirectInput, DirectSound) ---
// Une méthode par case de la table virtuelle, dans l'ordre de l'interface ; fn NULL : pas encore
// fournie (le jeu s'arrête en la nommant s'il l'appelle). args : arguments, « this » compris.
typedef struct {
    const char *name;
    RcnFn fn;
    u8 args;
} RcnMethod;
// Crée un objet : pointeur de table virtuelle, compteur de références, puis `data_size` octets à zéro
// (COM_DATA). La table est construite au premier objet de l'interface.
u64 rcn_com_new(RcnCtx *ctx, const char *interface_name, const RcnMethod *methods, u32 count, size_t data_size);
// Interface d'un objet créé par rcn_com_new, ou NULL : le journal d'une faute fatale nomme ainsi l'objet
// que le jeu appelait (rcn_runtime.c).
const char *rcn_com_iface(u64 object);
#define COM_DATA(type, object) ((type *)((u8 *)PTR(object) + 8))
#define COM_REFS(object) (*(u32 *)((u8 *)PTR(object) + 4))
// Message de suivi (sortie d'erreur ; logcat sous Android).
void rcn_log(const char *fmt, ...);
// Texte du jeu vers l'application (RCN_STREAM_STDOUT / RCN_STREAM_STDERR).
void rcn_host_write(RcnCtx *ctx, int stream, const void *data, size_t len);
// Chemin Windows du jeu → chemin de l'hôte, sous le dossier du jeu. À libérer par free.
char *rcn_host_path(RcnCtx *ctx, const char *windows_path);
char *rcn_guest_dir(RcnProcess *proc);
char *rcn_full_path(RcnCtx *ctx, const char *name);
// Dossier (chemin Windows du jeu) où chercher les DLL demandées par leur nom.
void rcn_dll_dir_add(RcnCtx *ctx, const char *windows_dir);
u64 rcn_now_ns(void);
void rcn_sleep_ms(u32 ms);
void *rcn_page_alloc(size_t size);
void rcn_page_free(void *p);

// --- Mémoire du jeu (rcn_mem.c) ---
// Mémoire rendue au jeu ou dont l'adresse lui est donnée : sous 4 Go pour un jeu 32 bits.
void *rcn_galloc(RcnCtx *ctx, size_t size, int zero);
void *rcn_grealloc(RcnCtx *ctx, void *p, size_t size);
void rcn_gfree(RcnCtx *ctx, void *p);
void *rcn_low_alloc(size_t size, int zero);
void rcn_low_free(void *p);
void *rcn_low_realloc(void *p, size_t size);
// Ligne de commande du jeu (texte de la mémoire du jeu : sous 2 Go pour un jeu 32 bits).
const char *rcn_command_line(RcnCtx *ctx);
// Bibliothèque C de Visual C++ pour les jeux 32 bits (rcn_crt32.c) : fonctions, et adresses de ses variables
// exportées (_acmdln, _iob…), 0 si inconnue.
// Winsock sans réseau pour les jeux 32 bits (rcn_net32.c).
extern const RcnShim rcn_wsock32[];
extern const size_t rcn_wsock32_count;
extern const RcnShim rcn_crt32[];
extern const size_t rcn_crt32_count;
u64 rcn_crt32_data(const char *name);
void rcn_eh32_RtlUnwind(RcnCtx *ctx); // remontée globale du SEH des jeux 32 bits (rcn_crt32.c)
size_t rcn_low_size(void *p);
// Bloc du tas bas qui contient `p` : 1 et sa taille (en-tête compris) et son état s'il y est, sinon 0.
int rcn_low_block(const void *p, size_t *size, int *used);
// Mémoire à une adresse imposée (image d'un exe 32 bits) ; NULL si elle est occupée.
void *rcn_map_fixed(u64 address, size_t size);
void rcn_unmap_fixed(void *address, size_t size);
// Mémoire virtuelle d'un jeu 64 bits : VirtualAlloc / VirtualFree / VirtualQuery (MEMORY_BASIC_INFORMATION en `info`).
u64 rcn_vm_alloc(u64 address, u64 size, u32 type, u32 protect);
int rcn_vm_free(u64 address, u64 size, u32 type);
int rcn_vm_query(u64 address, u64 info);
// Bibliothèque fournie par l'hôte, demandée par son nom : handle (0 si inconnue).
u64 rcn_module_handle(RcnCtx *ctx, const char *name);
// Nombre de processeurs de l'hôte, et mémoire physique (octets) : totale, libre.
u32 rcn_cpu_count(void);
void rcn_memory_status(u64 *total, u64 *available);
void rcn_pagefile_status(u64 *total, u64 *available); // mémoire physique plus swap

// --- Threads du jeu (rcn_runtime.c) ---
// Crée un thread (pile du jeu de stack_size octets, 0 : taille par défaut) sans le lancer.
RcnThread *rcn_thread_new(RcnProcess *proc, size_t stack_size);
// Lance fn(arg) sur un thread de l'hôte. Renvoie 0 en cas de succès.
int rcn_thread_start(RcnThread *thread, u64 fn, u64 arg);
// Le thread appelant se termine.
_Noreturn void rcn_thread_exit(RcnCtx *ctx, u32 code);
// Fonctions de fin de thread des cases FlsAlloc (sur le thread qui se termine).
void rcn_fls_thread_end(RcnCtx *ctx);

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
// Même chose pour un jeu 32 bits (cases de 4 octets).
void rcn_format32(RcnBuf *out, const char *fmt, u64 va);
// Réécrit un format de scanf de Windows pour l'hôte (long de 32 bits…). NULL si non géré. À libérer.
char *rcn_scan_format(const char *fmt);
// Texte UTF-16 du jeu → UTF-8 ; max : nombre de caractères au plus (-1 : jusqu'au zéro). À libérer.
char *rcn_utf16_to_utf8(const u16 *text, i64 max);
// UTF-8 (len octets) → UTF-16 dans out (NULL : compte seulement). Renvoie le nombre d'unités UTF-16.
size_t rcn_utf8_to_utf16(const char *text, size_t len, u16 *out);
// Code d'erreur Windows correspondant à l'errno de l'hôte.
u32 rcn_win32_error(int host_errno);
// ntdll!NtQueryDirectoryFile, sur un dossier ouvert par CreateFile (FILE_FLAG_BACKUP_SEMANTICS).
void rcn_nt_QueryDirectoryFile(RcnCtx *ctx);
void rcn_make_guest_dir(RcnCtx *ctx, const char *windows_path);
// Crée un thread du jeu ; renvoie son handle (0 en cas d'échec). thread_id : adresse où ranger son identifiant, ou 0.
u64 rcn_create_thread(RcnCtx *ctx, u64 fn, u64 arg, u64 stack_size, int suspended, u64 thread_id);

// Fonctions du runtime appelées par le code machine traduit, par leur nom (rcn_ops.c).
typedef struct {
    const char *name;
    void *fn;
} RcnHelper;
extern const RcnHelper rcn_helpers[];
extern const size_t rcn_helper_count;

// Tables de fonctions, par bibliothèque et par domaine.
extern const RcnShim rcn_kernel32[], rcn_k32_file[], rcn_k32_sync[], rcn_k32_more[], rcn_k32_unwind[], rcn_crt[];
extern const size_t rcn_kernel32_count, rcn_k32_file_count, rcn_k32_sync_count, rcn_k32_more_count, rcn_k32_unwind_count, rcn_crt_count;
extern const RcnShim rcn_crt_more[]; // bibliothèque C, suite (jeux 64 bits)
extern const size_t rcn_crt_more_count;
// Petites bibliothèques système des jeux 64 bits (rcn_k32_more.c).
extern const RcnShim rcn_advapi32[], rcn_ole32[], rcn_bcrypt[], rcn_user32_x64[];
extern const size_t rcn_advapi32_count, rcn_ole32_count, rcn_bcrypt_count, rcn_user32_x64_count;
// Bibliothèque de classes .NET (rcn_k32_x64.c).
extern const RcnShim rcn_k32_x64[], rcn_advapi32_x64[], rcn_ole32_x64[], rcn_shell32_x64[], rcn_ntdll_x64[];
extern const size_t rcn_k32_x64_count, rcn_advapi32_x64_count, rcn_ole32_x64_count, rcn_shell32_x64_count, rcn_ntdll_x64_count;
// WASAPI (rcn_wasapi.c) : objet COM de ce CLSID (premier mot) pour CoCreateInstance, ou 0.
u64 rcn_wasapi_create(RcnCtx *ctx, u32 clsid_data1);
// Attente de handles du jeu depuis le runtime : WAIT_OBJECT_0 + i, WAIT_TIMEOUT (0x102) ou WAIT_FAILED.
u32 rcn_wait_handles(RcnCtx *ctx, u32 count, const u64 *handles, int all, u32 ms);
// Signale un événement du jeu depuis un thread de l'hôte (fin d'un tampon de son…).
void rcn_event_signal(RcnProcess *proc, u64 handle);
// Nouvel événement du jeu (handle), à réarmement manuel ou automatique.
u64 rcn_event_new(RcnCtx *ctx, int manual, int signaled);
void rcn_wsa_reset_event(RcnCtx *ctx);
// Réveille tous les threads en attente (verrou du processus tenu) : rcn_k32_sync.c.
void rcn_wake_all(RcnProcess *proc);
RcnFn rcn_native_sync_lookup(const char *name);
void rcn_wsa_close_event(RcnCtx *ctx);
// Sémaphore du jeu (handle) ; Release depuis l'hôte, plafonné à son maximum.
u64 rcn_semaphore_new(RcnCtx *ctx, u32 count, u32 max);
void rcn_semaphore_release(RcnProcess *proc, u64 handle, u32 add);
// XInput : la manette de l'hôte (rcn_xinput.c).
extern const RcnShim rcn_xinput[];
extern const size_t rcn_xinput_count;
// Sortie de son waveOut, réseau absent, msacm (rcn_mm_x64.c).
extern const RcnShim rcn_winmm_x64[], rcn_ws2_x64[], rcn_msacm_x64[];
extern const size_t rcn_winmm_x64_count, rcn_ws2_x64_count, rcn_msacm_x64_count;
// Taille, nature et date d'un fichier ouvert (rcn_k32_file.c).
void rcn_file_info(RcnCtx *ctx, u64 handle, u64 *size, int *directory, u64 *time, int *ok);
// Bibliothèques système diverses des jeux 64 bits (rcn_sys_x64.c) : nom de DLL → table.
RcnFn rcn_sys_x64_lookup(const char *dll, const char *name);
// Même chose pour un jeu 32 bits : fonctions marquées prêtes seulement ; *pop : octets d'arguments retirés.
RcnFn rcn_sys_x86_lookup(const char *dll, const char *name, u32 *pop);
// Jeu 32 bits à Direct3D 9 ou plus récent : user32 complet (rcn_win32.c, posé au chargement de l'exécutable).
extern int rcn_user32_modern;
// Steam (steam_api64.dll, steam_api.dll) : fonctions fournies par l'hôte, pour les jeux 64 bits et 32 bits
// (rcn_steam.c).
RcnFn rcn_steam_lookup(const char *name, int bits32);
// Dernière méthode d'interface de l'ancienne API appelée : `back` = 0 pour la plus récente. Sert au journal
// d'une faute fatale (rcn_runtime.c), qui n'a pas d'autre moyen de savoir ce que le jeu venait de demander.
const RcnImport *rcn_steam_call(unsigned back);
// Lecture d'un fichier ouvert à une position (rcn_k32_file.c) : octets lus, ou -1.
i64 rcn_file_read_at(RcnCtx *ctx, u64 handle, u64 offset, void *buffer, size_t size);
// kernel32, suite (rcn_k32_x64b.c) : fichiers projetés, temps local, langue, mémoire globale…
extern const RcnShim rcn_k32_x64b[];
extern const size_t rcn_k32_x64b_count;
// CreateMutex(Ex) : mutex, pris par l'appelant si `owned`.
void rcn_k32_create_mutex(RcnCtx *ctx, int owned);
// Fenêtre, affichage, entrées, son : pour les jeux 32 bits seulement à ce stade.
extern const RcnShim rcn_user32[], rcn_gdi32[], rcn_winmm[], rcn_ddraw[], rcn_dinput[], rcn_dsound[];
extern const size_t rcn_user32_count, rcn_gdi32_count, rcn_winmm_count, rcn_ddraw_count, rcn_dinput_count, rcn_dsound_count;

// Adresse de retour de la fonction d'entrée d'un thread (jamais suivie : y revenir rend la main à l'hôte).
#define RCN_RETURN_SENTINEL 0xdead0000beef0000ULL
#define RCN_RETURN_SENTINEL32 0xdeadbeefu

#endif
