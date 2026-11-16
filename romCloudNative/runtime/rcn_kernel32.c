// RomCloud Native : kernel32, généralités — processus, tas, mémoire, temps, texte.
// Fichiers : rcn_k32_file.c ; threads et synchronisation : rcn_k32_sync.c.
// Chaque fonction lit ses arguments dans les registres du jeu (A0 … A3, STK) et rend son résultat
// dans rax (RET) : voir rcn_internal.h.
#include <stdlib.h>
#include <time.h>

#include "rcn_internal.h"

#if defined(_WIN32)
__declspec(dllimport) void __stdcall Sleep(unsigned long ms);
void rcn_sleep_ms(u32 ms) { Sleep(ms); }
#else
void rcn_sleep_ms(u32 ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
#endif

enum {
    HANDLE_STDIN = 0x10,
    HANDLE_STDOUT = 0x11,
    HANDLE_STDERR = 0x12,
    HANDLE_HEAP = 0x20,
    HEAP_ZERO_MEMORY = 8,
    MEM_COMMIT = 0x1000,
    MEM_RESERVE = 0x2000,
    MEM_RELEASE = 0x8000,
    MEM_PRIVATE = 0x20000,
    PAGE_EXECUTE_READWRITE = 0x40,
    ERROR_INSUFFICIENT_BUFFER = 122,
};

// --- Processus ---

static void k32_GetStdHandle(RcnCtx *ctx) {
    switch ((u32)A0) {
    case (u32)-10: RET(HANDLE_STDIN); break;
    case (u32)-11: RET(HANDLE_STDOUT); break;
    case (u32)-12: RET(HANDLE_STDERR); break;
    default: RET(~0ull); break;
    }
}
static void k32_ExitProcess(RcnCtx *ctx) { rcn_exit(ctx, (u32)A0); }
static void k32_GetLastError(RcnCtx *ctx) { RET(LAST_ERROR(ctx)); }
static void k32_SetLastError(RcnCtx *ctx) { LAST_ERROR(ctx) = (u32)A0; }
static void k32_GetCurrentProcessId(RcnCtx *ctx) { RET(1000); }
static void k32_IsDebuggerPresent(RcnCtx *ctx) { RET(0); }
// Aucun filtre d'exceptions n'est appelé : une faute du jeu arrête son processus.
static void k32_SetUnhandledExceptionFilter(RcnCtx *ctx) { RET(0); }
// Bibliothèque demandée par son nom (GetModuleHandle, LoadLibrary) : connue si l'hôte en fournit des
// fonctions. Son handle est l'adresse d'une copie de son nom, que GetProcAddress retrouve.
u64 rcn_module_handle(RcnCtx *ctx, const char *name) {
    static const char *const known[] = {"kernel32", "user32", "gdi32", "winmm", "ddraw", "dsound", "dinput", "advapi32",
                                        "ole32", "bcrypt", "ntdll", "kernelbase", "oleaut32", "shell32", "sdl2", "d3d11", "dxgi", "xinput1_4", "xinput1_3",
                                        "xinput9_1_0", "security", "secur32", "msvcrt", "ucrtbase", "dbghelp"};
    RcnProcess *proc = ctx->proc;
    size_t k = 0, n = sizeof known / sizeof known[0];
    // Comparaison sans la casse ni l'extension « .dll ».
    for (; k < n; k++) {
        size_t i = 0;
        while (known[k][i] && (name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]) == known[k][i]) i++;
        if (!known[k][i] && (!name[i] || name[i] == '.')) break;
    }
    if (k == n) return 0;
    // Bibliothèques fournies aux seuls jeux 32 bits : absentes pour les autres.
    // (dsound en 64 bits : l'énumération seulement, rcn_dsound_x64.c.)
    if (!ctx->bits32 && (!strcmp(known[k], "gdi32") || !strcmp(known[k], "ddraw") || !strcmp(known[k], "dinput")))
        return 0;
    u64 handle = 0;
    pthread_mutex_lock(&proc->lock);
    for (int i = 0; i < RCN_MAX_MODULES && !handle; i++) {
        if (!proc->modules[i]) {
            proc->modules[i] = rcn_galloc(ctx, strlen(known[k]) + 5, 0);
            if (!proc->modules[i]) break;
            strcpy(proc->modules[i], known[k]);
            strcat(proc->modules[i], ".dll");
        }
        if (strncmp(proc->modules[i], known[k], strlen(known[k])) == 0) handle = (u64)(uintptr_t)proc->modules[i];
    }
    pthread_mutex_unlock(&proc->lock);
    return handle;
}
// GetModuleHandleA(NULL) : l'exécutable lui-même.
// Bibliothèque système connue, sinon DLL du jeu déjà chargée.
static u64 handle_of(RcnCtx *ctx, const char *name) {
    const char *base = name;
    for (const char *p = name; *p; p++) {
        if (*p == '\\' || *p == '/') base = p + 1;
    }
    u64 h = rcn_module_handle(ctx, base);
    if (!h) {
        RcnLoaded *l = rcn_loaded_named(ctx->proc, base);
        if (l) h = (u64)(uintptr_t)l->image;
    }
    return h;
}
static void k32_GetModuleHandleA(RcnCtx *ctx) { RET(A0 ? handle_of(ctx, STR(A0)) : ctx->base); }
static void k32_GetModuleHandleW(RcnCtx *ctx) {
    char *name = A0 ? rcn_utf16_to_utf8(PTR(A0), -1) : NULL;
    RET(A0 ? (name ? handle_of(ctx, name) : 0) : ctx->base);
    free(name);
}
u64 rcn_load_library(RcnCtx *ctx, const char *name) {
    // Chemin complet : son dossier rejoint ceux où chercher les DLL ; seul le nom désigne le module.
    const char *base = name;
    for (const char *p = name; *p; p++) {
        if (*p == '\\' || *p == '/') base = p + 1;
    }
    if (base != name) {
        size_t n = (size_t)(base - name);
        char *dir = malloc(n + 1);
        if (dir) {
            memcpy(dir, name, n);
            dir[n] = 0;
            rcn_dll_dir_add(ctx, dir);
            free(dir);
        }
        name = base;
    }
    u64 h = handle_of(ctx, name);
    if (!h && !ctx->bits32) h = rcn_load_dll(ctx, name);
    if (!h) {
        LAST_ERROR(ctx) = 126; // ERROR_MOD_NOT_FOUND
        static int told;
        if (told++ < 64) rcn_log("LoadLibrary : %s introuvable", name);
    }
    return h;
}
static void k32_LoadLibraryA(RcnCtx *ctx) { RET(A0 ? rcn_load_library(ctx, STR(A0)) : 0); }
// GetProcAddress(module, name) : adresse appelable d'une fonction fournie par l'hôte, ou NULL.
static void k32_GetProcAddress(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    u64 module = A0, name = A1;
    u64 result = 0;
    const char *dll = NULL;
    pthread_mutex_lock(&proc->lock);
    for (int i = 0; i < RCN_MAX_MODULES; i++) {
        if (proc->modules[i] && (u64)(uintptr_t)proc->modules[i] == module) dll = proc->modules[i];
    }
    pthread_mutex_unlock(&proc->lock);
    // Module du jeu (exécutable ou DLL traduite) : sa table d'exportations.
    RcnLoaded *loaded = dll ? NULL : rcn_loaded_at(proc, module);
    if (loaded && (u64)(uintptr_t)loaded->image == module) {
        char ordinal[16];
        if (name < 0x10000) snprintf(ordinal, sizeof ordinal, "#%u", (u32)name);
        result = rcn_module_export(loaded, name < 0x10000 ? ordinal : STR(name));
        if (proc->trace) fprintf(stderr, " [%s!%s]", loaded->name, name < 0x10000 ? ordinal : STR(name));
        if (!result) {
            LAST_ERROR(ctx) = 127;
            static int told;
            if (told++ < 64) rcn_log("GetProcAddress : %s!%s absente", loaded->name, name < 0x10000 ? ordinal : STR(name));
        }
        RET(result);
        return;
    }
    // Un « nom » inférieur à 0x10000 est un numéro d'ordre : cherché comme « #n » dans les tables.
    char ordinal_name[16];
    const char *fname = name >= 0x10000 ? STR(name) : ordinal_name;
    if (name < 0x10000) snprintf(ordinal_name, sizeof ordinal_name, "#%u", (u32)name);
    if (dll) {
        u32 pop = 0;
        RcnFn fn = rcn_win32_lookup(dll, fname, ctx->bits32, &pop);
        // Déjà demandée : même adresse.
        for (u32 i = 0; fn && i < proc->thunk_count && !result; i++) {
            RcnImport *imp = &proc->thunks[i];
            if (imp->fn == fn && imp->dll == dll && strcmp(imp->name, fname) == 0) result = (u64)(uintptr_t)imp;
        }
        if (fn && !result) {
            char *copy = rcn_galloc(ctx, strlen(fname) + 1, 0);
            if (copy) {
                strcpy(copy, fname);
                result = rcn_thunk(ctx, dll, copy, fn, pop);
            }
        }
    }
    if (proc->trace) fprintf(stderr, " [%s!%s]", dll ? dll : "?", fname);
    // Fonction absente d'une bibliothèque fournie par l'hôte : notée (le jeu peut en dépendre).
    if (dll && !result) {
        static int told;
        if (told++ < 64) rcn_log("GetProcAddress : %s!%s absente", dll, fname);
    }
    if (!result) LAST_ERROR(ctx) = 127; // ERROR_PROC_NOT_FOUND
    RET(result);
}
// OutputDebugStringA : vers la sortie d'erreur.
static void k32_OutputDebugStringA(RcnCtx *ctx) {
    if (A0) rcn_host_write(ctx, RCN_STREAM_STDERR, STR(A0), strlen(STR(A0)));
}

