#include "mainwindow.h"
#include "transformdialog.h"
#include <QAccessible>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QDir>
#include <QFileDialog>
#include <QFocusEvent>
#include <QTabBar>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QPointingDevice>
#include <QPlainTextEdit>
#include <QTabletEvent>
#include <QSlider>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyleFactory>
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
void transformFixture(Document &d) {
    QVERIFY(d.create({360, 260}));
    d.beginEdit();
    Layer &layer = d.state.layers[0];
    layer.pixels = QImage(120, 80, QImage::Format_ARGB32_Premultiplied);
    layer.pixels.fill(QColor("#e8b18a"));
    QPainter painter(&layer.pixels);
    painter.fillRect(60, 0, 60, 80, QColor("#59c9b4"));
    painter.end();
    layer.mask = QImage(layer.pixels.size(), QImage::Format_ARGB32_Premultiplied);
    layer.mask.fill(QColor(210, 210, 210));
    layer.offset = {110, 90};
    d.commit("Transform fixture");
    d.markSaved();
}
void prepare(Canvas &canvas) {
    canvas.resize(760, 580);
    canvas.show();
    canvas.fit();
    canvas.actualSize();
    QTest::qWait(10);
}
// Only the part of a handle's nine-pixel pointer target inside the canvas
// can be reached. Off-screen handles must not force controls over visible ones.
QVector<QRectF> visibleTransformTargets(const Canvas &canvas) {
    const QRectF viewport = canvas.showRulers ? QRectF(canvas.rect().adjusted(24, 24, 0, 0))
                                             : QRectF(canvas.rect());
    QVector<QRectF> targets;
    for (int i = TransformHandles::TopLeft; i <= TransformHandles::Move; ++i) {
        const QPointF point = canvas.transformHandlePosition(TransformHandles::Handle(i));
        const QRectF target = QRectF(point - QPointF(9, 9), QSizeF(18, 18)).intersected(viewport);
        if (!target.isEmpty())
            targets.append(target);
    }
    return targets;
}
QSize compactTransformControlsSize(const QWidget &controls) {
    const auto margins = controls.layout()->contentsMargins();
    const auto frame = controls.contentsMargins();
    const int rowHeight = controls.layout()->itemAt(0)->minimumSize().height();
    const int height = rowHeight + margins.top() + margins.bottom() + frame.top() + frame.bottom();
    return {controls.width(), qMax(48, height)};
}
bool controlsCanAvoidTargets(const Canvas &canvas, const QVector<QRectF> &targets,
                            QSize controlsSize) {
    // Independent rectangle-edge search: if an empty placement exists, an edge
    // of that placement can touch the viewport or one of the target rectangles.
    const int top = canvas.showRulers ? 36 : 12;
    const int right = canvas.width() - controlsSize.width();
    const int bottom = canvas.height() - controlsSize.height() - 12;
    if (right < 0 || bottom < top)
        return false;
    QVector<int> columns{0, right}, rows{top, bottom};
    for (const auto &target : targets) {
        columns.append(qFloor(target.left() - controlsSize.width()));
        columns.append(qCeil(target.right()));
        rows.append(qFloor(target.top() - controlsSize.height()));
        rows.append(qCeil(target.bottom()));
    }
    for (int x : columns)
        for (int y : rows) {
            if (x < 0 || x > right || y < top || y > bottom)
                continue;
            const QRectF candidate(QPointF(x, y), controlsSize);
            bool clear = true;
            for (const auto &target : targets)
                clear &= !candidate.intersects(target);
            if (clear)
                return true;
        }
    return false;
}
void drag(Canvas &canvas, QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = {}) {
    QTest::mousePress(&canvas, Qt::LeftButton, modifiers, from);
    QTest::mouseMove(&canvas, to);
    QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, to);
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
        QAction *action = w.findChild<QAction *>("transformAdvanced");
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
    void handleGeometryHonorsConstraintsAndViewCoordinates() {
        TransformHandles box{{120, 80}, {}, {50, 30}};
        for (qreal zoom : {.25, .5, 1.0, 3.0, 8.0}) {
            const QPointF pan(71, 93);
            for (int i = TransformHandles::TopLeft; i <= TransformHandles::Rotate; ++i) {
                auto handle = TransformHandles::Handle(i);
                QCOMPARE(box.hitTest(box.widgetPoint(handle, zoom, pan), zoom, pan), handle);
            }
            QCOMPARE(QLineF(box.widgetPoint(TransformHandles::Top, zoom, pan),
                            box.widgetPoint(TransformHandles::Rotate, zoom, pan)).length(), 28.0);
        }
        const auto corner = TransformHandles::BottomRight;
        const QPointF press = box.point(corner);
        QTransform uniform = box.dragged(corner, press, press + QPointF(60, 40), {});
        QCOMPARE(uniform.map(QPointF(120, 80)), QPointF(180, 120));
        QCOMPARE(uniform.map(QPointF()), QPointF());
        QTransform free = box.dragged(corner, press, press + QPointF(60, 20), Qt::ShiftModifier);
        QCOMPARE(free.map(QPointF(120, 80)), QPointF(180, 100));
        QTransform centered = box.dragged(corner, press, press + QPointF(30, 20), Qt::AltModifier);
        QCOMPARE(centered.map(QPointF(60, 40)), QPointF(60, 40));
        QCOMPARE(centered.map(QPointF()), QPointF(-30, -20));
        QTransform edge = box.dragged(TransformHandles::Right, box.point(TransformHandles::Right),
                                     box.point(TransformHandles::Right) + QPointF(60, 20), {});
        QCOMPARE(edge.map(QPointF(120, 80)), QPointF(180, 80));
        QTransform constrained = box.dragged(TransformHandles::Move, {0, 0}, {10, 3}, Qt::ShiftModifier);
        QCOMPARE(constrained.map(QPointF()), QPointF(10, 0));
        QTransform crossed = box.dragged(corner, press, {0, 0}, {});
        QVERIFY(crossed.determinant() > 0);
        QTransform rotated;
        rotated.translate(20, 30);
        rotated.rotate(30);
        box.geometry = rotated;
        QTransform scaled = box.dragged(corner, box.point(corner),
                                       box.offset + rotated.map(QPointF(180, 120)), {});
        QVERIFY(QLineF(scaled.map(QPointF()), rotated.map(QPointF())).length() < 1e-8);
        QVERIFY(QLineF(scaled.map(QPointF(120, 80)), rotated.map(QPointF(180, 120))).length() < 1e-8);
        QCOMPARE(box.dragged(corner, press,
                             {std::numeric_limits<double>::quiet_NaN(), 0}, {}), box.geometry);
    }
    void onCanvasMultipleDragsApplyAsOneSourcePreservingUndo() {
        Document d;
        transformFixture(d);
        const Layer source = d.state.layers[0];
        const QImage before = d.composite();
        const int history = d.historyLabels().size();
        Canvas c(&d);
        prepare(c);
        QVERIFY(c.beginTransform());
        const QPoint center = c.transformHandlePosition(TransformHandles::Move).toPoint();
        drag(c, center, center + QPoint(20, 15));
        QVERIFY(c.isTransforming());
        QCOMPARE(d.state.layers[0].geometry, source.geometry);
        QCOMPARE(d.historyLabels().size(), history);
        QVERIFY(!d.isEditing());
        QVERIFY(!d.dirty);
        QVERIFY(c.transformGeometry() != source.geometry);
        const QPoint handle = c.transformHandlePosition(TransformHandles::BottomRight).toPoint();
        drag(c, handle, handle + QPoint(30, 20));
        const QTransform result = c.transformGeometry();
        QVERIFY(c.isTransforming());
        QTest::keyClick(&c, Qt::Key_Return);
        QVERIFY(!c.isTransforming());
        QCOMPARE(d.historyLabels().size(), history + 1);
        QCOMPARE(d.state.layers[0].geometry, result);
        QCOMPARE(d.state.layers[0].pixels, source.pixels);
        QCOMPARE(d.state.layers[0].mask, source.mask);
        QCOMPARE(d.state.layers[0].offset, source.offset);
        QCOMPARE(d.state.layers[0].text, source.text);
        QVERIFY(d.composite() != before);
        const QImage after = d.composite();
        d.undo();
        QCOMPARE(d.composite(), before);
        d.redo();
        QCOMPARE(d.composite(), after);
    }
    void onCanvasNoOpCancelAndLayerGuardsDoNotTouchHistory() {
        Document d;
        transformFixture(d);
        const Layer source = d.state.layers[0];
        const int history = d.historyLabels().size();
        d.selection = {5, 6, 7, 8};
        const Selection selection = d.selection;
        Canvas c(&d);
        prepare(c);
        QVERIFY(c.beginTransform());
        const QPoint handle = c.transformHandlePosition(TransformHandles::BottomRight).toPoint();
        QTest::mouseClick(&c, Qt::LeftButton, {}, handle);
        QTest::keyClick(&c, Qt::Key_Return);
        QCOMPARE(d.historyLabels().size(), history);
        QVERIFY(!d.dirty);
        QVERIFY(c.beginTransform());
        QTest::keyClick(&c, Qt::Key_Right);
        QTest::keyClick(&c, Qt::Key_Down, Qt::ShiftModifier);
        QTest::keyClick(&c, Qt::Key_Escape);
        QVERIFY(!c.isTransforming());
        QCOMPARE(d.state.layers[0].geometry, source.geometry);
        QCOMPARE(d.selection.bounds(), selection.bounds());
        QCOMPARE(d.historyLabels().size(), history);
        QVERIFY(!d.dirty);
        d.state.layers[0].locked = true;
        QVERIFY(!c.beginTransform());
        d.state.layers[0].locked = false;
        d.state.layers[0].visible = false;
        QVERIFY(!c.beginTransform());
        d.state.layers[0].visible = true;
        QVERIFY(c.beginTransform());
        QTest::keyClick(&c, Qt::Key_Right);
        d.state.layers[0].locked = true;
        d.changed();
        QVERIFY(!c.isTransforming());
        QCOMPARE(d.state.layers[0].geometry, source.geometry);
        QCOMPARE(d.historyLabels().size(), history);
    }
    void onCanvasApplyRevalidatesTargetAfterSessionEnd_data() {
        QTest::addColumn<QString>("change");
        for (const auto *change : {"document", "active-layer", "source", "revision", "new-preview",
                                   "new-preview-canceled", "deleted-document"})
            QTest::newRow(change) << QString(change);
    }
    void onCanvasApplyRevalidatesTargetAfterSessionEnd() {
        QFETCH(QString, change);
        QPointer<Document> target = new Document;
        transformFixture(*target);
        QString error;
        QVERIFY(target->addLayer("Second", &error));
        target->state.active = 0;
        target->markSaved();
        Document other;
        transformFixture(other);
        const int history = target->historyLabels().size();
        const int otherHistory = other.historyLabels().size();
        Canvas canvas(target);
        QVERIFY(canvas.beginTransform());
        QTest::keyClick(&canvas, Qt::Key_Right);
        QTransform replacement;
        bool handled = false;
        connect(&canvas, &Canvas::transformSessionChanged, &canvas, [&](bool active) {
            if (active || handled)
                return;
            handled = true;
            if (change == "document")
                canvas.document = &other;
            else if (change == "active-layer")
                target->state.active = 1;
            else if (change == "source")
                target->state.layers[0].offset += QPoint(1, 0);
            else if (change == "revision") {
                target->beginEdit();
                target->state.layers[0].name = "Changed synchronously";
                target->commit("External edit");
            } else if (change == "deleted-document")
                delete target.data();
            else {
                QVERIFY(canvas.beginTransform());
                QTest::keyClick(&canvas, Qt::Key_Down);
                replacement = canvas.transformGeometry();
                if (change == "new-preview-canceled")
                    canvas.cancelTransform();
            }
        });
        QVERIFY(!canvas.finishPendingGesture());
        QVERIFY(handled);
        QCOMPARE(canvas.isTransforming(), change == "new-preview");
        if (canvas.isTransforming())
            QCOMPARE(canvas.transformGeometry(), replacement);
        if (target) {
            QCOMPARE(target->state.layers[0].geometry, QTransform());
            QCOMPARE(target->state.layers[1].geometry, QTransform());
            QCOMPARE(target->historyLabels().size(), history + (change == "revision" ? 1 : 0));
            QVERIFY(!target->isEditing());
        }
        QCOMPARE(other.state.layers[0].geometry, QTransform());
        QCOMPARE(other.historyLabels().size(), otherHistory);
        canvas.document = target;
        delete target.data();
    }
    void onCanvasSessionEndDeletionDoesNotResumeCaller_data() {
        QTest::addColumn<QString>("operation");
        for (const auto *operation : {"apply", "finish", "cancel", "set-tool", "invalidate"})
            QTest::newRow(operation) << QString(operation);
    }
    void onCanvasSessionEndDeletionDoesNotResumeCaller() {
        QFETCH(QString, operation);
        Document target;
        transformFixture(target);
        const int history = target.historyLabels().size();
        QPointer<Canvas> canvas = new Canvas(&target);
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        connect(canvas, &Canvas::transformSessionChanged, &target, [&](bool active) {
            if (!active)
                delete canvas.data();
        });
        if (operation == "apply")
            QVERIFY(!canvas->applyTransform());
        else if (operation == "finish")
            QVERIFY(!canvas->finishPendingGesture());
        else if (operation == "cancel")
            canvas->cancelTransform();
        else if (operation == "set-tool")
            canvas->setTool(Canvas::Brush);
        else {
            target.beginEdit();
            target.state.layers[0].offset += QPoint(1, 0);
            target.changed();
            target.cancelEdit();
        }
        QVERIFY(!canvas);
        QCOMPARE(target.state.layers[0].geometry, QTransform());
        QCOMPARE(target.historyLabels().size(), history);
        QVERIFY(!target.isEditing());
    }
    void onCanvasBeginSignalsRespectLifetimeAndSessionOwnership_data() {
        QTest::addColumn<QString>("boundary");
        QTest::addColumn<QString>("action");
        for (const auto *boundary : {"hint", "session"})
            for (const auto *action : {"delete", "cancel", "replace", "retarget"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(boundary, action)))
                    << QString(boundary) << QString(action);
    }
    void onCanvasBeginSignalsRespectLifetimeAndSessionOwnership() {
        QFETCH(QString, boundary);
        QFETCH(QString, action);
        Document target;
        transformFixture(target);
        const int history = target.historyLabels().size();
        Document other;
        transformFixture(other);
        QPointer<Canvas> canvas = new Canvas(&target);
        QSignalSpy sessions(canvas, &Canvas::transformSessionChanged);
        bool handled = false;
        QTransform replacement;
        const auto interrupt = [&] {
            if (handled)
                return;
            handled = true;
            if (action == "delete")
                delete canvas.data();
            else if (action == "retarget")
                canvas->document = &other;
            else {
                canvas->cancelTransform();
                if (action == "replace") {
                    QVERIFY(canvas->beginTransform());
                    QTest::keyClick(canvas, Qt::Key_Down);
                    replacement = canvas->transformGeometry();
                }
            }
        };
        if (boundary == "hint")
            connect(canvas, &Canvas::toolHint, &target, [&](QString) { interrupt(); });
        else
            connect(canvas, &Canvas::transformSessionChanged, &target, [&](bool active) {
                if (active)
                    interrupt();
            });
        QVERIFY(!canvas->beginTransform());
        QVERIFY(handled);
        if (canvas) {
            QCOMPARE(canvas->isTransforming(), action == "replace");
            if (action == "replace")
                QCOMPARE(canvas->transformGeometry(), replacement);
        }
        if (boundary == "hint" && action != "replace")
            for (const auto &arguments : sessions)
                QVERIFY(!arguments.first().toBool());
        QCOMPARE(target.historyLabels().size(), history);
        QCOMPARE(target.state.layers[0].geometry, QTransform());
        delete canvas.data();
    }
    void onCanvasCancelKeepsReplacementSession_data() {
        QTest::addColumn<bool>("invalidate");
        QTest::newRow("cancel") << false;
        QTest::newRow("external-change") << true;
    }
    void onCanvasCancelKeepsReplacementSession() {
        QFETCH(bool, invalidate);
        Document target;
        transformFixture(target);
        const int history = target.historyLabels().size();
        Canvas canvas(&target);
        QVERIFY(canvas.beginTransform());
        QTest::keyClick(&canvas, Qt::Key_Right);
        QSignalSpy hints(&canvas, &Canvas::toolHint);
        bool handled = false;
        QTransform replacement;
        connect(&canvas, &Canvas::transformSessionChanged, &canvas, [&](bool active) {
            if (active || handled)
                return;
            handled = true;
            QVERIFY(canvas.beginTransform());
            QTest::keyClick(&canvas, Qt::Key_Down);
            replacement = canvas.transformGeometry();
        });
        if (invalidate) {
            target.state.layers[0].offset += QPoint(1, 0);
            target.changed();
        } else
            canvas.cancelTransform();
        QVERIFY(handled);
        QVERIFY(canvas.isTransforming());
        QCOMPARE(canvas.transformGeometry(), replacement);
        QCOMPARE(hints.size(), 1);
        const QString hint = hints.first().first().toString();
        QVERIFY(hint.startsWith(target.state.layers[target.state.active].name));
        QVERIFY(hint.contains("katmanının tamamı"));
        QCOMPARE(target.state.layers[0].geometry, QTransform());
        QCOMPARE(target.historyLabels().size(), history);
    }
    void onCanvasCommitSignalRespectsLifetimeAndNewPreview_data() {
        QTest::addColumn<QString>("action");
        for (const auto *action : {"delete-canvas", "delete-document", "delete-both", "new-preview"})
            QTest::newRow(action) << QString(action);
    }
    void onCanvasCommitSignalRespectsLifetimeAndNewPreview() {
        QFETCH(QString, action);
        QPointer<Document> target = new Document;
        transformFixture(*target);
        const int history = target->historyLabels().size();
        QPointer<Canvas> canvas = new Canvas(target);
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        const QTransform applied = canvas->transformGeometry();
        QSignalSpy hints(canvas, &Canvas::toolHint);
        bool handled = false;
        QTransform replacement;
        connect(target, &Document::updated, this, [&] {
            if (handled)
                return;
            handled = true;
            // Apply publishes only its completed transaction, so callbacks can
            // immediately start a new preview without inheriting an open edit.
            QVERIFY(!target->isEditing());
            QCOMPARE(target->historyLabels().size(), history + 1);
            if (action == "new-preview") {
                QVERIFY(canvas->beginTransform());
                QTest::keyClick(canvas, Qt::Key_Down);
                replacement = canvas->transformGeometry();
            } else {
                if (action != "delete-document")
                    delete canvas.data();
                if (action != "delete-canvas")
                    delete target.data();
            }
        });
        // The completed transaction survives, but reentry invalidates this
        // operation's continuation token so callers must not act on its old target.
        QVERIFY(!canvas->applyTransform());
        QVERIFY(handled);
        if (action == "new-preview") {
            QVERIFY(canvas->isTransforming());
            QCOMPARE(canvas->transformGeometry(), replacement);
        }
        if (target) {
            QCOMPARE(target->state.layers[0].geometry, applied);
            QCOMPARE(target->historyLabels().size(), history + 1);
            QVERIFY(!target->isEditing());
            target->undo();
            QCOMPARE(target->state.layers[0].geometry, QTransform());
        }
        if (action == "new-preview") {
            // Undo invalidates the replacement above; before it did, Apply
            // must not have emitted an old success hint over that new session.
            QVERIFY(replacement != applied);
            for (const auto &arguments : hints)
                QVERIFY(!arguments.first().toString().startsWith("Dönüşüm uygulandı"));
        }
        delete canvas.data();
        delete target.data();
    }
    void actionSettlementDoesNotResumeOnReplacementTarget_data() {
        QTest::addColumn<QString>("change");
        for (const auto *change : {"delete-window", "change-tab", "new-preview", "change-layer"})
            QTest::newRow(change) << QString(change);
    }
    void actionSettlementDoesNotResumeOnReplacementTarget() {
        QFETCH(QString, change);
        QPointer<MainWindow> window = new MainWindow(false, false);
        QPointer<Document> target = new Document;
        transformFixture(*target);
        QString error;
        QVERIFY(target->addLayer("Second", &error));
        target->state.active = 0;
        window->addDocument(target);
        QPointer<Canvas> canvas = window->currentCanvas();
        auto *other = new Document;
        transformFixture(*other);
        window->addDocument(other);
        Canvas *otherCanvas = window->currentCanvas();
        auto *tabs = window->findChild<QTabWidget *>("documents");
        tabs->setCurrentWidget(canvas);
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        const int history = target->historyLabels().size();
        const int otherHistory = other->historyLabels().size();
        bool handled = false;
        QTransform replacement;
        connect(canvas, &Canvas::transformSessionChanged, this, [&](bool active) {
            if (active || handled)
                return;
            handled = true;
            if (change == "delete-window")
                delete window.data();
            else if (change == "change-tab")
                tabs->setCurrentWidget(otherCanvas);
            else if (change == "change-layer")
                target->state.active = 1;
            else {
                QVERIFY(canvas->beginTransform());
                QTest::keyClick(canvas, Qt::Key_Down);
                replacement = canvas->transformGeometry();
            }
        });
        // This command must never duplicate a different tab/layer or run
        // underneath a new preview opened while the old one is being settled.
        window->findChild<QAction *>("duplicateWhole")->trigger();
        QVERIFY(handled);
        if (!window) {
            QVERIFY(!canvas);
            QVERIFY(!target);
            return;
        }
        QCOMPARE(target->state.layers.size(), 2);
        QCOMPARE(other->state.layers.size(), 1);
        QCOMPARE(other->historyLabels().size(), otherHistory);
        QCOMPARE(target->historyLabels().size(), history + (change == "change-tab" ? 1 : 0));
        QVERIFY(!target->isEditing());
        if (change == "new-preview") {
            QVERIFY(canvas->isTransforming());
            QCOMPARE(canvas->transformGeometry(), replacement);
        }
        delete window.data();
    }
    void onCanvasDragHitTestingRemainsCorrectAfterZoomAndPan_data() {
        QTest::addColumn<qreal>("zoom");
        for (qreal zoom : {.5, 1.0, 2.5})
            QTest::newRow(qPrintable(QString::number(zoom))) << zoom;
    }
    void onCanvasDragHitTestingRemainsCorrectAfterZoomAndPan() {
        QFETCH(qreal, zoom);
        Document d;
        transformFixture(d);
        Canvas c(&d);
        prepare(c);
        c.setZoom(zoom);
        QTest::mousePress(&c, Qt::MiddleButton, {}, {60, 60});
        QTest::mouseMove(&c, {101, 83});
        QTest::mouseRelease(&c, Qt::MiddleButton, {}, {101, 83});
        QVERIFY(c.beginTransform());
        const QPoint center = c.transformHandlePosition(TransformHandles::Move).toPoint();
        drag(c, center, center + QPoint(40, 20));
        const QPointF actual = c.transformGeometry().map(QPointF());
        QVERIFY(QLineF(actual, QPointF(40 / zoom, 20 / zoom)).length() < 1e-7);
        c.cancelTransform();
        QVERIFY(c.beginTransform());
        const QPoint corner = c.transformHandlePosition(TransformHandles::BottomRight).toPoint();
        drag(c, corner, corner + QPoint(qRound(60 * zoom), qRound(40 * zoom)));
        const QTransform geometry = c.transformGeometry();
        QVERIFY(qAbs(geometry.m11() - 1.5) < .02);
        QVERIFY(qAbs(geometry.m22() - 1.5) < .02);
        QVERIFY(c.applyTransform());
        QCOMPARE(d.state.layers[0].pixels.size(), QSize(120, 80));
    }
    void onCanvasRotateSnapAndInvalidDragRetainLastValidPreview() {
        Document d;
        transformFixture(d);
        Canvas c(&d);
        prepare(c);
        QVERIFY(c.beginTransform());
        const QPointF center = c.transformHandlePosition(TransformHandles::Move);
        const QPoint rotation = c.transformHandlePosition(TransformHandles::Rotate).toPoint();
        const qreal radius = QLineF(center, rotation).length();
        const QPoint target = (center + QPointF(radius, 0)).toPoint();
        drag(c, rotation, target, Qt::ShiftModifier);
        const QTransform rotated = c.transformGeometry();
        QVERIFY(qAbs(rotated.m11()) < 1e-7);
        QVERIFY(qAbs(rotated.m12() - 1) < 1e-7);
        const QPoint corner = c.transformHandlePosition(TransformHandles::BottomRight).toPoint();
        drag(c, corner, {1000000000, 1000000000});
        QCOMPARE(c.transformGeometry(), rotated);
        QVERIFY(c.findChild<QLabel *>("transformInfo")->text().contains("son geçerli"));
        QVERIFY(c.applyTransform());
        QVERIFY(d.state.layers[0].validGeometry());
    }
    void onCanvasRefreshAndFocusPreservePreviewButExternalEditInvalidatesIt() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        transformFixture(*d);
        w.addDocument(d);
        QTest::qWait(30);
        Canvas *c = w.currentCanvas();
        w.findChild<QAction *>("transform")->trigger();
        QVERIFY(c->isTransforming());
        QTest::keyClick(c, Qt::Key_Right);
        const QTransform preview = c->transformGeometry();
        auto *opacity = w.findChild<QSlider *>("layerOpacity");
        opacity->setFocus();
        QTest::qWait(40);
        d->changed(); // A redraw notification is not a real edit.
        QTest::qWait(40);
        QVERIFY(c->isTransforming());
        QCOMPARE(c->transformGeometry(), preview);
        d->beginEdit();
        d->state.layers[0].offset += QPoint(1, 0);
        d->changed();
        QVERIFY(!c->isTransforming());
        d->commit("External edit");
        QCOMPARE(d->state.layers[0].geometry, QTransform());
        d->undo();
        w.close();
    }
    void onCanvasActionUndoCancelsPreviewAndPropertyEditsStaySeparate() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        transformFixture(*d);
        w.addDocument(d);
        QTest::qWait(30);
        Canvas *c = w.currentCanvas();
        const int history = d->historyLabels().size();
        w.findChild<QAction *>("transform")->trigger();
        QTest::keyClick(c, Qt::Key_Right);
        QVERIFY(w.findChild<QAction *>("undo")->isEnabled());
        QVERIFY(w.findChild<QAction *>("undo")->text().contains("önizlemesini iptal"));
        w.findChild<QAction *>("undo")->trigger();
        QVERIFY(!c->isTransforming());
        QCOMPARE(d->historyLabels().size(), history);
        QCOMPARE(d->state.layers[0].geometry, QTransform());
        w.findChild<QAction *>("transform")->trigger();
        QTest::keyClick(c, Qt::Key_Right);
        w.findChild<QSlider *>("layerOpacity")->setValue(70);
        QVERIFY(!c->isTransforming());
        QCOMPARE(d->historyLabels().size(), history + 2);
        QCOMPARE(d->state.layers[0].geometry, QTransform::fromTranslate(1, 0));
        QCOMPARE(d->state.layers[0].opacity, .7);
        d->undo();
        QCOMPARE(d->state.layers[0].opacity, 1.0);
        QCOMPARE(d->state.layers[0].geometry, QTransform::fromTranslate(1, 0));
        d->undo();
        QCOMPARE(d->state.layers[0].geometry, QTransform());
        w.close();
    }
    void onCanvasColorResetPreservesPendingTransformAndSeparateUndo_data() {
        QTest::addColumn<bool>("adjusted");
        QTest::newRow("changed-color") << true;
        QTest::newRow("already-neutral") << false;
    }
    void onCanvasColorResetPreservesPendingTransformAndSeparateUndo() {
        QFETCH(bool, adjusted);
        MainWindow w(false, false);
        auto *d = new Document;
        transformFixture(*d);
        d->beginEdit();
        d->adjust(adjusted ? 20 : 0, adjusted ? -10 : 0, adjusted ? 15 : 0);
        ToneSettings tones;
        tones.exposure = adjusted ? .5 : 0;
        QVERIFY(d->adjustTones(tones));
        d->commit("Color fixture");
        d->markSaved();
        const Layer source = d->state.layers[0];
        const quint64 clean = d->revision();
        const int history = d->historyLabels().size();
        w.addDocument(d);
        w.applyUiPreset("graphite", false);
        w.show();
        QTest::qWait(40);
        auto *canvas = w.currentCanvas();
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        const QTransform preview = canvas->transformGeometry();
        QVERIFY(preview != source.geometry);
        auto *reset = w.findChild<QPushButton *>("resetColorAdjustments");
        QVERIFY(reset && reset->isVisible());
        QTest::mouseClick(reset, Qt::LeftButton);
        QVERIFY(!canvas->isTransforming());
        QCOMPARE(d->state.layers[0].geometry, preview);
        QCOMPARE(d->state.layers[0].brightness, 0);
        QCOMPARE(d->state.layers[0].contrast, 0);
        QCOMPARE(d->state.layers[0].saturation, 0);
        QVERIFY(d->state.layers[0].tones.isIdentity());
        QCOMPARE(d->state.layers[0].pixels, source.pixels);
        QCOMPARE(d->state.layers[0].mask, source.mask);
        QCOMPARE(d->historyLabels().size(), history + 1 + int(adjusted));
        if (adjusted) {
            d->undo();
            QCOMPARE(d->state.layers[0].geometry, preview);
            QCOMPARE(d->state.layers[0].brightness, 20);
            QCOMPARE(d->state.layers[0].contrast, -10);
            QCOMPARE(d->state.layers[0].saturation, 15);
            QVERIFY(d->state.layers[0].tones == tones);
        }
        d->undo();
        QCOMPARE(d->revision(), clean);
        QCOMPARE(d->state.layers[0].geometry, source.geometry);
        QVERIFY(!d->dirty);
        d->redo();
        if (adjusted)
            d->redo();
        QCOMPARE(d->state.layers[0].geometry, preview);
        QVERIFY(d->state.layers[0].tones.isIdentity());
        d->markSaved();
        w.close();
    }
    void onCanvasTextLayerDoubleClickFinishesPreviewBeforeDialog_data() {
        QTest::addColumn<bool>("accept");
        QTest::newRow("accept-text") << true;
        QTest::newRow("cancel-text") << false;
    }
    void onCanvasTextLayerDoubleClickFinishesPreviewBeforeDialog() {
        QFETCH(bool, accept);
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({400, 300}));
        QFont font("Sans");
        font.setPixelSize(24);
        QString error;
        QVERIFY(d->addText("Original", font, Qt::black, {50, 50}, &error));
        QVERIFY(d->addMask(&error));
        d->markSaved();
        const int active = d->state.active;
        const Layer original = d->state.layers[active];
        const int history = d->historyLabels().size();
        const quint64 clean = d->revision();
        w.addDocument(d);
        w.show();
        QTest::qWait(40);
        auto *canvas = w.currentCanvas();
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        const QTransform preview = canvas->transformGeometry();
        auto *list = w.findChild<QListWidget *>("layers");
        QVERIFY(list && list->currentItem());
        const QPoint point = list->visualItemRect(list->currentItem()).center();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, point);
        QVERIFY(canvas->isTransforming()); // The active row did not trigger row-change finalization.
        bool handled = false, appliedBeforeDialog = false;
        QTimer driver, timeout;
        driver.setInterval(10);
        timeout.setSingleShot(true);
        connect(&driver, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            auto *text = dialog ? dialog->findChild<QPlainTextEdit *>() : nullptr;
            if (!text || handled)
                return;
            handled = true;
            appliedBeforeDialog = !canvas->isTransforming() &&
                                  d->state.layers[active].geometry == preview;
            text->setPlainText("Edited");
            if (accept)
                dialog->accept();
            else
                dialog->reject();
        });
        connect(&timeout, &QTimer::timeout, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        driver.start();
        timeout.start(5000);
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, {}, point);
        driver.stop();
        timeout.stop();
        QVERIFY(handled);
        QVERIFY(appliedBeforeDialog);
        QCOMPARE(d->state.layers[active].geometry, preview);
        QCOMPARE(d->state.layers[active].text, accept ? QString("Edited") : original.text);
        QCOMPARE(d->historyLabels().size(), history + 1 + int(accept));
        if (accept) {
            d->undo();
            QCOMPARE(d->state.layers[active].geometry, preview);
            QCOMPARE(d->state.layers[active].text, original.text);
            QCOMPARE(d->state.layers[active].pixels, original.pixels);
            QCOMPARE(d->state.layers[active].mask, original.mask);
        }
        d->undo();
        QCOMPARE(d->revision(), clean);
        QCOMPARE(d->state.layers[active].geometry, original.geometry);
        QVERIFY(!d->dirty);
        d->redo();
        if (accept)
            d->redo();
        QCOMPARE(d->state.layers[active].geometry, preview);
        d->markSaved();
        w.close();
    }
    void onCanvasTabsAndLayerSwitchCannotRetargetPendingTransform() {
        MainWindow w(false, false);
        w.show();
        auto *first = new Document;
        transformFixture(*first);
        QVERIFY(first->addLayer("Other"));
        first->state.active = 0;
        first->markSaved();
        w.addDocument(first);
        QTest::qWait(30);
        Canvas *c = w.currentCanvas();
        QVERIFY(c->beginTransform());
        QTest::keyClick(c, Qt::Key_Right);
        auto *second = new Document;
        transformFixture(*second);
        w.addDocument(second);
        QVERIFY(c->isTransforming());
        const QTransform firstPreview = c->transformGeometry();
        QCOMPARE(first->state.layers[0].geometry, QTransform());
        Canvas *secondCanvas = w.currentCanvas();
        QVERIFY(secondCanvas->beginTransform());
        QTest::keyClick(secondCanvas, Qt::Key_Down);
        w.findChild<QAction *>("undo")->trigger();
        QVERIFY(!secondCanvas->isTransforming());
        QVERIFY(c->isTransforming());
        QCOMPARE(c->transformGeometry(), firstPreview);
        QCOMPARE(second->state.layers[0].geometry, QTransform());
        auto *documentTabs = w.findChild<QTabWidget *>("documents");
        if (!documentTabs)
            documentTabs = w.findChild<QTabWidget *>();
        QVERIFY(documentTabs);
        documentTabs->setCurrentWidget(c);
        QTest::qWait(40);
        QVERIFY(c->isTransforming());
        QCOMPARE(w.currentDocument(), first);
        w.findChild<QListWidget *>("layers")->setCurrentRow(0);
        QVERIFY(!c->isTransforming());
        QCOMPARE(first->state.active, 1);
        QCOMPARE(first->state.layers[0].geometry, QTransform::fromTranslate(1, 0));
        QCOMPARE(first->state.layers[1].geometry, QTransform());
        first->undo();
        first->markSaved();
        w.close();
    }
    void onCanvasNumericDialogAcceptCancelShareThePendingUndo() {
        Document d;
        transformFixture(d);
        Canvas c(&d);
        prepare(c);
        const int history = d.historyLabels().size();
        QVERIFY(c.beginTransform());
        QTest::keyClick(&c, Qt::Key_Right);
        const QTransform moved = c.transformGeometry();
        bool handled = false;
        QTimer timer;
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<TransformDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            dialog->findChild<QDoubleSpinBox *>("scaleX")->setValue(50);
            handled = c.transformGeometry() != moved;
            dialog->reject();
        });
        timer.start();
        QTest::mouseClick(c.findChild<QToolButton *>("transformNumeric"), Qt::LeftButton);
        timer.stop();
        QVERIFY(handled);
        QVERIFY(c.isTransforming());
        QCOMPARE(c.transformGeometry(), moved);
        QCOMPARE(d.historyLabels().size(), history);
        disconnect(&timer, nullptr, nullptr, nullptr);
        handled = false;
        connect(&timer, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<TransformDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            dialog->findChild<QDoubleSpinBox *>("scaleX")->setValue(50);
            dialog->accept();
            handled = true;
        });
        timer.start();
        QTest::mouseClick(c.findChild<QToolButton *>("transformNumeric"), Qt::LeftButton);
        timer.stop();
        QVERIFY(handled);
        QVERIFY(c.transformGeometry() != moved);
        QCOMPARE(d.historyLabels().size(), history);
        QVERIFY(c.applyTransform());
        QCOMPARE(d.historyLabels().size(), history + 1);
        d.undo();
        QCOMPARE(d.state.layers[0].geometry, QTransform());
    }
    void onCanvasSaveAndCanceledCloseKeepAppliedTransform() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        transformFixture(*d);
        QTemporaryDir dir;
        QString error;
        const QString file = dir.filePath("transform.pdeck");
        QVERIFY(d->save(file, &error));
        w.addDocument(d);
        QTest::qWait(30);
        Canvas *c = w.currentCanvas();
        QVERIFY(c->beginTransform());
        QTest::keyClick(c, Qt::Key_Right);
        w.findChild<QAction *>("save")->trigger();
        QVERIFY(!c->isTransforming());
        QVERIFY(!d->dirty);
        Document loaded;
        QVERIFY(loaded.load(file, &error));
        QCOMPARE(loaded.state.layers[0].geometry, QTransform::fromTranslate(1, 0));
        QVERIFY(c->beginTransform());
        QTest::keyClick(c, Qt::Key_Right);
        bool canceled = false;
        QTimer timer;
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, [&] {
            auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!question)
                return;
            question->button(QMessageBox::Cancel)->click();
            canceled = true;
        });
        timer.start();
        w.close();
        timer.stop();
        QVERIFY(canceled);
        QVERIFY(w.isVisible());
        QVERIFY(!c->isTransforming());
        QCOMPARE(d->state.layers[0].geometry, QTransform::fromTranslate(2, 0));
        QVERIFY(d->dirty);
        d->undo();
        QCOMPARE(d->state.layers[0].geometry, QTransform::fromTranslate(1, 0));
        QVERIFY(!d->dirty);
        w.close();
    }
    void onCanvasExistingGeometryKeepsEditableTextAndMask() {
        Document d;
        QVERIFY(d.create({480, 320}));
        QString error;
        QVERIFY(d.addText("PhotoDeck", QFont("Sans", 24), QColor("#59c9b4"), {130, 130}, &error));
        QVERIFY(d.addMask(&error));
        d.beginEdit();
        QTransform original;
        original.rotate(-12);
        original.shear(.1, 0);
        QVERIFY(d.setLayerTransform(original, &error));
        d.commit("Initial geometry");
        d.markSaved();
        const Layer source = d.state.layers[d.state.active];
        const int history = d.historyLabels().size();
        Canvas c(&d);
        prepare(c);
        QVERIFY(c.beginTransform());
        const QPoint corner = c.transformHandlePosition(TransformHandles::BottomRight).toPoint();
        const QPointF expected = c.toWidget(source.offset +
                                            original.map(QPointF(source.pixels.width() * 1.25,
                                                                 source.pixels.height() * 1.25)));
        drag(c, corner, expected.toPoint());
        QVERIFY(c.applyTransform());
        const Layer result = d.state.layers[d.state.active];
        QCOMPARE(result.pixels, source.pixels);
        QCOMPARE(result.mask, source.mask);
        QCOMPARE(result.text, source.text);
        QCOMPARE(result.font, source.font);
        QCOMPARE(d.historyLabels().size(), history + 1);
        const QPointF actual = c.toWidget(result.offset + result.geometry.map(
                                                           QPointF(source.pixels.width(),
                                                                   source.pixels.height())));
        QVERIFY(QLineF(actual, expected).length() < 2);
        QVERIFY(d.editText("Still editable", source.font, source.color, &error));
        QCOMPARE(d.state.layers[d.state.active].geometry, result.geometry);
        QVERIFY(!d.state.layers[d.state.active].mask.isNull());
        d.undo();
        QCOMPARE(d.state.layers[d.state.active].text, source.text);
        d.undo();
        QCOMPARE(d.state.layers[d.state.active].geometry, original);
        const int noOpHistory = d.historyLabels().size();
        QVERIFY(c.beginTransform());
        QTest::keyClick(&c, Qt::Key_Right);
        QTest::keyClick(&c, Qt::Key_Left);
        QVERIFY(c.applyTransform());
        QCOMPARE(d.historyLabels().size(), noOpHistory);
        QCOMPARE(d.state.layers[d.state.active].geometry, original);
        QVERIFY(!d.dirty);
    }
    void onCanvasCloseTabAndSaveAsCancellationDoNotLoseWork() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        transformFixture(*d);
        w.addDocument(d);
        QTest::qWait(30);
        Canvas *c = w.currentCanvas();
        QVERIFY(c->beginTransform());
        QTest::keyClick(c, Qt::Key_Right);
        bool canceled = false;
        QTimer timer;
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, [&] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                dialog->reject();
                canceled = true;
            }
        });
        timer.start();
        w.findChild<QAction *>("saveAs")->trigger();
        timer.stop();
        QVERIFY(canceled);
        QVERIFY(!c->isTransforming());
        QCOMPARE(d->state.layers[0].geometry, QTransform::fromTranslate(1, 0));
        QVERIFY(d->dirty);
        QVERIFY(c->beginTransform());
        QTest::keyClick(c, Qt::Key_Right);
        disconnect(&timer, nullptr, nullptr, nullptr);
        canceled = false;
        connect(&timer, &QTimer::timeout, [&] {
            if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                dialog->button(QMessageBox::Cancel)->click();
                canceled = true;
            }
        });
        auto *tabs = w.findChild<QTabWidget *>("documents");
        QWidget *close = tabs->tabBar()->tabButton(0, QTabBar::RightSide);
        if (!close)
            close = tabs->tabBar()->tabButton(0, QTabBar::LeftSide);
        QVERIFY(close);
        timer.start();
        QTest::mouseClick(close, Qt::LeftButton);
        timer.stop();
        QVERIFY(canceled);
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(w.currentCanvas(), c);
        QVERIFY(!c->isTransforming());
        QCOMPARE(d->state.layers[0].geometry, QTransform::fromTranslate(2, 0));
        d->undo();
        d->undo();
        QVERIFY(!d->dirty);
        w.close();
    }
    void onCanvasEveryToolUsesTransformMouseAndTabletHandlers_data() {
        QTest::addColumn<int>("tool");
        QTest::addColumn<bool>("eraser");
        for (int tool = 0; tool < Canvas::ToolCount; ++tool)
            for (bool eraser : {false, true})
                QTest::newRow(qPrintable(QString("tool-%1-%2").arg(tool).arg(eraser ? "eraser" : "pen")))
                    << tool << eraser;
    }
    void onCanvasEveryToolUsesTransformMouseAndTabletHandlers() {
        QFETCH(int, tool);
        QFETCH(bool, eraser);
        Document d;
        transformFixture(d);
        const QImage pixels = d.state.layers[0].pixels, mask = d.state.layers[0].mask;
        const int history = d.historyLabels().size();
        Canvas c(&d);
        prepare(c);
        c.setTool(Canvas::Tool(tool));
        const QPointF origin = c.toWidget(QPointF());
        QVERIFY(c.beginTransform());
        const QPoint center = c.transformHandlePosition(TransformHandles::Move).toPoint();
        drag(c, center, center + QPoint(20, 10));
        QCOMPARE(c.toWidget(QPointF()), origin); // Hand must move the layer, never pan this gesture.
        QCOMPARE(c.transformGeometry(), QTransform::fromTranslate(20, 10));
        c.cancelTransform();
        QVERIFY(c.beginTransform());
        QPointingDevice device("transform test pen", eraser ? 53 : 52,
                               QInputDevice::DeviceType::Stylus,
                               eraser ? QPointingDevice::PointerType::Eraser
                                      : QPointingDevice::PointerType::Pen,
                               QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                               1, 3);
        const QPointF start = c.transformHandlePosition(TransformHandles::Move);
        auto tablet = [&](QEvent::Type type, QPointF at, qreal pressure) {
            QTabletEvent event(type, &device, at, c.mapToGlobal(at.toPoint()), pressure, 0, 0,
                               0, 0, 0, {}, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                               type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(&c, &event);
            QVERIFY(event.isAccepted());
        };
        tablet(QEvent::TabletPress, start, .5);
        tablet(QEvent::TabletMove, start + QPointF(15, 8), .7);
        tablet(QEvent::TabletRelease, start + QPointF(15, 8), 0);
        QVERIFY(c.isTransforming());
        QCOMPARE(c.transformGeometry(), QTransform::fromTranslate(15, 8));
        QCOMPARE(d.state.layers[0].pixels, pixels);
        QCOMPARE(d.state.layers[0].mask, mask);
        QCOMPARE(d.historyLabels().size(), history);
        QVERIFY(!d.isEditing());
        QVERIFY(c.applyTransform());
        QCOMPARE(d.historyLabels().size(), history + 1);
        QCOMPARE(d.state.layers[0].pixels, pixels);
        QCOMPARE(d.state.layers[0].mask, mask);
    }
    void onCanvasViewActionsRepeatedTransformAndNumericShortcutKeepPreview() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        transformFixture(*d);
        w.addDocument(d);
        QTest::qWait(30);
        Canvas *c = w.currentCanvas();
        const int history = d->historyLabels().size();
        for (auto tool : {Canvas::Fill, Canvas::Wand, Canvas::Brush, Canvas::Hand}) {
            w.findChild<QAction *>("tool" + QString::number(tool))->trigger();
            w.findChild<QAction *>("transform")->trigger();
            QTest::keyClick(c, Qt::Key_Right);
            const QTransform preview = c->transformGeometry();
            for (const auto *action : {"fit", "actual", "zoomIn", "zoomOut", "transform"}) {
                w.findChild<QAction *>(action)->trigger();
                QVERIFY(c->isTransforming());
                QCOMPARE(c->transformGeometry(), preview);
                QCOMPARE(d->state.layers[0].geometry, QTransform());
                QCOMPARE(d->historyLabels().size(), history);
            }
            d->changed();
            QTest::qWait(40);
            for (const auto *name : {"brushControls", "flowControls", "selectionMode",
                                     "selectionTolerance", "contiguousSelection"})
                QVERIFY2(!w.findChild<QWidget *>(name)->isVisible(), name);
            c->cancelTransform();
        }
        QVERIFY(c->beginTransform());
        QTest::keyClick(c, Qt::Key_Right);
        const QTransform preview = c->transformGeometry();
        bool handled = false;
        QTimer timer;
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<TransformDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            dialog->findChild<QDoubleSpinBox *>("scaleX")->setValue(60);
            handled = c->isTransforming() && c->transformGeometry() != preview;
            dialog->reject();
        });
        timer.start();
        c->setFocus();
        QTest::keyClick(c, Qt::Key_T, Qt::ControlModifier);
        QVERIFY(c->isTransforming());
        QCOMPARE(c->transformGeometry(), preview);
        // Exercise the actual keyboard route to Numeric while a session is pending.
        QTest::keyClick(c, Qt::Key_T, Qt::ControlModifier | Qt::AltModifier);
        timer.stop();
        QVERIFY(handled);
        QVERIFY(c->isTransforming());
        QCOMPARE(c->transformGeometry(), preview);
        QCOMPARE(d->historyLabels().size(), history);
        c->cancelTransform();
        w.close();
    }
    void onCanvasFullLayerFitKeepsHandlesClearOfControlsAndRulers() {
        Document d;
        QVERIFY(d.create({360, 260}, true));
        Canvas c(&d);
        prepare(c);
        c.fit();
        QVERIFY(c.beginTransform());
        QWidget *controls = c.findChild<QWidget *>("transformControls");
        for (int i = TransformHandles::TopLeft; i <= TransformHandles::Rotate; ++i) {
            const QPointF at = c.transformHandlePosition(TransformHandles::Handle(i));
            QVERIFY2(!QRectF(controls->geometry()).adjusted(-9, -9, 9, 9).contains(at),
                     qPrintable(QString::number(i)));
        }
        const QPointF rotation = c.transformHandlePosition(TransformHandles::Rotate);
        QVERIFY(QRectF(c.rect().adjusted(33, 33, -9, -9)).contains(rotation));
        const QPointF before = c.toWidget(QPointF());
        QTest::mousePress(&c, Qt::MiddleButton, {}, {60, 60});
        QFocusEvent focusOut(QEvent::FocusOut);
        QApplication::sendEvent(&c, &focusOut);
        QTest::mouseMove(&c, {90, 90});
        QTest::mouseRelease(&c, Qt::MiddleButton, {}, {90, 90});
        QCOMPARE(c.toWidget(QPointF()), before);
        QVERIFY(c.isTransforming());
        c.cancelTransform();
    }
    void onCanvasControlsRenderAndAdapt() {
        Document d;
        transformFixture(d);
        Canvas c(&d);
        prepare(c);
        QVERIFY(c.beginTransform());
        for (QSize size : {QSize(760, 580), QSize(360, 320), QSize(220, 220)}) {
            c.resize(size);
            c.fit();
            QTest::qWait(10);
            QWidget *controls = c.findChild<QWidget *>("transformControls");
            QVERIFY(controls->isVisible());
            QVERIFY(c.rect().contains(controls->geometry()));
            for (const auto *id : {"transformNumeric", "transformCancel", "transformApply"}) {
                auto *button = c.findChild<QToolButton *>(id);
                QVERIFY(button->isVisible());
                const QPoint inControls = button->mapTo(controls, QPoint());
                QVERIFY(controls->rect().contains(QRect(inControls, button->size())));
                QVERIFY(!button->accessibleName().isEmpty());
            }
        }
        c.cancelTransform();
        const QString directory = qEnvironmentVariable("PHOTODECK_TRANSFORM_SCREENSHOTS");
        if (directory.isEmpty())
            return;
        QVERIFY(QDir().mkpath(directory));
        MainWindow w(false, false);
        w.resize(1280, 800);
        auto *demo = new Document;
        demo->makeDemo();
        demo->state.active = 2;
        w.addDocument(demo);
        w.show();
        QTest::qWait(50);
        Canvas *canvas = w.currentCanvas();
        canvas->fit();
        QVERIFY(canvas->beginTransform());
        const QPointF center = canvas->transformHandlePosition(TransformHandles::Move);
        const QPoint handle = canvas->transformHandlePosition(TransformHandles::Rotate).toPoint();
        const QPointF fromCenter = handle - center;
        const qreal angle = qDegreesToRadians(-12.0);
        const QPoint target = (center + QPointF(fromCenter.x() * std::cos(angle) -
                                                fromCenter.y() * std::sin(angle),
                                                fromCenter.x() * std::sin(angle) +
                                                fromCenter.y() * std::cos(angle))).toPoint();
        drag(*canvas, handle, target);
        QTest::qWait(50);
        QVERIFY(w.grab().save(directory + "/transform-text-preview-1280.png"));
        w.resize(900, 620);
        canvas->fit();
        QTest::qWait(50);
        QVERIFY(w.grab().save(directory + "/transform-text-preview-900.png"));
        QVERIFY(canvas->applyTransform());
        QTest::qWait(50);
        QVERIFY(w.grab().save(directory + "/transform-text-applied-900.png"));
        demo->markSaved();
        w.close();
    }
    void onCanvasCompactWindowLeavesImageClearWithAccessibleActions() {
        MainWindow window(false, false);
        window.applyUiSettings(UiSettings::forPreset("graphite"), true, false);
        auto *document = new Document;
        QVERIFY(document->create({2400, 1600}, true));
        document->state.layers[0].name = "QA-moderate-2400x1600 kopya kopya kopya kopya kopya";
        document->markSaved();
        window.addDocument(document);
        window.resize(996, 676);
        window.show();
        QTest::qWait(30);
        QCOMPARE(window.size(), QSize(996, 676));
        Canvas *canvas = window.currentCanvas();
        canvas->fit();
        QVERIFY(canvas->beginTransform());
        QTest::qWait(10);
        window.grab(); // Rendering must not expose a deferred, narrower header layout.
        auto *context = canvas->findChild<QWidget *>("transformContext");
        auto *controls = canvas->findChild<QWidget *>("transformControls");
        auto *details = canvas->findChild<QPushButton *>("transformDetails");
        auto *scope = canvas->findChild<QLabel *>("transformScope");
        QVERIFY(context && controls && details && scope);
        QVERIFY(!details->isChecked());
        QVERIFY(details->isVisible() && details->isEnabled());
        QVERIFY(!details->accessibleName().isEmpty());
        QVERIFY(!details->toolTip().isEmpty());
        QVERIFY(scope->isVisible());
        QVERIFY(scope->visibleRegion().contains(scope->rect()));
        QVERIFY(scope->height() >= scope->heightForWidth(scope->width()));
        QVERIFY(details->visibleRegion().contains(details->rect()));
        QVERIFY(scope->text().contains("Katmanın tamamı"));
        QVERIFY(scope->accessibleName().contains(document->state.layers[0].name));
        QVERIFY(!canvas->findChild<QLabel *>("transformMetrics")->isVisible());
        QVERIFY(!canvas->findChild<QLabel *>("transformHelp")->isVisible());
        QVERIFY(!canvas->findChild<QLabel *>("transformInfo")->isVisible());
        QVERIFY(canvas->rect().contains(context->geometry()));
        QVERIFY(canvas->rect().contains(controls->geometry()));
        QVERIFY(!context->geometry().intersects(controls->geometry()));
        QVERIFY(context->width() <= 360);
        QVERIFY(context->height() <= 60);
        QCOMPARE(controls->size(), compactTransformControlsSize(*controls));
        const QRectF image(canvas->toWidget(QPointF()), QSizeF(document->state.size) * canvas->zoom);
        // The old 540px context plus duplicate measurement row covered most of
        // the fitted image at this actual app-window size. Both now fit in its
        // gutters, rather than merely moving to different parts of the image.
        QVERIFY(!QRectF(context->geometry()).intersects(image));
        QVERIFY2(!QRectF(controls->geometry()).intersects(image),
                 qPrintable(QString("canvas=%1x%2 controls=%3,%4 %5x%6 image=%7,%8 %9x%10")
                                .arg(canvas->width()).arg(canvas->height())
                                .arg(controls->x()).arg(controls->y())
                                .arg(controls->width()).arg(controls->height())
                                .arg(image.x()).arg(image.y()).arg(image.width()).arg(image.height())));
        QVector<QRect> buttonBounds;
        for (const char *id : {"transformNumeric", "transformCancel", "transformApply"}) {
            auto *button = canvas->findChild<QToolButton *>(id);
            QVERIFY(button && button->isVisible() && button->isEnabled());
            QVERIFY(!button->accessibleName().isEmpty());
            QVERIFY(button->width() >= button->sizeHint().width());
            QVERIFY(button->height() >= button->sizeHint().height());
            const QRect bounds(button->mapTo(canvas, QPoint()), button->size());
            QVERIFY(controls->geometry().contains(bounds));
            for (const QRect &previous : buttonBounds)
                QVERIFY(!bounds.intersects(previous));
            buttonBounds.append(bounds);
            QCOMPARE(canvas->childAt(bounds.center()), button);
        }
        const QRect detailsBounds(details->mapTo(canvas, QPoint()), details->size());
        QVERIFY(context->geometry().contains(detailsBounds));
        QCOMPARE(canvas->childAt(detailsBounds.center()), details);
        for (const QRectF &target : visibleTransformTargets(*canvas)) {
            QVERIFY(!QRectF(context->geometry()).intersects(target));
            QVERIFY(!QRectF(controls->geometry()).intersects(target));
        }
        const QString directory = qEnvironmentVariable("PHOTODECK_TRANSFORM_SCREENSHOTS");
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(window.grab().save(directory + "/transform-compact-996x676.png"));
        }
        canvas->cancelTransform();
        QVERIFY(document->setSelection(Selection(100, 100, 200, 150)));
        QVERIFY(canvas->beginTransform());
        QVERIFY(details->isChecked());
        QTest::mouseClick(details, Qt::LeftButton);
        QTest::qWait(10);
        auto *copy = canvas->findChild<QPushButton *>("transformCopySelection");
        QVERIFY(copy && copy->isVisible());
        QVERIFY(copy->visibleRegion().contains(copy->rect()));
        QVERIFY(scope->visibleRegion().contains(scope->rect()));
        QVERIFY(scope->height() >= scope->heightForWidth(scope->width()));
        QVERIFY(details->visibleRegion().contains(details->rect()));
        QCOMPARE(canvas->childAt(copy->mapTo(canvas, copy->rect().center())), copy);
        QTest::mouseClick(copy, Qt::LeftButton);
        QVERIFY(canvas->isTransforming());
        QVERIFY(scope->text().contains("Seçim kopyası"));
        QCOMPARE(document->state.layers.size(), 1);
        canvas->cancelTransform();
        window.close();
    }
    void onCanvasCompactDetailsToggleResizeNumericAndHistory() {
        Document document;
        transformFixture(document);
        Canvas canvas(&document);
        prepare(canvas);
        const Layer original = document.state.layers[0];
        const int history = document.historyLabels().size();
        QVERIFY(canvas.beginTransform());
        QTest::qWait(10);
        auto *details = canvas.findChild<QPushButton *>("transformDetails");
        auto *metrics = canvas.findChild<QLabel *>("transformMetrics");
        QVERIFY(details && metrics);
        QVERIFY(details->isChecked());
        QTest::keyClick(&canvas, Qt::Key_Right);
        const QTransform preview = canvas.transformGeometry();
        const auto verifyPreview = [&] {
            QVERIFY(canvas.isTransforming());
            QCOMPARE(canvas.transformGeometry(), preview);
            QCOMPARE(document.state.layers[0].geometry, original.geometry);
            QCOMPARE(document.state.layers[0].pixels, original.pixels);
            QCOMPARE(document.state.layers[0].mask, original.mask);
            QCOMPARE(document.historyLabels().size(), history);
        };
        for (const QSize size : {QSize(512, 464), QSize(760, 580), QSize(512, 464)}) {
            canvas.resize(size);
            canvas.fit();
            QCOMPARE(details->isChecked(), size.width() >= 640);
            QCOMPARE(metrics->isVisible(), details->isChecked());
            verifyPreview();
        }
        // Keyboard-operable disclosure, an explicit choice across resizes, and
        // repeated unchanged-size placement must never consume the preview.
        for (bool expanded : {true, false, true, false}) {
            details->setFocus();
            QTest::keyClick(details, Qt::Key_Space);
            QCOMPARE(details->isChecked(), expanded);
            for (const QSize size : {QSize(760, 580), QSize(512, 464), QSize(512, 464)}) {
                canvas.resize(size);
                canvas.fit();
                QTest::qWait(5);
                QCOMPARE(details->isChecked(), expanded);
                QCOMPARE(metrics->isVisible(), expanded);
                auto *controls = canvas.findChild<QWidget *>("transformControls");
                auto *context = canvas.findChild<QWidget *>("transformContext");
                QVERIFY(canvas.rect().contains(controls->geometry()));
                QVERIFY(canvas.rect().contains(context->geometry()));
                QVERIFY(!context->geometry().intersects(controls->geometry()));
                verifyPreview();
            }
        }
        bool numericVisited = false;
        QTimer timer;
        timer.setInterval(5);
        connect(&timer, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<TransformDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            numericVisited = true;
            dialog->findChild<QDoubleSpinBox *>("scaleX")->setValue(60);
            dialog->reject();
        });
        timer.start();
        QTest::mouseClick(canvas.findChild<QToolButton *>("transformNumeric"), Qt::LeftButton);
        timer.stop();
        QVERIFY(numericVisited);
        verifyPreview();
        QTest::mouseClick(canvas.findChild<QToolButton *>("transformCancel"), Qt::LeftButton);
        QVERIFY(!canvas.isTransforming());
        QCOMPARE(document.state.layers[0].geometry, original.geometry);
        QCOMPARE(document.historyLabels().size(), history);
        QVERIFY(canvas.beginTransform());
        QVERIFY(!details->isChecked()); // A new compact session uses the default again.
        QTest::keyClick(&canvas, Qt::Key_Right);
        QTest::mouseClick(canvas.findChild<QToolButton *>("transformApply"), Qt::LeftButton);
        QVERIFY(!canvas.isTransforming());
        QCOMPARE(document.historyLabels().size(), history + 1);
        QCOMPARE(document.state.layers[0].geometry, preview);
        document.undo();
        QCOMPARE(document.state.layers[0].geometry, original.geometry);
        document.redo();
        QCOMPARE(document.state.layers[0].geometry, preview);
        QVERIFY(document.setSelection(Selection(120, 100, 30, 20)));
        QVERIFY(canvas.beginTransform());
        // The selection warning/copy action stays discoverable even in compact
        // mode; collapsing measurements never hides or changes that scope.
        QVERIFY(details->isChecked());
        auto *copy = canvas.findChild<QPushButton *>("transformCopySelection");
        QVERIFY(copy->isVisible());
        QTest::mouseClick(details, Qt::LeftButton);
        QTest::qWait(10);
        QVERIFY(!metrics->isVisible());
        QVERIFY(copy->isVisible());
        QVERIFY(copy->visibleRegion().contains(copy->rect()));
        auto *scope = canvas.findChild<QLabel *>("transformScope");
        QVERIFY(scope->text().contains("Seçim sabit kalır"));
        QVERIFY(scope->visibleRegion().contains(scope->rect()));
        QVERIFY(scope->height() >= scope->heightForWidth(scope->width()));
        QVERIFY(details->visibleRegion().contains(details->rect()));
        QCOMPARE(canvas.childAt(copy->mapTo(&canvas, copy->rect().center())), copy);
        QTest::mouseClick(copy, Qt::LeftButton);
        QVERIFY(scope->text().contains("Seçim kopyası"));
        canvas.cancelTransform();
    }
    void onCanvasAccessibleDetailsToggleTracksVisibilityAndDefaults() {
        Document document;
        transformFixture(document);
        Canvas canvas(&document);
        prepare(canvas);
        const int history = document.historyLabels().size();
        QVERIFY(canvas.beginTransform());
        QTest::keyClick(&canvas, Qt::Key_Right);
        const QTransform preview = canvas.transformGeometry();
        auto *details = canvas.findChild<QPushButton *>("transformDetails");
        auto *metrics = canvas.findChild<QLabel *>("transformMetrics");
        auto *help = canvas.findChild<QLabel *>("transformHelp");
        QVERIFY(details && metrics && help);
        // Automatic checked-state updates must not become explicit choices.
        for (const QSize size : {QSize(512, 464), QSize(760, 580), QSize(512, 464)}) {
            canvas.resize(size);
            canvas.fit();
            QCOMPARE(details->isChecked(), size.width() >= 640);
            QCOMPARE(metrics->isVisible(), details->isChecked());
        }
        auto *accessible = QAccessible::queryAccessibleInterface(details);
        QVERIFY(accessible);
        auto *actions = accessible->actionInterface();
        QVERIFY(actions);
        QVERIFY(actions->actionNames().contains(QAccessibleActionInterface::toggleAction()));
        QSignalSpy clicked(details, &QPushButton::clicked);
        // Qt exposes a toggle action distinct from press/click for checkable
        // buttons. Screen readers may use it without emitting clicked().
        for (bool expanded : {true, false, true}) {
            actions->doAction(QAccessibleActionInterface::toggleAction());
            QCOMPARE(details->isChecked(), expanded);
            QCOMPARE(bool(accessible->state().checked), expanded);
            QCOMPARE(clicked.count(), 0);
            QCOMPARE(metrics->isVisible(), expanded);
            QCOMPARE(help->isVisible(), expanded);
            QCOMPARE(accessible->text(QAccessible::Name),
                     expanded ? QString("Dönüşüm ayrıntılarını gizle")
                              : QString("Dönüşüm ayrıntılarını göster"));
            for (const QSize size : {QSize(760, 580), QSize(512, 464)}) {
                canvas.resize(size);
                canvas.fit();
                QTest::qWait(5);
                QCOMPARE(details->isChecked(), expanded);
                QCOMPARE(metrics->isVisible(), expanded);
                QCOMPARE(help->isVisible(), expanded);
                QVERIFY(canvas.isTransforming());
                QCOMPARE(canvas.transformGeometry(), preview);
                QCOMPARE(document.state.layers[0].geometry, QTransform());
                QCOMPARE(document.historyLabels().size(), history);
            }
        }
        canvas.cancelTransform();
        QVERIFY(canvas.beginTransform());
        QVERIFY(!details->isChecked());
        QVERIFY(!metrics->isVisible());
        canvas.cancelTransform();
    }
    void onCanvasCompactButtonsWrapWithoutClipping() {
        for (bool rulers : {false, true}) {
            Document d;
            transformFixture(d);
            Canvas c(&d);
            prepare(c);
            c.showRulers = rulers;
            c.resize(180, 100);
            c.fit();
            // Model native styles whose honest button hints exceed a single
            // compact row, without depending on the machine's default font.
            for (const auto *id : {"transformNumeric", "transformCancel", "transformApply"})
                c.findChild<QToolButton *>(id)->setStyleSheet(
                    "min-width:50px; padding:6px 9px; font-size:11px;");
            QVERIFY(c.beginTransform());
            auto *controls = c.findChild<QWidget *>("transformControls");
            auto *numeric = c.findChild<QToolButton *>("transformNumeric");
            auto *cancel = c.findChild<QToolButton *>("transformCancel");
            auto *apply = c.findChild<QToolButton *>("transformApply");
            QVERIFY(c.rect().contains(controls->geometry()));
            QVERIFY(numeric->y() < cancel->y());
            QCOMPARE(cancel->y(), apply->y());
            for (auto *button : {numeric, cancel, apply}) {
                QVERIFY(controls->rect().contains(button->geometry()));
                QVERIFY(button->width() >= button->sizeHint().width());
                QVERIFY(button->height() >= button->sizeHint().height());
            }
            c.cancelTransform();
        }
    }
    void onCanvasNativeMetricsRefreshAfterLabelAndRowChanges_data() {
        QTest::addColumn<bool>("wideText");
        QTest::newRow("wide-font") << true;
        QTest::newRow("wide-padding") << false;
    }
    void onCanvasNativeMetricsRefreshAfterLabelAndRowChanges() {
        QFETCH(bool, wideText);
        QScopedPointer<QStyle> nativeStyle(QStyleFactory::create("Windows"));
        QVERIFY(nativeStyle);
        for (bool rulers : {false, true}) {
            Document d;
            transformFixture(d);
            Canvas c(&d);
            prepare(c);
            c.showRulers = rulers;
            for (auto *button : c.findChildren<QToolButton *>()) {
                button->setStyle(nativeStyle.data());
                if (wideText) {
                    const QString label = button->text();
                    const bool numeric = button->objectName() == "transformNumeric";
                    button->setText(numeric ? "123"
                                           : button->objectName() == "transformCancel" ? "×" : "✓");
                    button->ensurePolished();
                    QFont font = button->font();
                    // Native fallback fonts can apply stretch nonlinearly. Use
                    // the complete polished compact hint, including padding,
                    // to keep this synthetic fixture feasible in a 180px panel.
                    const int maximumWidth = numeric ? 92 : 60;
                    int stretch = 300;
                    do {
                        font.setStretch(stretch);
                        button->setFont(font);
                        button->ensurePolished();
                    } while (button->sizeHint().width() > maximumWidth && --stretch >= 100);
                    QVERIFY2(button->sizeHint().width() <= maximumWidth,
                             qPrintable(QString("%1 compact hint=%2 maximum=%3 stretch=%4")
                                            .arg(button->objectName()).arg(button->sizeHint().width())
                                            .arg(maximumWidth).arg(font.stretch())));
                    button->setText(label);
                } else
                    button->setStyleSheet("min-width:50px; padding:6px 9px; font-size:11px;");
            }
            const auto verify = [&] {
                auto *controls = c.findChild<QWidget *>("transformControls");
                QVERIFY(c.rect().contains(controls->geometry()));
                for (auto *button : controls->findChildren<QToolButton *>()) {
                    QVERIFY(button->isVisible());
                    QVERIFY2(controls->rect().contains(button->geometry()),
                             qPrintable(QString("%1 canvas=%2x%3 controls=%4x%5 button=%6,%7 %8x%9")
                                            .arg(button->objectName()).arg(c.width()).arg(c.height())
                                            .arg(controls->width()).arg(controls->height())
                                            .arg(button->x()).arg(button->y())
                                            .arg(button->width()).arg(button->height())));
                    QVERIFY(button->width() >= button->sizeHint().width());
                    QVERIFY(button->height() >= button->sizeHint().height());
                }
                if (c.findChild<QLabel *>("transformInfo")->isHidden())
                    QCOMPARE(controls->size(), compactTransformControlsSize(*controls));
            };
            // Switch between full/compact labels and one/two button rows.
            // Check before event processing, after it, and with an unchanged
            // frame size: a nested grid must never reuse earlier label metrics.
            for (QSize size : {QSize(760, 580), QSize(180, 100), QSize(360, 100),
                               QSize(220, 160), QSize(601, 185), QSize(760, 580)}) {
                c.resize(size);
                c.fit();
                QVERIFY(c.beginTransform());
                verify();
                QTest::qWait(10);
                verify();
                const QSize controlsSize = c.findChild<QWidget *>("transformControls")->size();
                c.fit();
                verify();
                QCOMPARE(c.findChild<QWidget *>("transformControls")->size(), controlsSize);
                c.cancelTransform();
                QVERIFY(c.beginTransform());
                verify();
                c.cancelTransform();
            }
        }
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
    void onCanvasMinimumWindowViewportContainsControls_data() {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<bool>("largeUi");
        QTest::addColumn<bool>("squeezeDock");
        QTest::addColumn<bool>("layersOnLeft");
        for (const auto &preset : uiPresets()) {
            QTest::newRow(qPrintable(preset.id)) << preset.id << false << false << false;
            QTest::newRow(qPrintable(preset.id + "-large-ui")) << preset.id << true << false << false;
        }
        QTest::newRow("focus-large-ui-layers-left") << QString("focus") << true << false << true;
        QTest::newRow("graphite-wide-dock") << QString("graphite") << false << true << false;
        QTest::newRow("touch-wide-dock") << QString("touch") << false << true << false;
        QTest::newRow("touch-large-ui-wide-dock") << QString("touch") << true << true << false;
    }
    void onCanvasMinimumWindowViewportContainsControls() {
        QFETCH(QString, preset);
        QFETCH(bool, largeUi);
        QFETCH(bool, squeezeDock);
        QFETCH(bool, layersOnLeft);
        MainWindow w(false, false);
        auto *d = new Document;
        d->makeDemo();
        d->state.active = 2;
        w.addDocument(d);
        UiSettings settings = UiSettings::forPreset(preset);
        if (layersOnLeft)
            settings.panelSide = "left";
        if (largeUi) {
            settings.panelWidth = 440;
            settings.toolLabels = true;
            settings.fontSize = 20;
            settings.iconSize = 36;
        }
        w.applyUiSettings(settings, true, false);
        w.resize(900, 620);
        w.show();
        QTest::qWait(30);
        QCOMPARE(w.size(), QSize(900, 620));
        if (squeezeDock) {
            w.resizeDocks({w.findChild<QDockWidget *>("layersDock")}, {800}, Qt::Horizontal);
            QTest::qWait(20);
        }
        Canvas *canvas = w.currentCanvas();
        canvas->fit();
        QVERIFY(canvas->beginTransform());
        QWidget *controls = canvas->findChild<QWidget *>("transformControls");
        QVERIFY(controls);
        const auto verifyViewport = [&] {
            QVERIFY(controls->isVisible());
            QVERIFY(canvas->rect().contains(controls->geometry()));
            for (QWidget *ancestor = canvas->parentWidget(); ancestor;
                 ancestor = ancestor->parentWidget()) {
                const QRect canvasBounds(canvas->mapTo(ancestor, QPoint()), canvas->size());
                QVERIFY2(ancestor->rect().contains(canvasBounds),
                         qPrintable(preset + " canvas extends beyond " +
                                    ancestor->metaObject()->className()));
                const QRect controlsBounds(controls->mapTo(ancestor, QPoint()), controls->size());
                QVERIFY2(ancestor->rect().contains(controlsBounds),
                         qPrintable(preset + " controls extend beyond " +
                                    ancestor->metaObject()->className()));
            }
            if (preset == "focus") {
                auto *workspace = w.findChild<QWidget *>("workspace");
                const QRect canvasBounds(canvas->mapTo(workspace, QPoint()), canvas->size());
                for (const auto *id : {"floatingTools", "floatingLayers"}) {
                    auto *panel = w.findChild<QWidget *>(id);
                    QVERIFY(panel && panel->isVisible());
                    QVERIFY(workspace->rect().contains(panel->geometry()));
                    QVERIFY(!canvasBounds.intersects(panel->geometry()));
                }
            }
            for (const auto *id : {"transformNumeric", "transformCancel", "transformApply"}) {
                auto *button = canvas->findChild<QToolButton *>(id);
                QVERIFY(button->isVisible());
                QVERIFY(controls->rect().contains(
                    QRect(button->mapTo(controls, QPoint()), button->size())));
                // Visibility/containment alone also pass for a button reduced
                // to its padding, with no room left to paint the label.
                const QSize hint = button->sizeHint();
                QVERIFY2(button->width() >= hint.width() && button->height() >= hint.height(),
                         qPrintable(QString("%1 %2 text=%3 actual=%4x%5 hint=%6x%7")
                                        .arg(preset, id, button->text())
                                        .arg(button->width()).arg(button->height())
                                        .arg(hint.width()).arg(hint.height())));
            }
        };
        verifyViewport();
        const auto verifyReachableTargets = [&] {
            const auto targets = visibleTransformTargets(*canvas);
            // A one-row fallback can preserve all pointer targets in this viewport.
            if (!controlsCanAvoidTargets(*canvas, targets, compactTransformControlsSize(*controls)))
                return;
            for (int i = 0; i < targets.size(); ++i)
                QVERIFY2(!QRectF(controls->geometry()).intersects(targets[i]),
                         qPrintable(QString("%1 largeUi=%2 canvas=%3x%4 controls=%5,%6 %7x%8 target=%9")
                                        .arg(preset).arg(largeUi).arg(canvas->width())
                                        .arg(canvas->height())
                                        .arg(controls->x()).arg(controls->y()).arg(controls->width())
                                        .arg(controls->height()).arg(i)));
        };
        verifyReachableTargets();
        // Check synchronously after real input as well as after layout events.
        // Touch's short viewport hides the help row to keep handles clear. A
        // temporary show/hide during placement must not leave 12 px buttons
        // until the next event-loop turn, even when the frame size is unchanged.
        for (const auto handle :
             {TransformHandles::Move, TransformHandles::BottomRight, TransformHandles::Rotate}) {
            const QPoint point = canvas->transformHandlePosition(handle).toPoint();
            if (!canvas->rect().contains(point) || canvas->childAt(point))
                continue;
            const QTransform before = canvas->transformGeometry();
            const QPoint delta = handle == TransformHandles::Rotate ? QPoint(14, 2) : QPoint(12, 7);
            drag(*canvas, point, point + delta);
            QVERIFY(canvas->transformGeometry() != before);
            verifyViewport();
            verifyReachableTargets();
            QTest::qWait(20);
            verifyViewport();
        }
        canvas->cancelTransform();
        QVERIFY(canvas->beginTransform());
        verifyViewport();
        verifyReachableTargets();
        if (preset == "touch" && !largeUi && !squeezeDock) {
            QCOMPARE(controls->size(), compactTransformControlsSize(*controls));
            QVERIFY(!canvas->findChild<QLabel *>("transformInfo")->isVisible());
            const QPoint handle =
                canvas->transformHandlePosition(TransformHandles::BottomRight).toPoint();
            QVERIFY(!canvas->childAt(handle));
            const QTransform before = canvas->transformGeometry();
            drag(*canvas, handle, handle + QPoint(10, 5));
            QVERIFY(canvas->transformGeometry() != before);
            verifyViewport();
            canvas->cancelTransform();
            QVERIFY(canvas->beginTransform());
            verifyReachableTargets();
        }
        // Fitted geometry must use the same visible extent as the controls.
        const QRectF imageBounds(canvas->toWidget(QPointF()), QSizeF(d->state.size) * canvas->zoom);
        QVERIFY(QRectF(canvas->rect()).contains(imageBounds));
        const QString directory = qEnvironmentVariable("PHOTODECK_VIEWPORT_SCREENSHOTS");
        if (!directory.isEmpty() && !largeUi && !squeezeDock) {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(w.grab().save(directory + "/transform-window-" + preset + ".png"));
        }
        // Resizing a fitted view and an explicitly zoomed view must keep every
        // button reachable without committing or replacing the transform preview.
        QTest::keyClick(canvas, Qt::Key_Right);
        const QTransform preview = canvas->transformGeometry();
        for (const QSize size : {QSize(1280, 800), QSize(900, 620)}) {
            w.resize(size);
            QTest::qWait(20);
            verifyViewport();
            for (qreal zoom : {.15, 1.0, 4.0}) {
                canvas->setZoom(zoom);
                verifyViewport();
                verifyReachableTargets();
                QCOMPARE(canvas->transformGeometry(), preview);
                QCOMPARE(d->state.layers[d->state.active].geometry, QTransform());
            }
            w.resize(size + QSize(40, 40));
            QTest::qWait(10);
            verifyViewport();
            QCOMPARE(canvas->zoom, 4.0);
            w.resize(size);
            QTest::qWait(10);
            verifyViewport();
            QCOMPARE(canvas->zoom, 4.0);
            QCOMPARE(canvas->transformGeometry(), preview);
            canvas->fit();
            verifyViewport();
            verifyReachableTargets();
            if (!largeUi && !squeezeDock &&
                (preset == "touch" || (preset == "focus" && size == QSize(1280, 800))))
                QCOMPARE(canvas->findChild<QLabel *>("transformInfo")->isVisible(),
                         size == QSize(1280, 800));
        }
        canvas->cancelTransform();
        w.close();
    }
    void onCanvasCompactControlsFitSmallUsableViewport() {
        Document d;
        transformFixture(d);
        Canvas canvas(&d);
        canvas.show();
        for (bool rulers : {false, true}) {
            canvas.showRulers = rulers;
            for (const QSize size :
                 {QSize(180, 100), QSize(360, 100), QSize(220, 160), QSize(601, 185)}) {
                canvas.resize(size);
                canvas.fit();
                QVERIFY(canvas.beginTransform());
                QTest::qWait(10);
                QCOMPARE(canvas.size(), size);
                QWidget *controls = canvas.findChild<QWidget *>("transformControls");
                QVERIFY(canvas.rect().contains(controls->geometry()));
                for (const auto *id : {"transformNumeric", "transformCancel", "transformApply"}) {
                    auto *button = canvas.findChild<QToolButton *>(id);
                    QVERIFY(button->isVisible());
                    QVERIFY(controls->rect().contains(
                        QRect(button->mapTo(controls, QPoint()), button->size())));
                }
                const auto targets = visibleTransformTargets(canvas);
                if (controlsCanAvoidTargets(canvas, targets, compactTransformControlsSize(*controls)))
                    for (const auto &target : targets)
                        QVERIFY(!QRectF(controls->geometry()).intersects(target));
                canvas.cancelTransform();
            }
        }
    }
};
QTEST_MAIN(TransformTests)
#include "test_transforms.moc"
