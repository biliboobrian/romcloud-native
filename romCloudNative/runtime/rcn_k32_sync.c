// RomCloud Native : kernel32, threads et synchronisation — handles, threads, événements, mutex,
// sémaphores, attentes, sections critiques, verrous légers (SRWLOCK), variables de condition, cases
// de variables locales (TlsAlloc).
//
// Les objets à handle partagent le verrou et la variable de condition du processus : tout changement
// d'état réveille toutes les attentes, qui réexaminent leurs objets. Simple et juste ; à affiner si un
// jeu y passe trop de temps.
//
// Limite connue : x86 garantit l'ordre des écritures entre threads (TSO), pas ARM64. Les opérations
// atomiques (préfixe lock) sont traduites en opérations atomiques, mais du code qui se passe de
// verrou en comptant sur cet ordre peut mal se comporter.
#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>

#include "rcn_internal.h"

#if defined(_WIN32)
#include <io.h>
#define close _close
#else
#include <unistd.h>
#endif

enum {
    HANDLE_BASE = 0x1000,
    WAIT_OBJECT_0 = 0,
    WAIT_TIMEOUT = 0x102,
    WAIT_FAILED = 0xFFFFFFFFu,
    INFINITE = 0xFFFFFFFFu,
    STILL_ACTIVE = 259,
    ERROR_INVALID_HANDLE = 6,
    ERROR_NOT_OWNER = 288,
    ERROR_TOO_MANY_POSTS = 298,
    CREATE_SUSPENDED = 4,
    TLS_OUT_OF_INDEXES = 0xFFFFFFFFu,
};

#define LOCK(proc) pthread_mutex_lock(&(proc)->lock)
#define UNLOCK(proc) pthread_mutex_unlock(&(proc)->lock)
#define WAKE(proc) pthread_cond_broadcast(&(proc)->cond)

// --- Handles ---

u64 rcn_handle_new(RcnProcess *proc, RcnObject *object) {
    if (!object) return 0;
    u32 i = 0;
    while (i < proc->handle_cap && proc->handles[i]) i++;
    if (i == proc->handle_cap) {
        u32 cap = proc->handle_cap ? proc->handle_cap * 2 : 64;
        RcnObject **handles = realloc(proc->handles, cap * sizeof *handles);
        if (!handles) {
            free(object);
            return 0;
        }
        memset(handles + proc->handle_cap, 0, (cap - proc->handle_cap) * sizeof *handles);
        proc->handles = handles;
        proc->handle_cap = cap;
    }
    object->refs = 1;
    proc->handles[i] = object;
    return HANDLE_BASE + (u64)i * 4;
}

RcnObject *rcn_handle_get(RcnProcess *proc, u64 handle, int type) {
    u64 i = (handle - HANDLE_BASE) / 4;
    if (handle < HANDLE_BASE || (handle & 3) || i >= proc->handle_cap) return NULL;
    RcnObject *object = proc->handles[i];
    return object && (type == 0 || object->type == type) ? object : NULL;
}

void rcn_object_unref(RcnObject *object) {
    if (--object->refs > 0) return;
    if (object->type == RCN_OBJ_FILE) close(object->fd);
    free(object);
}

void rcn_handles_free(RcnProcess *proc) {
    for (u32 i = 0; i < proc->handle_cap; i++) {
        if (proc->handles[i]) rcn_object_unref(proc->handles[i]);
    }
    free(proc->handles);
    proc->handles = NULL;
    proc->handle_cap = 0;
}

static RcnObject *new_object(int type) {
    RcnObject *object = calloc(1, sizeof *object);
    if (object) object->type = type;
    return object;
}

static void k32_CloseHandle(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    u64 handle = A0;
    if (handle < HANDLE_BASE) { // console, tas, pseudo-handles : rien à fermer
        RET(1);
        return;
    }
    LOCK(proc);
    RcnObject *object = rcn_handle_get(proc, handle, 0);
    if (object) {
        proc->handles[(handle - HANDLE_BASE) / 4] = NULL;
        rcn_object_unref(object);
    }
    UNLOCK(proc);
    if (!object) LAST_ERROR(ctx) = ERROR_INVALID_HANDLE;
    RET(object != NULL);
}

// --- Attentes ---

