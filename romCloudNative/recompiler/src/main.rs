//! rcn-recomp : recompilateur statique d'un exécutable Windows x86 / x86-64.
//!
//!   rcn-recomp jeu.exe -o jeu_recomp.c                     C portable, à compiler avec le runtime
//!   rcn-recomp jeu.exe -o jeu.rcnx [--target aarch64]      code machine, chargé par le runtime
//!
//! Le format de sortie suit l'extension. Les données restent dans l'exécutable d'origine, chargé par le
//! runtime au lancement.

use rcn_recomp::{analyse, to_c, to_native, VERSION};
use std::process::ExitCode;

const USAGE: &str = "usage : rcn-recomp <jeu.exe> -o <sortie.c | sortie.rcnx> [--target aarch64|x86_64] [--no-comments] [--imports]";

fn main() -> ExitCode {
    let mut input = None;
    let mut output = None;
    let mut target = None;
    let mut comments = true;
    let mut imports_list = false;
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "-o" => output = args.next(),
            "--target" => target = args.next(),
            "--no-comments" => comments = false,
            "--imports" => imports_list = true,
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
    let report = if output.ends_with(".rcnx") {
        let (data, report) = to_native(&program, &exe_name, target, |_, _| {})?;
        std::fs::write(output, data).map_err(|e| format!("{output} : {e}"))?;
        report
    } else {
        let (text, report) = to_c(&program, &exe_name, comments);
        std::fs::write(output, text).map_err(|e| format!("{output} : {e}"))?;
        report
    };
    eprintln!("{}", report.summary(&exe_name));
    // Fonctions Windows importées : celles que l'hôte doit fournir.
    if imports_list {
        let mut names: Vec<&String> = program.pe.imports.values().collect();
        names.sort();
        for name in names {
            println!("{name}");
        }
    }
    Ok(())
}
