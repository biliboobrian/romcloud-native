//! Traduction des instructions x86-64 en C. Chaque fonction x86 devient une fonction C
//! « void sub_XXXXXXXX(RcnCtx *ctx) » : les registres vivent dans `ctx`, la pile du jeu est une zone
//! de mémoire ordinaire, et les adresses du jeu sont des adresses de l'hôte (image chargée telle quelle).
//! Une instruction non gérée devient un appel à rcn_trap : le programme s'arrête dessus en la nommant.

use crate::func::{branch_target, falls_through, flag_liveness, is_near_branch, mem_target, Func};
use crate::pe::Pe;
use iced_x86::{
    ConditionCode, FlowControl, Formatter, Instruction, IntelFormatter, Mnemonic as M, OpKind, Register, RflagsBits as F,
};
use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write;

/// La case d'import `slot` est-elle celle de setjmp (sous l'un de ses noms) ?
pub fn is_setjmp(pe: &Pe, slot: u32) -> bool {
    pe.imports.get(&slot).and_then(|n| n.rsplit('!').next()).is_some_and(|name| {
        // _setjmp3 : setjmp de la bibliothèque C de Visual C++ en 32 bits (msvcr90…).
        matches!(name, "setjmp" | "_setjmp" | "_setjmp3" | "_setjmpex" | "__intrinsic_setjmp" | "__intrinsic_setjmpex")
    })
}

pub struct Emitter<'a> {
    pub pe: &'a Pe,
    pub funcs: &'a BTreeSet<u32>,
    /// Fonctions réduites à un saut vers l'import setjmp (« jmp [__imp_setjmp] »).
    pub setjmp_thunks: &'a BTreeSet<u32>,
    pub comments: bool,
    /// Instructions non gérées : mnémonique → nombre.
    pub unimplemented: BTreeMap<String, usize>,
    pub total: usize,
    fmt: IntelFormatter,
}

fn ty(size: u32) -> &'static str {
    match size {
        1 => "u8",
        2 => "u16",
        4 => "u32",
        _ => "u64",
    }
}
fn sty(size: u32) -> &'static str {
    match size {
        1 => "i8",
        2 => "i16",
        4 => "i32",
        _ => "i64",
    }
}
fn gpr(reg: Register) -> usize {
    reg.full_register().number()
}
fn is_high8(reg: Register) -> bool {
    matches!(reg, Register::AH | Register::CH | Register::DH | Register::BH)
}
fn is_imm(kind: OpKind) -> bool {
    matches!(
        kind,
        OpKind::Immediate8
            | OpKind::Immediate16
            | OpKind::Immediate32
            | OpKind::Immediate64
            | OpKind::Immediate8to16
            | OpKind::Immediate8to32
            | OpKind::Immediate8to64
            | OpKind::Immediate32to64
    )
}

fn rd_reg(reg: Register) -> Option<String> {
    if !reg.is_gpr() {
        return None;
    }
    let n = gpr(reg);
    Some(match reg.size() {
        8 => format!("R({n})"),
        4 => format!("(u32)R({n})"),
        2 => format!("(u16)R({n})"),
        _ if is_high8(reg) => format!("(u8)(R({n}) >> 8)"),
        _ => format!("(u8)R({n})"),
    })
}
fn wr_reg(reg: Register, val: &str) -> Option<String> {
    if !reg.is_gpr() {
        return None;
    }
    let n = gpr(reg);
    Some(match reg.size() {
        8 => format!("R({n}) = (u64)({val});"),
        // Une écriture 32 bits met à zéro la moitié haute du registre.
        4 => format!("R({n}) = (u32)({val});"),
        2 => format!("R({n}) = (R({n}) & ~0xFFFFULL) | (u16)({val});"),
        _ if is_high8(reg) => format!("R({n}) = (R({n}) & ~0xFF00ULL) | ((u64)(u8)({val}) << 8);"),
        _ => format!("R({n}) = (R({n}) & ~0xFFULL) | (u8)({val});"),
    })
}

fn cond(cc: ConditionCode) -> Option<&'static str> {
    Some(match cc {
        ConditionCode::None => return None,
        ConditionCode::o => "ctx->of",
        ConditionCode::no => "!ctx->of",
        ConditionCode::b => "ctx->cf",
        ConditionCode::ae => "!ctx->cf",
        ConditionCode::e => "ctx->zf",
        ConditionCode::ne => "!ctx->zf",
        ConditionCode::be => "(ctx->cf | ctx->zf)",
        ConditionCode::a => "!(ctx->cf | ctx->zf)",
        ConditionCode::s => "ctx->sf",
        ConditionCode::ns => "!ctx->sf",
        ConditionCode::p => "ctx->pf",
        ConditionCode::np => "!ctx->pf",
        ConditionCode::l => "(ctx->sf != ctx->of)",
        ConditionCode::ge => "(ctx->sf == ctx->of)",
        ConditionCode::le => "(ctx->zf | (ctx->sf != ctx->of))",
        ConditionCode::g => "!(ctx->zf | (ctx->sf != ctx->of))",
    })
}

/// Affectation d'un drapeau, seulement s'il est lu plus loin.
fn fl(need: u32, bit: u32, name: &str, expr: &str) -> String {
    if need & bit != 0 {
        format!(" ctx->{name} = {expr};")
    } else {
        String::new()
    }
}
/// ZF, SF et PF du résultat `r` de `n` bits.
fn fl_zsp(need: u32, n: u32) -> String {
    fl(need, F::ZF, "zf", "r == 0")
        + &fl(need, F::SF, "sf", &format!("(r >> {}) & 1", n - 1))
        + &fl(need, F::PF, "pf", "rcn_parity((u8)r)")
}

enum Alu {
    Add,
    Adc,
    Sub,
    Sbb,
    Cmp,
    And,
    Test,
    Or,
    Xor,
}

/// Opérations sur 128 bits de la forme « dst = rcn_<mnémonique>(dst, src) ».
const PACKED_BINARY: &[M] = &[
    M::Addps, M::Subps, M::Mulps, M::Divps, M::Minps, M::Maxps, M::Addpd, M::Subpd, M::Mulpd, M::Divpd, M::Minpd,
    M::Maxpd, M::Andps, M::Andpd, M::Pand, M::Orps, M::Orpd, M::Por, M::Xorps, M::Xorpd, M::Pxor, M::Andnps,
    M::Andnpd, M::Pandn, M::Paddb, M::Paddw, M::Paddd, M::Paddq, M::Psubb, M::Psubw, M::Psubd, M::Psubq,
    M::Pcmpeqb, M::Pcmpeqw, M::Pcmpeqd, M::Pcmpgtb, M::Pcmpgtw, M::Pcmpgtd, M::Pmullw, M::Pmulld, M::Pmuludq,
    M::Punpcklbw, M::Punpcklwd, M::Punpckldq, M::Punpcklqdq, M::Punpckhbw, M::Punpckhwd, M::Punpckhdq,
    M::Punpckhqdq, M::Unpcklps, M::Unpckhps, M::Unpcklpd, M::Unpckhpd, M::Pminub, M::Pmaxub, M::Pminsw,
    M::Pmaxsw, M::Packuswb, M::Packsswb, M::Packssdw, M::Movlhps, M::Movhlps, M::Paddusb, M::Psubusb, M::Pavgb,
    M::Pmaddwd, M::Haddps, M::Paddsb, M::Psubsb, M::Paddsw, M::Psubsw, M::Paddusw, M::Psubusw, M::Pmulhw, M::Pmulhuw, M::Pavgw, M::Psadbw,
    M::Addsubps, M::Addsubpd, M::Hsubps, M::Haddpd, M::Hsubpd, M::Pmaddubsw, M::Pshufb,
    M::Phaddw, M::Phaddd, M::Phaddsw, M::Phsubw, M::Phsubd, M::Phsubsw, M::Pmulhrsw, M::Psignb, M::Psignw, M::Psignd,
    M::Pminsb, M::Pmaxsb, M::Pminuw, M::Pmaxuw, M::Pminsd, M::Pmaxsd, M::Pminud, M::Pmaxud, M::Pcmpeqq, M::Pcmpgtq,
    M::Packusdw, M::Pmuldq,
];
/// « dst = rcn_<mnémonique>(src) ».
const PACKED_UNARY: &[M] = &[M::Sqrtps, M::Sqrtpd, M::Cvtdq2ps, M::Cvtps2dq, M::Cvttps2dq, M::Cvtpd2ps, M::Cvttpd2dq, M::Cvtpd2dq, M::Movsldup, M::Movshdup, M::Rcpps, M::Rsqrtps, M::Pabsb, M::Pabsw, M::Pabsd, M::Phminposuw];
/// Comme PACKED_UNARY, la source n'occupant que 64 bits.
const PACKED_UNARY_LOW: &[M] = &[M::Cvtdq2pd, M::Cvtps2pd, M::Movddup];
/// « dst = rcn_<mnémonique>(dst, décalage) » : décalage immédiat ou dans un registre xmm.
const PACKED_SHIFT: &[M] = &[
    M::Psllw, M::Pslld, M::Psllq, M::Psrlw, M::Psrld, M::Psrlq, M::Psraw, M::Psrad, M::Pslldq, M::Psrldq,
];
const MOVE_128: &[M] = &[
    M::Movaps, M::Movups, M::Movapd, M::Movupd, M::Movdqa, M::Movdqu, M::Lddqu, M::Movntdq, M::Movntps, M::Movntpd, M::Movntdqa,
];
const NOPS: &[M] = &[
    M::Nop, M::Pause, M::Endbr64, M::Prefetchnta, M::Prefetcht0, M::Prefetcht1, M::Prefetcht2, M::Prefetchw,
    M::Lfence, M::Mfence, M::Sfence, M::Wait, M::Emms,
];