// L'objet peut-il être obtenu par ce thread ? Avec `take`, il l'est (événement automatique remis à
// zéro, mutex pris, sémaphore décompté).
static int acquire(RcnObject *o, u32 thread_id, int take) {
    switch (o->type) {
    case RCN_OBJ_THREAD:
        return o->signaled;
    case RCN_OBJ_EVENT:
        if (!o->signaled) return 0;
        if (take && !o->manual) o->signaled = 0;
        return 1;
    case RCN_OBJ_MUTEX:
        if (o->owner && o->owner != thread_id) return 0;
        if (take) {
            o->owner = thread_id;
            o->count++;
        }
        return 1;
    case RCN_OBJ_SEMAPHORE:
        if (!o->count) return 0;
        if (take) o->count--;
        return 1;
    default:
        return 1; // fichier : toujours signalé
    }
}

// Échéance d'une attente de `ms` millisecondes.
static struct timespec deadline_after(u32 ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    return ts;
}

// Attend un des objets (ou tous). Verrou du processus tenu ; les objets sont retenus pendant l'attente.
static u32 wait_objects(RcnCtx *ctx, RcnObject **objects, u32 count, int all, u32 ms) {
    RcnProcess *proc = ctx->proc;
    u32 thread_id = THREAD_ID(ctx);
    struct timespec deadline = deadline_after(ms == INFINITE ? 0 : ms);
    u32 result;
    for (u32 i = 0; i < count; i++) objects[i]->refs++;
    for (;;) {
        if (all) {
            u32 ready = 0;
            while (ready < count && acquire(objects[ready], thread_id, 0)) ready++;
            if (ready == count) {
                for (u32 i = 0; i < count; i++) acquire(objects[i], thread_id, 1);
                result = WAIT_OBJECT_0;
                break;
            }
        } else {
            u32 i = 0;
            while (i < count && !acquire(objects[i], thread_id, 1)) i++;
            if (i < count) {
                result = WAIT_OBJECT_0 + i;
                break;
            }
        }
        if (ms == INFINITE) {
            pthread_cond_wait(&proc->cond, &proc->lock);
        } else if (ms == 0 || pthread_cond_timedwait(&proc->cond, &proc->lock, &deadline) == ETIMEDOUT) {
            // Dernier examen à l'échéance : l'état a pu changer sans que ce thread soit réveillé à temps.
            if (ms != 0) {
                ms = 0;
                continue;
            }
            result = WAIT_TIMEOUT;
            break;
        }
    }
    for (u32 i = 0; i < count; i++) rcn_object_unref(objects[i]);
    return result;
}

// WaitForSingleObject(handle, milliseconds)
static void k32_WaitForSingleObject(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    LOCK(proc);
    RcnObject *object = rcn_handle_get(proc, A0, 0);
    u32 result = object ? wait_objects(ctx, &object, 1, 0, (u32)A1) : WAIT_FAILED;
    UNLOCK(proc);
    if (!object) LAST_ERROR(ctx) = ERROR_INVALID_HANDLE;
    RET(result);
}

// WaitForMultipleObjects(count, handles, wait_all, milliseconds)
static void k32_WaitForMultipleObjects(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    u32 count = (u32)A0;
    RcnObject *objects[64];
    u32 result = WAIT_FAILED;
    LOCK(proc);
    u32 n = 0;
    if (count >= 1 && count <= 64) {
        while (n < count && (objects[n] = rcn_handle_get(proc, rd64(A1 + 8 * n), 0))) n++;
        if (n == count) result = wait_objects(ctx, objects, count, (u32)A2 != 0, (u32)A3);
    }
    UNLOCK(proc);
    if (result == WAIT_FAILED) LAST_ERROR(ctx) = ERROR_INVALID_HANDLE;
    RET(result);
}

// --- Événements, mutex, sémaphores ---

static u64 publish(RcnCtx *ctx, RcnObject *object) {
    RcnProcess *proc = ctx->proc;
    LOCK(proc);
    u64 handle = rcn_handle_new(proc, object);
    UNLOCK(proc);
    return handle;
}

