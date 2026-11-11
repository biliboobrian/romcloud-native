// RomCloud Native : recherche des fonctions Windows fournies par l'hôte au jeu transposé.
// Les fonctions sont rangées par bibliothèque d'origine : rcn_kernel32.c, rcn_k32_file.c, rcn_k32_sync.c, rcn_crt.c. Une fonction
// absente n'empêche pas le chargement : le programme s'arrête en la nommant s'il l'appelle.
#include "rcn_internal.h"

static int starts_with(const char *text, const char *prefix) {
    for (; *prefix; text++, prefix++) {
        char c = *text;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != *prefix) return 0;
    }
    return 1;
}

static RcnFn find(const RcnShim *table, size_t count, const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(table[i].name, name) == 0) return table[i].fn;
    }
    return NULL;
}

RcnFn rcn_win32_lookup(const char *dll, const char *name) {
    // Bibliothèque C : UCRT (et ses « api-ms-win-crt-* »), msvcrt, runtime de Visual C++.
    static const char *const crt[] = {"api-ms-win-crt-", "ucrtbase", "msvcrt", "msvcr", "vcruntime"};
    for (size_t i = 0; i < sizeof crt / sizeof crt[0]; i++) {
        if (starts_with(dll, crt[i])) return find(rcn_crt, rcn_crt_count, name);
    }
    if (starts_with(dll, "kernel32") || starts_with(dll, "kernelbase") || starts_with(dll, "api-ms-win-core-")) {
        RcnFn fn = find(rcn_kernel32, rcn_kernel32_count, name);
        if (!fn) fn = find(rcn_k32_file, rcn_k32_file_count, name);
        if (!fn) fn = find(rcn_k32_sync, rcn_k32_sync_count, name);
        return fn;
    }
    return NULL;
}
