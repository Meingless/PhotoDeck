//! Native Klasik Pro workspace. Document edits still go through the same guards
//! and transactions as the legacy workspaces; this module only arranges controls.
use super::*;

#[derive(Default, Clone, Copy, PartialEq, Eq)]
enum PanelTab {
    #[default]
    Color,
    Properties,
    History,
}

pub(super) struct ProUi {
    pub(super) logo: egui::TextureHandle,
    panel: PanelTab,
    floating: bool,
    thumbnail_key: Option<(u64, u64, usize)>,
    thumbnails: Vec<(egui::TextureHandle, Option<egui::TextureHandle>)>,
}
impl ProUi {
    pub(super) fn new(ctx: &egui::Context) -> Self {
        let logo = image::load_from_memory(include_bytes!("../../packaging/photodeck.png"))
            .expect("Bundled PhotoDeck logo")
            .to_rgba8();
        Self {
            logo: texture(ctx, "photodeck-brand", &logo),
            panel: PanelTab::Color,
            floating: false,
            thumbnail_key: None,
            thumbnails: vec![],
        }
    }
}

fn texture(ctx: &egui::Context, name: &str, pixels: &RgbaImage) -> egui::TextureHandle {
    ctx.load_texture(
        name,
        egui::ColorImage::from_rgba_unmultiplied(
            [pixels.width() as usize, pixels.height() as usize],
            pixels.as_raw(),
        ),
        egui::TextureOptions::LINEAR,
    )
}