// Le dossier du jeu est vu par lui comme « C:\ » : son exécutable est « C:\<nom>.exe ».
// Chemin d'un fichier du dossier du jeu, tel que le jeu le voit (rcn_guest_dir ; « C:\ » à défaut).
static char *guest_file(RcnProcess *proc, const char *name) {
    char *dir = rcn_guest_dir(proc);
    const char *base = dir ? dir : "C:";
    size_t n = strlen(base), m = strlen(name);
    char *path = malloc(n + m + 2);
    if (path) {
        memcpy(path, base, n);
        path[n] = '\\';
        memcpy(path + n + 1, name, m + 1);
    }
    free(dir);
    return path;
}
static char *exe_path(RcnProcess *proc) { return guest_file(proc, proc->exe_name); }
// Copie un texte dans un tampon du jeu de `size` caractères (octets, ou unités UTF-16 si wide) ;
// renvoie le nombre de caractères copiés, zéro final non compris (texte tronqué s'il ne tient pas).
static u32 copy_text(RcnCtx *ctx, const char *text, u64 buffer, u32 size, int wide) {
    if (!text || !size) return 0;
    size_t len = strlen(text);
    if (!wide) {
        u32 n = len < size ? (u32)len : size - 1;
        memcpy(PTR(buffer), text, n);
        wr8(buffer + n, 0);
        if (len >= size) LAST_ERROR(ctx) = ERROR_INSUFFICIENT_BUFFER;
        return n;
    }
    size_t count = rcn_utf8_to_utf16(text, len, NULL);
    u16 *wide_text = malloc((count + 1) * 2);
    if (!wide_text) return 0;
    rcn_utf8_to_utf16(text, len, wide_text);
    u32 n = count < size ? (u32)count : size - 1;
    memcpy(PTR(buffer), wide_text, (size_t)n * 2);
    wr16(buffer + (u64)n * 2, 0);
    free(wide_text);
    if (count >= size) LAST_ERROR(ctx) = ERROR_INSUFFICIENT_BUFFER;
    return n;
}
// GetModuleFileNameA / W(module, buffer, size)
static void module_file_name(RcnCtx *ctx, int wide) {
    RcnLoaded *l = A0 ? rcn_loaded_at(ctx->proc, A0) : NULL;
    if (l && l != &ctx->proc->loaded[0]) {
        char *path = guest_file(ctx->proc, l->name);
        RET(copy_text(ctx, path, A1, (u32)A2, wide));
        free(path);
        return;
    }
    char *path = exe_path(ctx->proc);
    RET(copy_text(ctx, path, A1, (u32)A2, wide));
    free(path);
}
static void k32_GetModuleFileNameA(RcnCtx *ctx) { module_file_name(ctx, 0); }
static void k32_GetModuleFileNameW(RcnCtx *ctx) { module_file_name(ctx, 1); }
// GetCurrentDirectoryA / W(size, buffer)
static void k32_GetCurrentDirectoryA(RcnCtx *ctx) { RET((u32)A0 < 4 ? 4 : copy_text(ctx, "C:\\", A1, (u32)A0, 0)); }
static void k32_GetCurrentDirectoryW(RcnCtx *ctx) { RET((u32)A0 < 4 ? 4 : copy_text(ctx, "C:\\", A1, (u32)A0, 1)); }

