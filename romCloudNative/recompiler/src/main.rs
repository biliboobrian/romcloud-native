//! rcn-recomp : recompilateur statique d'un exécutable Windows x86 / x86-64.
//!
//!   rcn-recomp jeu.exe -o jeu_recomp.c                     C portable, à compiler avec le runtime
//!   rcn-recomp jeu.exe -o jeu.rcnx [--target aarch64]      code machine, chargé par le runtime
//!
//! Le format de sortie suit l'extension. Les données restent dans l'exécutable d'origine, chargé par le
//! runtime au lancement.

use rcn_recomp::{analyse, to_c, to_native, VERSION};
use std::process::ExitCode;

const USAGE: &str = "usage : rcn-recomp <jeu.exe> -o <sortie.c | sortie.rcnx> [--target aarch64|x86_64] [--no-comments] [--imports] [--tso] [--nan-probe]";

fn main() -> ExitCode {
    let mut input = None;
    let mut output = None;
    let mut target = None;
    let mut comments = true;
    let mut imports_list = false;
    let mut dump = None;
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "-o" => output = args.next(),
            "--target" => target = args.next(),
            "--no-comments" => comments = false,
            "--imports" => imports_list = true,
            // Ordre des accès mémoire de x86 garanti sur ARM (code sans verrou d'Unreal Engine 5).
            "--tso" => std::env::set_var("RCN_RECOMP_TSO", "1"),
            // Mise au point : signale au runtime chaque instruction flottante qui fait naître un NaN.
            "--nan-probe" => std::env::set_var("RCN_RECOMP_NAN", "1"),
            // Mise au point : entrée de ces fonctions (RVA en hexadécimal, séparés par des virgules) signalée.
            "--probe-fn" => {
                if let Some(list) = args.next() {
                    std::env::set_var("RCN_RECOMP_PROBE", list)
                }
            }
            // Mise au point : fonction qui contient ce RVA (étendue, tables de sauts).
            "--dump" => dump = args.next().and_then(|v| u32::from_str_radix(v.trim_start_matches("0x"), 16).ok()),
            "--version" => {
                println!("{VERSION}");
                return ExitCode::SUCCESS;
            }
            "-h" | "--help" => {
                println!("{USAGE}");
                return ExitCode::SUCCESS;
            }
            _ => input = Some(arg),
        }
    }
    if let (Some(input), Some(rva)) = (&input, dump) {
        let file = std::fs::read(input).unwrap_or_default();
        match analyse(&file) {
            Ok(program) => {
                for f in program.funcs.values().filter(|f| f.insts.contains_key(&rva) || f.entry == rva) {
                    println!("fonction 0x{:x}, étendue {:x?}, {} instructions", f.entry, f.range, f.insts.len());
                    for (jmp, targets) in &f.jump_tables {
                        println!("  table au saut 0x{jmp:x} : {targets:x?}");
                    }
                }
            }
            Err(e) => eprintln!("{e}"),
        }
        return ExitCode::SUCCESS;
    }
    let (Some(input), Some(output)) = (input, output) else {
        eprintln!("{USAGE}");
        return ExitCode::from(2);
    };
    match run(&input, &output, target.as_deref(), comments, imports_list) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("rcn-recomp : {e}");
            ExitCode::FAILURE
        }
    }
}

fn run(input: &str, output: &str, target: Option<&str>, comments: bool, imports_list: bool) -> Result<(), String> {
    let file = std::fs::read(input).map_err(|e| format!("{input} : {e}"))?;
    let program = analyse(&file)?;
    let exe_name = std::path::Path::new(input).file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
    let mut imports: Vec<String> = program.pe.imports.values().cloned().collect();
    let report = if output.ends_with(".rcnx") {
        let mut out = std::io::BufWriter::new(std::fs::File::create(output).map_err(|e| format!("{output} : {e}"))?);
        let report = to_native(program, &exe_name, target, &mut out, |_, _| {})?;
        std::io::Write::flush(&mut out).map_err(|e| format!("{output} : {e}"))?;
        report
    } else {
        let (text, report) = to_c(&program, &exe_name, comments);
        std::fs::write(output, text).map_err(|e| format!("{output} : {e}"))?;
        report
    };
    eprintln!("{}", report.summary(&exe_name));
    // Fonctions Windows importées : celles que l'hôte doit fournir.
    if imports_list {
        imports.sort();
        for name in imports {
            println!("{name}");
        }
    }
    Ok(())
}
