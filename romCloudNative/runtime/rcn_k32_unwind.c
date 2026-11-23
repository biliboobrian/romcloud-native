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

// Module (exécutable ou DLL) qui contient pc : adresse et taille. 0 s'il n'y en a pas.
static int module_of(RcnCtx *ctx, u64 pc, u64 *base, u32 *size) {
    RcnLoaded *l = rcn_loaded_at(ctx->proc, pc);
    if (!l) return 0;
    *base = (u64)(uintptr_t)l->image;
    *size = l->size;
    return 1;
}

// Table .pdata d'un module chargé : (adresse, nombre d'entrées de 12 octets).
static int pdata(u64 base, u64 *table, u32 *count) {
    u32 nt = rd32(base + 0x3C);
    u32 dir = rd32(base + nt + 24 + 112 + 3 * 8), size = rd32(base + nt + 24 + 112 + 3 * 8 + 4);
    if (!dir || !size) return 0;
    *table = base + dir;
    *count = size / 12;
    return 1;
}

// Entrée de .pdata qui couvre `pc`, ou 0.
static u64 lookup(RcnCtx *ctx, u64 pc, u64 *module) {
    u64 table, base;
    u32 count, size;
    if (!module_of(ctx, pc, &base, &size) || !pdata(base, &table, &count)) return 0;
    *module = base;
    u32 rva = (u32)(pc - base), lo = 0, hi = count;
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
    u64 base = 0;
    u64 entry = lookup(ctx, A0, &base);
    if (A1) wr64(A1, entry ? base : 0);
    RET(entry);
}
// RtlPcToFileHeader(pc, &base) : l'exécutable, si pc en fait partie.
static void k32_RtlPcToFileHeader(RcnCtx *ctx) {
    u64 base = 0;
    u32 size;
    if (!module_of(ctx, A0, &base, &size)) base = 0;
    wr64(A1, base);
    RET(base);
}
// RtlCaptureContext(&context) : état de l'appelant au retour de cet appel.
static void k32_RtlCaptureContext(RcnCtx *ctx) {
    u64 c = A0;
    if (ctx->bits32) {
        // CONTEXT de x86 : CONTEXT_FULL (0x10007), registres entiers, eip = adresse de retour.
        static const u32 at[8] = {0xB0, 0xAC, 0xA8, 0xA4, 0xC4, 0xB4, 0xA0, 0x9C}; // eax ecx edx ebx esp ebp esi edi
        memset(PTR(c), 0, 0x2CC);
        wr32(c, 0x10007);
        for (int i = 0; i < 8; i++) wr32(c + at[i], (u32)ctx->r[i]);
        wr32(c + 0xC4, (u32)ctx->r[4] + 8); // adresse de retour et argument dépilés (stdcall)
        wr32(c + 0xB8, rd32(ctx->r[4]));
        wr32(c + 0xC0, (u32)rcn_get_flags(ctx));
        return;
    }
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
// Déroule un cadre : c (CONTEXT du jeu) devient celui de l'appelant. Rend la fonction de gestion du
// cadre (type UNW_FLAG_EHANDLER ou UHANDLER), ou 0.
static u64 virtual_unwind(u32 type, u64 image, u64 pc, u64 function, u64 c, u64 handler_data, u64 establisher, u64 ptrs) {
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
                return 0;
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
                return 0;
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
    return handler;
}
static void k32_RtlVirtualUnwind(RcnCtx *ctx) {
    RET(virtual_unwind((u32)A0, A1, A2, A3, STK(4), STK(5), STK(6), STK(7)));
}

// --- Exceptions (RaiseException, RtlUnwindEx, RtlRestoreContext) ---
// Comme Windows x64 : première passe, cadre par cadre (.pdata), jusqu'à un gestionnaire du jeu qui prend
// l'exception ; il appelle RtlUnwindEx, seconde passe qui appelle les gestionnaires de fin puis reprend
// dans le cadre visé (reprise non locale du code traduit : ctx->unwind = RCN_UNWIND_RESUME).

enum {
    EXCEPTION_NONCONTINUABLE = 1,
    EXCEPTION_UNWINDING = 2,
    EXCEPTION_EXIT_UNWIND = 4,
    EXCEPTION_TARGET_UNWIND = 0x20,
    EXCEPTION_CONTINUE_EXECUTION = 0,
    EXCEPTION_CONTINUE_SEARCH = 1,
    RECORD_SIZE = 152,
    DISPATCHER_SIZE = 80,
};

// CONTEXT (aligné sur 16 octets) dans la mémoire de l'hôte, vue par le jeu.
static u64 context_new(void) {
    u8 *raw = calloc(1, CTX_SIZE + 16);
    if (!raw) return 0;
    return ((u64)(uintptr_t)raw + 15) & ~15ull;
}
static void context_free(u64 c) {
    // Le bloc d'origine précède l'adresse alignée de moins de 16 octets : conservé (petite fuite évitée
    // en ne libérant pas, ces contextes ne servent qu'au traitement d'une exception).
    (void)c;
}
static void capture_caller(RcnCtx *ctx, u64 c) {
    memset(PTR(c), 0, CTX_SIZE);
    wr32(c + CTX_FLAGS, CONTEXT_FULL);
    wr32(c + CTX_MXCSR, ctx->mxcsr);
    wr32(c + CTX_EFLAGS, rcn_get_flags(ctx));
    for (int i = 0; i < 16; i++) wr64(REG(c, i), ctx->r[i]);
    wr64(c + CTX_RSP, ctx->r[4] + 8);
    wr64(c + CTX_RIP, rd64(ctx->r[4]));
    for (int i = 0; i < 16; i++) memcpy(PTR(c + CTX_XMM0 + 16 * (u64)i), &ctx->xmm[i], 16);
}
// Le jeu reprend dans le contexte `c`, à son rip (registres non volatils et rsp compris).
static void resume_at(RcnCtx *ctx, u64 c) {
    for (int i = 0; i < 16; i++) ctx->r[i] = rd64(REG(c, i));
    for (int i = 0; i < 16; i++) memcpy(&ctx->xmm[i], PTR(c + CTX_XMM0 + 16 * (u64)i), 16);
    ctx->mxcsr = rd32(c + CTX_MXCSR);
    rcn_set_flags(ctx, rd32(c + CTX_EFLAGS));
    ctx->resume_rsp = rd64(c + CTX_RSP);
    ctx->resume_rip = rd64(c + CTX_RIP);
    ctx->unwind = RCN_UNWIND_RESUME;
}

// Blocs catch en cours (STATUS_UNWIND_CONSOLIDATE, par thread) : contexte du cadre visé de chacun. Une
// remontée qui sort du bloc catch par l'appel du runtime reprend de ce cadre, comme celle de Windows
// passe par RcConsolidateFrames (exception levée ou relancée dans un bloc catch).
enum { MAX_CATCH_DEPTH = 32 };
static _Thread_local u64 catch_frames[MAX_CATCH_DEPTH];
static _Thread_local int catch_depth;

// Cadre visé du bloc catch le plus proche au-dessus de rsp (pile descendante), ou 0.
static u64 enclosing_catch_frame(u64 rsp) {
    u64 best = 0;
    for (int i = 0; i < catch_depth; i++) {
        u64 frame_rsp = rd64(catch_frames[i] + CTX_RSP);
        if (frame_rsp > rsp && (!best || frame_rsp < rd64(best + CTX_RSP))) best = catch_frames[i];
    }
    return best;
}

// Un cadre plus haut : par .pdata, ou cadre feuille (adresse de retour au sommet). 0 hors du jeu.
static int step(RcnCtx *ctx, u32 type, u64 c, u64 *handler, u64 *handler_data, u64 *establisher, u64 *function, u64 *image) {
    u64 pc = rd64(c + CTX_RIP);
    *handler = 0;
    *function = lookup(ctx, pc, image);
    if (!*function) {
        if (!rcn_loaded_at(ctx->proc, pc)) {
            // Cadre intermédiaire (l'appel du runtime) : son établisseur est sa propre pile, pas celle du
            // cadre visé, que l'étape suivante (la fonction parente elle-même) trouvera.
            u64 rsp = rd64(c + CTX_RSP), frame = enclosing_catch_frame(rsp);
            if (!frame) return 0;
            memcpy(PTR(c), PTR(frame), CTX_SIZE);
            *establisher = rsp;
            return 1;
        }
        u64 rsp = rd64(c + CTX_RSP);
        *establisher = rsp;
        wr64(c + CTX_RIP, rd64(rsp));
        wr64(c + CTX_RSP, rsp + 8);
        return 1;
    }
    *handler = virtual_unwind(type, *image, pc, *function, c, (u64)(uintptr_t)handler_data, (u64)(uintptr_t)establisher, 0);
    return 1;
}

static void fill_dispatcher(u64 dc, u64 pc, u64 image, u64 function, u64 establisher, u64 target, u64 c, u64 handler, u64 data) {
    memset(PTR(dc), 0, DISPATCHER_SIZE);
    wr64(dc, pc);
    wr64(dc + 8, image);
    wr64(dc + 16, function);
    wr64(dc + 24, establisher);
    wr64(dc + 32, target);
    wr64(dc + 40, c);
    wr64(dc + 48, handler);
    wr64(dc + 56, data);
}

// Contexte du point où l'exception a été levée, pendant sa première passe (par thread) : RtlUnwindEx,
// appelé par un gestionnaire, repart de là.
static _Thread_local u64 raise_context;
// Cadre dont RtlUnwindEx appelle le gestionnaire : un RtlUnwindEx imbriqué (gestionnaire qui transfère
// lui-même le contrôle, comme la personnalité C++ de GCC / LLVM) repart de lui.
static _Thread_local u64 unwind_frame;

static void dispatch(RcnCtx *ctx, u64 record, u64 original) {
    u64 c = context_new(), dc = (u64)(uintptr_t)calloc(1, DISPATCHER_SIZE);
    if (!c || !dc) rcn_fail(ctx, "memoire insuffisante (exception)");
    memcpy(PTR(c), PTR(original), CTX_SIZE);
    u64 snapshot = context_new();
    if (snapshot) memcpy(PTR(snapshot), PTR(original), CTX_SIZE);
    u64 saved = raise_context;
    raise_context = original;
    for (int depth = 0; depth < 4096; depth++) {
        u64 pc = rd64(c + CTX_RIP), handler, data = 0, establisher = 0, function, image = 0;
        if (!step(ctx, UNW_FLAG_EHANDLER, c, &handler, &data, &establisher, &function, &image)) break;
        if (!handler) continue;
        fill_dispatcher(dc, pc, image, function, establisher, 0, c, handler, data);
        u64 args[4] = {record, establisher, original, dc};
        u64 r = rcn_call_guest_n(ctx, handler, 4, args);
        if (ctx->unwind) { // RtlUnwindEx : reprise ailleurs
            raise_context = saved;
            return;
        }
        if ((u32)r == EXCEPTION_CONTINUE_EXECUTION && !(rd32(record + 4) & EXCEPTION_NONCONTINUABLE)) {
            raise_context = saved;
            // Contexte intact (nom de thread de MSVC, 0x406D1388…) : simple retour de RaiseException, sans
            // reprise à son adresse de retour (qui n'est pas un point d'entrée traduit).
            int changed = snapshot ? memcmp(PTR(snapshot), PTR(original), CTX_SIZE) != 0 : 1;
            if (snapshot) context_free(snapshot);
            if (changed) resume_at(ctx, original);
            return;
        }
    }
    raise_context = saved;
    // Pile déroulée du point de levée (qui a levé l'exception), pour le journal.
    if (snapshot) {
        fprintf(stderr, "rcn : [%u] exception 0x%08x non geree, pile :", THREAD_ID(ctx), rd32(record));
        for (int depth = 0; depth < 40; depth++) {
            u64 pc = rd64(snapshot + CTX_RIP), handler, data = 0, establisher = 0, function, image = 0;
            RcnLoaded *l = rcn_loaded_at(ctx->proc, pc);
            fprintf(stderr, " %s+%llx", l ? l->name : "?", (unsigned long long)(pc - (l ? (u64)(uintptr_t)l->image : 0)));
            if (!step(ctx, UNW_FLAG_EHANDLER, snapshot, &handler, &data, &establisher, &function, &image)) break;
        }
        fprintf(stderr, "\n");
    }
    rcn_fail(ctx, "exception 0x%08x non geree par le jeu (levee en %s+0x%llx)", rd32(record),
             rcn_loaded_at(ctx->proc, rd64(original + CTX_RIP)) ? rcn_loaded_at(ctx->proc, rd64(original + CTX_RIP))->name : "?",
             (unsigned long long)(rd64(original + CTX_RIP) -
                                  (rcn_loaded_at(ctx->proc, rd64(original + CTX_RIP)) ? (u64)(uintptr_t)rcn_loaded_at(ctx->proc, rd64(original + CTX_RIP))->image : 0)));
}

// RaiseException(code, flags, count, arguments)
void rcn_raise_exception(RcnCtx *ctx) {
    u64 record = (u64)(uintptr_t)calloc(1, RECORD_SIZE), c = context_new();
    if (!record || !c) rcn_fail(ctx, "memoire insuffisante (exception)");
    u32 count = (u32)A2 > 15 ? 15 : (u32)A2;
    wr32(record, (u32)A0);
    wr32(record + 4, (u32)A1 & EXCEPTION_NONCONTINUABLE);
    capture_caller(ctx, c);
    wr64(record + 16, rd64(c + CTX_RIP));
    wr32(record + 24, count);
    for (u32 i = 0; i < count && A3; i++) wr64(record + 32 + 8 * (u64)i, rd64(A3 + 8 * (u64)i));
    if (ctx->proc->trace) {
        fprintf(stderr, " [exception %08x]", (u32)A0);
        // Exception C++ de Visual C++ : type de l'objet (ThrowInfo → CatchableTypeArray → TypeDescriptor).
        if ((u32)A0 == 0xE06D7363u && count >= 4 && A3) {
            u64 info = rd64(A3 + 16), image = rd64(A3 + 24);
            if (!info) fprintf(stderr, " [relance]");
            else if (image) {
                u64 array = image + rd32(info + 12), type = image + rd32(image + rd32(array + 4) + 4);
                fprintf(stderr, " [C++ %s]", STR(type + 16));
            }
        }
    }
    dispatch(ctx, record, c);
}
// RtlRaiseException(&record)
static void k32_RtlRaiseException(RcnCtx *ctx) {
    u64 c = context_new();
    if (!c) rcn_fail(ctx, "memoire insuffisante (exception)");
    capture_caller(ctx, c);
    wr64(A0 + 16, rd64(c + CTX_RIP));
    dispatch(ctx, A0, c);
}

// Exception STATUS_UNWIND_CONSOLIDATE (Visual C++, __CxxFrameHandler3/4) : une fois le cadre visé
// atteint, Windows appelle la fonction ExceptionInformation[0] (__CxxCallCatchBlock), qui exécute le
// bloc catch et rend l'adresse où reprendre. 1 si fait (rip du contexte remplacé).
#define STATUS_UNWIND_CONSOLIDATE 0x80000029u
static int consolidate(RcnCtx *ctx, u64 c, u64 record) {
    if (!record || rd32(record) != STATUS_UNWIND_CONSOLIDATE || rd32(record + 24) < 1) return 0;
    u64 callback = rd64(record + 32);
    u64 args[1] = {record};
    u64 frame = context_new();
    int pushed = frame && catch_depth < MAX_CATCH_DEPTH;
    if (pushed) {
        memcpy(PTR(frame), PTR(c), CTX_SIZE);
        catch_frames[catch_depth++] = frame;
    }
    u64 rip = rcn_call_guest_n(ctx, callback, 1, args);
    if (pushed) catch_depth--;
    if (frame) context_free(frame);
    if (ctx->unwind) return -1; // le bloc catch a lui-même transféré le contrôle
    wr64(c + CTX_RIP, rip);
    return 1;
}

// RtlUnwindEx(target_frame, target_ip, &record, return_value, &context, history)
static void k32_RtlUnwindEx(RcnCtx *ctx) {
    u64 target_frame = A0, target_ip = A1, record = A2, value = A3;
    // longjmp de la bibliothèque C liée dans l'exécutable : STATUS_LONGJUMP, jmp_buf en premier paramètre.
    // Son setjmp a été traduit en point de reprise de l'hôte (recompilateur) : le longjmp du runtime y revient.
    if (record && rd32(record) == 0x80000026u && rd32(record + 0x18) >= 1) {
        u64 buf = rd64(record + 0x20);
        if (ctx->proc->module->native) rcn_longjmp_native(ctx, buf, (int)value);
        else rcn_longjmp(ctx, buf, (int)value);
        return;
    }
    u64 c = context_new(), previous = context_new(), dc = (u64)(uintptr_t)calloc(1, DISPATCHER_SIZE);
    if (!c || !previous || !dc) rcn_fail(ctx, "memoire insuffisante (exception)");
    if (!record) {
        record = (u64)(uintptr_t)calloc(1, RECORD_SIZE);
        wr32(record, 0xC0000027u); // STATUS_UNWIND
    }
    // Pendant une exception, le déroulement part du point où elle a été levée (les cadres des
    // gestionnaires sont sous lui) ; sinon de l'appelant.
    if (unwind_frame) memcpy(PTR(c), PTR(unwind_frame), CTX_SIZE);
    else if (raise_context) memcpy(PTR(c), PTR(raise_context), CTX_SIZE);
    else capture_caller(ctx, c);
    u32 flags = rd32(record + 4) | EXCEPTION_UNWINDING | (target_frame ? 0 : EXCEPTION_EXIT_UNWIND);
    for (int depth = 0; depth < 4096; depth++) {
        memcpy(PTR(previous), PTR(c), CTX_SIZE);
        u64 pc = rd64(c + CTX_RIP), handler, data = 0, establisher = 0, function, image = 0;
        if (!step(ctx, UNW_FLAG_UHANDLER, c, &handler, &data, &establisher, &function, &image)) {
            rcn_fail(ctx, "RtlUnwindEx : cadre vise introuvable (0x%llx)", (unsigned long long)target_frame);
        }
        int target = establisher == target_frame;
        if (ctx->proc->trace) fprintf(stderr, "\n  [deroulement] pc %llx etablisseur %llx gestionnaire %llx", (unsigned long long)pc, (unsigned long long)establisher, (unsigned long long)handler);
        if (target) flags |= EXCEPTION_TARGET_UNWIND;
        if (handler) {
            wr32(record + 4, flags);
            // Comme Windows : le gestionnaire voit (et peut modifier) le contexte du cadre avant déroulement,
            // celui de la reprise finale (la personnalité C++ y range le sélecteur du catch).
            fill_dispatcher(dc, pc, image, function, establisher, target_ip, previous, handler, data);
            u64 args[4] = {record, establisher, previous, dc};
            u64 outer = unwind_frame;
            unwind_frame = previous;
            rcn_call_guest_n(ctx, handler, 4, args);
            unwind_frame = outer;
            if (ctx->unwind) return;
        }
        if (target) {
            // Reprise dans le cadre visé : son contexte avant déroulement, à target_ip, rax = valeur rendue.
            wr64(REG(previous, 0), value);
            wr64(previous + CTX_RIP, target_ip);
            if (consolidate(ctx, previous, record) < 0) return;
            resume_at(ctx, previous);
            return;
        }
    }
    rcn_fail(ctx, "RtlUnwindEx : pile trop profonde");
}

// --- Exceptions de Visual C++ pour les jeux 64 bits (vcruntime140, msvcrt) ---
// Fonctions que le jeu importe quand il ne fournit pas sa propre vcruntime140.dll (GalaxyNative64 de
// Carrion, xaudio2_7 de Skyrim) : SEH de C (__try/__except/__finally), lancement d'exceptions C++.

// __C_specific_handler(record, cadre, contexte, dispatcher) : gestionnaire des __try de C. HandlerData
// (dispatcher + 56) désigne la table des portées : nombre, puis {début, fin, filtre ou __finally, cible
// du __except} (adresses relatives à l'image) ; ScopeIndex (dispatcher + 72) : portées déjà traitées.
static void k32_C_specific_handler(RcnCtx *ctx) {
    u64 record = A0, frame = A1, context = A2, dc = A3;
    u64 base = rd64(dc + 8), pc = rd64(dc) - base, table = rd64(dc + 56);
    u32 count = rd32(table), flags = rd32(record + 4);
    for (u32 i = rd32(dc + 72); i < count; i++) {
        u64 scope = table + 4 + 16 * (u64)i;
        u32 begin = rd32(scope), end = rd32(scope + 4), handler = rd32(scope + 8), target = rd32(scope + 12);
        if (pc < begin || pc >= end) continue;
        if (flags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) {
            // Déroulement : les __finally des portées traversées, jusqu'au __except visé.
            if (target) {
                if ((flags & EXCEPTION_TARGET_UNWIND) && rd64(dc + 32) == base + target) break;
                continue;
            }
            wr32(dc + 72, i + 1);
            u64 args[2] = {1, frame}; // terminaison anormale
            rcn_call_guest_n(ctx, base + handler, 2, args);
            if (ctx->unwind) return;
            continue;
        }
        if (!target) continue; // __finally : rien pendant la recherche
        i32 decision = 1;      // EXCEPTION_EXECUTE_HANDLER
        if (handler != 1) {
            u64 *pointers = calloc(2, 8);
            if (!pointers) rcn_fail(ctx, "memoire insuffisante (exception)");
            pointers[0] = record, pointers[1] = context;
            u64 args[2] = {(u64)(uintptr_t)pointers, frame};
            decision = (i32)rcn_call_guest_n(ctx, base + handler, 2, args);
            free(pointers);
            if (ctx->unwind) return;
        }
        if (decision < 0) {
            RET(EXCEPTION_CONTINUE_EXECUTION);
            return;
        }
        if (decision == 0) continue;
        // __except choisi : RtlUnwindEx(cadre, cible, record, code) — ne revient qu'avec ctx->unwind posé.
        ctx->r[1] = frame, ctx->r[2] = base + target, ctx->r[8] = record, ctx->r[9] = rd32(record);
        k32_RtlUnwindEx(ctx);
        return;
    }
    RET(1); // ExceptionContinueSearch
}

// --- Exceptions C++ de Visual C++ en 64 bits : __CxxFrameHandler3 et __CxxFrameHandler4 ---
//
// Le gestionnaire de chaque cadre lit les tables du compilateur (FuncInfo : format 3, ou format 4
// compressé, ramenés tous deux à la même description), trouve l'état du cadre d'après son adresse, et :
// - première passe : cherche un catch du type lancé dans les try qui couvrent cet état. Trouvé : l'objet
//   est copié dans le cadre du catch, puis RtlUnwindEx déroule jusqu'à ce cadre avec une exception
//   STATUS_UNWIND_CONSOLIDATE dont la fonction (eh_call_catch) exécute le bloc catch et rend l'adresse
//   de reprise, comme __CxxCallCatchBlock de Windows ;
// - déroulement : détruit les objets du cadre (table de remontée) jusqu'à l'état visé : -1, ou le début
//   du try pour le cadre du catch.
// Les blocs (catch, destructeurs) sont des « funclets » appelés avec le cadre de leur fonction dans rdx.

enum { EH_MSC = 0xE06D7363u, EH_MAGIC1 = 0x19930520u, EH_MAGIC3 = 0x19930522u };
typedef struct {
    i32 next;    // état suivant
    u8 kind;     // 0 rien, 1 destructeur de l'objet du cadre, 2 destructeur de l'objet pointé, 3 funclet
    u32 action;  // adresse relative à l'image
    u32 object;  // décalage de l'objet dans le cadre
} EhUnwind;
typedef struct {
    u32 adjectives, type;  // type : TypeDescriptor (relatif à l'image), 0 pour catch(...)
    i32 catch_obj;         // place de l'objet dans le cadre (0 : aucune)
    u32 handler;           // funclet du bloc catch
    u32 disp_frame;        // format 3 : place du cadre de la fonction dans celui du funclet
    u32 cont[2];           // format 4 : adresses de reprise (relatives à l'image ou à la fonction)
    int cont_count, cont_rva;
} EhHandler;
typedef struct {
    i32 low, high;
    u32 count;
    EhHandler *handlers;
} EhTry;
typedef struct {
    u64 image, function_start; // fonction (ou funclet) du cadre
    u32 unwind_count, try_count;
    EhUnwind *unwind;
    EhTry *tries;
    i32 state;                 // état du cadre à son adresse courante
    int fh4, catch_funclet;    // format 4 ; FuncInfo d'un funclet catch (cadre de la fonction à disp_frame)
    u32 disp_frame;
} EhInfo;

static void eh_info_free(EhInfo *e) {
    for (u32 i = 0; i < e->try_count; i++) free(e->tries[i].handlers);
    free(e->tries);
    free(e->unwind);
}

// Format 3 : FuncInfo { magie, nombre d'états, table de remontée, nombre de try, table des try, nombre
// d'entrées IP → état, table IP → état, … } (adresses relatives à l'image).
static int eh_read3(u64 image, u64 fi, u64 pc, EhInfo *e) {
    u32 magic = rd32(fi) & 0x1FFFFFFFu;
    if (magic < EH_MAGIC1 || magic > EH_MAGIC3) return 0;
    e->unwind_count = rd32(fi + 4);
    e->unwind = calloc(e->unwind_count + 1, sizeof *e->unwind);
    for (u32 i = 0; i < e->unwind_count; i++) {
        u64 u = image + rd32(fi + 8) + 8 * (u64)i;
        e->unwind[i].next = (i32)rd32(u);
        e->unwind[i].action = rd32(u + 4);
        e->unwind[i].kind = e->unwind[i].action ? 3 : 0;
    }
    e->try_count = rd32(fi + 12);
    e->tries = calloc(e->try_count + 1, sizeof *e->tries);
    for (u32 t = 0; t < e->try_count; t++) {
        u64 m = image + rd32(fi + 16) + 20 * (u64)t;
        EhTry *tr = &e->tries[t];
        tr->low = (i32)rd32(m), tr->high = (i32)rd32(m + 4), tr->count = rd32(m + 12);
        tr->handlers = calloc(tr->count + 1, sizeof *tr->handlers);
        for (u32 h = 0; h < tr->count; h++) {
            u64 ht = image + rd32(m + 16) + 20 * (u64)h;
            EhHandler *x = &tr->handlers[h];
            x->adjectives = rd32(ht), x->type = rd32(ht + 4), x->catch_obj = (i32)rd32(ht + 8);
            x->handler = rd32(ht + 12), x->disp_frame = rd32(ht + 16);
        }
    }
    // État : dernière entrée dont l'adresse ne dépasse pas pc.
    e->state = -1;
    u32 n = rd32(fi + 20);
    u64 map = image + rd32(fi + 24);
    for (u32 i = 0; i < n; i++) {
        if (image + rd32(map + 8 * (u64)i) > pc) break;
        e->state = (i32)rd32(map + 8 * (u64)i + 4);
    }
    return 1;
}

// Format 4 : entiers compressés (1 à 5 octets, longueur dans les bits bas du premier).
static u32 fh4_unsigned(u64 *p) {
    static const u8 len[16] = {1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 5};
    static const u8 shift[16] = {25, 18, 25, 11, 25, 18, 25, 4, 25, 18, 25, 11, 25, 18, 25, 0};
    u32 bits = rd8(*p) & 0x0F, n = len[bits];
    u32 v = rd32(*p + n - 4) >> shift[bits];
    *p += n;
    return v;
}
static u32 fh4_int(u64 *p) {
    u32 v = rd32(*p);
    *p += 4;
    return v;
}
// FuncInfo4 : en-tête (catch, séparé, BBT, remontée, try), puis les tables présentes.
static int eh_read4(u64 image, u64 fi, u64 function_start, u64 pc, EhInfo *e) {
    u64 p = fi;
    u8 header = rd8(p++);
    u32 unwind_rva = 0, try_rva = 0, ip_rva = 0;
    if (header & 0x04) fh4_unsigned(&p); // bbtFlags
    if (header & 0x08) unwind_rva = fh4_int(&p);
    if (header & 0x10) try_rva = fh4_int(&p);
    if (header & 0x02) { // segments séparés : la table du segment de cette fonction
        u64 seg = image + fh4_int(&p);
        u32 n = fh4_unsigned(&seg);
        for (u32 i = 0; i < n; i++) {
            u32 start = fh4_int(&seg), map = fh4_int(&seg);
            if (image + start == function_start) {
                ip_rva = map;
                break;
            }
        }
    } else {
        ip_rva = fh4_int(&p);
    }
    if (header & 0x01) {
        e->catch_funclet = 1;
        e->disp_frame = fh4_unsigned(&p);
    }
    e->fh4 = 1;
    // Remontée : entrées de taille variable ; « next » est l'écart (en octets, vers l'arrière) jusqu'à
    // l'entrée de l'état suivant, 0 : aucun.
    if (unwind_rva) {
        u64 q = image + unwind_rva;
        u32 n = fh4_unsigned(&q);
        e->unwind_count = n;
        e->unwind = calloc(n + 1, sizeof *e->unwind);
        u64 *starts = calloc(n + 1, sizeof *starts);
        u32 *back = calloc(n + 1, sizeof *back);
        for (u32 i = 0; i < n && starts && back; i++) {
            starts[i] = q;
            u32 head = fh4_unsigned(&q);
            back[i] = head >> 2;
            e->unwind[i].kind = (u8)(head & 3);
            if (e->unwind[i].kind == 1 || e->unwind[i].kind == 2) {
                e->unwind[i].action = fh4_int(&q);
                e->unwind[i].object = fh4_unsigned(&q);
            } else if (e->unwind[i].kind == 3) {
                e->unwind[i].action = fh4_int(&q);
            }
        }
        for (u32 i = 0; i < n && starts && back; i++) {
            e->unwind[i].next = -1;
            if (!back[i]) continue;
            for (u32 j = 0; j < i; j++)
                if (starts[j] == starts[i] - back[i]) e->unwind[i].next = (i32)j;
        }
        free(starts);
        free(back);
    }
    if (try_rva) {
        u64 q = image + try_rva;
        e->try_count = fh4_unsigned(&q);
        e->tries = calloc(e->try_count + 1, sizeof *e->tries);
        for (u32 t = 0; t < e->try_count; t++) {
            EhTry *tr = &e->tries[t];
            tr->low = (i32)fh4_unsigned(&q);
            tr->high = (i32)fh4_unsigned(&q);
            fh4_unsigned(&q); // catchHigh
            u64 h = image + fh4_int(&q);
            tr->count = fh4_unsigned(&h);
            tr->handlers = calloc(tr->count + 1, sizeof *tr->handlers);
            for (u32 k = 0; k < tr->count; k++) {
                EhHandler *x = &tr->handlers[k];
                u8 flags = rd8(h++);
                if (flags & 0x01) x->adjectives = fh4_unsigned(&h);
                if (flags & 0x02) x->type = fh4_int(&h);
                if (flags & 0x04) x->catch_obj = (i32)fh4_unsigned(&h);
                x->handler = fh4_int(&h);
                x->cont_rva = (flags & 0x08) != 0;
                x->cont_count = (flags >> 4) & 3;
                for (int c = 0; c < x->cont_count && c < 2; c++) x->cont[c] = x->cont_rva ? fh4_int(&h) : fh4_unsigned(&h);
            }
        }
    }
    // IP → état : écarts d'adresse depuis le début de la fonction, états décalés de 1.
    e->state = -1;
    if (ip_rva) {
        u64 q = image + ip_rva, ip = function_start;
        u32 n = fh4_unsigned(&q);
        for (u32 i = 0; i < n; i++) {
            ip += fh4_unsigned(&q);
            i32 state = (i32)fh4_unsigned(&q) - 1;
            if (ip > pc) break;
            e->state = state;
        }
    }
    return 1;
}

// Description du cadre du dispatcher ; *frame : cadre de la fonction (celui d'un funclet catch remplacé).
static int eh_info(RcnCtx *ctx, u64 dc, int fh4, u64 *frame, EhInfo *e) {
    memset(e, 0, sizeof *e);
    u64 pc = rd64(dc), image = rd64(dc + 8), entry = rd64(dc + 16), data = rd64(dc + 56);
    if (!image || !entry || !data) return 0;
    e->image = image;
    e->function_start = image + rd32(entry);
    u64 fi = image + rd32(data);
    if (!(fh4 ? eh_read4(image, fi, e->function_start, pc, e) : eh_read3(image, fi, pc, e))) return 0;
    if (fh4) {
        if (e->catch_funclet) *frame = rd64(*frame + e->disp_frame);
        return 1;
    }
    // Format 3 : funclet catch reconnu à son entrée de .pdata, cadre de la fonction à disp_frame.
    for (u32 t = 0; t < e->try_count; t++)
        for (u32 h = 0; h < e->tries[t].count; h++) {
            u64 module = 0;
            if (lookup(ctx, image + e->tries[t].handlers[h].handler, &module) == entry) {
                *frame = rd64(*frame + e->tries[t].handlers[h].disp_frame);
                return 1;
            }
        }
    return 1;
}

// Destructeurs du cadre de l'état courant jusqu'à l'état visé (exclu).
static void eh_unwind_frame(RcnCtx *ctx, EhInfo *e, u64 frame, i32 target) {
    i32 state = e->state;
    for (int guard = 0; state != target && state >= 0 && (u32)state < e->unwind_count && guard < 100000; guard++) {
        EhUnwind *u = &e->unwind[state];
        state = u->next;
        if (u->kind == 3 && u->action) rcn_call_guest(ctx, e->image + u->action, frame, frame, 0, 0);
        else if (u->kind == 1) rcn_call_guest(ctx, e->image + u->action, frame + u->object, 0, 0, 0);
        else if (u->kind == 2) rcn_call_guest(ctx, e->image + u->action, rd64(frame + u->object), 0, 0, 0);
        if (ctx->unwind) return;
    }
}

// CatchableType du type lancé (ThrowInfo, relatif à timage) que prend ce catch ; 1 pour catch(...).
static u64 eh_match64(EhInfo *e, EhHandler *h, u64 info, u64 timage) {
    if (!h->type || (h->adjectives & 0x40)) return 1; // catch(...) (HT_IsStdDotDot)
    u64 type = e->image + h->type;
    if (!rd64(type) && !rd8(type + 16)) return 1;
    if (!info || !rd32(info + 12)) return 0;
    u64 array = timage + rd32(info + 12);
    u32 n = rd32(array);
    for (u32 i = 0; i < n && i < 64; i++) {
        u64 ct = timage + rd32(array + 4 + 4 * (u64)i), td = timage + rd32(ct + 4);
        if (td == type || !strcmp(STR(td + 16), STR(type + 16))) return ct;
    }
    return 0;
}
// Partie attrapée d'un objet (PMD { mdisp, pdisp, vdisp } de la classe de base).
static u64 eh_adjust64(u64 obj, u64 ct) {
    i32 mdisp = (i32)rd32(ct + 8), pdisp = (i32)rd32(ct + 12), vdisp = (i32)rd32(ct + 16);
    u64 p = obj;
    if (pdisp >= 0) p += (u64)(i64)pdisp + (u64)(i64)(i32)rd32(rd64(obj + (u64)(i64)pdisp) + (u64)(i64)vdisp);
    return p + (u64)(i64)mdisp;
}
// Objet attrapé rangé dans le cadre du catch : référence, type simple, copie, ou constructeur de copie.
static void eh_copy64(RcnCtx *ctx, EhHandler *h, u64 ct, u64 obj, u64 frame, u64 timage) {
    if (!h->catch_obj || ct <= 1 || !obj) return;
    u64 dst = frame + (u64)(i64)h->catch_obj;
    u32 props = rd32(ct), size = rd32(ct + 20), copy = rd32(ct + 24);
    if (h->adjectives & 8) { // HT_IsReference
        wr64(dst, eh_adjust64(obj, ct));
    } else if (props & 1) { // CT_IsSimpleType (pointeurs compris)
        memcpy(PTR(dst), PTR(obj), size);
        if (size == 8 && rd64(dst)) wr64(dst, eh_adjust64(rd64(dst), ct));
    } else if (!copy) {
        memcpy(PTR(dst), PTR(eh_adjust64(obj, ct)), size);
    } else {
        rcn_call_guest(ctx, timage + copy, dst, eh_adjust64(obj, ct), 1, 0);
    }
}

// Exception du bloc catch en cours (par thread), pour « throw; ».
#define EH_DEPTH 32
static _Thread_local struct {
    u64 obj, info, image;
} eh_current[EH_DEPTH];
static _Thread_local int eh_current_depth;

// Paramètres de l'exception STATUS_UNWIND_CONSOLIDATE (après la fonction, en 0) : cadre de la fonction,
// funclet catch, objet, ThrowInfo, son image, début du try (état visé), cadre du dispatcher, reprises.
enum { CC_FRAME = 1, CC_FUNCLET, CC_OBJ, CC_INFO, CC_TIMAGE, CC_LOW, CC_DISPATCH_FRAME, CC_CONT, CC_COUNT = 12 };
#define CC(record, i) ((record) + 32 + 8 * (u64)(i))

// __CxxCallCatchBlock (appelée par RtlUnwindEx une fois le cadre atteint) : bloc catch, puis objet lancé
// détruit ; rend l'adresse de reprise.
static void eh_call_catch(RcnCtx *ctx) {
    u64 record = A0, frame = rd64(CC(record, CC_FRAME)), obj = rd64(CC(record, CC_OBJ));
    u64 info = rd64(CC(record, CC_INFO)), timage = rd64(CC(record, CC_TIMAGE));
    int depth = eh_current_depth;
    if (depth < EH_DEPTH) eh_current[depth].obj = obj, eh_current[depth].info = info, eh_current[depth].image = timage;
    eh_current_depth = depth + 1;
    u64 cont = rcn_call_guest(ctx, rd64(CC(record, CC_FUNCLET)), frame, frame, 0, 0);
    eh_current_depth = depth;
    if (ctx->unwind) return; // exception sortie du bloc catch : reprise déjà décidée ailleurs
    if (info && rd32(info + 4) && obj) rcn_call_guest(ctx, timage + rd32(info + 4), obj, 0, 0, 0);
    // Format 4 : le funclet rend le numéro de l'adresse de reprise de sa table.
    u64 table = rd64(CC(record, CC_CONT));
    if (table) {
        EhHandler *h = PTR(table);
        if (h->cont_count && cont < (u64)h->cont_count)
            cont = h->cont_rva ? rd64(CC(record, CC_CONT + 1)) + h->cont[cont] : rd64(CC(record, CC_CONT + 2)) + h->cont[cont];
        free(h);
    }
    RET(cont);
}

// __CxxFrameHandler3 / 4(record, cadre, contexte, dispatcher)
static void cxx_frame_handler(RcnCtx *ctx, int fh4) {
    u64 record = A0, dispatch_frame = A1, dc = A3, frame = A1;
    u32 flags = rd32(record + 4);
    EhInfo e;
    if (!eh_info(ctx, dc, fh4, &frame, &e)) {
        RET(1);
        return;
    }
    if (flags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) {
        i32 target = -1;
        // Cadre du catch : objets du try seulement.
        if ((flags & EXCEPTION_TARGET_UNWIND) && rd32(record) == STATUS_UNWIND_CONSOLIDATE && rd32(record + 24) >= CC_COUNT &&
            rd64(CC(record, CC_DISPATCH_FRAME)) == dispatch_frame)
            target = (i32)rd64(CC(record, CC_LOW));
        eh_unwind_frame(ctx, &e, frame, target);
        eh_info_free(&e);
        RET(1);
        return;
    }
    u64 obj = 0, info = 0, timage = 0;
    if (rd32(record) == EH_MSC && rd32(record + 24) >= 3) {
        u32 magic = (u32)rd64(record + 32);
        if (magic >= EH_MAGIC1 && magic <= EH_MAGIC3) obj = rd64(record + 40), info = rd64(record + 48), timage = rd64(record + 56);
    }
    if (!info) { // exception d'ailleurs (SEH) ou relance sans exception en cours : pas pour un catch C++
        eh_info_free(&e);
        RET(1);
        return;
    }
    for (u32 t = 0; t < e.try_count; t++) {
        EhTry *tr = &e.tries[t];
        if (e.state < tr->low || e.state > tr->high) continue;
        for (u32 k = 0; k < tr->count; k++) {
            EhHandler *h = &tr->handlers[k];
            u64 ct = eh_match64(&e, h, info, timage);
            if (!ct) continue;
            eh_copy64(ctx, h, ct, obj, frame, timage);
            if (ctx->unwind) break;
            static u64 callback;
            if (!callback) callback = rcn_thunk(ctx, "vcruntime140", "__CxxCallCatchBlock", eh_call_catch, 0);
            u64 cc = (u64)(uintptr_t)calloc(1, RECORD_SIZE);
            if (!cc) rcn_fail(ctx, "memoire insuffisante (exception)");
            wr32(cc, STATUS_UNWIND_CONSOLIDATE);
            wr32(cc + 4, EXCEPTION_NONCONTINUABLE);
            wr32(cc + 24, CC_COUNT);
            wr64(CC(cc, 0), callback);
            wr64(CC(cc, CC_FRAME), frame);
            wr64(CC(cc, CC_FUNCLET), e.image + h->handler);
            wr64(CC(cc, CC_OBJ), obj), wr64(CC(cc, CC_INFO), info), wr64(CC(cc, CC_TIMAGE), timage);
            wr64(CC(cc, CC_LOW), (u64)(i64)tr->low);
            wr64(CC(cc, CC_DISPATCH_FRAME), dispatch_frame);
            if (h->cont_count) {
                EhHandler *copy = malloc(sizeof *copy);
                if (copy) *copy = *h;
                wr64(CC(cc, CC_CONT), (u64)(uintptr_t)copy);
                wr64(CC(cc, CC_CONT + 1), e.image), wr64(CC(cc, CC_CONT + 2), e.function_start);
            }
            if (getenv("RCN_EH_LOG")) rcn_log("catch C++ (format %d) : etat %d dans [%d, %d], bloc %llx", fh4 ? 4 : 3, e.state, tr->low, tr->high, (unsigned long long)(e.image + h->handler));
            eh_info_free(&e);
            // RtlUnwindEx(cadre du dispatcher, adresse, record) : ne revient qu'avec ctx->unwind posé.
            ctx->r[1] = dispatch_frame, ctx->r[2] = rd64(CC(cc, CC_FUNCLET)), ctx->r[8] = cc, ctx->r[9] = 0;
            k32_RtlUnwindEx(ctx);
            return;
        }
        if (ctx->unwind) break;
    }
    eh_info_free(&e);
    RET(1);
}
static void k32_CxxFrameHandler3(RcnCtx *ctx) { cxx_frame_handler(ctx, 0); }
static void k32_CxxFrameHandler4(RcnCtx *ctx) { cxx_frame_handler(ctx, 1); }
// _CxxThrowException(objet, ThrowInfo) : exception 0xE06D7363 à quatre paramètres (version, objet,
// ThrowInfo, image qui le contient), comme la bibliothèque de Visual C++. (NULL, NULL) : relance de
// l'exception du bloc catch en cours.
static void k32_CxxThrowException(RcnCtx *ctx) {
    u64 record = (u64)(uintptr_t)calloc(1, RECORD_SIZE), c = context_new(), image = 0;
    if (!record || !c) rcn_fail(ctx, "memoire insuffisante (exception)");
    u32 size = 0;
    u64 obj = A0, info = A1;
    if (!obj && !info && eh_current_depth > 0 && eh_current_depth <= EH_DEPTH) {
        obj = eh_current[eh_current_depth - 1].obj, info = eh_current[eh_current_depth - 1].info;
        image = eh_current[eh_current_depth - 1].image;
    } else if (info) {
        module_of(ctx, info, &image, &size);
    }
    wr32(record, 0xE06D7363u);
    wr32(record + 4, EXCEPTION_NONCONTINUABLE);
    capture_caller(ctx, c);
    wr64(record + 16, rd64(c + CTX_RIP));
    wr32(record + 24, 4);
    wr64(record + 32, 0x19930520u), wr64(record + 40, obj), wr64(record + 48, info), wr64(record + 56, image);
    if (ctx->proc->trace) fprintf(stderr, " [exception C++%s]", A1 ? "" : " relancee");
    if (getenv("RCN_EH_LOG") && info && rd32(info + 12)) {
        u64 array = image + rd32(info + 12), td = image + rd32(image + rd32(array + 4) + 4);
        rcn_log("exception C++ %s%s", STR(td + 16), A1 ? "" : " (relancee)");
    }
    dispatch(ctx, record, c);
}
// __std_exception_data { const char *what; bool do_free; } : texte de std::exception.
static void k32_std_exception_copy(RcnCtx *ctx) {
    u64 from = A0, to = A1, what = rd64(from);
    if (rd8(from + 8) && what) {
        size_t n = strlen(STR(what)) + 1;
        char *copy = rcn_galloc(ctx, n, 0);
        if (copy) memcpy(copy, STR(what), n);
        wr64(to, (u64)(uintptr_t)copy);
        wr8(to + 8, copy != NULL);
    } else {
        wr64(to, what);
        wr8(to + 8, 0);
    }
}
static void k32_std_exception_destroy(RcnCtx *ctx) {
    if (rd8(A0 + 8) && rd64(A0)) rcn_gfree(ctx, PTR(rd64(A0)));
    wr64(A0, 0);
    wr8(A0 + 8, 0);
}
static void k32_std_terminate(RcnCtx *ctx) { rcn_fail(ctx, "le jeu s'est arrete (std::terminate)"); }
static void k32_purecall64(RcnCtx *ctx) { rcn_fail(ctx, "appel d'une fonction virtuelle pure (_purecall)"); }
static void k32_nothing64(RcnCtx *ctx) { (void)ctx; }
static void k32_XcptFilter64(RcnCtx *ctx) { RET(0); } // EXCEPTION_CONTINUE_SEARCH
const RcnShim rcn_crt_eh64[] = {
    {"__C_specific_handler", k32_C_specific_handler, 0},
    {"__CxxFrameHandler3", k32_CxxFrameHandler3, 0},
    {"__CxxFrameHandler4", k32_CxxFrameHandler4, 0},
    {"_CxxThrowException", k32_CxxThrowException, 0},
    {"__std_exception_copy", k32_std_exception_copy, 0},
    {"__std_exception_destroy", k32_std_exception_destroy, 0},
    {"__std_terminate", k32_std_terminate, 0},
    {"__std_type_info_destroy_list", k32_nothing64, 0},
    {"_purecall", k32_purecall64, 0},
    {"_XcptFilter", k32_XcptFilter64, 0},
};
const size_t rcn_crt_eh64_count = sizeof rcn_crt_eh64 / sizeof rcn_crt_eh64[0];
// RtlCaptureStackBackTrace(skip, count, &frames, &hash) : adresses de retour de la pile du jeu, de
// l'appelant vers le haut (pistes des exceptions de IL2CPP…).
static void k32_RtlCaptureStackBackTrace(RcnCtx *ctx) {
    // Jeu 32 bits : pas de déroulement de sa pile (contexte x86 32 bits non géré ici) : aucun cadre.
    if (ctx->bits32) {
        RET(0);
        return;
    }
    u32 skip = (u32)A0, count = (u32)A1, n = 0, sum = 0;
    u64 frames = A2, hash = A3;
    u8 raw[CTX_SIZE + 16];
    u64 c = ((u64)(uintptr_t)raw + 15) & ~15ull;
    memset(PTR(c), 0, CTX_SIZE);
    if (!frames) {
        RET(0);
        return;
    }
    capture_caller(ctx, c);
    for (int depth = 0; depth < 512 && n < count; depth++) {
        u64 pc = rd64(c + CTX_RIP);
        if (!pc || !rcn_loaded_at(ctx->proc, pc)) break;
        if (skip) {
            skip--;
        } else {
            wr64(frames + 8ull * n++, pc);
            sum += (u32)pc;
        }
        u64 handler, data = 0, establisher = 0, function, image = 0;
        if (!step(ctx, 0, c, &handler, &data, &establisher, &function, &image)) break;
    }
    if (hash) wr32(hash, sum);
    RET(n);
}
// RtlRestoreContext(&context, &record) : reprise dans ce contexte.
static void k32_RtlRestoreContext(RcnCtx *ctx) {
    u64 c = A0;
    if (A1 && rd32(A1) == STATUS_UNWIND_CONSOLIDATE) {
        u64 copy = context_new();
        if (!copy) rcn_fail(ctx, "memoire insuffisante (exception)");
        memcpy(PTR(copy), PTR(c), CTX_SIZE);
        if (consolidate(ctx, copy, A1) < 0) return;
        c = copy;
    }
    resume_at(ctx, c);
}
const RcnShim rcn_k32_unwind[] = {
    {"RtlCaptureContext", k32_RtlCaptureContext, S32(1)},
    {"RtlLookupFunctionEntry", k32_RtlLookupFunctionEntry, 0},
    {"RtlPcToFileHeader", k32_RtlPcToFileHeader, 0},
    {"RtlRaiseException", k32_RtlRaiseException, 0},
    {"RtlRestoreContext", k32_RtlRestoreContext, 0},
    {"RtlCaptureStackBackTrace", k32_RtlCaptureStackBackTrace, S32(4)},
    {"RtlUnwindEx", k32_RtlUnwindEx, 0},
    {"RtlUnwind", k32_RtlUnwindEx, 0},
    {"RtlVirtualUnwind", k32_RtlVirtualUnwind, 0},
};
const size_t rcn_k32_unwind_count = sizeof rcn_k32_unwind / sizeof rcn_k32_unwind[0];
