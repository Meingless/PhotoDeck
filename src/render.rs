//! Deterministic CPU renderer: straight-alpha storage, premultiplied resampling/compositing.
use crate::{effects, jobs::CancellationToken, types::*};
use image::{Rgba, RgbaImage};
use std::sync::Arc;
fn lum(c: [f64; 3]) -> f64 {
    0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
}
fn sat(c: [f64; 3]) -> f64 {
    c.into_iter().fold(f64::NEG_INFINITY, f64::max) - c.into_iter().fold(f64::INFINITY, f64::min)
}
fn set_lum(mut c: [f64; 3], v: f64) -> [f64; 3] {
    let d = v - lum(c);
    for x in &mut c {
        *x += d;
    }
    let l = lum(c);
    let n = c.into_iter().fold(f64::INFINITY, f64::min);
    let x = c.into_iter().fold(f64::NEG_INFINITY, f64::max);
    if n < 0. {
        for v in &mut c {
            *v = l + (*v - l) * l / (l - n);
        }
    }
    if x > 1. {
        for v in &mut c {
            *v = l + (*v - l) * (1. - l) / (x - l);
        }
    }
    c
}
fn set_sat(mut c: [f64; 3], v: f64) -> [f64; 3] {
    let mut order = [0, 1, 2];
    order.sort_by(|a, b| c[*a].total_cmp(&c[*b]));
    let [lo, mid, hi] = order;
    if c[hi] > c[lo] {
        c[mid] = (c[mid] - c[lo]) * v / (c[hi] - c[lo]);
        c[hi] = v;
    } else {
        c[mid] = 0.;
        c[hi] = 0.;
    }
    c[lo] = 0.;
    c
}
fn burn(b: f64, s: f64) -> f64 {
    if b >= 1. {
        1.
    } else if s <= 0. {
        0.
    } else {
        1. - ((1. - b) / s).min(1.)
    }
}
fn dodge(b: f64, s: f64) -> f64 {
    if b <= 0. {
        0.
    } else if s >= 1. {
        1.
    } else {
        (b / (1. - s)).min(1.)
    }
}
pub fn blend_rgb(b: [f64; 3], s: [f64; 3], mode: u8) -> [f64; 3] {
    match mode {
        9 => {
            return if s.iter().sum::<f64>() < b.iter().sum() {
                s
            } else {
                b
            };
        }
        12 => {
            return if s.iter().sum::<f64>() > b.iter().sum() {
                s
            } else {
                b
            };
        }
        23 => return set_lum(set_sat(s, sat(b)), lum(b)),
        24 => return set_lum(set_sat(b, sat(s)), lum(b)),
        25 => return set_lum(s, lum(b)),
        26 => return set_lum(b, lum(s)),
        _ => {}
    }
    std::array::from_fn(|i| {
        let b = b[i];
        let s = s[i];
        let v = match mode {
            1 => b * s,
            2 => b + s - b * s,
            3 => {
                if b <= 0.5 {
                    2. * b * s
                } else {
                    1. - 2. * (1. - b) * (1. - s)
                }
            }
            4 => b.min(s),
            5 => b.max(s),
            7 => burn(b, s),
            8 => b + s - 1.,
            10 => dodge(b, s),
            11 => b + s,
            13 => {
                if s <= 0.5 {
                    b - (1. - 2. * s) * b * (1. - b)
                } else {
                    let d = if b <= 0.25 {
                        ((16. * b - 12.) * b + 4.) * b
                    } else {
                        b.sqrt()
                    };
                    b + (2. * s - 1.) * (d - b)
                }
            }
            14 => {
                if s <= 0.5 {
                    2. * b * s
                } else {
                    1. - 2. * (1. - b) * (1. - s)
                }
            }
            15 => {
                if s < 0.5 {
                    burn(b, 2. * s)
                } else {
                    dodge(b, 2. * s - 1.)
                }
            }
            16 => b + 2. * s - 1.,
            17 => {
                if s < 0.5 {
                    b.min(2. * s)
                } else {
                    b.max(2. * s - 1.)
                }
            }
            18 => {
                if (if s < 0.5 {
                    burn(b, 2. * s)
                } else {
                    dodge(b, 2. * s - 1.)
                }) < 0.5
                {
                    0.
                } else {
                    1.
                }
            }
            19 => (b - s).abs(),
            20 => b + s - 2. * b * s,
            21 => b - s,
            22 => {
                if s <= 0. {
                    1.
                } else {
                    b / s
                }
            }
            _ => s,
        };
        v.clamp(0., 1.)
    })
}
fn noise(x: i32, y: i32) -> u32 {
    let mut v =
        (x as u32).wrapping_mul(0x9e3779b9) ^ (y as u32).wrapping_mul(0x85ebca6b) ^ 0x7f4a7c15;
    v ^= v >> 16;
    v = v.wrapping_mul(0x7feb352d);
    v ^= v >> 15;
    v = v.wrapping_mul(0x846ca68b);
    v ^ (v >> 16)
}
pub fn compose(
    destination: &mut RgbaImage,
    source: &RgbaImage,
    offset: Point,
    opacity: f64,
    mode: u8,
    origin: Point,
    clipped: bool,
) {
    compose_controlled(
        destination,
        source,
        offset,
        opacity,
        mode,
        origin,
        clipped,
        &CancellationToken::default(),
    )
    .expect("Uncancelled composition");
}
#[allow(clippy::too_many_arguments)]
fn compose_controlled(
    destination: &mut RgbaImage,
    source: &RgbaImage,
    offset: Point,
    opacity: f64,
    mode: u8,
    origin: Point,
    clipped: bool,
    token: &CancellationToken,
) -> Result<()> {
    token.check()?;
    if !opacity.is_finite() || opacity <= 0. {
        return Ok(());
    }
    // Qt's native raster composition quantizes painter opacity to 8 fractional bits.
    // Custom blend and clipping paths use their explicit floating-point weights.
    let native = matches!(mode, 0..=5 | 7 | 10 | 13 | 14 | 19 | 20);
    let opacity = if native && !clipped {
        (opacity.clamp(0., 1.) * 256.).floor() / 256.
    } else {
        opacity.clamp(0., 1.)
    };
    let r = Rect::new(offset.x, offset.y, source.width(), source.height()).intersect(Rect::new(
        0,
        0,
        destination.width(),
        destination.height(),
    ));
    for y in r.y..r.y + r.height as i32 {
        token.check()?;
        for x in r.x..r.x + r.width as i32 {
            let top = source
                .get_pixel((x - offset.x) as u32, (y - offset.y) as u32)
                .0;
            let bottom = destination.get_pixel_mut(x as u32, y as u32);
            let mut a = top[3] as f64 / 255. * opacity.clamp(0., 1.);
            if mode == 6 {
                a = if noise(x + origin.x, y + origin.y) as f64 / 4294967296. < a {
                    1.
                } else {
                    0.
                };
            }
            if a <= 0. {
                continue;
            }
            let ab = bottom[3] as f64 / 255.;
            let cs = std::array::from_fn(|i| top[i] as f64 / 255.);
            let cb = std::array::from_fn(|i| bottom[i] as f64 / 255.);
            let mix = blend_rgb(cb, cs, mode);
            if clipped {
                if ab == 0. {
                    continue;
                }
                for i in 0..3 {
                    bottom[i] = ((cb[i] * (1. - a) + mix[i] * a) * 255.)
                        .round()
                        .clamp(0., 255.) as u8;
                }
            } else {
                let alpha = a + ab * (1. - a);
                for i in 0..3 {
                    bottom[i] = ((a * (1. - ab) * cs[i] + a * ab * mix[i] + (1. - a) * ab * cb[i])
                        / alpha
                        * 255.)
                        .round()
                        .clamp(0., 255.) as u8;
                }
                bottom[3] = (alpha * 255.).round() as u8;
            }
        }
    }
    Ok(())
}
/// Bilinear sample at pixel-edge coordinates, with transparent out-of-bounds taps.
pub fn sample(image: &RgbaImage, x: f64, y: f64) -> Rgba<u8> {
    let x = x - 0.5;
    let y = y - 0.5;
    let ix = x.floor() as i64;
    let iy = y.floor() as i64;
    let fx = x - x.floor();
    let fy = y - y.floor();
    let mut p = [0.; 4];
    for dy in 0..2 {
        for dx in 0..2 {
            let xx = ix + dx;
            let yy = iy + dy;
            if xx < 0 || yy < 0 || xx >= image.width() as i64 || yy >= image.height() as i64 {
                continue;
            }
            let w = (if dx == 0 { 1. - fx } else { fx }) * (if dy == 0 { 1. - fy } else { fy });
            let c = image.get_pixel(xx as u32, yy as u32);
            let a = c[3] as f64 / 255.;
            for i in 0..3 {
                p[i] += c[i] as f64 * a * w;
            }
            p[3] += a * w;
        }
    }
    if p[3] <= 0. {
        return Rgba([0; 4]);
    }
    Rgba([
        (p[0] / p[3]).round().clamp(0., 255.) as u8,
        (p[1] / p[3]).round().clamp(0., 255.) as u8,
        (p[2] / p[3]).round().clamp(0., 255.) as u8,
        (p[3] * 255.).round().clamp(0., 255.) as u8,
    ])
}
pub fn resize_image(image: &RgbaImage, size: Size) -> RgbaImage {
    resize_image_controlled(image, size, &CancellationToken::default()).expect("Uncancelled resize")
}
pub fn resize_image_controlled(
    image: &RgbaImage,
    size: Size,
    token: &CancellationToken,
) -> Result<RgbaImage> {
    token.check()?;
    if image.dimensions() == (size.width, size.height) {
        return Ok(image.clone());
    }
    let mut out = RgbaImage::new(size.width, size.height);
    for y in 0..size.height {
        token.check()?;
        for x in 0..size.width {
            out.put_pixel(
                x,
                y,
                sample(
                    image,
                    ((x as f64 + 0.5) * image.width() as f64 / size.width as f64)
                        .clamp(0.5, image.width() as f64 - 0.5),
                    ((y as f64 + 0.5) * image.height() as f64 / size.height as f64)
                        .clamp(0.5, image.height() as f64 - 0.5),
                ),
            );
        }
    }
    Ok(out)
}
pub fn render_layer(layer: &Layer) -> RgbaImage {
    render_layer_controlled(layer, &CancellationToken::default()).expect("Uncancelled layer")
}
pub fn render_layer_controlled(layer: &Layer, token: &CancellationToken) -> Result<RgbaImage> {
    token.check()?;
    let Some(bounds) = layer.geometry.bounds(layer.source_size()) else {
        return Ok(RgbaImage::new(0, 0));
    };
    let frame = layer.source_size();
    let has_mask =
        layer.mask.is_some() && layer.mask_settings.enabled && layer.mask_settings.density > 0;
    let identity = layer.brightness == 0
        && layer.contrast == 0
        && layer.saturation == 0
        && layer.blur == 0
        && layer.tones.is_identity()
        && layer.filters.is_identity();
    let direct = layer.is_smart_object() && !has_mask && identity && !layer.geometry.is_identity();
    let source = if direct {
        (*layer.pixels).clone()
    } else {
        let source = resize_image_controlled(&layer.pixels, frame, token)?;
        let mut result = effects::apply_controlled(&source, layer, token)?;
        if has_mask {
            let mask = effects::mask_coverage_controlled(
                layer.mask.as_ref().unwrap(),
                &layer.mask_settings,
                token,
            )?;
            for (x, y, p) in result.enumerate_pixels_mut() {
                if x == 0 {
                    token.check()?;
                }
                p[3] = (p[3] as u32 * mask.get_pixel(x, y)[0] as u32 / 255) as u8;
            }
        }
        result
    };
    if layer.geometry.is_identity() {
        return Ok(source);
    }
    let inv = layer.geometry.inverse().unwrap();
    let mut out = RgbaImage::new(bounds.width, bounds.height);
    for y in 0..bounds.height {
        token.check()?;
        for x in 0..bounds.width {
            let pixel = {
                let (mut sx, mut sy) = inv.map(
                    x as f64 + bounds.x as f64 + 0.5,
                    y as f64 + bounds.y as f64 + 0.5,
                );
                if direct {
                    sx *= source.width() as f64 / frame.width as f64;
                    sy *= source.height() as f64 / frame.height as f64;
                }
                if sx < 0. || sy < 0. || sx >= source.width() as f64 || sy >= source.height() as f64
                {
                    Rgba([0; 4])
                } else {
                    // Edge taps repeat the source edge; transparency belongs outside the
                    // transformed source polygon, not to the bilinear interpolation kernel.
                    sample(
                        &source,
                        sx.clamp(0.5, source.width() as f64 - 0.5),
                        sy.clamp(0.5, source.height() as f64 - 0.5),
                    )
                }
            };
            out.put_pixel(x, y, pixel);
        }
    }
    Ok(out)
}
pub fn composite_region(state: &State, bounds: Rect) -> RgbaImage {
    composite_region_controlled(state, bounds, &CancellationToken::default())
        .expect("Uncancelled composition")
}
fn composite_region_controlled(
    state: &State,
    bounds: Rect,
    token: &CancellationToken,
) -> Result<RgbaImage> {
    token.check()?;
    let mut output = RgbaImage::new(bounds.width, bounds.height);
    let mut i = 0;
    while i < state.layers.len() {
        token.check()?;
        token.report("Katmanlar işleniyor", i, Some(state.layers.len()));
        let base = &state.layers[i];
        let mut end = i + 1;
        while end < state.layers.len() && state.layers[end].clipped {
            end += 1;
        }
        if base.visible {
            let mut group = render_layer_controlled(base, token)?;
            if let Some(at) = base.bounds() {
                for child in &state.layers[i + 1..end] {
                    if child.visible
                        && let Some(cp) = child.bounds()
                    {
                        compose_controlled(
                            &mut group,
                            &render_layer_controlled(child, token)?,
                            Point::new(cp.x - at.x, cp.y - at.y),
                            child.opacity,
                            child.blend,
                            Point::new(at.x, at.y),
                            true,
                            token,
                        )?;
                    }
                }
                compose_controlled(
                    &mut output,
                    &group,
                    Point::new(at.x - bounds.x, at.y - bounds.y),
                    base.opacity,
                    base.blend,
                    Point::new(bounds.x, bounds.y),
                    false,
                    token,
                )?;
            }
        }
        i = end;
    }
    token.check()?;
    Ok(output)
}
pub fn composite(state: &State) -> RgbaImage {
    composite_region(state, Rect::new(0, 0, state.size.width, state.size.height))
}
pub fn composite_controlled(state: &State, token: &CancellationToken) -> Result<RgbaImage> {
    composite_region_controlled(
        state,
        Rect::new(0, 0, state.size.width, state.size.height),
        token,
    )
}
pub fn rasterized(layer: &Layer) -> Layer {
    let mut result = layer.clone();
    result.pixels = Arc::new(render_layer(layer));
    if let Some(bounds) = layer.bounds() {
        result.offset = Point::new(bounds.x, bounds.y);
    }
    result.geometry = Transform::identity();
    result.smart_object_id = [0; 16];
    result.smart_object_size = Size::default();
    result.mask = None;
    result.text.clear();
    result.brightness = 0;
    result.contrast = 0;
    result.saturation = 0;
    result.blur = 0;
    result.tones = ToneSettings::default();
    result.filters = FilterSettings::default();
    result.mask_settings = MaskSettings::default();
    result
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn clipping_preserves_base_alpha() {
        for mode in 0..27 {
            let mut b = RgbaImage::from_pixel(2, 2, Rgba([30, 90, 180, 90]));
            compose(
                &mut b,
                &RgbaImage::from_pixel(2, 2, Rgba([200, 120, 40, 150])),
                Point::default(),
                0.7,
                mode,
                Point::default(),
                true,
            );
            assert_eq!(b.get_pixel(0, 0)[3], 90);
        }
    }
    #[test]
    fn transparent_samples_do_not_bleed() {
        let mut i = RgbaImage::from_pixel(2, 1, Rgba([255, 0, 0, 0]));
        i.put_pixel(0, 0, Rgba([0, 0, 255, 255]));
        let p = sample(&i, 1., 0.5);
        assert_eq!(&p.0[..3], &[0, 0, 255]);
    }
    #[test]
    fn projective_horizon_is_rejected() {
        let t = Transform([1., 0., -1., 0., 1., 0., 0., 0., 1.]);
        assert!(t.bounds(Size::new(2, 2)).is_none());
    }
}