// CreateEventA / W(security, manual_reset, initial_state, name) : les noms ne sont pas gérés.
static void k32_CreateEvent(RcnCtx *ctx) {
    RcnObject *object = new_object(RCN_OBJ_EVENT);
    if (object) {
        object->manual = (u32)A1 != 0;
        object->signaled = (u32)A2 != 0;
    }
    RET(publish(ctx, object));
}
static void set_event(RcnCtx *ctx, int signaled) {
    RcnProcess *proc = ctx->proc;
    LOCK(proc);
    RcnObject *object = rcn_handle_get(proc, A0, RCN_OBJ_EVENT);
    if (object) {
        object->signaled = signaled;
        WAKE(proc);
    }
    UNLOCK(proc);
    RET(object != NULL);
}
static void k32_SetEvent(RcnCtx *ctx) { set_event(ctx, 1); }
static void k32_ResetEvent(RcnCtx *ctx) { set_event(ctx, 0); }

// CreateMutexA / W(security, initial_owner, name)
static void k32_CreateMutex(RcnCtx *ctx) {
    RcnObject *object = new_object(RCN_OBJ_MUTEX);
    if (object && (u32)A1) {
        object->owner = THREAD_ID(ctx);
        object->count = 1;
    }
    RET(publish(ctx, object));
}
static void k32_ReleaseMutex(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    LOCK(proc);
    RcnObject *object = rcn_handle_get(proc, A0, RCN_OBJ_MUTEX);
    int ok = object && object->owner == THREAD_ID(ctx);
    if (ok && --object->count == 0) {
        object->owner = 0;
        WAKE(proc);
    }
    UNLOCK(proc);
    if (!ok) LAST_ERROR(ctx) = object ? ERROR_NOT_OWNER : ERROR_INVALID_HANDLE;
    RET(ok);
}

// CreateSemaphoreA / W(security, initial_count, maximum_count, name)
static void k32_CreateSemaphore(RcnCtx *ctx) {
    RcnObject *object = new_object(RCN_OBJ_SEMAPHORE);
    if (object) {
        object->count = (u32)A1;
        object->max = (u32)A2;
    }
    RET(publish(ctx, object));
}
// ReleaseSemaphore(handle, release_count, &previous_count)
static void k32_ReleaseSemaphore(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    u32 add = (u32)A1;
    LOCK(proc);
    RcnObject *object = rcn_handle_get(proc, A0, RCN_OBJ_SEMAPHORE);
    int ok = object && add <= object->max - object->count;
    if (ok) {
        if (A2) wr32(A2, object->count);
        object->count += add;
        WAKE(proc);
    }
    UNLOCK(proc);
    if (!ok) LAST_ERROR(ctx) = object ? ERROR_TOO_MANY_POSTS : ERROR_INVALID_HANDLE;
    RET(ok);
}

// --- Threads ---

u64 rcn_create_thread(RcnCtx *ctx, u64 fn, u64 arg, u64 stack_size, int suspended, u64 thread_id) {
    RcnProcess *proc = ctx->proc;
    RcnThread *thread = rcn_thread_new(proc, (size_t)stack_size);
    RcnObject *object = new_object(RCN_OBJ_THREAD);
    if (!thread || !object) {
        free(object); // le thread non lancé est perdu : cas de mémoire épuisée
        return 0;
    }
    thread->object = object;
    thread->start_fn = fn;
    thread->start_arg = arg;
    object->thread_id = *(u32 *)(thread->teb + TEB_THREAD_ID);
    if (thread_id) wr32(thread_id, object->thread_id);
    LOCK(proc);
    u64 handle = rcn_handle_new(proc, object);
    if (handle) {
        object->refs++; // référence du thread, rendue à sa fin
        if (suspended) object->thread = thread;
    }
    UNLOCK(proc);
    if (!handle) return 0;
    if (!suspended && rcn_thread_start(thread, fn, arg) != 0) {
        LOCK(proc);
        object->signaled = 1; // n'a jamais tourné : toute attente aboutit
        object->refs--;
        UNLOCK(proc);
    }
    return handle;
}

