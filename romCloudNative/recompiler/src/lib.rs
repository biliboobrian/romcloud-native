//! rcn-recomp : recompilateur statique d'exécutables Windows x86 / x86-64.
//!
//! Deux générateurs, à partir de la même analyse (pe.rs, func.rs) :
//!  - C portable (emit.rs), compilé ensuite avec le runtime : outil de mise au point sur PC ;
//!  - code machine de l'hôte, directement (native.rs, Cranelift) : fichier .rcnx chargé par le runtime.
//!    C'est celui qu'utilise l'application Android, qui traduit les jeux elle-même.

pub mod emit;
pub mod func;
pub mod native;
pub mod pe;

use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write;

pub const VERSION: &str = env!("CARGO_PKG_VERSION");

/// Bilan d'une traduction.
pub struct Report {
    pub bits: u32,
    pub functions: usize,
    pub instructions: usize,
    pub imports: usize,
    /// Instructions non gérées : mnémonique → nombre.
    pub unimplemented: BTreeMap<String, usize>,
    /// Fonctions que le générateur n'a pas pu traduire (remplacées par un arrêt).
    pub failed: Vec<String>,
}

impl Report {
    pub fn summary(&self, exe_name: &str) -> String {
        let missing: usize = self.unimplemented.values().sum();
        let mut s = format!(
            "{exe_name} ({} bits) : {} fonctions, {} instructions, {} imports, {} non gérées ({:.2} %)",
            self.bits,
            self.functions,
            self.instructions,
            self.imports,
            missing,
            if self.instructions > 0 { missing as f64 * 100.0 / self.instructions as f64 } else { 0.0 }
        );
        let mut list: Vec<_> = self.unimplemented.iter().collect();
        list.sort_by(|a, b| b.1.cmp(a.1));
        for (name, count) in list {
            let _ = write!(s, "\n  {name} : {count}");
        }
        for f in &self.failed {
            let _ = write!(s, "\n  non traduite : {f}");
        }
        s
    }
}

/// Programme analysé : fonctions trouvées et relais vers setjmp.
pub struct Program {
    pub pe: pe::Pe,
    pub funcs: BTreeMap<u32, func::Func>,
    pub setjmp_thunks: BTreeSet<u32>,
}

pub fn analyse(file: &[u8]) -> Result<Program, String> {
    let pe = pe::Pe::parse(file)?;
    // Points d'entrée connus avant tout décodage : entrée du programme, .pdata, exports, et pointeurs
    // vers du code trouvés dans les données (tables de méthodes virtuelles, fonctions de rappel).
    let mut known: BTreeSet<u32> = BTreeSet::new();
    if pe.is_exec(pe.entry_rva) {
        known.insert(pe.entry_rva);
    }
    known.extend(pe.pdata.iter().map(|r| r.0));
    known.extend(pe.exports.iter().copied());
    for s in pe.sections.iter().filter(|s| s.characteristics & pe::SCN_MEM_EXECUTE == 0) {
        let mut rva = s.rva;
        let step = pe.bits / 8;
        while rva + step <= s.rva + s.size {
            if let Some(target) = pe.ptr(rva).and_then(|v| pe.code_rva(v)) {
                known.insert(target);
            }
            rva += step;
        }
    }
    let mut funcs: BTreeMap<u32, func::Func> = BTreeMap::new();
    let mut work: Vec<u32> = known.iter().rev().copied().collect();
    while let Some(entry) = work.pop() {
        if funcs.contains_key(&entry) {
            continue;
        }
        let f = func::decode_function(&pe, entry, &known);
        for &t in &f.refs {
            if known.insert(t) || !funcs.contains_key(&t) {
                work.push(t);
            }
        }
        funcs.insert(entry, f);
    }
    // Relais « jmp [__imp_setjmp] » : un appel à l'un d'eux est un appel à setjmp.
    let setjmp_thunks = funcs
        .values()
        .filter(|f| {
            f.insts.get(&f.entry).is_some_and(|ins| {
                ins.mnemonic() == iced_x86::Mnemonic::Jmp
                    && ins.op0_kind() == iced_x86::OpKind::Memory
                    && func::mem_target(&pe, ins).is_some_and(|slot| emit::is_setjmp(&pe, slot))
            })
        })
        .map(|f| f.entry)
        .collect();
    Ok(Program { pe, funcs, setjmp_thunks })
}

