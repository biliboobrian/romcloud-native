// RomCloud Native : mise en pause d'un thread du jeu par un autre (SuspendThread, ResumeThread,
// GetThreadContext), comme sous Windows. Les ramasse-miettes (Boehm, celui de Mono pour Unity) arrêtent
// ainsi tous les threads, puis parcourent leurs piles et leurs registres à la recherche de pointeurs.
//
// Android : un signal temps réel (SIGRTMIN + 11) porte au thread visé l'adresse de son RcnThread.
// - Interrompu dans du code traduit : ses registres de jeu sont dans ceux de l'ARM, à des places que seul le
//   compilateur connaît. Ils sont tous rangés (31, en mots de 4 octets) au fond de sa pile de jeu, zone
//   jamais atteinte, et l'esp annoncé est ce fond : le parcours prudent de la pile les trouve, avec toute
//   la pile. Le thread attend dans le gestionnaire du signal.
// - Interrompu dans le runtime (fonction Windows, attente, allocateur…), où il peut tenir des verrous :
//   ses registres de jeu sont exacts dans ctx (rangés à l'appel). Il poursuit et s'arrête de lui-même au
//   prochain passage par rcn_call (rcn_suspend_park).
// SuspendThread rend la main une fois le thread arrêté ou dans le runtime. Sur le PC (tests), la pause
// n'est que comptée.
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "rcn_internal.h"

#if !defined(_WIN32)
#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

typedef struct RcnSuspend {
    sem_t ack, go;
} RcnSuspend;

enum { RUNNING, PARKED, IN_RUNTIME, PARKED_CALL };

static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;
static int signal_number;

static void park_wait(RcnThread *t) {
    while (__atomic_load_n(&t->suspend_count, __ATOMIC_SEQ_CST)) {
        while (sem_wait(&t->suspend->go) != 0 && errno == EINTR) {
        }
    }
}

static void suspend_handler(int sig, siginfo_t *info, void *uc) {
    (void)sig;
    RcnThread *t = info->si_value.sival_ptr;
    if (!t || !t->suspend) return;
    int saved_errno = errno;
    mcontext_t *m = &((ucontext_t *)uc)->uc_mcontext;
    RcnCtx *ctx = &t->ctx;
    u32 esp = (u32)ctx->r[4];
    if (rcn_code_module(m->pc)) {
        // Fond de la pile de jeu (limite basse du TEB) : registres de l'ARM, puis ceux de ctx.
        u64 low = ctx->bits32 ? rd32((u64)(uintptr_t)rcn_teb(ctx, TEB_STACK_LIMIT)) : rd64((u64)(uintptr_t)rcn_teb(ctx, TEB_STACK_LIMIT));
        u64 dump = low + 64;
        if (low && dump + 64 * 4 < esp - 0x1000) {
            for (int i = 0; i < 31; i++) wr32(dump + 4 * (u64)i, (u32)m->regs[i]);
            for (int i = 0; i < 16; i++) wr32(dump + 4 * (u64)(31 + i), (u32)ctx->r[i]);
            esp = (u32)dump;
        }
        t->suspend_esp = esp;
        __atomic_store_n(&t->suspend_state, PARKED, __ATOMIC_SEQ_CST);
        sem_post(&t->suspend->ack);
        park_wait(t);
        __atomic_store_n(&t->suspend_state, RUNNING, __ATOMIC_SEQ_CST);
    } else {
        t->suspend_esp = esp;
        __atomic_store_n(&t->suspend_state, IN_RUNTIME, __ATOMIC_SEQ_CST);
        sem_post(&t->suspend->ack);
    }
    errno = saved_errno;
}

static int prepare(RcnThread *t) {
    pthread_mutex_lock(&init_lock);
    if (!signal_number) {
        signal_number = SIGRTMIN + 11;
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = suspend_handler;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(signal_number, &sa, NULL);
    }
    if (!t->suspend) {
        RcnSuspend *s = calloc(1, sizeof *s);
        if (s && sem_init(&s->ack, 0, 0) == 0 && sem_init(&s->go, 0, 0) == 0) t->suspend = s;
        else free(s);
    }
    pthread_mutex_unlock(&init_lock);
    return t->suspend != NULL;
}

