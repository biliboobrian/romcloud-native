// RomCloud Native : déroulement de la pile d'un jeu 64 bits, comme le fait Windows (RtlVirtualUnwind,
// RtlLookupFunctionEntry, RtlCaptureContext, RtlPcToFileHeader). Les programmes .NET compilés d'avance
// s'en servent pour parcourir leurs piles (ramasse-miettes, exceptions).
//
// Le code traduit reproduit exactement la pile du jeu (adresses de retour, registres sauvegardés aux
// mêmes places) : les informations de déroulement de l'exécutable (.pdata, UNWIND_INFO) s'y appliquent
// telles quelles. Les « registres » sont ceux d'une structure CONTEXT du jeu (x64).
#include <stdlib.h>

#include "rcn_internal.h"

// Champs de CONTEXT (x64).
enum {
    CTX_FLAGS = 0x30,
    CTX_MXCSR = 0x34,
    CTX_EFLAGS = 0x44,
    CTX_RAX = 0x78, // puis rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8 … r15, de 8 en 8 (même ordre que ctx->r)
    CTX_RSP = 0x98,
    CTX_RIP = 0xF8,
    CTX_XMM0 = 0x1A0,
    CTX_SIZE = 0x4D0,
    CONTEXT_FULL = 0x10000B,
};
enum {
    UWOP_PUSH_NONVOL = 0,
    UWOP_ALLOC_LARGE = 1,
    UWOP_ALLOC_SMALL = 2,
    UWOP_SET_FPREG = 3,
    UWOP_SAVE_NONVOL = 4,
    UWOP_SAVE_NONVOL_FAR = 5,
    UWOP_EPILOG = 6,
    UWOP_SAVE_XMM128 = 8,
    UWOP_SAVE_XMM128_FAR = 9,
    UWOP_PUSH_MACHFRAME = 10,
    UNW_FLAG_EHANDLER = 1,
    UNW_FLAG_UHANDLER = 2,
    UNW_FLAG_CHAININFO = 4,
};

#define REG(c, i) ((c) + CTX_RAX + 8 * (u64)(i))

// KNONVOLATILE_CONTEXT_POINTERS : adresse où chaque registre a été lu dans la pile (16 pointeurs xmm, puis
// 16 pointeurs d'entiers). Le ramasse-miettes s'en sert pour corriger les références qu'il déplace.
static void note_int(u64 ptrs, u32 reg, u64 at) {
    if (ptrs) wr64(ptrs + 128 + 8 * (u64)reg, at);
}
static void note_xmm(u64 ptrs, u32 reg, u64 at) {
    if (ptrs) wr64(ptrs + 8 * (u64)reg, at);
}

// Table .pdata de l'exécutable chargé : (adresse, nombre d'entrées de 12 octets).
static int pdata(RcnCtx *ctx, u64 *table, u32 *count) {
    u64 base = ctx->base;
    u32 nt = rd32(base + 0x3C);
    u32 dir = rd32(base + nt + 24 + 112 + 3 * 8), size = rd32(base + nt + 24 + 112 + 3 * 8 + 4);
    if (!dir || !size) return 0;
    *table = base + dir;
    *count = size / 12;
    return 1;
}

// Entrée de .pdata qui couvre `pc`, ou 0.
static u64 lookup(RcnCtx *ctx, u64 pc) {
    u64 table;
    u32 count;
    if (pc - ctx->base >= ctx->image_size || !pdata(ctx, &table, &count)) return 0;
    u32 rva = (u32)(pc - ctx->base), lo = 0, hi = count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        u64 e = table + 12 * (u64)mid;
        if (rva < rd32(e)) hi = mid;
        else if (rva >= rd32(e + 4)) lo = mid + 1;
        else return e;
    }
    return 0;
}

