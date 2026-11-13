// RomCloud Native : traduction des nuanceurs DXBC en GLSL (voir rcn_dxbc.h). Pas encore écrite.
#include <stdlib.h>

#include "rcn_dxbc.h"

DxbcShader *dxbc_translate(const u8 *code, size_t size, int es) {
    (void)code, (void)size, (void)es;
    return NULL;
}
void dxbc_free(DxbcShader *dx) {
    if (!dx) return;
    free(dx->glsl);
    free(dx);
}