fn helper(m: M) -> String {
    format!("rcn_{m:?}").to_lowercase()
}

impl<'a> Emitter<'a> {
    pub fn new(pe: &'a Pe, funcs: &'a BTreeSet<u32>, setjmp_thunks: &'a BTreeSet<u32>, comments: bool) -> Self {
        Emitter {
            pe,
            funcs,
            setjmp_thunks,
            comments,
            unimplemented: BTreeMap::new(),
            total: 0,
            fmt: IntelFormatter::new(),
        }
    }

    fn opsize(&self, ins: &Instruction, i: u32) -> u32 {
        match ins.op_kind(i) {
            OpKind::Register => ins.op_register(i).size() as u32,
            OpKind::Memory => ins.memory_size().size() as u32,
            _ => 0,
        }
    }

    /// Adresse effective d'un opérande mémoire.
    fn ea(&self, ins: &Instruction) -> Option<String> {
        let (base, index) = (ins.memory_base(), ins.memory_index());
        let disp = ins.memory_displacement64();
        let seg = match ins.segment_prefix() {
            Register::GS => "ctx->gs_base + ",
            Register::FS => "ctx->fs_base + ",
            _ => "",
        };
        if base == Register::RIP {
            return Some(format!("(ctx->base + 0x{:x}ULL)", disp.wrapping_sub(self.pe.image_base)));
        }
        if base == Register::EIP {
            return None;
        }
        if base == Register::None && index == Register::None {
            // Adresse absolue dans l'image (rare en x86-64) : relative à la base réelle.
            let rva = disp.wrapping_sub(self.pe.image_base);
            if seg.is_empty() && rva < self.pe.size_of_image as u64 {
                return Some(format!("(ctx->base + 0x{rva:x}ULL)"));
            }
            return Some(format!("({seg}0x{disp:x}ULL)"));
        }
        let mut narrow = false;
        let mut parts = Vec::new();
        for (reg, scale) in [(base, 1), (index, ins.memory_index_scale())] {
            if reg == Register::None {
                continue;
            }
            if !reg.is_gpr() || reg.size() < 4 {
                return None;
            }
            narrow |= reg.size() == 4;
            let r = format!("R({})", gpr(reg));
            parts.push(if scale == 1 { r } else { format!("{r} * {scale}") });
        }
        if disp != 0 {
            parts.push(format!("0x{disp:x}ULL"));
        }
        let sum = parts.join(" + ");
        Some(if narrow { format!("({seg}(u64)(u32)({sum}))") } else { format!("({seg}{sum})") })
    }

    /// Valeur entière d'un opérande, de `size` octets.
    fn rd(&self, ins: &Instruction, i: u32, size: u32) -> Option<String> {
        match ins.op_kind(i) {
            OpKind::Register => {
                let reg = ins.op_register(i);
                (reg.size() as u32 == size).then(|| rd_reg(reg)).flatten()
            }
            OpKind::Memory => Some(format!("rd{}({})", size * 8, self.ea(ins)?)),
            k if is_imm(k) => Some(format!("({})0x{:x}ULL", ty(size), ins.immediate(i))),
            _ => None,
        }
    }
    /// Instruction C qui écrit `val` dans un opérande.
    fn wr(&self, ins: &Instruction, i: u32, size: u32, val: &str) -> Option<String> {
        match ins.op_kind(i) {
            OpKind::Register => {
                let reg = ins.op_register(i);
                (reg.size() as u32 == size).then(|| wr_reg(reg, val)).flatten()
            }
            OpKind::Memory => Some(format!("wr{}({}, {val});", size * 8, self.ea(ins)?)),
            _ => None,
        }
    }
    fn xmm(&self, ins: &Instruction, i: u32) -> Option<usize> {
        (ins.op_kind(i) == OpKind::Register && ins.op_register(i).is_xmm()).then(|| ins.op_register(i).number())
    }
    /// Opérande de 128 bits (registre xmm ou mémoire).
    fn x128(&self, ins: &Instruction, i: u32) -> Option<String> {
        match ins.op_kind(i) {
            OpKind::Register => Some(format!("X({})", self.xmm(ins, i)?)),
            OpKind::Memory => Some(format!("rdx({})", self.ea(ins)?)),
            _ => None,
        }
    }
    /// Opérande scalaire d'un registre xmm ou de la mémoire : champ `field` (f, db, d, q) et lecture `rd`.
    fn xlow(&self, ins: &Instruction, i: u32, field: &str, rd: &str) -> Option<String> {
        match ins.op_kind(i) {
            OpKind::Register => Some(format!("X({}).{field}[0]", self.xmm(ins, i)?)),
            OpKind::Memory => Some(format!("{rd}({})", self.ea(ins)?)),
            _ => None,
        }
    }
    fn f32(&self, ins: &Instruction, i: u32) -> Option<String> {
        self.xlow(ins, i, "f", "rdf32")
    }
    fn f64(&self, ins: &Instruction, i: u32) -> Option<String> {
        self.xlow(ins, i, "db", "rdf64")
    }

    fn func_name(rva: u32) -> String {
        format!("sub_{rva:08X}")
    }

    /// Saut vers `target` : étiquette de la fonction, ou appel terminal d'une autre fonction.
    fn jump(&self, f: &Func, ins: &Instruction, rva: u32) -> String {
        match branch_target(self.pe, ins) {
            Some(t) if !f.is_external(t) => format!("goto L_{t:08X};"),
            Some(t) if self.funcs.contains(&t) => format!("{{ {}(ctx); return; }}", Self::func_name(t)),
            _ => format!("rcn_trap(ctx, 0x{rva:x}, \"saut hors du code\");"),
        }
    }

    fn alu(&self, ins: &Instruction, need: u32, kind: Alu) -> Option<String> {
        let size = self.opsize(ins, 0);
        let (t, n) = (ty(size), size * 8);
        let (a, b) = (self.rd(ins, 0, size)?, self.rd(ins, 1, size)?);
        // Préfixe lock : lecture, calcul et écriture en une opération atomique de l'hôte, qui rend
        // l'ancienne valeur ; le résultat ne sert plus qu'aux drapeaux.
        let locked = self.locked(ins);
        let mut s = if locked {
            let op = match kind {
                Alu::Add => "add",
                Alu::Sub => "sub",
                Alu::And => "and",
                Alu::Or => "or",
                Alu::Xor => "xor",
                _ => return None,
            };
            format!("{t} b = {b}, a = __atomic_fetch_{op}({}, b, __ATOMIC_SEQ_CST), r;", self.atomic_ptr(ins, size)?)
        } else {
            format!("{t} a = {a}, b = {b}, r;")
        };
        let top = n - 1;
        let af = "((a ^ b ^ r) >> 4) & 1";
        match kind {
            Alu::Add | Alu::Adc => {
                if matches!(kind, Alu::Adc) {
                    s += " u8 c = ctx->cf; r = a + b + c;";
                    s += &fl(need, F::CF, "cf", "c ? r <= a : r < a");
                } else {
                    s += " r = a + b;";
                    s += &fl(need, F::CF, "cf", "r < a");
                }
                s += &fl(need, F::OF, "of", &format!("(((a ^ r) & (b ^ r)) >> {top}) & 1"));
                s += &fl(need, F::AF, "af", af);
            }
            Alu::Sub | Alu::Sbb | Alu::Cmp => {
                if matches!(kind, Alu::Sbb) {
                    s += " u8 c = ctx->cf; r = a - b - c;";
                    s += &fl(need, F::CF, "cf", "c ? a <= b : a < b");
                } else {
                    s += " r = a - b;";
                    s += &fl(need, F::CF, "cf", "a < b");
                }
                s += &fl(need, F::OF, "of", &format!("(((a ^ b) & (a ^ r)) >> {top}) & 1"));
                s += &fl(need, F::AF, "af", af);
            }
            Alu::And | Alu::Test | Alu::Or | Alu::Xor => {
                s += match kind {
                    Alu::Or => " r = a | b;",
                    Alu::Xor => " r = a ^ b;",
                    _ => " r = a & b;",
                };
                s += &fl(need, F::CF, "cf", "0");
                s += &fl(need, F::OF, "of", "0");
                s += &fl(need, F::AF, "af", "0");
            }
        }
        s += &fl_zsp(need, n);
        if !locked && !matches!(kind, Alu::Cmp | Alu::Test) {
            s += " ";
            s += &self.wr(ins, 0, size, "r")?;
        }
        Some(s)
    }

