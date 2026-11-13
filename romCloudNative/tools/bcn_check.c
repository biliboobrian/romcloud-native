// Vérifie la décompression BC1 à BC5 (rcn_bcn.c) contre celle du GPU du PC : blocs aléatoires envoyés
// compressés à OpenGL, relus par glGetTexImage, comparés à notre décodage (écart toléré : 1 par canal pour
// les interpolations, que la norme laisse à l'arrondi du matériel).
//
//   clang -O1 -I runtime tools/bcn_check.c runtime/rcn_bcn.c -lopengl32 -lgdi32 -luser32 -o bcn_check.exe
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <GL/gl.h>

#include "rcn_bcn.h"

typedef void (__stdcall *CompressedTexImage2DFn)(unsigned, int, unsigned, int, int, int, int, const void *);
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_COMPRESSED_RED_RGTC1 0x8DBB
#define GL_COMPRESSED_SIGNED_RED_RGTC1 0x8DBC
#define GL_COMPRESSED_RG_RGTC2 0x8DBD
#define GL_COMPRESSED_SIGNED_RG_RGTC2 0x8DBE

int main(void) {
    static const PIXELFORMATDESCRIPTOR pfd = {sizeof pfd, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL, PFD_TYPE_RGBA, 32};
    HWND w = CreateWindowExA(0, "STATIC", "bcn", 0, 0, 0, 16, 16, NULL, NULL, NULL, NULL);
    HDC dc = GetDC(w);
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    wglMakeCurrent(dc, wglCreateContext(dc));
    CompressedTexImage2DFn upload = (CompressedTexImage2DFn)wglGetProcAddress("glCompressedTexImage2D");
    if (!upload) return 2;
    static const struct {
        u32 dxgi;
        unsigned gl, bytes;
        int snorm;
        const char *name;
    } fmts[] = {
        {71, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 8, 0, "BC1"}, {74, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 16, 0, "BC2"},
        {77, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 16, 0, "BC3"}, {80, GL_COMPRESSED_RED_RGTC1, 8, 0, "BC4"},
        {81, GL_COMPRESSED_SIGNED_RED_RGTC1, 8, 1, "BC4s"},  {83, GL_COMPRESSED_RG_RGTC2, 16, 0, "BC5"},
        {84, GL_COMPRESSED_SIGNED_RG_RGTC2, 16, 1, "BC5s"},
    };
    enum { N = 64 }; // 64 × 64 blocs : 256 × 256 pixels
    static u8 blocks[N * N * 16], ours[N * 4 * N * 4 * 4], gpu[N * 4 * N * 4 * 4];
    srand(1234);
    int failed = 0;
    unsigned tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    for (size_t f = 0; f < sizeof fmts / sizeof fmts[0]; f++) {
        for (size_t i = 0; i < sizeof blocks; i++) blocks[i] = (u8)rand();
        upload(GL_TEXTURE_2D, 0, fmts[f].gl, N * 4, N * 4, 0, N * N * fmts[f].bytes, blocks);
        rcn_bcn_decode(fmts[f].dxgi, blocks, N * fmts[f].bytes, N, N, ours, N * 16);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, fmts[f].snorm ? GL_BYTE : GL_UNSIGNED_BYTE, gpu);
        int channels = fmts[f].dxgi >= 82 ? 2 : fmts[f].dxgi >= 79 ? 1 : 4;
        int worst = 0, bad = 0;
        for (int p = 0; p < N * 4 * N * 4; p++) {
            for (int c = 0; c < channels; c++) {
                int a = fmts[f].snorm ? (signed char)ours[4 * p + c] : ours[4 * p + c];
                int b = fmts[f].snorm ? (signed char)gpu[4 * p + c] : gpu[4 * p + c];
                int d = abs(a - b);
                if (d > worst) worst = d;
                if (d > 1) bad++;
            }
        }
        printf("%-5s écart maximal %d, %d valeurs au-delà de 1\n", fmts[f].name, worst, bad);
        failed |= bad != 0;
    }
    return failed;
}
