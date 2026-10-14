#include "mainwindow.h"
#include "outputdialog.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QImageReader>
#include <QLineEdit>
#include <QMessageBox>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool modal(QAction *action, const std::function<bool(QDialog *)> &handler) {
    QTimer driver, timeout;
    driver.setInterval(10);
    timeout.setSingleShot(true);
    QPointer<QDialog> last;
    bool handled = false;
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d || d == last)
            return;
        last = d;
        handled |= handler(d);
    });
    QObject::connect(&timeout, &QTimer::timeout, [&] {
        if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            d->reject();
    });
    driver.start();
    timeout.start(5000);
    action->trigger();
    driver.stop();
    timeout.stop();
    return handled;
}
void choose(QFileDialog *dialog, const QString &file) {
    dialog->setDirectory(QFileInfo(file).absolutePath());
    dialog->selectFile(file);
    if (auto *edit = dialog->findChild<QLineEdit *>("fileNameEdit"))
        edit->setText(file);
    QMetaObject::invokeMethod(dialog, "accept");
}
} // namespace
class OutputTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }
    void layoutFitsCentersAndPreservesRatio() {
        QCOMPARE(Output::fittedRect({200, 100}, {10, 20, 300, 300}), QRectF(10, 95, 300, 150));
        QCOMPARE(Output::fittedRect({100, 200}, {10, 20, 300, 300}), QRectF(85, 20, 150, 300));
        QVERIFY(Output::fittedRect({0, 10}, {0, 0, 20, 20}).isEmpty());
        QImage paper(400, 400, QImage::Format_ARGB32_Premultiplied);
        paper.fill(Qt::black);
        QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(255, 0, 0, 128));
        QPainter p(&paper);
        QVERIFY(Output::paintPage(p, image, {50, 50, 300, 300}));
        p.end();
        QCOMPARE(paper.pixelColor(5, 5), QColor(Qt::black));
        QCOMPARE(paper.pixelColor(60, 60), QColor(Qt::white));
        QColor center = paper.pixelColor(200, 200);
        QCOMPARE(center.alpha(), 255);
        QVERIFY(center.red() == 255 && qAbs(center.green() - 127) <= 1);
    }
    void taggedRgbImportAndInsertNormalizeToWorkingSrgb() {
        QImage source(20, 10, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(150, 80, 40, 180));
        source.setColorSpace(QColorSpace(QColorSpace::AdobeRgb));
        QImage expected = source.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
        expected.setColorSpace({});
        QTemporaryDir temp;
        QString file = temp.filePath("adobe.png"), error;
        QVERIFY(source.save(file));
        QImage image = Document::readImage(file, &error);
        QCOMPARE(image.size(), expected.size());
        QCOMPARE(image.format(), QImage::Format_ARGB32_Premultiplied);
        QVERIFY(!image.colorSpace().isValid());
        // PNG stores straight alpha; its decode/color conversion can round one channel
        // differently from converting the original premultiplied buffer in memory.
        const QColor actual = image.pixelColor(2, 2), reference = expected.pixelColor(2, 2);
        QCOMPARE(actual.alpha(), reference.alpha());
        QVERIFY(qAbs(actual.red() - reference.red()) <= 2);
        QVERIFY(qAbs(actual.green() - reference.green()) <= 2);
        QVERIFY(qAbs(actual.blue() - reference.blue()) <= 2);
        QVERIFY(image.pixelColor(2, 2) != source.pixelColor(2, 2));
        Document d;
        d.create({20, 10});
        Layer layer;
        layer.pixels = source;
        QVERIFY(d.insertLayer(layer, "Profile input", &error));
        QCOMPARE(d.state.layers[d.state.active].pixels, expected);
    }
    void rasterSizeDpiAndProfiles_data() {
        QTest::addColumn<int>("profile");
        for (int p = 0; p < 3; ++p)
            QTest::newRow(qPrintable(QString::number(p))) << p;
    }
    void rasterSizeDpiAndProfiles() {
        QFETCH(int, profile);
        Document d;
        d.create({40, 20});
        d.beginEdit();
        d.state.layers[0].pixels.fill(QColor(150, 80, 40, 128));
        d.commit("Color");
        d.markSaved();
        const QImage original = d.composite();
        QTemporaryDir temp;
        QString file = temp.filePath("profile.png"), e;
        RasterExportOptions o;
        o.size = {20, 10};
        o.dpi = 300;
        o.profile = profile;
        o.embedProfile = true;
        QVERIFY(d.exportRaster(file, o, &e));
        QImage output(file);
        QCOMPARE(output.size(), QSize(20, 10));
        QCOMPARE(output.colorSpace(), Output::profile(profile));
        QVERIFY(qAbs(output.dotsPerMeterX() - qRound(300 / 0.0254)) <= 1);
        QCOMPARE(output.pixelColor(5, 5).alpha(), 128);
        QImage normal = Output::workingRgb(output);
        QColor a = normal.pixelColor(5, 5), b = original.pixelColor(5, 5);
        QVERIFY(qAbs(a.red() - b.red()) <= 3);
        QVERIFY(qAbs(a.green() - b.green()) <= 3);
        QVERIFY(qAbs(a.blue() - b.blue()) <= 3);
        QCOMPARE(d.composite(), original);
        QVERIFY(!d.dirty);
        QCOMPARE(d.state.size, QSize(40, 20));
    }
    void jpegAndTiffEmbedSupportedRgbProfile() {
        Document d;
        d.create({20, 10});
        d.state.layers[0].pixels.fill(QColor(80, 160, 40, 128));
        QTemporaryDir temp;
        QString e;
        RasterExportOptions o;
        o.profile = 1;
        o.embedProfile = true;
        o.dpi = 240;
        for (QString ext : {"jpg", "tiff", "webp"}) {
            QString file = temp.filePath("profile." + ext);
            QVERIFY2(d.exportRaster(file, o, &e), qPrintable(e));
            QImage image(file);
            QCOMPARE(image.colorSpace(), Output::profile(1));
            QCOMPARE(image.size(), QSize(20, 10));
            if (ext == "jpg")
                QCOMPARE(image.pixelColor(3, 3).alpha(), 255);
        }
    }
    void invalidRasterOptionsProtectExistingFile() {
        QTemporaryDir temp;
        QString file = temp.filePath("keep.png"), e;
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("keep");
        f.close();
        Document d;
        d.create({20, 10}, true);
        RasterExportOptions o;
        o.size = {20000, 20000};
        QVERIFY(!d.exportRaster(file, o, &e));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("keep"));
        f.close();
        o = {};
        o.dpi = 0;
        QVERIFY(!d.exportRaster(file, o, &e));
        o = {};
        o.embedProfile = true;
        QVERIFY(!d.exportRaster(temp.filePath("unsupported.bmp"), o, &e));
        QVERIFY(!d.exportRaster(temp.filePath("project.pdeck"), RasterExportOptions{}, &e));
    }
    void pdfHeaderPageAndAtomicFailure() {
        Document d;
        d.create({200, 100}, true);
        QTemporaryDir temp;
        QString file = temp.filePath("image.pdf"), e;
        PdfExportOptions o;
        QVERIFY2(d.exportPdf(file, o, &e), qPrintable(e));
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray bytes = f.readAll();
        f.close();
        QVERIFY(bytes.startsWith("%PDF-"));
        QVERIFY(bytes.contains("/Type /Page"));
        QVERIFY(bytes.contains("/Subtype /Image"));
        QVERIFY(bytes.contains("%%EOF"));
        o.marginMillimeters = 100;
        QVERIFY(!d.exportPdf(file, o, &e));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), bytes);
        f.close();
        o = {};
        o.paper = 2;
        o.dpi = 600;
        o.marginMillimeters = 50;
        QVERIFY(!d.exportPdf(file, o, &e));
        QVERIFY(!d.exportPdf(temp.filePath("wrong.pdeck"), PdfExportOptions{}, &e));
    }
    void pdfMarginsAreMillimetersAndFitInsidePaper() {
        PdfExportOptions o;
        o.landscape = true;
        QString error;
        const QPageLayout layout = Output::pdfLayout({600, 300}, o, &error);
        QVERIFY2(layout.isValid(), qPrintable(error));
        QCOMPARE(layout.margins(QPageLayout::Millimeter), QMarginsF(10, 10, 10, 10));
        QCOMPARE(layout.paintRect(QPageLayout::Millimeter), QRectF(10, 10, 277, 190));
        o.paper = 2;
        o.marginMillimeters = 15;
        QVERIFY(!Output::pdfLayout({600, 300}, o, &error).isValid());
    }
    void printerPdfDeviceUsesSameFitRenderer() {
        QTemporaryDir temp;
        QString file = temp.filePath("printer.pdf");
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(file);
        printer.setPageSize(QPageSize(QPageSize::A4));
        printer.setFullPage(true);
        QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::red);
        QPainter p(&printer);
        QVERIFY(Output::paintPage(p, image, printer.pageLayout().paintRectPixels(printer.resolution())));
        QVERIFY(p.end());
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().startsWith("%PDF-"));
    }
    void nativePdfPhysicalSizeRespectsOrientation_data() {
        QTest::addColumn<bool>("landscape");
        QTest::newRow("portrait") << false;
        QTest::newRow("landscape") << true;
    }
    void nativePdfPhysicalSizeRespectsOrientation() {
        QFETCH(bool, landscape);
        QTemporaryDir temp;
        Document d;
        d.create({600, 300});
        PdfExportOptions o;
        o.paper = 2;
        o.dpi = 300;
        o.marginMillimeters = 0;
        o.landscape = landscape;
        const QString path = temp.filePath("physical.pdf");
        QString error;
        QVERIFY2(d.exportPdf(path, o, &error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto box = QRegularExpression("/MediaBox\\s*\\[\\s*0\\s+0\\s+([0-9.]+)\\s+([0-9.]+)")
                             .match(QString::fromLatin1(file.readAll()));
        QVERIFY(box.hasMatch());
        QVERIFY(qAbs(box.captured(1).toDouble() - (landscape ? 144 : 72)) <= 1);
        QVERIFY(qAbs(box.captured(2).toDouble() - (landscape ? 72 : 144)) <= 1);
    }
    void rasterUiAspectCancelAndRealFileExport() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({40, 20}, true);
        w.addDocument(d);
        QTemporaryDir temp;
        QString path = temp.filePath("ui.png");
        auto *action = w.findChild<QAction *>("advancedRaster");
        QVERIFY(action);
        QSize cancelledSize;
        QVERIFY(modal(action, [&](QDialog *dialog) {
            auto *output = qobject_cast<RasterOutputDialog *>(dialog);
            if (!output)
                return false;
            output->findChild<QSpinBox *>("outputWidth")->setValue(20);
            cancelledSize = output->options().size;
            output->reject();
            return true;
        }));
        QCOMPARE(cancelledSize, QSize(20, 10));
        int stage = 0;
        QVERIFY(modal(action, [&](QDialog *dialog) {
            if (auto *output = qobject_cast<RasterOutputDialog *>(dialog)) {
                output->findChild<QSpinBox *>("outputWidth")->setValue(20);
                output->accept();
                ++stage;
                return true;
            }
            if (auto *file = qobject_cast<QFileDialog *>(dialog)) {
                choose(file, path);
                ++stage;
                return true;
            }
            return false;
        }));
        QCOMPARE(stage, 2);
        QCOMPARE(QImage(path).size(), QSize(20, 10));
        QVERIFY(!d->dirty);
        w.close();
    }
    void pdfUiExportsWithoutChangingDocument() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({40, 20}, true);
        w.addDocument(d);
        QTemporaryDir temp;
        QString path = temp.filePath("ui.pdf");
        auto *action = w.findChild<QAction *>("exportPdf");
        QVERIFY(action);
        int stage = 0;
        QVERIFY(modal(action, [&](QDialog *dialog) {
            if (auto *output = qobject_cast<PdfOutputDialog *>(dialog)) {
                output->findChild<QComboBox *>("pdfPaper")->setCurrentIndex(1);
                output->accept();
                ++stage;
                return true;
            }
            if (auto *file = qobject_cast<QFileDialog *>(dialog)) {
                choose(file, path);
                ++stage;
                return true;
            }
            return false;
        }));
        QCOMPARE(stage, 2);
        QVERIFY(QFileInfo(path).size() > 100);
        QVERIFY(!d->dirty);
        QVERIFY(w.findChild<QAction *>("print")->isEnabled());
        w.close();
    }
};
QTEST_MAIN(OutputTests)
#include "test_output.moc"
