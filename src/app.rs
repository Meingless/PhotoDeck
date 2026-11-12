mod pro;
mod tablet_ui;
mod work;
use eframe::egui::{self, Color32, Pos2, Sense, Stroke, Vec2};
use image::{Rgba, RgbaImage};
use photodeck::{brush, document::Document, output, project, render, types::*};
use std::{
    path::{Path, PathBuf},
    sync::Arc,
    time::{Duration, Instant},
};
use work::absolute_path;

#[derive(Default)]
pub struct LaunchOptions {
    pub no_settings: bool,
    pub preset: Option<String>,
    pub screenshot: Option<PathBuf>,
    pub files: Vec<PathBuf>,
    pub ui_scale: Option<f32>,
    pub window_size: Option<[f32; 2]>,
}
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Tool {
    Move,
    Select,
    EllipseSelect,
    Lasso,
    Polygon,
    Wand,
    Crop,
    Brush,
    Pencil,
    Eraser,
    Clone,
    Fill,
    Gradient,
    Text,
    Rectangle,
    Ellipse,
    Picker,
    Hand,
}
const TOOLS: [(Tool, &str, &str); 18] = [
    (Tool::Move, "↖", "Taşı · V"),
    (Tool::Select, "▧", "Dikdörtgen seçim · M"),
    (Tool::EllipseSelect, "◌", "Eliptik seçim · Shift+M"),
    (Tool::Lasso, "⌁", "Serbest kement · L"),
    (Tool::Polygon, "△", "Çokgen kement · Shift+L"),
    (Tool::Wand, "✦", "Sihirli değnek · W"),
    (Tool::Crop, "C", "Kırp · C"),
    (Tool::Brush, "●", "Fırça · B"),
    (Tool::Pencil, "╱", "Kalem · P"),
    (Tool::Eraser, "▱", "Silgi · E"),
    (Tool::Clone, "♧", "Klon · S · Alt+tıklama: kaynak"),
    (Tool::Fill, "▨", "Boya kovası · F"),
    (Tool::Gradient, "▥", "Degrade · G"),
    (Tool::Text, "T", "Metin · T"),
    (Tool::Rectangle, "□", "Dikdörtgen · U"),
    (Tool::Ellipse, "○", "Elips · O"),
    (Tool::Picker, "I", "Damlalık · I"),
    (Tool::Hand, "✋", "El · H"),
];
const PRESETS: [(&str, &str); 11] = [
    ("graphite", "Grafit"),
    ("paper", "Kağıt"),
    ("focus", "Odak"),
    ("film", "Fotoğraf Masası"),
    ("workbench", "Çalışma Tezgahı"),
    ("ribbon", "Şerit"),
    ("ink", "Çizim Atölyesi"),
    ("compact", "Kompakt"),
    ("touch", "Dokunmatik"),
    ("modular", "Modüler"),
    ("classic-pro", "Klasik Pro"),
];
#[derive(serde::Serialize, serde::Deserialize)]
#[serde(default)]
struct UiPrefs {
    #[serde(default)]
    ui_version: u32,
    preset: String,
    scale: f32,
    recent: Vec<PathBuf>,
    show_tips: bool,
    panel_width: f32,
    font_size: f32,
    tools: bool,
    layers: bool,
    accent: [u8; 3],
    legacy: serde_json::Map<String, serde_json::Value>,
    named: Vec<(String, serde_json::Value)>,
}
impl Default for UiPrefs {
    fn default() -> Self {
        Self {
            ui_version: 1,
            scale: 1.,
            recent: vec![],
            show_tips: true,
            preset: "classic-pro".into(),
            panel_width: 260.,
            font_size: 12.,
            tools: true,
            layers: true,
            accent: [89, 201, 180],
            legacy: Default::default(),
            named: vec![],
        }
    }
}
struct Tab {
    doc: Document,
    source_path: Option<PathBuf>,
    generation: u64,
    selection_key: Option<(u64, u64)>,
    file: Option<project::ProjectFile>,
    texture: Option<egui::TextureHandle>,
    selection_texture: Option<egui::TextureHandle>,
    rendered_revision: u64,
    rendered_generation: u64,
    invalid: bool,
    zoom: f32,
    fit_to_view: bool,
    pan: Vec2,
    id: u64,
    last_change: Instant,
    recovered_revision: u64,
    recovery_origin: Option<PathBuf>,
    has_recovery: bool,
}
impl Tab {
    fn new(doc: Document, id: u64) -> Self {
        Self {
            doc,
            source_path: None,
            generation: 0,
            selection_key: None,
            file: None,
            texture: None,
            selection_texture: None,
            rendered_revision: 0,
            rendered_generation: u64::MAX,
            invalid: true,
            zoom: 0.,
            fit_to_view: true,
            pan: Vec2::ZERO,
            id,
            last_change: Instant::now(),
            recovered_revision: 0,
            recovery_origin: None,
            has_recovery: false,
        }
    }
}
struct Gesture {
    start: (f64, f64),
    last: (f64, f64),
    pan_start: Vec2,
    points: Vec<(f64, f64)>,
    samples: Vec<StrokeSample>,
    original: Layer,
    handle: i32,
}
#[derive(Clone, Copy, PartialEq)]
enum Dialog {
    New,
    Export,
    Pdf,
    Text,
    Transform,
    Canvas,
    Resize,
    Effects,
    Brush,
    Appearance,
    About,
    Batch,
    Help,
}
pub struct PhotoDeck {
    pro: pro::ProUi,
    work: work::Background,
    tablet: tablet_ui::TabletState,
    tabs: Vec<Tab>,
    active: usize,
    next_id: u64,
    tool: Tool,
    foreground: [u8; 4],
    background: [u8; 4],
    brush: BrushSettings,
    mode: SelectionMode,
    tolerance: u8,
    contiguous: bool,
    gesture: Option<Gesture>,
    polygon: Vec<(f64, f64)>,
    clone_anchor: Option<(f64, f64)>,
    dialog: Option<Dialog>,
    error: Option<String>,
    status: String,
    prefs: UiPrefs,
    options: LaunchOptions,
    close_tab: Option<u64>,
    closing: bool,
    save_conflict: Option<(u64, PathBuf, bool)>,
    recovery: Option<photodeck::recovery::Recovery>,
    recoveries: Vec<photodeck::recovery::RecoveryCandidate>,
    width: u32,
    height: u32,
    white: bool,
    keep_ratio: bool,
    canvas_anchor: Point,
    quality: u8,
    dpi: u32,
    profile: usize,
    paper: u8,
    landscape: bool,
    margin: f64,
    text: String,
    font: String,
    font_size: f32,
    font_bold: bool,
    batch_format: String,
    batch_upscale: bool,
    batch_adjust: [i32; 3],
    preset_name: String,
    text_at: Point,
    edit_text: bool,
    transform_values: [f64; 7],
    transforming: bool,
    selection_name: String,
    brush_name: String,
    brush_presets: Vec<BrushPreset>,
    brush_store: Option<photodeck::settings::BrushStore>,
    screenshot_requested: bool,
    frames: u32,
    canvas_id: Option<egui::Id>,
    recovery_retry: Instant,
    effects_original: Option<Layer>,
    effects_draft: Option<Layer>,
    live_preview: bool,
    nudge_active: bool,
}
impl PhotoDeck {
    pub fn new(cc: &eframe::CreationContext<'_>, options: LaunchOptions) -> Self {
        let mut app = Self::with_context(&cc.egui_ctx, options);
        // SAFETY: TabletInput is owned by this App. eframe drops the App before
        // its root window/display, and the manager never enters a worker thread.
        match unsafe { photodeck::platform::tablet::TabletInput::new(cc) } {
            Ok(input) => {
                app.tablet.status =
                    format!("{} · basınç ve eğim destekleniyor", input.backend_name());
                app.tablet.input = Some(input);
            }
            Err(error) => app.tablet.status = error,
        }
        app
    }
    fn with_context(ctx: &egui::Context, options: LaunchOptions) -> Self {
        install_ui_fonts(ctx);
        let mut startup_error = None;
        let mut prefs = UiPrefs::default();
        if !options.no_settings
            && let Some(path) = prefs_path().filter(|p| p.exists())
        {
            let result = bounded_read(&path, 16 * 1024 * 1024)
                .and_then(|b| serde_json::from_slice::<UiPrefs>(&b).map_err(|e| e.to_string()))
                .and_then(|p| {
                    p.validate()?;
                    Ok(p)
                });
            match result {
                Ok(p) => prefs = p,
                Err(e) => startup_error = Some(e),
            }
        }
        prefs.migrate();
        if let Some(scale) = options.ui_scale {
            prefs.scale = scale;
        }
        if let Some(p) = &options.preset {
            prefs.preset = p.clone();
        }
        let recovery = if options.no_settings {
            None
        } else {
            match photodeck::recovery::Recovery::new() {
                Ok(r) => Some(r),
                Err(e) => {
                    startup_error = Some(e);
                    None
                }
            }
        };
        let recoveries = recovery
            .as_ref()
            .and_then(|r| r.candidates().ok())
            .unwrap_or_default();
        let mut app = Self {
            pro: pro::ProUi::new(ctx),
            work: work::Background::default(),
            tablet: tablet_ui::TabletState::default(),
            tabs: vec![],
            active: 0,
            next_id: 1,
            tool: Tool::Move,
            foreground: [89, 201, 180, 255],
            background: [255; 4],
            brush: BrushSettings::default(),
            mode: SelectionMode::Replace,
            tolerance: 24,
            contiguous: true,
            gesture: None,
            polygon: vec![],
            clone_anchor: None,
            dialog: None,
            error: startup_error,
            status: "Yerel çalışma alanı · Rust".into(),
            prefs,
            options,
            close_tab: None,
            closing: false,
            save_conflict: None,
            recovery,
            recoveries,
            width: 1280,
            height: 800,
            white: false,
            keep_ratio: true,
            canvas_anchor: Point::new(1, 1),
            quality: 92,
            dpi: 300,
            profile: 0,
            paper: 0,
            landscape: false,
            margin: 10.,
            text: String::new(),
            font: "Sans Serif".into(),
            font_size: 48.,
            font_bold: false,
            batch_format: "png".into(),
            batch_upscale: false,
            batch_adjust: [0; 3],
            preset_name: "Kişisel görünüm".into(),
            text_at: Point::new(100, 100),
            edit_text: false,
            transform_values: [100., 100., 0., 0., 0., 0., 0.],
            transforming: false,
            selection_name: "Seçim 1".into(),
            brush_name: "Yeni fırça".into(),
            brush_presets: photodeck::settings::builtins(),
            brush_store: None,
            screenshot_requested: false,
            frames: 0,
            canvas_id: None,
            recovery_retry: Instant::now(),
            effects_original: None,
            effects_draft: None,
            live_preview: true,
            nudge_active: false,
        };
        app.apply_style(ctx);
        if !app.options.no_settings {
            match photodeck::settings::BrushStore::open() {
                Ok(store) => {
                    app.brush_presets = store.presets.clone();
                    app.brush_store = Some(store)
                }
                Err(e) => app.error = Some(e),
            }
        }
        for p in app.options.files.clone() {
            app.open(&p, false);
        }
        if app.tabs.is_empty() {
            app.push(demo_document());
        }
        app
    }
    fn push(&mut self, d: Document) {
        let id = self.next_id;
        self.next_id += 1;
        self.tabs.push(Tab::new(d, id));
        self.active = self.tabs.len() - 1;
    }
    // All ordinary input/navigation shares this gate. Guard buttons resolve their
    // own stable document IDs directly instead of acting on the active tab.
    fn modal_active(&self) -> bool {
        self.dialog.is_some()
            || self.close_tab.is_some()
            || self.save_conflict.is_some()
            || self.work.user_busy()
    }
    fn tab_index(&self, id: u64) -> Option<usize> {
        self.tabs.iter().position(|t| t.id == id)
    }
    fn doc(&self) -> &Document {
        &self.tabs[self.active].doc
    }
    fn apply_style(&self, ctx: &egui::Context) {
        ctx.set_zoom_factor(self.prefs.scale);
        let theme = self
            .prefs
            .legacy
            .get("theme")
            .and_then(|v| v.as_str())
            .unwrap_or("preset");
        let light = theme == "light"
            || (theme != "dark"
                && matches!(self.prefs.preset.as_str(), "paper" | "ribbon" | "touch"));
        let mut visuals = if light {
            egui::Visuals::light()
        } else {
            egui::Visuals::dark()
        };
        let a = self.prefs.accent;
        visuals.selection.bg_fill = Color32::from_rgb(a[0], a[1], a[2]).gamma_multiply(0.35);
        visuals.selection.stroke = Stroke::new(1.0_f32, Color32::from_rgb(a[0], a[1], a[2]));
        let palette = match self.prefs.preset.as_str() {
            "classic-pro" => [48, 50, 54],
            "film" => [48, 40, 37],
            "workbench" => [43, 57, 75],
            "ink" => [53, 43, 62],
            "modular" => [53, 64, 51],
            "compact" => [40, 52, 60],
            _ => [37, 42, 50],
        };
        visuals.panel_fill = self
            .prefs
            .color("panel", if light { [255, 253, 248] } else { palette });
        visuals.window_fill = visuals.panel_fill;
        visuals.extreme_bg_color = self.prefs.color(
            "background",
            if light { [245, 243, 239] } else { [27, 31, 37] },
        );
        ctx.set_visuals(visuals);
        let mut style = (*ctx.style()).clone();
        style.spacing.item_spacing = Vec2::new(8., 7.);
        style.spacing.button_padding = Vec2::new(9., 6.);
        if matches!(self.prefs.preset.as_str(), "compact" | "classic-pro") {
            style.spacing.item_spacing = Vec2::new(4., 3.);
            style.spacing.button_padding = Vec2::new(6., 4.);
            style.spacing.interact_size.y = 24.;
            style.visuals.widgets.inactive.corner_radius = egui::CornerRadius::same(2);
            style.visuals.widgets.hovered.corner_radius = egui::CornerRadius::same(2);
            style.visuals.widgets.active.corner_radius = egui::CornerRadius::same(2);
        }
        if self.prefs.preset == "touch" {
            style.spacing.button_padding = Vec2::new(14., 12.);
        }
        for font in style.text_styles.values_mut() {
            font.size = self.prefs.font_size;
        }
        ctx.set_style(style);
    }
    fn run(&mut self, f: impl FnOnce(&mut Document) -> Result<()>) {
        let r = f(&mut self.tabs[self.active].doc);
        self.result(r);
        self.invalidate();
    }
    fn result<T>(&mut self, r: Result<T>) -> Option<T> {
        match r {
            Ok(v) => Some(v),
            Err(e) => {
                self.error = Some(e);
                None
            }
        }
    }
    fn invalidate(&mut self) {
        let t = &mut self.tabs[self.active];
        t.invalid = true;
        t.generation = t.generation.wrapping_add(1);
        t.last_change = Instant::now();
    }
    fn prepare_navigation(&mut self) {
        if self.gesture.is_some() {
            self.finish_gesture();
        }
        if self.transforming {
            self.finish_transform(true);
        }
        self.nudge_active = false;
        if self.doc().is_editing() {
            if self.dialog == Some(Dialog::Effects)
                && let Some(layer) = self.effects_draft.take()
            {
                let i = self.doc_active();
                self.tabs[self.active].doc.state.layers[i] = layer;
            }
            let r = commit_before_navigation(&mut self.tabs[self.active].doc);
            self.result(r);
            self.dialog = None;
            self.invalidate();
        }
        self.polygon.clear();
    }
    fn open(&mut self, path: &Path, as_layer: bool) {
        if self.modal_active() {
            return;
        }
        if !self.tabs.is_empty() {
            self.prepare_navigation();
        }
        if as_layer {
            match project::read_image(path) {
                Ok(image) => {
                    let name = path
                        .file_stem()
                        .unwrap_or_default()
                        .to_string_lossy()
                        .into_owned();
                    self.run(|d| d.insert_layer(Layer::new(name, image)));
                }
                Err(e) => self.error = Some(e),
            }
            return;
        }
        if let Ok(canon) = path.canonicalize()
            && let Some(i) = self.tabs.iter().position(|t| {
                t.file
                    .as_ref()
                    .is_some_and(|f| f.path.canonicalize().ok().as_ref() == Some(&canon))
            })
        {
            self.active = i;
            return;
        }
        if path
            .extension()
            .is_some_and(|e| e.eq_ignore_ascii_case("pdeck"))
        {
            match project::open(path) {
                Ok((d, f)) => {
                    self.push(d);
                    self.tabs[self.active].source_path = Some(absolute_path(&f.path));
                    self.remember_recent(&f.path);
                    self.tabs[self.active].file = Some(f)
                }
                Err(e) => self.error = Some(e),
            }
        } else {
            match project::read_image(path) {
                Ok(image) => {
                    let size = Size::new(image.width(), image.height());
                    match Document::new(size, false) {
                        Ok(mut d) => {
                            d.state.layers = vec![Layer::new("Görüntü", image)];
                            d.title = path
                                .file_stem()
                                .unwrap_or_default()
                                .to_string_lossy()
                                .into_owned();
                            match Document::from_state(d.state.clone()) {
                                Ok(mut ready) => {
                                    ready.title = d.title;
                                    self.push(ready);
                                    self.tabs[self.active].source_path = Some(absolute_path(path));
                                    self.remember_recent(path)
                                }
                                Err(e) => self.error = Some(e),
                            }
                        }
                        Err(e) => self.error = Some(e),
                    }
                }
                Err(e) => self.error = Some(e),
            }
        }
    }
    fn choose_open(&mut self, layer: bool) {
        if let Some(files) = rfd::FileDialog::new()
            .add_filter(
                "Görüntü ve PhotoDeck",
                &[
                    "pdeck", "png", "jpg", "jpeg", "webp", "bmp", "tif", "tiff", "gif",
                ],
            )
            .pick_files()
        {
            for p in files {
                self.open(&p, layer)
            }
        }
    }
    fn save(&mut self, copy: bool) -> bool {
        self.save_document(self.tabs[self.active].id, copy)
    }
    fn save_document(&mut self, id: u64, copy: bool) -> bool {
        let Some(index) = self.tab_index(id) else {
            return false;
        };
        let existing = self.tabs[index].file.as_ref().map(|f| f.path.clone());
        let path = if copy || existing.is_none() {
            rfd::FileDialog::new()
                .add_filter("PhotoDeck", &["pdeck"])
                .set_file_name(format!("{}.pdeck", self.tabs[index].doc.title))
                .save_file()
        } else {
            existing
        };
        path.is_some_and(|p| self.save_to_document(id, p, false, false))
    }
    fn save_to_document(&mut self, id: u64, path: PathBuf, overwrite: bool, upgrade: bool) -> bool {
        let Some(index) = self.tab_index(id) else {
            return false;
        };
        let t = &mut self.tabs[index];
        match project::save(
            &mut t.doc,
            &path,
            t.file.as_ref(),
            project::SaveOptions {
                overwrite_external: overwrite,
                upgrade_format: upgrade,
            },
        ) {
            Ok(file) => {
                t.source_path = Some(absolute_path(&file.path));
                t.file = Some(file);
                self.status = format!("Kaydedildi: {}", path.display());
                if let Some(r) = &mut self.recovery
                    && let Err(e) = r.remove(t.id)
                {
                    self.error = Some(e);
                }
                if let Some(path) = t.recovery_origin.take()
                    && let Err(e) = photodeck::recovery::discard(&path)
                {
                    self.error = Some(e);
                }
                self.remember_recent(&path);
                true
            }
            Err(project::SaveError::ExternalModification) => {
                self.save_conflict = Some((id, path, false));
                false
            }
            Err(project::SaveError::FormatUpgradeRequired) => {
                self.save_conflict = Some((id, path, true));
                false
            }
            Err(e) => {
                self.error = Some(e.to_string());
                false
            }
        }
    }
    fn request_close(&mut self, index: usize) {
        if self.modal_active() {
            return;
        }
        self.prepare_navigation();
        if self.tabs[index].doc.dirty {
            self.close_tab = Some(self.tabs[index].id);
        } else {
            self.remove_tab(index);
        }
    }
    fn request_app_close(&mut self, ctx: &egui::Context) {
        if self.modal_active() {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            return;
        }
        self.prepare_navigation();
        if self.tabs.iter().any(|t| t.doc.dirty) {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            self.closing = true;
        } else {
            self.persist_ui();
        }
    }
    fn remove_tab(&mut self, index: usize) {
        if let Some(r) = &mut self.recovery
            && let Err(e) = r.remove(self.tabs[index].id)
        {
            self.error = Some(e);
        }
        if let Some(path) = self.tabs[index].recovery_origin.take()
            && let Err(e) = photodeck::recovery::discard(&path)
        {
            self.error = Some(e);
        }
        self.tabs.remove(index);
        if self.tabs.is_empty() {
            self.push(Document::new(Size::new(1280, 800), false).unwrap());
        }
        self.active = self.active.min(self.tabs.len() - 1);
    }
    fn action(&mut self, name: &str) {
        if self.modal_active() {
            return;
        }
        if self.gesture.is_some() {
            self.finish_gesture();
        }
        if self.transforming && !matches!(name, "transform" | "numeric") {
            self.finish_transform(true);
        }
        match name {
            "new" => self.dialog = Some(Dialog::New),
            "open" => self.choose_open(false),
            "import" => self.choose_open(true),
            "save" => {
                self.save(false);
            }
            "saveas" => {
                self.save(true);
            }
            "close" => self.request_close(self.active),
            "export" => {
                self.width = self.doc().state.size.width;
                self.height = self.doc().state.size.height;
                self.dialog = Some(Dialog::Export)
            }
            "pdf" => self.dialog = Some(Dialog::Pdf),
            "batch" => self.dialog = Some(Dialog::Batch),
            "undo" => {
                self.tabs[self.active].doc.undo();
                self.invalidate()
            }
            "redo" => {
                self.tabs[self.active].doc.redo();
                self.invalidate()
            }
            "copy" => self.clipboard_copy(),
            "paste" => self.clipboard_paste(),
            "layer" => self.run(|d| d.add_layer("Piksel katmanı")),
            "clipping" => {
                let clipped = !self.doc().active_layer().clipped;
                self.run(|d| d.set_clipping(clipped));
            }
            "duplicate" => self.run(|d| d.layer_via_selection(false)),
            "cut" => self.run(|d| d.layer_via_selection(true)),
            "delete" => self.run(|d| d.delete_layer()),
            "merge" => self.run(|d| d.merge_down()),
            "mergevisible" => self.run(|d| d.merge_visible()),
            "flatten" => self.run(|d| d.flatten()),
            "mask" => self.run(|d| d.add_mask()),
            "invertmask" => self.run(|d| d.invert_mask()),
            "removemask" => self.run(|d| d.remove_mask()),
            "smart" => self.run(|d| d.convert_to_smart_object()),
            "rasterize" => self.run(|d| d.rasterize_layer()),
            "replace" => {
                if let Some(p) = rfd::FileDialog::new().pick_file()
                    && let Some(im) = self.result(project::read_image(&p))
                {
                    self.run(|d| d.replace_smart_object_contents(im));
                }
            }
            "selectall" => {
                let s = self.doc().state.size;
                self.run(|d| {
                    d.set_selection(Selection::Rectangle(Rect::new(0, 0, s.width, s.height)))
                })
            }
            "deselect" => self.run(|d| d.deselect()),
            "reselect" => self.run(|d| d.reselect()),
            "invertselection" => {
                let s = self.doc().state.clone();
                let selection = s.selection.inverted(s.size);
                self.run(|d| d.set_selection(selection));
            }
            "copytransform" => {
                let state = self.doc().selection_copy_state();
                if let Some(state) = self.result(state) {
                    self.begin_transform();
                    if self.transforming {
                        self.tabs[self.active].doc.state = state;
                        self.invalidate();
                    }
                }
            }
            "transform" => self.begin_transform(),
            "numeric" => {
                self.begin_transform();
                if self.transforming {
                    self.dialog = Some(Dialog::Transform)
                }
            }
            "resize" | "canvas" => {
                self.width = self.doc().state.size.width;
                self.height = self.doc().state.size.height;
                self.dialog = Some(if name == "resize" {
                    Dialog::Resize
                } else {
                    Dialog::Canvas
                })
            }
            "rotate" => self.run(|d| d.rotate(true)),
            "fliph" => self.run(|d| d.flip(true)),
            "flipv" => self.run(|d| d.flip(false)),
            "effects" => {
                if self.doc().active_layer().locked {
                    self.error = Some("Katman kilitli.".into());
                    return;
                }
                let r = self.tabs[self.active].doc.begin_edit();
                if self.result(r).is_some() {
                    self.effects_original = Some(self.doc().active_layer().clone());
                    self.effects_draft = self.effects_original.clone();
                    self.live_preview = true;
                    self.dialog = Some(Dialog::Effects)
                }
            }
            "text" => {
                self.edit_text = false;
                self.text.clear();
                self.dialog = Some(Dialog::Text)
            }
            "brush" => self.dialog = Some(Dialog::Brush),
            "appearance" => self.dialog = Some(Dialog::Appearance),
            "fit" => {
                self.tabs[self.active].zoom = 0.;
                self.tabs[self.active].fit_to_view = true;
                self.tabs[self.active].pan = Vec2::ZERO
            }
            "actual" => {
                self.tabs[self.active].zoom = 1.;
                self.tabs[self.active].fit_to_view = false;
            }
            "about" => self.dialog = Some(Dialog::About),
            "help" => self.dialog = Some(Dialog::Help),
            _ => {}
        }
    }
    fn clipboard_copy(&mut self) {
        let im = render::composite(&self.doc().state);
        match arboard::Clipboard::new().and_then(|mut c| {
            c.set_image(arboard::ImageData {
                width: im.width() as usize,
                height: im.height() as usize,
                bytes: std::borrow::Cow::Owned(im.into_raw()),
            })
        }) {
            Ok(_) => self.status = "Görüntü panoya kopyalandı".into(),
            Err(e) => self.error = Some(e.to_string()),
        }
    }
    fn clipboard_paste(&mut self) {
        match arboard::Clipboard::new().and_then(|mut c| c.get_image()) {
            Ok(im) => {
                if let Some(p) =
                    RgbaImage::from_raw(im.width as u32, im.height as u32, im.bytes.into_owned())
                {
                    self.run(|d| d.insert_layer(Layer::new("Pano", p)));
                }
            }
            Err(e) => self.error = Some(e.to_string()),
        }
    }
    fn begin_transform(&mut self) {
        if self.transforming {
            return;
        }
        if self.doc().active_layer().locked {
            self.error = Some("Katman kilitli.".into());
            return;
        }
        let r = self.tabs[self.active].doc.begin_edit();
        if self.result(r).is_some() {
            self.transforming = true;
            self.tool = Tool::Move;
            self.transform_values = [100., 100., 0., 0., 0., 0., 0.];
        }
    }
    fn finish_transform(&mut self, accept: bool) {
        if !self.transforming {
            return;
        }
        if accept {
            if !self.commit_dialog_edit("Katman dönüşümü") {
                self.gesture = None;
                return;
            }
        } else {
            self.tabs[self.active].doc.cancel_edit();
        }
        self.transforming = false;
        self.gesture = None;
        self.invalidate();
    }
    fn commit_dialog_edit(&mut self, label: &str) -> bool {
        match self.tabs[self.active].doc.commit(label) {
            Ok(()) => true,
            Err(error) => {
                // A failed commit restores the original state and consumes the
                // transaction. Re-arm on that restored state before any retry
                // can write a new draft; otherwise commit would silently no-op.
                let retry = self.tabs[self.active].doc.begin_edit();
                self.error = Some(match retry {
                    Ok(()) => error,
                    Err(retry) => format!("{error}\nDüzenleme yeniden başlatılamadı: {retry}"),
                });
                self.invalidate();
                false
            }
        }
    }
    fn apply_effects(&mut self) {
        if let Some(layer) = self.effects_draft.clone() {
            let active = self.doc_active();
            self.tabs[self.active].doc.state.layers[active] = layer;
        }
        if self.commit_dialog_edit("Renk / filtreler") {
            self.effects_draft = None;
            self.dialog = None;
            self.invalidate();
        }
    }
    fn menus(&mut self, ctx: &egui::Context) {
        let mut action = None;
        let mut recent_open = None;
        egui::TopBottomPanel::top("menu").show(ctx, |ui| {
            if self.modal_active() {
                ui.disable();
            }
            egui::MenuBar::new().ui(ui, |ui| {
                ui.add(egui::Image::new((self.pro.logo.id(), Vec2::splat(22.))))
                    .on_hover_text("PhotoDeck");
                if ui.available_width() > 650. {
                    ui.strong("PhotoDeck");
                }
                ui.separator();
                for (title, items) in [
                    (
                        "Dosya",
                        vec![
                            ("Yeni…", "new"),
                            ("Aç…", "open"),
                            ("Katman içe aktar…", "import"),
                            ("Kaydet    Ctrl+S", "save"),
                            ("Farklı kaydet…", "saveas"),
                            ("Dışa aktar…", "export"),
                            ("PDF / Yazdır…", "pdf"),
                            ("Toplu işlem…", "batch"),
                            ("Sekmeyi kapat", "close"),
                        ],
                    ),
                    (
                        "Düzenle",
                        vec![
                            ("Geri al    Ctrl+Z", "undo"),
                            ("Yinele    Ctrl+Shift+Z", "redo"),
                            ("Görünür görüntüyü kopyala", "copy"),
                            ("Yapıştır    Ctrl+V", "paste"),
                            ("Fırça / Kütüphane…", "brush"),
                        ],
                    ),
                    (
                        "Katman",
                        vec![
                            ("Yeni piksel katmanı", "layer"),
                            ("Metin…", "text"),
                            ("Çoğalt / Seçimi kopyala    Ctrl+J", "duplicate"),
                            ("Seçimi kes", "cut"),
                            ("Sil", "delete"),
                            ("Alta birleştir", "merge"),
                            ("Görünür katmanları birleştir", "mergevisible"),
                            ("Tuvale düzleştir", "flatten"),
                            ("Seçimden maske", "mask"),
                            ("Maskeyi ters çevir", "invertmask"),
                            ("Maskeyi kaldır", "removemask"),
                            ("Akıllı nesneye dönüştür", "smart"),
                            ("İçeriği değiştir…", "replace"),
                            ("Rasterleştir", "rasterize"),
                            ("Dönüştür    Ctrl+T", "transform"),
                            ("Sayısal dönüşüm…", "numeric"),
                        ],
                    ),
                    (
                        "Seçim",
                        vec![
                            ("Tümünü seç    Ctrl+A", "selectall"),
                            ("Seçimi kaldır    Ctrl+D", "deselect"),
                            ("Yeniden seç", "reselect"),
                            ("Ters çevir", "invertselection"),
                        ],
                    ),
                    (
                        "Görüntü",
                        vec![
                            ("Boyutlandır…", "resize"),
                            ("Tuval boyutu…", "canvas"),
                            ("Saat yönünde 90°", "rotate"),
                            ("Yatay aynala", "fliph"),
                            ("Dikey aynala", "flipv"),
                            ("Renk, ton ve filtreler…", "effects"),
                        ],
                    ),
                    (
                        "Görünüm",
                        vec![
                            ("Tuvale sığdır    Ctrl+0", "fit"),
                            ("Gerçek boyut · %100", "actual"),
                            ("Çalışma alanı…", "appearance"),
                            ("Kısayollar ve hızlı başlangıç · F1", "help"),
                            ("Hakkında", "about"),
                        ],
                    ),
                ] {
                    ui.menu_button(title, |ui| {
                        if title == "Dosya" {
                            ui.menu_button("Son dosyalar", |ui| {
                                if self.prefs.recent.is_empty() {
                                    ui.weak("Henüz dosya açılmadı");
                                }
                                for path in &self.prefs.recent {
                                    let label =
                                        path.file_name().unwrap_or_default().to_string_lossy();
                                    if ui
                                        .button(label.as_ref())
                                        .on_hover_text(path.display().to_string())
                                        .clicked()
                                    {
                                        recent_open = Some(path.clone());
                                        ui.close();
                                    }
                                }
                                if ui.button("Listeyi temizle").clicked() {
                                    self.prefs.recent.clear();
                                    ui.close();
                                }
                            });
                            ui.separator();
                        }
                        for (label, key) in items {
                            if ui.button(label).clicked() {
                                action = Some(key);
                                ui.close();
                            }
                        }
                    });
                }
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if ui.available_width() > 90. && ui.button("Dışa aktar…").clicked() {
                        action = Some("export");
                    }
                });
            });
        });
        if let Some(path) = recent_open {
            self.open(&path, false);
        }
        if let Some(a) = action {
            self.action(a)
        }
        egui::TopBottomPanel::top("options").show(ctx, |ui| {
            if self.modal_active() {
                ui.disable();
            }
            ui.horizontal_wrapped(|ui| {
                ui.strong(TOOLS.iter().find(|t| t.0 == self.tool).unwrap().2);
                ui.separator();
                if matches!(
                    self.tool,
                    Tool::Brush | Tool::Pencil | Tool::Eraser | Tool::Clone
                ) {
                    ui.add(
                        egui::Slider::new(&mut self.brush.size, 1..=512)
                            .text("Boyut")
                            .suffix(" px"),
                    );
                    ui.add(egui::Slider::new(&mut self.brush.flow, 1..=100).text("Akış"));
                    if ui.button("Uç ayarları…").clicked() {
                        self.dialog = Some(Dialog::Brush)
                    }
                } else if matches!(
                    self.tool,
                    Tool::Select | Tool::EllipseSelect | Tool::Lasso | Tool::Polygon | Tool::Wand
                ) {
                    for (m, l) in [
                        (SelectionMode::Replace, "Yeni"),
                        (SelectionMode::Add, "Ekle"),
                        (SelectionMode::Subtract, "Çıkar"),
                        (SelectionMode::Intersect, "Kesiştir"),
                    ] {
                        ui.selectable_value(&mut self.mode, m, l);
                    }
                    if self.tool == Tool::Wand {
                        ui.add(egui::Slider::new(&mut self.tolerance, 0..=255).text("Tolerans"));
                        ui.checkbox(&mut self.contiguous, "Bağlı");
                    }
                } else if self.tool == Tool::Move {
                    if ui.button("Dönüştür").clicked() {
                        self.begin_transform()
                    }
                    if !self.doc().state.selection.is_empty()
                        && ui.button("Seçimi kopyala ve dönüştür").clicked()
                    {
                        self.action("copytransform");
                    }
                    if ui.button("Sayısal…").clicked() {
                        self.action("numeric")
                    }
                }
                ui.separator();
                ui.color_edit_button_srgba_unmultiplied(&mut self.foreground);
                ui.color_edit_button_srgba_unmultiplied(&mut self.background);
                if self.transforming {
                    if ui.button("✓ Uygula").clicked() {
                        self.finish_transform(true)
                    }
                    if ui.button("İptal").clicked() {
                        self.finish_transform(false)
                    }
                }
            });
        });
    }
    fn document_tabs(&mut self, ctx: &egui::Context) {
        egui::TopBottomPanel::top("tabs").show(ctx, |ui| {
            if self.modal_active() {
                ui.disable();
            }
            ui.horizontal_wrapped(|ui| {
                let mut close = None;
                for i in 0..self.tabs.len() {
                    let t = &self.tabs[i];
                    let title = format!("{}{}", t.doc.title, if t.doc.dirty { " •" } else { "" });
                    if ui
                        .selectable_label(i == self.active, title)
                        .on_hover_text(
                            self.tabs[i]
                                .source_path
                                .as_ref()
                                .map(|p| p.display().to_string())
                                .unwrap_or_else(|| "Henüz kaydedilmemiş belge".into()),
                        )
                        .clicked()
                        && !self.transforming
                        && self.gesture.is_none()
                    {
                        self.prepare_navigation();
                        self.active = i;
                    }
                    if ui.small_button("×").clicked() {
                        close = Some(i);
                    }
                    ui.separator();
                }
                if ui.small_button("+").clicked() {
                    self.dialog = Some(Dialog::New)
                }
                if let Some(i) = close {
                    self.request_close(i)
                }
            });
        });
    }
    fn sidebar(&mut self, ctx: &egui::Context) {
        if self.prefs.preset == "classic-pro" {
            self.pro_sidebar(ctx);
            return;
        }
        if self.prefs.tools {
            let preset = self.prefs.preset.clone();
            if matches!(preset.as_str(), "compact" | "ribbon" | "touch") {
                let panel = if preset == "touch" {
                    egui::TopBottomPanel::bottom("tools")
                } else {
                    egui::TopBottomPanel::top("tools")
                };
                panel.show(ctx, |ui| {
                    ui.horizontal_wrapped(|ui| self.tool_contents(ui));
                });
            } else if preset == "modular" {
                egui::Window::new("Araçlar")
                    .default_pos([20., 110.])
                    .resizable(false)
                    .show(ctx, |ui| self.tool_contents(ui));
            } else {
                let panel = if preset == "workbench" {
                    egui::SidePanel::right("tools")
                } else {
                    egui::SidePanel::left("tools")
                };
                panel
                    .resizable(false)
                    .exact_width(if self.prefs.flag("toolLabels", preset == "paper") {
                        172.
                    } else {
                        54.
                    })
                    .show(ctx, |ui| {
                        egui::ScrollArea::vertical().show(ui, |ui| self.tool_contents(ui));
                    });
            }
        }
        if self.prefs.layers {
            let left = self
                .prefs
                .legacy
                .get("panelSide")
                .and_then(|v| v.as_str())
                .unwrap_or("preset")
                == "left"
                || self.prefs.preset == "workbench";
            if self.prefs.preset == "modular" {
                egui::Window::new("Katman çalışma alanı")
                    .default_pos([1040., 110.])
                    .default_width(self.prefs.panel_width)
                    .show(ctx, |ui| self.inspector_contents(ui));
            } else {
                let panel = if left {
                    egui::SidePanel::left("inspector")
                } else {
                    egui::SidePanel::right("inspector")
                };
                panel
                    .resizable(true)
                    .default_width(self.prefs.panel_width)
                    .width_range(220. ..=440.)
                    .show(ctx, |ui| self.inspector_contents(ui));
            }
        }
    }
    fn tool_contents(&mut self, ui: &mut egui::Ui) {
        if self.modal_active() || self.transforming {
            ui.disable();
        }
        let labels = self.prefs.flag(
            "toolLabels",
            matches!(self.prefs.preset.as_str(), "paper" | "ribbon" | "touch"),
        );
        for (t, icon, label) in TOOLS {
            let text = if labels {
                format!("{icon} {}", label.split('·').next().unwrap_or(label))
            } else {
                icon.into()
            };
            if ui
                .add_sized(
                    [
                        if labels { 150. } else { 36. },
                        if self.prefs.preset == "touch" {
                            44.
                        } else {
                            30.
                        },
                    ],
                    egui::Button::new(text).selected(self.tool == t),
                )
                .on_hover_text(label)
                .clicked()
            {
                if self.gesture.is_some() {
                    self.finish_gesture();
                }
                self.tool = t;
                self.polygon.clear();
            }
        }
    }
    fn inspector_contents(&mut self, ui: &mut egui::Ui) {
        if self.modal_active() || self.transforming {
            ui.disable();
        }
        egui::ScrollArea::vertical().show(ui, |ui| {
            ui.heading("Katmanlar");
            ui.horizontal(|ui| {
                if ui.button("+ Yeni").clicked() {
                    self.action("layer")
                }
                if ui.button("Çoğalt").clicked() {
                    self.action("duplicate")
                }
                if ui.button("−").clicked() {
                    self.action("delete")
                }
            });
            ui.separator();
            let active = self.doc().state.active;
            let layers = self.doc().state.layers.clone();
            let mut reorder = None;
            for (i, l) in layers.iter().enumerate().rev() {
                ui.horizontal(|ui| {
                    let mut visible = l.visible;
                    if ui
                        .checkbox(&mut visible, "")
                        .on_hover_text("Görünürlük")
                        .changed()
                    {
                        self.run(|d| {
                            d.transact("Katman görünürlüğü", true, |s| {
                                s.layers[i].visible = visible;
                                Ok(())
                            })
                        });
                    }
                    let prefix = if l.is_smart_object() {
                        "◇ "
                    } else if !l.text.is_empty() {
                        "T "
                    } else {
                        "▧ "
                    };
                    let response = ui.add(
                        egui::Button::new(format!(
                            "{}{}{}",
                            if l.clipped { "↳ " } else { "" },
                            prefix,
                            l.name
                        ))
                        .selected(active == i)
                        .sense(Sense::click_and_drag()),
                    );
                    if response.drag_started() {
                        response.dnd_set_drag_payload(i);
                    }
                    if let Some(from) = response.dnd_release_payload::<usize>() {
                        reorder = Some((*from, i));
                    }
                    if response.clicked() {
                        self.tabs[self.active].doc.state.active = i;
                        self.tabs[self.active].doc.editing_mask = false;
                    }
                    if response.double_clicked() && !l.text.is_empty() {
                        self.text = l.text.clone();
                        let parts = l.font.split(',').collect::<Vec<_>>();
                        self.font = parts.first().unwrap_or(&"Sans Serif").to_string();
                        self.font_size = parts.get(1).and_then(|p| p.parse().ok()).unwrap_or(48.);
                        self.font_bold = parts
                            .get(4)
                            .and_then(|p| p.parse::<i32>().ok())
                            .is_some_and(|v| v >= 600 || (60..=99).contains(&v));
                        self.foreground = l.color;
                        self.edit_text = true;
                        self.dialog = Some(Dialog::Text);
                    }
                    response.context_menu(|ui| {
                        for (label, cmd) in [
                            ("Çoğalt", "duplicate"),
                            ("Maske ekle", "mask"),
                            ("Akıllı nesne", "smart"),
                            ("Rasterleştir", "rasterize"),
                            ("Sil", "delete"),
                        ] {
                            if ui.button(label).clicked() {
                                self.tabs[self.active].doc.state.active = i;
                                self.action(cmd);
                                ui.close();
                            }
                        }
                    });
                    if ui.small_button("↑").clicked() && i + 1 < layers.len() {
                        reorder = Some((i, i + 1));
                    }
                    if ui.small_button("↓").clicked() && i > 0 {
                        reorder = Some((i, i - 1));
                    }
                });
            }
            if let Some((a, b)) = reorder {
                self.run(|d| d.reorder_layer(a, b));
            }
            ui.separator();
            self.layer_properties_contents(ui);
            ui.separator();
            ui.heading("Geçmiş");
            let revision = self.doc().revision();
            for (id, label) in self.doc().history() {
                if ui.selectable_label(revision == id, label).clicked() {
                    self.tabs[self.active].doc.jump_history(id);
                    self.invalidate();
                }
            }
        });
    }
    fn layer_properties_contents(&mut self, ui: &mut egui::Ui) {
        let old = self.doc().active_layer().clone();
        let mut layer = old.clone();
        ui.add_enabled(!old.locked, egui::TextEdit::singleline(&mut layer.name));
        ui.horizontal(|ui| {
            ui.checkbox(&mut layer.locked, "Kilitli");
            ui.add_enabled(
                !old.locked,
                egui::Checkbox::new(&mut layer.clipped, "Alta kırp"),
            );
        });
        ui.add_enabled(
            !old.locked,
            egui::Slider::new(&mut layer.opacity, 0. ..=1.).text("Opaklık"),
        );
        egui::ComboBox::from_id_salt("blend")
            .selected_text(BLEND_NAMES[layer.blend as usize])
            .show_ui(ui, |ui| {
                for (i, name) in BLEND_NAMES.iter().enumerate() {
                    ui.selectable_value(&mut layer.blend, i as u8, *name);
                }
            });
        let bounds = old.bounds().unwrap_or_default();
        let mut position = Point::new(bounds.x, bounds.y);
        ui.add_enabled_ui(!old.locked, |ui| {
            ui.horizontal(|ui| {
                ui.label("X");
                ui.add(egui::DragValue::new(&mut position.x));
                ui.label("Y");
                ui.add(egui::DragValue::new(&mut position.y));
            });
        });
        layer.offset.x = old
            .offset
            .x
            .saturating_add(position.x.saturating_sub(bounds.x));
        layer.offset.y = old
            .offset
            .y
            .saturating_add(position.y.saturating_sub(bounds.y));
        if old.locked {
            layer.blend = old.blend;
        }
        ui.add_enabled_ui(!old.locked, |ui| {
            ui.horizontal_wrapped(|ui| {
                use photodeck::document::{Alignment, AlignmentTarget};
                for (label, alignment) in [
                    ("Sol", Alignment::Left),
                    ("Orta", Alignment::Center),
                    ("Sağ", Alignment::Right),
                    ("Üst", Alignment::Top),
                    ("Merkez", Alignment::Middle),
                    ("Alt", Alignment::Bottom),
                ] {
                    if ui.small_button(label).clicked() {
                        let target = if self.doc().state.selection.is_empty() {
                            AlignmentTarget::Canvas
                        } else {
                            AlignmentTarget::Selection
                        };
                        self.run(|d| d.align_layer(alignment, target));
                    }
                }
            });
        });
        if layer != old {
            self.run(|d| {
                d.transact("Katman özellikleri", true, |s| {
                    s.layers[s.active] = layer;
                    Ok(())
                })
            });
        }
        if old.mask.is_some() {
            ui.horizontal(|ui| {
                ui.label("Düzenleme:");
                ui.selectable_value(
                    &mut self.tabs[self.active].doc.editing_mask,
                    false,
                    "Piksel",
                );
                ui.selectable_value(&mut self.tabs[self.active].doc.editing_mask, true, "Maske");
            });
        }
        ui.label(
            egui::RichText::new(format!(
                "{} × {} px{}",
                old.source_size().width,
                old.source_size().height,
                if old.is_smart_object() {
                    " · gömülü kaynak"
                } else {
                    ""
                }
            ))
            .small()
            .weak(),
        );
        if ui.button("Renk / ton / filtreler…").clicked() {
            self.action("effects")
        }
        ui.separator();
        ui.collapsing("Adlandırılmış seçimler", |ui| {
            ui.text_edit_singleline(&mut self.selection_name);
            if ui.button("Seçimi kaydet").clicked() {
                let n = self.selection_name.clone();
                self.run(|d| d.save_selection(&n));
            }
            for saved in self.doc().state.saved_selections.clone() {
                ui.horizontal(|ui| {
                    if ui.button(&saved.name).clicked() {
                        let mode = self.mode;
                        self.run(|d| d.load_selection(&saved.name, mode));
                    }
                    if ui.small_button("×").clicked() {
                        self.run(|d| d.remove_saved_selection(&saved.name));
                    }
                });
            }
            ui.horizontal(|ui| {
                for (label, r) in [("Yumuşat", 3), ("Büyüt", 2), ("Küçült", -2)] {
                    if ui.button(label).clicked() {
                        let s = &self.doc().state;
                        let sel = if r == 3 {
                            s.selection.feathered(r, s.size)
                        } else {
                            s.selection.grown(r, s.size)
                        };
                        self.run(|d| d.set_selection(sel));
                    }
                }
            });
        });
    }
    fn canvas(&mut self, ctx: &egui::Context) {
        egui::CentralPanel::default()
            .frame(egui::Frame::new().fill(self.prefs.color(
                "canvas",
                if matches!(self.prefs.preset.as_str(), "paper" | "ribbon" | "touch") {
                    [223, 226, 225]
                } else {
                    [19, 23, 29]
                },
            )))
            .show(ctx, |ui| {
                if self.modal_active() {
                    ui.disable();
                }
                let outer = ui.max_rect();
                let ruler_size =
                    if self.prefs.preset == "classic-pro" && self.prefs.flag("rulers", true) {
                        20.
                    } else {
                        0.
                    };
                let available =
                    egui::Rect::from_min_max(outer.min + Vec2::splat(ruler_size), outer.max);
                let response = ui.interact(
                    available,
                    ui.id().with("canvas_surface"),
                    Sense::click_and_drag(),
                );
                self.canvas_id = Some(response.id);
                let t = &mut self.tabs[self.active];
                let size = t.doc.state.size;
                if t.zoom == 0. || t.fit_to_view {
                    t.zoom = ((available.width() - 36.) / size.width as f32)
                        .min((available.height() - 36.) / size.height as f32)
                        .max(0.01);
                }
                if response.hovered() {
                    let delta = ctx.input(|i| i.smooth_scroll_delta.y);
                    if delta != 0. {
                        t.fit_to_view = false;
                        let old = t.zoom;
                        t.zoom = (t.zoom * (delta * 0.002).exp()).clamp(0.01, 32.);
                        if let Some(p) = response.hover_pos() {
                            t.pan = (t.pan + available.center().to_vec2() - p.to_vec2())
                                * (t.zoom / old)
                                - available.center().to_vec2()
                                + p.to_vec2();
                        }
                    }
                }
                let screen = egui::Rect::from_center_size(
                    available.center() + t.pan,
                    Vec2::new(size.width as f32 * t.zoom, size.height as f32 * t.zoom),
                );
                let zoom = t.zoom;
                self.tablet.canvas = Some((available, screen, zoom));
                if ruler_size > 0. {
                    pro::paint_rulers(ui, outer, screen, zoom, ruler_size);
                }
                if t.selection_key != Some((t.doc.revision(), t.generation)) {
                    if let Selection::Mask(mask) = &t.doc.state.selection {
                        let overlay = RgbaImage::from_fn(mask.width(), mask.height(), |x, y| {
                            let selected = mask.get_pixel(x, y)[0] > 0;
                            let edge = selected
                                && (x == 0
                                    || y == 0
                                    || x + 1 == mask.width()
                                    || y + 1 == mask.height()
                                    || mask.get_pixel(x.saturating_sub(1), y)[0] == 0
                                    || mask.get_pixel((x + 1).min(mask.width() - 1), y)[0] == 0
                                    || mask.get_pixel(x, y.saturating_sub(1))[0] == 0
                                    || mask.get_pixel(x, (y + 1).min(mask.height() - 1))[0] == 0);
                            let c = if ((x + y) / 5) % 2 == 0 { 255 } else { 0 };
                            Rgba([c, c, c, if edge { 255 } else { 0 }])
                        });
                        let image = egui::ColorImage::from_rgba_unmultiplied(
                            [overlay.width() as usize, overlay.height() as usize],
                            overlay.as_raw(),
                        );
                        t.selection_texture = Some(ctx.load_texture(
                            format!("selection{}", t.id),
                            image,
                            egui::TextureOptions::NEAREST,
                        ));
                    } else {
                        t.selection_texture = None;
                    }
                    t.selection_key = Some((t.doc.revision(), t.generation));
                }
                let painter = ui.painter().with_clip_rect(available);
                painter.rect_filled(screen.expand(8.), 4., Color32::BLACK.gamma_multiply(0.3));
                let checker = 16.;
                let clip = screen.intersect(available);
                let nx = (clip.width() / checker).ceil() as i32;
                let ny = (clip.height() / checker).ceil() as i32;
                for y in 0..ny {
                    for x in 0..nx {
                        let r = egui::Rect::from_min_size(
                            clip.min + Vec2::new(x as f32 * checker, y as f32 * checker),
                            Vec2::splat(checker),
                        )
                        .intersect(clip);
                        painter.rect_filled(
                            r,
                            0.,
                            if (x + y) % 2 == 0 {
                                Color32::from_gray(67)
                            } else {
                                Color32::from_gray(81)
                            },
                        );
                    }
                }
                if let Some(texture) = &t.texture {
                    painter.image(
                        texture.id(),
                        screen,
                        egui::Rect::from_min_max(Pos2::ZERO, Pos2::new(1., 1.)),
                        Color32::WHITE,
                    );
                }
                painter.rect_stroke(
                    screen,
                    0.,
                    Stroke::new(1.0_f32, Color32::from_gray(105)),
                    egui::StrokeKind::Outside,
                );
                let to_screen =
                    |p: (f64, f64)| screen.min + Vec2::new(p.0 as f32 * zoom, p.1 as f32 * zoom);
                let pointer = response.interact_pointer_pos().map(|p| {
                    (
                        ((p.x - screen.min.x) / zoom) as f64,
                        ((p.y - screen.min.y) / zoom) as f64,
                    )
                });
                if let Some(texture) = &t.selection_texture {
                    painter.image(
                        texture.id(),
                        screen,
                        egui::Rect::from_min_max(Pos2::ZERO, Pos2::new(1., 1.)),
                        Color32::WHITE,
                    );
                }
                if let Selection::Rectangle(bounds) = t.doc.state.selection {
                    let r = egui::Rect::from_min_size(
                        to_screen((bounds.x as f64, bounds.y as f64)),
                        Vec2::new(bounds.width as f32 * zoom, bounds.height as f32 * zoom),
                    );
                    painter.rect_stroke(
                        r,
                        0.,
                        Stroke::new(2.0_f32, Color32::BLACK),
                        egui::StrokeKind::Inside,
                    );
                    painter.rect_stroke(
                        r.shrink(1.),
                        0.,
                        Stroke::new(1.0_f32, Color32::WHITE),
                        egui::StrokeKind::Inside,
                    );
                }
                let mut handles = vec![];
                if self.tool == Tool::Move
                    && let Some(b) = t.doc.active_layer().bounds()
                {
                    let r = egui::Rect::from_min_size(
                        to_screen((b.x as f64, b.y as f64)),
                        Vec2::new(b.width as f32 * zoom, b.height as f32 * zoom),
                    );
                    painter.rect_stroke(
                        r,
                        0.,
                        Stroke::new(1.0_f32, Color32::from_rgb(89, 201, 180)),
                        egui::StrokeKind::Outside,
                    );
                    if self.transforming {
                        handles = vec![
                            r.left_top(),
                            r.center_top(),
                            r.right_top(),
                            r.right_center(),
                            r.right_bottom(),
                            r.center_bottom(),
                            r.left_bottom(),
                            r.left_center(),
                            r.center_top() - Vec2::new(0., 28.),
                        ];
                        painter.line_segment(
                            [r.center_top(), handles[8]],
                            Stroke::new(1.0_f32, Color32::WHITE),
                        );
                        for p in &handles {
                            painter.rect_filled(
                                egui::Rect::from_center_size(*p, Vec2::splat(8.)),
                                1.,
                                Color32::WHITE,
                            );
                        }
                    }
                }
                if let Some(g) = &self.gesture {
                    if matches!(
                        self.tool,
                        Tool::Select
                            | Tool::EllipseSelect
                            | Tool::Crop
                            | Tool::Rectangle
                            | Tool::Ellipse
                            | Tool::Gradient
                    ) {
                        let r = egui::Rect::from_two_pos(to_screen(g.start), to_screen(g.last));
                        painter.rect_stroke(
                            r,
                            0.,
                            Stroke::new(1.0_f32, Color32::from_rgb(89, 201, 180)),
                            egui::StrokeKind::Inside,
                        );
                    }
                    if self.tool == Tool::Lasso {
                        for p in g.points.windows(2) {
                            painter.line_segment(
                                [to_screen(p[0]), to_screen(p[1])],
                                Stroke::new(1.0_f32, Color32::WHITE),
                            );
                        }
                    }
                }
                for p in self.polygon.windows(2) {
                    painter.line_segment(
                        [to_screen(p[0]), to_screen(p[1])],
                        Stroke::new(1.0_f32, Color32::WHITE),
                    );
                }
                if response.drag_started() && !self.tablet.suppress_mouse(self.frames) {
                    response.request_focus();
                    if let Some(p) = pointer {
                        let handle = response
                            .interact_pointer_pos()
                            .and_then(|at| handles.iter().position(|h| h.distance(at) < 12.))
                            .map_or(-1, |i| i as i32);
                        let delta = response.drag_delta() / zoom;
                        self.start_gesture(
                            (p.0 - delta.x as f64, p.1 - delta.y as f64),
                            handle,
                            ctx,
                        );
                    }
                }
                if response.dragged()
                    && !self.tablet.suppress_mouse(self.frames)
                    && let Some(p) = pointer
                {
                    self.advance_gesture(p, response.drag_delta(), ctx);
                }
                if response.drag_stopped() && !self.tablet.suppress_mouse(self.frames) {
                    if let Some(at) = ctx.input(|i| i.pointer.latest_pos()) {
                        let p = (
                            ((at.x - screen.min.x) / zoom) as f64,
                            ((at.y - screen.min.y) / zoom) as f64,
                        );
                        self.advance_gesture(p, response.drag_delta(), ctx);
                    }
                    self.finish_gesture();
                }
                if response.clicked() && !self.tablet.suppress_mouse(self.frames) {
                    response.request_focus();
                    if let Some(p) = pointer {
                        match self.tool {
                            Tool::Text => {
                                self.text_at = Point::new(p.0 as i32, p.1 as i32);
                                self.action("text")
                            }
                            Tool::Picker => {
                                let im = render::composite(&self.doc().state);
                                if p.0 >= 0.
                                    && p.1 >= 0.
                                    && (p.0 as u32) < im.width()
                                    && (p.1 as u32) < im.height()
                                {
                                    self.foreground = im.get_pixel(p.0 as u32, p.1 as u32).0
                                }
                            }
                            Tool::Polygon => {
                                self.polygon.push(p);
                                if response.double_clicked() && self.polygon.len() > 2 {
                                    let points = self
                                        .polygon
                                        .iter()
                                        .map(|p| Point::new(p.0 as i32, p.1 as i32))
                                        .collect::<Vec<_>>();
                                    self.queue_path_selection(points);
                                    self.polygon.clear();
                                }
                            }
                            Tool::Wand => {
                                let im = render::composite(&self.doc().state);
                                let sel = Selection::color_range(
                                    &im,
                                    Point::new(p.0 as i32, p.1 as i32),
                                    self.tolerance as i32,
                                    self.contiguous,
                                );
                                self.apply_selection(sel)
                            }
                            Tool::Move => {
                                if !self.transforming {
                                    let hit = self
                                        .doc()
                                        .state
                                        .layers
                                        .iter()
                                        .enumerate()
                                        .rev()
                                        .find(|(_, l)| {
                                            l.visible
                                                && l.bounds().is_some_and(|b| {
                                                    b.contains(p.0 as i32, p.1 as i32)
                                                })
                                        })
                                        .map(|(i, _)| i);
                                    if let Some(i) = hit {
                                        self.tabs[self.active].doc.state.active = i;
                                    }
                                }
                            }
                            _ => {
                                self.start_gesture(p, -1, ctx);
                                self.finish_gesture();
                            }
                        }
                    }
                }
            });
    }
    fn apply_selection(&mut self, sel: Selection) {
        let state = &self.doc().state;
        let combined = state.selection.combined(&sel, self.mode, state.size);
        self.run(|d| d.set_selection(combined));
    }
    fn start_gesture(&mut self, p: (f64, f64), handle: i32, ctx: &egui::Context) {
        if self.tool == Tool::Clone && ctx.input(|i| i.modifiers.alt) {
            self.clone_anchor = Some(p);
            self.status = "Klon kaynağı seçildi".into();
            return;
        }
        let layer = self.doc().active_layer().clone();
        if matches!(
            self.tool,
            Tool::Brush
                | Tool::Pencil
                | Tool::Eraser
                | Tool::Clone
                | Tool::Fill
                | Tool::Gradient
                | Tool::Rectangle
                | Tool::Ellipse
        ) {
            if layer.locked {
                self.error = Some("Katman kilitli.".into());
                return;
            }
            if layer.is_smart_object() && !self.doc().editing_mask {
                self.error=Some("Akıllı nesne kaynağı korunuyor. Boyamak için yeni piksel katmanı ekleyin veya açıkça rasterleştirin.".into());
                return;
            }
        }
        if !self.doc().editing_mask
            && !layer.geometry.is_identity()
            && matches!(
                self.tool,
                Tool::Brush
                    | Tool::Pencil
                    | Tool::Eraser
                    | Tool::Clone
                    | Tool::Fill
                    | Tool::Gradient
                    | Tool::Rectangle
                    | Tool::Ellipse
            )
        {
            self.error=Some("Dönüştürülmüş katmanın kaynağı korunuyor. Önce Katman → Rasterleştir veya yeni piksel katmanı kullanın.".into());
            return;
        }
        if !matches!(
            self.tool,
            Tool::Hand | Tool::Select | Tool::EllipseSelect | Tool::Lasso | Tool::Crop
        ) && !self.transforming
        {
            let r = self.tabs[self.active].doc.begin_edit();
            if self.result(r).is_none() {
                return;
            }
        }
        let original = layer.clone();
        self.gesture = Some(Gesture {
            start: p,
            last: p,
            pan_start: self.tabs[self.active].pan,
            points: vec![p],
            samples: vec![
                self.tablet
                    .sample
                    .unwrap_or_else(|| StrokeSample::mouse(p.0, p.1)),
            ],
            original,
            handle,
        });
        if matches!(
            self.tool,
            Tool::Brush | Tool::Pencil | Tool::Eraser | Tool::Clone
        ) {
            self.paint_segment(p, p);
        }
    }
    fn paint_segment(&mut self, _from: (f64, f64), to: (f64, f64)) {
        let Some(g) = self.gesture.as_mut() else {
            return;
        };
        let sample = self
            .tablet
            .sample
            .unwrap_or_else(|| StrokeSample::mouse(to.0, to.1));
        if g.points.last() != Some(&to) {
            g.points.push(to);
            g.samples.push(sample);
        } else if let Some(last) = g.samples.last_mut() {
            *last = sample;
        }
        let original = g.original.clone();
        let start = g.start;
        let mut samples = g.samples.clone();
        let d = &mut self.tabs[self.active].doc;
        let mut sel = if d.state.selection.is_empty() {
            None
        } else {
            Some(d.state.selection.mask(d.state.size))
        };
        let l = &mut d.state.layers[d.state.active];
        let mut settings = self.brush.clone();
        if self.tool == Tool::Pencil {
            settings.hardness = 100;
            settings.spacing = 1;
        }
        if d.editing_mask {
            if let Some(mask) = &original.mask {
                let Some(inv) = l.geometry.inverse() else {
                    return;
                };
                for sample in &mut samples {
                    let p = inv.map(sample.x - l.offset.x as f64, sample.y - l.offset.y as f64);
                    sample.x = p.0;
                    sample.y = p.1;
                }
                if let Some(coverage) = sel.take() {
                    sel = Some(image::GrayImage::from_fn(
                        mask.width(),
                        mask.height(),
                        |x, y| {
                            let p = l.geometry.map(x as f64 + 0.5, y as f64 + 0.5);
                            let x = p.0.floor() as i64 + l.offset.x as i64;
                            let y = p.1.floor() as i64 + l.offset.y as i64;
                            image::Luma([
                                if x >= 0
                                    && y >= 0
                                    && x < coverage.width() as i64
                                    && y < coverage.height() as i64
                                {
                                    coverage.get_pixel(x as u32, y as u32)[0]
                                } else {
                                    0
                                },
                            ])
                        },
                    ));
                }
                let mut im = RgbaImage::from_fn(mask.width(), mask.height(), |x, y| {
                    let v = mask.get_pixel(x, y)[0];
                    Rgba([v, v, v, 255])
                });
                let gray = if self.tool == Tool::Eraser {
                    0
                } else {
                    ((11 * self.foreground[0] as u32
                        + 16 * self.foreground[1] as u32
                        + 5 * self.foreground[2] as u32)
                        / 32) as u8
                };
                if self.tool == Tool::Clone {
                    if let Some(anchor) = self.clone_anchor {
                        let a = inv.map(anchor.0 - l.offset.x as f64, anchor.1 - l.offset.y as f64);
                        let b = inv.map(start.0 - l.offset.x as f64, start.1 - l.offset.y as f64);
                        let source = im.clone();
                        brush::clone_stroke(
                            &mut im,
                            &source,
                            sel.as_ref(),
                            Point::default(),
                            &samples,
                            &settings,
                            Point::new((a.0 - b.0) as i32, (a.1 - b.1) as i32),
                        );
                    }
                } else {
                    brush::paint_stroke(
                        &mut im,
                        sel.as_ref(),
                        Point::default(),
                        &samples,
                        &settings,
                        [gray, gray, gray, self.foreground[3]],
                        false,
                    );
                }
                l.mask = Some(Arc::new(image::GrayImage::from_fn(
                    im.width(),
                    im.height(),
                    |x, y| image::Luma([im.get_pixel(x, y)[0]]),
                )));
            }
        } else {
            l.pixels = original.pixels.clone();
            if self.tool == Tool::Clone {
                if let Some(anchor) = self.clone_anchor {
                    brush::clone_stroke(
                        Arc::make_mut(&mut l.pixels),
                        &original.pixels,
                        sel.as_ref(),
                        l.offset,
                        &samples,
                        &settings,
                        Point::new((anchor.0 - start.0) as i32, (anchor.1 - start.1) as i32),
                    );
                }
            } else {
                brush::paint_stroke(
                    Arc::make_mut(&mut l.pixels),
                    sel.as_ref(),
                    l.offset,
                    &samples,
                    &settings,
                    self.foreground,
                    self.tool == Tool::Eraser,
                );
            }
        }
        self.invalidate();
    }
    fn advance_gesture(&mut self, p: (f64, f64), drag: Vec2, ctx: &egui::Context) {
        let Some(g) = self.gesture.as_ref() else {
            return;
        };
        let last = g.last;
        let pan_start = g.pan_start;
        let start = g.start;
        let original = g.original.clone();
        let handle = g.handle;
        if self.tool == Tool::Hand {
            self.tabs[self.active].pan = pan_start + drag;
        } else if matches!(
            self.tool,
            Tool::Brush | Tool::Pencil | Tool::Eraser | Tool::Clone
        ) {
            self.paint_segment(last, p);
        } else if self.tool == Tool::Move && !original.locked {
            let active = self.doc_active();
            let layer = &mut self.tabs[self.active].doc.state.layers[active];
            if self.transforming && handle >= 0 {
                if let Some(b) = original.bounds() {
                    let world = handle_transform(
                        b,
                        start,
                        p,
                        handle,
                        ctx.input(|i| i.modifiers.alt),
                        ctx.input(|i| i.modifiers.shift),
                    );
                    let geometry = original
                        .geometry
                        .then(Transform::translate(
                            original.offset.x as f64,
                            original.offset.y as f64,
                        ))
                        .then(world)
                        .then(Transform::translate(
                            -original.offset.x as f64,
                            -original.offset.y as f64,
                        ));
                    if geometry.bounds(original.source_size()).is_some() {
                        layer.geometry = geometry;
                    }
                }
            } else {
                layer.offset = Point::new(
                    original.offset.x + (p.0 - start.0).round() as i32,
                    original.offset.y + (p.1 - start.1).round() as i32,
                );
            }
            self.invalidate();
        }
        if let Some(g) = &mut self.gesture {
            g.last = p;
            if self.tool == Tool::Lasso {
                g.points.push(p);
            }
        }
        let _ = drag;
    }
    fn doc_active(&self) -> usize {
        self.doc().state.active
    }
    fn finish_gesture(&mut self) {
        let Some(g) = self.gesture.take() else { return };
        let rect = drag_rect(g.start, g.last);
        if !self.doc().editing_mask {
            let i = self.doc_active();
            if self.doc().state.layers[i].pixels != g.original.pixels {
                self.tabs[self.active].doc.state.layers[i].text.clear();
            }
        }
        match self.tool {
            Tool::Select => self.apply_selection(Selection::Rectangle(rect)),
            Tool::EllipseSelect => {
                self.apply_selection(Selection::ellipse(rect, self.doc().state.size))
            }
            Tool::Lasso => {
                let points = g
                    .points
                    .iter()
                    .map(|p| Point::new(p.0 as i32, p.1 as i32))
                    .collect::<Vec<_>>();
                self.queue_path_selection(points);
            }
            Tool::Crop => self.run(|d| d.crop(rect)),
            Tool::Fill | Tool::Gradient | Tool::Rectangle | Tool::Ellipse => {
                let d = &mut self.tabs[self.active].doc;
                let sel = if d.state.selection == Selection::None {
                    None
                } else {
                    Some(d.state.selection.mask(d.state.size))
                };
                let l = &mut d.state.layers[d.state.active];
                let start = Point::new(g.start.0 as i32, g.start.1 as i32);
                let end = Point::new(g.last.0 as i32, g.last.1 as i32);
                if d.editing_mask {
                    if let Some(mask) = &l.mask {
                        let mut im = RgbaImage::from_fn(mask.width(), mask.height(), |x, y| {
                            let v = mask.get_pixel(x, y)[0];
                            Rgba([v, v, v, 255])
                        });
                        let gray = |c: [u8; 4]| {
                            let v = ((11 * c[0] as u32 + 16 * c[1] as u32 + 5 * c[2] as u32) / 32)
                                as u8;
                            [v, v, v, c[3]]
                        };
                        if self.tool == Tool::Fill {
                            if let Some(inv) = l.geometry.inverse() {
                                let point = inv.map(
                                    start.x as f64 - l.offset.x as f64,
                                    start.y as f64 - l.offset.y as f64,
                                );
                                let coverage = sel.as_ref().map(|coverage| {
                                    image::GrayImage::from_fn(
                                        mask.width(),
                                        mask.height(),
                                        |x, y| {
                                            let p = l.geometry.map(x as f64 + 0.5, y as f64 + 0.5);
                                            let (x, y) = (
                                                p.0.floor() as i64 + l.offset.x as i64,
                                                p.1.floor() as i64 + l.offset.y as i64,
                                            );
                                            image::Luma([
                                                if x >= 0
                                                    && y >= 0
                                                    && x < coverage.width() as i64
                                                    && y < coverage.height() as i64
                                                {
                                                    coverage.get_pixel(x as u32, y as u32)[0]
                                                } else {
                                                    0
                                                },
                                            ])
                                        },
                                    )
                                });
                                brush::flood_fill(
                                    &mut im,
                                    coverage.as_ref(),
                                    Point::default(),
                                    Point::new(point.0 as i32, point.1 as i32),
                                    gray(self.foreground),
                                    self.brush.flow as f64 / 100.,
                                    self.tolerance,
                                );
                            }
                        } else {
                            let mut overlay =
                                RgbaImage::new(d.state.size.width, d.state.size.height);
                            if self.tool == Tool::Gradient {
                                brush::gradient(
                                    &mut overlay,
                                    sel.as_ref(),
                                    Point::default(),
                                    start,
                                    end,
                                    gray(self.foreground),
                                    gray(self.background),
                                );
                            } else {
                                brush::shape(
                                    &mut overlay,
                                    sel.as_ref(),
                                    Point::default(),
                                    rect,
                                    self.tool == Tool::Ellipse,
                                    true,
                                    self.brush.size as f64,
                                    gray(self.foreground),
                                );
                            }
                            for (x, y, pixel) in im.enumerate_pixels_mut() {
                                let p = l.geometry.map(x as f64 + 0.5, y as f64 + 0.5);
                                let (x, y) = (
                                    p.0.floor() as i64 + l.offset.x as i64,
                                    p.1.floor() as i64 + l.offset.y as i64,
                                );
                                if x >= 0
                                    && y >= 0
                                    && x < overlay.width() as i64
                                    && y < overlay.height() as i64
                                {
                                    brush::blend_pixel(
                                        pixel,
                                        overlay.get_pixel(x as u32, y as u32).0,
                                        1.,
                                        false,
                                    );
                                }
                            }
                        }
                        l.mask = Some(Arc::new(image::GrayImage::from_fn(
                            im.width(),
                            im.height(),
                            |x, y| image::Luma([im.get_pixel(x, y)[0]]),
                        )));
                    }
                } else {
                    match self.tool {
                        Tool::Fill => {
                            brush::flood_fill(
                                Arc::make_mut(&mut l.pixels),
                                sel.as_ref(),
                                l.offset,
                                start,
                                self.foreground,
                                self.brush.flow as f64 / 100.,
                                self.tolerance,
                            );
                        }
                        Tool::Gradient => {
                            brush::gradient(
                                Arc::make_mut(&mut l.pixels),
                                sel.as_ref(),
                                l.offset,
                                start,
                                end,
                                self.foreground,
                                self.background,
                            );
                        }
                        _ => {
                            brush::shape(
                                Arc::make_mut(&mut l.pixels),
                                sel.as_ref(),
                                l.offset,
                                rect,
                                self.tool == Tool::Ellipse,
                                true,
                                self.brush.size as f64,
                                self.foreground,
                            );
                        }
                    }
                    if l.pixels != g.original.pixels {
                        l.text.clear();
                    }
                }
                let r = d.commit("Çizim");
                self.result(r);
            }
            Tool::Hand => {}
            _ => {
                if !self.transforming {
                    let r = self.tabs[self.active].doc.commit("Tuval düzenlemesi");
                    self.result(r);
                }
            }
        }
        self.invalidate();
    }
    fn dialogs(&mut self, ctx: &egui::Context) {
        if let Some(kind) = self.dialog {
            let mut open = true;
            let title = match kind {
                Dialog::New => "Yeni belge",
                Dialog::Export => "Görüntüyü dışa aktar",
                Dialog::Pdf => "PDF ve yazdırma",
                Dialog::Text => "Düzenlenebilir metin",
                Dialog::Transform => "Kaynak koruyan dönüşüm",
                Dialog::Canvas => "Tuval boyutu",
                Dialog::Resize => "Görüntü boyutu",
                Dialog::Effects => "Renk, ton, filtre ve maske",
                Dialog::Brush => "Fırça ve kütüphane",
                Dialog::Appearance => "Çalışma alanı",
                Dialog::About => "PhotoDeck hakkında",
                Dialog::Batch => "Toplu işlem",
                Dialog::Help => "Hızlı başlangıç ve kısayollar",
            };
            egui::Window::new(title).id(egui::Id::new("dialog")).open(&mut open).collapsible(false).default_width(520_f32.min(ctx.content_rect().width()-32.)).show(ctx,|ui|{egui::ScrollArea::vertical().max_height((ctx.content_rect().height()-100.).max(100.)).show(ui,|ui|{if self.work.user_busy(){ui.disable();}match kind{
 Dialog::New|Dialog::Export|Dialog::Canvas|Dialog::Resize=>{let oldw=self.width;let oldh=self.height;ui.horizontal(|ui|{ui.label("Genişlik");ui.add(egui::DragValue::new(&mut self.width).range(1..=16384).suffix(" px"));ui.label("Yükseklik");ui.add(egui::DragValue::new(&mut self.height).range(1..=16384).suffix(" px"));});if kind!=Dialog::New{ui.checkbox(&mut self.keep_ratio,"Oranı koru");if self.keep_ratio{let s=self.doc().state.size;if self.width!=oldw||self.height!=oldh{let target=locked_dimensions(s,self.width,self.height,self.width!=oldw);self.width=target.width;self.height=target.height;}}}
if kind==Dialog::New{ui.checkbox(&mut self.white,"Beyaz arka plan");}
if kind==Dialog::Canvas{ui.label("Sabitleme noktası · tuval dışındaki kaynak pikseller korunur.");egui::Grid::new("canvas_anchor").show(ui,|ui|{let labels=[["↖","↑","↗"],["←","•","→"],["↙","↓","↘"]];for (y,row) in labels.iter().enumerate(){for (x,label) in row.iter().enumerate(){ui.selectable_value(&mut self.canvas_anchor,Point::new(x as i32,y as i32),*label).on_hover_text(format!("Sabitleme: {}, {}",x+1,y+1));}ui.end_row();}});}
if kind==Dialog::Export{ui.label("Kalite JPEG için geçerlidir; WebP kayıpsız yazılır.");ui.add(egui::Slider::new(&mut self.quality,0..=100).text("JPEG kalitesi"));ui.add(egui::DragValue::new(&mut self.dpi).range(72..=1200).prefix("DPI "));egui::ComboBox::from_label("ICC profili").selected_text(["sRGB","Display P3","Adobe RGB"][self.profile]).show_ui(ui,|ui|{for(i,n)in["sRGB","Display P3","Adobe RGB"].iter().enumerate(){ui.selectable_value(&mut self.profile,i,*n);}});}let valid=Size::new(self.width,self.height).valid();if !valid{ui.colored_label(Color32::LIGHT_RED,"En fazla 16 megapiksel ve 16384 piksel/kenar.");}
if ui.add_enabled(valid,egui::Button::new(if kind==Dialog::Export{"Dışa aktar…"}else{"Uygula"})).clicked(){let size=Size::new(self.width,self.height);match kind{Dialog::New=>{if let Some(d)=self.result(Document::new(size,self.white)){self.push(d);self.dialog=None;}},Dialog::Resize=>{let r=self.tabs[self.active].doc.resize(size);if self.result(r).is_some(){self.invalidate();self.dialog=None}},Dialog::Canvas=>{let anchor=self.canvas_anchor;let r=self.tabs[self.active].doc.canvas_size(size,anchor);if self.result(r).is_some(){self.invalidate();self.dialog=None}},_=>{if let Some(p)=rfd::FileDialog::new().add_filter("PNG",&["png"]).add_filter("JPEG",&["jpg"]).add_filter("WebP",&["webp"]).add_filter("TIFF",&["tif"]).add_filter("BMP",&["bmp"]).set_file_name(format!("{}.png",self.doc().title)).save_file(){self.begin_export(p);}}}}},
 Dialog::Pdf=>{self.dpi=self.dpi.clamp(72,600);egui::ComboBox::from_label("Kağıt").selected_text(["A4","Letter","Görüntü boyutu"][self.paper as usize]).show_ui(ui,|ui|{for(i,n)in["A4","Letter","Görüntü boyutu"].iter().enumerate(){ui.selectable_value(&mut self.paper,i as u8,*n);}});ui.checkbox(&mut self.landscape,"Yatay");ui.add(egui::DragValue::new(&mut self.margin).range(0. ..=50.).suffix(" mm kenar boşluğu"));ui.add(egui::DragValue::new(&mut self.dpi).range(72..=600).prefix("DPI "));ui.label("PDF kaydedilir; Yazdır, sistemin yerel yazıcı iletişim kutusunu açar.");for print in [false,true]{if ui.button(if print{"PDF oluştur ve yazdır…"}else{"PDF kaydet…"}).clicked()&& let Some(p)=rfd::FileDialog::new().add_filter("PDF",&["pdf"]).set_file_name(format!("{}.pdf",self.doc().title)).save_file(){self.begin_pdf(p,print);}}},
 Dialog::Text=>{ui.text_edit_multiline(&mut self.text);ui.horizontal(|ui|{ui.label("Font ailesi");ui.text_edit_singleline(&mut self.font);ui.add(egui::DragValue::new(&mut self.font_size).range(4. ..=512.).suffix(" pt"));});ui.checkbox(&mut self.font_bold,"Kalın");ui.color_edit_button_srgba_unmultiplied(&mut self.foreground);if ui.button("Metni uygula").clicked(){let text=self.text.clone();let font=format!("{},{},-1,5,{},0,0,0,0,0,0,0,0,0,0,1",self.font,self.font_size,if self.font_bold{700}else{400});let color=self.foreground;let at=self.text_at;let edit=self.edit_text;let d=&mut self.tabs[self.active].doc;let r=if edit{d.edit_text(&text,&font,color)}else{d.add_text(&text,&font,color,at)};if self.result(r).is_some(){self.invalidate();self.dialog=None;}}},
 Dialog::Transform=>{ui.label("Yüzde ölçek ve açı, mevcut dönüşümün üzerine uygulanır.");for(i,n)in["X ölçek %","Y ölçek %","Açı °","X eğim","Y eğim","X perspektif","Y perspektif"].iter().enumerate(){ui.horizontal(|ui|{ui.label(*n);ui.add(egui::DragValue::new(&mut self.transform_values[i]).speed(if i>2{0.001}else{1.}));});}ui.checkbox(&mut self.keep_ratio,"Ölçek oranını koru");if ui.button("Dönüşümü uygula").clicked(){let v=self.transform_values;let sy=if self.keep_ratio{v[0]}else{v[1]};let t=Transform::scale(v[0]/100.,sy/100.).then(Transform::rotate(v[2])).then(Transform([1.,v[4],v[5],v[3],1.,v[6],0.,0.,1.]));let i=self.doc_active();let current=self.doc().state.layers[i].geometry;self.tabs[self.active].doc.state.layers[i].geometry=current.then(t);self.finish_transform(true);if !self.transforming{self.dialog=None;}}
if ui.button("İptal").clicked(){self.finish_transform(false);self.dialog=None;}},
 Dialog::Effects=>{let i=self.doc_active();let before=self.effects_draft.clone().unwrap_or_else(||self.doc().active_layer().clone());let mut l=before.clone();let toggled=ui.checkbox(&mut self.live_preview,"Canlı önizleme").changed();ui.collapsing("Temel renk",|ui|{ui.add(egui::Slider::new(&mut l.brightness,-100..=100).text("Parlaklık"));ui.add(egui::Slider::new(&mut l.contrast,-100..=100).text("Kontrast"));ui.add(egui::Slider::new(&mut l.saturation,-100..=100).text("Doygunluk"));ui.add(egui::Slider::new(&mut l.blur,0..=20).text("Kutu bulanıklığı"));});ui.collapsing("Seviyeler ve ton",|ui|{let t=&mut l.tones;ui.add(egui::Slider::new(&mut t.input_black,0..=254).text("Giriş siyah"));ui.add(egui::Slider::new(&mut t.input_white,1..=255).text("Giriş beyaz"));ui.add(egui::Slider::new(&mut t.output_black,0..=254).text("Çıkış siyah"));ui.add(egui::Slider::new(&mut t.output_white,1..=255).text("Çıkış beyaz"));ui.add(egui::Slider::new(&mut t.gamma,0.1..=10.).logarithmic(true).text("Gamma"));ui.add(egui::Slider::new(&mut t.exposure,-10. ..=10.).text("Pozlama"));ui.add(egui::Slider::new(&mut t.hue,-180..=180).text("Ton"));ui.add(egui::Slider::new(&mut t.vibrance,-100..=100).text("Canlılık"));ui.add(egui::Slider::new(&mut t.red_balance,-100..=100).text("Kırmızı dengesi"));ui.add(egui::Slider::new(&mut t.green_balance,-100..=100).text("Yeşil dengesi"));ui.add(egui::Slider::new(&mut t.blue_balance,-100..=100).text("Mavi dengesi"));ui.checkbox(&mut t.inverted,"Ters renk");ui.add(egui::Slider::new(&mut t.threshold,-1..=255).text("Eşik (-1 kapalı)"));ui.add(egui::Slider::new(&mut t.posterize,2..=256).text("Posterize"));});ui.collapsing("RGB eğrileri",|ui|{ui.label("Eğri üzerine tıklayarak nokta ekleyin. Sıfırla ile doğrusal eğriye dönün.");for(channel,name)in["RGB","Kırmızı","Yeşil","Mavi"].iter().enumerate(){ui.label(*name);let(r,response)=ui.allocate_exact_size(Vec2::new(255.,90.),Sense::click());ui.painter().rect_filled(r,0.,Color32::from_gray(20));for pts in l.tones.curves[channel].windows(2){let a=Pos2::new(r.left()+pts[0].x as f32,r.bottom()-pts[0].y as f32/255.*r.height());let b=Pos2::new(r.left()+pts[1].x as f32,r.bottom()-pts[1].y as f32/255.*r.height());ui.painter().line_segment([a,b],Stroke::new(2.0_f32,Color32::LIGHT_GREEN));}
if response.clicked()&& let Some(p)=response.interact_pointer_pos(){let pt=Point::new((p.x-r.left()).round().clamp(0.,255.)as i32,((r.bottom()-p.y)/r.height()*255.).round().clamp(0.,255.)as i32);let c=&mut l.tones.curves[channel];if let Some(old)=c.iter_mut().find(|v|(v.x-pt.x).abs()<5){old.y=pt.y}else if c.len()<64{c.push(pt);c.sort_by_key(|v|v.x);}}
if ui.small_button(format!("{name} sıfırla")).clicked(){l.tones.curves[channel]=vec![Point::new(0,0),Point::new(255,255)];}}});ui.collapsing("Kaynak koruyan filtreler",|ui|{let f=&mut l.filters;ui.add(egui::Slider::new(&mut f.gaussian,0..=16).text("Gaussian"));ui.add(egui::Slider::new(&mut f.sharpen_amount,0..=500).text("Keskinlik miktarı"));ui.add(egui::Slider::new(&mut f.sharpen_radius,1..=16).text("Keskinlik yarıçapı"));ui.add(egui::Slider::new(&mut f.sharpen_threshold,0..=255).text("Keskinlik eşiği"));ui.add(egui::Slider::new(&mut f.high_pass,0..=16).text("High pass"));ui.add(egui::Slider::new(&mut f.noise,0..=100).text("Gren"));ui.checkbox(&mut f.monochrome_noise,"Tek renkli gren");ui.add(egui::DragValue::new(&mut f.noise_seed).prefix("Tohum "));ui.add(egui::Slider::new(&mut f.mosaic,1..=128).text("Pikselleştirme"));});if l.mask.is_some(){ui.collapsing("Maske",|ui|{ui.checkbox(&mut l.mask_settings.enabled,"Etkin");ui.add(egui::Slider::new(&mut l.mask_settings.density,0..=100).text("Yoğunluk"));ui.add(egui::Slider::new(&mut l.mask_settings.feather,0..=16).text("Kenar yumuşatma"));});}
if (l!=before||toggled)&&l.tones.valid(){self.effects_draft=Some(l.clone());self.tabs[self.active].doc.state.layers[i]=if self.live_preview{l}else{self.effects_original.clone().unwrap()};self.invalidate();}ui.horizontal(|ui|{if ui.button("Uygula").clicked(){self.apply_effects();}
if ui.button("İptal").clicked(){self.tabs[self.active].doc.cancel_edit();self.dialog=None;self.invalidate();}});},
 Dialog::Brush=>{ui.add(egui::Slider::new(&mut self.brush.size,1..=512).text("Boyut"));ui.add(egui::Slider::new(&mut self.brush.hardness,0..=100).text("Sertlik"));ui.add(egui::Slider::new(&mut self.brush.flow,1..=100).text("Akış"));ui.add(egui::Slider::new(&mut self.brush.spacing,1..=200).text("Aralık %"));ui.add(egui::Slider::new(&mut self.brush.angle,-180..=180).text("Açı"));ui.add(egui::Slider::new(&mut self.brush.roundness,5..=100).text("Yuvarlaklık"));ui.collapsing("Tablet parametreleri",|ui|{ui.label(&self.tablet.status);ui.label("Fare tam basınç/sıfır eğim kullanır. Yerel kalem desteği Windows Ink ve Wayland tablet-v2 arka ucuna bağlıdır.");ui.checkbox(&mut self.brush.pressure_size,"Basınç → boyut");ui.checkbox(&mut self.brush.pressure_flow,"Basınç → akış");ui.checkbox(&mut self.brush.tilt_angle,"Eğim → açı");ui.checkbox(&mut self.brush.tilt_roundness,"Eğim → yuvarlaklık");ui.add(egui::Slider::new(&mut self.brush.pressure_minimum,1..=100).text("Minimum boyut %"));ui.add(egui::Slider::new(&mut self.brush.pressure_curve,0.25..=4.).text("Basınç eğrisi"));});ui.horizontal(|ui|{if ui.button("Görüntüden uç…").clicked()&& let Some(p)=rfd::FileDialog::new().pick_file()&& let Some(im)=self.result(project::read_image(&p)){self.brush.tip_mask=Some(Arc::new(brush::define_tip(&im)));}
if ui.button("Görünür seçimden uç").clicked(){let im=self.doc().state.selection.copy_from(&render::composite(&self.doc().state));let tip=brush::define_tip(&im);if tip.pixels().any(|p|p[0]>0){self.brush.tip_mask=Some(Arc::new(tip));}else{self.error=Some("Seçimde fırça ucu oluşturacak görünür koyu piksel yok.".into());}}
if ui.button("Yuvarlak uç").clicked(){self.brush.tip_mask=None;}});ui.separator();ui.collapsing("Yerleşik fırçalar",|ui|{for p in photodeck::settings::builtins(){if ui.button(&p.name).clicked(){self.brush=p.settings;self.tool=match p.tool{1=>Tool::Pencil,2=>Tool::Eraser,3=>Tool::Clone,_=>Tool::Brush};}}});ui.text_edit_singleline(&mut self.brush_name);if ui.button("Preset kaydet").clicked(){let p=BrushPreset{name:self.brush_name.clone(),tool:match self.tool{Tool::Pencil=>1,Tool::Eraser=>2,Tool::Clone=>3,_=>0},settings:self.brush.clone()};match photodeck::settings::merge_brushes(&self.brush_presets,&[p]){Ok(p)=>{self.brush_presets=p;self.persist_brushes();},Err(e)=>self.error=Some(e)}}let mut remove=None;for (i,p) in self.brush_presets.clone().iter().enumerate(){ui.horizontal(|ui|{if ui.button(&p.name).clicked(){self.brush=p.settings.clone();self.tool=match p.tool{1=>Tool::Pencil,2=>Tool::Eraser,3=>Tool::Clone,_=>Tool::Brush};}
if ui.small_button("×").clicked(){remove=Some(i);}});}
if let Some(i)=remove{self.brush_presets.remove(i);self.persist_brushes();}ui.horizontal(|ui|{if ui.button(".pdbrush içe aktar…").clicked()&& let Some(p)=rfd::FileDialog::new().add_filter("Fırça kütüphanesi",&["pdbrush"]).pick_file(){let r=photodeck::settings::read_brushes(&p).and_then(|p|photodeck::settings::merge_brushes(&self.brush_presets,&p));if let Some(p)=self.result(r){self.brush_presets=p;self.persist_brushes();}}
if ui.button("Dışa aktar…").clicked()&& let Some(p)=rfd::FileDialog::new().add_filter("Fırça kütüphanesi",&["pdbrush"]).set_file_name("Fırçalar.pdbrush").save_file(){self.result(photodeck::settings::write_brushes(&p,&self.brush_presets));}});},
 Dialog::Appearance=>{for(id,label)in PRESETS{if ui.selectable_value(&mut self.prefs.preset,id.to_owned(),label).changed(){self.apply_style(ctx)}}ui.checkbox(&mut self.prefs.tools,"Araç paneli");ui.checkbox(&mut self.prefs.layers,"Katman paneli");if ui.add(egui::Slider::new(&mut self.prefs.font_size,10. ..=20.).text("Yazı boyutu")).changed(){self.apply_style(ctx)}
if ui.add(egui::Slider::new(&mut self.prefs.scale,0.75..=2.).text("Arayüz ölçeği").suffix("×")).changed(){self.apply_style(ctx)}
if ui.color_edit_button_srgb(&mut self.prefs.accent).changed(){self.apply_style(ctx)}ui.label("Panellerin genişliğini kenarlarından sürükleyerek ayarlayın.");ui.text_edit_singleline(&mut self.preset_name);if ui.button("Kişisel preset kaydet").clicked(){let name=self.preset_name.trim().to_string();if name.is_empty()||name.chars().count()>80{self.error=Some("Preset adı 1–80 karakter olmalı.".into());}else{let value=self.prefs.to_pdui();if let Some(p)=self.prefs.named.iter_mut().find(|p|p.0==name){p.1=value;}else if self.prefs.named.len()<64{self.prefs.named.push((name,value));}else{self.error=Some("En fazla 64 kişisel preset.".into());}}}
for(name,value)in self.prefs.named.clone(){ui.horizontal(|ui|{if ui.button(&name).clicked()&& let Ok(mut prefs)=UiPrefs::from_pdui(&serde_json::to_vec(&value).unwrap()){prefs.named=self.prefs.named.clone();self.prefs=prefs;self.apply_style(ctx);}
if ui.small_button("×").clicked(){self.prefs.named.retain(|p|p.0!=name);}});}ui.horizontal(|ui|{if ui.button("Görünümü dışa aktar…").clicked()&& let Some(p)=rfd::FileDialog::new().add_filter("PhotoDeck görünümü",&["pdui"]).set_file_name("PhotoDeck-gorunum.pdui").save_file()&& let Ok(b)=serde_json::to_vec_pretty(&self.prefs.to_pdui()){self.result(project::atomic_write(&p,&b));}
if ui.button("İçe aktar…").clicked()&& let Some(p)=rfd::FileDialog::new().pick_file(){let r=bounded_read(&p,131072).and_then(|b|UiPrefs::from_pdui(&b));if let Some(p)=self.result(r){self.prefs=p;self.apply_style(ctx);}}});},
 Dialog::Batch=>{ui.label("Orijinaller korunur; çıktılar _photodeck eki ve çakışmasız isimlerle yazılır.");ui.horizontal(|ui|{ui.add(egui::DragValue::new(&mut self.width).range(1..=16384).prefix("Genişlik "));ui.add(egui::DragValue::new(&mut self.height).range(1..=16384).prefix("Yükseklik "));});ui.checkbox(&mut self.batch_upscale,"Küçük görüntüleri büyüt");egui::ComboBox::from_label("Biçim").selected_text(&self.batch_format).show_ui(ui,|ui|{for format in ["png","jpg","webp","bmp","tif"]{ui.selectable_value(&mut self.batch_format,format.into(),format);}});ui.add_enabled(self.batch_format=="jpg",egui::Slider::new(&mut self.quality,0..=100).text("JPEG kalitesi"));ui.label("WebP kayıpsızdır.");for(i,label)in["Parlaklık","Kontrast","Doygunluk"].iter().enumerate(){ui.add(egui::Slider::new(&mut self.batch_adjust[i],-100..=100).text(*label));}
if ui.add_enabled(Size::new(self.width,self.height).valid(),egui::Button::new("Dosyaları ve çıktı klasörünü seç…")).clicked()&& let Some(files)=rfd::FileDialog::new().add_filter("Görüntüler",&["png","jpg","jpeg","webp","bmp","tif","tiff","gif"]).pick_files()&& let Some(dest)=rfd::FileDialog::new().pick_folder(){self.begin_batch(files,dest);}},
 Dialog::Help=>{ui.label("1. Dosya → Aç ile fotoğraf açın; Katman içe aktar ile belgeye ekleyin.");ui.label("2. Soldaki araçları seçin. Üst çubuk seçili aracın ayarlarını gösterir.");ui.label("3. Kaydet, düzenlenebilir .pdeck projesini; Dışa aktar, paylaşılabilir görüntüyü yazar.");ui.separator();for line in ["Ctrl+N yeni · Ctrl+O aç · Ctrl+S kaydet · Ctrl+Shift+S farklı kaydet","Ctrl+Z geri al · Ctrl+Shift+Z yinele · Ctrl+W sekmeyi kapat","V taşı · B fırça · E silgi · T metin · H el · X renkleri değiştir","M dikdörtgen seçim · Shift+M eliptik seçim · L kement · Shift+L çokgen","Ctrl+T dönüşüm · Enter uygula · Esc iptal","Ctrl+0 tuvale sığdır · Ctrl+1 gerçek boyut · tekerlek yakınlaştır","Tab/Shift+Tab denetimler arasında · Enter/Space etkinleştir","Görünüm → Çalışma alanı: yazı boyutu, arayüz ölçeği ve paneller"]{ui.label(line);}ui.checkbox(&mut self.prefs.show_tips,"Başlangıç ipucunu göster");},
 Dialog::About=>{ui.heading("PhotoDeck");ui.label(format!("{} · Yerel Rust / egui",env!("CARGO_PKG_VERSION")));ui.label("Tamamen yerel RGB 8 bit katman düzenleyici. Ağ hesabı ve tarayıcı motoru kullanmaz.");ui.label(".pdeck v1–v7 projeleri, kaynak koruyan akıllı nesneler, filtreler ve dönüşümler.");ui.label(&self.tablet.status);ui.label("Yazdırma sistemin yerel yazıcı iletişim kutusunu kullanır. Linux: xdg-desktop-portal Print desteği gerekir.");}
 }});});
            if !open && self.work.user_busy() {
                self.work.cancel();
                open = true;
            }
            if !open {
                if kind == Dialog::Effects {
                    self.tabs[self.active].doc.cancel_edit();
                    self.invalidate();
                }
                if kind == Dialog::Transform {
                    self.finish_transform(false);
                }
                self.dialog = None;
            }
        }
        self.guards(ctx);
    }
    fn persist_brushes(&mut self) {
        if self.options.no_settings {
            return;
        }
        if let Some(store) = &mut self.brush_store {
            let r = store.replace(self.brush_presets.clone());
            self.result(r);
        } else {
            self.error=Some("Kalıcı fırça kütüphanesi açılamadı; değişiklikler yalnızca bu oturumda. Dışa aktar ile ayrı dosya kaydedebilirsiniz.".into());
        }
    }
    fn guards(&mut self, ctx: &egui::Context) {
        if let Some(error) = self.error.clone() {
            egui::Window::new("İşlem tamamlanamadı")
                .collapsible(false)
                .resizable(false)
                .show(ctx, |ui| {
                    ui.label(error);
                    if ui.button("Tamam").clicked() {
                        self.error = None;
                    }
                });
        }
        if let Some(id) = self.close_tab {
            let Some(i) = self.tab_index(id) else {
                self.close_tab = None;
                return;
            };
            egui::Window::new("Kaydedilmemiş değişiklikler")
                .collapsible(false)
                .show(ctx, |ui| {
                    ui.label(format!(
                        "{} değişikliklerini kaydetmek ister misiniz?",
                        self.tabs[i].doc.title
                    ));
                    ui.label(
                        self.tabs[i]
                            .source_path
                            .as_ref()
                            .map(|p| p.display().to_string())
                            .unwrap_or_else(|| "Henüz dosyaya kaydedilmedi".into()),
                    );
                    ui.horizontal(|ui| {
                        if ui.button("Kaydet").clicked() && self.save_document(id, false) {
                            self.close_tab = None;
                            self.remove_tab(i);
                        }
                        if ui.button("Kaydetmeden kapat").clicked() {
                            self.close_tab = None;
                            self.remove_tab(i);
                        }
                        if ui.button("İptal").clicked() {
                            self.close_tab = None;
                            self.closing = false;
                        }
                    });
                });
        }
        if let Some((id, path, upgrade)) = self.save_conflict.clone() {
            egui::Window::new(if upgrade{"Proje biçimi yükseltmesi"}else{"Dosya dışarıdan değişti"}).collapsible(false).show(ctx,|ui|{ui.label(absolute_path(&path).display().to_string());ui.separator();ui.label(if upgrade{"Bu kayıt daha yeni proje biçimi gerektiriyor. Eski uygulamalar açamayabilir."}else{"Diskteki dosya değiştirildi veya silindi. Ayrı bir kopya kaydedebilir veya açıkça üzerine yazabilirsiniz."});ui.horizontal(|ui|{if ui.button("Ayrı kopya…").clicked(){self.save_conflict=None;self.save_document(id,true);}
if !upgrade && ui.button("Üzerine yaz").clicked(){self.save_conflict=None;self.save_to_document(id,path.clone(),!upgrade,upgrade);}
if ui.button("İptal").clicked(){self.save_conflict=None;}});});
        }
        if !self.modal_active() && !self.recoveries.is_empty() {
            egui::Window::new("Kurtarılabilir belgeler")
                .collapsible(false)
                .show(ctx, |ui| {
                    ui.label("Önceki oturumdan çalışma kopyaları bulundu.");
                    let mut remove = None;
                    for i in 0..self.recoveries.len() {
                        let name = self.recoveries[i].title.clone();
                        let path = self.recoveries[i].path.clone();
                        ui.horizontal(|ui| {
                            ui.label(name).on_hover_text(format!(
                                "Kaynak: {}\nKurtarma kopyası: {}",
                                self.recoveries[i]
                                    .source_path
                                    .as_ref()
                                    .map(|p| p.display().to_string())
                                    .unwrap_or_else(|| "Kaydedilmemiş belge".into()),
                                path.display()
                            ));
                            if ui.button("Kurtar").clicked()
                                && let Some(d) = self.result(photodeck::recovery::recover(&path))
                            {
                                self.push(d);
                                self.tabs[self.active].source_path =
                                    self.recoveries[i].source_path.clone();
                                self.tabs[self.active].recovery_origin = Some(path.clone());
                                remove = Some(i);
                            }
                            if ui.button("Yoksay").clicked() {
                                remove = Some(i);
                            }
                        });
                    }
                    if let Some(i) = remove {
                        self.recoveries.remove(i);
                    }
                });
        }
    }
    fn shortcuts(&mut self, ctx: &egui::Context) {
        if self.modal_active()
            || (ctx.wants_keyboard_input() && ctx.memory(|m| m.focused()) != self.canvas_id)
        {
            return;
        }
        let mut action = if ctx.input(|i| i.key_pressed(egui::Key::F1)) {
            Some("help")
        } else {
            None
        };
        let mut preset_index = None;
        ctx.input_mut(|i| {
            let cmd = egui::Modifiers::COMMAND;
            for (key, mods, a) in [
                (
                    egui::Key::N,
                    egui::Modifiers { shift: true, ..cmd },
                    "layer",
                ),
                (egui::Key::N, cmd, "new"),
                (egui::Key::O, cmd, "open"),
                (
                    egui::Key::S,
                    egui::Modifiers { shift: true, ..cmd },
                    "saveas",
                ),
                (egui::Key::S, cmd, "save"),
                (egui::Key::Z, egui::Modifiers { shift: true, ..cmd }, "redo"),
                (egui::Key::Z, cmd, "undo"),
                (egui::Key::J, egui::Modifiers { shift: true, ..cmd }, "cut"),
                (
                    egui::Key::J,
                    egui::Modifiers { alt: true, ..cmd },
                    "duplicate",
                ),
                (egui::Key::J, cmd, "duplicate"),
                (
                    egui::Key::T,
                    egui::Modifiers { alt: true, ..cmd },
                    "numeric",
                ),
                (egui::Key::T, cmd, "transform"),
                (egui::Key::A, cmd, "selectall"),
                (
                    egui::Key::D,
                    egui::Modifiers { shift: true, ..cmd },
                    "reselect",
                ),
                (egui::Key::D, cmd, "deselect"),
                (
                    egui::Key::I,
                    egui::Modifiers { shift: true, ..cmd },
                    "invertselection",
                ),
                (egui::Key::V, cmd, "paste"),
                (egui::Key::C, cmd, "copy"),
                (
                    egui::Key::E,
                    egui::Modifiers {
                        alt: true,
                        shift: true,
                        ..cmd
                    },
                    "mergevisible",
                ),
                (
                    egui::Key::E,
                    egui::Modifiers { shift: true, ..cmd },
                    "export",
                ),
                (egui::Key::E, cmd, "merge"),
                (egui::Key::L, cmd, "effects"),
                (
                    egui::Key::B,
                    egui::Modifiers {
                        alt: true,
                        shift: true,
                        ..cmd
                    },
                    "brush",
                ),
                (egui::Key::B, egui::Modifiers { alt: true, ..cmd }, "brush"),
                (
                    egui::Key::P,
                    egui::Modifiers { alt: true, ..cmd },
                    "appearance",
                ),
                (
                    egui::Key::G,
                    egui::Modifiers { alt: true, ..cmd },
                    "clipping",
                ),
                (egui::Key::Num0, cmd, "fit"),
                (egui::Key::Num1, cmd, "actual"),
                (egui::Key::W, cmd, "close"),
            ] {
                if i.consume_key(mods, key) {
                    action = Some(a)
                }
            }
            for (index, key) in [
                egui::Key::Num1,
                egui::Key::Num2,
                egui::Key::Num3,
                egui::Key::Num4,
                egui::Key::Num5,
                egui::Key::Num6,
                egui::Key::Num7,
                egui::Key::Num8,
                egui::Key::Num9,
                egui::Key::Num0,
            ]
            .into_iter()
            .enumerate()
            {
                if i.consume_key(egui::Modifiers { alt: true, ..cmd }, key) {
                    preset_index = Some(index);
                }
            }
            if !i.modifiers.command && !i.modifiers.alt {
                for (key, t) in [
                    (egui::Key::V, Tool::Move),
                    (
                        egui::Key::M,
                        if i.modifiers.shift {
                            Tool::EllipseSelect
                        } else {
                            Tool::Select
                        },
                    ),
                    (egui::Key::B, Tool::Brush),
                    (egui::Key::P, Tool::Pencil),
                    (egui::Key::E, Tool::Eraser),
                    (egui::Key::S, Tool::Clone),
                    (egui::Key::C, Tool::Crop),
                    (egui::Key::G, Tool::Gradient),
                    (egui::Key::F, Tool::Fill),
                    (egui::Key::H, Tool::Hand),
                    (egui::Key::I, Tool::Picker),
                    (egui::Key::W, Tool::Wand),
                    (egui::Key::T, Tool::Text),
                    (egui::Key::U, Tool::Rectangle),
                    (egui::Key::O, Tool::Ellipse),
                    (
                        egui::Key::L,
                        if i.modifiers.shift {
                            Tool::Polygon
                        } else {
                            Tool::Lasso
                        },
                    ),
                ] {
                    if i.key_pressed(key) {
                        if self.gesture.is_some() {
                            self.finish_gesture();
                        }
                        self.tool = t;
                    }
                }
                if i.key_pressed(egui::Key::X) {
                    std::mem::swap(&mut self.foreground, &mut self.background);
                }
            }
        });
        if let Some(index) = preset_index {
            self.prefs.preset = PRESETS[index].0.into();
            self.apply_style(ctx);
        }
        if let Some(a) = action {
            self.action(a)
        }
        if ctx.input(|i| i.key_pressed(egui::Key::Escape)) {
            if self.transforming {
                self.finish_transform(false)
            } else if self.gesture.is_some() {
                self.tabs[self.active].doc.cancel_edit();
                self.gesture = None;
                self.invalidate();
            }
            self.polygon.clear();
        }
        if ctx.input(|i| i.key_pressed(egui::Key::Enter)) {
            if self.transforming {
                self.finish_transform(true)
            } else if self.polygon.len() > 2 {
                let p = self
                    .polygon
                    .iter()
                    .map(|p| Point::new(p.0 as i32, p.1 as i32))
                    .collect::<Vec<_>>();
                self.queue_path_selection(p);
                self.polygon.clear();
            }
        }
        if ctx.input(|i| i.key_pressed(egui::Key::Delete)) {
            self.run(|d| d.delete_selection());
        }
        if self.tool == Tool::Move && !self.transforming {
            let step = if ctx.input(|i| i.modifiers.shift) {
                10
            } else {
                1
            };
            let mut delta = Point::default();
            ctx.input(|i| {
                if i.key_pressed(egui::Key::ArrowLeft) {
                    delta.x -= step
                }
                if i.key_pressed(egui::Key::ArrowRight) {
                    delta.x += step
                }
                if i.key_pressed(egui::Key::ArrowUp) {
                    delta.y -= step
                }
                if i.key_pressed(egui::Key::ArrowDown) {
                    delta.y += step
                }
            });
            if delta != Point::default() && !self.doc().active_layer().locked {
                if !self.nudge_active {
                    let r = self.tabs[self.active].doc.begin_edit();
                    if self.result(r).is_some() {
                        self.nudge_active = true;
                    }
                }
                if self.nudge_active {
                    let i = self.doc_active();
                    let layer = &mut self.tabs[self.active].doc.state.layers[i];
                    layer.offset.x = layer.offset.x.saturating_add(delta.x);
                    layer.offset.y = layer.offset.y.saturating_add(delta.y);
                    self.invalidate();
                }
            }
            let held = ctx.input(|i| {
                [
                    egui::Key::ArrowLeft,
                    egui::Key::ArrowRight,
                    egui::Key::ArrowUp,
                    egui::Key::ArrowDown,
                ]
                .iter()
                .any(|k| i.key_down(*k))
            });
            if self.nudge_active && !held {
                let r = self.tabs[self.active].doc.commit("Katman taşındı");
                self.result(r);
                self.nudge_active = false;
                self.invalidate();
            }
        }
    }
}
impl eframe::App for PhotoDeck {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.frames += 1;
        self.poll_background(ctx);
        self.pump_tablet(ctx);
        ctx.send_viewport_cmd(egui::ViewportCommand::Title(format!(
            "{}{} — PhotoDeck",
            self.tabs[self.active]
                .source_path
                .as_ref()
                .map(|p| p.display().to_string())
                .unwrap_or_else(|| self.doc().title.clone()),
            if self.doc().dirty { " *" } else { "" }
        )));
        self.shortcuts(ctx);
        for f in ctx.input(|i| i.raw.dropped_files.clone()) {
            if let Some(p) = f.path {
                self.open(&p, false);
            }
        }
        if ctx.input(|i| {
            i.events
                .iter()
                .any(|e| matches!(e, egui::Event::WindowFocused(false)))
        }) && self.gesture.is_some()
        {
            self.finish_gesture();
        }
        if ctx.input(|i| i.viewport().close_requested()) {
            self.request_app_close(ctx);
        }
        if self.closing && self.close_tab.is_none() {
            if let Some(i) = self.tabs.iter().position(|t| t.doc.dirty) {
                self.close_tab = Some(self.tabs[i].id);
            } else {
                self.persist_ui();
                ctx.send_viewport_cmd(egui::ViewportCommand::Close);
            }
        }
        self.menus(ctx);
        egui::TopBottomPanel::bottom("status").show(ctx, |ui| {
            ui.horizontal(|ui| {
                self.work_status(ui);
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    let t = &self.tabs[self.active];
                    ui.label(format!(
                        "{:.0}%  ·  {} × {} px  ·  {} katman",
                        t.zoom * 100.,
                        t.doc.state.size.width,
                        t.doc.state.size.height,
                        t.doc.state.layers.len()
                    ));
                });
            });
        });
        if self.prefs.preset != "classic-pro" {
            self.document_tabs(ctx);
        }
        self.sidebar(ctx);
        if self.prefs.preset == "classic-pro" {
            self.document_tabs(ctx);
        }
        self.canvas(ctx);
        self.dialogs(ctx);
        if self.prefs.show_tips
            && !self.options.no_settings
            && !self.modal_active()
            && self.recoveries.is_empty()
        {
            let mut show = true;
            egui::Window::new("PhotoDeck'e hoş geldiniz").open(&mut show).collapsible(false).default_width(310.).show(ctx,|ui|{
                ui.label("Dosya → Aç ile başlayın. Sol araçları seçince ayarları üstte görünür; katman özellikleri sağdadır.");
                ui.label(".pdeck düzenlenebilir projedir. PNG/JPEG için Dışa aktar kullanın.");
                if ui.button("Kısayollar ve hızlı başlangıç · F1").clicked(){self.dialog=Some(Dialog::Help);}
                if ui.button("Anladım").clicked(){self.prefs.show_tips=false;}
            });
            if !show {
                self.prefs.show_tips = false;
            }
        }
        if let Some(recovery) = &mut self.recovery {
            if let Some(Err(e)) = recovery.poll() {
                self.error = Some(e);
                self.recovery_retry = Instant::now() + Duration::from_secs(45);
                for t in &mut self.tabs {
                    t.recovered_revision = 0;
                }
            }
            for t in &mut self.tabs {
                if !t.doc.dirty && t.has_recovery {
                    if let Err(e) = recovery.remove(t.id) {
                        self.error = Some(e);
                    } else {
                        t.recovered_revision = 0;
                        t.has_recovery = false;
                    }
                }
                if Instant::now() >= self.recovery_retry
                    && t.doc.dirty
                    && !t.doc.is_editing()
                    && t.doc.revision() != t.recovered_revision
                    && t.last_change.elapsed() > Duration::from_secs(5)
                {
                    match recovery.submit_with_source(
                        t.id,
                        &t.doc.state,
                        &t.doc.title,
                        t.source_path.as_deref(),
                    ) {
                        Ok(true) => {
                            t.recovered_revision = t.doc.revision();
                            t.has_recovery = true;
                        }
                        Ok(false) => {}
                        Err(e) => self.error = Some(e),
                    }
                }
            }
        }
        if self.frames > 3
            && self.tabs[self.active].texture.is_some()
            && !self.tabs[self.active].invalid
            && self.tabs[self.active].rendered_generation == self.tabs[self.active].generation
            && self.tabs[self.active].rendered_revision == self.doc().revision()
            && !self.screenshot_requested
            && self.options.screenshot.is_some()
        {
            ctx.send_viewport_cmd(egui::ViewportCommand::Screenshot(egui::UserData::default()));
            self.screenshot_requested = true;
        }
        if let Some(path) = &self.options.screenshot {
            for event in ctx.input(|i| i.events.clone()) {
                if let egui::Event::Screenshot { image, .. } = event {
                    let bytes = image
                        .pixels
                        .iter()
                        .flat_map(|p| p.to_array())
                        .collect::<Vec<_>>();
                    if let Some(im) =
                        RgbaImage::from_raw(image.size[0] as u32, image.size[1] as u32, bytes)
                        && let Err(e) = im.save(path)
                    {
                        eprintln!("Ekran görüntüsü: {e}");
                    }
                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                }
            }
        }
        ctx.request_repaint_after(Duration::from_millis(500));
    }
    fn on_exit(&mut self, _gl: Option<&eframe::glow::Context>) {
        self.persist_ui();
    }
}
impl PhotoDeck {
    fn persist_ui(&self) {
        if !self.options.no_settings
            && let Some(path) = prefs_path()
        {
            if let Some(parent) = path.parent() {
                let _ = std::fs::create_dir_all(parent);
            }
            if let Ok(data) = serde_json::to_vec_pretty(&self.prefs)
                && let Err(e) = project::atomic_write(&path, &data)
            {
                eprintln!("Görünüm kaydedilemedi: {e}");
            }
        }
    }
}
fn prefs_path() -> Option<PathBuf> {
    directories::ProjectDirs::from("org", "PhotoDeck", "PhotoDeckRust")
        .map(|d| d.config_dir().join("rust-ui.json"))
}
fn drag_rect(a: (f64, f64), b: (f64, f64)) -> Rect {
    let x = a.0.min(b.0).floor();
    let y = a.1.min(b.1).floor();
    Rect::new(
        x as i32,
        y as i32,
        (a.0.max(b.0).ceil() - x).max(1.) as u32,
        (a.1.max(b.1).ceil() - y).max(1.) as u32,
    )
}
#[cfg(test)]
fn spawn_reaped(
    command: &mut std::process::Command,
) -> std::io::Result<std::thread::JoinHandle<std::io::Result<std::process::ExitStatus>>> {
    let mut child = command.spawn()?;
    Ok(std::thread::spawn(move || child.wait()))
}
fn demo_document() -> Document {
    let (state, _) = project::decode_state(include_bytes!("../examples/Horizon.pdeck"))
        .expect("Bundled Horizon project must be valid");
    let mut doc = Document::from_state(state).expect("Bundled Horizon state must be valid");
    doc.title = "Horizon".into();
    doc.mark_saved();
    doc
}

