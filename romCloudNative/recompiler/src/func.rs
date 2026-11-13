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
    // Étendue .pdata ; un morceau qui commence au milieu d'une fonction (point de reprise d'exception, suite
    // d'un catch) garde celle de la fonction : ses sauts internes y restent.
    let range = pe.pdata_range(entry);
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
                    // Cibles hors de l'étendue (autre morceau .pdata de la fonction) : traduites à part et
                    // atteintes par le saut terminal hors table, comme les branchements ordinaires.
                    let (targets, outside): (Vec<u32>, Vec<u32>) = jump_table(pe, &f, rva).into_iter().partition(|&t| !external(t));
                    f.refs.extend(outside);
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
    jump_table_64(pe, f, jmp_rva).unwrap_or_default()
}

/// Valeur d'un registre, si elle vient d'un « lea reg, [rip + …] » qui l'écrit en dernier sur tous les
/// chemins qui mènent à `before` (RVA). Les chemins sont remontés par les prédécesseurs connus : en
/// ordre d'adresses, une autre branche d'un switch peut réécrire le registre sans être sur le chemin.
fn lea_value(pe: &Pe, f: &Func, before: u32, reg: Register) -> Option<u32> {
    let reg = reg.full_register();
    // Registre volatil : un appel entre-temps l'a perdu.
    let volatile = matches!(reg, Register::RAX | Register::RCX | Register::RDX | Register::R8 | Register::R9 | Register::R10 | Register::R11);
    let mut preds: HashMap<u32, Vec<u32>> = HashMap::new();
    for (&rva, ins) in &f.insts {
        if falls_through(ins) {
            preds.entry(rva + ins.len() as u32).or_default().push(rva);
        }
        if ins.flow_control() != FlowControl::Call {
            if let Some(t) = branch_target(pe, ins) {
                preds.entry(t).or_default().push(rva);
            }
        }
        for &t in f.jump_tables.get(&rva).map(Vec::as_slice).unwrap_or(&[]) {
            preds.entry(t).or_default().push(rva);
        }
    }
    let mut value = None;
    let mut seen = std::collections::HashSet::new();
    let mut work: Vec<u32> = preds.get(&before).cloned().unwrap_or_default();
    if work.is_empty() {
        return None;
    }
    while let Some(rva) = work.pop() {
        if !seen.insert(rva) {
            continue;
        }
        if seen.len() > 2048 {
            return None;
        }
        let ins = &f.insts[&rva];
        if volatile && matches!(ins.flow_control(), FlowControl::Call | FlowControl::IndirectCall) {
            return None;
        }
        if ins.op_count() > 0 && ins.op0_kind() == OpKind::Register && ins.op0_register().full_register() == reg {
            let v = (ins.mnemonic() == Mnemonic::Lea && ins.memory_base() == Register::RIP).then(|| rip_target(pe, ins)).flatten()?;
            if value.is_some_and(|w| w != v) {
                return None;
            }
            value = Some(v);
            continue;
        }
        match preds.get(&rva) {
            Some(p) => work.extend(p.iter().copied()),
            // Début d'un morceau .pdata qui prolonge le précédent (fonction découpée par Visual C++) :
            // la valeur vient de la dernière écriture dans ce précédent, lu linéairement.
            None if rva == f.entry => {
                let v = lea_before_fragment(pe, f.entry, reg)?;
                if value.is_some_and(|w| w != v) {
                    return None;
                }
                value = Some(v);
            }
            // Début de la fonction atteint sans écriture : valeur inconnue.
            None => return None,
        }
    }
    value
}

/// Valeur d'un registre à l'entrée d'un morceau .pdata collé au précédent : dernière écriture de ce
/// registre dans le morceau précédent (décodé linéairement), si c'est un « lea reg, [rip + …] ».
fn lea_before_fragment(pe: &Pe, entry: u32, reg: Register) -> Option<u32> {
    let (begin, end) = pe.pdata_range(entry.checked_sub(1)?)?;
    if end != entry || entry - begin > 0x4000 {
        return None;
    }
    let mut rva = begin;
    let mut last = None;
    while rva < entry {
        let ins = decode_at(pe, rva)?;
        if matches!(ins.flow_control(), FlowControl::Call | FlowControl::IndirectCall) && matches!(reg, Register::RAX | Register::RCX | Register::RDX | Register::R8 | Register::R9 | Register::R10 | Register::R11) {
            last = None;
        }
        if ins.op_count() > 0 && ins.op0_kind() == OpKind::Register && ins.op0_register().full_register() == reg {
            last = Some(ins);
        }
        rva += ins.len() as u32;
    }
    if rva != entry {
        return None;
    }
    let ins = last?;
    (ins.mnemonic() == Mnemonic::Lea && ins.memory_base() == Register::RIP).then(|| rip_target(pe, &ins)).flatten()
}

