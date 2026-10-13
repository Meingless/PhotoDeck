#include "mainwindow.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QStandardPaths>
#include <QtTest>

namespace {
QPoint at(Canvas &c, int x, int y) {
    return c.toWidget({qreal(x), qreal(y)}).toPoint();
}
void drag(Canvas &c, QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = {}) {
    QTest::mousePress(&c, Qt::LeftButton, modifiers, from);
    QTest::mouseMove(&c, to);
    QTest::mouseRelease(&c, Qt::LeftButton, modifiers, to);
}
Selection ellipse(QSize size = {100, 100}) {
    QPainterPath p;
    p.addEllipse(QRectF(15, 15, 60, 60));
    return Selection::fromPath(p, size);
}
} // namespace
class SelectionTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void rectangleCompatibilityAndNoSelection() {
        Selection s;
        QVERIFY(s.isEmpty());
        QVERIFY(!s.hasPixels());
        QCOMPARE(s.valueAt({20, 20}), 255);
        s = {10, 20, 30, 40};
        QVERIFY(!s.isEmpty());
        QCOMPARE(s.bounds(), QRect(10, 20, 30, 40));
        QCOMPARE(s.valueAt({10, 20}), 255);
        QCOMPARE(s.valueAt({9, 20}), 0);
        s = QRect(2, 3, 4, 5);
        QCOMPARE(s.bounds(), QRect(2, 3, 4, 5));
        s = {};
        QVERIFY(s.isEmpty());
    }
    void ellipseHasRealCoverageAndBoundary() {
        Selection s = ellipse();
        QVERIFY(s.hasPixels());
        QCOMPARE(s.valueAt({45, 45}), 255);
        QCOMPARE(s.valueAt({16, 16}), 0);
        QVERIFY(!s.boundaryImage().isNull());
        QCOMPARE(s.boundaryImage().size(), s.bounds().size());
        QCOMPARE(s.valueAt({-1, -1}), 0);
    }
    void addSubtractAndIntersection() {
        Selection a(10, 10, 30, 30), b(25, 25, 30, 30);
        Selection add = a.combined(b, Selection::Add, {80, 80});
        QVERIFY(add.contains(15, 15));
        QVERIFY(add.contains(50, 50));
        QVERIFY(!add.contains(70, 70));
        Selection subtract = a.combined(b, Selection::Subtract, {80, 80});
        QVERIFY(subtract.contains(15, 15));
        QVERIFY(!subtract.contains(30, 30));
        Selection intersect = a.combined(b, Selection::Intersect, {80, 80});
        QVERIFY(intersect.contains(30, 30));
        QVERIFY(!intersect.contains(15, 15));
        Selection first = Selection{}.combined(b, Selection::Add, {80, 80});
        QCOMPARE(first.bounds(), b.bounds());
    }
    void emptyActiveSelectionCannotModifyOrCopy() {
        Selection all(0, 0, 20, 20);
        Selection zero = all.inverted({20, 20});
        QVERIFY(!zero.isEmpty());
        QVERIFY(!zero.hasPixels());
        QCOMPARE(zero.valueAt({10, 10}), 0);
        QImage image(20, 20, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::red);
        const QImage before = image;
        QVERIFY(!zero.paint(image, {}, [](QPainter &p) { p.fillRect(QRect(0, 0, 20, 20), Qt::blue); }));
        QCOMPARE(image, before);
        QVERIFY(zero.copyFrom(image).isNull());
        Selection inverse = zero.inverted({20, 20});
        QVERIFY(inverse.hasPixels());
        QCOMPARE(inverse.valueAt({10, 10}), 255);
    }
    void featherIsFractionalAndBounded() {
        Selection hard(20, 20, 20, 20);
        Selection soft = hard.feathered(4, {60, 60});
        QCOMPARE(soft.valueAt({30, 30}), 255);
        QCOMPARE(soft.valueAt({5, 5}), 0);
        QVERIFY(soft.valueAt({20, 30}) > 0 && soft.valueAt({20, 30}) < 255);
        QVERIFY(soft.valueAt({17, 30}) > 0);
        QCOMPARE(soft.valueAt({15, 30}), 0);
        QCOMPARE(soft.mask({60, 60}).format(), QImage::Format_Grayscale8);
        QCOMPARE(hard.feathered(0, {60, 60}).bounds(), hard.bounds());
    }
    void growShrinkAndClipping() {
        Selection s(10, 10, 20, 20);
        Selection grow = s.grown(3, {40, 40});
        QCOMPARE(grow.bounds(), QRect(7, 7, 26, 26));
        QCOMPARE(grow.valueAt({7, 7}), 255);
        Selection shrink = s.grown(-3, {40, 40});
        QCOMPARE(shrink.bounds(), QRect(13, 13, 14, 14));
        Selection vanish = s.grown(-20, {40, 40});
        QVERIFY(!vanish.isEmpty());
        QVERIFY(!vanish.hasPixels());
        Selection edge(0, 0, 10, 10);
        QCOMPARE(edge.grown(3, {20, 20}).bounds(), QRect(0, 0, 13, 13));
    }
    void colorRangeToleranceAndConnectivity() {
        QImage image(20, 12, QImage::Format_ARGB32);
        image.fill(Qt::red);
        for (int y = 0; y < image.height(); ++y)
            image.setPixelColor(10, y, Qt::blue);
        image.setPixelColor(3, 3, QColor(250, 2, 2));
        Selection connected = Selection::colorRange(image, {1, 1}, 8, true);
        QVERIFY(connected.contains(3, 3));
        QVERIFY(!connected.contains(12, 3));
        QVERIFY(!connected.contains(10, 3));
        Selection global = Selection::colorRange(image, {1, 1}, 8, false);
        QVERIFY(global.contains(12, 3));
        QVERIFY(!global.contains(10, 3));
        Selection exact = Selection::colorRange(image, {1, 1}, 0, false);
        QVERIFY(!exact.contains(3, 3));
        QVERIFY(Selection::colorRange(image, {-1, -1}, 5, true).isEmpty());
    }
    void alphaSelectionUsesDocumentOffset() {
        QImage image(4, 4, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(50, 100, 200, 128));
        Selection s = Selection::fromAlpha(image, {7, 8}, {20, 20});
        QCOMPARE(s.bounds(), QRect(7, 8, 4, 4));
        QCOMPARE(s.valueAt({8, 9}), 128);
        QCOMPARE(s.valueAt({6, 8}), 0);
    }
    void softPaintAndCopyPreservePremultipliedAlpha() {
        Selection soft = Selection(10, 10, 20, 20).feathered(4, {50, 50});
        QImage image(50, 50, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QVERIFY(soft.paint(image, {}, [](QPainter &p) { p.fillRect(QRect(0, 0, 50, 50), Qt::red); }));
        const int weight = soft.valueAt({10, 20});
        QCOMPARE(qAlpha(image.pixel(10, 20)), weight);
        QCOMPARE(qRed(image.pixel(10, 20)), weight);
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
        QImage original(50, 50, QImage::Format_ARGB32_Premultiplied);
        original.fill(Qt::red);
        QImage copy = soft.copyFrom(original);
        QPoint local = QPoint(10, 20) - soft.bounds().topLeft();
        QCOMPARE(qAlpha(copy.pixel(local)), weight);
        QCOMPARE(copy.pixelColor(local).red(), 255);
        QVERIFY(soft.paint(original, {}, [](QPainter &p) {
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.fillRect(QRect(0, 0, 50, 50), Qt::transparent);
        }));
        QCOMPARE(qAlpha(original.pixel(10, 20)), 255 - weight);
    }
    void paintingWithLayerOffset() {
        Selection s = ellipse();
        QImage image(80, 80, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QVERIFY(
            s.paint(image, {10, 10}, [](QPainter &p) { p.fillRect(QRect(0, 0, 80, 80), Qt::green); }));
        QCOMPARE(qAlpha(image.pixel(35, 35)), 255);
        QCOMPARE(qAlpha(image.pixel(6, 6)), 0);
        QCOMPARE(qAlpha(image.pixel(10, 35)), s.valueAt({20, 45}));
    }
    void ellipseAndModifierGestures() {
        Document d;
        QVERIFY(d.create({100, 100}));
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::EllipseSelect);
        drag(c, at(c, 15, 15), at(c, 75, 75));
        QCOMPARE(d.selection.valueAt({45, 45}), 255);
        QCOMPARE(d.selection.valueAt({16, 16}), 0);
        c.setTool(Canvas::Select);
        drag(c, at(c, 80, 80), at(c, 95, 95), Qt::ShiftModifier);
        QVERIFY(d.selection.contains(90, 90));
        QVERIFY(d.selection.contains(45, 45));
        drag(c, at(c, 30, 30), at(c, 60, 60), Qt::AltModifier);
        QVERIFY(!d.selection.contains(45, 45));
        QVERIFY(d.selection.contains(90, 90));
        QVERIFY(!d.dirty);
    }
    void lassoIncludesFinalPointerAndCancelRestoresSelection() {
        Document d;
        QVERIFY(d.create({100, 100}));
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Lasso);
        QTest::mousePress(&c, Qt::LeftButton, {}, at(c, 10, 10));
        QTest::mouseMove(&c, at(c, 80, 10));
        QTest::mouseMove(&c, at(c, 80, 80));
        QTest::mouseRelease(&c, Qt::LeftButton, {}, at(c, 10, 80));
        QVERIFY(d.selection.contains(20, 60));
        QVERIFY(!d.selection.contains(90, 90));
        QImage before = d.selection.mask(d.state.size);
        QTest::mousePress(&c, Qt::LeftButton, {}, at(c, 30, 30));
        QTest::mouseMove(&c, at(c, 50, 50));
        QTest::keyClick(&c, Qt::Key_Escape);
        QCOMPARE(d.selection.mask(d.state.size), before);
    }
    void polygonNeedsMultipleClicksAndFinishesWithEnter() {
        Document d;
        QVERIFY(d.create({100, 100}));
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::PolygonLasso);
        QTest::mouseClick(&c, Qt::LeftButton, {}, at(c, 10, 10));
        QTest::mouseClick(&c, Qt::LeftButton, {}, at(c, 80, 10));
        QTest::mouseClick(&c, Qt::LeftButton, {}, at(c, 80, 80));
        QVERIFY(d.selection.isEmpty());
        QTest::keyClick(&c, Qt::Key_Return);
        QVERIFY(d.selection.contains(70, 20));
        QVERIFY(!d.selection.contains(20, 70));
        const QImage before = d.selection.mask(d.state.size);
        QTest::mouseClick(&c, Qt::LeftButton, {}, at(c, 20, 20));
        QTest::keyClick(&c, Qt::Key_Escape);
        QCOMPARE(d.selection.mask(d.state.size), before);
    }
    void wandSamplesVisibleDocumentAndRespectsTolerance() {
        Document d;
        QVERIFY(d.create({100, 100}));
        d.state.layers[0].pixels.fill(Qt::red);
        QPainter p(&d.state.layers[0].pixels);
        p.fillRect(50, 0, 1, 100, Qt::blue);
        p.end();
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Wand);
        QTest::mouseClick(&c, Qt::LeftButton, {}, at(c, 20, 20));
        QVERIFY(d.selection.contains(20, 20));
        QVERIFY(!d.selection.contains(70, 20));
        c.contiguousSelection = false;
        QTest::mouseClick(&c, Qt::LeftButton, {}, at(c, 20, 20));
        QVERIFY(d.selection.contains(70, 20));
        QVERIFY(!d.selection.contains(50, 20));
    }
    void everyPaintToolRespectsNonRectangularSelection_data() {
        QTest::addColumn<int>("tool");
        for (int tool : {Canvas::Brush, Canvas::Pencil, Canvas::Eraser, Canvas::Clone, Canvas::Fill,
                         Canvas::Gradient, Canvas::Rectangle, Canvas::Ellipse})
            QTest::newRow(qPrintable(QString::number(tool))) << tool;
    }
    void everyPaintToolRespectsNonRectangularSelection() {
        QFETCH(int, tool);
        Document d;
        QVERIFY(d.create({100, 100}));
        d.state.layers[0].pixels.fill(QColor(10, 80, 160));
        const QRgb original = d.state.layers[0].pixels.pixel(20, 20);
        d.selection = ellipse();
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Tool(tool));
        c.foreground = Qt::red;
        c.background = Qt::white;
        c.hardness = 100;
        if (tool == Canvas::Clone) {
            QPainter p(&d.state.layers[0].pixels);
            p.fillRect(0, 0, 20, 20, Qt::red);
            p.end();
            QTest::mouseClick(&c, Qt::LeftButton, Qt::AltModifier, at(c, 10, 10));
            QTest::mouseClick(&c, Qt::LeftButton, {}, at(c, 45, 45));
        } else if (tool == Canvas::Fill)
            QTest::mouseClick(&c, Qt::LeftButton, {}, at(c, 45, 45));
        else
            drag(c, at(c, 10, 10), at(c, 80, 80));
        QCOMPARE(d.state.layers[0].pixels.pixel(20, 20), original);
        QVERIFY(d.state.layers[0].pixels.pixel(45, 45) != original);
    }
    void deletionAndMaskCreationUseCoverage() {
        Document d;
        QVERIFY(d.create({100, 100}, true));
        d.selection = ellipse();
        Canvas c(&d);
        c.deleteSelection();
        QCOMPARE(qAlpha(d.state.layers[0].pixels.pixel(45, 45)), 0);
        QCOMPARE(qAlpha(d.state.layers[0].pixels.pixel(16, 16)), 255);
        d.undo();
        d.selection = ellipse().feathered(3, d.state.size);
        QString error;
        const int weight = d.selection.valueAt({15, 45});
        QVERIFY(d.addMask(&error));
        QCOMPARE(qRed(d.state.layers[0].mask.pixel(15, 45)), weight);
        QCOMPARE(qRed(d.state.layers[0].mask.pixel(16, 16)), 0);
        d.selection = Selection(0, 0, 100, 100).inverted(d.state.size);
        const QImage mask = d.state.layers[0].mask;
        c.deleteSelection();
        QCOMPARE(d.state.layers[0].mask, mask);
    }
    void selectionControlsFollowDocumentTabsAndMenuState() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({100, 100});
        w.addDocument(d);
        auto *mode = w.findChild<QComboBox *>("selectionMode");
        auto *tolerance = w.findChild<QSpinBox *>("selectionTolerance");
        auto *contiguous = w.findChild<QCheckBox *>("contiguousSelection");
        QVERIFY(mode && tolerance && contiguous);
        w.findChild<QAction *>("tool17")->trigger();
        mode->setCurrentIndex(Selection::Subtract);
        tolerance->setValue(42);
        contiguous->setChecked(false);
        QCOMPARE(w.currentCanvas()->selectionMode, Selection::Subtract);
        QCOMPARE(w.currentCanvas()->selectionTolerance, 42);
        QVERIFY(!w.currentCanvas()->contiguousSelection);
        d->selection = {10, 10, 30, 30};
        d->changed();
        QVERIFY(w.findChild<QAction *>("invertSelection")->isEnabled());
        w.findChild<QAction *>("invertSelection")->trigger();
        QVERIFY(!d->selection.contains(20, 20));
        QVERIFY(d->selection.contains(60, 60));
        w.findChild<QAction *>("deselect")->trigger();
        QVERIFY(d->selection.isEmpty());
        auto *second = new Document;
        second->create({80, 80});
        w.addDocument(second);
        QTRY_COMPARE(tolerance->value(), 24);
        QCOMPARE(mode->currentIndex(), int(Selection::Replace));
        QVERIFY(contiguous->isChecked());
        w.close();
    }
};
QTEST_MAIN(SelectionTests)
#include "test_selections.moc"
