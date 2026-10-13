#include "blending.h"
#include "mainwindow.h"
#include <QAction>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void fill(Document &d, int index, QColor color) {
    d.beginEdit();
    d.state.layers[index].pixels.fill(color);
    d.commit("Fixture pixels");
}
void stack(Document &d) {
    d.create({30, 20});
    fill(d, 0, QColor(40, 80, 200, 128));
    d.addLayer("Clipped");
    fill(d, 1, Qt::red);
    d.setClipping(true);
    d.markSaved();
}
} // namespace
class LayerOpsTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void clippingPreservesBaseAlphaAndUsesGroupOpacity() {
        Document d;
        stack(d);
        QColor pixel = d.composite().pixelColor(10, 10);
        QCOMPARE(pixel.alpha(), 128);
        QVERIFY(pixel.red() >= 253);
        QCOMPARE(pixel.blue(), 0);
        d.beginEdit();
        d.state.layers[0].opacity = .5;
        d.state.layers[1].opacity = .5;
        d.commit("Opacity");
        pixel = d.composite().pixelColor(10, 10);
        QVERIFY(qAbs(pixel.alpha() - 64) <= 1);
        QVERIFY(qAbs(pixel.red() - 148) <= 3);
        QVERIFY(qAbs(pixel.blue() - 100) <= 3);
        d.beginEdit();
        d.state.layers[0].visible = false;
        d.commit("Hide base");
        QCOMPARE(d.composite().pixelColor(10, 10).alpha(), 0);
        d.undo();
        QVERIFY(d.composite().pixelColor(10, 10).alpha() > 0);
    }
    void clippingUsesBaseMaskAndIndependentGeometry() {
        Document d;
        stack(d);
        QString e;
        d.state.active = 0;
        QVERIFY(d.addMask(&e));
        d.beginEdit();
        d.state.layers[0].mask.fill(Qt::black);
        {
            QPainter p(&d.state.layers[0].mask);
            p.fillRect(5, 4, 10, 8, Qt::white);
        }
        d.commit("Mask");
        d.state.active = 1;
        d.beginEdit();
        QVERIFY(d.setLayerTransform(QTransform::fromScale(.5, .5)));
        d.commit("Geometry");
        QImage result = d.composite();
        QCOMPARE(qAlpha(result.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(result.pixel(8, 6)), 128);
        QCOMPARE(qAlpha(result.pixel(20, 6)), 0);
        QVERIFY(result.pixelColor(8, 6).red() > 250);
    }
    void clipModesPreserveCoverage_data() {
        QTest::addColumn<int>("mode");
        for (int i = 0; i < Blending::Count; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void clipModesPreserveCoverage() {
        QFETCH(int, mode);
        QImage base(3, 3, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(200, 100, 50, 128));
        QImage source(3, 3, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(100, 200, 50, 200));
        Blending::clipCompose(base, source, {}, .7, mode);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) {
                QRgb p = base.pixel(x, y);
                QCOMPARE(qAlpha(p), 128);
                QVERIFY(qRed(p) <= 128 && qGreen(p) <= 128 && qBlue(p) <= 128);
            }
        QImage empty(3, 3, QImage::Format_ARGB32_Premultiplied);
        empty.fill(Qt::transparent);
        Blending::clipCompose(empty, source, {}, 1, mode);
        QCOMPARE(qAlpha(empty.pixel(1, 1)), 0);
    }
    void opaqueClipMatchesEstablishedBlendModes_data() {
        QTest::addColumn<int>("mode");
        for (int i = 0; i < Blending::Count; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void opaqueClipMatchesEstablishedBlendModes() {
        QFETCH(int, mode);
        QImage base(4, 4, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(200, 100, 50));
        QImage source(4, 4, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(100, 200, 50));
        QImage reference = base;
        Blending::compose(reference, source, {}, 1, mode, {10, -5});
        Blending::clipCompose(base, source, {}, 1, mode, {10, -5});
        QColor a = base.pixelColor(1, 1), b = reference.pixelColor(1, 1);
        QVERIFY(qAbs(a.red() - b.red()) <= 2);
        QVERIFY(qAbs(a.green() - b.green()) <= 2);
        QVERIFY(qAbs(a.blue() - b.blue()) <= 2);
    }
    void clipStackChainsAndRelease() {
        Document d;
        stack(d);
        d.addLayer("Second");
        fill(d, 2, Qt::green);
        QVERIFY(d.setClipping(true));
        QCOMPARE(d.composite().pixelColor(1, 1).alpha(), 128);
        QVERIFY(d.composite().pixelColor(1, 1).green() > 120);
        QVERIFY(d.setClipping(false));
        QCOMPARE(d.composite().pixelColor(1, 1).alpha(), 255);
        d.undo();
        QCOMPARE(d.composite().pixelColor(1, 1).alpha(), 128);
        d.state.active = 0;
        QString e;
        QVERIFY(!d.setClipping(true, &e));
        QVERIFY(!e.isEmpty());
        d.state.active = 1;
        d.beginEdit();
        d.state.layers[1].locked = true;
        d.commit("Lock");
        QVERIFY(!d.setClipping(false));
    }
    void deletingBaseReleasesChildrenAndBottomReorderIsValid() {
        Document d;
        stack(d);
        d.addLayer("Third");
        fill(d, 2, Qt::green);
        d.setClipping(true);
        d.state.active = 0;
        d.deleteLayer();
        QVERIFY(!d.state.layers[0].clipped);
        QVERIFY(!d.state.layers[1].clipped);
        d.undo();
        QVERIFY(d.state.layers[1].clipped);
        QVERIFY(d.state.layers[2].clipped);
        d.state.active = 1;
        d.reorder(-1);
        QVERIFY(!d.state.layers.first().clipped);
        QTemporaryDir temp;
        QString e;
        QVERIFY(d.save(temp.filePath("ordered.pdeck"), &e));
    }
    void mergeVisiblePreservesClippedStacksAndOffCanvas() {
        Document d;
        stack(d);
        d.beginEdit();
        d.state.layers[0].offset = {-4, -3};
        d.state.layers[1].blend = 6;
        d.state.layers[1].opacity = .6;
        d.commit("Setup");
        const QImage before = d.composite();
        QString e;
        QVERIFY2(d.mergeVisible(&e), qPrintable(e));
        QCOMPARE(d.composite(), before);
        QCOMPARE(d.state.layers.size(), 1);
        QCOMPARE(d.state.layers[0].offset, QPoint(-4, -3));
        d.undo();
        QCOMPARE(d.composite(), before);
        QVERIFY(d.state.layers[1].clipped);
    }
    void hiddenClippingBaseIsNotDiscardedByMergeVisible() {
        Document d;
        stack(d);
        d.beginEdit();
        d.state.layers[0].visible = false;
        d.commit("Hidden");
        d.addLayer("Visible independent");
        fill(d, 2, Qt::blue);
        QString e;
        QVERIFY(d.mergeVisible(&e));
        QCOMPARE(d.state.layers.size(), 3);
        QVERIFY(!d.state.layers[0].visible);
        QVERIFY(d.state.layers[1].clipped);
        d.undo();
        d.beginEdit();
        d.state.layers[0].visible = true;
        d.state.layers[1].visible = false;
        d.commit("Mixed visibility");
        const QImage before = d.composite();
        QVERIFY(!d.mergeVisible(&e));
        QCOMPARE(d.composite(), before);
    }
    void selectionCopyKeepsSourceAndFeatherAlpha() {
        Document d;
        d.create({30, 20});
        fill(d, 0, Qt::red);
        d.selection = Selection(5, 5, 10, 10).feathered(2, d.state.size);
        const QImage original = d.state.layers[0].pixels;
        const int weight = d.selection.valueAt({5, 10});
        QString e;
        QVERIFY(d.layerViaSelection(false, &e));
        QCOMPARE(d.state.layers[0].pixels, original);
        const Layer &copy = d.state.layers[1];
        QCOMPARE(qAlpha(copy.pixels.pixel(QPoint(5, 10) - copy.offset)), weight);
        d.undo();
        QCOMPARE(d.state.layers.size(), 1);
        QCOMPARE(d.state.layers[0].pixels, original);
    }
    void selectionCutIsOneUndoAndKeepsHardSelectionAppearance() {
        Document d;
        d.create({30, 20});
        fill(d, 0, Qt::red);
        d.selection = {5, 5, 10, 10};
        QImage before = d.composite();
        int count = d.historyLabels().size();
        QString e;
        QVERIFY(d.layerViaSelection(true, &e));
        QCOMPARE(d.historyLabels().size(), count + 1);
        QCOMPARE(d.composite(), before);
        QCOMPARE(d.state.layers[1].offset, QPoint(5, 5));
        QCOMPARE(qAlpha(d.state.layers[0].pixels.pixel(8, 8)), 0);
        d.undo();
        QCOMPARE(d.state.layers.size(), 1);
        QCOMPARE(d.composite(), before);
        d.beginEdit();
        d.state.layers[0].locked = true;
        d.commit("Lock");
        d.selection = {5, 5, 10, 10};
        QVERIFY(!d.layerViaSelection(true, &e));
    }
    void emptySelectionPixelsFailWithoutChangingState() {
        Document d;
        d.create({20, 20});
        d.selection = {2, 2, 8, 8};
        QString e;
        const int count = d.historyLabels().size();
        QVERIFY(!d.layerViaSelection(false, &e));
        QVERIFY(!e.isEmpty());
        QCOMPARE(d.state.layers.size(), 1);
        QCOMPARE(d.historyLabels().size(), count);
        QVERIFY(!d.dirty);
    }
    void clippedSelectionCopyRemainsInSameClipGroup() {
        Document d;
        stack(d);
        d.selection = {5, 5, 10, 10};
        QString e;
        QVERIFY(d.layerViaSelection(false, &e));
        QCOMPARE(d.state.layers.size(), 3);
        QVERIFY(d.state.layers[2].clipped);
        QCOMPARE(d.composite().pixelColor(8, 8).alpha(), 128);
        QVERIFY(!d.layerViaSelection(true, &e));
    }
    void mergeDownPreservesNormalAndClippedCases() {
        Document d;
        stack(d);
        QImage before = d.composite();
        QString e;
        QVERIFY2(d.mergeDown(&e), qPrintable(e));
        QCOMPARE(d.composite(), before);
        QCOMPARE(d.state.layers.size(), 1);
        d.undo();
        QVERIFY(d.state.layers[1].clipped);
        QVERIFY(d.setClipping(false));
        before = d.composite();
        QVERIFY(d.mergeDown(&e));
        QCOMPARE(d.composite(), before);
    }
    void unsupportedMergeDownCannotChangePixelsOrHistory() {
        Document d;
        d.create({30, 20}, true);
        d.addLayer("Multiply");
        fill(d, 1, QColor(120, 80, 200, 100));
        d.beginEdit();
        d.state.layers[1].blend = 1;
        d.commit("Blend");
        d.addLayer("Top");
        fill(d, 2, QColor(40, 180, 90, 100));
        const QImage before = d.composite();
        const int count = d.historyLabels().size();
        QString e;
        QVERIFY(!d.mergeDown(&e));
        QVERIFY(!e.isEmpty());
        QCOMPARE(d.composite(), before);
        QCOMPARE(d.historyLabels().size(), count);
    }
    void currentRoundTripAndAuthenticV3Fixture() {
        Document d;
        stack(d);
        QTemporaryDir temp;
        QString e, file = temp.filePath("clipped.pdeck");
        QVERIFY(d.save(file, &e));
        Document read;
        QVERIFY(read.load(file, &e));
        QVERIFY(read.state.layers[1].clipped);
        QCOMPARE(read.composite(), d.composite());
        QString legacy = QFINDTESTDATA("fixtures/v3-demo.pdeck");
        QVERIFY(!legacy.isEmpty());
        QVERIFY2(read.load(legacy, &e), qPrintable(e));
        for (const Layer &l : read.state.layers)
            QVERIFY(!l.clipped);
        d.state.layers[0].clipped = true;
        QVERIFY(!d.save(file, &e));
    }
    void menuActionsConnectToLayerOperations() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({30, 20}, true);
        d->addLayer("Top");
        fill(*d, 1, Qt::red);
        w.addDocument(d);
        auto *clip = w.findChild<QAction *>("clipLayer");
        QVERIFY(clip);
        clip->trigger();
        QVERIFY(d->state.layers[1].clipped);
        clip->trigger();
        QVERIFY(!d->state.layers[1].clipped);
        d->selection = {4, 4, 8, 8};
        d->changed();
        w.findChild<QAction *>("selectionCopyLayer")->trigger();
        QCOMPARE(d->state.layers.size(), 3);
        w.findChild<QAction *>("undo")->trigger();
        d->selection = {4, 4, 8, 8};
        d->changed();
        w.findChild<QAction *>("selectionCutLayer")->trigger();
        QCOMPARE(d->state.layers.size(), 3);
        d->undo();
        d->undo();
        d->undo();
        d->undo();
        d->undo();
        d->markSaved();
        w.close();
    }
};
QTEST_MAIN(LayerOpsTests)
#include "test_layer_ops.moc"
