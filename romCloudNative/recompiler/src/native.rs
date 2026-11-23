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
use crate::pe::{Pe, SCN_MEM_WRITE};
use cranelift_codegen::control::ControlPlane;
use cranelift_codegen::ir::condcodes::{FloatCC, IntCC};
use cranelift_codegen::ir::types::{self, Type};
use cranelift_codegen::ir::immediates::Imm64;
use cranelift_codegen::ir::{ConstantData, Endianness};
use cranelift_codegen::ir::{
    AbiParam, AtomicRmwOp, Block, ExtFuncData, ExternalName, FuncRef, Function, GlobalValueData, InstBuilder, MemFlagsData,
    Signature, StackSlotData, StackSlotKind, UserExternalName, UserFuncName, Value,
};
use cranelift_codegen::isa::{CallConv, OwnedTargetIsa};
use cranelift_codegen::settings::{self, Configurable};
use cranelift_codegen::{binemit::Reloc, Context, RelocTarget};
use cranelift_frontend::{FunctionBuilder, FunctionBuilderContext, Switch, Variable};
use iced_x86::{Code, ConditionCode, FlowControl, Formatter, Instruction, IntelFormatter, MemorySize, Mnemonic as M, OpKind, Register, RflagsBits as F};
use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::io::Write;

// Disposition de RcnCtx (rcn.h, vérifiée là par des assertions à la compilation).
const OFF_R: i32 = 0;
const OFF_XMM: i32 = 128;
const OFF_FLAGS: i32 = 384; // cf, pf, af, zf, sf, of
const OFF_DF: i32 = 390;
const OFF_MXCSR: i32 = 392;
const OFF_FS: i32 = 416;
const OFF_GS: i32 = 424;
const OFF_ST: i32 = 440;
const OFF_FPTOP: i32 = 504;
const OFF_FPSW: i32 = 506;
const OFF_FPCW: i32 = 508;
const OFF_UNWIND: i32 = 512;
const OFF_UNWIND_SITE: i32 = 520;
const OFF_RESUME_RSP: i32 = 528;
const OFF_RESUME_RIP: i32 = 536;
/// AVX : moitiés hautes de ymm0–15, puis deux registres de travail (rcn.h).
const OFF_YMMH: i32 = 544;
const OFF_VSCRATCH: i32 = 800;
/// Registres vectoriels de 128 bits suivis par le traducteur : xmm0–15, moitiés hautes de ymm0–15 (16–31),
/// registres de travail (32, 33).
const XREGS: usize = 34;
const XSCRATCH: usize = 32;
/// Position dans ctx du registre de 128 bits `n` (voir XREGS).
fn xoff(n: usize) -> i32 {
    match n {
        0..=15 => OFF_XMM + 16 * n as i32,
        16..=31 => OFF_YMMH + 16 * (n as i32 - 16),
        _ => OFF_VSCRATCH + 16 * (n as i32 - 32),
    }
}
/// Valeur de ctx.unwind pour une reprise non locale (fin d'un catch, RtlUnwindEx) : les jetons de setjmp
/// sont des adresses de pile, jamais 1.
const UNWIND_RESUME: i64 = 1;

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

/// Types de relocalisations : RELOC_ABS8 est la seule qui reste dans le fichier .rcnx (cases de la table
/// des symboles, remplies par le chargeur) ; les autres, relatives au compteur ordinal, sont résolues à
/// l'écriture du fichier (le code ne change plus au chargement).
pub const RELOC_ABS8: u8 = 1;
pub const RELOC_PCREL4: u8 = 2;
pub const RELOC_ARM64_CALL: u8 = 3;
const RELOC_GOT_PAGE21: u8 = 4; // adrp vers la case du symbole dans la table
const RELOC_GOT_LO12: u8 = 5; // ldr x, [x, :lo12:case]
const RELOC_PAGE21: u8 = 6; // adrp vers le symbole lui-même (fonction traduite)
const RELOC_ADD_LO12: u8 = 7; // add x, x, :lo12:symbole
const RELOC_X86_GOTPCREL4: u8 = 8; // [rip + case]

/// Le code est suivi, à cet alignement, de la table des symboles (adresses des fonctions du runtime, de
/// la case « @base »…) : l'alignement permet de projeter le code directement depuis le fichier.
pub const CODE_ALIGN: usize = 64 << 10;

fn reloc_kind(kind: Reloc) -> Option<u8> {
    Some(match kind {
        Reloc::Abs8 => RELOC_ABS8,
        Reloc::X86CallPCRel4 | Reloc::X86PCRel4 | Reloc::X86CallPLTRel4 => RELOC_PCREL4,
        Reloc::X86GOTPCRel4 => RELOC_X86_GOTPCREL4,
        Reloc::Arm64Call => RELOC_ARM64_CALL,
        Reloc::Aarch64AdrGotPage21 => RELOC_GOT_PAGE21,
        Reloc::Aarch64Ld64GotLo12Nc => RELOC_GOT_LO12,
        Reloc::Aarch64AdrPrelPgHi21 => RELOC_PAGE21,
        Reloc::Aarch64AddAbsLo12Nc => RELOC_ADD_LO12,
        _ => return None,
    })
}

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
    M::Pmaddwd, M::Haddps, M::Paddsb, M::Psubsb, M::Paddsw, M::Psubsw, M::Paddusw, M::Psubusw, M::Pmulhw, M::Pmulhuw, M::Pavgw, M::Psadbw,
    M::Addsubps, M::Addsubpd, M::Hsubps, M::Haddpd, M::Hsubpd, M::Pmaddubsw, M::Pshufb,
    M::Phaddw, M::Phaddd, M::Phaddsw, M::Phsubw, M::Phsubd, M::Phsubsw, M::Pmulhrsw, M::Psignb, M::Psignw, M::Psignd,
    M::Pmuldq,
];
// MMX (registres mm0–mm7 de 64 bits, rangés avec ceux du x87) : opérations faites par le runtime
// (rcn_op_mmx / rcn_op_mmx_shift), désignées par leur rang dans ces listes — même ordre que rcn_ops.c.
const MMX_BINARY: &[M] = &[
    M::Paddb, M::Paddw, M::Paddd, M::Paddq, M::Psubb, M::Psubw, M::Psubd, M::Psubq,
    M::Paddsb, M::Paddsw, M::Paddusb, M::Paddusw, M::Psubsb, M::Psubsw, M::Psubusb, M::Psubusw,
    M::Pmullw, M::Pmulhw, M::Pmulhuw, M::Pmaddwd, M::Pmuludq,
    M::Pand, M::Pandn, M::Por, M::Pxor,
    M::Pcmpeqb, M::Pcmpeqw, M::Pcmpeqd, M::Pcmpgtb, M::Pcmpgtw, M::Pcmpgtd,
    M::Punpcklbw, M::Punpcklwd, M::Punpckldq, M::Punpckhbw, M::Punpckhwd, M::Punpckhdq,
    M::Packsswb, M::Packssdw, M::Packuswb,
    M::Pminub, M::Pmaxub, M::Pminsw, M::Pmaxsw, M::Pavgb, M::Pavgw, M::Psadbw,
    // SSSE3 sur registres MMX
    M::Pmaddubsw, M::Pshufb, M::Pmulhrsw, M::Psignb, M::Psignw, M::Psignd, M::Phaddw, M::Phaddd, M::Phsubw, M::Phsubd,
];
// Opérations entières vectorielles traduites en ligne (instructions SIMD de Cranelift), en SSE2 comme en MMX.
const SIMD_INLINE: &[M] = &[
    M::Paddb, M::Paddw, M::Paddd, M::Paddq, M::Psubb, M::Psubw, M::Psubd, M::Psubq,
    M::Paddsb, M::Paddsw, M::Paddusb, M::Paddusw, M::Psubsb, M::Psubsw, M::Psubusb, M::Psubusw,
    M::Pmullw, M::Pmulhw, M::Pmulhuw, M::Pmaddwd, M::Pand, M::Pandn, M::Por, M::Pxor,
    M::Pcmpeqb, M::Pcmpeqw, M::Pcmpeqd, M::Pcmpgtb, M::Pcmpgtw, M::Pcmpgtd,
    M::Punpcklbw, M::Punpcklwd, M::Punpckldq, M::Punpcklqdq, M::Punpckhbw, M::Punpckhwd, M::Punpckhdq, M::Punpckhqdq,
    M::Packsswb, M::Packssdw, M::Packuswb, M::Pminub, M::Pmaxub, M::Pminsw, M::Pmaxsw, M::Pavgb, M::Pavgw,
    // SSE4.1 / SSE4.2
    M::Pminsb, M::Pmaxsb, M::Pminuw, M::Pmaxuw, M::Pminsd, M::Pmaxsd, M::Pminud, M::Pmaxud, M::Pcmpeqq, M::Pcmpgtq,
    M::Packusdw,
    // SSSE3 : filtres d'interpolation des décodeurs vidéo (libvpx d'Unity), très chers par le runtime.
    M::Pshufb, M::Pmaddubsw,
];
// Opérations flottantes et mélanges traduits en ligne (min/max restent au runtime : règles des NaN de x86).
const FLOAT_INLINE: &[M] = &[
    M::Addps, M::Subps, M::Mulps, M::Divps, M::Addpd, M::Subpd, M::Mulpd, M::Divpd, M::Sqrtps, M::Sqrtpd,
    M::Andps, M::Andpd, M::Orps, M::Orpd, M::Xorps, M::Xorpd, M::Andnps, M::Andnpd,
    M::Unpcklps, M::Unpckhps, M::Unpcklpd, M::Unpckhpd, M::Movlhps, M::Movhlps,
];
const SHUFFLE_INLINE: &[M] = &[M::Pshufd, M::Pshuflw, M::Pshufhw, M::Shufps, M::Shufpd];
// Décalages par bits traduits en ligne.
const SHIFT_INLINE: &[M] = &[M::Psllw, M::Pslld, M::Psllq, M::Psrlw, M::Psrld, M::Psrlq, M::Psraw, M::Psrad];
const MMX_SHIFT: &[M] = &[M::Psllw, M::Pslld, M::Psllq, M::Psrlw, M::Psrld, M::Psrlq, M::Psraw, M::Psrad];
const PACKED_UNARY: &[M] = &[
    M::Sqrtps, M::Sqrtpd, M::Cvtdq2ps, M::Cvtps2dq, M::Cvttps2dq, M::Cvtpd2ps, M::Cvttpd2dq, M::Cvtdq2pd, M::Cvtps2pd,
    M::Movddup, M::Cvtpd2dq, M::Movsldup, M::Movshdup, M::Rcpps, M::Rsqrtps, M::Pabsb, M::Pabsw, M::Pabsd,
    M::Phminposuw,
];
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
    entry_flags: &'a HashMap<u32, u32>, // drapeaux lus en entrée de chaque fonction (func::entry_flags)
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
    /// Ordre des accès mémoire de x86 (TSO) garanti sur ARM par des barrières (RCN_RECOMP_TSO=1) : pour les
    /// jeux dont le code sans verrou compte sur lui (Unreal Engine 5). Plus lent.
    tso: bool,
    /// Barrières posées en mode TSO (bilan).
    pub fences: usize,
    /// Dont barrières complètes (dmb ish) ; les autres : dmb ishld, dmb ishst.
    pub fences_full: usize,
    /// Mise au point (RCN_RECOMP_NAN=1) : chaque addition, soustraction, multiplication, division ou racine
    /// flottante qui rend un NaN sans en avoir reçu appelle rcn_op_nan_born (rva de l'instruction).
    nan_probe: bool,
    /// Fonctions dont l'entrée est signalée au runtime (RCN_RECOMP_PROBE, RVA en hexadécimal).
    probe_fns: std::collections::HashSet<u32>,
    /// Programme 32 bits : cases relocalisées (.reloc) situées dans le code. Les adresses absolues qu'elles
    /// désignent (déplacement ou valeur immédiate d'une instruction) sont rendues relatives à la base réelle
    /// de l'image : une DLL 32 bits peut alors être chargée ailleurs qu'à son adresse préférée.
    code_relocs: BTreeSet<u32>,
}

