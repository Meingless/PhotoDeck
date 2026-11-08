//! Bounded, Qt-compatible .pdeck v1–v7 codec and atomic, conflict-aware file I/O.
use crate::{document::Document, types::*};
use image::{DynamicImage, ImageDecoder, ImageFormat, ImageReader, RgbaImage};
use sha2::{Digest, Sha256};
use std::{
    fs::{self, File},
    io::{Cursor, Read, Write},
    path::{Path, PathBuf},
    sync::Arc,
    time::SystemTime,
};

pub const MAX_FILE_BYTES: u64 = 384 * 1024 * 1024;
#[derive(Clone, Debug, PartialEq, Eq)]
struct Fingerprint {
    absolute: PathBuf,
    resolved: PathBuf,
    len: u64,
    modified: Option<SystemTime>,
    identity: Option<(u64, u64)>,
    digest: [u8; 32],
    exists: bool,
}
#[derive(Clone, Debug)]
pub struct ProjectFile {
    pub path: PathBuf,
    pub version: u32,
    fingerprint: Fingerprint,
}
#[derive(Clone, Copy, Debug, Default)]
pub struct SaveOptions {
    pub overwrite_external: bool,
    pub upgrade_format: bool,
}
#[derive(Debug, PartialEq, Eq)]
pub enum SaveError {
    ExternalModification,
    FormatUpgradeRequired,
    Other(String),
}
impl std::fmt::Display for SaveError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::ExternalModification => write!(
                f,
                "Dosya dışarıdan değiştirilmiş veya silinmiş. Bir kopya kaydedin ya da açıkça üzerine yazmayı seçin."
            ),
            Self::FormatUpgradeRequired => write!(
                f,
                "Bu kayıt daha yeni proje biçimi gerektiriyor. Eski özgünü korumak için farklı adla kaydedin."
            ),
            Self::Other(s) => write!(f, "{s}"),
        }
    }
}
impl From<String> for SaveError {
    fn from(s: String) -> Self {
        Self::Other(s)
    }
}
fn err(e: impl std::fmt::Display) -> String {
    e.to_string()
}
fn identity(file: &File) -> Result<Option<(u64, u64)>> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        let m = file.metadata().map_err(err)?;
        Ok(Some((m.dev(), m.ino())))
    }
    #[cfg(windows)]
    {
        use std::os::windows::io::AsRawHandle;
        use windows_sys::Win32::Storage::FileSystem::{
            BY_HANDLE_FILE_INFORMATION, GetFileInformationByHandle,
        };
        let mut info = std::mem::MaybeUninit::<BY_HANDLE_FILE_INFORMATION>::uninit();
        // The live File owns this handle throughout the call; the API initializes
        // the output only on success. No handle ownership is transferred.
        if unsafe { GetFileInformationByHandle(file.as_raw_handle(), info.as_mut_ptr()) } == 0 {
            return Err(std::io::Error::last_os_error().to_string());
        }
        let info = unsafe { info.assume_init() };
        Ok(Some((
            u64::from(info.dwVolumeSerialNumber),
            (u64::from(info.nFileIndexHigh) << 32) | u64::from(info.nFileIndexLow),
        )))
    }
    #[cfg(not(any(unix, windows)))]
    {
        let _ = file;
        Ok(None)
    }
}
fn absolute(path: &Path) -> Result<PathBuf> {
    let p = std::path::absolute(path).map_err(err)?;
    Ok(p)
}
fn fingerprint(path: &Path) -> Result<Fingerprint> {
    let absolute = absolute(path)?;
    if let Ok(metadata) = fs::metadata(&absolute)
        && (!metadata.is_file() || metadata.len() > MAX_FILE_BYTES)
    {
        return Err("Dosya türü veya boyutu geçersiz".into());
    }
    let resolved = if absolute.exists() {
        absolute.canonicalize().map_err(err)?
    } else {
        absolute
            .parent()
            .unwrap_or(Path::new("."))
            .canonicalize()
            .map_err(err)?
            .join(absolute.file_name().ok_or("Dosya adı yok")?)
    };
    let mut f = match File::open(&absolute) {
        Ok(f) => f,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
            return Ok(Fingerprint {
                absolute,
                resolved,
                len: 0,
                modified: None,
                identity: None,
                digest: [0; 32],
                exists: false,
            });
        }
        Err(e) => return Err(err(e)),
    };
    let m = f.metadata().map_err(err)?;
    let original_identity = identity(&f)?;
    if !m.is_file() || m.len() > MAX_FILE_BYTES {
        return Err("Dosya türü veya boyutu geçersiz".into());
    }
    let mut hash = Sha256::new();
    let mut buf = [0; 65536];
    loop {
        let n = f.read(&mut buf).map_err(err)?;
        if n == 0 {
            break;
        }
        hash.update(&buf[..n]);
    }
    let after = f.metadata().map_err(err)?;
    let current_file = File::open(&absolute).map_err(err)?;
    let current = current_file.metadata().map_err(err)?;
    if m.len() != after.len()
        || m.modified().ok() != after.modified().ok()
        || original_identity != identity(&current_file)?
        || m.len() != current.len()
        || m.modified().ok() != current.modified().ok()
        || absolute.canonicalize().map_err(err)? != resolved
    {
        return Err("Dosya okunurken değişti; yeniden deneyin.".into());
    }
    Ok(Fingerprint {
        absolute,
        resolved,
        len: m.len(),
        modified: m.modified().ok(),
        identity: original_identity,
        digest: hash.finalize().into(),
        exists: true,
    })
}
fn same_target(a: &Fingerprint, b: &Fingerprint) -> bool {
    same_path(&a.absolute, &b.absolute)
        || same_path(&a.resolved, &b.resolved)
        || (a.identity.is_some() && a.identity == b.identity)
}
fn same_path(a: &Path, b: &Path) -> bool {
    #[cfg(windows)]
    {
        a.to_string_lossy().to_lowercase() == b.to_string_lossy().to_lowercase()
    }
    #[cfg(not(windows))]
    {
        a == b
    }
}
pub fn atomic_write(path: &Path, bytes: &[u8]) -> Result<()> {
    let target = if path.is_symlink() {
        path.canonicalize().map_err(err)?
    } else {
        path.to_owned()
    };
    let parent = target
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    let mut temp = tempfile::NamedTempFile::new_in(parent).map_err(err)?;
    if let Ok(m) = fs::metadata(&target) {
        if !m.is_file() {
            return Err("Hedef normal dosya değil".into());
        }
        temp.as_file()
            .set_permissions(m.permissions())
            .map_err(err)?;
    }
    temp.write_all(bytes).map_err(err)?;
    temp.as_file().sync_all().map_err(err)?;
    temp.persist(&target).map_err(err)?;
    #[cfg(unix)]
    File::open(parent).and_then(|f| f.sync_all()).map_err(err)?;
    Ok(())
}
pub fn open(path: &Path) -> Result<(Document, ProjectFile)> {
    let before = fingerprint(path)?;
    if !before.exists {
        return Err("Dosya bulunamadı".into());
    }
    let mut bytes = Vec::new();
    File::open(path)
        .map_err(err)?
        .take(MAX_FILE_BYTES + 1)
        .read_to_end(&mut bytes)
        .map_err(err)?;
    let (state, version) = decode_state(&bytes)?;
    if before != fingerprint(path)? {
        return Err("Proje açılırken değişti".into());
    }
    let mut doc = Document::from_state(state)?;
    doc.title = path
        .file_name()
        .unwrap_or_default()
        .to_string_lossy()
        .into();
    doc.mark_saved();
    Ok((
        doc,
        ProjectFile {
            path: path.to_owned(),
            version,
            fingerprint: before,
        },
    ))
}
pub fn save(
    doc: &mut Document,
    path: &Path,
    source: Option<&ProjectFile>,
    options: SaveOptions,
) -> std::result::Result<ProjectFile, SaveError> {
    if !path
        .extension()
        .is_some_and(|x| x.eq_ignore_ascii_case("pdeck"))
    {
        return Err(SaveError::Other("Proje uzantısı .pdeck olmalı".into()));
    }
    if doc.is_editing() {
        return Err(SaveError::Other("Önce etkin düzenlemeyi tamamlayın".into()));
    }
    let before = fingerprint(path)?;
    let version = project_version(&doc.state, false);
    if let Some(s) = source.filter(|s| same_target(&s.fingerprint, &before)) {
        if s.fingerprint != before && !options.overwrite_external {
            return Err(SaveError::ExternalModification);
        }
        if (s.version < 5 || s.version < version) && !options.upgrade_format {
            return Err(SaveError::FormatUpgradeRequired);
        }
    }
    let bytes = encode_state(&doc.state, false)?;
    if before != fingerprint(path)? {
        return Err(SaveError::ExternalModification);
    }
    atomic_write(path, &bytes)?;
    let fingerprint = fingerprint(path)?;
    if fingerprint.len != bytes.len() as u64
        || fingerprint.digest != <[u8; 32]>::from(Sha256::digest(&bytes))
    {
        return Err(SaveError::ExternalModification);
    }
    doc.mark_saved();
    doc.title = path
        .file_name()
        .unwrap_or_default()
        .to_string_lossy()
        .into();
    Ok(ProjectFile {
        path: path.to_owned(),
        version,
        fingerprint,
    })
}
pub fn project_version(state: &State, recovery: bool) -> u32 {
    if state.layers.iter().any(Layer::is_smart_object) {
        7
    } else if recovery || !state.saved_selections.is_empty() {
        6
    } else {
        5
    }
}

