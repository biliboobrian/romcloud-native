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
void rcn_event_signal(RcnProcess *proc, u64 handle) {
    LOCK(proc);
    RcnObject *object = rcn_handle_get(proc, handle, RCN_OBJ_EVENT);
    if (object) {
        object->signaled = 1;
        WAKE(proc);
    }
    UNLOCK(proc);
}
static void k32_ResetEvent(RcnCtx *ctx) { set_event(ctx, 0); }

// CreateMutexA / W(security, initial_owner, name)
void rcn_k32_create_mutex(RcnCtx *ctx, int owned) {
    RcnObject *object = new_object(RCN_OBJ_MUTEX);
    if (object && owned) {
        object->owner = THREAD_ID(ctx);
        object->count = 1;
    }
    RET(publish(ctx, object));
}
static void k32_CreateMutex(RcnCtx *ctx) { rcn_k32_create_mutex(ctx, (u32)A1 != 0); }
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
    object->thread_id = THREAD_ID(&thread->ctx);
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
    if (thread) {
        rcn_thread_start(thread, thread->start_fn, thread->start_arg);
        RET(1);
        return;
    }
    // Thread en cours « suspendu » par SuspendThread : son compteur seulement.
    LOCK(proc);
    u32 previous = object ? object->count : 0;
    if (object && object->count) object->count--;
    UNLOCK(proc);
    RET(object ? previous : 0xFFFFFFFFu);
}
// SuspendThread : un thread du jeu ne peut pas être arrêté en plein code traduit. Le compteur est tenu,
// le thread continue ; GetThreadContext le montre hors du code du jeu. Les ramasse-miettes (.NET)
// attendent alors qu'il s'arrête de lui-même à son prochain appel système, ce qui suffit.
static void k32_SuspendThread(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    LOCK(proc);
    RcnObject *object = rcn_handle_get(proc, A0, RCN_OBJ_THREAD);
    u32 previous = object ? object->count++ : 0;
    UNLOCK(proc);
    RET(object ? previous : 0xFFFFFFFFu);
}
// GetThreadContext(thread, &context) : registres nuls, rip hors de tout module.
static void k32_GetThreadContext(RcnCtx *ctx) {
    u64 c = A1;
    u32 flags = rd32(c + 0x30);
    memset(PTR(c + 0x34), 0, 0x4D0 - 0x34);
    wr32(c + 0x30, flags);
    wr32(c + 0x34, 0x1F80); // MXCSR
    wr32(c + 0x44, 0x202);  // EFLAGS
    RET(1);
}
static void k32_SetThreadContext(RcnCtx *ctx) { RET(1); }
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

// En 32 bits la structure fait 24 octets : le mutex de l'hôte (8 octets) occupe LockSemaphore et SpinCount.
#define CS_LOCK_COUNT (ctx->bits32 ? 4 : 8)
#define CS_HOST_MUTEX (ctx->bits32 ? 16 : 24)
#define CS_SIZE (ctx->bits32 ? 24 : 40)

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
    memset(PTR(cs), 0, CS_SIZE);
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
        wrp(ctx, (u64)(uintptr_t)rcn_teb(ctx, TEB_TLS_SLOTS) + PTR_SIZE(ctx) * index, 0);
    }
    RET(index < 64);
}
static void k32_TlsGetValue(RcnCtx *ctx) {
    u32 index = (u32)A0;
    LAST_ERROR(ctx) = 0;
    RET(index < 64 ? rdp(ctx, (u64)(uintptr_t)rcn_teb(ctx, TEB_TLS_SLOTS) + PTR_SIZE(ctx) * index) : 0);
}
static void k32_TlsSetValue(RcnCtx *ctx) {
    u32 index = (u32)A0;
    if (index < 64) wrp(ctx, (u64)(uintptr_t)rcn_teb(ctx, TEB_TLS_SLOTS) + PTR_SIZE(ctx) * index, A1);
    RET(index < 64);
}

// --- Variantes « Ex » et duplication de handles ---