/// Architecture visée : celle de la machine (None) ou « aarch64 » / « x86_64 ».
pub fn make_isa(target: Option<&str>) -> Result<(OwnedTargetIsa, u32), String> {
    let mut flags = settings::builder();
    let set = |b: &mut settings::Builder, k: &str, v: &str| b.set(k, v).map_err(|e| format!("réglage {k} : {e}"));
    set(&mut flags, "opt_level", "speed")?;
    // Code indépendant de sa position : tout symbole externe passe par une table (GOT) placée après le
    // code, si bien que le chargeur ne modifie jamais le code lui-même.
    set(&mut flags, "is_pic", "true")?;
    set(&mut flags, "enable_probestack", "false")?;
    set(&mut flags, "enable_verifier", if cfg!(debug_assertions) { "true" } else { "false" })?;
    let flags = settings::Flags::new(flags);
    // L'architecture de la machine elle-même : son constructeur (convention d'appel de son système —
    // Windows x64 sous Windows —, celle du runtime qui appellera ce code), pas un triplet Linux.
    let host = match std::env::consts::ARCH {
        "x86_64" => Some("x86_64"),
        "aarch64" => Some("aarch64"),
        _ => None,
    };
    let same_as_host = |name: &str| {
        let arch = match name {
            "arm64" => "aarch64",
            "x64" => "x86_64",
            other => other,
        };
        host == Some(arch)
    };
    let builder = match target {
        None => cranelift_native::builder().map_err(|e| format!("processeur de la machine non géré : {e}"))?,
        Some(name) if same_as_host(name) => cranelift_native::builder().map_err(|e| format!("processeur de la machine non géré : {e}"))?,
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
    pub fn new(
        pe: &'a Pe, funcs: &'a BTreeSet<u32>, entry_flags: &'a HashMap<u32, u32>, setjmp_thunks: &'a BTreeSet<u32>, isa: OwnedTargetIsa,
        arch: u32,
    ) -> Self {
        Native {
            pe,
            funcs,
            entry_flags,
            setjmp_thunks,
            isa,
            tso: std::env::var("RCN_RECOMP_TSO").map(|v| v == "1").unwrap_or(false),
            fences: 0,
            fences_full: 0,
            nan_probe: std::env::var("RCN_RECOMP_NAN").map(|v| v == "1").unwrap_or(false),
            probe_fns: std::env::var("RCN_RECOMP_PROBE")
                .map(|v| v.split(',').filter_map(|x| u32::from_str_radix(x.trim().trim_start_matches("0x"), 16).ok()).collect())
                .unwrap_or_default(),
            helper_names: Vec::new(),
            helper_index: HashMap::new(),
            messages: Vec::new(),
            message_index: HashMap::new(),
            compiled: Vec::new(),
            unimplemented: BTreeMap::new(),
            total: 0,
            arch,
            code_relocs: if pe.bits == 32 { pe.relocs.iter().copied().filter(|&at| pe.is_exec(at)).collect() } else { BTreeSet::new() },
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
        // Mise au point : RCN_RECOMP_CLIF=<rva> affiche le code intermédiaire de cette fonction.
        if std::env::var("RCN_RECOMP_CLIF").ok().and_then(|v| u32::from_str_radix(&v, 16).ok()) == Some(f.entry) {
            eprintln!("{}", func.display());
        }
        let names = func.params.user_named_funcs().clone();
        let mut ctx = Context::for_function(func);
        let code = ctx
            .compile(&*self.isa, &mut ControlPlane::default())
            .map_err(|e| format!("sub_{:08X} : {:?}", f.entry, e.inner))?;
        let mut relocs = Vec::new();
        for r in code.buffer.relocs() {
            let kind = reloc_kind(r.kind).ok_or_else(|| format!("sub_{:08X} : relocalisation non gérée {:?}", f.entry, r.kind))?;
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
                let kind = reloc_kind(r.kind).ok_or_else(|| format!("relocalisation non gérée {:?}", r.kind))?;
                relocs.push((r.offset, kind, name.namespace, name.index, r.addend));
            }
        }
        self.compiled.push(CompiledFunc { rva: f.entry, code: code.code_buffer().to_vec(), alignment: 16, relocs });
        Ok(())
    }

    /// Fichier .rcnx, version 2 (petit-boutiste) :
    ///   "RCNX", version, architecture (1 x86-64, 2 ARM64), bits du jeu, adresse de chargement prévue (u64),
    ///   taille de l'image, date, point d'entrée, nombre de fonctions, de fonctions du runtime, de messages,
    ///   taille du code, nombre de relocalisations, nombre de cases de la table ; nom de l'exe ; noms des
    ///   fonctions du runtime ; messages ; fonctions (RVA, décalage) ; relocalisations (décalage depuis la
    ///   table, type, espace, indice, ajout) ; bourrage jusqu'à un multiple de CODE_ALIGN ; code.
    /// En mémoire, la table suit le code à `taille du code` arrondie à CODE_ALIGN. Le code est définitif :
    /// toutes ses références (appels, symboles, relais) sont relatives et résolues ici.
    pub fn write_rcnx(&mut self, exe_name: &str, out: &mut dyn Write) -> std::io::Result<()> {
        self.compiled.sort_by_key(|c| c.rva);
        let arm64 = self.arch == 2;
        // ARM64 : un appel direct (BL) ne porte qu'à ±128 Mo. Pour un code plus grand, des îlots de relais
        // (« adrp x16, cible ; add x16, x16, :lo12:cible ; br x16 ») sont placés après chaque région de
        // REGION octets ; un appel lointain d'une région va au relais de son îlot, à moins de REGION + taille
        // de l'îlot. Est « lointain » un appel de plus de REGION octets sans îlots.
        const REGION: usize = 64 << 20;
        let mut first: HashMap<u32, usize> = HashMap::new();
        let mut plain_size = 0usize;
        for c in &self.compiled {
            plain_size = plain_size.next_multiple_of(c.alignment as usize);
            first.insert(c.rva, plain_size);
            plain_size += c.code.len();
        }
        let islands = arm64 && plain_size > REGION;
        enum Piece {
            Pad(usize),
            Func(usize),
            Island(usize, Vec<(u32, i64)>), // position, relais (fonction visée, ajout)
        }
        fn pad(pieces: &mut Vec<Piece>, size: &mut usize, align: usize) {
            let n = size.next_multiple_of(align) - *size;
            if n > 0 {
                pieces.push(Piece::Pad(n));
                *size += n;
            }
        }
        // Disposition : position de chaque fonction et des relais, sans recopier le code.
        let mut pieces: Vec<Piece> = Vec::new();
        let mut size = 0usize;
        let mut offsets: HashMap<u32, usize> = HashMap::with_capacity(self.compiled.len());
        // Appels vers un relais : (fonction, BL dans elle) → (îlot, numéro du relais).
        let mut via: HashMap<(usize, u32), (usize, usize)> = HashMap::new();
        let mut pending: Vec<(usize, u32, u32, i64)> = Vec::new();
        let mut region_start = 0usize;
        let flush = |pieces: &mut Vec<Piece>, size: &mut usize, pending: &mut Vec<(usize, u32, u32, i64)>, via: &mut HashMap<(usize, u32), (usize, usize)>| {
            if pending.is_empty() {
                return;
            }
            pad(pieces, size, 16);
            let at = *size;
            let mut list: Vec<(u32, i64)> = Vec::new();
            let mut index: HashMap<(u32, i64), usize> = HashMap::new();
            for &(func, bl, target, addend) in pending.iter() {
                let n = *index.entry((target, addend)).or_insert_with(|| {
                    list.push((target, addend));
                    list.len() - 1
                });
                via.insert((func, bl), (pieces.len(), n));
            }
            *size += 16 * list.len();
            pieces.push(Piece::Island(at, list));
            pending.clear();
        };
        for i in 0..self.compiled.len() {
            let len = self.compiled[i].code.len();
            if islands && size - region_start + len > REGION {
                flush(&mut pieces, &mut size, &mut pending, &mut via);
                region_start = size;
            }
            pad(&mut pieces, &mut size, self.compiled[i].alignment as usize);
            let c = &self.compiled[i];
            offsets.insert(c.rva, size);
            if islands {
                let b = first[&c.rva];
                for &(at, kind, ns, index, addend) in &c.relocs {
                    if kind == RELOC_ARM64_CALL && ns == NS_FUNC {
                        let far = first.get(&index).is_some_and(|&t| (t as i64 + addend - (b + at as usize) as i64).unsigned_abs() as usize > REGION);
                        if far {
                            pending.push((i, at, index, addend));
                        }
                    }
                }
            }
            pieces.push(Piece::Func(i));
            size += len;
        }
        flush(&mut pieces, &mut size, &mut pending, &mut via);
        let code_size = size;
        let got_at = code_size.next_multiple_of(CODE_ALIGN);
        // Table des symboles : une case par symbole externe (espace, indice), remplie par le chargeur.
        let mut got: HashMap<(u32, u32), usize> = HashMap::new();
        let mut got_list: Vec<(u32, u32)> = Vec::new();
        let mut slot = |ns: u32, index: u32| -> usize {
            got_at + 8 * *got.entry((ns, index)).or_insert_with(|| {
                got_list.push((ns, index));
                got_list.len() - 1
            })
        };
        // Résolution d'une référence relative (site et cible : positions dans le code ou la table).
        fn patch(code: &mut [u8], at: usize, kind: u8, site: usize, target: i64) -> Result<(), String> {
            let word = |code: &[u8]| u32::from_le_bytes(code[at..at + 4].try_into().unwrap());
            let set = |code: &mut [u8], v: u32| code[at..at + 4].copy_from_slice(&v.to_le_bytes());
            match kind {
                RELOC_PCREL4 | RELOC_X86_GOTPCREL4 => {
                    let d = target - site as i64;
                    if d != d as i32 as i64 {
                        return Err("référence relative hors de portée".into());
                    }
                    code[at..at + 4].copy_from_slice(&(d as i32).to_le_bytes());
                }
                RELOC_ARM64_CALL => {
                    let d = target - site as i64;
                    if d & 3 != 0 || !(-(1 << 27)..(1 << 27)).contains(&d) {
                        return Err("appel hors de portée".into());
                    }
                    let v = (word(code) & 0xFC00_0000) | ((d >> 2) as u32 & 0x03FF_FFFF);
                    set(code, v);
                }
                RELOC_GOT_PAGE21 | RELOC_PAGE21 => {
                    let pages = (target >> 12) - (site as i64 >> 12);
                    let (lo, hi) = ((pages & 3) as u32, ((pages >> 2) & 0x7FFFF) as u32);
                    let v = (word(code) & 0x9F00_001F) | (lo << 29) | (hi << 5);
                    set(code, v);
                }
                RELOC_GOT_LO12 => {
                    let v = (word(code) & 0xFFC0_03FF) | ((((target & 0xFFF) >> 3) as u32) << 10);
                    set(code, v);
                }
                RELOC_ADD_LO12 => {
                    let v = (word(code) & 0xFFC0_03FF) | (((target & 0xFFF) as u32) << 10);
                    set(code, v);
                }
                _ => return Err(format!("relocalisation {kind} dans le code")),
            }
            Ok(())
        }
        let to_io = |e: String| std::io::Error::other(e);
        // Fonctions : références résolues dans leur propre tampon.
        let mut island_of: Vec<usize> = Vec::new(); // position des îlots, par morceau
        for p in &pieces {
            island_of.push(if let Piece::Island(at, _) = p { *at } else { 0 });
        }
        for i in 0..self.compiled.len() {
            let rva = self.compiled[i].rva;
            let base = offsets[&rva];
            let relocs = std::mem::take(&mut self.compiled[i].relocs);
            for &(at, kind, ns, index, addend) in &relocs {
                let site = base + at as usize;
                let target: i64 = match kind {
                    RELOC_GOT_PAGE21 | RELOC_GOT_LO12 | RELOC_X86_GOTPCREL4 => slot(ns, index) as i64 + addend,
                    _ if ns == NS_FUNC => match via.get(&(i, at)) {
                        Some(&(piece, n)) => (island_of[piece] + 16 * n) as i64,
                        None => match offsets.get(&index) {
                            Some(&t) => t as i64 + addend,
                            None => return Err(to_io(format!("sub_{rva:08X} : fonction {index:08X} absente"))),
                        },
                    },
                    // Appel direct d'une fonction du runtime (hors de la table) : non attendu en code indépendant de sa position.
                    _ => return Err(to_io(format!("sub_{rva:08X} : référence directe au runtime ({kind})"))),
                };
                patch(&mut self.compiled[i].code, at as usize, kind, site, target).map_err(|e| to_io(format!("sub_{rva:08X} : {e}")))?;
            }
        }
        // Relais : adrp x16, cible ; add x16, x16, :lo12:cible ; br x16 ; bourrage.
        let mut island_bytes: HashMap<usize, Vec<u8>> = HashMap::new();
        for (k, p) in pieces.iter().enumerate() {
            if let Piece::Island(at, list) = p {
                let mut bytes = vec![0u8; 16 * list.len()];
                for (n, &(index, addend)) in list.iter().enumerate() {
                    let v = at + 16 * n;
                    let target = offsets[&index] as i64 + addend;
                    bytes[16 * n..16 * n + 4].copy_from_slice(&0x9000_0010u32.to_le_bytes()); // adrp x16
                    bytes[16 * n + 4..16 * n + 8].copy_from_slice(&0x9100_0210u32.to_le_bytes()); // add x16, x16, #0
                    bytes[16 * n + 8..16 * n + 12].copy_from_slice(&0xD61F_0200u32.to_le_bytes()); // br x16
                    patch(&mut bytes, 16 * n, RELOC_PAGE21, v, target).map_err(to_io)?;
                    patch(&mut bytes, 16 * n + 4, RELOC_ADD_LO12, v + 4, target).map_err(to_io)?;
                }
                island_bytes.insert(k, bytes);
            }
        }
        // Relocalisations du fichier : les cases de la table (décalage depuis la table).
        let relocs: Vec<(u32, u8, u32, u32, i64)> = got_list.iter().enumerate().map(|(n, &(ns, index))| ((8 * n) as u32, RELOC_ABS8, ns, index, 0)).collect();

        let mut head = Vec::new();
        let u32le = |out: &mut Vec<u8>, v: u32| out.extend_from_slice(&v.to_le_bytes());
        let text = |out: &mut Vec<u8>, s: &str| {
            out.extend_from_slice(&(s.len() as u32).to_le_bytes());
            out.extend_from_slice(s.as_bytes());
        };
        head.extend_from_slice(b"RCNX");
        u32le(&mut head, 2);
        u32le(&mut head, self.arch);
        u32le(&mut head, self.pe.bits);
        head.extend_from_slice(&self.pe.image_base.to_le_bytes());
        u32le(&mut head, self.pe.size_of_image);
        u32le(&mut head, self.pe.timestamp);
        u32le(&mut head, self.pe.entry_rva);
        u32le(&mut head, self.compiled.len() as u32);
        u32le(&mut head, self.helper_names.len() as u32);
        u32le(&mut head, self.messages.len() as u32);
        u32le(&mut head, code_size as u32);
        u32le(&mut head, relocs.len() as u32);
        u32le(&mut head, got_list.len() as u32);
        text(&mut head, exe_name);
        for name in &self.helper_names {
            text(&mut head, name);
        }
        for message in &self.messages {
            text(&mut head, message);
        }
        for c in &self.compiled {
            u32le(&mut head, c.rva);
            u32le(&mut head, offsets[&c.rva] as u32);
        }
        for (offset, kind, ns, index, addend) in relocs {
            u32le(&mut head, offset);
            head.push(kind);
            head.push(ns as u8);
            head.extend_from_slice(&[0, 0]);
            u32le(&mut head, index);
            head.extend_from_slice(&addend.to_le_bytes());
        }
        let padding = head.len().next_multiple_of(CODE_ALIGN) - head.len();
        head.resize(head.len() + padding, 0);
        out.write_all(&head)?;
        let zeros = [0u8; 64];
        for (k, piece) in pieces.iter().enumerate() {
            match piece {
                Piece::Pad(n) => out.write_all(&zeros[..*n])?,
                Piece::Func(i) => out.write_all(&self.compiled[*i].code)?,
                Piece::Island(..) => out.write_all(&island_bytes[&k])?,
            }
        }
        Ok(())
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
    /// Registres xmm0–15 (I8X16) et mm0–7 (I64) : valeur connue dans le bloc courant (cache local au bloc,
    /// vidé à chaque changement de bloc, appel ou écriture faite en mémoire) ; toujours écrits aussi dans
    /// ctx, d'où ils sont relus hors du cache. `vec_written` : l'instruction en cours a tenu le cache à jour.
    xmm: [Option<Value>; XREGS],
    mm: [Option<Value>; 8],
    vec_written: bool,
    /// Registres xmm dont la valeur du cache est plus récente que ctx (rangée par `flush_xmm`, aux mêmes
    /// endroits que les registres généraux et avant toute lecture par pointeur). Un registre sale est
    /// toujours dans le cache.
    xdirty: u64,
    /// AVX : registre de 128 bits (voir XREGS) qui remplace l'opérande i de l'instruction SSE équivalente
    /// en cours de traduction (moitié haute d'un ymm, registre de travail).
    op_remap: [Option<usize>; 4],
    /// Dernière comparaison du bloc dont les drapeaux sont encore ceux du moment : (soustraction — cmp, sub —
    /// ou « et » — test, and —, opérandes, résultat). Un jcc / setcc / cmovcc qui suit en tire sa condition
    /// par une comparaison directe, que Cranelift fusionne avec le saut (cmp + b.cond) ; les drapeaux
    /// eux-mêmes, s'ils ne servent plus, ne sont pas calculés.
    last_cmp: Option<(bool, Value, Value, Value)>,
    /// ctx tel que reçu (registre des arguments) : passé aux appels. Les accès à ctx passent par `ctx`, une
    /// copie opaque (ctx + zéro lu en mémoire) que rien n'attache au registre x0 : sans elle, l'allocateur
    /// de registres recopie ctx dans x0 avant presque chaque accès.
    ctx_param: Value,
    /// Registres généraux écrits depuis leur dernier rangement dans ctx (un bit par registre). ctx n'a à
    /// être à jour qu'aux appels, aux retours et en entrée des blocs du code x86 : chaque écriture n'y est
    /// plus recopiée, les registres modifiés sont rangés une fois, à ces endroits (`flush_regs`).
    dirty: u16,
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
    /// rsp du jeu à l'entrée de la fonction (adresse de retour au sommet).
    entry_rsp: Option<Value>,
    fmt: IntelFormatter,
    /// Mode TSO : l'instruction en cours n'accède qu'à la pile (rsp), privée au thread ; dernier accès
    /// du jeu depuis la dernière barrière (0 aucun, 1 lecture, 2 écriture, 3 inconnu : entrée de bloc, appel).
    mem_private: bool,
    mem_last: u8,
    /// TSO : depuis la dernière barrière, seules des lectures partagées ont eu lieu, toutes faites par
    /// l'instruction x86 à cette adresse (RVA). Son écriture (« add [m], r », « inc [m] »…) va à la même
    /// adresse que sa lecture : l'ordre lecture → écriture d'une même case est garanti sans barrière.
    mem_rmw: Option<u32>,
    /// TSO : valeur lue par la dernière lecture partagée, quand toutes celles depuis la dernière barrière
    /// forment une chaîne de dépendances d'adresse (chacune lit à une adresse calculée avec la valeur de la
    /// précédente). Une lecture dont l'adresse dépend de cette valeur est ordonnée par le processeur ARM
    /// lui-même (dépendance d'adresse) : pas de barrière (suivre un pointeur : « mov rax,[rcx] ; mov edx,[rax+8] »).
    mem_dep: Option<Value>,
    /// La fonction commence par « push ebp ; mov ebp, esp » (ou rbp, rsp) : ebp désigne son cadre de pile,
    /// privé au thread comme esp (variables locales, arguments).
    frame_pointer: bool,
    /// RVA de l'instruction en cours (sonde des NaN).
    cur_rva: u32,
    /// Instruction en cours (32 bits) : son déplacement ou sa valeur immédiate est une adresse de l'image
    /// relocalisée (voir Native::code_relocs).
    disp_reloc: bool,
    imm_reloc: bool,
}

impl<'n, 'a, 'b> Tr<'n, 'a, 'b> {
    fn new(n: &'n mut Native<'a>, mut b: FunctionBuilder<'b>, f: &'b Func) -> Self {
        let regs = std::array::from_fn(|_| b.declare_var(I64));
        let flags = std::array::from_fn(|_| b.declare_var(I8));
        let xmm = [None; XREGS];
        let mm = [None; 8];
        let dummy = Value::from_u32(0);
        Tr {
            n,
            b,
            f,
            ctx: dummy,
            base: dummy,
            regs,
            flags,
            xmm,
            mm,
            vec_written: false,
            xdirty: 0,
            op_remap: [None; 4],
            last_cmp: None,
            mem_private: false,
            frame_pointer: false,
            cur_rva: 0,
            disp_reloc: false,
            imm_reloc: false,
            mem_last: 3,
            mem_rmw: None,
            mem_dep: None,
            ctx_param: dummy,
            dirty: 0,
            blocks: HashMap::new(),
            filled: BTreeSet::new(),
            helpers: HashMap::new(),
            callees: HashMap::new(),
            terminated: false,
            setjmp_sites: Vec::new(),
            frame_token: None,
            entry_rsp: None,
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
        self.dirty |= 1 << n;
    }
    /// Registres modifiés rangés dans ctx (avant un appel, un retour ou un saut vers un bloc du code x86).
    fn flush_regs(&mut self) {
        // Appel, retour, entrée de bloc : accès précédents inconnus.
        self.mem_last = 3;
        self.flush_xmm(u64::MAX);
        for n in 0..16 {
            if self.dirty & (1 << n) != 0 {
                let v = self.b.use_var(self.regs[n]);
                self.ctx_store(v, OFF_R + 8 * n as i32);
            }
        }
        self.dirty = 0;
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
        self.dirty = 0;
        for n in 0..16 {
            let v = self.ctx_load(I64, OFF_R + 8 * n as i32);
            self.b.def_var(self.regs[n], v);
        }
        for i in 0..6 {
            let v = self.ctx_load(I8, OFF_FLAGS + i as i32);
            self.b.def_var(self.flags[i], v);
        }
        self.vec_forget();
    }
    /// Registre xmm `n` écrit en mémoire hors du cache : sa valeur connue est oubliée.
    fn xmm_sync(&mut self, n: usize) {
        // Sale : ctx n'a pas été écrit à côté (une écriture par pointeur l'aurait rangé d'abord).
        if self.xdirty & (1 << n) == 0 {
            self.xmm[n] = None;
        }
    }
    fn mm_sync(&mut self, n: usize) {
        self.mm[n] = None;
    }
    /// Cache des registres vectoriels vidé (changement de bloc, appel).
    /// Les valeurs sales restent (elles ne sont pas encore dans ctx) : aux entrées des blocs du code x86
    /// il n'y en a plus (rangées avant chaque saut) ; dans les sous-blocs d'une instruction, elles dominent.
    fn vec_forget(&mut self) {
        for n in 0..XREGS {
            if self.xdirty & (1 << n) == 0 {
                self.xmm[n] = None;
            }
        }
        self.mm = [None; 8];
        self.last_cmp = None;
    }
    /// Registres xmm sales de `mask` rangés dans ctx.
    fn flush_xmm(&mut self, mask: u64) {
        for n in 0..XREGS {
            if self.xdirty & mask & (1 << n) != 0 {
                if let Some(v) = self.xmm[n] {
                    self.b.ins().store(MemFlagsData::new().with_notrap(), v, self.ctx, xoff(n));
                }
                self.xdirty &= !(1 << n);
            }
        }
    }
    /// Type vectoriel de 128 bits dont les éléments sont de type `t`.
    fn lanes_of(t: Type) -> Type {
        t.by(16 / t.bytes()).unwrap()
    }
    /// Élément de type `t` à `offset` octets dans le registre xmm `n` (cache, sinon ctx).
    fn xmm_get(&mut self, n: usize, t: Type, offset: i32) -> Value {
        if let Some(v) = self.xmm[n] {
            let vt = Self::lanes_of(t);
            let v = self.vcast(vt, v);
            return self.b.ins().extractlane(v, (offset / t.bytes() as i32) as u8);
        }
        self.ctx_load(t, xoff(n) + offset)
    }
    /// Registre xmm `n` : `v` dans son élément 0, le reste à zéro (movss/movsd depuis la mémoire, movd, movq…).
    fn xmm_from_low(&mut self, n: usize, v: Value) {
        let t = self.b.func.dfg.value_type(v);
        // scalar_to_vector : élément 0, les autres à zéro (sur ARM64, souvent le chargement lui-même).
        let r = self.b.ins().scalar_to_vector(Self::lanes_of(t), v);
        let r = self.vcast(types::I8X16, r);
        self.xmm_set(n, r);
    }
    /// Changement de bloc : les valeurs du bloc précédent ne s'y appliquent pas forcément.
    fn switch_block(&mut self, b: Block) {
        self.vec_forget();
        self.b.switch_to_block(b);
    }
    /// Nouvelle valeur (I8X16) du registre xmm `n` : cache et ctx.
    fn xmm_set(&mut self, n: usize, v: Value) {
        self.xmm[n] = Some(v);
        self.xdirty |= 1 << n;
        self.vec_written = true;
    }
    fn mm_set(&mut self, n: usize, v: Value) {
        self.mm[n] = Some(v);
        self.ctx_store(v, OFF_ST + 8 * n as i32);
        self.vec_written = true;
    }
    fn mm_get(&mut self, n: usize) -> Value {
        if let Some(v) = self.mm[n] {
            return v;
        }
        let v = self.ctx_load(I64, OFF_ST + 8 * n as i32);
        self.mm[n] = Some(v);
        v
    }
    /// Drapeaux rangés dans ctx (avant un appel ou un retour).
    fn store_flags_mask(&mut self, mask: u32) {
        for i in 0..6 {
            if mask & FLAG_BITS[i] != 0 {
                let v = self.flag(i);
                self.ctx_store(v, OFF_FLAGS + i as i32);
            }
        }
    }
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
    /// Arguments d'un appel : ctx passé tel que reçu (voir `ctx_param`).
    fn call_args(&self, args: &[Value]) -> Vec<Value> {
        args.iter().map(|&a| if a == self.ctx { self.ctx_param } else { a }).collect()
    }
    fn call_helper(&mut self, name: &str, params: &[Type], ret: Option<Type>, args: &[Value]) -> Option<Value> {
        let r = self.helper(name, params, ret);
        self.flush_regs();
        self.store_flags();
        let args = self.call_args(args);
        let call = self.b.ins().call(r, &args);
        let result = ret.map(|_| self.b.inst_results(call)[0]);
        self.reload();
        result
    }
    /// Appel d'une fonction du runtime qui ne touche ni aux registres ni aux drapeaux.
    fn call_pure(&mut self, name: &str, params: &[Type], ret: Option<Type>, args: &[Value]) -> Option<Value> {
        let r = self.helper(name, params, ret);
        self.vec_forget();
        let args = self.call_args(args);
        let call = self.b.ins().call(r, &args);
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
        self.switch_block(cont);
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
    // Appels et retours : aucun drapeau rangé, la fonction appelée ne les lit pas et l'appelant n'en attend
    // aucun au retour (convention d'appel Windows x64). Seules les fonctions du runtime qui imitent une
    // instruction (call_helper) les lisent dans ctx.
    fn call_func(&mut self, rva: u32) {
        let r = self.callee(rva);
        // Morceau de fonction qui lit des drapeaux en entrée : ceux-là lui sont transmis.
        let wanted = self.n.entry_flags.get(&rva).copied().unwrap_or(crate::func::STATUS_FLAGS);
        self.flush_regs();
        self.store_flags_mask(wanted);
        let ctx = self.ctx_param;
        self.b.ins().call(r, &[ctx]);
        self.check_unwind();
        self.reload();
    }
    /// Appel par adresse (fonction traduite ou fonction Windows de l'hôte).
    fn call_address(&mut self, target: Value) {
        let r = self.helper("rcn_call", &[I64, I64], None);
        self.flush_regs();
        let ctx = self.ctx_param;
        self.b.ins().call(r, &[ctx, target]);
        self.check_unwind();
        self.reload();
    }
    fn ret(&mut self) {
        self.flush_regs();
        self.b.ins().return_(&[]);
        self.terminated = true;
    }
    /// Arrêt du jeu sur une situation non gérée.
    fn trap(&mut self, rva: u32, message: &str) {
        let index = self.n.message_id(message);
        let r = self.helper("rcn_trap_index", &[I64, I64, I32], None);
        self.flush_regs();
        self.store_flags();
        let ctx = self.ctx_param;
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
                // Le relais range les registres pour lui-même : le chemin courant garde son état.
                let dirty = self.dirty;
                let (xdirty, xmm) = (self.xdirty, self.xmm);
                self.switch_block(stub);
                let was = self.terminated;
                match target {
                    Some(t) if self.n.funcs.contains(&t) => {
                        self.call_func(t);
                        self.ret();
                    }
                    _ => self.trap(rva, "saut hors du code"),
                }
                self.terminated = was;
                self.dirty = dirty;
                self.switch_block(current);
                self.xdirty = xdirty;
                self.xmm = xmm;
                stub
            }
        }
    }
    fn jump_to(&mut self, block: Block) {
        self.flush_regs();
        self.b.ins().jump(block, &[]);
        self.terminated = true;
    }
    /// Si `cond` (i8 non nul) : `then` ; puis suite commune.
    /// Opération atomique (rmw, ou cas si `expected`) qui rend l'ancienne valeur. Le x86 accepte une
    /// adresse non alignée ; l'ARM64 non (SIGBUS) : là, une adresse non alignée passe par le runtime,
    /// qui fait l'opération sous un verrou global.
    fn atomic_any(&mut self, t: Type, op: Option<AtomicRmwOp>, ea: Value, v: Value, expected: Option<Value>) -> Value {
        // Opération verrouillée : barrière complète (aucun accès ne la franchit), comme sous x86.
        if self.n.tso && self.mem_last != 0 {
            self.b.ins().fence();
            self.n.fences += 1;
        }
        self.mem_last = 0;
        let size = t.bytes();
        let hardware = |tr: &mut Self| match op {
            Some(op) => tr.b.ins().atomic_rmw(t, MemFlagsData::new(), op, ea, v),
            None => tr.b.ins().atomic_cas(MemFlagsData::new(), ea, expected.unwrap(), v),
        };
        if size == 1 || self.n.arch == 1 {
            return hardware(self);
        }
        let result = self.b.declare_var(t);
        let misaligned = self.b.ins().band_imm_s(ea, (size - 1) as i64);
        let slow = self.b.create_block();
        let fast = self.b.create_block();
        let join = self.b.create_block();
        self.b.ins().brif(misaligned, slow, &[], fast, &[]);
        self.switch_block(fast);
        let r = hardware(self);
        self.b.def_var(result, r);
        self.b.ins().jump(join, &[]);
        self.switch_block(slow);
        let code = match op {
            Some(AtomicRmwOp::Add) => 0,
            Some(AtomicRmwOp::Sub) => 1,
            Some(AtomicRmwOp::And) => 2,
            Some(AtomicRmwOp::Or) => 3,
            Some(AtomicRmwOp::Xor) => 4,
            Some(AtomicRmwOp::Xchg) => 5,
            None => 6,
            Some(_) => 5,
        };
        let wide = |tr: &mut Self, x: Value| if t == I64 { x } else { tr.b.ins().uextend(I64, x) };
        let v64 = wide(self, v);
        let e64 = match expected {
            Some(e) => wide(self, e),
            None => self.iconst(I64, 0),
        };
        let code_v = self.b.ins().iconst(I32, code);
        let size_v = self.b.ins().iconst(I32, size as i64);
        let old = self
            .call_pure("rcn_op_atomic_unaligned", &[I64, I32, I32, I64, I64], Some(I64), &[ea, code_v, size_v, v64, e64])
            .unwrap();
        let old = if t == I64 { old } else { self.b.ins().ireduce(t, old) };
        self.b.def_var(result, old);
        self.b.ins().jump(join, &[]);
        self.switch_block(join);
        self.b.use_var(result)
    }

    /// Sonde des NaN : `r` est NaN (une voie au moins) alors que `a` et `b` ne l'étaient pas → rcn_op_nan_born.
    fn nan_check(&mut self, r: Value, a: Option<Value>, b: Value) {
        if !self.n.nan_probe {
            return;
        }
        let vector = self.b.func.dfg.value_type(r).is_vector();
        let rn = self.b.ins().fcmp(FloatCC::Unordered, r, r);
        let bn = self.b.ins().fcmp(FloatCC::Unordered, b, b);
        let inputs = match a {
            Some(a) => {
                let an = self.b.ins().fcmp(FloatCC::Unordered, a, a);
                self.b.ins().bor(an, bn)
            }
            None => bn,
        };
        let born = self.b.ins().band_not(rn, inputs);
        let cond = if vector { self.b.ins().vany_true(born) } else { born };
        let rva = self.cur_rva;
        self.nan_report(cond, rva as u64);
        // Infini né d'opérandes finis (division par zéro, dépassement) : bit 63 du rva.
        let t = self.b.func.dfg.value_type(r);
        let lane = t.lane_type();
        let inf = if lane == F32 { self.b.ins().f32const(f32::INFINITY) } else { self.b.ins().f64const(f64::INFINITY) };
        let inf = if vector { self.b.ins().splat(t, inf) } else { inf };
        let finite = |tr: &mut Self, v: Value| {
            let m = tr.b.ins().fabs(v);
            tr.b.ins().fcmp(FloatCC::LessThan, m, inf)
        };
        let r_abs = self.b.ins().fabs(r);
        let r_inf = self.b.ins().fcmp(FloatCC::Equal, r_abs, inf);
        let b_fin = finite(self, b);
        let all_fin = match a {
            Some(a) => {
                let a_fin = finite(self, a);
                self.b.ins().band(a_fin, b_fin)
            }
            None => b_fin,
        };
        let born_inf = self.b.ins().band(r_inf, all_fin);
        let cond = if vector { self.b.ins().vany_true(born_inf) } else { born_inf };
        self.nan_report(cond, rva as u64 | 1 << 63);
    }
    fn nan_report(&mut self, cond: Value, rva: u64) {
        self.if_then(cond, |tr| {
            let ctx = tr.ctx;
            let rva = tr.b.ins().iconst(I64, rva as i64);
            // Appel « pur » : call_helper rangerait les registres sales dans ce seul chemin et les croirait
            // rangés dans l'autre. La pile notée part donc du dernier rsp rangé.
            tr.call_pure("rcn_op_nan_born", &[I64, I64], None, &[ctx, rva]);
        });
    }
    fn if_then(&mut self, cond: Value, then: impl FnOnce(&mut Self)) {        let yes = self.b.create_block();
        let done = self.b.create_block();
        self.b.ins().brif(cond, yes, &[], done, &[]);
        self.switch_block(yes);
        then(self);
        self.b.ins().jump(done, &[]);
        self.switch_block(done);
    }
    /// Si `cond` : `then`, sinon `otherwise` ; puis suite commune.
    fn if_else(&mut self, cond: Value, then: impl FnOnce(&mut Self), otherwise: impl FnOnce(&mut Self)) {
        let yes = self.b.create_block();
        let no = self.b.create_block();
        let done = self.b.create_block();
        self.b.ins().brif(cond, yes, &[], no, &[]);
        self.switch_block(yes);
        then(self);
        self.b.ins().jump(done, &[]);
        self.switch_block(no);
        otherwise(self);
        self.b.ins().jump(done, &[]);
        self.switch_block(done);
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
            // Code 32 bits, « [base + déplacement] » sans index : les registres y sont gardés à moitié haute
            // nulle, base + déplacement signé est déjà l'adresse sur 32 bits (sauf en franchissant 0 ou 4 Go :
            // faute sous Windows de toute façon). La troncature (une instruction par accès mémoire) est omise.
            // Avec un index, elle reste : un index négatif (0xFFFFFFFF…) doit faire le tour comme sur x86.
            if self.bits32() && narrow && index == Register::None && !self.disp_reloc {
                let d = disp as u32 as i32 as i64;
                if d != 0 {
                    v = self.b.ins().iadd_imm_s(v, d);
                }
                return Some(match seg {
                    Some(off) => {
                        let s = self.ctx_load(I64, off);
                        self.b.ins().iadd(v, s)
                    }
                    None => v,
                });
            }
            // Code 32 bits avec index : somme calculée sur 32 bits (registres w de l'ARM : le tour se fait comme
            // sur x86, et l'extension sur 64 bits ne coûte rien).
            if self.bits32() && narrow && !self.disp_reloc {
                let mut s: Option<Value> = None;
                for (reg, scale) in [(base, 1u32), (index, ins.memory_index_scale())] {
                    if reg == Register::None {
                        continue;
                    }
                    let r = self.get(gpr(reg));
                    let mut r = self.b.ins().ireduce(I32, r);
                    if scale != 1 {
                        r = self.b.ins().ishl_imm_s(r, scale.trailing_zeros() as i64);
                    }
                    s = Some(match s {
                        Some(a) => self.b.ins().iadd(a, r),
                        None => r,
                    });
                }
                let mut s = s.unwrap();
                if disp != 0 {
                    s = self.b.ins().iadd_imm_s(s, disp as u32 as i32 as i64);
                }
                let v = self.b.ins().uextend(I64, s);
                return Some(match seg {
                    Some(off) => {
                        let sg = self.ctx_load(I64, off);
                        self.b.ins().iadd(v, sg)
                    }
                    None => v,
                });
            }
            if self.disp_reloc {
                // Adresse de l'image : base réelle + (déplacement - base prévue).
                let at = self.b.ins().iadd_imm_s(self.base, disp.wrapping_sub(image_base) as i64);
                v = self.b.ins().iadd(v, at);
            } else if disp != 0 {
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

    // TSO : `mem_last` dit quels accès partagés ne sont pas encore ordonnés par une barrière : lectures
    // (bit 1), écritures (bit 2) ; 3 : inconnu (entrée de bloc, après un appel). Une lecture doit suivre les
    // lectures précédentes (dmb ishld) ; une écriture, les lectures (dmb ishld) et les écritures (dmb ishst)
    // précédentes, les deux à la fois par la barrière complète. Une lecture n'a pas à attendre les écritures
    // précédentes (le x86 le permet aussi).
    fn mem_load(&mut self, t: Type, addr: Value) -> Value {
        let mut chain = false;
        if self.n.tso && !self.mem_private {
            let loads = self.mem_last & 1 != 0;
            if !loads {
                self.mem_rmw = Some(self.cur_rva);
                chain = true;
            } else if self.mem_dep.is_some_and(|v| self.depends_on(addr, v, 6)) {
                // Ordonnée après la chaîne de lectures par la dépendance d'adresse.
                chain = true;
                if self.mem_rmw != Some(self.cur_rva) {
                    self.mem_rmw = None;
                }
            } else {
                self.b.ins().fence_load();
                self.n.fences += 1;
                self.mem_rmw = Some(self.cur_rva);
                chain = true;
            }
            self.mem_last |= 1;
        }
        let v = self.b.ins().load(t, MemFlagsData::new(), addr, 0);
        if self.n.tso && !self.mem_private {
            self.mem_dep = chain.then_some(v);
        }
        v
    }
    /// La valeur `addr` est-elle calculée (opérations arithmétiques du bloc, au plus `depth` niveaux) à partir
    /// de `v` ? Ni sélection (csel), ni lecture, ni valeur venue d'un autre bloc : dépendance non garantie.
    fn depends_on(&self, addr: Value, v: Value, depth: u32) -> bool {
        use cranelift_codegen::ir::{Opcode, ValueDef};
        if addr == v {
            return true;
        }
        if depth == 0 {
            return false;
        }
        let dfg = &self.b.func.dfg;
        let ValueDef::Result(inst, _) = dfg.value_def(addr) else { return false };
        let op = dfg.insts[inst].opcode();
        if op.can_load() || op.is_call() || matches!(op, Opcode::Select | Opcode::SelectSpectreGuard | Opcode::Bitselect) {
            return false;
        }
        dfg.inst_args(inst).iter().any(|&a| self.depends_on(a, v, depth - 1))
    }
    fn mem_store(&mut self, v: Value, addr: Value) {
        // TSO : une écriture ne passe devant aucun accès précédent.
        if self.n.tso && !self.mem_private {
            // Lectures en attente : seule celle de cette même instruction (même adresse) ne demande rien.
            let loads = self.mem_last & 1 != 0 && self.mem_rmw != Some(self.cur_rva);
            let stores = self.mem_last & 2 != 0;
            match (loads, stores) {
                (true, true) => {
                    self.b.ins().fence();
                    self.n.fences_full += 1;
                }
                (true, false) => {
                    self.b.ins().fence_load();
                }
                (false, true) => {
                    self.b.ins().fence_store();
                }
                (false, false) => {}
            }
            if loads || stores {
                self.n.fences += 1;
            }
            // Lectures ordonnées si une barrière les couvre ; celle de l'instruction elle-même reste en attente
            // pour les accès suivants.
            let still_loads = self.mem_last & 1 != 0 && !loads;
            self.mem_last = 2 | still_loads as u8;
            self.mem_rmw = None;
            self.mem_dep = None;
        }
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
            k if is_imm(k) && self.imm_reloc && size == 4 => Some(self.image_address(ins.immediate(i))),
            k if is_imm(k) => Some(self.iconst(ty(size), ins.immediate(i))),
            _ => None,
        }
    }
    /// Adresse absolue de l'image (prévue) → adresse réelle (32 bits) : base réelle + écart à la base prévue.
    fn image_address(&mut self, address: u64) -> Value {
        let image_base = self.n.pe.image_base;
        let v = self.b.ins().iadd_imm_s(self.base, address.wrapping_sub(image_base) as i64);
        self.b.ins().ireduce(I32, v)
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
    /// Condition tirée directement de la dernière comparaison (voir `last_cmp`), si elle s'y prête.
    fn cond_from_cmp(&mut self, cc: ConditionCode) -> Option<Value> {
        let (sub, a, b, r) = self.last_cmp?;
        let t = self.b.func.dfg.value_type(r);
        let zero = self.b.ins().iconst(t, 0);
        let icmp = |tr: &mut Self, c: IntCC, x: Value, y: Value| tr.b.ins().icmp(c, x, y);
        Some(if sub {
            // a - b
            match cc {
                ConditionCode::e => icmp(self, IntCC::Equal, a, b),
                ConditionCode::ne => icmp(self, IntCC::NotEqual, a, b),
                ConditionCode::b => icmp(self, IntCC::UnsignedLessThan, a, b),
                ConditionCode::ae => icmp(self, IntCC::UnsignedGreaterThanOrEqual, a, b),
                ConditionCode::be => icmp(self, IntCC::UnsignedLessThanOrEqual, a, b),
                ConditionCode::a => icmp(self, IntCC::UnsignedGreaterThan, a, b),
                ConditionCode::l => icmp(self, IntCC::SignedLessThan, a, b),
                ConditionCode::ge => icmp(self, IntCC::SignedGreaterThanOrEqual, a, b),
                ConditionCode::le => icmp(self, IntCC::SignedLessThanOrEqual, a, b),
                ConditionCode::g => icmp(self, IntCC::SignedGreaterThan, a, b),
                ConditionCode::s => icmp(self, IntCC::SignedLessThan, r, zero),
                ConditionCode::ns => icmp(self, IntCC::SignedGreaterThanOrEqual, r, zero),
                _ => return None,
            }
        } else {
            // r = a & b ; CF = OF = 0
            match cc {
                ConditionCode::e | ConditionCode::be => icmp(self, IntCC::Equal, r, zero),
                ConditionCode::ne | ConditionCode::a => icmp(self, IntCC::NotEqual, r, zero),
                ConditionCode::s | ConditionCode::l => icmp(self, IntCC::SignedLessThan, r, zero),
                ConditionCode::ns | ConditionCode::ge => icmp(self, IntCC::SignedGreaterThanOrEqual, r, zero),
                ConditionCode::le => icmp(self, IntCC::SignedLessThanOrEqual, r, zero),
                ConditionCode::g => icmp(self, IntCC::SignedGreaterThan, r, zero),
                _ => return None,
            }
        })
    }
    fn cond(&mut self, cc: ConditionCode) -> Option<Value> {
        if let Some(v) = self.cond_from_cmp(cc) {
            return Some(v);
        }
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
            self.atomic_any(t, Some(op), ea, b, None)
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
        self.last_cmp = match m {
            _ if locked => None,
            M::Sub | M::Cmp => Some((true, a, b, r)),
            M::And | M::Test => Some((false, a, b, r)),
            _ => None,
        };
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
            let sp32 = self.b.ins().ireduce(I32, sp);
            let sp32 = self.b.ins().iadd_imm_s(sp32, -4);
            let sp = self.b.ins().uextend(I64, sp32);
            self.set(4, sp);
            let v = self.fit(v, 4);
            self.stack_store(v, sp);
        } else {
            let sp = self.b.ins().iadd_imm_s(sp, -8);
            self.set(4, sp);
            let v = self.to64(v);
            self.stack_store(v, sp);
        }
    }
    /// Accès à une adresse fixe de l'image (absolue en 32 bits, relative à rip en 64 bits) dans une section
    /// sans droit d'écriture (constantes, tables) : rien ne la modifie, son ordre ne compte pas en mode TSO.
    fn read_only_target(&self, ins: &Instruction) -> bool {
        if ins.memory_index() != Register::None || ins.segment_prefix() != Register::None {
            return false;
        }
        let base = ins.memory_base();
        if base != Register::None && base != Register::RIP {
            return false;
        }
        if !(0..ins.op_count()).any(|i| ins.op_kind(i) == OpKind::Memory) {
            return false;
        }
        let rva = ins.memory_displacement64().wrapping_sub(self.n.pe.image_base);
        u32::try_from(rva).ok().and_then(|r| self.n.pe.section_of(r)).is_some_and(|s| s.characteristics & SCN_MEM_WRITE == 0)
    }
    /// Accès à la pile du thread (push, pop) : privés, sans barrière en mode TSO.
    fn stack_store(&mut self, v: Value, addr: Value) {
        let private = std::mem::replace(&mut self.mem_private, true);
        self.mem_store(v, addr);
        self.mem_private = private;
    }
    fn stack_load(&mut self, t: Type, addr: Value) -> Value {
        let private = std::mem::replace(&mut self.mem_private, true);
        let v = self.mem_load(t, addr);
        self.mem_private = private;
        v
    }
    fn pop(&mut self) -> Value {
        let sp = self.get(4);
        let ps = self.ps();
        let v = self.stack_load(ty(ps), sp);
        self.add_sp(ps as i64);
        v
    }
    fn add_sp(&mut self, amount: i64) {
        let sp = self.get(4);
        // 32 bits : calcul sur 32 bits (registre w de l'ARM : moitié haute remise à zéro, extension gratuite).
        let new = if self.bits32() {
            let low = self.b.ins().ireduce(I32, sp);
            let low = self.b.ins().iadd_imm_s(low, amount);
            self.b.ins().uextend(I64, low)
        } else {
            self.b.ins().iadd_imm_s(sp, amount)
        };
        self.set(4, new);
    }

    // --- SSE ---

    fn xmm(&self, ins: &Instruction, i: u32) -> Option<usize> {
        if let Some(n) = self.op_remap.get(i as usize).copied().flatten() {
            return Some(n);
        }
        (ins.op_kind(i) == OpKind::Register && ins.op_register(i).is_xmm()).then(|| ins.op_register(i).number())
    }
    fn xmm_ptr(&mut self, n: usize) -> Value {
        // Lu ou écrit par pointeur (fonction du runtime, copie par la mémoire) : ctx doit être à jour.
        self.flush_xmm(1 << n);
        self.b.ins().iadd_imm_s(self.ctx, xoff(n) as i64)
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
                Some(self.xmm_get(n, t, 0))
            }
            OpKind::Memory => {
                let ea = self.ea(ins)?;
                Some(self.mem_load(t, ea))
            }
            _ => None,
        }
    }
    /// Élément `v` écrit à `offset` octets dans le registre xmm `n` (dans le cache, rangé plus tard).
    fn xmm_store(&mut self, n: usize, v: Value, offset: i32) {
        let t = self.b.func.dfg.value_type(v);
        let vt = Self::lanes_of(t);
        let cur = self.vload_xmm(n);
        let cur = self.vcast(vt, cur);
        let r = self.b.ins().insertlane(cur, v, (offset / t.bytes() as i32) as u8);
        let r = self.vcast(types::I8X16, r);
        self.xmm_set(n, r);
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

    // --- Entiers vectoriels en ligne (SSE2, MMX) ---

    fn vcast(&mut self, t: Type, v: Value) -> Value {
        if self.b.func.dfg.value_type(v) == t {
            return v;
        }
        let le = MemFlagsData::new().with_endianness(Endianness::Little);
        self.b.ins().bitcast(t, le, v)
    }
    fn vshuffle(&mut self, a: Value, b: Value, mask: [u8; 16]) -> Value {
        let imm = self.b.func.dfg.immediates.push(ConstantData::from(mask.as_ref()));
        self.b.ins().shuffle(a, b, imm)
    }
    /// Vecteur de 128 bits (I8X16) d'un registre xmm, ou de 64 bits (mm, moitié haute nulle).
    fn vload_xmm(&mut self, n: usize) -> Value {
        if let Some(v) = self.xmm[n] {
            return v;
        }
        let v = self.b.ins().load(types::I8X16, MemFlagsData::new().with_notrap(), self.ctx, xoff(n));
        self.xmm[n] = Some(v);
        v
    }
    fn vfrom64(&mut self, q: Value) -> Value {
        let v = self.b.ins().scalar_to_vector(types::I64X2, q);
        self.vcast(types::I8X16, v)
    }
    fn vlow64(&mut self, v: Value) -> Value {
        let v = self.vcast(types::I64X2, v);
        self.b.ins().extractlane(v, 0)
    }
    /// a op b (I8X16 tous deux) ; `mmx` : opérandes de 64 bits dans la moitié basse (punpckh*, pack*
    /// réarrangés). None si l'opération n'est pas traduite en ligne.
    fn simd_int(&mut self, m: M, a: Value, b: Value, mmx: bool) -> Option<Value> {
        use types::{I16X8, I32X4, I64X2, I8X16};
        let lanes = match m {
            M::Paddb | M::Psubb | M::Paddsb | M::Paddusb | M::Psubsb | M::Psubusb | M::Pcmpeqb | M::Pcmpgtb | M::Pminub
            | M::Pmaxub | M::Pavgb | M::Pminsb | M::Pmaxsb => I8X16,
            M::Paddd | M::Psubd | M::Pcmpeqd | M::Pcmpgtd | M::Packssdw | M::Pminsd | M::Pmaxsd | M::Pminud | M::Pmaxud
            | M::Packusdw => I32X4,
            M::Paddq | M::Psubq | M::Pcmpeqq | M::Pcmpgtq => I64X2,
            M::Pshufb | M::Pmaddubsw => I8X16,
            _ => I16X8,
        };
        match m {
            // Octet choisi par l'indice (bit 7 : octet nul). En SSE, indice de 4 bits ; en MMX, de 3 bits.
            // swizzle rend 0 pour un indice hors du vecteur : bit 7 gardé, il le met hors limites.
            M::Pshufb => {
                let x = self.vcast(I8X16, a);
                let y = self.vcast(I8X16, b);
                let keep = self.b.ins().iconst(types::I8, if mmx { 0x87 } else { 0x8F });
                let keep = self.b.ins().splat(I8X16, keep);
                let idx = self.b.ins().band(y, keep);
                return Some(self.b.ins().swizzle(x, idx));
            }
            // Octets non signés de a × octets signés de b, paires voisines additionnées avec saturation
            // (produits exacts sur 16 bits : |255 × -128| < 32768).
            M::Pmaddubsw => {
                let x = self.vcast(I8X16, a);
                let y = self.vcast(I8X16, b);
                let xl = self.b.ins().uwiden_low(x);
                let xh = self.b.ins().uwiden_high(x);
                let yl = self.b.ins().swiden_low(y);
                let yh = self.b.ins().swiden_high(y);
                let lo = self.b.ins().imul(xl, yl);
                let hi = self.b.ins().imul(xh, yh);
                let lo8 = self.vcast(I8X16, lo);
                let hi8 = self.vcast(I8X16, hi);
                let even = self.vshuffle(lo8, hi8, [0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29]);
                let odd = self.vshuffle(lo8, hi8, [2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31]);
                let even = self.vcast(I16X8, even);
                let odd = self.vcast(I16X8, odd);
                let r = self.b.ins().sadd_sat(even, odd);
                return Some(self.vcast(I8X16, r));
            }
            _ => {}
        }
        let x = self.vcast(lanes, a);
        let y = self.vcast(lanes, b);
        let ins = self.b.ins();
        let r = match m {
            M::Paddb | M::Paddw | M::Paddd | M::Paddq => ins.iadd(x, y),
            M::Psubb | M::Psubw | M::Psubd | M::Psubq => ins.isub(x, y),
            M::Paddsb | M::Paddsw => ins.sadd_sat(x, y),
            M::Paddusb | M::Paddusw => ins.uadd_sat(x, y),
            M::Psubsb | M::Psubsw => ins.ssub_sat(x, y),
            M::Psubusb | M::Psubusw => ins.usub_sat(x, y),
            M::Pmullw => ins.imul(x, y),
            M::Pand => ins.band(x, y),
            M::Por => ins.bor(x, y),
            M::Pxor => ins.bxor(x, y),
            M::Pandn => ins.band_not(y, x),
            M::Pcmpeqb | M::Pcmpeqw | M::Pcmpeqd | M::Pcmpeqq => ins.icmp(IntCC::Equal, x, y),
            M::Pcmpgtb | M::Pcmpgtw | M::Pcmpgtd | M::Pcmpgtq => ins.icmp(IntCC::SignedGreaterThan, x, y),
            M::Pminub | M::Pminuw | M::Pminud => ins.umin(x, y),
            M::Pmaxub | M::Pmaxuw | M::Pmaxud => ins.umax(x, y),
            M::Pminsw | M::Pminsb | M::Pminsd => ins.smin(x, y),
            M::Pmaxsw | M::Pmaxsb | M::Pmaxsd => ins.smax(x, y),
            M::Packusdw => ins.unarrow(x, y),
            M::Pavgb | M::Pavgw => ins.avg_round(x, y),
            M::Pmulhw | M::Pmulhuw | M::Pmaddwd => {
                let signed = m != M::Pmulhuw;
                let (xl, xh, yl, yh) = if signed {
                    (self.b.ins().swiden_low(x), self.b.ins().swiden_high(x), self.b.ins().swiden_low(y), self.b.ins().swiden_high(y))
                } else {
                    (self.b.ins().uwiden_low(x), self.b.ins().uwiden_high(x), self.b.ins().uwiden_low(y), self.b.ins().uwiden_high(y))
                };
                let lo = self.b.ins().imul(xl, yl);
                let hi = self.b.ins().imul(xh, yh);
                match m {
                    M::Pmaddwd => self.b.ins().iadd_pairwise(lo, hi),
                    M::Pmulhw => {
                        let lo = self.b.ins().sshr_imm_s(lo, 16);
                        let hi = self.b.ins().sshr_imm_s(hi, 16);
                        self.b.ins().snarrow(lo, hi)
                    }
                    _ => {
                        let lo = self.b.ins().ushr_imm_u(lo, 16);
                        let hi = self.b.ins().ushr_imm_u(hi, 16);
                        self.b.ins().unarrow(lo, hi)
                    }
                }
            }
            M::Packsswb | M::Packssdw | M::Packuswb => {
                let r = if m == M::Packuswb { ins.unarrow(x, y) } else { ins.snarrow(x, y) };
                if mmx {
                    // 4 octets de chaque source : ceux de la seconde sont en tête de la moitié haute.
                    let r = self.vcast(I8X16, r);
                    return Some(self.vshuffle(r, r, [0, 1, 2, 3, 8, 9, 10, 11, 0, 0, 0, 0, 0, 0, 0, 0]));
                }
                r
            }
            M::Punpcklbw | M::Punpcklwd | M::Punpckldq | M::Punpcklqdq | M::Punpckhbw | M::Punpckhwd | M::Punpckhdq
            | M::Punpckhqdq => {
                let size: u8 = match m {
                    M::Punpcklbw | M::Punpckhbw => 1,
                    M::Punpcklwd | M::Punpckhwd => 2,
                    M::Punpckldq | M::Punpckhdq => 4,
                    _ => 8,
                };
                let high = matches!(m, M::Punpckhbw | M::Punpckhwd | M::Punpckhdq | M::Punpckhqdq);
                // Moitié haute de l'opérande : 8 octets en SSE, 4 en MMX.
                let base: u8 = if high { if mmx { 4 } else { 8 } } else { 0 };
                let mut mask = [0u8; 16];
                for (k, slot) in mask.iter_mut().enumerate() {
                    let k = k as u8;
                    let element = k / size;
                    let byte = k % size;
                    let src = base + (element / 2) * size + byte;
                    *slot = if element % 2 == 0 { src } else { 16 + src };
                }
                let a8 = self.vcast(I8X16, a);
                let b8 = self.vcast(I8X16, b);
                return Some(self.vshuffle(a8, b8, mask));
            }
            _ => return None,
        };
        Some(self.vcast(I8X16, r))
    }
    /// Opération flottante ou logique de SSE sur deux vecteurs (I8X16) ; résultat en I8X16.
    fn simd_float(&mut self, m: M, a: Value, b: Value) -> Value {
        use types::{F32X4, F64X2, I8X16};
        let double = matches!(m, M::Addpd | M::Subpd | M::Mulpd | M::Divpd | M::Sqrtpd);
        let t = if double { F64X2 } else { F32X4 };
        let byte_mask = |pairs: [(bool, u8); 16]| -> [u8; 16] {
            let mut mask = [0u8; 16];
            for (k, (from_b, i)) in pairs.iter().enumerate() {
                mask[k] = if *from_b { 16 + i } else { *i };
            }
            mask
        };
        let lanes = |sel: [(bool, u8); 4]| -> [(bool, u8); 16] {
            let mut out = [(false, 0u8); 16];
            for (l, (from_b, src)) in sel.iter().enumerate() {
                for k in 0..4 {
                    out[4 * l + k] = (*from_b, 4 * src + k as u8);
                }
            }
            out
        };
        match m {
            M::Addps | M::Subps | M::Mulps | M::Divps | M::Addpd | M::Subpd | M::Mulpd | M::Divpd => {
                let x = self.vcast(t, a);
                let y = self.vcast(t, b);
                let r = match m {
                    M::Addps | M::Addpd => self.b.ins().fadd(x, y),
                    M::Subps | M::Subpd => self.b.ins().fsub(x, y),
                    M::Mulps | M::Mulpd => self.b.ins().fmul(x, y),
                    _ => self.b.ins().fdiv(x, y),
                };
                self.nan_check(r, Some(x), y);
                self.vcast(I8X16, r)
            }
            M::Sqrtps | M::Sqrtpd => {
                let y = self.vcast(t, b);
                let r = self.b.ins().sqrt(y);
                self.nan_check(r, None, y);
                self.vcast(I8X16, r)
            }
            M::Andps | M::Andpd => self.b.ins().band(a, b),
            M::Orps | M::Orpd => self.b.ins().bor(a, b),
            M::Xorps | M::Xorpd => self.b.ins().bxor(a, b),
            M::Andnps | M::Andnpd => self.b.ins().band_not(b, a),
            M::Unpcklps => self.vshuffle(a, b, byte_mask(lanes([(false, 0), (true, 0), (false, 1), (true, 1)]))),
            M::Unpckhps => self.vshuffle(a, b, byte_mask(lanes([(false, 2), (true, 2), (false, 3), (true, 3)]))),
            M::Unpcklpd | M::Movlhps => self.vshuffle(a, b, byte_mask(lanes([(false, 0), (false, 1), (true, 0), (true, 1)]))),
            M::Unpckhpd => self.vshuffle(a, b, byte_mask(lanes([(false, 2), (false, 3), (true, 2), (true, 3)]))),
            // movhlps : basse ← haute de la source, haute inchangée.
            _ => self.vshuffle(a, b, byte_mask(lanes([(true, 2), (true, 3), (false, 2), (false, 3)]))),
        }
    }
    /// Mélange par immédiat : pshufd / pshuflw / pshufhw (source seule), shufps / shufpd (deux sources).
    fn simd_shuffle(&mut self, m: M, a: Value, b: Value, imm: u8) -> Value {
        let mut mask = [0u8; 16];
        match m {
            M::Pshufd => {
                // Source seule (second opérand), prise dans b.
                for l in 0..4 {
                    let s = (imm >> (2 * l)) & 3;
                    for k in 0..4 {
                        mask[4 * l + k] = 16 + 4 * s + k as u8;
                    }
                }
                self.vshuffle(a, b, mask)
            }
            M::Pshuflw | M::Pshufhw => {
                let high = m == M::Pshufhw;
                for (k, slot) in mask.iter_mut().enumerate() {
                    *slot = 16 + k as u8; // copie de la source
                }
                let base: u8 = if high { 8 } else { 0 };
                for w in 0..4u8 {
                    let s = (imm >> (2 * w)) & 3;
                    mask[(base + 2 * w) as usize] = 16 + base + 2 * s;
                    mask[(base + 2 * w + 1) as usize] = 16 + base + 2 * s + 1;
                }
                self.vshuffle(a, b, mask)
            }
            M::Shufps => {
                // Voies 0-1 de la destination (a), 2-3 de la source (b).
                for l in 0..4 {
                    let s = (imm >> (2 * l)) & 3;
                    let from_b = l >= 2;
                    for k in 0..4 {
                        mask[4 * l + k] = (if from_b { 16 } else { 0 }) + 4 * s + k as u8;
                    }
                }
                self.vshuffle(a, b, mask)
            }
            _ => {
                // shufpd : basse = a[imm bit 0], haute = b[imm bit 1].
                let lo = imm & 1;
                let hi = (imm >> 1) & 1;
                for k in 0..8 {
                    mask[k] = 8 * lo + k as u8;
                    mask[8 + k] = 16 + 8 * hi + k as u8;
                }
                self.vshuffle(a, b, mask)
            }
        }
    }
    /// Décalage par bits d'un vecteur (I8X16) : au-delà de la largeur, zéro (ou le signe pour psra*).
    fn simd_shift(&mut self, m: M, v: Value, count: Value) -> Value {
        use types::{I16X8, I32X4, I64X2, I8X16};
        let (t, bits) = match m {
            M::Psllw | M::Psrlw | M::Psraw => (I16X8, 16),
            M::Pslld | M::Psrld | M::Psrad => (I32X4, 32),
            _ => (I64X2, 64),
        };
        let x = self.vcast(t, v);
        let r = if matches!(m, M::Psraw | M::Psrad) {
            let max = self.b.ins().iconst(I64, bits - 1);
            let c = self.b.ins().umin(count, max);
            self.b.ins().sshr(x, c)
        } else {
            let shifted = if matches!(m, M::Psllw | M::Pslld | M::Psllq) { self.b.ins().ishl(x, count) } else { self.b.ins().ushr(x, count) };
            let zero = self.b.ins().iconst(types::I8, 0);
            let zero = self.b.ins().splat(I8X16, zero);
            let zero = self.vcast(t, zero);
            let big = self.b.ins().icmp_imm_u(IntCC::UnsignedGreaterThanOrEqual, count, bits);
            self.b.ins().select(big, zero, shifted)
        };
        self.vcast(I8X16, r)
    }

    // --- MMX ---

    fn mm(&self, ins: &Instruction, i: u32) -> Option<usize> {
        (ins.op_kind(i) == OpKind::Register && ins.op_register(i).is_mm()).then(|| ins.op_register(i).number())
    }
    fn mm_ptr(&mut self, n: usize) -> Value {
        self.b.ins().iadd_imm_s(self.ctx, (OFF_ST + 8 * n as i32) as i64)
    }
    /// Adresse d'un opérande de 64 bits : registre mm dans ctx, ou mémoire.
    fn m64(&mut self, ins: &Instruction, i: u32) -> Option<Value> {
        match ins.op_kind(i) {
            OpKind::Register => {
                let n = self.mm(ins, i)?;
                Some(self.mm_ptr(n))
            }
            OpKind::Memory => self.ea(ins),
            _ => None,
        }
    }
    fn mmx(&mut self, ins: &Instruction) -> Option<()> {
        let m = ins.mnemonic();
        if SIMD_INLINE.contains(&m) || SHIFT_INLINE.contains(&m) {
            let d = self.mm(ins, 0)?;
            let a = self.mm_get(d);
            let a = self.vfrom64(a);
            let r = if SHIFT_INLINE.contains(&m) {
                let count = if is_imm(ins.op1_kind()) {
                    self.b.ins().iconst(I64, ins.immediate(1) as i64)
                } else {
                    let p = self.m64(ins, 1)?;
                    self.mem_load(I64, p)
                };
                self.simd_shift(m, a, count)
            } else {
                let b = match self.mm(ins, 1) {
                    Some(s) => self.mm_get(s),
                    None => {
                        let p = self.m64(ins, 1)?;
                        self.mem_load(I64, p)
                    }
                };
                let b = self.vfrom64(b);
                self.simd_int(m, a, b, true)?
            };
            let q = self.vlow64(r);
            self.mm_set(d, q);
            return Some(());
        }
        if let Some(k) = MMX_BINARY.iter().position(|&x| x == m) {
            let d = self.mm(ins, 0)?;
            let d = self.mm_ptr(d);
            let s = self.m64(ins, 1)?;
            let k = self.b.ins().iconst(I32, k as i64);
            self.call_pure("rcn_op_mmx", &[I64, I64, I32], None, &[d, s, k]);
            return Some(());
        }
        if let Some(k) = MMX_SHIFT.iter().position(|&x| x == m) {
            let d = self.mm(ins, 0)?;
            let d = self.mm_ptr(d);
            let count = if is_imm(ins.op1_kind()) {
                self.b.ins().iconst(I64, ins.immediate(1) as i64)
            } else {
                let p = self.m64(ins, 1)?;
                self.mem_load(I64, p)
            };
            let k = self.b.ins().iconst(I32, k as i64);
            self.call_pure("rcn_op_mmx_shift", &[I64, I64, I32], None, &[d, count, k]);
            return Some(());
        }
        match m {
            M::Movd | M::Movq | M::Movntq | M::Movq2dq | M::Movdq2q => {
                let size = if m == M::Movd { 4 } else { 8 };
                if let Some(d) = self.mm(ins, 0) {
                    let v = if let Some(s) = self.mm(ins, 1) {
                        self.ctx_load(I64, OFF_ST + 8 * s as i32)
                    } else if let Some(s) = self.xmm(ins, 1) {
                        self.xmm_get(s, I64, 0)
                    } else {
                        let v = self.rd(ins, 1, size)?;
                        self.to64(v)
                    };
                    self.ctx_store(v, OFF_ST + 8 * d as i32);
                } else {
                    let s = self.mm(ins, 1)?;
                    let v = self.ctx_load(ty(size), OFF_ST + 8 * s as i32);
                    if let Some(d) = self.xmm(ins, 0) {
                        let zero = self.b.ins().iconst(I64, 0);
                        self.xmm_store(d, v, 0);
                        self.xmm_store(d, zero, 8);
                    } else {
                        self.wr(ins, 0, size, v)?;
                    }
                }
                Some(())
            }
            // Mots choisis par l'immédiat (2 bits chacun), de la source de 64 bits (décodeur vidéo d'Unity).
            M::Pshufw => {
                let d = self.mm(ins, 0)?;
                let src = match self.mm(ins, 1) {
                    Some(s) => self.mm_get(s),
                    None => {
                        let p = self.m64(ins, 1)?;
                        self.mem_load(I64, p)
                    }
                };
                let imm = ins.immediate(2);
                let mut r = self.b.ins().iconst(I64, 0);
                for i in 0..4 {
                    let sel = (imm >> (2 * i)) & 3;
                    let w = self.b.ins().ushr_imm_s(src, (16 * sel) as i64);
                    let w = self.b.ins().band_imm_s(w, 0xFFFF);
                    let w = self.b.ins().ishl_imm_s(w, 16 * i as i64);
                    r = self.b.ins().bor(r, w);
                }
                self.mm_set(d, r);
                Some(())
            }
            M::Pextrw => {
                let size = self.opsize(ins, 0);
                let s = self.mm(ins, 1)?;
                let v = self.mm_get(s);
                let v = self.b.ins().ushr_imm_s(v, (16 * (ins.immediate(2) & 3)) as i64);
                let v = self.b.ins().ireduce(I16, v);
                self.wr(ins, 0, size, v)
            }
            // Deux flottants (bas d'un xmm ou mémoire) tronqués en entiers de 32 bits (0x80000000 hors limites).
            M::Cvttps2pi | M::Cvtps2pi => {
                let d = self.mm(ins, 0)?;
                let (lo, hi) = match self.xmm(ins, 1) {
                    Some(s) => (self.xmm_get(s, F32, 0), self.xmm_get(s, F32, 4)),
                    None => {
                        let p = self.ea(ins)?;
                        let lo = self.mem_load(F32, p);
                        let p4 = self.b.ins().iadd_imm_s(p, 4);
                        (lo, self.mem_load(F32, p4))
                    }
                };
                let bits = self.b.ins().iconst(I32, 32);
                let trunc = self.b.ins().iconst(I32, (m == M::Cvttps2pi) as i64);
                let lo = self.b.ins().fpromote(F64, lo);
                let hi = self.b.ins().fpromote(F64, hi);
                let lo = self.call_pure("rcn_op_cvt", &[F64, I32, I32], Some(I64), &[lo, bits, trunc])?;
                let hi = self.call_pure("rcn_op_cvt", &[F64, I32, I32], Some(I64), &[hi, bits, trunc])?;
                let lo = self.b.ins().band_imm_s(lo, 0xFFFF_FFFF);
                let hi = self.b.ins().ishl_imm_s(hi, 32);
                let r = self.b.ins().bor(lo, hi);
                self.mm_set(d, r);
                Some(())
            }
            _ => None,
        }
    }

    fn sse(&mut self, ins: &Instruction) -> Option<()> {
        let m = ins.mnemonic();
        // Deux entiers de 32 bits (mm ou mémoire de 64 bits) en flottants : cvtpi2ps (bas du xmm, haut gardé),
        // cvtpi2pd (xmm entier). PhysX et les bibliothèques 32 bits de l'époque MMX (Dishonored).
        if matches!(m, M::Cvtpi2ps | M::Cvtpi2pd) {
            let d = self.xmm(ins, 0)?;
            let q = match ins.op1_kind() {
                OpKind::Register if ins.op1_register().is_mm() => self.mm_get(ins.op1_register().number()),
                OpKind::Memory => {
                    let p = self.ea(ins)?;
                    self.mem_load(I64, p)
                }
                _ => return None,
            };
            let lo = self.b.ins().ireduce(I32, q);
            let hi = self.b.ins().ushr_imm_s(q, 32);
            let hi = self.b.ins().ireduce(I32, hi);
            if m == M::Cvtpi2ps {
                let lo = self.b.ins().fcvt_from_sint(F32, lo);
                let hi = self.b.ins().fcvt_from_sint(F32, hi);
                self.xmm_store(d, lo, 0);
                self.xmm_store(d, hi, 4);
            } else {
                let lo = self.b.ins().fcvt_from_sint(F64, lo);
                let hi = self.b.ins().fcvt_from_sint(F64, hi);
                let v = self.b.ins().scalar_to_vector(types::F64X2, lo);
                let v = self.b.ins().insertlane(v, hi, 1);
                let v = self.vcast(types::I8X16, v);
                self.xmm_set(d, v);
            }
            return Some(());
        }
        if (0..ins.op_count()).any(|i| ins.op_kind(i) == OpKind::Register && ins.op_register(i).is_mm()) {
            return self.mmx(ins);
        }
        let dst = self.xmm(ins, 0);
        let name = || format!("rcn_op_{m:?}").to_lowercase();
        if FLOAT_INLINE.contains(&m) || SHUFFLE_INLINE.contains(&m) {
            let d = dst?;
            let a = self.vload_xmm(d);
            let b = match self.xmm(ins, 1) {
                Some(s) => self.vload_xmm(s),
                None => {
                    let p = self.x128(ins, 1)?;
                    self.mem_load(types::I8X16, p)
                }
            };
            let r = if FLOAT_INLINE.contains(&m) { self.simd_float(m, a, b) } else { self.simd_shuffle(m, a, b, ins.immediate(2) as u8) };
            self.xmm_set(d, r);
            return Some(());
        }
        if SIMD_INLINE.contains(&m) {
            let d = dst?;
            let a = self.vload_xmm(d);
            let b = match self.xmm(ins, 1) {
                Some(s) => self.vload_xmm(s),
                None => {
                    let p = self.x128(ins, 1)?;
                    self.mem_load(types::I8X16, p)
                }
            };
            let r = self.simd_int(m, a, b, false)?;
            self.xmm_set(d, r);
            return Some(());
        }
        if SHIFT_INLINE.contains(&m) {
            let d = dst?;
            let count = if is_imm(ins.op1_kind()) {
                self.b.ins().iconst(I64, ins.immediate(1) as i64)
            } else {
                let p = self.x128(ins, 1)?;
                self.mem_load(I64, p)
            };
            let a = self.vload_xmm(d);
            let r = self.simd_shift(m, a, count);
            self.xmm_set(d, r);
            return Some(());
        }
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
            match (self.xmm(ins, 0), self.xmm(ins, 1)) {
                (Some(d), Some(s)) => {
                    let v = self.vload_xmm(s);
                    self.xmm_set(d, v);
                    return Some(());
                }
                (Some(d), None) => {
                    let ea = self.ea(ins)?;
                    let v = self.mem_load(types::I8X16, ea);
                    self.xmm_set(d, v);
                    return Some(());
                }
                (None, Some(s)) => {
                    let v = self.vload_xmm(s);
                    let ea = self.ea(ins)?;
                    self.mem_store(v, ea);
                    return Some(());
                }
                _ => {}
            }
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
            let a = tr.xmm_get(d, t, 0);
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
                // Approximations de x86 (12 bits) : valeur exacte, plus précise.
                "rsqrt" => {
                    let one = tr.b.ins().f32const(1.0);
                    let s = tr.b.ins().sqrt(b);
                    tr.b.ins().fdiv(one, s)
                }
                "rcp" => {
                    let one = tr.b.ins().f32const(1.0);
                    tr.b.ins().fdiv(one, b)
                }
                _ => tr.b.ins().sqrt(b),
            };
            match op {
                "+" | "-" | "*" | "/" => tr.nan_check(r, Some(a), b),
                "sqrt" => tr.nan_check(r, None, b),
                _ => {}
            }
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
            M::Rsqrtss => scalar(self, false, "rsqrt"),
            M::Rcpss => scalar(self, false, "rcp"),
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
                        self.xmm_from_low(d, v);
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
                        Some(s) => self.xmm_get(s, ty(size), 0),
                        None => self.rd(ins, 1, size)?,
                    };
                    let v = self.to64(v);
                    self.xmm_from_low(d, v);
                    Some(())
                } else {
                    let s = self.xmm(ins, 1)?;
                    let v = self.xmm_get(s, ty(size), 0);
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
                        let v = self.xmm_get(s, I64, half);
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
            M::Pshufd | M::Pshuflw | M::Pshufhw | M::Shufps | M::Shufpd | M::Cmpps | M::Cmppd | M::Palignr | M::Dpps
            | M::Dppd => {
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
                let a = self.xmm_get(d, t, 0);
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
                let v = self.xmm_get(s, I16, 2 * (ins.immediate(2) & 7) as i32);
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
            _ => self.sse4(ins, dst),
        }
    }


    // --- AVX (instructions VEX) ---
    //
    // Forme générale : l'instruction SSE équivalente (même nom sans « v »), sur chaque moitié de 128 bits
    // d'un registre ymm (la moitié haute : registres 16–31 du cache, voir XREGS). La première source
    // supplémentaire de VEX (champ vvvv) est d'abord recopiée dans la destination ; si la destination est
    // aussi la seconde source, le calcul se fait dans un registre de travail. Une opération sur 128 bits
    // remet à zéro la moitié haute de sa destination. Les instructions qui mêlent les moitiés, changent de
    // largeur ou n'ont pas d'équivalent SSE passent par rcn_op_avx (rcn_ops.c).

    /// Registre de 128 bits (voir XREGS) de l'opérande `i` d'une instruction VEX, pour la moitié `half`
    /// (un registre xmm ne change pas d'une moitié à l'autre : compte d'un décalage, par exemple).
    fn vex_reg(ins: &Instruction, i: u32, half: usize) -> Option<usize> {
        if ins.op_kind(i) != OpKind::Register {
            return None;
        }
        let r = ins.op_register(i);
        if r.is_ymm() {
            Some(r.number() + 16 * half)
        } else if r.is_xmm() {
            Some(r.number())
        } else {
            None
        }
    }
    /// Numéro de ymm (0–15) de l'opérande `i`, 0xFF pour un opérande mémoire.
    fn vex_num(ins: &Instruction, i: u32) -> u32 {
        if ins.op_kind(i) == OpKind::Register && (ins.op_register(i).is_xmm() || ins.op_register(i).is_ymm()) {
            ins.op_register(i).number() as u32
        } else {
            0xFF
        }
    }
    fn vex_wide(ins: &Instruction) -> bool {
        (0..ins.op_count()).any(|i| ins.op_kind(i) == OpKind::Register && ins.op_register(i).is_ymm())
            || (ins.op_count() > 0 && (0..ins.op_count()).any(|i| ins.op_kind(i) == OpKind::Memory) && ins.memory_size().size() == 32)
    }
    /// Moitié haute (registre 16 + n) remise à zéro.
    fn ymm_zero_high(&mut self, n: usize) {
        if n < 16 {
            let z = self.b.ins().iconst(I64, 0);
            let z = self.b.ins().splat(types::I64X2, z);
            let z = self.vcast(types::I8X16, z);
            self.xmm_set(16 + n, z);
        }
    }
    /// rcn_op_avx(ctx, op, regs, mem, imm) : opérandes dans ctx (registres rangés avant, relus après).
    fn avx_call(&mut self, ins: &Instruction, op: u32, d: u32, a: u32, b: u32, imm: u32) -> Option<()> {
        let wide = Self::vex_wide(ins);
        let mem = if (0..ins.op_count()).any(|i| ins.op_kind(i) == OpKind::Memory) { self.ea(ins)? } else { self.b.ins().iconst(I64, 0) };
        let regs = self.b.ins().iconst(I32, (d | a << 8 | b << 16 | (wide as u32) << 24) as i64);
        let op = self.b.ins().iconst(I32, op as i64);
        let imm = self.b.ins().iconst(I32, imm as i64);
        let ctx = self.ctx;
        self.call_helper("rcn_op_avx", &[I64, I32, I32, I64, I32], None, &[ctx, op, regs, mem, imm]);
        Some(())
    }

    /// Instructions VEX traitées à part (rcn_op_avx) ; None si l'instruction suit la forme générale.
    fn avx_special(&mut self, ins: &Instruction) -> Option<Option<()>> {
        const BROADCAST: u32 = 1;
        const INSERT128: u32 = 2;
        const EXTRACT128: u32 = 3;
        const PERM2X128: u32 = 4;
        const PERMQ: u32 = 5;
        const PERMD: u32 = 6;
        const PERMIL_IMM: u32 = 7;
        const PERMIL_VAR: u32 = 8;
        const FMA: u32 = 9;
        const BLENDV: u32 = 10;
        const BLEND_IMM: u32 = 11;
        const CVT: u32 = 12;
        const PMOVX: u32 = 13;
        const PTEST: u32 = 14;
        const MOVMSK: u32 = 15;
        const CMP: u32 = 16;
        const VSHIFT: u32 = 17;
        const MASKLOAD: u32 = 18;
        const MASKSTORE: u32 = 19;
        let m = ins.mnemonic();
        let n = |i: u32| Self::vex_num(ins, i);
        let wide = Self::vex_wide(ins);
        let imm8 = || ins.immediate8() as u32;
        let last = ins.op_count().saturating_sub(1);
        let r = match m {
            M::Vbroadcastss => self.avx_call(ins, BROADCAST, n(0), 0, n(1), 4),
            M::Vbroadcastsd => self.avx_call(ins, BROADCAST, n(0), 0, n(1), 8),
            M::Vbroadcastf128 | M::Vbroadcasti128 => self.avx_call(ins, BROADCAST, n(0), 0, n(1), 16),
            M::Vpbroadcastb => self.avx_call(ins, BROADCAST, n(0), 0, n(1), 1),
            M::Vpbroadcastw => self.avx_call(ins, BROADCAST, n(0), 0, n(1), 2),
            M::Vpbroadcastd => self.avx_call(ins, BROADCAST, n(0), 0, n(1), 4),
            M::Vpbroadcastq => self.avx_call(ins, BROADCAST, n(0), 0, n(1), 8),
            M::Vinsertf128 | M::Vinserti128 => self.avx_call(ins, INSERT128, n(0), n(1), n(2), imm8()),
            M::Vextractf128 | M::Vextracti128 => self.avx_call(ins, EXTRACT128, n(0), n(1), 0xFF, imm8()),
            M::Vperm2f128 | M::Vperm2i128 => self.avx_call(ins, PERM2X128, n(0), n(1), n(2), imm8()),
            M::Vpermq | M::Vpermpd => self.avx_call(ins, PERMQ, n(0), 0, n(1), imm8()),
            M::Vpermd | M::Vpermps => self.avx_call(ins, PERMD, n(0), n(1), n(2), 0),
            M::Vpermilps | M::Vpermilpd => {
                let size = if m == M::Vpermilpd { 8 } else { 4 };
                if is_imm(ins.op_kind(2)) {
                    self.avx_call(ins, PERMIL_IMM, n(0), 0, n(1), imm8() | size << 8)
                } else {
                    self.avx_call(ins, PERMIL_VAR, n(0), n(1), n(2), size)
                }
            }
            M::Vblendvps | M::Vblendvpd | M::Vpblendvb => {
                let size = match m {
                    M::Vblendvps => 4,
                    M::Vblendvpd => 8,
                    _ => 1,
                };
                self.avx_call(ins, BLENDV, n(0), n(1), n(2), size | n(3) << 8)
            }
            M::Vpblendd => self.avx_call(ins, BLEND_IMM, n(0), n(1), n(2), imm8() | 4 << 8),
            M::Vcvtps2pd => self.avx_call(ins, CVT, n(0), 0, n(1), 0),
            M::Vcvtdq2pd => self.avx_call(ins, CVT, n(0), 0, n(1), 1),
            M::Vcvtpd2ps => self.avx_call(ins, CVT, n(0), 0, n(1), 2),
            M::Vcvttpd2dq => self.avx_call(ins, CVT, n(0), 0, n(1), 3),
            M::Vcvtpd2dq => self.avx_call(ins, CVT, n(0), 0, n(1), 4),
            M::Vcvtph2ps => self.avx_call(ins, CVT, n(0), 0, n(1), 5),
            M::Vcvtps2ph => self.avx_call(ins, CVT, n(0), n(1), 0xFF, 6),
            M::Vptest => self.avx_call(ins, PTEST, 0xFF, n(0), n(1), 0),
            M::Vtestps => self.avx_call(ins, PTEST, 0xFF, n(0), n(1), 4),
            M::Vtestpd => self.avx_call(ins, PTEST, 0xFF, n(0), n(1), 8),
            M::Vmovmskps | M::Vmovmskpd | M::Vpmovmskb if wide => {
                let size = match m {
                    M::Vmovmskps => 4,
                    M::Vmovmskpd => 8,
                    _ => 1,
                };
                let gpr = ins.op0_register().full_register().number() as u32;
                self.avx_call(ins, MOVMSK, gpr, n(1), 0xFF, size)
            }
            M::Vcmpps | M::Vcmppd | M::Vcmpss | M::Vcmpsd => {
                let t = match m {
                    M::Vcmpps => 0,
                    M::Vcmppd => 1,
                    M::Vcmpss => 2,
                    _ => 3,
                };
                self.avx_call(ins, CMP, n(0), n(1), n(2), (imm8() & 0x1F) | t << 8)
            }
            M::Vpsllvd => self.avx_call(ins, VSHIFT, n(0), n(1), n(2), 0),
            M::Vpsrlvd => self.avx_call(ins, VSHIFT, n(0), n(1), n(2), 1),
            M::Vpsravd => self.avx_call(ins, VSHIFT, n(0), n(1), n(2), 2),
            M::Vpsllvq => self.avx_call(ins, VSHIFT, n(0), n(1), n(2), 3),
            M::Vpsrlvq => self.avx_call(ins, VSHIFT, n(0), n(1), n(2), 4),
            M::Vmaskmovps | M::Vmaskmovpd | M::Vpmaskmovd | M::Vpmaskmovq => {
                let size = if matches!(m, M::Vmaskmovpd | M::Vpmaskmovq) { 8 } else { 4 };
                if ins.op0_kind() == OpKind::Memory {
                    self.avx_call(ins, MASKSTORE, 0xFF, n(1), n(2), size)
                } else {
                    self.avx_call(ins, MASKLOAD, n(0), n(1), 0xFF, size)
                }
            }
            _ => {
                // Extensions (vpmovzx*, vpmovsx*) sur 256 bits : la source est plus étroite que la moitié.
                let pmovx = |name: &str| -> Option<(u32, u32, u32)> {
                    let (sign, rest) = if let Some(r) = name.strip_prefix("Vpmovzx") { (0, r) } else { (1, name.strip_prefix("Vpmovsx")?) };
                    let size = |c: char| match c {
                        'b' => Some(1),
                        'w' => Some(2),
                        'd' => Some(4),
                        'q' => Some(8),
                        _ => None,
                    };
                    let mut c = rest.chars();
                    Some((size(c.next()?)?, size(c.next()?)?, sign))
                };
                if wide {
                    if let Some((from, to, sign)) = pmovx(&format!("{m:?}")) {
                        return Some(self.avx_call(ins, PMOVX, n(0), 0, n(1), from | to << 4 | sign << 8));
                    }
                }
                // FMA : vfmadd132ps… (forme, produit négatif, soustraction, type).
                let name = format!("{m:?}").to_lowercase();
                let fma = ["vfmaddsub", "vfmsubadd", "vfnmadd", "vfnmsub", "vfmadd", "vfmsub"].iter().find(|p| name.starts_with(*p));
                let Some(prefix) = fma else { return None };
                let rest = &name[prefix.len()..];
                let form = match &rest[..3.min(rest.len())] {
                    "132" => 0,
                    "213" => 1,
                    "231" => 2,
                    _ => return None,
                };
                let t = match &rest[3..] {
                    "ps" => 0,
                    "pd" => 1,
                    "ss" => 2,
                    "sd" => 3,
                    _ => return None,
                };
                let (neg, sub, extra) = match *prefix {
                    "vfmadd" => (0, 0, 0),
                    "vfmsub" => (0, 8, 0),
                    "vfnmadd" => (4, 0, 0),
                    "vfnmsub" => (4, 8, 0),
                    "vfmaddsub" => (0, 0, 64),
                    _ => (0, 0, 128),
                };
                let _ = last;
                self.avx_call(ins, FMA, n(0), n(1), n(2), form | neg | sub | extra | t << 4)
            }
        };
        Some(r)
    }

    fn avx(&mut self, ins: &Instruction) -> Option<()> {
        let m = ins.mnemonic();
        if matches!(m, M::Vzeroupper | M::Vzeroall) {
            for n in 0..16 {
                self.ymm_zero_high(n);
                if m == M::Vzeroall {
                    let z = self.b.ins().iconst(I64, 0);
                    let z = self.b.ins().splat(types::I64X2, z);
                    let z = self.vcast(types::I8X16, z);
                    self.xmm_set(n, z);
                }
            }
            return Some(());
        }
        if let Some(r) = self.avx_special(ins) {
            return r;
        }
        let (legacy, vvvv) = vex_to_legacy(ins.code())?;
        let wide = Self::vex_wide(ins);
        for half in 0..if wide { 2 } else { 1 } {
            self.avx_half(ins, legacy, vvvv, half)?;
        }
        // VEX sur 128 bits : moitié haute de la destination remise à zéro.
        if !wide && ins.op0_kind() == OpKind::Register && ins.op0_register().is_xmm() {
            self.ymm_zero_high(ins.op0_register().number());
        }
        Some(())
    }

    /// Moitié `half` d'une instruction VEX par son équivalent SSE `legacy`.
    fn avx_half(&mut self, ins: &Instruction, legacy: Code, vvvv: Option<u32>, half: usize) -> Option<()> {
        let m = ins.mnemonic();
        let mut s = *ins;
        s.set_code(legacy);
        let ops: Vec<u32> = (0..ins.op_count()).filter(|&i| Some(i) != vvvv).collect();
        let mut remap = [None; 4];
        for (j, &i) in ops.iter().enumerate() {
            let j = j as u32;
            s.set_op_kind(j, ins.op_kind(i));
            if ins.op_kind(i) == OpKind::Register {
                let r = ins.op_register(i);
                let r = if r.is_ymm() { Register::values().nth(Register::XMM0 as usize + r.number())? } else { r };
                s.set_op_register(j, r);
                remap[j as usize] = Self::vex_reg(ins, i, half);
            }
        }
        if half == 1 {
            // Opérande mémoire de 256 bits : sa moitié haute.
            if ops.iter().any(|&i| ins.op_kind(i) == OpKind::Memory) && ins.memory_size().size() == 32 {
                s.set_memory_displacement64(ins.memory_displacement64().wrapping_add(16));
            }
            // Valeur immédiate dont les bits diffèrent d'une moitié à l'autre.
            let shift = match m {
                M::Vshufpd | M::Vblendpd => 2,
                M::Vblendps => 4,
                M::Vmpsadbw => 3,
                _ => 0,
            };
            if shift != 0 {
                s.set_immediate8(ins.immediate8() >> shift);
            }
        }
        let mut result_to = None;
        if let Some(v) = vvvv {
            let a = Self::vex_reg(ins, v, half)?;
            let d = remap[0]?;
            let d_is_source = remap[1..].iter().any(|&r| r == Some(d));
            if d != a {
                let target = if d_is_source { XSCRATCH } else { d };
                let value = self.vload_xmm(a);
                self.xmm_set(target, value);
                if d_is_source {
                    remap[0] = Some(XSCRATCH);
                    result_to = Some(d);
                }
            }
        }
        self.op_remap = remap;
        self.vec_written = false;
        let r = self.sse(&s);
        let dst = self.op_remap[0];
        self.op_remap = [None; 4];
        r?;
        if !self.vec_written {
            if let Some(d) = dst {
                self.xmm_sync(d);
            }
        }
        if let Some(d) = result_to {
            let value = self.vload_xmm(XSCRATCH);
            self.xmm_set(d, value);
        }
        Some(())
    }

    /// SSE4.1 : mélanges, arrondis, extensions, extractions et insertions d'éléments, ptest.
    fn sse4(&mut self, ins: &Instruction, dst: Option<usize>) -> Option<()> {
        use types::{F32X4, F64X2, I16X8, I32X4, I8X16};
        let m = ins.mnemonic();
        // Source de 128 bits en vecteur (registre ou mémoire).
        let src128 = |tr: &mut Self| -> Option<Value> {
            match tr.xmm(ins, 1) {
                Some(s) => Some(tr.vload_xmm(s)),
                None => {
                    let p = tr.x128(ins, 1)?;
                    Some(tr.mem_load(I8X16, p))
                }
            }
        };
        let imm = ins.immediate(2) as u8;
        match m {
            M::Blendps | M::Blendpd | M::Pblendw => {
                let d = dst?;
                let a = self.vload_xmm(d);
                let b = src128(self)?;
                let (size, count) = match m {
                    M::Blendps => (4u8, 4u8),
                    M::Blendpd => (8, 2),
                    _ => (2, 8),
                };
                let mut mask = [0u8; 16];
                for e in 0..count {
                    let from_b = (imm >> e) & 1 != 0;
                    for k in 0..size {
                        let i = e * size + k;
                        mask[i as usize] = if from_b { 16 + i } else { i };
                    }
                }
                let r = self.vshuffle(a, b, mask);
                self.xmm_set(d, r);
                Some(())
            }
            M::Blendvps | M::Blendvpd | M::Pblendvb => {
                let d = self.xmm_ptr(dst?);
                let s = self.x128(ins, 1)?;
                let x0 = self.xmm_ptr(0);
                let name = format!("rcn_op_{m:?}").to_lowercase();
                self.call_pure(&name, &[I64, I64, I64], None, &[d, s, x0]);
                Some(())
            }
            M::Roundps | M::Roundpd | M::Roundss | M::Roundsd => {
                let d = dst?;
                let mode = if imm & 4 != 0 { 0 } else { imm & 3 };
                let round = |tr: &mut Self, v: Value| -> Value {
                    match mode {
                        0 => tr.b.ins().nearest(v),
                        1 => tr.b.ins().floor(v),
                        2 => tr.b.ins().ceil(v),
                        _ => tr.b.ins().trunc(v),
                    }
                };
                match m {
                    M::Roundps | M::Roundpd => {
                        let v = src128(self)?;
                        let v = self.vcast(if m == M::Roundps { F32X4 } else { F64X2 }, v);
                        let r = round(self, v);
                        let r = self.vcast(I8X16, r);
                        self.xmm_set(d, r);
                    }
                    _ => {
                        let v = self.xlow(ins, 1, if m == M::Roundss { F32 } else { F64 })?;
                        let r = round(self, v);
                        self.xmm_store(d, r, 0);
                    }
                }
                Some(())
            }
            M::Pmovsxbw | M::Pmovsxbd | M::Pmovsxbq | M::Pmovsxwd | M::Pmovsxwq | M::Pmovsxdq | M::Pmovzxbw
            | M::Pmovzxbd | M::Pmovzxbq | M::Pmovzxwd | M::Pmovzxwq | M::Pmovzxdq => {
                let d = dst?;
                let (from, steps, bytes) = match m {
                    M::Pmovsxbw | M::Pmovzxbw => (I8X16, 1, 8),
                    M::Pmovsxbd | M::Pmovzxbd => (I8X16, 2, 4),
                    M::Pmovsxbq | M::Pmovzxbq => (I8X16, 3, 2),
                    M::Pmovsxwd | M::Pmovzxwd => (I16X8, 1, 8),
                    M::Pmovsxwq | M::Pmovzxwq => (I16X8, 2, 4),
                    _ => (I32X4, 1, 8),
                };
                let signed = matches!(m, M::Pmovsxbw | M::Pmovsxbd | M::Pmovsxbq | M::Pmovsxwd | M::Pmovsxwq | M::Pmovsxdq);
                // Mémoire : seuls les octets utiles sont lus.
                let v = match self.xmm(ins, 1) {
                    Some(s) => self.vload_xmm(s),
                    None => {
                        let ea = self.ea(ins)?;
                        let q = self.mem_load(ty(bytes), ea);
                        let q = self.to64(q);
                        self.vfrom64(q)
                    }
                };
                let mut v = self.vcast(from, v);
                for _ in 0..steps {
                    v = if signed { self.b.ins().swiden_low(v) } else { self.b.ins().uwiden_low(v) };
                }
                let r = self.vcast(I8X16, v);
                self.xmm_set(d, r);
                Some(())
            }
            M::Pextrb | M::Pextrd | M::Pextrq | M::Extractps => {
                let s = self.xmm(ins, 1)?;
                let (t, count) = match m {
                    M::Pextrb => (I8, 16),
                    M::Pextrq => (I64, 2),
                    _ => (I32, 4),
                };
                let lane = (ins.immediate(2) as u32 % count) as i32;
                let v = self.xmm_get(s, t, lane * t.bytes() as i32);
                let size = self.opsize(ins, 0);
                self.wr(ins, 0, size, v)
            }
            M::Pinsrb | M::Pinsrd | M::Pinsrq => {
                let (bytes, count) = match m {
                    M::Pinsrb => (1u32, 16u32),
                    M::Pinsrd => (4, 4),
                    _ => (8, 2),
                };
                let v = match ins.op1_kind() {
                    OpKind::Register => {
                        let size = ins.op_register(1).size() as u32;
                        self.rd(ins, 1, size)?
                    }
                    _ => self.rd(ins, 1, bytes)?,
                };
                let v = self.fit(v, bytes);
                let lane = (ins.immediate(2) as u32 % count) as i32;
                self.xmm_store(dst?, v, lane * bytes as i32);
                Some(())
            }
            M::Insertps => {
                let d = dst?;
                let v = match self.xmm(ins, 1) {
                    Some(s) => self.xmm_get(s, I32, 4 * ((imm >> 6) & 3) as i32),
                    None => {
                        let ea = self.ea(ins)?;
                        self.mem_load(I32, ea)
                    }
                };
                self.xmm_store(d, v, 4 * ((imm >> 4) & 3) as i32);
                let zmask = imm & 15;
                if zmask != 0 {
                    let a = self.vload_xmm(d);
                    let z = self.b.ins().iconst(types::I8, 0);
                    let z = self.b.ins().splat(I8X16, z);
                    let mut mask = [0u8; 16];
                    for (i, slot) in mask.iter_mut().enumerate() {
                        *slot = if (zmask >> (i / 4)) & 1 != 0 { 16 + i as u8 } else { i as u8 };
                    }
                    let r = self.vshuffle(a, z, mask);
                    self.xmm_set(d, r);
                }
                Some(())
            }
            M::Pcmpestri | M::Pcmpestrm | M::Pcmpistri | M::Pcmpistrm | M::Pcmpestri64 | M::Pcmpestrm64 => {
                let explicit = matches!(m, M::Pcmpestri | M::Pcmpestrm | M::Pcmpestri64 | M::Pcmpestrm64);
                let mask = matches!(m, M::Pcmpestrm | M::Pcmpistrm | M::Pcmpestrm64);
                let wide = matches!(m, M::Pcmpestri64 | M::Pcmpestrm64);
                let info = (ins.immediate(2) as u32 & 0xFF) | (explicit as u32) << 8 | (mask as u32) << 9 | (wide as u32) << 10;
                let a = self.xmm_ptr(dst?);
                let b = self.x128(ins, 1)?;
                self.xmm_ptr(0);
                let info = self.b.ins().iconst(I32, info as i64);
                let ctx = self.ctx;
                self.call_helper("rcn_op_pcmpstr", &[I64, I64, I64, I32], None, &[ctx, a, b, info]);
                self.vec_forget();
                self.xmm[0] = None;
                Some(())
            }
            M::Ptest => {
                let a = self.vload_xmm(dst?);
                let b = src128(self)?;
                let and = self.b.ins().band(a, b);
                let andn = self.b.ins().band_not(b, a);
                let any = self.b.ins().vany_true(and);
                let zf = self.b.ins().bxor_imm_s(any, 1);
                let any = self.b.ins().vany_true(andn);
                let cf = self.b.ins().bxor_imm_s(any, 1);
                self.set_flag(ZF, zf);
                self.set_flag(CF, cf);
                for i in [OF, SF, AF, PF] {
                    let z = self.zero8();
                    self.set_flag(i, z);
                }
                Some(())
            }
            _ => {
                let _ = (I16X8, I32X4);
                None
            }
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
            M::Fnstenv | M::Fldenv | M::Fnsave | M::Frstor => {
                let ea = self.ea(ins)?;
                let ctx = self.ctx;
                let which = self.b.ins().iconst(I32, match m { M::Fnstenv => 0, M::Fldenv => 1, M::Fnsave => 2, _ => 3 });
                self.call_helper("rcn_op_fenv", &[I64, I64, I32], None, &[ctx, ea, which]);
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
        self.cur_rva = rva;
        // Adresses absolues relocalisées dans l'instruction (32 bits) : la case de 4 octets relocalisée vaut
        // le déplacement ou la valeur immédiate.
        self.disp_reloc = false;
        self.imm_reloc = false;
        if !self.n.code_relocs.is_empty() {
            let imm = (0..ins.op_count()).find(|&i| is_imm(ins.op_kind(i))).map(|i| ins.immediate(i) as u32);
            let disp = ins.memory_displacement32();
            for &at in self.n.code_relocs.range(rva..rva + ins.len() as u32) {
                let Some(v) = self.n.pe.u32(at) else { continue };
                if v == disp && (0..ins.op_count()).any(|i| ins.op_kind(i) == OpKind::Memory) {
                    self.disp_reloc = true;
                }
                if imm == Some(v) {
                    self.imm_reloc = true;
                }
            }
        }
        // Accès privés au thread (sans barrière en mode TSO) : par esp, ou par ebp dans une fonction à pointeur
        // de cadre.
        let base = ins.memory_base();
        self.mem_private = matches!(base, Register::RSP | Register::ESP)
            || (self.frame_pointer && matches!(base, Register::RBP | Register::EBP))
            || self.read_only_target(ins);
        // Barrières explicites de x86 : barrière complète (en mode TSO comme sans lui).
        if matches!(m, M::Mfence | M::Sfence | M::Lfence) {
            self.b.ins().fence();
            self.mem_last = 0;
            return Some(());
        }
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
                self.cond_branch(ins, rva, next, c);
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
            M::Mov | M::Movnti => {
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
                    self.atomic_any(t, Some(op), ea, one, None)
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
                    k if is_imm(k) && self.imm_reloc => {
                        let v = self.image_address(ins.immediate(0));
                        self.b.ins().uextend(I64, v)
                    }
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
                    if self.back_edge(ins, rva) {
                        self.edge();
                    } else {
                        self.flush_regs();
                    }
                    let b = self.jump_block(ins, rva);
                    self.b.ins().jump(b, &[]);
                    self.terminated = true;
                    return Some(());
                }
                let t = self.rd(ins, 0, ps)?;
                let t = self.to64(t);
                let fallback = self.b.create_block();
                self.flush_regs();
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
                self.switch_block(fallback);
                // Pile remontée au-dessus de l'entrée de la fonction : reprise non locale dans une fonction
                // appelante (fin d'un catch .NET, RtlRestoreContext…), signalée et remontée appel par appel.
                if let Some(entry) = self.entry_rsp {
                    let rsp = self.b.use_var(self.regs[4]);
                    let above = self.b.ins().icmp(IntCC::UnsignedGreaterThan, rsp, entry);
                    let resume = self.b.create_block();
                    let normal = self.b.create_block();
                    self.b.ins().brif(above, resume, &[], normal, &[]);
                    // Le rangement de cette branche ne vaut pas pour l'autre : état gardé pour elle.
                    let (dirty, xdirty, xmm) = (self.dirty, self.xdirty, self.xmm);
                    self.switch_block(resume);
                    self.ctx_store(rsp, OFF_RESUME_RSP);
                    self.ctx_store(t, OFF_RESUME_RIP);
                    let one = self.b.ins().iconst(I64, UNWIND_RESUME);
                    self.ctx_store(one, OFF_UNWIND);
                    self.ret();
                    self.terminated = false;
                    self.switch_block(normal);
                    (self.dirty, self.xdirty, self.xmm) = (dirty, xdirty, xmm);
                }
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
                self.cond_branch(ins, rva, next, test);
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
                let old = self.atomic_any(ty(size0), Some(AtomicRmwOp::Xchg), ea, v, None);
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
                    self.atomic_any(ty(size0), Some(AtomicRmwOp::Add), ea, b, None)
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
                    { let t = self.b.func.dfg.value_type(src); self.atomic_any(t, None, ea, src, Some(a)) }
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
                    self.atomic_any(t, Some(op), ea, operand, None)
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
            // Rotation à travers CF : par le runtime (CF et OF y sont lus et écrits).
            M::Rcl | M::Rcr => {
                if ins.has_lock_prefix() {
                    return None;
                }
                let t = ty(size0);
                let a = self.rd(ins, 0, size0)?;
                let a64 = if size0 < 8 { self.b.ins().uextend(I64, a) } else { a };
                let count = match ins.op1_kind() {
                    OpKind::Register => {
                        let c = self.rd(ins, 1, 1)?;
                        self.b.ins().uextend(I64, c)
                    }
                    _ => self.iconst(I64, ins.immediate(1)),
                };
                let info = self.b.ins().iconst(I32, (size0 as i64) | if m == M::Rcl { 0x100 } else { 0 });
                let ctx = self.ctx;
                let r = self.call_helper("rcn_op_rcx", &[I64, I64, I64, I32], Some(I64), &[ctx, a64, count, info])?;
                let r = if size0 < 8 { self.b.ins().ireduce(t, r) } else { r };
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
            M::Cmpxchg16b => {
                let ea = self.ea(ins)?;
                let ctx = self.ctx;
                self.call_helper("rcn_op_cmpxchg16b", &[I64, I64], None, &[ctx, ea]);
                Some(())
            }
            M::Cmpxchg8b => {
                let ea = self.ea(ins)?;
                let ctx = self.ctx;
                self.call_helper("rcn_op_cmpxchg8b", &[I64, I64], None, &[ctx, ea]);
                Some(())
            }
            M::Fxsave | M::Fxsave64 | M::Fxrstor | M::Fxrstor64 => {
                let ea = self.ea(ins)?;
                let ctx = self.ctx;
                let save = matches!(m, M::Fxsave | M::Fxsave64);
                self.call_helper(if save { "rcn_op_fxsave" } else { "rcn_op_fxrstor" }, &[I64, I64], None, &[ctx, ea]);
                Some(())
            }
            M::Cpuid | M::Rdtsc => {
                let ctx = self.ctx;
                self.call_helper(if m == M::Cpuid { "rcn_cpuid" } else { "rcn_rdtsc" }, &[I64], None, &[ctx]);
                Some(())
            }
            // XCR0 : états x87, SSE et AVX actifs (AVX n'est annoncé par cpuid qu'avec RCN_AVX).
            M::Xgetbv => {
                let three = self.b.ins().iconst(I64, 7);
                let zero = self.b.ins().iconst(I64, 0);
                self.set(0, three);
                self.set(2, zero);
                Some(())
            }
            M::Stmxcsr | M::Vstmxcsr => {
                let v = self.ctx_load(I32, OFF_MXCSR);
                self.wr(ins, 0, 4, v)
            }
            M::Ldmxcsr | M::Vldmxcsr => {
                let v = self.rd(ins, 0, 4)?;
                self.ctx_store(v, OFF_MXCSR);
                Some(())
            }
            M::Int3 | M::Ud2 | M::Int | M::Hlt | M::Syscall => {
                self.trap(rva, &format!("{m:?}").to_lowercase());
                Some(())
            }
            _ => {
                self.vec_written = false;
                if let Some(r) = self.x87(ins) {
                    // Pile x87 : mêmes cases que mm0–7.
                    for n in 0..8 {
                        self.mm_sync(n);
                    }
                    return Some(r);
                }
                // AVX (encodage VEX) : formes SSE moitié par moitié, ou rcn_op_avx ; tient le cache à jour.
                if ins.encoding() == iced_x86::EncodingKind::VEX {
                    return self.avx(ins);
                }
                let r = self.sse(ins);
                if r.is_some() {
                    if matches!(m, M::Fxrstor | M::Fxrstor64 | M::Xrstor | M::Xrstor64) {
                        for n in 0..16 {
                            self.xmm_sync(n);
                        }
                        for n in 0..8 {
                            self.mm_sync(n);
                        }
                    } else if !self.vec_written && ins.op_count() > 0 && ins.op0_kind() == OpKind::Register {
                        let reg = ins.op0_register();
                        if reg.is_xmm() {
                            self.xmm_sync(reg.number());
                        } else if reg.is_mm() {
                            self.mm_sync(reg.number());
                        }
                    }
                }
                r
            }
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
        self.switch_block(cont);
        self.reload();
        Some(())
    }

    fn start(&mut self) {
        let entry = self.b.create_block();
        self.b.append_block_params_for_function_params(entry);
        self.switch_block(entry);
        self.ctx_param = self.b.block_params(entry)[0];
        self.ctx = self.ctx_param;
        // Adresse de chargement du module (exécutable ou DLL) : case « @base » propre à chaque module,
        // remplie par le chargeur.
        let id = self.n.helper_id("@base");
        let user = self.b.func.declare_imported_user_function(UserExternalName::new(NS_HELPER, id));
        let gv = self.b.create_global_value(GlobalValueData::Symbol {
            name: ExternalName::user(user),
            offset: Imm64::new(0),
            colocated: false,
            tls: false,
        });
        let slot = self.b.ins().symbol_value(I64, gv);
        self.base = self.b.ins().load(I64, MemFlagsData::trusted(), slot, 0);
        let zero = self.b.ins().load(I64, MemFlagsData::trusted(), slot, 8);
        self.ctx = self.b.ins().iadd(self.ctx_param, zero);
        self.reload();
        self.entry_rsp = Some(self.b.use_var(self.regs[4]));
        // Mise au point (RCN_RECOMP_PROBE=rva,rva…) : entrée de ces fonctions signalée au runtime.
        if self.n.probe_fns.contains(&self.f.entry) {
            let ctx = self.ctx;
            let rva = self.b.ins().iconst(I64, self.f.entry as i64);
            self.call_pure("rcn_op_probe", &[I64, I64], None, &[ctx, rva]);
        }
    }

    /// L'exécution se poursuit hors du code décodé de la fonction : dans une autre fonction connue (suite
    /// d'une fonction découpée en plusieurs morceaux, comme Visual C++ en produit), sinon arrêt.
    fn continue_at(&mut self, rva: u32) {
        if self.n.funcs.contains(&rva) {
            self.call_func(rva);
            self.ret();
        } else {
            self.trap(rva, "fin du code traduit");
        }
    }

    /// La fonction appelle-t-elle setjmp ?
    fn calls_setjmp(&self) -> bool {
        self.f.insts.values().any(|ins| {
            ins.mnemonic() == M::Call
                && (branch_target(self.n.pe, ins).is_some_and(|t| self.n.setjmp_thunks.contains(&t))
                    || (ins.op0_kind() == OpKind::Memory && mem_target(self.n.pe, ins).is_some_and(|s| is_setjmp(self.n.pe, s))))
        })
    }

    /// Registres généraux peut-être modifiés et pas encore rangés dans ctx à l'entrée de chaque instruction
    /// (analyse de flot vers l'avant sur le code de la fonction, par instruction). Aux sauts vers l'avant et
    /// aux entrées de bloc par la suite du code, les registres modifiés sont rangés (rien ne passe) ; aux
    /// sauts arrière (tour de boucle), ils restent dans les registres de l'ARM (variables de Cranelift) et
    /// sont rangés plus tard (appel, retour, sortie de la boucle) : une boucle ne range plus ses registres
    /// à chaque tour. Prudente : un registre écrit par une instruction traduite par une fonction du runtime
    /// (qui range et relit tout) compte encore comme modifié — un rangement de trop, jamais un de moins.
    fn entry_dirty(&self) -> HashMap<u32, u16> {
        let f = self.f;
        let pe = self.n.pe;
        let mut factory = iced_x86::InstructionInfoFactory::new();
        let keys: Vec<u32> = f.insts.keys().copied().collect();
        let index: HashMap<u32, usize> = keys.iter().enumerate().map(|(i, &r)| (r, i)).collect();
        let mut writes = vec![0u16; keys.len()];
        let mut call = vec![false; keys.len()];
        // Successeurs : (instruction, l'état sale passe-t-il ?).
        let mut succ: Vec<Vec<(usize, bool)>> = vec![Vec::new(); keys.len()];
        // Débuts de bloc atteints par la suite du code (rangement à l'entrée) : cibles de saut et instruction
        // qui suit un saut conditionnel.
        let mut starts: std::collections::HashSet<u32> = f.leaders.iter().copied().collect();
        for (&rva, ins) in &f.insts {
            if ins.flow_control() == FlowControl::ConditionalBranch {
                starts.insert(rva + ins.len() as u32);
            }
        }
        for (i, &rva) in keys.iter().enumerate() {
            let ins = &f.insts[&rva];
            for u in factory.info(ins).used_registers() {
                let r = u.register();
                let written = matches!(
                    u.access(),
                    iced_x86::OpAccess::Write | iced_x86::OpAccess::CondWrite | iced_x86::OpAccess::ReadWrite | iced_x86::OpAccess::ReadCondWrite
                );
                if written && r.is_gpr() {
                    writes[i] |= 1 << r.full_register().number();
                }
            }
            call[i] = matches!(ins.flow_control(), FlowControl::Call | FlowControl::IndirectCall);
            let next = rva + ins.len() as u32;
            if falls_through(ins) {
                if let Some(&j) = index.get(&next) {
                    succ[i].push((j, !starts.contains(&next)));
                }
            }
            if ins.flow_control() != FlowControl::Call {
                if let Some(t) = branch_target(pe, ins) {
                    if let Some(&j) = index.get(&t) {
                        succ[i].push((j, t <= rva));
                    }
                }
            }
            for t in f.jump_tables.get(&rva).map(Vec::as_slice).unwrap_or(&[]) {
                if let Some(&j) = index.get(t) {
                    succ[i].push((j, false));
                }
            }
        }
        // Entrée : registres relus de ctx, aucun modifié. Après un appel : rangés, puis relus.
        let mut din = vec![0u16; keys.len()];
        let mut changed = true;
        let mut rounds = 0;
        while changed && rounds < 64 {
            changed = false;
            rounds += 1;
            for i in 0..keys.len() {
                let out = if call[i] { 0 } else { din[i] | writes[i] };
                for &(j, passes) in &succ[i] {
                    let out = if passes { out } else { 0 };
                    if din[j] | out != din[j] {
                        din[j] |= out;
                        changed = true;
                    }
                }
            }
        }
        if changed {
            // Pas de point fixe (fonction énorme) : tout registre écrit est supposé modifié partout.
            let all = writes.iter().fold(0, |a, &w| a | w);
            din.iter_mut().for_each(|d| *d = all);
        }
        keys.into_iter().zip(din).collect()
    }
    /// Saut arrière (tour de boucle) : les registres généraux restent dans l'ARM (voir entry_dirty) ; les
    /// xmm, dont le cache ne traverse pas les blocs, sont rangés ; accès mémoire précédents inconnus là-bas.
    fn edge(&mut self) {
        self.mem_last = 3;
        self.flush_xmm(u64::MAX);
    }
    /// Saut conditionnel vers la cible de `ins` si `c`, sinon suite à `next`. Saut arrière : registres gardés
    /// pour le tour de boucle, rangés à la sortie (bloc intermédiaire) ; sinon rangés avant le saut.
    fn cond_branch(&mut self, ins: &Instruction, rva: u32, next: u32, c: Value) {
        if self.back_edge(ins, rva) {
            self.edge();
            let target = self.jump_block(ins, rva);
            let fall = self.block_of(next);
            let exit = self.b.create_block();
            self.b.ins().brif(c, target, &[], exit, &[]);
            self.switch_block(exit);
            self.flush_regs();
            self.b.ins().jump(fall, &[]);
        } else {
            self.flush_regs();
            let target = self.jump_block(ins, rva);
            let fall = self.block_of(next);
            self.b.ins().brif(c, target, &[], fall, &[]);
        }
        self.terminated = true;
    }
    /// Le saut de `ins` (à `rva`) va-t-il en arrière, dans la fonction (tour de boucle) ?
    fn back_edge(&self, ins: &Instruction, rva: u32) -> bool {
        branch_target(self.n.pe, ins).is_some_and(|t| t <= rva && !self.f.is_external(t) && self.f.insts.contains_key(&t))
    }

    fn body(&mut self) {
        self.start();
        if self.calls_setjmp() {
            // Jeton : adresse d'une case de la pile de l'hôte, propre à cet appel de la fonction.
            let slot = self.b.create_sized_stack_slot(StackSlotData::new(StackSlotKind::ExplicitSlot, 8, 3));
            self.frame_token = Some(self.b.ins().stack_addr(I64, slot, 0));
        }
        let f = self.f;
        self.frame_pointer = has_frame_pointer(f);
        let entry_flags = self.n.entry_flags;
        let live = flag_liveness(self.n.pe, f, &|t| entry_flags.get(&t).copied());
        let keys: Vec<u32> = f.insts.keys().copied().collect();
        let entry_dirty = self.entry_dirty();
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
                    self.flush_regs();
                    self.b.ins().jump(b, &[]);
                }
                // Registres que l'un des chemins qui y mènent n'a peut-être pas rangés.
                self.dirty = entry_dirty.get(&rva).copied().unwrap_or(0xFFFF);
                if self.xdirty != 0 {
                    eprintln!("rcn-recomp : sub_{:08X}+{:x} : xmm non rangés en entrée de bloc ({:x})", f.entry, rva - f.entry, self.xdirty);
                    self.xdirty = 0;
                }
                self.switch_block(b);
                self.filled.insert(b);
                self.terminated = false;
            } else if self.terminated {
                // Code atteint seulement par un chemin inconnu : bloc propre, registres relus.
                let b = self.b.create_block();
                self.switch_block(b);
                self.reload();
                self.terminated = false;
            }
            self.n.total += 1;
            let need = live.get(&rva).copied().unwrap_or(0);
            if ins.rflags_modified() & crate::func::STATUS_FLAGS != 0 {
                self.last_cmp = None;
            }
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
                    self.continue_at(next);
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
            // Atteints seulement par des sauts : xmm déjà rangés ; les registres généraux, peut-être pas
            // (voir entry_dirty) : tous rangés par l'appel de la suite.
            self.dirty = 0xFFFF;
            self.xdirty = 0;
            self.switch_block(b);
            self.continue_at(rva);
        }
        // Blocs de reprise (après des appels) : rien de sale.
        self.dirty = 0;
        self.xdirty = 0;
        // Remontée d'un longjmp : reprise à un setjmp de cette fonction, ou retour à l'appelant.
        if let Some(unwind) = self.unwind_block {
            self.switch_block(unwind);
            // Reprise non locale : la première fonction dont le cadre contient le rsp visé y reprend, à l'adresse
            // visée (traduite comme une fonction : adresse prise par « lea »), puis rend la main à son appelant.
            if let Some(entry) = self.entry_rsp {
                let state = self.ctx_load(I64, OFF_UNWIND);
                let is_resume = self.b.ins().icmp_imm_s(IntCC::Equal, state, UNWIND_RESUME);
                let resume = self.b.create_block();
                let other = self.b.create_block();
                self.b.ins().brif(is_resume, resume, &[], other, &[]);
                self.switch_block(resume);
                let target_rsp = self.ctx_load(I64, OFF_RESUME_RSP);
                // Égalité : un morceau (suite d'un catch) a pour rsp d'entrée celui du corps de sa fonction ;
                // le cadre d'un appelant n'a jamais ce rsp.
                let mine = self.b.ins().icmp(IntCC::UnsignedLessThanOrEqual, target_rsp, entry);
                let here = self.b.create_block();
                let up = self.b.create_block();
                self.b.ins().brif(mine, here, &[], up, &[]);
                self.switch_block(here);
                let zero = self.b.ins().iconst(I64, 0);
                self.ctx_store(zero, OFF_UNWIND);
                self.ctx_store(target_rsp, OFF_R + 8 * 4);
                let rip = self.ctx_load(I64, OFF_RESUME_RIP);
                let r = self.helper("rcn_call", &[I64, I64], None);
                let ctx = self.ctx_param;
                self.b.ins().call(r, &[ctx, rip]);
                self.b.ins().return_(&[]);
                self.switch_block(up);
                self.b.ins().return_(&[]);
                self.switch_block(other);
            }
            let sites = self.setjmp_sites.clone();
            if let Some(token) = self.frame_token.filter(|_| !sites.is_empty()) {
                let target = self.ctx_load(I64, OFF_UNWIND);
                let mine = self.b.ins().icmp(IntCC::Equal, target, token);
                let resume = self.b.create_block();
                let leave = self.b.create_block();
                self.b.ins().brif(mine, resume, &[], leave, &[]);
                self.switch_block(resume);
                let zero = self.b.ins().iconst(I64, 0);
                self.ctx_store(zero, OFF_UNWIND);
                let site = self.ctx_load(I32, OFF_UNWIND_SITE);
                let mut sw = Switch::new();
                for (i, &b) in sites.iter().enumerate() {
                    sw.set_entry(i as u128, b);
                }
                sw.emit(&mut self.b, site, leave);
                self.switch_block(leave);
            }
            self.b.ins().return_(&[]);
        }
    }
}

