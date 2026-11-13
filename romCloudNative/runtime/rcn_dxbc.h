// RomCloud Native : traduction des nuanceurs DXBC (Direct3D 10/11, modèles 4.0 à 5.0) en GLSL.
//
// En deux temps : dxbc_parse lit le conteneur une fois (signatures, déclarations, instructions) ;
// dxbc_glsl écrit le texte pour une paire d'étapes (les varyings d'un nuanceur de sommets suivent
// l'interpolation que demande le nuanceur de pixels, et une entrée de pixels que les sommets n'écrivent
// pas vaut zéro), si bien que les deux s'éditent toujours ensemble, même sous OpenGL ES.
//
// GLSL produit : « #version 310 es » (avec les extensions voulues) sous OpenGL ES, « #version 430 » sur
// PC. Liaisons fixées dans le texte (layout(binding = …)) :
//   tampons de constantes   bloc std140 « CB<étape>_<case> », liaison D3D_CB_BINDING(étape, case) ;
//   textures                un sampler par paire (texture, échantillonneur) employée, unité de pairs[] ;
//   tampons structurés/bruts SSBO, liaison D3D_SSBO_BINDING(étape, case).
// Registres temporaires : uvec4 (les bits des entiers ne passent jamais par un flottant).
// Varyings : un par élément de signature, « v_<SÉMANTIQUE><index> », toujours sur 4 composantes.
// Position : gl_Position.y inversé, z ramené de [0, w] à [-w, w] (voir rcn_d3d11.h).
#ifndef RCN_DXBC_H
#define RCN_DXBC_H

#include "rcn_internal.h"

enum { DXBC_VS = 0, DXBC_PS = 1, DXBC_GS = 2, DXBC_HS = 3, DXBC_DS = 4, DXBC_CS = 5 };

// Liaisons des tampons de constantes : 14 par étape (le calcul reprend celles des sommets).
#define D3D_CB_BINDING(stage, slot) (((stage) == DXBC_CS ? 0 : (stage) * 14) + (slot))
// Unités de texture : pixels 0-15, sommets 16-31, géométrie 32-46 ; calcul 0-15.
#define D3D_UNIT_BASE(stage) ((stage) == DXBC_PS || (stage) == DXBC_CS ? 0 : (stage) == DXBC_VS ? 16 : 32)
#define D3D_SSBO_BINDING(stage, slot) (((stage) == DXBC_PS ? 8 : 0) + (slot))

typedef struct {
    char name[32];
    u32 index;    // index de sémantique
    u32 reg;      // registre
    u32 mask;     // composantes (bits xyzw)
    u32 sysval;   // D3D_NAME (0 : aucune)
    u32 type;     // 1 uint, 2 int, 3 float
    u32 interp;   // pixels : interpolation déclarée (1 constante, 2 linéaire, 3 centroïde…)
} DxbcSig;

// Paire (texture, échantillonneur) employée par le nuanceur, sur une unité de texture.
typedef struct {
    u32 t;        // case de vue (t#)
    i32 s;        // case d'échantillonneur (s#), -1 : lecture directe (texelFetch, sans filtrage)
    u32 unit;
    u32 dim;      // RESOURCE_DIMENSION du DXBC (1 BUFFER, 2 TEX1D … 10 TEXCUBEARRAY)
    u32 shadow;   // échantillonnage avec comparaison
    u32 ret;      // type rendu : 0 float, 1 int, 2 uint
} DxbcPair;

typedef struct DxbcShader DxbcShader;
struct DxbcShader {
    u32 stage;
    u32 version;          // 0x50 pour 5.0…
    DxbcSig in[32], out[32];
    u32 in_count, out_count;
    u32 cb_mask;          // cases de tampons de constantes lues
    u32 cb_size[14];      // en constantes de 16 octets
    DxbcPair pairs[32];
    u32 pair_count;
    u32 threads[3];       // calcul : taille d'un groupe
    struct DxbcPriv *priv; // instructions décodées (rcn_dxbc.c)
    char error[160];
};

// Lit un nuanceur (conteneur DXBC complet). NULL en cas d'échec (message dans le journal).
DxbcShader *dxbc_parse(const u8 *code, size_t size);
// Texte GLSL de `dx`, relié à l'étape précédente `prev` et à la suivante `next` (NULL si aucune).
// À libérer par free ; NULL si une instruction n'est pas traduite (message dans dx->error).
char *dxbc_glsl(const DxbcShader *dx, const DxbcShader *prev, const DxbcShader *next, int es);
void dxbc_free(DxbcShader *dx);

#endif
