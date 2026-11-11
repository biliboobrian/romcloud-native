// RomCloud Native : mémoire d'un jeu 32 bits.
// Les pointeurs d'un programme x86 32 bits tiennent sur 4 octets et sont utilisés tels quels par
// l'hôte : toute la mémoire que le jeu voit (image, piles, tas, structures que l'hôte lui tend) doit
// donc se trouver sous 4 Go — et même sous 2 Go, comme sous Windows 32 bits. L'image est placée à
// son adresse d'origine ; le reste vient d'une zone réservée d'un bloc, gérée par un allocateur
// simple (blocs chaînés, premier bloc libre qui convient, fusion des voisins libres).
// En 64 bits, la mémoire du jeu est celle de la bibliothèque C de l'hôte.
#include <pthread.h>
#include <stdlib.h>

#include "rcn_internal.h"

#if defined(_WIN32)
__declspec(dllimport) void *__stdcall VirtualAlloc(void *address, size_t size, unsigned long type, unsigned long protect);
__declspec(dllimport) int __stdcall VirtualFree(void *address, size_t size, unsigned long type);
enum { WIN_MEM_COMMIT = 0x1000, WIN_MEM_RESERVE = 0x2000, WIN_MEM_RELEASE = 0x8000, WIN_PAGE_NOACCESS = 1, WIN_PAGE_READWRITE = 4 };
#else
#include <sys/mman.h>
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000 // ignoré par les noyaux qui ne le connaissent pas : l'adresse rendue est vérifiée
#endif
#endif