// RtlLookupFunctionEntry(pc, &image_base, history)
static void k32_RtlLookupFunctionEntry(RcnCtx *ctx) {
    u64 entry = lookup(ctx, A0);
    if (A1) wr64(A1, entry ? ctx->base : 0);
    RET(entry);
}
// RtlPcToFileHeader(pc, &base) : l'exécutable, si pc en fait partie.
static void k32_RtlPcToFileHeader(RcnCtx *ctx) {
    u64 base = A0 - ctx->base < ctx->image_size ? ctx->base : 0;
    wr64(A1, base);
    RET(base);
}
// RtlCaptureContext(&context) : état de l'appelant au retour de cet appel.
static void k32_RtlCaptureContext(RcnCtx *ctx) {
    u64 c = A0;
    memset(PTR(c), 0, CTX_SIZE);
    wr32(c + CTX_FLAGS, CONTEXT_FULL);
    wr32(c + CTX_MXCSR, ctx->mxcsr);
    wr32(c + CTX_EFLAGS, rcn_get_flags(ctx));
    for (int i = 0; i < 16; i++) wr64(REG(c, i), ctx->r[i]);
    wr64(c + CTX_RSP, ctx->r[4] + 8); // l'adresse de retour dépilée
    wr64(c + CTX_RIP, rd64(ctx->r[4]));
    for (int i = 0; i < 16; i++) memcpy(PTR(c + CTX_XMM0 + 16 * (u64)i), &ctx->xmm[i], 16);
}

// Épilogue (« add rsp, n » ou « lea rsp, [...] », « pop » de registres, « ret ») à l'adresse pc ?
// Si oui, l'applique au contexte (comme si l'épilogue s'était exécuté) et renvoie 1.
static int unwind_epilog(u64 c, u64 ptrs, u64 pc, u32 frame_reg, u32 frame_offset) {
    u64 p = pc, rsp = rd64(c + CTX_RSP);
    u8 b0 = rd8(p), b1 = rd8(p + 1), b2 = rd8(p + 2);
    // add rsp, imm8 / imm32
    if (b0 == 0x48 && b1 == 0x83 && b2 == 0xC4) {
        rsp += rd8(p + 3);
        p += 4;
    } else if (b0 == 0x48 && b1 == 0x81 && b2 == 0xC4) {
        rsp += rd32(p + 3);
        p += 7;
    } else if ((b0 & 0xFE) == 0x48 && b1 == 0x8D && frame_reg) {
        // lea rsp, [frame_reg + disp]
        u8 modrm = b2;
        if ((modrm >> 3 & 7) != 4) return 0;
        u64 base = rd64(REG(c, frame_reg));
        if ((modrm >> 6) == 1) {
            rsp = base + (i8)rd8(p + 3);
            p += 4;
        } else if ((modrm >> 6) == 2) {
            rsp = base + (i32)rd32(p + 3);
            p += 7;
        } else {
            return 0;
        }
        (void)frame_offset;
    }
    // Suite de pop puis ret : sinon ce n'est pas un épilogue.
    u64 q = p;
    for (;;) {
        u8 x = rd8(q);
        if (x >= 0x58 && x <= 0x5F) q++;
        else if (x == 0x41 && rd8(q + 1) >= 0x58 && rd8(q + 1) <= 0x5F) q += 2;
        else break;
    }
    if (rd8(q) != 0xC3) return 0;
    for (;;) {
        u8 x = rd8(p);
        if (x >= 0x58 && x <= 0x5F) {
            wr64(REG(c, x - 0x58), rd64(rsp));
            note_int(ptrs, x - 0x58, rsp);
            rsp += 8;
            p++;
        } else if (x == 0x41) {
            wr64(REG(c, 8 + rd8(p + 1) - 0x58), rd64(rsp));
            note_int(ptrs, 8 + rd8(p + 1) - 0x58, rsp);
            rsp += 8;
            p += 2;
        } else {
            break;
        }
    }
    wr64(c + CTX_RIP, rd64(rsp));
    wr64(c + CTX_RSP, rsp + 8);
    return 1;
}

