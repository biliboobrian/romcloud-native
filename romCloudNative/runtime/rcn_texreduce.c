// RomCloud Native : réduction des grandes textures d'un jeu à leur création (RCN_TEX_REDUCE), pour les
// téléphones où le processeur graphique partage une mémoire vive trop petite pour le jeu.
//
// Une texture sans mips (jamais diffusée par le moteur : interface, cartes, tables) d'au moins `min` pixels
// de côté est créée 2^shift fois plus petite dans chaque dimension, dans le même format : les coordonnées de
// texture étant relatives, le jeu la lit sans rien voir d'autre qu'une image moins fine. Formats 8 bits par
// composante (RGBA, BGRA, RG, R) filtrés directement ; formats compressés BC1 à BC5 et BC7 décodés (bcdec.h),
// filtrés, puis réencodés (BC7 en mode 6, le plus simple : un seul sous-ensemble RGBA).
//
// Le travail se fait par bandes (une rangée de blocs de sortie à la fois) : jamais l'image entière décodée.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rcn_internal.h"

#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

enum { TR_NONE, TR_RAW, TR_BC1, TR_BC2, TR_BC3, TR_BC4, TR_BC5, TR_BC7 };

// Genre du format DXGI : copie brute de `*channels` octets par pixel, ou compressé (blocs de `*block` octets,
// décodés en `*channels` octets par pixel).
static int tr_kind(u32 format, u32 *channels, u32 *block) {
    *block = 0;
    switch (format) {
    case 27: case 28: case 29: case 30:           // R8G8B8A8 (sans type, UNORM, SRGB, UINT)
    case 87: case 88: case 90: case 91: case 92: case 93: // B8G8R8A8 / B8G8R8X8
        *channels = 4;
        return TR_RAW;
    case 48: case 49: case 50: // R8G8
        *channels = 2;
        return TR_RAW;
    case 60: case 61: case 62: case 65: // R8, A8
        *channels = 1;
        return TR_RAW;
    case 70: case 71: case 72: *channels = 4, *block = 8; return TR_BC1;
    case 73: case 74: case 75: *channels = 4, *block = 16; return TR_BC2;
    case 76: case 77: case 78: *channels = 4, *block = 16; return TR_BC3;
    case 79: case 80: *channels = 1, *block = 8; return TR_BC4;  // BC4 non signé
    case 82: case 83: *channels = 2, *block = 16; return TR_BC5; // BC5 non signé
    case 97: case 98: case 99: *channels = 4, *block = 16; return TR_BC7;
    default: return TR_NONE;
    }
}

int rcn_tex_reducible(u32 format) {
    u32 c, b;
    return tr_kind(format, &c, &b) != TR_NONE;
}

// --- Encodeurs (ajustement sur l'axe principal des couleurs du bloc) ---

static void tr_put_bits(u8 *out, u32 *pos, u64 value, u32 count) {
    for (u32 i = 0; i < count; i++, (*pos)++)
        if ((value >> i) & 1) out[*pos >> 3] |= (u8)(1u << (*pos & 7));
}

// Axe principal (itérations de puissance sur la covariance) et moyenne de n points à `dim` composantes.
static void tr_axis(const float *px, int n, int dim, float *mean, float *axis) {
    float cov[4][4] = {{0}};
    for (int d = 0; d < dim; d++) mean[d] = 0;
    for (int i = 0; i < n; i++)
        for (int d = 0; d < dim; d++) mean[d] += px[i * dim + d];
    for (int d = 0; d < dim; d++) mean[d] /= (float)n;
    for (int i = 0; i < n; i++)
        for (int a = 0; a < dim; a++)
            for (int b = 0; b < dim; b++) cov[a][b] += (px[i * dim + a] - mean[a]) * (px[i * dim + b] - mean[b]);
    for (int d = 0; d < dim; d++) axis[d] = 1.0f;
    for (int it = 0; it < 6; it++) {
        float next[4] = {0}, len = 0;
        for (int a = 0; a < dim; a++)
            for (int b = 0; b < dim; b++) next[a] += cov[a][b] * axis[b];
        for (int d = 0; d < dim; d++) len += next[d] * next[d];
        if (len < 1e-12f) break;
        len = 1.0f / sqrtf(len);
        for (int d = 0; d < dim; d++) axis[d] = next[d] * len;
    }
}

