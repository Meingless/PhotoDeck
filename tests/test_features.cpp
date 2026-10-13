#include "mainwindow.h"
#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFocusEvent>
#include <QFontComboBox>
#include <QImageReader>
#include <QInputDialog>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QPoint at(Canvas &c, int x, int y) {
    return c.toWidget({qreal(x), qreal(y)}).toPoint();
}
void drag(Canvas &c, QPoint a, QPoint b, Qt::KeyboardModifiers mods = Qt::NoModifier, bool move = true) {
    QTest::mousePress(&c, Qt::LeftButton, mods, a);
    if (move)
        QTest::mouseMove(&c, b);
    QTest::mouseRelease(&c, Qt::LeftButton, mods, b);
}
bool driveModal(const std::function<void()> &action, const std::function<bool(QDialog *)> &handler) {
    QTimer driver;
    driver.setInterval(10);
    QPointer<QDialog> last;
    int handled = 0;
    bool timedOut = false;
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (dialog && dialog != last) {

            last = dialog;
            if (handler(dialog))
                ++handled;
            else
                last = nullptr;
        }
    });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        qWarning() << "audit timeout"
                   << (QApplication::activeModalWidget()
                           ? QApplication::activeModalWidget()->metaObject()->className()
                           : "none");
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            qWarning() << "message" << box->text();
        timedOut = true;
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    driver.start();
    watchdog.start(5000);
    action();
    driver.stop();
    watchdog.stop();
    return handled > 0 && !timedOut;
}
void chooseFile(QFileDialog *dialog, const QString &path) {
    dialog->setDirectory(QFileInfo(path).absolutePath());
    dialog->selectFile(path);
    if (auto *name = dialog->findChild<QLineEdit *>("fileNameEdit")) {
        name->setFocus();
        name->selectAll();
        QInputMethodEvent event;
        event.setCommitString(path);
        QApplication::sendEvent(name, &event);
    } else
        dialog->selectFile(path);

    QMetaObject::invokeMethod(dialog, "accept");
}
QAction *command(MainWindow &window, const char *id) {
    return window.findChild<QAction *>(id);
}

} // namespace
class FeatureAudit : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setApplicationName("PhotoDeckFeatureAudit");
        QApplication::setOrganizationName("PhotoDeckTests");
        QStandardPaths::setTestModeEnabled(true);
        QImageReader::setAllocationLimit(128);
    }
    void finalPointerPositionIsApplied_data() {
        QTest::addColumn<int>("tool");
        for (int i : {Canvas::Brush, Canvas::Pencil, Canvas::Eraser, Canvas::Move, Canvas::Select,
                      Canvas::Crop, Canvas::Gradient, Canvas::Rectangle, Canvas::Ellipse})
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void finalPointerPositionIsApplied() {
        QFETCH(int, tool);
        Document d;
        QVERIFY(d.create({100, 100}));
        if (tool == Canvas::Eraser)
            d.state.layers[0].pixels.fill(Qt::red);
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Tool(tool));
        c.foreground = Qt::blue;
        c.background = Qt::red;
        c.brushSize = 8;
        c.hardness = 100;
        drag(c, at(c, 20, 20), at(c, 70, 70), Qt::NoModifier, false);
        if (tool == Canvas::Select || tool == Canvas::Crop)
            QVERIFY(d.selection.contains(50, 50));
        else if (tool == Canvas::Move)
            QCOMPARE(d.state.layers[0].offset, QPoint(50, 50));
        else if (tool == Canvas::Eraser)
            QCOMPARE(d.composite().pixelColor(68, 68).alpha(), 0);
        else if (tool == Canvas::Gradient)
            QVERIFY(d.composite().pixelColor(60, 60).red() > 100);
        else if (tool == Canvas::Rectangle || tool == Canvas::Ellipse)
            QVERIFY(d.composite().pixelColor(45, 45).blue() > 200);
        else
            QVERIFY(d.composite().pixelColor(68, 68).blue() > 200);
    }
    void idleClicksDoNotConsumeUndo() {
        Document d;
        d.create({100, 100});
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Move);
        QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, at(c, 30, 30));
        QVERIFY(!d.dirty);
        QVERIFY(!d.canUndo());
    }
    void maskPaintingKeepsTextEditable() {
        Document d;
        d.create({200, 100});
        QFont f;
        f.setPixelSize(30);
        QString e;
        QVERIFY(d.addText("Editable", f, Qt::red, {10, 10}, &e));
        QVERIFY(d.addMask(&e));
        Canvas c(&d);
        c.resize(600, 400);
        c.show();
        c.fit();
        c.setTool(Canvas::Brush);
        c.brushSize = 10;
        c.foreground = Qt::black;
        c.hardness = 100;
        QImage source = d.state.layers[d.state.active].pixels;
        QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, at(c, 25, 25));
        QCOMPARE(d.state.layers[d.state.active].text, QString("Editable"));
        QCOMPARE(d.state.layers[d.state.active].pixels, source);
    }
    void textEditingPreservesMask() {
        Document d;
        d.create({200, 100});
        QFont f;
        f.setPixelSize(25);
        QString e;
        QVERIFY(d.addText("abc", f, Qt::red, {0, 0}, &e));
        QVERIFY(d.addMask(&e));
        auto &l = d.state.layers[d.state.active];
        l.mask.setPixelColor(10, 10, Qt::black);
        QVERIFY(d.editText("abcdef", f, Qt::blue, &e));
        QVERIFY(!d.state.layers[d.state.active].mask.isNull());
        QCOMPARE(d.state.layers[d.state.active].mask.pixelColor(10, 10), QColor(Qt::black));
        QCOMPARE(d.state.layers[d.state.active].mask.size(),
                 d.state.layers[d.state.active].pixels.size());
    }
    void existingLayerMaskAtLayerLimit() {
        Document d;
        d.create({1, 1});
        QString e;
        for (int i = 1; i < 64; ++i)
            QVERIFY(d.addLayer("test", &e));
        QVERIFY2(d.addMask(&e), qPrintable(e));
        QCOMPARE(d.state.layers.size(), 64);
    }
    void transformedDocumentCanAlwaysReopen() {
        Document d;
        d.create({100, 100});
        d.state.layers[0].offset = {-32768, -32768};
        QString e;
        QVERIFY(d.crop({10, 10, 80, 80}, &e));
        QTemporaryDir dir;
        QVERIFY(d.save(dir.filePath("crop.pdeck"), &e));
        Document reopened;
        QVERIFY2(reopened.load(dir.filePath("crop.pdeck"), &e), qPrintable(e));
        QCOMPARE(reopened.state.layers[0].offset, d.state.layers[0].offset);
    }
    void eraserIsIndependentOfPickedAlpha() {
        Document d;
        d.create({100, 100}, true);
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Eraser);
        c.foreground = QColor(0, 0, 0, 0);
        c.hardness = 100;
        c.brushSize = 12;
        QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, at(c, 50, 50));
        QCOMPARE(d.composite().pixelColor(50, 50).alpha(), 0);
    }
    void blendModes_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<QColor>("expected");
        QTest::newRow("normal") << 0 << QColor(100, 200, 50);
        QTest::newRow("multiply") << 1 << QColor(78, 78, 10);
        QTest::newRow("screen") << 2 << QColor(222, 222, 90);
        QTest::newRow("overlay") << 3 << QColor(188, 157, 20);
        QTest::newRow("darken") << 4 << QColor(100, 100, 50);
        QTest::newRow("lighten") << 5 << QColor(200, 200, 50);
    }
    void blendModes() {
        QFETCH(int, mode);
        QFETCH(QColor, expected);
        Document d;
        d.create({8, 8});
        d.state.layers[0].pixels.fill(QColor(200, 100, 50));
        QString e;
        QVERIFY(d.addLayer("top", &e));
        d.state.layers[1].pixels.fill(QColor(100, 200, 50));
        d.state.layers[1].blend = mode;
        QColor actual = d.composite().pixelColor(3, 3);
        QVERIFY(qAbs(actual.red() - expected.red()) <= 1);
        QVERIFY(qAbs(actual.green() - expected.green()) <= 1);
        QVERIFY(qAbs(actual.blue() - expected.blue()) <= 1);
    }
    void nonDestructiveColorExtremes() {
        Document d;
        d.create({5, 5});
        d.state.layers[0].pixels.fill(QColor(100, 150, 200));
        QImage original = d.state.layers[0].pixels;
        d.adjust(100, 0, 0);
        QCOMPARE(d.composite().pixelColor(1, 1), QColor(Qt::white));
        d.adjust(-100, 0, 0);
        QCOMPARE(d.composite().pixelColor(1, 1), QColor(Qt::black));
        d.adjust(0, -100, 0);
        QCOMPARE(d.composite().pixelColor(1, 1), QColor(121, 133, 146));
        d.adjust(0, 0, -100);
        QColor gray = d.composite().pixelColor(1, 1);
        QCOMPARE(gray.red(), gray.green());
        QCOMPARE(gray.green(), gray.blue());
        QCOMPARE(d.state.layers[0].pixels, original);
    }
    void allToolsRespectSelection_data() {
        QTest::addColumn<int>("tool");
        for (int i : {Canvas::Brush, Canvas::Pencil, Canvas::Eraser, Canvas::Fill, Canvas::Gradient,
                      Canvas::Rectangle, Canvas::Ellipse})
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void allToolsRespectSelection() {
        QFETCH(int, tool);
        Document d;
        d.create({100, 100}, tool == Canvas::Eraser);
        QImage original = d.composite();
        d.selection = {30, 30, 20, 20};
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Tool(tool));
        c.hardness = 100;
        c.brushSize = 15;
        c.foreground = Qt::blue;
        c.background = Qt::red;
        if (tool == Canvas::Fill)
            QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, at(c, 40, 40));
        else
            drag(c, at(c, 10, 10), at(c, 80, 80));
        QImage result = d.composite();
        QVERIFY(result != original);
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 100; ++x)
                if (!QRect(30, 30, 20, 20).contains(x, y))
                    QCOMPARE(result.pixel(x, y), original.pixel(x, y));
        d.undo();
        QCOMPARE(d.composite(), original);
    }
    void maskSelectionInvertRemoveAndUndo() {
        Document d;
        d.create({40, 40}, true);
        d.state.layers[0].offset = {2, 3};
        d.selection = {10, 10, 10, 10};
        QString e;
        QVERIFY(d.addMask(&e));
        QImage source = d.state.layers[0].pixels;
        QImage selected = d.composite();
        QCOMPARE(selected.pixelColor(12, 12).alpha(), 255);
        QCOMPARE(selected.pixelColor(5, 5).alpha(), 0);
        d.invertMask();
        QCOMPARE(d.composite().pixelColor(12, 12).alpha(), 0);
        QCOMPARE(d.composite().pixelColor(5, 5).alpha(), 255);
        d.undo();
        QCOMPARE(d.composite(), selected);
        d.removeMask();
        QVERIFY(d.state.layers[0].mask.isNull());
        QCOMPARE(d.composite().pixelColor(5, 5).alpha(), 255);
        d.undo();
        QCOMPARE(d.composite(), selected);
        QCOMPARE(d.state.layers[0].pixels, source);
    }
    void shapesAndShiftConstraint() {
        Document d;
        d.create({100, 100});
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Rectangle);
        c.foreground = Qt::red;
        drag(c, at(c, 20, 20), at(c, 60, 40), Qt::ShiftModifier, false);
        QCOMPARE(d.composite().pixelColor(50, 55), QColor(Qt::red));
        d.undo();
        c.setTool(Canvas::Ellipse);
        drag(c, at(c, 20, 20), at(c, 60, 40), Qt::ShiftModifier, false);
        QCOMPARE(d.composite().pixelColor(40, 50), QColor(Qt::red));
        QCOMPARE(d.composite().pixelColor(21, 21).alpha(), 0);
    }
    void connectedFillDoesNotCrossBorder() {
        Document d;
        d.create({40, 40});
        QPainter p(&d.state.layers[0].pixels);
        p.setPen(QPen(Qt::black, 1));
        p.drawRect(10, 10, 20, 20);
        p.end();
        Canvas c(&d);
        c.resize(300, 300);
        c.show();
        c.fit();
        c.setTool(Canvas::Fill);
        c.foreground = Qt::red;
        QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, at(c, 20, 20));
        QCOMPARE(d.composite().pixelColor(20, 20), QColor(Qt::red));
        QCOMPARE(d.composite().pixelColor(5, 5).alpha(), 0);
        QCOMPARE(d.composite().pixelColor(10, 20), QColor(Qt::black));
    }
    void pickerSeesLastPixelAndZoomKeepsAnchor() {
        Document d;
        d.create({100, 100});
        d.state.layers[0].pixels.setPixelColor(99, 99, QColor(17, 83, 149));
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Picker);
        QSignalSpy spy(&c, &Canvas::colorPicked);
        QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, c.toWidget({99.6, 99.6}).toPoint());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].value<QColor>(), QColor(17, 83, 149));
        QPointF anchor(100, 150), pixel = c.toImage(anchor);
        c.setZoom(2.5, anchor);
        QVERIFY(QLineF(c.toImage(anchor), pixel).length() < .001);
        c.actualSize();
        QCOMPARE(c.zoom, 1.0);
        c.setZoom(100);
        QCOMPARE(c.zoom, 16.0);
        c.setZoom(.0001);
        QCOMPARE(c.zoom, .02);
    }
    void handPanIncludesMouseRelease() {
        Document d;
        d.create({100, 100});
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        QPointF original = c.toWidget({0, 0});
        c.setTool(Canvas::Hand);
        drag(c, {200, 200}, {230, 240}, Qt::NoModifier, false);
        QCOMPARE(c.toWidget({0, 0}), original + QPointF(30, 40));
        QVERIFY(!d.dirty);
    }
    void allMasksKeepTextEditable_data() {
        QTest::addColumn<int>("tool");
        for (int i : {Canvas::Brush, Canvas::Pencil, Canvas::Eraser, Canvas::Fill, Canvas::Gradient,
                      Canvas::Rectangle, Canvas::Ellipse})
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void allMasksKeepTextEditable() {
        QFETCH(int, tool);
        Document d;
        d.create({300, 160});
        QFont font;
        font.setPixelSize(50);
        QString e;
        QVERIFY(d.addText("Editable", font, Qt::red, {0, 0}, &e));
        QVERIFY(d.addMask(&e));
        QImage original = d.state.layers[d.state.active].pixels;
        Canvas c(&d);
        c.resize(600, 400);
        c.show();
        c.fit();
        c.setTool(Canvas::Tool(tool));
        c.foreground = Qt::black;
        c.background = Qt::white;
        c.hardness = 100;
        c.brushSize = 12;
        if (tool == Canvas::Fill)
            QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, at(c, 15, 15));
        else
            drag(c, at(c, 10, 10), at(c, 60, 40));
        QCOMPARE(d.state.layers[d.state.active].text, QString("Editable"));
        QCOMPARE(d.state.layers[d.state.active].pixels, original);
        c.deleteSelection();
        QCOMPARE(d.state.layers[d.state.active].text, QString("Editable"));
    }
    void textPadding_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("italic");
        QTest::addColumn<int>("size");
        QTest::newRow("large italic bearing") << QString("jfj") << true << 512;
        QTest::newRow("Turkish multiline") << QString("Çığ ÖŞÜ\nİstanbul") << false << 80;
        QTest::newRow("regular") << QString("Hello world") << false << 32;
    }
    void textPadding() {
        QFETCH(QString, text);
        QFETCH(bool, italic);
        QFETCH(int, size);
        Document d;
        d.create({100, 100});
        QFont font("Sans Serif");
        font.setPixelSize(size);
        font.setItalic(italic);
        QString e;
        QVERIFY2(d.addText(text, font, Qt::black, {0, 0}, &e), qPrintable(e));
        const QImage im = d.state.layers[d.state.active].pixels;
        int minX = im.width(), minY = im.height(), maxX = -1, maxY = -1;
        for (int y = 0; y < im.height(); ++y)
            for (int x = 0; x < im.width(); ++x)
                if (qAlpha(im.pixel(x, y))) {
                    minX = qMin(minX, x);
                    maxX = qMax(maxX, x);
                    minY = qMin(minY, y);
                    maxY = qMax(maxY, y);
                }
        QVERIFY(maxX > minX);
        QVERIFY2(minX >= 3 && minY >= 3 && maxX < im.width() - 3 && maxY < im.height() - 3,
                 qPrintable(QString("bounds %1 %2 %3 %4 in %5x%6")
                                .arg(minX)
                                .arg(minY)
                                .arg(maxX)
                                .arg(maxY)
                                .arg(im.width())
                                .arg(im.height())));
    }
    void transformsAndMasksRoundTrip() {
        Document d;
        d.create({60, 40}, true);
        QString e;
        QVERIFY(d.addLayer("pattern", &e));
        d.beginEdit();
        QPainter p(&d.state.layers[1].pixels);
        p.fillRect(8, 9, 20, 12, Qt::red);
        p.end();
        d.commit("paint");
        d.selection = {8, 9, 20, 12};
        QVERIFY(d.addMask(&e));
        d.state.layers[1].offset = {3, 4};
        QImage original = d.composite();
        QVERIFY(d.rotate(false, &e));
        QTransform t;
        t.rotate(-90);
        QCOMPARE(d.composite(), original.transformed(t));
        QVERIFY(d.flip(false, &e));
        QCOMPARE(d.composite(), original.transformed(t).transformed(QTransform::fromScale(1, -1)));
        QVERIFY(d.resize({80, 120}, &e));
        QCOMPARE(d.state.layers[1].mask.size(), d.state.layers[1].pixels.size());
        QTemporaryDir dir;
        QVERIFY(d.save(dir.filePath("transformed.pdeck"), &e));
        Document again;
        QVERIFY(again.load(dir.filePath("transformed.pdeck"), &e));
        QCOMPARE(again.composite(), d.composite());
    }
    void layerLifecycleAndOpacity() {
        Document d;
        d.create({8, 8}, true);
        QString e;
        QVERIFY(d.addLayer("red", &e));
        d.beginEdit();
        d.state.layers[1].pixels.fill(Qt::red);
        d.commit("red");
        QVERIFY(d.duplicate(&e));
        QCOMPARE(d.state.layers.size(), 3);
        QCOMPARE(d.state.layers[2].pixels, d.state.layers[1].pixels);
        d.reorder(-1);
        QCOMPARE(d.state.active, 1);
        d.deleteLayer();
        QCOMPARE(d.state.layers.size(), 2);
        d.undo();
        QCOMPARE(d.state.layers.size(), 3);
        d.redo();
        QCOMPARE(d.state.layers.size(), 2);
        d.state.layers[1].opacity = .5;
        QColor half = d.composite().pixelColor(1, 1);
        QVERIFY(qAbs(half.green() - 128) <= 1);
        d.state.layers[1].visible = false;
        QCOMPARE(d.composite().pixelColor(1, 1), QColor(Qt::white));
        d.state.layers[1].visible = true;
        d.state.layers[1].locked = true;
        d.beginEdit();
        d.adjust(100, 100, 100);
        d.commit("locked");
        QCOMPARE(d.state.layers[1].brightness, 0);
    }
    void supportedRasterFormats_data() {
        QTest::addColumn<QString>("extension");
        for (auto ext : {"png", "jpg", "bmp", "webp"})
            QTest::newRow(ext) << QString(ext);
    }
    void supportedRasterFormats() {
        QFETCH(QString, extension);
        QTemporaryDir dir;
        Document d;
        d.create({64, 40});
        d.state.layers[0].pixels.fill(QColor(80, 140, 200));
        QString e;
        QString path = dir.filePath("Türkçe görsel." + extension);
        QVERIFY2(d.exportImage(path, 100, &e), qPrintable(e));
        Document read;
        QVERIFY2(read.importImage(path, false, &e), qPrintable(e));
        QCOMPARE(read.state.size, d.state.size);
        QColor c = read.composite().pixelColor(20, 20);
        QVERIFY(qAbs(c.red() - 80) <= 3);
        QVERIFY(qAbs(c.green() - 140) <= 3);
        QVERIFY(qAbs(c.blue() - 200) <= 3);
    }
    void coordinateBoundsFailWithoutChangingDocument() {
        Document d;
        d.create({2, 2});
        d.state.layers[0].offset = {Document::MaxCoordinate, Document::MaxCoordinate};
        QImage image = d.composite();
        State original = d.state;
        QString e;
        QVERIFY(!d.resize({100, 100}, &e));
        QCOMPARE(d.state.size, original.size);
        QCOMPARE(d.state.layers[0].offset, original.layers[0].offset);
        QCOMPARE(d.composite(), image);
        QVERIFY(!d.dirty);
        QTemporaryDir dir;
        QVERIFY(d.save(dir.filePath("valid.pdeck"), &e));
        d.state.layers[0].offset.setX(Document::MaxCoordinate + 1);
        QVERIFY(!d.save(dir.filePath("valid.pdeck"), &e));
        Document restored;
        QVERIFY(restored.load(dir.filePath("valid.pdeck"), &e));
        QCOMPARE(restored.state.layers[0].offset, original.layers[0].offset);
    }
    void documentMenuFileWorkflow() {
        QTemporaryDir dir;
        MainWindow w(false, false);
        w.show();
        QVERIFY(driveModal([&] { command(w, "new")->trigger(); },
                           [](QDialog *dialog) {
                               if (qobject_cast<QFileDialog *>(dialog))
                                   return false;
                               auto spins = dialog->findChildren<QSpinBox *>();
                               if (spins.size() != 2)
                                   return false;
                               spins[0]->setValue(128);
                               spins[1]->setValue(96);
                               dialog->findChild<QLineEdit *>()->setText("Çalışma");
                               dialog->accept();
                               return true;
                           }));
        QCOMPARE(w.currentDocument()->state.size, QSize(128, 96));
        QCOMPARE(w.currentDocument()->title, QString("Çalışma"));
        auto *d = w.currentDocument();
        d->beginEdit();
        d->state.layers[0].pixels.fill(Qt::red);
        d->commit("red");
        QString project = dir.filePath("Çalışma.pdeck");
        QVERIFY(driveModal([&] { command(w, "save")->trigger(); },
                           [&](QDialog *dialog) {
                               auto *file = qobject_cast<QFileDialog *>(dialog);
                               if (!file)
                                   return false;
                               chooseFile(file, project);
                               return true;
                           }));
        QVERIFY(QFileInfo::exists(project));
        QVERIFY(!d->dirty);
        QCOMPARE(d->path, project);
        QString copy = dir.filePath("copy.pdeck");
        QVERIFY(driveModal([&] { command(w, "saveAs")->trigger(); },
                           [&](QDialog *dialog) {
                               auto *file = qobject_cast<QFileDialog *>(dialog);
                               if (!file)
                                   return false;
                               chooseFile(file, copy);
                               return true;
                           }));
        QCOMPARE(d->path, copy);
        QString png = dir.filePath("export.png");
        QVERIFY(driveModal([&] { command(w, "export")->trigger(); },
                           [&](QDialog *dialog) {
                               auto *file = qobject_cast<QFileDialog *>(dialog);
                               if (!file)
                                   return false;
                               chooseFile(file, png);
                               return true;
                           }));
        QCOMPARE(QImage(png).pixelColor(20, 20), QColor(Qt::red));
        command(w, "close")->trigger();
        QVERIFY(!w.currentDocument());
        QVERIFY(driveModal([&] { command(w, "open")->trigger(); },
                           [&](QDialog *dialog) {
                               auto *file = qobject_cast<QFileDialog *>(dialog);
                               if (!file)
                                   return false;
                               chooseFile(file, project);
                               return true;
                           }));
        QCOMPARE(w.currentDocument()->composite().pixelColor(20, 20), QColor(Qt::red));
        QImage layer(24, 20, QImage::Format_ARGB32);
        layer.fill(Qt::green);
        QString image = dir.filePath("image.png");
        QVERIFY(layer.save(image));
        QVERIFY(driveModal([&] { command(w, "import")->trigger(); },
                           [&](QDialog *dialog) {
                               auto *file = qobject_cast<QFileDialog *>(dialog);
                               if (!file)
                                   return false;
                               chooseFile(file, image);
                               return true;
                           }));
        QCOMPARE(w.currentDocument()->state.layers.size(), 2);
        QCOMPARE(w.currentDocument()->state.layers[1].pixels.convertToFormat(QImage::Format_ARGB32),
                 layer);
        command(w, "undo")->trigger();
        QCOMPARE(w.currentDocument()->state.layers.size(), 1);
        command(w, "redo")->trigger();
        QCOMPARE(w.currentDocument()->state.layers.size(), 2);
    }
    void clipboardSelectionAndLayerCommands() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({80, 60}, true);
        w.addDocument(d);
        command(w, "addLayer")->trigger();
        d->beginEdit();
        d->state.layers[1].pixels.fill(Qt::red);
        d->commit("paint");
        command(w, "duplicate")->trigger();
        QCOMPARE(d->state.layers.size(), 3);
        command(w, "layerDown")->trigger();
        QCOMPARE(d->state.active, 1);
        command(w, "layerUp")->trigger();
        QCOMPARE(d->state.active, 2);
        command(w, "deleteLayer")->trigger();
        QCOMPARE(d->state.layers.size(), 2);
        QVERIFY(driveModal([&] { command(w, "rename")->trigger(); },
                           [](QDialog *dialog) {
                               auto *input = qobject_cast<QInputDialog *>(dialog);
                               if (!input)
                                   return false;
                               input->setTextValue("Renamed");
                               input->accept();
                               return true;
                           }));
        QCOMPARE(d->state.layers[1].name, QString("Renamed"));
        command(w, "selectAll")->trigger();
        QCOMPARE(d->selection, QRect(0, 0, 80, 60));
        command(w, "deselect")->trigger();
        QVERIFY(d->selection.isEmpty());
        d->selection = {10, 10, 12, 15};
        command(w, "copy")->trigger();
        QImage clipboard = QApplication::clipboard()->image();
        QCOMPARE(clipboard.size(), QSize(12, 15));
        QCOMPARE(clipboard.pixelColor(3, 3), QColor(Qt::red));
        command(w, "paste")->trigger();
        QCOMPARE(d->state.layers.size(), 3);
        QCOMPARE(d->state.layers[2].pixels.size(), QSize(12, 15));
        command(w, "undo")->trigger();
        d->selection = {10, 10, 12, 15};
        command(w, "lock")->trigger();
        QVERIFY(d->state.layers[1].locked);
        QImage before = d->state.layers[1].pixels;
        command(w, "deleteSelection")->trigger();
        QCOMPARE(d->state.layers[1].pixels, before);
        command(w, "lock")->trigger();
        QVERIFY(!d->state.layers[1].locked);
        command(w, "deleteSelection")->trigger();
        QCOMPARE(d->state.layers[1].pixels.pixelColor(12, 12).alpha(), 0);
        QCOMPARE(d->state.layers[1].pixels.pixelColor(2, 2), QColor(Qt::red));
        command(w, "undo")->trigger();
        QCOMPARE(d->state.layers[1].pixels, before);
        command(w, "mask")->trigger();
        QVERIFY(!d->state.layers[1].mask.isNull());
        QVERIFY(d->editingMask);
        command(w, "invertMask")->trigger();
        QCOMPARE(d->composite().pixelColor(12, 12), QColor(Qt::white));
        command(w, "removeMask")->trigger();
        QVERIFY(d->state.layers[1].mask.isNull());
        command(w, "undo")->trigger();
        QVERIFY(!d->state.layers[1].mask.isNull());
    }
    void filterTransformAndViewCommands() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({80, 60}, true);
        w.addDocument(d);
        d->beginEdit();
        QPainter p(&d->state.layers[0].pixels);
        p.fillRect(10, 10, 10, 10, Qt::red);
        p.end();
        d->commit("pattern");
        QImage original = d->composite();
        QVERIFY(driveModal([&] { command(w, "blur")->trigger(); },
                           [](QDialog *dialog) {
                               auto *input = qobject_cast<QInputDialog *>(dialog);
                               if (!input)
                                   return false;
                               input->setIntValue(3);
                               input->accept();
                               return true;
                           }));
        QCOMPARE(d->state.layers[0].blur, 3);
        QVERIFY(d->composite() != original);
        command(w, "undo")->trigger();
        QCOMPARE(d->composite(), original);
        command(w, "grayscale")->trigger();
        QCOMPARE(d->state.layers[0].saturation, -100);
        command(w, "undo")->trigger();
        command(w, "rotateRight")->trigger();
        QCOMPARE(d->state.size, QSize(60, 80));
        command(w, "rotateLeft")->trigger();
        QCOMPARE(d->composite(), original);
        command(w, "flipH")->trigger();
        QCOMPARE(d->composite(), original.transformed(QTransform::fromScale(-1, 1)));
        command(w, "flipV")->trigger();
        QCOMPARE(d->composite(), original.transformed(QTransform::fromScale(-1, -1)));
        command(w, "undo")->trigger();
        command(w, "undo")->trigger();
        QVERIFY(driveModal([&] { command(w, "resize")->trigger(); },
                           [](QDialog *dialog) {
                               auto inputs = dialog->findChildren<QSpinBox *>();
                               if (inputs.size() != 2)
                                   return false;
                               inputs[0]->setValue(160);
                               dialog->accept();
                               return true;
                           }));
        QCOMPARE(d->state.size, QSize(160, 120));
        d->selection = {20, 20, 40, 30};
        d->changed();
        command(w, "crop")->trigger();
        QCOMPARE(d->state.size, QSize(40, 30));
        command(w, "actual")->trigger();
        QCOMPARE(w.currentCanvas()->zoom, 1.0);
        command(w, "zoomIn")->trigger();
        QCOMPARE(w.currentCanvas()->zoom, 1.25);
        command(w, "zoomOut")->trigger();
        QCOMPARE(w.currentCanvas()->zoom, 1.0);
        command(w, "fit")->trigger();
        QVERIFY(w.currentCanvas()->zoom > 0);
    }
    void textAndColorDialogs() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({400, 240});
        w.addDocument(d);
        command(w, "tool9")->trigger();
        QVERIFY(driveModal(
            [&] {
                QTest::mouseClick(w.currentCanvas(), Qt::LeftButton, Qt::NoModifier,
                                  at(*w.currentCanvas(), 20, 20));
            },
            [](QDialog *dialog) {
                auto *edit = dialog->findChild<QPlainTextEdit *>();
                if (!edit)
                    return false;
                edit->setPlainText("İlk metin\nİkinci satır");
                dialog->findChild<QSpinBox *>()->setValue(32);
                dialog->accept();
                return true;
            }));
        QCOMPARE(d->state.layers.size(), 2);
        QCOMPARE(d->state.layers[1].text, QString("İlk metin\nİkinci satır"));
        QVERIFY(!d->state.layers[1].pixels.isNull());
        QVERIFY(driveModal([&] { command(w, "editText")->trigger(); },
                           [](QDialog *dialog) {
                               auto *edit = dialog->findChild<QPlainTextEdit *>();
                               if (!edit)
                                   return false;
                               edit->setPlainText("Yeni metin");
                               dialog->accept();
                               return true;
                           }));
        QCOMPARE(d->state.layers[1].text, QString("Yeni metin"));
        command(w, "undo")->trigger();
        QCOMPARE(d->state.layers[1].text, QString("İlk metin\nİkinci satır"));
        QVERIFY(driveModal([&] { w.findChild<QToolButton *>("foreground")->click(); },
                           [](QDialog *dialog) {
                               auto *color = qobject_cast<QColorDialog *>(dialog);
                               if (!color)
                                   return false;
                               color->setCurrentColor(QColor("#123456"));
                               color->accept();
                               return true;
                           }));
        QCOMPARE(w.currentCanvas()->foreground, QColor("#123456"));
        QColor background = w.currentCanvas()->background;
        command(w, "swapColors")->trigger();
        QCOMPARE(w.currentCanvas()->foreground, background);
        QCOMPARE(w.currentCanvas()->background, QColor("#123456"));
    }
    void tabsDropFilmstripAndBrushPresets() {
        QTemporaryDir dir;
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        QString path = dir.filePath("drop.png");
        QVERIFY(image.save(path));
        MainWindow w(false, false);
        w.show();
        auto *a = new Document;
        a->create({20, 20}, true);
        a->title = "a";
        w.addDocument(a);
        auto *b = new Document;
        b->create({20, 20});
        b->title = "b";
        w.addDocument(b);
        auto *c = new Document;
        c->create({20, 20});
        c->title = "c";
        w.addDocument(c);
        w.applyUiPreset("film", false);
        auto *tabs = w.findChild<QTabWidget *>("documents");
        tabs->setCurrentIndex(0);
        tabs->tabBar()->moveTab(1, 2);
        auto *strip = w.findChild<QListWidget *>("filmstrip");
        QCOMPARE(strip->item(1)->text(), QString("c"));
        strip->setCurrentRow(1);
        QCOMPARE(w.currentDocument(), c);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter(QPoint(30, 30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&w, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop({30, 30}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&w, &drop);
        QCOMPARE(tabs->count(), 4);
        QCOMPARE(w.currentDocument()->state.size, QSize(16, 16));
        w.applyUiPreset("ink", false);
        auto *dock = w.findChild<QDockWidget *>("brushDock");
        for (auto *button : dock->findChildren<QPushButton *>())
            if (button->text() == "Yumuşak") {
                button->click();
                break;
            }
        QCOMPARE(w.currentCanvas()->hardness, 15);
        QCOMPARE(w.currentCanvas()->tool, Canvas::Brush);
        for (auto *button : dock->findChildren<QPushButton *>())
            if (button->text() == "Kalem") {
                button->click();
                break;
            }
        QCOMPARE(w.currentCanvas()->hardness, 100);
        QCOMPARE(w.currentCanvas()->tool, Canvas::Pencil);
        auto palette = dock->findChildren<QToolButton *>();
        QVERIFY(!palette.isEmpty());
        palette[0]->click();
        QCOMPARE(w.currentCanvas()->foreground, QColor("#e8d4b3"));
    }
    void allPresetAndPanelControls() {
        MainWindow w(false, false);
        w.show();
        w.demo();
        QImage source = w.currentDocument()->composite();
        for (const auto &preset : uiPresets()) {
            command(w, qPrintable("ui.preset." + preset.id))->trigger();
            QCOMPARE(w.uiSettings().preset, preset.id);
            QCOMPARE(w.currentDocument()->composite(), source);
            for (auto panel : {"tools", "layers", "color", "history", "film", "brush"}) {
                auto *a = command(w, qPrintable(QString("ui.panel.") + panel));
                a->setChecked(false);
                QWidget *widget = preset.id == "focus" && QString(panel) == "tools"
                                      ? static_cast<QWidget *>(w.findChild<QFrame *>("floatingTools"))
                                  : preset.id == "focus" && QString(panel) == "layers"
                                      ? static_cast<QWidget *>(w.findChild<QFrame *>("floatingLayers"))
                                      : w.findChild<QDockWidget *>(QString(panel) + "Dock");
                QVERIFY(widget);
                QVERIFY(widget->isHidden());
                a->setChecked(true);
                QVERIFY(!widget->isHidden());
            }
        }
        command(w, "ui.reset")->trigger();
        QCOMPARE(w.uiSettings().preset, QString("focus"));
    }
    void helpAndCloseControls() {
        MainWindow w(false, false);
        w.show();
        w.demo();
        QVERIFY(driveModal([&] { command(w, "help")->trigger(); },
                           [](QDialog *dialog) {
                               auto *box = qobject_cast<QMessageBox *>(dialog);
                               if (!box)
                                   return false;
                               box->accept();
                               return true;
                           }));
        int aboutSeen = 0;
        QTimer aboutCloser;
        aboutCloser.setInterval(10);
        connect(&aboutCloser, &QTimer::timeout, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget))
                    if (box->isVisible() && box->text().contains("PhotoDeck")) {
                        ++aboutSeen;
                        box->accept();
                    }
        });
        aboutCloser.start();
        command(w, "about")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(aboutSeen > 0, 5000);
        aboutCloser.stop();
        command(w, "demo")->trigger();
        QCOMPARE(w.findChild<QTabWidget *>("documents")->count(), 2);
        command(w, "close")->trigger();
        QCOMPARE(w.findChild<QTabWidget *>("documents")->count(), 1);
        command(w, "quit")->trigger();
        QVERIFY(!w.isVisible());
    }
    void largeProjectsRetainIncrementalUndo() {
        Document d;
        QVERIFY(d.create({3072, 3072}));
        QString e;
        for (int i = 0; i < 3; ++i)
            QVERIFY(d.addLayer("large", &e));
        QVERIFY(d.imageBytes() > Document::HistoryBudget);
        d.beginEdit();
        QPainter p(&d.state.layers[d.state.active].pixels);
        p.fillRect(0, 0, 10, 10, Qt::red);
        p.end();
        d.commit("paint");
        QVERIFY(d.canUndo());
        d.undo();
        QCOMPARE(d.state.layers[d.state.active].pixels.pixelColor(2, 2).alpha(), 0);
    }
    void gesturesFinishOnFocusChange() {
        Document d;
        d.create({100, 100});
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Brush);
        c.foreground = Qt::red;
        QTest::mousePress(&c, Qt::LeftButton, Qt::NoModifier, at(c, 30, 30));
        QVERIFY(d.isEditing());
        QFocusEvent event(QEvent::FocusOut, Qt::TabFocusReason);
        QApplication::sendEvent(&c, &event);
        QVERIFY(!d.isEditing());
        QVERIFY(d.canUndo());
    }
    void projectFilenameSuffixRespectsOverwriteCancellation() {
        QTemporaryDir dir;
        QString path = dir.filePath("existing.pdeck");
        Document sentinel;
        sentinel.create({8, 8}, true);
        QString e;
        QVERIFY(sentinel.save(path, &e));
        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QByteArray before = saved.readAll();
        saved.close();
        MainWindow w(false, false);
        auto *d = new Document;
        d->create({16, 16});
        w.addDocument(d);
        int prompts = 0;
        QVERIFY(driveModal(
            [&] { command(w, "save")->trigger(); },
            [&](QDialog *dialog) {
                auto *file = qobject_cast<QFileDialog *>(dialog);
                if (!file)
                    return false;
                QTimer::singleShot(20, [&] {
                    if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                        ++prompts;
                        box->button(QMessageBox::No)->click();
                    }
                    if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
                        picker->reject();
                });
                chooseFile(file, dir.filePath("existing"));
                return true;
            }));
        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), before);
        QCOMPARE(prompts, 1);
    }
    void fillFlowBlendsOverExistingPixels() {
        Document d;
        d.create({20, 20});
        d.state.layers[0].pixels.fill(Qt::red);
        Canvas c(&d);
        c.resize(300, 300);
        c.show();
        c.fit();
        c.setTool(Canvas::Fill);
        c.foreground = Qt::blue;
        c.brushOpacity = 50;
        QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, at(c, 10, 10));
        QColor color = d.composite().pixelColor(10, 10);
        QCOMPARE(color.alpha(), 255);
        QVERIFY(qAbs(color.red() - 128) <= 1);
        QVERIFY(qAbs(color.blue() - 128) <= 1);
    }
    void customPresetsAndSettingsSurviveRestart() {
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/ui.ini";
        QFile existing(path);
        bool had = existing.exists();
        QByteArray backup;
        if (had) {
            QVERIFY(existing.open(QIODevice::ReadOnly));
            backup = existing.readAll();
            existing.close();
        }
        QFile::remove(path);
        UiSettings custom = UiSettings::forPreset("paper");
        custom.accent = QColor("#813456");
        custom.canvas = QColor("#d6e2d4");
        custom.panel = QColor("#e0dfd2");
        custom.background = QColor("#dae0dc");
        custom.fontSize = 15;
        custom.iconSize = 26;
        custom.radius = 11;
        custom.panelWidth = 320;
        custom.rulers = false;
        custom.toolLabels = true;
        {
            MainWindow w(false, true);
            w.show();
            w.demo();
            w.applyUiSettings(custom);
            QVERIFY(driveModal([&] { command(w, "ui.savePreset")->trigger(); },
                               [](QDialog *dialog) {
                                   auto *input = qobject_cast<QInputDialog *>(dialog);
                                   if (!input)
                                       return false;
                                   input->setTextValue("Audit theme");
                                   input->accept();
                                   return true;
                               }));
            w.close();
        }
        {
            MainWindow w(false, true);
            w.show();
            w.demo();
            QCOMPARE(w.uiSettings().preset, custom.preset);
            QCOMPARE(w.uiSettings().accent, custom.accent);
            QCOMPARE(w.uiSettings().canvas, custom.canvas);
            QCOMPARE(w.uiSettings().panel, custom.panel);
            QCOMPARE(w.uiSettings().fontSize, custom.fontSize);
            QCOMPARE(w.uiSettings().iconSize, custom.iconSize);
            QCOMPARE(w.uiSettings().radius, custom.radius);
            QVERIFY(!w.currentCanvas()->showRulers);
            QAction *saved = nullptr;
            for (auto *menu : w.findChildren<QMenu *>())
                for (auto *a : menu->actions())
                    if (a->text() == "Audit theme")
                        saved = a;
            QVERIFY(saved);
            w.applyUiPreset("focus");
            saved->trigger();
            QCOMPARE(w.uiSettings().accent, custom.accent);
            QFile before(path);
            QVERIFY(before.open(QIODevice::ReadOnly));
            QByteArray snapshot = before.readAll();
            before.close();
            QVERIFY(driveModal([&] { command(w, "ui.customize")->trigger(); },
                               [](QDialog *dialog) {
                                   auto *font = dialog->findChild<QSpinBox *>("uiFontSize");
                                   if (!font)
                                       return false;
                                   font->setValue(19);
                                   dialog->reject();
                                   return true;
                               }));
            QTest::qWait(400);
            QFile after(path);
            QVERIFY(after.open(QIODevice::ReadOnly));
            QCOMPARE(after.readAll(), snapshot);
            after.close();
            QTemporaryDir temp;
            QString preset = temp.filePath("view.pdui");
            QVERIFY(driveModal([&] { command(w, "ui.exportPreset")->trigger(); },
                               [&](QDialog *dialog) {
                                   auto *file = qobject_cast<QFileDialog *>(dialog);
                                   if (!file)
                                       return false;
                                   chooseFile(file, preset);
                                   return true;
                               }));
            QVERIFY(QFileInfo::exists(preset));
            w.applyUiPreset("focus");
            QVERIFY(driveModal([&] { command(w, "ui.importPreset")->trigger(); },
                               [&](QDialog *dialog) {
                                   auto *file = qobject_cast<QFileDialog *>(dialog);
                                   if (!file)
                                       return false;
                                   chooseFile(file, preset);
                                   return true;
                               }));
            QCOMPARE(w.uiSettings().accent, custom.accent);
            w.close();
        }
        if (had) {
            QSaveFile restore(path);
            QVERIFY(restore.open(QIODevice::WriteOnly));
            QCOMPARE(restore.write(backup), qint64(backup.size()));
            QVERIFY(restore.commit());
        } else
            QFile::remove(path);
    }
    void batchResizeColorsAndNoUpscaling() {
        QTemporaryDir dir;
        QString output = dir.filePath("output");
        QVERIFY(QDir().mkpath(output));
        QImage big(240, 120, QImage::Format_RGB32);
        big.fill(QColor(210, 80, 30));
        QImage small(40, 20, QImage::Format_RGB32);
        small.fill(QColor(20, 140, 220));
        QString first = dir.filePath("big.png"), second = dir.filePath("small.png");
        QVERIFY(big.save(first));
        QVERIFY(small.save(second));
        MainWindow w(false, false);
        w.show();
        int stage = 0;
        QVERIFY(driveModal([&] { command(w, "batch")->trigger(); },
                           [&](QDialog *dialog) {
                               if (auto *file = qobject_cast<QFileDialog *>(dialog)) {
                                   if (stage == 0) {
                                       file->setDirectory(dir.path());
                                       file->selectFile(first);
                                       auto *name = file->findChild<QLineEdit *>("fileNameEdit");
                                       name->setFocus();
                                       name->selectAll();
                                       QInputMethodEvent event;
                                       event.setCommitString("\"" + first + "\" \"" + second + "\"");
                                       QApplication::sendEvent(name, &event);
                                       ++stage;
                                       QMetaObject::invokeMethod(file, "accept");
                                       return true;
                                   }
                                   if (stage == 1) {
                                       file->setDirectory(output);
                                       file->selectFile(output);
                                       ++stage;
                                       QMetaObject::invokeMethod(file, "accept");
                                       return true;
                                   }
                               }
                               if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
                                   box->accept();
                                   ++stage;
                                   return true;
                               }
                               auto spin = dialog->findChildren<QSpinBox *>();
                               if (stage == 2 && spin.size() == 5) {
                                   spin[0]->setValue(120);
                                   spin[1]->setValue(100);
                                   spin[4]->setValue(-100);
                                   dialog->findChild<QComboBox *>()->setCurrentIndex(1);
                                   ++stage;
                                   dialog->accept();
                                   return true;
                               }
                               return false;
                           }));
        QCOMPARE(stage, 4);
        QImage converted(output + "/big_photodeck.jpg"), unchangedSize(output + "/small_photodeck.jpg");
        QCOMPARE(converted.size(), QSize(120, 60));
        QCOMPARE(unchangedSize.size(), QSize(40, 20));
        QColor gray = converted.pixelColor(30, 30);
        QVERIFY(qAbs(gray.red() - gray.green()) <= 1);
        QVERIFY(qAbs(gray.green() - gray.blue()) <= 1);
        QCOMPARE(QImage(first), big);
        QCOMPARE(QImage(second), small);
    }
    void focusPanelDragAndMovability() {
        MainWindow w(false, false);
        w.resize(1380, 880);
        w.show();
        w.demo();
        auto *panel = w.findChild<QFrame *>("floatingLayers");
        auto *handle = panel->findChild<QToolButton *>();
        QPoint old = panel->pos();
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, {10, 10});
        QTest::mouseMove(handle, {-30, 30});
        QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, {-30, 30});
        QVERIFY(panel->pos() != old);
        QVERIFY(w.findChild<QWidget *>("workspace")->rect().contains(panel->geometry()));
        auto settings = w.uiSettings();
        settings.movablePanels = false;
        w.applyUiSettings(settings, false, false);
        QVERIFY(!handle->isEnabled());
        for (auto *dock : w.findChildren<QDockWidget *>())
            QVERIFY(!(dock->features() & QDockWidget::DockWidgetMovable));
    }
    void transparentFormatsPreserveAlpha_data() {
        QTest::addColumn<QString>("extension");
        QTest::newRow("png") << QString("png");
        QTest::newRow("webp") << QString("webp");
    }
    void transparentFormatsPreserveAlpha() {
        QFETCH(QString, extension);
        QTemporaryDir dir;
        Document d;
        d.create({32, 32});
        QPainter painter(&d.state.layers[0].pixels);
        painter.fillRect(8, 8, 16, 16, QColor(80, 160, 220, 128));
        painter.end();
        QString e, path = dir.filePath("alpha." + extension);
        QVERIFY2(d.exportImage(path, 100, &e), qPrintable(e));
        QImage read(path);
        QVERIFY(read.hasAlphaChannel());
        QCOMPARE(read.pixelColor(2, 2).alpha(), 0);
        QCOMPARE(read.pixelColor(12, 12).alpha(), 128);
    }
    void singleBrushStampRespectsFlow() {
        Document d;
        d.create({100, 100});
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Brush);
        c.foreground = Qt::red;
        c.hardness = 100;
        c.brushSize = 10;
        c.brushOpacity = 50;
        QTest::mouseClick(&c, Qt::LeftButton, Qt::NoModifier, at(c, 50, 50));
        int a = d.composite().pixelColor(50, 50).alpha();
        QVERIFY2(a >= 126 && a <= 128, qPrintable(QString::number(a)));
    }
};
QTEST_MAIN(FeatureAudit)
#include "test_features.moc"
