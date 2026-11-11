// RomCloud Native : chargement de l'exécutable d'origine (données, relocalisations, imports) et
// exécution du code traduit. L'image est chargée à une adresse quelconque de l'hôte : le code traduit
// adresse tout relativement à ctx->base, et les pointeurs absolus des données sont relocalisés.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rcn_internal.h"

extern const RcnModule rcn_module;

#define STACK_SIZE (8u << 20)
#define TEB_SIZE 0x3000

// --- Mémoire alignée sur une page ---
#if defined(_WIN32)
#include <malloc.h>
void *rcn_page_alloc(size_t size) {
    void *p = _aligned_malloc(size, 4096);
    if (p) memset(p, 0, size);
    return p;
}
void rcn_page_free(void *p) { _aligned_free(p); }
#else
void *rcn_page_alloc(size_t size) {
    void *p = NULL;
    if (posix_memalign(&p, 4096, size) != 0) return NULL;
    memset(p, 0, size);
    return p;
}
void rcn_page_free(void *p) { free(p); }
#endif

static void set_error(RcnProcess *proc, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(proc->result->error, sizeof proc->result->error, fmt, args);
    va_end(args);
}

// --- Appels ---

static RcnFn find_func(const RcnModule *m, u32 rva) {
    u32 lo = 0, hi = m->func_count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (m->funcs[mid].rva < rva) lo = mid + 1;
        else hi = mid;
    }
    return lo < m->func_count && m->funcs[lo].rva == rva ? m->funcs[lo].fn : NULL;
}

void rcn_call(RcnCtx *ctx, u64 target) {
    RcnProcess *proc = ctx->proc;
    u64 rva = target - ctx->base;
    if (rva < ctx->image_size) {
        RcnFn fn = find_func(proc->module, (u32)rva);
        if (!fn) rcn_trap(ctx, rva, "appel d'une adresse qui n'a pas ete traduite");
        fn(ctx);
        return;
    }
    const RcnImport *first = proc->imports;
    const RcnImport *imp = (const RcnImport *)(uintptr_t)target;
    if (imp >= first && imp < first + proc->import_count) {
        proc->current_import = imp;
        imp->fn(ctx);
        ctx->r[4] += 8; // adresse de retour empilée par l'appelant
        return;
    }
    set_error(proc, "appel d'une adresse inconnue : 0x%llx", (unsigned long long)target);
    proc->result->status = RCN_ERR_TRAP;
    longjmp(proc->exit_jmp, RCN_JMP_TRAP);
}

_Noreturn void rcn_trap(RcnCtx *ctx, u64 rva, const char *what) {
    RcnProcess *proc = ctx->proc;
    set_error(proc, "%s (%s+0x%llx)", what, proc->module->name, (unsigned long long)rva);
    proc->result->status = RCN_ERR_TRAP;
    longjmp(proc->exit_jmp, RCN_JMP_TRAP);
}

_Noreturn void rcn_fail(RcnCtx *ctx, const char *fmt, ...) {
    RcnProcess *proc = ctx->proc;
    va_list args;
    va_start(args, fmt);
    vsnprintf(proc->result->error, sizeof proc->result->error, fmt, args);
    va_end(args);
    proc->result->status = RCN_ERR_TRAP;
    longjmp(proc->exit_jmp, RCN_JMP_TRAP);
}

_Noreturn void rcn_exit(RcnCtx *ctx, u32 code) {
    ctx->proc->result->exit_code = code;
    longjmp(ctx->proc->exit_jmp, RCN_JMP_EXIT);
}

u64 rcn_call_guest(RcnCtx *ctx, u64 fn, u64 a0, u64 a1, u64 a2, u64 a3) {
    u64 rsp = ctx->r[4];
    // Convention Windows x64 : pile alignée sur 16 octets avant l'appel, 32 octets réservés à l'appelé.
    ctx->r[4] = ((rsp - 0x40) & ~0xFull);
    ctx->r[1] = a0;
    ctx->r[2] = a1;
    ctx->r[8] = a2;
    ctx->r[9] = a3;
    PUSH64(RCN_RETURN_SENTINEL);
    rcn_call(ctx, fn);
    ctx->r[4] = rsp;
    return ctx->r[0];
}

void rcn_host_write(RcnCtx *ctx, int stream, const void *data, size_t len) {
    const RcnHost *host = ctx->proc->host;
    if (len) host->write(host->user, stream, data, len);
}