// CreateThread(security, stack_size, start, parameter, flags, &thread_id)
static void k32_CreateThread(RcnCtx *ctx) {
    RET(rcn_create_thread(ctx, A2, A3, A1, ((u32)STK(4) & CREATE_SUSPENDED) != 0, STK(5)));
}
// ResumeThread : lance un thread créé suspendu. Suspendre un thread en cours n'est pas géré.
static void k32_ResumeThread(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    LOCK(proc);
    RcnObject *object = rcn_handle_get(proc, A0, RCN_OBJ_THREAD);
    RcnThread *thread = object ? object->thread : NULL;
    if (thread) object->thread = NULL;
    UNLOCK(proc);
    if (thread) rcn_thread_start(thread, thread->start_fn, thread->start_arg);
    RET(object ? (thread ? 1 : 0) : 0xFFFFFFFFu);
}
static void k32_ExitThread(RcnCtx *ctx) { rcn_thread_exit(ctx, (u32)A0); }
// GetExitCodeThread(handle, &code)
static void k32_GetExitCodeThread(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    LOCK(proc);
    RcnObject *object = rcn_handle_get(proc, A0, RCN_OBJ_THREAD);
    if (object) wr32(A1, object->signaled ? object->exit_code : STILL_ACTIVE);
    UNLOCK(proc);
    RET(object != NULL);
}
static void k32_GetCurrentThread(RcnCtx *ctx) { RET(~1ull); }
static void k32_GetCurrentProcess(RcnCtx *ctx) { RET(~0ull); }
static void k32_GetCurrentThreadId(RcnCtx *ctx) { RET(THREAD_ID(ctx)); }
static void k32_SwitchToThread(RcnCtx *ctx) {
    sched_yield();
    RET(1);
}

// --- Sections critiques ---
// CRITICAL_SECTION du jeu (40 octets) : son champ LockSemaphore (décalage 24) reçoit l'adresse d'un
// mutex récursif de l'hôte, créé par InitializeCriticalSection ou à la première prise.

enum { CS_LOCK_COUNT = 8, CS_HOST_MUTEX = 24 };

static pthread_mutex_t *cs_mutex(RcnCtx *ctx, u64 cs) {
    pthread_mutex_t *mutex = PTR(rd64(cs + CS_HOST_MUTEX));
    if (mutex) return mutex;
    RcnProcess *proc = ctx->proc;
    LOCK(proc);
    mutex = PTR(rd64(cs + CS_HOST_MUTEX));
    if (!mutex) {
        mutex = malloc(sizeof *mutex);
        if (!mutex) abort();
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(mutex, &attr);
        pthread_mutexattr_destroy(&attr);
        wr64(cs + CS_HOST_MUTEX, (u64)(uintptr_t)mutex);
    }
    UNLOCK(proc);
    return mutex;
}
static void k32_InitializeCriticalSection(RcnCtx *ctx) {
    u64 cs = A0;
    memset(PTR(cs), 0, 40);
    wr32(cs + CS_LOCK_COUNT, (u32)-1);
    cs_mutex(ctx, cs);
    RET(1);
}
static void k32_EnterCriticalSection(RcnCtx *ctx) { pthread_mutex_lock(cs_mutex(ctx, A0)); }
static void k32_TryEnterCriticalSection(RcnCtx *ctx) { RET(pthread_mutex_trylock(cs_mutex(ctx, A0)) == 0); }
static void k32_LeaveCriticalSection(RcnCtx *ctx) { pthread_mutex_unlock(cs_mutex(ctx, A0)); }
static void k32_DeleteCriticalSection(RcnCtx *ctx) {
    u64 cs = A0;
    pthread_mutex_t *mutex = PTR(rd64(cs + CS_HOST_MUTEX));
    if (mutex) {
        pthread_mutex_destroy(mutex);
        free(mutex);
        wr64(cs + CS_HOST_MUTEX, 0);
    }
}

// --- Verrous légers (SRWLOCK) et variables de condition ---
// Un mot de 8 octets dans la mémoire du jeu, valide à zéro sans initialisation ni destruction : son
// état y est tenu sous le verrou du processus. SRWLOCK : 0 libre, 1 pris en exclusivité, sinon
// 2 × le nombre de lecteurs. CONDITION_VARIABLE : compteur de réveils.

