//! Points de reprise des exceptions : adresses où le code reprend après une exception (landing pads,
//! blocs __except), connues seulement des tables d'exceptions. Elles deviennent des fonctions traduites
//! (morceaux) pour que la reprise non locale du runtime puisse y entrer.
//!
//! Formats lus, chacun validé par des contrôles de plage (une table mal reconnue ne donne rien) :
//!  - LSDA de GCC / LLVM (MinGW, `__gxx_personality_seh0`) : en ligne après le gestionnaire (LLVM) ou par son RVA (GCC) ;
//!  - table de portées de `__C_specific_handler` (MSVC `__try` / `__except`) ;
//!  - tables de `__CxxFrameHandler4` (MSVC C++ 2019+) : continuations des blocs catch.

use crate::pe::Pe;

const UNW_FLAG_EHANDLER: u8 = 1;
const UNW_FLAG_UHANDLER: u8 = 2;
const UNW_FLAG_CHAININFO: u8 = 4;

/// Données du gestionnaire d'une fonction (.pdata) : RVA juste après le RVA du gestionnaire, ou None.
fn handler_data(pe: &Pe, unwind: u32) -> Option<u32> {
    let head = *pe.image.get(unwind as usize)?;
    let flags = head >> 3;
    if flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER) == 0 || flags & UNW_FLAG_CHAININFO != 0 {
        return None;
    }
    let count = *pe.image.get(unwind as usize + 2)? as u32;
    let handler_at = unwind + 4 + 2 * ((count + 1) & !1);
    pe.u32(handler_at)?;
    Some(handler_at + 4)
}

struct Reader<'a> {
    pe: &'a Pe,
    at: u32,
}

impl Reader<'_> {
    fn u8(&mut self) -> Option<u8> {
        let v = *self.pe.image.get(self.at as usize)?;
        self.at += 1;
        Some(v)
    }
    fn uleb(&mut self) -> Option<u64> {
        let mut v = 0u64;
        for shift in (0..64).step_by(7) {
            let b = self.u8()?;
            v |= ((b & 0x7f) as u64) << shift;
            if b & 0x80 == 0 {
                return Some(v);
            }
        }
        None
    }
    /// Valeur codée DW_EH_PE_* (formats usuels des tables d'appels).
    fn encoded(&mut self, enc: u8) -> Option<u64> {
        match enc & 0x0f {
            0x01 => self.uleb(),
            0x02 => {
                let v = self.pe.image.get(self.at as usize..self.at as usize + 2)?;
                self.at += 2;
                Some(u16::from_le_bytes([v[0], v[1]]) as u64)
            }
            0x03 | 0x0b => {
                let v = self.pe.u32(self.at)?;
                self.at += 4;
                Some(v as u64)
            }
            0x04 | 0x0c => {
                let v = self.pe.u64(self.at)?;
                self.at += 8;
                Some(v)
            }
            _ => None,
        }
    }
}

/// Landing pads d'une LSDA de GCC / LLVM pour la fonction [begin, end).
fn lsda_pads(pe: &Pe, lsda: u32, begin: u32, end: u32) -> Option<Vec<u32>> {
    let mut r = Reader { pe, at: lsda };
    let lp_enc = r.u8()?;
    let lp_start = if lp_enc != 0xff { r.encoded(lp_enc)? as u32 } else { begin };
    let tt_enc = r.u8()?;
    if tt_enc != 0xff {
        r.uleb()?;
    }
    let cs_enc = r.u8()?;
    let cs_len = r.uleb()? as u32;
    if cs_len > 0x10000 {
        return None;
    }
    let stop = r.at.checked_add(cs_len)?;
    let mut pads = Vec::new();
    while r.at < stop {
        let start = r.encoded(cs_enc)?;
        let len = r.encoded(cs_enc)?;
        let pad = r.encoded(cs_enc)?;
        r.uleb()?;
        // Plages et landing pads dans la fonction, sinon ce n'est pas une LSDA.
        if (lp_start as u64 + start + len) > end as u64 {
            return None;
        }
        if pad != 0 {
            let target = lp_start as u64 + pad;
            if target < begin as u64 || target >= end as u64 {
                return None;
            }
            pads.push(target as u32);
        }
    }
    (r.at == stop).then_some(pads)
}

/// Destinations des blocs __except d'une table de portées de __C_specific_handler.
fn scope_targets(pe: &Pe, table: u32, begin: u32, end: u32) -> Option<Vec<u32>> {
    let count = pe.u32(table)?;
    if count == 0 || count > 256 {
        return None;
    }
    let mut targets = Vec::new();
    for i in 0..count {
        let e = table + 4 + 16 * i;
        let (b, en, handler, jump) = (pe.u32(e)?, pe.u32(e + 4)?, pe.u32(e + 8)?, pe.u32(e + 12)?);
        if b < begin || en > end || b >= en {
            return None;
        }
        // Gestionnaire : 1 (EXCEPTION_EXECUTE_HANDLER) ou fonction de filtre ; jump 0 : bloc __finally.
        if handler > 1 && !pe.is_exec(handler) {
            return None;
        }
        if jump != 0 {
            if jump < begin || jump >= end {
                return None;
            }
            targets.push(jump);
        }
    }
    Some(targets)
}