// Extrémités de l'ajustement : projections minimale et maximale des points sur l'axe.
static void tr_endpoints(const float *px, int n, int dim, float *e0, float *e1) {
    float mean[4], axis[4], lo = 1e30f, hi = -1e30f;
    tr_axis(px, n, dim, mean, axis);
    for (int i = 0; i < n; i++) {
        float t = 0;
        for (int d = 0; d < dim; d++) t += (px[i * dim + d] - mean[d]) * axis[d];
        if (t < lo) lo = t;
        if (t > hi) hi = t;
    }
    for (int d = 0; d < dim; d++) {
        e0[d] = mean[d] + axis[d] * lo;
        e1[d] = mean[d] + axis[d] * hi;
        if (e0[d] < 0) e0[d] = 0;
        if (e0[d] > 255) e0[d] = 255;
        if (e1[d] < 0) e1[d] = 0;
        if (e1[d] > 255) e1[d] = 255;
    }
}

static u16 tr_565(const float *c) {
    int r = (int)(c[0] * 31.0f / 255.0f + 0.5f), g = (int)(c[1] * 63.0f / 255.0f + 0.5f), b = (int)(c[2] * 31.0f / 255.0f + 0.5f);
    return (u16)((r << 11) | (g << 5) | b);
}
static void tr_565_rgb(u16 c, int *rgb) {
    rgb[0] = ((c >> 11) & 31) * 255 / 31, rgb[1] = ((c >> 5) & 63) * 255 / 63, rgb[2] = (c & 31) * 255 / 31;
}

// Bloc de couleur BC1 (8 octets) de 16 pixels RGBA ; `alpha` : mode 3 couleurs avec transparence permis.
static void tr_bc1_block(const u8 *rgba, u8 *out, int alpha) {
    float px[16 * 3];
    int n = 0, transparent = 0;
    for (int i = 0; i < 16; i++) {
        if (alpha && rgba[i * 4 + 3] < 128) {
            transparent = 1;
            continue;
        }
        px[n * 3] = rgba[i * 4], px[n * 3 + 1] = rgba[i * 4 + 1], px[n * 3 + 2] = rgba[i * 4 + 2];
        n++;
    }
    memset(out, 0, 8);
    if (!n) { // tout transparent : mode 3 couleurs, index 3 partout
        out[2] = out[3] = 0; // c0 = c1 = 0 (c0 <= c1)
        memset(out + 4, 0xFF, 4);
        return;
    }
    float e0[3], e1[3];
    tr_endpoints(px, n, 3, e0, e1);
    u16 c0 = tr_565(e1), c1 = tr_565(e0);
    // 4 couleurs : c0 > c1 ; 3 couleurs avec transparence : c0 <= c1.
    if (transparent ? c0 > c1 : c0 < c1) {
        u16 t = c0;
        c0 = c1, c1 = t;
    }
    if (!transparent && c0 == c1) {
        out[0] = (u8)c0, out[1] = (u8)(c0 >> 8), out[2] = (u8)c1, out[3] = (u8)(c1 >> 8);
        return; // index 0 partout
    }
    int p[4][3], a[3], b[3];
    tr_565_rgb(c0, a), tr_565_rgb(c1, b);
    for (int d = 0; d < 3; d++) {
        p[0][d] = a[d], p[1][d] = b[d];
        if (transparent) p[2][d] = (a[d] + b[d]) / 2, p[3][d] = 0;
        else p[2][d] = (2 * a[d] + b[d]) / 3, p[3][d] = (a[d] + 2 * b[d]) / 3;
    }
    u32 idx = 0;
    for (int i = 0; i < 16; i++) {
        int best = 0, best_err = 1 << 30;
        if (transparent && rgba[i * 4 + 3] < 128) best = 3;
        else
            for (int k = 0; k < (transparent ? 3 : 4); k++) {
                int err = 0;
                for (int d = 0; d < 3; d++) err += (rgba[i * 4 + d] - p[k][d]) * (rgba[i * 4 + d] - p[k][d]);
                if (err < best_err) best_err = err, best = k;
            }
        idx |= (u32)best << (2 * i);
    }
    out[0] = (u8)c0, out[1] = (u8)(c0 >> 8), out[2] = (u8)c1, out[3] = (u8)(c1 >> 8);
    out[4] = (u8)idx, out[5] = (u8)(idx >> 8), out[6] = (u8)(idx >> 16), out[7] = (u8)(idx >> 24);
}

// Bloc BC4 (8 octets) d'une composante (16 valeurs, pas `stride`) : mode 8 valeurs (a0 > a1).
static void tr_bc4_block(const u8 *v, int stride, u8 *out) {
    int lo = 255, hi = 0;
    for (int i = 0; i < 16; i++) {
        if (v[i * stride] < lo) lo = v[i * stride];
        if (v[i * stride] > hi) hi = v[i * stride];
    }
    memset(out, 0, 8);
    out[0] = (u8)hi, out[1] = (u8)lo;
    if (hi == lo) return;
    int pal[8];
    pal[0] = hi, pal[1] = lo;
    for (int i = 2; i < 8; i++) pal[i] = ((8 - i) * hi + (i - 1) * lo) / 7;
    u32 pos = 16;
    for (int i = 0; i < 16; i++) {
        int best = 0, best_err = 1 << 30;
        for (int k = 0; k < 8; k++) {
            int err = abs(v[i * stride] - pal[k]);
            if (err < best_err) best_err = err, best = k;
        }
        tr_put_bits(out, &pos, (u64)best, 3);
    }
}

