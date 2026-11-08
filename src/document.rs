//! Transactional document model. Rejected edits never alter pixels, selection or redo.
use crate::{render, text, types::*};
use image::{GrayImage, Luma, Rgba, RgbaImage};
use std::{
    collections::{HashMap, HashSet},
    sync::Arc,
};
#[derive(Clone, Copy, Debug)]
pub enum Alignment {
    Left,
    Center,
    Right,
    Top,
    Middle,
    Bottom,
}
#[derive(Clone, Copy, Debug)]
pub enum AlignmentTarget {
    Canvas,
    Selection,
}
#[derive(Clone)]
struct Entry {
    state: State,
    label: String,
    id: u64,
    content_id: u64,
}
pub struct Document {
    pub state: State,
    pub title: String,
    pub dirty: bool,
    pub editing_mask: bool,
    history: Vec<Entry>,
    cursor: usize,
    next_id: u64,
    saved_id: u64,
    before: Option<State>,
}
pub fn validate_state(s: &State) -> Result<()> {
    if !s.size.valid()
        || s.layers.is_empty()
        || s.layers.len() > 64
        || s.active >= s.layers.len()
        || s.layers[0].clipped
    {
        return Err("Belge boyutu veya katman sayısı geçersiz.".into());
    }
    let mut bytes = 0u64;
    let mut rendered = 0u64;
    let mut ids = HashSet::new();
    for l in &s.layers {
        if !Size::new(l.pixels.width(), l.pixels.height()).valid()
            || !l.opacity.is_finite()
            || !(0. ..=1.).contains(&l.opacity)
            || l.blend >= 27
            || [l.brightness, l.contrast, l.saturation]
                .iter()
                .any(|n| !(-100..=100).contains(n))
            || !(0..=20).contains(&l.blur)
            || !l.tones.valid()
            || !l.filters.valid()
            || !l.mask_settings.valid()
            || [&l.name, &l.text, &l.font]
                .iter()
                .any(|s| s.encode_utf16().count() > 16384)
            || l.offset.x.unsigned_abs() > MAX_COORDINATE as u32
            || l.offset.y.unsigned_abs() > MAX_COORDINATE as u32
        {
            return Err("Katman parametreleri güvenli sınırı aşıyor.".into());
        }
        if l.is_smart_object() {
            if !l.smart_object_size.valid() || !l.text.is_empty() || !ids.insert(l.smart_object_id)
            {
                return Err("Akıllı nesne kimliği/çerçevesi geçersiz.".into());
            }
        } else if l.smart_object_size != Size::default() {
            return Err("Raster katmanda akıllı çerçeve olamaz.".into());
        }
        bytes += l.pixels.as_raw().len() as u64;
        if let Some(m) = &l.mask {
            if m.dimensions() != (l.source_size().width, l.source_size().height) {
                return Err("Maske boyutu katmanla eşleşmiyor.".into());
            }
            bytes += m.as_raw().len() as u64 * 4;
        }
        let b = l.bounds().ok_or("Dönüşüm geçersiz veya çok büyük.")?;
        rendered += b.width as u64 * b.height as u64 * 4;
        if bytes > MAX_LAYER_BYTES || rendered > MAX_LAYER_BYTES {
            return Err("256 MiB katman bütçesi aşıldı.".into());
        }
    }
    if s.saved_selections.len() > 16 {
        return Err("En fazla 16 adlandırılmış seçim.".into());
    }
    let mut names = HashSet::new();
    for saved in &s.saved_selections {
        if saved.name.is_empty()
            || saved.name.trim() != saved.name
            || saved.name.encode_utf16().count() > 128
            || saved.name.chars().any(|c| c.is_control())
            || !names.insert(&saved.name)
            || matches!(saved.selection, Selection::None)
        {
            return Err("Kayıtlı seçim adı geçersiz.".into());
        }
    }
    let mut selection_bytes = 0u64;
    for sel in [&s.selection, &s.previous_selection]
        .into_iter()
        .chain(s.saved_selections.iter().map(|s| &s.selection))
    {
        match sel {
            Selection::None => {}
            Selection::Rectangle(r) => {
                if r.is_empty()
                    || r.x < 0
                    || r.y < 0
                    || r.x as u64 + r.width as u64 > s.size.width as u64
                    || r.y as u64 + r.height as u64 > s.size.height as u64
                {
                    return Err("Seçim tuval dışında.".into());
                }
            }
            Selection::Mask(m) => {
                if m.dimensions() != (s.size.width, s.size.height) {
                    return Err("Seçim maskesi boyutu geçersiz.".into());
                }
                selection_bytes += m.as_raw().len() as u64;
            }
        }
    }
    if selection_bytes > MAX_SELECTION_BYTES {
        return Err("64 MiB seçim bütçesi aşıldı.".into());
    }
    Ok(())
}
fn content_equal(a: &State, b: &State) -> bool {
    a.size == b.size && a.layers == b.layers && a.saved_selections == b.saved_selections
}
fn active_editable(s: &State) -> Result<()> {
    if s.layers[s.active].locked {
        Err("Katman kilitli.".into())
    } else {
        Ok(())
    }
}
fn no_smart(s: &State) -> Result<()> {
    if s.layers.iter().any(Layer::is_smart_object) {
        Err("Akıllı kaynakları korumak için önce açıkça rasterleştirin.".into())
    } else {
        Ok(())
    }
}
fn selection_weight(s: &Selection, p: Point) -> u8 {
    match s {
        Selection::None => 255,
        Selection::Rectangle(r) => {
            if r.contains(p.x, p.y) {
                255
            } else {
                0
            }
        }
        Selection::Mask(m) => {
            if p.x >= 0 && p.y >= 0 && (p.x as u32) < m.width() && (p.y as u32) < m.height() {
                m.get_pixel(p.x as u32, p.y as u32)[0]
            } else {
                0
            }
        }
    }
}
impl Document {
    pub fn new(size: Size, white: bool) -> Result<Self> {
        if !size.valid() {
            return Err("Tuval boyutu geçersiz.".into());
        }
        Self::from_state(State {
            size,
            layers: vec![Layer::new(
                "Arka plan",
                RgbaImage::from_pixel(
                    size.width,
                    size.height,
                    Rgba(if white { [255; 4] } else { [0; 4] }),
                ),
            )],
            active: 0,
            selection: Selection::None,
            previous_selection: Selection::None,
            saved_selections: vec![],
        })
    }
    pub fn from_state(state: State) -> Result<Self> {
        validate_state(&state)?;
        Ok(Self {
            history: vec![Entry {
                state: state.clone(),
                label: "Başlangıç".into(),
                id: 1,
                content_id: 1,
            }],
            state,
            title: "Adsız".into(),
            dirty: false,
            editing_mask: false,
            cursor: 0,
            next_id: 2,
            saved_id: 1,
            before: None,
        })
    }
    pub fn active_layer(&self) -> &Layer {
        &self.state.layers[self.state.active]
    }
    pub fn revision(&self) -> u64 {
        self.history[self.cursor].id
    }
    pub fn mark_saved(&mut self) {
        self.saved_id = self.history[self.cursor].content_id;
        self.dirty = false;
    }
    pub fn mark_recovered(&mut self) {
        self.saved_id = 0;
        self.dirty = true;
    }
    pub fn is_editing(&self) -> bool {
        self.before.is_some()
    }
    pub fn begin_edit(&mut self) -> Result<()> {
        if self.before.is_some() {
            return Err("Önce geçerli düzenlemeyi bitirin.".into());
        }
        self.before = Some(self.state.clone());
        Ok(())
    }
    pub fn cancel_edit(&mut self) {
        if let Some(before) = self.before.take() {
            self.state = before;
        }
    }
    pub fn commit(&mut self, label: &str) -> Result<()> {
        let Some(before) = self.before.take() else {
            return Ok(());
        };
        if let Err(e) = validate_state(&self.state) {
            self.state = before;
            return Err(e);
        }
        if before == self.state {
            return Ok(());
        }
        let content_changed = !content_equal(&before, &self.state);
        self.push(label, content_changed);
        Ok(())
    }
    fn push(&mut self, label: &str, content_changed: bool) {
        if self.state.layers[self.state.active].mask.is_none() {
            self.editing_mask = false;
        }
        let id = self.next_id;
        self.next_id += 1;
        let content_id = if content_changed {
            id
        } else {
            self.history[self.cursor].content_id
        };
        self.history.truncate(self.cursor + 1);
        self.history.push(Entry {
            state: self.state.clone(),
            label: label.into(),
            id,
            content_id,
        });
        self.cursor += 1;
        while self.history.len() > 30
            || self.history.len() > 1 && self.history_bytes() > HISTORY_BUDGET
        {
            self.history.remove(0);
            self.cursor = self.cursor.saturating_sub(1);
        }
        self.dirty = content_id != self.saved_id;
    }
    pub fn transact(
        &mut self,
        label: &str,
        _content_change: bool,
        edit: impl FnOnce(&mut State) -> Result<()>,
    ) -> Result<()> {
        if self.is_editing() {
            return Err("Önce geçerli düzenlemeyi bitirin.".into());
        }
        let mut pending = self.state.clone();
        edit(&mut pending)?;
        validate_state(&pending)?;
        if pending == self.state {
            return Ok(());
        }
        let content_changed = !content_equal(&pending, &self.state);
        self.state = pending;
        self.push(label, content_changed);
        Ok(())
    }
    pub fn can_undo(&self) -> bool {
        self.cursor > 0
    }
    pub fn can_redo(&self) -> bool {
        self.cursor + 1 < self.history.len()
    }
    pub fn undo(&mut self) {
        self.cancel_edit();
        if self.can_undo() {
            self.cursor -= 1;
            self.restore();
        }
    }
    pub fn redo(&mut self) {
        self.cancel_edit();
        if self.can_redo() {
            self.cursor += 1;
            self.restore();
        }
    }
    fn restore(&mut self) {
        self.state = self.history[self.cursor].state.clone();
        self.dirty = self.history[self.cursor].content_id != self.saved_id;
        self.editing_mask = false;
    }
    pub fn history(&self) -> Vec<(u64, String)> {
        self.history
            .iter()
            .map(|e| (e.id, e.label.clone()))
            .collect()
    }
    pub fn jump_history(&mut self, id: u64) -> bool {
        if let Some(i) = self.history.iter().position(|e| e.id == id)
            && i != self.cursor
        {
            self.cancel_edit();
            self.cursor = i;
            self.restore();
            return true;
        }
        false
    }
    pub fn history_bytes(&self) -> u64 {
        let mut live = HashSet::new();
        let mut unique = HashMap::new();
        let mut collect = |state: &State, current: bool| {
            for l in &state.layers {
                let p = Arc::as_ptr(&l.pixels) as usize;
                if current {
                    live.insert(p);
                } else {
                    unique.insert(p, l.pixels.as_raw().len() as u64);
                }
                if let Some(m) = &l.mask {
                    let p = Arc::as_ptr(m) as usize;
                    if current {
                        live.insert(p);
                    } else {
                        unique.insert(p, m.as_raw().len() as u64);
                    }
                }
            }
            for sel in [&state.selection, &state.previous_selection]
                .into_iter()
                .chain(state.saved_selections.iter().map(|s| &s.selection))
            {
                if let Selection::Mask(m) = sel {
                    let p = Arc::as_ptr(m) as usize;
                    if current {
                        live.insert(p);
                    } else {
                        unique.insert(p, m.as_raw().len() as u64);
                    }
                }
            }
        };
        collect(&self.state, true);
        for e in &self.history {
            collect(&e.state, false);
        }
        unique
            .iter()
            .filter(|(p, _)| !live.contains(p))
            .map(|(_, n)| *n)
            .sum()
    }
    pub fn add_layer(&mut self, name: &str) -> Result<()> {
        let size = self.state.size;
        self.insert_layer(Layer::new(name, RgbaImage::new(size.width, size.height)))
    }
    pub fn insert_layer(&mut self, layer: Layer) -> Result<()> {
        self.transact("Katman eklendi", true, |s| {
            let mut at = s.active + 1;
            while at < s.layers.len() && s.layers[at].clipped {
                at += 1;
            }
            s.layers.insert(at, layer);
            s.active = at;
            Ok(())
        })
    }
    pub fn duplicate(&mut self) -> Result<()> {
        self.transact("Katman çoğaltıldı", true, |s| {
            let mut l = s.layers[s.active].clone();
            l.name = format!("{} kopya", l.name);
            if l.is_smart_object() {
                l.smart_object_id = *uuid::Uuid::new_v4().as_bytes();
            }
            s.active += 1;
            s.layers.insert(s.active, l);
            Ok(())
        })
    }
    pub fn delete_layer(&mut self) -> Result<()> {
        self.transact("Katman silindi", true, |s| {
            active_editable(s)?;
            if s.layers.len() == 1 {
                return Err("Son katman silinemez.".into());
            }
            let i = s.active;
            let base = !s.layers[i].clipped;
            s.layers.remove(i);
            if base {
                for l in s.layers.iter_mut().skip(i) {
                    if !l.clipped {
                        break;
                    }
                    l.clipped = false;
                }
            }
            s.layers[0].clipped = false;
            s.active = i.min(s.layers.len() - 1);
            Ok(())
        })
    }
    pub fn reorder_layer(&mut self, from: usize, to: usize) -> Result<()> {
        self.transact("Katman sırası", true, |s| {
            if from >= s.layers.len() || to >= s.layers.len() {
                return Err("Katman sırası geçersiz.".into());
            }
            if from == to {
                return Ok(());
            }
            let layer = s.layers.remove(from);
            s.layers.insert(to, layer);
            s.active = if s.active == from {
                to
            } else if from < s.active && s.active <= to {
                s.active - 1
            } else if to <= s.active && s.active < from {
                s.active + 1
            } else {
                s.active
            };
            s.layers[0].clipped = false;
            Ok(())
        })
    }
    pub fn set_clipping(&mut self, clipped: bool) -> Result<()> {
        self.transact("Kırpma maskesi", true, |s| {
            active_editable(s)?;
            if clipped && s.active == 0 {
                return Err("Alt katman olmadan kırpılamaz.".into());
            }
            s.layers[s.active].clipped = clipped;
            Ok(())
        })
    }
    pub fn add_mask(&mut self) -> Result<()> {
        self.transact("Maske eklendi", true, |s| {
            active_editable(s)?;
            let l = &s.layers[s.active];
            let size = l.source_size();
            let mask = GrayImage::from_fn(size.width, size.height, |x, y| {
                let (dx, dy) = l.geometry.map(x as f64 + 0.5, y as f64 + 0.5);
                Luma([selection_weight(
                    &s.selection,
                    Point::new(
                        dx.floor() as i32 + l.offset.x,
                        dy.floor() as i32 + l.offset.y,
                    ),
                )])
            });
            s.layers[s.active].mask = Some(Arc::new(mask));
            Ok(())
        })
    }
    pub fn remove_mask(&mut self) -> Result<()> {
        self.transact("Maske kaldırıldı", true, |s| {
            active_editable(s)?;
            s.layers[s.active].mask = None;
            Ok(())
        })
    }
    pub fn invert_mask(&mut self) -> Result<()> {
        self.transact("Maske ters çevrildi", true, |s| {
            active_editable(s)?;
            let m = s.layers[s.active]
                .mask
                .as_mut()
                .ok_or("Katman maskesi yok.")?;
            for p in Arc::make_mut(m).pixels_mut() {
                p[0] = 255 - p[0];
            }
            Ok(())
        })
    }
    pub fn rasterize_layer(&mut self) -> Result<()> {
        self.transact("Katman rasterleştirildi", true, |s| {
            active_editable(s)?;
            s.layers[s.active] = render::rasterized(&s.layers[s.active]);
            Ok(())
        })
    }
    pub fn convert_to_smart_object(&mut self) -> Result<()> {
        self.transact("Akıllı nesneye dönüştürüldü", true, |s| {
            active_editable(s)?;
            let l = &mut s.layers[s.active];
            if !l.is_smart_object() {
                l.smart_object_size = Size::new(l.pixels.width(), l.pixels.height());
                l.smart_object_id = *uuid::Uuid::new_v4().as_bytes();
                l.text.clear();
            }
            Ok(())
        })
    }
    pub fn replace_smart_object_contents(&mut self, image: RgbaImage) -> Result<()> {
        self.transact("Akıllı nesne içeriği değişti", true, |s| {
            active_editable(s)?;
            let l = &mut s.layers[s.active];
            if !l.is_smart_object() {
                return Err("Etkin katman akıllı nesne değil.".into());
            }
            l.pixels = Arc::new(image);
            Ok(())
        })
    }
    pub fn set_layer_transform(&mut self, transform: Transform) -> Result<()> {
        self.transact("Katman dönüşümü", true, |s| {
            active_editable(s)?;
            s.layers[s.active].geometry = transform;
            Ok(())
        })
    }
    pub fn position_layer(&mut self, p: Point) -> Result<()> {
        self.transact("Katman konumu", true, |s| {
            active_editable(s)?;
            if !s.layers[s.active].visible {
                return Err("Konumlandırmak için katmanı görünür yapın.".into());
            }
            let b = s.layers[s.active]
                .bounds()
                .ok_or("Geçersiz katman geometrisi.")?;
            let l = &mut s.layers[s.active];
            l.offset.x = l
                .offset
                .x
                .checked_add(p.x.checked_sub(b.x).ok_or("Konum taşması.")?)
                .ok_or("Konum taşması.")?;
            l.offset.y = l
                .offset
                .y
                .checked_add(p.y.checked_sub(b.y).ok_or("Konum taşması.")?)
                .ok_or("Konum taşması.")?;
            Ok(())
        })
    }
    pub fn nudge_layer(&mut self, delta: Point) -> Result<()> {
        let b = self.active_layer().bounds().ok_or("Geçersiz geometri.")?;
        self.position_layer(Point::new(
            b.x.checked_add(delta.x).ok_or("Konum taşması.")?,
            b.y.checked_add(delta.y).ok_or("Konum taşması.")?,
        ))
    }
    pub fn align_layer(&mut self, alignment: Alignment, target: AlignmentTarget) -> Result<()> {
        let b = self.active_layer().bounds().ok_or("Geçersiz geometri.")?;
        let t = match target {
            AlignmentTarget::Canvas => {
                Rect::new(0, 0, self.state.size.width, self.state.size.height)
            }
            AlignmentTarget::Selection => {
                if matches!(self.state.selection, Selection::None)
                    || !self.state.selection.has_pixels()
                {
                    return Err("Etkin dolu seçim gerekli.".into());
                }
                self.state.selection.bounds()
            }
        };
        let mut p = Point::new(b.x, b.y);
        match alignment {
            Alignment::Left => p.x = t.x,
            Alignment::Center => p.x = t.x + (t.width as i32 - b.width as i32).div_euclid(2),
            Alignment::Right => p.x = t.x + t.width as i32 - b.width as i32,
            Alignment::Top => p.y = t.y,
            Alignment::Middle => p.y = t.y + (t.height as i32 - b.height as i32).div_euclid(2),
            Alignment::Bottom => p.y = t.y + t.height as i32 - b.height as i32,
        }
        self.position_layer(p)
    }
    pub fn set_selection(&mut self, selection: Selection) -> Result<()> {
        self.transact("Seçim değişti", false, |s| {
            let selection = selection.clipped(s.size);
            if s.selection == selection {
                return Ok(());
            }
            if !matches!(s.selection, Selection::None) {
                s.previous_selection = s.selection.clone();
            }
            s.selection = selection;
            Ok(())
        })
    }
    pub fn deselect(&mut self) -> Result<()> {
        if matches!(self.state.selection, Selection::None) {
            return Ok(());
        }
        self.set_selection(Selection::None)
    }
    pub fn reselect(&mut self) -> Result<()> {
        if !matches!(self.state.selection, Selection::None)
            || matches!(self.state.previous_selection, Selection::None)
        {
            return Err("Yeniden seçilecek alan yok.".into());
        }
        self.set_selection(self.state.previous_selection.clone())
    }
    pub fn save_selection(&mut self, name: &str) -> Result<()> {
        let name = name.to_owned();
        self.transact("Seçim kaydedildi", true, |s| {
            if matches!(s.selection, Selection::None) {
                return Err("Kaydedilecek seçim yok.".into());
            }
            if let Some(saved) = s.saved_selections.iter_mut().find(|v| v.name == name) {
                saved.selection = s.selection.clone();
            } else {
                s.saved_selections.push(SavedSelection {
                    name,
                    selection: s.selection.clone(),
                });
            }
            Ok(())
        })
    }
    pub fn load_selection(&mut self, name: &str, mode: SelectionMode) -> Result<()> {
        let saved = self
            .state
            .saved_selections
            .iter()
            .find(|s| s.name == name)
            .ok_or("Seçim bulunamadı.")?
            .selection
            .clone();
        let selection = self.state.selection.combined(&saved, mode, self.state.size);
        self.set_selection(selection)
    }
    pub fn remove_saved_selection(&mut self, name: &str) -> Result<()> {
        self.transact("Kayıtlı seçim silindi", true, |s| {
            s.saved_selections.retain(|v| v.name != name);
            Ok(())
        })
    }
    pub fn delete_selection(&mut self) -> Result<()> {
        self.transact("Seçili pikseller silindi", true, |s| {
            active_editable(s)?;
            if matches!(s.selection, Selection::None) {
                return Err("Önce bir alan seçin.".into());
            }
            let l = &mut s.layers[s.active];
            if l.is_smart_object() || !l.geometry.is_identity() {
                return Err("Piksel düzenleme için önce rasterleştirin.".into());
            }
            for (x, y, p) in Arc::make_mut(&mut l.pixels).enumerate_pixels_mut() {
                let w = selection_weight(
                    &s.selection,
                    Point::new(x as i32 + l.offset.x, y as i32 + l.offset.y),
                );
                p[3] = (p[3] as u32 * (255 - w as u32) / 255) as u8;
            }
            l.text.clear();
            Ok(())
        })
    }
    pub fn crop(&mut self, rect: Rect) -> Result<()> {
        self.transact("Görüntü kırpıldı", true, |s| {
            let rect = rect.intersect(Rect::new(0, 0, s.size.width, s.size.height));
            let new = Size::new(rect.width, rect.height);
            if !new.valid() {
                return Err("Kırpma alanı boş.".into());
            }
            let old = s.size;
            map_selections(s, |selection| selection.cropped(rect, old))?;
            for l in &mut s.layers {
                l.offset.x -= rect.x;
                l.offset.y -= rect.y;
            }
            s.size = new;
            Ok(())
        })
    }
    pub fn canvas_size(&mut self, size: Size, anchor: Point) -> Result<()> {
        self.transact("Tuval boyutu", true, |s| {
            if !size.valid() || !(0..=2).contains(&anchor.x) || !(0..=2).contains(&anchor.y) {
                return Err("Tuval boyutu/sabitleme geçersiz.".into());
            }
            let dx = (size.width as i32 - s.size.width as i32) * anchor.x / 2;
            let dy = (size.height as i32 - s.size.height as i32) * anchor.y / 2;
            for l in &mut s.layers {
                l.offset.x += dx;
                l.offset.y += dy;
            }
            map_selections(s, |selection| {
                selection.translated(Point::new(dx, dy), size)
            })?;
            s.size = size;
            Ok(())
        })
    }
    pub fn resize(&mut self, size: Size) -> Result<()> {
        self.transact("Görüntü boyutu", true, |s| {
            no_smart(s)?;
            if !size.valid() {
                return Err("Boyut geçersiz.".into());
            }
            let old = s.size;
            let sx = size.width as f64 / old.width as f64;
            let sy = size.height as f64 / old.height as f64;
            let mut required = 0_u64;
            for layer in &s.layers {
                let source = if layer.geometry.is_identity() {
                    Size::new(layer.pixels.width(), layer.pixels.height())
                } else {
                    let bounds = layer.bounds().ok_or("Geçersiz geometri.")?;
                    Size::new(bounds.width, bounds.height)
                };
                let target = Size::new(
                    (source.width as f64 * sx).round().max(1.) as u32,
                    (source.height as f64 * sy).round().max(1.) as u32,
                );
                if !target.valid() {
                    return Err("Katman boyutu sınırı aşıldı.".into());
                }
                required += target.width as u64
                    * target.height as u64
                    * 4
                    * if layer.geometry.is_identity() && layer.mask.is_some() {
                        2
                    } else {
                        1
                    };
            }
            if required > MAX_LAYER_BYTES {
                return Err("Yeniden boyutlandırma katman bütçesini aşar.".into());
            }
            for l in &mut s.layers {
                if !l.geometry.is_identity() {
                    *l = render::rasterized(l);
                }
                let target = Size::new(
                    (l.pixels.width() as f64 * sx).round().max(1.) as u32,
                    (l.pixels.height() as f64 * sy).round().max(1.) as u32,
                );
                if !target.valid() {
                    return Err("Katman boyutu sınırı aşıldı.".into());
                }
                l.pixels = Arc::new(render::resize_image(&l.pixels, target));
                if let Some(m) = &l.mask {
                    l.mask = Some(Arc::new(image::imageops::resize(
                        &**m,
                        target.width,
                        target.height,
                        image::imageops::FilterType::Triangle,
                    )));
                }
                l.offset = Point::new(
                    (l.offset.x as f64 * sx).round() as i32,
                    (l.offset.y as f64 * sy).round() as i32,
                );
                l.text.clear();
            }
            map_selections(s, |selection| selection.resized(old, size))?;
            s.size = size;
            Ok(())
        })
    }
    pub fn rotate(&mut self, clockwise: bool) -> Result<()> {
        self.transact("Görüntü döndürüldü", true, |s| {
            no_smart(s)?;
            let old = s.size;
            for l in &mut s.layers {
                if !l.geometry.is_identity() {
                    *l = render::rasterized(l);
                }
                let (w, h) = l.pixels.dimensions();
                l.pixels = Arc::new(if clockwise {
                    image::imageops::rotate90(&*l.pixels)
                } else {
                    image::imageops::rotate270(&*l.pixels)
                });
                l.offset = if clockwise {
                    Point::new(old.height as i32 - l.offset.y - h as i32, l.offset.x)
                } else {
                    Point::new(l.offset.y, old.width as i32 - l.offset.x - w as i32)
                };
                if let Some(m) = &l.mask {
                    l.mask = Some(Arc::new(if clockwise {
                        image::imageops::rotate90(&**m)
                    } else {
                        image::imageops::rotate270(&**m)
                    }));
                }
                l.text.clear();
            }
            map_selections(s, |selection| selection.rotated(clockwise, old))?;
            s.size = Size::new(old.height, old.width);
            Ok(())
        })
    }
    pub fn flip(&mut self, horizontal: bool) -> Result<()> {
        self.transact("Görüntü aynalandı", true, |s| {
            no_smart(s)?;
            let old = s.size;
            for l in &mut s.layers {
                if !l.geometry.is_identity() {
                    *l = render::rasterized(l);
                }
                l.pixels = Arc::new(if horizontal {
                    image::imageops::flip_horizontal(&*l.pixels)
                } else {
                    image::imageops::flip_vertical(&*l.pixels)
                });
                if horizontal {
                    l.offset.x = old.width as i32 - l.offset.x - l.pixels.width() as i32;
                } else {
                    l.offset.y = old.height as i32 - l.offset.y - l.pixels.height() as i32;
                }
                if let Some(m) = &l.mask {
                    l.mask = Some(Arc::new(if horizontal {
                        image::imageops::flip_horizontal(&**m)
                    } else {
                        image::imageops::flip_vertical(&**m)
                    }));
                }
                l.text.clear();
            }
            map_selections(s, |selection| selection.flipped(horizontal, old))?;
            Ok(())
        })
    }
    pub fn add_text(&mut self, value: &str, font: &str, color: [u8; 4], at: Point) -> Result<()> {
        let mut layer = Layer::new("Metin", text::render_text(value, font, color)?);
        layer.text = value.into();
        layer.font = font.into();
        layer.color = color;
        layer.offset = at;
        self.insert_layer(layer)
    }
    pub fn edit_text(&mut self, value: &str, font: &str, color: [u8; 4]) -> Result<()> {
        let pixels = text::render_text(value, font, color)?;
        self.transact("Metin düzenlendi", true, |s| {
            active_editable(s)?;
            let l = &mut s.layers[s.active];
            if l.text.is_empty() || l.is_smart_object() {
                return Err("Katman düzenlenebilir metin değil.".into());
            }
            l.pixels = Arc::new(pixels);
            l.text = value.into();
            l.font = font.into();
            l.color = color;
            if let Some(old) = &l.mask {
                let mut mask =
                    GrayImage::from_pixel(l.pixels.width(), l.pixels.height(), Luma([255]));
                image::imageops::overlay(&mut mask, &**old, 0, 0);
                l.mask = Some(Arc::new(mask));
            }
            Ok(())
        })
    }
    pub fn layer_via_selection(&mut self, cut: bool) -> Result<()> {
        if matches!(self.state.selection, Selection::None) {
            if cut {
                return Err("Kesmek için önce bir alan seçin.".into());
            }
            return self.duplicate();
        }
        self.transact(
            if cut {
                "Seçim kesildi"
            } else {
                "Seçim kopyalandı"
            },
            true,
            |s| {
                if !s.selection.has_pixels() {
                    return Err("Seçim boş.".into());
                }
                let source = &s.layers[s.active];
                if cut
                    && (source.locked
                        || source.is_smart_object()
                        || source.clipped
                        || s.layers.get(s.active + 1).is_some_and(|l| l.clipped))
                {
                    return Err("Kilitli/akıllı/kırpılmış katmandan kesilemez.".into());
                }
                let mut raster = render::rasterized(source);
                let bounds = raster.bounds().ok_or("Geçersiz geometri.")?;
                let region = bounds.intersect(s.selection.bounds());
                if region.is_empty() {
                    return Err("Seçim katmanla kesişmiyor.".into());
                }
                let mut pixels = image::imageops::crop_imm(
                    &*raster.pixels,
                    (region.x - bounds.x) as u32,
                    (region.y - bounds.y) as u32,
                    region.width,
                    region.height,
                )
                .to_image();
                let mut any = false;
                for (x, y, p) in pixels.enumerate_pixels_mut() {
                    let w = selection_weight(
                        &s.selection,
                        Point::new(region.x + x as i32, region.y + y as i32),
                    );
                    p[3] = (p[3] as u32 * w as u32 / 255) as u8;
                    any |= p[3] > 0;
                }
                if !any {
                    return Err("Seçimde katman pikseli yok.".into());
                }
                if cut {
                    for (x, y, p) in Arc::make_mut(&mut raster.pixels).enumerate_pixels_mut() {
                        let w = selection_weight(
                            &s.selection,
                            Point::new(bounds.x + x as i32, bounds.y + y as i32),
                        );
                        p[3] = (p[3] as u32 * (255 - w as u32) / 255) as u8;
                    }
                    s.layers[s.active] = raster.clone();
                }
                raster.name = format!("{} seçim", raster.name);
                raster.pixels = Arc::new(pixels);
                raster.offset = Point::new(region.x, region.y);
                let mut at = s.active + 1;
                if !raster.clipped {
                    while at < s.layers.len() && s.layers[at].clipped {
                        at += 1;
                    }
                }
                s.layers.insert(at, raster);
                s.active = at;
                Ok(())
            },
        )
    }
    /// Detached preview, including base coverage when copying from a clipping chain.
    pub fn selection_copy_state(&self) -> Result<State> {
        if matches!(self.state.selection, Selection::None) {
            return Err("Önce bir alan seçin.".into());
        }
        let mut preview = Self::from_state(self.state.clone())?;
        preview.layer_via_selection(false)?;
        if !self.active_layer().clipped {
            return Ok(preview.state);
        }
        let mut copied = preview.state.layers[preview.state.active].clone();
        let mut base_index = self.state.active - 1;
        while base_index > 0 && self.state.layers[base_index].clipped {
            base_index -= 1;
        }
        let base = &self.state.layers[base_index];
        let bounds = base.bounds().ok_or("Geçersiz kırpma temeli.")?;
        let coverage = render::render_layer(base);
        let mut any = false;
        for (x, y, p) in Arc::make_mut(&mut copied.pixels).enumerate_pixels_mut() {
            let dx = copied.offset.x + x as i32 - bounds.x;
            let dy = copied.offset.y + y as i32 - bounds.y;
            let alpha = if base.visible
                && dx >= 0
                && dy >= 0
                && (dx as u32) < coverage.width()
                && (dy as u32) < coverage.height()
            {
                (coverage.get_pixel(dx as u32, dy as u32)[3] as f64 * base.opacity).round() as u32
            } else {
                0
            };
            p[3] = ((p[3] as u32 * alpha + 127) / 255) as u8;
            any |= p[3] > 0;
        }
        if !any {
            return Err("Kırpma temelinin gösterdiği seçili piksel yok.".into());
        }
        copied.clipped = false;
        let mut state = self.state.clone();
        let mut at = state.active + 1;
        while at < state.layers.len() && state.layers[at].clipped {
            at += 1;
        }
        state.layers.insert(at, copied);
        state.active = at;
        validate_state(&state)?;
        Ok(state)
    }
    pub fn flatten(&mut self) -> Result<()> {
        self.transact("Tuvale düzleştirildi", true, |s| {
            no_smart(s)?;
            let image = render::composite(s);
            s.layers = vec![Layer::new("Düzleştirilmiş", image)];
            s.active = 0;
            Ok(())
        })
    }
    pub fn merge_visible(&mut self) -> Result<()> {
        self.transact("Görünür katmanlar birleştirildi", true, |s| {
            let mut include = vec![false; s.layers.len()];
            let mut i = 0;
            while i < s.layers.len() {
                let mut end = i + 1;
                while end < s.layers.len() && s.layers[end].clipped {
                    end += 1;
                }
                if s.layers[i].visible {
                    if s.layers[i + 1..end].iter().any(|l| !l.visible) {
                        return Err(
                            "Gizli kırpma çocuklarını önce ayırın veya görünür yapın.".into()
                        );
                    }
                    if s.layers[i..end].iter().any(Layer::is_smart_object) {
                        return Err("Birleştirilecek akıllı nesneleri önce rasterleştirin.".into());
                    }
                    include[i..end].fill(true);
                }
                i = end;
            }
            let visible: Vec<Layer> = s
                .layers
                .iter()
                .enumerate()
                .filter(|(i, _)| include[*i])
                .map(|(_, l)| l.clone())
                .collect();
            if visible.len() < 2 {
                return Ok(());
            }
            let bounds = union_bounds(&visible)?;
            let mut temp = s.clone();
            temp.layers = visible;
            temp.active = 0;
            let mut merged = Layer::new("Birleştirilmiş", render::composite_region(&temp, bounds));
            merged.offset = Point::new(bounds.x, bounds.y);
            let upper = include.iter().rposition(|v| *v).unwrap();
            let insertion = include.iter().take(upper).filter(|v| !**v).count();
            let mut layers: Vec<_> = s
                .layers
                .iter()
                .enumerate()
                .filter(|(i, _)| !include[*i])
                .map(|(_, l)| l.clone())
                .collect();
            layers.insert(insertion, merged);
            s.layers = layers;
            s.active = insertion;
            Ok(())
        })
    }
    pub fn merge_down(&mut self) -> Result<()> {
        self.transact("Alta birleştirildi", true, |s| {
            active_editable(s)?;
            let i = s.active;
            if i == 0 {
                return Err("Alt katman yok.".into());
            }
            let a = &s.layers[i - 1];
            let b = &s.layers[i];
            if a.is_smart_object()
                || b.is_smart_object()
                || a.locked
                || a.clipped
                || !a.visible
                || !b.visible
            {
                return Err("Bu katman ilişkisi güvenle birleştirilemiyor.".into());
            }
            let bounds = if b.clipped {
                a.bounds().ok_or("Geçersiz geometri.")?
            } else {
                union_bounds(&s.layers[i - 1..=i])?
            };
            let before = render::composite_region(s, bounds);
            let mut merged = if b.clipped {
                let mut pixels = render::render_layer(a);
                let top = b.bounds().ok_or("Geçersiz geometri.")?;
                render::compose(
                    &mut pixels,
                    &render::render_layer(b),
                    Point::new(top.x - bounds.x, top.y - bounds.y),
                    b.opacity,
                    b.blend,
                    Point::new(bounds.x, bounds.y),
                    true,
                );
                let mut layer = Layer::new(&b.name, pixels);
                layer.opacity = a.opacity;
                layer.blend = a.blend;
                layer
            } else {
                let mut temp = s.clone();
                temp.layers = s.layers[i - 1..=i].to_vec();
                temp.active = 0;
                Layer::new("Birleştirilmiş", render::composite_region(&temp, bounds))
            };
            merged.offset = Point::new(bounds.x, bounds.y);
            let mut next = s.clone();
            next.layers.remove(i);
            next.layers[i - 1] = merged;
            next.active = i - 1;
            let after = render::composite_region(&next, bounds);
            if before != after {
                return Err(
                    "Harmanlama görünümü değiştirir; görünür katmanları birleştirin.".into(),
                );
            }
            *s = next;
            Ok(())
        })
    }
}
fn map_selections(s: &mut State, map: impl Fn(&Selection) -> Selection) -> Result<()> {
    let mut bytes = 0_u64;
    let mut apply = |selection: &Selection| -> Result<Selection> {
        let result = map(selection);
        if let Selection::Mask(mask) = &result {
            bytes += mask.as_raw().len() as u64;
            if bytes > MAX_SELECTION_BYTES {
                return Err("Dönüşüm seçim bütçesini aşar.".into());
            }
        }
        Ok(result)
    };
    s.selection = apply(&s.selection)?;
    s.previous_selection = apply(&s.previous_selection)?;
    for saved in &mut s.saved_selections {
        saved.selection = apply(&saved.selection)?;
    }
    Ok(())
}
fn union_bounds(layers: &[Layer]) -> Result<Rect> {
    let mut x = i32::MAX;
    let mut y = i32::MAX;
    let mut r = i64::MIN;
    let mut b = i64::MIN;
    for l in layers {
        let bounds = l.bounds().ok_or("Geçersiz katman geometrisi.")?;
        x = x.min(bounds.x);
        y = y.min(bounds.y);
        r = r.max(bounds.x as i64 + bounds.width as i64);
        b = b.max(bounds.y as i64 + bounds.height as i64);
    }
    let size = Size::new((r - x as i64) as u32, (b - y as i64) as u32);
    if !size.valid() {
        return Err("Birleştirme alanı sınırı aşıyor.".into());
    }
    Ok(Rect::new(x, y, size.width, size.height))
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejected_transaction_preserves_redo() {
        let mut d = Document::new(Size::new(10, 10), false).unwrap();
        d.add_layer("A").unwrap();
        d.undo();
        let rev = d.revision();
        assert!(
            d.transact("bad", true, |s| {
                s.layers.clear();
                Ok(())
            })
            .is_err()
        );
        assert_eq!(d.revision(), rev);
        assert!(d.can_redo());
    }
    #[test]
    fn selection_history_is_not_content() {
        let mut d = Document::new(Size::new(10, 10), false).unwrap();
        d.set_selection(Selection::Rectangle(Rect::new(1, 1, 3, 3)))
            .unwrap();
        assert!(!d.dirty);
        assert!(d.can_undo());
        d.save_selection("A").unwrap();
        assert!(d.dirty);
        d.undo();
        assert!(!d.dirty);
    }
    #[test]
    fn smart_copy_has_independent_identity_and_undo() {
        let mut d = Document::new(Size::new(10, 10), false).unwrap();
        d.convert_to_smart_object().unwrap();
        let source = d.active_layer().smart_object_id;
        d.duplicate().unwrap();
        assert_ne!(source, d.active_layer().smart_object_id);
        let frame = d.active_layer().source_size();
        d.replace_smart_object_contents(RgbaImage::new(20, 20))
            .unwrap();
        assert_eq!(frame, d.active_layer().source_size());
        assert_eq!(d.state.layers[0].pixels.width(), 10);
        assert!(d.resize(Size::new(20, 20)).is_err());
        d.undo();
        assert_eq!(d.active_layer().pixels.width(), 10);
    }
    #[test]
    fn geometry_preserves_source() {
        let mut d = Document::new(Size::new(10, 10), false).unwrap();
        let original = d.active_layer().pixels.clone();
        d.set_layer_transform(Transform::rotate(30.)).unwrap();
        assert!(Arc::ptr_eq(&original, &d.active_layer().pixels));
        d.undo();
        assert!(d.active_layer().geometry.is_identity());
    }
    #[test]
    fn active_zero_selection_is_not_unrestricted() {
        let mut d = Document::new(Size::new(2, 2), true).unwrap();
        d.set_selection(Selection::Mask(Arc::new(GrayImage::new(2, 2))))
            .unwrap();
        d.delete_selection().unwrap();
        assert_eq!(d.active_layer().pixels.get_pixel(0, 0)[3], 255);
    }
}
