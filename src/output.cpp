#include "output.h"
#include <QFileInfo>
#include <QPageLayout>
#include <QPdfWriter>
#include <QSaveFile>
#include <cmath>

namespace {
bool bounded(QSize s) {
    return s.width() > 0 && s.height() > 0 && s.width() <= 16384 && s.height() <= 16384 &&
           qint64(s.width()) * s.height() <= 16 * 1024 * 1024;
}
} // namespace
bool RasterExportOptions::valid() const {
    return quality >= 0 && quality <= 100 && dpi >= 72 && dpi <= 1200 && profile >= 0 && profile <= 2 &&
           (size == QSize() || bounded(size));
}
bool PdfExportOptions::valid() const {
    return dpi >= 72 && dpi <= 600 && paper >= 0 && paper <= 2 && std::isfinite(marginMillimeters) &&
           marginMillimeters >= 0 && marginMillimeters <= 50;
}
namespace Output {
QColorSpace profile(int index) {
    switch (index) {
    case 1:
        return QColorSpace(QColorSpace::DisplayP3);
    case 2:
        return QColorSpace(QColorSpace::AdobeRgb);
    default:
        return QColorSpace(QColorSpace::SRgb);
    }
}
QImage workingRgb(const QImage &input) {
    QImage image = input;
    if (image.colorSpace().isValid() && image.colorSpace() != QColorSpace(QColorSpace::SRgb))
        image = image.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
    // The editor's numeric working space is sRGB. Keep legacy pixel-image equality and file behavior;
    // explicit profile embedding belongs to output options.
    if (image.colorSpace().isValid())
        image.setColorSpace(QColorSpace{});
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
QRectF fittedRect(QSizeF image, QRectF page) {
    if (image.width() <= 0 || image.height() <= 0 || page.width() <= 0 || page.height() <= 0)
        return {};
    double scale = qMin(page.width() / image.width(), page.height() / image.height());
    QSizeF fitted = image * scale;
    return QRectF(page.center() - QPointF(fitted.width() / 2, fitted.height() / 2), fitted);
}
bool paintPage(QPainter &p, const QImage &image, QRectF page) {
    if (!p.isActive() || image.isNull() || !page.isValid())
        return false;
    p.save();
    p.setClipRect(page);
    p.fillRect(page, Qt::white);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(fittedRect(image.size(), page), image);
    p.restore();
    return true;
}
QRect printableRect(QPageLayout layout, int resolution) {
    // Full-page QPrinter coordinates start at the paper edge. Retain the
    // layout margins when choosing the destination inside that coordinate space.
    layout.setMode(QPageLayout::StandardMode);
    return layout.paintRectPixels(resolution);
}
QPageLayout pdfLayout(QSize image, const PdfExportOptions &o, QString *error) {
    auto fail = [&](QString message) {
        if (error)
            *error = message;
        return QPageLayout{};
    };
    if (!o.valid() || !bounded(image))
        return fail("PDF ayarı veya görüntü boyutu geçersiz.");
    QSizeF native(image.width() * 25.4 / o.dpi, image.height() * 25.4 / o.dpi);
    if (o.paper == 2 &&
        (native.width() < 1 || native.height() < 1 || native.width() > 2000 || native.height() > 2000))
        return fail("PDF fiziksel boyutu 1–2000 mm sınırında olmalı. DPI veya kağıdı değiştirin.");
    // QPageLayout applies orientation itself. Supply a portrait base size so
    // native landscape images are not rotated a second time.
    if (native.width() > native.height())
        native.transpose();
    QPageSize size = o.paper == 0   ? QPageSize(QPageSize::A4)
                     : o.paper == 1 ? QPageSize(QPageSize::Letter)
                                    : QPageSize(native, QPageSize::Millimeter, "PhotoDeck");
    QPageLayout layout(
        size, o.landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
        QMarginsF(o.marginMillimeters, o.marginMillimeters, o.marginMillimeters, o.marginMillimeters),
        QPageLayout::Millimeter);
    QSizeF paper = layout.fullRect(QPageLayout::Millimeter).size();
    if (2 * o.marginMillimeters >= paper.width() || 2 * o.marginMillimeters >= paper.height())
        return fail("PDF kenar boşluğu kağıt alanını aşıyor.");
    if (!layout.isValid() || layout.paintRect(QPageLayout::Millimeter).width() <= 0 ||
        layout.paintRect(QPageLayout::Millimeter).height() <= 0)
        return fail("PDF kenar boşluğu kağıt alanını aşıyor.");
    return layout;
}
bool writePdf(const QImage &input, const QString &file, const QString &title, const PdfExportOptions &o,
              QString *error) {
    auto fail = [&](QString message) {
        if (error)
            *error = message;
        return false;
    };
    if (!o.valid() || !bounded(input.size()))
        return fail("PDF ayarı veya görüntü boyutu geçersiz.");
    if (QFileInfo(file).suffix().compare("pdf", Qt::CaseInsensitive) != 0)
        return fail("PDF çıktısı için .pdf uzantısını kullanın.");
    const QPageLayout layout = pdfLayout(input.size(), o, error);
    if (!layout.isValid())
        return false;
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly))
        return fail(output.errorString());
    {
        QPdfWriter writer(&output);
        writer.setTitle(title);
        writer.setCreator("PhotoDeck");
        writer.setResolution(o.dpi);
        if (!writer.setPageLayout(layout))
            return fail("PDF kağıt düzeni uygulanamadı.");
        QImage image = input;
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        QPainter painter(&writer);
        if (!paintPage(painter, image, QRectF(0, 0, writer.width(), writer.height())))
            return fail("PDF boyama başlatılamadı.");
        if (!painter.end())
            return fail("PDF boyama tamamlanamadı.");
    }
    if (!output.commit())
        return fail(output.errorString());
    return true;
}
} // namespace Output