pub fn write_demo(path: &Path) -> Result<()> {
    let mut d = demo_document();
    project::save(
        &mut d,
        path,
        None,
        project::SaveOptions {
            overwrite_external: false,
            upgrade_format: false,
        },
    )
    .map(|_| ())
    .map_err(|e| e.to_string())
}

fn install_ui_fonts(ctx: &egui::Context) {
    let mut db = fontdb::Database::new();
    db.load_system_fonts();
    let mut fonts = egui::FontDefinitions::default();
    for (name, families) in [
        (
            "native-ui",
            vec![
                fontdb::Family::Name("DejaVu Sans"),
                fontdb::Family::Name("Segoe UI"),
                fontdb::Family::Name("Helvetica"),
                fontdb::Family::SansSerif,
            ],
        ),
        (
            "native-symbols",
            vec![
                fontdb::Family::Name("Noto Sans Symbols 2"),
                fontdb::Family::Name("Segoe UI Symbol"),
                fontdb::Family::Name("Apple Symbols"),
            ],
        ),
    ] {
        if let Some(id) = db.query(&fontdb::Query {
            families: &families,
            ..Default::default()
        }) && let Some(data) = db.with_face_data(id, |bytes, index| {
            let mut data = egui::FontData::from_owned(bytes.to_vec());
            data.index = index;
            data
        }) {
            fonts.font_data.insert(name.into(), Arc::new(data));
            fonts
                .families
                .entry(egui::FontFamily::Proportional)
                .or_default()
                .push(name.into());
        }
    }
    ctx.set_fonts(fonts);
}
impl UiPrefs {
    fn migrate(&mut self) {
        if self.ui_version == 0 {
            // Only replace the untouched previous default. Imported .pdui,
            // named/custom layouts and deliberate preset choices survive.
            if self.preset == "focus"
                && self.panel_width == 280.
                && self.font_size == 14.
                && self.accent == [89, 201, 180]
                && self.tools
                && self.layers
                && self.legacy.is_empty()
                && self.named.is_empty()
            {
                self.preset = "classic-pro".into();
                self.panel_width = 260.;
                self.font_size = 12.;
            }
            self.ui_version = 1;
        }
    }
    fn to_pdui(&self) -> serde_json::Value {
        let mut j = self.legacy.clone();
        for (k, v) in [
            ("version", serde_json::json!(1)),
            ("preset", serde_json::json!(self.preset)),
            ("uiScale", serde_json::json!(self.scale)),
            ("fontSize", serde_json::json!(self.font_size.round() as i32)),
            (
                "panelWidth",
                serde_json::json!(self.panel_width.round() as i32),
            ),
            (
                "accent",
                serde_json::json!(format!(
                    "#{:02x}{:02x}{:02x}",
                    self.accent[0], self.accent[1], self.accent[2]
                )),
            ),
            ("focusToolsVisible", serde_json::json!(self.tools)),
            ("focusLayersVisible", serde_json::json!(self.layers)),
        ] {
            j.insert(k.into(), v);
        }
        serde_json::Value::Object(j)
    }
    fn from_pdui(bytes: &[u8]) -> Result<Self> {
        if bytes.len() > 131072 {
            return Err("Preset 128 KiB sınırını aşıyor.".into());
        }
        let value: serde_json::Value = serde_json::from_slice(bytes).map_err(|e| e.to_string())?;
        let j = value.as_object().ok_or("Geçersiz preset nesnesi")?;
        if j.get("version").and_then(|v| v.as_u64()) != Some(1) {
            return Err("Desteklenmeyen görünüm sürümü".into());
        }
        let preset = j
            .get("preset")
            .and_then(|v| v.as_str())
            .ok_or("Preset adı eksik")?;
        if !PRESETS.iter().any(|p| p.0 == preset) {
            return Err("Bilinmeyen görünüm".into());
        }
        for (k, min, max) in [
            ("fontSize", 10, 20),
            ("iconSize", 16, 36),
            ("radius", 0, 16),
            ("panelWidth", 220, 440),
        ] {
            if let Some(v) = j.get(k) {
                let n = v.as_i64().ok_or("Geçersiz sayısal görünüm değeri")?;
                if n < min || n > max {
                    return Err(format!("{k} sınır dışında"));
                }
            }
        }
        for k in [
            "toolLabels",
            "movablePanels",
            "showMenu",
            "showStatus",
            "rulers",
            "focusToolsVisible",
            "focusLayersVisible",
        ] {
            if j.get(k).is_some_and(|v| !v.is_boolean()) {
                return Err(format!("{k} geçersiz"));
            }
        }
        for k in ["accent", "canvas", "panel", "background"] {
            if let Some(v) = j.get(k) {
                let t = v.as_str().ok_or("Geçersiz renk")?;
                if !t.is_empty() && parse_hex(t).is_none() {
                    return Err("Geçersiz renk".into());
                }
            }
        }
        for k in ["toolsPosition", "layersPosition"] {
            if let Some(v) = j.get(k) {
                let a = v.as_array().ok_or("Geçersiz panel konumu")?;
                if a.len() != 2
                    || a.iter()
                        .any(|v| v.as_f64().is_none_or(|v| !(-1. ..=1.).contains(&v)))
                {
                    return Err("Geçersiz panel konumu".into());
                }
            }
        }
        for (k, allowed) in [
            ("theme", vec!["preset", "light", "dark"]),
            ("panelSide", vec!["preset", "left", "right"]),
        ] {
            if let Some(v) = j.get(k)
                && !allowed.contains(&v.as_str().unwrap_or(""))
            {
                return Err(format!("{k} geçersiz"));
            }
        }
        if let Some(v) = j.get("fontFamily")
            && v.as_str()
                .is_none_or(|s| s.is_empty() || s.chars().count() > 128)
        {
            return Err("Geçersiz font ailesi".into());
        }
        if let Some(v) = j.get("dockState") {
            use base64::Engine;
            let data = v.as_str().ok_or("Geçersiz panel kaydı")?;
            if data.len() > 90000
                || base64::engine::general_purpose::STANDARD
                    .decode(data)
                    .map_err(|_| "Geçersiz panel kaydı")?
                    .len()
                    > 65536
            {
                return Err("Panel kaydı çok büyük".into());
            }
        }
        let mut p = Self {
            preset: preset.into(),
            ..Self::default()
        };
        if let Some(scale) = j.get("uiScale") {
            let scale = scale.as_f64().ok_or("Geçersiz arayüz ölçeği")?;
            if !(0.75..=2.).contains(&scale) {
                return Err("Arayüz ölçeği sınır dışında".into());
            }
            p.scale = scale as f32;
        }
        p.font_size = j.get("fontSize").and_then(|v| v.as_f64()).unwrap_or(14.) as f32;
        p.panel_width = j.get("panelWidth").and_then(|v| v.as_f64()).unwrap_or(280.) as f32;
        p.tools = j
            .get("focusToolsVisible")
            .and_then(|v| v.as_bool())
            .unwrap_or(true);
        p.layers = j
            .get("focusLayersVisible")
            .and_then(|v| v.as_bool())
            .unwrap_or(true);
        if let Some(c) = j.get("accent").and_then(|v| v.as_str()).and_then(parse_hex) {
            p.accent = c;
        }
        p.legacy = j.clone();
        Ok(p)
    }
    fn color(&self, key: &str, fallback: [u8; 3]) -> Color32 {
        let c = self
            .legacy
            .get(key)
            .and_then(|v| v.as_str())
            .and_then(parse_hex)
            .unwrap_or(fallback);
        Color32::from_rgb(c[0], c[1], c[2])
    }
    fn flag(&self, key: &str, default: bool) -> bool {
        self.legacy
            .get(key)
            .and_then(|v| v.as_bool())
            .unwrap_or(default)
    }
}
fn parse_hex(s: &str) -> Option<[u8; 3]> {
    if s.len() != 7 || !s.starts_with('#') || !s.is_ascii() {
        return None;
    }
    Some([
        u8::from_str_radix(&s[1..3], 16).ok()?,
        u8::from_str_radix(&s[3..5], 16).ok()?,
        u8::from_str_radix(&s[5..7], 16).ok()?,
    ])
}
fn handle_transform(
    b: Rect,
    start: (f64, f64),
    end: (f64, f64),
    handle: i32,
    alt: bool,
    shift: bool,
) -> Transform {
    let left = b.x as f64;
    let top = b.y as f64;
    let right = left + b.width as f64;
    let bottom = top + b.height as f64;
    let center = ((left + right) / 2., (top + bottom) / 2.);
    if handle == 8 {
        let mut angle = ((end.1 - center.1).atan2(end.0 - center.0)
            - (start.1 - center.1).atan2(start.0 - center.0))
        .to_degrees();
        if shift {
            angle = (angle / 15.).round() * 15.;
        }
        return Transform::translate(-center.0, -center.1)
            .then(Transform::rotate(angle))
            .then(Transform::translate(center.0, center.1));
    }
    let anchor = if alt {
        center
    } else {
        match handle {
            0 => (right, bottom),
            1 => (center.0, bottom),
            2 => (left, bottom),
            3 => (left, center.1),
            4 => (left, top),
            5 => (center.0, top),
            6 => (right, top),
            _ => (right, center.1),
        }
    };
    let ratio = |a: f64, b: f64| {
        if b.abs() > 1e-8 {
            (a / b).max(0.01)
        } else {
            1.
        }
    };
    let mut sx = if matches!(handle, 1 | 5) {
        1.
    } else {
        ratio(end.0 - anchor.0, start.0 - anchor.0)
    };
    let mut sy = if matches!(handle, 3 | 7) {
        1.
    } else {
        ratio(end.1 - anchor.1, start.1 - anchor.1)
    };
    if handle % 2 == 0 && !shift {
        let scale = if (sx - 1.).abs() > (sy - 1.).abs() {
            sx
        } else {
            sy
        };
        sx = scale;
        sy = scale;
    }
    Transform::translate(-anchor.0, -anchor.1)
        .then(Transform::scale(sx, sy))
        .then(Transform::translate(anchor.0, anchor.1))
}