u64 rcn_now_ns(void) {
    struct timespec ts;
#if defined(_WIN32)
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

// --- Chemins de fichiers ---

#if !defined(_WIN32)
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
// Windows ne distingue pas majuscules et minuscules dans les noms de fichiers : si `name` n'existe pas
// dans `dir`, le remplace par l'entrée du dossier qui n'en diffère que par la casse.
static void match_case(const char *dir, char *name) {
    DIR *d = opendir(dir);
    if (!d) return;
    for (struct dirent *e; (e = readdir(d));) {
        if (strcasecmp(e->d_name, name) == 0) {
            memcpy(name, e->d_name, strlen(name));
            break;
        }
    }
    closedir(d);
}
#endif

char *rcn_host_path(RcnCtx *ctx, const char *path) {
    const char *root = ctx->proc->game_dir;
    // Lettre de lecteur et barres initiales retirées : tout chemin est relatif au dossier du jeu.
    if (path[0] && path[1] == ':') path += 2;
    while (*path == '\\' || *path == '/') path++;
    size_t root_len = strlen(root), len = strlen(path);
    char *out = malloc(root_len + len + 2);
    if (!out) return NULL;
    memcpy(out, root, root_len);
    out[root_len] = '/';
    memcpy(out + root_len + 1, path, len + 1);
    for (char *p = out + root_len + 1; *p; p++) {
        if (*p == '\\') *p = '/';
    }
#if !defined(_WIN32)
    // Composant par composant, tant que le chemin existe.
    for (char *name = out + root_len + 1; *name;) {
        char *end = strchr(name, '/');
        if (end) *end = 0;
        if (access(out, F_OK) != 0) {
            name[-1] = 0;
            match_case(out, name);
            name[-1] = '/';
        }
        if (!end) break;
        *end = '/';
        name = end + 1;
    }
#endif
    return out;
}

// --- setjmp / longjmp du jeu ---
// Le code traduit appelle le setjmp de l'hôte dans la fonction C qui correspond à la fonction du jeu
// (voir le recompilateur) : longjmp déroule alors la pile de l'hôte jusqu'à elle, comme dans le jeu.

jmp_buf *rcn_setjmp(RcnCtx *ctx, u64 guest_buf) {
    RcnProcess *proc = ctx->proc;
    RcnJmp *slot = &proc->jmps[0];
    for (int i = 0; i < RCN_JMP_SLOTS; i++) {
        RcnJmp *j = &proc->jmps[i];
        if (j->guest_buf == guest_buf) {
            slot = j;
            break;
        }
        if (j->used < slot->used) slot = j;
    }
    slot->guest_buf = guest_buf;
    slot->used = ++proc->jmp_clock;
    memcpy(slot->gpr, ctx->r, sizeof slot->gpr);
    memcpy(slot->xmm, &ctx->xmm[6], sizeof slot->xmm);
    return &slot->host;
}

_Noreturn void rcn_longjmp(RcnCtx *ctx, u64 guest_buf, int value) {
    RcnProcess *proc = ctx->proc;
    for (int i = 0; i < RCN_JMP_SLOTS; i++) {
        RcnJmp *j = &proc->jmps[i];
        if (j->used && j->guest_buf == guest_buf) {
            // Registres que la convention d'appel préserve, pile comprise ; rax : valeur de retour de setjmp.
            memcpy(ctx->r, j->gpr, sizeof j->gpr);
            memcpy(&ctx->xmm[6], j->xmm, sizeof j->xmm);
            ctx->r[0] = (u32)(value ? value : 1);
            longjmp(j->host, 1);
        }
    }
    rcn_fail(ctx, "longjmp vers un setjmp inconnu");
}

// Import que l'hôte ne fournit pas : le programme s'arrête en le nommant s'il l'appelle.
static void missing_import(RcnCtx *ctx) {
    RcnProcess *proc = ctx->proc;
    const RcnImport *imp = proc->current_import;
    set_error(proc, "fonction Windows non fournie : %s!%s", imp->dll, imp->name);
    proc->result->status = RCN_ERR_TRAP;
    longjmp(proc->exit_jmp, RCN_JMP_TRAP);
}

// --- Instructions fournies par le runtime ---

void rcn_cpuid(RcnCtx *ctx) {
    u32 leaf = (u32)ctx->r[0];
    u32 a = 0, b = 0, c = 0, d = 0;
    switch (leaf) {
    case 0:
        a = 1;
        b = 0x756e6547; // « GenuineIntel »
        d = 0x49656e69;
        c = 0x6c65746e;
        break;
    case 1:
        a = 0x000306a9;
        // Seules les extensions que le traducteur gère sont annoncées : SSE, SSE2 (edx), POPCNT (ecx).
        d = (1u << 0) | (1u << 4) | (1u << 8) | (1u << 15) | (1u << 24) | (1u << 25) | (1u << 26);
        c = 1u << 23;
        break;
    case 0x80000000u:
        a = 0x80000001u;
        break;
    case 0x80000001u:
        d = 1u << 29; // mode 64 bits
        c = 1u << 5;  // LZCNT
        break;
    }
    ctx->r[0] = a;
    ctx->r[3] = b;
    ctx->r[1] = c;
    ctx->r[2] = d;
}

void rcn_rdtsc(RcnCtx *ctx) {
    struct timespec ts;
#if defined(_WIN32)
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    u64 t = (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
    ctx->r[0] = (u32)t;
    ctx->r[2] = (u32)(t >> 32);
}

static u64 load(u64 a, int size) {
    switch (size) {
    case 1: return rd8(a);
    case 2: return rd16(a);
    case 4: return rd32(a);
    default: return rd64(a);
    }
}
static void store(u64 a, int size, u64 v) {
    switch (size) {
    case 1: wr8(a, (u8)v); break;
    case 2: wr16(a, (u16)v); break;
    case 4: wr32(a, (u32)v); break;
    default: wr64(a, v); break;
    }
}
// Drapeaux de a - b sur `size` octets (cmps, scas).
static void flags_sub(RcnCtx *ctx, u64 a, u64 b, int size) {
    int top = size * 8 - 1;
    u64 mask = size == 8 ? ~0ull : (1ull << (size * 8)) - 1;
    u64 r = (a - b) & mask;
    ctx->cf = a < b;
    ctx->zf = r == 0;
    ctx->sf = (r >> top) & 1;
    ctx->of = (((a ^ b) & (a ^ r)) >> top) & 1;
    ctx->af = ((a ^ b ^ r) >> 4) & 1;
    ctx->pf = rcn_parity((u8)r);
}
static void set_acc(RcnCtx *ctx, int size, u64 v) {
    switch (size) {
    case 1: ctx->r[0] = (ctx->r[0] & ~0xFFull) | (u8)v; break;
    case 2: ctx->r[0] = (ctx->r[0] & ~0xFFFFull) | (u16)v; break;
    case 4: ctx->r[0] = (u32)v; break;
    default: ctx->r[0] = v; break;
    }
}

void rcn_movs(RcnCtx *ctx, int size, int rep) {
    i64 step = ctx->df ? -size : size;
    for (u64 n = rep ? ctx->r[1] : 1; n; n--) {
        store(ctx->r[7], size, load(ctx->r[6], size));
        ctx->r[6] += step;
        ctx->r[7] += step;
    }
    if (rep) ctx->r[1] = 0;
}
void rcn_stos(RcnCtx *ctx, int size, int rep) {
    i64 step = ctx->df ? -size : size;
    for (u64 n = rep ? ctx->r[1] : 1; n; n--) {
        store(ctx->r[7], size, ctx->r[0]);
        ctx->r[7] += step;
    }
    if (rep) ctx->r[1] = 0;
}
void rcn_lods(RcnCtx *ctx, int size, int rep) {
    i64 step = ctx->df ? -size : size;
    for (u64 n = rep ? ctx->r[1] : 1; n; n--) {
        set_acc(ctx, size, load(ctx->r[6], size));
        ctx->r[6] += step;
    }
    if (rep) ctx->r[1] = 0;
}
// repe : tant que les éléments sont égaux ; repne : tant qu'ils diffèrent.
void rcn_cmps(RcnCtx *ctx, int size, int rep) {
    i64 step = ctx->df ? -size : size;
    do {
        if (rep && ctx->r[1] == 0) return;
        flags_sub(ctx, load(ctx->r[6], size), load(ctx->r[7], size), size);
        ctx->r[6] += step;
        ctx->r[7] += step;
        if (rep) ctx->r[1]--;
    } while ((rep == 1 && ctx->zf) || (rep == 2 && !ctx->zf));
}
void rcn_scas(RcnCtx *ctx, int size, int rep) {
    i64 step = ctx->df ? -size : size;
    u64 mask = size == 8 ? ~0ull : (1ull << (size * 8)) - 1;
    do {
        if (rep && ctx->r[1] == 0) return;
        flags_sub(ctx, ctx->r[0] & mask, load(ctx->r[7], size), size);
        ctx->r[7] += step;
        if (rep) ctx->r[1]--;
    } while ((rep == 1 && ctx->zf) || (rep == 2 && !ctx->zf));
}

// --- Chargement de l'exécutable ---

typedef struct {
    const u8 *data;
    size_t size;
} Bytes;

static int in_range(size_t off, size_t len, size_t size) { return off <= size && len <= size - off; }
static u32 get32(Bytes b, size_t off) { return in_range(off, 4, b.size) ? rd32((u64)(uintptr_t)(b.data + off)) : 0; }
static u16 get16(Bytes b, size_t off) { return in_range(off, 2, b.size) ? rd16((u64)(uintptr_t)(b.data + off)) : 0; }
static u64 get64(Bytes b, size_t off) { return in_range(off, 8, b.size) ? rd64((u64)(uintptr_t)(b.data + off)) : 0; }

static u8 *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    u8 *data = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long len = ftell(f);
        if (len > 0 && fseek(f, 0, SEEK_SET) == 0) {
            data = malloc((size_t)len);
            if (data && fread(data, 1, (size_t)len, f) != (size_t)len) {
                free(data);
                data = NULL;
            }
            *size = (size_t)len;
        }
    }
    fclose(f);
    return data;
}