// WaitForSingleObjectEx(handle, milliseconds, alertable) : aucun appel asynchrone en attente.
static void k32_WaitForSingleObjectEx(RcnCtx *ctx) { k32_WaitForSingleObject(ctx); }
static void k32_WaitForMultipleObjectsEx(RcnCtx *ctx) { k32_WaitForMultipleObjects(ctx); }
// CreateEventExW(security, name, flags, access) : CREATE_EVENT_MANUAL_RESET 1, CREATE_EVENT_INITIAL_SET 2.
static void k32_CreateEventExW(RcnCtx *ctx) {
    RcnObject *object = new_object(RCN_OBJ_EVENT);
    if (object) {
        object->manual = ((u32)A2 & 1) != 0;
        object->signaled = ((u32)A2 & 2) != 0;
    }
    RET(publish(ctx, object));
}
// DuplicateHandle(source_process, source, target_process, &target, access, inherit, options) : un handle
// de plus sur le même objet ; le pseudo-handle du thread courant donne un vrai handle sur ce thread.
static void k32_DuplicateHandle(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    u64 source = A1, target_out = A3;
    RcnThread *thread = THREAD(ctx);
    LOCK(proc);
    RcnObject *object = NULL;
    if (source == ~1ull) {
        if (!thread->object) {
            thread->object = new_object(RCN_OBJ_THREAD);
            if (thread->object) {
                thread->object->thread_id = THREAD_ID(ctx);
                thread->object->refs = 1; // référence du thread lui-même, rendue à sa fin
            }
        }
        object = thread->object;
    } else {
        object = rcn_handle_get(proc, source, 0);
    }
    u64 handle = 0;
    if (object) {
        object->refs++;
        u64 slot = rcn_handle_new(proc, calloc(1, sizeof(RcnObject)));
        // Le handle désigne l'objet lui-même : on remplace l'objet neuf alloué pour le réserver.
        if (slot) {
            RcnObject *placeholder = proc->handles[(slot - HANDLE_BASE) / 4];
            free(placeholder);
            proc->handles[(slot - HANDLE_BASE) / 4] = object;
            handle = slot;
        } else {
            object->refs--;
        }
    }
    UNLOCK(proc);
    if (target_out) wrp(ctx, target_out, handle);
    if (!handle) LAST_ERROR(ctx) = ERROR_INVALID_HANDLE;
    RET(handle != 0);
}

// --- Pool de threads (CreateThreadpoolWork, Timer, Wait, Io) ---
// Chaque travail soumis, chaque minuterie et chaque attente a son propre thread du jeu ; les fonctions
// du jeu sont appelées comme le ferait le pool de Windows : (instance, contexte, objet[, résultat]).

enum { TP_WORK = 1, TP_TIMER, TP_WAIT, TP_IO };
typedef struct {
    int kind;
    u64 callback, context;
    // Minuterie : génération (une nouvelle échéance annule la précédente), échéance et période.
    volatile u32 generation;
    i64 due_ns;
    u32 period_ms;
    // Attente : objet attendu et délai.
    u64 wait_handle;
    u32 wait_ms;
} TpObject;

// Thread d'un travail, d'une minuterie ou d'une attente : A0 = { TpObject, génération } (libéré ici).
static void tp_thread(RcnCtx *ctx) {
    u64 *job = PTR(A0);
    TpObject *tp = PTR(job[0]);
    u32 generation = (u32)job[1];
    free(job);
    u64 object = (u64)(uintptr_t)tp;
    switch (tp->kind) {
    case TP_WORK: {
        u64 args[3] = {0, tp->context, object};
        rcn_call_guest_n(ctx, tp->callback, 3, args);
        break;
    }
    case TP_TIMER: {
        i64 wait = tp->due_ns;
        do {
            if (wait > 0) rcn_sleep_ms((u32)(wait / 1000000));
            if (__atomic_load_n(&tp->generation, __ATOMIC_SEQ_CST) != generation) break;
            u64 args[3] = {0, tp->context, object};
            rcn_call_guest_n(ctx, tp->callback, 3, args);
            wait = (i64)tp->period_ms * 1000000;
        } while (tp->period_ms && __atomic_load_n(&tp->generation, __ATOMIC_SEQ_CST) == generation);
        break;
    }
    case TP_WAIT: {
        RcnProcess *proc = ctx->proc;
        LOCK(proc);
        RcnObject *target = rcn_handle_get(proc, tp->wait_handle, 0);
        u32 result = target ? wait_objects(ctx, &target, 1, 0, tp->wait_ms) : WAIT_FAILED;
        UNLOCK(proc);
        if (__atomic_load_n(&tp->generation, __ATOMIC_SEQ_CST) == generation) {
            u64 args[4] = {0, tp->context, object, result};
            rcn_call_guest_n(ctx, tp->callback, 4, args);
        }
        break;
    }
    default:
        break;
    }
    RET(0);
}

