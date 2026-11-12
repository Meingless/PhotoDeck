//! AppKit's native NSPrintOperation; must be invoked on the main GUI thread.
use super::PrintOutcome;
use crate::types::Result;
use image::{ImageFormat, RgbaImage};
use objc2::{AnyThread, MainThreadMarker, MainThreadOnly};
use objc2_app_kit::{
    NSImage, NSImageView, NSPrintInfo, NSPrintOperation, NSPrintingPaginationMode,
};
use objc2_foundation::{NSCopying, NSData, NSPoint, NSRect, NSSize, NSString};

pub fn print_image(image: &RgbaImage, title: &str) -> Result<PrintOutcome> {
    let mtm = MainThreadMarker::new()
        .ok_or("macOS yazdırma diyaloğu ana arayüz iş parçacığında açılmalı.")?;
    if image.width() == 0 || image.height() == 0 {
        return Err("Yazdırılacak görüntü boş.".into());
    }
    let mut png = std::io::Cursor::new(Vec::new());
    image
        .write_to(&mut png, ImageFormat::Png)
        .map_err(|e| e.to_string())?;
    let data = NSData::with_bytes(png.get_ref());
    let native = NSImage::initWithData(NSImage::alloc(), &data)
        .ok_or("AppKit yazdırma görüntüsünü okuyamadı.")?;
    let view = NSImageView::initWithFrame(
        NSImageView::alloc(mtm),
        NSRect::new(
            NSPoint::new(0., 0.),
            NSSize::new(image.width() as f64, image.height() as f64),
        ),
    );
    view.setImage(Some(&native));
    let info = NSPrintInfo::sharedPrintInfo().copy();
    info.setHorizontalPagination(NSPrintingPaginationMode::Fit);
    info.setVerticalPagination(NSPrintingPaginationMode::Fit);
    info.setHorizontallyCentered(true);
    info.setVerticallyCentered(true);
    let operation = NSPrintOperation::printOperationWithView_printInfo(&view, &info);
    operation.setJobTitle(Some(&NSString::from_str(title)));
    operation.setShowsPrintPanel(true);
    operation.setShowsProgressPanel(true);
    if operation.runOperation() {
        Ok(PrintOutcome::Submitted)
    } else {
        Ok(PrintOutcome::Cancelled)
    }
}
