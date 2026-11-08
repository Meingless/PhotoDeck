//! Color processing in straight alpha, spatial processing in premultiplied alpha.
use crate::types::{FilterSettings, Layer, MaskSettings, Point, ToneSettings};
use image::{GrayImage, Luma, Rgba, RgbaImage};
fn bounded(im: &RgbaImage) -> bool {
    im.width() > 0
        && im.height() > 0
        && im.width() <= 16384
        && im.height() <= 16384
        && u64::from(im.width()) * u64::from(im.height()) <= 16 * 1024 * 1024
}
fn byte(v: f64) -> u8 {
    v.round().clamp(0., 255.) as u8
}
fn premul(im: &RgbaImage) -> RgbaImage {
    RgbaImage::from_fn(im.width(), im.height(), |x, y| {
        let p = im.get_pixel(x, y);
        Rgba([
            ((p[0] as u32 * p[3] as u32 + 127) / 255) as u8,
            ((p[1] as u32 * p[3] as u32 + 127) / 255) as u8,
            ((p[2] as u32 * p[3] as u32 + 127) / 255) as u8,
            p[3],
        ])
    })
}
fn straight(im: &RgbaImage) -> RgbaImage {
    RgbaImage::from_fn(im.width(), im.height(), |x, y| {
        let p = im.get_pixel(x, y);
        if p[3] == 0 {
            Rgba([0; 4])
        } else {
            Rgba([
                byte(p[0] as f64 * 255. / p[3] as f64),
                byte(p[1] as f64 * 255. / p[3] as f64),
                byte(p[2] as f64 * 255. / p[3] as f64),
                p[3],
            ])
        }
    })
}
fn convolve(im: &RgbaImage, k: &[f64], round: bool) -> RgbaImage {
    let r = k.len() as i32 / 2;
    let mut m = premul(im);
    for horizontal in [true, false] {
        let mut out = RgbaImage::new(im.width(), im.height());
        for (x, y, p) in out.enumerate_pixels_mut() {
            let mut sums = [0.; 4];
            for (i, w) in k.iter().enumerate() {
                let d = i as i32 - r;
                let (xx, yy) = if horizontal {
                    ((x as i32 + d).clamp(0, im.width() as i32 - 1) as u32, y)
                } else {
                    (x, (y as i32 + d).clamp(0, im.height() as i32 - 1) as u32)
                };
                let q = m.get_pixel(xx, yy);
                for c in 0..4 {
                    sums[c] += q[c] as f64 * w
                }
            }
            for c in 0..4 {
                p[c] = if round {
                    byte(sums[c])
                } else {
                    sums[c].clamp(0., 255.) as u8
                }
            }
            for c in 0..3 {
                p[c] = p[c].min(p[3])
            }
        }
        m = out
    }
    straight(&m)
}
pub fn gaussian_blur(im: &RgbaImage, r: i32) -> RgbaImage {
    if !bounded(im) || !(0..=16).contains(&r) {
        return RgbaImage::new(0, 0);
    }
    if r == 0 {
        return im.clone();
    }
    let sigma = (r as f64 / 2.).max(0.5);
    let mut k: Vec<f64> = (-r..=r)
        .map(|i| (-(i * i) as f64 / (2. * sigma * sigma)).exp())
        .collect();
    let sum: f64 = k.iter().sum();
    for v in &mut k {
        *v /= sum
    }
    convolve(im, &k, true)
}
pub fn box_blur(im: &RgbaImage, r: i32) -> RgbaImage {
    if !bounded(im) || !(0..=128).contains(&r) {
        return RgbaImage::new(0, 0);
    }
    if r == 0 {
        return im.clone();
    }
    let mut m = premul(im);
    let window = (2 * r + 1) as u64;
    for horizontal in [true, false] {
        let mut out = RgbaImage::new(im.width(), im.height());
        let (lines, len) = if horizontal {
            (im.height(), im.width())
        } else {
            (im.width(), im.height())
        };
        for line in 0..lines {
            let get = |i: i32| {
                let i = i.clamp(0, len as i32 - 1) as u32;
                if horizontal {
                    m.get_pixel(i, line).0
                } else {
                    m.get_pixel(line, i).0
                }
            };
            let mut sums = [0u64; 4];
            for i in -r..=r {
                let p = get(i);
                for c in 0..4 {
                    sums[c] += p[c] as u64
                }
            }
            for i in 0..len {
                let color = Rgba(sums.map(|v| (v / window) as u8));
                if horizontal {
                    out.put_pixel(i, line, color)
                } else {
                    out.put_pixel(line, i, color)
                }
                let a = get(i as i32 - r);
                let b = get(i as i32 + r + 1);
                for c in 0..4 {
                    sums[c] = sums[c] - a[c] as u64 + b[c] as u64
                }
            }
        }
        m = out
    }
    straight(&m)
}
fn interpolate(points: &[Point], x: f64) -> f64 {
    for w in points.windows(2) {
        if x <= w[1].x as f64 {
            return w[0].y as f64
                + (w[1].y - w[0].y) as f64 * (x - w[0].x as f64) / (w[1].x - w[0].x) as f64;
        }
    }
    points.last().map_or(x, |p| p.y as f64)
}
pub fn tone_map(im: &RgbaImage, t: &ToneSettings) -> RgbaImage {
    let fallback = ToneSettings::default();
    let t = if t.valid() { t } else { &fallback };
    let balances = [t.red_balance, t.green_balance, t.blue_balance];
    let composite: [f64; 256] = std::array::from_fn(|i| {
        let mut v = ((i as f64 - t.input_black as f64) / (t.input_white - t.input_black) as f64)
            .clamp(0., 1.)
            .powf(1. / t.gamma);
        if t.exposure != 0. {
            let linear = if v <= 0.04045 {
                v / 12.92
            } else {
                ((v + 0.055) / 1.055).powf(2.4)
            };
            let l = (linear * 2f64.powf(t.exposure)).clamp(0., 1.);
            v = if l <= 0.0031308 {
                12.92 * l
            } else {
                1.055 * l.powf(1. / 2.4) - 0.055
            }
        }
        interpolate(
            &t.curves[0],
            t.output_black as f64 + v * (t.output_white - t.output_black) as f64,
        )
    });
    let tables: [[u8; 256]; 3] = std::array::from_fn(|c| {
        std::array::from_fn(|i| {
            byte(interpolate(&t.curves[c + 1], composite[i]) + balances[c] as f64 * 2.55)
        })
    });
    RgbaImage::from_fn(im.width(), im.height(), |x, y| {
        let p = im.get_pixel(x, y);
        let mut rgb = [
            tables[0][p[0] as usize],
            tables[1][p[1] as usize],
            tables[2][p[2] as usize],
        ];
        if t.hue != 0 || t.vibrance != 0 {
            let f = rgb.map(|v| v as f64 / 255.);
            let max = f.into_iter().fold(0., f64::max);
            let min = f.into_iter().fold(1., f64::min);
            let d = max - min;
            let mut h = if d == 0. {
                0.
            } else if max == f[0] {
                ((f[1] - f[2]) / d).rem_euclid(6.) / 6.
            } else if max == f[1] {
                ((f[2] - f[0]) / d + 2.) / 6.
            } else {
                ((f[0] - f[1]) / d + 4.) / 6.
            };
            h = (h + t.hue as f64 / 360.).rem_euclid(1.);
            let s = if max == 0. { 0. } else { d / max };
            let s = if t.vibrance >= 0 {
                s + t.vibrance as f64 / 100. * s * (1. - s)
            } else {
                s * (1. + t.vibrance as f64 / 100.)
            };
            let c = max * s;
            let xx = c * (1. - ((h * 6.).rem_euclid(2.) - 1.).abs());
            let a = match (h * 6.) as u32 {
                0 => [c, xx, 0.],
                1 => [xx, c, 0.],
                2 => [0., c, xx],
                3 => [0., xx, c],
                4 => [xx, 0., c],
                _ => [c, 0., xx],
            };
            rgb = a.map(|v| byte((v + max - c) * 255.));
        }
        for v in &mut rgb {
            if t.inverted {
                *v = 255 - *v
            }
            if t.posterize != 256 {
                *v = byte(
                    (*v as f64 * (t.posterize - 1) as f64 / 255.).round() * 255.
                        / (t.posterize - 1) as f64,
                )
            }
        }
        if t.threshold >= 0 {
            let v = if 0.2126 * rgb[0] as f64 + 0.7152 * rgb[1] as f64 + 0.0722 * rgb[2] as f64
                >= t.threshold as f64
            {
                255
            } else {
                0
            };
            rgb = [v; 3]
        }
        Rgba([rgb[0], rgb[1], rgb[2], p[3]])
    })
}
fn hash(mut v: u32) -> u32 {
    v ^= v >> 16;
    v = v.wrapping_mul(0x7feb352d);
    v ^= v >> 15;
    v = v.wrapping_mul(0x846ca68b);
    v ^ (v >> 16)
}
fn noise(x: u32, y: u32, seed: u32, c: u32, a: i32) -> i32 {
    let v = hash(
        x.wrapping_mul(0x9e3779b9) ^ y.wrapping_mul(0x85ebca6b) ^ seed ^ c.wrapping_mul(0xc2b2ae35),
    );
    ((f64::from(v & 65535) / 32767.5 - 1.) * a as f64 * 2.55).round() as i32
}
pub fn apply_filters(im: &RgbaImage, s: &FilterSettings) -> RgbaImage {
    if !bounded(im) || !s.valid() {
        return RgbaImage::new(0, 0);
    }
    let mut out = im.clone();
    if s.mosaic > 1 {
        let mut m = premul(im);
        let b = s.mosaic as u32;
        for top in (0..im.height()).step_by(b as usize) {
            for left in (0..im.width()).step_by(b as usize) {
                let bottom = (top + b).min(im.height());
                let right = (left + b).min(im.width());
                let n = u64::from((bottom - top) * (right - left));
                let mut sums = [0u64; 4];
                for y in top..bottom {
                    for x in left..right {
                        for (c, sum) in sums.iter_mut().enumerate() {
                            *sum += m.get_pixel(x, y)[c] as u64;
                        }
                    }
                }
                let color = Rgba(sums.map(|v| ((v + n / 2) / n) as u8));
                for y in top..bottom {
                    for x in left..right {
                        m.put_pixel(x, y, color)
                    }
                }
            }
        }
        out = straight(&m)
    }
    if s.noise > 0 {
        for (x, y, p) in out.enumerate_pixels_mut() {
            if p[3] == 0 {
                continue;
            }
            for c in 0..3 {
                p[c] = (p[c] as i32
                    + noise(
                        x,
                        y,
                        s.noise_seed,
                        if s.monochrome_noise { 0 } else { c as u32 },
                        s.noise,
                    ))
                .clamp(0, 255) as u8
            }
        }
    }
    if s.gaussian > 0 {
        out = gaussian_blur(&out, s.gaussian)
    }
    for (high, r) in [
        (true, s.high_pass),
        (
            false,
            if s.sharpen_amount > 0 {
                s.sharpen_radius
            } else {
                0
            },
        ),
    ] {
        if r == 0 {
            continue;
        }
        let soft = gaussian_blur(&out, r);
        for (x, y, p) in out.enumerate_pixels_mut() {
            if p[3] == 0 {
                *p = Rgba([0; 4]);
                continue;
            }
            let q = soft.get_pixel(x, y);
            let delta = (0..3).map(|c| p[c].abs_diff(q[c]) as i32).max().unwrap();
            if !high && delta < s.sharpen_threshold {
                continue;
            }
            for c in 0..3 {
                p[c] = byte(if high {
                    128. + p[c] as f64 - q[c] as f64
                } else {
                    p[c] as f64 + (p[c] as f64 - q[c] as f64) * s.sharpen_amount as f64 / 100.
                })
            }
        }
    }
    out
}
pub fn apply(im: &RgbaImage, l: &Layer) -> RgbaImage {
    if !bounded(im) {
        return RgbaImage::new(0, 0);
    }
    let mut m = box_blur(im, l.blur);
    let contrast = 2f64.powf(l.contrast as f64 / 50.);
    let sat = 1. + l.saturation as f64 / 100.;
    for p in m.pixels_mut() {
        let lum = 0.2126 * p[0] as f64 + 0.7152 * p[1] as f64 + 0.0722 * p[2] as f64;
        for c in 0..3 {
            p[c] = byte(
                (lum + (p[c] as f64 - lum) * sat - 127.5) * contrast
                    + 127.5
                    + l.brightness as f64 * 2.55,
            )
        }
    }
    apply_filters(&tone_map(&m, &l.tones), &l.filters)
}
pub fn mask_coverage(m: &GrayImage, s: &MaskSettings) -> GrayImage {
    if !s.valid() || !crate::types::Size::new(m.width(), m.height()).valid() {
        return GrayImage::new(0, 0);
    }
    if !s.enabled || s.density == 0 {
        return GrayImage::from_pixel(m.width(), m.height(), Luma([255]));
    }
    let blurred = if s.feather > 0 {
        let rgba = RgbaImage::from_fn(m.width(), m.height(), |x, y| {
            let v = m.get_pixel(x, y)[0];
            Rgba([v, v, v, 255])
        });
        Some(gaussian_blur(&rgba, s.feather))
    } else {
        None
    };
    GrayImage::from_fn(m.width(), m.height(), |x, y| {
        let v = blurred
            .as_ref()
            .map_or_else(|| m.get_pixel(x, y)[0], |b| b.get_pixel(x, y)[0]);
        Luma([(255 - ((255 - v as i32) * s.density + 50) / 100) as u8])
    })
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn exact_box_edge_replication() {
        let im = RgbaImage::from_fn(3, 1, |x, _| Rgba([if x == 1 { 255 } else { 0 }, 0, 0, 255]));
        let out = box_blur(&im, 1);
        assert_eq!(
            out.pixels().map(|p| p[0]).collect::<Vec<_>>(),
            vec![85, 85, 85]
        );
    }
    #[test]
    fn density_and_disabled_mask() {
        let m = GrayImage::from_pixel(1, 1, Luma([0]));
        let mut s = MaskSettings {
            density: 50,
            ..MaskSettings::default()
        };
        assert_eq!(mask_coverage(&m, &s).get_pixel(0, 0)[0], 127);
        s.enabled = false;
        assert_eq!(mask_coverage(&m, &s).get_pixel(0, 0)[0], 255);
    }
    #[test]
    fn threshold_after_posterize() {
        let t = ToneSettings {
            posterize: 2,
            threshold: 128,
            ..ToneSettings::default()
        };
        let im = RgbaImage::from_pixel(1, 1, Rgba([200, 100, 200, 31]));
        assert_eq!(tone_map(&im, &t).get_pixel(0, 0).0, [0, 0, 0, 31]);
    }
    #[test]
    fn invert_keeps_alpha() {
        let im = RgbaImage::from_pixel(1, 1, Rgba([10, 20, 30, 77]));
        let t = ToneSettings {
            inverted: true,
            ..ToneSettings::default()
        };
        assert_eq!(tone_map(&im, &t).get_pixel(0, 0).0, [245, 235, 225, 77]);
    }
    #[test]
    fn blur_ignores_hidden_rgb() {
        let im = RgbaImage::from_fn(3, 1, |x, _| {
            if x == 1 {
                Rgba([255, 0, 0, 255])
            } else {
                Rgba([0, 255, 255, 0])
            }
        });
        let out = gaussian_blur(&im, 1);
        for p in out.pixels() {
            assert_eq!(p[1], 0);
            assert_eq!(p[2], 0);
        }
    }
    #[test]
    fn noise_deterministic() {
        let im = RgbaImage::from_pixel(4, 3, Rgba([128, 128, 128, 199]));
        let s = FilterSettings {
            noise: 20,
            ..FilterSettings::default()
        };
        let a = apply_filters(&im, &s);
        assert_eq!(a, apply_filters(&im, &s));
        assert!(
            a.pixels()
                .all(|p| p[0] == p[1] && p[1] == p[2] && p[3] == 199)
        );
    }
}
