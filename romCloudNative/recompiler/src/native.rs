//! Traduction directe des fonctions x86 en code machine de l'hôte (ARM64 sur le téléphone, x86-64 sur
//! PC pour les tests), avec Cranelift : aucun compilateur C n'est nécessaire.
//!
//! Même modèle que le générateur C (emit.rs), dont ce module reprend la sémantique instruction par
//! instruction : une fonction de l'hôte « fn(ctx) » par fonction x86, registres du jeu dans `RcnCtx`.
//! Les registres généraux sont gardés dans des variables (registres de l'hôte) et recopiés dans `ctx` à
//! chaque écriture ; ils sont relus après chaque appel. Les drapeaux restent dans des variables et ne
//! sont rangés dans `ctx` qu'avant les appels et les retours. Les opérations rares ou longues (SSE sur
//! 128 bits, fonctions mathématiques du x87, divisions, chaînes…) appellent des fonctions du runtime.
//!
//! Le résultat (fichier .rcnx, voir `write_rcnx`) contient le code des fonctions et les endroits à
//! compléter au chargement : appels entre fonctions et adresses des fonctions du runtime.

use crate::emit::is_setjmp;
use crate::func::{branch_target, falls_through, flag_liveness, is_near_branch, mem_target, Func};
use crate::pe::Pe;
use cranelift_codegen::control::ControlPlane;
use cranelift_codegen::ir::condcodes::{FloatCC, IntCC};
use cranelift_codegen::ir::types::{self, Type};
use cranelift_codegen::ir::{
    AbiParam, AtomicRmwOp, Block, ExtFuncData, ExternalName, FuncRef, Function, InstBuilder, MemFlagsData, Signature,
    StackSlotData, StackSlotKind, UserExternalName, UserFuncName, Value,
};
use cranelift_codegen::isa::{CallConv, OwnedTargetIsa};
use cranelift_codegen::settings::{self, Configurable};
use cranelift_codegen::{binemit::Reloc, Context, RelocTarget};
use cranelift_frontend::{FunctionBuilder, FunctionBuilderContext, Switch, Variable};
use iced_x86::{ConditionCode, FlowControl, Formatter, Instruction, IntelFormatter, MemorySize, Mnemonic as M, OpKind, Register, RflagsBits as F};
use std::collections::{BTreeMap, BTreeSet, HashMap};

// Disposition de RcnCtx (rcn.h, vérifiée là par des assertions à la compilation).
const OFF_R: i32 = 0;
const OFF_XMM: i32 = 128;
const OFF_FLAGS: i32 = 384; // cf, pf, af, zf, sf, of
const OFF_DF: i32 = 390;
const OFF_MXCSR: i32 = 392;
const OFF_BASE: i32 = 400;
const OFF_FS: i32 = 416;
const OFF_GS: i32 = 424;
const OFF_ST: i32 = 440;
const OFF_FPTOP: i32 = 504;
const OFF_FPSW: i32 = 506;
const OFF_FPCW: i32 = 508;
const OFF_UNWIND: i32 = 512;
const OFF_UNWIND_SITE: i32 = 520;

const CF: usize = 0;
const PF: usize = 1;
const AF: usize = 2;
const ZF: usize = 3;
const SF: usize = 4;
const OF: usize = 5;
const FLAG_BITS: [u32; 6] = [F::CF, F::PF, F::AF, F::ZF, F::SF, F::OF];

/// Espaces de noms des appels : fonctions traduites (indice : RVA) et fonctions du runtime.
const NS_FUNC: u32 = 0;
const NS_HELPER: u32 = 1;

/// Types de relocalisations du fichier .rcnx.
pub const RELOC_ABS8: u8 = 1;
pub const RELOC_PCREL4: u8 = 2;
pub const RELOC_ARM64_CALL: u8 = 3;

const I8: Type = types::I8;
const I16: Type = types::I16;
const I32: Type = types::I32;
const I64: Type = types::I64;
const F32: Type = types::F32;
const F64: Type = types::F64;

fn ty(size: u32) -> Type {
    match size {
        1 => I8,
        2 => I16,
        4 => I32,
        _ => I64,
    }
}
fn mask(size: u32) -> u64 {
    if size >= 8 {
        u64::MAX
    } else {
        (1u64 << (size * 8)) - 1
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

const PACKED_BINARY: &[M] = &[
    M::Addps, M::Subps, M::Mulps, M::Divps, M::Minps, M::Maxps, M::Addpd, M::Subpd, M::Mulpd, M::Divpd, M::Minpd,
    M::Maxpd, M::Andps, M::Andpd, M::Pand, M::Orps, M::Orpd, M::Por, M::Xorps, M::Xorpd, M::Pxor, M::Andnps,
    M::Andnpd, M::Pandn, M::Paddb, M::Paddw, M::Paddd, M::Paddq, M::Psubb, M::Psubw, M::Psubd, M::Psubq,
    M::Pcmpeqb, M::Pcmpeqw, M::Pcmpeqd, M::Pcmpgtb, M::Pcmpgtw, M::Pcmpgtd, M::Pmullw, M::Pmulld, M::Pmuludq,
    M::Punpcklbw, M::Punpcklwd, M::Punpckldq, M::Punpcklqdq, M::Punpckhbw, M::Punpckhwd, M::Punpckhdq,
    M::Punpckhqdq, M::Unpcklps, M::Unpckhps, M::Unpcklpd, M::Unpckhpd, M::Pminub, M::Pmaxub, M::Pminsw,
    M::Pmaxsw, M::Packuswb, M::Packsswb, M::Packssdw, M::Movlhps, M::Movhlps, M::Paddusb, M::Psubusb, M::Pavgb,
    M::Pmaddwd, M::Haddps,
];
const PACKED_UNARY: &[M] = &[
    M::Sqrtps, M::Sqrtpd, M::Cvtdq2ps, M::Cvtps2dq, M::Cvttps2dq, M::Cvtpd2ps, M::Cvttpd2dq, M::Cvtdq2pd, M::Cvtps2pd,
    M::Movddup,
];
const PACKED_SHIFT: &[M] = &[
    M::Psllw, M::Pslld, M::Psllq, M::Psrlw, M::Psrld, M::Psrlq, M::Psraw, M::Psrad, M::Pslldq, M::Psrldq,
];
const MOVE_128: &[M] = &[
    M::Movaps, M::Movups, M::Movapd, M::Movupd, M::Movdqa, M::Movdqu, M::Lddqu, M::Movntdq, M::Movntps, M::Movntpd,
];
const NOPS: &[M] = &[
    M::Nop, M::Pause, M::Endbr64, M::Prefetchnta, M::Prefetcht0, M::Prefetcht1, M::Prefetcht2, M::Prefetchw,
    M::Lfence, M::Mfence, M::Sfence, M::Wait, M::Emms,
];

/// Fonctions mathématiques du runtime (rcn_op_math1 / rcn_op_math2), numérotées comme dans rcn_ops.c.
enum Math1 {
    Sin = 0,
    Cos,
    Tan,
    _Log2,
    Exp2m1,
}
enum Math2 {
    Atan2 = 0,
    Fmod,
    Remainder,
    Scalbn,
    Yl2x,
    Yl2xp1,
}

/// Code d'une fonction traduite, avant assemblage.
struct CompiledFunc {
    rva: u32,
    code: Vec<u8>,
    alignment: u32,
    relocs: Vec<(u32, u8, u32, u32, i64)>, // décalage, type, espace de noms, indice, ajout
}

pub struct Native<'a> {
    pe: &'a Pe,
    funcs: &'a BTreeSet<u32>,
    setjmp_thunks: &'a BTreeSet<u32>,
    isa: OwnedTargetIsa,
    pub helper_names: Vec<String>,
    helper_index: HashMap<String, u32>,
    pub messages: Vec<String>,
    message_index: HashMap<String, u32>,
    compiled: Vec<CompiledFunc>,
    pub unimplemented: BTreeMap<String, usize>,
    pub total: usize,
    arch: u32,
}

/// Architecture visée : celle de la machine (None) ou « aarch64 » / « x86_64 ».
pub fn make_isa(target: Option<&str>) -> Result<(OwnedTargetIsa, u32), String> {
    let mut flags = settings::builder();
    let set = |b: &mut settings::Builder, k: &str, v: &str| b.set(k, v).map_err(|e| format!("réglage {k} : {e}"));
    set(&mut flags, "opt_level", "speed")?;
    set(&mut flags, "is_pic", "false")?;
    set(&mut flags, "enable_probestack", "false")?;
    set(&mut flags, "enable_verifier", if cfg!(debug_assertions) { "true" } else { "false" })?;
    let flags = settings::Flags::new(flags);
    let builder = match target {
        None => cranelift_native::builder().map_err(|e| format!("processeur de la machine non géré : {e}"))?,
        Some(name) => {
            let triple = match name {
                "aarch64" | "arm64" => "aarch64-unknown-linux-gnu",
                "x86_64" | "x64" => "x86_64-unknown-linux-gnu",
                other => return Err(format!("architecture inconnue : {other}")),
            };
            cranelift_codegen::isa::lookup_by_name(triple).map_err(|e| format!("{e}"))?
        }
    };
    let isa = builder.finish(flags).map_err(|e| format!("{e}"))?;
    let arch = match isa.name() {
        "x64" => 1,
        "aarch64" => 2,
        other => return Err(format!("architecture non gérée : {other}")),
    };
    Ok((isa, arch))
}

impl<'a> Native<'a> {
    pub fn new(pe: &'a Pe, funcs: &'a BTreeSet<u32>, setjmp_thunks: &'a BTreeSet<u32>, isa: OwnedTargetIsa, arch: u32) -> Self {
        Native {
            pe,
            funcs,
            setjmp_thunks,
            isa,
            helper_names: Vec::new(),
            helper_index: HashMap::new(),
            messages: Vec::new(),
            message_index: HashMap::new(),
            compiled: Vec::new(),
            unimplemented: BTreeMap::new(),
            total: 0,
            arch,
        }
    }

    fn helper_id(&mut self, name: &str) -> u32 {
        if let Some(&i) = self.helper_index.get(name) {
            return i;
        }
        let i = self.helper_names.len() as u32;
        self.helper_names.push(name.to_string());
        self.helper_index.insert(name.to_string(), i);
        i
    }
    fn message_id(&mut self, text: &str) -> u32 {
        if let Some(&i) = self.message_index.get(text) {
            return i;
        }
        let i = self.messages.len() as u32;
        self.messages.push(text.to_string());
        self.message_index.insert(text.to_string(), i);
        i
    }

    fn signature(&self) -> Signature {
        let mut sig = Signature::new(CallConv::triple_default(self.isa.triple()));
        sig.params.push(AbiParam::new(I64));
        sig
    }

    /// Traduit une fonction. Une erreur de Cranelift est rendue en message (fonction remplacée par un arrêt).
    pub fn translate(&mut self, f: &Func) -> Result<(), String> {
        let mut func = Function::with_name_signature(UserFuncName::user(NS_FUNC, f.entry), self.signature());
        let mut fb_ctx = FunctionBuilderContext::new();
        let frontend = self.isa.frontend_config();
        {
            let builder = FunctionBuilder::new(&mut func, &mut fb_ctx);
            let mut tr = Tr::new(self, builder, f);
            tr.body();
            tr.b.seal_all_blocks();
            tr.b.finalize(frontend);
        }
        let names = func.params.user_named_funcs().clone();
        let mut ctx = Context::for_function(func);
        let code = ctx
            .compile(&*self.isa, &mut ControlPlane::default())
            .map_err(|e| format!("sub_{:08X} : {:?}", f.entry, e.inner))?;
        let mut relocs = Vec::new();
        for r in code.buffer.relocs() {
            let kind = match r.kind {
                Reloc::Abs8 => RELOC_ABS8,
                Reloc::X86CallPCRel4 | Reloc::X86PCRel4 | Reloc::X86CallPLTRel4 => RELOC_PCREL4,
                Reloc::Arm64Call => RELOC_ARM64_CALL,
                other => return Err(format!("sub_{:08X} : relocalisation non gérée {other:?}", f.entry)),
            };
            let (ns, index) = match &r.target {
                RelocTarget::ExternalName(ExternalName::User(user)) => {
                    let name = &names[*user];
                    (name.namespace, name.index)
                }
                RelocTarget::ExternalName(ExternalName::LibCall(libcall)) => {
                    (NS_HELPER, self.helper_id(&format!("libcall:{libcall:?}")))
                }
                other => return Err(format!("sub_{:08X} : cible de relocalisation non gérée {other:?}", f.entry)),
            };
            relocs.push((r.offset, kind, ns, index, r.addend));
        }
        self.compiled.push(CompiledFunc {
            rva: f.entry,
            code: code.code_buffer().to_vec(),
            alignment: code.buffer.min_alignment.max(16),
            relocs,
        });
        Ok(())
    }

    /// Fonction remplacée par un arrêt (traduction impossible).
    pub fn translate_failed(&mut self, f: &Func, why: &str) -> Result<(), String> {
        let empty = Func {
            entry: f.entry,
            range: None,
            insts: BTreeMap::new(),
            leaders: BTreeSet::new(),
            jump_tables: HashMap::new(),
            refs: Vec::new(),
        };
        let message = format!("traduction impossible : {why}");
        let mut func = Function::with_name_signature(UserFuncName::user(NS_FUNC, f.entry), self.signature());
        let mut fb_ctx = FunctionBuilderContext::new();
        let frontend = self.isa.frontend_config();
        {
            let builder = FunctionBuilder::new(&mut func, &mut fb_ctx);
            let mut tr = Tr::new(self, builder, &empty);
            tr.start();
            let rva = f.entry;
            tr.trap(rva, &message);
            tr.b.seal_all_blocks();
            tr.b.finalize(frontend);
        }
        let names = func.params.user_named_funcs().clone();
        let mut ctx = Context::for_function(func);
        let code = ctx.compile(&*self.isa, &mut ControlPlane::default()).map_err(|e| format!("{:?}", e.inner))?;
        let mut relocs = Vec::new();
        for r in code.buffer.relocs() {
            if let RelocTarget::ExternalName(ExternalName::User(user)) = &r.target {
                let name = &names[*user];
                let kind = if r.kind == Reloc::Abs8 { RELOC_ABS8 } else if r.kind == Reloc::Arm64Call { RELOC_ARM64_CALL } else { RELOC_PCREL4 };
                relocs.push((r.offset, kind, name.namespace, name.index, r.addend));
            }
        }
        self.compiled.push(CompiledFunc { rva: f.entry, code: code.code_buffer().to_vec(), alignment: 16, relocs });
        Ok(())
    }

    /// Fichier .rcnx (petit-boutiste) :
    ///   "RCNX", version, architecture (1 x86-64, 2 ARM64), bits du jeu, adresse de chargement prévue (u64),
    ///   taille de l'image, date, point d'entrée, nombre de fonctions, de fonctions du runtime, de messages,
    ///   taille du code, nombre de relocalisations ; nom de l'exe ; noms des fonctions du runtime ;
    ///   messages ; fonctions (RVA, décalage) ; relocalisations (décalage, type, espace, indice, ajout) ; code.
    pub fn write_rcnx(&mut self, exe_name: &str) -> Vec<u8> {
        self.compiled.sort_by_key(|c| c.rva);
        let mut code: Vec<u8> = Vec::new();
        let mut table = Vec::new();
        let mut relocs = Vec::new();
        for c in &self.compiled {
            let align = c.alignment as usize;
            while code.len() % align != 0 {
                code.push(0);
            }
            let offset = code.len() as u32;
            table.push((c.rva, offset));
            for &(at, kind, ns, index, addend) in &c.relocs {
                relocs.push((offset + at, kind, ns, index, addend));
            }
            code.extend_from_slice(&c.code);
        }
        let mut out = Vec::new();
        let u32le = |out: &mut Vec<u8>, v: u32| out.extend_from_slice(&v.to_le_bytes());
        let text = |out: &mut Vec<u8>, s: &str| {
            out.extend_from_slice(&(s.len() as u32).to_le_bytes());
            out.extend_from_slice(s.as_bytes());
        };
        out.extend_from_slice(b"RCNX");
        u32le(&mut out, 1);
        u32le(&mut out, self.arch);
        u32le(&mut out, self.pe.bits);
        out.extend_from_slice(&self.pe.image_base.to_le_bytes());
        u32le(&mut out, self.pe.size_of_image);
        u32le(&mut out, self.pe.timestamp);
        u32le(&mut out, self.pe.entry_rva);
        u32le(&mut out, table.len() as u32);
        u32le(&mut out, self.helper_names.len() as u32);
        u32le(&mut out, self.messages.len() as u32);
        u32le(&mut out, code.len() as u32);
        u32le(&mut out, relocs.len() as u32);
        text(&mut out, exe_name);
        for name in &self.helper_names {
            text(&mut out, name);
        }
        for message in &self.messages {
            text(&mut out, message);
        }
        for (rva, offset) in table {
            u32le(&mut out, rva);
            u32le(&mut out, offset);
        }
        for (offset, kind, ns, index, addend) in relocs {
            u32le(&mut out, offset);
            out.push(kind);
            out.push(ns as u8);
            out.extend_from_slice(&[0, 0]);
            u32le(&mut out, index);
            out.extend_from_slice(&addend.to_le_bytes());
        }
        out.extend_from_slice(&code);
        out
    }
}

/// Traduction d'une fonction.
struct Tr<'n, 'a, 'b> {
    n: &'n mut Native<'a>,
    b: FunctionBuilder<'b>,
    f: &'b Func,
    ctx: Value,
    base: Value,
    regs: [Variable; 16],
    flags: [Variable; 6],
    blocks: HashMap<u32, Block>,
    /// Blocs déjà remplis (ceux de `blocks` qui restent vides sont complétés à la fin).
    filled: BTreeSet<Block>,
    helpers: HashMap<String, FuncRef>,
    callees: HashMap<u32, FuncRef>,
    terminated: bool,
    /// Points de reprise de setjmp : bloc qui suit chaque appel.
    setjmp_sites: Vec<Block>,
    frame_token: Option<Value>,
    unwind_block: Option<Block>,
    fmt: IntelFormatter,
}