void *rcn_map_fixed(u64 address, size_t size) {
    void *want = (void *)(uintptr_t)address;
#if defined(_WIN32)
    return VirtualAlloc(want, size, WIN_MEM_RESERVE | WIN_MEM_COMMIT, WIN_PAGE_READWRITE) == want ? want : NULL;
#else
    void *got = mmap(want, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (got == want) return got;
    if (got != MAP_FAILED) munmap(got, size);
    return NULL;
#endif
}

void rcn_unmap_fixed(void *address, size_t size) {
    if (!address) return;
#if defined(_WIN32)
    (void)size;
    VirtualFree(address, 0, WIN_MEM_RELEASE);
#else
    munmap(address, size);
#endif
}

// --- Zone basse ---

#define ARENA_SIZE ((size_t)512 << 20)
#define COMMIT_STEP ((size_t)4 << 20)
#define HEADER 16
#define MIN_BLOCK 32

// En-tête d'un bloc (16 octets, suivi des données alignées sur 16) ; un bloc libre range ses liens
// de liste dans ses données.
typedef struct Block {
    u32 size;      // taille du bloc, en-tête compris (multiple de 16)
    u32 prev_size; // taille du bloc précédent en mémoire (0 : premier bloc)
    u32 used;
    u32 pad;
} Block;
typedef struct FreeLinks {
    struct Block *next, *prev;
} FreeLinks;

static struct {
    pthread_mutex_t lock;
    u8 *base;
    size_t top;       // fin des blocs
    size_t committed; // fin de la mémoire utilisable (Windows : engagée au fur et à mesure)
    Block *free_list;
    Block *last; // dernier bloc en mémoire (celui qui touche le sommet)
} arena = {PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0, NULL, NULL};

#define LINKS(b) ((FreeLinks *)((u8 *)(b) + HEADER))
#define NEXT(b) ((Block *)((u8 *)(b) + (b)->size))
#define PREV(b) ((Block *)((u8 *)(b) - (b)->prev_size))
#define AT_TOP(b) ((b) == arena.last)

static int arena_init(void) {
    if (arena.base) return 1;
    // Première adresse libre parmi quelques-unes, au-dessus des images habituelles (0x400000, 0x10000000).
    for (u64 address = 0x20000000; address <= 0x50000000 && !arena.base; address += 0x10000000) {
#if defined(_WIN32)
        void *want = (void *)(uintptr_t)address;
        if (VirtualAlloc(want, ARENA_SIZE, WIN_MEM_RESERVE, WIN_PAGE_NOACCESS) == want) arena.base = want;
#else
        arena.base = rcn_map_fixed(address, ARENA_SIZE);
        if (arena.base) arena.committed = ARENA_SIZE;
#endif
    }
    return arena.base != NULL;
}

// Étend les blocs de `size` octets au sommet de la zone.
static Block *arena_grow(size_t size) {
    if (size > ARENA_SIZE - arena.top) return NULL;
    size_t end = arena.top + size;
#if defined(_WIN32)
    if (end > arena.committed) {
        size_t target = (end + COMMIT_STEP - 1) / COMMIT_STEP * COMMIT_STEP;
        if (target > ARENA_SIZE) target = ARENA_SIZE;
        if (!VirtualAlloc(arena.base + arena.committed, target - arena.committed, WIN_MEM_COMMIT, WIN_PAGE_READWRITE)) return NULL;
        arena.committed = target;
    }
#endif
    Block *block = (Block *)(arena.base + arena.top);
    arena.top = end;
    return block;
}

static void list_remove(Block *b) {
    FreeLinks *l = LINKS(b);
    if (l->prev) LINKS(l->prev)->next = l->next;
    else arena.free_list = l->next;
    if (l->next) LINKS(l->next)->prev = l->prev;
}
static void list_insert(Block *b) {
    FreeLinks *l = LINKS(b);
    l->prev = NULL;
    l->next = arena.free_list;
    if (arena.free_list) LINKS(arena.free_list)->prev = b;
    arena.free_list = b;
}

static void *low_alloc_locked(size_t size) {
    if (!arena_init() || size > ARENA_SIZE / 2) return NULL;
    u32 need = (u32)((size + 15) & ~(size_t)15) + HEADER;
    if (need < MIN_BLOCK) need = MIN_BLOCK;
    Block *block = arena.free_list;
    while (block && block->size < need) block = LINKS(block)->next;
    if (block) {
        list_remove(block);
        // Le reste du bloc, s'il est utilisable, redevient un bloc libre.
        if (block->size - need >= MIN_BLOCK) {
            Block *rest = (Block *)((u8 *)block + need);
            rest->size = block->size - need;
            rest->prev_size = need;
            rest->used = 0;
            if (AT_TOP(block)) arena.last = rest;
            else NEXT(block)->prev_size = rest->size;
            block->size = need;
            list_insert(rest);
        }
    } else {
        block = arena_grow(need);
        if (!block) return NULL;
        block->size = need;
        block->prev_size = arena.last ? arena.last->size : 0;
        arena.last = block;
    }
    block->used = 1;
    return (u8 *)block + HEADER;
}

void *rcn_low_alloc(size_t size, int zero) {
    pthread_mutex_lock(&arena.lock);
    void *p = low_alloc_locked(size ? size : 1);
    pthread_mutex_unlock(&arena.lock);
    if (p && zero) memset(p, 0, size);
    return p;
}

size_t rcn_low_size(void *p) { return ((Block *)((u8 *)p - HEADER))->size - HEADER; }

void rcn_low_free(void *p) {
    if (!p) return;
    pthread_mutex_lock(&arena.lock);
    Block *block = (Block *)((u8 *)p - HEADER);
    block->used = 0;
    // Fusion avec le bloc suivant, puis avec le précédent, s'ils sont libres.
    if (!AT_TOP(block) && !NEXT(block)->used) {
        Block *next = NEXT(block);
        list_remove(next);
        if (AT_TOP(next)) arena.last = block;
        block->size += next->size;
    }
    if (block->prev_size && !PREV(block)->used) {
        Block *prev = PREV(block);
        list_remove(prev);
        if (AT_TOP(block)) arena.last = prev;
        prev->size += block->size;
        block = prev;
    }
    if (!AT_TOP(block)) NEXT(block)->prev_size = block->size;
    list_insert(block);
    pthread_mutex_unlock(&arena.lock);
}

void *rcn_low_realloc(void *p, size_t size) {
    if (!p) return rcn_low_alloc(size, 0);
    size_t old = rcn_low_size(p);
    if (size <= old) return p;
    void *q = rcn_low_alloc(size, 0);
    if (q) {
        memcpy(q, p, old);
        rcn_low_free(p);
    }
    return q;
}

// --- Mémoire rendue au jeu : zone basse en 32 bits, bibliothèque C de l'hôte en 64 bits ---

void *rcn_galloc(RcnCtx *ctx, size_t size, int zero) {
    if (ctx->bits32) return rcn_low_alloc(size, zero);
    if (!size) size = 1;
    return zero ? calloc(1, size) : malloc(size);
}
void *rcn_grealloc(RcnCtx *ctx, void *p, size_t size) {
    if (ctx->bits32) return rcn_low_realloc(p, size ? size : 1);
    return realloc(p, size ? size : 1);
}
void rcn_gfree(RcnCtx *ctx, void *p) {
    if (ctx->bits32) rcn_low_free(p);
    else free(p);
}