    /// Instruction à préfixe lock sur un opérande mémoire : à traduire en opération atomique.
    fn locked(&self, ins: &Instruction) -> bool {
        ins.has_lock_prefix() && ins.op0_kind() == OpKind::Memory
    }
    /// Pointeur typé sur l'opérande mémoire, pour les opérations atomiques du compilateur.
    fn atomic_ptr(&self, ins: &Instruction, size: u32) -> Option<String> {
        Some(format!("({} *)(uintptr_t){}", ty(size), self.ea(ins)?))
    }

    fn shift(&self, ins: &Instruction, need: u32) -> Option<String> {
        let size = self.opsize(ins, 0);
        let (t, st, n) = (ty(size), sty(size), size * 8);
        let mask = if n == 64 { 63 } else { 31 };
        let a = self.rd(ins, 0, size)?;
        let count = self.rd(ins, 1, 1)?;
        let top = n - 1;
        let mut s = format!("{t} a = {a}, r; u8 c = ({count}) & {mask}; if (c) {{ u8 cf;");
        match ins.mnemonic() {
            M::Shl | M::Sal => {
                s += &format!(" r = ({t})((u64)a << c); cf = c <= {n} ? ((u64)a >> ({n} - c)) & 1 : 0;");
                s += &fl(need, F::OF, "of", &format!("((r >> {top}) & 1) ^ cf"));
            }
            M::Shr => {
                s += &format!(" r = ({t})((u64)a >> c); cf = ((u64)a >> (c - 1)) & 1;");
                s += &fl(need, F::OF, "of", &format!("(a >> {top}) & 1"));
            }
            M::Sar => {
                s += &format!(" r = ({t})((i64)({st})a >> c); cf = ((i64)({st})a >> (c - 1)) & 1;");
                s += &fl(need, F::OF, "of", "0");
            }
            M::Rol => {
                s += &format!(" u8 m = c % {n}; r = m ? ({t})(((u64)a << m) | ((u64)a >> ({n} - m))) : a; cf = r & 1;");
                s += &fl(need, F::OF, "of", &format!("((r >> {top}) & 1) ^ cf"));
            }
            M::Ror => {
                s += &format!(
                    " u8 m = c % {n}; r = m ? ({t})(((u64)a >> m) | ((u64)a << ({n} - m))) : a; cf = (r >> {top}) & 1;"
                );
                s += &fl(need, F::OF, "of", &format!("((r >> {top}) ^ (r >> {})) & 1", n - 2));
            }
            _ => return None,
        }
        s += &fl(need, F::CF, "cf", "cf");
        if !matches!(ins.mnemonic(), M::Rol | M::Ror) {
            s += &fl_zsp(need, n);
            s += &fl(need, F::AF, "af", "0");
        }
        s += " (void)cf; ";
        s += &self.wr(ins, 0, size, "r")?;
        s += " }";
        // Même sans décalage, une destination 32 bits est étendue par des zéros.
        if size == 4 && ins.op0_kind() == OpKind::Register {
            s += &format!(" else {{ {} }}", self.wr(ins, 0, size, "a")?);
        }
        Some(s)
    }

    /// shld / shrd (32 et 64 bits).
    fn shift_double(&self, ins: &Instruction, need: u32) -> Option<String> {
        let size = self.opsize(ins, 0);
        if size < 4 {
            return None;
        }
        let (t, n) = (ty(size), size * 8);
        let (a, b) = (self.rd(ins, 0, size)?, self.rd(ins, 1, size)?);
        let count = self.rd(ins, 2, 1)?;
        let mut s = format!("{t} a = {a}, b = {b}, r; u8 c = ({count}) & {}; if (c) {{", n - 1);
        if ins.mnemonic() == M::Shld {
            s += &format!(" r = (a << c) | (b >> ({n} - c));");
            s += &fl(need, F::CF, "cf", &format!("(a >> ({n} - c)) & 1"));
        } else {
            s += &format!(" r = (a >> c) | (b << ({n} - c));");
            s += &fl(need, F::CF, "cf", "(a >> (c - 1)) & 1");
        }
        s += &fl(need, F::OF, "of", &format!("((a ^ r) >> {}) & 1", n - 1));
        s += &fl_zsp(need, n);
        s += " ";
        s += &self.wr(ins, 0, size, "r")?;
        s += " }";
        Some(s)
    }

    fn mul_div(&self, ins: &Instruction, need: u32, rva: u32) -> Option<String> {
        let m = ins.mnemonic();
        let co = |s: &mut String| {
            *s += &fl(need, F::CF, "cf", "o");
            *s += &fl(need, F::OF, "of", "o");
        };
        if m == M::Imul && ins.op_count() >= 2 {
            let size = self.opsize(ins, 0);
            let (a, b) = if ins.op_count() == 3 {
                (self.rd(ins, 1, size)?, self.rd(ins, 2, size)?)
            } else {
                (self.rd(ins, 0, size)?, self.rd(ins, 1, size)?)
            };
            let st = sty(size);
            let mut s = if size == 8 {
                format!("i64 r; u8 o = __builtin_mul_overflow((i64){a}, (i64){b}, &r);")
            } else {
                format!("i64 w = (i64)({st}){a} * (i64)({st}){b}; {st} r = ({st})w; u8 o = w != (i64)r;")
            };
            co(&mut s);
            s += " (void)o; ";
            s += &self.wr(ins, 0, size, "r")?;
            return Some(s);
        }
        let size = self.opsize(ins, 0);
        let b = self.rd(ins, 0, size)?;
        let mut s = match (m, size) {
            (M::Mul, 8) => format!(
                "unsigned __int128 w = (unsigned __int128)R(0) * {b}; R(0) = (u64)w; R(2) = (u64)(w >> 64); u8 o = R(2) != 0;"
            ),
            (M::Imul, 8) => format!(
                "__int128 w = (__int128)(i64)R(0) * (i64){b}; R(0) = (u64)w; R(2) = (u64)((unsigned __int128)w >> 64); u8 o = w != (__int128)(i64)w;"
            ),
            (M::Mul, 4) => {
                format!("u64 w = (u64)(u32)R(0) * {b}; R(0) = (u32)w; R(2) = (u32)(w >> 32); u8 o = R(2) != 0;")
            }
            (M::Imul, 4) => format!(
                "i64 w = (i64)(i32)R(0) * (i64)(i32){b}; R(0) = (u32)w; R(2) = (u32)((u64)w >> 32); u8 o = w != (i64)(i32)w;"
            ),
            (M::Mul, 1) => format!("u16 w = (u16)((u8)R(0) * {b}); R(0) = (R(0) & ~0xFFFFULL) | w; u8 o = (w >> 8) != 0;"),
            (M::Imul, 1) => format!(
                "i16 w = (i16)((i8)R(0) * (i8){b}); R(0) = (R(0) & ~0xFFFFULL) | (u16)w; u8 o = w != (i16)(i8)w;"
            ),
            (M::Div, 4 | 8) | (M::Idiv, 4 | 8) => {
                let name = if m == M::Div { "div" } else { "idiv" };
                return Some(format!("rcn_{name}{}(ctx, {b}, 0x{rva:x});", size * 8));
            }
            _ => return None,
        };
        co(&mut s);
        s += " (void)o;";
        Some(s)
    }

