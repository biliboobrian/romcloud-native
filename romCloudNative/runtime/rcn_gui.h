// RomCloud Native : état de la fenêtre et de l'affichage d'un jeu, partagé par user32, gdi32,
// DirectDraw et DirectInput. Le jeu n'a qu'une fenêtre, qui occupe tout l'écran de l'hôte.
#ifndef RCN_GUI_H
#define RCN_GUI_H

#include "rcn_internal.h"

typedef struct {
    u32 hwnd, message, wparam, lparam;
} RcnMsg;

// Contrôle d'une boîte de dialogue : case cochée, ligne choisie d'une liste.
typedef struct {
    u32 id;
    u32 checked;
    u32 selection;
    u32 items;
} RcnControl;

#define RCN_HWND 0x00010001u         // la fenêtre du jeu
#define RCN_HWND_DIALOG 0x00010002u  // boîte de dialogue en cours
#define RCN_HWND_DESKTOP 0x00010010u

typedef struct RcnGui {
    u32 wndproc; // procédure de la classe de fenêtre du jeu
    u32 window_created;
    u32 window_longs[2][8]; // SetWindowLong : fenêtre du jeu, boîte de dialogue ; par indice / 4 (GWL_USERDATA…)
    RcnMsg queue[64];
    u32 queue_head, queue_count;
    // Boîte de dialogue simulée.
    RcnControl controls[128];
    u32 control_count;
    u32 dialog_result;
    int dialog_ended;
    // Affichage : mode demandé par le jeu, surface affichée.
    u32 width, height, bpp;
    u64 frames;
    u64 last_present_ns;
} RcnGui;

// Surface DirectDraw : données de l'objet COM (COM_DATA), partagées avec Direct3D (cible de rendu, textures).
typedef struct {
    u32 width, height, bpp, pitch;
    u32 pixels;   // adresse des pixels dans la mémoire du jeu
    u32 caps;
    u32 has_key, key; // couleur transparente des copies
    u32 back;     // surface arrière attachée (objet COM), pour la primaire
    u32 primary;  // affichée à l'écran
    u32 masks[4]; // bits du rouge, du vert, du bleu et de l'opacité dans un pixel
    // Contexte de dessin gdi32 (GetDC) : couleur et hauteur du texte.
    u32 text_color, font_height, opaque, back_color;
} RcnSurface;

// Objet Direct3D 7 obtenu de DirectDraw (rcn_d3d.c).
u64 rcn_d3d_new(RcnCtx *ctx);
// Écrit un DDPIXELFORMAT (32 octets) : couleurs RVB aux masques donnés.
void rcn_write_pixel_format(u64 at, u32 bpp, const u32 masks[4]);

// État de la fenêtre, créé à la demande. Jamais NULL (arrêt du jeu si la mémoire manque).
RcnGui *rcn_gui(RcnCtx *ctx);
// Ajoute un message à la file de la fenêtre du jeu.
void rcn_post_message(RcnCtx *ctx, u32 message, u32 wparam, u32 lparam);
// Affiche une image (bpp : 16 ou 32) ; cadence limitée à 60 images par seconde si `wait`.
void rcn_present(RcnCtx *ctx, const void *pixels, u32 width, u32 height, u32 pitch, u32 bpp, int wait);
// État de la manette (bits RCN_PAD_*).
u32 rcn_pad(RcnCtx *ctx);

// Hôte par défaut (essais en ligne de commande), piloté par des variables d'environnement.
void rcn_default_present(void *user, const void *pixels, int width, int height, int pitch, int format);
uint32_t rcn_default_pad(void *user);

#endif
