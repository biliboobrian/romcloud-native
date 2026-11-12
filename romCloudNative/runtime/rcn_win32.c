// RomCloud Native : recherche des fonctions Windows fournies par l'hôte au jeu transposé.
// Les fonctions sont rangées par bibliothèque d'origine : rcn_kernel32.c, rcn_k32_file.c, rcn_k32_sync.c, rcn_crt.c. Une fonction
// absente n'empêche pas le chargement : le programme s'arrête en la nommant s'il l'appelle.
#include "rcn_internal.h"
#include "rcn_sdl.h"

static int starts_with(const char *text, const char *prefix) {
    for (; *prefix; text++, prefix++) {
        char c = *text;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != *prefix) return 0;
    }
    return 1;
}

// Une fonction qui n'a pas été vérifiée pour les jeux 32 bits leur est présentée comme absente.
static RcnFn find(const RcnShim *table, size_t count, const char *name, int bits32, u32 *pop) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(table[i].name, name) != 0) continue;
        if (!bits32) return table[i].fn;
        if (!table[i].args32) return NULL;
        *pop = table[i].args32 == C32 ? 0 : (u32)(table[i].args32 - 1) * 4;
        return table[i].fn;
    }
    return NULL;
}

RcnFn rcn_win32_lookup(const char *dll, const char *name, int bits32, u32 *pop) {
    *pop = 0;
    // Bibliothèque C : UCRT (et ses « api-ms-win-crt-* »), msvcrt, runtime de Visual C++.
    static const char *const crt[] = {"api-ms-win-crt-", "ucrtbase", "msvcrt", "msvcr", "vcruntime"};
    for (size_t i = 0; i < sizeof crt / sizeof crt[0]; i++) {
        if (starts_with(dll, crt[i])) return find(rcn_crt, rcn_crt_count, name, bits32, pop);
    }
    if (starts_with(dll, "kernel32") || starts_with(dll, "kernelbase") || starts_with(dll, "api-ms-win-core-")) {
        RcnFn fn = find(rcn_kernel32, rcn_kernel32_count, name, bits32, pop);
        if (!fn) fn = find(rcn_k32_file, rcn_k32_file_count, name, bits32, pop);
        if (!fn) fn = find(rcn_k32_sync, rcn_k32_sync_count, name, bits32, pop);
        if (!fn) fn = find(rcn_k32_more, rcn_k32_more_count, name, bits32, pop);
        if (!fn) fn = find(rcn_k32_unwind, rcn_k32_unwind_count, name, bits32, pop);
        if (!fn) fn = find(rcn_k32_x64, rcn_k32_x64_count, name, bits32, pop);
        return fn;
    }
    // Bibliothèques système des jeux 64 bits (deux tables chacune au plus).
    static const struct {
        const char *dll;
        const RcnShim *table, *more;
        const size_t *count, *more_count;
    } system[] = {
        {"advapi32", rcn_advapi32, rcn_advapi32_x64, &rcn_advapi32_count, &rcn_advapi32_x64_count},
        {"ole32", rcn_ole32, rcn_ole32_x64, &rcn_ole32_count, &rcn_ole32_x64_count},
        {"oleaut32", rcn_ole32_x64, NULL, &rcn_ole32_x64_count, NULL},
        {"bcrypt", rcn_bcrypt, NULL, &rcn_bcrypt_count, NULL},
        {"shell32", rcn_shell32_x64, NULL, &rcn_shell32_x64_count, NULL},
        {"ntdll", rcn_ntdll_x64, NULL, &rcn_ntdll_x64_count, NULL},
    };
    for (size_t i = 0; i < sizeof system / sizeof system[0]; i++) {
        if (!starts_with(dll, system[i].dll)) continue;
        RcnFn fn = find(system[i].table, *system[i].count, name, bits32, pop);
        if (!fn && system[i].more) fn = find(system[i].more, *system[i].more_count, name, bits32, pop);
        return fn;
    }
    if (!bits32 && starts_with(dll, "sdl2")) return find(rcn_sdl2, rcn_sdl2_count, name, bits32, pop);
    if (!bits32 && starts_with(dll, "winmm")) return find(rcn_winmm_x64, rcn_winmm_x64_count, name, bits32, pop);
    if (!bits32 && starts_with(dll, "ws2_32")) return find(rcn_ws2_x64, rcn_ws2_x64_count, name, bits32, pop);
    if (!bits32 && starts_with(dll, "msacm32")) return find(rcn_msacm_x64, rcn_msacm_x64_count, name, bits32, pop);
    if (!bits32 && starts_with(dll, "user32")) return find(rcn_user32_x64, rcn_user32_x64_count, name, bits32, pop);
    static const struct {
        const char *dll;
        const RcnShim *table;
        const size_t *count;
    } others[] = {
        {"user32", rcn_user32, &rcn_user32_count}, {"gdi32", rcn_gdi32, &rcn_gdi32_count},
        {"winmm", rcn_winmm, &rcn_winmm_count},    {"ddraw", rcn_ddraw, &rcn_ddraw_count},
        {"dinput", rcn_dinput, &rcn_dinput_count}, {"dsound", rcn_dsound, &rcn_dsound_count},
    };
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) {
        if (starts_with(dll, others[i].dll)) return bits32 ? find(others[i].table, *others[i].count, name, bits32, pop) : NULL;
    }
    return NULL;
}