impl<'n, 'a, 'b> Tr<'n, 'a, 'b> {
    fn new(n: &'n mut Native<'a>, mut b: FunctionBuilder<'b>, f: &'b Func) -> Self {
        let regs = std::array::from_fn(|_| b.declare_var(I64));
        let flags = std::array::from_fn(|_| b.declare_var(I8));
        let dummy = Value::from_u32(0);
        Tr {
            n,
            b,
            f,
            ctx: dummy,
            base: dummy,
            regs,
            flags,
            blocks: HashMap::new(),
            filled: BTreeSet::new(),
            helpers: HashMap::new(),
            callees: HashMap::new(),
            terminated: false,
            setjmp_sites: Vec::new(),
            frame_token: None,
            unwind_block: None,
            fmt: IntelFormatter::new(),
        }
    }

    fn bits32(&self) -> bool {
        self.n.pe.bits == 32
    }
    fn ps(&self) -> u32 {
        self.n.pe.bits / 8
    }

    // --- Accès à ctx ---

    fn ctx_load(&mut self, t: Type, off: i32) -> Value {
        self.b.ins().load(t, MemFlagsData::trusted(), self.ctx, off)
    }
    fn ctx_store(&mut self, v: Value, off: i32) {
        self.b.ins().store(MemFlagsData::trusted(), v, self.ctx, off);
    }
    fn iconst(&mut self, t: Type, v: u64) -> Value {
        let v = if t == I64 { v as i64 } else { (v & mask(t.bytes())) as i64 };
        self.b.ins().iconst(t, v)
    }
    fn get(&mut self, n: usize) -> Value {
        self.b.use_var(self.regs[n])
    }
    fn set(&mut self, n: usize, v: Value) {
        self.b.def_var(self.regs[n], v);
        self.ctx_store(v, OFF_R + 8 * n as i32);
    }
    fn flag(&mut self, i: usize) -> Value {
        self.b.use_var(self.flags[i])
    }
    fn set_flag(&mut self, i: usize, v: Value) {
        self.b.def_var(self.flags[i], v);
    }
    /// Drapeau `i`, s'il est lu plus loin.
    fn put_flag(&mut self, need: u32, i: usize, v: impl FnOnce(&mut Self) -> Value) {
        if need & FLAG_BITS[i] != 0 {
            let v = v(self);
            self.set_flag(i, v);
        }
    }
    fn zero8(&mut self) -> Value {
        self.b.ins().iconst(I8, 0)
    }

    /// Registres relus depuis ctx (après un appel qui a pu les changer). Les lectures inutiles sont supprimées.
    fn reload(&mut self) {
        for n in 0..16 {
            let v = self.ctx_load(I64, OFF_R + 8 * n as i32);
            self.b.def_var(self.regs[n], v);
        }
        for i in 0..6 {
            let v = self.ctx_load(I8, OFF_FLAGS + i as i32);
            self.b.def_var(self.flags[i], v);
        }
    }
    /// Drapeaux rangés dans ctx (avant un appel ou un retour).
    fn store_flags(&mut self) {
        for i in 0..6 {
            let v = self.flag(i);
            self.ctx_store(v, OFF_FLAGS + i as i32);
        }
    }

    // --- Appels ---

    fn helper(&mut self, name: &str, params: &[Type], ret: Option<Type>) -> FuncRef {
        if let Some(&r) = self.helpers.get(name) {
            return r;
        }
        let mut sig = Signature::new(CallConv::triple_default(self.n.isa.triple()));
        sig.params.extend(params.iter().map(|&t| AbiParam::new(t)));
        sig.returns.extend(ret.map(AbiParam::new));
        let sigref = self.b.import_signature(sig);
        let id = self.n.helper_id(name);
        let user = self.b.func.declare_imported_user_function(UserExternalName::new(NS_HELPER, id));
        let r = self.b.import_function(ExtFuncData { name: ExternalName::user(user), signature: sigref, colocated: false, patchable: false });
        self.helpers.insert(name.to_string(), r);
        r
    }
    /// Appel d'une fonction du runtime, drapeaux rangés avant et registres relus après.
    fn call_helper(&mut self, name: &str, params: &[Type], ret: Option<Type>, args: &[Value]) -> Option<Value> {
        let r = self.helper(name, params, ret);
        self.store_flags();
        let call = self.b.ins().call(r, args);
        let result = ret.map(|_| self.b.inst_results(call)[0]);
        self.reload();
        result
    }
    /// Appel d'une fonction du runtime qui ne touche ni aux registres ni aux drapeaux.
    fn call_pure(&mut self, name: &str, params: &[Type], ret: Option<Type>, args: &[Value]) -> Option<Value> {
        let r = self.helper(name, params, ret);
        let call = self.b.ins().call(r, args);
        ret.map(|_| self.b.inst_results(call)[0])
    }
    fn callee(&mut self, rva: u32) -> FuncRef {
        if let Some(&r) = self.callees.get(&rva) {
            return r;
        }
        let sig = self.n.signature();
        let sigref = self.b.import_signature(sig);
        let user = self.b.func.declare_imported_user_function(UserExternalName::new(NS_FUNC, rva));
        let r = self.b.import_function(ExtFuncData { name: ExternalName::user(user), signature: sigref, colocated: true, patchable: false });
        self.callees.insert(rva, r);
        r
    }
    /// Après l'appel d'une fonction du jeu : un longjmp en cours remonte jusqu'à son setjmp.
    fn check_unwind(&mut self) {
        let unwind = self.ctx_load(I64, OFF_UNWIND);
        let block = self.unwind_block();
        let cont = self.b.create_block();
        self.b.ins().brif(unwind, block, &[], cont, &[]);
        self.b.switch_to_block(cont);
    }
    fn unwind_block(&mut self) -> Block {
        if let Some(b) = self.unwind_block {
            return b;
        }
        let b = self.b.create_block();
        self.unwind_block = Some(b);
        b
    }
    /// Appel d'une fonction traduite (adresse de retour empilée par l'appelant).
    fn call_func(&mut self, rva: u32) {
        let r = self.callee(rva);
        self.store_flags();
        let ctx = self.ctx;
        self.b.ins().call(r, &[ctx]);
        self.check_unwind();
        self.reload();
    }
    /// Appel par adresse (fonction traduite ou fonction Windows de l'hôte).
    fn call_address(&mut self, target: Value) {
        let r = self.helper("rcn_call", &[I64, I64], None);
        self.store_flags();
        let ctx = self.ctx;
        self.b.ins().call(r, &[ctx, target]);
        self.check_unwind();
        self.reload();
    }
    fn ret(&mut self) {
        self.store_flags();
        self.b.ins().return_(&[]);
        self.terminated = true;
    }
    /// Arrêt du jeu sur une situation non gérée.
    fn trap(&mut self, rva: u32, message: &str) {
        let index = self.n.message_id(message);
        let r = self.helper("rcn_trap_index", &[I64, I64, I32], None);
        self.store_flags();
        let ctx = self.ctx;
        let rva = self.b.ins().iconst(I64, rva as i64);
        let index = self.b.ins().iconst(I32, index as i64);
        self.b.ins().call(r, &[ctx, rva, index]);
        self.b.ins().return_(&[]);
        self.terminated = true;
    }

    // --- Blocs ---

