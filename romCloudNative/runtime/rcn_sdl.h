// RomCloud Native : SDL2 et OpenGL des jeux 64 bits (rcn_sdl.c, rcn_sdl_gl.c).
#ifndef RCN_SDL_H
#define RCN_SDL_H

#include "rcn_internal.h"

// SDL2 vue par le jeu (« SDL2.dll »).
extern const RcnShim rcn_sdl2[];
extern const size_t rcn_sdl2_count;
// Flux, surfaces et petite bibliothèque C de SDL (rcn_sdl_data.c), cherchés avant rcn_sdl2.
extern const RcnShim rcn_sdl2_data[];
extern const size_t rcn_sdl2_data_count;
// Message rendu par SDL_GetError.
void rcn_sdl_set_error(const char *text);

// --- OpenGL (rcn_sdl_gl.c), contexte courant sur le thread appelant ---

// Charge les fonctions du pilote ; es : OpenGL ES 3 (Android), sinon OpenGL de bureau. 0 si l'essentiel manque.
int rcn_sdlgl_load(void *(*get_proc)(const char *name), int es);
// Image où le jeu dessine quand il vise le framebuffer 0 : créée ou mise à la taille width × height.
void rcn_sdlgl_resize(u32 width, u32 height);
// Extension d'OpenGL présente (nom exact), telle qu'annoncée au jeu.
int rcn_sdlgl_extension(const char *name);
// Fin d'image : statistiques des envois de textures (RCN_GL_STATS).
void rcn_sdlgl_frame_stats(void);
// Taille de cette image.
void rcn_sdlgl_size(u32 *width, u32 *height);
// Relais de la fonction OpenGL `name` pour le jeu, ou 0 si elle n'existe pas.
u64 rcn_sdlgl_proc(RcnCtx *ctx, const char *name);
// Fin d'image : copie mise à l'échelle dans le framebuffer 0 de l'hôte (out_width × out_height, bandes
// noires autour), état du jeu rétabli ensuite.
void rcn_sdlgl_blit(int out_width, int out_height);
// Fin d'image : relit l'image (BVRX, de haut en bas, width * 4 octets par ligne) dans `pixels`.
void rcn_sdlgl_read(u8 *pixels);

#endif
