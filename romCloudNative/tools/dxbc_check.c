// Banc d'essai du traducteur de nuanceurs (rcn_dxbc.c), sur PC, sans lancer de jeu : cherche les
// conteneurs DXBC dans des fichiers (données d'un jeu Unity…), traduit chaque nuanceur de sommets ou de
// pixels en GLSL (ES par défaut, comme sur le téléphone) et le fait compiler par le pilote OpenGL.
//
//   dxbc_check [--desktop] [--dump dossier] fichier…
//
// Compilation (llvm-mingw) :
//   clang -O1 -I runtime tools/dxbc_check.c runtime/rcn_dxbc.c -lopengl32 -lgdi32 -luser32 -o dxbc_check.exe
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "rcn_dxbc.h"

void rcn_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

typedef unsigned int (__stdcall *CreateShaderFn)(unsigned int);
typedef void (__stdcall *ShaderSourceFn)(unsigned int, int, const char *const *, const int *);
typedef void (__stdcall *CompileShaderFn)(unsigned int);
typedef void (__stdcall *GetShaderivFn)(unsigned int, unsigned int, int *);
typedef void (__stdcall *GetShaderInfoLogFn)(unsigned int, int, int *, char *);
typedef void (__stdcall *DeleteShaderFn)(unsigned int);
static CreateShaderFn glCreateShader_;
static ShaderSourceFn glShaderSource_;
static CompileShaderFn glCompileShader_;
static GetShaderivFn glGetShaderiv_;
static GetShaderInfoLogFn glGetShaderInfoLog_;
static DeleteShaderFn glDeleteShader_;

static int gl_init(void) {
    static const PIXELFORMATDESCRIPTOR pfd = {sizeof pfd, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL, PFD_TYPE_RGBA, 32};
    HWND w = CreateWindowExA(0, "STATIC", "dxbc", 0, 0, 0, 16, 16, NULL, NULL, NULL, NULL);
    HDC dc = GetDC(w);
    int f = ChoosePixelFormat(dc, &pfd);
    if (!f || !SetPixelFormat(dc, f, &pfd)) return 0;
    HGLRC rc = wglCreateContext(dc);
    if (!rc || !wglMakeCurrent(dc, rc)) return 0;
    glCreateShader_ = (CreateShaderFn)wglGetProcAddress("glCreateShader");
    glShaderSource_ = (ShaderSourceFn)wglGetProcAddress("glShaderSource");
    glCompileShader_ = (CompileShaderFn)wglGetProcAddress("glCompileShader");
    glGetShaderiv_ = (GetShaderivFn)wglGetProcAddress("glGetShaderiv");
    glGetShaderInfoLog_ = (GetShaderInfoLogFn)wglGetProcAddress("glGetShaderInfoLog");
    glDeleteShader_ = (DeleteShaderFn)wglGetProcAddress("glDeleteShader");
    return glCreateShader_ && glShaderSource_ && glCompileShader_ && glGetShaderiv_;
}

// Compile ; 1 si accepté (sinon le journal dans log).
static int compile(unsigned type, const char *src, char *log, int size) {
    unsigned sh = glCreateShader_(type);
    glShaderSource_(sh, 1, &src, NULL);
    glCompileShader_(sh);
    int ok = 0;
    glGetShaderiv_(sh, 0x8B81, &ok);
    log[0] = 0;
    if (!ok) glGetShaderInfoLog_(sh, size, NULL, log);
    glDeleteShader_(sh);
    return ok;
}

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *p = malloc((size_t)n + 1);
    if (p && fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        p = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return p;
}

static unsigned long long fnv(const unsigned char *p, size_t n) {
    unsigned long long h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

int main(int argc, char **argv) {
    int es = 1;
    const char *dump = NULL;
    if (!gl_init()) {
        fprintf(stderr, "OpenGL indisponible\n");
        return 2;
    }
    unsigned total = 0, skipped = 0, unreadable = 0, untranslated = 0, rejected = 0;
    // Nuanceurs déjà vus (le même nuanceur revient dans plusieurs fichiers).
    static unsigned long long seen[65536];
    unsigned seen_count = 0;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--desktop")) {
            es = 0;
            continue;
        }
        if (!strcmp(argv[a], "--dump") && a + 1 < argc) {
            dump = argv[++a];
            continue;
        }
        size_t size;
        unsigned char *data = read_file(argv[a], &size);
        if (!data) continue;
        for (size_t i = 0; i + 32 <= size; i++) {
            if (memcmp(data + i, "DXBC", 4)) continue;
            unsigned len;
            memcpy(&len, data + i + 24, 4);
            if (len < 32 || i + len > size) continue;
            unsigned long long h = fnv(data + i, len);
            int dup = 0;
            for (unsigned k = 0; k < seen_count && !dup; k++) dup = seen[k] == h;
            if (dup) continue;
            if (seen_count < 65536) seen[seen_count++] = h;
            if (getenv("DXBC_TRACE")) fprintf(stderr, "essai %016llx (%s @%zu)\n", h, argv[a], i);
            DxbcShader *dx = dxbc_parse(data + i, len);
            if (!dx) {
                unreadable++;
                continue;
            }
            if (dx->stage != DXBC_VS && dx->stage != DXBC_PS) {
                skipped++;
                dxbc_free(dx);
                continue;
            }
            total++;
            char *src = dxbc_glsl(dx, NULL, NULL, es);
            if (!src) {
                untranslated++;
                printf("non traduit %016llx (%s) : %s\n", h, dx->stage == DXBC_VS ? "sommets" : "pixels", dx->error);
            } else {
                char log[2048];
                if (!compile(dx->stage == DXBC_VS ? 0x8B31 : 0x8B30, src, log, sizeof log)) {
                    rejected++;
                    printf("refusé %016llx (%s) :\n%s", h, dx->stage == DXBC_VS ? "sommets" : "pixels", log);
                    if (dump) {
                        char path[512];
                        snprintf(path, sizeof path, "%s/%016llx.%s", dump, h, dx->stage == DXBC_VS ? "vert" : "frag");
                        FILE *f = fopen(path, "wb");
                        if (f) fputs(src, f), fclose(f);
                    }
                }
                free(src);
            }
            dxbc_free(dx);
        }
        free(data);
    }
    printf("%u nuanceurs (sommets et pixels) : %u non traduits, %u refusés ; %u d'autres étapes, %u illisibles\n", total,
           untranslated, rejected, skipped, unreadable);
    return rejected || untranslated ? 1 : 0;
}
