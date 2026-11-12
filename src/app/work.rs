use super::*;
use photodeck::jobs::{BatchOptions, JobOutput, JobRequest, JobTarget, Jobs};

pub(super) enum WorkKind {
    Preview,
    Export(Dialog),
    Pdf {
        print: bool,
        state: State,
        title: String,
    },
    #[cfg(not(target_os = "linux"))]
    PrintRaster {
        path: PathBuf,
        title: String,
    },
    Batch,
    Selection(SelectionMode),
}
struct Ticket {
    revoked: bool,
    id: u64,
    target: JobTarget,
    generation: u64,
    kind: WorkKind,
}
impl Ticket {
    fn accepts_preview(&self, tab: &Tab) -> bool {
        !self.revoked
            && self.target.matches(tab.id, tab.doc.revision())
            && self.generation == tab.generation
    }
}
struct Pending {
    request: JobRequest,
    target: JobTarget,
    generation: u64,
    kind: WorkKind,
}
#[derive(Default)]
pub(super) struct Background {
    jobs: Jobs,
    ticket: Option<Ticket>,
    pending: Option<Pending>,
    print: Option<std::thread::JoinHandle<Result<photodeck::platform::PrintOutcome>>>,
}
impl Background {
    pub(super) fn user_busy(&self) -> bool {
        self.print.is_some()
            || self.pending.is_some()
            || self
                .ticket
                .as_ref()
                .is_some_and(|t| !matches!(t.kind, WorkKind::Preview))
    }
    pub(super) fn cancel(&mut self) {
        self.pending = None;
        if let Some(ticket) = &mut self.ticket {
            ticket.revoked = true;
        }
        self.jobs.cancel();
    }
}

