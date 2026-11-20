// Test steam32 : exécutable x86 32 bits sans bibliothèque C qui charge « steam_api.dll » et prend les
// fonctions de l'API « flat » de Steamworks par GetProcAddress, comme le fait un jeu 32 bits.
// Ce DLL n'existe pas sur Windows : la comparaison avec l'exécutable d'origine est donc sautée
// (noOriginal dans scripts/test.mjs). Le runtime, lui, fournit ces fonctions (runtime/rcn_steam.c) :
// initialisation réussie, tuyau et utilisateur non nuls, interface factice, CSteamID de 64 bits rendu
// dans edx:eax, textes rangés dans la mémoire du jeu, et convention cdecl (l'appelant retire ses
// arguments) — une pile décalée se verrait dès le deuxième appel.
#include <windows.h>

typedef unsigned long long u64;

static HANDLE out;

static void put(const char *s) {
    DWORD written;
    size_t n = 0;
    while (s[n]) n++;
    if (n) WriteFile(out, s, (DWORD)n, &written, NULL);
}
static void put_hex32(unsigned v) {
    char text[9];
    for (int i = 7; i >= 0; i--, v >>= 4) {
        unsigned d = v & 15;
        text[i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    text[8] = 0;
    put(text);
}
static void put_u64(u64 v) {
    put_hex32((unsigned)(v >> 32));
    put_hex32((unsigned)v);
}
static void put_dec(unsigned v) {
    char text[12];
    int i = 11;
    text[i] = 0;
    do {
        text[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    put(text + i);
}

// Signatures de l'API flat : convention cdecl sur Windows 32 bits (S_CALLTYPE).
typedef int(__cdecl *InitFn)(const char *version, char *error);
typedef int(__cdecl *IntFn)(void);
typedef void(__cdecl *VoidFn)(void);
typedef u64(__cdecl *IdFn)(void *iface);
typedef const char *(__cdecl *TextFn)(void *iface);
typedef unsigned(__cdecl *AppFn)(void *iface);
typedef int(__cdecl *RestartFn)(unsigned app);
typedef void *(__cdecl *IfaceFn)(void);
typedef int(__cdecl *DispatchFn)(int pipe, void *message);

static int found;
static void *get(HMODULE steam, const char *name) {
    void *fn = (void *)GetProcAddress(steam, name);
    if (fn) found++;
    put(fn ? "." : "!");
    return fn;
}

void start(void) {
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    put("bits ");
    put_dec((unsigned)sizeof(void *));
    put("\n");

    HMODULE steam = LoadLibraryA("steam_api.dll");
    put("charge ");
    put_dec(steam != NULL);
    put("\n");
    if (!steam) ExitProcess(1);

    InitFn init = (InitFn)get(steam, "SteamInternal_SteamAPI_Init");
    IntFn pipe = (IntFn)get(steam, "SteamAPI_GetHSteamPipe");
    IntFn user = (IntFn)get(steam, "SteamAPI_GetHSteamUser");
    IfaceFn user_iface = (IfaceFn)get(steam, "SteamAPI_SteamUser_v023");
    IfaceFn apps_iface = (IfaceFn)get(steam, "SteamAPI_SteamApps_v008");
    IdFn steam_id = (IdFn)get(steam, "SteamAPI_ISteamUser_GetSteamID");
    TextFn persona = (TextFn)get(steam, "SteamAPI_ISteamUser_GetPersonaName");
    TextFn language = (TextFn)get(steam, "SteamAPI_ISteamUser_GetCurrentGameLanguage");
    AppFn app_id = (AppFn)get(steam, "SteamAPI_ISteamApps_GetAppID");
    RestartFn restart = (RestartFn)get(steam, "SteamAPI_RestartAppIfNecessary");
    VoidFn dispatch_init = (VoidFn)get(steam, "SteamAPI_ManualDispatch_Init");
    DispatchFn next = (DispatchFn)get(steam, "SteamAPI_ManualDispatch_GetNextCallback");
    VoidFn free_last = (VoidFn)get(steam, "SteamAPI_ManualDispatch_FreeLastCallback");
    VoidFn shutdown = (VoidFn)get(steam, "SteamAPI_Shutdown");
    put("\nproc ");
    put_dec((unsigned)found);
    put("\n");
    if (found != 14) ExitProcess(2);

    // Le message d'erreur (2e argument) doit être vidé par l'initialisation.
    char error[256];
    put("init ");
    put_dec((unsigned)init("SteamAPI_Init_v017", error));
    put("\nmsg ");
    put(error[0] ? "non vide" : "vide");
    put("\n");

    put("pipe ");
    put_dec((unsigned)pipe());
    put("\nuser ");
    put_dec((unsigned)user());
    put("\n");

    // Interfaces factices : non nulles, et utilisables par les fonctions de l'API flat.
    void *u = user_iface();
    void *a = apps_iface();
    put("iface ");
    put_dec(u != NULL && a != NULL);
    put("\nid ");
    put_u64(steam_id(u)); // CSteamID de 64 bits : edx:eax
    put("\nnom ");
    put(persona(u));
    put("\nlangue ");
    put(language(u));
    put("\n");

    // Application annoncée : reprise hors de Steam refusée (0 = continuer), puis GetAppID.
    put("restart ");
    put_dec((unsigned)restart(573170));
    put("\napp ");
    put_dec(app_id(a));
    put("\n");

    // Boucle de rappels : aucun évènement.
    dispatch_init();
    put("dispatch ");
    put_dec((unsigned)next(pipe(), NULL));
    put("\n");
    free_last();
    shutdown();

    put("fin\n");
    ExitProcess(0);
}
