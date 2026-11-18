// RomCloud Native : mémoire d'un jeu 32 bits.
// Les pointeurs d'un programme x86 32 bits tiennent sur 4 octets et sont utilisés tels quels par
// l'hôte : toute la mémoire que le jeu voit (image, piles, tas, structures que l'hôte lui tend) doit
// donc se trouver sous 4 Go — et même sous 2 Go, comme sous Windows 32 bits. L'image est placée à
// son adresse d'origine ; le reste vient d'une zone réservée d'un bloc, gérée par un allocateur
// simple (blocs chaînés, premier bloc libre qui convient, fusion des voisins libres).
// En 64 bits, la mémoire du jeu est celle de la bibliothèque C de l'hôte.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define ARENA_MAX ((size_t)512 << 20)
#define ARENA_MIN ((size_t)32 << 20)
#define ARENA_SIZE (arena.size)
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
    size_t size;      // taille réservée
    size_t top;       // fin des blocs
    size_t committed; // fin de la mémoire utilisable (Windows : engagée au fur et à mesure)
    Block *free_list;
    Block *last; // dernier bloc en mémoire (celui qui touche le sommet)
} arena = {PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0, 0, NULL, NULL};

#define LINKS(b) ((FreeLinks *)((u8 *)(b) + HEADER))
#define NEXT(b) ((Block *)((u8 *)(b) + (b)->size))
#define PREV(b) ((Block *)((u8 *)(b) - (b)->prev_size))
#define AT_TOP(b) ((b) == arena.last)

#if !defined(_WIN32)
// Plus grand intervalle d'adresses libre entre 16 Mo et 2 Go, d'après /proc/self/maps. Dans une
// application Android, le bas de l'espace d'adresses est déjà en partie pris (tas de la machine Java,
// bibliothèques du système) : la zone se loge là où il reste de la place.
static size_t largest_gap(u64 *start) {
    const u64 low = 0x01000000, high = 0x7FFF0000;
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    u64 cursor = low, best_start = 0, best = 0;
    char line[512];
    while (fgets(line, sizeof line, maps)) {
        unsigned long long from, to;
        if (sscanf(line, "%llx-%llx", &from, &to) != 2) continue;
        if (from > high) from = high;
        if (from > cursor && from - cursor > best) {
            best = from - cursor;
            best_start = cursor;
        }
        if (to > cursor) cursor = to;
        if (cursor >= high) break;
    }
    fclose(maps);
    if (cursor < high && high - cursor > best) {
        best = high - cursor;
        best_start = cursor;
    }
    *start = best_start;
    return (size_t)best;
}
#endif

