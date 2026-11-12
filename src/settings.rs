//! Qt-compatible brush library JSON, bounded before decoding, atomically persisted.
use crate::{project, types::*};
use base64::{Engine, engine::general_purpose::STANDARD};
use fs2::FileExt;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    fs::{self, OpenOptions},
    io::{Cursor, Read},
    path::{Path, PathBuf},
    sync::Arc,
};
use unicode_normalization::UnicodeNormalization;
const MAX_FILE: u64 = 16 * 1024 * 1024;
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
struct Settings {
    size: i32,
    hardness: i32,
    flow: i32,
    spacing: i32,
    angle: i32,
    roundness: i32,
    pressure_minimum: i32,
    pressure_curve: f64,
    pressure_size: bool,
    pressure_flow: bool,
    tilt_angle: bool,
    tilt_roundness: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    tip_mask_png: Option<String>,
}
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Preset {
    name: String,
    tool: u8,
    settings: Settings,
}
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Library {
    format: String,
    version: u32,
    presets: Vec<Preset>,
}
fn err(e: impl std::fmt::Display) -> String {
    e.to_string()
}
fn valid_name(name: &str) -> bool {
    !name.is_empty()&&name.encode_utf16().count()<=80&&name.trim()==name&&name.nfc().collect::<String>()==name&&!name.chars().any(|c|c.is_control()||matches!(c,'<'|'>'|'\u{200b}'..='\u{200f}'|'\u{202a}'..='\u{202e}'|'\u{2060}'..='\u{206f}'|'\u{feff}'))
}
pub fn validate(presets: &[BrushPreset]) -> Result<()> {
    if presets.len() > 128 {
        return Err("En fazla 128 kişisel fırça".into());
    }
    let mut names = std::collections::HashSet::new();
    let mut bytes = 0;
    for p in presets {
        if !valid_name(&p.name) || !names.insert(&p.name) || p.tool > 3 || !p.settings.valid() {
            return Err("Fırça adı, ayarı veya yinelenen kayıt geçersiz".into());
        }
        if let Some(m) = &p.settings.tip_mask {
            bytes += m.as_raw().len()
        }
    }
    if bytes > 8 * 1024 * 1024 {
        return Err("Fırça uçları 8 MiB sınırını aşıyor".into());
    }
    Ok(())
}
pub fn read_brushes(path: &Path) -> Result<Vec<BrushPreset>> {
    let data = read_optional(path)?.ok_or("Fırça dosyası bulunamadı")?;
    decode(&data)
}
fn decode(data: &[u8]) -> Result<Vec<BrushPreset>> {
    if data.len() as u64 > MAX_FILE {
        return Err("Fırça dosyası çok büyük".into());
    }
    let lib: Library = serde_json::from_slice(data).map_err(err)?;
    if lib.format != "PhotoDeck-brush-library" || lib.version != 1 || lib.presets.len() > 128 {
        return Err("Fırça biçimi/sürümü desteklenmiyor".into());
    }
    let mut out = vec![];
    let mut total = 0usize;
    for p in lib.presets {
        let s = p.settings;
        let tip = if let Some(encoded) = s.tip_mask_png {
            if encoded.len() > 524288 {
                return Err("Fırça uç kaydı çok büyük".into());
            }
            let png = STANDARD.decode(encoded).map_err(err)?;
            let mut decoder = png::Decoder::new(Cursor::new(png));
            decoder.set_limits(png::Limits {
                bytes: 512 * 512 * 2,
            });
            let mut reader = decoder.read_info().map_err(err)?;
            let info = reader.info();
            if info.width == 0
                || info.height == 0
                || info.width > 512
                || info.height > 512
                || info.bit_depth != png::BitDepth::Eight
                || info.color_type != png::ColorType::Grayscale
                || info.animation_control.is_some()
            {
                return Err("Uç 8 bit gri PNG olmalı; en fazla 512 px".into());
            }
            let (w, h) = (info.width, info.height);
            let mut b = vec![0; w as usize * h as usize];
            reader.next_frame(&mut b).map_err(err)?;
            total += b.len();
            if total > 8 * 1024 * 1024 {
                return Err("Uç bellek sınırı".into());
            }
            Some(Arc::new(
                image::GrayImage::from_raw(w, h, b).ok_or("Uç verisi bozuk")?,
            ))
        } else {
            None
        };
        out.push(BrushPreset {
            name: p.name,
            tool: p.tool,
            settings: BrushSettings {
                size: s.size,
                hardness: s.hardness,
                flow: s.flow,
                spacing: s.spacing,
                angle: s.angle,
                roundness: s.roundness,
                pressure_minimum: s.pressure_minimum,
                pressure_curve: s.pressure_curve,
                pressure_size: s.pressure_size,
                pressure_flow: s.pressure_flow,
                tilt_angle: s.tilt_angle,
                tilt_roundness: s.tilt_roundness,
                tip_mask: tip,
            },
        })
    }
    validate(&out)?;
    Ok(out)
}
fn encode(presets: &[BrushPreset]) -> Result<Vec<u8>> {
    validate(presets)?;
    let mut out = vec![];
    for p in presets {
        let s = &p.settings;
        let tip = if let Some(m) = &s.tip_mask {
            let mut c = Cursor::new(Vec::new());
            m.write_to(&mut c, image::ImageFormat::Png).map_err(err)?;
            Some(STANDARD.encode(c.into_inner()))
        } else {
            None
        };
        out.push(Preset {
            name: p.name.clone(),
            tool: p.tool,
            settings: Settings {
                size: s.size,
                hardness: s.hardness,
                flow: s.flow,
                spacing: s.spacing,
                angle: s.angle,
                roundness: s.roundness,
                pressure_minimum: s.pressure_minimum,
                pressure_curve: s.pressure_curve,
                pressure_size: s.pressure_size,
                pressure_flow: s.pressure_flow,
                tilt_angle: s.tilt_angle,
                tilt_roundness: s.tilt_roundness,
                tip_mask_png: tip,
            },
        })
    }
    let data = serde_json::to_vec_pretty(&Library {
        format: "PhotoDeck-brush-library".into(),
        version: 1,
        presets: out,
    })
    .map_err(err)?;
    if data.len() as u64 > MAX_FILE {
        return Err("Fırça dosyası çok büyük".into());
    }
    Ok(data)
}
pub fn write_brushes(path: &Path, presets: &[BrushPreset]) -> Result<()> {
    if !path
        .extension()
        .is_some_and(|e| e.eq_ignore_ascii_case("pdbrush"))
    {
        return Err("Fırça dosyası .pdbrush olmalı".into());
    }
    project::atomic_write(path, &encode(presets)?)
}
pub fn merge_brushes(
    current: &[BrushPreset],
    incoming: &[BrushPreset],
) -> Result<Vec<BrushPreset>> {
    validate(current)?;
    validate(incoming)?;
    let mut out = current.to_vec();
    for p in incoming {
        let mut p = p.clone();
        let name = p.name.clone();
        let mut i = 2;
        while out.iter().any(|x| x.name == p.name) {
            let suffix = format!(" ({i})");
            let mut base = name.clone();
            while base.encode_utf16().count() + suffix.len() > 80 {
                base.pop();
            }
            p.name = format!("{base}{suffix}");
            i += 1;
        }
        out.push(p);
    }
    validate(&out)?;
    Ok(out)
}
pub fn builtins() -> Vec<BrushPreset> {
    [
        ("Yumuşak", 0, 64, 10),
        ("Sert", 0, 32, 100),
        ("Kalem", 1, 3, 100),
        ("Silgi", 2, 64, 70),
    ]
    .into_iter()
    .map(|(name, tool, size, hardness)| BrushPreset {
        name: name.into(),
        tool,
        settings: BrushSettings {
            size,
            hardness,
            ..Default::default()
        },
    })
    .collect()
}
pub struct BrushStore {
    pub presets: Vec<BrushPreset>,
    path: PathBuf,
    digest: Option<[u8; 32]>,
    unreadable: bool,
}
impl BrushStore {
    pub fn open() -> Result<Self> {
        let dirs = directories::ProjectDirs::from("org", "PhotoDeck", "PhotoDeck-Rust")
            .ok_or("Ayar klasörü bulunamadı")?;
        Self::at(dirs.config_dir().join("brushes.pdbrush"))
    }
    pub fn at(path: PathBuf) -> Result<Self> {
        fs::create_dir_all(path.parent().ok_or("Ayar yolu geçersiz")?).map_err(err)?;
        let bytes = read_optional(&path)?;
        let digest = bytes.as_ref().map(|b| Sha256::digest(b).into());
        let (presets, unreadable) = match bytes {
            Some(b) => match decode(&b) {
                Ok(p) => (p, false),
                Err(_) => (vec![], true),
            },
            None => (vec![], false),
        };
        Ok(Self {
            presets,
            path,
            digest,
            unreadable,
        })
    }
    pub fn has_unreadable_backup_pending(&self) -> bool {
        self.unreadable
    }
    pub fn replace(&mut self, presets: Vec<BrushPreset>) -> Result<()> {
        let data = encode(&presets)?;
        let lock = OpenOptions::new()
            .create(true)
            .truncate(false)
            .read(true)
            .write(true)
            .open(self.path.with_extension("lock"))
            .map_err(err)?;
        lock.try_lock_exclusive()
            .map_err(|_| "Fırça kütüphanesi başka bir oturumda kullanılıyor")?;
        let before = read_optional(&self.path)?;
        let digest = before.as_ref().map(|b| Sha256::digest(b).into());
        if digest != self.digest {
            return Err("Fırça kütüphanesi başka oturumda değişti. Önce yeniden açın.".into());
        }
        if self.unreadable
            && let Some(bytes) = &before
        {
            let backup = self
                .path
                .with_extension(format!("pdbrush.bak-{}", uuid::Uuid::new_v4()));
            let mut f = OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(backup)
                .map_err(err)?;
            use std::io::Write;
            f.write_all(bytes).map_err(err)?;
            f.sync_all().map_err(err)?;
        }
        project::atomic_write(&self.path, &data)?;
        self.digest = Some(Sha256::digest(data).into());
        self.presets = presets;
        self.unreadable = false;
        Ok(())
    }
}
fn read_optional(path: &Path) -> Result<Option<Vec<u8>>> {
    match fs::metadata(path) {
        Ok(m) if !m.is_file() || m.len() > MAX_FILE => {
            return Err("Normal ve en fazla 16 MiB fırça dosyası gerekli".into());
        }
        Ok(_) => {}
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(err(e)),
    }
    match std::fs::File::open(path) {
        Ok(f) => {
            let m = f.metadata().map_err(err)?;
            if !m.is_file() || m.len() > MAX_FILE {
                return Err("Normal ve en fazla 16 MiB fırça dosyası gerekli".into());
            }
            let mut b = vec![];
            f.take(MAX_FILE + 1).read_to_end(&mut b).map_err(err)?;
            if b.len() as u64 > MAX_FILE {
                return Err("Fırça dosyası 16 MiB sınırını aşıyor".into());
            }
            Ok(Some(b))
        }
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(e) => Err(err(e)),
    }
}
