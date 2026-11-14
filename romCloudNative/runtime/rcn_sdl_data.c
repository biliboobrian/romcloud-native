// RomCloud Native : SDL2, partie « données » — flux SDL_RWops, surfaces de pixels et petite bibliothèque C
// de SDL. Ce sont les fonctions dont se sert SDL2_image (traduit avec le jeu) pour décoder les images :
// FNA lui confie ses textures (Texture2D.FromStream) et l'icône de la fenêtre.
//
// Les flux sont en mémoire (un fichier est lu en entier à l'ouverture) ; leurs fonctions, appelées par le
// jeu au travers du SDL_RWops, sont des fonctions de l'hôte rendues appelables (rcn_thunk).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "rcn_internal.h"
#include "rcn_sdl.h"

// --- Petite bibliothèque C ---

static u64 gmalloc(RcnCtx *ctx, size_t size, int zero) { return (u64)(uintptr_t)rcn_galloc(ctx, size ? size : 1, zero); }
static void d_malloc(RcnCtx *ctx) { RET(gmalloc(ctx, (size_t)A0, 0)); }
static void d_calloc(RcnCtx *ctx) { RET(gmalloc(ctx, (size_t)(A0 * A1), 1)); }
static void d_realloc(RcnCtx *ctx) { RET((u64)(uintptr_t)rcn_grealloc(ctx, PTR(A0), (size_t)A1)); }
static void d_free(RcnCtx *ctx) {
    if (A0) rcn_gfree(ctx, PTR(A0));
}
static void d_memcpy(RcnCtx *ctx) {
    memmove(PTR(A0), PTR(A1), (size_t)A2);
    RET(A0);
}
static void d_memset(RcnCtx *ctx) {
    memset(PTR(A0), (int)A1, (size_t)A2);
    RET(A0);
}
static void d_memcmp(RcnCtx *ctx) { RET_I32(memcmp(PTR(A0), PTR(A1), (size_t)A2)); }
static void d_strlen(RcnCtx *ctx) { RET(strlen(STR(A0))); }
static void d_strcmp(RcnCtx *ctx) { RET_I32(strcmp(STR(A0), STR(A1))); }
static void d_strncmp(RcnCtx *ctx) { RET_I32(strncmp(STR(A0), STR(A1), (size_t)A2)); }
static void d_strrchr(RcnCtx *ctx) {
    const char *p = strrchr(STR(A0), (int)(u8)A1);
    RET((u64)(uintptr_t)p);
}
static void d_strchr(RcnCtx *ctx) {
    const char *p = strchr(STR(A0), (int)(u8)A1);
    RET((u64)(uintptr_t)p);
}
static void d_toupper(RcnCtx *ctx) {
    int c = (int)A0;
    RET_I32(c >= 'a' && c <= 'z' ? c - 32 : c);
}
static void d_tolower(RcnCtx *ctx) {
    int c = (int)A0;
    RET_I32(c >= 'A' && c <= 'Z' ? c + 32 : c);
}
static void d_pow(RcnCtx *ctx) { F64(0) = pow(F64(0), F64(1)); }
static void d_floor(RcnCtx *ctx) { F64(0) = floor(F64(0)); }
static void d_floorf(RcnCtx *ctx) { F32(0) = floorf(F32(0)); }
static void d_sqrt(RcnCtx *ctx) { F64(0) = sqrt(F64(0)); }
// Arguments variables (convention Windows x64) : ceux des registres recopiés dans leurs cases réservées.
static u64 variadic(RcnCtx *ctx, int first) {
    u64 home = ctx->r[4] + 8;
    wr64(home, ctx->r[1]), wr64(home + 8, ctx->r[2]), wr64(home + 16, ctx->r[8]), wr64(home + 24, ctx->r[9]);
    return home + 8ull * (u64)first;
}
// SDL_snprintf(tampon, taille, format, …) : longueur du texte complet.
static void d_snprintf(RcnCtx *ctx) {
    RcnBuf buf = {0};
    rcn_format(&buf, STR(A2), variadic(ctx, 3));
    size_t max = (size_t)A1;
    if (A0 && max) {
        size_t n = buf.len < max ? buf.len : max - 1;
        if (n) memcpy(PTR(A0), buf.data, n);
        wr8(A0 + n, 0);
    }
    RET_I32((i64)buf.len);
    free(buf.data);
}
// SDL_SetError(format, …) / SDL_Error(code) : message gardé pour SDL_GetError ; -1.
static void d_SetError(RcnCtx *ctx) {
    RcnBuf buf = {0};
    rcn_format(&buf, STR(A0), variadic(ctx, 1));
    rcn_sdl_set_error(buf.data ? buf.data : "");
    free(buf.data);
    RET_I32(-1);
}
static void d_Error(RcnCtx *ctx) {
    rcn_sdl_set_error((u32)A0 == 0 ? "Out of memory" : "SDL error");
    RET_I32(-1);
}

