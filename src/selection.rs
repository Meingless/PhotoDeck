//! Immutable document-coordinate selection coverage. `None` is unrestricted;
//! a zero mask is an active selection that permits no edits.
pub use crate::types::SelectionMode;
use crate::types::{Point, Rect, Selection, Size};
use image::{GrayImage, Luma, RgbaImage};
use std::{collections::VecDeque, sync::Arc};
fn valid(s: Size) -> bool {
    s.width > 0
        && s.height > 0
        && s.width <= 16384
        && s.height <= 16384
        && u64::from(s.width) * u64::from(s.height) <= 16 * 1024 * 1024
}
fn inside(r: Rect, p: Point) -> bool {
    i64::from(p.x) >= i64::from(r.x)
        && i64::from(p.y) >= i64::from(r.y)
        && i64::from(p.x) < i64::from(r.x) + i64::from(r.width)
        && i64::from(p.y) < i64::from(r.y) + i64::from(r.height)
}
impl Selection {
    /// Mix two straight-alpha pixels through selection coverage in premultiplied space.
    pub fn mix_pixel(
        &self,
        before: image::Rgba<u8>,
        after: image::Rgba<u8>,
        p: Point,
    ) -> image::Rgba<u8> {
        let w = self.value_at(p) as f64 / 255.;
        let a = before[3] as f64 * (1. - w) + after[3] as f64 * w;
        let mut out = [0u8; 4];
        out[3] = a.round() as u8;
        if a > 0. {
            for c in 0..3 {
                out[c] = ((before[c] as f64 * before[3] as f64 * (1. - w)
                    + after[c] as f64 * after[3] as f64 * w)
                    / a)
                    .round()
                    .clamp(0., 255.) as u8;
            }
        }
        image::Rgba(out)
    }
    pub fn copy_from(&self, image: &RgbaImage) -> RgbaImage {
        if self.is_none() {
            return image.clone();
        }
        if !self.has_pixels() {
            return RgbaImage::new(0, 0);
        }
        let r = self
            .bounds()
            .intersect(Rect::new(0, 0, image.width(), image.height()));
        RgbaImage::from_fn(r.width, r.height, |x, y| {
            let px = x + r.x as u32;
            let py = y + r.y as u32;
            let mut p = *image.get_pixel(px, py);
            p[3] = ((p[3] as u32 * self.value_at(Point::new(px as i32, py as i32)) as u32 + 127)
                / 255) as u8;
            if p[3] == 0 {
                p = image::Rgba([0; 4])
            }
            p
        })
    }
    pub fn is_none(&self) -> bool {
        matches!(self, Self::None)
    }
    pub fn is_empty(&self) -> bool {
        matches!(self, Self::None)
    }
    pub fn value_at(&self, p: Point) -> u8 {
        match self {
            Self::None => 255,
            Self::Rectangle(r) => {
                if inside(*r, p) {
                    255
                } else {
                    0
                }
            }
            Self::Mask(m) => {
                if p.x >= 0 && p.y >= 0 && (p.x as u32) < m.width() && (p.y as u32) < m.height() {
                    m.get_pixel(p.x as u32, p.y as u32)[0]
                } else {
                    0
                }
            }
        }
    }
    pub fn contains(&self, p: Point) -> bool {
        !self.is_empty() && self.value_at(p) > 0
    }
    pub fn has_pixels(&self) -> bool {
        match self {
            Self::None => false,
            Self::Rectangle(r) => r.width > 0 && r.height > 0,
            Self::Mask(m) => m.as_raw().iter().any(|v| *v > 0),
        }
    }
    pub fn bounds(&self) -> Rect {
        match self {
            Self::None => Rect {
                x: 0,
                y: 0,
                width: 0,
                height: 0,
            },
            Self::Rectangle(r) => *r,
            Self::Mask(m) => {
                let (mut l, mut t, mut r, mut b) = (m.width(), m.height(), 0, 0);
                let mut found = false;
                for (x, y, p) in m.enumerate_pixels() {
                    if p[0] > 0 {
                        found = true;
                        l = l.min(x);
                        t = t.min(y);
                        r = r.max(x);
                        b = b.max(y)
                    }
                }
                if found {
                    Rect {
                        x: l as i32,
                        y: t as i32,
                        width: r - l + 1,
                        height: b - t + 1,
                    }
                } else {
                    Rect {
                        x: 0,
                        y: 0,
                        width: m.width(),
                        height: m.height(),
                    }
                }
            }
        }
    }
    pub fn from_mask(m: GrayImage) -> Self {
        if !valid(Size {
            width: m.width(),
            height: m.height(),
        }) {
            Self::None
        } else {
            Self::Mask(Arc::new(m))
        }
    }
    pub fn mask(&self, s: Size) -> GrayImage {
        if !valid(s) {
            return GrayImage::new(0, 0);
        }
        GrayImage::from_fn(s.width, s.height, |x, y| {
            Luma([self.value_at(Point {
                x: x as i32,
                y: y as i32,
            })])
        })
    }
    pub fn equivalent(&self, o: &Self, s: Size) -> bool {
        if self.is_empty() || o.is_empty() {
            self.is_empty() == o.is_empty()
        } else {
            self.mask(s) == o.mask(s)
        }
    }
    pub fn clipped(&self, s: Size) -> Self {
        if self.is_none() || !valid(s) {
            return self.clone();
        }
        if let Self::Rectangle(r) = self {
            let r = r.intersect(Rect::new(0, 0, s.width, s.height));
            return if r.is_empty() {
                Self::from_mask(GrayImage::new(s.width, s.height))
            } else {
                Self::Rectangle(r)
            };
        }
        if matches!(self,Self::Mask(m) if m.dimensions()==(s.width,s.height)) {
            return self.clone();
        }
        self.translated(Point::default(), s)
    }
    pub fn translated(&self, d: Point, s: Size) -> Self {
        if self.is_none() || !valid(s) {
            return self.clone();
        }
        if let Self::Rectangle(r) = self {
            if let (Some(x), Some(y)) = (r.x.checked_add(d.x), r.y.checked_add(d.y)) {
                return Self::Rectangle(Rect::new(x, y, r.width, r.height)).clipped(s);
            }
            return Self::from_mask(GrayImage::new(s.width, s.height));
        }
        if matches!(self,Self::Mask(m) if d==Point::default()&&m.dimensions()==(s.width,s.height)) {
            return self.clone();
        }
        Self::from_mask(GrayImage::from_fn(s.width, s.height, |x, y| {
            let xx = i64::from(x) - i64::from(d.x);
            let yy = i64::from(y) - i64::from(d.y);
            Luma([
                if let (Ok(x), Ok(y)) = (i32::try_from(xx), i32::try_from(yy)) {
                    self.value_at(Point { x, y })
                } else {
                    0
                },
            ])
        }))
    }
    pub fn cropped(&self, r: Rect, _source: Size) -> Self {
        if self.is_empty() {
            self.clone()
        } else {
            self.translated(
                Point {
                    x: r.x.saturating_neg(),
                    y: r.y.saturating_neg(),
                },
                Size {
                    width: r.width,
                    height: r.height,
                },
            )
        }
    }
    pub fn resized(&self, source: Size, target: Size) -> Self {
        if self.is_empty() || !valid(source) || !valid(target) {
            return self.clone();
        }
        if source == target {
            return self.clone();
        }
        if matches!(self,Self::Rectangle(r) if *r==Rect::new(0,0,source.width,source.height)) {
            return Self::Rectangle(Rect::new(0, 0, target.width, target.height));
        }
        Self::from_mask(image::imageops::resize(
            &self.mask(source),
            target.width,
            target.height,
            image::imageops::FilterType::Triangle,
        ))
    }
    pub fn rotated(&self, clockwise: bool, s: Size) -> Self {
        if self.is_empty() || !valid(s) {
            return self.clone();
        }
        if let Self::Rectangle(r) = self {
            let (x, y) = if clockwise {
                (s.height as i64 - r.y as i64 - r.height as i64, r.x as i64)
            } else {
                (r.y as i64, s.width as i64 - r.x as i64 - r.width as i64)
            };
            if let (Ok(x), Ok(y)) = (i32::try_from(x), i32::try_from(y)) {
                return Self::Rectangle(Rect::new(x, y, r.height, r.width))
                    .clipped(Size::new(s.height, s.width));
            }
            return Self::from_mask(GrayImage::new(s.height, s.width));
        }
        let m = self.mask(s);
        Self::from_mask(if clockwise {
            image::imageops::rotate90(&m)
        } else {
            image::imageops::rotate270(&m)
        })
    }
    pub fn flipped(&self, horizontal: bool, s: Size) -> Self {
        if self.is_empty() || !valid(s) {
            return self.clone();
        }
        if let Self::Rectangle(r) = self {
            let (x, y) = if horizontal {
                (s.width as i64 - r.x as i64 - r.width as i64, r.y as i64)
            } else {
                (r.x as i64, s.height as i64 - r.y as i64 - r.height as i64)
            };
            if let (Ok(x), Ok(y)) = (i32::try_from(x), i32::try_from(y)) {
                return Self::Rectangle(Rect::new(x, y, r.width, r.height)).clipped(s);
            }
            return Self::from_mask(GrayImage::new(s.width, s.height));
        }
        let m = self.mask(s);
        Self::from_mask(if horizontal {
            image::imageops::flip_horizontal(&m)
        } else {
            image::imageops::flip_vertical(&m)
        })
    }
    pub fn combined(&self, o: &Self, mode: SelectionMode, s: Size) -> Self {
        if mode == SelectionMode::Replace || (self.is_empty() && mode == SelectionMode::Add) {
            return o.clone();
        }
        if !valid(s) {
            return self.clone();
        }
        Self::from_mask(GrayImage::from_fn(s.width, s.height, |x, y| {
            let p = Point {
                x: x as i32,
                y: y as i32,
            };
            let (a, b) = (self.value_at(p) as u32, o.value_at(p) as u32);
            Luma([match mode {
                SelectionMode::Add => a.max(b),
                SelectionMode::Subtract => (a * (255 - b) + 127) / 255,
                _ => (a * b + 127) / 255,
            } as u8])
        }))
    }
    pub fn inverted(&self, s: Size) -> Self {
        if !valid(s) {
            return self.clone();
        }
        let mut m = self.mask(s);
        for p in m.pixels_mut() {
            p[0] = 255 - p[0]
        }
        Self::from_mask(m)
    }
    pub fn feathered(&self, r: i32, s: Size) -> Self {
        if self.is_empty() || !(1..=128).contains(&r) || !valid(s) {
            return self.clone();
        }
        let mut m = self.mask(s);
        let window = (2 * r + 1) as u32;
        for horizontal in [true, false] {
            let mut out = GrayImage::new(s.width, s.height);
            let (lines, len) = if horizontal {
                (s.height, s.width)
            } else {
                (s.width, s.height)
            };
            for line in 0..lines {
                let get = |i: i32| -> u32 {
                    if i < 0 || i >= len as i32 {
                        0
                    } else if horizontal {
                        m.get_pixel(i as u32, line)[0] as u32
                    } else {
                        m.get_pixel(line, i as u32)[0] as u32
                    }
                };
                let mut sum: u32 = (-r..=r).map(get).sum();
                for i in 0..len {
                    let v = Luma([((sum + window / 2) / window) as u8]);
                    if horizontal {
                        out.put_pixel(i, line, v)
                    } else {
                        out.put_pixel(line, i, v)
                    }
                    sum = sum - get(i as i32 - r) + get(i as i32 + r + 1);
                }
            }
            m = out;
        }
        Self::from_mask(m)
    }
    pub fn grown(&self, amount: i32, s: Size) -> Self {
        if self.is_empty() || amount == 0 || amount.unsigned_abs() > 128 || !valid(s) {
            return self.clone();
        }
        let r = amount.abs();
        let mut m = self.mask(s);
        for horizontal in [true, false] {
            let mut out = GrayImage::new(s.width, s.height);
            let (lines, len) = if horizontal {
                (s.height, s.width)
            } else {
                (s.width, s.height)
            };
            for line in 0..lines {
                let mut q: VecDeque<(i32, u8)> = VecDeque::new();
                for i in -r..len as i32 + r {
                    let v = if i >= 0 && i < len as i32 {
                        if horizontal {
                            m.get_pixel(i as u32, line)[0]
                        } else {
                            m.get_pixel(line, i as u32)[0]
                        }
                    } else {
                        0
                    };
                    while q
                        .back()
                        .is_some_and(|p| if amount > 0 { p.1 <= v } else { p.1 >= v })
                    {
                        q.pop_back();
                    }
                    q.push_back((i, v));
                    while q.front().is_some_and(|p| p.0 < i - 2 * r) {
                        q.pop_front();
                    }
                    let at = i - r;
                    if at >= 0 && at < len as i32 {
                        let (x, y) = if horizontal {
                            (at as u32, line)
                        } else {
                            (line, at as u32)
                        };
                        out.put_pixel(x, y, Luma([q[0].1]));
                    }
                }
            }
            m = out
        }
        Self::from_mask(m)
    }
    pub fn from_alpha(im: &RgbaImage, offset: Point, s: Size) -> Self {
        if !valid(s) {
            return Self::None;
        }
        Self::from_mask(GrayImage::from_fn(s.width, s.height, |x, y| {
            let (xx, yy) = (
                i64::from(x) - i64::from(offset.x),
                i64::from(y) - i64::from(offset.y),
            );
            Luma([
                if xx >= 0 && yy >= 0 && xx < i64::from(im.width()) && yy < i64::from(im.height()) {
                    im.get_pixel(xx as u32, yy as u32)[3]
                } else {
                    0
                },
            ])
        }))
    }
    pub fn color_range(im: &RgbaImage, seed: Point, tolerance: i32, contiguous: bool) -> Self {
        let s = Size {
            width: im.width(),
            height: im.height(),
        };
        if !valid(s)
            || seed.x < 0
            || seed.y < 0
            || seed.x as u32 >= s.width
            || seed.y as u32 >= s.height
            || !(0..=255).contains(&tolerance)
        {
            return Self::None;
        }
        let sample = im.get_pixel(seed.x as u32, seed.y as u32);
        let matches = |x: u32, y: u32| {
            let p = im.get_pixel(x, y);
            let mut d = p[3].abs_diff(sample[3]);
            if p[3] > 0 || sample[3] > 0 {
                for c in 0..3 {
                    d = d.max(p[c].abs_diff(sample[c]));
                }
            }
            d as i32 <= tolerance
        };
        let mut m = GrayImage::new(s.width, s.height);
        if !contiguous {
            for (x, y, p) in m.enumerate_pixels_mut() {
                p[0] = if matches(x, y) { 255 } else { 0 }
            }
        } else {
            let mut stack = vec![(seed.x as u32, seed.y as u32)];
            while let Some((x, y)) = stack.pop() {
                if m.get_pixel(x, y)[0] > 0 || !matches(x, y) {
                    continue;
                }
                m.put_pixel(x, y, Luma([255]));
                if x > 0 {
                    stack.push((x - 1, y))
                }
                if y > 0 {
                    stack.push((x, y - 1))
                }
                if x + 1 < s.width {
                    stack.push((x + 1, y))
                }
                if y + 1 < s.height {
                    stack.push((x, y + 1))
                }
            }
        }
        Self::from_mask(m)
    }
    pub fn from_path(points: &[Point], s: Size) -> Self {
        Self::from_path_controlled(points, s, &crate::jobs::CancellationToken::default())
            .expect("An uncancelled selection rasterization cannot fail")
    }
    /// Even-odd fill with the same 4×4 sample positions as the original path
    /// rasterizer. Only edges crossing the current sub-scanline are visited.
    pub fn from_path_controlled(
        points: &[Point],
        s: Size,
        token: &crate::jobs::CancellationToken,
    ) -> crate::types::Result<Self> {
        token.check()?;
        if !valid(s) {
            return Ok(Self::None);
        }
        let mut mask = GrayImage::new(s.width, s.height);
        if points.len() < 3 {
            return Ok(Self::from_mask(mask));
        }
        let mut edges: Vec<_> = points
            .iter()
            .copied()
            .zip(points.iter().copied().cycle().skip(points.len() - 1))
            .filter(|(a, b)| a.y != b.y)
            .collect();
        edges.sort_unstable_by_key(|(a, b)| a.y.min(b.y));
        let top = points
            .iter()
            .map(|p| p.y)
            .min()
            .unwrap()
            .clamp(0, s.height as i32) as u32;
        let bottom = points
            .iter()
            .map(|p| p.y)
            .max()
            .unwrap()
            .clamp(0, s.height as i32) as u32;
        let mut next = 0;
        let mut active: Vec<usize> = Vec::new();
        let mut crossings = Vec::new();
        let mut coverage = vec![0u8; s.width as usize];
        for y in top..bottom {
            token.check()?;
            token.report(
                "Kement seçimi",
                (y - top) as usize,
                Some((bottom - top) as usize),
            );
            coverage.fill(0);
            for sy in 0..4 {
                let py = y as f64 + (sy as f64 + 0.5) / 4.;
                while next < edges.len() && edges[next].0.y.min(edges[next].1.y) as f64 <= py {
                    if edges[next].0.y.max(edges[next].1.y) as f64 > py {
                        active.push(next);
                    }
                    next += 1;
                }
                active.retain(|&i| edges[i].0.y.max(edges[i].1.y) as f64 > py);
                crossings.clear();
                for &i in &active {
                    let (a, b) = edges[i];
                    crossings.push(
                        (b.x as f64 - a.x as f64) * (py - a.y as f64) / (b.y as f64 - a.y as f64)
                            + a.x as f64,
                    );
                }
                crossings.sort_unstable_by(f64::total_cmp);
                for pair in crossings.as_chunks::<2>().0 {
                    // Sample k is at (k + 0.5)/4; left inclusive, right exclusive.
                    let first = (pair[0] * 4. - 0.5).ceil().clamp(0., s.width as f64 * 4.) as usize;
                    let end = (pair[1] * 4. - 0.5).ceil().clamp(0., s.width as f64 * 4.) as usize;
                    if first >= end {
                        continue;
                    }
                    for (x, count) in coverage
                        .iter_mut()
                        .enumerate()
                        .take((end - 1) / 4 + 1)
                        .skip(first / 4)
                    {
                        *count += (end.min(x * 4 + 4) - first.max(x * 4)) as u8;
                    }
                }
            }
            let offset = y as usize * s.width as usize;
            for (out, &count) in mask.as_mut()[offset..offset + s.width as usize]
                .iter_mut()
                .zip(&coverage)
            {
                *out = ((count as u32 * 255 + 8) / 16) as u8;
            }
        }
        token.check()?;
        token.report(
            "Kement seçimi",
            (bottom - top) as usize,
            Some((bottom - top) as usize),
        );
        Ok(Self::from_mask(mask))
    }
    pub fn ellipse(r: Rect, s: Size) -> Self {
        if !valid(s) {
            return Self::None;
        }
        Self::from_mask(GrayImage::from_fn(s.width, s.height, |x, y| {
            let mut n = 0;
            if r.width > 0 && r.height > 0 {
                for sy in 0..4 {
                    for sx in 0..4 {
                        let dx = (x as f64 + (sx as f64 + 0.5) / 4. - r.x as f64) * 2.
                            / r.width as f64
                            - 1.;
                        let dy = (y as f64 + (sy as f64 + 0.5) / 4. - r.y as f64) * 2.
                            / r.height as f64
                            - 1.;
                        if dx * dx + dy * dy <= 1. {
                            n += 1
                        }
                    }
                }
            }
            Luma([((n * 255 + 8) / 16) as u8])
        }))
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn reference_path(points: &[Point], size: Size) -> GrayImage {
        GrayImage::from_fn(size.width, size.height, |x, y| {
            let mut count = 0;
            for sy in 0..4 {
                for sx in 0..4 {
                    let px = x as f64 + (sx as f64 + 0.5) / 4.;
                    let py = y as f64 + (sy as f64 + 0.5) / 4.;
                    let mut inside = false;
                    if points.len() > 2 {
                        let mut previous = points.len() - 1;
                        for i in 0..points.len() {
                            let (a, b) = (points[i], points[previous]);
                            if (a.y as f64 > py) != (b.y as f64 > py)
                                && px
                                    < (b.x as f64 - a.x as f64) * (py - a.y as f64)
                                        / (b.y as f64 - a.y as f64)
                                        + a.x as f64
                            {
                                inside = !inside;
                            }
                            previous = i;
                        }
                    }
                    count += u32::from(inside);
                }
            }
            Luma([((count * 255 + 8) / 16) as u8])
        })
    }
    #[test]
    fn scanline_matches_supersampled_even_odd_reference() {
        let size = Size::new(24, 20);
        let mut polygons = vec![
            vec![],
            vec![Point::new(3, 3)],
            vec![
                Point::new(0, 0),
                Point::new(10, 0),
                Point::new(10, 10),
                Point::new(0, 10),
            ],
            vec![
                Point::new(-8, -5),
                Point::new(40, 25),
                Point::new(-8, 25),
                Point::new(40, -5),
            ],
            vec![
                Point::new(0, 0),
                Point::new(20, 3),
                Point::new(6, 8),
                Point::new(20, 20),
                Point::new(0, 20),
            ],
            vec![
                Point::new(i32::MIN, i32::MIN),
                Point::new(i32::MAX, i32::MIN),
                Point::new(i32::MAX, i32::MAX),
                Point::new(i32::MIN, i32::MAX),
            ],
        ];
        let mut seed = 0x7946_u32;
        for n in 0..64 {
            let mut points = Vec::new();
            for _ in 0..3 + n % 13 {
                seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
                let x = (seed % 48) as i32 - 12;
                seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
                points.push(Point::new(x, (seed % 40) as i32 - 10));
            }
            polygons.push(points);
        }
        for points in polygons {
            assert_eq!(
                Selection::from_path(&points, size).mask(size),
                reference_path(&points, size),
                "{points:?}"
            );
        }
    }
    #[test]
    fn path_selection_cancellation_returns_no_partial_selection() {
        let token = crate::jobs::CancellationToken::default();
        token.cancel();
        assert!(
            Selection::from_path_controlled(&[Point::new(0, 0); 3], Size::new(32, 32), &token)
                .is_err()
        );
        let empty = Selection::from_path(&[], Size::new(32, 32));
        assert!(!empty.is_none());
        assert!(!empty.has_pixels());
    }
    #[test]
    fn rectangle_transforms_stay_compact() {
        let s = Size::new(1000, 1000);
        let a = Selection::Rectangle(Rect::new(10, 20, 30, 40));
        assert!(matches!(a.clipped(s), Selection::Rectangle(_)));
        assert!(matches!(
            a.translated(Point::new(1, 1), s),
            Selection::Rectangle(_)
        ));
        assert!(matches!(a.rotated(true, s), Selection::Rectangle(_)));
        assert!(matches!(a.flipped(true, s), Selection::Rectangle(_)));
        assert!(matches!(
            a.cropped(Rect::new(5, 5, 100, 100), s),
            Selection::Rectangle(_)
        ));
        let empty = a.translated(Point::new(-500, 0), s);
        assert!(!empty.is_none());
        assert!(!empty.has_pixels());
    }
    #[test]
    fn mask_identity_transform_shares_buffer() {
        let a = Selection::from_mask(GrayImage::from_pixel(2, 2, Luma([127])));
        let b = a.translated(Point::default(), Size::new(2, 2));
        if let (Selection::Mask(a), Selection::Mask(b)) = (a, b) {
            assert!(Arc::ptr_eq(&a, &b))
        } else {
            panic!("mask lost")
        }
    }
    #[test]
    fn feather_uses_zero_outside() {
        let a = Selection::Rectangle(Rect::new(0, 0, 1, 1));
        let m = a.feathered(1, Size::new(3, 3)).mask(Size::new(3, 3));
        assert_eq!(m.get_pixel(0, 0)[0], 28);
        assert_eq!(m.get_pixel(1, 1)[0], 28);
        assert_eq!(m.get_pixel(2, 2)[0], 0);
    }
    #[test]
    fn grow_and_shrink_square() {
        let s = Size::new(5, 5);
        let a = Selection::Rectangle(Rect::new(2, 2, 1, 1));
        assert_eq!(a.grown(1, s).bounds(), Rect::new(1, 1, 3, 3));
        assert_eq!(a.grown(1, s).grown(-1, s).mask(s), a.mask(s));
        assert_eq!(a.grown(i32::MIN, s), a);
    }
    #[test]
    fn transforms_keep_coverage() {
        let a = Selection::from_mask(GrayImage::from_fn(2, 1, |x, _| {
            Luma([if x == 0 { 64 } else { 255 }])
        }));
        let b = a.rotated(true, Size::new(2, 1));
        assert_eq!(b.mask(Size::new(1, 2)).as_raw(), &[64, 255]);
        let c = a.translated(Point::new(-1, 0), Size::new(2, 1));
        assert_eq!(c.mask(Size::new(2, 1)).as_raw(), &[255, 0]);
    }
    #[test]
    fn zero_is_active() {
        let s = Selection::from_mask(GrayImage::new(2, 2));
        assert!(!s.is_empty());
        assert!(!s.has_pixels());
        assert_eq!(s.value_at(Point { x: 0, y: 0 }), 0);
        assert_eq!(Selection::None.value_at(Point { x: 0, y: 0 }), 255);
    }
    #[test]
    fn fractional_intersection() {
        let a = Selection::from_mask(GrayImage::from_pixel(1, 1, Luma([128])));
        assert_eq!(
            a.combined(
                &a,
                SelectionMode::Intersect,
                Size {
                    width: 1,
                    height: 1
                }
            )
            .value_at(Point { x: 0, y: 0 }),
            64
        );
    }
    #[test]
    fn transparent_rgb_not_color() {
        let im = RgbaImage::from_fn(2, 1, |x, _| image::Rgba([x as u8 * 255, 0, 0, 0]));
        assert_eq!(
            Selection::color_range(&im, Point { x: 0, y: 0 }, 0, true)
                .mask(Size {
                    width: 2,
                    height: 1
                })
                .as_raw(),
            &[255, 255]
        );
    }
}