    fn block_of(&mut self, rva: u32) -> Block {
        if let Some(&b) = self.blocks.get(&rva) {
            return b;
        }
        let b = self.b.create_block();
        self.blocks.insert(rva, b);
        b
    }
    /// Bloc de destination d'un saut : dans la fonction, ou petit bloc qui appelle l'autre fonction.
    fn jump_block(&mut self, ins: &Instruction, rva: u32) -> Block {
        match branch_target(self.n.pe, ins) {
            Some(t) if !self.f.is_external(t) => self.block_of(t),
            target => {
                let current = self.b.current_block().unwrap();
                let stub = self.b.create_block();
                self.b.switch_to_block(stub);
                let was = self.terminated;
                match target {
                    Some(t) if self.n.funcs.contains(&t) => {
                        self.call_func(t);
                        self.ret();
                    }
                    _ => self.trap(rva, "saut hors du code"),
                }
                self.terminated = was;
                self.b.switch_to_block(current);
                stub
            }
        }
    }
    fn jump_to(&mut self, block: Block) {
        self.b.ins().jump(block, &[]);
        self.terminated = true;
    }
    /// Si `cond` (i8 non nul) : `then` ; puis suite commune.
    fn if_then(&mut self, cond: Value, then: impl FnOnce(&mut Self)) {
        let yes = self.b.create_block();
        let done = self.b.create_block();
        self.b.ins().brif(cond, yes, &[], done, &[]);
        self.b.switch_to_block(yes);
        then(self);
        self.b.ins().jump(done, &[]);
        self.b.switch_to_block(done);
    }
    /// Si `cond` : `then`, sinon `otherwise` ; puis suite commune.
    fn if_else(&mut self, cond: Value, then: impl FnOnce(&mut Self), otherwise: impl FnOnce(&mut Self)) {
        let yes = self.b.create_block();
        let no = self.b.create_block();
        let done = self.b.create_block();
        self.b.ins().brif(cond, yes, &[], no, &[]);
        self.b.switch_to_block(yes);
        then(self);
        self.b.ins().jump(done, &[]);
        self.b.switch_to_block(no);
        otherwise(self);
        self.b.ins().jump(done, &[]);
        self.b.switch_to_block(done);
    }

    // --- Opérandes ---

    fn opsize(&self, ins: &Instruction, i: u32) -> u32 {
        match ins.op_kind(i) {
            OpKind::Register => ins.op_register(i).size() as u32,
            OpKind::Memory => ins.memory_size().size() as u32,
            _ => 0,
        }
    }

    fn ea(&mut self, ins: &Instruction) -> Option<Value> {
        let (base, index) = (ins.memory_base(), ins.memory_index());
        let disp = ins.memory_displacement64();
        let seg = match ins.segment_prefix() {
            Register::GS => Some(OFF_GS),
            Register::FS => Some(OFF_FS),
            _ => None,
        };
        let image_base = self.n.pe.image_base;
        let size_of_image = self.n.pe.size_of_image as u64;
        if base == Register::RIP {
            let rva = disp.wrapping_sub(image_base);
            return Some(self.b.ins().iadd_imm_s(self.base, rva as i64));
        }
        if base == Register::EIP {
            return None;
        }
        let mut sum = if base == Register::None && index == Register::None {
            let rva = disp.wrapping_sub(image_base);
            if seg.is_none() && rva < size_of_image {
                return Some(self.b.ins().iadd_imm_s(self.base, rva as i64));
            }
            self.b.ins().iconst(I64, disp as i64)
        } else {
            let mut narrow = false;
            let mut acc: Option<Value> = None;
            for (reg, scale) in [(base, 1u32), (index, ins.memory_index_scale())] {
                if reg == Register::None {
                    continue;
                }
                if !reg.is_gpr() || reg.size() < 4 {
                    return None;
                }
                narrow |= reg.size() == 4;
                let mut v = self.get(gpr(reg));
                if scale != 1 {
                    v = self.b.ins().imul_imm_s(v, scale as i64);
                }
                acc = Some(match acc {
                    Some(a) => self.b.ins().iadd(a, v),
                    None => v,
                });
            }
            let mut v = acc.unwrap();
            if disp != 0 {
                v = self.b.ins().iadd_imm_s(v, disp as i64);
            }
            if narrow {
                let low = self.b.ins().ireduce(I32, v);
                v = self.b.ins().uextend(I64, low);
            }
            v
        };
        if let Some(off) = seg {
            let s = self.ctx_load(I64, off);
            sum = self.b.ins().iadd(sum, s);
        }
        Some(sum)
    }

    fn mem_load(&mut self, t: Type, addr: Value) -> Value {
        self.b.ins().load(t, MemFlagsData::new(), addr, 0)
    }
    fn mem_store(&mut self, v: Value, addr: Value) {
        self.b.ins().store(MemFlagsData::new(), v, addr, 0);
    }

    fn rd_reg(&mut self, reg: Register) -> Option<Value> {
        if !reg.is_gpr() {
            return None;
        }
        let v = self.get(gpr(reg));
        Some(match reg.size() {
            8 => v,
            4 => self.b.ins().ireduce(I32, v),
            2 => self.b.ins().ireduce(I16, v),
            _ if is_high8(reg) => {
                let s = self.b.ins().ushr_imm_s(v, 8);
                self.b.ins().ireduce(I8, s)
            }
            _ => self.b.ins().ireduce(I8, v),
        })
    }
    fn wr_reg(&mut self, reg: Register, val: Value) -> Option<()> {
        if !reg.is_gpr() {
            return None;
        }
        let n = gpr(reg);
        let v64 = if self.b.func.dfg.value_type(val) == I64 { val } else { self.b.ins().uextend(I64, val) };
        let new = match reg.size() {
            8 | 4 => v64,
            2 => {
                let old = self.get(n);
                let kept = self.b.ins().band_imm_s(old, !0xFFFFi64);
                self.b.ins().bor(kept, v64)
            }
            _ if is_high8(reg) => {
                let old = self.get(n);
                let kept = self.b.ins().band_imm_s(old, !0xFF00i64);
                let shifted = self.b.ins().ishl_imm_s(v64, 8);
                self.b.ins().bor(kept, shifted)
            }
            _ => {
                let old = self.get(n);
                let kept = self.b.ins().band_imm_s(old, !0xFFi64);
                self.b.ins().bor(kept, v64)
            }
        };
        self.set(n, new);
        Some(())
    }

    /// Valeur d'un opérande entier de `size` octets.
    fn rd(&mut self, ins: &Instruction, i: u32, size: u32) -> Option<Value> {
        match ins.op_kind(i) {
            OpKind::Register => {
                let reg = ins.op_register(i);
                if reg.size() as u32 != size {
                    return None;
                }
                self.rd_reg(reg)
            }
            OpKind::Memory => {
                let ea = self.ea(ins)?;
                Some(self.mem_load(ty(size), ea))
            }
            k if is_imm(k) => Some(self.iconst(ty(size), ins.immediate(i))),
            _ => None,
        }
    }
    fn wr(&mut self, ins: &Instruction, i: u32, size: u32, val: Value) -> Option<()> {
        let val = self.fit(val, size);
        match ins.op_kind(i) {
            OpKind::Register => {
                let reg = ins.op_register(i);
                if reg.size() as u32 != size {
                    return None;
                }
                self.wr_reg(reg, val)
            }
            OpKind::Memory => {
                let ea = self.ea(ins)?;
                self.mem_store(val, ea);
                Some(())
            }
            _ => None,
        }
    }
    /// Valeur ramenée au type de `size` octets (troncature ou extension par des zéros).
    fn fit(&mut self, v: Value, size: u32) -> Value {
        let t = ty(size);
        let vt = self.b.func.dfg.value_type(v);
        if vt == t {
            v
        } else if vt.bits() > t.bits() {
            self.b.ins().ireduce(t, v)
        } else {
            self.b.ins().uextend(t, v)
        }
    }
    fn to64(&mut self, v: Value) -> Value {
        if self.b.func.dfg.value_type(v) == I64 {
            v
        } else {
            self.b.ins().uextend(I64, v)
        }
    }
    fn sext64(&mut self, v: Value) -> Value {
        if self.b.func.dfg.value_type(v) == I64 {
            v
        } else {
            self.b.ins().sextend(I64, v)
        }
    }

    // --- Drapeaux ---

    /// Bit `bit` de `v` (i8).
    fn bit(&mut self, v: Value, bit: u32) -> Value {
        let s = if bit == 0 { v } else { self.b.ins().ushr_imm_s(v, bit as i64) };
        let one = self.b.ins().band_imm_s(s, 1);
        let vt = self.b.func.dfg.value_type(one);
        if vt == I8 {
            one
        } else {
            self.b.ins().ireduce(I8, one)
        }
    }
    fn sign(&mut self, v: Value) -> Value {
        self.b.ins().icmp_imm_s(IntCC::SignedLessThan, v, 0)
    }
    fn parity(&mut self, r: Value) -> Value {
        let low = if self.b.func.dfg.value_type(r) == I8 { r } else { self.b.ins().ireduce(I8, r) };
        let count = self.b.ins().popcnt(low);
        let odd = self.b.ins().band_imm_s(count, 1);
        self.b.ins().icmp_imm_s(IntCC::Equal, odd, 0)
    }
    fn zsp(&mut self, need: u32, r: Value) {
        self.put_flag(need, ZF, |t| t.b.ins().icmp_imm_s(IntCC::Equal, r, 0));
        self.put_flag(need, SF, |t| t.sign(r));
        self.put_flag(need, PF, |t| t.parity(r));
    }
    fn af(&mut self, need: u32, a: Value, b: Value, r: Value) {
        self.put_flag(need, AF, |t| {
            let x = t.b.ins().bxor(a, b);
            let x = t.b.ins().bxor(x, r);
            t.bit(x, 4)
        });
    }
    fn not(&mut self, v: Value) -> Value {
        self.b.ins().bxor_imm_s(v, 1)
    }
    fn cond(&mut self, cc: ConditionCode) -> Option<Value> {
        let f = |t: &mut Self, i: usize| t.flag(i);
        Some(match cc {
            ConditionCode::None => return None,
            ConditionCode::o => f(self, OF),
            ConditionCode::no => {
                let v = f(self, OF);
                self.not(v)
            }
            ConditionCode::b => f(self, CF),
            ConditionCode::ae => {
                let v = f(self, CF);
                self.not(v)
            }
            ConditionCode::e => f(self, ZF),
            ConditionCode::ne => {
                let v = f(self, ZF);
                self.not(v)
            }
            ConditionCode::be | ConditionCode::a => {
                let c = f(self, CF);
                let z = f(self, ZF);
                let v = self.b.ins().bor(c, z);
                if cc == ConditionCode::a {
                    self.not(v)
                } else {
                    v
                }
            }
            ConditionCode::s => f(self, SF),
            ConditionCode::ns => {
                let v = f(self, SF);
                self.not(v)
            }
            ConditionCode::p => f(self, PF),
            ConditionCode::np => {
                let v = f(self, PF);
                self.not(v)
            }
            ConditionCode::l | ConditionCode::ge => {
                let s = f(self, SF);
                let o = f(self, OF);
                let v = self.b.ins().bxor(s, o);
                if cc == ConditionCode::ge {
                    self.not(v)
                } else {
                    v
                }
            }
            ConditionCode::le | ConditionCode::g => {
                let s = f(self, SF);
                let o = f(self, OF);
                let z = f(self, ZF);
                let x = self.b.ins().bxor(s, o);
                let v = self.b.ins().bor(z, x);
                if cc == ConditionCode::g {
                    self.not(v)
                } else {
                    v
                }
            }
        })
    }

    // --- Instructions entières ---

    fn alu(&mut self, ins: &Instruction, need: u32, m: M) -> Option<()> {
        let size = self.opsize(ins, 0);
        let t = ty(size);
        let b = self.rd(ins, 1, size)?;
        let locked = ins.has_lock_prefix() && ins.op0_kind() == OpKind::Memory;
        let a = if locked {
            let op = match m {
                M::Add => AtomicRmwOp::Add,
                M::Sub => AtomicRmwOp::Sub,
                M::And => AtomicRmwOp::And,
                M::Or => AtomicRmwOp::Or,
                M::Xor => AtomicRmwOp::Xor,
                _ => return None,
            };
            let ea = self.ea(ins)?;
            self.b.ins().atomic_rmw(t, MemFlagsData::new(), op, ea, b)
        } else {
            self.rd(ins, 0, size)?
        };
        let r = match m {
            M::Add | M::Adc => {
                let r = if m == M::Adc {
                    let c8 = self.flag(CF);
                    let c = if t == I8 { c8 } else { self.b.ins().uextend(t, c8) };
                    let s = self.b.ins().iadd(a, b);
                    let r = self.b.ins().iadd(s, c);
                    self.put_flag(need, CF, |tr| {
                        let le = tr.b.ins().icmp(IntCC::UnsignedLessThanOrEqual, r, a);
                        let lt = tr.b.ins().icmp(IntCC::UnsignedLessThan, r, a);
                        tr.b.ins().select(c8, le, lt)
                    });
                    r
                } else {
                    let r = self.b.ins().iadd(a, b);
                    self.put_flag(need, CF, |tr| tr.b.ins().icmp(IntCC::UnsignedLessThan, r, a));
                    r
                };
                self.put_flag(need, OF, |tr| {
                    let x = tr.b.ins().bxor(a, r);
                    let y = tr.b.ins().bxor(b, r);
                    let z = tr.b.ins().band(x, y);
                    tr.sign(z)
                });
                self.af(need, a, b, r);
                r
            }
            M::Sub | M::Sbb | M::Cmp => {
                let r = if m == M::Sbb {
                    let c8 = self.flag(CF);
                    let c = if t == I8 { c8 } else { self.b.ins().uextend(t, c8) };
                    let s = self.b.ins().isub(a, b);
                    let r = self.b.ins().isub(s, c);
                    self.put_flag(need, CF, |tr| {
                        let le = tr.b.ins().icmp(IntCC::UnsignedLessThanOrEqual, a, b);
                        let lt = tr.b.ins().icmp(IntCC::UnsignedLessThan, a, b);
                        tr.b.ins().select(c8, le, lt)
                    });
                    r
                } else {
                    let r = self.b.ins().isub(a, b);
                    self.put_flag(need, CF, |tr| tr.b.ins().icmp(IntCC::UnsignedLessThan, a, b));
                    r
                };
                self.put_flag(need, OF, |tr| {
                    let x = tr.b.ins().bxor(a, b);
                    let y = tr.b.ins().bxor(a, r);
                    let z = tr.b.ins().band(x, y);
                    tr.sign(z)
                });
                self.af(need, a, b, r);
                r
            }
            _ => {
                let r = match m {
                    M::Or => self.b.ins().bor(a, b),
                    M::Xor => self.b.ins().bxor(a, b),
                    _ => self.b.ins().band(a, b),
                };
                for i in [CF, OF, AF] {
                    self.put_flag(need, i, |tr| tr.zero8());
                }
                r
            }
        };
        self.zsp(need, r);
        if !locked && !matches!(m, M::Cmp | M::Test) {
            self.wr(ins, 0, size, r)?;
        }
        Some(())
    }