// --- Flux SDL_RWops ---
// Disposition de SDL 2 : size, seek, read, write, close (pointeurs de fonctions), type (u32 à +40), puis
// la partie « mem » : début (+48), position (+56), fin (+64). +72 : 1 si le tampon appartient au flux.
enum { RW_SIZE = 80, RW_TYPE = 40, RW_BASE = 48, RW_HERE = 56, RW_STOP = 64, RW_OWNED = 72 };
enum { SDL_RWOPS_UNKNOWN = 0, SDL_RWOPS_MEMORY = 4, SDL_RWOPS_MEMORY_RO = 5 };

static void rw_size(RcnCtx *ctx) { RET(rd64(A0 + RW_STOP) - rd64(A0 + RW_BASE)); }
static void rw_seek(RcnCtx *ctx) {
    u64 rw = A0, base = rd64(rw + RW_BASE), stop = rd64(rw + RW_STOP);
    i64 offset = (i64)A1, origin = (u32)A2 == 0 ? (i64)base : (u32)A2 == 1 ? (i64)rd64(rw + RW_HERE) : (i64)stop;
    i64 at = origin + offset;
    if (at < (i64)base) at = (i64)base;
    if (at > (i64)stop) at = (i64)stop;
    wr64(rw + RW_HERE, (u64)at);
    RET((u64)(at - (i64)base));
}
static void rw_read(RcnCtx *ctx) {
    u64 rw = A0, here = rd64(rw + RW_HERE), stop = rd64(rw + RW_STOP), size = A2, count = A3;
    if (!size || !count) {
        RET(0);
        return;
    }
    u64 n = (stop - here) / size;
    if (n > count) n = count;
    memcpy(PTR(A1), PTR(here), (size_t)(n * size));
    wr64(rw + RW_HERE, here + n * size);
    RET(n);
}
static void rw_write(RcnCtx *ctx) {
    u64 rw = A0, here = rd64(rw + RW_HERE), stop = rd64(rw + RW_STOP), size = A2, count = A3;
    if (rd32(rw + RW_TYPE) != SDL_RWOPS_MEMORY || !size || !count) {
        RET(0);
        return;
    }
    u64 n = (stop - here) / size;
    if (n > count) n = count;
    memcpy(PTR(here), PTR(A1), (size_t)(n * size));
    wr64(rw + RW_HERE, here + n * size);
    RET(n);
}
static void rw_close(RcnCtx *ctx) {
    u64 rw = A0;
    if (rw) {
        if (rd64(rw + RW_OWNED)) rcn_gfree(ctx, PTR(rd64(rw + RW_BASE)));
        rcn_gfree(ctx, PTR(rw));
    }
    RET(0);
}