static int arena_init(void) {
    if (arena.base) return 1;
#if defined(_WIN32)
    // Première adresse libre parmi quelques-unes, au-dessus des images habituelles (0x400000, 0x10000000).
    for (size_t size = ARENA_MAX; size >= ARENA_MIN && !arena.base; size /= 2) {
        for (u64 address = 0x20000000; address + size <= 0x7FFF0000 && !arena.base; address += 0x08000000) {
            void *want = (void *)(uintptr_t)address;
            if (VirtualAlloc(want, size, WIN_MEM_RESERVE, WIN_PAGE_NOACCESS) == want) {
                arena.base = want;
                arena.size = size;
            }
        }
    }
#else
    // Dans le plus grand intervalle libre, alignée sur 64 Ko ; plus petite si la place manque.
    u64 start = 0;
    size_t gap = largest_gap(&start);
    start = (start + 0xFFFF) & ~0xFFFFull;
    gap = gap > 0x20000 ? (gap - 0x20000) & ~(size_t)0xFFFF : 0;
    for (size_t size = gap < ARENA_MAX ? gap : ARENA_MAX; size >= ARENA_MIN && !arena.base; size /= 2) {
        arena.base = rcn_map_fixed(start, size);
        if (arena.base) arena.size = arena.committed = size;
    }
    // À défaut (pas de /proc, intervalle pris entre-temps) : quelques adresses habituelles.
    for (size_t size = ARENA_MAX; size >= ARENA_MIN && !arena.base; size /= 2) {
        for (u64 address = 0x08000000; address + size <= 0x7FFF0000 && !arena.base; address += 0x04000000) {
            arena.base = rcn_map_fixed(address, size);
            if (arena.base) arena.size = arena.committed = size;
        }
    }
#endif
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
    if (!arena_init() || size > ARENA_SIZE) return NULL;
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

// Octets du tas du jeu (HeapAlloc, malloc de son CRT) encore alloués : rapport mémoire (rcn_mem_report).
static i64 galloc_live;
#if defined(__ANDROID__) || defined(__linux__)
#include <malloc.h>
#define GSIZE(p) malloc_usable_size(p)
#elif defined(_WIN32)
#include <malloc.h>
#define GSIZE(p) _msize(p)
#endif
void *rcn_galloc(RcnCtx *ctx, size_t size, int zero) {
    if (ctx->bits32) return rcn_low_alloc(size, zero);
    if (!size) size = 1;
    void *p = zero ? calloc(1, size) : malloc(size);
    if (p) __atomic_add_fetch(&galloc_live, (i64)GSIZE(p), __ATOMIC_RELAXED);
    return p;
}
void *rcn_grealloc(RcnCtx *ctx, void *p, size_t size) {
    if (ctx->bits32) return rcn_low_realloc(p, size ? size : 1);
    i64 before = p ? (i64)GSIZE(p) : 0;
    void *q = realloc(p, size ? size : 1);
    if (q) __atomic_add_fetch(&galloc_live, (i64)GSIZE(q) - before, __ATOMIC_RELAXED);
    return q;
}
void rcn_gfree(RcnCtx *ctx, void *p) {
    if (ctx->bits32) rcn_low_free(p);
    else {
        if (p) __atomic_sub_fetch(&galloc_live, (i64)GSIZE(p), __ATOMIC_RELAXED);
        free(p);
    }
}

// Rapport mémoire (mise au point) : tas du jeu, tas natif entier (jeu + DXVK + pilote + runtime), mémoire
// résidente et échangée du processus.
void rcn_mem_report(void) {
    long rss = 0, swap = 0;
#if defined(__ANDROID__) || defined(__linux__)
    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "VmRSS:", 6)) rss = atol(line + 6);
            if (!strncmp(line, "VmSwap:", 7)) swap = atol(line + 7);
        }
        fclose(f);
    }
    struct mallinfo mi = mallinfo();
    fprintf(stderr, "rcn : memoire : tas du jeu %lld Mo, tas natif %zu Mo, residente %ld Mo, echangee %ld Mo\n",
            (long long)(galloc_live >> 20), (size_t)mi.uordblks >> 20, rss >> 10, swap >> 10);
#else
    fprintf(stderr, "rcn : memoire : tas du jeu %lld Mo\n", (long long)(galloc_live >> 20));
    (void)rss, (void)swap;
#endif
}

// --- Mémoire virtuelle d'un jeu 64 bits (VirtualAlloc, VirtualFree, VirtualQuery) ---
// Réserver puis engager par morceaux, comme le fait le ramasse-miettes de .NET : sous Windows, les
// fonctions du système elles-mêmes ; ailleurs, des zones de mmap réservées sans accès puis ouvertes.

enum { VM_COMMIT = 0x1000, VM_RESERVE = 0x2000, VM_DECOMMIT = 0x4000, VM_RELEASE = 0x8000, VM_FREE = 0x10000, VM_PRIVATE = 0x20000 };

#if defined(_WIN32)
__declspec(dllimport) size_t __stdcall VirtualQuery(const void *address, void *info, size_t length);

u64 rcn_vm_alloc(u64 address, u64 size, u32 type, u32 protect) {
    (void)protect;
    // Toujours en lecture-écriture (le code du jeu n'est jamais exécuté depuis sa propre mémoire).
    return (u64)(uintptr_t)VirtualAlloc((void *)(uintptr_t)address, (size_t)size, type, WIN_PAGE_READWRITE);
}
int rcn_vm_free(u64 address, u64 size, u32 type) { return VirtualFree((void *)(uintptr_t)address, (size_t)size, type); }
int rcn_vm_query(u64 address, u64 info) { return VirtualQuery((void *)(uintptr_t)address, (void *)(uintptr_t)info, 48) == 48; }

#else
#include <stdio.h>

// Zones réservées, triées par adresse (recherche dichotomique), sans limite de nombre : l'allocateur
// d'Unreal Engine (FMallocBinned2) réserve des milliers de petites zones ; l'ancienne table de 8192 places
// pleine, toute allocation échouait (Clair Obscur : « Ran out of memory » au chargement du prologue).
typedef struct {
    u64 base, size;
} Region;
static Region *regions;
static size_t region_count, region_cap;
static pthread_mutex_t regions_lock = PTHREAD_MUTEX_INITIALIZER;

// Rang de la première zone dont la base dépasse `address` (verrou tenu).
static size_t region_upper(u64 address) {
    size_t lo = 0, hi = region_count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (regions[mid].base <= address) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}