impl PhotoDeck {
    pub(super) fn remember_recent(&mut self, path: &Path) {
        let path = absolute_path(path);
        self.prefs.recent.retain(|p| p != &path);
        self.prefs.recent.insert(0, path);
        self.prefs.recent.truncate(12);
    }
    pub(super) fn queue_work(&mut self, request: JobRequest, kind: WorkKind) -> bool {
        if self.work.user_busy() {
            self.error = Some("Önce çalışan işlemi tamamlayın veya iptal edin.".into());
            return false;
        }
        let tab = &self.tabs[self.active];
        self.work.pending = Some(Pending {
            request,
            kind,
            target: JobTarget {
                document_id: tab.id,
                revision: tab.doc.revision(),
            },
            generation: tab.generation,
        });
        // One worker and one pending explicit action. A live preview is cancelled
        // and drained before export/selection starts, avoiding two large renders.
        self.work.jobs.cancel();
        true
    }
    pub(super) fn queue_path_selection(&mut self, points: Vec<Point>) {
        self.queue_work(
            JobRequest::PathSelection {
                points,
                size: self.doc().state.size,
            },
            WorkKind::Selection(self.mode),
        );
    }
    pub(super) fn begin_export(&mut self, path: PathBuf) {
        let request = JobRequest::Raster {
            state: self.doc().state.clone(),
            path,
            options: output::RasterOptions {
                size: Some(Size::new(self.width, self.height)),
                quality: self.quality,
                dpi: self.dpi,
                profile: Some(self.profile as u8),
            },
        };
        self.queue_work(request, WorkKind::Export(Dialog::Export));
    }
    pub(super) fn begin_pdf(&mut self, path: PathBuf, print: bool) {
        let state = self.doc().state.clone();
        let request = JobRequest::Pdf {
            state: state.clone(),
            path,
            options: output::PdfOptions {
                dpi: self.dpi,
                paper: self.paper,
                landscape: self.landscape,
                margin_mm: self.margin,
            },
        };
        self.queue_work(
            request,
            WorkKind::Pdf {
                print,
                state,
                title: self.doc().title.clone(),
            },
        );
    }
    pub(super) fn begin_batch(&mut self, files: Vec<PathBuf>, destination: PathBuf) {
        self.queue_work(
            JobRequest::Batch {
                options: BatchOptions {
                    files,
                    destination,
                    size: Some(Size::new(self.width, self.height)),
                    allow_upscale: self.batch_upscale,
                    brightness: self.batch_adjust[0],
                    contrast: self.batch_adjust[1],
                    saturation: self.batch_adjust[2],
                    format: self.batch_format.clone(),
                    quality: self.quality,
                },
            },
            WorkKind::Batch,
        );
    }
    pub(super) fn poll_background(&mut self, ctx: &egui::Context) {
        if self.work.print.as_ref().is_some_and(|h| h.is_finished()) {
            let result = self
                .work
                .print
                .take()
                .unwrap()
                .join()
                .unwrap_or_else(|_| Err("Yazdırma işlemi beklenmedik biçimde durdu".into()));
            self.finish_print(result);
        }
        if let Some(completion) = self.work.jobs.poll()
            && let Some(ticket) = self
                .work
                .ticket
                .take()
                .filter(|t| t.id == completion.id && t.target == completion.target)
        {
            if ticket.revoked {
                match &completion.result {
                    Ok(JobOutput::Batch(summary)) => {
                        let errors = summary
                            .results
                            .iter()
                            .filter_map(|(path, result)| {
                                result
                                    .as_ref()
                                    .err()
                                    .map(|e| format!("{}: {e}", path.display()))
                            })
                            .collect::<Vec<_>>();
                        self.status = format!(
                            "İptal edildi · {} / {} dosya tamamlandı; çıktılar korundu",
                            summary.results.len() - errors.len(),
                            summary.total
                        );
                        if !errors.is_empty() {
                            self.error = Some(errors.join("\n"));
                        }
                    }
                    Ok(JobOutput::Exported(path)) => {
                        self.status = format!(
                            "İptal edildi; tamamlanmış çıktı korundu: {}",
                            path.display()
                        )
                    }
                    _ => self.status = "İptal edildi; tamamlanmış dosya çıktıları korunur".into(),
                }
                ctx.request_repaint();
                return;
            }
            let current = self.tab_index(ticket.target.document_id).filter(|&i| {
                ticket
                    .target
                    .matches(self.tabs[i].id, self.tabs[i].doc.revision())
            });
            let preview_current = current.filter(|&i| ticket.accepts_preview(&self.tabs[i]));
            match (ticket.kind, completion.result) {
                (WorkKind::Preview, Ok(JobOutput::Rendered(image))) => {
                    if let Some(i) = preview_current {
                        let color = egui::ColorImage::from_rgba_unmultiplied(
                            [image.width() as usize, image.height() as usize],
                            image.as_raw(),
                        );
                        let t = &mut self.tabs[i];
                        if let Some(texture) = &mut t.texture {
                            texture.set(color, egui::TextureOptions::LINEAR);
                        } else {
                            t.texture = Some(ctx.load_texture(
                                format!("document{}", t.id),
                                color,
                                egui::TextureOptions::LINEAR,
                            ));
                        }
                        t.rendered_revision = t.doc.revision();
                        t.rendered_generation = t.generation;
                        t.invalid = false;
                    }
                }
                (WorkKind::Preview, Err(error)) => {
                    if error != photodeck::jobs::CANCELLED
                        && let Some(i) =
                            current.filter(|&i| self.tabs[i].generation == ticket.generation)
                    {
                        self.tabs[i].invalid = false;
                        self.tabs[i].rendered_revision = self.tabs[i].doc.revision();
                        self.error = Some(error);
                    }
                }
                (WorkKind::Selection(mode), Ok(JobOutput::Selection(selection))) => {
                    if let Some(i) = current {
                        let s = &self.tabs[i].doc.state;
                        let combined = s.selection.combined(&selection, mode, s.size);
                        let result = self.tabs[i].doc.set_selection(combined);
                        if self.result(result).is_some() {
                            self.tabs[i].invalid = true;
                            self.tabs[i].generation = self.tabs[i].generation.wrapping_add(1);
                        }
                    } else {
                        self.status = "Belge değiştiği için eski seçim sonucu uygulanmadı".into();
                    }
                }
                (WorkKind::Export(dialog), Ok(JobOutput::Exported(path))) => {
                    self.status = format!("Dışa aktarıldı: {}", path.display());
                    if self.dialog == Some(dialog) {
                        self.dialog = None;
                    }
                }
                (
                    WorkKind::Pdf {
                        print,
                        state,
                        title,
                    },
                    Ok(JobOutput::Exported(path)),
                ) => {
                    self.status = format!("PDF kaydedildi: {}", path.display());
                    if print {
                        #[cfg(target_os = "linux")]
                        {
                            let _ = state;
                            self.launch_print(path, RgbaImage::new(1, 1), title);
                        }
                        #[cfg(not(target_os = "linux"))]
                        {
                            self.work.pending = Some(Pending {
                                request: JobRequest::Render { state },
                                kind: WorkKind::PrintRaster { path, title },
                                target: ticket.target,
                                generation: ticket.generation,
                            });
                        }
                    } else if self.dialog == Some(Dialog::Pdf) {
                        self.dialog = None;
                    }
                }
                #[cfg(not(target_os = "linux"))]
                (WorkKind::PrintRaster { path, title }, Ok(JobOutput::Rendered(image))) => {
                    self.launch_print(path, image, title)
                }
                (WorkKind::Batch, Ok(JobOutput::Batch(summary))) => {
                    let errors = summary
                        .results
                        .iter()
                        .filter_map(|(path, r)| {
                            r.as_ref().err().map(|e| format!("{}: {e}", path.display()))
                        })
                        .collect::<Vec<_>>();
                    self.status = format!(
                        "{} / {} dosya tamamlandı{}",
                        summary.results.len() - errors.len(),
                        summary.total,
                        if summary.cancelled {
                            " · iptal edildi"
                        } else {
                            ""
                        }
                    );
                    if !errors.is_empty() {
                        self.error = Some(errors.join("\n"));
                    } else if !summary.cancelled && self.dialog == Some(Dialog::Batch) {
                        self.dialog = None;
                    }
                }
                (_, Err(error)) => {
                    // Keep the operation dialog and its entered values intact.
                    if error == photodeck::jobs::CANCELLED {
                        self.status = error;
                    } else {
                        self.error = Some(error);
                    }
                }
                _ => self.error = Some("Beklenmeyen arka plan sonucu".into()),
            }
        }
        if !self.work.jobs.is_busy()
            && let Some(pending) = self.work.pending.take()
        {
            match self.work.jobs.submit(pending.target, pending.request) {
                Ok(id) => {
                    self.work.ticket = Some(Ticket {
                        revoked: false,
                        id,
                        target: pending.target,
                        generation: pending.generation,
                        kind: pending.kind,
                    })
                }
                Err(e) => self.error = Some(e),
            }
        }
        if !self.work.user_busy() {
            let tab = &self.tabs[self.active];
            let target = JobTarget {
                document_id: tab.id,
                revision: tab.doc.revision(),
            };
            let generation = tab.generation;
            if self
                .work
                .ticket
                .as_ref()
                .is_some_and(|t| t.target != target || t.generation != generation)
            {
                self.work.jobs.cancel();
            }
            if !self.work.jobs.is_busy()
                && (tab.invalid || tab.rendered_revision != tab.doc.revision())
            {
                match self.work.jobs.submit(
                    target,
                    JobRequest::Render {
                        state: tab.doc.state.clone(),
                    },
                ) {
                    Ok(id) => {
                        self.work.ticket = Some(Ticket {
                            revoked: false,
                            id,
                            target,
                            generation,
                            kind: WorkKind::Preview,
                        })
                    }
                    Err(e) => self.error = Some(e),
                }
            }
        }
        if self.work.jobs.is_busy() || self.work.print.is_some() {
            ctx.request_repaint_after(Duration::from_millis(20));
        }
    }
    pub(super) fn work_status(&mut self, ui: &mut egui::Ui) {
        if self.work.print.is_some() {
            ui.spinner();
            ui.label("Sistem yazdırma iletişim kutusu…");
            return;
        }
        if self.work.user_busy() {
            ui.spinner();
            if let Some(progress) = self.work.jobs.progress() {
                ui.label(progress.stage);
                if let Some(total) = progress.total {
                    ui.weak(format!("{} / {}", progress.completed, total));
                }
            } else {
                ui.label("İşlem hazırlanıyor…");
            }
            if ui.button("İptal").clicked() {
                self.work.cancel();
                self.status = "İptal isteniyor…".into();
            }
        } else {
            ui.add(egui::Label::new(&self.status).truncate())
                .on_hover_text(&self.status);
        }
    }
    fn launch_print(&mut self, path: PathBuf, image: RgbaImage, title: String) {
        #[cfg(target_os = "macos")]
        self.finish_print(photodeck::platform::print_document(&path, &image, &title));
        #[cfg(not(target_os = "macos"))]
        {
            self.work.print = Some(std::thread::spawn(move || {
                photodeck::platform::print_document(&path, &image, &title)
            }));
        }
    }
    fn finish_print(&mut self, result: Result<photodeck::platform::PrintOutcome>) {
        match result {
            Ok(photodeck::platform::PrintOutcome::Submitted) => {
                self.status = "Yazdırma işi sisteme gönderildi".into();
                self.dialog = None;
            }
            Ok(photodeck::platform::PrintOutcome::Cancelled) => {
                self.status = "Yazdırma iptal edildi; PDF korundu".into()
            }
            Err(e) => self.error = Some(e),
        }
    }
}