// Bloc BC2 : alpha explicite sur 4 bits, puis couleur BC1 (toujours lue en 4 couleurs).
static void tr_bc2_block(const u8 *rgba, u8 *out) {
    memset(out, 0, 8);
    for (int i = 0; i < 16; i++) out[i / 2] |= (u8)(((rgba[i * 4 + 3] * 15 + 127) / 255) << (4 * (i & 1)));
    tr_bc1_block(rgba, out + 8, 0);
}

// Bloc BC7 en mode 6 : extrémités RGBA 7 bits + bit P chacune, index 4 bits (16 pondérations).
static void tr_bc7_block(const u8 *rgba, u8 *out) {
    static const int w4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
    float px[16 * 4], e[2][4];
    for (int i = 0; i < 64; i++) px[i] = rgba[i];
    tr_endpoints(px, 16, 4, e[0], e[1]);
    int q[2][4], pbit[2];
    for (int k = 0; k < 2; k++) {
        // Bit P choisi pour l'erreur d'arrondi la plus faible des quatre composantes.
        int best_err = 1 << 30;
        for (int p = 0; p < 2; p++) {
            int err = 0, c[4];
            for (int d = 0; d < 4; d++) {
                c[d] = (int)((e[k][d] - p) / 2.0f + 0.5f);
                if (c[d] < 0) c[d] = 0;
                if (c[d] > 127) c[d] = 127;
                int v = (c[d] << 1) | p;
                err += (v - (int)e[k][d]) * (v - (int)e[k][d]);
            }
            if (err < best_err) {
                best_err = err, pbit[k] = p;
                memcpy(q[k], c, sizeof c);
            }
        }
    }
    int v0[4], v1[4];
    for (int d = 0; d < 4; d++) v0[d] = (q[0][d] << 1) | pbit[0], v1[d] = (q[1][d] << 1) | pbit[1];
    u8 idx[16];
    for (int i = 0; i < 16; i++) {
        int best = 0, best_err = 1 << 30;
        for (int k = 0; k < 16; k++) {
            int err = 0;
            for (int d = 0; d < 4; d++) {
                int v = ((64 - w4[k]) * v0[d] + w4[k] * v1[d] + 32) >> 6;
                err += (rgba[i * 4 + d] - v) * (rgba[i * 4 + d] - v);
            }
            if (err < best_err) best_err = err, best = k;
        }
        idx[i] = (u8)best;
    }
    // Index du pixel 0 (« ancre ») écrit sur 3 bits : son bit fort doit être nul, sinon extrémités inversées.
    if (idx[0] & 8) {
        for (int d = 0; d < 4; d++) {
            int t = q[0][d];
            q[0][d] = q[1][d], q[1][d] = t;
        }
        int t = pbit[0];
        pbit[0] = pbit[1], pbit[1] = t;
        for (int i = 0; i < 16; i++) idx[i] = (u8)(15 - idx[i]);
    }
    memset(out, 0, 16);
    u32 pos = 0;
    tr_put_bits(out, &pos, 1u << 6, 7); // mode 6
    for (int d = 0; d < 4; d++) {
        tr_put_bits(out, &pos, (u64)q[0][d], 7);
        tr_put_bits(out, &pos, (u64)q[1][d], 7);
    }
    tr_put_bits(out, &pos, (u64)pbit[0], 1);
    tr_put_bits(out, &pos, (u64)pbit[1], 1);
    for (int i = 0; i < 16; i++) tr_put_bits(out, &pos, idx[i], i ? 4 : 3);
}

// --- Réduction ---

// Moyenne des f × f pixels de la bande `src` (largeur sw, `ch` octets par pixel) pour la ligne de sortie y
// (relative à la bande), vers `dst` (largeur dw).
static void tr_box_row(const u8 *src, u32 sw, u32 ch, u32 f, u32 y, u8 *dst, u32 dw) {
    for (u32 x = 0; x < dw; x++)
        for (u32 c = 0; c < ch; c++) {
            u32 sum = 0;
            for (u32 j = 0; j < f; j++) {
                const u8 *row = src + ((size_t)(y * f + j) * sw) * ch;
                for (u32 i = 0; i < f; i++) sum += row[(size_t)(x * f + i) * ch + c];
            }
            dst[(size_t)x * ch + c] = (u8)((sum + f * f / 2) / (f * f));
        }
}