// Chaîne terminée par un zéro dans l'image, ou "" si elle en sort.
static const char *image_str(Bytes img, u32 rva) {
    if (rva >= img.size || !memchr(img.data + rva, 0, img.size - rva)) return "";
    return (const char *)img.data + rva;
}

// Charge l'exécutable : sections, relocalisations, imports. Renvoie 0 en cas de succès.
static int load_image(RcnProcess *proc, const char *path) {
    const RcnModule *m = proc->module;
    size_t file_size = 0;
    u8 *file_data = read_file(path, &file_size);
    if (!file_data) {
        set_error(proc, "executable illisible : %s", path);
        return -1;
    }
    Bytes file = {file_data, file_size};
    int status = -1;
    u32 nt = get32(file, 0x3c);
    u32 opt = nt + 24;
    if (get16(file, 0) != 0x5a4d || get32(file, nt) != 0x4550 || get16(file, nt + 4) != 0x8664 || get16(file, opt) != 0x20b) {
        set_error(proc, "ce n'est pas un executable Windows x86-64 : %s", path);
        goto done;
    }
    u32 image_size = get32(file, opt + 56);
    // Le code traduit ne vaut que pour l'exécutable dont il vient.
    if (image_size != m->image_size || get32(file, nt + 8) != m->timestamp || get32(file, opt + 16) != m->entry_rva ||
        get64(file, opt + 24) != m->image_base) {
        set_error(proc, "cet executable n'est pas celui qui a ete traduit (%s)", m->name);
        goto done;
    }
    u8 *image = rcn_page_alloc(image_size);
    if (!image) {
        set_error(proc, "memoire insuffisante pour l'image (%u octets)", image_size);
        goto done;
    }
    proc->image = image;
    Bytes img = {image, image_size};

    u32 headers = get32(file, opt + 60);
    if (headers > file_size) headers = (u32)file_size;
    if (headers > image_size) headers = image_size;
    memcpy(image, file_data, headers);
    u32 section_count = get16(file, nt + 6);
    u32 table = opt + get16(file, nt + 20);
    for (u32 i = 0; i < section_count; i++) {
        u32 s = table + i * 40;
        u32 vsize = get32(file, s + 8), rva = get32(file, s + 12);
        u32 raw_size = get32(file, s + 16), raw_off = get32(file, s + 20);
        u32 n = vsize && vsize < raw_size ? vsize : raw_size;
        if (!in_range(raw_off, n, file_size) || !in_range(rva, n, image_size)) {
            set_error(proc, "section %u hors de l'executable", i);
            goto done;
        }
        memcpy(image + rva, file_data + raw_off, n);
    }

    u32 dir_count = get32(file, opt + 108);
    // Relocalisations : pointeurs absolus des données, décalés de l'écart avec l'adresse prévue.
    u64 delta = (u64)(uintptr_t)image - m->image_base;
    u32 reloc = dir_count > 5 ? get32(file, opt + 112 + 5 * 8) : 0;
    u32 reloc_size = dir_count > 5 ? get32(file, opt + 112 + 5 * 8 + 4) : 0;
    if (!reloc) {
        set_error(proc, "executable sans table de relocalisations : il ne peut pas etre charge");
        goto done;
    }
    for (u32 off = 0; off + 8 <= reloc_size;) {
        u32 page = get32(img, reloc + off), block = get32(img, reloc + off + 4);
        if (block < 8) break;
        for (u32 e = 8; e + 2 <= block && off + e + 2 <= reloc_size; e += 2) {
            u16 entry = get16(img, reloc + off + e);
            u32 at = page + (entry & 0xfff);
            if ((entry >> 12) == 10 && in_range(at, 8, image_size)) wr64((u64)(uintptr_t)(image + at), get64(img, at) + delta);
        }
        off += block;
    }

    // Imports : chaque case de la table reçoit l'adresse d'un RcnImport (reconnue par rcn_call).
    u32 imp_dir = dir_count > 1 ? get32(file, opt + 112 + 8) : 0;
    u32 count = 0;
    for (int pass = 0; pass < 2 && imp_dir; pass++) {
        if (pass == 1) {
            proc->imports = calloc(count ? count : 1, sizeof(RcnImport));
            if (!proc->imports) {
                set_error(proc, "memoire insuffisante");
                goto done;
            }
            proc->import_count = count;
        }
        u32 k = 0;
        for (u32 d = imp_dir; in_range(d, 20, image_size); d += 20) {
            u32 ilt = get32(img, d), name_rva = get32(img, d + 12), iat = get32(img, d + 16);
            if (!name_rva && !iat) break;
            const char *dll = image_str(img, name_rva);
            for (u32 i = 0;; i++, k++) {
                u64 entry = get64(img, (ilt ? ilt : iat) + i * 8);
                if (!entry || !in_range(iat + i * 8, 8, image_size)) break;
                if (pass == 0) continue;
                RcnImport *imp = &proc->imports[k];
                imp->dll = dll;
                imp->name = (entry >> 63) ? "(ordinal)" : image_str(img, (u32)entry + 2);
                imp->fn = rcn_win32_lookup(dll, imp->name);
                if (!imp->fn) imp->fn = missing_import;
                wr64((u64)(uintptr_t)(image + iat + i * 8), (u64)(uintptr_t)imp);
            }
        }
        count = k;
    }
    status = 0;
done:
    free(file_data);
    return status;
}