/// La fonction établit un pointeur de cadre (« push ebp ; mov ebp, esp », après un éventuel « mov edi, edi »
/// de correctif à chaud) et ne modifie ebp ailleurs que par pop ou leave : ebp désigne alors son cadre.
fn has_frame_pointer(f: &Func) -> bool {
    let mut it = f.insts.range(f.entry..);
    let mut next = || it.next().map(|(_, ins)| *ins);
    let mut first = next();
    if let Some(ins) = first {
        if ins.mnemonic() == M::Mov && ins.op0_register() == Register::EDI && ins.op1_register() == Register::EDI {
            first = next();
        }
    }
    let (Some(push), Some(mov)) = (first, next()) else { return false };
    let (bp, sp) = if push.op0_register() == Register::EBP { (Register::EBP, Register::ESP) } else { (Register::RBP, Register::RSP) };
    if push.mnemonic() != M::Push || push.op0_register() != bp {
        return false;
    }
    if mov.mnemonic() != M::Mov || mov.op0_register() != bp || mov.op1_register() != sp {
        return false;
    }
    // Aucune autre écriture de ebp (registre de destination, hors pop et leave).
    f.insts.values().all(|ins| {
        let writes_bp = ins.op_count() > 0 && ins.op0_kind() == OpKind::Register && ins.op0_register() == bp;
        !writes_bp
            || matches!(ins.mnemonic(), M::Pop | M::Push | M::Cmp | M::Test)
            || (ins.mnemonic() == M::Mov && ins.op1_register() == sp)
    })
}

