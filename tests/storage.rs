use photodeck::{
    document::Document,
    output::{self, PdfOptions, RasterOptions},
    project::{self, SaveError, SaveOptions},
    recovery::{self, Recovery},
    settings,
    types::*,
};
use std::{
    fs,
    path::PathBuf,
    sync::Arc,
    time::{Duration, Instant},
};
fn doc() -> Document {
    Document::new(Size::new(12, 9), false).unwrap()
}
fn fixture(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("tests/fixtures")
        .join(name)
}
#[test]
fn reads_actual_qt_versions_and_roundtrips() {
    for n in ["qt-v5.pdeck", "qt-v6.pdeck", "qt-v7.pdeck"] {
        let bytes = fs::read(fixture(n)).unwrap();
        let (s, v) = project::decode_state(&bytes).unwrap();
        assert_eq!(s.layers[0].name, "Türkçe — α");
        assert_eq!(s.layers[0].color, [13, 29, 111, 233]);
        let data = project::encode_state(&s, false).unwrap();
        let (reopened, version) = project::decode_state(&data).unwrap();
        assert_eq!(v, version);
        assert_eq!(s, reopened);
        if v == 7 {
            assert!(s.layers[0].is_smart_object());
            assert_eq!(s.layers[0].smart_object_size, Size::new(12, 9));
            assert_eq!(s.layers[0].pixels.dimensions(), (7, 5));
        }
    }
    for n in [
        "v2-demo.pdeck",
        "v3-demo.pdeck",
        "v4-demo.pdeck",
        "v5-demo.pdeck",
    ] {
        let p = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("legacy/qt/tests/fixtures")
            .join(n);
        let (s, _) = project::decode_state(&fs::read(p).unwrap()).unwrap();
        let (reopened, _) =
            project::decode_state(&project::encode_state(&s, false).unwrap()).unwrap();
        assert_eq!(s, reopened);
    }
    let legacy = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("examples/Horizon.pdeck");
    project::decode_state(&fs::read(legacy).unwrap()).unwrap();
}
#[test]
fn rejects_all_truncations_bad_lengths_and_trailing_data() {
    let data = fs::read(fixture("qt-v7.pdeck")).unwrap();
    for len in 0..data.len() {
        assert!(
            project::decode_state(&data[..len]).is_err(),
            "accepted truncated length {len}"
        );
    }
    let mut invalid = data.clone();
    invalid.extend([0]);
    assert!(project::decode_state(&invalid).is_err());
    for start in [4, 8, 12, 16, 20, 24] {
        let mut bad = data.clone();
        bad[start..start + 4].copy_from_slice(&u32::MAX.to_be_bytes());
        assert!(project::decode_state(&bad).is_err(), "field {start}");
    }
}
#[test]
fn native_save_excludes_transient_but_recovery_keeps_it() {
    let mut s = doc().state;
    s.selection = Selection::Rectangle(Rect::new(2, 1, 4, 3));
    s.previous_selection = Selection::Mask(Arc::new(image::GrayImage::new(12, 9)));
    let (normal, version) =
        project::decode_state(&project::encode_state(&s, false).unwrap()).unwrap();
    assert_eq!(version, 5);
    assert_eq!(normal.selection, Selection::None);
    let (recovery, version) =
        project::decode_state(&project::encode_state(&s, true).unwrap()).unwrap();
    assert_eq!(version, 6);
    assert_eq!(s, recovery);
}
#[test]
fn save_conflict_and_format_upgrade_leave_original_intact() {
    let tmp = tempfile::tempdir().unwrap();
    let path = tmp.path().join("doc.pdeck");
    fs::copy(fixture("qt-v5.pdeck"), &path).unwrap();
    let (mut d, p) = project::open(&path).unwrap();
    d.set_selection(Selection::Rectangle(Rect::new(1, 1, 2, 2)))
        .unwrap();
    d.save_selection("Test").unwrap();
    let original = fs::read(&path).unwrap();
    assert!(matches!(
        project::save(&mut d, &path, Some(&p), SaveOptions::default()),
        Err(SaveError::FormatUpgradeRequired)
    ));
    assert_eq!(original, fs::read(&path).unwrap());
    let copy = tmp.path().join("copy.pdeck");
    project::save(&mut d, &copy, Some(&p), SaveOptions::default()).unwrap();
    fs::write(&path, b"other author").unwrap();
    assert!(matches!(
        project::save(&mut d, &path, Some(&p), SaveOptions::default()),
        Err(SaveError::ExternalModification)
    ));
    assert_eq!(fs::read(&path).unwrap(), b"other author");
    fs::remove_file(&path).unwrap();
    assert!(matches!(
        project::save(&mut d, &path, Some(&p), SaveOptions::default()),
        Err(SaveError::ExternalModification)
    ));
}
#[cfg(unix)]
#[test]
fn symlink_alias_cannot_bypass_save_conflict() {
    use std::os::unix::fs::symlink;
    let tmp = tempfile::tempdir().unwrap();
    let path = tmp.path().join("real.pdeck");
    let alias = tmp.path().join("alias.pdeck");
    let mut d = doc();
    project::save(&mut d, &path, None, SaveOptions::default()).unwrap();
    symlink(&path, &alias).unwrap();
    let (mut d, p) = project::open(&alias).unwrap();
    fs::write(&path, b"external").unwrap();
    assert!(matches!(
        project::save(&mut d, &path, Some(&p), SaveOptions::default()),
        Err(SaveError::ExternalModification)
    ));
    assert_eq!(fs::read(path).unwrap(), b"external");
}
#[test]
fn lossless_raster_formats_and_profiles_decode() {
    let tmp = tempfile::tempdir().unwrap();
    let mut d = doc();
    d.state.layers[0].pixels = Arc::new(image::RgbaImage::from_pixel(
        12,
        9,
        image::Rgba([200, 40, 100, 160]),
    ));
    let before = d.state.clone();
    for ext in ["png", "webp", "bmp", "tif", "jpg"] {
        let p = tmp.path().join(format!("out.{ext}"));
        let o = RasterOptions {
            profile: if ext == "bmp" { None } else { Some(1) },
            dpi: 300,
            ..Default::default()
        };
        output::export_raster(&d.state, &p, &o).unwrap();
        let im = project::read_image(&p).unwrap();
        assert_eq!(im.dimensions(), (12, 9));
        if ext == "png" || ext == "tif" || ext == "webp" {
            assert_eq!(im.get_pixel(2, 2)[3], 160);
            assert!(
                (im.get_pixel(2, 2)[0] as i32 - 200).abs() < 4,
                "{ext}: {:?}",
                im.get_pixel(2, 2)
            );
        }
        if ext == "jpg" {
            assert_eq!(im.get_pixel(2, 2)[3], 255)
        }
    }
    assert_eq!(d.state, before);
}
#[test]
fn failed_export_preserves_existing_and_pdf_is_valid_structure() {
    let tmp = tempfile::tempdir().unwrap();
    let p = tmp.path().join("out.png");
    fs::write(&p, b"original").unwrap();
    assert!(
        output::export_raster(
            &doc().state,
            &p,
            &RasterOptions {
                size: Some(Size::new(0, 10)),
                ..Default::default()
            }
        )
        .is_err()
    );
    assert_eq!(fs::read(p).unwrap(), b"original");
    let p = tmp.path().join("out.pdf");
    output::export_pdf(&doc().state, &p, &PdfOptions::default()).unwrap();
    let data = fs::read(&p).unwrap();
    assert!(data.starts_with(b"%PDF-1.4"));
    assert!(data.ends_with(b"%%EOF\n"));
    assert!(
        output::export_pdf(
            &doc().state,
            &p,
            &PdfOptions {
                margin_mm: 1000.,
                ..Default::default()
            }
        )
        .is_err()
    );
    assert_eq!(fs::read(p).unwrap(), data);
}
#[test]
fn brush_roundtrip_merge_and_concurrent_store_guard() {
    let tmp = tempfile::tempdir().unwrap();
    let path = tmp.path().join("brushes.pdbrush");
    let p = settings::builtins();
    settings::write_brushes(&path, &p).unwrap();
    assert_eq!(settings::read_brushes(&path).unwrap(), p);
    assert_eq!(settings::merge_brushes(&p, &p).unwrap().len(), 8);
    let mut a = settings::BrushStore::at(path.clone()).unwrap();
    let mut b = settings::BrushStore::at(path.clone()).unwrap();
    a.replace(vec![p[0].clone()]).unwrap();
    assert!(b.replace(vec![p[1].clone()]).is_err());
    assert_eq!(settings::read_brushes(&path).unwrap(), vec![p[0].clone()]);
    fs::write(&path, b"corrupt").unwrap();
    let mut c = settings::BrushStore::at(path).unwrap();
    assert!(c.has_unreadable_backup_pending());
    c.replace(p).unwrap();
    assert!(
        fs::read_dir(tmp.path()).unwrap().any(|p| p
            .unwrap()
            .file_name()
            .to_string_lossy()
            .contains(".bak-"))
    );
}
fn finish(r: &mut Recovery) {
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        if let Some(result) = r.poll() {
            result.unwrap();
            break;
        }
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(10));
    }
}
#[test]
fn recovery_session_ownership_and_reopened_copy() {
    let tmp = tempfile::tempdir().unwrap();
    let mut r = Recovery::in_directory(tmp.path().to_owned()).unwrap();
    let mut d = doc();
    d.state.selection = Selection::Rectangle(Rect::new(1, 2, 3, 4));
    assert!(r.submit(1, &d.state, "Test").unwrap());
    assert!(!r.submit(2, &d.state, "Second").unwrap());
    finish(&mut r);
    assert!(r.candidates().unwrap().is_empty());
    drop(r);
    let observer = Recovery::in_directory(tmp.path().to_owned()).unwrap();
    let candidates = observer.candidates().unwrap();
    assert_eq!(candidates.len(), 1);
    let recovered = recovery::recover(&candidates[0].path).unwrap();
    assert!(recovered.dirty);
    assert_eq!(recovered.state.selection, d.state.selection);
}
#[test]
fn discard_during_worker_cannot_resurrect_recovery() {
    let tmp = tempfile::tempdir().unwrap();
    let mut r = Recovery::in_directory(tmp.path().to_owned()).unwrap();
    r.submit(1, &doc().state, "Test").unwrap();
    r.remove(1).unwrap();
    finish(&mut r);
    drop(r);
    let r = Recovery::in_directory(tmp.path().to_owned()).unwrap();
    assert!(r.candidates().unwrap().is_empty());
}

