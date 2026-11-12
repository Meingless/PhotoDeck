use super::*;
use photodeck::platform::tablet::{TabletEvent, TabletInput, TabletPhase};

pub(super) struct TabletState {
    pub(super) input: Option<TabletInput>,
    pub(super) status: String,
    pub(super) sample: Option<StrokeSample>,
    pub(super) canvas: Option<(egui::Rect, egui::Rect, f32)>,
    down: bool,
    suppress_until: u32,
    previous_tool: Option<Tool>,
}
impl Default for TabletState {
    fn default() -> Self {
        Self {
            input: None,
            status: "Tablet bağlantısı kurulmadı; fare tam basınç kullanır.".into(),
            sample: None,
            canvas: None,
            down: false,
            suppress_until: 0,
            previous_tool: None,
        }
    }
}
impl TabletState {
    pub(super) fn suppress_mouse(&self, frame: u32) -> bool {
        self.down || frame <= self.suppress_until
    }
}
impl PhotoDeck {
    pub(super) fn pump_tablet(&mut self, ctx: &egui::Context) {
        let events = match self.tablet.input.as_mut().map(TabletInput::pump) {
            Some(Ok(events)) => events,
            Some(Err(e)) => {
                self.tablet.status = e;
                self.tablet.input = None;
                self.end_tablet_stroke();
                return;
            }
            None => return,
        };
        for event in events {
            self.handle_tablet_event(ctx, event);
        }
    }
    fn handle_tablet_event(&mut self, ctx: &egui::Context, event: TabletEvent) {
        if matches!(event.phase, TabletPhase::Up | TabletPhase::Leave) {
            if self.tablet.down {
                self.end_tablet_stroke();
            }
            return;
        }
        if self.modal_active() {
            self.end_tablet_stroke();
            return;
        }
        let Some((available, screen, zoom)) = self.tablet.canvas else {
            return;
        };
        let pos = Pos2::new(event.position[0], event.position[1]) / ctx.zoom_factor();
        let point = (
            ((pos.x - screen.left()) / zoom) as f64,
            ((pos.y - screen.top()) / zoom) as f64,
        );
        if event.phase == TabletPhase::Down
            && available.contains(pos)
            && matches!(
                self.tool,
                Tool::Brush | Tool::Pencil | Tool::Eraser | Tool::Clone
            )
        {
            if self.gesture.is_some() {
                self.finish_gesture();
            }
            if event.eraser && self.tool != Tool::Eraser {
                self.tablet.previous_tool = Some(self.tool);
                self.tool = Tool::Eraser;
            }
            self.tablet.down = true;
            self.tablet.sample = Some(event.sample_at(point.0, point.1));
            self.start_gesture(point, -1, ctx);
        } else if event.phase == TabletPhase::Move && self.tablet.down {
            self.tablet.sample = Some(event.sample_at(point.0, point.1));
            self.advance_gesture(point, Vec2::ZERO, ctx);
        }
    }
    fn end_tablet_stroke(&mut self) {
        if self.tablet.down {
            self.finish_gesture();
            self.tablet.down = false;
            // Drop synthesized mouse down/up events arriving in the same/next frame.
            self.tablet.suppress_until = self.frames.saturating_add(2);
        }
        self.tablet.sample = None;
        if let Some(tool) = self.tablet.previous_tool.take() {
            self.tool = tool;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn native_samples_keep_pressure_and_tilt_and_suppress_mouse() {
        let ctx = egui::Context::default();
        let mut app = PhotoDeck::with_context(
            &ctx,
            LaunchOptions {
                no_settings: true,
                ..Default::default()
            },
        );
        app.tabs.clear();
        app.push(Document::new(Size::new(64, 64), false).unwrap());
        app.tool = Tool::Brush;
        let rect = egui::Rect::from_min_size(Pos2::ZERO, Vec2::splat(64.));
        app.tablet.canvas = Some((rect, rect, 1.));
        app.handle_tablet_event(
            &ctx,
            TabletEvent {
                position: [10., 10.],
                pressure: 0.2,
                tilt: [20., -12.],
                eraser: false,
                phase: TabletPhase::Down,
            },
        );
        app.handle_tablet_event(
            &ctx,
            TabletEvent {
                position: [20., 20.],
                pressure: 0.8,
                tilt: [35., 3.],
                eraser: false,
                phase: TabletPhase::Move,
            },
        );
        let samples = &app.gesture.as_ref().unwrap().samples;
        assert_eq!(samples[0].pressure, 0.2);
        assert_eq!(samples[0].tilt_y, -12.);
        assert_eq!(samples[1].pressure, 0.8);
        assert_eq!(samples[1].tilt_x, 35.);
        assert!(app.tablet.suppress_mouse(app.frames));
        app.handle_tablet_event(
            &ctx,
            TabletEvent {
                position: [20., 20.],
                pressure: 0.,
                tilt: [0.; 2],
                eraser: false,
                phase: TabletPhase::Up,
            },
        );
        assert!(app.gesture.is_none());
        assert!(app.doc().dirty);
        assert!(app.tablet.suppress_mouse(app.frames));
    }
}
