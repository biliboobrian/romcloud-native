//! Générateur de tests d'instructions : relève les formes d'instructions d'un exécutable x86-64 et
//! écrit un fichier assembleur où chacune est isolée dans une petite fonction. Le programme de test
//! (samples/isa/driver.c) les exécute avec des registres, drapeaux et mémoire tirés au hasard et
//! imprime l'état obtenu : sur le processeur, puis traduit par rcn-recomp, les sorties doivent être
//! identiques. Sert à trouver une instruction mal traduite.
//!
//!   cargo run --release --example isagen -- jeu.exe isa_tests.S [exemples par forme]

use iced_x86::{
    Code, CpuidFeature, Encoder, EncodingKind, FlowControl, Formatter, InstructionInfoFactory, IntelFormatter, Mnemonic, OpKind,
    Register, RflagsBits,
};
use rcn_recomp::analyse;
use std::collections::HashMap;
use std::fmt::Write;

fn gpr_index(r: Register) -> Option<usize> {
    let full = r.full_register();
    let list = [
        Register::RAX, Register::RCX, Register::RDX, Register::RBX, Register::RSP, Register::RBP, Register::RSI, Register::RDI,
        Register::R8, Register::R9, Register::R10, Register::R11, Register::R12, Register::R13, Register::R14, Register::R15,
    ];
    list.iter().position(|&x| x == full)
}

