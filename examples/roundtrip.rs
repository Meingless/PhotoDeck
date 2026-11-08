//! Development oracle harness: Rust decode/save and raster output, no GUI or Qt linkage.
use photodeck::{output, project};
fn main() {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    assert_eq!(args.len(), 2, "roundtrip INPUT.pdeck OUTPUT.pdeck");
    let (mut doc, _) = project::open(std::path::Path::new(&args[0])).expect("read project");
    let output = std::path::Path::new(&args[1]);
    project::save(&mut doc, output, None, project::SaveOptions::default()).expect("save project");
    output::export_raster(
        &doc.state,
        &output.with_extension("png"),
        &Default::default(),
    )
    .expect("render");
    output::export_pdf(
        &doc.state,
        &output.with_extension("pdf"),
        &Default::default(),
    )
    .expect("PDF");
}
