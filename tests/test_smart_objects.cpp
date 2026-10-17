#include "document.h"
#include "mainwindow.h"
#include <QAbstractButton>
#include <QAction>
#include <QBuffer>
#include <QDataStream>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
QImage image(QSize size, QColor color) {
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(color);
    return result;
}
void fixture(Document &document) {
    if (!document.create({48, 36})) qFatal("Smart fixture create failed");
    document.beginEdit();
    Layer &layer = document.state.layers[0];
    layer.name = "Photo";
    layer.pixels = image({8, 6}, Qt::red);
    QPainter painter(&layer.pixels);
    painter.fillRect(4, 0, 4, 6, Qt::blue);
    painter.end();
    layer.offset = {11, 9};
    layer.mask = image(layer.pixels.size(), Qt::white);
    layer.mask.setPixelColor(0, 0, Qt::black);
    layer.opacity = .75;
    layer.brightness = 7;
    layer.geometry = QTransform::fromScale(1.5, 1.5);
    document.commit("Fixture");
    document.markSaved();
}
int storedVersion(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_2);
    quint32 magic = 0, version = 0;
    stream >> magic >> version;
    return magic == 0x50444543 && stream.status() == QDataStream::Ok ? int(version) : 0;
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
void choose(QFileDialog *dialog, const QString &path) {
    dialog->setDirectory(QFileInfo(path).absolutePath());
    dialog->selectFile(path);
    if (auto *name = dialog->findChild<QLineEdit *>("fileNameEdit")) {
        name->setFocus();
        name->selectAll();
        name->setText(path);
    }
    QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
}
} // namespace

class SmartObjectTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName("PhotoDeckSmartObjectTests");
        QApplication::setOrganizationName("PhotoDeckTests");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QImageReader::setAllocationLimit(128);
    }
    void conversionAndReplacementAreSingleUndoableSteps() {
        Document document;
        fixture(document);
        const Layer original = document.state.layers[0];
        const QImage appearance = document.composite();
        const int initialHistory = document.historyLabels().size();
        QString error;
        QVERIFY2(document.convertToSmartObject(&error), qPrintable(error));
        const Layer converted = document.state.layers[0];
        QVERIFY(converted.isSmartObject());
        QVERIFY(!converted.smartObjectId.isNull());
        QCOMPARE(converted.smartObjectSize, original.pixels.size());
        QCOMPARE(converted.sourceSize(), original.pixels.size());
        QCOMPARE(converted.pixels, original.pixels);
        QCOMPARE(converted.mask, original.mask);
        QCOMPARE(document.composite(), appearance);
        QCOMPARE(document.historyLabels().size(), initialHistory + 1);
        QVERIFY(document.dirty);
        document.undo();
        QVERIFY(!document.state.layers[0].isSmartObject());
        QCOMPARE(document.composite(), appearance);
        QVERIFY(!document.dirty);
        document.redo();
        QCOMPARE(document.state.layers[0].smartObjectId, converted.smartObjectId);
        const int beforeReplacement = document.historyLabels().size();
        QImage native = image({3, 2}, QColor("#32c875"));
        QVERIFY2(document.replaceSmartObjectContents(native, &error), qPrintable(error));
        const Layer &replaced = document.state.layers[0];
        QCOMPARE(replaced.smartObjectId, converted.smartObjectId);
        QCOMPARE(replaced.smartObjectSize, original.pixels.size());
        QCOMPARE(replaced.sourceSize(), original.pixels.size());
        QCOMPARE(replaced.pixels, native);
        QCOMPARE(replaced.geometry, original.geometry);
        QCOMPARE(replaced.offset, original.offset);
        QCOMPARE(replaced.mask, original.mask);
        QCOMPARE(replaced.opacity, original.opacity);
        QCOMPARE(replaced.brightness, original.brightness);
        QCOMPARE(replaced.rendered().size(), original.transformedBounds().size());
        QCOMPARE(document.historyLabels().size(), beforeReplacement + 1);
        const QImage replacementAppearance = document.composite();
        QVERIFY(replacementAppearance != appearance);
        QImage retained = native;
        native.fill(Qt::yellow);
        QCOMPARE(document.state.layers[0].pixels, retained);
        const int unchangedHistory = document.historyLabels().size();
        QVERIFY(!document.replaceSmartObjectContents(retained, &error));
        QCOMPARE(document.historyLabels().size(), unchangedHistory);
        document.undo();
        QCOMPARE(document.state.layers[0].pixels, converted.pixels);
        QCOMPARE(document.state.layers[0].smartObjectId, converted.smartObjectId);
        QCOMPARE(document.composite(), appearance);
        document.redo();
        QCOMPARE(document.state.layers[0].pixels, retained);
        QCOMPARE(document.composite(), replacementAppearance);
    }
    void duplicateAndRasterizeHandleIdentity() {
        Document document;
        fixture(document);
        QString error;
        QVERIFY2(document.convertToSmartObject(&error), qPrintable(error));
        const QUuid originalId = document.state.layers[0].smartObjectId;
        QVERIFY2(document.duplicate(&error), qPrintable(error));
        QCOMPARE(document.state.layers.size(), 2);
        QVERIFY(document.state.layers[1].isSmartObject());
        QVERIFY(document.state.layers[1].smartObjectId != originalId);
        QCOMPARE(document.state.layers[1].pixels, document.state.layers[0].pixels);
        const QImage appearance = document.composite();
        const Layer before = document.state.layers[1];
        QVERIFY2(document.rasterizeLayer(&error), qPrintable(error));
        const Layer &raster = document.state.layers[1];
        QVERIFY(!raster.isSmartObject());
        QVERIFY(raster.smartObjectId.isNull());
        QVERIFY(raster.smartObjectSize.isEmpty());
        QVERIFY(raster.geometry.isIdentity());
        QVERIFY(raster.mask.isNull());
        QCOMPARE(document.composite(), appearance);
        document.undo();
        QCOMPARE(document.state.layers[1].smartObjectId, before.smartObjectId);
        QCOMPARE(document.state.layers[1].pixels, before.pixels);
    }
    void plainReplacementKeepsNativeDetailWhenGeometryRestoresScale() {
        Document document;
        QVERIFY(document.create({20, 20}));
        document.beginEdit();
        Layer &layer = document.state.layers[0];
        layer.pixels = image({4, 4}, Qt::black);
        layer.geometry = QTransform::fromScale(2, 2);
        document.commit("Small source");
        QString error;
        QVERIFY2(document.convertToSmartObject(&error), qPrintable(error));
        QImage highFrequency({8, 8}, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                highFrequency.setPixelColor(x, y, (x + y) % 2 ? Qt::yellow : Qt::blue);
        QVERIFY2(document.replaceSmartObjectContents(highFrequency, &error), qPrintable(error));
        const Layer &smart = document.state.layers[0];
        QCOMPARE(smart.sourceSize(), QSize(4, 4));
        QCOMPARE(smart.pixels, highFrequency);
        QCOMPARE(smart.rendered().size(), QSize(8, 8));
        QCOMPARE(smart.rendered(), highFrequency);
    }
    void selectionCopyIsRasterAndCutIsRejected() {
        Document document;
        fixture(document);
        QString error;
        QVERIFY2(document.convertToSmartObject(&error), qPrintable(error));
        QVERIFY(document.setSelection(Selection(11, 9, 10, 9)));
        const Layer original = document.state.layers[0];
        const quint64 revision = document.revision();
        const int history = document.historyLabels().size();
        State pending;
        QVERIFY2(document.selectionCopyState(pending, &error), qPrintable(error));
        QCOMPARE(pending.layers.size(), 2);
        QVERIFY(!pending.layers[pending.active].isSmartObject());
        QCOMPARE(document.state.layers.size(), 1);
        QCOMPARE(document.state.layers[0].pixels, original.pixels);
        QCOMPARE(document.revision(), revision);
        QVERIFY(!document.layerViaSelection(true, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(document.historyLabels().size(), history);
        QCOMPARE(document.state.layers[0].smartObjectId, original.smartObjectId);
    }
    void protectedOperationsAndRejectionsAreInert() {
        Document document;
        fixture(document);
        QString error;
        document.state.layers[0].locked = true;
        QVERIFY(!document.convertToSmartObject(&error));
        document.state.layers[0].locked = false;
        document.state.layers[0].visible = false;
        QVERIFY(!document.convertToSmartObject(&error));
        document.state.layers[0].visible = true;
        document.state.layers[0].text = "Live text";
        QVERIFY(!document.convertToSmartObject(&error));
        document.state.layers[0].text.clear();
        document.beginEdit();
        QVERIFY(!document.convertToSmartObject(&error));
        document.cancelEdit();
        QVERIFY2(document.convertToSmartObject(&error), qPrintable(error));
        const Layer before = document.state.layers[0];
        const quint64 revision = document.revision();
        const QStringList history = document.historyLabels();
        QVERIFY(!document.resize({24, 18}, &error));
        QVERIFY(!document.rotate(true, &error));
        QVERIFY(!document.flip(true, &error));
        QVERIFY(!document.replaceSmartObjectContents({}, &error));
        QCOMPARE(document.revision(), revision);
        QCOMPARE(document.historyLabels(), history);
        QCOMPARE(document.state.layers[0].pixels, before.pixels);
        QCOMPARE(document.state.layers[0].smartObjectId, before.smartObjectId);
        QCOMPARE(document.state.layers[0].smartObjectSize, before.smartObjectSize);
        QVERIFY2(document.canvasSize({52, 40}, {0, 0}, &error), qPrintable(error));
        QCOMPARE(document.state.layers[0].smartObjectId, before.smartObjectId);
        QCOMPARE(document.state.layers[0].pixels, before.pixels);
        QVERIFY2(document.crop({0, 0, 48, 36}, &error), qPrintable(error));
        QCOMPARE(document.state.layers[0].smartObjectId, before.smartObjectId);
        QCOMPARE(document.state.layers[0].pixels, before.pixels);
    }
    void mergeOperationsDoNotBakeSmartLayers() {
        Document document;
        fixture(document);
        QString error;
        QVERIFY2(document.convertToSmartObject(&error), qPrintable(error));
        QVERIFY2(document.duplicate(&error), qPrintable(error));
        const QImage appearance = document.composite();
        const QStringList history = document.historyLabels();
        const QUuid lower = document.state.layers[0].smartObjectId;
        const QUuid upper = document.state.layers[1].smartObjectId;
        QVERIFY(!document.mergeDown(&error));
        QVERIFY(!document.mergeVisible(&error));
        QVERIFY(!document.flatten(&error));
        QCOMPARE(document.historyLabels(), history);
        QCOMPARE(document.state.layers.size(), 2);
        QCOMPARE(document.state.layers[0].smartObjectId, lower);
        QCOMPARE(document.state.layers[1].smartObjectId, upper);
        QCOMPARE(document.composite(), appearance);
    }
    void clippedSmartReplacementAndMaskPaintingPreserveEmbeddedPixels() {
        Document document;
        QVERIFY(document.create({32, 24}));
        document.beginEdit();
        Layer &base = document.state.layers[0];
        base.pixels = image({10, 8}, Qt::white);
        base.offset = {4, 4};
        Layer child;
        child.name = "Clipped object";
        child.pixels = image({6, 4}, Qt::red);
        child.offset = {7, 6};
        document.state.layers.append(child);
        document.state.active = 1;
        document.commit("Clipped fixture");
        QString error;
        QVERIFY2(document.setClipping(true, &error), qPrintable(error));
        QVERIFY2(document.convertToSmartObject(&error), qPrintable(error));
        const QUuid identity = document.state.layers[1].smartObjectId;
        const QImage native = image({3, 2}, Qt::blue);
        QVERIFY2(document.replaceSmartObjectContents(native, &error), qPrintable(error));
        QVERIFY(document.state.layers[1].clipped);
        QCOMPARE(document.state.layers[1].smartObjectId, identity);
        QCOMPARE(document.state.layers[1].pixels, native);
        QCOMPARE(document.state.layers[1].sourceSize(), QSize(6, 4));
        QVERIFY2(document.addMask(&error), qPrintable(error));
        QVERIFY(document.editingMask);
        Canvas canvas(&document);
        canvas.resize(480, 360);
        canvas.showRulers = false;
        canvas.show();
        canvas.actualSize();
        canvas.setTool(Canvas::Brush);
        canvas.foreground = Qt::black;
        const QImage pixelsBefore = document.state.layers[1].pixels;
        const QImage maskBefore = document.state.layers[1].mask;
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, canvas.toWidget({9, 7}).toPoint());
        QCOMPARE(document.state.layers[1].pixels, pixelsBefore);
        QVERIFY(document.state.layers[1].mask != maskBefore);
        QCOMPARE(document.state.layers[1].smartObjectId, identity);
        QVERIFY(document.state.layers[1].clipped);
    }
    void nativeAndRecoveryVersionsPreserveSmartContents() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString error;
        Document ordinary;
        fixture(ordinary);
        const QString v5 = dir.filePath("ordinary.pdeck");
        QVERIFY2(ordinary.save(v5, &error), qPrintable(error));
        QCOMPARE(storedVersion(v5), 5);
        QVERIFY(ordinary.setSelection(Selection(3, 4, 9, 8)));
        QVERIFY(ordinary.saveSelection("Subject", &error));
        const QString v6 = dir.filePath("selection.pdeck");
        QVERIFY2(ordinary.save(v6, &error), qPrintable(error));
        QCOMPARE(storedVersion(v6), 6);
        Document smart;
        fixture(smart);
        QVERIFY2(smart.convertToSmartObject(&error), qPrintable(error));
        const QImage native = image({3, 2}, QColor("#58c7df"));
        QVERIFY2(smart.replaceSmartObjectContents(native, &error), qPrintable(error));
        QVERIFY(smart.setSelection(Selection(2, 3, 9, 7)));
        QVERIFY2(smart.saveSelection("Smart subject", &error), qPrintable(error));
        QVERIFY(smart.setSelection(Selection(5, 6, 12, 8)));
        const State editingContext = smart.state;
        const Layer before = smart.state.layers[0];
        const QImage appearance = smart.composite();
        const QString v7 = dir.filePath("smart.pdeck");
        QVERIFY2(smart.save(v7, &error), qPrintable(error));
        QCOMPARE(storedVersion(v7), 7);
        QCOMPARE(smart.sourceVersion(), 7);
        const QString fixtureDir = qEnvironmentVariable("PHOTO_SMART_FIXTURE_DIR");
        if (!fixtureDir.isEmpty()) {
            QVERIFY(QDir().mkpath(fixtureDir));
            const QString retained = fixtureDir + "/valid-v7-smart.pdeck";
            QFile::remove(retained);
            QVERIFY(QFile::copy(v7, retained));
        }
        Document loaded;
        QVERIFY2(loaded.load(v7, &error), qPrintable(error));
        QCOMPARE(loaded.sourceVersion(), 7);
        QCOMPARE(loaded.state.layers[0].smartObjectId, before.smartObjectId);
        QCOMPARE(loaded.state.layers[0].smartObjectSize, before.smartObjectSize);
        QCOMPARE(loaded.state.layers[0].pixels, native);
        QCOMPARE(loaded.state.layers[0].mask, before.mask);
        QCOMPARE(loaded.composite(), appearance);
        QVERIFY(loaded.selection.isEmpty());
        QVERIFY(loaded.state.previousSelection.isEmpty());
        QCOMPARE(loaded.state.savedSelections.size(), 1);
        QCOMPARE(loaded.state.savedSelections[0].name, QString("Smart subject"));
        QVERIFY(loaded.state.savedSelections[0].selection.equivalent(
            editingContext.savedSelections[0].selection, smart.state.size));
        QByteArray bytes;
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY2(Document::writeRecoverySnapshot(Document::recoverySnapshot(smart.state), buffer, &error),
                 qPrintable(error));
        buffer.close();
        const QString recovery = dir.filePath("recovery.pdeck");
        QFile file(recovery);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        QCOMPARE(storedVersion(recovery), 7);
        Document recovered;
        QVERIFY2(recovered.load(recovery, &error), qPrintable(error));
        QCOMPARE(recovered.state.layers[0].smartObjectId, before.smartObjectId);
        QCOMPARE(recovered.state.layers[0].pixels, native);
        QCOMPARE(recovered.composite(), appearance);
        QVERIFY(recovered.selection.equivalent(editingContext.selection, smart.state.size));
        QVERIFY(recovered.state.previousSelection.equivalent(
            editingContext.previousSelection, smart.state.size));
        QCOMPARE(recovered.state.savedSelections.size(), 1);
        QVERIFY(recovered.state.savedSelections[0].selection.equivalent(
            editingContext.savedSelections[0].selection, smart.state.size));
        QVERIFY2(loaded.rasterizeLayer(&error), qPrintable(error));
        QCOMPARE(loaded.projectSaveVersion(), 6);
        loaded.state.savedSelections.clear();
        QCOMPARE(loaded.projectSaveVersion(), 5);
    }
    void legacySmartConversionRequiresExplicitSaveUpgrade() {
        const QString fixturePath = QFINDTESTDATA("fixtures/v4-demo.pdeck");
        QVERIFY(!fixturePath.isEmpty());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("legacy.pdeck");
        QVERIFY(QFile::copy(fixturePath, path));
        QString error;
        Document document;
        QVERIFY2(document.load(path, &error), qPrintable(error));
        int eligible = -1;
        for (int i = 0; i < document.state.layers.size(); ++i) {
            const Layer &layer = document.state.layers[i];
            if (layer.visible && !layer.locked && layer.text.isEmpty()) {
                eligible = i;
                break;
            }
        }
        QVERIFY(eligible >= 0);
        document.state.active = eligible;
        QVERIFY2(document.convertToSmartObject(&error), qPrintable(error));
        const QUuid id = document.state.layers[eligible].smartObjectId;
        QVERIFY(document.requiresFormatUpgrade(path));
        QVERIFY(!document.save(path, &error));
        QCOMPARE(document.lastSaveStatus(), Document::SaveStatus::FormatUpgradeRequired);
        QCOMPARE(storedVersion(path), 4);
        Document::SaveOptions consent;
        consent.allowFormatUpgrade = true;
        QVERIFY2(document.save(path, &error, true, consent), qPrintable(error));
        QCOMPARE(storedVersion(path), 7);
        Document reopened;
        QVERIFY2(reopened.load(path, &error), qPrintable(error));
        QCOMPARE(reopened.state.layers[eligible].smartObjectId, id);
    }
    void menuConversionAndReplacementCancelKeepDocument() {
        MainWindow window(false, false);
        auto *document = new Document;
        fixture(*document);
        window.addDocument(document);
        window.show();
        auto *convert = window.findChild<QAction *>("convertSmartObject");
        auto *replace = window.findChild<QAction *>("replaceSmartObjectContents");
        QVERIFY(convert && replace);
        QVERIFY(convert->isEnabled());
        QVERIFY(!replace->isEnabled());
        convert->trigger();
        QVERIFY(document->state.layers[0].isSmartObject());
        QVERIFY(replace->isEnabled());
        auto *layers = window.findChild<QListWidget *>("layers");
        QVERIFY(layers && layers->count() == 1);
        QVERIFY(layers->item(0)->text().contains("Akıllı nesne"));
        QVERIFY(layers->item(0)->toolTip().contains("Gömülü kaynak"));
        auto *canvas = window.currentCanvas();
        QVERIFY(canvas);
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        const QTransform preview = canvas->transformGeometry();
        const Layer before = document->state.layers[0];
        const QImage appearance = document->composite();
        const quint64 revision = document->revision();
        QVERIFY(drive(replace, [](QDialog *dialog) { dialog->reject(); }));
        QVERIFY(canvas->isTransforming());
        QCOMPARE(canvas->transformGeometry(), preview);
        QCOMPARE(document->revision(), revision);
        QCOMPARE(document->state.layers[0].pixels, before.pixels);
        QCOMPARE(document->state.layers[0].smartObjectId, before.smartObjectId);
        QCOMPARE(document->composite(), appearance);
        canvas->cancelTransform();
        canvas->resize(600, 450);
        canvas->showRulers = false;
        canvas->actualSize();
        const QString screenshotDir = qEnvironmentVariable("PHOTO_SMART_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(QDir().mkpath(screenshotDir));
            QVERIFY(window.grab().save(screenshotDir + "/smart-object-ui.png"));
        }
        canvas->setTool(Canvas::Brush);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, canvas->toWidget({14, 12}).toPoint());
        QCOMPARE(document->revision(), revision);
        QCOMPARE(document->state.layers[0].pixels, before.pixels);
        canvas->setTool(Canvas::Fill);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, canvas->toWidget({14, 12}).toPoint());
        canvas->deleteSelection();
        QCOMPARE(document->revision(), revision);
        QCOMPARE(document->state.layers[0].pixels, before.pixels);
    }
    void menuReplacementLoadsNativePixelsIntoFixedFrame() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("replacement.png");
        const QImage native = image({3, 2}, QColor("#35c1a6"));
        QVERIFY(native.save(path));
        MainWindow window(false, false);
        auto *document = new Document;
        fixture(*document);
        window.addDocument(document);
        window.show();
        auto *convert = window.findChild<QAction *>("convertSmartObject");
        auto *replace = window.findChild<QAction *>("replaceSmartObjectContents");
        QVERIFY(convert && replace);
        convert->trigger();
        const Layer before = document->state.layers[0];
        const int history = document->historyLabels().size();
        bool selected = false;
        QVERIFY(drive(replace, [&](QDialog *dialog) {
            if (auto *file = qobject_cast<QFileDialog *>(dialog)) {
                selected = true;
                choose(file, path);
            } else if (auto *warning = qobject_cast<QMessageBox *>(dialog)) warning->reject();
            else dialog->reject();
        }));
        QVERIFY(selected);
        QCOMPARE(document->historyLabels().size(), history + 1);
        QCOMPARE(document->state.layers[0].smartObjectId, before.smartObjectId);
        QCOMPARE(document->state.layers[0].smartObjectSize, before.smartObjectSize);
        QCOMPARE(document->state.layers[0].pixels, native);
        QCOMPARE(document->state.layers[0].rendered().size(), before.rendered().size());
        document->undo();
        QCOMPARE(document->state.layers[0].pixels, before.pixels);
    }
    void selectionCopyPreviewDisablesReplaceUntilCanceled() {
        MainWindow window(false, false);
        auto *document = new Document;
        fixture(*document);
        QString error;
        QVERIFY2(document->convertToSmartObject(&error), qPrintable(error));
        QVERIFY(document->setSelection(Selection(11, 9, 8, 6)));
        document->markSaved();
        window.addDocument(document);
        window.show();
        auto *replace = window.findChild<QAction *>("replaceSmartObjectContents");
        auto *canvas = window.currentCanvas();
        QVERIFY(replace && canvas);
        QVERIFY(replace->isEnabled());
        const Layer before = document->state.layers[0];
        const quint64 revision = document->revision();
        const QStringList history = document->historyLabels();
        QVERIFY(canvas->beginTransform());
        QVERIFY(canvas->copySelectionAndTransform());
        QVERIFY(canvas->isTransforming());
        QVERIFY(!replace->isEnabled());
        QCOMPARE(document->revision(), revision);
        QCOMPARE(document->historyLabels(), history);
        QCOMPARE(document->state.layers.size(), 1);
        QCOMPARE(document->state.layers[0].pixels, before.pixels);
        QCOMPARE(document->state.layers[0].smartObjectId, before.smartObjectId);
        canvas->cancelTransform();
        QVERIFY(!canvas->isTransforming());
        QVERIFY(replace->isEnabled());
        QCOMPARE(document->revision(), revision);
        QCOMPARE(document->historyLabels(), history);
        QCOMPARE(document->state.layers.size(), 1);
        QCOMPARE(document->state.layers[0].pixels, before.pixels);
        QCOMPARE(document->state.layers[0].smartObjectId, before.smartObjectId);
        QVERIFY(!document->dirty);
    }
    void firstSmartSaveWarnsAndCancellationLeavesNoFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("first-smart.pdeck");
        MainWindow window(false, false);
        auto *document = new Document;
        fixture(*document);
        QString error;
        QVERIFY2(document->convertToSmartObject(&error), qPrintable(error));
        window.addDocument(document);
        window.show();
        auto *save = window.findChild<QAction *>("save");
        QVERIFY(save);
        bool warned = false;
        QVERIFY(drive(save, [&](QDialog *dialog) {
            if (auto *file = qobject_cast<QFileDialog *>(dialog)) choose(file, path);
            else if (auto *message = qobject_cast<QMessageBox *>(dialog)) {
                warned |= message->objectName() == "smartObjectFormatWarning";
                const QString screenshotDir = qEnvironmentVariable("PHOTO_SMART_SCREENSHOT_DIR");
                if (warned && !screenshotDir.isEmpty()) {
                    QVERIFY(QDir().mkpath(screenshotDir));
                    QVERIFY(message->grab().save(screenshotDir + "/smart-format-warning.png"));
                }
                message->button(QMessageBox::Cancel)->click();
            } else dialog->reject();
        }));
        QVERIFY(warned);
        QVERIFY(!QFile::exists(path));
        QVERIFY(document->path.isEmpty());
        QVERIFY(document->dirty);
        warned = false;
        QVERIFY(drive(save, [&](QDialog *dialog) {
            if (auto *file = qobject_cast<QFileDialog *>(dialog)) choose(file, path);
            else if (auto *message = qobject_cast<QMessageBox *>(dialog)) {
                warned |= message->objectName() == "smartObjectFormatWarning";
                message->button(QMessageBox::Save)->click();
            } else dialog->reject();
        }));
        QVERIFY(warned);
        QCOMPARE(storedVersion(path), 7);
        QCOMPARE(document->path, path);
        QVERIFY(!document->dirty);
    }
    void legacySaveActionProtectsOriginalWithV7Copy() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString original = dir.filePath("original.pdeck");
        const QString copy = dir.filePath("original-v7.pdeck");
        QString error;
        Document ordinary;
        fixture(ordinary);
        QVERIFY2(ordinary.save(original, &error), qPrintable(error));
        MainWindow window(false, false);
        auto *document = new Document;
        QVERIFY2(document->load(original, &error), qPrintable(error));
        QVERIFY2(document->convertToSmartObject(&error), qPrintable(error));
        window.addDocument(document);
        window.show();
        auto *save = window.findChild<QAction *>("save");
        QVERIFY(save);
        bool warned = false, selectedCopy = false;
        QVERIFY(drive(save, [&](QDialog *dialog) {
            if (auto *message = qobject_cast<QMessageBox *>(dialog)) {
                if (message->objectName() == "projectUpgradeWarning") {
                    warned = true;
                    if (auto *button = message->findChild<QAbstractButton *>("saveProjectCopy"))
                        button->click();
                    else message->reject();
                } else message->reject();
            } else if (auto *file = qobject_cast<QFileDialog *>(dialog)) {
                selectedCopy = true;
                choose(file, copy);
            } else dialog->reject();
        }));
        QVERIFY(warned && selectedCopy);
        QCOMPARE(storedVersion(original), 5);
        QCOMPARE(storedVersion(copy), 7);
        QCOMPARE(document->path, copy);
    }
};

QTEST_MAIN(SmartObjectTests)
#include "test_smart_objects.moc"
