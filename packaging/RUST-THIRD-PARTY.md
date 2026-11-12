# Rust dependencies

PhotoDeck's application code and original artwork are MIT licensed. Cargo.lock records the exact Rust dependency versions. The Rust executable does not link PhotoDeck's old Qt application code.

Primary dependencies include eframe/egui (MIT OR Apache-2.0), winit (Apache-2.0), image (MIT OR Apache-2.0), cosmic-text (MIT OR Apache-2.0), fontdb (MIT), rfd (MIT), arboard (MIT OR Apache-2.0), serde (MIT OR Apache-2.0), tempfile (MIT OR Apache-2.0), tiff (MIT), png (MIT OR Apache-2.0), and lcms2 (MIT). Little CMS is bundled through lcms2-sys's static feature and is MIT licensed. System fonts remain system resources and are not redistributed by PhotoDeck.

Native tablet and print services additionally use octotablet (MIT), ashpd (MIT), and platform bindings from objc2 (MIT) / objc2-app-kit (Zlib OR Apache-2.0 OR MIT) and windows-sys (MIT OR Apache-2.0). Platform-specific libraries are selected by Cargo target configuration.

Exact metadata for every transitive dependency is available with `cargo metadata --locked --format-version 1`. The release packager collects their license/copyright texts from the registry sources and pinned upstream revisions. Explicitly audited packages without their own license file include the declared license text from a pinned SPDX license-data revision together with the original package/author evidence. Every binary package includes the resulting THIRD-PARTY-LICENSES inventory and SHA-256 records. Windows packages are unsigned; macOS packages are ad-hoc signed and are not notarized.

The historical Qt package's separate notice is retained in legacy/qt/packaging/THIRD-PARTY.md and applies only to that older build.
