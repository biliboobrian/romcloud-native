// Essai de rcn_texreduce.c sur PC : image de synthèse encodée dans chaque format, réduite, décodée par bcdec
// et comparée à l'image filtrée directement (PSNR par format).
#include <math.h>
#include <stdio.h>
#include "../../runtime/rcn_texreduce.c"

static double psnr(const u8 *a, const u8 *b, size_t n) {
    double e = 0;
    for (size_t i = 0; i < n; i++) e += (double)(a[i] - b[i]) * (a[i] - b[i]);
    e /= (double)n;
    return e < 1e-9 ? 99 : 10 * log10(255.0 * 255.0 / e);
}

int main(void) {
    enum { W = 256, H = 256 };
    static u8 img[W * H * 4];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            u8 *p = img + (y * W + x) * 4;
            p[0] = (u8)x, p[1] = (u8)y, p[2] = (u8)(128 + 100 * sin(x * 0.05) * cos(y * 0.07));
            p[3] = (u8)(((x / 32 + y / 32) & 1) ? 255 : 40 + y / 2);
        }
    struct { const char *name; u32 format, ch, block; } fmts[] = {
        {"RGBA8", 28, 4, 0}, {"R8G8", 49, 2, 0}, {"R8", 61, 1, 0}, {"BC1", 71, 4, 8}, {"BC2", 74, 4, 16},
        {"BC3", 77, 4, 16}, {"BC4", 80, 1, 8}, {"BC5", 83, 2, 16}, {"BC7", 98, 4, 16},
    };
    int fail = 0;
    for (size_t t = 0; t < sizeof fmts / sizeof fmts[0]; t++) {
        u32 ch = fmts[t].ch, blk = fmts[t].block, fmt = fmts[t].format;
        // Source dans le format : pixels (canaux gardés) ou blocs encodés.
        static u8 chan[W * H * 4];
        for (int i = 0; i < W * H; i++)
            for (u32 c = 0; c < ch; c++) chan[i * ch + c] = img[i * 4 + c];
        static u8 src[W * H * 4];
        u32 pitch;
        if (!blk) {
            memcpy(src, chan, (size_t)W * H * ch);
            pitch = W * ch;
        } else {
            pitch = (W / 4) * blk;
            for (int by = 0; by < H / 4; by++)
                for (int bx = 0; bx < W / 4; bx++) {
                    u8 px[64];
                    for (int y = 0; y < 4; y++) memcpy(px + y * 4 * ch, chan + ((by * 4 + y) * W + bx * 4) * ch, 4 * ch);
                    u8 *o = src + by * pitch + bx * blk;
                    switch (fmt) {
                    case 71: tr_bc1_block(px, o, 1); break;
                    case 74: tr_bc2_block(px, o); break;
                    case 77: tr_bc4_block(px + 3, 4, o), tr_bc1_block(px, o + 8, 0); break;
                    case 80: tr_bc4_block(px, 1, o); break;
                    case 83: tr_bc4_block(px, 2, o), tr_bc4_block(px + 1, 2, o + 8); break;
                    case 98: tr_bc7_block(px, o); break;
                    }
                }
        }
        // Source décodée (référence : l'image que voit le jeu), puis filtrée directement.
        static u8 dec[W * H * 4];
        if (!blk) memcpy(dec, src, (size_t)W * H * ch);
        else
            for (int by = 0; by < H / 4; by++)
                for (int bx = 0; bx < W / 4; bx++) {
                    const u8 *b = src + by * pitch + bx * blk;
                    u8 *px = dec + ((by * 4) * W + bx * 4) * ch;
                    switch (fmt) {
                    case 71: bcdec_bc1(b, px, W * ch); break;
                    case 74: bcdec_bc2(b, px, W * ch); break;
                    case 77: bcdec_bc3(b, px, W * ch); break;
                    case 80: bcdec_bc4(b, px, W * ch); break;
                    case 83: bcdec_bc5(b, px, W * ch); break;
                    case 98: bcdec_bc7(b, px, W * ch); break;
                    }
                }
        double enc = psnr(chan, dec, (size_t)W * H * ch);
        for (u32 shift = 1; shift <= 2; shift++) {
            u32 f = 1u << shift, ow = W >> shift, oh = H >> shift, nw, nh, op;
            void *out;
            if (!rcn_tex_reduce(fmt, W, H, src, pitch, shift, &nw, &nh, &out, &op) || nw != ow || nh != oh) {
                printf("%-6s shift %u : ECHEC de la reduction\n", fmts[t].name, shift);
                fail = 1;
                continue;
            }
            static u8 ref[W * H * 4], got[W * H * 4];
            for (u32 y = 0; y < oh; y++) tr_box_row(dec, W, ch, f, y, ref + y * ow * ch, ow);
            if (!blk) memcpy(got, out, (size_t)ow * oh * ch);
            else
                for (u32 by = 0; by < oh / 4; by++)
                    for (u32 bx = 0; bx < ow / 4; bx++) {
                        const u8 *b = (u8 *)out + by * op + bx * blk;
                        u8 *px = got + ((by * 4) * ow + bx * 4) * ch;
                        switch (fmt) {
                        case 71: bcdec_bc1(b, px, ow * ch); break;
                        case 74: bcdec_bc2(b, px, ow * ch); break;
                        case 77: bcdec_bc3(b, px, ow * ch); break;
                        case 80: bcdec_bc4(b, px, ow * ch); break;
                        case 83: bcdec_bc5(b, px, ow * ch); break;
                        case 98: bcdec_bc7(b, px, ow * ch); break;
                        }
                    }
            double q = psnr(ref, got, (size_t)ow * oh * ch);
            int ok = q > (blk ? 28.0 : 45.0);
            fail |= !ok;
            printf("%-6s shift %u : %ux%u, encodage source %.1f dB, reduit/reference %.1f dB %s\n", fmts[t].name, shift, nw, nh,
                   enc, q, ok ? "ok" : "MAUVAIS");
            free(out);
        }
    }
    printf(fail ? "ECHEC\n" : "tout bon\n");
    return fail;
}