#[test]
fn legacy_qt_recovery_is_offered_only_without_its_pid_lock() {
    let tmp = tempfile::tempdir().unwrap();
    let id = "22f47573-8243-49ed-a286-2ed1b8910725";
    let path = tmp.path().join(format!("photodeck-{id}-1.pdeck"));
    let lock = tmp.path().join(format!(".photodeck-{id}.lock"));
    fs::write(&path, project::encode_state(&doc().state, true).unwrap()).unwrap();
    fs::write(&lock, b"12345\nPhotoDeck\nhost\n").unwrap();
    let recovery = Recovery::in_directory(tmp.path().to_owned()).unwrap();
    assert!(recovery.candidates().unwrap().is_empty());
    assert!(recovery::recover(&path).is_err());
    fs::remove_file(lock).unwrap();
    assert_eq!(recovery.candidates().unwrap().len(), 1);
    assert!(recovery::recover(&path).unwrap().dirty);
}

#[cfg(windows)]
#[test]
fn deleted_windows_source_case_alias_still_conflicts() {
    let tmp = tempfile::tempdir().unwrap();
    let path = tmp.path().join("Document.pdeck");
    let alias = tmp.path().join("DOCUMENT.PDECK");
    let mut d = doc();
    let source = project::save(&mut d, &path, None, SaveOptions::default()).unwrap();
    fs::remove_file(&path).unwrap();
    assert!(matches!(
        project::save(&mut d, &alias, Some(&source), SaveOptions::default()),
        Err(SaveError::ExternalModification)
    ));
    assert!(!alias.exists());
}