impl PhotoDeck {
    pub(super) fn pro_sidebar(&mut self, ctx: &egui::Context) {
        if self.prefs.tools {
            egui::SidePanel::left("pro_tools")
                .resizable(false)
                .exact_width(70.)
                .frame(
                    egui::Frame::new()
                        .fill(ctx.style().visuals.panel_fill)
                        .inner_margin(4.),
                )
                .show(ctx, |ui| {
                    ui.add_enabled_ui(!self.modal_active() && !self.transforming, |ui| {
                        egui::ScrollArea::vertical()
                            .id_salt("pro_tools_scroll")
                            .show(ui, |ui| {
                                egui::Grid::new("pro_tool_grid")
                                    .spacing([2., 2.])
                                    .show(ui, |ui| {
                                        for (index, (tool, _, label)) in TOOLS.iter().enumerate() {
                                            let response =
                                                tool_button(ui, *tool, self.tool == *tool, label);
                                            if response.clicked() {
                                                if self.gesture.is_some() {
                                                    self.finish_gesture();
                                                }
                                                self.tool = *tool;
                                                self.polygon.clear();
                                            }
                                            if index % 2 == 1 {
                                                ui.end_row();
                                            }
                                        }
                                    });
                                ui.add_space(10.);
                                ui.horizontal(|ui| {
                                    ui.spacing_mut().interact_size.x = 24.;
                                    ui.color_edit_button_srgba_unmultiplied(&mut self.foreground)
                                        .on_hover_text("Ön plan rengi · X: renkleri değiştir");
                                    ui.color_edit_button_srgba_unmultiplied(&mut self.background)
                                        .on_hover_text("Arka plan rengi");
                                });
                                if ui
                                    .small_button("⇄")
                                    .on_hover_text("Ön / arka renkleri değiştir · X")
                                    .clicked()
                                {
                                    std::mem::swap(&mut self.foreground, &mut self.background);
                                }
                            });
                    });
                });
        }
        if !self.prefs.layers {
            return;
        }
        self.refresh_thumbnails(ctx);
        // Docks stay usable at 800 px; on a scaled viewport leave at least 180
        // logical pixels for the canvas. egui's scroll areas handle short windows.
        let max_width = (ctx.available_rect().width() - 180.).clamp(180., 440.);
        let min_width = 180_f32.min(max_width);
        if ctx.content_rect().width() < 650. {
            egui::SidePanel::right("pro_panel_switch")
                .exact_width(35.)
                .resizable(false)
                .frame(
                    egui::Frame::new()
                        .fill(ctx.style().visuals.panel_fill)
                        .inner_margin(2.),
                )
                .show(ctx, |ui| {
                    if ui
                        .button("☰")
                        .on_hover_text("Katmanlar, renk, özellikler ve geçmiş")
                        .clicked()
                    {
                        self.pro.floating = !self.pro.floating;
                    }
                });
            if self.pro.floating {
                let mut open = true;
                egui::Window::new("Katman çalışma alanı")
                    .open(&mut open)
                    .default_width(250.)
                    .default_pos([80., 80.])
                    .max_height((ctx.content_rect().height() - 90.).max(100.))
                    .show(ctx, |ui| self.pro_inspector_contents(ui));
                if !open {
                    self.pro.floating = false;
                }
            }
        } else {
            egui::SidePanel::right("pro_inspector")
                .default_width(self.prefs.panel_width.min(max_width))
                .width_range(min_width..=max_width)
                .resizable(true)
                .frame(
                    egui::Frame::new()
                        .fill(ctx.style().visuals.panel_fill)
                        .inner_margin(6.),
                )
                .show(ctx, |ui| self.pro_inspector_contents(ui));
        }
    }
    fn pro_inspector_contents(&mut self, ui: &mut egui::Ui) {
        let panel_height = ui
            .available_height()
            .min(ui.ctx().content_rect().height() - 80.)
            .max(120.);
        egui::ScrollArea::vertical()
            .id_salt("pro_whole_dock")
            .max_height(panel_height)
            .show(ui, |ui| {
                ui.add_enabled_ui(!self.modal_active() && !self.transforming, |ui| {
                    ui.horizontal_wrapped(|ui| {
                        ui.selectable_value(&mut self.pro.panel, PanelTab::Color, "Renk");
                        ui.selectable_value(
                            &mut self.pro.panel,
                            PanelTab::Properties,
                            "Özellikler",
                        );
                        ui.selectable_value(&mut self.pro.panel, PanelTab::History, "Geçmiş");
                    });
                    ui.separator();
                    let top_height = (panel_height * 0.27).clamp(75., 175.);
                    egui::ScrollArea::vertical()
                        .id_salt("pro_upper_panel")
                        .max_height(top_height)
                        .show(ui, |ui| match self.pro.panel {
                            PanelTab::Color => self.pro_colors(ui),
                            PanelTab::Properties => self.layer_properties_contents(ui),
                            PanelTab::History => {
                                ui.horizontal(|ui| {
                                    if ui.button("Geri al").clicked() {
                                        self.action("undo");
                                    }
                                    if ui.button("Yinele").clicked() {
                                        self.action("redo");
                                    }
                                });
                                let revision = self.doc().revision();
                                for (id, label) in self.doc().history() {
                                    if ui.selectable_label(id == revision, label).clicked() {
                                        self.tabs[self.active].doc.jump_history(id);
                                        self.invalidate();
                                    }
                                }
                            }
                        });
                    ui.separator();
                    ui.horizontal(|ui| {
                        ui.strong("Katmanlar");
                        ui.weak(format!("{}", self.doc().state.layers.len()));
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            if ui
                                .small_button("•••")
                                .on_hover_text("Katman özellikleri")
                                .clicked()
                            {
                                self.pro.panel = PanelTab::Properties;
                            }
                        });
                    });
                    self.pro_layer_controls(ui);
                    ui.separator();
                    let row_height = (panel_height - top_height - 160.).max(60.);
                    egui::ScrollArea::vertical()
                        .id_salt("pro_layer_list")
                        .max_height(row_height)
                        .auto_shrink([false, false])
                        .show(ui, |ui| self.pro_layer_rows(ui));
                    ui.separator();
                    ui.horizontal_wrapped(|ui| {
                        for (label, tip, action) in [
                            ("+", "Yeni piksel katmanı · Ctrl+Shift+N", "layer"),
                            ("Maske", "Seçimden katman maskesi", "mask"),
                            ("Kopya", "Katmanı çoğalt · Ctrl+J", "duplicate"),
                            ("Sil", "Katmanı sil", "delete"),
                        ] {
                            if ui.button(label).on_hover_text(tip).clicked() {
                                self.action(action);
                            }
                        }
                        ui.menu_button("İşlemler", |ui| {
                            for (label, action) in [
                                ("Alta birleştir", "merge"),
                                ("Maske kaldır", "removemask"),
                                ("Akıllı nesne", "smart"),
                                ("Rasterleştir", "rasterize"),
                                ("Renk / ton / filtreler…", "effects"),
                            ] {
                                if ui.button(label).clicked() {
                                    self.action(action);
                                    ui.close();
                                }
                            }
                        });
                    });
                });
            });
    }

    fn refresh_thumbnails(&mut self, ctx: &egui::Context) {
        let tab = &self.tabs[self.active];
        let key = (tab.id, tab.doc.revision(), tab.doc.state.layers.len());
        if self.pro.thumbnail_key == Some(key) {
            return;
        }
        self.pro.thumbnails = tab
            .doc
            .state
            .layers
            .iter()
            .enumerate()
            .map(|(i, layer)| {
                // Source thumbnails deliberately avoid applying expensive full-size
                // effects in the UI. The canvas remains the authoritative composite.
                let pixels = image::imageops::thumbnail(&*layer.pixels, 42, 32);
                let thumbnail = texture(ctx, &format!("layer-{}-{i}", tab.id), &pixels);
                let mask = layer.mask.as_ref().map(|mask| {
                    let small = image::imageops::thumbnail(&**mask, 28, 32);
                    let rgba = RgbaImage::from_fn(small.width(), small.height(), |x, y| {
                        let v = small.get_pixel(x, y)[0];
                        Rgba([v, v, v, 255])
                    });
                    texture(ctx, &format!("mask-{}-{i}", tab.id), &rgba)
                });
                (thumbnail, mask)
            })
            .collect();
        self.pro.thumbnail_key = Some(key);
    }

    fn pro_colors(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.color_edit_button_srgba_unmultiplied(&mut self.foreground)
                .on_hover_text("Ön plan rengi");
            ui.label(format!(
                "#{:02X}{:02X}{:02X}",
                self.foreground[0], self.foreground[1], self.foreground[2]
            ));
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                ui.color_edit_button_srgba_unmultiplied(&mut self.background)
                    .on_hover_text("Arka plan rengi");
            });
        });
        let mut hsva = egui::ecolor::Hsva::from_srgba_unmultiplied(self.foreground);
        let (rect, response) = ui.allocate_exact_size(
            Vec2::new(ui.available_width(), 76.),
            Sense::click_and_drag(),
        );
        let mut mesh = egui::Mesh::default();
        for y in 0..=8 {
            for x in 0..=16 {
                let sx = x as f32 / 16.;
                let sy = y as f32 / 8.;
                let c = Color32::from(egui::ecolor::Hsva::new(hsva.h, sx, 1. - sy, 1.));
                mesh.colored_vertex(rect.min + rect.size() * Vec2::new(sx, sy), c);
            }
        }
        for y in 0..8 {
            for x in 0..16 {
                let a = y * 17 + x;
                mesh.add_triangle(a, a + 1, a + 17);
                mesh.add_triangle(a + 1, a + 18, a + 17);
            }
        }
        ui.painter().add(egui::Shape::mesh(mesh));
        if (response.clicked() || response.dragged())
            && let Some(p) = response.interact_pointer_pos()
        {
            hsva.s = ((p.x - rect.left()) / rect.width()).clamp(0., 1.);
            hsva.v = (1. - (p.y - rect.top()) / rect.height()).clamp(0., 1.);
        }
        ui.painter().circle_stroke(
            rect.min + rect.size() * Vec2::new(hsva.s, 1. - hsva.v),
            4.,
            Stroke::new(1.5_f32, Color32::WHITE),
        );
        ui.add(
            egui::Slider::new(&mut hsva.h, 0. ..=1.)
                .text("Ton")
                .show_value(false),
        );
        self.foreground = hsva.to_srgba_unmultiplied();
        ui.horizontal_wrapped(|ui| {
            for color in [
                [230, 224, 201, 255],
                [128, 153, 137, 255],
                [52, 94, 86, 255],
                [30, 66, 62, 255],
                [255; 4],
                [0, 0, 0, 255],
            ] {
                if ui
                    .add(
                        egui::Button::new(" ")
                            .fill(Color32::from_rgba_unmultiplied(
                                color[0], color[1], color[2], color[3],
                            ))
                            .min_size(Vec2::splat(18.)),
                    )
                    .on_hover_text(format!(
                        "Renk #{:02X}{:02X}{:02X}",
                        color[0], color[1], color[2]
                    ))
                    .clicked()
                {
                    self.foreground = color;
                }
            }
        });
    }

    fn pro_layer_controls(&mut self, ui: &mut egui::Ui) {
        let old = self.doc().active_layer().clone();
        let mut blend = old.blend;
        let mut opacity = old.opacity;
        ui.add_enabled_ui(!old.locked, |ui| {
            egui::ComboBox::from_id_salt("pro_blend")
                .selected_text(BLEND_NAMES[blend as usize])
                .width(ui.available_width().min(200.))
                .show_ui(ui, |ui| {
                    for (i, name) in BLEND_NAMES.iter().enumerate() {
                        ui.selectable_value(&mut blend, i as u8, *name);
                    }
                });
            ui.add(
                egui::Slider::new(&mut opacity, 0. ..=1.)
                    .text("Opaklık")
                    .custom_formatter(|v, _| format!("{:.0}%", v * 100.)),
            );
        });
        if blend != old.blend || opacity != old.opacity {
            self.run(|d| {
                d.transact("Katman görünümü", true, |s| {
                    s.layers[s.active].blend = blend;
                    s.layers[s.active].opacity = opacity;
                    Ok(())
                })
            });
        }
    }

    fn pro_layer_rows(&mut self, ui: &mut egui::Ui) {
        // History/properties above this list may change the layer count during
        // this very frame. Refresh after those actions, before indexing cache.
        self.refresh_thumbnails(ui.ctx());
        let layers = self.doc().state.layers.clone();
        let mut reorder = None;
        for (i, layer) in layers.iter().enumerate().rev() {
            ui.push_id((self.tabs[self.active].id, i), |ui| {
                let active = self.doc_active() == i;
                egui::Frame::new()
                    .fill(if active {
                        ui.visuals().selection.bg_fill
                    } else {
                        Color32::TRANSPARENT
                    })
                    .inner_margin(3.)
                    .show(ui, |ui| {
                        ui.horizontal(|ui| {
                            let mut visible = layer.visible;
                            if ui
                                .checkbox(&mut visible, "")
                                .on_hover_text(format!("{}: görünürlük", layer.name))
                                .changed()
                            {
                                self.run(|d| {
                                    d.transact("Katman görünürlüğü", true, |s| {
                                        s.layers[i].visible = visible;
                                        Ok(())
                                    })
                                });
                            }
                            let (thumb, mask) = &self.pro.thumbnails[i];
                            let thumb_id = thumb.id();
                            let mask_id = mask.as_ref().map(|m| m.id());
                            let suffix = if layer.clipped {
                                " ↳"
                            } else if layer.is_smart_object() {
                                " ◇"
                            } else {
                                ""
                            };
                            let response = ui
                                .add_sized(
                                    [
                                        (ui.available_width()
                                            - if mask_id.is_some() { 34. } else { 0. }
                                            - 22.)
                                            .max(50.),
                                        38.,
                                    ],
                                    egui::Button::image_and_text(
                                        egui::Image::new((thumb_id, Vec2::new(36., 28.))),
                                        format!("{}{suffix}", layer.name),
                                    )
                                    .frame(false)
                                    .wrap()
                                    .sense(Sense::click_and_drag()),
                                )
                                .on_hover_text(format!(
                                    "{}\n{} × {} px{}",
                                    layer.name,
                                    layer.source_size().width,
                                    layer.source_size().height,
                                    if !layer.text.is_empty() {
                                        " · metin (çift tıklayarak düzenle)"
                                    } else {
                                        ""
                                    }
                                ));
                            if response.clicked() {
                                self.prepare_navigation();
                                self.tabs[self.active].doc.state.active = i;
                                self.tabs[self.active].doc.editing_mask = false;
                            }
                            if response.double_clicked() && !layer.text.is_empty() {
                                self.edit_existing_text(i);
                            }
                            if response.drag_started() {
                                response.dnd_set_drag_payload(i);
                            }
                            if let Some(from) = response.dnd_release_payload::<usize>() {
                                reorder = Some((*from, i));
                            }
                            response.context_menu(|ui| {
                                for (label, action) in [
                                    ("Çoğalt", "duplicate"),
                                    ("Seçimden maske", "mask"),
                                    ("Rasterleştir", "rasterize"),
                                    ("Sil", "delete"),
                                ] {
                                    if ui.button(label).clicked() {
                                        self.prepare_navigation();
                                        self.tabs[self.active].doc.state.active = i;
                                        self.action(action);
                                        ui.close();
                                    }
                                }
                                if ui.button("Yukarı taşı").clicked() && i + 1 < layers.len() {
                                    reorder = Some((i, i + 1));
                                    ui.close();
                                }
                                if ui.button("Aşağı taşı").clicked() && i > 0 {
                                    reorder = Some((i, i - 1));
                                    ui.close();
                                }
                            });
                            if let Some(mask) = mask_id
                                && ui
                                    .add(
                                        egui::Button::image(egui::Image::new((
                                            mask,
                                            Vec2::new(22., 28.),
                                        )))
                                        .selected(active && self.doc().editing_mask),
                                    )
                                    .on_hover_text("Katman maskesini düzenle")
                                    .clicked()
                            {
                                self.prepare_navigation();
                                self.tabs[self.active].doc.state.active = i;
                                self.tabs[self.active].doc.editing_mask = true;
                            }
                            let mut locked = layer.locked;
                            if ui
                                .toggle_value(&mut locked, if layer.locked { "▣" } else { "·" })
                                .on_hover_text("Katman kilidini değiştir")
                                .changed()
                            {
                                self.run(|d| {
                                    d.transact("Katman kilidi", true, |s| {
                                        s.layers[i].locked = locked;
                                        Ok(())
                                    })
                                });
                            }
                        });
                    });
            });
        }
        if let Some((a, b)) = reorder {
            self.run(|d| d.reorder_layer(a, b));
        }
    }

    fn edit_existing_text(&mut self, index: usize) {
        self.prepare_navigation();
        self.tabs[self.active].doc.state.active = index;
        let layer = self.doc().active_layer().clone();
        self.text = layer.text;
        let parts = layer.font.split(',').collect::<Vec<_>>();
        self.font = parts.first().unwrap_or(&"Sans Serif").to_string();
        self.font_size = parts.get(1).and_then(|s| s.parse().ok()).unwrap_or(48.);
        self.font_bold = parts
            .get(4)
            .and_then(|s| s.parse::<i32>().ok())
            .is_some_and(|v| v >= 600 || (60..=99).contains(&v));
        self.foreground = layer.color;
        self.edit_text = true;
        self.dialog = Some(Dialog::Text);
    }
}

