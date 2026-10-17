#include "mainwindow.h"
#include "outputdialog.h"
#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
void fixture(Document &document) {
    if (!document.create({32, 24})) qFatal("Export fixture create failed");
    Layer top;
    top.name = "Embedded photo";
    top.pixels = QImage(8, 8, QImage::Format_ARGB32_Premultiplied);
    top.pixels.fill(QColor("#2341e0"));
    top.offset = {6, 5};
    QString error;
    if (!document.insertLayer(top, "Photo", &error)) qFatal("Export fixture insert failed");
    if (!document.convertToSmartObject(&error)) qFatal("Export fixture conversion failed");
    document.markSaved();
}
bool drive(QAction *action, const std::function<void(QDialog *)> &handle) {
    bool seen = false, timedOut = false;
    QPointer<QDialog> previous;
    QTimer driver, watchdog;
    driver.setInterval(10);
    watchdog.setSingleShot(true);
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog == previous) return;
        previous = dialog;
        seen = true;
        handle(dialog);
    });
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        timedOut = true;
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
    });
    driver.start();
    watchdog.start(5000);
    action->trigger();
    return seen && !timedOut;
}
QComboBox *format(ExportDialog &dialog) { return dialog.findChild<QComboBox *>("exportFormat"); }
QLineEdit *path(ExportDialog &dialog) { return dialog.findChild<QLineEdit *>("exportPath"); }
void chooseFormat(ExportDialog &dialog, const QByteArray &name) {
    auto *box = format(dialog);
    if (!box) qFatal("Export format control missing");
    const int index = box->findData(name);
    if (index < 0) qFatal("Expected export encoder missing");
    box->setCurrentIndex(index);
}
void chooseOutput(ExportDialog &dialog, const QString &target, const QByteArray &name) {
    chooseFormat(dialog, name);
    auto *location = path(dialog);
    if (!location) qFatal("Export path control missing");
    location->setText(target);
    auto *submit = dialog.findChild<QPushButton *>("exportSubmit");
    if (!submit) qFatal("Export submit control missing");
    submit->click();
}
} // namespace

class ExportDialogTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName("PhotoDeckExportDialogTests");
        QApplication::setOrganizationName("PhotoDeckTests");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QImageReader::setAllocationLimit(128);
    }
    void controlsRatioAndFormatOptions() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ExportDialog dialog({40, 20}, directory.filePath("image.png"));
        QCOMPARE(dialog.objectName(), QString("exportDialog"));
        auto *scroll = dialog.findChild<QScrollArea *>();
        QVERIFY(scroll && scroll->widget());
        QVERIFY(!scroll->widget()->autoFillBackground());
        QVERIFY(!scroll->viewport()->autoFillBackground());
        auto *location = path(dialog);
        auto *browse = dialog.findChild<QPushButton *>("exportBrowse");
        auto *kind = format(dialog);
        auto *note = dialog.findChild<QLabel *>("exportFormatNote");
        auto *submit = dialog.findChild<QPushButton *>("exportSubmit");
        auto *width = dialog.findChild<QSpinBox *>("outputWidth");
        auto *height = dialog.findChild<QSpinBox *>("outputHeight");
        auto *ratio = dialog.findChild<QCheckBox *>("outputRatio");
        auto *quality = dialog.findChild<QSpinBox *>("outputQuality");
        auto *dpi = dialog.findChild<QSpinBox *>("outputDpi");
        auto *profile = dialog.findChild<QComboBox *>("outputProfile");
        auto *embedded = dialog.findChild<QCheckBox *>("embedProfile");
        QVERIFY(location && browse && kind && note && submit && width && height && ratio &&
                quality && dpi && profile && embedded);
        QCOMPARE(kind->currentData().toByteArray(), QByteArray("png"));
        QVERIFY(!note->text().isEmpty());
        QVERIFY(!quality->isEnabled());
        QCOMPARE(width->value(), 40);
        QCOMPARE(height->value(), 20);
        QVERIFY(ratio->isChecked());
        width->setValue(80);
        QCOMPARE(height->value(), 40);
        height->setValue(30);
        QCOMPARE(width->value(), 60);
        ratio->setChecked(false);
        width->setValue(72);
        QCOMPARE(height->value(), 30);
        dpi->setValue(240);
        QCOMPARE(dialog.options().size, QSize(72, 30));
        QCOMPARE(dialog.options().dpi, 240);
        if (kind->findData(QByteArray("jpeg")) >= 0) {
            chooseFormat(dialog, "jpeg");
            QVERIFY(quality->isEnabled());
            quality->setValue(82);
            QCOMPARE(dialog.options().quality, 82);
            QCOMPARE(QFileInfo(dialog.filePath()).suffix().toLower(), QString("jpg"));
        }
        if (kind->findData(QByteArray("bmp")) >= 0) {
            chooseFormat(dialog, "bmp");
            QVERIFY(!quality->isEnabled());
            QVERIFY(!embedded->isEnabled());
            QVERIFY(!dialog.options().embedProfile);
            QVERIFY(!profile->isEnabled());
        }
        chooseFormat(dialog, "png");
        QVERIFY(embedded->isEnabled());
        QVERIFY(!quality->isEnabled());
        QVERIFY(submit->isEnabled());
    }
    void authoritativeFormatNormalizesSuffixes() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ExportDialog dialog({20, 10}, directory.filePath("image.png"));
        auto *location = path(dialog);
        QVERIFY(location);
        auto *kind = format(dialog);
        QVERIFY(kind);
        if (kind->findData(QByteArray("jpeg")) >= 0) {
            chooseFormat(dialog, "jpeg");
            location->setText(directory.filePath("portrait.jpeg"));
            QCOMPARE(dialog.filePath(), directory.filePath("portrait.jpeg"));
            location->setText(directory.filePath("portrait.png"));
            QCOMPARE(dialog.filePath(), directory.filePath("portrait.jpg"));
        }
        if (kind->findData(QByteArray("tiff")) >= 0) {
            chooseFormat(dialog, "tiff");
            location->setText(directory.filePath("negative.tif"));
            QCOMPARE(dialog.filePath(), directory.filePath("negative.tif"));
        }
        chooseFormat(dialog, "png");
        location->setText(directory.filePath("layers.pdeck"));
        QCOMPARE(dialog.filePath(), directory.filePath("layers.pdeck.png"));
        location->setText(directory.filePath("name.webp"));
        QCOMPARE(dialog.filePath(), directory.filePath("name.png"));
    }
    void dialogCancelAndCanceledBrowseDoNotWrite() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString target = directory.filePath("cancel.png");
        ExportDialog dialog({20, 10}, target);
        auto *browse = dialog.findChild<QPushButton *>("exportBrowse");
        QVERIFY(browse);
        const QString before = dialog.filePath();
        bool sawChooser = false;
        QTimer driver, watchdog;
        driver.setInterval(10);
        watchdog.setSingleShot(true);
        QObject::connect(&driver, &QTimer::timeout, [&] {
            if (auto *chooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                sawChooser = true;
                chooser->reject();
            }
        });
        QObject::connect(&watchdog, &QTimer::timeout, [&] {
            if (auto *chooser = qobject_cast<QDialog *>(QApplication::activeModalWidget())) chooser->reject();
        });
        driver.start();
        watchdog.start(5000);
        browse->click();
        driver.stop();
        QVERIFY(sawChooser);
        QCOMPARE(dialog.filePath(), before);
        QVERIFY(!QFile::exists(target));
        dialog.reject();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(!QFile::exists(target));
    }
    void acceptIsReadOnlyAndDecliningOverwritePreservesFile() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString fresh = directory.filePath("fresh.png");
        ExportDialog newTarget({20, 10}, fresh);
        newTarget.accept();
        QCOMPARE(newTarget.result(), int(QDialog::Accepted));
        QVERIFY(!QFile::exists(fresh));
        const QString existing = directory.filePath("existing.png");
        const QByteArray original("Existing file contents");
        QFile file(existing);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original), qint64(original.size()));
        file.close();
        ExportDialog overwrite({20, 10}, existing);
        bool warned = false;
        QTimer driver, watchdog;
        driver.setInterval(10);
        watchdog.setSingleShot(true);
        QObject::connect(&driver, &QTimer::timeout, [&] {
            if (auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                warned = warning->objectName() == "exportOverwriteDialog";
                warning->button(QMessageBox::Cancel)->click();
            }
        });
        QObject::connect(&watchdog, &QTimer::timeout, [&] {
            if (auto *warning = qobject_cast<QDialog *>(QApplication::activeModalWidget())) warning->reject();
        });
        driver.start();
        watchdog.start(5000);
        overwrite.accept();
        driver.stop();
        QVERIFY(warned);
        QVERIFY(overwrite.result() != QDialog::Accepted);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }
    void bothRasterActionsUseSameDialog_data() {
        QTest::addColumn<QString>("actionId");
        QTest::newRow("normal") << QString("export");
        QTest::newRow("advanced") << QString("advancedRaster");
    }
    void bothRasterActionsUseSameDialog() {
        QFETCH(QString, actionId);
        MainWindow window(false, false);
        auto *document = new Document;
        fixture(*document);
        window.addDocument(document);
        window.show();
        auto *action = window.findChild<QAction *>(actionId);
        QVERIFY(action);
        const auto revision = document->revision();
        bool sawExport = false;
        QVERIFY(drive(action, [&](QDialog *dialog) {
            sawExport = dialog->objectName() == "exportDialog" &&
                        dialog->findChild<QLineEdit *>("exportPath") &&
                        dialog->findChild<QSpinBox *>("outputWidth");
            dialog->reject();
        }));
        QVERIFY(sawExport);
        QCOMPARE(document->revision(), revision);
        QVERIFY(!document->dirty);
    }
    void actionsExportPngAndJpegWithoutChangingSmartProject() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MainWindow window(false, false);
        auto *document = new Document;
        fixture(*document);
        window.addDocument(document);
        window.show();
        auto *action = window.findChild<QAction *>("export");
        QVERIFY(action);
        const Layer smart = document->state.layers[1];
        const QImage source = document->composite();
        const auto revision = document->revision();
        const auto history = document->historyLabels();
        const QString png = directory.filePath("composite.png");
        bool submitted = false;
        QVERIFY(drive(action, [&](QDialog *dialog) {
            if (auto *exporter = qobject_cast<ExportDialog *>(dialog)) {
                submitted = true;
                chooseOutput(*exporter, png, "png");
            } else dialog->reject();
        }));
        QVERIFY(submitted);
        QImage pngImage(png);
        QVERIFY(!pngImage.isNull());
        QCOMPARE(pngImage.size(), source.size());
        QCOMPARE(pngImage.pixelColor(8, 8), source.pixelColor(8, 8));
        QCOMPARE(pngImage.pixelColor(1, 1).alpha(), 0);
        if (QImageWriter::supportedImageFormats().contains("jpeg")) {
            const QString jpeg = directory.filePath("composite.jpeg");
            QVERIFY(drive(action, [&](QDialog *dialog) {
                if (auto *exporter = qobject_cast<ExportDialog *>(dialog)) {
                    auto *quality = exporter->findChild<QSpinBox *>("outputQuality");
                    QVERIFY(quality);
                    quality->setValue(83);
                    chooseOutput(*exporter, jpeg, "jpeg");
                } else dialog->reject();
            }));
            QImage jpegImage(jpeg);
            QVERIFY(!jpegImage.isNull());
            QCOMPARE(jpegImage.size(), source.size());
            QVERIFY(jpegImage.pixelColor(8, 8).blue() > jpegImage.pixelColor(8, 8).red());
            QVERIFY(jpegImage.pixelColor(1, 1).red() > 230);
            QVERIFY(jpegImage.pixelColor(1, 1).green() > 230);
            QVERIFY(jpegImage.pixelColor(1, 1).blue() > 230);
        }
        QCOMPARE(document->state.layers.size(), 2);
        QCOMPARE(document->state.layers[1].smartObjectId, smart.smartObjectId);
        QCOMPARE(document->state.layers[1].pixels, smart.pixels);
        QCOMPARE(document->revision(), revision);
        QCOMPARE(document->historyLabels(), history);
        QVERIFY(!document->dirty);
    }
    void pendingTransformIsExportedWithoutApplyingIt() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString target = directory.filePath("preview.png");
        MainWindow window(false, false);
        auto *document = new Document;
        fixture(*document);
        window.addDocument(document);
        window.show();
        auto *canvas = window.currentCanvas();
        auto *action = window.findChild<QAction *>("export");
        QVERIFY(canvas && action);
        const Layer smart = document->state.layers[1];
        const QImage source = document->composite();
        const auto revision = document->revision();
        const auto history = document->historyLabels();
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        const QTransform preview = canvas->transformGeometry();
        Document previewDocument;
        previewDocument.state = canvas->pendingTransformState();
        const QImage expected = previewDocument.composite();
        QVERIFY(expected != source);
        bool submitted = false;
        QVERIFY(drive(action, [&](QDialog *dialog) {
            if (auto *exporter = qobject_cast<ExportDialog *>(dialog)) {
                submitted = true;
                chooseOutput(*exporter, target, "png");
            } else dialog->reject();
        }));
        QVERIFY(submitted);
        const QImage actual(target);
        QVERIFY(!actual.isNull());
        QCOMPARE(actual.size(), expected.size());
        bool checkedDifference = false;
        for (int y = 0; y < expected.height() && !checkedDifference; ++y)
            for (int x = 0; x < expected.width(); ++x)
                if (expected.pixelColor(x, y) != source.pixelColor(x, y)) {
                    QCOMPARE(actual.pixelColor(x, y), expected.pixelColor(x, y));
                    checkedDifference = true;
                    break;
                }
        QVERIFY(checkedDifference);
        QVERIFY(canvas->isTransforming());
        QCOMPARE(canvas->transformGeometry(), preview);
        QCOMPARE(document->revision(), revision);
        QCOMPARE(document->historyLabels(), history);
        QCOMPARE(document->state.layers[1].smartObjectId, smart.smartObjectId);
        QCOMPARE(document->state.layers[1].pixels, smart.pixels);
        QVERIFY(!document->dirty);
        canvas->cancelTransform();
    }
    void cancelKeepsPendingTransformAndSource() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString target = directory.filePath("canceled-preview.png");
        MainWindow window(false, false);
        auto *document = new Document;
        fixture(*document);
        window.addDocument(document);
        window.show();
        auto *canvas = window.currentCanvas();
        auto *action = window.findChild<QAction *>("export");
        QVERIFY(canvas && action);
        const Layer smart = document->state.layers[1];
        const QImage appearance = document->composite();
        const auto revision = document->revision();
        const auto history = document->historyLabels();
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        const QTransform preview = canvas->transformGeometry();
        bool canceled = false;
        QVERIFY(drive(action, [&](QDialog *dialog) {
            if (auto *exporter = qobject_cast<ExportDialog *>(dialog)) {
                auto *location = path(*exporter);
                QVERIFY(location);
                location->setText(target);
                canceled = true;
            }
            dialog->reject();
        }));
        QVERIFY(canceled);
        QVERIFY(!QFile::exists(target));
        QVERIFY(canvas->isTransforming());
        QCOMPARE(canvas->transformGeometry(), preview);
        QCOMPARE(document->revision(), revision);
        QCOMPARE(document->historyLabels(), history);
        QCOMPARE(document->state.layers[1].smartObjectId, smart.smartObjectId);
        QCOMPARE(document->state.layers[1].pixels, smart.pixels);
        QCOMPARE(document->composite(), appearance);
        QVERIFY(!document->dirty);
        canvas->cancelTransform();
    }
    void changedTargetDuringDialogCannotWrite_data() {
        QTest::addColumn<bool>("changeTab");
        QTest::newRow("revision") << false;
        QTest::newRow("tab") << true;
    }
    void changedTargetDuringDialogCannotWrite() {
        QFETCH(bool, changeTab);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString target = directory.filePath("blocked.png");
        MainWindow window(false, false);
        auto *document = new Document;
        fixture(*document);
        window.addDocument(document);
        window.show();
        auto *action = window.findChild<QAction *>("export");
        QVERIFY(action);
        bool sawDialog = false;
        QVERIFY(drive(action, [&](QDialog *dialog) {
            auto *exporter = qobject_cast<ExportDialog *>(dialog);
            if (!exporter) {
                dialog->reject();
                return;
            }
            sawDialog = true;
            if (changeTab) {
                auto *other = new Document;
                QVERIFY(other->create({16, 12}));
                window.addDocument(other);
            } else {
                QString error;
                QVERIFY2(document->nudgeLayer({1, 0}, 0, &error), qPrintable(error));
            }
            chooseOutput(*exporter, target, "png");
        }));
        QVERIFY(sawDialog);
        QVERIFY(!QFile::exists(target));
    }
};

QTEST_MAIN(ExportDialogTests)
#include "test_export_dialog.moc"
