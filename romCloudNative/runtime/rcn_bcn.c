// RomCloud Native : décompression des textures BC1 à BC5 (S3TC / RGTC) pour les GPU qui ne les lisent
// pas (la plupart des GPU de téléphone). Chaque bloc de 4 × 4 pixels devient 16 pixels RGBA de 8 bits.
#include "rcn_bcn.h"

#include <string.h>

static void rgb565(u16 c, u8 *out) {
    u32 r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[0] = (u8)((r << 3) | (r >> 2));
    out[1] = (u8)((g << 2) | (g >> 4));
    out[2] = (u8)((b << 3) | (b >> 2));
}

// Couleurs d'un bloc BC1 (8 octets) ; four : toujours 4 couleurs (BC2, BC3), sinon selon l'ordre des extrémités.
static void color_block(const u8 *b, u8 out[16][4], int four) {
    u16 c0 = (u16)(b[0] | b[1] << 8), c1 = (u16)(b[2] | b[3] << 8);
    u8 p[4][4];
    rgb565(c0, p[0]);
    rgb565(c1, p[1]);
    p[0][3] = p[1][3] = p[2][3] = 255;
    if (four || c0 > c1) {
        for (int k = 0; k < 3; k++) {
            p[2][k] = (u8)((2 * p[0][k] + p[1][k] + 1) / 3);
            p[3][k] = (u8)((p[0][k] + 2 * p[1][k] + 1) / 3);
        }
        p[3][3] = 255;
    } else {
        for (int k = 0; k < 3; k++) p[2][k] = (u8)((p[0][k] + p[1][k] + 1) / 2);
        p[3][0] = p[3][1] = p[3][2] = p[3][3] = 0; // noir transparent
    }
    u32 bits = (u32)b[4] | (u32)b[5] << 8 | (u32)b[6] << 16 | (u32)b[7] << 24;
    for (int i = 0; i < 16; i++) memcpy(out[i], p[(bits >> (2 * i)) & 3], 4);
}

// Canal d'un bloc BC4 / alpha de BC3 (8 octets) : 16 valeurs 8 bits (signées si snorm, rangées en octets signés).
static void channel_block(const u8 *b, u8 out[16], int snorm) {
    int v[8];
    int a0 = snorm ? (i8)b[0] : b[0], a1 = snorm ? (i8)b[1] : b[1];
    if (snorm) {
        if (a0 == -128) a0 = -127;
        if (a1 == -128) a1 = -127;
    }
    v[0] = a0;
    v[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i < 7; i++) v[i + 1] = ((7 - i) * a0 + i * a1 + 3) / 7;
    } else {
        for (int i = 1; i < 5; i++) v[i + 1] = ((5 - i) * a0 + i * a1 + 2) / 5;
        v[6] = snorm ? -127 : 0;
        v[7] = snorm ? 127 : 255;
    }
    u64 bits = 0;
    for (int i = 0; i < 6; i++) bits |= (u64)b[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++) out[i] = (u8)(v[(bits >> (3 * i)) & 7] & 0xFF);
}

int rcn_bcn_kind(u32 dxgi) {
    switch (dxgi) {
    case 70: case 71: case 72: return 1;
    case 73: case 74: case 75: return 2;
    case 76: case 77: case 78: return 3;
    case 79: case 80: return 4;
    case 81: return -4;
    case 82: case 83: return 5;
    case 84: return -5;
    default: return 0;
    }
}

void rcn_bcn_decode(u32 dxgi, const u8 *src, u32 src_pitch, u32 blocks_w, u32 blocks_h, u8 *dst, u32 dst_pitch) {
    int kind = rcn_bcn_kind(dxgi), snorm = kind < 0;
    if (kind < 0) kind = -kind;
    u32 block_bytes = kind == 1 || kind == 4 ? 8 : 16;
    for (u32 by = 0; by < blocks_h; by++) {
        for (u32 bx = 0; bx < blocks_w; bx++) {
            const u8 *b = src + (size_t)by * src_pitch + (size_t)bx * block_bytes;
            u8 px[16][4];
            switch (kind) {
            case 1: color_block(b, px, 0); break;
            case 2:
                color_block(b + 8, px, 1);
                for (int i = 0; i < 16; i++) {
                    u32 a = (b[i / 2] >> (4 * (i & 1))) & 15;
                    px[i][3] = (u8)(a * 17);
                }
                break;
            case 3: {
                u8 a[16];
                color_block(b + 8, px, 1);
                channel_block(b, a, 0);
                for (int i = 0; i < 16; i++) px[i][3] = a[i];
                break;
            }
            case 4: {
                u8 r[16];
                channel_block(b, r, snorm);
                for (int i = 0; i < 16; i++) px[i][0] = r[i], px[i][1] = px[i][2] = 0, px[i][3] = snorm ? 127 : 255;
                break;
            }
            case 5: {
                u8 r[16], g[16];
                channel_block(b, r, snorm);
                channel_block(b + 8, g, snorm);
                for (int i = 0; i < 16; i++) px[i][0] = r[i], px[i][1] = g[i], px[i][2] = 0, px[i][3] = snorm ? 127 : 255;
                break;
            }
            default: memset(px, 0, sizeof px); break;
            }
            for (int y = 0; y < 4; y++) memcpy(dst + (size_t)(by * 4 + y) * dst_pitch + (size_t)bx * 16, px[y * 4], 16);
        }
    }
}
