//! Tablet events from Wayland tablet-v2 / Windows Ink RealTimeStylus.
//! X11 and macOS are explicitly unsupported by the upstream backend.
use crate::types::{Result, StrokeSample};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TabletPhase {
    Hover,
    Down,
    Move,
    Up,
    Leave,
}
#[derive(Clone, Copy, Debug)]
pub struct TabletEvent {
    /// Window-relative logical pixels (before egui's user zoom).
    pub position: [f32; 2],
    pub pressure: f64,
    /// Degrees from the perpendicular, matching the brush engine.
    pub tilt: [f64; 2],
    pub eraser: bool,
    pub phase: TabletPhase,
}
impl TabletEvent {
    /// Call after converting window coordinates into document coordinates.
    pub fn sample_at(self, x: f64, y: f64) -> StrokeSample {
        StrokeSample {
            x,
            y,
            pressure: self.pressure,
            tilt_x: self.tilt[0],
            tilt_y: self.tilt[1],
        }
    }
}

pub struct TabletInput {
    #[cfg(any(target_os = "linux", target_os = "windows"))]
    manager: octotablet::Manager,
    #[cfg(any(target_os = "linux", target_os = "windows"))]
    tools: std::collections::HashMap<octotablet::tool::ID, ToolState>,
}
#[cfg(any(target_os = "linux", target_os = "windows"))]
#[derive(Default)]
struct ToolState {
    pose: Option<octotablet::axis::Pose>,
    down: bool,
    pending: Option<TabletPhase>,
}
impl TabletInput {
    /// Connect to the native tablet service on the GUI thread.
    ///
    /// # Safety
    /// The window/display in `cc` must outlive this object. Store it in the
    /// eframe App and drop it before the root native window is destroyed. Never
    /// move it to an export worker or retain it after App shutdown.
    /// In eframe 0.33's glow backend, `GlowWinitRunning` declares `app` before
    /// `glutin` (the native window owner), so normal struct destruction drops
    /// the App first. Re-audit that ordering when upgrading the framework.
    pub unsafe fn new(cc: &eframe::CreationContext<'_>) -> Result<Self> {
        #[cfg(any(target_os = "linux", target_os = "windows"))]
        {
            // SAFETY: the caller guarantees the raw-handle owner lifetime.
            let manager = unsafe {
                octotablet::Builder::new()
                    .emulate_tool_from_mouse(false)
                    .build_raw(cc)
            }
            .map_err(|e| format!("Yerel tablet bağlantısı kullanılamıyor: {e}"))?;
            Ok(Self {
                manager,
                tools: Default::default(),
            })
        }
        #[cfg(not(any(target_os = "linux", target_os = "windows")))]
        {
            let _ = cc;
            Err("Bu platform için yerel tablet adaptörü henüz yok.".into())
        }
    }
    pub fn backend_name(&self) -> &'static str {
        #[cfg(any(target_os = "linux", target_os = "windows"))]
        {
            match self.manager.backed() {
                octotablet::Backend::WaylandTabletUnstableV2 => "Wayland tablet-v2",
                octotablet::Backend::WindowsInkRealTimeStylus => "Windows Ink",
            }
        }
        #[cfg(not(any(target_os = "linux", target_os = "windows")))]
        {
            "Desteklenmiyor"
        }
    }
    /// Nonblocking. Pump on the GUI thread even while the pointer is hovering.
    /// Native frames are kept in order; callers should suppress synthesized
    /// mouse painting for the duration of a native tablet stroke.
    pub fn pump(&mut self) -> Result<Vec<TabletEvent>> {
        #[cfg(any(target_os = "linux", target_os = "windows"))]
        {
            use octotablet::events::{Event, ToolEvent};
            let mut output = Vec::new();
            for event in self.manager.pump().map_err(|e| e.to_string())? {
                let Event::Tool { tool, event } = event else {
                    continue;
                };
                if matches!(
                    tool.tool_type,
                    Some(
                        octotablet::tool::Type::Emulated
                            | octotablet::tool::Type::Mouse
                            | octotablet::tool::Type::Finger
                    )
                ) {
                    continue;
                }
                let state = self.tools.entry(tool.id()).or_default();
                match event {
                    ToolEvent::Pose(pose) => {
                        state.pose = Some(pose);
                    }
                    ToolEvent::Down => {
                        state.down = true;
                        state.pending = Some(TabletPhase::Down);
                    }
                    ToolEvent::Up => {
                        state.down = false;
                        state.pending = Some(TabletPhase::Up);
                    }
                    ToolEvent::Out | ToolEvent::Removed => {
                        if let Some(pose) = state.pose
                            && let Some(e) = normalized(
                                pose,
                                tool.tool_type == Some(octotablet::tool::Type::Eraser),
                                TabletPhase::Leave,
                            )
                        {
                            output.push(e);
                        }
                        self.tools.remove(&tool.id());
                    }
                    ToolEvent::Frame(_) => {
                        let phase = state.pending.take().unwrap_or(if state.down {
                            TabletPhase::Move
                        } else {
                            TabletPhase::Hover
                        });
                        if let Some(pose) = state.pose
                            && let Some(e) = normalized(
                                pose,
                                tool.tool_type == Some(octotablet::tool::Type::Eraser),
                                phase,
                            )
                        {
                            output.push(e);
                        }
                    }
                    _ => {}
                }
            }
            Ok(output)
        }
        #[cfg(not(any(target_os = "linux", target_os = "windows")))]
        {
            Ok(Vec::new())
        }
    }
}
#[cfg(any(target_os = "linux", target_os = "windows"))]
fn normalized(
    pose: octotablet::axis::Pose,
    eraser: bool,
    phase: TabletPhase,
) -> Option<TabletEvent> {
    if !pose.position.iter().all(|n| n.is_finite()) {
        return None;
    }
    let pressure = pose
        .pressure
        .get()
        .filter(|p| p.is_finite())
        .unwrap_or(1.)
        .clamp(0., 1.) as f64;
    let tilt = pose.tilt.unwrap_or([0.; 2]).map(|v| {
        if v.is_finite() {
            (v as f64).to_degrees().clamp(-90., 90.)
        } else {
            0.
        }
    });
    Some(TabletEvent {
        position: pose.position,
        pressure,
        tilt,
        eraser,
        phase,
    })
}
#[cfg(all(test, any(target_os = "linux", target_os = "windows")))]
mod tests {
    use super::*;
    #[test]
    fn native_units_and_invalid_axes_are_normalized() {
        let mut pose = octotablet::axis::Pose {
            position: [120., 44.],
            pressure: octotablet::util::NicheF32::new_some(0.25).unwrap(),
            tilt: Some([std::f32::consts::FRAC_PI_4, f32::NAN]),
            ..Default::default()
        };
        let e = normalized(pose, true, TabletPhase::Down).unwrap();
        assert_eq!(e.pressure, 0.25);
        assert!((e.tilt[0] - 45.).abs() < 0.0001);
        assert_eq!(e.tilt[1], 0.);
        assert_eq!(e.sample_at(12., 4.).x, 12.);
        pose.position[0] = f32::NAN;
        assert!(normalized(pose, false, TabletPhase::Move).is_none());
    }
}
