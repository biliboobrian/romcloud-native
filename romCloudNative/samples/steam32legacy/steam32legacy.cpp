// Test steam32legacy : exécutable x86 32 bits sans bibliothèque C qui prend l'ancienne API Steam (avant
// 2013) comme le fait un jeu de l'époque : « SteamUser », « SteamApps »… rendent une interface, et ses
// méthodes s'appellent par la table virtuelle (__thiscall : « this » dans ecx, arguments sur la pile, que
// la méthode retire). Le DLL n'existe pas sous Windows : la comparaison avec l'exécutable d'origine est
// donc sautée (noOriginal dans scripts/test.mjs) ; le runtime fournit ces interfaces (runtime/rcn_steam.c,
// tables de rcn_steam_legacy_tables.h).
//
// Les classes ci-dessous ne décrivent que la *position* des méthodes dans la table virtuelle de
// l'interface (les méthodes d'avant, non appelées ici, sont nommées d'après le SDK) : c'est cet ordre qui
// compte, pas les noms. Un nombre d'arguments faux dans une entrée de table décalerait la pile de
// l'appelant, ce que la suite d'appels (0, 2 et 3 arguments) rendrait visible.
#include <windows.h>

typedef unsigned long long u64;

// CSteamID est une classe à constructeur de copie : rendue par valeur, elle passe par un pointeur caché
// (premier argument de la pile pour une méthode __thiscall). La méthode y écrit l'identifiant et rend ce
// pointeur dans eax — c'est ce que fait Visual C++, et ce que le runtime doit imiter. Les tables comptent
// donc un argument de plus pour ces méthodes (voir tools/gen_steam_ifaces.py).
class CSteamID {
public:
    CSteamID() : id(0) {}
    CSteamID(const CSteamID &autre) : id(autre.id) {}
    u64 id;
};

// ISteamUser, table du SDK : GetHSteamUser, BLoggedOn, GetSteamID…
class ISteamUser {
public:
    virtual unsigned GetHSteamUser();
    virtual bool BLoggedOn();
    virtual CSteamID GetSteamID();
};

// ISteamApps, table du SDK : BIsSubscribed, BIsLowViolence, BIsCybercafe, BIsVACBanned,
// GetCurrentGameLanguage…
class ISteamApps {
public:
    virtual bool BIsSubscribed();
    virtual bool BIsLowViolence();
    virtual bool BIsCybercafe();
    virtual bool BIsVACBanned();
    virtual const char *GetCurrentGameLanguage();
};

// ISteamFriends : GetPersonaName est la première de la table.
class ISteamFriends {
public:
    virtual const char *GetPersonaName();
};

// ISteamUserStats, table v010 (SDK 1.18/1.19) : RequestCurrentStats, GetStat (entier et flottant),
// SetStat (entier et flottant), UpdateAvgRateStat, puis GetAchievement. La version suivante (v011)
// insère GetNumAchievements et GetAchievementName après IndicateAchievementProgress : tout ce qui suit
// se décale, donc c'est bien la v010 que le jeu attend.
class ISteamUserStats {
public:
    virtual bool RequestCurrentStats();
    virtual bool GetStatInt(const char *name, int *value);
    virtual bool GetStatFloat(const char *name, float *value);
    virtual bool SetStatInt(const char *name, int value);
    virtual bool SetStatFloat(const char *name, float value);
    virtual bool UpdateAvgRateStat(const char *name, float count, double rate);
    virtual bool GetAchievement(const char *name, bool *achieved);
};

// ISteamRemoteStorage, table v006 (SDK 1.18/1.19) : FileWrite, FileRead, FileForget, FileDelete,
// FileShare, SetSyncPlatforms, FileExists, FilePersisted, GetFileSize… Les FileWriteStream* du SDK
// suivant (v008) s'insèrent après SetSyncPlatforms et décalent FileExists : cette table est donc la
// bonne pour les jeux de l'époque (Dishonored), pas celle du SDK récent.
class ISteamRemoteStorage {
public:
    virtual bool FileWrite(const char *name, const void *data, int size);
    virtual int FileRead(const char *name, void *data, int size);
    virtual bool FileForget(const char *name);
    virtual bool FileDelete(const char *name);
    virtual unsigned long long FileShare(const char *name);
    virtual bool SetSyncPlatforms(const char *name, int platforms);
    virtual bool FileExists(const char *name);
    virtual bool FilePersisted(const char *name);
    virtual int GetFileSize(const char *name);
};

