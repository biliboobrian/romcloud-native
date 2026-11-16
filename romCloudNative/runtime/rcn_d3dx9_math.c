// RomCloud Native : calculs de D3DX 9 (d3dx9_*.dll) que des jeux importent sans le reste de D3DX :
// matrices 4×4 (lignes, comme Direct3D), plans et vecteurs. Les résultats passent par une copie : la
// sortie peut être aussi une entrée.
#include <math.h>

#include "rcn_internal.h"

typedef struct {
    float m[4][4];
} Mat;

static Mat load_mat(u64 p) {
    Mat r;
    memcpy(&r, PTR(p), sizeof r);
    return r;
}
static Mat mul(const Mat *a, const Mat *b) {
    Mat r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] + a->m[i][3] * b->m[3][j];
    return r;
}
static Mat transpose(const Mat *a) {
    Mat r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) r.m[i][j] = a->m[j][i];
    return r;
}

// D3DXMatrixMultiply(sortie, m1, m2) : m1 × m2 ; …Transpose : (m1 × m2) transposée ; D3DXMatrixTranspose(sortie, m).
static void x_D3DXMatrixMultiply(RcnCtx *ctx) {
    Mat a = load_mat(A1), b = load_mat(A2), r = mul(&a, &b);
    memcpy(PTR(A0), &r, sizeof r);
    RET(A0);
}
static void x_D3DXMatrixMultiplyTranspose(RcnCtx *ctx) {
    Mat a = load_mat(A1), b = load_mat(A2), p = mul(&a, &b), r = transpose(&p);
    memcpy(PTR(A0), &r, sizeof r);
    RET(A0);
}
static void x_D3DXMatrixTranspose(RcnCtx *ctx) {
    Mat a = load_mat(A1), r = transpose(&a);
    memcpy(PTR(A0), &r, sizeof r);
    RET(A0);
}
// D3DXMatrixInverse(sortie, &déterminant, m) : NULL si la matrice n'est pas inversible.
static void x_D3DXMatrixInverse(RcnCtx *ctx) {
    Mat a = load_mat(A2);
    const float *m = &a.m[0][0];
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (A1) memcpy(PTR(A1), &det, 4);
    if (det == 0.0f) {
        RET(0);
        return;
    }
    for (int i = 0; i < 16; i++) inv[i] /= det;
    memcpy(PTR(A0), inv, sizeof inv);
    RET(A0);
}
// D3DXPlaneNormalize(sortie, plan) : (a, b, c, d) divisé par la longueur de (a, b, c).
static void x_D3DXPlaneNormalize(RcnCtx *ctx) {
    float p[4];
    memcpy(p, PTR(A1), sizeof p);
    float len = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    for (int i = 0; i < 4; i++) p[i] = len ? p[i] / len : 0.0f;
    memcpy(PTR(A0), p, sizeof p);
    RET(A0);
}
// Vecteur ligne (x, y, z, w) × matrice.
static void row_times(const float v[4], const Mat *m, float out[4]) {
    for (int j = 0; j < 4; j++) out[j] = v[0] * m->m[0][j] + v[1] * m->m[1][j] + v[2] * m->m[2][j] + v[3] * m->m[3][j];
}
// D3DXPlaneTransform(sortie, plan, m)
static void x_D3DXPlaneTransform(RcnCtx *ctx) {
    float p[4], r[4];
    memcpy(p, PTR(A1), sizeof p);
    Mat m = load_mat(A2);
    row_times(p, &m, r);
    memcpy(PTR(A0), r, sizeof r);
    RET(A0);
}
// D3DXVec3Normalize(sortie, v)
static void x_D3DXVec3Normalize(RcnCtx *ctx) {
    float v[3];
    memcpy(v, PTR(A1), sizeof v);
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; i < 3; i++) v[i] = len ? v[i] / len : 0.0f;
    memcpy(PTR(A0), v, sizeof v);
    RET(A0);
}
// D3DXVec3TransformCoord(sortie, v, m) : (x, y, z, 1) × m, divisé par w.
static void x_D3DXVec3TransformCoord(RcnCtx *ctx) {
    float v[4] = {0, 0, 0, 1}, r[4];
    memcpy(v, PTR(A1), 12);
    Mat m = load_mat(A2);
    row_times(v, &m, r);
    float w = r[3] ? r[3] : 1.0f;
    float out[3] = {r[0] / w, r[1] / w, r[2] / w};
    memcpy(PTR(A0), out, sizeof out);
    RET(A0);
}
// D3DXVec3TransformNormal(sortie, v, m) : (x, y, z, 0) × m.
static void x_D3DXVec3TransformNormal(RcnCtx *ctx) {
    float v[4] = {0, 0, 0, 0}, r[4];
    memcpy(v, PTR(A1), 12);
    Mat m = load_mat(A2);
    row_times(v, &m, r);
    memcpy(PTR(A0), r, 12);
    RET(A0);
}

const RcnShim rcn_d3dx9_math[] = {
    {"D3DXMatrixMultiply", x_D3DXMatrixMultiply, 0},
    {"D3DXMatrixMultiplyTranspose", x_D3DXMatrixMultiplyTranspose, 0},
    {"D3DXMatrixTranspose", x_D3DXMatrixTranspose, 0},
    {"D3DXMatrixInverse", x_D3DXMatrixInverse, 0},
    {"D3DXPlaneNormalize", x_D3DXPlaneNormalize, 0},
    {"D3DXPlaneTransform", x_D3DXPlaneTransform, 0},
    {"D3DXVec3Normalize", x_D3DXVec3Normalize, 0},
    {"D3DXVec3TransformCoord", x_D3DXVec3TransformCoord, 0},
    {"D3DXVec3TransformNormal", x_D3DXVec3TransformNormal, 0},
    {NULL, NULL, 0},
};