fn tool_button(ui: &mut egui::Ui, tool: Tool, selected: bool, label: &str) -> egui::Response {
    let response = ui.add_sized([29., 29.], egui::Button::new("").selected(selected));
    response.widget_info(|| {
        egui::WidgetInfo::labeled(egui::WidgetType::Button, ui.is_enabled(), label)
    });
    let r = response.rect.shrink(6.);
    let p = ui.painter();
    let stroke = Stroke::new(1.4_f32, ui.visuals().text_color());
    let pt = |x: f32, y: f32| r.min + r.size() * Vec2::new(x, y);
    let line = |a: (f32, f32), b: (f32, f32)| {
        p.line_segment([pt(a.0, a.1), pt(b.0, b.1)], stroke);
    };
    match tool {
        Tool::Move => {
            line((0.1, 0.1), (0.45, 0.95));
            line((0.1, 0.1), (0.95, 0.45));
            line((0.95, 0.45), (0.55, 0.55));
            line((0.55, 0.55), (0.45, 0.95));
        }
        Tool::Select | Tool::Crop | Tool::Rectangle => {
            p.rect_stroke(r.shrink(2.), 0., stroke, egui::StrokeKind::Inside);
            if tool == Tool::Crop {
                line((0., 0.25), (1., 0.25));
                line((0.25, 0.), (0.25, 1.));
            }
        }
        Tool::EllipseSelect | Tool::Ellipse => {
            p.circle_stroke(r.center(), r.width() * 0.42, stroke);
        }
        Tool::Lasso | Tool::Polygon => {
            let points = vec![
                pt(0.15, 0.2),
                pt(0.75, 0.05),
                pt(0.95, 0.6),
                pt(0.4, 0.9),
                pt(0.05, 0.55),
                pt(0.15, 0.2),
            ];
            p.add(egui::Shape::line(points, stroke));
            line((0.4, 0.9), (0.2, 1.));
        }
        Tool::Wand => {
            line((0.1, 0.9), (0.75, 0.25));
            line((0.75, 0.), (0.75, 0.15));
            line((0.9, 0.25), (1., 0.25));
            line((0.45, 0.1), (0.55, 0.2));
        }
        Tool::Brush | Tool::Pencil | Tool::Picker => {
            line((0.2, 0.8), (0.85, 0.1));
            line((0.3, 0.95), (0.98, 0.2));
            line((0.85, 0.1), (0.98, 0.2));
            line((0.2, 0.8), (0.1, 1.));
            line((0.1, 1.), (0.3, 0.95));
        }
        Tool::Eraser => {
            p.add(egui::Shape::closed_line(
                vec![pt(0.05, 0.65), pt(0.6, 0.1), pt(1., 0.5), pt(0.5, 1.)],
                stroke,
            ));
            line((0.3, 0.4), (0.75, 0.85));
        }
        Tool::Clone => {
            p.rect_stroke(
                egui::Rect::from_min_max(pt(0.1, 0.65), pt(0.9, 0.95)),
                1.,
                stroke,
                egui::StrokeKind::Inside,
            );
            p.circle_stroke(pt(0.5, 0.25), r.width() * 0.2, stroke);
            line((0.5, 0.45), (0.5, 0.65));
        }
        Tool::Fill => {
            p.add(egui::Shape::closed_line(
                vec![pt(0.1, 0.45), pt(0.45, 0.1), pt(0.9, 0.55), pt(0.55, 0.9)],
                stroke,
            ));
            line((0.1, 0.45), (0.9, 0.55));
            p.circle_filled(pt(0.95, 0.95), 2., stroke.color);
        }
        Tool::Gradient => {
            for x in 0..5 {
                let c = stroke.color.gamma_multiply((x + 1) as f32 / 5.);
                p.rect_filled(
                    egui::Rect::from_min_max(
                        pt(x as f32 / 5., 0.15),
                        pt((x + 1) as f32 / 5., 0.85),
                    ),
                    0.,
                    c,
                );
            }
        }
        Tool::Text => {
            line((0.05, 0.1), (0.95, 0.1));
            line((0.5, 0.1), (0.5, 0.9));
            line((0.3, 0.9), (0.7, 0.9));
        }
        Tool::Hand => {
            p.add(egui::Shape::line(
                vec![
                    pt(0.1, 0.5),
                    pt(0.25, 0.6),
                    pt(0.25, 0.1),
                    pt(0.4, 0.05),
                    pt(0.45, 0.55),
                    pt(0.6, 0.1),
                    pt(0.75, 0.2),
                    pt(0.85, 0.65),
                    pt(0.65, 0.95),
                    pt(0.3, 0.95),
                    pt(0.1, 0.5),
                ],
                stroke,
            ));
        }
    }
    response.on_hover_text(label)
}

