// RomCloud Native : Steam (steam_api64.dll) fourni par l'hôte. Le jeu charge « steam_api64.dll » et y
// prend ses fonctions par GetProcAddress (API « flat » de Steamworks, celle qu'emploie Facepunch.Steamworks
// et Steamworks.NET). Aucun client Steam n'existe sur l'hôte : les interfaces sont un objet factice et les
// lectures répondent des valeurs plausibles (compte « Player », anglais, temps de l'hôte), de façon que le
// jeu se lance et joue hors de Steam.
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

// Interfaces : un même objet factice, que le jeu ne fait que passer aux fonctions ci-dessous.
static u64 iface;

static void steam_True(RcnCtx *ctx) { RET(1); }
static void steam_False(RcnCtx *ctx) { RET(0); }
static void steam_Nothing(RcnCtx *ctx) { (void)ctx; }
static void steam_Iface(RcnCtx *ctx) { RET((u64)(uintptr_t)&iface); }
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
    static char name[] = "Player";
    RET((u64)(uintptr_t)name);
}
static void steam_Language(RcnCtx *ctx) {
    static char language[] = "english";
    RET((u64)(uintptr_t)language);
}
static void steam_Country(RcnCtx *ctx) {
    static char country[] = "US";
    RET((u64)(uintptr_t)country);
}
static void steam_SteamID(RcnCtx *ctx) { RET(76561198000000000ULL); } // compte public
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

// RCN_STEAM_LOG=1 : fonctions demandées, une fois chacune (mise au point).
static void log_once(const char *name) {
    static int enabled = -1, shown;
    if (enabled < 0) enabled = getenv("RCN_STEAM_LOG") != NULL;
    if (enabled && shown < 256) {
        shown++;
        rcn_log("steam_api64 : %s fournie", name);
    }
}