static void tp_start(RcnCtx *ctx, TpObject *tp) {
    static u64 thunk;
    if (!thunk) thunk = rcn_thunk(ctx, "kernel32", "pool de threads", tp_thread, 0);
    RcnThread *thread = rcn_thread_new(ctx->proc, 0);
    if (!thread) rcn_fail(ctx, "creation d'un thread du pool impossible");
    u64 *job = malloc(2 * sizeof *job);
    if (!job) rcn_fail(ctx, "memoire insuffisante");
    job[0] = (u64)(uintptr_t)tp;
    job[1] = tp->generation;
    if (rcn_thread_start(thread, thunk, (u64)(uintptr_t)job) != 0) rcn_fail(ctx, "creation d'un thread du pool impossible");
}

static u64 tp_new(int kind, u64 callback, u64 context) {
    TpObject *tp = calloc(1, sizeof *tp);
    if (!tp) return 0;
    tp->kind = kind;
    tp->callback = callback;
    tp->context = context;
    return (u64)(uintptr_t)tp;
}
// CreateThreadpoolWork(callback, context, environment)
static void k32_CreateThreadpoolWork(RcnCtx *ctx) { RET(tp_new(TP_WORK, A0, A1)); }
static void k32_SubmitThreadpoolWork(RcnCtx *ctx) { tp_start(ctx, PTR(A0)); }
static void k32_CreateThreadpoolTimer(RcnCtx *ctx) { RET(tp_new(TP_TIMER, A0, A1)); }
// SetThreadpoolTimer(timer, &due_time, period, window) : échéance absolue (FILETIME) ou relative (négative,
// en centaines de nanosecondes) ; NULL arrête la minuterie.
static void k32_SetThreadpoolTimer(RcnCtx *ctx) {
    TpObject *tp = PTR(A0);
    __atomic_add_fetch(&tp->generation, 1, __ATOMIC_SEQ_CST);
    if (!A1) return;
    i64 due = (i64)rd64(A1);
    if (due < 0) {
        tp->due_ns = -due * 100;
    } else {
        i64 now = (i64)((u64)time(NULL) + 11644473600ull) * 10000000ll;
        tp->due_ns = due > now ? (due - now) * 100 : 0;
    }
    tp->period_ms = (u32)A2;
    tp_start(ctx, tp);
}
static void k32_CreateThreadpoolWait(RcnCtx *ctx) { RET(tp_new(TP_WAIT, A0, A1)); }
// SetThreadpoolWait(wait, handle, &timeout) : NULL comme handle arrête l'attente.
static void k32_SetThreadpoolWait(RcnCtx *ctx) {
    TpObject *tp = PTR(A0);
    __atomic_add_fetch(&tp->generation, 1, __ATOMIC_SEQ_CST);
    if (!A1) return;
    tp->wait_handle = A1;
    tp->wait_ms = INFINITE;
    if (A2) {
        i64 t = (i64)rd64(A2);
        tp->wait_ms = t < 0 ? (u32)(-t / 10000) : 0;
    }
    tp_start(ctx, tp);
}
static void k32_WaitForThreadpoolWaitCallbacks(RcnCtx *ctx) { (void)ctx; }
// Fermeture : la génération change, les threads en cours s'arrêtent d'eux-mêmes. L'objet n'est pas
// libéré (un thread peut encore le lire).
static void k32_CloseThreadpoolObject(RcnCtx *ctx) {
    TpObject *tp = PTR(A0);
    if (tp) __atomic_add_fetch(&tp->generation, 1, __ATOMIC_SEQ_CST);
}
// Entrées-sorties asynchrones : jamais demandées par les fichiers ouverts ici (pas d'OVERLAPPED).
static void k32_CreateThreadpoolIo(RcnCtx *ctx) { RET(tp_new(TP_IO, A1, A2)); }
static void k32_ThreadpoolIoNothing(RcnCtx *ctx) { (void)ctx; }

