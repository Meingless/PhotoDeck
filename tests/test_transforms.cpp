#include "mainwindow.h"
#include "transformdialog.h"
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

namespace {
void pattern(Document &d) {
    d.beginEdit();
    d.state.layers[0].pixels.fill(Qt::transparent);
    QPainter p(&d.state.layers[0].pixels);
    p.fillRect(0, 0, 10, 8, Qt::red);
    p.fillRect(10, 0, 10, 8, Qt::blue);
    p.end();
    d.commit("Pattern fixture");
    d.markSaved();
}
QImage reference(const Layer &l, QSize canvas, QTransform matrix) {
    QImage image(canvas, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(l.offset);
    p.setTransform(matrix, true);
    p.drawImage(QPoint(), l.pixels);
    p.end();
    return image;
}
} // namespace
class TransformTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void sourcePreservedAndIndependentRenderMatches() {
        Document d;
        QVERIFY(d.create({60, 40}));
        pattern(d);
        QString error;
        QImage pixels = d.state.layers[0].pixels;
        QTransform matrix;
        matrix.translate(12, 4);
        matrix.scale(.5, .5);
        d.beginEdit();
        QVERIFY(d.setLayerTransform(matrix, &error));
        d.commit("Transform");
        QCOMPARE(d.state.layers[0].pixels, pixels);
        QCOMPARE(d.composite(), reference(d.state.layers[0], d.state.size, matrix));
        QImage transformed = d.composite();
        d.undo();
        QCOMPARE(d.state.layers[0].geometry, QTransform());
        d.redo();
        QCOMPARE(d.composite(), transformed);
        d.beginEdit();
        QVERIFY(d.setLayerTransform({}));
        d.commit("Reset");
        QCOMPARE(d.state.layers[0].pixels, pixels);
        QCOMPARE(d.composite(), pixels);
    }
    void scaleRotateSkewAndProjective_data() {
        QTest::addColumn<QTransform>("matrix");
        QTransform scale;
        scale.translate(20, 8);
        scale.scale(.5, .5);
        QTest::newRow("scale") << scale;
        QTransform rotate;
        rotate.translate(30, 20);
        rotate.rotate(27);
        rotate.translate(-30, -20);
        QTest::newRow("rotation") << rotate;
        QTransform skew;
        skew.shear(.2, .1);
        QTest::newRow("skew") << skew;
        QTransform flip(-1, 0, 0, 1, 60, 0);
        QTest::newRow("mirror") << flip;
        QTransform projective;
        QVERIFY(QTransform::quadToQuad(QPolygonF{{0, 0}, {60, 0}, {60, 40}, {0, 40}},
                                       QPolygonF{{10, 2}, {55, 10}, {50, 35}, {5, 38}}, projective));
        QTest::newRow("perspective") << projective;
    }
    void scaleRotateSkewAndProjective() {
        QFETCH(QTransform, matrix);
        Document d;
        QVERIFY(d.create({60, 40}));
        pattern(d);
        d.beginEdit();
        QVERIFY(d.setLayerTransform(matrix));
        d.commit("Pose");
        QImage actual = d.composite(), expected = reference(d.state.layers[0], d.state.size, matrix);
        // Raster engines can round the translated projective bounding box differently by one level.
        int significant = 0;
        for (int y = 0; y < actual.height(); ++y)
            for (int x = 0; x < actual.width(); ++x) {
                QColor a = actual.pixelColor(x, y), b = expected.pixelColor(x, y);
                if (qAbs(a.alpha() - b.alpha()) > 2 ||
                    (a.alpha() > 10 && b.alpha() > 10 &&
                     (qAbs(a.red() - b.red()) > 4 || qAbs(a.blue() - b.blue()) > 4)))
                    ++significant;
            }
        QVERIFY2(significant <= 3, qPrintable(QString::number(significant)));
        QCOMPARE(d.state.layers[0].pixels.size(), QSize(60, 40));
    }
    void invalidTransformsNeverMutate() {
        Document d;
        d.create({10, 10}, true);
        const QImage original = d.composite();
        QString error;
        for (QTransform t : {QTransform(0, 0, 0, 0, 0, 0), QTransform::fromScale(10000, 10000),
                             QTransform(1, 0, -.2, 0, 1, 0, 0, 0, 1),
                             QTransform::fromTranslate(Document::MaxCoordinate + 1.0, 0),
                             QTransform(std::numeric_limits<double>::quiet_NaN(), 0, 0, 1, 0, 0)}) {
            QVERIFY(!d.setLayerTransform(t, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(d.composite(), original);
        }
        d.state.layers[0].locked = true;
        QVERIFY(!d.setLayerTransform(QTransform::fromScale(.5, .5)));
    }
    void aggregateRenderBudgetAndInsertionValidation() {
        Document d;
        QVERIFY(d.create({64, 64}));
        QString error;
        for (int i = 0; i < 3; ++i)
            QVERIFY(d.addLayer("Small", &error));
        for (int i = 0; i < 4; ++i) {
            d.state.active = i;
            d.beginEdit();
            QVERIFY(d.setLayerTransform(QTransform::fromScale(64, 64), &error));
            d.commit("Scale");
        }
        const int count = d.historyLabels().size();
        QVERIFY(!d.addLayer("Too much derived memory", &error));
        QCOMPARE(d.state.layers.size(), 4);
        QVERIFY(!d.duplicate(&error));
        QCOMPARE(d.state.layers.size(), 4);
        QCOMPARE(d.historyLabels().size(), count);
        Layer bad;
        bad.pixels = QImage(4, 4, QImage::Format_RGB32);
        bad.mask = QImage(3, 3, QImage::Format_RGB32);
        QVERIFY(!d.insertLayer(bad, "Invalid", &error));
        QCOMPARE(d.state.layers.size(), 4);
        d.state.active = 0;
        d.beginEdit();
        QVERIFY(d.setLayerTransform({}));
        d.commit("Reset");
        Layer rgb;
        rgb.pixels = QImage(4, 4, QImage::Format_RGB32);
        rgb.pixels.fill(Qt::red);
        QVERIFY(d.insertLayer(rgb, "RGB input", &error));
        QCOMPARE(d.state.layers[d.state.active].pixels.format(), QImage::Format_ARGB32_Premultiplied);
    }
    void transformedTextAndMaskRetainIdentity() {
        Document d;
        d.create({100, 70});
        QString error;
        QVERIFY(d.addText("Türkçe İŞ", QFont("Sans", 16), Qt::red, {10, 10}, &error));
        Layer original = d.state.layers[d.state.active];
        d.beginEdit();
        QVERIFY(d.setLayerTransform(QTransform::fromScale(.6, .6)));
        d.commit("Scale");
        d.selection = {10, 10, 12, 12};
        QVERIFY(d.addMask(&error));
        const Layer &l = d.state.layers[d.state.active];
        QCOMPARE(l.text, original.text);
        QCOMPARE(l.pixels, original.pixels);
        QCOMPARE(qRed(l.mask.pixel(0, 0)), 255);
        if (l.mask.width() > 30)
            QCOMPARE(qRed(l.mask.pixel(30, 0)), 0);
        QVERIFY(d.editText("Yeni Türkçe", QFont("Sans", 16), Qt::blue, &error));
        QCOMPARE(d.state.layers[d.state.active].geometry, QTransform::fromScale(.6, .6));
        QVERIFY(!d.state.layers[d.state.active].mask.isNull());
    }
    void rasterizeIsExplicitReversibleAndKeepsVisibleResult() {
        Document d;
        d.create({60, 40});
        pattern(d);
        QString error;
        QVERIFY(d.addMask(&error));
        d.state.layers[0].mask.fill(QColor(100, 100, 100));
        d.beginEdit();
        QVERIFY(d.setLayerTransform(QTransform::fromScale(.7, .7)));
        d.commit("Transform");
        const QImage before = d.composite(), source = d.state.layers[0].pixels;
        QVERIFY(d.rasterizeLayer(&error));
        QCOMPARE(d.composite(), before);
        QVERIFY(d.state.layers[0].geometry.isIdentity());
        QVERIFY(d.state.layers[0].mask.isNull());
        d.undo();
        QCOMPARE(d.state.layers[0].pixels, source);
        QVERIFY(!d.state.layers[0].mask.isNull());
        QCOMPARE(d.composite(), before);
    }
    void pixelPaintingCannotSilentlyDiscardSourceTransform() {
        Document d;
        d.create({60, 40});
        pattern(d);
        d.beginEdit();
        d.setLayerTransform(QTransform::fromScale(.7, .7));
        d.commit("Scale");
        Canvas c(&d);
        c.resize(500, 400);
        c.show();
        c.fit();
        c.setTool(Canvas::Brush);
        QImage before = d.state.layers[0].pixels;
        int history = d.historyLabels().size();
        QTest::mouseClick(&c, Qt::LeftButton, {}, c.toWidget({5, 5}).toPoint());
        c.deleteSelection();
        QCOMPARE(d.state.layers[0].pixels, before);
        QCOMPARE(d.historyLabels().size(), history);
        QVERIFY(d.rasterizeLayer());
        QImage raster = d.state.layers[0].pixels;
        c.foreground = Qt::green;
        QTest::mouseClick(&c, Qt::LeftButton, {}, c.toWidget({5, 5}).toPoint());
        QVERIFY(d.state.layers[0].pixels != raster);
    }
    void canvasAnchorsPreserveSourceAndOffCanvasPixels() {
        Document d;
        d.create({20, 10}, true);
        QImage original = d.state.layers[0].pixels;
        QString error;
        QVERIFY(d.canvasSize({40, 30}, {1, 1}, &error));
        QCOMPARE(d.state.layers[0].offset, QPoint(10, 10));
        QCOMPARE(d.state.layers[0].pixels, original);
        QCOMPARE(d.composite().pixelColor(10, 10), QColor(Qt::white));
        QVERIFY(d.canvasSize({10, 5}, {2, 2}, &error));
        QCOMPARE(d.state.layers[0].pixels, original);
        d.undo();
        QCOMPARE(d.state.size, QSize(40, 30));
        d.undo();
        QCOMPARE(d.state.size, QSize(20, 10));
        QVERIFY(!d.canvasSize({40000, 1}, {1, 1}, &error));
        QCOMPARE(d.state.size, QSize(20, 10));
    }
    void wholeImageOperationsRemainValidAfterGeometry() {
        Document d;
        d.create({60, 40});
        pattern(d);
        QString error;
        d.beginEdit();
        d.setLayerTransform(QTransform::fromScale(.5, .5));
        d.commit("Geometry");
        QImage original = d.composite();
        QVERIFY(d.rotate(true, &error));
        QCOMPARE(d.composite(), original.transformed(QTransform().rotate(90)));
        QVERIFY(d.flip(true, &error));
        QVERIFY(d.resize({80, 120}, &error));
        QTemporaryDir dir;
        QVERIFY(d.save(dir.filePath("image.pdeck"), &error));
        Document read;
        QVERIFY(read.load(dir.filePath("image.pdeck"), &error));
        QCOMPARE(read.composite(), d.composite());
    }
    void mergePreservesHiddenLayersOffCanvasAndDissolve() {
        Document d;
        d.create({60, 40});
        d.state.layers[0].pixels.fill(Qt::red);
        d.state.layers[0].offset = {-10, -8};
        QString error;
        QVERIFY(d.addLayer("Hidden", &error));
        d.state.layers[1].visible = false;
        d.state.layers[1].pixels.fill(Qt::green);
        Layer hidden = d.state.layers[1];
        QVERIFY(d.addLayer("Dissolve", &error));
        d.state.layers[2].pixels.fill(QColor(10, 80, 240, 100));
        d.beginEdit();
        d.state.layers[2].blend = 6;
        d.state.layers[2].offset = {8, 5};
        d.commit("Dissolve fixture");
        QImage before = d.composite();
        QVERIFY(d.mergeVisible(&error));
        QCOMPARE(d.composite(), before);
        QCOMPARE(d.state.layers.size(), 2);
        QVERIFY(!d.state.layers[0].visible);
        QCOMPARE(d.state.layers[0].pixels, hidden.pixels);
        QCOMPARE(d.state.layers[1].offset, QPoint(-10, -8));
        QVERIFY(d.state.layers[1].pixels.width() > d.state.size.width());
        d.undo();
        QCOMPARE(d.state.layers.size(), 3);
        QCOMPARE(d.composite(), before);
        QVERIFY(d.flatten(&error));
        QCOMPARE(d.state.layers.size(), 1);
        QCOMPARE(d.composite(), before);
    }
    void excessiveMergeFailsWithoutChangingState() {
        Document d;
        d.create({10, 10}, true);
        QString error;
        d.addLayer("Far", &error);
        d.state.layers[1].offset = {20000, 0};
        const QImage before = d.composite();
        const int count = d.historyLabels().size();
        QVERIFY(!d.mergeVisible(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(d.state.layers.size(), 2);
        QCOMPARE(d.historyLabels().size(), count);
        QCOMPARE(d.composite(), before);
    }
    void v3RoundTripAndActualV2Fixture() {
        Document d;
        d.create({60, 40});
        pattern(d);
        QString error;
        QTransform t;
        t.translate(12, 5);
        t.rotate(18);
        t.scale(.7, .7);
        d.beginEdit();
        QVERIFY(d.setLayerTransform(t));
        d.commit("Pose");
        QTemporaryDir dir;
        QString file = dir.filePath("pose.pdeck");
        QVERIFY(d.save(file, &error));
        Document loaded;
        QVERIFY(loaded.load(file, &error));
        QCOMPARE(loaded.state.layers[0].geometry, t);
        QCOMPARE(loaded.state.layers[0].pixels, d.state.layers[0].pixels);
        QCOMPARE(loaded.composite(), d.composite());
        QString legacy = QFINDTESTDATA("fixtures/v2-demo.pdeck");
        QVERIFY(!legacy.isEmpty());
        QVERIFY2(loaded.load(legacy, &error), qPrintable(error));
        QCOMPARE(loaded.state.layers.size(), 4);
        for (const auto &l : loaded.state.layers)
            QVERIFY(l.geometry.isIdentity());
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray data = f.readAll();
        f.close();
        // Truncated v3 matrix/image records must not replace an already open document.
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data.left(data.size() - 3));
        f.close();
        QImage before = loaded.composite();
        QVERIFY(!loaded.load(file, &error));
        QCOMPARE(loaded.composite(), before);
    }
    void transformDialogPreviewCancelAcceptAndInvalidGate() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({60, 40});
        pattern(*d);
        w.addDocument(d);
        QAction *action = w.findChild<QAction *>("transform");
        QVERIFY(action);
        QImage before = d->composite();
        bool handled = false, changed = false;
        QTimer timer;
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<TransformDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            dialog->findChild<QDoubleSpinBox *>("scaleX")->setValue(50);
            changed = d->composite() != before && d->state.layers[0].pixels.size() == QSize(60, 40);
            dialog->reject();
            handled = true;
        });
        timer.start();
        action->trigger();
        timer.stop();
        QVERIFY(handled);
        QVERIFY(changed);
        QCOMPARE(d->composite(), before);
        disconnect(&timer, nullptr, nullptr, nullptr);
        handled = false;
        connect(&timer, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<TransformDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *input = dialog->findChild<QDoubleSpinBox *>("scaleX");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            input->setValue(0);
            changed = !buttons->button(QDialogButtonBox::Ok)->isEnabled();
            dialog->accept();
            if (dialog->isVisible()) {
                input->setValue(50);
                QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
                handled = true;
            }
        });
        timer.start();
        action->trigger();
        timer.stop();
        QVERIFY(handled);
        QVERIFY(changed);
        QVERIFY(!d->state.layers[0].geometry.isIdentity());
        QCOMPARE(d->historyLabels().size(), 3);
        d->undo();
        QCOMPARE(d->composite(), before);
        w.close();
    }
    void canvasSizeDialogAndLayerMenuCommands() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        QVERIFY(d->create({20, 10}, true));
        w.addDocument(d);
        bool handled = false;
        QTimer timer;
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "canvasSizeDialog")
                return;
            dialog->findChild<QSpinBox *>("canvasWidth")->setValue(40);
            dialog->findChild<QSpinBox *>("canvasHeight")->setValue(30);
            dialog->findChild<QComboBox *>("canvasAnchor")->setCurrentIndex(4);
            QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok),
                              Qt::LeftButton);
            handled = true;
        });
        timer.start();
        w.findChild<QAction *>("canvasSize")->trigger();
        timer.stop();
        QVERIFY(handled);
        QCOMPARE(d->state.size, QSize(40, 30));
        QCOMPARE(d->state.layers[0].offset, QPoint(10, 10));
        w.findChild<QAction *>("undo")->trigger();
        QCOMPARE(d->state.size, QSize(20, 10));
        QString error;
        QVERIFY(d->addLayer("Other", &error));
        d->state.layers[1].pixels.fill(Qt::blue);
        w.findChild<QAction *>("mergeVisible")->trigger();
        QCOMPARE(d->state.layers.size(), 1);
        w.findChild<QAction *>("undo")->trigger();
        QCOMPARE(d->state.layers.size(), 2);
        d->undo();
        w.close();
    }
    void perspectiveTabStartsFromLastPreview() {
        Document d;
        d.create({60, 40});
        pattern(d);
        d.beginEdit();
        TransformDialog dialog(&d);
        dialog.show();
        dialog.findChild<QDoubleSpinBox *>("angle")->setValue(20);
        const QTransform before = d.state.layers[0].geometry;
        dialog.findChild<QTabWidget *>("transformTabs")->setCurrentIndex(1);
        const QTransform after = d.state.layers[0].geometry;
        for (QPointF point : {QPointF(0, 0), QPointF(60, 0), QPointF(60, 40), QPointF(0, 40)})
            QVERIFY(QLineF(before.map(point), after.map(point)).length() < .02);
        dialog.findChild<QDoubleSpinBox *>("cornerX1")->setValue(50);
        QVERIFY(d.state.layers[0].geometry != after);
        dialog.reject();
        d.cancelEdit();
        QVERIFY(d.state.layers[0].geometry.isIdentity());
    }
};
QTEST_MAIN(TransformTests)
#include "test_transforms.moc"
