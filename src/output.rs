//! Atomic raster/PDF output. Export never mutates the source document.
use crate::{project, render, types::*};
use image::{ImageEncoder, RgbImage, RgbaImage};
use std::{
    io::{Cursor, Write},
    path::{Path, PathBuf},
};
#[derive(Clone, Debug)]
pub struct RasterOptions {
    pub size: Option<Size>,
    pub quality: u8,
    pub dpi: u32,
    pub profile: Option<u8>,
}
impl Default for RasterOptions {
    fn default() -> Self {
        Self {
            size: None,
            quality: 95,
            dpi: 96,
            profile: None,
        }
    }
}
#[derive(Clone, Debug)]
pub struct PdfOptions {
    pub dpi: u32,
    pub paper: u8,
    pub landscape: bool,
    pub margin_mm: f64,
}
impl Default for PdfOptions {
    fn default() -> Self {
        Self {
            dpi: 300,
            paper: 0,
            landscape: false,
            margin_mm: 10.,
        }
    }
}
fn err(e: impl std::fmt::Display) -> String {
    e.to_string()
}
pub fn rgb_profile(index: u8) -> Result<lcms2::Profile> {
    if index == 0 {
        return Ok(lcms2::Profile::new_srgb());
    }
    if index > 2 {
        return Err("Bilinmeyen ICC profili".into());
    }
    let xy = |x, y| lcms2::CIExyY { x, y, Y: 1. };
    let white = xy(0.3127, 0.3290);
    let primaries = if index == 1 {
        lcms2::CIExyYTRIPLE {
            Red: xy(0.680, 0.320),
            Green: xy(0.265, 0.690),
            Blue: xy(0.150, 0.060),
        }
    } else {
        lcms2::CIExyYTRIPLE {
            Red: xy(0.640, 0.330),
            Green: xy(0.210, 0.710),
            Blue: xy(0.150, 0.060),
        }
    };
    let curve = if index == 1 {
        lcms2::ToneCurve::new_parametric(4, &[2.4, 1. / 1.055, 0.055 / 1.055, 1. / 12.92, 0.04045])
            .map_err(err)?
    } else {
        lcms2::ToneCurve::new(563. / 256.)
    };
    lcms2::Profile::new_rgb(&white, &primaries, &[&curve, &curve, &curve]).map_err(err)
}
fn white_background(im: &RgbaImage) -> RgbImage {
    RgbImage::from_fn(im.width(), im.height(), |x, y| {
        let p = im.get_pixel(x, y);
        let a = p[3] as u32;
        image::Rgb(std::array::from_fn(|i| {
            ((p[i] as u32 * a + 255 * (255 - a) + 127) / 255) as u8
        }))
    })
}
fn resized(im: RgbaImage, size: Option<Size>) -> Result<RgbaImage> {
    let Some(s) = size else { return Ok(im) };
    if !s.valid() {
        return Err("Çıktı boyutu geçersiz".into());
    }
    if (im.width(), im.height()) == (s.width, s.height) {
        return Ok(im);
    }
    Ok(render::resize_image(&im, s))
}
fn exif_dpi(dpi: u32) -> Vec<u8> {
    // Little-endian TIFF EXIF payload, two rational resolution fields.
    let mut b = b"II\x2a\x00\x08\x00\x00\x00".to_vec();
    b.extend(3u16.to_le_bytes());
    for (tag, typ, count, value) in [
        (0x011Au16, 5u16, 1u32, 50u32),
        (0x011B, 5, 1, 58),
        (0x0128, 3, 1, 2),
    ] {
        b.extend(tag.to_le_bytes());
        b.extend(typ.to_le_bytes());
        b.extend(count.to_le_bytes());
        b.extend(value.to_le_bytes());
    }
    b.extend(0u32.to_le_bytes());
    for _ in 0..2 {
        b.extend(dpi.to_le_bytes());
        b.extend(1u32.to_le_bytes());
    }
    b
}
pub fn export_raster(state: &State, path: &Path, options: &RasterOptions) -> Result<()> {
    crate::document::validate_state(state)?;
    write_raster(&render::composite(state), path, options)
}
pub fn write_raster(source: &RgbaImage, path: &Path, options: &RasterOptions) -> Result<()> {
    if options.quality > 100
        || !(72..=1200).contains(&options.dpi)
        || options.profile.is_some_and(|p| p > 2)
    {
        return Err("Kalite, DPI veya profil geçersiz".into());
    }
    let ext = path
        .extension()
        .and_then(|s| s.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();
    if !["png", "jpg", "jpeg", "webp", "bmp", "tif", "tiff"].contains(&ext.as_str()) {
        return Err("Çıktı biçimi desteklenmiyor".into());
    }
    if ext == "bmp" && options.profile.is_some() {
        return Err("BMP ICC profili taşımaz; profil seçimini kaldırın".into());
    }
    let mut im = resized(source.clone(), options.size)?;
    let profile = if let Some(index) = options.profile {
        let p = rgb_profile(index)?;
        project::convert_profile(&mut im, &lcms2::Profile::new_srgb(), &p)?;
        Some(p.icc().map_err(err)?)
    } else {
        None
    };
    let mut data = Vec::new();
    match ext.as_str() {
        "png" => {
            let mut info = png::Info::with_size(im.width(), im.height());
            info.color_type = png::ColorType::Rgba;
            info.bit_depth = png::BitDepth::Eight;
            info.icc_profile = profile.map(std::borrow::Cow::Owned);
            info.pixel_dims = Some(png::PixelDimensions {
                xppu: (options.dpi as f64 / 0.0254).round() as u32,
                yppu: (options.dpi as f64 / 0.0254).round() as u32,
                unit: png::Unit::Meter,
            });
            let enc = png::Encoder::with_info(&mut data, info).map_err(err)?;
            enc.write_header()
                .map_err(err)?
                .write_image_data(im.as_raw())
                .map_err(err)?;
        }
        "jpg" | "jpeg" => {
            let rgb = white_background(&im);
            let mut e =
                image::codecs::jpeg::JpegEncoder::new_with_quality(&mut data, options.quality);
            e.set_pixel_density(image::codecs::jpeg::PixelDensity::dpi(options.dpi as u16));
            if let Some(p) = profile {
                e.set_icc_profile(p).map_err(err)?
            }
            e.encode(
                rgb.as_raw(),
                im.width(),
                im.height(),
                image::ExtendedColorType::Rgb8,
            )
            .map_err(err)?;
        }
        "webp" => {
            let mut e = image::codecs::webp::WebPEncoder::new_lossless(&mut data);
            if let Some(p) = profile {
                e.set_icc_profile(p).map_err(err)?
            }
            e.set_exif_metadata(exif_dpi(options.dpi)).map_err(err)?;
            e.write_image(
                im.as_raw(),
                im.width(),
                im.height(),
                image::ExtendedColorType::Rgba8,
            )
            .map_err(err)?;
        }
        "tif" | "tiff" => {
            let mut cursor = Cursor::new(Vec::new());
            let mut t = tiff::encoder::TiffEncoder::new(&mut cursor).map_err(err)?;
            let mut e = t
                .new_image::<tiff::encoder::colortype::RGBA8>(im.width(), im.height())
                .map_err(err)?;
            e.resolution(
                tiff::tags::ResolutionUnit::Inch,
                tiff::encoder::Rational {
                    n: options.dpi,
                    d: 1,
                },
            );
            if let Some(p) = profile {
                e.encoder()
                    .write_tag(tiff::tags::Tag::IccProfile, p.as_slice())
                    .map_err(err)?;
            }
            e.write_data(im.as_raw()).map_err(err)?;
            data = cursor.into_inner();
        }
        "bmp" => {
            image::codecs::bmp::BmpEncoder::new(&mut data)
                .encode(
                    im.as_raw(),
                    im.width(),
                    im.height(),
                    image::ExtendedColorType::Rgba8,
                )
                .map_err(err)?;
        }
        _ => unreachable!(),
    }
    project::atomic_write(path, &data)
}
pub fn export_pdf(state: &State, path: &Path, o: &PdfOptions) -> Result<()> {
    crate::document::validate_state(state)?;
    if !path
        .extension()
        .is_some_and(|s| s.eq_ignore_ascii_case("pdf"))
    {
        return Err("PDF uzantısı gerekli".into());
    }
    if !(72..=600).contains(&o.dpi)
        || o.paper > 2
        || !o.margin_mm.is_finite()
        || !(0.0..=50.0).contains(&o.margin_mm)
    {
        return Err("PDF sayfa ayarları geçersiz".into());
    }
    let im = white_background(&render::composite(state));
    if o.paper == 2
        && [im.width(), im.height()]
            .iter()
            .any(|n| !(1.0..=2000.0).contains(&(*n as f64 * 25.4 / o.dpi as f64)))
    {
        return Err("PDF fiziksel boyutu 1–2000 mm sınırında olmalı".into());
    }
    let (mut w, mut h) = match o.paper {
        0 => (595.2756, 841.8898),
        1 => (612., 792.),
        _ => (
            im.width() as f64 / o.dpi as f64 * 72.,
            im.height() as f64 / o.dpi as f64 * 72.,
        ),
    };
    if (o.landscape && w < h) || (!o.landscape && w > h) {
        std::mem::swap(&mut w, &mut h)
    }
    let margin = o.margin_mm / 25.4 * 72.;
    if w <= 2. * margin || h <= 2. * margin {
        return Err("Kenar boşluğu sayfaya sığmıyor".into());
    }
    let scale = ((w - 2. * margin) / im.width() as f64).min((h - 2. * margin) / im.height() as f64);
    let iw = im.width() as f64 * scale;
    let ih = im.height() as f64 * scale;
    let mut zip = flate2::write::ZlibEncoder::new(Vec::new(), flate2::Compression::default());
    zip.write_all(im.as_raw()).map_err(err)?;
    let pixels = zip.finish().map_err(err)?;
    let content = format!(
        "q {iw:0.6} 0 0 {ih:0.6} {:0.6} {:0.6} cm /Im0 Do Q\n",
        (w - iw) / 2.,
        (h - ih) / 2.
    );
    let mut objects:Vec<Vec<u8>>=vec![b"<< /Type /Catalog /Pages 2 0 R >>".to_vec(),b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>".to_vec(),format!("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {w:0.6} {h:0.6}] /Resources << /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>").into_bytes()];
    let mut stream=format!("<< /Type /XObject /Subtype /Image /Width {} /Height {} /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode /Length {} >>\nstream\n",im.width(),im.height(),pixels.len()).into_bytes();
    stream.extend(pixels);
    stream.extend(b"\nendstream");
    objects.push(stream);
    objects.push(
        format!(
            "<< /Length {} >>\nstream\n{}endstream",
            content.len(),
            content
        )
        .into_bytes(),
    );
    let mut pdf = b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n".to_vec();
    let mut offsets = vec![0usize];
    for (i, obj) in objects.iter().enumerate() {
        offsets.push(pdf.len());
        pdf.extend(format!("{} 0 obj\n", i + 1).as_bytes());
        pdf.extend(obj);
        pdf.extend(b"\nendobj\n");
    }
    let xref = pdf.len();
    pdf.extend(format!("xref\n0 {}\n0000000000 65535 f \n", offsets.len()).as_bytes());
    for o in offsets.iter().skip(1) {
        pdf.extend(format!("{o:010} 00000 n \n").as_bytes())
    }
    pdf.extend(
        format!(
            "trailer\n<< /Size {} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n",
            offsets.len()
        )
        .as_bytes(),
    );
    project::atomic_write(path, &pdf)
}
#[allow(clippy::too_many_arguments)]
pub fn batch(
    files: &[PathBuf],
    destination: &Path,
    size: Option<Size>,
    allow_upscale: bool,
    brightness: i32,
    contrast: i32,
    saturation: i32,
    format: &str,
    quality: u8,
) -> Vec<(PathBuf, Result<PathBuf>)> {
    files
        .iter()
        .map(|source| {
            let result = (|| {
                let pixels = project::read_image(source)?;
                let original = Size::new(pixels.width(), pixels.height());
                let mut layer = Layer::new("Görüntü", pixels);
                layer.brightness = brightness;
                layer.contrast = contrast;
                layer.saturation = saturation;
                let state = State {
                    size: original,
                    layers: vec![layer],
                    active: 0,
                    selection: Selection::None,
                    previous_selection: Selection::None,
                    saved_selections: vec![],
                };
                crate::document::validate_state(&state)?;
                let target = size.map(|s| {
                    let mut scale = (s.width as f64 / original.width as f64)
                        .min(s.height as f64 / original.height as f64);
                    if !allow_upscale {
                        scale = scale.min(1.)
                    }
                    Size::new(
                        (original.width as f64 * scale).round().max(1.) as u32,
                        (original.height as f64 * scale).round().max(1.) as u32,
                    )
                });
                if !["png", "jpg", "jpeg", "webp", "bmp", "tif", "tiff"].contains(&format) {
                    return Err("Toplu çıktı biçimi geçersiz".into());
                }
                let stem = source.file_stem().unwrap_or_default().to_string_lossy();
                let options = RasterOptions {
                    size: target,
                    quality,
                    ..Default::default()
                };
                let im = render::composite(&state);
                let temp = tempfile::Builder::new()
                    .suffix(&format!(".{format}"))
                    .tempfile_in(destination)
                    .map_err(err)?;
                write_raster(&im, temp.path(), &options)?;
                let temp = temp.into_temp_path();
                let mut n = 0;
                loop {
                    let suffix = if n == 0 {
                        String::new()
                    } else {
                        format!("_{n}")
                    };
                    let path = destination.join(format!("{stem}_photodeck{suffix}.{format}"));
                    match std::fs::hard_link(&temp, &path) {
                        Ok(()) => return Ok(path),
                        Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => {
                            n += 1;
                        }
                        Err(e) => return Err(err(e)),
                    }
                }
            })();
            (source.clone(), result)
        })
        .collect()
}