    /// Instructions SSE / SSE2 (valeurs dans ctx->xmm).
    fn sse(&self, ins: &Instruction) -> Option<String> {
        let m = ins.mnemonic();
        let dst = self.xmm(ins, 0);
        if PACKED_BINARY.contains(&m) {
            let d = dst?;
            return Some(format!("X({d}) = {}(X({d}), {});", helper(m), self.x128(ins, 1)?));
        }
        if PACKED_UNARY.contains(&m) {
            return Some(format!("X({}) = {}({});", dst?, helper(m), self.x128(ins, 1)?));
        }
        if PACKED_UNARY_LOW.contains(&m) {
            let src = self.xlow(ins, 1, "q", "rd64")?;
            return Some(format!("X({}) = {}(rcn_xmm_q({src}));", dst?, helper(m)));
        }
        if PACKED_SHIFT.contains(&m) {
            let d = dst?;
            let count = if is_imm(ins.op1_kind()) {
                format!("0x{:x}", ins.immediate(1))
            } else {
                format!("{}.q[0]", self.x128(ins, 1)?)
            };
            return Some(format!("X({d}) = {}(X({d}), {count});", helper(m)));
        }
        if MOVE_128.contains(&m) {
            return match ins.op0_kind() {
                OpKind::Register => Some(format!("X({}) = {};", dst?, self.x128(ins, 1)?)),
                OpKind::Memory => Some(format!("wrx({}, X({}));", self.ea(ins)?, self.xmm(ins, 1)?)),
                _ => None,
            };
        }
        let scalar = |op: &str, double: bool| -> Option<String> {
            let d = dst?;
            let (field, src) = if double { ("db", self.f64(ins, 1)?) } else { ("f", self.f32(ins, 1)?) };
            let dv = format!("X({d}).{field}[0]");
            Some(match op {
                "min" => format!("{dv} = {dv} < {src} ? {dv} : {src};"),
                "max" => format!("{dv} = {dv} > {src} ? {dv} : {src};"),
                "sqrt" => format!("{dv} = {}({src});", if double { "sqrt" } else { "sqrtf" }),
                // Approximations de x86 (12 bits) : valeur exacte, plus précise.
                "rsqrt" => format!("{dv} = 1.0f / sqrtf({src});"),
                "rcp" => format!("{dv} = 1.0f / {src};"),
                _ => format!("{dv} = {dv} {op} {src};"),
            })
        };
        // Comparaison de flottants : ZF, PF, CF ; les autres drapeaux à zéro.
        let compare = |double: bool| -> Option<String> {
            let (t, a, b) = if double {
                ("double", self.f64(ins, 0)?, self.f64(ins, 1)?)
            } else {
                ("float", self.f32(ins, 0)?, self.f32(ins, 1)?)
            };
            Some(format!(
                "{t} a = {a}, b = {b}; u8 un = (a != a) | (b != b); ctx->zf = un | (a == b); ctx->pf = un; ctx->cf = un | (a < b); ctx->of = 0; ctx->sf = 0; ctx->af = 0;"
            ))
        };
        let to_int = |name: &str, double: bool| -> Option<String> {
            let size = self.opsize(ins, 0);
            let src = if double { self.f64(ins, 1)? } else { self.f32(ins, 1)? };
            let call = format!("{name}_{}_i{}({src})", if double { "f64" } else { "f32" }, size * 8);
            self.wr(ins, 0, size, &call)
        };
        match m {
            M::Addss => scalar("+", false),
            M::Subss => scalar("-", false),
            M::Mulss => scalar("*", false),
            M::Divss => scalar("/", false),
            M::Minss => scalar("min", false),
            M::Maxss => scalar("max", false),
            M::Sqrtss => scalar("sqrt", false),
            M::Rsqrtss => scalar("rsqrt", false),
            M::Rcpss => scalar("rcp", false),
            M::Addsd => scalar("+", true),
            M::Subsd => scalar("-", true),
            M::Mulsd => scalar("*", true),
            M::Divsd => scalar("/", true),
            M::Minsd => scalar("min", true),
            M::Maxsd => scalar("max", true),
            M::Sqrtsd => scalar("sqrt", true),
            M::Comiss | M::Ucomiss => compare(false),
            M::Comisd | M::Ucomisd => compare(true),
            M::Movss | M::Movsd => {
                let double = m == M::Movsd;
                let (field, wr) = if double { ("db", "wrf64") } else { ("f", "wrf32") };
                match (ins.op0_kind(), ins.op1_kind()) {
                    // Entre registres : seule la partie basse est copiée.
                    (OpKind::Register, OpKind::Register) => {
                        Some(format!("X({}).{field}[0] = X({}).{field}[0];", dst?, self.xmm(ins, 1)?))
                    }
                    // Depuis la mémoire : le reste du registre est mis à zéro.
                    (OpKind::Register, OpKind::Memory) => {
                        let (t, rd) = if double { ("double", "rdf64") } else { ("float", "rdf32") };
                        let d = dst?;
                        Some(format!(
                            "{t} v = {rd}({}); X({d}).q[0] = 0; X({d}).q[1] = 0; X({d}).{field}[0] = v;",
                            self.ea(ins)?
                        ))
                    }
                    (OpKind::Memory, OpKind::Register) => {
                        Some(format!("{wr}({}, X({}).{field}[0]);", self.ea(ins)?, self.xmm(ins, 1)?))
                    }
                    _ => None,
                }
            }
            M::Movd | M::Movq => {
                let size = if m == M::Movd { 4 } else { 8 };
                let field = if size == 4 { "d" } else { "q" };
                if let Some(d) = dst {
                    let src = match self.xmm(ins, 1) {
                        Some(s) => format!("X({s}).{field}[0]"),
                        None => self.rd(ins, 1, size)?,
                    };
                    Some(format!("u64 v = {src}; X({d}).q[0] = v; X({d}).q[1] = 0;"))
                } else {
                    self.wr(ins, 0, size, &format!("X({}).{field}[0]", self.xmm(ins, 1)?))
                }
            }
            M::Movlps | M::Movlpd | M::Movhps | M::Movhpd => {
                let half = if matches!(m, M::Movlps | M::Movlpd) { 0 } else { 1 };
                match (ins.op0_kind(), ins.op1_kind()) {
                    (OpKind::Register, OpKind::Memory) => Some(format!("X({}).q[{half}] = rd64({});", dst?, self.ea(ins)?)),
                    (OpKind::Memory, OpKind::Register) => {
                        Some(format!("wr64({}, X({}).q[{half}]);", self.ea(ins)?, self.xmm(ins, 1)?))
                    }
                    _ => None,
                }
            }
            M::Cvtsi2ss | M::Cvtsi2sd => {
                let size = self.opsize(ins, 1);
                let (field, t) = if m == M::Cvtsi2ss { ("f", "float") } else { ("db", "double") };
                Some(format!("X({}).{field}[0] = ({t})({}){};", dst?, sty(size), self.rd(ins, 1, size)?))
            }
            M::Cvtss2sd => Some(format!("X({}).db[0] = (double){};", dst?, self.f32(ins, 1)?)),
            M::Cvtsd2ss => Some(format!("X({}).f[0] = (float){};", dst?, self.f64(ins, 1)?)),
            M::Cvttss2si => to_int("rcn_cvtt", false),
            M::Cvttsd2si => to_int("rcn_cvtt", true),
            M::Cvtss2si => to_int("rcn_cvt", false),
            M::Cvtsd2si => to_int("rcn_cvt", true),
            M::Movmskps | M::Movmskpd | M::Pmovmskb => {
                let size = self.opsize(ins, 0);
                self.wr(ins, 0, size, &format!("{}(X({}))", helper(m), self.xmm(ins, 1)?))
            }
            M::Pshufd | M::Pshuflw | M::Pshufhw | M::Roundps | M::Roundpd => {
                Some(format!("X({}) = {}({}, 0x{:x});", dst?, helper(m), self.x128(ins, 1)?, ins.immediate(2)))
            }
            M::Shufps | M::Shufpd | M::Cmpps | M::Cmppd | M::Palignr | M::Blendps | M::Blendpd | M::Pblendw | M::Dpps
            | M::Dppd => {
                let d = dst?;
                Some(format!("X({d}) = {}(X({d}), {}, 0x{:x});", helper(m), self.x128(ins, 1)?, ins.immediate(2)))
            }
            M::Cmpss => {
                let d = dst?;
                Some(format!(
                    "X({d}).d[0] = rcn_cmp_f64(X({d}).f[0], {}, 0x{:x}) ? 0xFFFFFFFFu : 0;",
                    self.f32(ins, 1)?,
                    ins.immediate(2)
                ))
            }
            M::Cmpsd => {
                let d = dst?;
                Some(format!(
                    "X({d}).q[0] = rcn_cmp_f64(X({d}).db[0], {}, 0x{:x}) ? ~0ULL : 0;",
                    self.f64(ins, 1)?,
                    ins.immediate(2)
                ))
            }
            M::Pextrw => {
                let size = self.opsize(ins, 0);
                self.wr(ins, 0, size, &format!("X({}).w[{}]", self.xmm(ins, 1)?, ins.immediate(2) & 7))
            }
            M::Pinsrw => {
                let src = match ins.op1_kind() {
                    OpKind::Register => self.rd(ins, 1, 4)?,
                    _ => self.rd(ins, 1, 2)?,
                };
                Some(format!("X({}).w[{}] = (u16){src};", dst?, ins.immediate(2) & 7))
            }
            _ => self.sse4(ins, dst),
        }
    }