// Variables locales au thread : copie du modèle de l'exe, rangée dans le tableau que le code lit par
// gs:[0x58]. Renvoie l'adresse de la liste des fonctions à appeler avant l'entrée du programme (ou 0).
static u64 setup_tls(RcnProcess *proc, u64 teb) {
    Bytes img = {proc->image, proc->module->image_size};
    u32 opt = get32(img, 0x3c) + 24;
    u32 dir = get32(img, opt + 108) > 9 ? get32(img, opt + 112 + 9 * 8) : 0;
    wr64(teb + 0x58, (u64)(uintptr_t)proc->tls_slots);
    if (!dir || !in_range(dir, 40, img.size)) return 0;
    // Adresses absolues, déjà relocalisées : modèle [start, end), puis une zone à zéro.
    u64 base = (u64)(uintptr_t)proc->image;
    u64 start = get64(img, dir), end = get64(img, dir + 8), index = get64(img, dir + 16);
    u64 callbacks = get64(img, dir + 24);
    u32 zero_fill = get32(img, dir + 32);
    if (start < base || end < start || end - base > img.size) return 0;
    size_t size = (size_t)(end - start);
    proc->tls_block = rcn_page_alloc(size + zero_fill + 16);
    if (!proc->tls_block) return 0;
    memcpy(proc->tls_block, PTR(start), size);
    proc->tls_slots[0] = (u64)(uintptr_t)proc->tls_block;
    if (index - base < img.size) wr32(index, 0);
    return callbacks - base < img.size ? callbacks : 0;
}

