//! Native OS services. Unsupported backends return an explicit error; they never
//! silently substitute a PDF viewer or pretend mouse data came from a tablet.
mod printing;
pub mod tablet;
pub use printing::{PrintOutcome, print_document, print_pdf};

#[cfg(target_os = "macos")]
mod macos_print;
#[cfg(target_os = "windows")]
mod windows_print;