    fn shift(&mut self, ins: &Instruction, need: u32) -> Option<()> {
        let m = ins.mnemonic();
        let size = self.opsize(ins, 0);
        let t = ty(size);
        let n = size * 8;
        let a = self.rd(ins, 0, size)?;
        let count = self.rd(ins, 1, 1)?;
        let c = self.b.ins().band_imm_s(count, if n == 64 { 63 } else { 31 });
        let c64 = self.b.ins().uextend(I64, c);
        // Les opérandes sont lus avant le test, l'écriture se fait dans la branche.
        let ins_copy = *ins;
        let dest_reg = (ins.op0_kind() == OpKind::Register).then(|| ins.op0_register());
        let ea = if ins.op0_kind() == OpKind::Memory { Some(self.ea(ins)?) } else { None };
        let write = move |tr: &mut Self, v: Value| match (dest_reg, ea) {
            (Some(reg), _) => {
                tr.wr_reg(reg, v);
            }
            (_, Some(ea)) => tr.mem_store(v, ea),
            _ => {}
        };
        let _ = ins_copy;
        let nonzero = self.b.ins().icmp_imm_s(IntCC::NotEqual, c, 0);
        let rotate = matches!(m, M::Rol | M::Ror);
        self.if_else(
            nonzero,
            |tr| {
                let a64 = tr.b.ins().uextend(I64, a);
                let a64 = if t == I64 { a } else { a64 };
                let one = tr.b.ins().iconst(I64, 1);
                let cm1 = tr.b.ins().isub(c64, one);
                let (r, cf) = match m {
                    M::Shl | M::Sal => {
                        let wide = tr.b.ins().ishl(a64, c64);
                        let r = if t == I64 { wide } else { tr.b.ins().ireduce(t, wide) };
                        // cf : dernier bit sorti ; nul si le décalage dépasse la largeur.
                        let nn = tr.b.ins().iconst(I64, n as i64);
                        let amount = tr.b.ins().isub(nn, c64);
                        let out = tr.b.ins().ushr(a64, amount);
                        let bit = tr.bit(out, 0);
                        let within = tr.b.ins().icmp_imm_s(IntCC::UnsignedLessThanOrEqual, c64, n as i64);
                        let zero = tr.zero8();
                        let cf = tr.b.ins().select(within, bit, zero);
                        tr.put_flag(need, OF, |tr| {
                            let top = tr.bit(r, n - 1);
                            tr.b.ins().bxor(top, cf)
                        });
                        (r, cf)
                    }
                    M::Shr => {
                        let wide = tr.b.ins().ushr(a64, c64);
                        let r = if t == I64 { wide } else { tr.b.ins().ireduce(t, wide) };
                        let out = tr.b.ins().ushr(a64, cm1);
                        let cf = tr.bit(out, 0);
                        tr.put_flag(need, OF, |tr| tr.bit(a, n - 1));
                        (r, cf)
                    }
                    M::Sar => {
                        let sa = tr.sext64(a);
                        let wide = tr.b.ins().sshr(sa, c64);
                        let r = if t == I64 { wide } else { tr.b.ins().ireduce(t, wide) };
                        let out = tr.b.ins().sshr(sa, cm1);
                        let cf = tr.bit(out, 0);
                        tr.put_flag(need, OF, |tr| tr.zero8());
                        (r, cf)
                    }
                    M::Rol => {
                        let r = tr.b.ins().rotl(a, c);
                        let cf = tr.bit(r, 0);
                        tr.put_flag(need, OF, |tr| {
                            let top = tr.bit(r, n - 1);
                            tr.b.ins().bxor(top, cf)
                        });
                        (r, cf)
                    }
                    _ => {
                        let r = tr.b.ins().rotr(a, c);
                        let cf = tr.bit(r, n - 1);
                        tr.put_flag(need, OF, |tr| {
                            let top = tr.bit(r, n - 1);
                            let next = tr.bit(r, n - 2);
                            tr.b.ins().bxor(top, next)
                        });
                        (r, cf)
                    }
                };
                tr.put_flag(need, CF, |_| cf);
                if !rotate {
                    tr.zsp(need, r);
                    tr.put_flag(need, AF, |tr| tr.zero8());
                }
                write(tr, r);
            },
            |tr| {
                // Même sans décalage, une destination 32 bits est étendue par des zéros.
                if size == 4 && dest_reg.is_some() {
                    write(tr, a);
                }
            },
        );
        Some(())
    }

    fn shift_double(&mut self, ins: &Instruction, need: u32) -> Option<()> {
        let size = self.opsize(ins, 0);
        if size < 4 {
            return None;
        }
        let n = size * 8;
        let a = self.rd(ins, 0, size)?;
        let b = self.rd(ins, 1, size)?;
        let count = self.rd(ins, 2, 1)?;
        let c8 = self.b.ins().band_imm_s(count, (n - 1) as i64);
        let t = ty(size);
        let c = self.b.ins().uextend(t, c8);
        let dest_reg = (ins.op0_kind() == OpKind::Register).then(|| ins.op0_register());
        let ea = if ins.op0_kind() == OpKind::Memory { Some(self.ea(ins)?) } else { None };
        let left = ins.mnemonic() == M::Shld;
        let nonzero = self.b.ins().icmp_imm_s(IntCC::NotEqual, c8, 0);
        self.if_then(nonzero, |tr| {
            let nn = tr.b.ins().iconst(t, n as i64);
            let rest = tr.b.ins().isub(nn, c);
            let r = if left {
                let x = tr.b.ins().ishl(a, c);
                let y = tr.b.ins().ushr(b, rest);
                tr.b.ins().bor(x, y)
            } else {
                let x = tr.b.ins().ushr(a, c);
                let y = tr.b.ins().ishl(b, rest);
                tr.b.ins().bor(x, y)
            };
            tr.put_flag(need, CF, |tr| {
                if left {
                    let o = tr.b.ins().ushr(a, rest);
                    tr.bit(o, 0)
                } else {
                    let one = tr.b.ins().iconst(t, 1);
                    let cm1 = tr.b.ins().isub(c, one);
                    let o = tr.b.ins().ushr(a, cm1);
                    tr.bit(o, 0)
                }
            });
            tr.put_flag(need, OF, |tr| {
                let x = tr.b.ins().bxor(a, r);
                tr.sign(x)
            });
            tr.zsp(need, r);
            match (dest_reg, ea) {
                (Some(reg), _) => {
                    tr.wr_reg(reg, r);
                }
                (_, Some(ea)) => tr.mem_store(r, ea),
                _ => {}
            }
        });
        Some(())
    }

    fn mul_div(&mut self, ins: &Instruction, need: u32, rva: u32) -> Option<()> {
        let m = ins.mnemonic();
        if m == M::Imul && ins.op_count() >= 2 {
            let size = self.opsize(ins, 0);
            let (a, b) = if ins.op_count() == 3 {
                (self.rd(ins, 1, size)?, self.rd(ins, 2, size)?)
            } else {
                (self.rd(ins, 0, size)?, self.rd(ins, 1, size)?)
            };
            let (r, o) = if size == 8 {
                let r = self.b.ins().imul(a, b);
                let hi = self.b.ins().smulhi(a, b);
                let sign = self.b.ins().sshr_imm_s(r, 63);
                (r, self.b.ins().icmp(IntCC::NotEqual, hi, sign))
            } else {
                let wa = self.sext64(a);
                let wb = self.sext64(b);
                let w = self.b.ins().imul(wa, wb);
                let r = self.b.ins().ireduce(ty(size), w);
                let back = self.b.ins().sextend(I64, r);
                (r, self.b.ins().icmp(IntCC::NotEqual, w, back))
            };
            self.put_flag(need, CF, |_| o);
            self.put_flag(need, OF, |_| o);
            return self.wr(ins, 0, size, r);
        }
        let size = self.opsize(ins, 0);
        let b = self.rd(ins, 0, size)?;
        if matches!(m, M::Div | M::Idiv) {
            let b64 = self.to64(b);
            let ctx = self.ctx;
            let bits = self.b.ins().iconst(I32, (size * 8) as i64);
            let signed = self.b.ins().iconst(I32, (m == M::Idiv) as i64);
            let rva = self.b.ins().iconst(I64, rva as i64);
            self.call_helper("rcn_op_div", &[I64, I64, I32, I32, I64], None, &[ctx, b64, bits, signed, rva]);
            return Some(());
        }
        let signed = m == M::Imul;
        let o = match size {
            8 => {
                let a = self.get(0);
                let lo = self.b.ins().imul(a, b);
                let hi = if signed { self.b.ins().smulhi(a, b) } else { self.b.ins().umulhi(a, b) };
                self.set(0, lo);
                self.set(2, hi);
                if signed {
                    let sign = self.b.ins().sshr_imm_s(lo, 63);
                    self.b.ins().icmp(IntCC::NotEqual, hi, sign)
                } else {
                    self.b.ins().icmp_imm_s(IntCC::NotEqual, hi, 0)
                }
            }
            4 | 2 => {
                let half = ty(size);
                let acc = if size == 4 { Register::EAX } else { Register::AX };
                let a = self.rd_reg(acc)?;
                let (wa, wb) = if signed { (self.sext64(a), self.sext64(b)) } else { (self.to64(a), self.to64(b)) };
                let w = self.b.ins().imul(wa, wb);
                let lo = self.b.ins().ireduce(half, w);
                let hi_wide = self.b.ins().ushr_imm_s(w, (size * 8) as i64);
                let hi = self.b.ins().ireduce(half, hi_wide);
                let dx = if size == 4 { Register::EDX } else { Register::DX };
                self.wr_reg(acc, lo)?;
                self.wr_reg(dx, hi)?;
                if signed {
                    let back = self.b.ins().sextend(I64, lo);
                    self.b.ins().icmp(IntCC::NotEqual, w, back)
                } else {
                    self.b.ins().icmp_imm_s(IntCC::NotEqual, hi, 0)
                }
            }
            1 => {
                let a = self.rd_reg(Register::AL)?;
                let (wa, wb) = if signed {
                    (self.b.ins().sextend(I16, a), self.b.ins().sextend(I16, b))
                } else {
                    (self.b.ins().uextend(I16, a), self.b.ins().uextend(I16, b))
                };
                let w = self.b.ins().imul(wa, wb);
                self.wr_reg(Register::AX, w)?;
                if signed {
                    let low = self.b.ins().ireduce(I8, w);
                    let back = self.b.ins().sextend(I16, low);
                    self.b.ins().icmp(IntCC::NotEqual, w, back)
                } else {
                    let high = self.b.ins().ushr_imm_s(w, 8);
                    self.b.ins().icmp_imm_s(IntCC::NotEqual, high, 0)
                }
            }
            _ => return None,
        };
        self.put_flag(need, CF, |_| o);
        self.put_flag(need, OF, |_| o);
        Some(())
    }

    // --- Pile ---

    fn push(&mut self, v: Value) {
        let sp = self.get(4);
        if self.bits32() {
            let sp = self.b.ins().iadd_imm_s(sp, -4);
            let sp32 = self.b.ins().ireduce(I32, sp);
            let sp = self.b.ins().uextend(I64, sp32);
            self.set(4, sp);
            let v = self.fit(v, 4);
            self.mem_store(v, sp);
        } else {
            let sp = self.b.ins().iadd_imm_s(sp, -8);
            self.set(4, sp);
            let v = self.to64(v);
            self.mem_store(v, sp);
        }
    }
    fn pop(&mut self) -> Value {
        let sp = self.get(4);
        let ps = self.ps();
        let v = self.mem_load(ty(ps), sp);
        self.add_sp(ps as i64);
        v
    }
    fn add_sp(&mut self, amount: i64) {
        let sp = self.get(4);
        let new = self.b.ins().iadd_imm_s(sp, amount);
        let new = if self.bits32() {
            let low = self.b.ins().ireduce(I32, new);
            self.b.ins().uextend(I64, low)
        } else {
            new
        };
        self.set(4, new);
    }