static void srw_lock(RcnProcess *proc, u64 lock, int shared) {
    while (shared ? rd64(lock) == 1 : rd64(lock) != 0) pthread_cond_wait(&proc->cond, &proc->lock);
    wr64(lock, shared ? rd64(lock) + 2 : 1);
}
static int srw_try(u64 lock, int shared) {
    if (shared ? rd64(lock) == 1 : rd64(lock) != 0) return 0;
    wr64(lock, shared ? rd64(lock) + 2 : 1);
    return 1;
}
static void srw_unlock(RcnProcess *proc, u64 lock, int shared) {
    wr64(lock, shared ? rd64(lock) - 2 : 0);
    WAKE(proc);
}
#define SRW(name, body)                      \
    static void k32_##name(RcnCtx *ctx) {    \
        RcnProcess *proc = ctx->proc;        \
        LOCK(proc);                          \
        body;                                \
        UNLOCK(proc);                        \
    }
SRW(InitializeSRWLock, wr64(A0, 0))
SRW(AcquireSRWLockExclusive, srw_lock(proc, A0, 0))
SRW(AcquireSRWLockShared, srw_lock(proc, A0, 1))
SRW(ReleaseSRWLockExclusive, srw_unlock(proc, A0, 0))
SRW(ReleaseSRWLockShared, srw_unlock(proc, A0, 1))
SRW(TryAcquireSRWLockExclusive, RET(srw_try(A0, 0)))
SRW(TryAcquireSRWLockShared, RET(srw_try(A0, 1)))
SRW(InitializeConditionVariable, wr64(A0, 0))
// Réveiller un seul thread ou tous : tous sont réveillés (un réveil sans raison est permis par Windows).
SRW(WakeConditionVariable, (wr64(A0, rd64(A0) + 1), WAKE(proc)))

// Attend un réveil de la variable de condition, au plus `ms` millisecondes. Verrou du processus tenu.
static int condition_wait(RcnProcess *proc, u64 variable, u32 ms) {
    u64 seen = rd64(variable);
    struct timespec deadline = deadline_after(ms == INFINITE ? 0 : ms);
    while (rd64(variable) == seen) {
        if (ms == INFINITE) pthread_cond_wait(&proc->cond, &proc->lock);
        else if (pthread_cond_timedwait(&proc->cond, &proc->lock, &deadline) == ETIMEDOUT) return rd64(variable) != seen;
    }
    return 1;
}
enum { ERROR_TIMEOUT = 1460, CONDITION_VARIABLE_LOCKMODE_SHARED = 1 };
// SleepConditionVariableCS(variable, critical_section, milliseconds)
static void k32_SleepConditionVariableCS(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    u64 variable = A0;
    u32 ms = (u32)A2;
    pthread_mutex_t *mutex = cs_mutex(ctx, A1);
    LOCK(proc);
    // La section est rendue sous le verrou du processus : aucun réveil ne peut se glisser entre les deux.
    pthread_mutex_unlock(mutex);
    int woken = condition_wait(proc, variable, ms);
    UNLOCK(proc);
    pthread_mutex_lock(mutex);
    if (!woken) LAST_ERROR(ctx) = ERROR_TIMEOUT;
    RET(woken);
}
// SleepConditionVariableSRW(variable, lock, milliseconds, flags)
static void k32_SleepConditionVariableSRW(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    u64 variable = A0, lock = A1;
    u32 ms = (u32)A2;
    int shared = ((u32)A3 & CONDITION_VARIABLE_LOCKMODE_SHARED) != 0;
    LOCK(proc);
    srw_unlock(proc, lock, shared);
    int woken = condition_wait(proc, variable, ms);
    srw_lock(proc, lock, shared);
    UNLOCK(proc);
    if (!woken) LAST_ERROR(ctx) = ERROR_TIMEOUT;
    RET(woken);
}

// --- Cases de variables locales au thread (TlsAlloc, FlsAlloc) ---
// Rangées dans le bloc d'environnement du thread, au même endroit que sous Windows.