pub(super) fn absolute_path(path: &Path) -> PathBuf {
    path.canonicalize().unwrap_or_else(|_| {
        if path.is_absolute() {
            path.to_owned()
        } else {
            std::env::current_dir().unwrap_or_default().join(path)
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn cancelling_completed_unpolled_selection_revokes_ui_publication() {
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
        let before = app.doc().state.clone();
        app.queue_path_selection(vec![
            Point::new(1, 1),
            Point::new(6, 1),
            Point::new(6, 6),
            Point::new(1, 6),
        ]);
        app.poll_background(&ctx);
        let deadline = Instant::now() + Duration::from_secs(3);
        while !app.work.jobs.is_finished() && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(1));
        }
        assert!(app.work.jobs.is_finished());
        assert!(app.work.jobs.is_busy());
        app.work.cancel();
        app.poll_background(&ctx);
        assert_eq!(app.doc().state, before);
        assert!(app.work.print.is_none());
        assert!(app.status.starts_with("İptal edildi"));
    }
    #[test]
    fn preview_rejects_uncommitted_mutations_and_other_documents() {
        let mut tab = Tab::new(Document::new(Size::new(8, 8), false).unwrap(), 7);
        let ticket = Ticket {
            revoked: false,
            id: 1,
            target: JobTarget {
                document_id: 7,
                revision: tab.doc.revision(),
            },
            generation: 0,
            kind: WorkKind::Preview,
        };
        assert!(ticket.accepts_preview(&tab));
        tab.generation += 1; // live effects/brush input need not create a revision yet
        assert!(!ticket.accepts_preview(&tab));
        tab.generation = 0;
        tab.id = 8;
        assert!(!ticket.accepts_preview(&tab));
        tab.id = 7;
        tab.doc.add_layer("changed").unwrap();
        assert!(!ticket.accepts_preview(&tab));
    }
}
