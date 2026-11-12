//! System-font text shaping and rasterization without a Qt or browser runtime.
use crate::types::{Result, Size};
use cosmic_text::{
    Attrs, Buffer, Color, Family, FontSystem, Metrics, Shaping, Style, SwashCache, Weight,
};
use image::RgbaImage;
use std::sync::{Mutex, OnceLock};
static FONTS: OnceLock<Mutex<FontSystem>> = OnceLock::new();
pub fn render_text(value: &str, font: &str, color: [u8; 4]) -> Result<RgbaImage> {
    if value.trim().is_empty() || value.encode_utf16().count() > 16384 {
        return Err("Metin boş veya çok uzun.".into());
    }
    let parts: Vec<_> = font.split(',').collect();
    let family = parts.first().copied().unwrap_or("Sans Serif");
    let points = parts
        .get(1)
        .and_then(|s| s.parse::<f32>().ok())
        .unwrap_or(32.);
    if !points.is_finite() || !(1. ..=512.).contains(&points) {
        return Err("Yazı boyutu 1–512 pt olmalı.".into());
    }
    let px = points * 96. / 72.;
    let weight = parts
        .get(4)
        .and_then(|s| s.parse::<u16>().ok())
        .unwrap_or(400);
    let italic = parts.get(5).is_some_and(|s| *s == "1");
    let mut fonts = FONTS
        .get_or_init(|| Mutex::new(FontSystem::new()))
        .lock()
        .map_err(|_| "Yazı sistemi kilidi açılamadı.")?;
    let mut buffer = Buffer::new(&mut fonts, Metrics::new(px, px * 1.25));
    buffer.set_size(None, None);
    let attrs = Attrs::new()
        .family(if family == "Sans Serif" {
            Family::SansSerif
        } else {
            Family::Name(family)
        })
        .weight(Weight(weight))
        .style(if italic { Style::Italic } else { Style::Normal });
    buffer.set_text(value, &attrs, Shaping::Advanced, None);
    buffer.shape_until_scroll(&mut fonts, false);
    let mut cache = SwashCache::new();
    let mut minx = i32::MAX;
    let mut miny = i32::MAX;
    let mut maxx = i32::MIN;
    let mut maxy = i32::MIN;
    let rgba = Color::rgba(color[0], color[1], color[2], color[3]);
    buffer.draw(&mut fonts, &mut cache, rgba, |x, y, w, h, c| {
        if c.a() > 0 {
            minx = minx.min(x);
            miny = miny.min(y);
            maxx = maxx.max(x.saturating_add(w as i32));
            maxy = maxy.max(y.saturating_add(h as i32));
        }
    });
    if minx == i32::MAX {
        return Ok(RgbaImage::new(1, 1));
    }
    let size = Size::new((maxx - minx) as u32, (maxy - miny) as u32);
    if !size.valid() {
        return Err("Metin görüntüsü güvenli boyut sınırını aşıyor.".into());
    }
    let mut image = RgbaImage::new(size.width, size.height);
    buffer.draw(&mut fonts, &mut cache, rgba, |x, y, w, h, c| {
        for dy in 0..h {
            for dx in 0..w {
                let xx = x - minx + dx as i32;
                let yy = y - miny + dy as i32;
                if xx >= 0 && yy >= 0 && (xx as u32) < size.width && (yy as u32) < size.height {
                    let dest = image.get_pixel_mut(xx as u32, yy as u32);
                    let a = c.a() as f64 / 255.;
                    let ab = dest[3] as f64 / 255.;
                    let oa = a + ab * (1. - a);
                    if oa > 0. {
                        let top = [c.r(), c.g(), c.b()];
                        for i in 0..3 {
                            dest[i] = ((top[i] as f64 * a + dest[i] as f64 * ab * (1. - a)) / oa)
                                .round() as u8;
                        }
                        dest[3] = (oa * 255.).round() as u8;
                    }
                }
            }
        }
    });
    Ok(image)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn multiline_and_real_font_glyphs() {
        let a = render_text(
            "PhotoDeck\nTürkçe",
            "Sans Serif,24,-1,5,400",
            [255, 255, 255, 255],
        )
        .unwrap();
        assert!(a.width() > 20 && a.height() > 30);
        assert!(a.pixels().any(|p| p[3] > 0));
        let b = render_text("iiii", "Sans Serif,24,-1,5,400", [255; 4]).unwrap();
        let c = render_text("WWWW", "Sans Serif,24,-1,5,400", [255; 4]).unwrap();
        assert_ne!(b, c);
    }
}