fn allowed_feature(f: CpuidFeature) -> bool {
    matches!(
        f,
        CpuidFeature::INTEL8086
            | CpuidFeature::INTEL8086_ONLY
            | CpuidFeature::INTEL186
            | CpuidFeature::INTEL286
            | CpuidFeature::INTEL386
            | CpuidFeature::INTEL486
            | CpuidFeature::X64
            | CpuidFeature::CMOV
            | CpuidFeature::MMX
            | CpuidFeature::SSE
            | CpuidFeature::SSE2
            | CpuidFeature::SSE3
            | CpuidFeature::SSSE3
            | CpuidFeature::SSE4_1
            | CpuidFeature::SSE4_2
            | CpuidFeature::POPCNT
            | CpuidFeature::LZCNT
            | CpuidFeature::CX8
            | CpuidFeature::CMPXCHG16B
            | CpuidFeature::MULTIBYTENOP
            | CpuidFeature::PAUSE
    )
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.len() < 2 {
        eprintln!("usage : isagen <jeu.exe> <sortie.S> [exemples par forme]");
        std::process::exit(2);
    }
    let per_form: usize = args.get(2).and_then(|v| v.parse().ok()).unwrap_or(3);
    let file = std::fs::read(&args[0]).expect("lecture");
    let program = analyse(&file).expect("analyse");
    let pe = &program.pe;
    let mut factory = InstructionInfoFactory::new();
    let mut fmt = IntelFormatter::new();
    // Forme → exemples retenus (octets, texte, masques).
    let mut seen: HashMap<(Code, bool, bool), Vec<Vec<u8>>> = HashMap::new();
    let mut tests: Vec<(Vec<u8>, String, u32, u32, u32, u32)> = Vec::new();
    let mut skipped: HashMap<String, usize> = HashMap::new();
    for f in program.funcs.values() {
        for (&rva, original) in &f.insts {
            let mut skip = |why: &str| *skipped.entry(why.to_string()).or_default() += 1;
            // Adresse relative à rip (constantes) ou à rsp (variables sur la pile) : réécrite sur un registre
            // libre, qui pointera dans le tampon de travail ; l'instruction est alors réencodée.
            let mut ins_copy = *original;
            let mut rewritten: Option<Vec<u8>> = None;
            let mem_op = (0..original.op_count()).any(|i| original.op_kind(i) == OpKind::Memory);
            if mem_op
                && (original.is_ip_rel_memory_operand() || original.memory_base() == Register::RSP)
                && original.segment_prefix() == Register::None
                && original.stack_pointer_increment() == 0
                && original.memory_index() != Register::RSP
                && original.flow_control() == FlowControl::Next
                && original.encoding() == EncodingKind::Legacy
            {
                let used: Vec<Register> =
                    factory.info(original).used_registers().iter().map(|u| u.register().full_register()).collect();
                let free = [Register::R15, Register::R14, Register::R13, Register::R12, Register::RBX, Register::RBP, Register::RSI, Register::RDI]
                    .into_iter()
                    .find(|r| !used.contains(r));
                if let Some(free) = free {
                    ins_copy.set_memory_base(free);
                    ins_copy.set_memory_displacement64(0x40);
                    ins_copy.set_memory_displ_size(1);
                    let mut enc = Encoder::new(64);
                    if enc.encode(&ins_copy, rva as u64).is_ok() {
                        rewritten = Some(enc.take_buffer());
                    } else {
                        ins_copy = *original;
                    }
                }
            }
            let ins = &ins_copy;
            if ins.flow_control() != FlowControl::Next {
                skip("flot");
                continue;
            }
            if ins.encoding() != EncodingKind::Legacy {
                skip("vex/evex");
                continue;
            }
            // ISAGEN_ONLY=sse4_1,sse4_2 : seulement les instructions de ces extensions.
            if let Ok(only) = std::env::var("ISAGEN_ONLY") {
                let wanted = only.to_lowercase();
                if !ins.cpuid_features().iter().any(|c| wanted.split(',').any(|w| format!("{c:?}").to_lowercase() == w)) {
                    skip("filtre");
                    continue;
                }
            }
            if !ins.cpuid_features().iter().all(|&c| allowed_feature(c)) {
                skip("extension");
                continue;
            }
            if ins.is_ip_rel_memory_operand() || ins.segment_prefix() != Register::None || ins.memory_segment() == Register::FS
                || ins.memory_segment() == Register::GS
            {
                skip("rip/segment");
                continue;
            }
            if matches!(ins.mnemonic(), Mnemonic::Cpuid | Mnemonic::Rdtsc | Mnemonic::Xgetbv | Mnemonic::Int3 | Mnemonic::Ud2 | Mnemonic::Lfence | Mnemonic::Mfence | Mnemonic::Sfence | Mnemonic::Prefetchnta | Mnemonic::Prefetcht0 | Mnemonic::Prefetcht1 | Mnemonic::Prefetcht2 | Mnemonic::Prefetchw | Mnemonic::Ldmxcsr | Mnemonic::Stmxcsr | Mnemonic::Fxsave | Mnemonic::Fxrstor | Mnemonic::Pushfq | Mnemonic::Popfq | Mnemonic::Std | Mnemonic::Cld | Mnemonic::Xlatb | Mnemonic::Pshufw | Mnemonic::Sti | Mnemonic::Cli | Mnemonic::In | Mnemonic::Out | Mnemonic::Insb | Mnemonic::Insw | Mnemonic::Insd | Mnemonic::Outsb | Mnemonic::Outsw | Mnemonic::Outsd | Mnemonic::Hlt | Mnemonic::Iretd | Mnemonic::Iretq | Mnemonic::Verr | Mnemonic::Verw | Mnemonic::Lar | Mnemonic::Lsl | Mnemonic::Clts | Mnemonic::Invd | Mnemonic::Wbinvd | Mnemonic::Rdmsr | Mnemonic::Wrmsr | Mnemonic::Rdpmc | Mnemonic::Int1 | Mnemonic::Into | Mnemonic::Enter | Mnemonic::Leave | Mnemonic::Lahf | Mnemonic::Sahf) {
                skip("systeme");
                continue;
            }
            let info = factory.info(ins);
            if info.used_registers().iter().any(|u| u.register().full_register() == Register::RSP) || ins.stack_pointer_increment() != 0 {
                skip("pile");
                continue;
            }
            if ins.memory_size().size() > 0 && ins.op_count() > 0 && ins.memory_displacement64() as i64 > 16000
                && (0..ins.op_count()).any(|i| ins.op_kind(i) == OpKind::Memory)
            {
                skip("deplacement");
                continue;
            }
            let has_mem = (0..ins.op_count()).any(|i| ins.op_kind(i) == OpKind::Memory);
            if has_mem && ins.memory_base() != Register::None && ins.memory_base().size() != 8 {
                skip("adresse32");
                continue;
            }
            if has_mem && (ins.memory_displacement64() as i64) < -16000 {
                skip("deplacement");
                continue;
            }
            let bytes = rewritten.clone().unwrap_or_else(|| pe.image[rva as usize..rva as usize + ins.len()].to_vec());
            let list = seen.entry((ins.code(), has_mem, rewritten.is_some())).or_default();
            if list.len() >= per_form || list.contains(&bytes) {
                continue;
            }
            // Registres d'adresse : pointeurs vers le tampon ; index : petits ; rdx nul pour les divisions.
            let mut ptr = 0u32;
            let mut small = 0u32;
            let mut special = 0u32;
            if has_mem {
                // Adresse finale alignée sur 16 octets (movaps, movdqa…) quand l'index est nul.
                special |= ((ins.memory_displacement64() as i64).wrapping_neg() as u32 & 15) << 8;
                if let Some(i) = gpr_index(ins.memory_base()) {
                    ptr |= 1 << i;
                }
                if let Some(i) = gpr_index(ins.memory_index()) {
                    small |= 1 << i;
                }
                if ins.memory_base() != Register::None && ins.memory_base() == ins.memory_index() {
                    skip("base=index");
                    continue;
                }
            }
            let string_op = (0..ins.op_count()).any(|i| {
                matches!(ins.op_kind(i), OpKind::MemorySegRSI | OpKind::MemorySegESI | OpKind::MemoryESRDI | OpKind::MemoryESEDI | OpKind::MemorySegRDI)
            });
            if string_op {
                if ins.op_kind(0) == OpKind::MemorySegESI || ins.op_kind(0) == OpKind::MemoryESEDI {
                    skip("adresse32");
                    continue;
                }
                ptr |= (1 << 6) | (1 << 7);
                small |= 1 << 1;
            }
            if matches!(ins.mnemonic(), Mnemonic::Xlatb) {
                ptr |= 1 << 3;
                small |= 1;
            }
            if matches!(ins.mnemonic(), Mnemonic::Div | Mnemonic::Idiv) {
                if ins.op_kind(0) == OpKind::Register && matches!(gpr_index(ins.op0_register()), Some(0) | Some(2)) {
                    skip("division");
                    continue;
                }
                if ins.op_kind(0) != OpKind::Register {
                    skip("division");
                    continue;
                }
                special |= 1; // rdx = 0, rax positif (8 bits : ah = 0)
                // Diviseur impair, donc non nul (registres 8 bits hauts ah…bh exclus).
                if ins.op0_register().size() == 1 && matches!(ins.op0_register(), Register::AH | Register::CH | Register::DH | Register::BH) {
                    skip("division");
                    continue;
                }
                special |= (gpr_index(ins.op0_register()).unwrap() as u32 + 1) << 16;
                let size = if ins.op_kind(0) == OpKind::Register { ins.op0_register().size() } else { ins.memory_size().size() };
                if size == 1 {
                    special |= 2;
                }
                // Division signée : dividende assez petit pour que le quotient tienne (diviseur ±1).
                if ins.mnemonic() == Mnemonic::Idiv {
                    special |= 4 | ((size as u32) << 4);
                }
            }
            // Drapeaux comparés : ceux qui sont définis après l'instruction (AF exclu).
            let undefined = ins.rflags_undefined();
            let mut flags_mask = 0u32;
            for (bit, eflags) in [(RflagsBits::CF, 1), (RflagsBits::PF, 4), (RflagsBits::ZF, 0x40), (RflagsBits::SF, 0x80), (RflagsBits::OF, 0x800)] {
                if undefined & bit == 0 {
                    flags_mask |= eflags;
                }
            }
            // Adresse du tampon tronquée à 32 bits : diffère d'une exécution à l'autre.
            if ins.mnemonic() == Mnemonic::Lea && ins.op0_register().size() < 8 && ptr != 0 {
                skip("lea32");
                continue;
            }
            let mut text = String::new();
            fmt.format(ins, &mut text);
            list.push(bytes.clone());
            tests.push((bytes, text, rva, ptr | (small << 16), special, flags_mask));
        }
    }
    let mut out = String::new();
    out.push_str("// Produit par isagen : ne pas modifier.\n.intel_syntax noprefix\n.text\n");
    for (n, (bytes, text, rva, _, _, _)) in tests.iter().enumerate() {
        let _ = writeln!(out, "\n// {rva:x} : {text}\n.p2align 4\nt_{n}:");
        out.push_str(
            "  push rbx\n  push rbp\n  push rsi\n  push rdi\n  push r12\n  push r13\n  push r14\n  push r15\n  sub rsp, 8\n  mov [rip + isa_state], rcx\n",
        );
        for i in 0..16 {
            let _ = writeln!(out, "  movdqu xmm{i}, [rcx + {}]", 136 + 16 * i);
        }
        // MMX : mm0–mm7 pris dans la moitié basse de xmm8–xmm15 de l'état, rendus de même (puis emms).
        let mmx = text.split(|c: char| !c.is_ascii_alphanumeric()).any(|w| w.len() == 3 && w.starts_with("mm") && w.as_bytes()[2].is_ascii_digit());
        if mmx {
            for i in 0..8 {
                let _ = writeln!(out, "  movq mm{i}, [rcx + {}]", 136 + 16 * (8 + i));
            }
        }
        out.push_str("  push qword ptr [rcx + 128]\n  popfq\n");
        let names = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"];
        for (i, r) in names.iter().enumerate() {
            if i != 1 && i != 4 {
                let _ = writeln!(out, "  mov {r}, [rcx + {}]", 8 * i);
            }
        }
        out.push_str("  mov rcx, [rcx + 8]\n");
        let list: Vec<String> = bytes.iter().map(|b| format!("0x{b:02x}")).collect();
        let _ = writeln!(out, "  .byte {}", list.join(", "));
        out.push_str("  mov [rip + isa_tmp], rax\n  mov rax, [rip + isa_state]\n");
        for (i, r) in names.iter().enumerate() {
            if i != 0 && i != 4 {
                let _ = writeln!(out, "  mov [rax + {}], {r}", 8 * i);
            }
        }
        out.push_str("  pushfq\n  pop qword ptr [rax + 128]\n");
        for i in 0..16 {
            let _ = writeln!(out, "  movdqu [rax + {}], xmm{i}", 136 + 16 * i);
        }
        if mmx {
            for i in 0..8 {
                let _ = writeln!(out, "  movq [rax + {}], mm{i}", 136 + 16 * (8 + i));
            }
            out.push_str("  emms\n");
        }
        out.push_str("  mov rcx, [rip + isa_tmp]\n  mov [rax], rcx\n");
        out.push_str("  add rsp, 8\n  pop r15\n  pop r14\n  pop r13\n  pop r12\n  pop rdi\n  pop rsi\n  pop rbp\n  pop rbx\n  ret\n");
    }
    out.push_str("\n.data\n.p2align 3\n.globl isa_state\nisa_state: .quad 0\nisa_tmp: .quad 0\n");
    let _ = writeln!(out, ".globl isa_count\nisa_count: .long {}\n.p2align 3\n.globl isa_table\nisa_table:", tests.len());
    for (n, (_, text, rva, masks, special, flags)) in tests.iter().enumerate() {
        let _ = writeln!(out, "  .quad t_{n}, s_{n}\n  .long 0x{masks:x}, 0x{special:x}, 0x{:x}, 0x{rva:x}", flags);
        let _ = text;
    }
    out.push_str(".section .rdata\n");
    for (n, (_, text, rva, _, _, _)) in tests.iter().enumerate() {
        let _ = writeln!(out, "s_{n}: .asciz \"{rva:x} {}\"", text.replace('"', "'"));
    }
    std::fs::write(&args[1], out).expect("ecriture");
    eprintln!("{} tests ; ignorées : {skipped:?}", tests.len());
}
