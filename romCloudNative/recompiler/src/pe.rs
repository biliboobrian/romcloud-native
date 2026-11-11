//! Lecture d'un exécutable PE32+ (Windows x86-64) : image telle que chargée en mémoire, sections,
//! table des fonctions (.pdata) et imports.

use std::collections::HashMap;

pub const SCN_MEM_EXECUTE: u32 = 0x2000_0000;

pub struct Section {
    pub rva: u32,
    pub size: u32,
    pub characteristics: u32,
}

pub struct Pe {
    pub image_base: u64,
    pub entry_rva: u32,
    pub size_of_image: u32,
    pub timestamp: u32,
    pub sections: Vec<Section>,
    /// Image à plat, indexée par RVA (sections copiées à leur adresse virtuelle).
    pub image: Vec<u8>,
    /// Fonctions déclarées par .pdata : (début, fin) en RVA, triées.
    pub pdata: Vec<(u32, u32)>,
    /// Case de la table d'imports (RVA) → « dll!fonction ».
    pub imports: HashMap<u32, String>,
    /// Fonctions exportées (RVA).
    pub exports: Vec<u32>,
}

fn u16_at(d: &[u8], o: usize) -> Option<u16> {
    Some(u16::from_le_bytes(d.get(o..o + 2)?.try_into().ok()?))
}
fn u32_at(d: &[u8], o: usize) -> Option<u32> {
    Some(u32::from_le_bytes(d.get(o..o + 4)?.try_into().ok()?))
}
fn u64_at(d: &[u8], o: usize) -> Option<u64> {
    Some(u64::from_le_bytes(d.get(o..o + 8)?.try_into().ok()?))
}

