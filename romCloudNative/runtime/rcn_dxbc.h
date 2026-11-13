// RomCloud Native : traduction des nuanceurs DXBC (Direct3D 10/11, modèles 4.0 à 5.0) en GLSL.
//
// GLSL produit : « #version 310 es » sous OpenGL ES, « #version 430 » sur PC. Les liaisons sont fixées
// dans le texte (layout(binding = …)), selon les conventions ci-dessous, et le contexte Direct3D lie ses
// objets aux mêmes numéros :
//   tampons de constantes   bloc std140 « CB<étape>_<case> », liaison D3D_CB_BINDING(étape, case) ;
//   textures                sampler par paire (texture, échantillonneur) employée, unité donnée par pairs[] ;
//   tampons structurés      SSBO, liaison D3D_SSBO_BINDING(étape, case).
// Varyings : un par élément de signature, nommé d'après sa sémantique (« vs_TEXCOORD3 »), si bien que
// les étapes se relient par nom quelle que soit la façon dont chacune range ses registres.
// Position : gl_Position.y est inversé et z ramené de [0, w] à [-w, w] (voir rcn_d3d11.h).
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
} DxbcSig;

// Paire (texture, échantillonneur) employée par le nuanceur, sur une unité de texture.
typedef struct {
    u32 t;        // case de vue (t#)
    i32 s;        // case d'échantillonneur (s#), -1 : lecture directe (texelFetch, sans filtrage)
    u32 unit;
    u32 dim;      // RESOURCE_DIMENSION du DXBC (2 TEX1D … 10 TEXCUBEARRAY, 1 BUFFER)
    u32 shadow;   // échantillonnage avec comparaison
    u32 ret;      // type rendu : 0 float, 1 int, 2 uint
} DxbcPair;

typedef struct DxbcShader {
    u32 stage;
    u32 version;  // 0x50 pour 5.0…
    char *glsl;
    DxbcSig in[32], out[32];
    u32 in_count, out_count;
    u32 cb_mask;          // cases de tampons de constantes lues
    u32 cb_size[14];      // en constantes de 16 octets
    DxbcPair pairs[32];
    u32 pair_count;
    u32 ssbo_mask;        // vues de tampons structurés ou bruts lues (t#), en SSBO
    u32 uav_mask;         // u#
    u32 threads[3];       // calcul : taille d'un groupe
    char error[160];
} DxbcShader;

// Traduit un nuanceur (conteneur DXBC complet). NULL en cas d'échec (message dans le journal).
DxbcShader *dxbc_translate(const u8 *code, size_t size, int es);
void dxbc_free(DxbcShader *dx);

#endif
