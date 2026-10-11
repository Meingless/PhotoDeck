#include "appearance.h"
#include "canvas.h"
#include "document.h"
#include "mainwindow.h"
#include <QAbstractButton>
#include <QAction>
#include <QDataStream>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class PhotoDeckTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QImageReader::setAllocationLimit(128);
        QStandardPaths::setTestModeEnabled(true);
    }
    void dimensionsAndLayerLimits() {
        Document d;
        QVERIFY(!d.create({0, 100}));
        QVERIFY(!d.create({100000, 100}));
        QVERIFY(!d.create({8192, 8192}));
        QVERIFY(d.create({32, 32}));
        QString error;
        for (int i = 1; i < 64; ++i)
            QVERIFY(d.addLayer(QString::number(i), &error));
        QVERIFY(!d.addLayer("excess", &error));
        QCOMPARE(d.state.layers.size(), 64);
        QVERIFY(!error.isEmpty());
    }
    void compositeMasksAndBlend() {
        Document d;
        QVERIFY(d.create({8, 8}));
        d.state.layers[0].pixels.fill(QColor(200, 100, 50));
        QString error;
        QVERIFY(d.addLayer("Top", &error));
        d.state.layers[1].pixels.fill(QColor(100, 200, 50));
        d.state.layers[1].opacity = .5;
        QColor c = d.composite().pixelColor(3, 3);
        QVERIFY(qAbs(c.red() - 150) <= 1);
        QVERIFY(qAbs(c.green() - 150) <= 1);
        QVERIFY(d.addMask(&error));
        auto &l = d.state.layers[1];
        l.mask.fill(Qt::black);
        QCOMPARE(d.composite().pixelColor(3, 3), QColor(200, 100, 50));
        l.mask.fill(Qt::white);
        l.opacity = 1;
        l.blend = 1;
        c = d.composite().pixelColor(3, 3);
        QVERIFY(qAbs(c.red() - 78) <= 1);
        QVERIFY(qAbs(c.green() - 78) <= 1);
        d.state.layers[1].visible = false;
        QCOMPARE(d.composite().pixelColor(3, 3), QColor(200, 100, 50));
    }
    void adjustmentsAndBlurPreserveSource() {
        Document d;
        d.create({9, 9});
        auto &l = d.state.layers[0];
        l.pixels.fill(Qt::black);
        l.pixels.setPixelColor(4, 4, Qt::white);
        QImage original = l.pixels;
        d.beginEdit();
        d.commit("seed");
        d.beginEdit();
        l.blur = 2;
        d.adjust(0, 0, -100);
        d.commit("filter");
        QImage rendered = d.composite();
        QVERIFY(rendered.pixelColor(4, 4).red() < 255);
        QVERIFY(rendered.pixelColor(3, 4).red() > 0);
        QCOMPARE(l.pixels, original);
        d.undo();
        QCOMPARE(d.composite(), original);
        d.redo();
        QCOMPARE(d.composite(), rendered);
    }
    void historyAndSavePoint() {
        QTemporaryDir dir;
        Document d;
        d.create({10, 10});
        QString err;
        d.beginEdit();
        d.state.layers[0].pixels.fill(Qt::red);
        d.commit("red");
        QVERIFY(d.dirty);
        QVERIFY(d.save(dir.filePath("saved.pdeck"), &err));
        QVERIFY(!d.dirty);
        d.beginEdit();
        d.state.layers[0].pixels.fill(Qt::blue);
        d.commit("blue");
        d.undo();
        QVERIFY(!d.dirty);
        QCOMPARE(d.composite().pixelColor(0, 0), QColor(Qt::red));
        d.redo();
        QVERIFY(d.dirty);
        d.undo();
        d.beginEdit();
        d.state.layers[0].pixels.fill(Qt::green);
        d.commit("new branch");
        QVERIFY(!d.canRedo());
        for (int i = 0; i < 40; ++i) {
            d.beginEdit();
            d.state.layers[0].pixels.fill(QColor(i, 0, 0));
            d.commit(QString::number(i));
        }
        QVERIFY(d.historyLabels().size() <= 30);
        QVERIFY(d.historyBytes() <= Document::HistoryBudget);
        d.beginEdit();
        d.state.layers[0].pixels.fill(Qt::yellow);
        d.cancelEdit();
        QCOMPARE(d.composite().pixelColor(0, 0), QColor(39, 0, 0));
    }
    void historyMemoryBudget() {
        Document d;
        d.create({2048, 2048});
        for (int i = 0; i < 12; ++i) {
            d.beginEdit();
            d.state.layers[0].pixels.fill(QColor(i, 0, 0));
            d.commit("edit");
        }
        QVERIFY(d.historyBytes() <= Document::HistoryBudget);
        QVERIFY(d.canUndo());
        qInfo() << "2048x2048 history bytes:" << d.historyBytes()
                << "states:" << d.historyLabels().size();
    }
    void nativeRoundTripAndExport() {
        QTemporaryDir dir;
        Document d;
        d.makeDemo();
        QString err;
        d.state.layers[1].brightness = 12;
        d.state.layers[1].contrast = -7;
        d.state.layers[1].blur = 2;
        d.state.active = 1;
        d.selection = QRect(100, 100, 400, 300);
        QVERIFY(d.addMask(&err));
        d.state.layers[1].offset = {-10, 30};
        d.state.layers[1].opacity = .72;
        d.state.layers[1].locked = true;
        QImage expected = d.composite();
        QVERIFY2(d.save(dir.filePath("work.pdeck"), &err), qPrintable(err));
        Document restored;
        QVERIFY2(restored.load(dir.filePath("work.pdeck"), &err), qPrintable(err));
        QCOMPARE(restored.composite(), expected);
        QCOMPARE(restored.state.layers.size(), 4);
        QCOMPARE(restored.state.layers[2].text, QString("HORIZON"));
        QCOMPARE(restored.state.layers[2].font, d.state.layers[2].font);
        QVERIFY(restored.state.layers[1].locked);
        QVERIFY(restored.exportImage(dir.filePath("export.png"), 95, &err));
        QCOMPARE(QImage(dir.filePath("export.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied),
                 expected);
        QVERIFY(restored.exportImage(dir.filePath("export.jpg"), 95, &err));
        QVERIFY(!QImage(dir.filePath("export.jpg")).hasAlphaChannel());
        if (QImageWriter::supportedImageFormats().contains("webp")) {
            QVERIFY(restored.exportImage(dir.filePath("export.webp"), 95, &err));
            QCOMPARE(QImage(dir.filePath("export.webp")).size(), expected.size());
        }
        QString before = d.path;
        QVERIFY(!d.save(dir.filePath("missing/failed.pdeck"), &err));
        QCOMPARE(d.path, before);
    }
    void transparencyAndJpegBackground() {
        QTemporaryDir dir;
        Document d;
        d.create({16, 16});
        QString err;
        QVERIFY(d.exportImage(dir.filePath("a.png"), 95, &err));
        QCOMPARE(QImage(dir.filePath("a.png")).pixelColor(5, 5).alpha(), 0);
        QVERIFY(d.exportImage(dir.filePath("a.jpg"), 100, &err));
        QCOMPARE(QImage(dir.filePath("a.jpg")).pixelColor(5, 5), QColor(Qt::white));
    }
    void transformsKeepLayerAlignment() {
        Document d;
        d.create({20, 15});
        d.state.layers[0].pixels.fill(Qt::transparent);
        QPainter p(&d.state.layers[0].pixels);
        p.fillRect(3, 4, 6, 5, Qt::red);
        p.end();
        QString err;
        QImage original = d.composite();
        d.beginEdit();
        d.commit("seed");
        QVERIFY(d.crop(QRect(2, 3, 12, 10), &err));
        QCOMPARE(d.composite(), original.copy(2, 3, 12, 10));
        QImage cropped = d.composite();
        QTransform t;
        t.rotate(90);
        QVERIFY(d.rotate(true, &err));
        QCOMPARE(d.composite(), cropped.transformed(t));
        d.flip(true);
        QCOMPARE(d.composite(), cropped.transformed(t).transformed(QTransform::fromScale(-1, 1)));
        d.undo();
        d.undo();
        d.undo();
        QCOMPARE(d.composite(), original);
        QVERIFY(d.resize({40, 30}, &err));
        QCOMPARE(d.state.size, QSize(40, 30));
        QCOMPARE(d.state.layers[0].pixels.size(), QSize(40, 30));
        QVERIFY(!d.resize({16384, 16384}, &err));
        QCOMPARE(d.state.size, QSize(40, 30));
    }
    void hostileFilesDoNotReplaceDocument() {
        QTemporaryDir dir;
        Document d;
        d.create({10, 10}, true);
        QImage before = d.composite();
        QString err;
        QString file = dir.filePath("bad.pdeck");
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QDataStream s(&f);
            s.setVersion(QDataStream::Qt_6_2);
            s << quint32(0x50444543) << quint32(1) << QSize(10, 10) << qint32(0) << qint32(1)
              << quint32(0x7fffffff);
        }
        QVERIFY(!d.load(file, &err));
        QCOMPARE(d.composite(), before);
        Document valid;
        valid.makeDemo();
        QVERIFY(valid.save(dir.filePath("valid.pdeck"), &err));
        QFile source(dir.filePath("valid.pdeck"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QByteArray data = source.readAll();
        for (int length : {0, 1, 10, 40, int(data.size() / 2), int(data.size() - 1)}) {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data.left(length));
            f.close();
            QVERIFY(!d.load(file, &err));
            QCOMPARE(d.composite(), before);
        }
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
            f.write("unexpected tail");
        }
        QVERIFY(!d.load(file, &err));
        QCOMPARE(d.composite(), before);
    }
    void canvasBrushSelectionMaskAndUndo() {
        Document d;
        d.create({100, 100});
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Brush);
        c.foreground = Qt::red;
        c.brushSize = 10;
        c.hardness = 100;
        d.selection = QRect(20, 20, 30, 30);
        auto pos = [&](int x, int y) { return c.toWidget({qreal(x), qreal(y)}).toPoint(); };
        QTest::mousePress(&c, Qt::LeftButton, Qt::NoModifier, pos(10, 30));
        QTest::mouseMove(&c, pos(65, 30));
        QTest::mouseRelease(&c, Qt::LeftButton, Qt::NoModifier, pos(65, 30));
        QVERIFY(d.composite().pixelColor(30, 30).red() > 200);
        QCOMPARE(d.composite().pixelColor(10, 30).alpha(), 0);
        QCOMPARE(d.composite().pixelColor(65, 30).alpha(), 0);
        QVERIFY(d.canUndo());
        d.undo();
        QCOMPARE(d.composite().pixelColor(30, 30).alpha(), 0);
        d.redo();
        d.selection = {};
        QString err;
        QVERIFY(d.addMask(&err));
        c.foreground = Qt::black;
        QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, pos(30, 30));
        QCOMPARE(d.composite().pixelColor(30, 30).alpha(), 0);
        QVERIFY(d.state.layers[0].pixels.pixelColor(30, 30).alpha() > 0);
        d.undo();
        QVERIFY(d.composite().pixelColor(30, 30).alpha() > 0);
    }
    void canvasFillCloneAndLock() {
        Document d;
        d.create({100, 100});
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        auto pos = [&](int x, int y) { return c.toWidget({qreal(x), qreal(y)}).toPoint(); };
        c.setTool(Canvas::Fill);
        c.foreground = QColor(40, 80, 120);
        QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, pos(5, 5));
        QCOMPARE(d.composite().pixelColor(90, 90), c.foreground);
        QPainter p(&d.state.layers[0].pixels);
        p.fillRect(10, 10, 20, 20, Qt::red);
        p.end();
        c.setTool(Canvas::Clone);
        c.brushSize = 8;
        QTest::mouseClick(&c, Qt::LeftButton, Qt::AltModifier, pos(20, 20));
        QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, pos(70, 70));
        QCOMPARE(d.composite().pixelColor(70, 70), QColor(Qt::red));
        QImage before = d.composite();
        d.state.layers[0].locked = true;
        c.setTool(Canvas::Eraser);
        QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, pos(70, 70));
        QCOMPARE(d.composite(), before);
    }
    void canvasSelectionShapeMoveAndCancel() {
        Document d;
        d.create({100, 100});
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        auto pos = [&](int x, int y) { return c.toWidget({qreal(x), qreal(y)}).toPoint(); };
        c.setTool(Canvas::Select);
        QTest::mousePress(&c, Qt::LeftButton, Qt::NoModifier, pos(20, 20));
        QTest::mouseMove(&c, pos(70, 60));
        QTest::mouseRelease(&c, Qt::LeftButton, Qt::NoModifier, pos(70, 60));
        QVERIFY(d.selection.contains(40, 40));
        QVERIFY(!d.selection.contains(90, 90));
        c.setTool(Canvas::Rectangle);
        c.foreground = Qt::green;
        QTest::mousePress(&c, Qt::LeftButton, Qt::NoModifier, pos(10, 10));
        QTest::mouseMove(&c, pos(90, 90));
        QTest::mouseRelease(&c, Qt::LeftButton, Qt::NoModifier, pos(90, 90));
        QCOMPARE(d.composite().pixelColor(40, 40), QColor(Qt::green));
        QCOMPARE(d.composite().pixelColor(10, 10).alpha(), 0);
        QImage before = d.composite();
        c.setTool(Canvas::Move);
        QTest::mousePress(&c, Qt::LeftButton, Qt::NoModifier, pos(50, 50));
        QTest::mouseMove(&c, pos(65, 65));
        QTest::keyClick(&c, Qt::Key_Escape);
        QCOMPARE(d.composite(), before);
    }
    void multiDocumentUiAndSaveGuard() {
        MainWindow window(false, false);
        window.show();
        window.demo();
        Document *first = window.currentDocument();
        auto *second = new Document;
        second->create({120, 80});
        second->title = "Second";
        window.addDocument(second);
        QCOMPARE(window.currentDocument(), second);
        QVERIFY(window.findChild<QAction *>("addLayer"));
        window.findChild<QAction *>("addLayer")->trigger();
        QCOMPARE(second->state.layers.size(), 2);
        QCOMPARE(first->state.layers.size(), 4);
        QTest::qWait(60);
        auto *tabs = window.findChild<QTabWidget *>("documents");
        tabs->setCurrentIndex(0);
        window.findChild<QAction *>("tool3")->trigger();
        QCOMPARE(window.currentCanvas()->tool, Canvas::Brush);
        tabs->setCurrentIndex(1);
        QCOMPARE(window.currentCanvas()->tool, Canvas::Move);
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box)
                QTest::mouseClick(box->button(QMessageBox::Cancel), Qt::LeftButton);
        });
        window.findChild<QAction *>("close")->trigger();
        QCOMPARE(tabs->count(), 2);
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box)
                QTest::mouseClick(box->button(QMessageBox::Discard), Qt::LeftButton);
        });
        window.findChild<QAction *>("close")->trigger();
        QCOMPARE(tabs->count(), 1);
    }
    void automaticRecoveryKeepsEdits() {
        QString dirPath =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery";
        QDir dir(dirPath);
        QVERIFY(dir.entryList({"*.pdeck"}, QDir::Files).isEmpty());
        auto *window = new MainWindow(false, false);
        auto *d = new Document;
        d->create({32, 24});
        window->addDocument(d);
        d->beginEdit();
        d->state.layers[0].pixels.fill(Qt::cyan);
        d->commit("recover me");
        auto *timer = window->findChild<QTimer *>("autosaveTimer");
        QVERIFY(timer);
        timer->setInterval(10);
        QTRY_VERIFY_WITH_TIMEOUT(!QDir(dirPath).entryList({"*.pdeck"}, QDir::Files).isEmpty(), 5000);
        delete window; // Abrupt destruction skips closeEvent, as a crashed session would.
        MainWindow recovered(true, false);
        QTimer::singleShot(1, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box)
                QTest::mouseClick(box->button(QMessageBox::Yes), Qt::LeftButton);
        });
        QTRY_VERIFY_WITH_TIMEOUT(recovered.currentDocument() != nullptr, 5000);
        QVERIFY(recovered.currentDocument()->dirty);
        QCOMPARE(recovered.currentDocument()->composite().pixelColor(3, 3), QColor(Qt::cyan));
        QVERIFY(recovered.currentDocument()->path.isEmpty());
        recovered.currentDocument()->markSaved();
        recovered.close();
        QVERIFY(dir.entryList({"*.pdeck"}, QDir::Files).isEmpty());
    }
    void batchPreservesSourcesAndExistingOutputs() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage source(24, 24, QImage::Format_ARGB32);
        source.fill(Qt::red);
        QString input = dir.filePath("source.png"), existing = dir.filePath("source_photodeck.png"),
                output = dir.filePath("source_photodeck_2.png");
        QVERIFY(source.save(input));
        QImage sentinel(24, 24, QImage::Format_ARGB32);
        sentinel.fill(Qt::green);
        QVERIFY(sentinel.save(existing));
        MainWindow window(false, false);
        window.show();
        QTimer driver;
        driver.setInterval(20);
        int stage = 0;
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (stage == 0 && dialog->windowTitle() == "Toplu işlem için görüntüleri seç") {
                auto *picker = qobject_cast<QFileDialog *>(dialog);
                if (!picker)
                    return;
                picker->setDirectory(dir.path());
                picker->selectFile("source.png");
                stage = 1;
            } else if (stage == 1 && dialog->windowTitle() == "Toplu işlem için görüntüleri seç") {
                auto *picker = qobject_cast<QFileDialog *>(dialog);
                if (!picker)
                    return;
                picker->selectFile("source.png");
                auto *fileName = picker->findChild<QLineEdit *>("fileNameEdit");
                if (fileName)
                    fileName->setText(input);
                auto *buttonBox = picker->findChild<QDialogButtonBox *>();
                if (buttonBox)
                    QTest::mouseClick(buttonBox->button(QDialogButtonBox::Open), Qt::LeftButton);
            } else if (stage == 1 && dialog->windowTitle() == "Çıktı klasörünü seç") {
                auto *picker = qobject_cast<QFileDialog *>(dialog);
                if (!picker)
                    return;
                picker->setDirectory(dir.path());
                stage = 2;
                QMetaObject::invokeMethod(picker, "accept");
            } else if (stage == 2 && dialog->windowTitle() == "Toplu dönüştür") {
                auto *buttons = dialog->findChild<QDialogButtonBox *>();
                if (!buttons)
                    return;
                stage = 3;
                QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            } else if (stage == 3 && qobject_cast<QMessageBox *>(dialog)) {
                auto *box = qobject_cast<QMessageBox *>(dialog);
                if (!box)
                    return;
                stage = 4;
                QTest::mouseClick(box->button(QMessageBox::Ok), Qt::LeftButton);
            }
        });
        QTimer::singleShot(5000, &window, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        driver.start();
        window.findChild<QAction *>("batch")->trigger();
        driver.stop();
        QCOMPARE(stage, 4);
        QVERIFY(QFileInfo::exists(output));
        QCOMPARE(QImage(input), source);
        QCOMPARE(QImage(existing), sentinel);
        QCOMPARE(QImage(output), source);
    }
    void defaultFocusAndAllPresetsPreserveDocuments() {
        MainWindow window(false, false);
        window.resize(1380, 880);
        window.show();
        window.demo();
        QCOMPARE(window.uiSettings().preset, QString("focus"));
        QCOMPARE(uiPresets().size(), 10);
        auto *doc = window.currentDocument();
        QImage pixels = doc->composite();
        quint64 revision = doc->revision();
        for (const auto &preset : uiPresets()) {
            window.applyUiPreset(preset.id, false);
            QTest::qWait(20);
            QCOMPARE(window.currentDocument(), doc);
            QCOMPARE(doc->composite(), pixels);
            QCOMPARE(doc->revision(), revision);
            QCOMPARE(window.findChild<QComboBox *>("presetPicker")->currentData().toString(), preset.id);
            QVERIFY(window.findChild<QAction *>("ui.preset." + preset.id)->isChecked());
            QVERIFY(window.findChild<QAction *>("ui.customize")->isEnabled());
        }
        window.applyUiPreset("focus", false);
        auto *workspace = window.findChild<QWidget *>("workspace");
        for (const auto &name : {"floatingTools", "floatingLayers"}) {
            auto *panel = window.findChild<QFrame *>(name);
            QVERIFY(panel);
            QVERIFY(!panel->isHidden());
            QVERIFY(workspace->rect().contains(panel->geometry()));
        }
    }
    void customAppearanceRoundTripAndHostilePreset() {
        QTemporaryDir dir;
        MainWindow window(false, false);
        window.demo();
        auto *doc = window.currentDocument();
        QImage before = doc->composite();
        UiSettings s = UiSettings::forPreset("paper");
        s.accent = QColor("#a34d63");
        s.canvas = QColor("#ced9e2");
        s.fontSize = 15;
        s.iconSize = 28;
        s.radius = 12;
        s.panelSide = "left";
        s.panelWidth = 310;
        window.applyUiSettings(s, true, false);
        QString error;
        QVERIFY(window.exportUiPreset(dir.filePath("custom.pdui"), &error));
        window.applyUiPreset("focus", false);
        QVERIFY(window.importUiPreset(dir.filePath("custom.pdui"), &error));
        QCOMPARE(window.uiSettings().preset, QString("paper"));
        QCOMPARE(window.uiSettings().accent, s.accent);
        QCOMPARE(window.uiSettings().fontSize, 15);
        QCOMPARE(window.dockWidgetArea(window.findChild<QDockWidget *>("layersDock")),
                 Qt::LeftDockWidgetArea);
        QCOMPARE(doc->composite(), before);
        QJsonObject bad = s.toJson();
        bad["fontSize"] = 1000000;
        UiSettings parsed;
        QVERIFY(!UiSettings::fromJson(bad, &parsed, &error));
        bad = s.toJson();
        bad["accent"] = "red; image:url(file:///private)";
        QVERIFY(!UiSettings::fromJson(bad, &parsed, &error));
        bad = s.toJson();
        bad["preset"] = "unknown";
        QVERIFY(!UiSettings::fromJson(bad, &parsed, &error));
        QFile file(dir.filePath("bad.pdui"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(bad).toJson());
        file.close();
        QVERIFY(!window.importUiPreset(file.fileName(), &error));
        QCOMPARE(window.uiSettings().preset, QString("paper"));
        QCOMPARE(doc->composite(), before);
    }
    void customizationCancelRestoresLayout() {
        MainWindow window(false, false);
        window.show();
        window.demo();
        auto original = window.uiSettings();
        QTimer::singleShot(10, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *spin = dialog->findChild<QSpinBox *>("uiFontSize");
            auto *combo = dialog->findChild<QComboBox *>("customPreset");
            if (combo)
                combo->setCurrentIndex(combo->findData("touch"));
            if (spin)
                spin->setValue(18);
            dialog->reject();
        });
        window.findChild<QAction *>("ui.customize")->trigger();
        QCOMPARE(window.uiSettings().preset, original.preset);
        QCOMPARE(window.uiSettings().fontSize, original.fontSize);
        QVERIFY(!window.findChild<QFrame *>("floatingTools")->isHidden());
    }
    void focusPanelKeyboardAndSettingsPersistence() {
        QTemporaryDir dir;
        MainWindow window(false, false);
        window.show();
        window.demo();
        QTest::qWait(30);
        auto *handle = window.findChild<QFrame *>("floatingLayers")->findChild<QToolButton *>();
        QVERIFY(handle);
        QTest::keyClick(handle, Qt::Key_Left);
        QVERIFY(window.uiSettings().layersPosition.x() >= 0);
        window.findChild<QAction *>("ui.panel.tools")->setChecked(false);
        QVERIFY(window.findChild<QFrame *>("floatingTools")->isHidden());
        QString error;
        QVERIFY(window.exportUiPreset(dir.filePath("focus.pdui"), &error));
        window.applyUiPreset("graphite", false);
        QVERIFY(window.importUiPreset(dir.filePath("focus.pdui"), &error));
        QVERIFY(window.findChild<QFrame *>("floatingTools")->isHidden());
        QVERIFY(window.uiSettings().layersPosition.x() >= 0);
    }
    void representativePerformance() {
        Document d;
        d.makeDemo();
        QElapsedTimer timer;
        timer.start();
        QImage result;
        for (int i = 0; i < 20; ++i)
            result = d.composite();
        qInfo() << "1400x1000 4-layer composite average ms:" << timer.elapsed() / 20.0;
        d.state.layers[1].brightness = 10;
        timer.restart();
        result = d.composite();
        qInfo() << "1400x1000 first adjusted render ms:" << timer.elapsed();
        QVERIFY(!result.isNull());
    }
};
QTEST_MAIN(PhotoDeckTests)
#include "test_photodeck.moc"
