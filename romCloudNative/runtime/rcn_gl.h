// RomCloud Native : rendu Direct3D par le processeur graphique (OpenGL ES 3 sous Android), à la place
// du rendu logiciel de rcn_d3d.c quand l'hôte fournit une fenêtre. Voir rcn_gl.c.
#ifndef RCN_GL_H
#define RCN_GL_H

#include "rcn_gui.h"

// Sommet prêt à dessiner : position en points de l'image du jeu, couleur de 0 à 255, coordonnées de texture.
typedef struct {
    float x, y;
    float c[4]; // r, g, b, a
    float u, v;
} RcnVertex;

// État de rendu Direct3D d'un appel de dessin (valeurs de Direct3D 7).
typedef struct {
    i32 clip[4]; // fenêtre d'affichage : gauche, haut, droite, bas
    u32 blend, src_factor, dst_factor;
    u32 alpha_test, alpha_ref, alpha_func;
    u32 color_op, color_arg1, color_arg2, alpha_op, alpha_arg1, alpha_arg2;
    u32 clamp_u, clamp_v, linear;
    u32 factor;    // couleur constante 0xAARRGGBB
    u32 cull;      // 1 aucune, 2 sens horaire, 3 sens antihoraire
    u32 texture;   // surface (objet COM) de l'étage 0, ou 0
    u32 color_key; // la couleur transparente de la texture s'applique
} RcnGlState;

enum {
    RCN_GL_NONE = 0,   // pas de rendu par GPU : rendu logiciel
    RCN_GL_READY = 1,  // dessiner par GPU
    RCN_GL_WAIT = 2,   // GPU prévu mais fenêtre pas encore disponible : ne rien dessiner
};

// À appeler avant de dessiner dans une cible de width × height points.
int rcn_gl_begin(RcnCtx *ctx, u32 width, u32 height);
// Remplit un rectangle (gauche, haut, droite, bas) d'une couleur 0xAARRGGBB.
void rcn_gl_clear(const i32 rect[4], u32 color);
// Dessine `count` sommets (type de primitive Direct3D : 1 points … 6 éventail de triangles).
void rcn_gl_draw(u32 type, const RcnVertex *vertices, u32 count, const RcnGlState *state);
// Affiche l'image en cours (wait : cadence limitée à 60 images par seconde). Renvoie 0 si rien n'a été
// dessiné par GPU depuis la précédente.
int rcn_gl_present(RcnCtx *ctx, int wait);
// La surface disparaît : sa texture est supprimée au prochain rendu.
void rcn_gl_forget(u32 surface);

#endif
