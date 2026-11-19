//! rcn-recomp : recompilateur statique d'exécutables Windows x86 / x86-64.
//!
//! Deux générateurs, à partir de la même analyse (pe.rs, func.rs) :
//!  - C portable (emit.rs), compilé ensuite avec le runtime : outil de mise au point sur PC ;
//!  - code machine de l'hôte, directement (native.rs, Cranelift) : fichier .rcnx chargé par le runtime.
//!    C'est celui qu'utilise l'application Android, qui traduit les jeux elle-même.

pub mod eh;
pub mod emit;
pub mod func;
pub mod native;
pub mod pe;

use std::collections::{BTreeMap, BTreeSet, HashMap};
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
    /// Exports dont le code n'est pas dans le fichier (section vidée par un protecteur, remplie au
    /// chargement) : ils restent absents de la traduction.
    pub unavailable_exports: usize,
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
        if self.unavailable_exports > 0 {
            let _ = write!(
                s,
                "\n  {} exports sans code dans le fichier (section remplie à l'exécution) : non traduits",
                self.unavailable_exports
            );
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

/// Relais de l'édition de liens incrémentale (MSVC /INCREMENTAL) : au début du code, une suite de
/// `jmp rel32` vers les fonctions. Les tables (exceptions de __CxxFrameHandler3, rappels) ne désignent
/// parfois une fonction que par son relais, en RVA : chaque relais devient un point d'entrée.
fn incremental_stubs(pe: &pe::Pe) -> Vec<u32> {
    let mut out = Vec::new();
    for s in pe.sections.iter().filter(|s| s.characteristics & pe::SCN_MEM_EXECUTE != 0 && s.has_data) {
        let (mut at, end) = (s.rva, s.rva + s.size);
        // La table peut suivre quelques octets de remplissage (int3).
        while at < end && at < s.rva + 16 && pe.image.get(at as usize) == Some(&0xCC) {
            at += 1;
        }
        let mut stubs = Vec::new();
        while at + 5 <= end && pe.image.get(at as usize) == Some(&0xE9) {
            let rel = i32::from_le_bytes(pe.image[at as usize + 1..at as usize + 5].try_into().unwrap());
            let target = (at as i64 + 5 + rel as i64) as u32;
            if !pe.is_exec(target) {
                break;
            }
            stubs.push(at);
            at += 5;
        }
        // Une vraie table compte de nombreux relais ; quelques sauts isolés n'en sont pas une.
        if stubs.len() >= 16 {
            out.extend(stubs);
        }
    }
    out
}

/// Tables de relais au milieu du code : au moins 4 « jmp rel32 » qui se suivent, juste après un ret ou un
/// int3 ou un saut court, tous vers du code. On y entre par une adresse calculée (base + rang × 5 : Galaxy64.dll de GOG),
/// que ni les relocalisations ni les appels ne montrent ; chaque relais devient un point d'entrée.
fn jump_stub_runs(pe: &pe::Pe) -> Vec<u32> {
    let mut out = Vec::new();
    for s in pe.sections.iter().filter(|s| s.characteristics & pe::SCN_MEM_EXECUTE != 0 && s.has_data) {
        let (start, end) = (s.rva as usize, (s.rva + s.size) as usize);
        let image = &pe.image;
        let mut at = start + 1;
        while at + 5 <= end && at + 5 <= image.len() {
            // Début de table : après un ret, un int3 ou un saut court (EB xx), fin de l'instruction d'avant.
            let after_jump = at >= start + 2 && image[at - 2] == 0xEB;
            if image[at] != 0xE9 || !(matches!(image[at - 1], 0xC3 | 0xCC) || after_jump) {
                at += 1;
                continue;
            }
            let mut stubs = Vec::new();
            let mut p = at;
            while p + 5 <= end && p + 5 <= image.len() && image[p] == 0xE9 {
                let rel = i32::from_le_bytes(image[p + 1..p + 5].try_into().unwrap());
                let target = (p as i64 + 5 + rel as i64) as u32;
                if !pe.is_exec(target) {
                    break;
                }
                stubs.push(p as u32);
                p += 5;
            }
            if stubs.len() >= 4 {
                out.extend(stubs);
                at = p;
            } else {
                at += 1;
            }
        }
    }
    out
}

/// Mise au point : étapes de l'analyse sur la sortie d'erreur (RCN_RECOMP_PROGRESS=1).
fn progress(msg: &str) {
    static ENABLED: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    if *ENABLED.get_or_init(|| std::env::var_os("RCN_RECOMP_PROGRESS").is_some()) {
        eprintln!("[analyse] {msg}");
    }
}

pub fn analyse(file: &[u8]) -> Result<Program, String> {
    let started = std::time::Instant::now();
    let pe = pe::Pe::parse(file)?;
    progress(&format!(
        "PE lu : {} bits, {} sections, {} exports, {} entrées .pdata, {} imports, image {} octets ({:.2} s)",
        pe.bits,
        pe.sections.len(),
        pe.exports.len(),
        pe.pdata.len(),
        pe.imports.len(),
        pe.size_of_image,
        started.elapsed().as_secs_f64()
    ));
    // Points d'entrée connus avant tout décodage : entrée du programme, .pdata, exports, et pointeurs
    // vers du code trouvés dans les données (tables de méthodes virtuelles, fonctions de rappel).
    let mut known: BTreeSet<u32> = BTreeSet::new();
    if pe.is_exec(pe.entry_rva) {
        known.insert(pe.entry_rva);
    }
    known.extend(pe.pdata.iter().map(|r| r.0));
    known.extend(pe.exports.iter().copied());
    // Points de reprise des exceptions (landing pads, blocs __except) : entrées de morceaux de fonctions.
    if pe.bits == 64 {
        known.extend(eh::resume_targets(&pe));
    }
    known.extend(incremental_stubs(&pe));
    known.extend(jump_stub_runs(&pe));
    for s in pe.sections.iter().filter(|s| s.characteristics & pe::SCN_MEM_EXECUTE == 0 && s.has_data) {
        let mut rva = s.rva;
        let step = pe.bits / 8;
        while rva + step <= s.rva + s.size {
            if let Some(target) = pe.ptr(rva).and_then(|v| pe.code_rva(v)) {
                known.insert(target);
            }
            rva += step;
        }
    }
    // Adresses relocalisées rangées dans le code lui-même : tables de méthodes virtuelles des DLL sans
    // section de données en lecture seule (rangées dans .text, comme XAudio2_7.dll).
    for &at in &pe.relocs {
        if pe.bits == 64 && pe.is_exec(at) {
            let value = if pe.bits == 64 { pe.u64(at) } else { pe.u32(at).map(u64::from) };
            if let Some(target) = value.and_then(|v| pe.code_rva(v)) {
                known.insert(target);
            }
        }
    }
    let mut funcs: BTreeMap<u32, func::Func> = BTreeMap::new();
    let mut work: Vec<u32> = known.iter().rev().copied().collect();
    progress(&format!(
        "points d'entrée : {} ({:.2} s)",
        work.len(),
        started.elapsed().as_secs_f64()
    ));
    let mut slow = 0usize;
    loop {
    while let Some(entry) = work.pop() {
        if funcs.contains_key(&entry) {
            continue;
        }
        let t = std::time::Instant::now();
        let f = func::decode_function(&pe, entry, &known);
        // Fonction pathologique : au moins une seconde pour la décoder.
        if t.elapsed().as_secs() >= 1 {
            slow += 1;
            progress(&format!(
                "fonction 0x{entry:x} : {:.1} s, {} instructions, étendue {:x?}",
                t.elapsed().as_secs_f64(),
                f.insts.len(),
                f.range
            ));
        }
        for &t in &f.refs {
            if known.insert(t) || !funcs.contains_key(&t) {
                work.push(t);
            }
        }
        funcs.insert(entry, f);
        if funcs.len() % 2000 == 0 {
            progress(&format!(
                "{} fonctions, {} en attente, {:.1} s",
                funcs.len(),
                work.len(),
                started.elapsed().as_secs_f64()
            ));
        }
    }
    // 32 bits : adresses relocalisées rangées dans .text (données fusionnées dans le code : tables de
    // _initterm, tables de méthodes virtuelles). Le site lui-même peut sembler couvert par des instructions
    // décodées dans des données ; la cible devient une fonction si elle a l'allure d'un début de fonction et
    // n'est pas déjà une instruction décodée (les cibles des tables de sauts le sont).
    // Répété tant que de nouvelles fonctions apparaissent.
    if pe.bits != 32 {
        break;
    }
    let mut covered: Vec<(u32, u32)> = funcs
        .values()
        .flat_map(|f| f.insts.iter().map(|(&rva, ins)| (rva, rva + ins.len() as u32)))
        .collect();
    covered.sort_unstable();
    let inside = |at: u32| {
        let i = covered.partition_point(|&(start, _)| start <= at);
        i > 0 && at < covered[i - 1].1
    };
    for &at in &pe.relocs {
        if !pe.is_exec(at) {
            continue;
        }
        if let Some(target) = pe.u32(at).and_then(|v| pe.code_rva(u64::from(v))) {
            // Début de fonction plausible : juste après du bourrage (int3, nop) ou un ret. Écarte les
            // opérandes des instructions pas encore décodées (cases de l'IAT rangée dans .text, données).
            let byte = |rva: u32| pe.image.get(rva as usize).copied();
            let after_gap = matches!(byte(target.wrapping_sub(1)), Some(0xCC | 0x90 | 0xC3))
                || byte(target.wrapping_sub(3)) == Some(0xC2);
            let first = byte(target);
            if after_gap
                && !matches!(first, None | Some(0x00 | 0xCC))
                && !inside(target)
                && !funcs.contains_key(&target)
                && known.insert(target)
            {
                work.push(target);
            }
        }
    }
    if work.is_empty() {
        break;
    }
    progress(&format!("32 bits : {} fonctions trouvées par les adresses rangées dans le code", work.len()));
    }
    progress(&format!(
        "décodage terminé : {} fonctions, {} lentes, {:.1} s",
        funcs.len(),
        slow,
        started.elapsed().as_secs_f64()
    ));
    if pe.bits == 64 {
        fill_gaps(&pe, &mut funcs, &mut known);
        progress(&format!(
            "trous comblés : {} fonctions ({:.1} s)",
            funcs.len(),
            started.elapsed().as_secs_f64()
        ));
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

/// Code jamais atteint par l'analyse mais qui peut l'être à l'exécution : petits relais visés seulement
/// par des pointeurs relatifs (relais de « déballage » .NET : « add rcx, 8 ; jmp méthode »), fonctions
/// appelées par une table que l'analyse ne lit pas. Chaque trou des sections de code est décodé, après
/// le bourrage (int3, nop), tant qu'il donne des fonctions complètes qui y tiennent.
fn fill_gaps(pe: &pe::Pe, funcs: &mut BTreeMap<u32, func::Func>, known: &mut BTreeSet<u32>) {
    use iced_x86::{FlowControl, Mnemonic};
    let mut covered: Vec<(u32, u32)> = funcs
        .values()
        .flat_map(|f| f.insts.iter().map(|(&rva, ins)| (rva, rva + ins.len() as u32)))
        .collect();
    covered.sort_unstable();
    let mut merged: Vec<(u32, u32)> = Vec::new();
    for (b, e) in covered {
        match merged.last_mut() {
            Some(last) if b <= last.1 => last.1 = last.1.max(e),
            _ => merged.push((b, e)),
        }
    }
    let mut gaps = Vec::new();
    for s in pe.sections.iter().filter(|s| s.characteristics & pe::SCN_MEM_EXECUTE != 0 && s.has_data) {
        let mut pos = s.rva;
        let end = s.rva + s.size;
        let first = merged.partition_point(|&(_, e)| e <= s.rva);
        for &(b, e) in &merged[first..] {
            if b >= end {
                break;
            }
            if b > pos {
                gaps.push((pos, b));
            }
            pos = pos.max(e);
        }
        if pos < end {
            gaps.push((pos, end));
        }
    }
    let mut work = Vec::new();
    let gap_bytes: u64 = gaps.iter().map(|&(b, e)| u64::from(e - b)).sum();
    progress(&format!("trous : {} plages, {} octets", gaps.len(), gap_bytes));
    for (start, end) in gaps {
        let mut p = start;
        while p < end {
            // Bourrage entre fonctions.
            match func::decode_at(pe, p) {
                Some(ins) if matches!(ins.mnemonic(), Mnemonic::Int3 | Mnemonic::Nop) => {
                    p += ins.len() as u32;
                    continue;
                }
                Some(_) => {}
                None => break,
            }
            if pe.image.get(p as usize).is_some_and(|&b| b == 0) {
                break;
            }
            let f = func::decode_function(pe, p, known);
            let last = f.insts.iter().next_back().map(|(&rva, ins)| (rva + ins.len() as u32, ins.flow_control()));
            let fits = f.insts.iter().all(|(&rva, ins)| rva >= p && rva + ins.len() as u32 <= end);
            let complete = f.insts.values().all(|ins| !ins.is_invalid())
                && last.is_some_and(|(_, flow)| {
                    matches!(flow, FlowControl::Return | FlowControl::UnconditionalBranch | FlowControl::IndirectBranch | FlowControl::Interrupt)
                });
            if !fits || !complete {
                break;
            }
            let next = f.insts.iter().map(|(&rva, ins)| rva + ins.len() as u32).max().unwrap_or(end);
            known.insert(p);
            work.extend(f.refs.iter().copied());
            funcs.insert(p, f);
            p = next;
        }
    }
    // Ce que visent les fonctions trouvées ainsi.
    while let Some(entry) = work.pop() {
        if funcs.contains_key(&entry) {
            continue;
        }
        known.insert(entry);
        let f = func::decode_function(pe, entry, known);
        work.extend(f.refs.iter().copied().filter(|t| !funcs.contains_key(t)));
        funcs.insert(entry, f);
    }
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
        unavailable_exports: pe.unavailable_exports,
    };
    (out, report)
}

/// Traduction en code machine : fichier .rcnx écrit dans `out`. `target` : None pour la machine qui
/// traduit, sinon « aarch64 » ou « x86_64 ». `progress(fait, total)` est appelé au fil des fonctions.
/// Le programme est consommé : chaque fonction décodée est libérée dès qu'elle est traduite (la mémoire
/// des gros exécutables reste ainsi sous la somme « décodé + traduit »).
pub fn to_native(
    program: Program,
    exe_name: &str,
    target: Option<&str>,
    out: &mut dyn std::io::Write,
    mut progress: impl FnMut(usize, usize),
) -> Result<Report, String> {
    let (isa, arch) = native::make_isa(target)?;
    let Program { pe, funcs, setjmp_thunks } = program;
    let names: BTreeSet<u32> = funcs.keys().copied().collect();
    let entry_flags: HashMap<u32, u32> = funcs.iter().map(|(&rva, f)| (rva, func::entry_flags(&pe, f))).collect();
    let mut gen = native::Native::new(&pe, &names, &entry_flags, &setjmp_thunks, isa, arch);
    let mut failed = Vec::new();
    let total = funcs.len();
    for (i, (_, f)) in funcs.into_iter().enumerate() {
        // Une erreur, ou une panique de Cranelift, ne fait perdre que la fonction concernée.
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| gen.translate(&f)))
            .unwrap_or_else(|_| Err(format!("sub_{:08X} : erreur interne du générateur", f.entry)));
        if let Err(e) = result {
            failed.push(e.clone());
            gen.translate_failed(&f, &e)?;
        }
        drop(f);
        progress(i + 1, total);
    }
    gen.write_rcnx(exe_name, out).map_err(|e| format!("écriture : {e}"))?;
    Ok(Report {
        bits: pe.bits,
        functions: total,
        instructions: gen.total,
        imports: pe.imports.len(),
        unimplemented: gen.unimplemented.clone(),
        failed,
        unavailable_exports: pe.unavailable_exports,
    })
}

/// Traduit un exécutable en fichier .rcnx (code de la machine qui traduit). Pour l'application Android.
pub fn translate_file(input: &str, output: &str, progress: impl FnMut(usize, usize)) -> Result<Report, String> {
    let file = std::fs::read(input).map_err(|e| format!("{input} : {e}"))?;
    let program = analyse(&file)?;
    drop(file);
    let exe_name = std::path::Path::new(input).file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
    let mut out = std::io::BufWriter::new(std::fs::File::create(output).map_err(|e| format!("{output} : {e}"))?);
    let report = to_native(program, &exe_name, None, &mut out, progress)?;
    std::io::Write::flush(&mut out).map_err(|e| format!("{output} : {e}"))?;
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