/// Traduction en C.
pub fn to_c(program: &Program, exe_name: &str, comments: bool) -> (String, Report) {
    let names: BTreeSet<u32> = program.funcs.keys().copied().collect();
    let pe = &program.pe;
    let mut emitter = emit::Emitter::new(pe, &names, &program.setjmp_thunks, comments);
    let mut out = String::new();
    writeln!(out, "// Code traduit de {exe_name} par rcn-recomp {VERSION} : ne pas modifier.").unwrap();
    out.push_str("#include \"rcn.h\"\n\n");
    for &rva in program.funcs.keys() {
        writeln!(out, "void sub_{rva:08X}(RcnCtx *ctx);").unwrap();
    }
    out.push('\n');
    for f in program.funcs.values() {
        emitter.emit_func(f, &mut out);
    }
    out.push_str("static const RcnFuncEntry rcn_funcs[] = {\n");
    for &rva in program.funcs.keys() {
        writeln!(out, "    {{0x{rva:x}, sub_{rva:08X}}},").unwrap();
    }
    out.push_str("};\n\n");
    writeln!(
        out,
        "RCN_EXPORT const RcnModule rcn_module = {{\n    RCN_ABI_VERSION, \"{}\", {}, 0x{:x}ULL, 0x{:x}, 0x{:x}, 0x{:x}, rcn_funcs, {},\n}};",
        exe_name.replace(['"', '\\'], "_"),
        pe.bits,
        pe.image_base,
        pe.size_of_image,
        pe.timestamp,
        pe.entry_rva,
        program.funcs.len()
    )
    .unwrap();
    let report = Report {
        bits: pe.bits,
        functions: program.funcs.len(),
        instructions: emitter.total,
        imports: pe.imports.len(),
        unimplemented: emitter.unimplemented.clone(),
        failed: Vec::new(),
    };
    (out, report)
}

/// Traduction en code machine : contenu du fichier .rcnx. `target` : None pour la machine qui
/// traduit, sinon « aarch64 » ou « x86_64 ». `progress(fait, total)` est appelé au fil des fonctions.
pub fn to_native(
    program: &Program,
    exe_name: &str,
    target: Option<&str>,
    mut progress: impl FnMut(usize, usize),
) -> Result<(Vec<u8>, Report), String> {
    let (isa, arch) = native::make_isa(target)?;
    let names: BTreeSet<u32> = program.funcs.keys().copied().collect();
    let mut gen = native::Native::new(&program.pe, &names, &program.setjmp_thunks, isa, arch);
    let mut failed = Vec::new();
    let total = program.funcs.len();
    for (i, f) in program.funcs.values().enumerate() {
        // Une erreur, ou une panique de Cranelift, ne fait perdre que la fonction concernée.
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| gen.translate(f)))
            .unwrap_or_else(|_| Err(format!("sub_{:08X} : erreur interne du générateur", f.entry)));
        if let Err(e) = result {
            failed.push(e.clone());
            gen.translate_failed(f, &e)?;
        }
        progress(i + 1, total);
    }
    let data = gen.write_rcnx(exe_name);
    let report = Report {
        bits: program.pe.bits,
        functions: total,
        instructions: gen.total,
        imports: program.pe.imports.len(),
        unimplemented: gen.unimplemented.clone(),
        failed,
    };
    Ok((data, report))
}

/// Traduit un exécutable en fichier .rcnx (code de la machine qui traduit). Pour l'application Android.
pub fn translate_file(input: &str, output: &str, progress: impl FnMut(usize, usize)) -> Result<Report, String> {
    let file = std::fs::read(input).map_err(|e| format!("{input} : {e}"))?;
    let program = analyse(&file)?;
    let exe_name = std::path::Path::new(input).file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
    let (data, report) = to_native(&program, &exe_name, None, progress)?;
    std::fs::write(output, data).map_err(|e| format!("{output} : {e}"))?;
    Ok(report)
}

/// Version du recompilateur, pour l'application : un jeu traduit par une autre version est retraduit.
#[no_mangle]
pub extern "C" fn rcn_recompiler_version() -> *const std::ffi::c_char {
    concat!(env!("CARGO_PKG_VERSION"), "\0").as_ptr() as *const std::ffi::c_char
}

/// Entrée pour le code natif de l'application : traduit `input` (exe) en `output` (.rcnx).
/// `progress` reçoit (fait, total) ; `message` (512 octets au moins) reçoit le bilan ou l'erreur.
/// Renvoie 0 en cas de succès.
///
/// # Safety
/// Chaînes terminées par un zéro, `message` d'au moins `message_size` octets.
#[no_mangle]
pub unsafe extern "C" fn rcn_recompile(
    input: *const std::ffi::c_char,
    output: *const std::ffi::c_char,
    progress: Option<extern "C" fn(*mut std::ffi::c_void, u32, u32)>,
    user: *mut std::ffi::c_void,
    message: *mut std::ffi::c_char,
    message_size: usize,
) -> i32 {
    let text = |p: *const std::ffi::c_char| std::ffi::CStr::from_ptr(p).to_string_lossy().into_owned();
    let (input, output) = (text(input), text(output));
    let result = std::panic::catch_unwind(|| {
        translate_file(&input, &output, |done, total| {
            if let Some(cb) = progress {
                cb(user, done as u32, total as u32);
            }
        })
    });
    let exe_name = std::path::Path::new(&input).file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
    let (status, msg) = match result {
        Ok(Ok(report)) => (0, report.summary(&exe_name)),
        Ok(Err(e)) => (1, e),
        Err(_) => (2, "erreur interne du recompilateur".to_string()),
    };
    if !message.is_null() && message_size > 0 {
        let bytes = msg.as_bytes();
        let n = bytes.len().min(message_size - 1);
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), message as *mut u8, n);
        *message.add(n) = 0;
    }
    status
}