// Ligne de commande : le chemin de l'exécutable entre guillemets, sans argument.
static void make_command_line(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    pthread_mutex_lock(&proc->lock);
    if (!proc->command_line) {
        char *path = exe_path(proc);
        const char *args = getenv("RCN_ARGS"); // arguments donnés au jeu (essais)
        size_t n = path ? strlen(path) : 0, extra = args && *args ? strlen(args) + 1 : 0;
        char *line = rcn_galloc(ctx, n + extra + 3, 0);
        u16 *wide = rcn_galloc(ctx, (n + extra + 3) * 2 * 2, 0);
        if (path && line && wide) {
            line[0] = '"';
            memcpy(line + 1, path, n);
            line[n + 1] = '"';
            line[n + 2] = 0;
            if (extra) {
                line[n + 2] = ' ';
                strcpy(line + n + 3, args);
            }
            wide[rcn_utf8_to_utf16(line, strlen(line), wide)] = 0;
            proc->command_line = line;
            proc->command_line_w = wide;
        } else {
            rcn_gfree(ctx, line);
            rcn_gfree(ctx, wide);
        }
        free(path);
    }
    pthread_mutex_unlock(&proc->lock);
}
const char *rcn_command_line(RcnCtx *ctx) {
    make_command_line(ctx);
    return ctx->proc->command_line;
}
static void k32_GetCommandLineA(RcnCtx *ctx) {
    make_command_line(ctx);
    RET((uintptr_t)ctx->proc->command_line);
}
// Mise au point : RCN_CALL_TEST=<rva hex>:<texte> appelle une fonction du jeu (texte, longueur en octets),
// le texte en UTF-16 si son premier caractère est « w », et affiche ce qu'elle rend (au premier
// GetCommandLineW). Compare une fonction pure traduite (hachage…) au résultat attendu.
static void call_test(RcnCtx *ctx) {
    static int done;
    const char *spec = getenv("RCN_CALL_TEST");
    if (!spec || done++) return;
    u32 rva = (u32)strtoul(spec, NULL, 16);
    const char *text = strchr(spec, ':');
    if (!text) return;
    text++;
    int wide = *text == 'w';
    if (wide) text++;
    size_t n = strlen(text), bytes = wide ? 2 * n : n;
    u8 *buf = rcn_galloc(ctx, bytes + 2, 1);
    for (size_t i = 0; i < n; i++) {
        if (wide) buf[2 * i] = (u8)text[i];
        else buf[i] = (u8)text[i];
    }
    u64 r = rcn_call_guest(ctx, ctx->base + rva, (u64)(uintptr_t)buf, bytes, 0, 0);
    fprintf(stderr, "rcn : appel %x(\"%s\", %zu) = %llx\n", rva, text, bytes, (unsigned long long)r);
}
static void k32_GetCommandLineW(RcnCtx *ctx) {
    call_test(ctx);
    make_command_line(ctx);
    RET((uintptr_t)ctx->proc->command_line_w);
}