pub(super) fn paint_rulers(
    ui: &egui::Ui,
    outer: egui::Rect,
    canvas: egui::Rect,
    zoom: f32,
    gutter: f32,
) {
    let painter = ui.painter().with_clip_rect(outer);
    let fill = ui.visuals().panel_fill;
    let color = ui.visuals().weak_text_color();
    painter.rect_filled(
        egui::Rect::from_min_max(outer.min, Pos2::new(outer.right(), outer.top() + gutter)),
        0.,
        fill,
    );
    painter.rect_filled(
        egui::Rect::from_min_max(outer.min, Pos2::new(outer.left() + gutter, outer.bottom())),
        0.,
        fill,
    );
    let step = [
        1., 5., 10., 25., 50., 100., 250., 500., 1000., 2500., 5000., 10000.,
    ]
    .into_iter()
    .find(|s| s * zoom >= 65.)
    .unwrap_or(10000.);
    for vertical in [false, true] {
        let start = if vertical {
            outer.top() + gutter
        } else {
            outer.left() + gutter
        };
        let end = if vertical {
            outer.bottom()
        } else {
            outer.right()
        };
        let origin = if vertical {
            canvas.top()
        } else {
            canvas.left()
        };
        let first = ((start - origin) / (step * zoom)).floor() as i32;
        let last = ((end - origin) / (step * zoom)).ceil() as i32;
        for i in first..=last {
            let at = origin + i as f32 * step * zoom;
            if at < start || at > end {
                continue;
            }
            let a = if vertical {
                Pos2::new(outer.left() + gutter - 5., at)
            } else {
                Pos2::new(at, outer.top() + gutter - 5.)
            };
            let b = if vertical {
                a + Vec2::new(5., 0.)
            } else {
                a + Vec2::new(0., 5.)
            };
            painter.line_segment([a, b], Stroke::new(1.0_f32, color));
            let text_pos = if vertical {
                Pos2::new(outer.left() + 2., at + 2.)
            } else {
                Pos2::new(at + 3., outer.top() + 2.)
            };
            painter.text(
                text_pos,
                egui::Align2::LEFT_TOP,
                format!("{}", (i as f32 * step) as i32),
                egui::FontId::proportional(9.),
                color,
            );
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn layer_cache_handles_same_frame_history_layer_count_changes() {
        let ctx = egui::Context::default();
        let mut app = PhotoDeck::with_context(
            &ctx,
            LaunchOptions {
                no_settings: true,
                ..Default::default()
            },
        );
        app.tabs.clear();
        app.push(Document::new(Size::new(8, 8), false).unwrap());
        let _ = ctx.run(egui::RawInput::default(), |ctx| {
            egui::CentralPanel::default().show(ctx, |ui| {
                app.refresh_thumbnails(ctx);
                assert_eq!(app.pro.thumbnails.len(), 1);
                app.tabs[0].doc.add_layer("second").unwrap();
                app.pro_layer_rows(ui);
                assert_eq!(app.pro.thumbnails.len(), 2);
                app.action("undo");
                app.pro_layer_rows(ui);
                assert_eq!(app.pro.thumbnails.len(), 1);
                app.action("redo");
                app.pro_layer_rows(ui);
                assert_eq!(app.pro.thumbnails.len(), 2);
            });
        });
    }
}
