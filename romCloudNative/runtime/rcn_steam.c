// RomCloud Native : Steam fourni par l'hôte, pour les jeux 32 bits (steam_api.dll) comme 64 bits
// (steam_api64.dll). Le jeu charge le DLL et y prend ses fonctions par GetProcAddress (API « flat » de
// Steamworks, celle qu'emploient Facepunch.Steamworks et Steamworks.NET). Aucun client Steam n'existe sur
// l'hôte : les interfaces sont un objet factice et les lectures répondent des valeurs plausibles (compte
// « Player », anglais, temps de l'hôte), de façon que le jeu se lance et joue hors de Steam.
//
// Ces fonctions suivent la convention de Steamworks : cdecl (S_CALLTYPE) sur Windows 32 bits, donc
// l'appelant retire ses arguments (args32 = C32) — rien à dépiler ici, et les replis d'après le nom
// restent sûrs même quand le nombre d'arguments n'est pas connu.
//
// L'initialisation que ces bibliothèques managées attendent : SteamInternal_SteamAPI_Init rend 0
// (k_ESteamAPIInitResult_OK), le tuyau et l'utilisateur sont non nuls, et chaque interface demandée
// (SteamAPI_SteamXxx_vNNN, SteamInternal_CreateInterface, SteamInternal_FindOrCreate*Interface) rend un
// pointeur non nul. La boucle de rappels (SteamAPI_ManualDispatch_*) ne rend aucun évènement.
//
// RCN_STEAM_LOG=1 : nom des fonctions Steam demandées, pour voir ce que le jeu utilise.
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rcn_internal.h"

// Application en cours : celle que le jeu annonce (RestartAppIfNecessary) ou celle de son environnement
// (« SteamAppId », posée par Facepunch.Steamworks avant l'initialisation).
static u32 app_id;

static u32 steam_app_id(void) {
    if (app_id) return app_id;
    static const char *const names[] = {"SteamAppId", "SteamGameId"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const char *text = getenv(names[i]);
        if (!text || !*text) continue;
        app_id = (u32)strtoul(text, NULL, 10);
        if (app_id) break;
    }
    return app_id;
}

// Interfaces : un même objet factice, que le jeu ne fait que passer aux fonctions ci-dessous. Il vit dans
// la mémoire du jeu : une adresse de l'hôte, tronquée à 32 bits, ne désignerait rien pour un jeu 32 bits.
static u64 iface;
// Texte rendu au jeu, copié une fois dans sa mémoire (même raison).
static u64 guest_text(RcnCtx *ctx, u64 *cache, const char *text) {
    if (!*cache) {
        size_t n = strlen(text) + 1;
        char *copy = rcn_galloc(ctx, n, 1);
        if (copy) memcpy(copy, text, n);
        *cache = (u64)(uintptr_t)copy;
    }
    return *cache;
}

static void steam_True(RcnCtx *ctx) { RET(1); }
static void steam_False(RcnCtx *ctx) { RET(0); }
static void steam_Nothing(RcnCtx *ctx) { (void)ctx; }
static void steam_Iface(RcnCtx *ctx) {
    if (!iface) iface = (u64)(uintptr_t)rcn_galloc(ctx, 64, 1);
    RET(iface);
}
// k_ESteamAPIInitResult_OK : l'initialisation réussit. Le message d'erreur éventuel est vidé.
static void steam_Init(RcnCtx *ctx) {
    if (A1) wr8(A1, 0);
    RET(0);
}
static void steam_RestartAppIfNecessary(RcnCtx *ctx) {
    if ((u32)A0) app_id = (u32)A0;
    RET(0); // lancé hors de Steam : continuer
}
static void steam_PersonaName(RcnCtx *ctx) {
    static u64 text;
    RET(guest_text(ctx, &text, "Player"));
}
static void steam_Language(RcnCtx *ctx) {
    static u64 text;
    RET(guest_text(ctx, &text, "english"));
}
static void steam_Country(RcnCtx *ctx) {
    static u64 text;
    RET(guest_text(ctx, &text, "US"));
}
static void steam_SteamID(RcnCtx *ctx) { RET64(76561198000000000ULL); } // compte public
static void steam_AppID(RcnCtx *ctx) { RET(steam_app_id()); }
// Voix : fréquence des échantillons qu'aurait donnée le client Steam.
static void steam_VoiceRate(RcnCtx *ctx) { RET(48000); }
static void steam_ServerTime(RcnCtx *ctx) { RET((u64)time(NULL)); }
// GetQuota(uint64 *total, uint64 *disponible) : nuage vide.
static void steam_Quota(RcnCtx *ctx) {
    if (A1) wr64(A1, 0);
    if (A2) wr64(A2, 0);
    RET(1);
}
// GetAchievement(const char *nom, bool *obtenu) : aucun haut fait.
static void steam_GetAchievement(RcnCtx *ctx) {
    if (A2) wr8(A2, 0);
    RET(1);
}
// GetAchievementAchievedPercent(const char *nom, float *pourcentage).
static void steam_AchievementPercent(RcnCtx *ctx) {
    if (A2) wr32(A2, 0);
    RET(1);
}