// --- Tas et mémoire ---

static void k32_GetProcessHeap(RcnCtx *ctx) { RET(HANDLE_HEAP); }
// HeapAlloc(heap, flags, size)
static void k32_HeapAlloc(RcnCtx *ctx) {
    RET((uintptr_t)rcn_galloc(ctx, (size_t)A2, ((u32)A1 & HEAP_ZERO_MEMORY) != 0));
}
// HeapReAlloc(heap, flags, pointer, size) : sans HEAP_ZERO_MEMORY pour la partie ajoutée.
static void k32_HeapReAlloc(RcnCtx *ctx) { RET((uintptr_t)rcn_grealloc(ctx, PTR(A2), (size_t)A3)); }
static void k32_HeapFree(RcnCtx *ctx) {
    rcn_gfree(ctx, PTR(A2));
    RET(1);
}

// VirtualAlloc(address, size, type, protection) : la mémoire est réservée et engagée d'un coup ;
// engager une zone déjà réservée (adresse donnée) ne fait rien, réserver à une adresse imposée échoue.
static void k32_VirtualAlloc(RcnCtx *ctx) {
    if (!ctx->bits32) {
        RET(rcn_vm_alloc(A0, A1, (u32)A2, (u32)A3));
        return;
    }
    if (A0) {
        RET(((u32)A2 & (MEM_COMMIT | MEM_RESERVE)) == MEM_COMMIT ? A0 : 0);
        return;
    }
    size_t size = A1 ? (size_t)A1 : 1;
    // 32 bits : zone basse, alignée sur 64 Ko comme sous Windows. L'adresse du bloc d'origine est
    // rangée juste avant l'adresse rendue, pour VirtualFree.
    u8 *raw = rcn_low_alloc(size + 0x10000 + 16, 1);
    if (!raw) {
        RET(0);
        return;
    }
    u64 aligned = ((u64)(uintptr_t)raw + 16 + 0xFFFF) & ~0xFFFFull;
    wr64(aligned - 8, (u64)(uintptr_t)raw);
    RET(aligned);
}
// VirtualFree(address, size, type) : seule la libération complète rend la mémoire.
static void k32_VirtualFree(RcnCtx *ctx) {
    if (!ctx->bits32) {
        RET(rcn_vm_free(A0, A1, (u32)A2));
        return;
    }
    if ((u32)A2 & MEM_RELEASE) rcn_low_free(PTR(rd64(A0 - 8)));
    RET(1);
}
// VirtualProtect(address, size, protection, &old) : la mémoire du jeu est toujours lisible et modifiable.
static void k32_VirtualProtect(RcnCtx *ctx) {
    if (A3) wr32(A3, PAGE_EXECUTE_READWRITE);
    RET(1);
}
// VirtualQuery(address, &info, length) : MEMORY_BASIC_INFORMATION d'une page de l'image ou du tas.
static void k32_VirtualQuery(RcnCtx *ctx) {
    u64 info = A1;
    u64 page = A0 & ~0xFFFull;
    int in_image = A0 - ctx->base < ctx->image_size;
    if (ctx->bits32) { // sept champs de 4 octets
        if (A2 < 28) {
            RET(0);
            return;
        }
        wr32(info + 0, (u32)page);
        wr32(info + 4, (u32)(in_image ? ctx->base : page));
        wr32(info + 8, PAGE_EXECUTE_READWRITE);
        wr32(info + 12, (u32)(in_image ? ctx->base + ctx->image_size - page : 0x1000));
        wr32(info + 16, MEM_COMMIT);
        wr32(info + 20, PAGE_EXECUTE_READWRITE);
        wr32(info + 24, MEM_PRIVATE);
        RET(28);
        return;
    }
    if (A2 < 48) {
        RET(0);
        return;
    }
    if (!in_image && rcn_vm_query(A0, info)) {
        RET(48);
        return;
    }
    memset(PTR(info), 0, 48);
    wr64(info + 0, page);                                  // BaseAddress
    wr64(info + 8, in_image ? ctx->base : page);           // AllocationBase
    wr32(info + 16, PAGE_EXECUTE_READWRITE);               // AllocationProtect
    wr64(info + 24, in_image ? ctx->base + ctx->image_size - page : 0x1000); // RegionSize
    wr32(info + 32, MEM_COMMIT);                           // State
    wr32(info + 36, PAGE_EXECUTE_READWRITE);               // Protect
    wr32(info + 40, MEM_PRIVATE);                          // Type
    RET(48);
}