#[test]
fn tightly_packed_selection_masks_roundtrip_at_budget_boundary() {
    let mut state = Document::new(Size::new(1025, 16368), false).unwrap().state;
    let mask = Arc::new(image::GrayImage::new(1025, 16368));
    for i in 0..4 {
        state.saved_selections.push(SavedSelection {
            name: format!("Mask {i}"),
            selection: Selection::Mask(mask.clone()),
        });
    }
    photodeck::document::validate_state(&state).unwrap();
    let bytes = project::encode_state(&state, false).unwrap();
    let (reopened, _) = project::decode_state(&bytes).unwrap();
    assert_eq!(reopened.saved_selections, state.saved_selections);
}

#[cfg(unix)]
#[test]
fn non_regular_brush_and_recovery_metadata_do_not_block() {
    let tmp = tempfile::tempdir().unwrap();
    let brush = tmp.path().join("blocked.pdbrush");
    assert!(
        std::process::Command::new("mkfifo")
            .arg(&brush)
            .status()
            .unwrap()
            .success()
    );
    let path = tmp
        .path()
        .join("22f47573-8243-49ed-a286-2ed1b8910725-1.pdeck");
    fs::write(&path, project::encode_state(&doc().state, true).unwrap()).unwrap();
    assert!(
        std::process::Command::new("mkfifo")
            .arg(path.with_extension("json"))
            .status()
            .unwrap()
            .success()
    );
    let (tx, rx) = std::sync::mpsc::channel();
    std::thread::spawn(move || {
        assert!(settings::read_brushes(&brush).is_err());
        assert!(settings::BrushStore::at(brush).is_err());
        let observer = Recovery::in_directory(tmp.path().to_owned()).unwrap();
        let candidates = observer.candidates().unwrap();
        assert_eq!(candidates.len(), 1);
        assert_eq!(candidates[0].title, "Kurtarılan belge");
        tx.send(()).unwrap();
    });
    rx.recv_timeout(Duration::from_secs(3))
        .expect("non-regular input blocked reader");
}

