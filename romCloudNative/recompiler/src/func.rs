//! Découverte et décodage des fonctions : descente récursive depuis chaque point d'entrée, tables de
//! sauts, puis durée de vie des drapeaux (pour ne calculer que ceux qui sont lus).

use crate::pe::Pe;
use iced_x86::{Decoder, DecoderOptions, FlowControl, Instruction, Mnemonic, OpKind, Register, RflagsBits};
use std::collections::{BTreeMap, BTreeSet, HashMap};

/// Drapeaux d'état suivis (DF est toujours tenu à jour).
pub const STATUS_FLAGS: u32 =
    RflagsBits::OF | RflagsBits::SF | RflagsBits::ZF | RflagsBits::AF | RflagsBits::CF | RflagsBits::PF;

pub struct Func {
    pub entry: u32,
    /// Étendue déclarée par .pdata, si la fonction y figure.
    pub range: Option<(u32, u32)>,
    pub insts: BTreeMap<u32, Instruction>,
    /// Instructions qui reçoivent une étiquette (cibles de sauts).
    pub leaders: BTreeSet<u32>,
    /// Saut indirect (RVA) → cibles de sa table de sauts.
    pub jump_tables: HashMap<u32, Vec<u32>>,
    /// Autres fonctions visées (appels, sauts terminaux, adresses de code prises).
    pub refs: Vec<u32>,
}

impl Func {
    /// La cible d'un saut est-elle hors de la fonction (saut terminal vers une autre fonction) ?
    pub fn is_external(&self, target: u32) -> bool {
        !self.insts.contains_key(&target)
    }
}

pub fn decode_at(pe: &Pe, rva: u32) -> Option<Instruction> {
    let bytes = pe.image.get(rva as usize..)?;
    let mut decoder = Decoder::with_ip(pe.bits, bytes, pe.image_base + rva as u64, DecoderOptions::NONE);
    let ins = decoder.decode();
    (!ins.is_invalid()).then_some(ins)
}

/// RVA de la cible d'un branchement direct, si elle est dans le code de l'image.
pub fn branch_target(pe: &Pe, ins: &Instruction) -> Option<u32> {
    if ins.op_count() == 0 || !is_near_branch(ins) {
        return None;
    }
    pe.code_rva(ins.near_branch_target())
}

/// Branchement direct (cible dans l'instruction), en 32 ou en 64 bits.
pub fn is_near_branch(ins: &Instruction) -> bool {
    matches!(ins.op0_kind(), OpKind::NearBranch64 | OpKind::NearBranch32 | OpKind::NearBranch16)
}

/// RVA visé par un opérande mémoire relatif à RIP.
pub fn rip_target(pe: &Pe, ins: &Instruction) -> Option<u32> {
    if ins.memory_base() != Register::RIP {
        return None;
    }
    let rva = ins.memory_displacement64().checked_sub(pe.image_base)?;
    (rva < pe.size_of_image as u64).then_some(rva as u32)
}

/// RVA de la case mémoire désignée par une adresse fixe : relative à RIP (64 bits) ou absolue (32 bits).
pub fn mem_target(pe: &Pe, ins: &Instruction) -> Option<u32> {
    if ins.memory_base() == Register::RIP {
        return rip_target(pe, ins);
    }
    if pe.bits != 32 || ins.memory_base() != Register::None || ins.memory_index() != Register::None {
        return None;
    }
    let rva = ins.memory_displacement64().checked_sub(pe.image_base)?;
    (rva < pe.size_of_image as u64).then_some(rva as u32)
}