// Nom terminé par `suffix` ; `suffix` commence par le « _ » qui sépare le nom de la méthode.
static int ends_with(const char *text, const char *suffix) {
    size_t n = strlen(text), m = strlen(suffix);
    return n >= m && memcmp(text + n - m, suffix, m) == 0;
}

// Accesseur d'interface : « SteamAPI_SteamUser_v023 », « SteamAPI_SteamFriends_v017 »…
static int is_interface_accessor(const char *name) {
    size_t n = strlen(name);
    if (n < 6 || memcmp(name + n - 5, "_v", 2) != 0) return 0;
    for (size_t i = n - 3; i < n; i++) {
        if (name[i] < '0' || name[i] > '9') return 0;
    }
    return 1;
}

// Fonction d'une interface (SteamAPI_ISteamXxx_Methode) d'après son nom : ce que le jeu lit, jamais ce
// qu'il écrit. Les écritures (Set, Request…) réussissent ; les lectures rendent zéro (rien n'est obtenu).
static RcnFn method_default(const char *method) {
    static const char *const yes[] = {"Set", "Request", "Store", "Init", "Update", "Reset", "Indicate", "Activate", "Commit"};
    if (!strcmp(method, "GetPersonaName")) return steam_PersonaName;
    if (!strcmp(method, "GetSteamID")) return steam_SteamID;
    if (!strcmp(method, "GetAppID")) return steam_AppID;
    if (!strcmp(method, "BLoggedOn")) return steam_True;
    if (!strcmp(method, "GetSteamUILanguage") || !strcmp(method, "GetCurrentGameLanguage") ||
        !strcmp(method, "GetAvailableGameLanguages"))
        return steam_Language;
    if (!strcmp(method, "GetIPCountry")) return steam_Country;
    if (!strcmp(method, "GetServerRealTime")) return steam_ServerTime;
    if (!strcmp(method, "GetConnectedUniverse")) return steam_True;
    if (!strcmp(method, "GetAchievement")) return steam_GetAchievement;
    if (!strcmp(method, "GetAchievementAchievedPercent")) return steam_AchievementPercent;
    if (!strcmp(method, "GetQuota")) return steam_Quota;
    // ISteamClient : tuyaux, utilisateurs et interfaces (Steamworks.NET passe par là).
    if (!strncmp(method, "GetISteam", 9)) return steam_Iface;
    if (!strcmp(method, "CreateSteamPipe") || !strcmp(method, "ConnectToGlobalUser") || !strcmp(method, "CreateLocalUser"))
        return steam_True;
    // BIsSubscribed, BIsAppInstalled… : le jeu est considéré comme possédé.
    if (!strcmp(method, "BIsSubscribed") || !strcmp(method, "BIsAppInstalled") || !strcmp(method, "BIsSubscribedApp"))
        return steam_True;
    if ((method[0] == 'B' && method[1] >= 'A' && method[1] <= 'Z') || !strncmp(method, "Is", 2)) return steam_False;
    if (!strncmp(method, "Run", 3) || !strncmp(method, "Shutdown", 8)) return steam_Nothing;
    for (size_t i = 0; i < sizeof yes / sizeof yes[0]; i++) {
        if (!strncmp(method, yes[i], strlen(yes[i]))) return steam_True;
    }
    return steam_False;
}