// Dossier et nom de l'exécutable (séparateurs « / » ou « \ »).
static int setup_paths(RcnProcess *proc, const char *exe_path) {
    const char *name = exe_path;
    for (const char *p = exe_path; *p; p++) {
        if (*p == '/' || *p == '\\') name = p + 1;
    }
    size_t dir_len = (size_t)(name - exe_path);
    proc->exe_name = malloc(strlen(name) + 1);
    proc->game_dir = malloc(dir_len + 2);
    if (!proc->exe_name || !proc->game_dir) return -1;
    strcpy(proc->exe_name, name);
    if (dir_len == 0) {
        strcpy(proc->game_dir, ".");
    } else {
        // Sans la barre finale (sauf pour la racine).
        memcpy(proc->game_dir, exe_path, dir_len);
        proc->game_dir[dir_len > 1 ? dir_len - 1 : dir_len] = 0;
    }
    proc->crt_argv_data[0] = proc->exe_name;
    proc->crt_argv = proc->crt_argv_data;
    proc->crt_argc = 1;
    proc->crt_environ = proc->crt_environ_data;
    return 0;
}

static void default_write(void *user, int stream, const char *data, size_t len) {
    (void)user;
    fwrite(data, 1, len, stream == RCN_STREAM_STDERR ? stderr : stdout);
}

