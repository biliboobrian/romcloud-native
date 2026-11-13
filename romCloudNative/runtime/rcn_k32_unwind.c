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

// Un cadre plus haut : par .pdata, ou cadre feuille (adresse de retour au sommet). 0 hors du jeu.
static int step(RcnCtx *ctx, u32 type, u64 c, u64 *handler, u64 *handler_data, u64 *establisher, u64 *function, u64 *image) {
    u64 pc = rd64(c + CTX_RIP);
    *handler = 0;
    *function = lookup(ctx, pc, image);
    if (!*function) {
        if (!rcn_loaded_at(ctx->proc, pc)) return 0;
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
            resume_at(ctx, original);
            return;
        }
    }
    raise_context = saved;
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
    if (ctx->proc->trace) fprintf(stderr, " [exception %08x]", (u32)A0);
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
    u64 rip = rcn_call_guest_n(ctx, callback, 1, args);
    if (ctx->unwind) return -1; // le bloc catch a lui-même transféré le contrôle
    wr64(c + CTX_RIP, rip);
    return 1;
}

// RtlUnwindEx(target_frame, target_ip, &record, return_value, &context, history)
static void k32_RtlUnwindEx(RcnCtx *ctx) {
    u64 target_frame = A0, target_ip = A1, record = A2, value = A3;
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
    {"RtlCaptureContext", k32_RtlCaptureContext, 0},
    {"RtlLookupFunctionEntry", k32_RtlLookupFunctionEntry, 0},
    {"RtlPcToFileHeader", k32_RtlPcToFileHeader, 0},
    {"RtlRaiseException", k32_RtlRaiseException, 0},
    {"RtlRestoreContext", k32_RtlRestoreContext, 0},
    {"RtlUnwindEx", k32_RtlUnwindEx, 0},
    {"RtlUnwind", k32_RtlUnwindEx, 0},
    {"RtlVirtualUnwind", k32_RtlVirtualUnwind, 0},
};
const size_t rcn_k32_unwind_count = sizeof rcn_k32_unwind / sizeof rcn_k32_unwind[0];