// HeapCreate / HeapDestroy : un seul tas, celui du processus.
static void k32_HeapCreate(RcnCtx *ctx) { RET(HANDLE_HEAP); }
static void k32_HeapDestroy(RcnCtx *ctx) { RET(1); }
// HeapSize(heap, flags, pointer) : taille utilisable du bloc (au moins celle demandée). 32 bits seulement.
#if defined(_WIN32)
#include <malloc.h>
#define usable_size _msize
#else
#include <malloc.h>
#define usable_size malloc_usable_size
#endif
static void k32_HeapSize(RcnCtx *ctx) {
    if (!A2) RET(~0ull);
    else RET(ctx->bits32 ? rcn_low_size(PTR(A2)) : usable_size(PTR(A2)));
}
// Tests de validité d'adresses : toute adresse non nulle est tenue pour valide.
static void k32_IsBadPtr(RcnCtx *ctx) { RET(A0 == 0); }

// GetSystemInfo(&info) : pages de 4 Ko ; jeux 32 bits : un processeur, adresses sous 2 Go ; 64 bits :
// les processeurs de l'hôte.
static void k32_GetSystemInfo(RcnCtx *ctx) {
    u64 info = A0;
    u32 psz = PTR_SIZE(ctx);
    memset(PTR(info), 0, 24 + 3 * psz);
    wr16(info, ctx->bits32 ? 0 : 9); // x86 ou x64
    wr32(info + 4, 0x1000);
    wrp(ctx, info + 8, 0x10000);
    wrp(ctx, info + 8 + psz, ctx->bits32 ? 0x7FFEFFFFull : 0x7FFFFFFEFFFFull);
    u32 cpus = ctx->bits32 ? 1 : rcn_cpu_count();
    wrp(ctx, info + 8 + 2 * psz, cpus >= 64 ? ~0ull : (1ull << cpus) - 1); // masque des processeurs actifs
    u64 rest = info + 8 + 3 * psz;
    wr32(rest, cpus);         // nombre de processeurs
    wr32(rest + 4, ctx->bits32 ? 586 : 8664);
    wr32(rest + 8, 0x10000);  // granularité de VirtualAlloc
    wr16(rest + 12, 6);
    wr16(rest + 14, 0x0F0B);
}
// GetVersionExA(&info) : Windows XP SP3 (5.1.2600), ce que ces jeux visaient.
static void k32_GetVersionExA(RcnCtx *ctx) {
    u64 info = A0;
    u32 size = rd32(info);
    if (size < 148) {
        RET(0);
        return;
    }
    memset(PTR(info + 4), 0, size - 4);
    wr32(info + 4, 5);
    wr32(info + 8, 1);
    wr32(info + 12, 2600);
    wr32(info + 16, 2); // VER_PLATFORM_WIN32_NT
    memcpy(PTR(info + 20), "Service Pack 3", 15);
    if (size >= 156) wr16(info + 148, 3); // wServicePackMajor
    RET(1);
}
// GetStartupInfoA(&info) : aucune consigne de lancement (STARTUPINFO à zéro, sa taille en tête).
static void k32_GetStartupInfoA(RcnCtx *ctx) {
    u32 size = ctx->bits32 ? 68 : 104;
    memset(PTR(A0), 0, size);
    wr32(A0, size);
}
static void k32_SetHandleCount(RcnCtx *ctx) { RET(A0); }
static void k32_SetStdHandle(RcnCtx *ctx) { RET(1); }
// Environnement vide : un bloc de deux zéros (quatre octets couvrent aussi la version UTF-16).
static void k32_GetEnvironmentStrings(RcnCtx *ctx) { RET((uintptr_t)rcn_galloc(ctx, 4, 1)); }
static void k32_FreeEnvironmentStrings(RcnCtx *ctx) {
    rcn_gfree(ctx, PTR(A0));
    RET(1);
}
static void k32_TerminateProcess(RcnCtx *ctx) { rcn_exit(ctx, (u32)A1); }