    // --- SSE ---

    fn xmm(&self, ins: &Instruction, i: u32) -> Option<usize> {
        (ins.op_kind(i) == OpKind::Register && ins.op_register(i).is_xmm()).then(|| ins.op_register(i).number())
    }
    fn xmm_ptr(&mut self, n: usize) -> Value {
        self.b.ins().iadd_imm_s(self.ctx, (OFF_XMM + 16 * n as i32) as i64)
    }
    /// Adresse d'un opérande de 128 bits : registre xmm dans ctx, ou mémoire.
    fn x128(&mut self, ins: &Instruction, i: u32) -> Option<Value> {
        match ins.op_kind(i) {
            OpKind::Register => {
                let n = self.xmm(ins, i)?;
                Some(self.xmm_ptr(n))
            }
            OpKind::Memory => self.ea(ins),
            _ => None,
        }
    }
    /// Partie basse (flottant de type `t`) d'un registre xmm ou d'un opérande mémoire.
    fn xlow(&mut self, ins: &Instruction, i: u32, t: Type) -> Option<Value> {
        match ins.op_kind(i) {
            OpKind::Register => {
                let n = self.xmm(ins, i)?;
                Some(self.ctx_load(t, OFF_XMM + 16 * n as i32))
            }
            OpKind::Memory => {
                let ea = self.ea(ins)?;
                Some(self.mem_load(t, ea))
            }
            _ => None,
        }
    }
    fn xmm_store(&mut self, n: usize, v: Value, offset: i32) {
        self.ctx_store(v, OFF_XMM + 16 * n as i32 + offset);
    }

    fn float_flags(&mut self, a: Value, b: Value) {
        let un = self.b.ins().fcmp(FloatCC::Unordered, a, b);
        let eq = self.b.ins().fcmp(FloatCC::Equal, a, b);
        let lt = self.b.ins().fcmp(FloatCC::LessThan, a, b);
        let zf = self.b.ins().bor(un, eq);
        let cf = self.b.ins().bor(un, lt);
        self.set_flag(ZF, zf);
        self.set_flag(PF, un);
        self.set_flag(CF, cf);
        for i in [OF, SF, AF] {
            let z = self.zero8();
            self.set_flag(i, z);
        }
    }

    fn sse(&mut self, ins: &Instruction) -> Option<()> {
        let m = ins.mnemonic();
        let dst = self.xmm(ins, 0);
        let name = || format!("rcn_op_{m:?}").to_lowercase();
        if PACKED_BINARY.contains(&m) || PACKED_UNARY.contains(&m) {
            let d = self.xmm_ptr(dst?);
            let s = self.x128(ins, 1)?;
            self.call_pure(&name(), &[I64, I64], None, &[d, s]);
            return Some(());
        }
        if PACKED_SHIFT.contains(&m) {
            let d = self.xmm_ptr(dst?);
            let count = if is_imm(ins.op1_kind()) {
                self.b.ins().iconst(I64, ins.immediate(1) as i64)
            } else {
                let p = self.x128(ins, 1)?;
                self.mem_load(I64, p)
            };
            self.call_pure(&name(), &[I64, I64], None, &[d, count]);
            return Some(());
        }
        if MOVE_128.contains(&m) {
            let src = self.x128(ins, 1)?;
            let dst_ptr = self.x128(ins, 0)?;
            let lo = self.mem_load(I64, src);
            let hi = self.b.ins().load(I64, MemFlagsData::new(), src, 8);
            self.mem_store(lo, dst_ptr);
            self.b.ins().store(MemFlagsData::new(), hi, dst_ptr, 8);
            return Some(());
        }
        let scalar = |tr: &mut Self, double: bool, op: &str| -> Option<()> {
            let d = dst?;
            let t = if double { F64 } else { F32 };
            let a = tr.ctx_load(t, OFF_XMM + 16 * d as i32);
            let b = tr.xlow(ins, 1, t)?;
            let r = match op {
                "+" => tr.b.ins().fadd(a, b),
                "-" => tr.b.ins().fsub(a, b),
                "*" => tr.b.ins().fmul(a, b),
                "/" => tr.b.ins().fdiv(a, b),
                "min" => {
                    let lt = tr.b.ins().fcmp(FloatCC::LessThan, a, b);
                    tr.b.ins().select(lt, a, b)
                }
                "max" => {
                    let gt = tr.b.ins().fcmp(FloatCC::GreaterThan, a, b);
                    tr.b.ins().select(gt, a, b)
                }
                _ => tr.b.ins().sqrt(b),
            };
            tr.xmm_store(d, r, 0);
            Some(())
        };
        let compare = |tr: &mut Self, double: bool| -> Option<()> {
            let t = if double { F64 } else { F32 };
            let a = tr.xlow(ins, 0, t)?;
            let b = tr.xlow(ins, 1, t)?;
            tr.float_flags(a, b);
            Some(())
        };
        let to_int = |tr: &mut Self, double: bool, truncate: bool| -> Option<()> {
            let size = tr.opsize(ins, 0);
            let v = tr.xlow(ins, 1, if double { F64 } else { F32 })?;
            let v = if double { v } else { tr.b.ins().fpromote(F64, v) };
            let bits = tr.b.ins().iconst(I32, (size * 8) as i64);
            let tr_flag = tr.b.ins().iconst(I32, truncate as i64);
            let r = tr.call_pure("rcn_op_cvt", &[F64, I32, I32], Some(I64), &[v, bits, tr_flag])?;
            tr.wr(ins, 0, size, r)
        };
        match m {
            M::Addss => scalar(self, false, "+"),
            M::Subss => scalar(self, false, "-"),
            M::Mulss => scalar(self, false, "*"),
            M::Divss => scalar(self, false, "/"),
            M::Minss => scalar(self, false, "min"),
            M::Maxss => scalar(self, false, "max"),
            M::Sqrtss => scalar(self, false, "sqrt"),
            M::Addsd => scalar(self, true, "+"),
            M::Subsd => scalar(self, true, "-"),
            M::Mulsd => scalar(self, true, "*"),
            M::Divsd => scalar(self, true, "/"),
            M::Minsd => scalar(self, true, "min"),
            M::Maxsd => scalar(self, true, "max"),
            M::Sqrtsd => scalar(self, true, "sqrt"),
            M::Comiss | M::Ucomiss => compare(self, false),
            M::Comisd | M::Ucomisd => compare(self, true),
            M::Movss | M::Movsd => {
                let t = if m == M::Movsd { F64 } else { F32 };
                match (ins.op0_kind(), ins.op1_kind()) {
                    (OpKind::Register, OpKind::Register) => {
                        let v = self.xlow(ins, 1, t)?;
                        self.xmm_store(dst?, v, 0);
                    }
                    (OpKind::Register, OpKind::Memory) => {
                        let d = dst?;
                        let v = self.xlow(ins, 1, t)?;
                        let zero = self.b.ins().iconst(I64, 0);
                        self.xmm_store(d, zero, 0);
                        self.xmm_store(d, zero, 8);
                        self.xmm_store(d, v, 0);
                    }
                    (OpKind::Memory, OpKind::Register) => {
                        let v = self.xlow(ins, 1, t)?;
                        let ea = self.ea(ins)?;
                        self.mem_store(v, ea);
                    }
                    _ => return None,
                }
                Some(())
            }
            M::Movd | M::Movq => {
                let size = if m == M::Movd { 4 } else { 8 };
                if let Some(d) = dst {
                    let v = match self.xmm(ins, 1) {
                        Some(s) => self.ctx_load(ty(size), OFF_XMM + 16 * s as i32),
                        None => self.rd(ins, 1, size)?,
                    };
                    let v = self.to64(v);
                    let zero = self.b.ins().iconst(I64, 0);
                    self.xmm_store(d, v, 0);
                    self.xmm_store(d, zero, 8);
                    Some(())
                } else {
                    let s = self.xmm(ins, 1)?;
                    let v = self.ctx_load(ty(size), OFF_XMM + 16 * s as i32);
                    self.wr(ins, 0, size, v)
                }
            }
            M::Movlps | M::Movlpd | M::Movhps | M::Movhpd => {
                let half = if matches!(m, M::Movlps | M::Movlpd) { 0 } else { 8 };
                match (ins.op0_kind(), ins.op1_kind()) {
                    (OpKind::Register, OpKind::Memory) => {
                        let ea = self.ea(ins)?;
                        let v = self.mem_load(I64, ea);
                        self.xmm_store(dst?, v, half);
                    }
                    (OpKind::Memory, OpKind::Register) => {
                        let s = self.xmm(ins, 1)?;
                        let v = self.ctx_load(I64, OFF_XMM + 16 * s as i32 + half);
                        let ea = self.ea(ins)?;
                        self.mem_store(v, ea);
                    }
                    _ => return None,
                }
                Some(())
            }
            M::Cvtsi2ss | M::Cvtsi2sd => {
                let size = self.opsize(ins, 1);
                let v = self.rd(ins, 1, size)?;
                let t = if m == M::Cvtsi2ss { F32 } else { F64 };
                let f = self.b.ins().fcvt_from_sint(t, v);
                self.xmm_store(dst?, f, 0);
                Some(())
            }
            M::Cvtss2sd => {
                let v = self.xlow(ins, 1, F32)?;
                let f = self.b.ins().fpromote(F64, v);
                self.xmm_store(dst?, f, 0);
                Some(())
            }
            M::Cvtsd2ss => {
                let v = self.xlow(ins, 1, F64)?;
                let f = self.b.ins().fdemote(F32, v);
                self.xmm_store(dst?, f, 0);
                Some(())
            }
            M::Cvttss2si => to_int(self, false, true),
            M::Cvttsd2si => to_int(self, true, true),
            M::Cvtss2si => to_int(self, false, false),
            M::Cvtsd2si => to_int(self, true, false),
            M::Movmskps | M::Movmskpd | M::Pmovmskb => {
                let size = self.opsize(ins, 0);
                let s = self.xmm(ins, 1)?;
                let p = self.xmm_ptr(s);
                let r = self.call_pure(&name(), &[I64], Some(I32), &[p])?;
                self.wr(ins, 0, size, r)
            }
            M::Pshufd | M::Pshuflw | M::Pshufhw | M::Shufps | M::Shufpd | M::Cmpps | M::Cmppd => {
                let d = self.xmm_ptr(dst?);
                let s = self.x128(ins, 1)?;
                let imm = self.b.ins().iconst(I32, ins.immediate(2) as i64);
                self.call_pure(&name(), &[I64, I64, I32], None, &[d, s, imm]);
                Some(())
            }
            M::Cmpss | M::Cmpsd => {
                let d = dst?;
                let double = m == M::Cmpsd;
                let t = if double { F64 } else { F32 };
                let a = self.ctx_load(t, OFF_XMM + 16 * d as i32);
                let b = self.xlow(ins, 1, t)?;
                let (a, b) = if double { (a, b) } else { (self.b.ins().fpromote(F64, a), self.b.ins().fpromote(F64, b)) };
                let pred = self.b.ins().iconst(I32, ins.immediate(2) as i64);
                let r = self.call_pure("rcn_op_fcmp", &[F64, F64, I32], Some(I32), &[a, b, pred])?;
                let it = if double { I64 } else { I32 };
                let ones = self.b.ins().iconst(it, -1);
                let zero = self.b.ins().iconst(it, 0);
                let mask = self.b.ins().select(r, ones, zero);
                self.xmm_store(d, mask, 0);
                Some(())
            }
            M::Pextrw => {
                let size = self.opsize(ins, 0);
                let s = self.xmm(ins, 1)?;
                let v = self.ctx_load(I16, OFF_XMM + 16 * s as i32 + 2 * (ins.immediate(2) & 7) as i32);
                self.wr(ins, 0, size, v)
            }
            M::Pinsrw => {
                let v = match ins.op1_kind() {
                    OpKind::Register => self.rd(ins, 1, 4)?,
                    _ => self.rd(ins, 1, 2)?,
                };
                let v = self.fit(v, 2);
                self.xmm_store(dst?, v, 2 * (ins.immediate(2) & 7) as i32);
                Some(())
            }
            _ => None,
        }
    }

    // --- x87 ---

