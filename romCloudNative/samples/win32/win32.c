// Troisième programme de test : fonctions de kernel32 que l'hôte fournit à la place de Windows —
// fichiers (CreateFile…), threads, opérations atomiques, sections critiques, événements, mutex,
// sémaphores, verrous légers et variables de condition, cases de variables locales, texte UTF-16.
// Seul le thread principal écrit : la sortie ne dépend pas de l'ordre d'exécution des threads.
//
// Compilation (llvm-mingw) : x86_64-w64-mingw32-clang -O2 win32.c -o win32.exe
#include <stdio.h>
#include <string.h>
#include <windows.h>

// --- Fichiers ---

static void files(void) {
    printf("CreateDirectory %d, de nouveau %d (erreur %lu)\n", CreateDirectoryA("rcn_dossier", NULL),
           CreateDirectoryA("rcn_dossier", NULL), GetLastError());
    HANDLE h = CreateFileA("rcn_dossier\\Fichier.bin", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    unsigned char data[1000];
    for (int i = 0; i < 1000; i++) data[i] = (unsigned char)(i * 7);
    DWORD n = 0;
    BOOL ok = WriteFile(h, data, sizeof data, &n, NULL);
    LARGE_INTEGER size;
    printf("WriteFile %d %lu, GetFileSizeEx %d %lld, GetFileType %lu\n", ok, n, GetFileSizeEx(h, &size), size.QuadPart,
           GetFileType(h));
    CloseHandle(h);

    // Windows ne distingue pas la casse des noms : même fichier.
    h = CreateFileA("RCN_DOSSIER/fichier.BIN", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    printf("ouverture sous une autre casse : %s\n", h != INVALID_HANDLE_VALUE ? "oui" : "non");
    unsigned char buf[16];
    DWORD pos = SetFilePointer(h, 500, NULL, FILE_BEGIN);
    ok = ReadFile(h, buf, 16, &n, NULL);
    printf("SetFilePointer %lu, ReadFile %d %lu : %u %u %u\n", pos, ok, n, buf[0], buf[1], buf[15]);
    LARGE_INTEGER move, where;
    move.QuadPart = -4;
    ok = SetFilePointerEx(h, move, &where, FILE_END);
    BOOL ok2 = ReadFile(h, buf, 16, &n, NULL);
    printf("SetFilePointerEx %d %lld, ReadFile au-dela de la fin %d %lu : %u %u\n", ok, where.QuadPart, ok2, n, buf[0], buf[3]);
    ok = ReadFile(h, buf, 16, &n, NULL);
    printf("ReadFile a la fin %d %lu, GetFileSize %lu\n", ok, n, GetFileSize(h, NULL));
    CloseHandle(h);

    h = CreateFileW(L"rcn_dossier\\fichier.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 0, NULL);
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, "fin", 3, &n, NULL);
    printf("CreateFileW, ajout : taille %lu\n", GetFileSize(h, NULL));
    CloseHandle(h);

    h = CreateFileA("rcn_dossier\\fichier.bin", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    printf("CREATE_NEW sur un fichier present : %s, erreur %lu\n", h == INVALID_HANDLE_VALUE ? "refuse" : "ouvert", GetLastError());
    h = CreateFileA("rcn_dossier\\absent.bin", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    printf("fichier absent : %s, erreur %lu\n", h == INVALID_HANDLE_VALUE ? "refuse" : "ouvert", GetLastError());
    printf("attributs : dossier %lx, fichier %lx, absent %lx\n", GetFileAttributesA("rcn_dossier"),
           GetFileAttributesA("rcn_dossier\\fichier.bin"), GetFileAttributesA("rcn_dossier\\absent.bin"));
    printf("RemoveDirectory d'un dossier plein %d\n", RemoveDirectoryA("rcn_dossier"));
    printf("DeleteFile %d, de nouveau %d, RemoveDirectory %d\n", DeleteFileA("rcn_dossier\\fichier.bin"),
           DeleteFileA("rcn_dossier\\fichier.bin"), RemoveDirectoryA("rcn_dossier"));
}

// --- Threads et compteurs partagés ---

#define THREADS 4
#define ROUNDS 50000

static volatile LONG atomicCounter;
static volatile LONG64 atomicCounter64;
static volatile LONG casCounter;
static LONG lockedCounter;
static LONG srwCounter;
static LONG mutexCounter;
static CRITICAL_SECTION section;
static SRWLOCK srw = SRWLOCK_INIT;
static HANDLE mutex;
static DWORD tlsIndex;
static __thread int threadLocal = 40;
static volatile LONG tlsErrors;
static volatile LONG bits;

static DWORD WINAPI worker(LPVOID param) {
    int id = (int)(INT_PTR)param;
    // Chaque thread a ses propres variables locales, initialisées de la même façon.
    if (threadLocal != 40 || TlsGetValue(tlsIndex) != NULL) InterlockedIncrement(&tlsErrors);
    threadLocal += id;
    TlsSetValue(tlsIndex, (LPVOID)(INT_PTR)(id * 1000));
    for (int i = 0; i < ROUNDS; i++) {
        InterlockedIncrement(&atomicCounter);
        InterlockedExchangeAdd64(&atomicCounter64, 3);
        for (;;) { // addition par comparaison-échange
            LONG old = casCounter;
            if (InterlockedCompareExchange(&casCounter, old + 2, old) == old) break;
        }
        EnterCriticalSection(&section);
        EnterCriticalSection(&section); // prise imbriquée permise
        lockedCounter++;
        LeaveCriticalSection(&section);
        LeaveCriticalSection(&section);
        AcquireSRWLockExclusive(&srw);
        srwCounter++;
        ReleaseSRWLockExclusive(&srw);
        if (i % 50 == 0) {
            WaitForSingleObject(mutex, INFINITE);
            mutexCounter++;
            ReleaseMutex(mutex);
        }
    }
    InterlockedOr(&bits, 1 << id);
    if (threadLocal != 40 + id || TlsGetValue(tlsIndex) != (LPVOID)(INT_PTR)(id * 1000)) InterlockedIncrement(&tlsErrors);
    return 100 + (DWORD)id;
}

static void threads(void) {
    InitializeCriticalSection(&section);
    mutex = CreateMutexA(NULL, FALSE, NULL);
    tlsIndex = TlsAlloc();
    TlsSetValue(tlsIndex, (LPVOID)(INT_PTR)7);
    HANDLE handles[THREADS];
    DWORD ids[THREADS];
    for (int i = 0; i < THREADS; i++) handles[i] = CreateThread(NULL, 0, worker, (LPVOID)(INT_PTR)(i + 1), 0, &ids[i]);
    DWORD wait = WaitForMultipleObjects(THREADS, handles, TRUE, INFINITE);
    printf("WaitForMultipleObjects %lu, identifiants distincts %d\n", wait,
           ids[0] != ids[1] && ids[1] != ids[2] && ids[2] != ids[3] && ids[0] != GetCurrentThreadId());
    printf("codes de sortie :");
    for (int i = 0; i < THREADS; i++) {
        DWORD code = 0;
        GetExitCodeThread(handles[i], &code);
        printf(" %lu", code);
        CloseHandle(handles[i]);
    }
    printf("\natomique %ld, 64 bits %lld, comparaison-echange %ld\n", atomicCounter, atomicCounter64, casCounter);
    printf("section critique %ld, verrou leger %ld, mutex %ld, bits %lx\n", lockedCounter, srwCounter, mutexCounter, bits);
    printf("variables locales : erreurs %ld, thread principal %d et %d\n", tlsErrors, threadLocal,
           (int)(INT_PTR)TlsGetValue(tlsIndex));
    TlsFree(tlsIndex);
    DeleteCriticalSection(&section);
    CloseHandle(mutex);
}

// --- Événements : deux threads se renvoient la balle ---

static HANDLE ping, pong;
static int rallies;

static DWORD WINAPI ponger(LPVOID param) {
    (void)param;
    for (int i = 0; i < 200; i++) {
        WaitForSingleObject(ping, INFINITE);
        rallies++;
        SetEvent(pong);
    }
    return 0;
}

static void events(void) {
    ping = CreateEventA(NULL, FALSE, FALSE, NULL); // remise à zéro automatique
    pong = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE thread = CreateThread(NULL, 0, ponger, NULL, CREATE_SUSPENDED, NULL);
    DWORD code = 0;
    GetExitCodeThread(thread, &code);
    printf("thread suspendu : code %lu, attente %lx\n", code, WaitForSingleObject(thread, 20));
    ResumeThread(thread);
    for (int i = 0; i < 200; i++) {
        SetEvent(ping);
        WaitForSingleObject(pong, INFINITE);
    }
    printf("echanges %d, fin du thread %lu\n", rallies, WaitForSingleObject(thread, INFINITE));
    CloseHandle(thread);

    HANDLE manual = CreateEventA(NULL, TRUE, TRUE, NULL); // remise à zéro manuelle, signalé
    printf("evenement manuel : %lu %lu,", WaitForSingleObject(manual, 0), WaitForSingleObject(manual, 0));
    ResetEvent(manual);
    printf(" apres ResetEvent %lx\n", WaitForSingleObject(manual, 10));
    printf("evenement automatique non signale : %lx\n", WaitForSingleObject(ping, 0));

    HANDLE semaphore = CreateSemaphoreA(NULL, 2, 3, NULL);
    DWORD a = WaitForSingleObject(semaphore, 0), b = WaitForSingleObject(semaphore, 0), c = WaitForSingleObject(semaphore, 0);
    LONG previous = -1;
    BOOL released = ReleaseSemaphore(semaphore, 3, &previous);
    BOOL tooMany = ReleaseSemaphore(semaphore, 1, NULL);
    printf("semaphore : %lu %lu %lx, rendu %d (avant %ld), de trop %d\n", a, b, c, released, previous, tooMany);
    HANDLE both[2] = {manual, semaphore};
    printf("attente de l'un des deux : %lu\n", WaitForMultipleObjects(2, both, FALSE, 0));
    CloseHandle(ping);
    CloseHandle(pong);
    CloseHandle(manual);
    CloseHandle(semaphore);
}

// --- Variable de condition : file d'attente entre un producteur et des consommateurs ---

static CRITICAL_SECTION queueLock;
static CONDITION_VARIABLE queueReady;
static int queued, produced, done;
static LONG consumed;

static DWORD WINAPI consumer(LPVOID param) {
    (void)param;
    for (;;) {
        EnterCriticalSection(&queueLock);
        while (queued == 0 && !done) SleepConditionVariableCS(&queueReady, &queueLock, INFINITE);
        if (queued == 0) {
            LeaveCriticalSection(&queueLock);
            return 0;
        }
        queued--;
        LeaveCriticalSection(&queueLock);
        InterlockedIncrement(&consumed);
    }
}

static void conditions(void) {
    InitializeCriticalSection(&queueLock);
    InitializeConditionVariable(&queueReady);
    HANDLE handles[3];
    for (int i = 0; i < 3; i++) handles[i] = CreateThread(NULL, 0, consumer, NULL, 0, NULL);
    for (int i = 0; i < 3000; i++) {
        EnterCriticalSection(&queueLock);
        queued++;
        produced++;
        LeaveCriticalSection(&queueLock);
        WakeConditionVariable(&queueReady);
    }
    EnterCriticalSection(&queueLock);
    done = 1;
    LeaveCriticalSection(&queueLock);
    WakeAllConditionVariable(&queueReady);
    WaitForMultipleObjects(3, handles, TRUE, INFINITE);
    for (int i = 0; i < 3; i++) CloseHandle(handles[i]);
    EnterCriticalSection(&queueLock);
    BOOL woken = SleepConditionVariableCS(&queueReady, &queueLock, 10);
    DWORD error = GetLastError();
    LeaveCriticalSection(&queueLock);
    printf("file : produits %d, consommes %ld, attente sans reveil %d (erreur %lu)\n", produced, consumed, woken, error);
    DeleteCriticalSection(&queueLock);
}

// --- Texte et divers ---

static void text(void) {
    const char *utf8 = "caf\xc3\xa9 \xe2\x82\xac \xf0\x9f\x8e\xae"; // café, euro, manette (hors du plan de base)
    wchar_t wide[32];
    int count = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, 32);
    printf("MultiByteToWideChar %d (necessaire %d) : %x %x %x %x, lstrlenW %d\n", count,
           MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0), wide[3], wide[5], wide[7], wide[8], lstrlenW(wide));
    char back[32];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, back, sizeof back, NULL, NULL);
    printf("WideCharToMultiByte %d, identique %d, tampon trop petit %d (erreur %lu)\n", bytes, strcmp(back, utf8) == 0,
           WideCharToMultiByte(CP_UTF8, 0, wide, -1, back, 4, NULL, NULL), GetLastError());

    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, sizeof path);
    printf("GetModuleFileName : %s, GetCommandLine : %s\n", len > 4 && strcmp(path + len - 4, ".exe") == 0 ? ".exe" : "?",
           strstr(GetCommandLineA(), "win32") ? "contient win32" : "?");
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    unsigned long long ticks = ((unsigned long long)now.dwHighDateTime << 32) | now.dwLowDateTime;
    printf("date %s, ", ticks > 133000000000000000ull && ticks < 200000000000000000ull ? "plausible" : "fausse");
    DWORD before = GetTickCount();
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    Sleep(50);
    QueryPerformanceCounter(&end);
    long long ms = (end.QuadPart - start.QuadPart) * 1000 / frequency.QuadPart;
    DWORD elapsed = GetTickCount() - before;
    printf("Sleep(50) : %s, %s\n", elapsed >= 40 && elapsed < 1000 ? "GetTickCount ok" : "GetTickCount faux",
           ms >= 40 && ms < 1000 ? "QueryPerformanceCounter ok" : "QueryPerformanceCounter faux");
}

int main(void) {
    files();
    threads();
    events();
    conditions();
    text();
    return 3;
}