// Opérations atomiques importées par les programmes 32 bits (en 64 bits, le compilateur les insère).
static void k32_InterlockedExchange(RcnCtx *ctx) { RET(__atomic_exchange_n((u32 *)PTR(A0), (u32)A1, __ATOMIC_SEQ_CST)); }
static void k32_InterlockedIncrement(RcnCtx *ctx) { RET(__atomic_add_fetch((u32 *)PTR(A0), 1, __ATOMIC_SEQ_CST)); }
static void k32_InterlockedDecrement(RcnCtx *ctx) { RET(__atomic_sub_fetch((u32 *)PTR(A0), 1, __ATOMIC_SEQ_CST)); }
static void k32_InterlockedExchangeAdd(RcnCtx *ctx) { RET(__atomic_fetch_add((u32 *)PTR(A0), (u32)A1, __ATOMIC_SEQ_CST)); }
// InterlockedCompareExchange(&target, exchange, comparand) : renvoie l'ancienne valeur.
static void k32_InterlockedCompareExchange(RcnCtx *ctx) {
    u32 expected = (u32)A2;
    __atomic_compare_exchange_n((u32 *)PTR(A0), &expected, (u32)A1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    RET(expected);
}

// --- Temps ---

static void k32_GetTickCount(RcnCtx *ctx) { RET((u32)(rcn_now_ns() / 1000000)); }
static void k32_GetTickCount64(RcnCtx *ctx) { RET(rcn_now_ns() / 1000000); }
// Compteur de performances : sur ARM64, le compteur matériel (CNTVCT_EL0, lisible en mode utilisateur, de
// fréquence CNTFRQ_EL0) — quelques nanosecondes, quand clock_gettime en coûte des dizaines (les jeux .NET
// l'appellent sans cesse) ; ailleurs, nanosecondes de l'horloge monotone.
static void k32_QueryPerformanceCounter(RcnCtx *ctx) {
#if defined(__aarch64__)
    u64 v;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
    wr64(A0, v);
#else
    wr64(A0, rcn_now_ns());
#endif
    RET(1);
}
static void k32_QueryPerformanceFrequency(RcnCtx *ctx) {
#if defined(__aarch64__)
    u64 f;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    wr64(A0, f);
#else
    wr64(A0, 1000000000ull);
#endif
    RET(1);
}
static void k32_Sleep(RcnCtx *ctx) { rcn_sleep_ms((u32)A0); }
// GetSystemTimeAsFileTime(&filetime) : centaines de nanosecondes depuis le 1er janvier 1601.
static void k32_GetSystemTimeAsFileTime(RcnCtx *ctx) {
    wr64(A0, ((u64)time(NULL) + 11644473600ull) * 10000000ull);
}

// --- Texte ---
// Toutes les pages de codes sont traitées comme de l'UTF-8 (identique pour l'ASCII).

static void k32_GetACP(RcnCtx *ctx) { RET(1252); }
static void k32_GetOEMCP(RcnCtx *ctx) { RET(437); }
static void k32_GetUserDefaultLangID(RcnCtx *ctx) { RET(0x0409); } // anglais (États-Unis)
static void k32_GetLocaleInfoA(RcnCtx *ctx) { RET(0); }
// GetCPInfo(code_page, &info) : un octet par caractère, pas d'octets de tête.
static void k32_GetCPInfo(RcnCtx *ctx) {
    memset(PTR(A1), 0, 20);
    wr32(A1, 1);
    wr8(A1 + 4, '?');
    RET(1);
}
// Classe d'un caractère pour GetStringType (CT_CTYPE1) : seuls les caractères ASCII sont classés.
static u16 char_type(u32 c) {
    enum { UPPER = 1, LOWER = 2, DIGIT = 4, SPACE = 8, PUNCT = 0x10, CNTRL = 0x20, BLANK = 0x40, XDIGIT = 0x80, ALPHA = 0x100, DEFINED = 0x200 };
    u16 t = DEFINED;
    if (c >= 128) return t;
    if (c >= 'A' && c <= 'Z') t |= UPPER | ALPHA;
    if (c >= 'a' && c <= 'z') t |= LOWER | ALPHA;
    if (c >= '0' && c <= '9') t |= DIGIT | XDIGIT;
    if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) t |= XDIGIT;
    if (c == ' ' || (c >= 9 && c <= 13)) t |= SPACE;
    if (c == ' ' || c == '\t') t |= BLANK;
    if (c < 32 || c == 127) t |= CNTRL;
    if (c > 32 && c < 127 && !(t & (ALPHA | DIGIT))) t |= PUNCT;
    return t;
}
// GetStringTypeW(type, source, count, &types) ; GetStringTypeA(locale, type, source, count, &types).
static void string_type(RcnCtx *ctx, u64 source, i32 count, u64 out, int wide) {
    if (count < 0) {
        count = 0;
        while (wide ? rd16(source + 2 * (u64)count) : rd8(source + (u64)count)) count++;
        count++;
    }
    for (i32 i = 0; i < count; i++) wr16(out + 2 * (u64)i, char_type(wide ? rd16(source + 2 * (u64)i) : rd8(source + (u64)i)));
    RET(1);
}
static void k32_GetStringTypeW(RcnCtx *ctx) { string_type(ctx, A1, (i32)A2, A3, 1); }
static void k32_GetStringTypeA(RcnCtx *ctx) { string_type(ctx, A2, (i32)A3, STK(4), 0); }
// LCMapString(locale, flags, source, source_length, destination, destination_size) : changement de
// casse des lettres ASCII (LCMAP_LOWERCASE 0x100, LCMAP_UPPERCASE 0x200) ; taille 0 : seulement compter.
static void map_string(RcnCtx *ctx, int wide) {
    u32 flags = (u32)A1;
    u64 source = A2, destination = STK(4);
    i32 count = (i32)A3, size = (i32)STK(5);
    u32 width = wide ? 2 : 1;
    if (count < 0) {
        count = 0;
        while (wide ? rd16(source + 2 * (u64)count) : rd8(source + (u64)count)) count++;
        count++;
    }
    if (size == 0) {
        RET(count);
        return;
    }
    if (count > size) {
        LAST_ERROR(ctx) = ERROR_INSUFFICIENT_BUFFER;
        RET(0);
        return;
    }
    for (i32 i = 0; i < count; i++) {
        u32 c = wide ? rd16(source + 2 * (u64)i) : rd8(source + (u64)i);
        if ((flags & 0x100) && c >= 'A' && c <= 'Z') c += 32;
        if ((flags & 0x200) && c >= 'a' && c <= 'z') c -= 32;
        if (wide) wr16(destination + (u64)i * width, (u16)c);
        else wr8(destination + (u64)i, (u8)c);
    }
    RET(count);
}
static void k32_LCMapStringW(RcnCtx *ctx) { map_string(ctx, 1); }
static void k32_LCMapStringA(RcnCtx *ctx) { map_string(ctx, 0); }