#[test]
fn recovery_metadata_roundtrips_source_without_reading_it() {
    let tmp = tempfile::tempdir().unwrap();
    let source = tmp.path().join("missing-original.pdeck");
    let mut r = Recovery::in_directory(tmp.path().to_owned()).unwrap();
    r.submit_with_source(1, &doc().state, "Çalışma", Some(&source))
        .unwrap();
    finish(&mut r);
    drop(r);
    let observer = Recovery::in_directory(tmp.path().to_owned()).unwrap();
    let candidates = observer.candidates().unwrap();
    assert_eq!(candidates.len(), 1);
    assert_eq!(candidates[0].title, "Çalışma");
    assert_eq!(candidates[0].source_path.as_deref(), Some(source.as_path()));
    assert!(!source.exists());
    assert!(recovery::recover(&candidates[0].path).unwrap().dirty);
    assert!(!source.exists());
}

#[test]
fn recovery_reads_legacy_string_and_bounds_new_metadata() {
    let tmp = tempfile::tempdir().unwrap();
    let path = tmp
        .path()
        .join("22f47573-8243-49ed-a286-2ed1b8910725-1.pdeck");
    fs::write(&path, project::encode_state(&doc().state, true).unwrap()).unwrap();
    fs::write(path.with_extension("json"), br#""Legacy title""#).unwrap();
    let observer = Recovery::in_directory(tmp.path().to_owned()).unwrap();
    let candidates = observer.candidates().unwrap();
    assert_eq!(candidates[0].title, "Legacy title");
    assert!(candidates[0].source_path.is_none());
    fs::write(
        path.with_extension("json"),
        serde_json::to_vec(&serde_json::json!({
            "title": "Too large", "source_path": "x".repeat(65536)
        }))
        .unwrap(),
    )
    .unwrap();
    let candidates = observer.candidates().unwrap();
    assert_eq!(candidates[0].title, "Kurtarılan belge");
    assert!(candidates[0].source_path.is_none());
    fs::write(path.with_extension("json"), b"{invalid").unwrap();
    assert_eq!(observer.candidates().unwrap()[0].title, "Kurtarılan belge");
}