    fn st_addr(&mut self, i: u32) -> Value {
        let top = self.b.ins().load(I8, MemFlagsData::trusted(), self.ctx, OFF_FPTOP);
        let top = self.b.ins().uextend(I64, top);
        let idx = self.b.ins().iadd_imm_s(top, i as i64);
        let idx = self.b.ins().band_imm_s(idx, 7);
        let off = self.b.ins().ishl_imm_s(idx, 3);
        let p = self.b.ins().iadd(self.ctx, off);
        self.b.ins().iadd_imm_s(p, OFF_ST as i64)
    }
    fn st(&mut self, i: u32) -> Value {
        let p = self.st_addr(i);
        self.b.ins().load(F64, MemFlagsData::trusted(), p, 0)
    }
    fn set_st(&mut self, i: u32, v: Value) {
        let p = self.st_addr(i);
        self.b.ins().store(MemFlagsData::trusted(), v, p, 0);
    }
    fn fpush(&mut self, v: Value) {
        let top = self.ctx_load(I8, OFF_FPTOP);
        let top = self.b.ins().iadd_imm_s(top, -1);
        let top = self.b.ins().band_imm_s(top, 7);
        self.ctx_store(top, OFF_FPTOP);
        self.set_st(0, v);
    }
    fn fpop(&mut self) {
        let top = self.ctx_load(I8, OFF_FPTOP);
        let top = self.b.ins().iadd_imm_s(top, 1);
        let top = self.b.ins().band_imm_s(top, 7);
        self.ctx_store(top, OFF_FPTOP);
    }
    fn st_index(ins: &Instruction, i: u32) -> Option<u32> {
        let reg = ins.op_register(i);
        (ins.op_kind(i) == OpKind::Register && reg >= Register::ST0 && reg <= Register::ST7).then(|| reg.number() as u32)
    }
    fn fload(&mut self, ins: &Instruction) -> Option<Value> {
        let ea = self.ea(ins)?;
        Some(match ins.memory_size() {
            MemorySize::Float32 => {
                let v = self.mem_load(F32, ea);
                self.b.ins().fpromote(F64, v)
            }
            MemorySize::Float64 => self.mem_load(F64, ea),
            MemorySize::Float80 => self.call_pure("rcn_ld80", &[I64], Some(F64), &[ea])?,
            MemorySize::Int16 | MemorySize::Int32 | MemorySize::Int64 => {
                let size = ins.memory_size().size() as u32;
                let v = self.mem_load(ty(size), ea);
                self.b.ins().fcvt_from_sint(F64, v)
            }
            _ => return None,
        })
    }
    fn math1(&mut self, v: Value, which: Math1) -> Value {
        let w = self.b.ins().iconst(I32, which as i64);
        self.call_pure("rcn_op_math1", &[F64, I32], Some(F64), &[v, w]).unwrap()
    }
    fn math2(&mut self, a: Value, b: Value, which: Math2) -> Value {
        let w = self.b.ins().iconst(I32, which as i64);
        self.call_pure("rcn_op_math2", &[F64, F64, I32], Some(F64), &[a, b, w]).unwrap()
    }

    fn x87(&mut self, ins: &Instruction) -> Option<()> {
        let m = ins.mnemonic();
        let is_mem = ins.op_count() > 0 && ins.op0_kind() == OpKind::Memory;
        // Second terme : mémoire, ST(i) (dernier opérande) ou ST(1).
        let other = |tr: &mut Self| -> Option<Value> {
            match ins.op_count() {
                0 => Some(tr.st(1)),
                _ if is_mem => tr.fload(ins),
                n => {
                    let i = Self::st_index(ins, n - 1)?;
                    Some(tr.st(i))
                }
            }
        };
        let arith = |tr: &mut Self, op: char, reversed: bool, pop: bool| -> Option<()> {
            let (di, src) = if is_mem {
                (0, tr.fload(ins)?)
            } else {
                let d = Self::st_index(ins, 0)?;
                let s = Self::st_index(ins, 1)?;
                (d, tr.st(s))
            };
            let dst = tr.st(di);
            let (x, y) = if reversed { (src, dst) } else { (dst, src) };
            let r = match op {
                '+' => tr.b.ins().fadd(x, y),
                '-' => tr.b.ins().fsub(x, y),
                '*' => tr.b.ins().fmul(x, y),
                _ => tr.b.ins().fdiv(x, y),
            };
            tr.set_st(di, r);
            if pop {
                tr.fpop();
            }
            Some(())
        };
        let compare = |tr: &mut Self, pops: u32| -> Option<()> {
            let a = tr.st(0);
            let b = other(tr)?;
            let ctx = tr.ctx;
            tr.call_pure("rcn_op_fcom", &[I64, F64, F64], None, &[ctx, a, b]);
            for _ in 0..pops {
                tr.fpop();
            }
            Some(())
        };
        let compare_flags = |tr: &mut Self, pop: bool| -> Option<()> {
            let a = tr.st(0);
            let b = other(tr)?;
            tr.float_flags(a, b);
            if pop {
                tr.fpop();
            }
            Some(())
        };
        let store_int = |tr: &mut Self, truncate: bool, pop: bool| -> Option<()> {
            let size = ins.memory_size().size() as u32;
            let v = tr.st(0);
            let ctx = tr.ctx;
            let bits = tr.b.ins().iconst(I32, (size * 8) as i64);
            let t = tr.b.ins().iconst(I32, truncate as i64);
            let r = tr.call_pure("rcn_op_fist", &[I64, F64, I32, I32], Some(I64), &[ctx, v, bits, t])?;
            let r = tr.fit(r, size);
            let ea = tr.ea(ins)?;
            tr.mem_store(r, ea);
            if pop {
                tr.fpop();
            }
            Some(())
        };
        let constant = |tr: &mut Self, c: f64| {
            let v = tr.b.ins().f64const(c);
            tr.fpush(v);
            Some(())
        };
        let cmov = |tr: &mut Self, c: Value| -> Option<()> {
            let src = other(tr)?;
            let cur = tr.st(0);
            let v = tr.b.ins().select(c, src, cur);
            tr.set_st(0, v);
            Some(())
        };
        let unary = |tr: &mut Self, f: &dyn Fn(&mut Self, Value) -> Value| {
            let v = tr.st(0);
            let r = f(tr, v);
            tr.set_st(0, r);
            Some(())
        };
        match m {
            M::Fld if is_mem => {
                let v = self.fload(ins)?;
                self.fpush(v);
                Some(())
            }
            M::Fld => {
                let i = Self::st_index(ins, 0)?;
                let v = self.st(i);
                self.fpush(v);
                Some(())
            }
            M::Fild => {
                let v = self.fload(ins)?;
                self.fpush(v);
                Some(())
            }
            M::Fst | M::Fstp => {
                let v = self.st(0);
                if !is_mem {
                    let i = Self::st_index(ins, 0)?;
                    self.set_st(i, v);
                } else {
                    let ea = self.ea(ins)?;
                    match ins.memory_size() {
                        MemorySize::Float32 => {
                            let f = self.b.ins().fdemote(F32, v);
                            self.mem_store(f, ea);
                        }
                        MemorySize::Float64 => self.mem_store(v, ea),
                        MemorySize::Float80 => {
                            self.call_pure("rcn_st80", &[I64, F64], None, &[ea, v]);
                        }
                        _ => return None,
                    }
                }
                if m == M::Fstp {
                    self.fpop();
                }
                Some(())
            }
            M::Fist => store_int(self, false, false),
            M::Fistp => store_int(self, false, true),
            M::Fisttp => store_int(self, true, true),
            M::Fadd | M::Fiadd => arith(self, '+', false, false),
            M::Fsub | M::Fisub => arith(self, '-', false, false),
            M::Fsubr | M::Fisubr => arith(self, '-', true, false),
            M::Fmul | M::Fimul => arith(self, '*', false, false),
            M::Fdiv | M::Fidiv => arith(self, '/', false, false),
            M::Fdivr | M::Fidivr => arith(self, '/', true, false),
            M::Faddp => arith(self, '+', false, true),
            M::Fsubp => arith(self, '-', false, true),
            M::Fsubrp => arith(self, '-', true, true),
            M::Fmulp => arith(self, '*', false, true),
            M::Fdivp => arith(self, '/', false, true),
            M::Fdivrp => arith(self, '/', true, true),
            M::Fchs => unary(self, &|tr, v| tr.b.ins().fneg(v)),
            M::Fabs => unary(self, &|tr, v| tr.b.ins().fabs(v)),
            M::Fsqrt => unary(self, &|tr, v| tr.b.ins().sqrt(v)),
            M::Fsin => unary(self, &|tr, v| tr.math1(v, Math1::Sin)),
            M::Fcos => unary(self, &|tr, v| tr.math1(v, Math1::Cos)),
            M::F2xm1 => unary(self, &|tr, v| tr.math1(v, Math1::Exp2m1)),
            M::Frndint => unary(self, &|tr, v| {
                let ctx = tr.ctx;
                tr.call_pure("rcn_op_fround", &[I64, F64], Some(F64), &[ctx, v]).unwrap()
            }),
            M::Fsincos => {
                let v = self.st(0);
                let s = self.math1(v, Math1::Sin);
                let c = self.math1(v, Math1::Cos);
                self.set_st(0, s);
                self.fpush(c);
                Some(())
            }
            M::Fptan => {
                let v = self.st(0);
                let t = self.math1(v, Math1::Tan);
                self.set_st(0, t);
                constant(self, 1.0)
            }
            M::Fpatan | M::Fyl2x | M::Fyl2xp1 => {
                let x = self.st(0);
                let y = self.st(1);
                let r = match m {
                    M::Fpatan => self.math2(y, x, Math2::Atan2),
                    M::Fyl2x => self.math2(y, x, Math2::Yl2x),
                    _ => self.math2(y, x, Math2::Yl2xp1),
                };
                self.set_st(1, r);
                self.fpop();
                Some(())
            }
            M::Fscale => {
                let a = self.st(0);
                let b = self.st(1);
                let r = self.math2(a, b, Math2::Scalbn);
                self.set_st(0, r);
                Some(())
            }
            M::Fprem | M::Fprem1 => {
                let a = self.st(0);
                let b = self.st(1);
                let r = self.math2(a, b, if m == M::Fprem { Math2::Fmod } else { Math2::Remainder });
                self.set_st(0, r);
                let sw = self.ctx_load(I16, OFF_FPSW);
                let sw = self.b.ins().band_imm_s(sw, !0x0400i64);
                self.ctx_store(sw, OFF_FPSW);
                Some(())
            }
            M::Fxch => {
                let i = if ins.op_count() == 0 { 1 } else { Self::st_index(ins, ins.op_count() - 1)? };
                let a = self.st(0);
                let b = self.st(i);
                self.set_st(0, b);
                self.set_st(i, a);
                Some(())
            }
            M::Fld1 => constant(self, 1.0),
            M::Fldz => constant(self, 0.0),
            M::Fldpi => constant(self, std::f64::consts::PI),
            M::Fldl2e => constant(self, std::f64::consts::LOG2_E),
            M::Fldl2t => constant(self, std::f64::consts::LOG2_10),
            M::Fldlg2 => constant(self, std::f64::consts::LOG10_2),
            M::Fldln2 => constant(self, std::f64::consts::LN_2),
            M::Fcom | M::Fucom | M::Ficom => compare(self, 0),
            M::Fcomp | M::Fucomp | M::Ficomp => compare(self, 1),
            M::Fcompp | M::Fucompp => compare(self, 2),
            M::Ftst => {
                let a = self.st(0);
                let z = self.b.ins().f64const(0.0);
                let ctx = self.ctx;
                self.call_pure("rcn_op_fcom", &[I64, F64, F64], None, &[ctx, a, z]);
                Some(())
            }
            M::Fxam => {
                let ctx = self.ctx;
                self.call_pure("rcn_fxam", &[I64], None, &[ctx]);
                Some(())
            }
            M::Fcomi | M::Fucomi => compare_flags(self, false),
            M::Fcomip | M::Fucomip => compare_flags(self, true),
            M::Fcmovb | M::Fcmove | M::Fcmovbe | M::Fcmovu | M::Fcmovnb | M::Fcmovne | M::Fcmovnbe | M::Fcmovnu => {
                let cc = match m {
                    M::Fcmovb => ConditionCode::b,
                    M::Fcmove => ConditionCode::e,
                    M::Fcmovbe => ConditionCode::be,
                    M::Fcmovu => ConditionCode::p,
                    M::Fcmovnb => ConditionCode::ae,
                    M::Fcmovne => ConditionCode::ne,
                    M::Fcmovnbe => ConditionCode::a,
                    _ => ConditionCode::np,
                };
                let c = self.cond(cc)?;
                cmov(self, c)
            }
            M::Fnstsw => {
                let ctx = self.ctx;
                let sw = self.call_pure("rcn_op_fsw", &[I64], Some(I32), &[ctx])?;
                self.wr(ins, 0, 2, sw)
            }
            M::Fnstcw => {
                let cw = self.ctx_load(I16, OFF_FPCW);
                self.wr(ins, 0, 2, cw)
            }
            M::Fldcw => {
                let v = self.rd(ins, 0, 2)?;
                self.ctx_store(v, OFF_FPCW);
                Some(())
            }
            M::Fninit => {
                let cw = self.b.ins().iconst(I16, 0x037F);
                let zero16 = self.b.ins().iconst(I16, 0);
                let zero8 = self.b.ins().iconst(I8, 0);
                self.ctx_store(cw, OFF_FPCW);
                self.ctx_store(zero16, OFF_FPSW);
                self.ctx_store(zero8, OFF_FPTOP);
                Some(())
            }
            M::Fnclex | M::Ffree | M::Fnop => Some(()),
            M::Fincstp => {
                self.fpop();
                Some(())
            }
            M::Fdecstp => {
                let top = self.ctx_load(I8, OFF_FPTOP);
                let top = self.b.ins().iadd_imm_s(top, -1);
                let top = self.b.ins().band_imm_s(top, 7);
                self.ctx_store(top, OFF_FPTOP);
                Some(())
            }
            _ => None,
        }
    }