static void k32_TlsAlloc(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    u32 index = TLS_OUT_OF_INDEXES;
    LOCK(proc);
    for (u32 i = 0; i < 64; i++) {
        if (!(proc->tls_used >> i & 1)) {
            proc->tls_used |= 1ull << i;
            index = i;
            break;
        }
    }
    UNLOCK(proc);
    RET(index);
}
static void k32_TlsFree(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    u32 index = (u32)A0;
    if (index < 64) {
        LOCK(proc);
        proc->tls_used &= ~(1ull << index);
        UNLOCK(proc);
        wr64((u64)(uintptr_t)THREAD(ctx)->teb + TEB_TLS_SLOTS + 8 * index, 0);
    }
    RET(index < 64);
}
static void k32_TlsGetValue(RcnCtx *ctx) {
    u32 index = (u32)A0;
    LAST_ERROR(ctx) = 0;
    RET(index < 64 ? rd64((u64)(uintptr_t)THREAD(ctx)->teb + TEB_TLS_SLOTS + 8 * index) : 0);
}
static void k32_TlsSetValue(RcnCtx *ctx) {
    u32 index = (u32)A0;
    if (index < 64) wr64((u64)(uintptr_t)THREAD(ctx)->teb + TEB_TLS_SLOTS + 8 * index, A1);
    RET(index < 64);
}

const RcnShim rcn_k32_sync[] = {
    {"AcquireSRWLockExclusive", k32_AcquireSRWLockExclusive},
    {"AcquireSRWLockShared", k32_AcquireSRWLockShared},
    {"CloseHandle", k32_CloseHandle},
    {"CreateEventA", k32_CreateEvent},
    {"CreateEventW", k32_CreateEvent},
    {"CreateMutexA", k32_CreateMutex},
    {"CreateMutexW", k32_CreateMutex},
    {"CreateSemaphoreA", k32_CreateSemaphore},
    {"CreateSemaphoreW", k32_CreateSemaphore},
    {"CreateThread", k32_CreateThread},
    {"DeleteCriticalSection", k32_DeleteCriticalSection},
    {"EnterCriticalSection", k32_EnterCriticalSection},
    {"ExitThread", k32_ExitThread},
    // FlsAlloc(callback) : la fonction de fin de thread n'est pas appelée.
    {"FlsAlloc", k32_TlsAlloc},
    {"FlsFree", k32_TlsFree},
    {"FlsGetValue", k32_TlsGetValue},
    {"FlsSetValue", k32_TlsSetValue},
    {"GetCurrentProcess", k32_GetCurrentProcess},
    {"GetCurrentThread", k32_GetCurrentThread},
    {"GetCurrentThreadId", k32_GetCurrentThreadId},
    {"GetExitCodeThread", k32_GetExitCodeThread},
    {"InitializeConditionVariable", k32_InitializeConditionVariable},
    {"InitializeCriticalSection", k32_InitializeCriticalSection},
    {"InitializeCriticalSectionAndSpinCount", k32_InitializeCriticalSection},
    {"InitializeCriticalSectionEx", k32_InitializeCriticalSection},
    {"InitializeSRWLock", k32_InitializeSRWLock},
    {"LeaveCriticalSection", k32_LeaveCriticalSection},
    {"ReleaseMutex", k32_ReleaseMutex},
    {"ReleaseSRWLockExclusive", k32_ReleaseSRWLockExclusive},
    {"ReleaseSRWLockShared", k32_ReleaseSRWLockShared},
    {"ReleaseSemaphore", k32_ReleaseSemaphore},
    {"ResetEvent", k32_ResetEvent},
    {"ResumeThread", k32_ResumeThread},
    {"SetEvent", k32_SetEvent},
    {"SleepConditionVariableCS", k32_SleepConditionVariableCS},
    {"SleepConditionVariableSRW", k32_SleepConditionVariableSRW},
    {"SwitchToThread", k32_SwitchToThread},
    {"TlsAlloc", k32_TlsAlloc},
    {"TlsFree", k32_TlsFree},
    {"TlsGetValue", k32_TlsGetValue},
    {"TlsSetValue", k32_TlsSetValue},
    {"TryAcquireSRWLockExclusive", k32_TryAcquireSRWLockExclusive},
    {"TryAcquireSRWLockShared", k32_TryAcquireSRWLockShared},
    {"TryEnterCriticalSection", k32_TryEnterCriticalSection},
    {"WaitForMultipleObjects", k32_WaitForMultipleObjects},
    {"WaitForSingleObject", k32_WaitForSingleObject},
    {"WakeAllConditionVariable", k32_WakeConditionVariable},
    {"WakeConditionVariable", k32_WakeConditionVariable},
};
const size_t rcn_k32_sync_count = sizeof rcn_k32_sync / sizeof rcn_k32_sync[0];