const char *rcn_port_name(void) { return rcn_module.name; }

int rcn_port_run(const char *exe_path, const RcnHost *host, RcnResult *result) {
    static const RcnHost default_host = {RCN_HOST_ABI, NULL, default_write};
    memset(result, 0, sizeof *result);
    if (!host) host = &default_host;
    if (host->abi != RCN_HOST_ABI || rcn_module.abi != RCN_ABI_VERSION) {
        snprintf(result->error, sizeof result->error, "version de l'application incompatible avec ce jeu");
        return result->status = RCN_ERR_HOST;
    }
    RcnProcess *proc = calloc(1, sizeof *proc);
    RcnCtx *ctx = calloc(1, sizeof *ctx);
    if (!proc || !ctx) {
        free(proc);
        free(ctx);
        snprintf(result->error, sizeof result->error, "memoire insuffisante");
        return result->status = RCN_ERR_LOAD;
    }
    proc->module = &rcn_module;
    proc->host = host;
    proc->result = result;
    ctx->proc = proc;
    if (load_image(proc, exe_path) != 0) {
        result->status = RCN_ERR_LOAD;
        goto done;
    }
    proc->stack = rcn_page_alloc(STACK_SIZE);
    proc->teb = rcn_page_alloc(TEB_SIZE);
    proc->jmps = calloc(RCN_JMP_SLOTS, sizeof(RcnJmp));
    proc->start_ns = rcn_now_ns();
    if (!proc->stack || !proc->teb || !proc->jmps || setup_paths(proc, exe_path) != 0) {
        snprintf(result->error, sizeof result->error, "memoire insuffisante");
        result->status = RCN_ERR_LOAD;
        goto done;
    }
    ctx->base = (u64)(uintptr_t)proc->image;
    ctx->image_size = proc->module->image_size;
    ctx->mxcsr = 0x1f80;
    // Bloc d'environnement du thread (gs) : limites de la pile, lui-même, et le bloc du processus.
    u64 teb = (u64)(uintptr_t)proc->teb, peb = teb + 0x2000;
    u64 stack_top = (u64)(uintptr_t)proc->stack + STACK_SIZE;
    wr64(teb + 0x08, stack_top);
    wr64(teb + 0x10, (u64)(uintptr_t)proc->stack);
    wr64(teb + 0x30, teb);
    wr64(teb + 0x60, peb);
    wr64(peb + 0x10, ctx->base);
    ctx->gs_base = teb;
    // Convention Windows x64 : pile alignée sur 16 octets avant l'appel, 32 octets réservés à l'appelé.
    ctx->r[4] = stack_top - 0x100;
    u64 tls_callbacks = setup_tls(proc, teb);
    if (setjmp(proc->exit_jmp) == 0) {
        // Fonctions d'initialisation des variables locales au thread : (image, DLL_PROCESS_ATTACH, 0).
        for (u64 p = tls_callbacks; p && rd64(p); p += 8) rcn_call_guest(ctx, rd64(p), ctx->base, 1, 0, 0);
        PUSH64(RCN_RETURN_SENTINEL);
        rcn_call(ctx, ctx->base + proc->module->entry_rva);
        // La fonction d'entrée est revenue : son résultat est le code de sortie.
        result->exit_code = (u32)ctx->r[0];
    }
done:
    rcn_page_free(proc->stack);
    rcn_page_free(proc->teb);
    rcn_page_free(proc->image);
    rcn_page_free(proc->tls_block);
    free(proc->jmps);
    free(proc->game_dir);
    free(proc->exe_name);
    free(proc->atexit_fns);
    free(proc->imports);
    free(proc);
    free(ctx);
    return result->status;
}