typedef void *(__cdecl *AccessorFn)(void);

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

static HMODULE steam;
static int found;
static AccessorFn get(const char *name) {
    AccessorFn fn = (AccessorFn)GetProcAddress(steam, name);
    if (fn) found++;
    put(fn ? "." : "!");
    return fn;
}

extern "C" void start(void) {
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    put("bits ");
    put_dec((unsigned)sizeof(void *));
    put("\n");

    steam = LoadLibraryA("steam_api.dll");
    put("charge ");
    put_dec(steam != NULL);
    put("\n");
    if (!steam) ExitProcess(1);

    AccessorFn user_fn = get("SteamUser");
    AccessorFn apps_fn = get("SteamApps");
    AccessorFn friends_fn = get("SteamFriends");
    AccessorFn stats_fn = get("SteamUserStats");
    AccessorFn storage_fn = get("SteamRemoteStorage");
    put("\nproc ");
    put_dec((unsigned)found);
    put("\n");
    if (found != 5) ExitProcess(2);

    // Objets d'interface : non nuls.
    ISteamUser *user = (ISteamUser *)user_fn();
    ISteamApps *apps = (ISteamApps *)apps_fn();
    ISteamFriends *friends = (ISteamFriends *)friends_fn();
    ISteamUserStats *stats = (ISteamUserStats *)stats_fn();
    ISteamRemoteStorage *storage = (ISteamRemoteStorage *)storage_fn();
    put("ifaces ");
    put_dec(user != NULL && apps != NULL && friends != NULL && stats != NULL && storage != NULL);
    put("\n");

    // Appels sans argument : this dans ecx, aucun mot à retirer.
    put("huser ");
    put_dec(user->GetHSteamUser());
    put("\nlogged ");
    put_dec((unsigned)user->BLoggedOn());
    put("\nid ");
    CSteamID me = user->GetSteamID();
    put_u64(me.id);
    put("\nnom ");
    put(friends->GetPersonaName());
    put("\nlangue ");
    put(apps->GetCurrentGameLanguage());
    put("\nsubscribed ");
    put_dec((unsigned)apps->BIsSubscribed());
    put("\n");

    // Ancienne ISteamRemoteStorage : FileExists (case 6), GetFileSize (case 8) et FileRead (case 1).
    // Une table du SDK récent (FileDelete, SetSyncPlatforms…) décalerait la pile, et « encore » le dirait.
    char cloud[4];
    put("nuage ");
    put_dec((unsigned)storage->FileExists("save.dat"));
    put(" taille ");
    put_dec((unsigned)storage->GetFileSize("save.dat"));
    put(" lu ");
    put_dec((unsigned)storage->FileRead("save.dat", cloud, sizeof cloud));
    put("\n");

    // RequestCurrentStats ouvre la table de l'époque (le SDK récent commence par GetStat).
    put("stats ");
    put_dec((unsigned)stats->RequestCurrentStats());
    put("\n");

    // Appel à trois arguments (dont une sortie) : la méthode doit retirer 12 octets, sinon la suite des
    // appels lirait ses variables dans le désordre.
    bool achieved = true;
    put("hautfait ");
    put_dec((unsigned)stats->GetAchievement("ACH_WIN", &achieved));
    put(" obtenu ");
    put_dec((unsigned)achieved);
    put("\n");

    // Un dernier appel sans argument, après ceux qui en ont : la pile doit être intacte.
    put("encore ");
    put_dec((unsigned)apps->BIsSubscribed());
    put("\nfin\n");
    ExitProcess(0);
}