/// Codes SSE (encodage classique) par mnémonique en minuscules, avec leurs opérandes (fin du nom du code :
/// « xmm », « xmmm128 », « imm8 »…).
fn legacy_codes() -> &'static HashMap<String, Vec<(Code, Vec<String>)>> {
    static MAP: std::sync::OnceLock<HashMap<String, Vec<(Code, Vec<String>)>>> = std::sync::OnceLock::new();
    MAP.get_or_init(|| {
        let mut map: HashMap<String, Vec<(Code, Vec<String>)>> = HashMap::new();
        for code in Code::values() {
            if code.encoding() != iced_x86::EncodingKind::Legacy {
                continue;
            }
            let name = format!("{code:?}");
            let mut parts = name.split('_');
            let Some(mnemonic) = parts.next() else { continue };
            map.entry(mnemonic.to_lowercase()).or_default().push((code, parts.map(str::to_string).collect()));
        }
        map
    })
}

/// Instruction SSE équivalente à une instruction VEX (code de 128 bits, même si la VEX est de 256 bits), et
/// position de l'opérande vvvv de la VEX (première source, absente de la forme SSE).
fn vex_to_legacy(code: Code) -> Option<(Code, Option<u32>)> {
    use iced_x86::OpCodeOperandKind as K;
    let name = format!("{code:?}");
    let rest = name.strip_prefix("VEX_V")?;
    let mut parts = rest.split('_');
    let mnemonic = parts.next()?.to_lowercase();
    let info = code.op_code();
    let mut vvvv = None;
    let mut want = Vec::new();
    for (i, p) in parts.enumerate() {
        if matches!(info.op_kind(i as u32), K::xmm_vvvv | K::ymm_vvvv) {
            vvvv = Some(i as u32);
            continue;
        }
        // Moitié de 128 bits d'un opérande de 256 bits.
        let p = match p {
            "ymm" => "xmm".to_string(),
            "ymmm256" => "xmmm128".to_string(),
            "m256" => "m128".to_string(),
            other => other.to_string(),
        };
        want.push(p);
    }
    // Opérande compatible : identique ; registre xmm ou mémoire « mN » face à « xmmmN » ; mémoire de même
    // taille.
    let fits = |vex: &str, sse: &str| -> bool {
        vex == sse
            || (vex == "xmm" && sse.starts_with("xmmm"))
            || (vex.starts_with('m') && sse == format!("xmm{vex}"))
            || (vex.starts_with('m') && sse.starts_with("xmmm") && vex.len() > 1 && sse.ends_with(&vex[1..]))
    };
    let candidates = legacy_codes().get(&mnemonic)?;
    candidates
        .iter()
        .find(|(_, ops)| ops.len() == want.len() && ops.iter().zip(&want).all(|(s, v)| fits(v, s)))
        .map(|&(c, _)| (c, vvvv))
}
