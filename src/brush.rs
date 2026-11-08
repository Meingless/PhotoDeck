//! Pressure-aware raster tools with document-coordinate coverage and straight-alpha storage.
use crate::types::{BrushSettings, Point, Rect, StrokeSample};
use image::{GrayImage, Rgba, RgbaImage};
fn coverage(m: Option<&GrayImage>, x: i32, y: i32) -> f64 {
    m.map_or(1., |m| {
        if x >= 0 && y >= 0 && (x as u32) < m.width() && (y as u32) < m.height() {
            m.get_pixel(x as u32, y as u32)[0] as f64 / 255.
        } else {
            0.
        }
    })
}
pub fn blend_pixel(dst: &mut Rgba<u8>, color: [u8; 4], weight: f64, erase: bool) {
    let a = weight.clamp(0., 1.) * color[3] as f64 / 255.;
    let da = dst[3] as f64 / 255.;
    if erase {
        dst[3] = (da * (1. - a) * 255.).round() as u8;
        if dst[3] == 0 {
            *dst = Rgba([0; 4])
        }
        return;
    }
    let out = a + da * (1. - a);
    if out <= 0. {
        *dst = Rgba([0; 4]);
        return;
    }
    for c in 0..3 {
        dst[c] = ((color[c] as f64 * a + dst[c] as f64 * da * (1. - a)) / out)
            .round()
            .clamp(0., 255.) as u8
    }
    dst[3] = (out * 255.).round() as u8;
}
fn diameter(s: &BrushSettings, p: f64) -> f64 {
    let min = s.pressure_minimum as f64 / 100.;
    (s.size as f64
        * if s.pressure_size {
            min + (1. - min) * p.clamp(0., 1.).powf(s.pressure_curve)
        } else {
            1.
        })
    .max(1.)
}
fn valid_sample(p: &StrokeSample) -> bool {
    [p.x, p.y, p.pressure, p.tilt_x, p.tilt_y]
        .iter()
        .all(|v| v.is_finite())
        && p.x.abs() <= 16777216.
        && p.y.abs() <= 16777216.
}
#[allow(clippy::too_many_arguments)] // Raster target, coverage and brush dynamics are independent inputs.
fn dab(
    im: &mut RgbaImage,
    mask: Option<&GrayImage>,
    offset: Point,
    p: StrokeSample,
    s: &BrushSettings,
    color: [u8; 4],
    erase: bool,
    clone: Option<(&RgbaImage, Point)>,
) -> bool {
    if p.pressure <= 0. {
        return false;
    }
    let r = diameter(s, p.pressure) / 2.;
    let tilt = p.tilt_x.hypot(p.tilt_y);
    let angle = (s.angle as f64
        + if s.tilt_angle && tilt > 0.01 {
            p.tilt_y.atan2(p.tilt_x).to_degrees()
        } else {
            0.
        })
    .to_radians();
    let (sin, cos) = angle.sin_cos();
    let round = s.roundness as f64 / 100.
        * if s.tilt_roundness {
            tilt.min(85.).to_radians().cos().clamp(0.05, 1.)
        } else {
            1.
        };
    let flow = s.flow as f64 / 100.
        * if s.pressure_flow {
            p.pressure.clamp(0., 1.).powf(s.pressure_curve)
        } else {
            1.
        };
    let (cx, cy) = (p.x - offset.x as f64, p.y - offset.y as f64);
    let left = (cx - r - 1.).floor().max(0.) as u32;
    let top = (cy - r - 1.).floor().max(0.) as u32;
    let right = (cx + r + 1.).ceil().clamp(0., im.width() as f64) as u32;
    let bottom = (cy + r + 1.).ceil().clamp(0., im.height() as f64) as u32;
    let mut changed = false;
    for y in top..bottom {
        for x in left..right {
            let mut alpha = 0.;
            for sy in 0..2 {
                for sx in 0..2 {
                    let (dx, dy) = (
                        x as f64 + (sx as f64 + 0.5) / 2. - cx,
                        y as f64 + (sy as f64 + 0.5) / 2. - cy,
                    );
                    let (tx, ty) = (
                        (dx * cos + dy * sin) / r,
                        (-dx * sin + dy * cos) / (r * round),
                    );
                    let v = if let Some(tip) = &s.tip_mask {
                        let longest = tip.width().max(tip.height()) as f64;
                        let ux = tx * longest / 2. + tip.width() as f64 / 2.;
                        let uy = ty * longest / 2. + tip.height() as f64 / 2.;
                        if ux >= 0.
                            && uy >= 0.
                            && ux < tip.width() as f64
                            && uy < tip.height() as f64
                        {
                            {
                                let fx = (ux - 0.5).clamp(0., tip.width() as f64 - 1.);
                                let fy = (uy - 0.5).clamp(0., tip.height() as f64 - 1.);
                                let x0 = fx.floor() as u32;
                                let y0 = fy.floor() as u32;
                                let x1 = (x0 + 1).min(tip.width() - 1);
                                let y1 = (y0 + 1).min(tip.height() - 1);
                                let (tx, ty) = (fx - x0 as f64, fy - y0 as f64);
                                (tip.get_pixel(x0, y0)[0] as f64 * (1. - tx) * (1. - ty)
                                    + tip.get_pixel(x1, y0)[0] as f64 * tx * (1. - ty)
                                    + tip.get_pixel(x0, y1)[0] as f64 * (1. - tx) * ty
                                    + tip.get_pixel(x1, y1)[0] as f64 * tx * ty)
                                    / 255.
                            }
                        } else {
                            0.
                        }
                    } else {
                        let d = tx.hypot(ty);
                        let hard = s.hardness as f64 / 100.;
                        if d > 1. {
                            0.
                        } else if d <= hard {
                            1.
                        } else {
                            (1. - d) / (1. - hard)
                        }
                    };
                    alpha += v / 4.
                }
            }
            alpha *= flow * coverage(mask, x as i32 + offset.x, y as i32 + offset.y);
            if alpha <= 0. {
                continue;
            }
            let col = if let Some((source, delta)) = clone {
                let (xx, yy) = (
                    i64::from(x) + i64::from(delta.x),
                    i64::from(y) + i64::from(delta.y),
                );
                if xx < 0
                    || yy < 0
                    || xx >= i64::from(source.width())
                    || yy >= i64::from(source.height())
                {
                    continue;
                }
                source.get_pixel(xx as u32, yy as u32).0
            } else {
                color
            };
            let before = *im.get_pixel(x, y);
            blend_pixel(im.get_pixel_mut(x, y), col, alpha, erase);
            changed |= before != *im.get_pixel(x, y);
        }
    }
    changed
}
#[allow(clippy::too_many_arguments)]
fn stroke(
    im: &mut RgbaImage,
    mask: Option<&GrayImage>,
    offset: Point,
    points: &[StrokeSample],
    s: &BrushSettings,
    color: [u8; 4],
    erase: bool,
    clone: Option<(&RgbaImage, Point)>,
) -> bool {
    if !s.valid() || points.is_empty() || points.iter().any(|p| !valid_sample(p)) {
        return false;
    }
    let mut changed = dab(im, mask, offset, points[0], s, color, erase, clone);
    let mut remaining = (diameter(s, points[0].pressure) * s.spacing as f64 / 100.).max(1.);
    let mut last = points[0];
    for w in points.windows(2) {
        let (a, b) = (w[0], w[1]);
        let distance = (b.x - a.x).hypot(b.y - a.y);
        let mut travelled = 0.;
        while distance - travelled + 0.000001 >= remaining {
            travelled += remaining;
            let t = (travelled / distance).min(1.);
            let lerp = |a: f64, b: f64| a + (b - a) * t;
            let p = StrokeSample {
                x: lerp(a.x, b.x),
                y: lerp(a.y, b.y),
                pressure: lerp(a.pressure, b.pressure),
                tilt_x: lerp(a.tilt_x, b.tilt_x),
                tilt_y: lerp(a.tilt_y, b.tilt_y),
            };
            changed |= dab(im, mask, offset, p, s, color, erase, clone);
            last = p;
            remaining = (diameter(s, p.pressure) * s.spacing as f64 / 100.).max(1.);
        }
        remaining -= (distance - travelled).max(0.);
    }
    let p = *points.last().unwrap();
    if (last.x - p.x).hypot(last.y - p.y) > 0.001 {
        changed |= dab(im, mask, offset, p, s, color, erase, clone)
    }
    changed
}
pub fn paint_stroke(
    im: &mut RgbaImage,
    mask: Option<&GrayImage>,
    offset: Point,
    points: &[StrokeSample],
    s: &BrushSettings,
    color: [u8; 4],
    erase: bool,
) -> bool {
    stroke(im, mask, offset, points, s, color, erase, None)
}
pub fn clone_stroke(
    im: &mut RgbaImage,
    source: &RgbaImage,
    mask: Option<&GrayImage>,
    offset: Point,
    points: &[StrokeSample],
    s: &BrushSettings,
    delta: Point,
) -> bool {
    stroke(
        im,
        mask,
        offset,
        points,
        s,
        [255; 4],
        false,
        Some((source, delta)),
    )
}
pub fn flood_fill(
    im: &mut RgbaImage,
    mask: Option<&GrayImage>,
    offset: Point,
    seed: Point,
    color: [u8; 4],
    flow: f64,
    tolerance: u8,
) -> bool {
    let (sx, sy) = (
        i64::from(seed.x) - i64::from(offset.x),
        i64::from(seed.y) - i64::from(offset.y),
    );
    if sx < 0
        || sy < 0
        || sx >= i64::from(im.width())
        || sy >= i64::from(im.height())
        || !flow.is_finite()
    {
        return false;
    }
    let old = *im.get_pixel(sx as u32, sy as u32);
    let mut visited = vec![false; im.width() as usize * im.height() as usize];
    let mut stack = vec![(sx as u32, sy as u32)];
    let mut changed = false;
    while let Some((x, y)) = stack.pop() {
        let idx = y as usize * im.width() as usize + x as usize;
        if visited[idx] {
            continue;
        }
        visited[idx] = true;
        let p = *im.get_pixel(x, y);
        if (0..4).any(|c| p[c].abs_diff(old[c]) > tolerance) {
            continue;
        }
        let a = coverage(mask, x as i32 + offset.x, y as i32 + offset.y);
        if a == 0. {
            continue;
        }
        blend_pixel(im.get_pixel_mut(x, y), color, flow * a, false);
        changed |= p != *im.get_pixel(x, y);
        if x > 0 {
            stack.push((x - 1, y))
        }
        if y > 0 {
            stack.push((x, y - 1))
        }
        if x + 1 < im.width() {
            stack.push((x + 1, y))
        }
        if y + 1 < im.height() {
            stack.push((x, y + 1))
        }
    }
    changed
}
pub fn gradient(
    im: &mut RgbaImage,
    mask: Option<&GrayImage>,
    offset: Point,
    from: Point,
    to: Point,
    a: [u8; 4],
    b: [u8; 4],
) -> bool {
    let (dx, dy) = (to.x as f64 - from.x as f64, to.y as f64 - from.y as f64);
    let norm = dx * dx + dy * dy;
    if norm <= 0. {
        return false;
    }
    let mut changed = false;
    for (x, y, p) in im.enumerate_pixels_mut() {
        let t = (((x as f64 + offset.x as f64 + 0.5 - from.x as f64) * dx
            + (y as f64 + offset.y as f64 + 0.5 - from.y as f64) * dy)
            / norm)
            .clamp(0., 1.);
        let mut c = [0; 4];
        let alpha = a[3] as f64 * (1. - t) + b[3] as f64 * t;
        for i in 0..3 {
            c[i] = if alpha > 0. {
                ((a[i] as f64 * a[3] as f64 * (1. - t) + b[i] as f64 * b[3] as f64 * t) / alpha)
                    .round() as u8
            } else {
                0
            }
        }
        c[3] = alpha.round() as u8;
        let before = *p;
        blend_pixel(
            p,
            c,
            coverage(mask, x as i32 + offset.x, y as i32 + offset.y),
            false,
        );
        changed |= before != *p
    }
    changed
}
#[allow(clippy::too_many_arguments)] // Public raster tool contract.
pub fn shape(
    im: &mut RgbaImage,
    mask: Option<&GrayImage>,
    offset: Point,
    r: Rect,
    ellipse: bool,
    fill: bool,
    width: f64,
    color: [u8; 4],
) -> bool {
    if r.width == 0 || r.height == 0 || !width.is_finite() || width <= 0. {
        return false;
    }
    let mut changed = false;
    for (x, y, p) in im.enumerate_pixels_mut() {
        let mut n = 0;
        for sy in 0..2 {
            for sx in 0..2 {
                let px = x as f64 + offset.x as f64 + (sx as f64 + 0.5) / 2. - r.x as f64;
                let py = y as f64 + offset.y as f64 + (sy as f64 + 0.5) / 2. - r.y as f64;
                let outside = if ellipse {
                    ((px - r.width as f64 / 2.) / (r.width as f64 / 2.)).powi(2)
                        + ((py - r.height as f64 / 2.) / (r.height as f64 / 2.)).powi(2)
                        > 1.
                } else {
                    px < 0. || py < 0. || px > r.width as f64 || py > r.height as f64
                };
                if outside {
                    continue;
                }
                let inner = if ellipse {
                    let (rx, ry) = (r.width as f64 / 2. - width, r.height as f64 / 2. - width);
                    rx > 0.
                        && ry > 0.
                        && ((px - r.width as f64 / 2.) / rx).powi(2)
                            + ((py - r.height as f64 / 2.) / ry).powi(2)
                            < 1.
                } else {
                    px > width
                        && py > width
                        && px < r.width as f64 - width
                        && py < r.height as f64 - width
                };
                if fill || !inner {
                    n += 1
                }
            }
        }
        let before = *p;
        blend_pixel(
            p,
            color,
            n as f64 / 4. * coverage(mask, x as i32 + offset.x, y as i32 + offset.y),
            false,
        );
        changed |= before != *p
    }
    changed
}
pub fn define_tip(source: &RgbaImage) -> GrayImage {
    if !crate::types::Size::new(source.width(), source.height()).valid() {
        return GrayImage::new(0, 0);
    }
    let mut gray = GrayImage::from_fn(source.width(), source.height(), |x, y| {
        let p = source.get_pixel(x, y);
        let g = (11 * p[0] as u32 + 16 * p[1] as u32 + 5 * p[2] as u32) / 32;
        image::Luma([(((255 - g) * p[3] as u32 + 127) / 255) as u8])
    });
    if gray.width().max(gray.height()) > 512 {
        let ratio = 512. / gray.width().max(gray.height()) as f64;
        gray = image::imageops::resize(
            &gray,
            (gray.width() as f64 * ratio).round().max(1.) as u32,
            (gray.height() as f64 * ratio).round().max(1.) as u32,
            image::imageops::FilterType::Triangle,
        );
    }
    if gray.pixels().any(|p| p[0] > 0) {
        gray
    } else {
        GrayImage::new(0, 0)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn define_tip_white_is_empty_darkness_has_alpha() {
        assert_eq!(
            define_tip(&RgbaImage::from_pixel(1, 1, Rgba([255; 4]))).dimensions(),
            (0, 0)
        );
        assert_eq!(
            define_tip(&RgbaImage::from_pixel(1, 1, Rgba([0, 0, 0, 128]))).get_pixel(0, 0)[0],
            128
        );
    }
    #[test]
    fn stroke_segmentation_preserves_spacing_and_flow() {
        let mut a = RgbaImage::new(30, 12);
        let mut b = a.clone();
        let s = BrushSettings {
            size: 7,
            flow: 23,
            spacing: 37,
            ..BrushSettings::default()
        };
        paint_stroke(
            &mut a,
            None,
            Point::default(),
            &[StrokeSample::mouse(2., 6.), StrokeSample::mouse(26., 6.)],
            &s,
            [32, 90, 220, 180],
            false,
        );
        paint_stroke(
            &mut b,
            None,
            Point::default(),
            &[
                StrokeSample::mouse(2., 6.),
                StrokeSample::mouse(7., 6.),
                StrokeSample::mouse(14., 6.),
                StrokeSample::mouse(26., 6.),
            ],
            &s,
            [32, 90, 220, 180],
            false,
        );
        assert_eq!(a, b);
    }
    #[test]
    fn preview_replay_does_not_accumulate_flow() {
        let base = RgbaImage::new(20, 8);
        let s = BrushSettings {
            size: 5,
            flow: 20,
            ..BrushSettings::default()
        };
        let points = [StrokeSample::mouse(2., 4.), StrokeSample::mouse(15., 4.)];
        let mut expected = base.clone();
        paint_stroke(
            &mut expected,
            None,
            Point::default(),
            &points,
            &s,
            [255; 4],
            false,
        );
        let mut preview = base.clone();
        paint_stroke(
            &mut preview,
            None,
            Point::default(),
            &points[..1],
            &s,
            [255; 4],
            false,
        );
        preview = base.clone();
        paint_stroke(
            &mut preview,
            None,
            Point::default(),
            &points,
            &s,
            [255; 4],
            false,
        );
        assert_eq!(preview, expected);
    }
    #[test]
    fn pressure_reduces_size() {
        let s = BrushSettings::default();
        assert!((diameter(&s, 0.) - 3.2).abs() < 1e-9);
        assert_eq!(diameter(&s, 1.), 32.);
    }
    #[test]
    fn flood_respects_barrier() {
        let mut im =
            RgbaImage::from_fn(3, 1, |x, _| Rgba([if x == 1 { 200 } else { 0 }, 0, 0, 255]));
        assert!(flood_fill(
            &mut im,
            None,
            Point::default(),
            Point::default(),
            [0, 255, 0, 255],
            1.,
            0
        ));
        assert_eq!(im.get_pixel(2, 0).0, [0, 0, 0, 255]);
        assert_eq!(im.get_pixel(0, 0).0, [0, 255, 0, 255]);
    }
    #[test]
    fn custom_tip_and_offset() {
        let s = BrushSettings {
            size: 4,
            tip_mask: Some(std::sync::Arc::new(GrayImage::from_pixel(
                1,
                1,
                image::Luma([255]),
            ))),
            ..BrushSettings::default()
        };
        let mut im = RgbaImage::new(4, 4);
        assert!(paint_stroke(
            &mut im,
            None,
            Point::new(100, 100),
            &[StrokeSample::mouse(102., 102.)],
            &s,
            [255; 4],
            false
        ));
        assert!(im.get_pixel(2, 2)[3] > 0);
    }
    #[test]
    fn zero_coverage_blocks() {
        let mut im = RgbaImage::new(8, 8);
        let m = GrayImage::new(8, 8);
        assert!(!paint_stroke(
            &mut im,
            Some(&m),
            Point::default(),
            &[StrokeSample::mouse(4., 4.)],
            &BrushSettings::default(),
            [255; 4],
            false
        ));
    }
    #[test]
    fn alpha_over_and_erase() {
        let mut p = Rgba([255, 0, 0, 128]);
        blend_pixel(&mut p, [0, 0, 255, 128], 1., false);
        assert_eq!(p[3], 192);
        assert!(p[2] > p[0]);
        blend_pixel(&mut p, [255; 4], 1., true);
        assert_eq!(p.0, [0; 4]);
    }
    #[test]
    fn final_endpoint() {
        let mut im = RgbaImage::new(20, 4);
        let s = BrushSettings {
            size: 2,
            hardness: 100,
            spacing: 200,
            ..BrushSettings::default()
        };
        assert!(paint_stroke(
            &mut im,
            None,
            Point::default(),
            &[StrokeSample::mouse(1., 2.), StrokeSample::mouse(18., 2.)],
            &s,
            [255; 4],
            false
        ));
        assert!(im.get_pixel(18, 2)[3] > 0);
    }
}
