//! Shared, GUI-independent editor data. Coordinates and transform order match PhotoDeck v1–v7.
use image::{GrayImage, RgbaImage};
use std::sync::Arc;
pub type Result<T> = std::result::Result<T, String>;
pub type PixelBuffer = Arc<RgbaImage>;
pub type MaskBuffer = Arc<GrayImage>;
pub const MAX_PIXELS: u64 = 16 * 1024 * 1024;
pub const MAX_LAYER_BYTES: u64 = 256 * 1024 * 1024;
pub const MAX_SELECTION_BYTES: u64 = 64 * 1024 * 1024;
pub const HISTORY_BUDGET: u64 = 128 * 1024 * 1024;
pub const MAX_COORDINATE: i32 = 1 << 24;
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}
impl Point {
    pub fn new(x: i32, y: i32) -> Self {
        Self { x, y }
    }
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Size {
    pub width: u32,
    pub height: u32,
}
impl Size {
    pub fn new(width: u32, height: u32) -> Self {
        Self { width, height }
    }
    pub fn valid(self) -> bool {
        self.width > 0
            && self.height > 0
            && self.width <= 16384
            && self.height <= 16384
            && self.width as u64 * self.height as u64 <= MAX_PIXELS
    }
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}
impl Rect {
    pub fn new(x: i32, y: i32, width: u32, height: u32) -> Self {
        Self {
            x,
            y,
            width,
            height,
        }
    }
    pub fn contains(self, x: i32, y: i32) -> bool {
        x as i64 >= self.x as i64
            && y as i64 >= self.y as i64
            && (x as i64) < self.x as i64 + self.width as i64
            && (y as i64) < self.y as i64 + self.height as i64
    }
    pub fn is_empty(self) -> bool {
        self.width == 0 || self.height == 0
    }
    pub fn intersect(self, b: Self) -> Self {
        let x = self.x.max(b.x);
        let y = self.y.max(b.y);
        let r = (self.x as i64 + self.width as i64).min(b.x as i64 + b.width as i64);
        let d = (self.y as i64 + self.height as i64).min(b.y as i64 + b.height as i64);
        Self::new(
            x,
            y,
            (r - x as i64).max(0) as u32,
            (d - y as i64).max(0) as u32,
        )
    }
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Transform(pub [f64; 9]);
impl Default for Transform {
    fn default() -> Self {
        Self([1., 0., 0., 0., 1., 0., 0., 0., 1.])
    }
}
impl Transform {
    pub fn identity() -> Self {
        Self::default()
    }
    pub fn is_identity(self) -> bool {
        self == Self::default()
    }
    pub fn map(self, x: f64, y: f64) -> (f64, f64) {
        let m = self.0;
        let w = m[2] * x + m[5] * y + m[8];
        (
            (m[0] * x + m[3] * y + m[6]) / w,
            (m[1] * x + m[4] * y + m[7]) / w,
        )
    }
    /// Composition: first self, then other (Qt row-vector convention).
    pub fn then(self, other: Self) -> Self {
        let mut c = [0.; 9];
        for r in 0..3 {
            for k in 0..3 {
                for n in 0..3 {
                    c[r * 3 + k] += self.0[r * 3 + n] * other.0[n * 3 + k];
                }
            }
        }
        Self(c)
    }
    pub fn translate(x: f64, y: f64) -> Self {
        Self([1., 0., 0., 0., 1., 0., x, y, 1.])
    }
    pub fn scale(x: f64, y: f64) -> Self {
        Self([x, 0., 0., 0., y, 0., 0., 0., 1.])
    }
    pub fn rotate(degrees: f64) -> Self {
        let (s, c) = degrees.to_radians().sin_cos();
        Self([c, s, 0., -s, c, 0., 0., 0., 1.])
    }
    pub fn inverse(self) -> Option<Self> {
        let m = self.0;
        let d = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6])
            + m[2] * (m[3] * m[7] - m[4] * m[6]);
        if !d.is_finite() || d.abs() < 1e-12 {
            return None;
        }
        Some(Self([
            (m[4] * m[8] - m[5] * m[7]) / d,
            (m[2] * m[7] - m[1] * m[8]) / d,
            (m[1] * m[5] - m[2] * m[4]) / d,
            (m[5] * m[6] - m[3] * m[8]) / d,
            (m[0] * m[8] - m[2] * m[6]) / d,
            (m[2] * m[3] - m[0] * m[5]) / d,
            (m[3] * m[7] - m[4] * m[6]) / d,
            (m[1] * m[6] - m[0] * m[7]) / d,
            (m[0] * m[4] - m[1] * m[3]) / d,
        ]))
    }
    pub fn bounds(self, size: Size) -> Option<Rect> {
        if !size.valid()
            || self
                .0
                .iter()
                .any(|v| !v.is_finite() || v.abs() > MAX_COORDINATE as f64)
            || self.inverse().is_none()
        {
            return None;
        }
        let corners = [
            (0., 0.),
            (size.width as f64, 0.),
            (0., size.height as f64),
            (size.width as f64, size.height as f64),
        ];
        let ws = corners.map(|(x, y)| self.0[2] * x + self.0[5] * y + self.0[8]);
        if !(ws.iter().all(|w| *w > 1e-8) || ws.iter().all(|w| *w < -1e-8)) {
            return None;
        }
        let p = corners.map(|(x, y)| self.map(x, y));
        let x = p.iter().map(|p| p.0).fold(f64::INFINITY, f64::min).floor();
        let y = p.iter().map(|p| p.1).fold(f64::INFINITY, f64::min).floor();
        let r = p
            .iter()
            .map(|p| p.0)
            .fold(f64::NEG_INFINITY, f64::max)
            .ceil();
        let b = p
            .iter()
            .map(|p| p.1)
            .fold(f64::NEG_INFINITY, f64::max)
            .ceil();
        if x.abs() > MAX_COORDINATE as f64
            || y.abs() > MAX_COORDINATE as f64
            || r.abs() > MAX_COORDINATE as f64
            || b.abs() > MAX_COORDINATE as f64
            || r - x > 16384.
            || b - y > 16384.
        {
            return None;
        }
        let s = Size::new((r - x) as u32, (b - y) as u32);
        s.valid()
            .then_some(Rect::new(x as i32, y as i32, s.width, s.height))
    }
}
#[derive(Clone, Debug, PartialEq)]
pub struct ToneSettings {
    pub input_black: i32,
    pub input_white: i32,
    pub output_black: i32,
    pub output_white: i32,
    pub gamma: f64,
    pub exposure: f64,
    pub hue: i32,
    pub vibrance: i32,
    pub red_balance: i32,
    pub green_balance: i32,
    pub blue_balance: i32,
    pub inverted: bool,
    pub threshold: i32,
    pub posterize: i32,
    pub curves: [Vec<Point>; 4],
}
impl Default for ToneSettings {
    fn default() -> Self {
        Self {
            input_black: 0,
            input_white: 255,
            output_black: 0,
            output_white: 255,
            gamma: 1.,
            exposure: 0.,
            hue: 0,
            vibrance: 0,
            red_balance: 0,
            green_balance: 0,
            blue_balance: 0,
            inverted: false,
            threshold: -1,
            posterize: 256,
            curves: std::array::from_fn(|_| vec![Point::new(0, 0), Point::new(255, 255)]),
        }
    }
}
impl ToneSettings {
    pub fn valid(&self) -> bool {
        self.input_black >= 0
            && self.input_white <= 255
            && self.input_black < self.input_white
            && self.output_black >= 0
            && self.output_white <= 255
            && self.output_black <= self.output_white
            && self.gamma.is_finite()
            && (0.1..=10.).contains(&self.gamma)
            && self.exposure.is_finite()
            && (-10. ..=10.).contains(&self.exposure)
            && (-180..=180).contains(&self.hue)
            && [
                self.vibrance,
                self.red_balance,
                self.green_balance,
                self.blue_balance,
            ]
            .iter()
            .all(|n| (-100..=100).contains(n))
            && (-1..=255).contains(&self.threshold)
            && (2..=256).contains(&self.posterize)
            && self.curves.iter().all(|c| {
                c.len() >= 2
                    && c.len() <= 64
                    && c.first().unwrap().x == 0
                    && c.last().unwrap().x == 255
                    && c.iter()
                        .all(|p| (0..=255).contains(&p.x) && (0..=255).contains(&p.y))
                    && c.windows(2).all(|p| p[0].x < p[1].x)
            })
    }
    pub fn is_identity(&self) -> bool {
        self == &Self::default()
    }
}
#[derive(Clone, Debug, PartialEq)]
pub struct FilterSettings {
    pub gaussian: i32,
    pub sharpen_amount: i32,
    pub sharpen_radius: i32,
    pub sharpen_threshold: i32,
    pub high_pass: i32,
    pub noise: i32,
    pub mosaic: i32,
    pub monochrome_noise: bool,
    pub noise_seed: u32,
}
impl Default for FilterSettings {
    fn default() -> Self {
        Self {
            gaussian: 0,
            sharpen_amount: 0,
            sharpen_radius: 2,
            sharpen_threshold: 0,
            high_pass: 0,
            noise: 0,
            mosaic: 1,
            monochrome_noise: true,
            noise_seed: 1,
        }
    }
}
impl FilterSettings {
    pub fn valid(&self) -> bool {
        (0..=16).contains(&self.gaussian)
            && (0..=300).contains(&self.sharpen_amount)
            && (1..=16).contains(&self.sharpen_radius)
            && (0..=255).contains(&self.sharpen_threshold)
            && (0..=16).contains(&self.high_pass)
            && (0..=100).contains(&self.noise)
            && (1..=128).contains(&self.mosaic)
    }
    pub fn is_identity(&self) -> bool {
        self.gaussian == 0
            && self.sharpen_amount == 0
            && self.high_pass == 0
            && self.noise == 0
            && self.mosaic == 1
    }
}
#[derive(Clone, Debug, PartialEq)]
pub struct MaskSettings {
    pub enabled: bool,
    pub density: i32,
    pub feather: i32,
}
impl Default for MaskSettings {
    fn default() -> Self {
        Self {
            enabled: true,
            density: 100,
            feather: 0,
        }
    }
}
impl MaskSettings {
    pub fn valid(&self) -> bool {
        (0..=100).contains(&self.density) && (0..=16).contains(&self.feather)
    }
}
#[derive(Clone, Debug, PartialEq, Default)]
pub enum Selection {
    #[default]
    None,
    Rectangle(Rect),
    Mask(MaskBuffer),
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum SelectionMode {
    #[default]
    Replace,
    Add,
    Subtract,
    Intersect,
}
#[derive(Clone, Debug, PartialEq)]
pub struct SavedSelection {
    pub name: String,
    pub selection: Selection,
}
#[derive(Clone, Debug, PartialEq)]
pub struct Layer {
    pub name: String,
    pub pixels: PixelBuffer,
    pub mask: Option<MaskBuffer>,
    pub offset: Point,
    pub geometry: Transform,
    pub visible: bool,
    pub locked: bool,
    pub clipped: bool,
    pub opacity: f64,
    pub blend: u8,
    pub brightness: i32,
    pub contrast: i32,
    pub saturation: i32,
    pub blur: i32,
    pub tones: ToneSettings,
    pub filters: FilterSettings,
    pub mask_settings: MaskSettings,
    pub text: String,
    pub font: String,
    pub color: [u8; 4],
    pub smart_object_id: [u8; 16],
    pub smart_object_size: Size,
}
impl Layer {
    pub fn new(name: impl Into<String>, pixels: RgbaImage) -> Self {
        Self {
            name: name.into(),
            pixels: Arc::new(pixels),
            mask: None,
            offset: Point::default(),
            geometry: Transform::default(),
            visible: true,
            locked: false,
            clipped: false,
            opacity: 1.,
            blend: 0,
            brightness: 0,
            contrast: 0,
            saturation: 0,
            blur: 0,
            tones: ToneSettings::default(),
            filters: FilterSettings::default(),
            mask_settings: MaskSettings::default(),
            text: String::new(),
            font: "Sans Serif,32,-1,5,400,0,0,0,0,0,0,0,0,0,0,1".into(),
            color: [255; 4],
            smart_object_id: [0; 16],
            smart_object_size: Size::default(),
        }
    }
    pub fn is_smart_object(&self) -> bool {
        self.smart_object_id != [0; 16]
    }
    pub fn source_size(&self) -> Size {
        if self.is_smart_object() {
            self.smart_object_size
        } else {
            Size::new(self.pixels.width(), self.pixels.height())
        }
    }
    pub fn bounds(&self) -> Option<Rect> {
        let mut b = self.geometry.bounds(self.source_size())?;
        b.x = b.x.checked_add(self.offset.x)?;
        b.y = b.y.checked_add(self.offset.y)?;
        if b.x.unsigned_abs() > MAX_COORDINATE as u32 || b.y.unsigned_abs() > MAX_COORDINATE as u32
        {
            return None;
        }
        Some(b)
    }
}
#[derive(Clone, Debug, PartialEq)]
pub struct State {
    pub size: Size,
    pub layers: Vec<Layer>,
    pub active: usize,
    pub selection: Selection,
    pub previous_selection: Selection,
    pub saved_selections: Vec<SavedSelection>,
}
#[derive(Clone, Debug, PartialEq)]
pub struct BrushSettings {
    pub size: i32,
    pub hardness: i32,
    pub flow: i32,
    pub spacing: i32,
    pub angle: i32,
    pub roundness: i32,
    pub pressure_minimum: i32,
    pub pressure_curve: f64,
    pub pressure_size: bool,
    pub pressure_flow: bool,
    pub tilt_angle: bool,
    pub tilt_roundness: bool,
    pub tip_mask: Option<MaskBuffer>,
}
impl Default for BrushSettings {
    fn default() -> Self {
        Self {
            size: 32,
            hardness: 80,
            flow: 100,
            spacing: 16,
            angle: 0,
            roundness: 100,
            pressure_minimum: 10,
            pressure_curve: 1.,
            pressure_size: true,
            pressure_flow: false,
            tilt_angle: false,
            tilt_roundness: false,
            tip_mask: None,
        }
    }
}
impl BrushSettings {
    pub fn valid(&self) -> bool {
        (1..=512).contains(&self.size)
            && (0..=100).contains(&self.hardness)
            && (1..=100).contains(&self.flow)
            && (1..=200).contains(&self.spacing)
            && (-180..=180).contains(&self.angle)
            && (5..=100).contains(&self.roundness)
            && (1..=100).contains(&self.pressure_minimum)
            && self.pressure_curve.is_finite()
            && (0.25..=4.).contains(&self.pressure_curve)
            && self.tip_mask.as_ref().is_none_or(|m| {
                m.width() > 0
                    && m.height() > 0
                    && m.width() <= 512
                    && m.height() <= 512
                    && m.pixels().any(|p| p[0] > 0)
            })
    }
}
#[derive(Clone, Copy, Debug)]
pub struct StrokeSample {
    pub x: f64,
    pub y: f64,
    pub pressure: f64,
    pub tilt_x: f64,
    pub tilt_y: f64,
}
impl StrokeSample {
    pub fn mouse(x: f64, y: f64) -> Self {
        Self {
            x,
            y,
            pressure: 1.,
            tilt_x: 0.,
            tilt_y: 0.,
        }
    }
}
#[derive(Clone, Debug, PartialEq)]
pub struct BrushPreset {
    pub name: String,
    pub tool: u8,
    pub settings: BrushSettings,
}
pub const BLEND_NAMES: [&str; 27] = [
    "Normal",
    "Çarp",
    "Ekran",
    "Kaplama",
    "Koyulaştır",
    "Açıklaştır",
    "Dağıt",
    "Renk yakma",
    "Doğrusal yakma",
    "Daha koyu renk",
    "Renk soldurma",
    "Doğrusal soldurma",
    "Daha açık renk",
    "Yumuşak ışık",
    "Sert ışık",
    "Canlı ışık",
    "Doğrusal ışık",
    "Pin ışığı",
    "Sert karışım",
    "Fark",
    "Dışlama",
    "Çıkar",
    "Böl",
    "Ton",
    "Doygunluk",
    "Renk",
    "Parlaklık",
];
