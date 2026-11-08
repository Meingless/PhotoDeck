//! One immutable recovery snapshot in flight; session locks prevent stealing live work.
use crate::{document::Document, project, types::*};
use fs2::FileExt;
use std::{
    collections::HashSet,
    fs::{self, File, OpenOptions},
    path::{Path, PathBuf},
    sync::{Arc, Mutex, mpsc},
    thread::{self, JoinHandle},
};
#[derive(Debug, Clone)]
pub struct RecoveryCandidate {
    pub path: PathBuf,
    pub title: String,
}
struct Completion {
    id: u64,
    result: Result<()>,
}
pub struct Recovery {
    directory: PathBuf,
    legacy_directories: Vec<PathBuf>,
    session: String,
    _lock: Arc<File>,
    worker: Option<JoinHandle<()>>,
    rx: Option<mpsc::Receiver<Completion>>,
    busy: bool,
    removed: Arc<Mutex<HashSet<u64>>>,
}
pub fn directory() -> Result<PathBuf> {
    let dirs = directories::ProjectDirs::from("org", "PhotoDeck", "PhotoDeck-Rust")
        .ok_or("Kurtarma klasörü bulunamadı")?;
    Ok(dirs.data_local_dir().join("recovery"))
}
impl Recovery {
    pub fn new() -> Result<Self> {
        let mut recovery = Self::in_directory(directory()?)?;
        if let Some(base) = directories::BaseDirs::new() {
            recovery.legacy_directories = vec![
                base.data_local_dir().join("PhotoDeck/PhotoDeck/recovery"),
                base.data_local_dir().join("PhotoDeck/recovery"),
            ];
        }
        Ok(recovery)
    }
    pub fn in_directory(directory: PathBuf) -> Result<Self> {
        fs::create_dir_all(&directory).map_err(|e| e.to_string())?;
        let session = uuid::Uuid::new_v4().to_string();
        let lock = OpenOptions::new()
            .write(true)
            .read(true)
            .create_new(true)
            .open(directory.join(format!("{session}.lock")))
            .map_err(|e| e.to_string())?;
        lock.try_lock_exclusive().map_err(|e| e.to_string())?;
        Ok(Self {
            directory,
            legacy_directories: Vec::new(),
            session,
            _lock: Arc::new(lock),
            worker: None,
            rx: None,
            busy: false,
            removed: Arc::new(Mutex::new(HashSet::new())),
        })
    }
    fn path(&self, id: u64) -> PathBuf {
        self.directory.join(format!("{}-{id}.pdeck", self.session))
    }
    pub fn candidates(&self) -> Result<Vec<RecoveryCandidate>> {
        let mut candidates = candidates_in(&self.directory)?;
        for path in &self.legacy_directories {
            if path.is_dir() {
                candidates.extend(candidates_in(path)?);
            }
        }
        Ok(candidates)
    }
    pub fn submit(&mut self, id: u64, state: &State, title: &str) -> Result<bool> {
        if self.busy {
            return Ok(false);
        }
        crate::document::validate_state(state)?;
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
        self.removed
            .lock()
            .map_err(|_| "Kurtarma kilidi bozuldu")?
            .remove(&id);
        let removed = self.removed.clone();
        let state = state.clone();
        let path = self.path(id);
        let title = title.to_owned();
        let lock = self._lock.clone();
        let (tx, rx) = mpsc::channel();
        self.busy = true;
        self.rx = Some(rx);
        self.worker = Some(thread::spawn(move || {
            let _ownership = lock;
            let result = (|| {
                let data = project::encode_state(&state, true)?;
                // Encode and write the large payload outside the publication gate.
                use std::io::Write;
                let mut staged =
                    tempfile::NamedTempFile::new_in(path.parent().ok_or("Kurtarma yolu yok")?)
                        .map_err(|e| e.to_string())?;
                staged.write_all(&data).map_err(|e| e.to_string())?;
                staged.as_file().sync_all().map_err(|e| e.to_string())?;
                // Serialize invalidation and publication; a saved/discarded document can
                // never be resurrected even if the process exits before the next poll.
                let guard = removed.lock().map_err(|_| "Kurtarma kilidi bozuldu")?;
                if guard.contains(&id) {
                    return Ok(());
                }
                staged.persist(&path).map_err(|e| e.to_string())?;
                project::atomic_write(
                    &path.with_extension("json"),
                    serde_json::to_string(&title)
                        .map_err(|e| e.to_string())?
                        .as_bytes(),
                )
            })();
            let _ = tx.send(Completion { id, result });
        }));
        Ok(true)
    }
    pub fn poll(&mut self) -> Option<Result<()>> {
        let completion = match self.rx.as_ref()?.try_recv() {
            Ok(c) => c,
            Err(mpsc::TryRecvError::Empty) => return None,
            Err(_) => {
                self.busy = false;
                self.rx = None;
                return Some(Err("Kurtarma işçisi beklenmedik biçimde durdu".into()));
            }
        };
        self.busy = false;
        self.rx = None;
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
        if self.removed.lock().ok()?.remove(&completion.id) {
            return Some(self.delete_files(completion.id));
        }
        Some(completion.result)
    }
    fn delete_files(&self, id: u64) -> Result<()> {
        for path in [self.path(id), self.path(id).with_extension("json")] {
            match fs::remove_file(path) {
                Ok(()) => {}
                Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
                Err(e) => return Err(e.to_string()),
            }
        }
        Ok(())
    }
    pub fn remove(&mut self, id: u64) -> Result<()> {
        let mut removed = self.removed.lock().map_err(|_| "Kurtarma kilidi bozuldu")?;
        removed.insert(id);
        self.delete_files(id)
    }
}
impl Drop for Recovery {
    fn drop(&mut self) {
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
        if let Ok(removed) = self.removed.lock() {
            for id in removed.iter() {
                let _ = self.delete_files(*id);
            }
        } // Keep snapshots on unexpected shutdown; explicit save/discard removes them.
        let _ = FileExt::unlock(self._lock.as_ref());
        let _ = fs::remove_file(self.directory.join(format!("{}.lock", self.session)));
    }
}
fn candidates_in(directory: &Path) -> Result<Vec<RecoveryCandidate>> {
    let mut out = vec![];
    for entry in fs::read_dir(directory).map_err(|e| e.to_string())? {
        let entry = entry.map_err(|e| e.to_string())?;
        let path = entry.path();
        if !entry.file_type().map_err(|e| e.to_string())?.is_file()
            || path.extension().is_none_or(|s| s != "pdeck")
        {
            continue;
        }
        let raw_stem = path.file_stem().unwrap_or_default().to_string_lossy();
        let legacy = raw_stem.starts_with("photodeck-");
        let stem = raw_stem.strip_prefix("photodeck-").unwrap_or(&raw_stem);
        if stem.len() < 38 {
            continue;
        }
        let Some(session) = stem.get(..36) else {
            continue;
        };
        if uuid::Uuid::parse_str(session).is_err() {
            continue;
        }
        let lock_path = if legacy {
            directory.join(format!(".photodeck-{session}.lock"))
        } else {
            directory.join(format!("{session}.lock"))
        };
        // QLockFile is a pid-file protocol, not flock. Never infer an abandoned
        // Qt session from our ability to acquire an OS file lock.
        if legacy && fs::symlink_metadata(&lock_path).is_ok() {
            continue;
        }
        let lock = match OpenOptions::new().read(true).write(true).open(&lock_path) {
            Ok(l) => Some(l),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => None,
            Err(_) => continue,
        };
        if let Some(l) = &lock
            && l.try_lock_exclusive().is_err()
        {
            continue;
        }
        let title = read_title(&path.with_extension("json"))
            .ok()
            .filter(|b| b.len() <= 65536)
            .and_then(|b| serde_json::from_slice::<String>(&b).ok())
            .unwrap_or_else(|| "Kurtarılan belge".into());
        out.push(RecoveryCandidate { path, title });
    }
    Ok(out)
}
fn read_title(path: &Path) -> std::io::Result<Vec<u8>> {
    use std::io::Read;
    let mut bytes = Vec::new();
    File::open(path)?.take(65537).read_to_end(&mut bytes)?;
    Ok(bytes)
}
pub fn recover(path: &Path) -> Result<Document> {
    let directory = path.parent().ok_or("Kurtarma yolu geçersiz")?;
    if !candidates_in(directory)?.iter().any(|c| c.path == path) {
        return Err("Kurtarma başka bir oturuma ait veya doğrulanamadı".into());
    }
    let (mut doc, _) = project::open(path)?;
    doc.mark_recovered();
    Ok(doc)
}
pub fn discard(path: &Path) -> Result<()> {
    let directory = path.parent().ok_or("Kurtarma yolu geçersiz")?;
    if !candidates_in(directory)?.iter().any(|c| c.path == path) {
        return Err("Etkin oturumun kurtarması silinemez".into());
    }
    fs::remove_file(path).map_err(|e| e.to_string())?;
    let _ = fs::remove_file(path.with_extension("json"));
    Ok(())
}