    // --- Une instruction ---

    fn instruction(&mut self, ins: &Instruction, rva: u32, need: u32) -> Option<()> {
        let m = ins.mnemonic();
        let next = rva + ins.len() as u32;
        let is_string = matches!(
            ins.op0_kind(),
            OpKind::MemoryESRDI | OpKind::MemoryESEDI | OpKind::MemorySegRSI | OpKind::MemorySegESI
        ) || matches!(ins.op1_kind(), OpKind::MemorySegRSI | OpKind::MemoryESRDI | OpKind::MemorySegESI | OpKind::MemoryESEDI);
        if is_string {
            let size = ins.memory_size().size() as i64;
            let rep = if ins.has_repne_prefix() {
                2
            } else if ins.has_rep_prefix() || ins.has_repe_prefix() {
                1
            } else {
                0
            };
            let name = match m {
                M::Movsb | M::Movsw | M::Movsd | M::Movsq => "rcn_movs",
                M::Stosb | M::Stosw | M::Stosd | M::Stosq => "rcn_stos",
                M::Lodsb | M::Lodsw | M::Lodsd | M::Lodsq => "rcn_lods",
                M::Cmpsb | M::Cmpsw | M::Cmpsd | M::Cmpsq => "rcn_cmps",
                M::Scasb | M::Scasw | M::Scasd | M::Scasq => "rcn_scas",
                _ => return None,
            };
            let ctx = self.ctx;
            let s = self.b.ins().iconst(I32, size);
            let r = self.b.ins().iconst(I32, rep);
            self.call_helper(name, &[I64, I32, I32], None, &[ctx, s, r]);
            return Some(());
        }
        if NOPS.contains(&m) {
            return Some(());
        }
        if matches!(m, M::Loope | M::Loopne) {
            return None;
        }
        if let Some(c) = self.cond(ins.condition_code()) {
            if ins.flow_control() == FlowControl::ConditionalBranch {
                let target = self.jump_block(ins, rva);
                let fall = self.block_of(next);
                self.b.ins().brif(c, target, &[], fall, &[]);
                self.terminated = true;
                return Some(());
            }
            if ins.op_count() == 1 {
                return self.wr(ins, 0, 1, c);
            }
            let size = self.opsize(ins, 0);
            let a = self.rd(ins, 0, size)?;
            let b = self.rd(ins, 1, size)?;
            let v = self.b.ins().select(c, b, a);
            return self.wr(ins, 0, size, v);
        }
        let size0 = if ins.op_count() > 0 { self.opsize(ins, 0) } else { 0 };
        let ps = self.ps();
        match m {
            M::Mov => {
                let v = self.rd(ins, 1, size0)?;
                self.wr(ins, 0, size0, v)
            }
            M::Movzx => {
                let v = self.rd(ins, 1, self.opsize(ins, 1))?;
                let v = self.b.ins().uextend(ty(size0), v);
                self.wr(ins, 0, size0, v)
            }
            M::Movsx | M::Movsxd => {
                let src = self.opsize(ins, 1);
                let v = self.rd(ins, 1, src)?;
                let v = if src == size0 { v } else { self.b.ins().sextend(ty(size0), v) };
                self.wr(ins, 0, size0, v)
            }
            M::Lea => {
                let ea = self.ea(ins)?;
                self.wr(ins, 0, size0, ea)
            }
            M::Add | M::Adc | M::Sub | M::Sbb | M::Cmp | M::And | M::Test | M::Or | M::Xor => self.alu(ins, need, m),
            M::Inc | M::Dec => {
                let n = size0 * 8;
                let t = ty(size0);
                let locked = ins.has_lock_prefix() && ins.op0_kind() == OpKind::Memory;
                let one = self.b.ins().iconst(t, 1);
                let a = if locked {
                    let ea = self.ea(ins)?;
                    let op = if m == M::Inc { AtomicRmwOp::Add } else { AtomicRmwOp::Sub };
                    self.b.ins().atomic_rmw(t, MemFlagsData::new(), op, ea, one)
                } else {
                    self.rd(ins, 0, size0)?
                };
                let r = if m == M::Inc { self.b.ins().iadd(a, one) } else { self.b.ins().isub(a, one) };
                self.put_flag(need, OF, |tr| {
                    let x = if m == M::Inc {
                        let na = tr.b.ins().bnot(a);
                        tr.b.ins().band(na, r)
                    } else {
                        let nr = tr.b.ins().bnot(r);
                        tr.b.ins().band(a, nr)
                    };
                    tr.bit(x, n - 1)
                });
                self.put_flag(need, AF, |tr| {
                    let x = tr.b.ins().bxor(a, r);
                    tr.bit(x, 4)
                });
                self.zsp(need, r);
                if !locked {
                    self.wr(ins, 0, size0, r)?;
                }
                Some(())
            }
            M::Neg | M::Not | M::Shl | M::Shr | M::Sar | M::Rol | M::Ror if ins.has_lock_prefix() && ins.op0_kind() == OpKind::Memory => None,
            M::Neg => {
                let a = self.rd(ins, 0, size0)?;
                let r = self.b.ins().ineg(a);
                self.put_flag(need, CF, |tr| tr.b.ins().icmp_imm_s(IntCC::NotEqual, a, 0));
                self.put_flag(need, OF, |tr| {
                    let x = tr.b.ins().band(a, r);
                    tr.sign(x)
                });
                self.put_flag(need, AF, |tr| {
                    let x = tr.b.ins().bxor(a, r);
                    tr.bit(x, 4)
                });
                self.zsp(need, r);
                self.wr(ins, 0, size0, r)
            }
            M::Not => {
                let a = self.rd(ins, 0, size0)?;
                let r = self.b.ins().bnot(a);
                self.wr(ins, 0, size0, r)
            }
            M::Shl | M::Sal | M::Shr | M::Sar | M::Rol | M::Ror => self.shift(ins, need),
            M::Shld | M::Shrd => self.shift_double(ins, need),
            M::Imul | M::Mul | M::Div | M::Idiv => self.mul_div(ins, need, rva),
            M::Push => {
                let v = match ins.op0_kind() {
                    k if is_imm(k) => self.b.ins().iconst(I64, ins.immediate(0) as i64),
                    _ if size0 == ps => self.rd(ins, 0, ps)?,
                    _ => return None,
                };
                self.push(v);
                Some(())
            }
            M::Pop => {
                if size0 != ps {
                    return None;
                }
                let v = self.pop();
                // pop vers la mémoire : l'adresse se calcule après l'incrément de la pile.
                self.wr(ins, 0, ps, v)
            }
            M::Leave => {
                let bp = self.get(5);
                self.set(4, bp);
                let v = self.pop();
                let v = self.to64(v);
                self.set(5, v);
                Some(())
            }
            M::Pushad if ps == 4 => {
                let sp = self.get(4);
                for r in [0, 1, 2, 3] {
                    let v = self.get(r);
                    self.push(v);
                }
                self.push(sp);
                for r in [5, 6, 7] {
                    let v = self.get(r);
                    self.push(v);
                }
                Some(())
            }
            M::Popad if ps == 4 => {
                let sp = self.get(4);
                for (i, r) in [(0, 7), (4, 6), (8, 5), (16, 3), (20, 2), (24, 1), (28, 0)] {
                    let v = self.b.ins().load(I32, MemFlagsData::new(), sp, i);
                    let v = self.b.ins().uextend(I64, v);
                    self.set(r, v);
                }
                self.add_sp(32);
                Some(())
            }
            M::Pushfd | M::Pushfq => {
                let ctx = self.ctx;
                let f = self.call_helper("rcn_get_flags", &[I64], Some(I32), &[ctx])?;
                self.push(f);
                Some(())
            }
            M::Popfd | M::Popfq => {
                let v = self.pop();
                let v = self.to64(v);
                let ctx = self.ctx;
                self.call_helper("rcn_set_flags", &[I64, I64], None, &[ctx, v]);
                Some(())
            }
            M::Sahf | M::Lahf => {
                let ctx = self.ctx;
                let name = if m == M::Sahf { "rcn_op_sahf" } else { "rcn_op_lahf" };
                self.call_helper(name, &[I64], None, &[ctx]);
                Some(())
            }
            M::Call => {
                let direct = branch_target(self.n.pe, ins);
                let slot = if ins.op0_kind() == OpKind::Memory { mem_target(self.n.pe, ins) } else { None };
                if direct.is_some_and(|t| self.n.setjmp_thunks.contains(&t)) || slot.is_some_and(|s| is_setjmp(self.n.pe, s)) {
                    return self.setjmp_site();
                }
                let ret_addr = self.b.ins().iadd_imm_s(self.base, next as i64);
                if is_near_branch(ins) {
                    match direct {
                        Some(t) if self.n.funcs.contains(&t) => {
                            self.push(ret_addr);
                            self.call_func(t);
                        }
                        _ => self.trap(rva, "appel hors du code"),
                    }
                } else {
                    let t = self.rd(ins, 0, ps)?;
                    let t = self.to64(t);
                    self.push(ret_addr);
                    self.call_address(t);
                }
                Some(())
            }
            M::Ret => {
                let extra = if ins.op_count() == 1 { ins.immediate(0) } else { 0 };
                self.add_sp(ps as i64 + extra as i64);
                self.ret();
                Some(())
            }
            M::Jmp => {
                if is_near_branch(ins) {
                    let b = self.jump_block(ins, rva);
                    self.jump_to(b);
                    return Some(());
                }
                let t = self.rd(ins, 0, ps)?;
                let t = self.to64(t);
                let fallback = self.b.create_block();
                if let Some(targets) = self.f.jump_tables.get(&rva).cloned() {
                    let offset = self.b.ins().isub(t, self.base);
                    let mut sw = Switch::new();
                    for target in targets {
                        let block = self.block_of(target);
                        sw.set_entry(target as u128, block);
                    }
                    sw.emit(&mut self.b, offset, fallback);
                } else {
                    self.b.ins().jump(fallback, &[]);
                }
                // Hors table : saut terminal vers une autre fonction ou une fonction Windows.
                self.b.switch_to_block(fallback);
                self.call_address(t);
                self.ret();
                Some(())
            }
            M::Jrcxz | M::Jecxz | M::Loop => {
                let c = self.get(1);
                let test = if m == M::Loop {
                    let one = self.b.ins().iadd_imm_s(c, -1);
                    let one = if self.bits32() {
                        let low = self.b.ins().ireduce(I32, one);
                        self.b.ins().uextend(I64, low)
                    } else {
                        one
                    };
                    self.set(1, one);
                    self.b.ins().icmp_imm_s(IntCC::NotEqual, one, 0)
                } else if m == M::Jecxz {
                    let low = self.b.ins().ireduce(I32, c);
                    self.b.ins().icmp_imm_s(IntCC::Equal, low, 0)
                } else {
                    self.b.ins().icmp_imm_s(IntCC::Equal, c, 0)
                };
                let target = self.jump_block(ins, rva);
                let fall = self.block_of(next);
                self.b.ins().brif(test, target, &[], fall, &[]);
                self.terminated = true;
                Some(())
            }
            M::Cbw => {
                let al = self.rd_reg(Register::AL)?;
                let v = self.b.ins().sextend(I16, al);
                self.wr_reg(Register::AX, v)
            }
            M::Cwde => {
                let ax = self.rd_reg(Register::AX)?;
                let v = self.b.ins().sextend(I32, ax);
                self.wr_reg(Register::EAX, v)
            }
            M::Cdqe => {
                let eax = self.rd_reg(Register::EAX)?;
                let v = self.b.ins().sextend(I64, eax);
                self.set(0, v);
                Some(())
            }
            M::Cwd | M::Cdq | M::Cqo => {
                let (src, dst, bits) = match m {
                    M::Cwd => (Register::AX, Register::DX, 15),
                    M::Cdq => (Register::EAX, Register::EDX, 31),
                    _ => (Register::RAX, Register::RDX, 63),
                };
                let a = self.rd_reg(src)?;
                let v = self.b.ins().sshr_imm_s(a, bits);
                self.wr_reg(dst, v)
            }
            M::Xchg if ins.op0_kind() == OpKind::Memory || ins.op1_kind() == OpKind::Memory => {
                let reg = if ins.op0_kind() == OpKind::Memory { 1 } else { 0 };
                let v = self.rd(ins, reg, size0)?;
                let ea = self.ea(ins)?;
                let old = self.b.ins().atomic_rmw(ty(size0), MemFlagsData::new(), AtomicRmwOp::Xchg, ea, v);
                self.wr(ins, reg, size0, old)
            }
            M::Xchg => {
                let a = self.rd(ins, 0, size0)?;
                let b = self.rd(ins, 1, size0)?;
                self.wr(ins, 0, size0, b)?;
                self.wr(ins, 1, size0, a)
            }
            M::Xadd => {
                let n = size0 * 8;
                let locked = ins.has_lock_prefix() && ins.op0_kind() == OpKind::Memory;
                let b = self.rd(ins, 1, size0)?;
                let a = if locked {
                    let ea = self.ea(ins)?;
                    self.b.ins().atomic_rmw(ty(size0), MemFlagsData::new(), AtomicRmwOp::Add, ea, b)
                } else {
                    self.rd(ins, 0, size0)?
                };
                let r = self.b.ins().iadd(a, b);
                self.put_flag(need, CF, |tr| tr.b.ins().icmp(IntCC::UnsignedLessThan, r, a));
                self.put_flag(need, OF, |tr| {
                    let x = tr.b.ins().bxor(a, r);
                    let y = tr.b.ins().bxor(b, r);
                    let z = tr.b.ins().band(x, y);
                    tr.sign(z)
                });
                self.af(need, a, b, r);
                self.zsp(need, r);
                let _ = n;
                self.wr(ins, 1, size0, a)?;
                if !locked {
                    self.wr(ins, 0, size0, r)?;
                }
                Some(())
            }
            M::Cmpxchg => {
                let acc = match size0 {
                    1 => Register::AL,
                    2 => Register::AX,
                    4 => Register::EAX,
                    _ => Register::RAX,
                };
                let locked = ins.has_lock_prefix() && ins.op0_kind() == OpKind::Memory;
                let a = self.rd_reg(acc)?;
                let src = self.rd(ins, 1, size0)?;
                let b = if locked {
                    let ea = self.ea(ins)?;
                    self.b.ins().atomic_cas(MemFlagsData::new(), ea, a, src)
                } else {
                    self.rd(ins, 0, size0)?
                };
                let r = self.b.ins().isub(a, b);
                self.put_flag(need, CF, |tr| tr.b.ins().icmp(IntCC::UnsignedLessThan, a, b));
                self.put_flag(need, OF, |tr| {
                    let x = tr.b.ins().bxor(a, b);
                    let y = tr.b.ins().bxor(a, r);
                    let z = tr.b.ins().band(x, y);
                    tr.sign(z)
                });
                self.af(need, a, b, r);
                self.zsp(need, r);
                let equal = self.b.ins().icmp(IntCC::Equal, a, b);
                if locked {
                    // Échange fait si égal ; sinon l'accumulateur reçoit la valeur lue.
                    let v = self.b.ins().select(equal, a, b);
                    self.wr_reg(acc, v)?;
                } else {
                    let ins_copy = *ins;
                    self.if_else(
                        equal,
                        |tr| {
                            tr.wr(&ins_copy, 0, size0, src);
                        },
                        |tr| {
                            tr.wr_reg(acc, b);
                        },
                    );
                }
                Some(())
            }
            M::Bt | M::Bts | M::Btr | M::Btc => {
                if ins.op0_kind() == OpKind::Memory && ins.op1_kind() == OpKind::Register {
                    return None;
                }
                let t = ty(size0);
                let n = size0 * 8;
                let idx = match ins.op1_kind() {
                    OpKind::Register => self.rd(ins, 1, size0)?,
                    _ => self.iconst(t, ins.immediate(1)),
                };
                let c = self.b.ins().band_imm_s(idx, (n - 1) as i64);
                let one = self.b.ins().iconst(t, 1);
                let bitmask = self.b.ins().ishl(one, c);
                let locked = ins.has_lock_prefix() && ins.op0_kind() == OpKind::Memory;
                let a = if locked && m != M::Bt {
                    let ea = self.ea(ins)?;
                    let (op, operand) = match m {
                        M::Bts => (AtomicRmwOp::Or, bitmask),
                        M::Btr => (AtomicRmwOp::And, self.b.ins().bnot(bitmask)),
                        _ => (AtomicRmwOp::Xor, bitmask),
                    };
                    self.b.ins().atomic_rmw(t, MemFlagsData::new(), op, ea, operand)
                } else {
                    self.rd(ins, 0, size0)?
                };
                self.put_flag(need, CF, |tr| {
                    let s = tr.b.ins().ushr(a, c);
                    tr.bit(s, 0)
                });
                if m == M::Bt || locked {
                    return Some(());
                }
                let v = match m {
                    M::Bts => self.b.ins().bor(a, bitmask),
                    M::Btr => {
                        let nm = self.b.ins().bnot(bitmask);
                        self.b.ins().band(a, nm)
                    }
                    _ => self.b.ins().bxor(a, bitmask),
                };
                self.wr(ins, 0, size0, v)
            }
            M::Bsf | M::Bsr => {
                let n = size0 * 8;
                let a = self.rd(ins, 1, size0)?;
                self.put_flag(need, ZF, |tr| tr.b.ins().icmp_imm_s(IntCC::Equal, a, 0));
                let nonzero = self.b.ins().icmp_imm_s(IntCC::NotEqual, a, 0);
                let r = if m == M::Bsf {
                    self.b.ins().ctz(a)
                } else {
                    let lz = self.b.ins().clz(a);
                    let top = self.iconst(ty(size0), (n - 1) as u64);
                    self.b.ins().isub(top, lz)
                };
                let dest = ins.op0_register();
                self.if_then(nonzero, |tr| {
                    tr.wr_reg(dest, r);
                });
                Some(())
            }
            M::Tzcnt | M::Lzcnt | M::Popcnt => {
                let a = self.rd(ins, 1, size0)?;
                let r = match m {
                    M::Tzcnt => self.b.ins().ctz(a),
                    M::Lzcnt => self.b.ins().clz(a),
                    _ => self.b.ins().popcnt(a),
                };
                if m == M::Popcnt {
                    self.put_flag(need, ZF, |tr| tr.b.ins().icmp_imm_s(IntCC::Equal, a, 0));
                    for i in [CF, OF, SF, AF, PF] {
                        self.put_flag(need, i, |tr| tr.zero8());
                    }
                } else {
                    self.put_flag(need, CF, |tr| tr.b.ins().icmp_imm_s(IntCC::Equal, a, 0));
                    self.put_flag(need, ZF, |tr| tr.b.ins().icmp_imm_s(IntCC::Equal, r, 0));
                }
                self.wr(ins, 0, size0, r)
            }
            M::Bswap => {
                if size0 < 4 {
                    return None;
                }
                let a = self.rd(ins, 0, size0)?;
                let r = self.b.ins().bswap(a);
                self.wr(ins, 0, size0, r)
            }
            M::Cld | M::Std => {
                let v = self.b.ins().iconst(I8, (m == M::Std) as i64);
                self.ctx_store(v, OFF_DF);
                Some(())
            }
            M::Clc | M::Stc => {
                let v = self.b.ins().iconst(I8, (m == M::Stc) as i64);
                self.set_flag(CF, v);
                Some(())
            }
            M::Cmc => {
                let c = self.flag(CF);
                let v = self.not(c);
                self.set_flag(CF, v);
                Some(())
            }
            M::Cpuid | M::Rdtsc => {
                let ctx = self.ctx;
                self.call_helper(if m == M::Cpuid { "rcn_cpuid" } else { "rcn_rdtsc" }, &[I64], None, &[ctx]);
                Some(())
            }
            M::Xgetbv => {
                let three = self.b.ins().iconst(I64, 3);
                let zero = self.b.ins().iconst(I64, 0);
                self.set(0, three);
                self.set(2, zero);
                Some(())
            }
            M::Stmxcsr => {
                let v = self.ctx_load(I32, OFF_MXCSR);
                self.wr(ins, 0, 4, v)
            }
            M::Ldmxcsr => {
                let v = self.rd(ins, 0, 4)?;
                self.ctx_store(v, OFF_MXCSR);
                Some(())
            }
            M::Int3 | M::Ud2 | M::Int | M::Hlt | M::Syscall => {
                self.trap(rva, &format!("{m:?}").to_lowercase());
                Some(())
            }
            _ => self.x87(ins).or_else(|| self.sse(ins)),
        }
    }