/// Decode through an explicitly bounded decoder, then normalize RGB profiles to sRGB.
pub fn read_image(path: &Path) -> Result<RgbaImage> {
    let metadata = fs::metadata(path).map_err(err)?;
    if !metadata.is_file() || metadata.len() > MAX_FILE_BYTES {
        return Err("Normal ve en fazla 384 MiB görüntü dosyası gerekli".into());
    }
    let f = File::open(path).map_err(err)?;
    if !f.metadata().map_err(err)?.is_file() {
        return Err("Normal görüntü dosyası gerekli".into());
    }
    let reader = ImageReader::new(std::io::BufReader::new(f))
        .with_guessed_format()
        .map_err(err)?;
    decode_image(reader, MAX_LAYER_BYTES)
}
fn decode_image<R: std::io::BufRead + std::io::Seek>(
    mut reader: ImageReader<R>,
    budget: u64,
) -> Result<RgbaImage> {
    // image's generic TIFF decoder can omit its ICC tag; read bounded metadata
    // with the owning TIFF decoder before pixel decoding, preserving the profile.
    let tiff_profile = if reader.format() == Some(ImageFormat::Tiff) {
        let mut input = reader.into_inner();
        let mut limits = tiff::decoder::Limits::default();
        limits.ifd_value_size = 4 * 1024 * 1024;
        let mut decoder = tiff::decoder::Decoder::new(&mut input)
            .map_err(err)?
            .with_limits(limits);
        let profile = decoder
            .find_tag(tiff::tags::Tag::IccProfile)
            .map_err(err)?
            .map(|value| value.into_u8_vec().map_err(err))
            .transpose()?;
        drop(decoder);
        input.rewind().map_err(err)?;
        reader = ImageReader::with_format(input, ImageFormat::Tiff);
        profile
    } else {
        None
    };
    let mut limits = image::Limits::default();
    limits.max_image_width = Some(16384);
    limits.max_image_height = Some(16384);
    limits.max_alloc = Some(budget.min(128 * 1024 * 1024));
    reader.limits(limits);
    let mut decoder = reader.into_decoder().map_err(err)?;
    let (w, h) = decoder.dimensions();
    if !Size::new(w, h).valid() || w as u64 * h as u64 * 4 > budget {
        return Err("Görüntü boyut/bellek sınırını aşıyor".into());
    }
    let profile = tiff_profile.or(decoder.icc_profile().map_err(err)?);
    let orientation = decoder.orientation().map_err(err)?;
    let mut im = DynamicImage::from_decoder(decoder).map_err(err)?;
    im.apply_orientation(orientation);
    let mut rgba = im.to_rgba8();
    if let Some(bytes) = profile.filter(|b| !b.is_empty()) {
        let input = lcms2::Profile::new_icc(&bytes).map_err(err)?;
        if input.color_space() != lcms2::ColorSpaceSignature::RgbData {
            return Err("Yalnız RGB ICC profilleri destekleniyor".into());
        }
        convert_profile(&mut rgba, &input, &lcms2::Profile::new_srgb())?;
    }
    Ok(rgba)
}
pub fn convert_profile(
    image: &mut RgbaImage,
    from: &lcms2::Profile,
    to: &lcms2::Profile,
) -> Result<()> {
    let t = lcms2::Transform::<u8, u8>::new_flags(
        from,
        lcms2::PixelFormat::RGBA_8,
        to,
        lcms2::PixelFormat::RGBA_8,
        lcms2::Intent::Perceptual,
        lcms2::Flags::COPY_ALPHA,
    )
    .map_err(err)?;
    t.transform_in_place(image.as_mut());
    Ok(())
}