    /// SSE4.1 : mélanges par xmm0, arrondis scalaires, extensions, extractions et insertions, ptest.
    fn sse4(&self, ins: &Instruction, dst: Option<usize>) -> Option<String> {
        let m = ins.mnemonic();
        let imm = ins.immediate(2) as u32;
        match m {
            M::Blendvps | M::Blendvpd | M::Pblendvb => {
                let d = dst?;
                Some(format!("X({d}) = {}(X({d}), {}, X(0));", helper(m), self.x128(ins, 1)?))
            }
            M::Roundss => Some(format!("X({}).f[0] = rcn_round_f32({}, 0x{imm:x});", dst?, self.f32(ins, 1)?)),
            M::Roundsd => Some(format!("X({}).db[0] = rcn_round_f64({}, 0x{imm:x});", dst?, self.f64(ins, 1)?)),
            M::Pmovsxbw | M::Pmovsxbd | M::Pmovsxbq | M::Pmovsxwd | M::Pmovsxwq | M::Pmovsxdq | M::Pmovzxbw
            | M::Pmovzxbd | M::Pmovzxbq | M::Pmovzxwd | M::Pmovzxwq | M::Pmovzxdq => {
                let bytes = match m {
                    M::Pmovsxbd | M::Pmovzxbd | M::Pmovsxwq | M::Pmovzxwq => 4,
                    M::Pmovsxbq | M::Pmovzxbq => 2,
                    _ => 8,
                };
                // Mémoire : seuls les octets utiles sont lus.
                let src = match self.xmm(ins, 1) {
                    Some(s) => format!("X({s})"),
                    None => format!("rcn_xmm_q(rd{}({}))", bytes * 8, self.ea(ins)?),
                };
                Some(format!("X({}) = {}({src});", dst?, helper(m)))
            }
            M::Pextrb | M::Pextrd | M::Pextrq | M::Extractps => {
                let s = self.xmm(ins, 1)?;
                let val = match m {
                    M::Pextrb => format!("X({s}).b[{}]", imm & 15),
                    M::Pextrq => format!("X({s}).q[{}]", imm & 1),
                    _ => format!("X({s}).d[{}]", imm & 3),
                };
                let size = self.opsize(ins, 0);
                self.wr(ins, 0, size, &val)
            }
            M::Pinsrb | M::Pinsrd | M::Pinsrq => {
                let (bytes, field, t, lane) = match m {
                    M::Pinsrb => (1, "b", "u8", imm & 15),
                    M::Pinsrd => (4, "d", "u32", imm & 3),
                    _ => (8, "q", "u64", imm & 1),
                };
                let src = match ins.op1_kind() {
                    OpKind::Register => self.rd(ins, 1, ins.op_register(1).size() as u32)?,
                    _ => self.rd(ins, 1, bytes)?,
                };
                Some(format!("X({}).{field}[{lane}] = ({t}){src};", dst?))
            }
            M::Insertps => {
                let src = match self.xmm(ins, 1) {
                    Some(s) => format!("X({s}).d[{}]", (imm >> 6) & 3),
                    None => format!("rd32({})", self.ea(ins)?),
                };
                let d = dst?;
                Some(format!("X({d}) = rcn_insertps(X({d}), {src}, 0x{imm:x});"))
            }
            M::Pcmpestri | M::Pcmpestrm | M::Pcmpistri | M::Pcmpistrm | M::Pcmpestri64 | M::Pcmpestrm64 => {
                let explicit = matches!(m, M::Pcmpestri | M::Pcmpestrm | M::Pcmpestri64 | M::Pcmpestrm64);
                let mask = matches!(m, M::Pcmpestrm | M::Pcmpistrm | M::Pcmpestrm64);
                let wide = matches!(m, M::Pcmpestri64 | M::Pcmpestrm64);
                let info = (ins.immediate(2) as u32 & 0xFF) | (explicit as u32) << 8 | (mask as u32) << 9 | (wide as u32) << 10;
                Some(format!("rcn_pcmpstr(ctx, X({}), {}, 0x{info:x});", dst?, self.x128(ins, 1)?))
            }
            M::Ptest => Some(format!(
                "u32 t = rcn_ptest(X({}), {}); ctx->zf = t & 1; ctx->cf = t >> 1; ctx->of = 0; ctx->sf = 0; ctx->af = 0; ctx->pf = 0;",
                dst?,
                self.x128(ins, 1)?
            )),
            _ => None,
        }
    }

    /// Unité flottante x87 : pile de huit registres (ST(0) au sommet), tenus en double précision —
    /// la précision que le CRT de Windows impose par défaut, et non les 80 bits du matériel.
    fn x87(&self, ins: &Instruction) -> Option<String> {
        use iced_x86::MemorySize as S;
        let m = ins.mnemonic();
        let is_mem = ins.op_count() > 0 && ins.op0_kind() == OpKind::Memory;
        // Registre ST(i) d'un opérande.
        let st = |i: u32| -> Option<String> {
            let reg = ins.op_register(i);
            (ins.op_kind(i) == OpKind::Register && reg >= Register::ST0 && reg <= Register::ST7)
                .then(|| format!("ST({})", reg.number()))
        };
        // Valeur d'un opérande mémoire (flottant ou entier), en double.
        let load = || -> Option<String> {
            let ea = self.ea(ins)?;
            Some(match ins.memory_size() {
                S::Float32 => format!("(double)rdf32({ea})"),
                S::Float64 => format!("rdf64({ea})"),
                S::Float80 => format!("rcn_ld80({ea})"),
                S::Int16 => format!("(double)(i16)rd16({ea})"),
                S::Int32 => format!("(double)(i32)rd32({ea})"),
                S::Int64 => format!("(double)(i64)rd64({ea})"),
                _ => return None,
            })
        };
        // Second terme d'une opération ou d'une comparaison : mémoire, ST(i) (dernier opérande), ou ST(1).
        let other = || -> Option<String> {
            match ins.op_count() {
                0 => Some("ST(1)".into()),
                _ if is_mem => load(),
                n => st(n - 1),
            }
        };
        let arith = |op: &str, reversed: bool, pop: bool| -> Option<String> {
            let (dst, src) = if is_mem { ("ST(0)".to_string(), load()?) } else { (st(0)?, st(1)?) };
            let expr = if reversed { format!("{src} {op} {dst}") } else { format!("{dst} {op} {src}") };
            Some(format!("{dst} = {expr};{}", if pop { " rcn_fpop(ctx);" } else { "" }))
        };
        let compare = |pops: u32| -> Option<String> {
            Some(format!("rcn_fcom(ctx, ST(0), {});{}", other()?, " rcn_fpop(ctx);".repeat(pops as usize)))
        };
        // fcomi : résultat dans les drapeaux du processeur, comme comisd.
        let compare_flags = |pop: bool| -> Option<String> {
            Some(format!(
                "double a = ST(0), b = {}; u8 un = (a != a) | (b != b); ctx->zf = un | (a == b); ctx->pf = un; ctx->cf = un | (a < b); ctx->of = 0; ctx->sf = 0; ctx->af = 0;{}",
                other()?,
                if pop { " rcn_fpop(ctx);" } else { "" }
            ))
        };
        let store_int = |round: &str, pop: bool| -> Option<String> {
            let size = ins.memory_size().size() as u32;
            let call = format!("{round}{}(ctx, ST(0))", size * 8);
            Some(format!("wr{}({}, {call});{}", size * 8, self.ea(ins)?, if pop { " rcn_fpop(ctx);" } else { "" }))
        };
        let unary = |expr: &str| Some(format!("ST(0) = {expr};"));
        let push = |expr: &str| Some(format!("rcn_fpush(ctx, {expr});"));
        let cmov = |c: &str| Some(format!("if ({c}) ST(0) = {};", other()?));
        match m {
            M::Fld if is_mem => push(&load()?),
            M::Fld => Some(format!("double v = {}; rcn_fpush(ctx, v);", st(0)?)),
            M::Fild => push(&load()?),
            M::Fst | M::Fstp => {
                let pop = if m == M::Fstp { " rcn_fpop(ctx);" } else { "" };
                if !is_mem {
                    return Some(format!("{} = ST(0);{pop}", st(0)?));
                }
                let ea = self.ea(ins)?;
                Some(match ins.memory_size() {
                    S::Float32 => format!("wrf32({ea}, (float)ST(0));{pop}"),
                    S::Float64 => format!("wrf64({ea}, ST(0));{pop}"),
                    S::Float80 => format!("rcn_st80({ea}, ST(0));{pop}"),
                    _ => return None,
                })
            }
            M::Fist => store_int("rcn_fist", false),
            M::Fistp => store_int("rcn_fist", true),
            M::Fisttp => store_int("rcn_fistt", true),
            M::Fadd | M::Fiadd => arith("+", false, false),
            M::Fsub | M::Fisub => arith("-", false, false),
            M::Fsubr | M::Fisubr => arith("-", true, false),
            M::Fmul | M::Fimul => arith("*", false, false),
            M::Fdiv | M::Fidiv => arith("/", false, false),
            M::Fdivr | M::Fidivr => arith("/", true, false),
            M::Faddp => arith("+", false, true),
            M::Fsubp => arith("-", false, true),
            M::Fsubrp => arith("-", true, true),
            M::Fmulp => arith("*", false, true),
            M::Fdivp => arith("/", false, true),
            M::Fdivrp => arith("/", true, true),
            M::Fchs => unary("-ST(0)"),
            M::Fabs => unary("fabs(ST(0))"),
            M::Fsqrt => unary("sqrt(ST(0))"),
            M::Fsin => unary("sin(ST(0))"),
            M::Fcos => unary("cos(ST(0))"),
            M::Frndint => unary("rcn_fround(ctx, ST(0))"),
            M::F2xm1 => unary("exp2(ST(0)) - 1.0"),
            M::Fsincos => Some("double v = ST(0); ST(0) = sin(v); rcn_fpush(ctx, cos(v));".into()),
            M::Fptan => Some("ST(0) = tan(ST(0)); rcn_fpush(ctx, 1.0);".into()),
            M::Fpatan => Some("ST(1) = atan2(ST(1), ST(0)); rcn_fpop(ctx);".into()),
            M::Fyl2x => Some("ST(1) = ST(1) * log2(ST(0)); rcn_fpop(ctx);".into()),
            M::Fyl2xp1 => Some("ST(1) = ST(1) * log2(ST(0) + 1.0); rcn_fpop(ctx);".into()),
            M::Fscale => unary("scalbn(ST(0), (int)rcn_fistt32(ctx, ST(1)))"),
            // Reste partiel : calculé d'un coup (C2 à zéro : terminé).
            M::Fprem => Some("ST(0) = fmod(ST(0), ST(1)); ctx->fp_sw &= ~0x0400;".into()),
            M::Fprem1 => Some("ST(0) = remainder(ST(0), ST(1)); ctx->fp_sw &= ~0x0400;".into()),
            M::Fxch => Some(format!("double v = ST(0); ST(0) = {0}; {0} = v;", other()?)),
            M::Fld1 => push("1.0"),
            M::Fldz => push("0.0"),
            M::Fldpi => push("3.14159265358979323846"),
            M::Fldl2e => push("1.44269504088896340736"),
            M::Fldl2t => push("3.32192809488736234787"),
            M::Fldlg2 => push("0.30102999566398119521"),
            M::Fldln2 => push("0.69314718055994530942"),
            M::Fcom | M::Fucom | M::Ficom => compare(0),
            M::Fcomp | M::Fucomp | M::Ficomp => compare(1),
            M::Fcompp | M::Fucompp => compare(2),
            M::Ftst => Some("rcn_fcom(ctx, ST(0), 0.0);".into()),
            M::Fxam => Some("rcn_fxam(ctx);".into()),
            M::Fcomi | M::Fucomi => compare_flags(false),
            M::Fcomip | M::Fucomip => compare_flags(true),
            M::Fcmovb => cmov("ctx->cf"),
            M::Fcmove => cmov("ctx->zf"),
            M::Fcmovbe => cmov("ctx->cf | ctx->zf"),
            M::Fcmovu => cmov("ctx->pf"),
            M::Fcmovnb => cmov("!ctx->cf"),
            M::Fcmovne => cmov("!ctx->zf"),
            M::Fcmovnbe => cmov("!(ctx->cf | ctx->zf)"),
            M::Fcmovnu => cmov("!ctx->pf"),
            M::Fnstsw => self.wr(ins, 0, 2, "rcn_fsw(ctx)"),
            M::Fnstcw => self.wr(ins, 0, 2, "ctx->fp_cw"),
            M::Fldcw => Some(format!("ctx->fp_cw = {};", self.rd(ins, 0, 2)?)),
            M::Fninit => Some("ctx->fp_cw = 0x037F; ctx->fp_sw = 0; ctx->fp_top = 0;".into()),
            M::Fnstenv | M::Fldenv | M::Fnsave | M::Frstor => {
                let which = match m { M::Fnstenv => 0, M::Fldenv => 1, M::Fnsave => 2, _ => 3 };
                Some(format!("rcn_op_fenv(ctx, {}, {which});", self.ea(ins)?))
            }
            M::Fnclex | M::Ffree | M::Fnop => Some(String::new()),
            M::Fincstp => Some("rcn_fpop(ctx);".into()),
            M::Fdecstp => Some("ctx->fp_top = (ctx->fp_top - 1) & 7;".into()),
            _ => None,
        }
    }