pub fn decode_function(pe: &Pe, entry: u32, known: &BTreeSet<u32>) -> Func {
    let range = pe.pdata_range(entry).filter(|r| r.0 == entry);
    let mut f = Func {
        entry,
        range,
        insts: BTreeMap::new(),
        leaders: BTreeSet::new(),
        jump_tables: HashMap::new(),
        refs: Vec::new(),
    };
    // Hors de la fonction : au-delà de son étendue .pdata ou, sans étendue, dans une autre fonction connue.
    let external = |t: u32| -> bool {
        if t == entry {
            return false;
        }
        match range {
            Some((b, e)) => t < b || t >= e,
            None => known.contains(&t) || pe.pdata_range(t).is_some(),
        }
    };
    f.leaders.insert(entry);
    let mut work = vec![entry];
    while let Some(start) = work.pop() {
        let mut rva = start;
        loop {
            if f.insts.contains_key(&rva) || !pe.is_exec(rva) {
                break;
            }
            if range.is_some_and(|(_, e)| rva >= e) {
                break;
            }
            let Some(ins) = decode_at(pe, rva) else { break };
            f.insts.insert(rva, ins);
            let next = rva + ins.len() as u32;
            // Adresse de code prise par « lea reg, [rip+…] » : fonction de rappel possible.
            if ins.mnemonic() == Mnemonic::Lea {
                if let Some(t) = rip_target(pe, &ins) {
                    if pe.is_exec(t) {
                        f.refs.push(t);
                    }
                }
            }
            // En 32 bits, une adresse de code est prise par une valeur immédiate (« push offset f »,
            // « mov eax, offset f ») ou par un déplacement absolu.
            if pe.bits == 32 {
                for i in 0..ins.op_count() {
                    if matches!(ins.op_kind(i), OpKind::Immediate32) {
                        if let Some(t) = pe.code_rva(ins.immediate(i)) {
                            f.refs.push(t);
                        }
                    }
                }
            }
            match ins.flow_control() {
                FlowControl::Call => {
                    if let Some(t) = branch_target(pe, &ins) {
                        f.refs.push(t);
                    }
                    rva = next;
                }
                FlowControl::ConditionalBranch => {
                    if let Some(t) = branch_target(pe, &ins) {
                        if external(t) {
                            f.refs.push(t);
                        } else {
                            f.leaders.insert(t);
                            work.push(t);
                        }
                    }
                    rva = next;
                }
                FlowControl::UnconditionalBranch => {
                    if let Some(t) = branch_target(pe, &ins) {
                        if external(t) {
                            f.refs.push(t);
                        } else {
                            f.leaders.insert(t);
                            work.push(t);
                        }
                    }
                    break;
                }
                FlowControl::IndirectBranch => {
                    let targets = jump_table(pe, &f, rva);
                    for &t in &targets {
                        f.leaders.insert(t);
                        work.push(t);
                    }
                    if !targets.is_empty() {
                        f.jump_tables.insert(rva, targets);
                    }
                    break;
                }
                FlowControl::Return | FlowControl::Interrupt | FlowControl::Exception => break,
                _ => rva = next,
            }
        }
    }
    // Une instruction dont la suivante en mémoire n'est pas celle émise juste après (décodages qui se
    // chevauchent) y saute explicitement : il lui faut une étiquette.
    let keys: Vec<u32> = f.insts.keys().copied().collect();
    for (i, &rva) in keys.iter().enumerate() {
        // Cible décodée avec la fonction (une autre fonction connue dans laquelle le code se
        // poursuit sans saut) : le branchement y va directement, il lui faut une étiquette.
        if let Some(t) = branch_target(pe, &f.insts[&rva]) {
            if f.insts[&rva].flow_control() != FlowControl::Call && f.insts.contains_key(&t) {
                f.leaders.insert(t);
            }
        }
        let next = rva + f.insts[&rva].len() as u32;
        if falls_through(&f.insts[&rva]) && keys.get(i + 1) != Some(&next) && f.insts.contains_key(&next) {
            f.leaders.insert(next);
        }
    }
    f
}

/// L'exécution continue-t-elle à l'instruction suivante ?
pub fn falls_through(ins: &Instruction) -> bool {
    !matches!(
        ins.flow_control(),
        FlowControl::UnconditionalBranch
            | FlowControl::IndirectBranch
            | FlowControl::Return
            | FlowControl::Interrupt
            | FlowControl::Exception
    )
}