// Décalage réellement appliqué à une image w × h de ce format (réduit pour garder des dimensions valides), 0 si
// le format n'est pas pris en charge.
u32 rcn_tex_shift(u32 format, u32 w, u32 h, u32 shift) {
    u32 ch, block;
    if (tr_kind(format, &ch, &block) == TR_NONE) return 0;
    while (shift && (block ? ((w >> shift) % 4 || (h >> shift) % 4 || (w >> shift) < 4 || (h >> shift) < 4)
                           : ((w >> shift) < 1 || (h >> shift) < 1)))
        shift--;
    return shift;
}

// Image `data` (w × h, `pitch` octets par ligne de pixels ou de blocs) réduite de 2^shift : nouvelle image
// allouée dans *out (à libérer), son pas dans *out_pitch, sa taille dans *nw × *nh. 0 si non réduite.
int rcn_tex_reduce(u32 format, u32 w, u32 h, const void *data, u32 pitch, u32 shift, u32 *nw, u32 *nh, void **out,
                   u32 *out_pitch) {
    u32 ch, block;
    int kind = tr_kind(format, &ch, &block);
    if (kind == TR_NONE || !data || !shift) return 0;
    // Formats compressés : dimensions de sortie multiples de 4 (exigé pour le niveau 0 d'une texture BC).
    shift = rcn_tex_shift(format, w, h, shift);
    if (!shift) return 0;
    u32 f = 1u << shift, ow = w >> shift, oh = h >> shift;
    const u8 *src = data;
    if (kind == TR_RAW) {
        u8 *dst = malloc((size_t)ow * oh * ch);
        u8 *strip = malloc((size_t)w * f * ch);
        if (!dst || !strip) {
            free(dst), free(strip);
            return 0;
        }
        for (u32 y = 0; y < oh; y++) {
            for (u32 j = 0; j < f; j++) memcpy(strip + (size_t)j * w * ch, src + (size_t)(y * f + j) * pitch, (size_t)w * ch);
            tr_box_row(strip, w, ch, f, 0, dst + (size_t)y * ow * ch, ow);
        }
        free(strip);
        *nw = ow, *nh = oh, *out = dst, *out_pitch = ow * ch;
        return 1;
    }
    // Compressé : bande de 4·f lignes décodées (une rangée de blocs de sortie), réduite en 4 lignes, réencodée.
    u32 obw = ow / 4, obh = oh / 4, rows = 4 * f;
    u8 *dst = malloc((size_t)obw * obh * block);
    u8 *strip = malloc((size_t)w * rows * ch);
    u8 *small = malloc((size_t)ow * 4 * ch);
    if (!dst || !strip || !small) {
        free(dst), free(strip), free(small);
        return 0;
    }
    for (u32 by = 0; by < obh; by++) {
        // Décodage des f rangées de blocs d'entrée de la bande.
        for (u32 r = 0; r < f; r++) {
            const u8 *blocks = src + (size_t)(by * f + r) * pitch;
            for (u32 bx = 0; bx < w / 4; bx++) {
                const u8 *b = blocks + (size_t)bx * block;
                u8 *px = strip + ((size_t)(r * 4) * w + bx * 4) * ch;
                int dpitch = (int)(w * ch);
                switch (kind) {
                case TR_BC1: bcdec_bc1(b, px, dpitch); break;
                case TR_BC2: bcdec_bc2(b, px, dpitch); break;
                case TR_BC3: bcdec_bc3(b, px, dpitch); break;
                case TR_BC4: bcdec_bc4(b, px, dpitch); break;
                case TR_BC5: bcdec_bc5(b, px, dpitch); break;
                case TR_BC7: bcdec_bc7(b, px, dpitch); break;
                }
            }
        }
        for (u32 y = 0; y < 4; y++) tr_box_row(strip, w, ch, f, y, small + (size_t)y * ow * ch, ow);
        // Encodage des blocs de sortie.
        for (u32 bx = 0; bx < obw; bx++) {
            u8 px[16 * 4];
            for (u32 y = 0; y < 4; y++) memcpy(px + y * 4 * ch, small + ((size_t)y * ow + bx * 4) * ch, 4 * ch);
            u8 *o = dst + ((size_t)by * obw + bx) * block;
            switch (kind) {
            case TR_BC1: tr_bc1_block(px, o, 1); break;
            case TR_BC2: tr_bc2_block(px, o); break;
            case TR_BC3:
                tr_bc4_block(px + 3, 4, o);
                tr_bc1_block(px, o + 8, 0);
                break;
            case TR_BC4: tr_bc4_block(px, 1, o); break;
            case TR_BC5:
                tr_bc4_block(px, 2, o);
                tr_bc4_block(px + 1, 2, o + 8);
                break;
            case TR_BC7: tr_bc7_block(px, o); break;
            }
        }
    }
    free(strip), free(small);
    *nw = ow, *nh = oh, *out = dst, *out_pitch = obw * block;
    return 1;
}
