#pragma once
#include <QColorSpace>
#include <QImage>
#include <QPageSize>
#include <QPageLayout>
#include <QPainter>

struct RasterExportOptions {
    int quality = 95, dpi = 96;
    QSize size; // Empty preserves document size.
    bool embedProfile = false;
    int profile = 0; // sRGB, Display P3, Adobe RGB.
    bool valid() const;
};
struct PdfExportOptions {
    int dpi = 300, paper = 0; // A4, Letter, native image physical size.
    bool landscape = false;
    double marginMillimeters = 10;
    bool valid() const;
};
namespace Output {
QColorSpace profile(int index);
QImage workingRgb(const QImage &image);
QRectF fittedRect(QSizeF image, QRectF page);
QRect printableRect(QPageLayout layout, int resolution);
bool paintPage(QPainter &painter, const QImage &image, QRectF page);
QPageLayout pdfLayout(QSize image, const PdfExportOptions &options, QString *error);
bool writePdf(const QImage &image, const QString &file, const QString &title,
              const PdfExportOptions &options, QString *error);
} // namespace Output
