// RomCloud Native : recherche des fonctions Windows fournies par l'hôte au jeu transposé.
// Les fonctions sont rangées par bibliothèque d'origine : rcn_kernel32.c, rcn_k32_file.c, rcn_k32_sync.c, rcn_crt.c. Une fonction
// absente n'empêche pas le chargement : le programme s'arrête en la nommant s'il l'appelle.
#include "rcn_internal.h"
#include "rcn_d3d11.h"
#include "rcn_sdl.h"

// Jeu 32 bits servi par le user32 complet (rcn_user32_x64.c) plutôt que celui des jeux DirectDraw : posé au
// chargement d'après les bibliothèques graphiques qu'il importe.
int rcn_user32_modern;

static int starts_with(const char *text, const char *prefix) {
    for (; *prefix; text++, prefix++) {
        char c = *text;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != *prefix) return 0;
    }
    return 1;
}

// Une fonction qui n'a pas été vérifiée pour les jeux 32 bits leur est présentée comme absente.
#if defined(RCN_HAS_DXVK) || defined(_WIN32)
extern const RcnShim rcn_dxvk_d3d11_exports[], rcn_dxvk_dxgi_exports[];
extern const size_t rcn_dxvk_d3d11_exports_count, rcn_dxvk_dxgi_exports_count;
extern const RcnShim rcn_dxvk_d3d9_exports[];
extern const size_t rcn_dxvk_d3d9_exports_count;
int rcn_dxvk_enabled(void);
#endif

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
        if (!starts_with(dll, crt[i])) continue;
        // Jeu 32 bits : bibliothèque de Visual C++ écrite pour lui (rcn_crt32.c).
        if (bits32) return find(rcn_crt32, rcn_crt32_count, name, bits32, pop);
        RcnFn fn = find(rcn_crt, rcn_crt_count, name, bits32, pop);
        if (!fn) fn = find(rcn_crt_more, rcn_crt_more_count, name, bits32, pop);
        return fn ? fn : find(rcn_crt_eh64, rcn_crt_eh64_count, name, bits32, pop);
    }
    if (starts_with(dll, "kernel32") || starts_with(dll, "kernelbase") || starts_with(dll, "api-ms-win-core-")) {
        // Mise au point sur PC : synchronisation de Windows elle-même (rcn_native_sync.c, RCN_NATIVE_SYNC=1).
        RcnFn native = bits32 ? NULL : rcn_native_sync_lookup(name);
        if (native) return native;
        // Jeux 32 bits : gestionnaires d'exceptions tenus (rcn_fault.c), appelés aux fautes du code dynamique.
        RcnFn fn = bits32 ? find(rcn_fault32, rcn_fault32_count, name, bits32, pop) : NULL;
        if (!fn) fn = find(rcn_kernel32, rcn_kernel32_count, name, bits32, pop);
        if (!fn) fn = find(rcn_k32_file, rcn_k32_file_count, name, bits32, pop);
        if (!fn) fn = find(rcn_k32_sync, rcn_k32_sync_count, name, bits32, pop);
        if (!fn) fn = find(rcn_k32_more, rcn_k32_more_count, name, bits32, pop);
        if (!fn) fn = find(rcn_k32_unwind, rcn_k32_unwind_count, name, bits32, pop);
        if (!fn) fn = find(rcn_k32_x64, rcn_k32_x64_count, name, bits32, pop);
        if (!fn) fn = find(rcn_k32_x64b, rcn_k32_x64b_count, name, bits32, pop);
        // psapi sous ses noms de kernel32 (K32EnumProcessModules…, Windows 7 et plus).
        if (!fn && !strncmp(name, "K32", 3)) fn = find(rcn_psapi, rcn_psapi_count, name + 3, bits32, pop);
        return fn;
    }
    if (starts_with(dll, "psapi")) return find(rcn_psapi, rcn_psapi_count, name, bits32, pop);
    // Compléments des bibliothèques système des jeux 64 bits (rcn_sys_x64.c), cherchés d'abord.
    if (!bits32) {
        RcnFn fn = rcn_sys_x64_lookup(dll, name);
        if (fn) return fn;
    } else {
        RcnFn fn = rcn_sys_x86_lookup(dll, name, pop);
        if (fn) return fn;
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
    if (!bits32 && starts_with(dll, "sdl2")) {
        RcnFn fn = find(rcn_sdl2_data, rcn_sdl2_data_count, name, bits32, pop);
        return fn ? fn : find(rcn_sdl2, rcn_sdl2_count, name, bits32, pop);
    }
    // Réseau des jeux 32 bits : Winsock sans réseau (rcn_net32.c).
    // ws2_32 : complété par les fonctions de rcn_mm_x64.c prêtes pour le 32 bits (getaddrinfo, WSAIoctl…).
    if (bits32 && (starts_with(dll, "wsock32") || starts_with(dll, "ws2_32"))) {
        RcnFn fn = find(rcn_wsock32, rcn_wsock32_count, name, bits32, pop);
        return fn || !starts_with(dll, "ws2_32") ? fn : find(rcn_ws2_x64, rcn_ws2_x64_count, name, bits32, pop);
    }
    // Steam : les interfaces sont fournies par l'hôte (rcn_steam.c), jamais le DLL du jeu — steam_api.dll
    // (jeux 32 bits) comme steam_api64.dll.
    if (starts_with(dll, "steam_api")) return rcn_steam_lookup(name, bits32);
    if (starts_with(dll, "xinput")) return find(rcn_xinput, rcn_xinput_count, name, bits32, pop);
#if defined(RCN_HAS_DXVK) || defined(_WIN32)
    // Direct3D 11 par DXVK (Vulkan) quand l'application l'a choisi pour ce jeu (rcn_dxvk_bridge.c).
    if (!bits32 && rcn_dxvk_enabled()) {
        if (starts_with(dll, "d3d11")) return find(rcn_dxvk_d3d11_exports, rcn_dxvk_d3d11_exports_count, name, bits32, pop);
        if (starts_with(dll, "dxgi")) return find(rcn_dxvk_dxgi_exports, rcn_dxvk_dxgi_exports_count, name, bits32, pop);
    }
    // Direct3D 9 des jeux 32 bits par le Direct3D 9 de l'hôte (rcn_dxvk_d3d9.c).
    if (bits32 && rcn_dxvk_enabled() && starts_with(dll, "d3d9"))
        return find(rcn_dxvk_d3d9_exports, rcn_dxvk_d3d9_exports_count, name, bits32, pop);
#endif
    if (!bits32 && starts_with(dll, "d3d11")) return find(rcn_d3d11_exports, rcn_d3d11_exports_count, name, bits32, pop);
    if (!bits32 && starts_with(dll, "dxgi")) return find(rcn_dxgi_exports, rcn_dxgi_exports_count, name, bits32, pop);
    if (!bits32 && starts_with(dll, "winmm")) return find(rcn_winmm_x64, rcn_winmm_x64_count, name, bits32, pop);
    // Jeux 32 bits : sortie de son waveOut de rcn_mm_x64.c (WAVEHDR des deux tailles), sinon le winmm des
    // jeux DirectDraw (rcn_winmm).
    if (bits32 && starts_with(dll, "winmm")) {
        RcnFn fn = find(rcn_winmm_x64, rcn_winmm_x64_count, name, bits32, pop);
        if (fn) return fn;
    }
    if (!bits32 && starts_with(dll, "ws2_32")) return find(rcn_ws2_x64, rcn_ws2_x64_count, name, bits32, pop);
    if (starts_with(dll, "msacm32")) return find(rcn_msacm_x64, rcn_msacm_x64_count, name, bits32, pop);
    if (!bits32 && starts_with(dll, "user32")) return find(rcn_user32_x64, rcn_user32_x64_count, name, bits32, pop);
    // Jeu 32 bits à Direct3D 9 et plus : le user32 complet (fenêtres, messages), le plus ancien (jeux DirectDraw,
    // rcn_user32.c) pour ce qui lui manque.
    if (bits32 && rcn_user32_modern && starts_with(dll, "user32")) {
        RcnFn fn = find(rcn_user32_x64, rcn_user32_x64_count, name, bits32, pop);
        return fn ? fn : find(rcn_user32, rcn_user32_count, name, bits32, pop);
    }
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