const RcnShim rcn_steam[] = {
    // Initialisation, tuyau, boucle de rappels.
    {"SteamInternal_SteamAPI_Init", steam_Init, 0},
    {"SteamInternal_GameServer_Init", steam_False, 0},
    {"SteamInternal_GameServer_Init_V2", steam_False, 0},
    {"SteamInternal_CreateInterface", steam_Iface, 0},
    {"SteamInternal_FindOrCreateUserInterface", steam_Iface, 0},
    {"SteamInternal_FindOrCreateGameServerInterface", steam_Iface, 0},
    {"SteamAPI_Shutdown", steam_Nothing, 0},
    {"SteamAPI_RunCallbacks", steam_Nothing, 0},
    {"SteamAPI_ReleaseCurrentThreadMemory", steam_Nothing, 0},
    {"SteamAPI_RestartAppIfNecessary", steam_RestartAppIfNecessary, 0},
    {"SteamAPI_IsSteamRunning", steam_True, 0},
    {"SteamAPI_GetHSteamPipe", steam_True, 0},
    {"SteamAPI_GetHSteamUser", steam_True, 0},
    {"SteamAPI_ManualDispatch_Init", steam_Nothing, 0},
    {"SteamAPI_ManualDispatch_RunFrame", steam_Nothing, 0},
    {"SteamAPI_ManualDispatch_GetNextCallback", steam_False, 0},
    {"SteamAPI_ManualDispatch_FreeLastCallback", steam_True, 0},
    {"SteamAPI_ManualDispatch_GetAPICallResult", steam_False, 0},
    {"SteamAPI_RegisterCallback", steam_Nothing, 0},
    {"SteamAPI_UnregisterCallback", steam_Nothing, 0},
    {"SteamAPI_RegisterCallResult", steam_Nothing, 0},
    {"SteamAPI_UnregisterCallResult", steam_Nothing, 0},
    // Serveur de jeu : les jeux solo ne s'en servent pas, mais leurs bibliothèques le demandent parfois.
    {"SteamGameServer_Shutdown", steam_Nothing, 0},
    {"SteamGameServer_RunCallbacks", steam_Nothing, 0},
    {"SteamGameServer_ReleaseCurrentThreadMemory", steam_Nothing, 0},
    {"SteamGameServer_GetHSteamPipe", steam_True, 0},
    {"SteamGameServer_GetHSteamUser", steam_True, 0},
    {"SteamGameServer_GetSteamID", steam_SteamID, 0},
    {"SteamGameServer_BSecure", steam_False, 0},
    // Compte, amis, application.
    {"SteamAPI_ISteamUser_GetSteamID", steam_SteamID, 0},
    {"SteamAPI_ISteamUser_BLoggedOn", steam_True, 0},
    {"SteamAPI_ISteamUser_GetVoiceOptimalSampleRate", steam_VoiceRate, 0},
    {"SteamAPI_ISteamGameServer_GetSteamID", steam_SteamID, 0},
    {"SteamAPI_ISteamFriends_GetPersonaName", steam_PersonaName, 0},
    {"SteamAPI_ISteamFriends_GetPersonaState", steam_True, 0},
    {"SteamAPI_ISteamApps_GetCurrentGameLanguage", steam_Language, 0},
    {"SteamAPI_ISteamApps_GetAvailableGameLanguages", steam_Language, 0},
    {"SteamAPI_ISteamApps_BIsSubscribed", steam_True, 0},
    {"SteamAPI_ISteamApps_BIsAppInstalled", steam_True, 0},
    {"SteamAPI_ISteamApps_BIsSubscribedApp", steam_True, 0},
    {"SteamAPI_ISteamApps_BIsDlcInstalled", steam_False, 0},
    {"SteamAPI_ISteamUtils_GetAppID", steam_AppID, 0},
    {"SteamAPI_ISteamUtils_GetSteamUILanguage", steam_Language, 0},
    {"SteamAPI_ISteamUtils_GetIPCountry", steam_Country, 0},
    {"SteamAPI_ISteamUtils_GetServerRealTime", steam_ServerTime, 0},
    {"SteamAPI_ISteamUtils_GetConnectedUniverse", steam_True, 0},
    // Hors de Steam Desktop : ni Steam Deck, ni mode « grand écran », ni superposition.
    {"SteamAPI_ISteamUtils_IsRunningOnSteamDeck", steam_False, 0},
    {"SteamAPI_ISteamUtils_IsSteamRunningOnSteamDeck", steam_False, 0},
    {"SteamAPI_ISteamUtils_IsSteamInBigPictureMode", steam_False, 0},
    {"SteamAPI_ISteamUtils_IsOverlayEnabled", steam_False, 0},
    // Statistiques et hauts faits : demandés avec succès, mais vides.
    {"SteamAPI_ISteamUserStats_RequestCurrentStats", steam_True, 0},
    {"SteamAPI_ISteamUserStats_StoreStats", steam_True, 0},
    {"SteamAPI_ISteamUserStats_GetAchievement", steam_GetAchievement, 0},
    {"SteamAPI_ISteamUserStats_GetAchievementAchievedPercent", steam_AchievementPercent, 0},
    {"SteamAPI_ISteamUserStats_ResetAllStats", steam_True, 0},
    // Nuage : vide, mais interrogé sans échec.
    {"SteamAPI_ISteamRemoteStorage_GetQuota", steam_Quota, 0},
    // Manettes : Steam Input ne voit aucune manette (celles de l'hôte sont ailleurs).
    {"SteamAPI_ISteamInput_Init", steam_True, 0},
    {"SteamAPI_ISteamInput_Shutdown", steam_Nothing, 0},
};
const size_t rcn_steam_count = sizeof rcn_steam / sizeof rcn_steam[0];

// Fonction Steam demandée par le jeu (64 bits seulement) : la table, sinon un repli d'après le nom.
RcnFn rcn_steam_lookup(const char *name) {
    for (size_t i = 0; i < rcn_steam_count; i++) {
        if (!strcmp(rcn_steam[i].name, name)) return rcn_steam[i].fn;
    }
    log_once(name);
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
