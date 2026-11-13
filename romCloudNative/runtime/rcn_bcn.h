// RomCloud Native : décompression des textures BC1 à BC5 (rcn_bcn.c).
#ifndef RCN_BCN_H
#define RCN_BCN_H

#include "rcn_internal.h"

// Genre d'un format DXGI : 1 BC1, 2 BC2, 3 BC3, 4 BC4, 5 BC5 (négatif : BC4 / BC5 signés), 0 autre.
int rcn_bcn_kind(u32 dxgi);
// Décompresse blocks_w × blocks_h blocs (lignes de blocs de src_pitch octets) en pixels RGBA de 8 bits
// (dst_pitch octets par ligne de pixels ; BC4 / BC5 signés : octets signés).
void rcn_bcn_decode(u32 dxgi, const u8 *src, u32 src_pitch, u32 blocks_w, u32 blocks_h, u8 *dst, u32 dst_pitch);

#endif
