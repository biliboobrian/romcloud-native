//! rcn-recomp : recompilateur statique d'un exécutable Windows x86-64 vers du C portable.
//!
//!   rcn-recomp jeu.exe -o jeu_recomp.c
//!
//! Le C produit ne contient que le code traduit : compilé pour ARM64 avec le runtime (rcn.h), il
//! s'exécute sans émulation du processeur. Les données restent dans l'exécutable d'origine, chargé
//! par le runtime au lancement.

mod emit;
mod func;
mod pe;

use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write;
use std::process::ExitCode;

fn main() -> ExitCode {
    let mut input = None;
    let mut output = None;
    let mut comments = true;
    let mut imports_list = false;
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "-o" => output = args.next(),
            "--no-comments" => comments = false,
            "--imports" => imports_list = true,
            "--version" => {
                println!("{}", env!("CARGO_PKG_VERSION"));
                return ExitCode::SUCCESS;
            }
            "-h" | "--help" => {
                println!("usage : rcn-recomp <jeu.exe> -o <sortie.c> [--no-comments]");
                return ExitCode::SUCCESS;
            }
            _ => input = Some(arg),
        }
    }
    let (Some(input), Some(output)) = (input, output) else {
        eprintln!("usage : rcn-recomp <jeu.exe> -o <sortie.c> [--no-comments]");
        return ExitCode::from(2);
    };
    match run(&input, &output, comments, imports_list) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("rcn-recomp : {e}");
            ExitCode::FAILURE
        }
    }
}

fn run(input: &str, output: &str, comments: bool, imports_list: bool) -> Result<(), String> {
    let file = std::fs::read(input).map_err(|e| format!("{input} : {e}"))?;
    let pe = pe::Pe::parse(&file)?;

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

    let names: BTreeSet<u32> = funcs.keys().copied().collect();
    // Relais « jmp [__imp_setjmp] » : un appel à l'un d'eux est un appel à setjmp.
    let setjmp_thunks: BTreeSet<u32> = funcs
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
    let mut emitter = emit::Emitter::new(&pe, &names, &setjmp_thunks, comments);
    let exe_name = std::path::Path::new(input).file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
    let mut out = String::new();
    writeln!(out, "// Code traduit de {exe_name} par rcn-recomp {} : ne pas modifier.", env!("CARGO_PKG_VERSION")).unwrap();
    out.push_str("#include \"rcn.h\"\n\n");
    for &rva in funcs.keys() {
        writeln!(out, "void sub_{rva:08X}(RcnCtx *ctx);").unwrap();
    }
    out.push('\n');
    for f in funcs.values() {
        emitter.emit_func(f, &mut out);
    }
    out.push_str("static const RcnFuncEntry rcn_funcs[] = {\n");
    for &rva in funcs.keys() {
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
        funcs.len()
    )
    .unwrap();
    std::fs::write(output, out).map_err(|e| format!("{output} : {e}"))?;

    let missing: usize = emitter.unimplemented.values().sum();
    eprintln!(
        "{exe_name} ({} bits) : {} fonctions, {} instructions, {} imports, {} non gérées ({:.2} %)",
        pe.bits,
        funcs.len(),
        emitter.total,
        pe.imports.len(),
        missing,
        if emitter.total > 0 { missing as f64 * 100.0 / emitter.total as f64 } else { 0.0 }
    );
    // Fonctions Windows importées : celles que l'hôte doit fournir.
    if imports_list {
        let mut names: Vec<&String> = pe.imports.values().collect();
        names.sort();
        for name in names {
            println!("{name}");
        }
    }
    let mut list: Vec<_> = emitter.unimplemented.iter().collect();
    list.sort_by(|a, b| b.1.cmp(a.1));
    for (name, count) in list {
        eprintln!("  {name} : {count}");
    }
    Ok(())
}