/// Cibles de la table de sauts d'un « jmp reg » (forme de clang et gcc : entrées de 32 bits relatives
/// à la table, « lea base, [rip+table] ; movsxd r, [base+i*4] ; add r, base ; jmp r »).
fn jump_table(pe: &Pe, f: &Func, jmp_rva: u32) -> Vec<u32> {
    // 32 bits (MSVC, clang) : « jmp [table + reg*4] », table d'adresses absolues, lue tant que ses
    // entrées désignent du code.
    let jmp = &f.insts[&jmp_rva];
    if pe.bits == 32 {
        if jmp.op0_kind() != OpKind::Memory || jmp.memory_base() != Register::None || jmp.memory_index_scale() != 4 {
            return Vec::new();
        }
        let Some(table) = jmp.memory_displacement64().checked_sub(pe.image_base).filter(|&t| t < pe.size_of_image as u64) else {
            return Vec::new();
        };
        let mut targets = Vec::new();
        for i in 0..1024u32 {
            match pe.u32(table as u32 + i * 4).and_then(|v| pe.code_rva(v as u64)) {
                Some(t) => {
                    if !targets.contains(&t) {
                        targets.push(t);
                    }
                }
                // Les premières cases peuvent ne jamais servir (indice de 1 à 3 : la table « commence »
                // dans les octets de l'instruction précédente) ; après une case valide, la table est finie.
                None if targets.is_empty() && i < 4 => {}
                None => break,
            }
        }
        // Indice négatif (« jmp [table + reg*4] » avec reg de -n à -1) : la table précède son adresse.
        for i in 1..=64u32 {
            let Some(at) = (table as u32).checked_sub(i * 4) else { break };
            let Some(t) = pe.u32(at).and_then(|v| pe.code_rva(v as u64)) else { break };
            if !targets.contains(&t) {
                targets.push(t);
            }
        }        return targets;
    }
    // Registre qui porte l'adresse de la table dans « movsxd r, [base+i*4] », juste avant le saut.
    let mut base = Register::None;
    let mut bound = None;
    for (n, (_, ins)) in f.insts.range(..jmp_rva).rev().enumerate() {
        if n >= 48 {
            break;
        }
        match ins.mnemonic() {
            Mnemonic::Movsxd if n < 12 && base == Register::None => {
                if ins.op1_kind() == OpKind::Memory && ins.memory_index_scale() == 4 {
                    base = ins.memory_base();
                }
            }
            // Borne : « cmp index, N ; ja défaut » avant le saut.
            Mnemonic::Cmp if bound.is_none() => {
                if matches!(
                    ins.op1_kind(),
                    OpKind::Immediate8 | OpKind::Immediate8to32 | OpKind::Immediate8to64 | OpKind::Immediate32 | OpKind::Immediate32to64
                ) {
                    bound = Some(ins.immediate(1).saturating_add(1));
                }
            }
            _ => {}
        }
    }
    if base == Register::None || base == Register::RIP {
        return Vec::new();
    }
    // Son chargement « lea base, [rip+table] » peut être loin avant (sorti d'une boucle) : le plus
    // proche qui précède le saut.
    let table = f
        .insts
        .range(..jmp_rva)
        .rev()
        .find(|(_, ins)| ins.mnemonic() == Mnemonic::Lea && ins.op0_register() == base && ins.memory_base() == Register::RIP)
        .and_then(|(_, ins)| rip_target(pe, ins));
    let Some(table) = table else { return Vec::new() };
    // Sans borne connue, la lecture s'arrête à la première entrée qui sort de la fonction.
    let count = bound.filter(|&b| b <= 4096).unwrap_or(if f.range.is_some() { 1024 } else { 0 });
    let mut targets = Vec::new();
    for i in 0..count as u32 {
        let Some(entry) = pe.i32(table + i * 4) else { break };
        let t = (table as i64 + entry as i64) as u64;
        if t >= pe.size_of_image as u64 || !pe.is_exec(t as u32) {
            break;
        }
        let t = t as u32;
        if f.range.is_some_and(|(b, e)| t < b || t >= e) {
            break;
        }
        if !targets.contains(&t) {
            targets.push(t);
        }
    }
    targets
}

/// Drapeaux lus après chaque instruction avant d'être réécrits : seuls ceux-là sont calculés.
pub fn flag_liveness(pe: &Pe, f: &Func) -> HashMap<u32, u32> {
    let keys: Vec<u32> = f.insts.keys().rev().copied().collect();
    let mut live_in: HashMap<u32, u32> = keys.iter().map(|&k| (k, 0)).collect();
    let mut live_out: HashMap<u32, u32> = live_in.clone();
    loop {
        let mut changed = false;
        for &rva in &keys {
            let ins = &f.insts[&rva];
            let next = rva + ins.len() as u32;
            let get = |t: u32| live_in.get(&t).copied().unwrap_or(0);
            let mut out = 0;
            let flow = ins.flow_control();
            if falls_through(ins) {
                out |= get(next);
            }
            match flow {
                FlowControl::ConditionalBranch | FlowControl::UnconditionalBranch => {
                    if let Some(t) = branch_target(pe, ins) {
                        out |= get(t);
                    }
                }
                FlowControl::IndirectBranch => {
                    for &t in f.jump_tables.get(&rva).map(Vec::as_slice).unwrap_or(&[]) {
                        out |= get(t);
                    }
                }
                _ => {}
            }
            // Un appel ne transmet aucun drapeau (convention d'appel Windows x64).
            let input = match flow {
                FlowControl::Call | FlowControl::IndirectCall => 0,
                _ => ((out & !ins.rflags_modified()) | ins.rflags_read()) & STATUS_FLAGS,
            };
            if live_out[&rva] != out || live_in[&rva] != input {
                live_out.insert(rva, out);
                live_in.insert(rva, input);
                changed = true;
            }
        }
        if !changed {
            return live_out;
        }
    }
}