    /// Corps C d'une instruction, ou None si elle n'est pas gérée.
    fn translate(&self, f: &Func, ins: &Instruction, rva: u32, need: u32) -> Option<String> {
        let m = ins.mnemonic();
        let next = rva + ins.len() as u32;
        // Opérations sur chaînes (movs, stos…) : même mnémonique que certaines instructions SSE.
        let is_string = matches!(
            ins.op0_kind(),
            OpKind::MemoryESRDI | OpKind::MemoryESEDI | OpKind::MemorySegRSI | OpKind::MemorySegESI
        ) || matches!(ins.op1_kind(), OpKind::MemorySegRSI | OpKind::MemoryESRDI | OpKind::MemorySegESI | OpKind::MemoryESEDI);
        if is_string {
            let size = ins.memory_size().size();
            let rep = if ins.has_repne_prefix() {
                2
            } else if ins.has_rep_prefix() || ins.has_repe_prefix() {
                1
            } else {
                0
            };
            let name = match m {
                M::Movsb | M::Movsw | M::Movsd | M::Movsq => "movs",
                M::Stosb | M::Stosw | M::Stosd | M::Stosq => "stos",
                M::Lodsb | M::Lodsw | M::Lodsd | M::Lodsq => "lods",
                M::Cmpsb | M::Cmpsw | M::Cmpsd | M::Cmpsq => "cmps",
                M::Scasb | M::Scasw | M::Scasd | M::Scasq => "scas",
                _ => return None,
            };
            return Some(format!("rcn_{name}(ctx, {size}, {rep});"));
        }
        if NOPS.contains(&m) {
            return Some(String::new());
        }
        if matches!(m, M::Loope | M::Loopne) {
            return None;
        }
        if let Some(c) = cond(ins.condition_code()) {
            if ins.flow_control() == FlowControl::ConditionalBranch {
                return Some(format!("if ({c}) {}", self.jump(f, ins, rva)));
            }
            if ins.op_count() == 1 {
                // setcc
                return self.wr(ins, 0, 1, c);
            }
            // cmovcc : une destination 32 bits est étendue par des zéros même si la condition est fausse.
            let size = self.opsize(ins, 0);
            let (a, b) = (self.rd(ins, 0, size)?, self.rd(ins, 1, size)?);
            return Some(format!("{} v = {c} ? {b} : {a}; {}", ty(size), self.wr(ins, 0, size, "v")?));
        }
        let size0 = if ins.op_count() > 0 { self.opsize(ins, 0) } else { 0 };
        // Taille d'une adresse : octets, bits, type C.
        let ps = self.pe.bits / 8;
        let (p, pt) = (self.pe.bits, ty(ps));
        match m {
            M::Mov | M::Movnti => {
                let v = self.rd(ins, 1, size0)?;
                Some(format!("{} v = {v}; {}", ty(size0), self.wr(ins, 0, size0, "v")?))
            }
            M::Movzx => {
                let v = self.rd(ins, 1, self.opsize(ins, 1))?;
                self.wr(ins, 0, size0, &v)
            }
            M::Movsx | M::Movsxd => {
                let src = self.opsize(ins, 1);
                let v = format!("({})({}){}", sty(size0), sty(src), self.rd(ins, 1, src)?);
                self.wr(ins, 0, size0, &v)
            }
            M::Lea => self.wr(ins, 0, size0, &self.ea(ins)?),
            M::Add => self.alu(ins, need, Alu::Add),
            M::Adc => self.alu(ins, need, Alu::Adc),
            M::Sub => self.alu(ins, need, Alu::Sub),
            M::Sbb => self.alu(ins, need, Alu::Sbb),
            M::Cmp => self.alu(ins, need, Alu::Cmp),
            M::And => self.alu(ins, need, Alu::And),
            M::Test => self.alu(ins, need, Alu::Test),
            M::Or => self.alu(ins, need, Alu::Or),
            M::Xor => self.alu(ins, need, Alu::Xor),
            M::Inc | M::Dec => {
                let (t, n) = (ty(size0), size0 * 8);
                let a = self.rd(ins, 0, size0)?;
                let (op, of) = if m == M::Inc {
                    ("+", format!("((~a & r) >> {}) & 1", n - 1))
                } else {
                    ("-", format!("((a & ~r) >> {}) & 1", n - 1))
                };
                if self.locked(ins) {
                    let fetch = if m == M::Inc { "add" } else { "sub" };
                    let ptr = self.atomic_ptr(ins, size0)?;
                    let mut s = format!("{t} a = __atomic_fetch_{fetch}({ptr}, 1, __ATOMIC_SEQ_CST), r; r = a {op} 1;");
                    s += &fl(need, F::OF, "of", &of);
                    s += &fl(need, F::AF, "af", "((a ^ r) >> 4) & 1");
                    return Some(s + &fl_zsp(need, n));
                }
                let mut s = format!("{t} a = {a}, r; r = a {op} 1;");
                s += &fl(need, F::OF, "of", &of);
                s += &fl(need, F::AF, "af", "((a ^ r) >> 4) & 1");
                s += &fl_zsp(need, n);
                Some(s + " " + &self.wr(ins, 0, size0, "r")?)
            }
            M::Neg | M::Not | M::Shl | M::Shr | M::Sar | M::Rol | M::Ror if self.locked(ins) => None,
            M::Neg => {
                let (t, n) = (ty(size0), size0 * 8);
                let mut s = format!("{t} a = {}, r; r = ({t})(0 - a);", self.rd(ins, 0, size0)?);
                s += &fl(need, F::CF, "cf", "a != 0");
                s += &fl(need, F::OF, "of", &format!("((a & r) >> {}) & 1", n - 1));
                s += &fl(need, F::AF, "af", "((a ^ r) >> 4) & 1");
                s += &fl_zsp(need, n);
                Some(s + " " + &self.wr(ins, 0, size0, "r")?)
            }
            M::Not => {
                let v = format!("~{}", self.rd(ins, 0, size0)?);
                self.wr(ins, 0, size0, &v)
            }
            M::Shl | M::Sal | M::Shr | M::Sar | M::Rol | M::Ror => self.shift(ins, need),
            M::Shld | M::Shrd => self.shift_double(ins, need),
            M::Imul | M::Mul | M::Div | M::Idiv => self.mul_div(ins, need, rva),
            // Pile : cases de 8 octets en 64 bits, de 4 en 32 bits (P : taille d'une adresse en bits).
            M::Push => {
                let v = match ins.op0_kind() {
                    k if is_imm(k) => format!("0x{:x}ULL", ins.immediate(0)),
                    _ if size0 == ps => self.rd(ins, 0, ps)?,
                    _ => return None,
                };
                Some(format!("u64 v = {v}; PUSH{p}(v);"))
            }
            M::Pop => {
                if size0 != ps {
                    return None;
                }
                Some(format!("{pt} v = rd{p}(R(4)); R(4) = ({pt})(R(4) + {ps}); {}", self.wr(ins, 0, ps, "v")?))
            }
            M::Leave => Some(format!("R(4) = R(5); R(5) = rd{p}(R(4)); R(4) = ({pt})(R(4) + {ps});")),
            M::Pushad if ps == 4 => Some(
                "u32 sp = (u32)R(4); PUSH32(R(0)); PUSH32(R(1)); PUSH32(R(2)); PUSH32(R(3)); PUSH32(sp); PUSH32(R(5)); PUSH32(R(6)); PUSH32(R(7));"
                    .into(),
            ),
            M::Popad if ps == 4 => Some(
                "u32 sp = (u32)R(4); R(7) = rd32(sp); R(6) = rd32(sp + 4); R(5) = rd32(sp + 8); R(3) = rd32(sp + 16); R(2) = rd32(sp + 20); R(1) = rd32(sp + 24); R(0) = rd32(sp + 28); R(4) = (u32)(sp + 32);"
                    .into(),
            ),
            M::Pushfd | M::Pushfq => Some(format!("PUSH{p}(rcn_get_flags(ctx));")),
            M::Popfd | M::Popfq => Some(format!("rcn_set_flags(ctx, rd{p}(R(4))); R(4) = ({pt})(R(4) + {ps});")),
            // ah ↔ drapeaux (comparaisons x87 : « fnstsw ax ; sahf »).
            M::Sahf => Some("rcn_set_flags(ctx, (rcn_get_flags(ctx) & ~0xD5u) | ((R(0) >> 8) & 0xD5));".into()),
            M::Lahf => Some("R(0) = (R(0) & ~0xFF00ULL) | ((rcn_get_flags(ctx) & 0xD7) << 8);".into()),
            M::Call => {
                let ret = format!("PUSH{p}(ctx->base + 0x{next:x}ULL);");
                // setjmp : le point de reprise est pris par le setjmp de l'hôte, dans cette fonction C
                // (celle qui reste active, comme la fonction du jeu). longjmp y revient avec les
                // registres du jeu rétablis et rax non nul. Son argument : rcx, ou le sommet de la pile en 32 bits.
                let direct = branch_target(self.pe, ins);
                let slot = if ins.op0_kind() == OpKind::Memory { mem_target(self.pe, ins) } else { None };
                if direct.is_some_and(|t| self.setjmp_thunks.contains(&t)) || slot.is_some_and(|s| is_setjmp(self.pe, s)) {
                    let buf = if ps == 4 { "rd32(R(4))" } else { "R(1)" };
                    return Some(format!("if (setjmp(*rcn_setjmp(ctx, {buf})) == 0) R(0) = 0;"));
                }
                if is_near_branch(ins) {
                    match direct {
                        Some(t) if self.funcs.contains(&t) => Some(format!("{ret} {}(ctx);", Self::func_name(t))),
                        _ => Some(format!("rcn_trap(ctx, 0x{rva:x}, \"appel hors du code\");")),
                    }
                } else {
                    Some(format!("u64 t = {}; {ret} rcn_call(ctx, t);", self.rd(ins, 0, ps)?))
                }
            }
            M::Ret => {
                let extra = if ins.op_count() == 1 { ins.immediate(0) } else { 0 };
                Some(format!("R(4) = ({pt})(R(4) + {}); return;", ps as u64 + extra))
            }
            M::Jmp => {
                if is_near_branch(ins) {
                    return Some(self.jump(f, ins, rva));
                }
                let mut s = format!("u64 t = {};", self.rd(ins, 0, ps)?);
                if let Some(targets) = f.jump_tables.get(&rva) {
                    s += " switch (t - ctx->base) {";
                    for t in targets {
                        write!(s, " case 0x{t:x}: goto L_{t:08X};").unwrap();
                    }
                    s += " }";
                }
                // Hors table : saut terminal vers une autre fonction ou un import.
                Some(s + " rcn_call(ctx, t); return;")
            }
            M::Jrcxz => Some(format!("if (R(1) == 0) {}", self.jump(f, ins, rva))),
            M::Jecxz => Some(format!("if ((u32)R(1) == 0) {}", self.jump(f, ins, rva))),
            M::Loop => Some(format!("R(1) = ({pt})(R(1) - 1); if (R(1) != 0) {}", self.jump(f, ins, rva))),
            M::Cbw => Some("R(0) = (R(0) & ~0xFFFFULL) | (u16)(i16)(i8)R(0);".into()),
            M::Cwde => Some("R(0) = (u32)(i32)(i16)R(0);".into()),
            M::Cdqe => Some("R(0) = (u64)(i64)(i32)R(0);".into()),
            M::Cwd => Some("R(2) = (R(2) & ~0xFFFFULL) | (u16)((i16)R(0) >> 15);".into()),
            M::Cdq => Some("R(2) = (u32)((i32)R(0) >> 31);".into()),
            M::Cqo => Some("R(2) = (u64)((i64)R(0) >> 63);".into()),
            M::Xchg if ins.op0_kind() == OpKind::Memory || ins.op1_kind() == OpKind::Memory => {
                // Avec la mémoire, xchg est toujours atomique (verrou implicite).
                let reg = if ins.op0_kind() == OpKind::Memory { 1 } else { 0 };
                let (t, ptr) = (ty(size0), self.atomic_ptr(ins, size0)?);
                Some(format!(
                    "{t} a = __atomic_exchange_n({ptr}, {}, __ATOMIC_SEQ_CST); {}",
                    self.rd(ins, reg, size0)?,
                    self.wr(ins, reg, size0, "a")?
                ))
            }
            M::Xchg => {
                let (a, b) = (self.rd(ins, 0, size0)?, self.rd(ins, 1, size0)?);
                let t = ty(size0);
                Some(format!(
                    "{t} a = {a}, b = {b}; {} {}",
                    self.wr(ins, 0, size0, "b")?,
                    self.wr(ins, 1, size0, "a")?
                ))
            }
            M::Xadd => {
                let (t, n) = (ty(size0), size0 * 8);
                let locked = self.locked(ins);
                let mut s = if locked {
                    format!(
                        "{t} b = {}, a = __atomic_fetch_add({}, b, __ATOMIC_SEQ_CST), r; r = a + b;",
                        self.rd(ins, 1, size0)?,
                        self.atomic_ptr(ins, size0)?
                    )
                } else {
                    format!("{t} a = {}, b = {}, r; r = a + b;", self.rd(ins, 0, size0)?, self.rd(ins, 1, size0)?)
                };
                s += &fl(need, F::CF, "cf", "r < a");
                s += &fl(need, F::OF, "of", &format!("(((a ^ r) & (b ^ r)) >> {}) & 1", n - 1));
                s += &fl(need, F::AF, "af", "((a ^ b ^ r) >> 4) & 1");
                s += &fl_zsp(need, n);
                if locked {
                    return Some(format!("{s} {}", self.wr(ins, 1, size0, "a")?));
                }
                Some(format!("{s} {} {}", self.wr(ins, 1, size0, "a")?, self.wr(ins, 0, size0, "r")?))
            }
            M::Cmpxchg => {
                // Comparaison de l'accumulateur avec la destination (a - b), puis échange.
                let (t, n) = (ty(size0), size0 * 8);
                let acc = match size0 {
                    1 => Register::AL,
                    2 => Register::AX,
                    4 => Register::EAX,
                    _ => Register::RAX,
                };
                let locked = self.locked(ins);
                let mut s = if locked {
                    // b reçoit la valeur lue en mémoire ; égale à l'accumulateur si l'échange a eu lieu.
                    format!(
                        "{t} a = {}, b = a, r; __atomic_compare_exchange_n({}, &b, {}, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); r = a - b;",
                        rd_reg(acc)?,
                        self.atomic_ptr(ins, size0)?,
                        self.rd(ins, 1, size0)?
                    )
                } else {
                    format!("{t} a = {}, b = {}, r; r = a - b;", rd_reg(acc)?, self.rd(ins, 0, size0)?)
                };
                s += &fl(need, F::CF, "cf", "a < b");
                s += &fl(need, F::OF, "of", &format!("(((a ^ b) & (a ^ r)) >> {}) & 1", n - 1));
                s += &fl(need, F::AF, "af", "((a ^ b ^ r) >> 4) & 1");
                s += &fl_zsp(need, n);
                if locked {
                    return Some(format!("{s} if (a != b) {{ {} }}", wr_reg(acc, "b")?));
                }
                Some(format!(
                    "{s} if (a == b) {{ {} }} else {{ {} }}",
                    self.wr(ins, 0, size0, &self.rd(ins, 1, size0)?)?,
                    wr_reg(acc, "b")?
                ))
            }
            M::Bt | M::Bts | M::Btr | M::Btc => {
                // Avec un indice dans un registre, un opérande mémoire désigne une chaîne de bits : non géré.
                if ins.op0_kind() == OpKind::Memory && ins.op1_kind() == OpKind::Register {
                    return None;
                }
                let (t, n) = (ty(size0), size0 * 8);
                let idx = match ins.op1_kind() {
                    OpKind::Register => self.rd(ins, 1, size0)?,
                    _ => format!("0x{:x}", ins.immediate(1)),
                };
                if self.locked(ins) {
                    let op = match m {
                        M::Bts => format!("__atomic_fetch_or({}, ({t})1 << c, __ATOMIC_SEQ_CST)", self.atomic_ptr(ins, size0)?),
                        M::Btr => format!("__atomic_fetch_and({}, ~(({t})1 << c), __ATOMIC_SEQ_CST)", self.atomic_ptr(ins, size0)?),
                        M::Btc => format!("__atomic_fetch_xor({}, ({t})1 << c, __ATOMIC_SEQ_CST)", self.atomic_ptr(ins, size0)?),
                        _ => return None,
                    };
                    let s = format!("u8 c = ({idx}) & {}; {t} a = {op};", n - 1);
                    return Some(s + &fl(need, F::CF, "cf", "(a >> c) & 1") + " (void)a;");
                }
                let mut s = format!("{t} a = {}; u8 c = ({idx}) & {};", self.rd(ins, 0, size0)?, n - 1);
                s += &fl(need, F::CF, "cf", "(a >> c) & 1");
                let v = match m {
                    M::Bts => format!("a | (({t})1 << c)"),
                    M::Btr => format!("a & ~(({t})1 << c)"),
                    M::Btc => format!("a ^ (({t})1 << c)"),
                    _ => return Some(s + " (void)c;"),
                };
                Some(s + " " + &self.wr(ins, 0, size0, &v)?)
            }
            M::Bsf | M::Bsr | M::Tzcnt | M::Lzcnt | M::Popcnt => {
                let (t, n) = (ty(size0), size0 * 8);
                let mut s = format!("{t} a = {}, r;", self.rd(ins, 1, size0)?);
                match m {
                    M::Bsf | M::Bsr => {
                        let bit = if m == M::Bsf { "__builtin_ctzll(a)".to_string() } else { "63 - __builtin_clzll(a)".to_string() };
                        s += &fl(need, F::ZF, "zf", "a == 0");
                        return Some(format!("{s} if (a) {{ r = ({t})({bit}); {} }}", self.wr(ins, 0, size0, "r")?));
                    }
                    M::Tzcnt => {
                        s += &format!(" r = a ? ({t})__builtin_ctzll(a) : {n};");
                        s += &fl(need, F::CF, "cf", "a == 0");
                        s += &fl(need, F::ZF, "zf", "r == 0");
                    }
                    M::Lzcnt => {
                        s += &format!(" r = a ? ({t})(__builtin_clzll(a) - {}) : {n};", 64 - n);
                        s += &fl(need, F::CF, "cf", "a == 0");
                        s += &fl(need, F::ZF, "zf", "r == 0");
                    }
                    _ => {
                        s += &format!(" r = ({t})__builtin_popcountll(a);");
                        s += &fl(need, F::ZF, "zf", "a == 0");
                        for (bit, name) in [(F::CF, "cf"), (F::OF, "of"), (F::SF, "sf"), (F::AF, "af"), (F::PF, "pf")] {
                            s += &fl(need, bit, name, "0");
                        }
                    }
                }
                Some(s + " " + &self.wr(ins, 0, size0, "r")?)
            }
            M::Rcl | M::Rcr if !ins.has_lock_prefix() => {
                let count = match ins.op1_kind() {
                    OpKind::Register => self.rd(ins, 1, 1)?,
                    _ => format!("{}", ins.immediate(1)),
                };
                let info = size0 | if m == M::Rcl { 0x100 } else { 0 };
                let v = format!("rcn_op_rcx(ctx, (u64){}, {count}, {info})", self.rd(ins, 0, size0)?);
                self.wr(ins, 0, size0, &v)
            }
            M::Bswap => {
                let v = format!("__builtin_bswap{}({})", size0 * 8, self.rd(ins, 0, size0)?);
                (size0 >= 4).then(|| self.wr(ins, 0, size0, &v)).flatten()
            }
            M::Cld => Some("ctx->df = 0;".into()),
            M::Std => Some("ctx->df = 1;".into()),
            M::Clc => Some("ctx->cf = 0;".into()),
            M::Stc => Some("ctx->cf = 1;".into()),
            M::Cmc => Some("ctx->cf = !ctx->cf;".into()),
            M::Cmpxchg16b => Some(format!("rcn_op_cmpxchg16b(ctx, {});", self.ea(ins)?)),
            M::Cmpxchg8b => Some(format!("rcn_op_cmpxchg8b(ctx, {});", self.ea(ins)?)),
            M::Fxsave | M::Fxsave64 => Some(format!("rcn_op_fxsave(ctx, {});", self.ea(ins)?)),
            M::Fxrstor | M::Fxrstor64 => Some(format!("rcn_op_fxrstor(ctx, {});", self.ea(ins)?)),
            M::Cpuid => Some("rcn_cpuid(ctx);".into()),
            M::Rdtsc => Some("rcn_rdtsc(ctx);".into()),
            M::Xgetbv => Some("R(0) = 7; R(2) = 0;".into()),
            M::Stmxcsr => self.wr(ins, 0, 4, "ctx->mxcsr"),
            M::Ldmxcsr => Some(format!("ctx->mxcsr = {};", self.rd(ins, 0, 4)?)),
            M::Int3 | M::Ud2 | M::Int | M::Hlt | M::Syscall => {
                Some(format!("rcn_trap(ctx, 0x{rva:x}, \"{m:?}\");").to_lowercase())
            }
            _ => self.x87(ins).or_else(|| self.sse(ins)),
        }
    }

