#include "mainwindow.h"
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QStandardPaths>
#include <QTabletEvent>
#include <QtTest>
#include <limits>
namespace {
QPointingDevice pen("test pen", 42, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                    QInputDevice::Capability::Position | QInputDevice::Capability::Pressure |
                        QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt,
                    1, 3);
QPointingDevice eraser("test eraser", 43, QInputDevice::DeviceType::Stylus,
                       QPointingDevice::PointerType::Eraser,
                       QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
void ready(Canvas &c) {
    c.resize(500, 500);
    c.show();
    c.actualSize();
    c.setTool(Canvas::Brush);
    c.foreground = Qt::red;
    c.hardness = 100;
    c.brushSize = 40;
}
void tablet(Canvas &c, QEvent::Type type, QPointF at, qreal pressure, float x = 0, float y = 0,
            const QPointingDevice *device = &pen) {
    QPointF local = c.toWidget(at);
    QTabletEvent event(type, device, local, c.mapToGlobal(local.toPoint()), pressure, x, y, 0, 0, 0,
                       Qt::NoModifier, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                       type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
    QApplication::sendEvent(&c, &event);
}
void tap(Canvas &c, qreal pressure, float x = 0, float y = 0, const QPointingDevice *device = &pen) {
    tablet(c, QEvent::TabletPress, {60, 60}, pressure, x, y, device);
    tablet(c, QEvent::TabletRelease, {60, 60}, 0, x, y, device);
}
QRect ink(const QImage &image) {
    QRect r;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)) > 5)
                r |= QRect(x, y, 1, 1);
    return r;
}
void mouse(Canvas &c, QEvent::Type type, QPointF at) {
    QPointF local = c.toWidget(at);
    QMouseEvent e(type, local, c.mapToGlobal(local.toPoint()),
                  type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                  type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&c, &e);
}
} // namespace
class BrushTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void pressureChangesDiameterAndOneUndo() {
        Document d;
        d.create({120, 120});
        Canvas c(&d);
        ready(c);
        tap(c, .25);
        QRect small = ink(d.composite());
        QVERIFY(small.width() >= 12 && small.width() <= 14);
        QCOMPARE(d.historyLabels().size(), 2);
        d.undo();
        QVERIFY(ink(d.composite()).isEmpty());
        tap(c, 1);
        QVERIFY(ink(d.composite()).width() >= 39);
        d.undo();
        QVERIFY(ink(d.composite()).isEmpty());
    }
    void pressureFlowCurveAndMouseIsolation() {
        Document d;
        d.create({120, 120});
        Canvas c(&d);
        ready(c);
        c.pressureSize = false;
        c.pressureFlow = true;
        c.pressureCurve = 2;
        tap(c, .5);
        int alpha = d.composite().pixelColor(60, 60).alpha();
        QVERIFY(alpha >= 63 && alpha <= 65);
        d.undo();
        mouse(c, QEvent::MouseButtonPress, {60, 60});
        mouse(c, QEvent::MouseButtonRelease, {60, 60});
        QCOMPARE(d.composite().pixelColor(60, 60).alpha(), 255);
    }
    void stationaryPressureAndReleasePreserveLastValidSample() {
        Document d;
        d.create({120, 120});
        Canvas c(&d);
        ready(c);
        tablet(c, QEvent::TabletPress, {60, 60}, 0);
        QVERIFY(ink(d.composite()).isEmpty());
        tablet(c, QEvent::TabletMove, {60, 60}, 1);
        tablet(c, QEvent::TabletRelease, {60, 60}, 0);
        QVERIFY(ink(d.composite()).width() >= 39);
        QCOMPARE(d.historyLabels().size(), 2);
        d.undo();
        QVERIFY(ink(d.composite()).isEmpty());
    }
    void tabletPressureInterpolatesDuringStroke() {
        Document d;
        d.create({160, 120});
        Canvas c(&d);
        ready(c);
        tablet(c, QEvent::TabletPress, {30, 60}, .1);
        tablet(c, QEvent::TabletMove, {120, 60}, 1);
        tablet(c, QEvent::TabletRelease, {120, 60}, 0);
        QCOMPARE(d.composite().pixelColor(30, 45).alpha(), 0);
        QVERIFY(d.composite().pixelColor(120, 45).alpha() > 200);
        QCOMPARE(d.historyLabels().size(), 2);
        d.undo();
        QVERIFY(ink(d.composite()).isEmpty());
    }
    void angleRoundnessAndTilt_data() {
        QTest::addColumn<int>("angle");
        QTest::addColumn<int>("roundness");
        QTest::addColumn<float>("x");
        QTest::addColumn<float>("y");
        QTest::addColumn<bool>("tilt");
        QTest::addColumn<bool>("vertical");
        QTest::newRow("flat") << 0 << 25 << 0.f << 0.f << false << false;
        QTest::newRow("rotated") << 90 << 25 << 0.f << 0.f << false << true;
        QTest::newRow("x-tilt") << 0 << 100 << 60.f << 0.f << true << false;
        QTest::newRow("y-tilt") << 0 << 100 << 0.f << 60.f << true << true;
    }
    void angleRoundnessAndTilt() {
        QFETCH(int, angle);
        QFETCH(int, roundness);
        QFETCH(float, x);
        QFETCH(float, y);
        QFETCH(bool, tilt);
        QFETCH(bool, vertical);
        Document d;
        d.create({120, 120});
        Canvas c(&d);
        ready(c);
        c.pressureSize = false;
        c.brushAngle = angle;
        c.brushRoundness = roundness;
        c.tiltAngle = c.tiltRoundness = tilt;
        tap(c, 1, x, y);
        QRect bounds = ink(d.composite());
        const int major = vertical ? bounds.height() : bounds.width();
        const int minor = vertical ? bounds.width() : bounds.height();
        QVERIFY(major >= 39 && major <= 42);
        QVERIFY(minor >= (tilt ? 19 : 9) && minor <= (tilt ? 22 : 12));
    }
    void spacingIsIndependentOfCollinearEventSplitsAndFeather() {
        auto draw = [](bool split) {
            Document d;
            d.create({120, 120});
            d.selection = Selection(QRect(30, 40, 70, 40)).feathered(3, d.state.size);
            Canvas c(&d);
            ready(c);
            c.brushSize = 10;
            c.hardness = 60;
            c.brushOpacity = 37;
            c.brushSpacing = 50;
            mouse(c, QEvent::MouseButtonPress, {20, 60});
            if (split)
                for (int x = 25; x <= 100; x += 5)
                    mouse(c, QEvent::MouseMove, {qreal(x), 60});
            else
                mouse(c, QEvent::MouseMove, {100, 60});
            mouse(c, QEvent::MouseButtonRelease, {100, 60});
            return d.composite();
        };
        QCOMPARE(draw(false), draw(true));
        QVERIFY(ink(draw(false)).width() > 50);
    }
    void wideSpacingAndFinalEndpoint() {
        Document d;
        d.create({120, 120});
        Canvas c(&d);
        ready(c);
        c.brushSize = 10;
        c.brushSpacing = 200;
        mouse(c, QEvent::MouseButtonPress, {20, 60});
        mouse(c, QEvent::MouseMove, {65, 60});
        mouse(c, QEvent::MouseButtonRelease, {65, 60});
        QCOMPARE(d.composite().pixelColor(20, 60).alpha(), 255);
        QCOMPARE(d.composite().pixelColor(30, 60).alpha(), 0);
        QCOMPARE(d.composite().pixelColor(40, 60).alpha(), 255);
        QCOMPARE(d.composite().pixelColor(65, 60).alpha(), 255);
        QCOMPARE(d.historyLabels().size(), 2);
    }
    void tabletEraserPointerPixelsAndMask() {
        Document d;
        d.create({120, 120}, true);
        Canvas c(&d);
        ready(c);
        c.pressureSize = false;
        c.pressureFlow = true;
        c.foreground.setAlpha(0);
        tap(c, .5, 0, 0, &eraser);
        QVERIFY(qAbs(d.composite().pixelColor(60, 60).alpha() - 127) <= 1);
        d.undo();
        QVERIFY(d.addMask(nullptr));
        d.editingMask = true;
        tap(c, .5, 0, 0, &eraser);
        QColor mask = d.state.layers[0].mask.pixelColor(60, 60);
        QCOMPARE(mask.alpha(), 255);
        QVERIFY(qAbs(mask.red() - 127) <= 1);
    }
    void tabletRespectsLockSelectionAndCancel() {
        Document d;
        d.create({120, 120});
        Canvas c(&d);
        ready(c);
        d.state.layers[0].locked = true;
        tap(c, 1);
        QVERIFY(!d.canUndo());
        d.state.layers[0].locked = false;
        d.selection = Selection(QRect(60, 0, 60, 120));
        tap(c, 1);
        QCOMPARE(d.composite().pixelColor(55, 60).alpha(), 0);
        QCOMPARE(d.composite().pixelColor(65, 60).alpha(), 255);
        d.undo();
        tablet(c, QEvent::TabletPress, {60, 60}, .5);
        QTest::keyClick(&c, Qt::Key_Escape);
        tablet(c, QEvent::TabletRelease, {60, 60}, 0);
        QVERIFY(ink(d.composite()).isEmpty());
        QVERIFY(!d.canUndo());
    }
    void invalidSensorInputDoesNotPaint() {
        Document d;
        d.create({120, 120});
        Canvas c(&d);
        ready(c);
        tablet(c, QEvent::TabletPress, {60, 60}, std::numeric_limits<qreal>::quiet_NaN());
        tablet(c, QEvent::TabletRelease, {60, 60}, 0);
        QVERIFY(!d.canUndo());
        QVERIFY(ink(d.composite()).isEmpty());
    }
    void brushSettingsAcceptCancelAndDocumentPreservation() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({120, 120});
        w.addDocument(d);
        auto *action = w.findChild<QAction *>("brushSettings");
        QVERIFY(action);
        const QImage before = d->composite();
        bool handled = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            dialog->findChild<QSpinBox *>("brushAngle")->setValue(90);
            dialog->findChild<QCheckBox *>("pressureFlow")->setChecked(true);
            handled = true;
            dialog->accept();
        });
        action->trigger();
        QVERIFY(handled);
        QCOMPARE(w.currentCanvas()->brushAngle, 90);
        QVERIFY(w.currentCanvas()->pressureFlow);
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            dialog->findChild<QSpinBox *>("brushAngle")->setValue(0);
            dialog->reject();
        });
        action->trigger();
        QCOMPARE(w.currentCanvas()->brushAngle, 90);
        QCOMPARE(d->composite(), before);
        QVERIFY(!d->dirty);
        QVERIFY(!d->canUndo());
        w.close();
    }
};
QTEST_MAIN(BrushTests)
#include "test_brushes.moc"