// RtlVirtualUnwind(type, image_base, pc, &function, &context, &handler_data, &establisher, &pointers)
static void k32_RtlVirtualUnwind(RcnCtx *ctx) {
    u32 type = (u32)A0;
    u64 image = A1, pc = A2, function = A3, c = STK(4), handler_data = STK(5), establisher = STK(6), ptrs = STK(7);
    u64 handler = 0;
    u64 frame = 0;
    int first = 1;
    while (function) {
        u32 begin = rd32(function), info_rva = rd32(function + 8);
        u64 info = image + info_rva;
        u8 flags = rd8(info) >> 3, prolog = rd8(info + 1), count = rd8(info + 2);
        u32 frame_reg = rd8(info + 3) & 0x0F, frame_offset = rd8(info + 3) >> 4;
        u64 offset = pc - (image + begin);
        u64 codes = info + 4;
        if (first) {
            frame = rd64(c + CTX_RSP);
            if (frame_reg) frame = rd64(REG(c, frame_reg)) - 16 * (u64)frame_offset;
            wr64(establisher, frame);
            // Au milieu d'un épilogue : on le termine, l'appelant est atteint.
            if (offset >= prolog && unwind_epilog(c, ptrs, pc, frame_reg, frame_offset)) {
                RET(0);
                return;
            }
        }
        u64 prolog_offset = offset < prolog ? offset : ~0ull;
        if (!first) prolog_offset = ~0ull;
        for (u32 i = 0; i < count; i++) {
            u16 code = rd16(codes + 2 * (u64)i);
            u8 code_offset = (u8)code, op = (code >> 8) & 0x0F, op_info = (u8)(code >> 12);
            u32 slots = op == UWOP_ALLOC_LARGE ? (op_info ? 2 : 1) : op == UWOP_SAVE_NONVOL || op == UWOP_SAVE_XMM128 || op == UWOP_EPILOG ? 1
                        : op == UWOP_SAVE_NONVOL_FAR || op == UWOP_SAVE_XMM128_FAR ? 2 : 0;
            // Instruction du prologue pas encore exécutée : rien à défaire.
            if (code_offset > prolog_offset) {
                i += slots;
                continue;
            }
            u64 rsp = rd64(c + CTX_RSP);
            switch (op) {
            case UWOP_PUSH_NONVOL:
                wr64(REG(c, op_info), rd64(rsp));
                note_int(ptrs, op_info, rsp);
                wr64(c + CTX_RSP, rsp + 8);
                break;
            case UWOP_ALLOC_LARGE:
                wr64(c + CTX_RSP, rsp + (op_info ? rd32(codes + 2 * (u64)(i + 1)) : (u64)rd16(codes + 2 * (u64)(i + 1)) * 8));
                break;
            case UWOP_ALLOC_SMALL:
                wr64(c + CTX_RSP, rsp + op_info * 8u + 8);
                break;
            case UWOP_SET_FPREG:
                wr64(c + CTX_RSP, rd64(REG(c, frame_reg)) - 16 * (u64)frame_offset);
                break;
            case UWOP_SAVE_NONVOL:
            case UWOP_SAVE_NONVOL_FAR: {
                u64 at = frame + (op == UWOP_SAVE_NONVOL ? (u64)rd16(codes + 2 * (u64)(i + 1)) * 8 : rd32(codes + 2 * (u64)(i + 1)));
                wr64(REG(c, op_info), rd64(at));
                note_int(ptrs, op_info, at);
                break;
            }
            case UWOP_SAVE_XMM128:
            case UWOP_SAVE_XMM128_FAR: {
                u64 at = frame + (op == UWOP_SAVE_XMM128 ? (u64)rd16(codes + 2 * (u64)(i + 1)) * 16 : rd32(codes + 2 * (u64)(i + 1)));
                memcpy(PTR(c + CTX_XMM0 + 16 * (u64)op_info), PTR(at), 16);
                note_xmm(ptrs, op_info, at);
                break;
            }
            case UWOP_PUSH_MACHFRAME: {
                // Cadre d'interruption : rip, cs, eflags, rsp, ss (précédés d'un code d'erreur si op_info).
                u64 at = rsp + (op_info ? 8 : 0);
                wr64(c + CTX_RIP, rd64(at));
                wr64(c + CTX_RSP, rd64(at + 24));
                RET(0);
                return;
            }
            default:
                break;
            }
            i += slots;
        }
        if (first && (flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)) && (flags & type)) {
            u64 after = codes + 2 * (u64)((count + 1) & ~1u);
            handler = image + rd32(after);
            if (handler_data) wr64(handler_data, after + 4);
        }
        first = 0;
        // Informations chaînées : le reste du prologue est décrit par une autre entrée.
        function = (flags & UNW_FLAG_CHAININFO) ? codes + 2 * (u64)((count + 1) & ~1u) : 0;
    }
    // Adresse de retour : l'appelant.
    u64 rsp = rd64(c + CTX_RSP);
    wr64(c + CTX_RIP, rd64(rsp));
    wr64(c + CTX_RSP, rsp + 8);
    RET(handler);
}

const RcnShim rcn_k32_unwind[] = {
    {"RtlCaptureContext", k32_RtlCaptureContext, 0},
    {"RtlLookupFunctionEntry", k32_RtlLookupFunctionEntry, 0},
    {"RtlPcToFileHeader", k32_RtlPcToFileHeader, 0},
    {"RtlVirtualUnwind", k32_RtlVirtualUnwind, 0},
};
const size_t rcn_k32_unwind_count = sizeof rcn_k32_unwind / sizeof rcn_k32_unwind[0];
