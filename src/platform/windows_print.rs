//! Win32 common print dialog and one-page GDI raster output.
use super::PrintOutcome;
use crate::types::Result;
use image::RgbaImage;
use windows_sys::Win32::{
    Foundation::GlobalFree, Graphics::Gdi::*, Storage::Xps::*, UI::Controls::Dialogs::*,
};

pub fn print_image(image: &RgbaImage, title: &str) -> Result<PrintOutcome> {
    if image.width() == 0
        || image.height() == 0
        || image.width() > i32::MAX as u32
        || image.height() > i32::MAX as u32
    {
        return Err("Yazdırılacak görüntü boyutu geçersiz.".into());
    }
    // A top-down 32-bit DIB, composited onto paper white (GDI ignores alpha).
    let mut pixels = Vec::with_capacity(image.as_raw().len());
    for p in image.pixels() {
        for channel in [p[2], p[1], p[0]] {
            pixels.push(
                ((channel as u32 * p[3] as u32 + 255 * (255 - p[3] as u32) + 127) / 255) as u8,
            );
        }
        pixels.push(0);
    }
    let title: Vec<u16> = title
        .replace('\0', "")
        .encode_utf16()
        .chain(Some(0))
        .collect();
    // All native allocations and the device context are released on every exit.
    unsafe {
        let mut dialog: PRINTDLGW = std::mem::zeroed();
        dialog.lStructSize = std::mem::size_of::<PRINTDLGW>() as u32;
        dialog.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
        dialog.nCopies = 1;
        let accepted = PrintDlgW(&mut dialog) != 0;
        let error = if accepted { 0 } else { CommDlgExtendedError() };
        if !dialog.hDevMode.is_null() {
            GlobalFree(dialog.hDevMode);
        }
        if !dialog.hDevNames.is_null() {
            GlobalFree(dialog.hDevNames);
        }
        if !accepted {
            if !dialog.hDC.is_null() {
                DeleteDC(dialog.hDC);
            }
            return if error == 0 {
                Ok(PrintOutcome::Cancelled)
            } else {
                Err(format!("Yazıcı diyaloğu açılamadı: {error:#x}"))
            };
        }
        let dc = dialog.hDC;
        let result = (|| {
            if dc.is_null() {
                return Err("Yazıcı bağlamı alınamadı.".into());
            }
            let mut info: DOCINFOW = std::mem::zeroed();
            info.cbSize = std::mem::size_of::<DOCINFOW>() as i32;
            info.lpszDocName = title.as_ptr();
            if StartDocW(dc, &info) <= 0 {
                return Err("Yazdırma işi başlatılamadı.".into());
            }
            let rendered = (|| {
                if StartPage(dc) <= 0 {
                    return false;
                }
                let pw = GetDeviceCaps(dc, HORZRES as i32);
                let ph = GetDeviceCaps(dc, VERTRES as i32);
                if pw <= 0 || ph <= 0 {
                    return false;
                }
                let scale =
                    (pw as f64 / image.width() as f64).min(ph as f64 / image.height() as f64);
                let w = (image.width() as f64 * scale).round() as i32;
                let h = (image.height() as f64 * scale).round() as i32;
                let mut bitmap: BITMAPINFO = std::mem::zeroed();
                bitmap.bmiHeader.biSize = std::mem::size_of::<BITMAPINFOHEADER>() as u32;
                bitmap.bmiHeader.biWidth = image.width() as i32;
                bitmap.bmiHeader.biHeight = -(image.height() as i32);
                bitmap.bmiHeader.biPlanes = 1;
                bitmap.bmiHeader.biBitCount = 32;
                bitmap.bmiHeader.biCompression = BI_RGB;
                SetStretchBltMode(dc, HALFTONE);
                let lines = StretchDIBits(
                    dc,
                    (pw - w) / 2,
                    (ph - h) / 2,
                    w,
                    h,
                    0,
                    0,
                    image.width() as i32,
                    image.height() as i32,
                    pixels.as_ptr().cast(),
                    &bitmap,
                    DIB_RGB_COLORS,
                    SRCCOPY,
                );
                lines != 0 && lines != GDI_ERROR as i32 && EndPage(dc) > 0
            })();
            if !rendered {
                AbortDoc(dc);
                return Err("Yazıcıya sayfa gönderilemedi.".into());
            }
            if EndDoc(dc) <= 0 {
                AbortDoc(dc);
                return Err("Yazdırma işi tamamlanamadı.".into());
            }
            Ok(PrintOutcome::Submitted)
        })();
        if !dc.is_null() {
            DeleteDC(dc);
        }
        result
    }
}