// RCN_STEAM_LOG=1 : fonctions demandées, une fois chacune (mise à point), et le DLL qui les a demandées.
static void log_once(const char *name, int bits32) {
    static int enabled = -1, shown;
    if (enabled < 0) enabled = getenv("RCN_STEAM_LOG") != NULL;
    if (enabled && shown < 256) {
        shown++;
        rcn_log("%s : %s fournie", bits32 ? "steam_api" : "steam_api64", name);
    }
}

// --- Ancienne API (avant 2013) : interfaces à table virtuelle -------------------------------
// Le jeu prend directement « SteamUser », « SteamApps »… au DLL, puis appelle leurs méthodes par la
// table virtuelle de l'interface. En 32 bits, ces méthodes sont __thiscall : le pointeur « this » est
// dans ecx et la méthode retire ses arguments de la pile — d'où le nombre d'arguments de chaque entrée
// des tables (rcn_steam_legacy_tables.h, générées depuis le SDK Steamworks).

// Argument n d'une méthode d'interface. 32 bits : tous sur la pile, après l'adresse de retour (« this »
// est dans ecx, hors pile). 64 bits : rcx est « this », les arguments suivent dans rdx, r8, r9, puis la
// pile.
static u64 larg(RcnCtx *ctx, int n) {
    if (ctx->bits32) return rd32((u32)(ctx->r[4] + 4 + 4 * (u32)n));
    return rcn_arg(ctx, n + 1);
}

// Texte rendu par une méthode, copié une fois dans la mémoire du jeu.
static u64 legacy_text(RcnCtx *ctx, const char *text) {
    static struct {
        const char *text;
        u64 addr;
    } cache[16];
    static int count;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    u64 addr = 0;
    for (int i = 0; i < count && !addr; i++)
        if (cache[i].text == text) addr = cache[i].addr;
    if (!addr) {
        size_t n = strlen(text) + 1;
        char *copy = rcn_galloc(ctx, n, 1);
        if (copy) memcpy(copy, text, n);
        addr = (u64)(uintptr_t)copy;
        if (count < 16) {
            cache[count].text = text;
            cache[count].addr = addr;
            count++;
        }
    }
    pthread_mutex_unlock(&lock);
    return addr;
}

// Objet d'une interface : sa table virtuelle est construite une fois par processus (32 et 64 bits).
static u64 steam_object(RcnCtx *ctx, const char *iface, const RcnMethod *methods, u32 count) {
    if (ctx->bits32) return rcn_com_new(ctx, iface, methods, count, 4);
    static struct {
        const RcnMethod *methods;
        u64 table;
    } tables[16];
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    int slot = 0;
    while (slot < 16 && tables[slot].methods && tables[slot].methods != methods) slot++;
    if (slot < 16 && !tables[slot].methods) {
        u64 *built = rcn_galloc(ctx, count * 8, 1);
        for (u32 i = 0; built && i < count; i++) built[i] = rcn_thunk(ctx, iface, methods[i].name, methods[i].fn, 0);
        tables[slot].methods = methods;
        tables[slot].table = (u64)(uintptr_t)built;
    }
    u64 table = slot < 16 ? tables[slot].table : 0;
    pthread_mutex_unlock(&lock);
    u8 *object = table ? rcn_galloc(ctx, 16 + 4, 1) : NULL;
    if (!object) return 0;
    memcpy(object, &table, 8);
    wr64((u64)(uintptr_t)object + 8, 1);
    return (u64)(uintptr_t)object;
}

// Dernières méthodes d'interface appelées : un anneau, lu par le journal d'une faute fatale. Les noms
// (dll = interface, name = méthode) sont des chaînes du programme, valables jusqu'à la fin du processus.
#define STEAM_RECENT 32
static const RcnImport *recent_calls[STEAM_RECENT];
static unsigned recent_calls_count;
const RcnImport *rcn_steam_call(unsigned back) {
    if (back >= recent_calls_count || back >= STEAM_RECENT) return NULL;
    return recent_calls[(recent_calls_count - 1 - back) % STEAM_RECENT];
}

