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
// IsWow64Process(processus, &oui) : jeu 32 bits exécuté tel quel, pas de WOW64.
static void k32_IsWow64Process(RcnCtx *ctx) {
    if (A1) wr32(A1, 0);
    RET(1);
}
// Aucun filtre d'exceptions n'est appelé : une faute du jeu arrête son processus.
static void k32_SetUnhandledExceptionFilter(RcnCtx *ctx) { RET(0); }
// Bibliothèque demandée par son nom (GetModuleHandle, LoadLibrary) : connue si l'hôte en fournit des
// fonctions. Son handle est l'adresse d'une copie de son nom, que GetProcAddress retrouve.
#if defined(RCN_HAS_DXVK) || defined(_WIN32)
int rcn_dxvk_enabled(void);
static int d3d9_available(void) { return rcn_dxvk_enabled(); }
#else
static int d3d9_available(void) { return 0; }
#endif
u64 rcn_module_handle(RcnCtx *ctx, const char *name) {
    static const char *const known[] = {"kernel32", "user32", "gdi32", "winmm", "ddraw", "dsound", "dinput", "advapi32",
                                        "ole32", "bcrypt", "ntdll", "kernelbase", "oleaut32", "shell32", "sdl2", "d3d11", "dxgi", "xinput1_4", "xinput1_3",
                                        "xinput9_1_0", "security", "secur32", "msvcrt", "ucrtbase", "dbghelp", "shlwapi",
                                        "steam_api", "steam_api64", "d3d9", "mf", "mfplat", "mfplay", "mfreadwrite"};
    RcnProcess *proc = ctx->proc;
    size_t k = 0, n = sizeof known / sizeof known[0];
    // Media Foundation annoncée seulement avec RCN_MF=1 : sans décodeur, chaque film est refusé, et les
    // écrans de chargement d'Unreal Engine rouvrent leur film en boucle (des centaines de lectures du
    // fichier pendant un chargement, Clair Obscur sur téléphone). Absente, le jeu se passe de films.
    static int with_mf = -1;
    if (with_mf < 0) with_mf = getenv("RCN_MF") != NULL;
    if (!with_mf) n -= 4; // "mf", "mfplat", "mfplay", "mfreadwrite" sont les quatre derniers
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
    // Direct3D 11 et DXGI n'existent que pour les jeux 64 bits : un jeu 32 bits qui les cherche (Unity) se
    // rabat sur Direct3D 9, comme sous un Windows qui n'en a pas.
    if (ctx->bits32 && (!strcmp(known[k], "d3d11") || !strcmp(known[k], "dxgi"))) return 0;
    // Direct3D 9 chargé à l'exécution (Unity) : celui de DXVK, comme pour un import direct (rcn_win32.c).
    if (!strcmp(known[k], "d3d9") && !(ctx->bits32 && d3d9_available())) return 0;
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
    // RCN_HIDE_DLLS=<nom>,<nom>… : bibliothèques refusées comme absentes (préfixe du nom, sans la casse),
    // pour essayer un jeu sans l'une de ses bibliothèques facultatives.
    const char *hide = getenv("RCN_HIDE_DLLS");
    for (const char *p = hide; p && *p;) {
        const char *end = strchr(p, ',');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n && strncasecmp(name, p, n) == 0) {
            LAST_ERROR(ctx) = 126; // ERROR_MOD_NOT_FOUND
            rcn_log("LoadLibrary : %s cachée (RCN_HIDE_DLLS)", name);
            return 0;
        }
        p = end ? end + 1 : p + n;
    }
    u64 h = handle_of(ctx, name);
    // DLL du jeu traduite (32 bits aussi : chargeur des DLL 32 bits relocalisées sous 2 Go).
    if (!h) h = rcn_load_dll(ctx, name);
    if (!h) {
        LAST_ERROR(ctx) = 126; // ERROR_MOD_NOT_FOUND
        static int told;
        // Bibliothèques facultatives des fabricants (pilotes NVIDIA et AMD, mise à jour de PhysX, mesure de
        // PhysX) : les jeux continuent sans elles, refus sans bruit.
        static const char *const optional[] = {"physxupdateloader", "agperfmon", "nvcpl", "atimgpud", "nvapi", "atiadlxx", "atiadlxy"};
        int quiet = 0;
        for (size_t i = 0; i < sizeof optional / sizeof optional[0] && !quiet; i++) quiet = strncasecmp(name, optional[i], strlen(optional[i])) == 0;
        if (!quiet && told++ < 64) rcn_log("LoadLibrary : %s introuvable", name);
        // RCN_LL_STACK=1 : qui la demande (pile du jeu), une fois par adresse de retour.
        static int stack_log = -1;
        if (stack_log < 0) stack_log = getenv("RCN_LL_STACK") != NULL;
        if (stack_log) rcn_guest_stack_once(ctx, name);
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
    // RCN_GPA_LOG=1 : fonctions demandées aux bibliothèques fournies par l'hôte (trouvées ou non).
    static int gpa_log = -1;
    if (gpa_log < 0) gpa_log = getenv("RCN_GPA_LOG") != NULL;
    if (gpa_log && dll) rcn_log("GetProcAddress : %s!%s %s", dll, fname, result ? "fournie" : "absente");
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
    static int log = -1;
    if (log < 0) log = getenv("RCN_ODS_LOG") != NULL;
    if (log && A0) rcn_log("OutputDebugString : %s", STR(A0));
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

// --- psapi (et ses noms « K32… » de kernel32) : le processus du jeu et ses modules ---
// Module du jeu désigné par un handle (NULL : l'exécutable), ou NULL s'il n'en est pas un.
static RcnLoaded *psapi_module(RcnCtx *ctx, u64 module) {
    if (!module) return ctx->proc->loaded_count ? &ctx->proc->loaded[0] : NULL;
    RcnLoaded *l = rcn_loaded_at(ctx->proc, module);
    return l && (u64)(uintptr_t)l->image == module ? l : NULL;
}
// EnumProcesses(&identifiants, taille, &utilisée) : le seul processus, celui du jeu (GetCurrentProcessId).
static void ps_EnumProcesses(RcnCtx *ctx) {
    if (A0 && (u32)A1 >= 4) wr32(A0, 1000);
    if (A2) wr32(A2, 4);
    RET(1);
}
// EnumProcessModules(processus, &modules, taille, &utilisée) : l'exécutable puis ses DLL traduites.
static void ps_EnumProcessModules(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    u32 size = PTR_SIZE(ctx), room = A1 ? (u32)A2 / size : 0;
    for (u32 i = 0; i < proc->loaded_count && i < room; i++) wrp(ctx, A1 + (u64)i * size, (u64)(uintptr_t)proc->loaded[i].image);
    if (STK(3)) wr32(STK(3), proc->loaded_count * size);
    RET(1);
}
// GetModuleBaseNameA / W(processus, module, tampon, taille) : nom du fichier du module, sans dossier.
static void module_base_name(RcnCtx *ctx, int wide) {
    RcnLoaded *l = psapi_module(ctx, A1);
    if (!l) {
        LAST_ERROR(ctx) = 126; // ERROR_MOD_NOT_FOUND
        RET(0);
        return;
    }
    const char *name = l == &ctx->proc->loaded[0] ? ctx->proc->exe_name : l->name, *base = name;
    for (const char *c = name; *c; c++) {
        if (*c == '/' || *c == '\\') base = c + 1;
    }
    RET(copy_text(ctx, base, A2, (u32)A3, wide));
}
static void ps_GetModuleBaseNameA(RcnCtx *ctx) { module_base_name(ctx, 0); }
static void ps_GetModuleBaseNameW(RcnCtx *ctx) { module_base_name(ctx, 1); }
// GetModuleFileNameExA / W(processus, module, tampon, taille) : chemin complet, comme GetModuleFileName.
static void module_file_name_ex(RcnCtx *ctx, int wide) {
    RcnLoaded *l = psapi_module(ctx, A1);
    if (!l) {
        LAST_ERROR(ctx) = 126;
        RET(0);
        return;
    }
    char *path = l == &ctx->proc->loaded[0] ? exe_path(ctx->proc) : guest_file(ctx->proc, l->name);
    RET(copy_text(ctx, path, A2, (u32)A3, wide));
    free(path);
}
static void ps_GetModuleFileNameExA(RcnCtx *ctx) { module_file_name_ex(ctx, 0); }
static void ps_GetModuleFileNameExW(RcnCtx *ctx) { module_file_name_ex(ctx, 1); }
// GetModuleInformation(processus, module, &MODULEINFO, taille) : base, taille de l'image et point d'entrée
// (trois champs de la taille d'une adresse, la taille sur 4 octets).
static void ps_GetModuleInformation(RcnCtx *ctx) {
    RcnLoaded *l = psapi_module(ctx, A1);
    u32 size = PTR_SIZE(ctx);
    if (!l || !A2 || (u32)A3 < 3 * size) {
        LAST_ERROR(ctx) = l ? ERROR_INSUFFICIENT_BUFFER : 126;
        RET(0);
        return;
    }
    u64 base = (u64)(uintptr_t)l->image;
    u32 entry = rd32(base + rd32(base + 0x3C) + 0x28); // IMAGE_OPTIONAL_HEADER.AddressOfEntryPoint
    wrp(ctx, A2, base);
    wr32(A2 + size, l->size);
    wrp(ctx, A2 + 2 * size, entry ? base + entry : 0);
    RET(1);
}
// Dossier courant tel que le jeu le voit (« C:\data » pour <jeu>/data).
static const char *guest_cwd(RcnCtx *ctx, char *buf, size_t size) {
    const char *cwd = ctx->proc->cwd, *root = ctx->proc->game_dir;
    size_t n = strlen(root);
    if (cwd && !strncmp(cwd, root, n) && (cwd[n] == '/' || !cwd[n]))
        snprintf(buf, size, "C:\\%s", cwd + n + (cwd[n] == '/' ? 1 : 0));
    else
        snprintf(buf, size, "C:\\");
    for (char *p = buf; *p; p++)
        if (*p == '/') *p = '\\';
    return buf;
}
// GetCurrentDirectoryA / W(size, buffer)
static void k32_GetCurrentDirectoryA(RcnCtx *ctx) {
    char guest[4096];
    guest_cwd(ctx, guest, sizeof guest);
    u32 len = (u32)strlen(guest);
    RET((u32)A0 < len + 1 ? len + 1 : copy_text(ctx, guest, A1, (u32)A0, 0));
}
static void k32_GetCurrentDirectoryW(RcnCtx *ctx) {
    char guest[4096];
    guest_cwd(ctx, guest, sizeof guest);
    u32 len = (u32)strlen(guest);
    RET((u32)A0 < len + 1 ? len + 1 : copy_text(ctx, guest, A1, (u32)A0, 1));
}
// SetCurrentDirectoryA / W(chemin) : vrai si le dossier existe ; les chemins relatifs suivants s'y rattachent.
static void k32_SetCurrentDirectoryA(RcnCtx *ctx) { RET(rcn_set_current_directory(ctx, A0 ? STR(A0) : NULL)); }
static void k32_SetCurrentDirectoryW(RcnCtx *ctx) {
    char *path = A0 ? rcn_utf16_to_utf8(PTR(A0), -1) : NULL;
    if (!path) {
        RET(0);
        return;
    }
    RET(rcn_set_current_directory(ctx, path));
    free(path);
}

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
        // Bloc de la zone basse (pile d'un thread, VirtualAlloc, tas) : la région va jusqu'à sa fin, et son
        // allocation commence à son début. Le ramasse-miettes de Mono (Boehm) trouve ainsi le haut de la pile
        // d'un thread ; avec une région de 4 Ko, il en ignorait le reste et libérait des objets vivants.
        void *block = NULL;
        size_t block_size = 0;
        u64 alloc = in_image ? ctx->base : page, size = in_image ? ctx->base + ctx->image_size - page : 0x1000;
        if (!in_image && rcn_low_block_range((void *)(uintptr_t)A0, &block, &block_size)) {
            u64 start = (u64)(uintptr_t)block, end = start + block_size;
            alloc = start & ~0xFFFull;
            size = ((end + 0xFFF) & ~0xFFFull) - page;
        }
        wr32(info + 0, (u32)page);
        wr32(info + 4, (u32)alloc);
        wr32(info + 8, PAGE_EXECUTE_READWRITE);
        wr32(info + 12, (u32)size);
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
// GlobalSize / LocalSize(bloc) : taille du bloc de GlobalAlloc (même allocateur que HeapAlloc).
static void k32_GlobalSize(RcnCtx *ctx) {
    if (!A0) RET(0);
    else RET(ctx->bits32 ? rcn_low_size(PTR(A0)) : usable_size(PTR(A0)));
}
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
    // Jeux DirectDraw (32 bits anciens) : un processeur, comme les PC de leur époque ; les autres : tous.
    u32 cpus = ctx->bits32 && !rcn_user32_modern ? 1 : rcn_cpu_count();
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
// GetStartupInfoW(&info) : la même chose en Unicode (le CRT de Unity, notamment, la demande en 32 bits).
static void k32_GetStartupInfoW(RcnCtx *ctx) {
    u32 size = ctx->bits32 ? 68 : 104;
    memset(PTR(A0), 0, size);
    wr32(A0, size);
}
// HeapSetInformation(tas, classe, information, taille) : rien à régler, succès.
static void k32_HeapSetInformation(RcnCtx *ctx) { RET(1); }
// FlushInstructionCache(processus, adresse, taille) : le code traduit est déjà à jour.
static void k32_FlushInstructionCache(RcnCtx *ctx) { RET(1); }
// EnumSystemLocalesA(rappel, drapeaux) : une seule langue installée, l'anglais des États-Unis.
static void k32_EnumSystemLocalesA(RcnCtx *ctx) {
    if (A0) {
        static char locale[] = "0409";
        rcn_call_guest(ctx, A0, (u64)(uintptr_t)locale, 0, 0, 0);
    }
    RET(1);
}
// FileTimeToDosDateTime(&filetime, &date, &heure) : mots DOS (date et heure locales).
static void k32_FileTimeToDosDateTime(RcnCtx *ctx) {
    if (!A0) {
        RET(0);
        return;
    }
    i64 seconds = (i64)(rd64(A0) / 10000000ull) - 11644473600ll;
    time_t t = (time_t)seconds;
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    if (A1) wr16(A1, (u16)(((tmv.tm_year + 1900 - 1980) << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday));
    if (A2) wr16(A2, (u16)((tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2)));
    RET(1);
}
// GetDateFormatA/W(locale, drapeaux, &systemtime, format, tampon, taille) : « jj/mm/aaaa ».
static void date_format(RcnCtx *ctx, int wide) {
    u64 st = A2;
    char text[32];
    if (st) snprintf(text, sizeof text, "%02u/%02u/%04u", rd16(st + 2), rd16(st + 6), rd16(st));
    else snprintf(text, sizeof text, "01/01/2026");
    u32 n = copy_text(ctx, text, STK(4), (u32)STK(5), wide);
    RET(n ? n + 1 : 0);
}
static void k32_GetDateFormatA(RcnCtx *ctx) { date_format(ctx, 0); }
static void k32_GetDateFormatW(RcnCtx *ctx) { date_format(ctx, 1); }
// GetTimeFormatA/W(locale, drapeaux, &systemtime, format, tampon, taille) : « hh:mm:ss ».
static void time_format(RcnCtx *ctx, int wide) {
    u64 st = A2;
    char text[32];
    if (st) snprintf(text, sizeof text, "%02u:%02u:%02u", rd16(st + 8), rd16(st + 10), rd16(st + 12));
    else snprintf(text, sizeof text, "00:00:00");
    u32 n = copy_text(ctx, text, STK(4), (u32)STK(5), wide);
    RET(n ? n + 1 : 0);
}
static void k32_GetTimeFormatA(RcnCtx *ctx) { time_format(ctx, 0); }
static void k32_GetTimeFormatW(RcnCtx *ctx) { time_format(ctx, 1); }
// lstrcpyA / lstrcpynA / lstrcpynW : copies de chaînes (lstrcpyn s'arrête avant la taille donnée).
static void k32_lstrcpyA(RcnCtx *ctx) {
    if (A0 && A1) strcpy((char *)PTR(A0), (const char *)PTR(A1));
    RET(A0);
}
static void k32_lstrcpynA(RcnCtx *ctx) {
    u32 n = (u32)A2;
    if (A0 && A1 && n) {
        char *dst = PTR(A0);
        const char *src = PTR(A1);
        u32 i = 0;
        for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
        dst[i] = 0;
    }
    RET(A0);
}
static void k32_lstrcpynW(RcnCtx *ctx) {
    u32 n = (u32)A2;
    if (A0 && A1 && n) {
        u16 *dst = PTR(A0);
        const u16 *src = PTR(A1);
        u32 i = 0;
        for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
        dst[i] = 0;
    }
    RET(A0);
}
// --- FLS (stockage local de fibre), par thread ---
static _Thread_local u64 fls_values[64];
static volatile u32 fls_next;
static void k32_FlsAlloc(RcnCtx *ctx) {
    u32 i = __atomic_fetch_add(&fls_next, 1, __ATOMIC_RELAXED);
    RET(i < 64 ? (u64)(i + 1) : 0xFFFFFFFFu); // FLS_OUT_OF_INDEXES
}
static void k32_FlsGetValue(RcnCtx *ctx) {
    u32 i = (u32)A0;
    RET(i >= 1 && i <= 64 ? fls_values[i - 1] : 0);
}
static void k32_FlsSetValue(RcnCtx *ctx) {
    u32 i = (u32)A0;
    if (i >= 1 && i <= 64) fls_values[i - 1] = A1;
    // Le CRT (UCRT) considère un retour nul comme un échec : c'est la valeur précédente sous Windows, mais
    // elle vaut zéro à la première pose (R6016 « not enough space for thread data » sinon).
    RET(1);
}
static void k32_FlsFree(RcnCtx *ctx) { RET(1); }

// --- Langue : anglais des États-Unis (GetLocaleInfoA/W) ---
static const char *locale_info(u32 type) {
    static const struct {
        u32 type;
        const char *text;
    } table[] = {
        {0x1, "0409"}, {0x2, "English (United States)"}, {0x3, "ENU"}, {0x4, "English"}, {0x5, "1"},
        {0x6, "United States"}, {0x7, "USA"}, {0x8, "United States"}, {0x9, "0409"}, {0xA, "1"},
        {0xB, "437"}, {0xC, ","}, {0xD, "1"}, {0xE, "."}, {0xF, ","}, {0x10, "3;0"}, {0x11, "2"},
        {0x12, "1"}, {0x13, "0123456789"}, {0x14, "$"}, {0x15, "USD"}, {0x16, "."}, {0x17, ","},
        {0x18, "3;0"}, {0x19, "2"}, {0x1A, "2"}, {0x1B, "0"}, {0x1C, "0"}, {0x1D, "/"}, {0x1E, ":"},
        {0x1F, "M/d/yyyy"}, {0x20, "dddd, MMMM d, yyyy"}, {0x21, "0"}, {0x22, "0"}, {0x23, "0"},
        {0x24, "1"}, {0x25, "0"}, {0x26, "0"}, {0x27, "0"}, {0x28, "AM"}, {0x29, "PM"},
        {0x59, "en"}, {0x5A, "US"}, {0x100C, "0"}, {0x1003, "0"}, {0x1002, "1"},
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (table[i].type == type) return table[i].text;
    return NULL;
}
static void locale_info_result(RcnCtx *ctx, int wide) {
    u32 type = (u32)A1;
    int number = (type & 0x20000000u) != 0; // LOCALE_RETURN_NUMBER
    const char *text = locale_info(type & ~0x20000000u);
    if (!text) {
        RET(0);
        return;
    }
    if (number) {
        if (A2) wr32(A2, (u32)strtoul(text, NULL, 10));
        RET(4);
        return;
    }
    u32 n = copy_text(ctx, text, A2, (u32)A3, wide);
    RET(n ? n + 1 : 0);
}
static void k32_GetLocaleInfoA(RcnCtx *ctx) { locale_info_result(ctx, 0); }
static void k32_GetLocaleInfoW(RcnCtx *ctx) { locale_info_result(ctx, 1); }
static void k32_SetHandleCount(RcnCtx *ctx) { RET(A0); }
static void k32_SetStdHandle(RcnCtx *ctx) { RET(1); }
// Variables de l'hôte transmises au jeu, pour la mise au point : réglages des runtimes .NET (RH_, DOTNET_,
// COMPlus_), du ramasse-miettes de Mono (GC_, Boehm) et de Mono (MONO_). Le reste de l'environnement est
// absent.
int rcn_env_passed(const char *name) {
    static const char *const prefixes[] = {"RH_", "DOTNET_", "COMPlus_", "GC_", "MONO_"};
    for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++)
        if (!strncmp(name, prefixes[i], strlen(prefixes[i]))) return 1;
    return 0;
}
// Bloc d'environnement (« NOM=valeur », zéro, …, zéro), en UTF-16 si wide.
static u64 environment_block(RcnCtx *ctx, int wide) {
    extern char **environ;
    size_t bytes = 2 * (wide ? 2 : 1);
    for (char **e = environ; e && *e; e++)
        if (rcn_env_passed(*e)) bytes += (strlen(*e) + 1) * (wide ? 2 : 1);
    u8 *block = rcn_galloc(ctx, bytes, 1);
    if (!block) return 0;
    size_t at = 0;
    for (char **e = environ; e && *e; e++) {
        if (!rcn_env_passed(*e)) continue;
        size_t n = strlen(*e);
        if (wide) {
            size_t len = rcn_utf8_to_utf16(*e, n, (u16 *)(block + at));
            at += 2 * (len + 1);
        } else {
            memcpy(block + at, *e, n);
            at += n + 1;
        }
    }
    return (u64)(uintptr_t)block;
}
static void k32_GetEnvironmentStrings(RcnCtx *ctx) { RET(environment_block(ctx, 0)); }
static void k32_GetEnvironmentStringsW(RcnCtx *ctx) { RET(environment_block(ctx, 1)); }
static void k32_FreeEnvironmentStrings(RcnCtx *ctx) {
    rcn_gfree(ctx, PTR(A0));
    RET(1);
}
static void k32_TerminateProcess(RcnCtx *ctx) { rcn_exit(ctx, (u32)A1); }

// Opérations atomiques importées par les programmes 32 bits (en 64 bits, le compilateur les insère).
// Adresse non alignée (Mono corrige ainsi les 4 octets de destination d'un « call » dans son code) :
// permise par le x86, refusée par l'ARM64, faite sous le verrou des accès atomiques non alignés du code
// traduit (rcn_ops.c). op : 0 add, 5 xchg, 6 cmpxchg.
u64 rcn_op_atomic_unaligned(u64 addr, u32 op, u32 size, u64 value, u64 expected);
#define MISALIGNED(a) (((a) & 3) != 0)
static void k32_InterlockedExchange(RcnCtx *ctx) {
    if (MISALIGNED(A0)) RET(rcn_op_atomic_unaligned(A0, 5, 4, (u32)A1, 0));
    else RET(__atomic_exchange_n((u32 *)PTR(A0), (u32)A1, __ATOMIC_SEQ_CST));
}
static void k32_InterlockedIncrement(RcnCtx *ctx) {
    if (MISALIGNED(A0)) RET((u32)rcn_op_atomic_unaligned(A0, 0, 4, 1, 0) + 1);
    else RET(__atomic_add_fetch((u32 *)PTR(A0), 1, __ATOMIC_SEQ_CST));
}
static void k32_InterlockedDecrement(RcnCtx *ctx) {
    if (MISALIGNED(A0)) RET((u32)rcn_op_atomic_unaligned(A0, 0, 4, 0xFFFFFFFFu, 0) - 1);
    else RET(__atomic_sub_fetch((u32 *)PTR(A0), 1, __ATOMIC_SEQ_CST));
}
static void k32_InterlockedExchangeAdd(RcnCtx *ctx) {
    if (MISALIGNED(A0)) RET(rcn_op_atomic_unaligned(A0, 0, 4, (u32)A1, 0));
    else RET(__atomic_fetch_add((u32 *)PTR(A0), (u32)A1, __ATOMIC_SEQ_CST));
}
// InterlockedCompareExchange(&target, exchange, comparand) : renvoie l'ancienne valeur.
static void k32_InterlockedCompareExchange(RcnCtx *ctx) {
    if (MISALIGNED(A0)) {
        RET(rcn_op_atomic_unaligned(A0, 6, 4, (u32)A1, (u32)A2));
        return;
    }
    u32 expected = (u32)A2;
    __atomic_compare_exchange_n((u32 *)PTR(A0), &expected, (u32)A1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    RET(expected);
}

// --- Temps ---

static void k32_GetTickCount(RcnCtx *ctx) { RET((u32)(rcn_now_ns() / 1000000)); }
static void k32_GetTickCount64(RcnCtx *ctx) { RET(rcn_now_ns() / 1000000); }
// Compteur de performances : sur ARM64, le compteur matériel (CNTVCT_EL0, lisible en mode utilisateur, de
// fréquence CNTFRQ_EL0) — quelques nanosecondes, quand clock_gettime en coûte des dizaines (les jeux .NET
// l'appellent sans cesse) ; ailleurs, nanosecondes de l'horloge monotone. Sans « isb » avant la lecture :
// elle vidait le pipeline à chaque appel (3 % du thread de rendu d'Unreal Engine 3, qui chronomètre tout),
// pour un ordre exact par rapport aux instructions voisines dont un jeu n'a pas besoin.
//
// Sur certains téléphones, le noyau intercepte cette lecture (contournement d'un défaut du processeur) :
// ~100 ns l'appel au lieu de ~10 (Poco F3), et Unreal Engine 3 l'appelle ~600 000 fois par seconde (6 % de
// son thread de rendu). D'où une lecture limitée, par thread : l'horloge grossière (~9 ns, pas de 3-4 ms)
// dit si du temps a passé ; la vraie lecture n'est refaite que si elle a avancé ou tous les 32 appels.
// Entre deux, la dernière valeur plus un tick par appel : strictement croissante, en retard au plus de 32
// appels rapprochés. Un thread qui lit l'horloge une fois par image a toujours une lecture exacte.
// Un Sleep vient d'avoir lieu sur ce thread : prochaine lecture exacte (boucle « Sleep, lire l'heure »).
static __thread int qpc_after_sleep;
static void k32_QueryPerformanceCounter(RcnCtx *ctx) {
#if defined(__aarch64__)
    static __thread u64 last, coarse_last;
    static __thread u32 calls;
    struct timespec c;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &c);
    u64 coarse = (u64)c.tv_sec * 1000000000ull + (u64)c.tv_nsec;
    u64 v;
    if (coarse != coarse_last || ++calls >= 32 || qpc_after_sleep) {
        qpc_after_sleep = 0;
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
        if (v <= last) v = last + 1;
        coarse_last = coarse;
        calls = 0;
    } else {
        v = last + 1;
    }
    last = v;
    wr64(A0, v);
#else
    wr64(A0, rcn_now_ns() / 100); // 10 MHz, comme Windows
#endif
    RET(1);
}
static void k32_QueryPerformanceFrequency(RcnCtx *ctx) {
#if defined(__aarch64__)
    u64 f;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    wr64(A0, f);
#else
    // 10 MHz comme sous Windows : à 1 GHz, les compteurs 32 bits qu'en tirent les jeux (FPlatformTime::Cycles
    // d'Unreal) reviennent à zéro toutes les 4 s au lieu de 7 min.
    wr64(A0, 10000000ull);
#endif
    RET(1);
}
static void k32_Sleep(RcnCtx *ctx) {
    // RCN_SLEEP_TRACE=1 : adresse de retour d'un Sleep(n) sur 20000, avec n (boucles d'attente active).
    static int trace = -1;
    if (trace < 0) trace = getenv("RCN_SLEEP_TRACE") != NULL;
    if (trace) {
        static u32 calls;
        if (__atomic_add_fetch(&calls, 1, __ATOMIC_RELAXED) % 20000 == 0)
            rcn_log("Sleep(%u) appelé depuis 0x%llx (thread %u)", (u32)A0, (unsigned long long)(rdp(ctx, ctx->r[4]) - ctx->base),
                    THREAD_ID(ctx));
    }
    // Lecture exacte de l'horloge juste après (boucle « Sleep(0), lire l'heure » du limiteur d'images de
    // Skyrim : avec la lecture limitée, il voyait l'heure figée et dépassait sa cible, 57 images/s au lieu de 61).
    qpc_after_sleep = 1;
    rcn_sleep_ms((u32)A0);
}
// GetSystemTimeAsFileTime(&filetime) : centaines de nanosecondes depuis le 1er janvier 1601.
static void k32_GetSystemTimeAsFileTime(RcnCtx *ctx) {
    wr64(A0, ((u64)time(NULL) + 11644473600ull) * 10000000ull);
}

// --- Texte ---
// Toutes les pages de codes sont traitées comme de l'UTF-8 (identique pour l'ASCII).

static void k32_GetACP(RcnCtx *ctx) { RET(1252); }
static void k32_GetOEMCP(RcnCtx *ctx) { RET(437); }
static void k32_GetUserDefaultLangID(RcnCtx *ctx) { RET(0x0409); } // anglais (États-Unis)
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
const RcnShim rcn_psapi[] = {
    {"EnumProcesses", ps_EnumProcesses, S32(3)},
    {"EnumProcessModules", ps_EnumProcessModules, S32(4)},
    {"EnumProcessModulesEx", ps_EnumProcessModules, S32(5)},
    {"GetModuleBaseNameA", ps_GetModuleBaseNameA, S32(4)},
    {"GetModuleBaseNameW", ps_GetModuleBaseNameW, S32(4)},
    {"GetModuleFileNameExA", ps_GetModuleFileNameExA, S32(4)},
    {"GetModuleFileNameExW", ps_GetModuleFileNameExW, S32(4)},
    {"GetModuleInformation", ps_GetModuleInformation, S32(4)},
};
const size_t rcn_psapi_count = sizeof rcn_psapi / sizeof rcn_psapi[0];

const RcnShim rcn_kernel32[] = {
    {"FreeEnvironmentStringsA", k32_FreeEnvironmentStrings, S32(1)},
    {"FreeEnvironmentStringsW", k32_FreeEnvironmentStrings, S32(1)},
    {"GetACP", k32_GetACP, S32(0)},
    {"GetCPInfo", k32_GetCPInfo, S32(2)},
    {"GetEnvironmentStrings", k32_GetEnvironmentStrings, S32(0)},
    {"GetEnvironmentStringsW", k32_GetEnvironmentStringsW, S32(0)},
    {"GetLocaleInfoA", k32_GetLocaleInfoA, S32(4)},
    {"GetOEMCP", k32_GetOEMCP, S32(0)},
    {"GetProcAddress", k32_GetProcAddress, S32(2)},
    {"GetStartupInfoA", k32_GetStartupInfoA, S32(1)},
    {"GetStartupInfoW", k32_GetStartupInfoW, S32(1)},
    {"HeapSetInformation", k32_HeapSetInformation, S32(4)},
    {"FlushInstructionCache", k32_FlushInstructionCache, S32(3)},
    {"EnumSystemLocalesA", k32_EnumSystemLocalesA, S32(2)},
    {"FileTimeToDosDateTime", k32_FileTimeToDosDateTime, S32(3)},
    {"GetDateFormatA", k32_GetDateFormatA, S32(6)},
    {"GetDateFormatW", k32_GetDateFormatW, S32(6)},
    {"GetTimeFormatA", k32_GetTimeFormatA, S32(6)},
    {"GetTimeFormatW", k32_GetTimeFormatW, S32(6)},
    {"lstrcpyA", k32_lstrcpyA, S32(2)},
    {"lstrcpynA", k32_lstrcpynA, S32(3)},
    {"lstrcpynW", k32_lstrcpynW, S32(3)},
    {"GetLocaleInfoA", k32_GetLocaleInfoA, S32(4)},
    {"GetLocaleInfoW", k32_GetLocaleInfoW, S32(4)},
    {"FlsAlloc", k32_FlsAlloc, S32(1)},
    {"FlsGetValue", k32_FlsGetValue, S32(1)},
    {"FlsSetValue", k32_FlsSetValue, S32(2)},
    {"FlsFree", k32_FlsFree, S32(1)},
    {"GetStringTypeA", k32_GetStringTypeA, S32(5)},
    {"GetStringTypeW", k32_GetStringTypeW, S32(4)},
    {"GetSystemInfo", k32_GetSystemInfo, S32(1)},
    {"GetNativeSystemInfo", k32_GetSystemInfo, S32(1)},
    {"GetUserDefaultLangID", k32_GetUserDefaultLangID, S32(0)},
    {"GetVersionExA", k32_GetVersionExA, S32(1)},
    {"HeapCreate", k32_HeapCreate, S32(3)},
    {"HeapDestroy", k32_HeapDestroy, S32(1)},
    {"HeapSize", k32_HeapSize, S32(3)},
    {"GlobalSize", k32_GlobalSize, S32(1)},
    {"LocalSize", k32_GlobalSize, S32(1)},
    {"HeapValidate", k32_HeapDestroy, S32(3)}, // TRUE : tas valide
    {"HeapWalk", k32_IsDebuggerPresent, S32(2)}, // FALSE : pas de parcours
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
    {"GetCurrentDirectoryA", k32_GetCurrentDirectoryA, S32(2)},
    {"GetCurrentDirectoryW", k32_GetCurrentDirectoryW, S32(2)},
    {"SetCurrentDirectoryA", k32_SetCurrentDirectoryA, S32(1)},
    {"SetCurrentDirectoryW", k32_SetCurrentDirectoryW, S32(1)},
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
    {"IsWow64Process", k32_IsWow64Process, S32(2)},
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