const RcnShim rcn_k32_sync[] = {
    {"CancelThreadpoolIo", k32_ThreadpoolIoNothing},
    {"CloseThreadpoolIo", k32_CloseThreadpoolObject},
    {"CloseThreadpoolTimer", k32_CloseThreadpoolObject},
    {"CloseThreadpoolWait", k32_CloseThreadpoolObject},
    {"CloseThreadpoolWork", k32_CloseThreadpoolObject},
    {"CreateEventExW", k32_CreateEventExW},
    {"CreateThreadpoolIo", k32_CreateThreadpoolIo},
    {"CreateThreadpoolTimer", k32_CreateThreadpoolTimer},
    {"CreateThreadpoolWait", k32_CreateThreadpoolWait},
    {"CreateThreadpoolWork", k32_CreateThreadpoolWork},
    {"DuplicateHandle", k32_DuplicateHandle},
    {"SetThreadpoolTimer", k32_SetThreadpoolTimer},
    {"SetThreadpoolWait", k32_SetThreadpoolWait},
    {"StartThreadpoolIo", k32_ThreadpoolIoNothing},
    {"SubmitThreadpoolWork", k32_SubmitThreadpoolWork},
    {"WaitForMultipleObjectsEx", k32_WaitForMultipleObjectsEx},
    {"WaitForSingleObjectEx", k32_WaitForSingleObjectEx},
    {"WaitForThreadpoolWaitCallbacks", k32_WaitForThreadpoolWaitCallbacks},
    {"AcquireSRWLockExclusive", k32_AcquireSRWLockExclusive},
    {"AcquireSRWLockShared", k32_AcquireSRWLockShared},
    {"CloseHandle", k32_CloseHandle, S32(1)},
    {"CreateEventA", k32_CreateEvent},
    {"CreateEventW", k32_CreateEvent},
    {"CreateMutexA", k32_CreateMutex},
    {"CreateMutexW", k32_CreateMutex},
    {"CreateSemaphoreA", k32_CreateSemaphore},
    {"CreateSemaphoreW", k32_CreateSemaphore},
    {"CreateThread", k32_CreateThread, S32(6)},
    {"DeleteCriticalSection", k32_DeleteCriticalSection, S32(1)},
    {"EnterCriticalSection", k32_EnterCriticalSection, S32(1)},
    {"ExitThread", k32_ExitThread, S32(1)},
    // FlsAlloc(callback) : la fonction de fin de thread n'est pas appelée.
    {"FlsAlloc", k32_TlsAlloc},
    {"FlsFree", k32_TlsFree},
    {"FlsGetValue", k32_TlsGetValue},
    {"FlsSetValue", k32_TlsSetValue},
    {"GetCurrentProcess", k32_GetCurrentProcess, S32(0)},
    {"GetCurrentThread", k32_GetCurrentThread, S32(0)},
    {"GetCurrentThreadId", k32_GetCurrentThreadId, S32(0)},
    {"GetExitCodeThread", k32_GetExitCodeThread, S32(2)},
    {"InitializeConditionVariable", k32_InitializeConditionVariable},
    {"InitializeCriticalSection", k32_InitializeCriticalSection, S32(1)},
    {"InitializeCriticalSectionAndSpinCount", k32_InitializeCriticalSection, S32(2)},
    {"InitializeCriticalSectionEx", k32_InitializeCriticalSection},
    {"InitializeSRWLock", k32_InitializeSRWLock},
    {"LeaveCriticalSection", k32_LeaveCriticalSection, S32(1)},
    {"ReleaseMutex", k32_ReleaseMutex},
    {"ReleaseSRWLockExclusive", k32_ReleaseSRWLockExclusive},
    {"ReleaseSRWLockShared", k32_ReleaseSRWLockShared},
    {"ReleaseSemaphore", k32_ReleaseSemaphore},
    {"ResetEvent", k32_ResetEvent},
    {"ResumeThread", k32_ResumeThread, S32(1)},
    {"SuspendThread", k32_SuspendThread},
    {"GetThreadContext", k32_GetThreadContext},
    {"SetThreadContext", k32_SetThreadContext},
    {"SetEvent", k32_SetEvent},
    {"SleepConditionVariableCS", k32_SleepConditionVariableCS},
    {"SleepConditionVariableSRW", k32_SleepConditionVariableSRW},
    {"SwitchToThread", k32_SwitchToThread},
    {"TlsAlloc", k32_TlsAlloc, S32(0)},
    {"TlsFree", k32_TlsFree, S32(1)},
    {"TlsGetValue", k32_TlsGetValue, S32(1)},
    {"TlsSetValue", k32_TlsSetValue, S32(2)},
    {"TryAcquireSRWLockExclusive", k32_TryAcquireSRWLockExclusive},
    {"TryAcquireSRWLockShared", k32_TryAcquireSRWLockShared},
    {"TryEnterCriticalSection", k32_TryEnterCriticalSection, S32(1)},
    {"WaitForMultipleObjects", k32_WaitForMultipleObjects},
    {"WaitForSingleObject", k32_WaitForSingleObject},
    {"WakeAllConditionVariable", k32_WakeConditionVariable},
    {"WakeConditionVariable", k32_WakeConditionVariable},
};
const size_t rcn_k32_sync_count = sizeof rcn_k32_sync / sizeof rcn_k32_sync[0];