// Une méthode d'interface : aucun client Steam sur l'hôte, donc des lectures plausibles et des écritures
// ignorées. Ce que le jeu consulte vraiment (identité, langue, possession, statistiques) est traité ici.
static void steam_legacy_method(RcnCtx *ctx) {
    const RcnImport *call = THREAD(ctx)->current_import;
    const char *m = call->name;
    recent_calls[recent_calls_count++ % STEAM_RECENT] = call;
    // Identité et application.
    if (!strcmp(m, "GetSteamID") || !strcmp(m, "GetAppOwner")) { RET64(76561198000000000ULL); return; }
    if (!strcmp(m, "GetAppID")) { RET(steam_app_id()); return; }
    if (!strcmp(m, "GetConnectedUniverse")) { RET(1); return; }
    // Poignées du tuyau et de l'utilisateur : non nulles (0 est « aucun »).
    if (!strcmp(m, "GetHSteamUser") || !strcmp(m, "GetHSteamPipe")) { RET(1); return; }
    if (!strcmp(m, "GetServerRealTime") || !strcmp(m, "GetEarliestPurchaseUnixTime")) { RET((u64)time(NULL)); return; }
    if (!strcmp(m, "GetPersonaState")) { RET(1); return; } // en ligne
    // Textes.
    if (!strcmp(m, "GetPersonaName") || !strcmp(m, "GetFriendPersonaName") || !strcmp(m, "GetPlayerNickname") ||
        !strcmp(m, "GetClanName")) {
        RET(legacy_text(ctx, "Player"));
        return;
    }
    if (!strcmp(m, "GetCurrentGameLanguage") || !strcmp(m, "GetAvailableGameLanguages") ||
        !strcmp(m, "GetSteamUILanguage")) {
        RET(legacy_text(ctx, "english"));
        return;
    }
    if (!strcmp(m, "GetIPCountry")) { RET(legacy_text(ctx, "US")); return; }
    if (!strcmp(m, "GetLaunchQueryParam") || !strcmp(m, "GetFriendRichPresence") || !strcmp(m, "GetAchievementName"))
        { RET(legacy_text(ctx, "")); return; }
    // Possession du jeu : oui, comme la bibliothèque Steam de l'hôte l'aurait dit.
    if (!strcmp(m, "BIsSubscribed") || !strcmp(m, "BIsSubscribedApp") || !strcmp(m, "BIsDlcInstalled") ||
        !strcmp(m, "BIsAppInstalled") || !strcmp(m, "BIsSubscribedFromFamilySharing") || !strcmp(m, "BLoggedOn") ||
        !strcmp(m, "BIsLoggedOn") || !strcmp(m, "BConnected") || !strcmp(m, "BSecure") || !strcmp(m, "BHasGame")) {
        RET(1);
        return;
    }
    // Statistiques et hauts faits : la demande réussit, mais rien n'est enregistré.
    if (!strcmp(m, "RequestCurrentStats") || !strcmp(m, "StoreStats") || !strcmp(m, "SetStat") ||
        !strcmp(m, "SetAchievement") || !strcmp(m, "ClearAchievement") || !strcmp(m, "IndicateAchievementProgress") ||
        !strcmp(m, "ResetAllStats") || !strcmp(m, "UpdateAvgRateStat")) {
        RET(1);
        return;
    }
    // GetAchievement(const char *, bool *) : aucun haut fait obtenu.
    if (!strcmp(m, "GetAchievement") || !strcmp(m, "GetAchievementAndUnlockTime")) {
        u64 achieved = ctx->bits32 ? larg(ctx, 1) : rcn_arg(ctx, 2);
        if (achieved) wr8(achieved, 0);
        RET(1);
        return;
    }
    // Jeton d'authentification : aucun (le jeu joue hors ligne).
    if (!strcmp(m, "GetAuthSessionTicket")) {
        u64 out = ctx->bits32 ? larg(ctx, 2) : rcn_arg(ctx, 3);
        if (out) wr32(out, 0);
        RET(0);
        return;
    }
    // Sauvegardes dans le nuage : aucun fichier (les sauvegardes restent locales).
    if (!strcmp(m, "FileExists") || !strcmp(m, "FileWrite") || !strcmp(m, "FileWriteAsync") ||
        !strcmp(m, "FileReadAsync") || !strcmp(m, "FileRead") || !strcmp(m, "FileDelete") ||
        !strcmp(m, "FileForget") || !strcmp(m, "FilePersist") || !strcmp(m, "FileWriteStreamWriteChunk")) {
        RET(0);
        return;
    }
    // Quota du nuage : vide.
    if (!strcmp(m, "GetQuota")) {
        u64 used = ctx->bits32 ? larg(ctx, 0) : rcn_arg(ctx, 1);
        u64 total = ctx->bits32 ? larg(ctx, 2) : rcn_arg(ctx, 2);
        if (used) wr64(used, 0);
        if (total) wr64(total, 0);
        RET(1);
        return;
    }
    // Par défaut : rien lu, rien obtenu ; les méthodes sans résultat (Set, Install, Shutdown…) sont muettes.
    RET(0);
}