    pub fn emit_func(&mut self, f: &Func, out: &mut String) {
        let live = flag_liveness(self.pe, f, &|_| None);
        writeln!(out, "void {}(RcnCtx *ctx) {{", Self::func_name(f.entry)).unwrap();
        let keys: Vec<u32> = f.insts.keys().copied().collect();
        if keys.first() != Some(&f.entry) {
            // Le point d'entrée n'est pas la première instruction émise (ou n'a pas pu être décodé).
            if f.insts.contains_key(&f.entry) {
                writeln!(out, "    goto L_{:08X};", f.entry).unwrap();
            } else {
                writeln!(out, "    rcn_trap(ctx, 0x{:x}, \"code illisible\");", f.entry).unwrap();
            }
        }
        for (i, &rva) in keys.iter().enumerate() {
            let ins = &f.insts[&rva];
            if f.leaders.contains(&rva) {
                writeln!(out, "L_{rva:08X}: ;").unwrap();
            }
            self.total += 1;
            let mut text = String::new();
            self.fmt.format(ins, &mut text);
            let body = match self.translate(f, ins, rva, live.get(&rva).copied().unwrap_or(0)) {
                Some(body) => body,
                None => {
                    *self.unimplemented.entry(format!("{:?}", ins.mnemonic()).to_lowercase()).or_default() += 1;
                    format!("rcn_trap(ctx, 0x{rva:x}, \"instruction non geree : {}\");", text.replace('"', "'"))
                }
            };
            if self.comments {
                writeln!(out, "    /* {:x}  {text} */", self.pe.image_base + rva as u64).unwrap();
            }
            if !body.is_empty() {
                writeln!(out, "    {{ {body} }}").unwrap();
            }
            // Suite de l'exécution : l'instruction suivante en mémoire, si ce n'est pas la prochaine émise.
            let next = rva + ins.len() as u32;
            if falls_through(ins) && keys.get(i + 1) != Some(&next) {
                if f.insts.contains_key(&next) {
                    writeln!(out, "    goto L_{next:08X};").unwrap();
                } else if self.funcs.contains(&next) {
                    // Suite dans une autre fonction connue (fonction découpée en morceaux).
                    writeln!(out, "    {{ {}(ctx); return; }}", Self::func_name(next)).unwrap();
                } else {
                    writeln!(out, "    rcn_trap(ctx, 0x{next:x}, \"fin du code traduit\");").unwrap();
                }
            }
        }
        out.push_str("}\n\n");
    }
}
