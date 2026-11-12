//! One bounded background job, immutable inputs and explicit cancellation/publication.
use crate::{output, render, types::*};
use image::RgbaImage;
use std::{
    path::PathBuf,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
        mpsc,
    },
    thread::{self, JoinHandle},
};

pub const CANCELLED: &str = "İşlem iptal edildi";
#[derive(Clone, Debug, Default)]
pub struct Progress {
    pub stage: String,
    pub completed: usize,
    pub total: Option<usize>,
}
#[derive(Default)]
struct Control {
    cancelled: AtomicBool,
    publication: Mutex<()>,
    progress: Mutex<Progress>,
}
#[derive(Clone, Default)]
pub struct CancellationToken(Arc<Control>);
impl CancellationToken {
    pub fn cancel(&self) {
        // Publication itself is a short rename/hard-link operation. Staging and
        // encoding never hold this gate; cancel remains independent of their cost.
        let _gate = self.0.publication.lock().unwrap_or_else(|e| e.into_inner());
        self.0.cancelled.store(true, Ordering::Release);
    }
    pub fn is_cancelled(&self) -> bool {
        self.0.cancelled.load(Ordering::Acquire)
    }
    pub fn check(&self) -> Result<()> {
        if self.is_cancelled() {
            Err(CANCELLED.into())
        } else {
            Ok(())
        }
    }
    pub fn report(&self, stage: &str, completed: usize, total: Option<usize>) {
        if let Ok(mut progress) = self.0.progress.lock() {
            *progress = Progress {
                stage: stage.into(),
                completed,
                total,
            };
        }
    }
    pub fn progress(&self) -> Progress {
        self.0
            .progress
            .lock()
            .map(|p| p.clone())
            .unwrap_or_default()
    }
    pub(crate) fn publish<T>(&self, f: impl FnOnce() -> Result<T>) -> Result<T> {
        let _gate = self
            .0
            .publication
            .lock()
            .map_err(|_| "İşlem kilidi bozuldu")?;
        self.check()?;
        f()
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct JobTarget {
    pub document_id: u64,
    pub revision: u64,
}
impl JobTarget {
    pub fn matches(self, document_id: u64, revision: u64) -> bool {
        self.document_id == document_id && self.revision == revision
    }
}
#[derive(Clone, Debug)]
pub struct BatchOptions {
    pub files: Vec<PathBuf>,
    pub destination: PathBuf,
    pub size: Option<Size>,
    pub allow_upscale: bool,
    pub brightness: i32,
    pub contrast: i32,
    pub saturation: i32,
    pub format: String,
    pub quality: u8,
}
#[derive(Debug, Default)]
pub struct BatchSummary {
    /// Attempted inputs. Successfully published outputs remain on disk after cancellation.
    pub results: Vec<(PathBuf, Result<PathBuf>)>,
    pub cancelled: bool,
    pub total: usize,
}
pub enum JobRequest {
    PathSelection {
        points: Vec<Point>,
        size: Size,
    },
    Render {
        state: State,
    },
    Raster {
        state: State,
        path: PathBuf,
        options: output::RasterOptions,
    },
    Pdf {
        state: State,
        path: PathBuf,
        options: output::PdfOptions,
    },
    Batch {
        options: BatchOptions,
    },
}
pub enum JobOutput {
    Selection(Selection),
    Rendered(RgbaImage),
    Exported(PathBuf),
    Batch(BatchSummary),
}
pub struct JobCompletion {
    pub id: u64,
    pub target: JobTarget,
    pub result: Result<JobOutput>,
}
struct Active {
    id: u64,
    target: JobTarget,
    token: CancellationToken,
    worker: JoinHandle<()>,
    receiver: mpsc::Receiver<Result<JobOutput>>,
}
#[derive(Default)]
pub struct Jobs {
    next_id: u64,
    active: Option<Active>,
}
impl Jobs {
    pub fn new() -> Self {
        Self::default()
    }
    pub fn is_busy(&self) -> bool {
        self.active.is_some()
    }
    /// A completed worker remains busy until its result is drained with `poll`.
    pub fn is_finished(&self) -> bool {
        self.active
            .as_ref()
            .is_some_and(|active| active.worker.is_finished())
    }
    pub fn progress(&self) -> Option<Progress> {
        self.active.as_ref().map(|a| a.token.progress())
    }
    pub fn cancel(&self) {
        if let Some(a) = &self.active {
            a.token.cancel();
        }
    }
    pub fn submit(&mut self, target: JobTarget, request: JobRequest) -> Result<u64> {
        if self.is_busy() {
            return Err("Önce çalışan işlemi tamamlayın veya iptal edin".into());
        }
        let token = CancellationToken::default();
        token.report("Hazırlanıyor", 0, None);
        let control = token.clone();
        let (tx, receiver) = mpsc::sync_channel(1);
        let worker = thread::Builder::new()
            .name("photodeck-job".into())
            .spawn(move || {
                let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                    execute(request, &control)
                }))
                .unwrap_or_else(|_| Err("Arka plan işlemi beklenmedik biçimde durdu".into()));
                let _ = tx.send(result);
            })
            .map_err(|e| e.to_string())?;
        self.next_id += 1;
        let id = self.next_id;
        self.active = Some(Active {
            id,
            target,
            token,
            worker,
            receiver,
        });
        Ok(id)
    }
    pub fn poll(&mut self) -> Option<JobCompletion> {
        let active = self.active.as_ref()?;
        let result = match active.receiver.try_recv() {
            Ok(result) => result,
            Err(mpsc::TryRecvError::Empty) => return None,
            Err(mpsc::TryRecvError::Disconnected) => Err("Arka plan işlemi sonuç vermedi".into()),
        };
        let active = self.active.take().unwrap();
        let _ = active.worker.join();
        Some(JobCompletion {
            id: active.id,
            target: active.target,
            result,
        })
    }
}
impl Drop for Jobs {
    fn drop(&mut self) {
        if let Some(active) = self.active.take() {
            active.token.cancel();
            // No detached writer may publish after the owner has shut down.
            let _ = active.worker.join();
        }
    }
}
fn execute(request: JobRequest, token: &CancellationToken) -> Result<JobOutput> {
    token.check()?;
    match request {
        JobRequest::PathSelection { points, size } => {
            let selection = Selection::from_path_controlled(&points, size, token)?;
            token.check()?;
            Ok(JobOutput::Selection(selection))
        }
        JobRequest::Render { state } => {
            crate::document::validate_state(&state)?;
            let image = render::composite_controlled(&state, token)?;
            token.check()?;
            Ok(JobOutput::Rendered(image))
        }
        JobRequest::Raster {
            state,
            path,
            options,
        } => {
            output::export_raster_controlled(&state, &path, &options, token)?;
            Ok(JobOutput::Exported(path))
        }
        JobRequest::Pdf {
            state,
            path,
            options,
        } => {
            output::export_pdf_controlled(&state, &path, &options, token)?;
            Ok(JobOutput::Exported(path))
        }
        JobRequest::Batch { options } => {
            Ok(JobOutput::Batch(output::batch_controlled(&options, token)))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::document::Document;
    use std::time::{Duration, Instant};
    fn wait(jobs: &mut Jobs) -> JobCompletion {
        let deadline = Instant::now() + Duration::from_secs(10);
        loop {
            if let Some(done) = jobs.poll() {
                return done;
            }
            assert!(Instant::now() < deadline, "worker did not finish");
            thread::yield_now();
        }
    }
    #[test]
    fn snapshot_target_and_single_worker_are_preserved() {
        let mut doc = Document::new(Size::new(8, 8), true).unwrap();
        let target = JobTarget {
            document_id: 7,
            revision: doc.revision(),
        };
        let mut jobs = Jobs::new();
        let id = jobs
            .submit(
                target,
                JobRequest::Render {
                    state: doc.state.clone(),
                },
            )
            .unwrap();
        doc.add_layer("Later edit").unwrap();
        assert!(
            jobs.submit(
                target,
                JobRequest::Render {
                    state: doc.state.clone()
                }
            )
            .is_err()
        );
        let done = wait(&mut jobs);
        assert_eq!(done.id, id);
        assert_eq!(done.target, target);
        assert!(!done.target.matches(7, doc.revision()));
        assert!(!done.target.matches(8, target.revision));
        match done.result.unwrap() {
            JobOutput::Rendered(image) => assert_eq!(image.get_pixel(0, 0).0, [255; 4]),
            _ => panic!("unexpected result"),
        }
        assert!(!jobs.is_busy());
    }
    #[test]
    fn cancellation_orders_publication_and_preserves_prior_results() {
        let token = CancellationToken::default();
        let published = std::sync::atomic::AtomicUsize::new(0);
        token
            .publish(|| {
                published.fetch_add(1, Ordering::Relaxed);
                Ok(())
            })
            .unwrap();
        token.cancel();
        assert!(
            token
                .publish(|| {
                    published.fetch_add(1, Ordering::Relaxed);
                    Ok(())
                })
                .is_err()
        );
        assert_eq!(published.load(Ordering::Relaxed), 1);
    }
    #[test]
    fn cancelled_render_and_effects_return_no_partial_image() {
        let token = CancellationToken::default();
        token.cancel();
        let state = Document::new(Size::new(8, 8), true).unwrap().state;
        assert!(render::composite_controlled(&state, &token).is_err());
        assert!(
            crate::effects::gaussian_blur_controlled(&state.layers[0].pixels, 16, &token).is_err()
        );
    }
    #[test]
    fn drop_cancels_and_joins_worker() {
        let token = CancellationToken::default();
        let control = token.clone();
        let exited = Arc::new(AtomicBool::new(false));
        let exit = exited.clone();
        let (tx, receiver) = mpsc::sync_channel(1);
        let worker = thread::spawn(move || {
            while !control.is_cancelled() {
                thread::yield_now();
            }
            exit.store(true, Ordering::Release);
            let _ = tx.send(Err(CANCELLED.into()));
        });
        let jobs = Jobs {
            next_id: 1,
            active: Some(Active {
                id: 1,
                target: JobTarget {
                    document_id: 1,
                    revision: 1,
                },
                token,
                worker,
                receiver,
            }),
        };
        drop(jobs);
        assert!(exited.load(Ordering::Acquire));
    }
    #[test]
    fn disconnected_worker_releases_busy_state() {
        let (tx, receiver) = mpsc::sync_channel(1);
        let worker = thread::spawn(move || drop(tx));
        let mut jobs = Jobs {
            next_id: 1,
            active: Some(Active {
                id: 1,
                target: JobTarget {
                    document_id: 1,
                    revision: 1,
                },
                token: CancellationToken::default(),
                worker,
                receiver,
            }),
        };
        assert!(wait(&mut jobs).result.is_err());
        assert!(!jobs.is_busy());
    }
    #[test]
    fn cancellation_interrupts_running_gaussian_rows() {
        let token = CancellationToken::default();
        let control = token.clone();
        let worker = thread::spawn(move || {
            let im = RgbaImage::from_pixel(1024, 1024, image::Rgba([127; 4]));
            crate::effects::gaussian_blur_controlled(&im, 16, &control)
        });
        let deadline = Instant::now() + Duration::from_secs(10);
        while token.progress().stage.is_empty() {
            assert!(Instant::now() < deadline);
            thread::yield_now();
        }
        token.cancel();
        assert!(worker.join().unwrap().is_err());
    }
}