    /// Appel de setjmp : la fonction de l'hôte enregistre les registres et rend 0 ; un longjmp vers ce
    /// point remonte les appels jusqu'ici (voir check_unwind) et reprend après l'appel, rax rétabli.
    fn setjmp_site(&mut self) -> Option<()> {
        let token = self.frame_token?;
        let buf = if self.bits32() {
            let sp = self.get(4);
            let v = self.mem_load(I32, sp);
            self.b.ins().uextend(I64, v)
        } else {
            self.get(1)
        };
        let site = self.setjmp_sites.len();
        let cont = self.b.create_block();
        self.setjmp_sites.push(cont);
        let ctx = self.ctx;
        let site_v = self.b.ins().iconst(I32, site as i64);
        self.call_helper("rcn_setjmp_native", &[I64, I64, I64, I32], None, &[ctx, buf, token, site_v]);
        self.b.ins().jump(cont, &[]);
        self.b.switch_to_block(cont);
        self.reload();
        Some(())
    }

    fn start(&mut self) {
        let entry = self.b.create_block();
        self.b.append_block_params_for_function_params(entry);
        self.b.switch_to_block(entry);
        self.ctx = self.b.block_params(entry)[0];
        self.base = self.ctx_load(I64, OFF_BASE);
        self.reload();
    }

    /// La fonction appelle-t-elle setjmp ?
    fn calls_setjmp(&self) -> bool {
        self.f.insts.values().any(|ins| {
            ins.mnemonic() == M::Call
                && (branch_target(self.n.pe, ins).is_some_and(|t| self.n.setjmp_thunks.contains(&t))
                    || (ins.op0_kind() == OpKind::Memory && mem_target(self.n.pe, ins).is_some_and(|s| is_setjmp(self.n.pe, s))))
        })
    }

    fn body(&mut self) {
        self.start();
        if self.calls_setjmp() {
            // Jeton : adresse d'une case de la pile de l'hôte, propre à cet appel de la fonction.
            let slot = self.b.create_sized_stack_slot(StackSlotData::new(StackSlotKind::ExplicitSlot, 8, 3));
            self.frame_token = Some(self.b.ins().stack_addr(I64, slot, 0));
        }
        let f = self.f;
        let live = flag_liveness(self.n.pe, f);
        let keys: Vec<u32> = f.insts.keys().copied().collect();
        if f.insts.contains_key(&f.entry) {
            let b = self.block_of(f.entry);
            self.jump_to(b);
        } else {
            self.trap(f.entry, "code illisible");
        }
        for (i, &rva) in keys.iter().enumerate() {
            let ins = f.insts[&rva];
            if f.leaders.contains(&rva) || self.blocks.contains_key(&rva) {
                let b = self.block_of(rva);
                if !self.terminated {
                    self.b.ins().jump(b, &[]);
                }
                self.b.switch_to_block(b);
                self.filled.insert(b);
                self.terminated = false;
            } else if self.terminated {
                // Code atteint seulement par un chemin inconnu : bloc propre, registres relus.
                let b = self.b.create_block();
                self.b.switch_to_block(b);
                self.reload();
                self.terminated = false;
            }
            self.n.total += 1;
            let need = live.get(&rva).copied().unwrap_or(0);
            if self.instruction(&ins, rva, need).is_none() {
                let mut text = String::new();
                self.fmt.format(&ins, &mut text);
                *self.n.unimplemented.entry(format!("{:?}", ins.mnemonic()).to_lowercase()).or_default() += 1;
                if !self.terminated {
                    self.trap(rva, &format!("instruction non geree : {text}"));
                }
            }
            if self.terminated {
                continue;
            }
            let next = rva + ins.len() as u32;
            if falls_through(&ins) && keys.get(i + 1) != Some(&next) {
                if f.insts.contains_key(&next) {
                    let b = self.block_of(next);
                    self.jump_to(b);
                } else {
                    self.trap(next, "fin du code traduit");
                }
            }
        }
        if !self.terminated {
            let end = keys.last().copied().unwrap_or(f.entry);
            self.trap(end, "fin du code traduit");
        }
        // Destinations jamais atteintes par le parcours (suite d'un saut vers du code non décodé) : arrêt.
        let mut empty: Vec<(u32, Block)> = self.blocks.iter().filter(|(_, b)| !self.filled.contains(b)).map(|(&r, &b)| (r, b)).collect();
        empty.sort_by_key(|&(r, _)| r);
        for (rva, b) in empty {
            self.b.switch_to_block(b);
            self.trap(rva, "fin du code traduit");
        }
        // Remontée d'un longjmp : reprise à un setjmp de cette fonction, ou retour à l'appelant.
        if let Some(unwind) = self.unwind_block {
            self.b.switch_to_block(unwind);
            let sites = self.setjmp_sites.clone();
            if let Some(token) = self.frame_token.filter(|_| !sites.is_empty()) {
                let target = self.ctx_load(I64, OFF_UNWIND);
                let mine = self.b.ins().icmp(IntCC::Equal, target, token);
                let resume = self.b.create_block();
                let leave = self.b.create_block();
                self.b.ins().brif(mine, resume, &[], leave, &[]);
                self.b.switch_to_block(resume);
                let zero = self.b.ins().iconst(I64, 0);
                self.ctx_store(zero, OFF_UNWIND);
                let site = self.ctx_load(I32, OFF_UNWIND_SITE);
                let mut sw = Switch::new();
                for (i, &b) in sites.iter().enumerate() {
                    sw.set_entry(i as u128, b);
                }
                sw.emit(&mut self.b, site, leave);
                self.b.switch_to_block(leave);
            }
            self.b.ins().return_(&[]);
        }
    }
}
