mod app;
use std::path::PathBuf;
fn main() -> eframe::Result {
    let mut options = app::LaunchOptions::default();
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--no-settings" => options.no_settings = true,
            "--ui-scale" => {
                let value = required(&mut args, "--ui-scale")
                    .parse::<f32>()
                    .ok()
                    .filter(|v| v.is_finite() && (0.75..=2.).contains(v));
                if value.is_none() {
                    eprintln!("Arayüz ölçeği 0.75–2 arasında olmalı");
                    std::process::exit(2);
                }
                options.ui_scale = value;
            }
            "--window-size" => {
                let w = required(&mut args, "--window-size").parse::<f32>().ok();
                let h = required(&mut args, "--window-size").parse::<f32>().ok();
                match (w, h) {
                    (Some(w), Some(h))
                        if (800. ..=7680.).contains(&w) && (560. ..=4320.).contains(&h) =>
                    {
                        options.window_size = Some([w, h])
                    }
                    _ => {
                        eprintln!("Pencere boyutu en az 800 × 560 olmalı");
                        std::process::exit(2);
                    }
                }
            }
            "--preset" => {
                let p = required(&mut args, "--preset");
                if ![
                    "classic-pro",
                    "graphite",
                    "paper",
                    "focus",
                    "film",
                    "workbench",
                    "ribbon",
                    "ink",
                    "compact",
                    "touch",
                    "modular",
                ]
                .contains(&p.as_str())
                {
                    eprintln!("Bilinmeyen preset: {p}");
                    std::process::exit(2);
                }
                options.preset = Some(p);
            }
            "--screenshot" => {
                options.screenshot = Some(PathBuf::from(required(&mut args, "--screenshot")))
            }
            "--write-demo" => {
                let path = required(&mut args, "--write-demo");
                if let Err(e) = app::write_demo(&PathBuf::from(path)) {
                    eprintln!("{e}");
                    std::process::exit(1);
                }
                return Ok(());
            }
            "--version" => {
                println!("PhotoDeck {} · Rust", env!("CARGO_PKG_VERSION"));
                return Ok(());
            }
            "--help" => {
                println!(
                    "PhotoDeck [FILE…] [--no-settings] [--preset NAME] [--screenshot FILE.png] [--write-demo FILE.pdeck]"
                );
                return Ok(());
            }
            _ if arg.starts_with('-') => {
                eprintln!("Bilinmeyen seçenek: {arg}");
                std::process::exit(2);
            }
            _ => options.files.push(PathBuf::from(arg)),
        }
    }
    let icon = image::load_from_memory(include_bytes!("../packaging/photodeck.png"))
        .expect("Bundled application icon")
        .to_rgba8();
    let icon = eframe::egui::IconData {
        width: icon.width(),
        height: icon.height(),
        rgba: icon.into_raw(),
    };
    let native = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default()
            .with_inner_size(options.window_size.unwrap_or([1380., 900.]))
            .with_icon(icon)
            .with_min_inner_size([800., 560.]),
        ..Default::default()
    };
    eframe::run_native(
        "PhotoDeck",
        native,
        Box::new(move |cc| Ok(Box::new(app::PhotoDeck::new(cc, options)))),
    )
}

fn required(args: &mut impl Iterator<Item = String>, flag: &str) -> String {
    match args.next() {
        Some(v) if !v.starts_with("--") => v,
        _ => {
            eprintln!("{flag} bir değer gerektiriyor");
            std::process::exit(2);
        }
    }
}