static Region *region_of(u64 address) {
    size_t k = region_upper(address);
    if (k == 0) return NULL;
    Region *r = &regions[k - 1];
    return address - r->base < r->size ? r : NULL;
}
static int region_add(u64 base, u64 size) {
    if (region_count == region_cap) {
        size_t cap = region_cap ? region_cap * 2 : 4096;
        Region *grown = realloc(regions, cap * sizeof *grown);
        if (!grown) return 0;
        regions = grown, region_cap = cap;
    }
    size_t k = region_upper(base);
    memmove(&regions[k + 1], &regions[k], (region_count - k) * sizeof *regions);
    regions[k].base = base, regions[k].size = size;
    region_count++;
    return 1;
}
static void region_remove(Region *r) {
    size_t k = (size_t)(r - regions);
    memmove(&regions[k], &regions[k + 1], (region_count - k - 1) * sizeof *regions);
    region_count--;
}

u64 rcn_vm_alloc(u64 address, u64 size, u32 type, u32 protect) {
    (void)protect;
    if (!size) return 0;
    const u64 page = 0x1000, granularity = 0x10000;
    if ((type & VM_RESERVE) || !address) {
        // Nouvelle zone, alignée sur 64 Ko comme sous Windows.
        u64 length = (size + granularity - 1) & ~(granularity - 1);
        int prot = (type & VM_COMMIT) ? PROT_READ | PROT_WRITE : PROT_NONE;
        u8 *base = NULL;
        if (address) {
            void *want = (void *)(uintptr_t)(address & ~(granularity - 1));
            void *got = mmap(want, length, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
            if (got != want) {
                if (got != MAP_FAILED) munmap(got, length);
                return 0;
            }
            base = got;
        } else {
            u8 *raw = mmap(NULL, length + granularity, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (raw == MAP_FAILED) return 0;
            base = (u8 *)(((uintptr_t)raw + granularity - 1) & ~(uintptr_t)(granularity - 1));
            if (base > raw) munmap(raw, (size_t)(base - raw));
            u8 *end = raw + length + granularity;
            if (end > base + length) munmap(base + length, (size_t)(end - (base + length)));
        }
        pthread_mutex_lock(&regions_lock);
        int added = region_add((u64)(uintptr_t)base, length);
        pthread_mutex_unlock(&regions_lock);
        if (!added) {
            munmap(base, length);
            return 0;
        }
        return (u64)(uintptr_t)base;
    }
    // Engagement d'une partie d'une zone réservée.
    u64 start = address & ~(page - 1);
    u64 end = (address + size + page - 1) & ~(page - 1);
    if (mprotect((void *)(uintptr_t)start, (size_t)(end - start), PROT_READ | PROT_WRITE) != 0) return 0;
    return start;
}

int rcn_vm_free(u64 address, u64 size, u32 type) {
    const u64 page = 0x1000;
    if (type & VM_RELEASE) {
        pthread_mutex_lock(&regions_lock);
        Region *r = region_of(address);
        int ok = r && r->base == address;
        if (ok) {
            munmap((void *)(uintptr_t)r->base, (size_t)r->size);
            region_remove(r);
        }
        pthread_mutex_unlock(&regions_lock);
        return ok;
    }
    // Désengagement : contenu rendu au système (relu à zéro), accès fermé.
    u64 start = address & ~(page - 1);
    u64 end = (address + size + page - 1) & ~(page - 1);
    madvise((void *)(uintptr_t)start, (size_t)(end - start), MADV_DONTNEED);
    return mprotect((void *)(uintptr_t)start, (size_t)(end - start), PROT_NONE) == 0;
}

// MEMORY_BASIC_INFORMATION (48 octets) d'une zone réservée ; 0 si l'adresse n'en fait pas partie.
int rcn_vm_query(u64 address, u64 info) {
    pthread_mutex_lock(&regions_lock);
    Region *r = region_of(address);
    Region copy = r ? *r : (Region){0, 0};
    pthread_mutex_unlock(&regions_lock);
    if (!copy.size) return 0;
    u64 page = address & ~0xFFFull;
    memset((void *)(uintptr_t)info, 0, 48);
    wr64(info + 0, page);
    wr64(info + 8, copy.base);
    wr32(info + 16, 4);
    wr64(info + 24, copy.base + copy.size - page);
    wr32(info + 32, VM_COMMIT);
    wr32(info + 36, 4); // PAGE_READWRITE
    wr32(info + 40, VM_PRIVATE);
    return 1;
}
#endif