// Tables des interfaces (ordre des méthodes = ordre de la table virtuelle) et accès « SteamApps() ».
#include "rcn_steam_legacy_tables.h"

const RcnShim rcn_steam[] = {
    // Initialisation, tuyau, boucle de rappels.
    {"SteamInternal_SteamAPI_Init", steam_Init, C32},
    {"SteamInternal_GameServer_Init", steam_False, C32},
    {"SteamInternal_GameServer_Init_V2", steam_False, C32},
    {"SteamInternal_CreateInterface", steam_Iface, C32},
    {"SteamInternal_FindOrCreateUserInterface", steam_Iface, C32},
    {"SteamInternal_FindOrCreateGameServerInterface", steam_Iface, C32},
    // Ancienne API (avant 2013) : « SteamAPI_Init() » sans argument, et ses variantes.
    {"SteamAPI_Init", steam_True, C32},
    {"SteamAPI_InitSafe", steam_True, C32},
    {"SteamAPI_GetSteamInstallPath", steam_False, C32},
    {"SteamAPI_WriteMiniDump", steam_Nothing, C32},
    {"SteamAPI_SetMiniDumpComment", steam_Nothing, C32},
    {"SteamAPI_Shutdown", steam_Nothing, C32},
    {"SteamAPI_RunCallbacks", steam_Nothing, C32},
    {"SteamAPI_ReleaseCurrentThreadMemory", steam_Nothing, C32},
    {"SteamAPI_RestartAppIfNecessary", steam_RestartAppIfNecessary, C32},
    {"SteamAPI_IsSteamRunning", steam_True, C32},
    {"SteamAPI_GetHSteamPipe", steam_True, C32},
    {"SteamAPI_GetHSteamUser", steam_True, C32},
    {"SteamAPI_ManualDispatch_Init", steam_Nothing, C32},
    {"SteamAPI_ManualDispatch_RunFrame", steam_Nothing, C32},
    {"SteamAPI_ManualDispatch_GetNextCallback", steam_False, C32},
    {"SteamAPI_ManualDispatch_FreeLastCallback", steam_True, C32},
    {"SteamAPI_ManualDispatch_GetAPICallResult", steam_False, C32},
    {"SteamAPI_RegisterCallback", steam_Nothing, C32},
    {"SteamAPI_UnregisterCallback", steam_Nothing, C32},
    {"SteamAPI_RegisterCallResult", steam_Nothing, C32},
    {"SteamAPI_UnregisterCallResult", steam_Nothing, C32},
    // Serveur de jeu : les jeux solo ne s'en servent pas, mais leurs bibliothèques le demandent parfois.
    {"SteamGameServer_Shutdown", steam_Nothing, C32},
    {"SteamGameServer_RunCallbacks", steam_Nothing, C32},
    {"SteamGameServer_ReleaseCurrentThreadMemory", steam_Nothing, C32},
    {"SteamGameServer_GetHSteamPipe", steam_True, C32},
    {"SteamGameServer_GetHSteamUser", steam_True, C32},
    {"SteamGameServer_GetSteamID", steam_SteamID, C32},
    {"SteamGameServer_BSecure", steam_False, C32},
    // Compte, amis, application.
    {"SteamAPI_ISteamUser_GetSteamID", steam_SteamID, C32},
    {"SteamAPI_ISteamUser_BLoggedOn", steam_True, C32},
    {"SteamAPI_ISteamUser_GetVoiceOptimalSampleRate", steam_VoiceRate, C32},
    {"SteamAPI_ISteamGameServer_GetSteamID", steam_SteamID, C32},
    {"SteamAPI_ISteamFriends_GetPersonaName", steam_PersonaName, C32},
    {"SteamAPI_ISteamFriends_GetPersonaState", steam_True, C32},
    {"SteamAPI_ISteamApps_GetCurrentGameLanguage", steam_Language, C32},
    {"SteamAPI_ISteamApps_GetAvailableGameLanguages", steam_Language, C32},
    {"SteamAPI_ISteamApps_BIsSubscribed", steam_True, C32},
    {"SteamAPI_ISteamApps_BIsAppInstalled", steam_True, C32},
    {"SteamAPI_ISteamApps_BIsSubscribedApp", steam_True, C32},
    {"SteamAPI_ISteamApps_BIsDlcInstalled", steam_False, C32},
    {"SteamAPI_ISteamUtils_GetAppID", steam_AppID, C32},
    {"SteamAPI_ISteamUtils_GetSteamUILanguage", steam_Language, C32},
    {"SteamAPI_ISteamUtils_GetIPCountry", steam_Country, C32},
    {"SteamAPI_ISteamUtils_GetServerRealTime", steam_ServerTime, C32},
    {"SteamAPI_ISteamUtils_GetConnectedUniverse", steam_True, C32},
    // Hors de Steam Desktop : ni Steam Deck, ni mode « grand écran », ni superposition.
    {"SteamAPI_ISteamUtils_IsRunningOnSteamDeck", steam_False, C32},
    {"SteamAPI_ISteamUtils_IsSteamRunningOnSteamDeck", steam_False, C32},
    {"SteamAPI_ISteamUtils_IsSteamInBigPictureMode", steam_False, C32},
    {"SteamAPI_ISteamUtils_IsOverlayEnabled", steam_False, C32},
    // Statistiques et hauts faits : demandés avec succès, mais vides.
    {"SteamAPI_ISteamUserStats_RequestCurrentStats", steam_True, C32},
    {"SteamAPI_ISteamUserStats_StoreStats", steam_True, C32},
    {"SteamAPI_ISteamUserStats_GetAchievement", steam_GetAchievement, C32},
    {"SteamAPI_ISteamUserStats_GetAchievementAchievedPercent", steam_AchievementPercent, C32},
    {"SteamAPI_ISteamUserStats_ResetAllStats", steam_True, C32},
    // Nuage : vide, mais interrogé sans échec.
    {"SteamAPI_ISteamRemoteStorage_GetQuota", steam_Quota, C32},
    // Manettes : Steam Input ne voit aucune manette (celles de l'hôte sont ailleurs).
    {"SteamAPI_ISteamInput_Init", steam_True, C32},
    {"SteamAPI_ISteamInput_Shutdown", steam_Nothing, C32},
};
const size_t rcn_steam_count = sizeof rcn_steam / sizeof rcn_steam[0];