static u64 rw_new(RcnCtx *ctx, u64 base, u64 size, u32 type, int owned) {
    static u64 fns[5];
    if (!fns[0]) {
        fns[0] = rcn_thunk(ctx, "SDL2.dll", "rw_size", rw_size, 0);
        fns[1] = rcn_thunk(ctx, "SDL2.dll", "rw_seek", rw_seek, 0);
        fns[2] = rcn_thunk(ctx, "SDL2.dll", "rw_read", rw_read, 0);
        fns[3] = rcn_thunk(ctx, "SDL2.dll", "rw_write", rw_write, 0);
        fns[4] = rcn_thunk(ctx, "SDL2.dll", "rw_close", rw_close, 0);
    }
    u64 rw = gmalloc(ctx, RW_SIZE, 1);
    if (!rw) return 0;
    for (int i = 0; i < 5; i++) wr64(rw + 8ull * (u64)i, fns[i]);
    wr32(rw + RW_TYPE, type);
    wr64(rw + RW_BASE, base), wr64(rw + RW_HERE, base), wr64(rw + RW_STOP, base + size), wr64(rw + RW_OWNED, owned);
    return rw;
}
static void d_RWFromMem(RcnCtx *ctx) { RET(rw_new(ctx, A0, A1, SDL_RWOPS_MEMORY, 0)); }
static void d_RWFromConstMem(RcnCtx *ctx) { RET(rw_new(ctx, A0, A1, SDL_RWOPS_MEMORY_RO, 0)); }
// SDL_RWFromFile(chemin, mode) : lecture seule, fichier lu en entier.
static void d_RWFromFile(RcnCtx *ctx) {
    const char *mode = A1 ? STR(A1) : "rb";
    if (!A0 || strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+')) {
        rcn_sdl_set_error("SDL_RWFromFile : ecriture non prise en charge");
        RET(0);
        return;
    }
    char *path = rcn_host_path(ctx, STR(A0));
    FILE *f = path ? fopen(path, "rb") : NULL;
    free(path);
    if (!f) {
        char message[300];
        snprintf(message, sizeof message, "Couldn't open %s", STR(A0));
        rcn_sdl_set_error(message);
        RET(0);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    u64 data = size > 0 ? gmalloc(ctx, (size_t)size, 0) : 0;
    if (size > 0 && (!data || fread(PTR(data), 1, (size_t)size, f) != (size_t)size)) {
        if (data) rcn_gfree(ctx, PTR(data));
        fclose(f);
        rcn_sdl_set_error("SDL_RWFromFile : lecture impossible");
        RET(0);
        return;
    }
    fclose(f);
    u64 rw = rw_new(ctx, data, size > 0 ? (u64)size : 0, SDL_RWOPS_MEMORY_RO, 1);
    if (!rw && data) rcn_gfree(ctx, PTR(data));
    RET(rw);
}
// SDL_AllocRW / SDL_FreeRW : flux dont le jeu remplit lui-même les fonctions.
static void d_AllocRW(RcnCtx *ctx) {
    u64 rw = gmalloc(ctx, RW_SIZE, 1);
    if (rw) wr32(rw + RW_TYPE, SDL_RWOPS_UNKNOWN);
    RET(rw);
}
static void d_FreeRW(RcnCtx *ctx) { d_free(ctx); }
// Fonctions d'accès : celles du flux (celles de l'hôte ou du jeu), appelées par leurs pointeurs.
static u64 rw_call(RcnCtx *ctx, u64 rw, int slot, u64 a1, u64 a2, u64 a3) { return rcn_call_guest(ctx, rd64(rw + 8ull * (u64)slot), rw, a1, a2, a3); }
static void d_RWsize(RcnCtx *ctx) { RET(rw_call(ctx, A0, 0, 0, 0, 0)); }
static void d_RWseek(RcnCtx *ctx) {
    u64 rw = A0, a1 = A1, a2 = A2;
    RET(rw_call(ctx, rw, 1, a1, a2, 0));
}
static void d_RWtell(RcnCtx *ctx) { RET(rw_call(ctx, A0, 1, 0, 1, 0)); }
static void d_RWread(RcnCtx *ctx) {
    u64 rw = A0, a1 = A1, a2 = A2, a3 = A3;
    RET(rw_call(ctx, rw, 2, a1, a2, a3));
}
static void d_RWwrite(RcnCtx *ctx) {
    u64 rw = A0, a1 = A1, a2 = A2, a3 = A3;
    RET(rw_call(ctx, rw, 3, a1, a2, a3));
}
static void d_RWclose(RcnCtx *ctx) { RET(rw_call(ctx, A0, 4, 0, 0, 0)); }
static u64 read_le(RcnCtx *ctx, u64 rw, int bytes) {
    u64 tmp = gmalloc(ctx, 8, 1), value = 0;
    if (tmp && rw_call(ctx, rw, 2, tmp, (u64)bytes, 1) == 1) value = rd64(tmp);
    if (tmp) rcn_gfree(ctx, PTR(tmp));
    return bytes == 2 ? (u16)value : bytes == 4 ? (u32)value : value;
}
static void d_ReadU8(RcnCtx *ctx) { RET((u8)read_le(ctx, A0, 1)); }
static void d_ReadLE16(RcnCtx *ctx) { RET(read_le(ctx, A0, 2)); }
static void d_ReadLE32(RcnCtx *ctx) { RET(read_le(ctx, A0, 4)); }
static void d_ReadLE64(RcnCtx *ctx) { RET(read_le(ctx, A0, 8)); }

// --- Formats de pixels et surfaces ---
// SDL_PixelFormat : format, palette (+8), bits (+16), octets (+17), masques R G B A (+20…+32), pertes
// (+36…+39), décalages (+40…+43), compteur (+44), suivant (+48) ; 56 octets.
// SDL_Surface : flags, format (+8), w (+16), h (+20), pitch (+24), pixels (+32), userdata (+40),
// locked (+48), list_blitmap (+56), clip_rect (+64), map (+80), refcount (+88) ; 96 octets.
// La couleur transparente (SDL_SetColorKey) est gardée dans `map`, que seul SDL lit : bit 32 + clé.
enum { SDL_PREALLOC = 1, SF_FORMAT = 8, SF_W = 16, SF_H = 20, SF_PITCH = 24, SF_PIXELS = 32, SF_CLIP = 64, SF_KEY = 80, SF_REF = 88 };
enum { PIXELFORMAT_INDEX8 = 0x13000801 };

static const struct {
    u32 format, bpp, r, g, b, a;
} formats[] = {
    {0x16762004, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000}, // ABGR8888 (octets R G B A)
    {0x16362004, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000}, // ARGB8888 (octets B G R A)
    {0x16462004, 32, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF}, // RGBA8888
    {0x16862004, 32, 0x0000FF00, 0x00FF0000, 0xFF000000, 0x000000FF}, // BGRA8888
    {0x16161804, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0},          // RGB888 (XRGB)
    {0x16561804, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0},          // BGR888 (XBGR)
    {0x17101803, 24, 0x000000FF, 0x0000FF00, 0x00FF0000, 0},          // RGB24 (octets R G B)
    {0x17401803, 24, 0x00FF0000, 0x0000FF00, 0x000000FF, 0},          // BGR24
    {0x15151002, 16, 0x0000F800, 0x000007E0, 0x0000001F, 0},          // RGB565
};

static u32 masks_to_format(u32 bpp, u32 r, u32 g, u32 b, u32 a) {
    if (bpp == 8 && !r && !g && !b && !a) return PIXELFORMAT_INDEX8;
    for (size_t i = 0; i < sizeof formats / sizeof formats[0]; i++) {
        if (formats[i].bpp == bpp && formats[i].r == r && formats[i].g == g && formats[i].b == b && formats[i].a == a)
            return formats[i].format;
    }
    // Profondeur 32 sans masques : ARGB8888 de SDL.
    if (bpp == 32 && !r && !g && !b && !a) return 0x16362004;
    return 0;
}
static int format_masks(u32 format, u32 *bpp, u32 m[4]) {
    if (format == PIXELFORMAT_INDEX8) {
        *bpp = 8, m[0] = m[1] = m[2] = m[3] = 0;
        return 1;
    }
    for (size_t i = 0; i < sizeof formats / sizeof formats[0]; i++) {
        if (formats[i].format == format) {
            *bpp = formats[i].bpp, m[0] = formats[i].r, m[1] = formats[i].g, m[2] = formats[i].b, m[3] = formats[i].a;
            return 1;
        }
    }
    return 0;
}
static u32 mask_shift(u32 mask) {
    u32 s = 0;
    while (mask && !(mask & 1)) mask >>= 1, s++;
    return s;
}
static u32 mask_bits(u32 mask) {
    u32 n = 0;
    for (; mask; mask &= mask - 1) n++;
    return n;
}

static u64 new_format(RcnCtx *ctx, u32 format) {
    u32 bpp, m[4];
    if (!format_masks(format, &bpp, m)) return 0;
    u64 f = gmalloc(ctx, 56, 1);
    if (!f) return 0;
    wr32(f, format);
    wr8(f + 16, (u8)bpp), wr8(f + 17, (u8)((bpp + 7) / 8));
    for (int i = 0; i < 4; i++) {
        wr32(f + 20 + 4ull * (u64)i, m[i]);
        u32 bits = mask_bits(m[i]);
        wr8(f + 36 + (u64)i, (u8)(bits >= 8 ? 0 : 8 - bits));
        wr8(f + 40 + (u64)i, (u8)mask_shift(m[i]));
    }
    wr32(f + 44, 1);
    if (format == PIXELFORMAT_INDEX8) {
        // SDL_Palette : ncolors, colors (+8), version (+16), refcount (+20) ; couleurs RGBA, blanc opaque.
        u64 palette = gmalloc(ctx, 24, 1), colors = gmalloc(ctx, 256 * 4, 0);
        if (!palette || !colors) return f;
        for (u32 i = 0; i < 256; i++) wr32(colors + 4ull * i, 0xFFFFFFFFu);
        wr32(palette, 256), wr64(palette + 8, colors), wr32(palette + 16, 1), wr32(palette + 20, 1);
        wr64(f + 8, palette);
    }
    return f;
}
static void free_format(RcnCtx *ctx, u64 f) {
    if (!f) return;
    u64 palette = rd64(f + 8);
    if (palette) {
        rcn_gfree(ctx, PTR(rd64(palette + 8)));
        rcn_gfree(ctx, PTR(palette));
    }
    rcn_gfree(ctx, PTR(f));
}

static u64 new_surface(RcnCtx *ctx, i32 w, i32 h, u32 format, u64 pixels, i32 pitch) {
    if (w < 0 || h < 0) {
        rcn_sdl_set_error("taille de surface invalide");
        return 0;
    }
    u64 f = new_format(ctx, format);
    if (!f) {
        rcn_sdl_set_error("format de pixels non pris en charge");
        return 0;
    }
    u64 s = gmalloc(ctx, 96, 1);
    if (!s) {
        free_format(ctx, f);
        return 0;
    }
    u32 bytes = rd8(f + 17);
    if (!pixels) {
        pitch = (i32)(((u32)w * bytes + 3) & ~3u);
        pixels = gmalloc(ctx, (size_t)pitch * (size_t)(h ? h : 1), 1);
        if (!pixels) {
            free_format(ctx, f);
            rcn_gfree(ctx, PTR(s));
            return 0;
        }
    } else {
        wr32(s, SDL_PREALLOC);
    }
    wr64(s + SF_FORMAT, f);
    wr32(s + SF_W, (u32)w), wr32(s + SF_H, (u32)h), wr32(s + SF_PITCH, (u32)pitch);
    wr64(s + SF_PIXELS, pixels);
    wr32(s + SF_CLIP + 8, (u32)w), wr32(s + SF_CLIP + 12, (u32)h);
    wr32(s + SF_REF, 1);
    return s;
}
// SDL_CreateRGBSurface(flags, w, h, profondeur, R, G, B, A)
static void d_CreateRGBSurface(RcnCtx *ctx) {
    u32 format = masks_to_format((u32)A3, (u32)STK(4), (u32)STK(5), (u32)STK(6), (u32)STK(7));
    RET(new_surface(ctx, (i32)A1, (i32)A2, format, 0, 0));
}
// SDL_CreateRGBSurfaceFrom(pixels, w, h, profondeur, pitch, R, G, B, A)
static void d_CreateRGBSurfaceFrom(RcnCtx *ctx) {
    u32 format = masks_to_format((u32)A3, (u32)STK(5), (u32)STK(6), (u32)STK(7), (u32)STK(8));
    RET(new_surface(ctx, (i32)A1, (i32)A2, format, A0, (i32)STK(4)));
}
// SDL_CreateRGBSurfaceWithFormat(flags, w, h, profondeur, format)
static void d_CreateRGBSurfaceWithFormat(RcnCtx *ctx) { RET(new_surface(ctx, (i32)A1, (i32)A2, (u32)STK(4), 0, 0)); }
// SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, profondeur, pitch, format)
static void d_CreateRGBSurfaceWithFormatFrom(RcnCtx *ctx) {
    RET(new_surface(ctx, (i32)A1, (i32)A2, (u32)STK(5), A0, (i32)STK(4)));
}
static void d_FreeSurface(RcnCtx *ctx) {
    u64 s = A0;
    if (!s) return;
    u32 ref = rd32(s + SF_REF);
    if (ref > 1) {
        wr32(s + SF_REF, ref - 1);
        return;
    }
    if (!(rd32(s) & SDL_PREALLOC)) rcn_gfree(ctx, PTR(rd64(s + SF_PIXELS)));
    free_format(ctx, rd64(s + SF_FORMAT));
    rcn_gfree(ctx, PTR(s));
}
// SDL_SetColorKey(surface, flag, clé) / SDL_GetColorKey(surface, &clé)
static void d_SetColorKey(RcnCtx *ctx) {
    if (!A0) {
        RET_I32(-1);
        return;
    }
    wr64(A0 + SF_KEY, A1 ? (1ull << 32) | (u32)A2 : 0);
    RET(0);
}
static void d_GetColorKey(RcnCtx *ctx) {
    u64 key = A0 ? rd64(A0 + SF_KEY) : 0;
    if (!(key >> 32)) {
        RET_I32(-1);
        return;
    }
    if (A1) wr32(A1, (u32)key);
    RET(0);
}

// Pixel → RGBA (octets R G B A dans un u32 petit-boutiste) selon le format de la surface.
static u32 load_pixel(u64 f, const u8 *p, u32 bytes, u64 key) {
    u32 raw = 0;
    memcpy(&raw, p, bytes);
    if (key >> 32 && raw == (u32)key) return 0;
    if (rd32(f) == PIXELFORMAT_INDEX8) {
        u64 palette = rd64(f + 8);
        return palette ? rd32(rd64(palette + 8) + 4ull * (raw & 0xFF)) : 0xFF000000u | raw * 0x010101u;
    }
    u32 rgba = 0;
    for (int c = 0; c < 4; c++) {
        u32 mask = rd32(f + 20 + 4ull * (u64)c);
        u32 v;
        if (!mask) {
            v = c == 3 ? 255 : 0;
        } else {
            u32 bits = mask_bits(mask), x = (raw & mask) >> mask_shift(mask);
            v = bits >= 8 ? x >> (bits - 8) : (x * 255 + ((1u << bits) - 1) / 2) / ((1u << bits) - 1);
        }
        rgba |= (v & 0xFF) << (8 * c);
    }
    return rgba;
}
static void store_pixel(u64 f, u8 *p, u32 bytes, u32 rgba) {
    u32 raw = 0;
    for (int c = 0; c < 4; c++) {
        u32 mask = rd32(f + 20 + 4ull * (u64)c);
        if (!mask) continue;
        u32 bits = mask_bits(mask), v = (rgba >> (8 * c)) & 0xFF;
        raw |= ((bits >= 8 ? v << (bits - 8) : v >> (8 - bits)) << mask_shift(mask)) & mask;
    }
    memcpy(p, &raw, bytes);
}
static u64 convert(RcnCtx *ctx, u64 src, u32 format) {
    if (!src) return 0;
    i32 w = (i32)rd32(src + SF_W), h = (i32)rd32(src + SF_H);
    u64 out = new_surface(ctx, w, h, format, 0, 0);
    if (!out) return 0;
    u64 sf = rd64(src + SF_FORMAT), df = rd64(out + SF_FORMAT), key = rd64(src + SF_KEY);
    u32 sb = rd8(sf + 17), db = rd8(df + 17), sp = rd32(src + SF_PITCH), dp = rd32(out + SF_PITCH);
    const u8 *s = PTR(rd64(src + SF_PIXELS));
    u8 *d = PTR(rd64(out + SF_PIXELS));
    for (i32 y = 0; y < h; y++) {
        for (i32 x = 0; x < w; x++) store_pixel(df, d + (size_t)y * dp + (size_t)x * db, db, load_pixel(sf, s + (size_t)y * sp + (size_t)x * sb, sb, key));
    }
    return out;
}
// SDL_ConvertSurfaceFormat(surface, format, flags) / SDL_ConvertSurface(surface, &format, flags)
static void d_ConvertSurfaceFormat(RcnCtx *ctx) { RET(convert(ctx, A0, (u32)A1)); }
static void d_ConvertSurface(RcnCtx *ctx) { RET(A1 ? convert(ctx, A0, rd32(A1)) : 0); }
static void d_LockSurface(RcnCtx *ctx) { RET(0); }
static void d_UnlockSurface(RcnCtx *ctx) { (void)ctx; }
static void d_SetSurfaceBlendMode(RcnCtx *ctx) { RET(0); }
static void d_SetPaletteColors(RcnCtx *ctx) {
    u64 palette = A0, colors = A1;
    i32 first = (i32)A2, count = (i32)A3;
    if (!palette) {
        RET_I32(-1);
        return;
    }
    i32 n = (i32)rd32(palette);
    for (i32 i = 0; i < count && first + i < n; i++) wr32(rd64(palette + 8) + 4ull * (u64)(first + i), rd32(colors + 4ull * (u64)i));
    RET(0);
}
static void d_MasksToPixelFormatEnum(RcnCtx *ctx) { RET(masks_to_format((u32)A0, (u32)A1, (u32)A2, (u32)A3, (u32)STK(4))); }
static void d_PixelFormatEnumToMasks(RcnCtx *ctx) {
    u32 bpp, m[4];
    if (!format_masks((u32)A0, &bpp, m)) {
        RET(0);
        return;
    }
    if (A1) wr32(A1, bpp);
    for (int i = 0; i < 4; i++) {
        u64 at = i == 0 ? A2 : i == 1 ? A3 : STK(4 + i - 2);
        if (at) wr32(at, m[i]);
    }
    RET(1);
}
static void d_AllocFormat(RcnCtx *ctx) { RET(new_format(ctx, (u32)A0)); }
static void d_FreeFormat(RcnCtx *ctx) { free_format(ctx, A0); }
// Rendu SDL (SDL_Renderer) absent : SDL_CreateTextureFromSurface échoue.
static void d_CreateTextureFromSurface(RcnCtx *ctx) {
    rcn_sdl_set_error("SDL_Renderer non pris en charge");
    RET(0);
}

const RcnShim rcn_sdl2_data[] = {
    {"SDL_malloc", d_malloc, 0},
    {"SDL_calloc", d_calloc, 0},
    {"SDL_realloc", d_realloc, 0},
    {"SDL_free", d_free, 0},
    {"SDL_memcpy", d_memcpy, 0},
    {"SDL_memmove", d_memcpy, 0},
    {"SDL_memset", d_memset, 0},
    {"SDL_memcmp", d_memcmp, 0},
    {"SDL_strlen", d_strlen, 0},
    {"SDL_strcmp", d_strcmp, 0},
    {"SDL_strncmp", d_strncmp, 0},
    {"SDL_strrchr", d_strrchr, 0},
    {"SDL_strchr", d_strchr, 0},
    {"SDL_toupper", d_toupper, 0},
    {"SDL_tolower", d_tolower, 0},
    {"SDL_pow", d_pow, 0},
    {"SDL_floor", d_floor, 0},
    {"SDL_floorf", d_floorf, 0},
    {"SDL_sqrt", d_sqrt, 0},
    {"SDL_snprintf", d_snprintf, 0},
    {"SDL_SetError", d_SetError, 0},
    {"SDL_Error", d_Error, 0},
    {"SDL_RWFromMem", d_RWFromMem, 0},
    {"SDL_RWFromConstMem", d_RWFromConstMem, 0},
    {"SDL_RWFromFile", d_RWFromFile, 0},
    {"SDL_AllocRW", d_AllocRW, 0},
    {"SDL_FreeRW", d_FreeRW, 0},
    {"SDL_RWsize", d_RWsize, 0},
    {"SDL_RWseek", d_RWseek, 0},
    {"SDL_RWtell", d_RWtell, 0},
    {"SDL_RWread", d_RWread, 0},
    {"SDL_RWwrite", d_RWwrite, 0},
    {"SDL_RWclose", d_RWclose, 0},
    {"SDL_ReadU8", d_ReadU8, 0},
    {"SDL_ReadLE16", d_ReadLE16, 0},
    {"SDL_ReadLE32", d_ReadLE32, 0},
    {"SDL_ReadLE64", d_ReadLE64, 0},
    {"SDL_CreateRGBSurface", d_CreateRGBSurface, 0},
    {"SDL_CreateRGBSurfaceFrom", d_CreateRGBSurfaceFrom, 0},
    {"SDL_CreateRGBSurfaceWithFormat", d_CreateRGBSurfaceWithFormat, 0},
    {"SDL_CreateRGBSurfaceWithFormatFrom", d_CreateRGBSurfaceWithFormatFrom, 0},
    {"SDL_FreeSurface", d_FreeSurface, 0},
    {"SDL_SetColorKey", d_SetColorKey, 0},
    {"SDL_GetColorKey", d_GetColorKey, 0},
    {"SDL_ConvertSurfaceFormat", d_ConvertSurfaceFormat, 0},
    {"SDL_ConvertSurface", d_ConvertSurface, 0},
    {"SDL_LockSurface", d_LockSurface, 0},
    {"SDL_UnlockSurface", d_UnlockSurface, 0},
    {"SDL_SetSurfaceBlendMode", d_SetSurfaceBlendMode, 0},
    {"SDL_SetPaletteColors", d_SetPaletteColors, 0},
    {"SDL_MasksToPixelFormatEnum", d_MasksToPixelFormatEnum, 0},
    {"SDL_PixelFormatEnumToMasks", d_PixelFormatEnumToMasks, 0},
    {"SDL_AllocFormat", d_AllocFormat, 0},
    {"SDL_FreeFormat", d_FreeFormat, 0},
    {"SDL_CreateTextureFromSurface", d_CreateTextureFromSurface, 0},
};
const size_t rcn_sdl2_data_count = sizeof rcn_sdl2_data / sizeof rcn_sdl2_data[0];
