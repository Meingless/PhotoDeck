use crate::types::Result;
use std::path::Path;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PrintOutcome {
    Submitted,
    Cancelled,
}

/// Show the desktop's native print dialog, then submit the PDF using the portal.
/// Run on a worker: the dialog response may wait indefinitely for the user.
#[cfg(target_os = "linux")]
pub fn print_pdf(path: &Path, title: &str) -> Result<PrintOutcome> {
    use ashpd::desktop::print::{PreparePrintOptions, PrintOptions, PrintProxy};
    use std::os::fd::AsFd;
    let file = std::fs::File::open(path).map_err(|e| format!("Yazdırılacak PDF açılamadı: {e}"))?;
    let result: ashpd::Result<()> = pollster::block_on(async {
        let proxy = PrintProxy::new().await?;
        let prepared = proxy
            .prepare_print(
                None,
                title,
                Default::default(),
                Default::default(),
                PreparePrintOptions::default().set_modal(true),
            )
            .await?
            .response()?;
        proxy
            .print(
                None,
                title,
                &file.as_fd(),
                PrintOptions::default()
                    .set_token(prepared.token)
                    .set_modal(true),
            )
            .await?
            .response()?;
        Ok(())
    });
    match result {
        Ok(()) => Ok(PrintOutcome::Submitted),
        Err(ashpd::Error::Response(ashpd::desktop::ResponseError::Cancelled)) => {
            Ok(PrintOutcome::Cancelled)
        }
        Err(e) => Err(format!(
            "Yerel yazdırma portalı başarısız (xdg-desktop-portal Print desteği gerekir): {e}"
        )),
    }
}
#[cfg(not(target_os = "linux"))]
pub fn print_pdf(_path: &Path, _title: &str) -> Result<PrintOutcome> {
    Err("Bu platformda yazdırma için raster belge adaptörünü kullanın.".into())
}

/// Native printing. Linux uses the already paginated PDF; Windows/macOS use
/// the flattened image and the printer's page settings. macOS requires the GUI
/// thread; Linux should run on a worker to keep its parent UI responsive.
pub fn print_document(path: &Path, image: &image::RgbaImage, title: &str) -> Result<PrintOutcome> {
    #[cfg(target_os = "linux")]
    {
        let _ = image;
        print_pdf(path, title)
    }
    #[cfg(target_os = "windows")]
    {
        let _ = path;
        super::windows_print::print_image(image, title)
    }
    #[cfg(target_os = "macos")]
    {
        let _ = path;
        super::macos_print::print_image(image, title)
    }
    #[cfg(not(any(target_os = "linux", target_os = "windows", target_os = "macos")))]
    {
        let _ = (path, image, title);
        Err("Yerel yazdırma bu platformda desteklenmiyor.".into())
    }
}