/// Entier compressé de __CxxFrameHandler4 : la longueur (1 à 5 octets) est codée dans les bits bas du
/// premier octet ; la valeur est lue dans les 4 octets qui finissent l'encodage.
fn fh4_unsigned(pe: &Pe, at: &mut u32) -> Option<u32> {
    const LEN: [u32; 16] = [1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 5];
    const SHIFT: [u32; 16] = [0x19, 0x12, 0x19, 0x0b, 0x19, 0x12, 0x19, 0x04, 0x19, 0x12, 0x19, 0x0b, 0x19, 0x12, 0x19, 0x00];
    let b = *pe.image.get(*at as usize)? as usize & 15;
    let v = pe.u32((*at + LEN[b]).checked_sub(4)?)? >> SHIFT[b];
    *at += LEN[b];
    Some(v)
}
fn fh4_int(pe: &Pe, at: &mut u32) -> Option<u32> {
    let v = pe.u32(*at)?;
    *at += 4;
    Some(v)
}
fn data_rva(pe: &Pe, r: u32) -> bool {
    r != 0 && (r as usize) < pe.image.len() && !pe.is_exec(r)
}

/// Adresses de continuation des blocs catch d'une table __CxxFrameHandler4 (Visual C++ 2019 et
/// suivants) : FuncInfo4 → TryBlockMap4 → HandlerMap4 → HandlerType4. Sous FH4, le catch rend un
/// indice et la continuation n'existe que dans ces tables.
fn fh4_targets(pe: &Pe, data: u32, begin: u32, end: u32) -> Option<Vec<u32>> {
    let info = pe.u32(data)?;
    if !data_rva(pe, info) {
        return None;
    }
    let mut at = info;
    let header = *pe.image.get(at as usize)?;
    at += 1;
    if header & 0x80 != 0 || header & 0x10 == 0 {
        return None; // réservé, ou pas de blocs try
    }
    if header & 0x04 != 0 {
        fh4_unsigned(pe, &mut at)?; // BBT
    }
    if header & 0x08 != 0 {
        let unwind = fh4_int(pe, &mut at)?;
        if !data_rva(pe, unwind) {
            return None;
        }
    }
    let tries = fh4_int(pe, &mut at)?;
    if !data_rva(pe, tries) {
        return None;
    }
    let mut at = tries;
    let count = fh4_unsigned(pe, &mut at)?;
    if count == 0 || count > 512 {
        return None;
    }
    let mut targets = Vec::new();
    for _ in 0..count {
        let (low, high, catch_high) = (fh4_unsigned(pe, &mut at)?, fh4_unsigned(pe, &mut at)?, fh4_unsigned(pe, &mut at)?);
        if low > high || high > catch_high {
            return None;
        }
        let handlers = fh4_int(pe, &mut at)?;
        if !data_rva(pe, handlers) {
            return None;
        }
        let mut h = handlers;
        let n = fh4_unsigned(pe, &mut h)?;
        if n == 0 || n > 64 {
            return None;
        }
        for _ in 0..n {
            let hb = *pe.image.get(h as usize)?;
            h += 1;
            if hb & 0xC0 != 0 {
                return None;
            }
            if hb & 1 != 0 {
                fh4_unsigned(pe, &mut h)?;
            }
            if hb & 2 != 0 {
                fh4_int(pe, &mut h)?;
            }
            if hb & 4 != 0 {
                fh4_unsigned(pe, &mut h)?;
            }
            let funclet = fh4_int(pe, &mut h)?;
            if !pe.is_exec(funclet) {
                return None;
            }
            for _ in 0..(hb >> 4) & 3 {
                let cont = if hb & 8 != 0 { fh4_int(pe, &mut h)? } else { begin.checked_add(fh4_unsigned(pe, &mut h)?)? };
                // La continuation peut être dans un autre morceau de la fonction : du code, pas trop loin.
                if !pe.is_exec(cont) || cont.abs_diff(begin) > 0x100000 {
                    return None;
                }
                let _ = end;
                targets.push(cont);
            }
        }
    }
    Some(targets)
}

/// Tous les points de reprise trouvés dans les tables d'exceptions de l'exécutable.
pub fn resume_targets(pe: &Pe) -> Vec<u32> {
    let mut out = Vec::new();
    for &(begin, end, unwind) in &pe.runtime_functions {
        let Some(data) = handler_data(pe, unwind) else { continue };
        // LLVM (MinGW) : la LSDA suit directement le gestionnaire.
        if let Some(pads) = lsda_pads(pe, data, begin, end) {
            out.extend(pads);
            continue;
        }
        // GCC : données = RVA de la LSDA (hors du code).
        if let Some(lsda) = pe.u32(data).filter(|&r| r != 0 && !pe.is_exec(r) && (r as usize) < pe.image.len()) {
            if let Some(pads) = lsda_pads(pe, lsda, begin, end) {
                out.extend(pads);
                continue;
            }
        }
        if let Some(targets) = scope_targets(pe, data, begin, end) {
            out.extend(targets);
            continue;
        }
        if let Some(targets) = fh4_targets(pe, data, begin, end) {
            out.extend(targets);
        }
    }
    out.sort_unstable();
    out.dedup();
    out
}