static void k32_lstrlenA(RcnCtx *ctx) { RET(A0 ? strlen(STR(A0)) : 0); }
static void k32_lstrlenW(RcnCtx *ctx) {
    const u16 *s = PTR(A0);
    u32 n = 0;
    while (s && s[n]) n++;
    RET(n);
}
// MultiByteToWideChar(code_page, flags, source, source_length, destination, destination_size)
// Longueur -1 : jusqu'au zéro final, compris dans le résultat. Taille 0 : seulement compter.
// Page de codes UTF-8 (65001) : conversion UTF-8. Toute autre (celle du système, 1252…) : un octet
// par caractère, de même valeur (Latin-1) — ce qu'attend le CRT, qui convertit les 256 octets un à un.
enum { CP_UTF8 = 65001 };
static void k32_MultiByteToWideChar(RcnCtx *ctx) {
    const char *source = STR(A2);
    i32 source_length = (i32)A3;
    u64 destination = STK(4);
    u32 size = (u32)STK(5);
    int utf8 = (u32)A0 == CP_UTF8;
    size_t len = source_length < 0 ? strlen(source) + 1 : (size_t)source_length;
    size_t count = utf8 ? rcn_utf8_to_utf16(source, len, NULL) : len;
    if (size == 0) {
        RET(count);
        return;
    }
    if (count > size) {
        LAST_ERROR(ctx) = ERROR_INSUFFICIENT_BUFFER;
        RET(0);
        return;
    }
    if (utf8) {
        rcn_utf8_to_utf16(source, len, PTR(destination));
    } else {
        for (size_t i = 0; i < len; i++) wr16(destination + 2 * i, (u8)source[i]);
    }
    RET(count);
}
// WideCharToMultiByte(code_page, flags, source, source_length, destination, destination_size, default, &used)
static void k32_WideCharToMultiByte(RcnCtx *ctx) {
    const u16 *source = PTR(A2);
    i32 source_length = (i32)A3;
    u64 destination = STK(4);
    u32 size = (u32)STK(5);
    u64 used = STK(7);
    if (used) wr32(used, 0);
    size_t len = 0;
    if (source_length < 0) {
        while (source[len]) len++;
        len++;
    } else {
        len = (size_t)source_length;
    }
    if ((u32)A0 != CP_UTF8) {
        // Un octet par caractère ; « ? » pour ceux qui n'en ont pas.
        if (size == 0) {
            RET(len);
        } else if (len > size) {
            LAST_ERROR(ctx) = ERROR_INSUFFICIENT_BUFFER;
            RET(0);
        } else {
            for (size_t i = 0; i < len; i++) {
                u16 c = rd16((u64)(uintptr_t)source + 2 * i);
                wr8(destination + i, c < 256 ? (u8)c : '?');
                if (c >= 256 && used) wr32(used, 1);
            }
            RET(len);
        }
        return;
    }
    // UTF-8 : par morceaux séparés par les zéros que le texte peut contenir.
    RcnBuf out = {0};
    for (size_t i = 0; i < len;) {
        size_t n = 0;
        while (i + n < len && source[i + n]) n++;
        char *part = rcn_utf16_to_utf8(source + i, (i64)n);
        if (part) rcn_buf_put(&out, part, strlen(part));
        free(part);
        i += n;
        if (i < len) {
            rcn_buf_put(&out, "", 1);
            i++;
        }
    }
    if (size == 0) {
        RET(out.len);
    } else if (out.len > size) {
        LAST_ERROR(ctx) = ERROR_INSUFFICIENT_BUFFER;
        RET(0);
    } else {
        if (out.len) memcpy(PTR(destination), out.data, out.len);
        RET(out.len);
    }
    free(out.data);
}
const RcnShim rcn_kernel32[] = {
    {"FreeEnvironmentStringsA", k32_FreeEnvironmentStrings, S32(1)},
    {"FreeEnvironmentStringsW", k32_FreeEnvironmentStrings, S32(1)},
    {"GetACP", k32_GetACP, S32(0)},
    {"GetCPInfo", k32_GetCPInfo, S32(2)},
    {"GetEnvironmentStrings", k32_GetEnvironmentStrings, S32(0)},
    {"GetEnvironmentStringsW", k32_GetEnvironmentStrings, S32(0)},
    {"GetLocaleInfoA", k32_GetLocaleInfoA, S32(4)},
    {"GetOEMCP", k32_GetOEMCP, S32(0)},
    {"GetProcAddress", k32_GetProcAddress, S32(2)},
    {"GetStartupInfoA", k32_GetStartupInfoA, S32(1)},
    {"GetStringTypeA", k32_GetStringTypeA, S32(5)},
    {"GetStringTypeW", k32_GetStringTypeW, S32(4)},
    {"GetSystemInfo", k32_GetSystemInfo, S32(1)},
    {"GetNativeSystemInfo", k32_GetSystemInfo, S32(1)},
    {"GetUserDefaultLangID", k32_GetUserDefaultLangID, S32(0)},
    {"GetVersionExA", k32_GetVersionExA, S32(1)},
    {"HeapCreate", k32_HeapCreate, S32(3)},
    {"HeapDestroy", k32_HeapDestroy, S32(1)},
    {"HeapSize", k32_HeapSize, S32(3)},
    {"InterlockedCompareExchange", k32_InterlockedCompareExchange, S32(3)},
    {"InterlockedDecrement", k32_InterlockedDecrement, S32(1)},
    {"InterlockedExchange", k32_InterlockedExchange, S32(2)},
    {"InterlockedExchangeAdd", k32_InterlockedExchangeAdd, S32(2)},
    {"InterlockedIncrement", k32_InterlockedIncrement, S32(1)},
    {"IsBadCodePtr", k32_IsBadPtr, S32(1)},
    {"IsBadReadPtr", k32_IsBadPtr, S32(2)},
    {"IsBadWritePtr", k32_IsBadPtr, S32(2)},
    {"LCMapStringA", k32_LCMapStringA, S32(6)},
    {"LCMapStringW", k32_LCMapStringW, S32(6)},
    {"LCMapStringEx", k32_LCMapStringW},
    {"LoadLibraryA", k32_LoadLibraryA, S32(1)},
    {"LoadLibraryExA", k32_LoadLibraryA, S32(3)},
    {"SetHandleCount", k32_SetHandleCount, S32(1)},
    {"SetStdHandle", k32_SetStdHandle, S32(2)},
    {"TerminateProcess", k32_TerminateProcess, S32(2)},
    {"ExitProcess", k32_ExitProcess, S32(1)},
    {"GetCommandLineA", k32_GetCommandLineA, S32(0)},
    {"GetCommandLineW", k32_GetCommandLineW, S32(0)},
    {"GetCurrentDirectoryA", k32_GetCurrentDirectoryA},
    {"GetCurrentDirectoryW", k32_GetCurrentDirectoryW},
    {"GetCurrentProcessId", k32_GetCurrentProcessId, S32(0)},
    {"GetLastError", k32_GetLastError, S32(0)},
    {"GetModuleFileNameA", k32_GetModuleFileNameA, S32(3)},
    {"GetModuleFileNameW", k32_GetModuleFileNameW, S32(3)},
    {"GetModuleHandleA", k32_GetModuleHandleA, S32(1)},
    {"GetModuleHandleW", k32_GetModuleHandleW, S32(1)},
    {"GetProcessHeap", k32_GetProcessHeap, S32(0)},
    {"GetStdHandle", k32_GetStdHandle, S32(1)},
    {"GetSystemTimeAsFileTime", k32_GetSystemTimeAsFileTime, S32(1)},
    {"GetTickCount", k32_GetTickCount, S32(0)},
    {"GetTickCount64", k32_GetTickCount64},
    {"HeapAlloc", k32_HeapAlloc, S32(3)},
    {"HeapFree", k32_HeapFree, S32(3)},
    {"HeapReAlloc", k32_HeapReAlloc, S32(4)},
    {"IsDebuggerPresent", k32_IsDebuggerPresent, S32(0)},
    {"MultiByteToWideChar", k32_MultiByteToWideChar, S32(6)},
    {"OutputDebugStringA", k32_OutputDebugStringA, S32(1)},
    {"QueryPerformanceCounter", k32_QueryPerformanceCounter, S32(1)},
    {"QueryPerformanceFrequency", k32_QueryPerformanceFrequency, S32(1)},
    {"SetLastError", k32_SetLastError, S32(1)},
    {"SetUnhandledExceptionFilter", k32_SetUnhandledExceptionFilter, S32(1)},
    {"Sleep", k32_Sleep, S32(1)},
    {"VirtualAlloc", k32_VirtualAlloc, S32(4)},
    {"VirtualFree", k32_VirtualFree, S32(3)},
    {"VirtualProtect", k32_VirtualProtect, S32(4)},
    {"VirtualQuery", k32_VirtualQuery, S32(3)},
    {"WideCharToMultiByte", k32_WideCharToMultiByte, S32(8)},
    {"lstrlenA", k32_lstrlenA, S32(1)},
    {"lstrlenW", k32_lstrlenW, S32(1)},
};
const size_t rcn_kernel32_count = sizeof rcn_kernel32 / sizeof rcn_kernel32[0];