fn commit_before_navigation(doc: &mut Document) -> Result<()> {
    if doc.is_editing() {
        doc.commit("Bekleyen düzenleme")
    } else {
        Ok(())
    }
}
fn bounded_read(path: &Path, limit: u64) -> Result<Vec<u8>> {
    use std::io::Read;
    let metadata = std::fs::metadata(path).map_err(|e| e.to_string())?;
    if !metadata.is_file() || metadata.len() > limit {
        return Err("Dosya boyutu sınırı aşıldı veya normal dosya değil.".into());
    }
    let file = std::fs::File::open(path).map_err(|e| e.to_string())?;
    let metadata = file.metadata().map_err(|e| e.to_string())?;
    if !metadata.is_file() || metadata.len() > limit {
        return Err("Dosya boyutu sınırı aşıldı veya normal dosya değil.".into());
    }
    let mut bytes = Vec::new();
    file.take(limit + 1)
        .read_to_end(&mut bytes)
        .map_err(|e| e.to_string())?;
    if bytes.len() as u64 > limit {
        return Err("Dosya boyutu sınırı aşıldı.".into());
    }
    Ok(bytes)
}
impl UiPrefs {
    fn validate(&self) -> Result<()> {
        if !self.scale.is_finite()
            || !(0.75..=2.).contains(&self.scale)
            || self.recent.len() > 12
            || !self.font_size.is_finite()
            || !(10. ..=20.).contains(&self.font_size)
            || !self.panel_width.is_finite()
            || !(220. ..=440.).contains(&self.panel_width)
            || self.named.len() > 64
        {
            return Err("Kaydedilmiş görünüm ayarları sınır dışında.".into());
        }
        UiPrefs::from_pdui(&serde_json::to_vec(&self.to_pdui()).map_err(|e| e.to_string())?)?;
        for (name, value) in &self.named {
            if name.is_empty() || name.chars().count() > 80 {
                return Err("Geçersiz kişisel görünüm adı.".into());
            }
            UiPrefs::from_pdui(&serde_json::to_vec(value).map_err(|e| e.to_string())?)?;
        }
        Ok(())
    }
}
fn locked_dimensions(source: Size, width: u32, height: u32, width_changed: bool) -> Size {
    let requested = if width_changed {
        width as f64 / source.width as f64
    } else {
        height as f64 / source.height as f64
    };
    let scale = requested
        .max(1.0 / source.width as f64)
        .max(1.0 / source.height as f64)
        .min(16384.0 / source.width as f64)
        .min(16384.0 / source.height as f64);
    Size::new(
        (source.width as f64 * scale).round().clamp(1.0, 16384.0) as u32,
        (source.height as f64 * scale).round().clamp(1.0, 16384.0) as u32,
    )
}
#[cfg(test)]
mod tests {
    #[test]
    fn classic_pro_migrates_only_untouched_legacy_default() {
        let mut prefs: UiPrefs =
            serde_json::from_str(r#"{"preset":"focus","panel_width":280,"font_size":14}"#).unwrap();
        assert_eq!(prefs.ui_version, 0);
        prefs.migrate();
        assert_eq!(prefs.preset, "classic-pro");
        assert_eq!(prefs.font_size, 12.);
        let mut custom: UiPrefs =
            serde_json::from_str(r#"{"preset":"focus","panel_width":300,"font_size":14}"#).unwrap();
        custom.migrate();
        assert_eq!(custom.preset, "focus");
        assert_eq!(custom.panel_width, 300.);
        let imported = UiPrefs::from_pdui(br#"{"version":1,"preset":"focus"}"#).unwrap();
        assert_eq!(imported.preset, "focus");
        assert_eq!(
            UiPrefs::from_pdui(&serde_json::to_vec(&UiPrefs::default().to_pdui()).unwrap())
                .unwrap()
                .preset,
            "classic-pro"
        );
    }
    #[test]
    fn classic_workspace_bounds_at_small_window_and_scales() {
        for scale in [1., 1.5, 2.] {
            let ctx = egui::Context::default();
            let mut app = PhotoDeck::with_context(
                &ctx,
                LaunchOptions {
                    no_settings: true,
                    ui_scale: Some(scale),
                    ..Default::default()
                },
            );
            for _ in 0..6 {
                let input = egui::RawInput {
                    screen_rect: Some(egui::Rect::from_min_size(
                        Pos2::ZERO,
                        Vec2::new(800., 560.) / scale,
                    )),
                    ..Default::default()
                };
                let _ = ctx.run(input, |ctx| {
                    app.menus(ctx);
                    egui::TopBottomPanel::bottom("status").show(ctx, |ui| {
                        ui.label("Hazır");
                    });
                    app.sidebar(ctx);
                    app.document_tabs(ctx);
                    app.canvas(ctx);
                });
            }
            let bounds = ctx.content_rect();
            for name in [
                "menu",
                "options",
                "tabs",
                "status",
                "pro_tools",
                "pro_inspector",
                "pro_panel_switch",
            ] {
                // egui retains the geometry of panels which are no longer
                // rendered after the startup viewport/zoom settles.
                if (name == "pro_inspector" && bounds.width() < 650.)
                    || (name == "pro_panel_switch" && bounds.width() >= 650.)
                {
                    continue;
                }
                if let Some(panel) =
                    egui::containers::panel::PanelState::load(&ctx, egui::Id::new(name))
                {
                    assert!(
                        bounds.expand(1.).contains_rect(panel.rect),
                        "{scale}× {name}: {:?} outside {:?}",
                        panel.rect,
                        bounds
                    );
                }
            }
            let (available, _, _) = app.tablet.canvas.unwrap();
            assert!(
                available.width() > 120. && available.height() > 60.,
                "{scale}× usable canvas: {available:?}"
            );
            assert!(bounds.contains_rect(available));
        }
    }
    #[test]
    fn failed_background_export_preserves_dialog_inputs_and_document() {
        let ctx = egui::Context::default();
        let mut app = test_app();
        let source = app.doc().state.clone();
        let revision = app.doc().revision();
        app.dialog = Some(Dialog::Export);
        app.width = 12;
        app.height = 9;
        app.quality = 37;
        let dir = tempfile::tempdir().unwrap();
        app.begin_export(dir.path().join("missing").join("output.png"));
        let deadline = Instant::now() + Duration::from_secs(3);
        while app.work.user_busy() && Instant::now() < deadline {
            app.poll_background(&ctx);
            std::thread::sleep(Duration::from_millis(2));
        }
        assert!(!app.work.user_busy());
        assert!(app.error.is_some());
        assert!(app.dialog == Some(Dialog::Export));
        assert_eq!((app.width, app.height, app.quality), (12, 9, 37));
        assert_eq!(app.doc().state, source);
        assert_eq!(app.doc().revision(), revision);
    }
    #[test]
    fn transform_retry_after_failed_commit_rearms_history_transaction() {
        let mut app = test_app();
        app.tabs[0].doc.mark_saved();
        let original = app.doc().state.clone();
        let revision = app.doc().revision();
        app.begin_transform();
        app.dialog = Some(Dialog::Transform);
        let active = app.doc_active();
        app.tabs[0].doc.state.layers[active].geometry = Transform::scale(f64::NAN, 1.);
        app.finish_transform(true);
        assert!(app.error.is_some());
        assert!(app.transforming);
        assert!(app.doc().is_editing());
        assert_eq!(app.doc().state, original);
        assert_eq!(app.doc().revision(), revision);
        assert!(!app.doc().dirty);
        app.tabs[0].doc.state.layers[active].geometry = Transform::scale(2., 2.);
        app.finish_transform(true);
        assert!(!app.transforming);
        assert!(!app.doc().is_editing());
        assert!(app.doc().dirty);
        assert_ne!(app.doc().revision(), revision);
        app.tabs[0].doc.undo();
        assert_eq!(app.doc().state, original);
        assert!(!app.doc().dirty);
    }
    #[test]
    fn locked_layer_cannot_enter_numeric_transform_without_transaction() {
        let mut app = test_app();
        let active = app.doc_active();
        app.tabs[0].doc.state.layers[active].locked = true;
        app.tabs[0].doc.mark_saved();
        let original = app.doc().state.clone();
        let revision = app.doc().revision();
        app.action("numeric");
        assert!(app.error.is_some());
        assert!(app.dialog.is_none());
        assert!(!app.transforming);
        assert!(!app.doc().is_editing());
        assert_eq!(app.doc().state, original);
        assert_eq!(app.doc().revision(), revision);
        assert!(!app.doc().dirty);
    }
    #[test]
    fn effects_retry_after_failed_commit_keeps_draft_and_records_undo() {
        let mut app = test_app();
        app.tabs[0].doc.mark_saved();
        let original = app.doc().state.clone();
        let revision = app.doc().revision();
        app.action("effects");
        app.effects_draft.as_mut().unwrap().blur = 21;
        app.apply_effects();
        assert!(app.error.is_some());
        assert!(app.dialog == Some(Dialog::Effects));
        assert_eq!(app.effects_draft.as_ref().unwrap().blur, 21);
        assert!(app.doc().is_editing());
        assert_eq!(app.doc().state, original);
        assert_eq!(app.doc().revision(), revision);
        assert!(!app.doc().dirty);
        app.effects_draft.as_mut().unwrap().blur = 20;
        app.apply_effects();
        assert!(app.dialog.is_none());
        assert!(!app.doc().is_editing());
        assert!(app.doc().dirty);
        assert_eq!(app.doc().active_layer().blur, 20);
        assert_ne!(app.doc().revision(), revision);
        app.tabs[0].doc.undo();
        assert_eq!(app.doc().state, original);
        assert!(!app.doc().dirty);
    }
    fn test_app() -> PhotoDeck {
        let mut app = PhotoDeck::with_context(
            &egui::Context::default(),
            LaunchOptions {
                no_settings: true,
                ..Default::default()
            },
        );
        app.tabs.clear();
        app.push(Document::new(Size::new(8, 8), false).unwrap());
        app
    }
    #[test]
    fn pending_close_blocks_shortcuts_and_keeps_stable_target() {
        let mut app = test_app();
        app.push(Document::new(Size::new(8, 8), false).unwrap());
        app.tabs[1].doc.add_layer("unsaved B").unwrap();
        let target = app.tabs[1].id;
        app.active = 0;
        app.request_close(1);
        let ctx = egui::Context::default();
        let mut input = egui::RawInput::default();
        input.events.push(egui::Event::Key {
            key: egui::Key::W,
            physical_key: None,
            pressed: true,
            repeat: false,
            modifiers: egui::Modifiers::COMMAND,
        });
        let _ = ctx.run(input, |ctx| {
            app.shortcuts(ctx);
            app.guards(ctx);
        });
        assert_eq!(app.tabs.len(), 2);
        assert_eq!(app.close_tab, Some(target));
        assert!(app.tabs[1].doc.dirty);
        // Even if a future caller removes an earlier tab, the target stays B.
        app.remove_tab(0);
        assert_eq!(app.tab_index(target), Some(0));
        let _ = ctx.run(Default::default(), |ctx| app.guards(ctx));
        assert_eq!(app.tabs[0].doc.state.layers[1].name, "unsaved B");
    }
    #[test]
    fn conflict_resolution_saves_original_document_even_if_active_changes() {
        let mut app = test_app();
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("original.pdeck");
        let target = app.tabs[0].id;
        assert!(app.save_to_document(target, path.clone(), false, false));
        app.tabs[0].doc.add_layer("A edit").unwrap();
        let expected = app.tabs[0].doc.state.clone();
        std::fs::write(&path, b"external change").unwrap();
        assert!(!app.save_to_document(target, path.clone(), false, false));
        assert_eq!(app.save_conflict.as_ref().unwrap().0, target);
        app.push(Document::new(Size::new(4, 4), false).unwrap());
        app.tabs[1].doc.add_layer("B edit").unwrap();
        let b = app.tabs[1].doc.state.clone();
        let (id, destination, upgrade) = app.save_conflict.take().unwrap();
        assert!(!upgrade);
        assert!(app.save_to_document(id, destination, true, false));
        assert_eq!(project::open(&path).unwrap().0.state, expected);
        assert!(!app.tabs[0].doc.dirty);
        assert_eq!(app.tabs[1].doc.state, b);
        assert!(app.tabs[1].doc.dirty);
        assert!(app.tabs[1].file.is_none());
    }
    #[test]
    fn dialogs_and_pending_guards_block_dropped_documents_and_actions() {
        let mut app = test_app();
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("dropped.png");
        RgbaImage::from_pixel(4, 4, Rgba([1, 2, 3, 255]))
            .save(&path)
            .unwrap();
        let original = app.doc().state.clone();
        let id = app.tabs[0].id;
        for kind in [Dialog::Text, Dialog::Resize, Dialog::Canvas, Dialog::Export] {
            app.dialog = Some(kind);
            app.open(&path, false);
            app.open(&path, true);
            app.action("layer");
            app.request_close(0);
            let ctx = egui::Context::default();
            let frame = ctx.run(Default::default(), |ctx| app.request_app_close(ctx));
            assert!(
                frame
                    .viewport_output
                    .values()
                    .any(|v| v.commands.contains(&egui::ViewportCommand::CancelClose))
            );
            assert!(!app.closing);
            assert_eq!(app.tabs.len(), 1);
            assert_eq!(app.tabs[0].id, id);
            assert_eq!(app.doc().state, original);
            assert!(!app.doc().dirty);
            assert!(app.dialog == Some(kind));
        }
        app.dialog = None;
        app.save_conflict = Some((id, dir.path().join("conflict.pdeck"), false));
        app.open(&path, false);
        app.action("close");
        assert_eq!(app.tabs.len(), 1);
        assert_eq!(app.doc().state, original);
        app.save_conflict = None;
        app.open(&path, false);
        assert_eq!(app.tabs.len(), 2);
        assert_eq!(app.doc().state.size, Size::new(4, 4));
    }
    #[cfg(unix)]
    #[test]
    fn external_launcher_is_reaped_and_spawn_errors_are_returned() {
        let dir = tempfile::tempdir().unwrap();
        let pid_path = dir.path().join("launcher.pid");
        let mut command = std::process::Command::new("/bin/sh");
        command
            .arg("-c")
            .arg("echo $$ > \"$1\"; exit 0")
            .arg("launcher")
            .arg(&pid_path);
        let status = spawn_reaped(&mut command).unwrap().join().unwrap().unwrap();
        assert!(status.success());
        let pid = std::fs::read_to_string(&pid_path).unwrap();
        #[cfg(target_os = "linux")]
        assert!(!Path::new(&format!("/proc/{}", pid.trim())).exists());
        #[cfg(not(target_os = "linux"))]
        let _ = pid;
        assert!(
            spawn_reaped(&mut std::process::Command::new(
                dir.path().join("missing-launcher")
            ))
            .is_err()
        );
    }
    #[test]
    fn bundled_demo_keeps_four_original_layers() {
        let doc = demo_document();
        assert_eq!(doc.state.layers.len(), 4);
        assert_eq!(doc.state.size, Size::new(1400, 1000));
        assert!(!doc.dirty);
    }

    #[test]
    fn aspect_lock_bounds_both_dimensions_together() {
        assert_eq!(
            locked_dimensions(Size::new(1000, 100), 1, 100, true),
            Size::new(10, 1)
        );
        assert_eq!(
            locked_dimensions(Size::new(100, 1000), 100, 1, false),
            Size::new(1, 10)
        );
        assert_eq!(
            locked_dimensions(Size::new(100, 1000), 16384, 1000, true),
            Size::new(1638, 16384)
        );
        assert_eq!(
            locked_dimensions(Size::new(1000, 100), 1000, 16384, false),
            Size::new(16384, 1638)
        );
    }

    use super::*;
    #[test]
    fn closing_pending_stroke_marks_document_dirty() {
        let mut d = Document::new(Size::new(8, 8), false).unwrap();
        d.mark_saved();
        d.begin_edit().unwrap();
        Arc::make_mut(&mut d.state.layers[0].pixels).put_pixel(2, 2, Rgba([255, 0, 0, 255]));
        assert!(!d.dirty);
        commit_before_navigation(&mut d).unwrap();
        assert!(d.dirty);
        assert!(!d.is_editing());
    }

    #[test]
    fn drag_rect_normalizes_negative_direction() {
        assert_eq!(
            drag_rect((20.5, 40.2), (-3.2, 8.)),
            Rect::new(-4, 8, 25, 33)
        );
    }
    #[test]
    fn drag_rect_click_is_not_empty() {
        assert_eq!(drag_rect((4., 5.), (4., 5.)), Rect::new(4, 5, 1, 1));
    }
    #[test]
    fn transform_opposite_anchor_and_alt_center() {
        let b = Rect::new(20, 30, 100, 80);
        let t = handle_transform(b, (120., 110.), (170., 150.), 4, false, false);
        assert_eq!(t.map(20., 30.), (20., 30.));
        assert_eq!(t.map(120., 110.), (170., 150.));
        let c = handle_transform(b, (120., 110.), (170., 150.), 4, true, false);
        assert_eq!(c.map(70., 70.), (70., 70.));
    }
    #[test]
    fn legacy_ui_roundtrip_preserves_unknown_dock_bytes() {
        let data=br##"{"version":1,"preset":"workbench","accent":"#123456","fontSize":12,"dockState":"AQID","panelSide":"left"}"##;
        let p = UiPrefs::from_pdui(data).unwrap();
        assert_eq!(p.accent, [18, 52, 86]);
        assert_eq!(p.to_pdui()["dockState"], "AQID");
        assert_eq!(p.to_pdui()["panelSide"], "left");
        assert!(UiPrefs::from_pdui(br#"{"version":1,"preset":"focus","fontSize":400}"#).is_err());
    }
}