/// Tables de sauts 64 bits, sous toutes les formes connues, de la forme générale
/// « mov(sxd) r, [T + i*4 + D] ; add r, B ; jmp r » avec T et B chargés par « lea …, [rip + …] » :
///  - clang / gcc : T = B = la table, entrées relatives à elle ;
///  - Visual C++ : T = B = l'adresse de chargement (__ImageBase), D = la table, entrées en RVA ;
///  - .NET (RyuJIT) : T = la table, B = le début de la fonction.
fn jump_table_64(pe: &Pe, f: &Func, jmp_rva: u32) -> Option<Vec<u32>> {
    let jmp = &f.insts[&jmp_rva];
    if jmp.op_count() != 1 || jmp.op0_kind() != OpKind::Register {
        return None;
    }
    let target = jmp.op0_register().full_register();
    let mut base_reg = None;
    let mut load = None;
    let mut bound = None;
    // Registres qui portent l'indice : celui de la table, et ceux qui y ont été recopiés.
    let mut index_regs: Vec<Register> = Vec::new();
    for (n, (&rva, ins)) in f.insts.range(..jmp_rva).rev().enumerate() {
        if n >= 48 {
            break;
        }
        let writes = |reg: Register| ins.op_count() > 0 && ins.op0_kind() == OpKind::Register && ins.op0_register().full_register() == reg;
        // « add jmp_reg, autre » : l'un des deux reçoit l'entrée de la table, l'autre porte la base.
        if base_reg.is_none() && n < 12 && writes(target) && ins.mnemonic() == Mnemonic::Add && ins.op1_kind() == OpKind::Register {
            base_reg = Some((rva, ins.op1_register().full_register()));
            continue;
        }
        let loads_entry = base_reg.is_some_and(|(_, other)| writes(target) || writes(other));
        if base_reg.is_some()
            && load.is_none()
            && loads_entry
            && matches!(ins.mnemonic(), Mnemonic::Mov | Mnemonic::Movsxd)
            && ins.op1_kind() == OpKind::Memory
            && ins.memory_index_scale() == 4
            && ins.memory_base() != Register::None
            && ins.memory_base() != Register::RIP
        {
            load = Some((rva, ins.op0_register().full_register(), ins.memory_base(), ins.memory_displacement64() as u32, ins.mnemonic() == Mnemonic::Movsxd));
            if ins.memory_index() != Register::None {
                index_regs.push(ins.memory_index().full_register());
            }
            continue;
        }
        // « mov indice, autre » : l'autre registre porte aussi l'indice.
        if load.is_some()
            && matches!(ins.mnemonic(), Mnemonic::Mov | Mnemonic::Movzx | Mnemonic::Movsxd)
            && ins.op0_kind() == OpKind::Register
            && ins.op1_kind() == OpKind::Register
            && index_regs.contains(&ins.op0_register().full_register())
        {
            index_regs.push(ins.op1_register().full_register());
            continue;
        }
        // Borne : « cmp indice, N ; ja défaut » avant le saut, sur un registre qui porte l'indice.
        if load.is_some()
            && bound.is_none()
            && ins.mnemonic() == Mnemonic::Cmp
            && ins.op0_kind() == OpKind::Register
            && index_regs.contains(&ins.op0_register().full_register())
        {
            if matches!(
                ins.op1_kind(),
                OpKind::Immediate8 | OpKind::Immediate8to32 | OpKind::Immediate8to64 | OpKind::Immediate32 | OpKind::Immediate32to64
            ) {
                bound = Some(ins.immediate(1).saturating_add(1));
            }
        }
    }
    let (add_rva, other) = base_reg?;
    let (load_rva, loaded, table_reg, disp, signed) = load?;
    // La base est celui des deux registres de l'addition qui n'a pas reçu l'entrée.
    let base_reg = if loaded == target { other } else { target };
    let base = lea_value(pe, f, add_rva, base_reg)?;
    let table = lea_value(pe, f, load_rva, table_reg)?.wrapping_add(disp);
    // Sans borne connue (indice tiré d'une seconde table d'octets, par exemple), la lecture s'arrête à
    // la première entrée qui sort de la fonction ; sans étendue connue de la fonction (fonction feuille
    // sans .pdata), à la première qui s'éloigne de plus de 64 Ko du saut.
    let count = bound.filter(|&b| b <= 4096).unwrap_or(1024) as u32;
    // (Le code d'une fonction sans .pdata peut précéder son point d'entrée : saut vers un morceau partagé.)
    let (low, high) = f.range.unwrap_or((jmp_rva.saturating_sub(0x10000), jmp_rva.saturating_add(0x10000)));
    let mut targets = Vec::new();
    for i in 0..count {
        let Some(entry) = pe.u32(table.wrapping_add(i * 4)) else { break };
        let offset = if signed { entry as i32 as i64 } else { entry as i64 };
        let t = base as i64 + offset;
        if t < 0 || t >= pe.size_of_image as i64 || !pe.is_exec(t as u32) {
            break;
        }
        let t = t as u32;
        // Borne connue (« cmp ; ja ») : toutes les entrées servent, même hors de l'étendue .pdata
        // (fonction découpée en plusieurs morceaux par Visual C++).
        if bound.is_none() && (t < low || t >= high) {
            break;
        }
        if !targets.contains(&t) {
            targets.push(t);
        }
    }
    (!targets.is_empty()).then_some(targets)
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
            // Code hors de la fonction (morceau partagé, suite dans une autre) : tous les drapeaux peuvent y être lus.
            let get = |t: u32| live_in.get(&t).copied().unwrap_or(STATUS_FLAGS);
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
            // « rep cmps / scas » répété zéro fois laisse les drapeaux intacts : ils ne sont pas écrasés.
            let repeated = ins.has_rep_prefix() || ins.has_repne_prefix();
            let input = match flow {
                FlowControl::Call | FlowControl::IndirectCall => 0,
                _ if repeated => (out | ins.rflags_read()) & STATUS_FLAGS,
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