struct Reader<'a> {
    data: &'a [u8],
    at: usize,
}
impl<'a> Reader<'a> {
    fn take(&mut self, n: usize) -> Result<&'a [u8]> {
        let end = self.at.checked_add(n).ok_or("Taşan uzunluk")?;
        let b = self
            .data
            .get(self.at..end)
            .ok_or("Eksik veya bozuk proje")?;
        self.at = end;
        Ok(b)
    }
    fn u8(&mut self) -> Result<u8> {
        Ok(self.take(1)?[0])
    }
    fn bool(&mut self) -> Result<bool> {
        match self.u8()? {
            0 => Ok(false),
            1 => Ok(true),
            _ => Err("Geçersiz bayrak".into()),
        }
    }
    fn u16(&mut self) -> Result<u16> {
        Ok(u16::from_be_bytes(self.take(2)?.try_into().unwrap()))
    }
    fn u32(&mut self) -> Result<u32> {
        Ok(u32::from_be_bytes(self.take(4)?.try_into().unwrap()))
    }
    fn i32(&mut self) -> Result<i32> {
        Ok(self.u32()? as i32)
    }
    fn f64(&mut self) -> Result<f64> {
        Ok(f64::from_be_bytes(self.take(8)?.try_into().unwrap()))
    }
    fn point(&mut self) -> Result<Point> {
        Ok(Point::new(self.i32()?, self.i32()?))
    }
    fn size(&mut self) -> Result<Size> {
        Ok(Size::new(self.u32()?, self.u32()?))
    }
    fn string(&mut self, max: usize) -> Result<String> {
        let n = self.u32()?;
        if n == u32::MAX {
            return Ok(String::new());
        }
        let n = n as usize;
        if n > max || !n.is_multiple_of(2) {
            return Err("Geçersiz metin uzunluğu".into());
        }
        let raw = self.take(n)?;
        let v: Vec<u16> = raw
            .as_chunks::<2>()
            .0
            .iter()
            .map(|b| u16::from_be_bytes(*b))
            .collect();
        String::from_utf16(&v).map_err(err)
    }
    fn color(&mut self) -> Result<[u8; 4]> {
        let spec = self.u8()?;
        let a = self.u16()?;
        let r = self.u16()?;
        let g = self.u16()?;
        let b = self.u16()?;
        let pad = self.u16()?;
        match spec {
            0 => Ok([0, 0, 0, 255]),
            1 => Ok([
                (r / 257) as u8,
                (g / 257) as u8,
                (b / 257) as u8,
                (a / 257) as u8,
            ]),
            2 | 4 => {
                let h = r as f64 / 100.;
                let s = g as f64 / 65535.;
                let v = b as f64 / 65535.;
                let c = if spec == 2 {
                    v * s
                } else {
                    (1. - (2. * v - 1.).abs()) * s
                };
                let x = c * (1. - ((h / 60.) % 2. - 1.).abs());
                let m = if spec == 2 { v - c } else { v - c / 2. };
                let p = match (h / 60.) as i32 {
                    0 => [c, x, 0.],
                    1 => [x, c, 0.],
                    2 => [0., c, x],
                    3 => [0., x, c],
                    4 => [x, 0., c],
                    _ => [c, 0., x],
                };
                Ok([
                    ((p[0] + m) * 255.).round() as u8,
                    ((p[1] + m) * 255.).round() as u8,
                    ((p[2] + m) * 255.).round() as u8,
                    (a / 257) as u8,
                ])
            }
            3 => {
                let k = pad as f64 / 65535.;
                Ok([
                    ((1. - r as f64 / 65535.) * (1. - k) * 255.).round() as u8,
                    ((1. - g as f64 / 65535.) * (1. - k) * 255.).round() as u8,
                    ((1. - b as f64 / 65535.) * (1. - k) * 255.).round() as u8,
                    (a / 257) as u8,
                ])
            }
            _ => Err("Desteklenmeyen Qt renk kodu".into()),
        }
    }
    fn image(&mut self, remaining: &mut u64) -> Result<Option<RgbaImage>> {
        let n = self.u32()? as usize;
        if n == 0 {
            return Ok(None);
        }
        if n > 80 * 1024 * 1024 {
            return Err("Görüntü kaydı çok büyük".into());
        }
        let data = self.take(n)?;
        if !data.starts_with(b"\x89PNG\r\n\x1a\n") {
            return Err("Proje görüntüsü PNG değil".into());
        }
        let im = decode_image(
            ImageReader::with_format(Cursor::new(data), ImageFormat::Png),
            *remaining,
        )?;
        *remaining -= im.width() as u64 * im.height() as u64 * 4;
        Ok(Some(im))
    }
    fn selection(&mut self, size: Size, budget: &mut u64) -> Result<Selection> {
        match self.u8()? {
            0 => Ok(Selection::None),
            1 => {
                let a = self.point()?;
                let b = self.point()?;
                if a.x < 0
                    || a.y < 0
                    || b.x < a.x
                    || b.y < a.y
                    || b.x as u32 >= size.width
                    || b.y as u32 >= size.height
                {
                    return Err("Seçim sınırı geçersiz".into());
                }
                Ok(Selection::Rectangle(Rect::new(
                    a.x,
                    a.y,
                    (b.x - a.x + 1) as u32,
                    (b.y - a.y + 1) as u32,
                )))
            }
            2 => {
                if self.size()? != size {
                    return Err("Seçim boyutu geçersiz".into());
                }
                let n = self.u32()? as u64;
                let memory = (size.width as u64).div_ceil(4) * 4 * size.height as u64;
                if n != size.width as u64 * size.height as u64 || memory > *budget {
                    return Err("Seçim bellek sınırı".into());
                }
                *budget -= memory;
                let data = self.take(n as usize)?.to_vec();
                Ok(Selection::Mask(Arc::new(
                    image::GrayImage::from_raw(size.width, size.height, data)
                        .ok_or("Bozuk seçim")?,
                )))
            }
            _ => Err("Seçim türü geçersiz".into()),
        }
    }
    fn tones(&mut self) -> Result<ToneSettings> {
        let mut t = ToneSettings {
            input_black: self.i32()?,
            input_white: self.i32()?,
            output_black: self.i32()?,
            output_white: self.i32()?,
            gamma: self.f64()?,
            exposure: self.f64()?,
            hue: self.i32()?,
            vibrance: self.i32()?,
            red_balance: self.i32()?,
            green_balance: self.i32()?,
            blue_balance: self.i32()?,
            inverted: self.bool()?,
            threshold: self.i32()?,
            posterize: self.i32()?,
            ..Default::default()
        };
        for curve in &mut t.curves {
            let n = self.u32()?;
            if !(2..=64).contains(&n) {
                return Err("Eğri uzunluğu geçersiz".into());
            }
            curve.clear();
            for _ in 0..n {
                curve.push(self.point()?);
            }
        }
        if !t.valid() {
            return Err("Ton parametreleri geçersiz".into());
        }
        Ok(t)
    }
}
pub fn decode_state(bytes: &[u8]) -> Result<(State, u32)> {
    if bytes.len() as u64 > MAX_FILE_BYTES {
        return Err("Proje dosyası çok büyük".into());
    }
    let mut r = Reader { data: bytes, at: 0 };
    if r.u32()? != 0x50444543 {
        return Err("PhotoDeck projesi değil".into());
    }
    let version = r.u32()?;
    if !(1..=7).contains(&version) {
        return Err("Proje sürümü desteklenmiyor".into());
    }
    let size = r.size()?;
    let active = r.u32()? as usize;
    let count = r.u32()? as usize;
    if !size.valid() || !(1..=64).contains(&count) || active >= count {
        return Err("Belge boyutu/katman sayısı geçersiz".into());
    }
    let mut remaining = MAX_LAYER_BYTES;
    let mut layers = Vec::with_capacity(count);
    for _ in 0..count {
        let name = r.string(32768)?;
        let mut l = Layer::new(name, RgbaImage::new(0, 0));
        l.offset = r.point()?;
        l.visible = r.bool()?;
        l.locked = r.bool()?;
        l.opacity = r.f64()?;
        let blend = r.i32()?;
        if !(0..if version == 1 { 6 } else { 27 }).contains(&blend) {
            return Err("Harmanlama modu geçersiz".into());
        }
        l.blend = blend as u8;
        l.brightness = r.i32()?;
        l.contrast = r.i32()?;
        l.saturation = r.i32()?;
        l.blur = r.i32()?;
        l.text = r.string(32768)?;
        l.font = r.string(32768)?;
        l.color = r.color()?;
        if version >= 2 {
            l.tones = r.tones()?
        }
        if version >= 3 {
            for m in &mut l.geometry.0 {
                *m = r.f64()?;
            }
        }
        if version >= 4 {
            l.clipped = r.bool()?
        }
        if version >= 5 {
            l.filters = FilterSettings {
                gaussian: r.i32()?,
                sharpen_amount: r.i32()?,
                sharpen_radius: r.i32()?,
                sharpen_threshold: r.i32()?,
                high_pass: r.i32()?,
                noise: r.i32()?,
                mosaic: r.i32()?,
                monochrome_noise: r.bool()?,
                noise_seed: r.u32()?,
            };
            l.mask_settings = MaskSettings {
                enabled: r.bool()?,
                density: r.i32()?,
                feather: r.i32()?,
            };
        }
        if version >= 7 {
            l.smart_object_id.copy_from_slice(r.take(16)?);
            let frame = r.size()?;
            l.smart_object_size = if frame.width == u32::MAX && frame.height == u32::MAX {
                Size::default()
            } else {
                frame
            };
        }
        l.pixels = Arc::new(r.image(&mut remaining)?.ok_or("Katman pikselleri eksik")?);
        l.mask = r.image(&mut remaining)?.map(|im| {
            Arc::new(image::GrayImage::from_fn(
                im.width(),
                im.height(),
                |x, y| {
                    let p = im.get_pixel(x, y);
                    image::Luma([((p[0] as u16 * p[3] as u16 + 127) / 255) as u8])
                },
            ))
        });
        layers.push(l);
    }
    let mut state = State {
        size,
        layers,
        active,
        selection: Selection::None,
        previous_selection: Selection::None,
        saved_selections: Vec::new(),
    };
    if version >= 6 {
        let context = r.bool()?;
        let n = r.u32()?;
        if n > 16 {
            return Err("Çok fazla kayıtlı seçim".into());
        }
        let mut budget = MAX_SELECTION_BYTES;
        for _ in 0..n {
            let name = r.string(256)?;
            state.saved_selections.push(SavedSelection {
                name,
                selection: r.selection(size, &mut budget)?,
            });
        }
        if context {
            state.selection = r.selection(size, &mut budget)?;
            state.previous_selection = r.selection(size, &mut budget)?;
        }
    }
    if r.at != bytes.len() {
        return Err("Proje sonunda beklenmeyen veri".into());
    }
    crate::document::validate_state(&state)?;
    Ok((state, version))
}
#[derive(Default)]
struct Writer(Vec<u8>);
impl Writer {
    fn u8(&mut self, v: u8) {
        self.0.push(v)
    }
    fn bool(&mut self, v: bool) {
        self.u8(u8::from(v))
    }
    fn u16(&mut self, v: u16) {
        self.0.extend(v.to_be_bytes())
    }
    fn u32(&mut self, v: u32) {
        self.0.extend(v.to_be_bytes())
    }
    fn i32(&mut self, v: i32) {
        self.u32(v as u32)
    }
    fn f64(&mut self, v: f64) {
        self.0.extend(v.to_be_bytes())
    }
    fn point(&mut self, p: Point) {
        self.i32(p.x);
        self.i32(p.y)
    }
    fn size(&mut self, s: Size) {
        self.u32(s.width);
        self.u32(s.height)
    }
    fn string(&mut self, s: &str) {
        let u: Vec<u16> = s.encode_utf16().collect();
        self.u32((u.len() * 2) as u32);
        for c in u {
            self.u16(c)
        }
    }
    fn color(&mut self, c: [u8; 4]) {
        self.u8(1);
        for v in [c[3], c[0], c[1], c[2], 0] {
            self.u16(v as u16 * 257)
        }
    }
    fn image(&mut self, im: Option<&RgbaImage>) -> Result<()> {
        if let Some(im) = im {
            let mut buf = Cursor::new(Vec::new());
            im.write_to(&mut buf, ImageFormat::Png).map_err(err)?;
            let data = buf.into_inner();
            if data.len() > 80 * 1024 * 1024 {
                return Err("Kodlanan görüntü çok büyük".into());
            }
            self.u32(data.len() as u32);
            self.0.extend(data);
        } else {
            self.u32(0)
        }
        Ok(())
    }
    fn selection(&mut self, s: &Selection) {
        match s {
            Selection::None => self.u8(0),
            Selection::Rectangle(r) => {
                self.u8(1);
                self.point(Point::new(r.x, r.y));
                self.point(Point::new(
                    r.x + r.width as i32 - 1,
                    r.y + r.height as i32 - 1,
                ));
            }
            Selection::Mask(m) => {
                self.u8(2);
                self.size(Size::new(m.width(), m.height()));
                self.u32(m.as_raw().len() as u32);
                self.0.extend(m.as_raw());
            }
        }
    }
    fn tones(&mut self, t: &ToneSettings) {
        for v in [t.input_black, t.input_white, t.output_black, t.output_white] {
            self.i32(v)
        }
        self.f64(t.gamma);
        self.f64(t.exposure);
        for v in [
            t.hue,
            t.vibrance,
            t.red_balance,
            t.green_balance,
            t.blue_balance,
        ] {
            self.i32(v)
        }
        self.bool(t.inverted);
        self.i32(t.threshold);
        self.i32(t.posterize);
        for c in &t.curves {
            self.u32(c.len() as u32);
            for p in c {
                self.point(*p)
            }
        }
    }
}
pub fn encode_state(state: &State, recovery: bool) -> Result<Vec<u8>> {
    crate::document::validate_state(state)?;
    let version = project_version(state, recovery);
    let mut w = Writer::default();
    w.u32(0x50444543);
    w.u32(version);
    w.size(state.size);
    w.u32(state.active as u32);
    w.u32(state.layers.len() as u32);
    for l in &state.layers {
        w.string(&l.name);
        w.point(l.offset);
        w.bool(l.visible);
        w.bool(l.locked);
        w.f64(l.opacity);
        w.i32(l.blend as i32);
        for v in [l.brightness, l.contrast, l.saturation, l.blur] {
            w.i32(v)
        }
        w.string(&l.text);
        w.string(&l.font);
        w.color(l.color);
        w.tones(&l.tones);
        for m in l.geometry.0 {
            w.f64(m)
        }
        w.bool(l.clipped);
        let f = &l.filters;
        for v in [
            f.gaussian,
            f.sharpen_amount,
            f.sharpen_radius,
            f.sharpen_threshold,
            f.high_pass,
            f.noise,
            f.mosaic,
        ] {
            w.i32(v)
        }
        w.bool(f.monochrome_noise);
        w.u32(f.noise_seed);
        w.bool(l.mask_settings.enabled);
        w.i32(l.mask_settings.density);
        w.i32(l.mask_settings.feather);
        if version >= 7 {
            w.0.extend(l.smart_object_id);
            if l.is_smart_object() {
                w.size(l.smart_object_size)
            } else {
                w.size(Size::new(u32::MAX, u32::MAX));
            }
        }
        w.image(Some(&l.pixels))?;
        if let Some(m) = &l.mask {
            let rgba = RgbaImage::from_fn(m.width(), m.height(), |x, y| {
                let v = m.get_pixel(x, y)[0];
                image::Rgba([v, v, v, 255])
            });
            w.image(Some(&rgba))?;
        } else {
            w.image(None)?
        }
    }
    if version >= 6 {
        w.bool(recovery);
        w.u32(state.saved_selections.len() as u32);
        for s in &state.saved_selections {
            w.string(&s.name);
            w.selection(&s.selection)
        }
        if recovery {
            w.selection(&state.selection);
            w.selection(&state.previous_selection);
        }
    }
    if w.0.len() as u64 > MAX_FILE_BYTES {
        return Err("Kodlanan proje çok büyük".into());
    }
    Ok(w.0)
}