impl Pe {
    pub fn parse(file: &[u8]) -> Result<Pe, String> {
        let bad = || "fichier PE tronqué".to_string();
        if file.get(0..2) != Some(b"MZ") {
            return Err("ce n'est pas un exécutable Windows (signature MZ absente)".into());
        }
        let nt = u32_at(file, 0x3c).ok_or_else(bad)? as usize;
        if file.get(nt..nt + 4) != Some(b"PE\0\0") {
            return Err("signature PE absente".into());
        }
        let machine = u16_at(file, nt + 4).ok_or_else(bad)?;
        if machine != 0x8664 {
            return Err(format!(
                "processeur 0x{machine:04x} non géré : seul x86-64 (0x8664) est traduit"
            ));
        }
        let section_count = u16_at(file, nt + 6).ok_or_else(bad)? as usize;
        let timestamp = u32_at(file, nt + 8).ok_or_else(bad)?;
        let opt_size = u16_at(file, nt + 20).ok_or_else(bad)? as usize;
        let opt = nt + 24;
        if u16_at(file, opt).ok_or_else(bad)? != 0x20b {
            return Err("en-tête optionnel PE32+ attendu".into());
        }
        let entry_rva = u32_at(file, opt + 16).ok_or_else(bad)?;
        let image_base = u64_at(file, opt + 24).ok_or_else(bad)?;
        let size_of_image = u32_at(file, opt + 56).ok_or_else(bad)?;
        let size_of_headers = u32_at(file, opt + 60).ok_or_else(bad)? as usize;
        let dir_count = u32_at(file, opt + 108).ok_or_else(bad)? as usize;
        let dir = |i: usize| -> (u32, u32) {
            if i >= dir_count {
                return (0, 0);
            }
            let o = opt + 112 + i * 8;
            (u32_at(file, o).unwrap_or(0), u32_at(file, o + 4).unwrap_or(0))
        };

        if size_of_image as usize > 1 << 30 {
            return Err("image de plus de 1 Go".into());
        }
        let mut image = vec![0u8; size_of_image as usize];
        let headers = size_of_headers.min(file.len()).min(image.len());
        image[..headers].copy_from_slice(&file[..headers]);

        let mut sections = Vec::new();
        let table = opt + opt_size;
        for i in 0..section_count {
            let s = table + i * 40;
            let vsize = u32_at(file, s + 8).ok_or_else(bad)?;
            let rva = u32_at(file, s + 12).ok_or_else(bad)?;
            let raw_size = u32_at(file, s + 16).ok_or_else(bad)?;
            let raw_off = u32_at(file, s + 20).ok_or_else(bad)? as usize;
            let characteristics = u32_at(file, s + 36).ok_or_else(bad)?;
            let size = if vsize != 0 { vsize } else { raw_size };
            let copy = (raw_size.min(size) as usize)
                .min(file.len().saturating_sub(raw_off))
                .min(image.len().saturating_sub(rva as usize));
            if copy > 0 {
                image[rva as usize..rva as usize + copy].copy_from_slice(&file[raw_off..raw_off + copy]);
            }
            sections.push(Section { rva, size, characteristics });
        }

        let mut pe = Pe {
            image_base,
            entry_rva,
            size_of_image,
            timestamp,
            sections,
            image,
            pdata: Vec::new(),
            imports: HashMap::new(),
            exports: Vec::new(),
        };

        // .pdata : RUNTIME_FUNCTION { début, fin, infos de déroulement }.
        let (pdata_rva, pdata_size) = dir(3);
        for i in 0..(pdata_size / 12) {
            let o = pdata_rva + i * 12;
            if let (Some(begin), Some(end)) = (pe.u32(o), pe.u32(o + 4)) {
                if begin != 0 && end > begin && pe.is_exec(begin) {
                    pe.pdata.push((begin, end));
                }
            }
        }
        pe.pdata.sort_unstable();
        pe.pdata.dedup();

        // Imports : chaque case de la table d'adresses reçoit un nom.
        let (imp_rva, _) = dir(1);
        if imp_rva != 0 {
            let mut d = imp_rva;
            loop {
                let (Some(ilt), Some(name_rva), Some(iat)) = (pe.u32(d), pe.u32(d + 12), pe.u32(d + 16)) else { break };
                if name_rva == 0 && iat == 0 {
                    break;
                }
                let dll = pe.cstr(name_rva);
                let lookup = if ilt != 0 { ilt } else { iat };
                let mut i = 0;
                while let Some(entry) = pe.u64(lookup + i * 8) {
                    if entry == 0 {
                        break;
                    }
                    let name = if entry >> 63 != 0 {
                        format!("#{}", entry & 0xffff)
                    } else {
                        pe.cstr(entry as u32 + 2)
                    };
                    pe.imports.insert(iat + i * 8, format!("{dll}!{name}"));
                    i += 1;
                }
                d += 20;
            }
        }

        // Exports : points d'entrée supplémentaires.
        let (exp_rva, exp_size) = dir(0);
        if exp_rva != 0 {
            if let (Some(count), Some(funcs)) = (pe.u32(exp_rva + 20), pe.u32(exp_rva + 28)) {
                for i in 0..count.min(65536) {
                    if let Some(rva) = pe.u32(funcs + i * 4) {
                        // Hors du répertoire d'export (sinon c'est un renvoi vers une autre DLL).
                        if pe.is_exec(rva) && !(rva >= exp_rva && rva < exp_rva + exp_size) {
                            pe.exports.push(rva);
                        }
                    }
                }
            }
        }
        Ok(pe)
    }

    pub fn u32(&self, rva: u32) -> Option<u32> {
        u32_at(&self.image, rva as usize)
    }
    pub fn u64(&self, rva: u32) -> Option<u64> {
        u64_at(&self.image, rva as usize)
    }
    pub fn i32(&self, rva: u32) -> Option<i32> {
        self.u32(rva).map(|v| v as i32)
    }
    fn cstr(&self, rva: u32) -> String {
        let start = rva as usize;
        let Some(tail) = self.image.get(start..) else { return String::new() };
        let end = tail.iter().position(|&b| b == 0).unwrap_or(tail.len()).min(512);
        String::from_utf8_lossy(&tail[..end]).to_string()
    }
    pub fn section_of(&self, rva: u32) -> Option<&Section> {
        self.sections.iter().find(|s| rva >= s.rva && rva < s.rva + s.size)
    }
    pub fn is_exec(&self, rva: u32) -> bool {
        self.section_of(rva).is_some_and(|s| s.characteristics & SCN_MEM_EXECUTE != 0)
    }
    /// Plage .pdata contenant ce RVA.
    pub fn pdata_range(&self, rva: u32) -> Option<(u32, u32)> {
        let i = self.pdata.partition_point(|&(b, _)| b <= rva);
        if i == 0 {
            return None;
        }
        let r = self.pdata[i - 1];
        (rva < r.1).then_some(r)
    }
}