// Fonction Steam demandée par le jeu, 64 bits ou 32 bits : la table, sinon un repli d'après le nom.
RcnFn rcn_steam_lookup(const char *name, int bits32) {
    for (size_t i = 0; i < rcn_steam_count; i++) {
        if (!strcmp(rcn_steam[i].name, name)) return rcn_steam[i].fn;
    }
    log_once(name, bits32);
    // Ancienne API : « SteamApps », « SteamUser »… rendent leur interface à table virtuelle.
    for (size_t i = 0; i < sizeof steam_legacy_accessors / sizeof steam_legacy_accessors[0]; i++) {
        if (!strcmp(steam_legacy_accessors[i].accessor, name)) return steam_legacy_accessors[i].fn;
    }
    if (is_interface_accessor(name)) return steam_Iface;
    // SteamAPI_ISteamUser_GetSteamID → « GetSteamID ».
    if (!strncmp(name, "SteamAPI_ISteam", 15)) {
        const char *method = strchr(name + 15, '_');
        if (method) return method_default(method + 1);
    }
    // SteamGameServer_BSecure, SteamGameServer_GetHSteamPipe…
    if (!strncmp(name, "SteamGameServer_", 16)) {
        const char *method = name + 16;
        if (!strcmp(method, "GetSteamID")) return steam_SteamID;
        if (!strncmp(method, "GetHSteam", 9)) return steam_True;
        if (!strncmp(method, "Run", 3) || !strncmp(method, "Shutdown", 8) || !strncmp(method, "Release", 7))
            return steam_Nothing;
        return steam_False;
    }
    // Structure d'aide (SteamNetworkingIPAddr_Clear, MatchMakingKeyValuePair_t_Construct…) : rien à faire.
    return steam_False;
}