u32 rcn_suspend_thread(RcnCtx *ctx, RcnThread *t) {
    u32 previous = __atomic_fetch_add(&t->suspend_count, 1, __ATOMIC_SEQ_CST);
    if (previous || t == THREAD(ctx) || !t->host_tid || !prepare(t)) return previous;
    siginfo_t si;
    memset(&si, 0, sizeof si);
    si.si_signo = signal_number;
    si.si_code = SI_QUEUE;
    si.si_pid = getpid();
    si.si_value.sival_ptr = t;
    if (syscall(SYS_rt_tgsigqueueinfo, getpid(), (pid_t)t->host_tid, signal_number, &si) != 0) return previous;
    // Accusé du thread (arrêté, ou dans le runtime) ; deux secondes au plus.
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += 2;
    while (sem_timedwait(&t->suspend->ack, &until) != 0) {
        if (errno != EINTR) {
            rcn_log("rcn : SuspendThread : le thread %u ne repond pas", THREAD_ID(&t->ctx));
            break;
        }
    }
    return previous;
}

u32 rcn_resume_thread(RcnCtx *ctx, RcnThread *t) {
    (void)ctx;
    u32 count = __atomic_load_n(&t->suspend_count, __ATOMIC_SEQ_CST);
    if (!count) return 0;
    if (__atomic_sub_fetch(&t->suspend_count, 1, __ATOMIC_SEQ_CST) == 0 && t->suspend) {
        u32 state = __atomic_load_n(&t->suspend_state, __ATOMIC_SEQ_CST);
        if (state == PARKED || state == PARKED_CALL) sem_post(&t->suspend->go);
        else __atomic_store_n(&t->suspend_state, RUNNING, __ATOMIC_SEQ_CST);
    }
    return count;
}

// Passage par rcn_call d'un thread dont la pause est demandée alors qu'il était dans le runtime.
void rcn_suspend_park(RcnCtx *ctx) {
    RcnThread *t = THREAD(ctx);
    if (!t->suspend || !__atomic_load_n(&t->suspend_count, __ATOMIC_SEQ_CST)) return;
    __atomic_store_n(&t->suspend_state, PARKED_CALL, __ATOMIC_SEQ_CST);
    park_wait(t);
    __atomic_store_n(&t->suspend_state, RUNNING, __ATOMIC_SEQ_CST);
}

#else
u32 rcn_suspend_thread(RcnCtx *ctx, RcnThread *t) {
    (void)ctx;
    return __atomic_fetch_add(&t->suspend_count, 1, __ATOMIC_SEQ_CST);
}
u32 rcn_resume_thread(RcnCtx *ctx, RcnThread *t) {
    (void)ctx;
    u32 count = t->suspend_count;
    if (count) t->suspend_count--;
    return count;
}
void rcn_suspend_park(RcnCtx *ctx) { (void)ctx; }
#endif

// CONTEXT (x86) d'un thread arrêté : registres de ctx, esp annoncé (fond de la pile s'il a été arrêté dans
// du code traduit). 0 si le thread n'est pas arrêté (rien d'exact à rendre).
int rcn_thread_context32(RcnThread *t, u64 c) {
    if (!__atomic_load_n(&t->suspend_count, __ATOMIC_SEQ_CST) || !t->suspend_esp) return 0;
    RcnCtx *x = &t->ctx;
    u32 flags = rd32(c);
    memset(PTR(c + 4), 0, 0x2CC - 4);
    wr32(c, flags);
    wr32(c + 0x9C, (u32)x->r[7]), wr32(c + 0xA0, (u32)x->r[6]), wr32(c + 0xA4, (u32)x->r[3]);
    wr32(c + 0xA8, (u32)x->r[2]), wr32(c + 0xAC, (u32)x->r[1]), wr32(c + 0xB0, (u32)x->r[0]);
    wr32(c + 0xB4, (u32)x->r[5]), wr32(c + 0xC4, t->suspend_esp);
    wr32(c + 0xC0, 0x202);
    wr32(c + 0xBC, 0x23), wr32(c + 0xC8, 0x2B);
    return 1;
}
