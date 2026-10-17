#include "mainwindow.h"
#include "transformdialog.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageReader>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cmath>

namespace {
void fixture(Document &d) {
    QVERIFY(d.create({240, 180}));
    d.beginEdit();
    auto &layer = d.state.layers[0];
    layer.name = "Kaynak nesne";
    layer.pixels = QImage(80, 50, QImage::Format_ARGB32_Premultiplied);
    layer.pixels.fill(QColor("#d25b42"));
    QPainter painter(&layer.pixels);
    painter.fillRect(40, 0, 40, 50, QColor("#46b9c5"));
    painter.end();
    layer.offset = {70, 60};
    layer.mask = QImage(layer.pixels.size(), QImage::Format_ARGB32_Premultiplied);
    layer.mask.fill(Qt::white);
    layer.mask.setPixelColor(5, 5, QColor(128, 128, 128));
    d.commit("Usability fixture");
    d.markSaved();
}
void unchanged(const Layer &actual, const Layer &before) {
    QCOMPARE(actual.name, before.name);
    QCOMPARE(actual.pixels, before.pixels);
    QCOMPARE(actual.mask, before.mask);
    QCOMPARE(actual.offset, before.offset);
    QCOMPARE(actual.geometry, before.geometry);
    QCOMPARE(actual.text, before.text);
    QCOMPARE(actual.font, before.font);
    QCOMPARE(actual.color, before.color);
    QCOMPARE(actual.visible, before.visible);
    QCOMPARE(actual.locked, before.locked);
    QCOMPARE(actual.clipped, before.clipped);
    QCOMPARE(actual.opacity, before.opacity);
    QCOMPARE(actual.blend, before.blend);
    QCOMPARE(actual.brightness, before.brightness);
    QCOMPARE(actual.contrast, before.contrast);
    QCOMPARE(actual.saturation, before.saturation);
    QCOMPARE(actual.blur, before.blur);
    QVERIFY(actual.tones == before.tones);
    QVERIFY(actual.filters == before.filters);
    QVERIFY(actual.maskSettings == before.maskSettings);
}
void prepare(Canvas &canvas) {
    canvas.resize(760, 560);
    canvas.show();
    canvas.showRulers = false;
    canvas.fit();
    canvas.actualSize();
    QTest::qWait(10);
}
void drag(Canvas &canvas, QPointF from, QPointF to) {
    const QPoint start = canvas.toWidget(from).toPoint(), end = canvas.toWidget(to).toPoint();
    QTest::mousePress(&canvas, Qt::LeftButton, {}, start);
    QTest::mouseMove(&canvas, end);
    QTest::mouseRelease(&canvas, Qt::LeftButton, {}, end);
}
bool drive(QAction *action, const std::function<void(QDialog *)> &handle) {
    bool seen = false, timedOut = false;
    QPointer<QDialog> previous;
    QTimer driver, watchdog;
    driver.setInterval(10);
    watchdog.setSingleShot(true);
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog == previous)
            return;
        previous = dialog;
        seen = true;
        handle(dialog);
    });
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        timedOut = true;
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
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
        QInputMethodEvent event;
        event.setCommitString(path);
        QApplication::sendEvent(name, &event);
    }
    QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
}
qreal luminance(QColor color) {
    const auto linear = [](qreal value) {
        return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) +
           .0722 * linear(color.blueF());
}
qreal contrast(QColor one, QColor two) {
    const qreal a = luminance(one), b = luminance(two);
    return (qMax(a, b) + .05) / (qMin(a, b) + .05);
}
// Inspect the actual composed pixels behind a label, rather than assuming the
// scroll viewport and form inherited the dialog's intended surface palette.
void readableLabel(QDialog &dialog, QLabel *label, QScrollArea *scroll) {
    QVERIFY(label);
    QVERIFY(!label->text().isEmpty());
    scroll->ensureWidgetVisible(label);
    QTest::qWait(5);
    QVERIFY(label->isVisible());
    const QPixmap shot = dialog.grab();
    const qreal scale = shot.devicePixelRatio();
    const QPoint start = label->mapTo(&dialog, QPoint());
    const QRect visible = QRect(start, label->size()).intersected(
        QRect(scroll->viewport()->mapTo(&dialog, QPoint()), scroll->viewport()->size()));
    QVERIFY(!visible.isEmpty());
    const QRect pixels(qFloor(visible.x() * scale), qFloor(visible.y() * scale),
                       qFloor(visible.width() * scale), qFloor(visible.height() * scale));
    const QImage region = shot.toImage().copy(pixels).convertToFormat(QImage::Format_RGB32);
    QHash<QRgb, int> frequencies;
    for (int y = 0; y < region.height(); ++y)
        for (int x = 0; x < region.width(); ++x)
            ++frequencies[region.pixel(x, y)];
    QVERIFY(!frequencies.isEmpty());
    const QRgb background = std::max_element(frequencies.cbegin(), frequencies.cend(),
        [](int a, int b) { return a < b; }).key();
    const qreal intended = contrast(QColor(background), label->palette().color(QPalette::WindowText));
    QVERIFY2(intended >= 4.5, qPrintable(QString("%1 contrast=%2")
                                          .arg(label->text()).arg(intended)));
    int legiblePixels = 0;
    for (auto it = frequencies.cbegin(); it != frequencies.cend(); ++it)
        if (contrast(QColor(background), QColor(it.key())) >= 4.5)
            legiblePixels += it.value();
    QVERIFY2(legiblePixels >= 3, qPrintable("No readable rendered glyphs: " + label->text()));
}
void screenshot(QWidget &widget, const QString &name) {
    const QString directory = qEnvironmentVariable("PHOTODECK_UX_SCREENSHOTS");
    if (directory.isEmpty())
        return;
    QVERIFY(QDir().mkpath(directory));
    QString scale = qEnvironmentVariable("QT_SCALE_FACTOR", "1");
    scale.replace('.', '_');
    QVERIFY(widget.grab().save(directory + "/" + name + "-scale-" + scale + ".png"));
}
void unobstructedContextWhenPossible(Canvas &canvas) {
    auto *context = canvas.findChild<QFrame *>("transformContext");
    auto *controls = canvas.findChild<QFrame *>("transformControls");
    QVERIFY(context && controls);
    QVector<QRectF> obstacles{QRectF(controls->geometry())};
    const QRectF viewport = canvas.showRulers ? QRectF(canvas.rect().adjusted(24, 24, 0, 0))
                                             : QRectF(canvas.rect());
    for (int i = TransformHandles::TopLeft; i <= TransformHandles::Move; ++i) {
        const QPointF at = canvas.transformHandlePosition(TransformHandles::Handle(i));
        const QRectF reachable = QRectF(at - QPointF(10, 10), QSizeF(20, 20)).intersected(viewport);
        if (!reachable.isEmpty())
            obstacles.append(reachable);
    }
    const int right = canvas.width() - context->width();
    const int top = canvas.showRulers ? 28 : 4;
    const int bottom = canvas.height() - context->height();
    if (right < 0 || bottom < top)
        return;
    QVector<int> columns{0, right}, rows{top, bottom};
    for (const QRectF &obstacle : obstacles) {
        columns.append(qFloor(obstacle.left()) - context->width());
        columns.append(qCeil(obstacle.right()));
        rows.append(qFloor(obstacle.top()) - context->height());
        rows.append(qCeil(obstacle.bottom()));
    }
    bool possible = false;
    for (int x : columns)
        for (int y : rows) {
            if (x < 0 || x > right || y < top || y > bottom)
                continue;
            const QRectF candidate(QPointF(x, y), context->size());
            bool clear = true;
            for (const QRectF &obstacle : obstacles)
                clear &= !candidate.intersects(obstacle);
            possible |= clear;
        }
    if (possible)
        for (const QRectF &obstacle : obstacles)
            QVERIFY2(!QRectF(context->geometry()).intersects(obstacle),
                     "Scope context covers a reachable control despite a clear placement");
}
} // namespace

class UsabilityTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName("PhotoDeckUsabilityTests");
        QApplication::setOrganizationName("PhotoDeckTests");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QImageReader::setAllocationLimit(128);
    }
    void autoSelectUsesTopmostEligibleVisibleContent_data() {
        QTest::addColumn<QString>("condition");
        QTest::addColumn<int>("expected");
        QTest::newRow("opaque-top") << QString("opaque") << 1;
        QTest::newRow("transparent-hole") << QString("transparent") << 0;
        QTest::newRow("masked-hole") << QString("masked") << 0;
        QTest::newRow("hidden-top") << QString("hidden") << 0;
        QTest::newRow("locked-top") << QString("locked") << 0;
        QTest::newRow("zero-opacity-top") << QString("zero-opacity") << 0;
        QTest::newRow("transformed-top") << QString("transformed") << 1;
    }
    void autoSelectUsesTopmostEligibleVisibleContent() {
        QFETCH(QString, condition);
        QFETCH(int, expected);
        Document d;
        fixture(d);
        d.beginEdit();
        Layer top = d.state.layers[0];
        top.name = "Üst nesne";
        top.pixels.fill(Qt::blue);
        top.mask = {};
        if (condition == "transparent")
            top.pixels.setPixelColor(20, 20, Qt::transparent);
        if (condition == "masked") {
            top.mask = QImage(top.pixels.size(), QImage::Format_ARGB32_Premultiplied);
            top.mask.fill(Qt::white);
            top.mask.setPixelColor(20, 20, Qt::black);
        }
        top.visible = condition != "hidden";
        top.locked = condition == "locked";
        top.opacity = condition == "zero-opacity" ? 0 : 1;
        if (condition == "transformed")
            top.geometry = QTransform::fromTranslate(10, 0);
        d.state.layers.append(top);
        d.state.active = 0;
        d.commit("Overlapping targets");
        d.markSaved();
        const int history = d.historyLabels().size();
        const QImage before = d.composite();
        Canvas canvas(&d);
        prepare(canvas);
        QVERIFY(!canvas.autoSelectLayers());
        QVERIFY(canvas.showsLayerBounds());
        canvas.setAutoSelectLayers(true);
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, canvas.toWidget({90, 80}).toPoint());
        QCOMPARE(d.state.active, expected);
        QCOMPARE(canvas.activeLayerTargetName(), d.state.layers[expected].name);
        QCOMPARE(canvas.activeLayerSourceSize(), d.state.layers[expected].pixels.size());
        QCOMPARE(canvas.activeLayerDisplayBounds(), d.activeLayerBounds());
        QCOMPARE(d.historyLabels().size(), history);
        QCOMPARE(d.composite(), before);
        QVERIFY(!d.dirty);
    }
    void autoSelectBackgroundDoesNotDragUnrelatedLayer() {
        Document d;
        fixture(d);
        Canvas canvas(&d);
        prepare(canvas);
        canvas.setAutoSelectLayers(true);
        const Layer before = d.state.layers[0];
        const int history = d.historyLabels().size();
        drag(canvas, {15, 15}, {35, 25});
        unchanged(d.state.layers[0], before);
        QCOMPARE(d.historyLabels().size(), history);
        canvas.setAutoSelectLayers(false);
        drag(canvas, {15, 15}, {35, 25});
        QCOMPARE(d.state.layers[0].offset, before.offset + QPoint(20, 10));
        QCOMPARE(d.historyLabels().size(), history + 1);
        d.undo();
        unchanged(d.state.layers[0], before);
    }
    void autoSelectRespectsClippingAndText() {
        Document d;
        fixture(d);
        d.beginEdit();
        d.state.layers[0].pixels.fill(Qt::transparent);
        d.state.layers[0].pixels.setPixelColor(20, 20, Qt::red);
        Layer child = d.state.layers[0];
        child.name = "Clipped overlay";
        child.pixels.fill(Qt::blue);
        child.mask = {};
        child.clipped = true;
        d.state.layers.append(child);
        d.state.active = 0;
        d.commit("Clipping targets");
        Canvas canvas(&d);
        prepare(canvas);
        canvas.setAutoSelectLayers(true);
        const int history = d.historyLabels().size();
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, canvas.toWidget({90, 80}).toPoint());
        QCOMPARE(d.state.active, 1); // Revealed by the clipping base.
        d.state.active = 0;
        d.changed();
        drag(canvas, {100, 90}, {110, 95}); // Clipped away: must not move the selected base.
        QCOMPARE(d.state.active, 0);
        QCOMPARE(d.state.layers[0].offset, QPoint(70, 60));
        QCOMPARE(d.historyLabels().size(), history);
        QString error;
        QVERIFY2(d.addText("TARGET", QFont("Sans Serif", 18), Qt::white, {20, 20}, &error),
                 qPrintable(error));
        const int textIndex = d.state.active;
        const Layer text = d.state.layers[textIndex];
        QPoint hit(-1, -1);
        for (int y = 0; y < text.pixels.height() && hit.x() < 0; ++y)
            for (int x = 0; x < text.pixels.width(); ++x)
                if (qAlpha(text.pixels.pixel(x, y)) > 128) {
                    hit = text.offset + QPoint(x, y);
                    break;
                }
        QVERIFY(hit.x() >= 0);
        d.state.active = 0;
        d.changed();
        const int textHistory = d.historyLabels().size();
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, canvas.toWidget(hit).toPoint());
        QCOMPARE(d.state.active, textIndex);
        QCOMPARE(d.historyLabels().size(), textHistory);
    }
    void selectedCopyIsAtomicAndPreservesSource_data() {
        QTest::addColumn<bool>("apply");
        QTest::addColumn<bool>("soft");
        for (bool apply : {false, true})
            for (bool soft : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(apply ? "apply" : "escape")
                                            .arg(soft ? "feather" : "rectangle"))) << apply << soft;
    }
    void selectedCopyIsAtomicAndPreservesSource() {
        QFETCH(bool, apply);
        QFETCH(bool, soft);
        Document d;
        fixture(d);
        const Selection area(66, 56, 46, 36); // Part of the selection lies outside the source.
        QVERIFY(d.setSelection(soft ? area.feathered(2, d.state.size) : area));
        d.markSaved();
        const Layer before = d.state.layers[0];
        const QImage selection = d.selection.mask(d.state.size), composite = d.composite();
        const int history = d.historyLabels().size();
        const quint64 revision = d.revision();
        Canvas canvas(&d);
        prepare(canvas);
        QVERIFY(canvas.beginTransform());
        auto *scope = canvas.findChild<QLabel *>("transformScope");
        auto *copy = canvas.findChild<QPushButton *>("transformCopySelection");
        QVERIFY(scope && copy);
        QVERIFY(scope->isVisible() && scope->text().contains(before.name));
        QVERIFY(copy->isVisible() && copy->isEnabled());
        QTest::mouseClick(copy, Qt::LeftButton);
        QVERIFY(canvas.isTransforming());
        QTest::keyClick(&canvas, Qt::Key_Right);
        QTest::keyClick(&canvas, Qt::Key_Down);
        QCOMPARE(d.state.layers.size(), 1);
        QCOMPARE(d.state.active, 0);
        QCOMPARE(d.revision(), revision);
        QCOMPARE(d.historyLabels().size(), history);
        QCOMPARE(d.composite(), composite);
        unchanged(d.state.layers[0], before);
        if (!apply) {
            QTest::keyClick(&canvas, Qt::Key_Escape);
            QVERIFY(!canvas.isTransforming());
            QCOMPARE(d.state.layers.size(), 1);
            QCOMPARE(d.revision(), revision);
            QCOMPARE(d.historyLabels().size(), history);
            QCOMPARE(d.selection.mask(d.state.size), selection);
            QCOMPARE(d.composite(), composite);
            QVERIFY(!d.dirty);
            unchanged(d.state.layers[0], before);
            return;
        }
        QTest::keyClick(&canvas, Qt::Key_Return);
        QVERIFY(!canvas.isTransforming());
        QCOMPARE(d.state.layers.size(), 2);
        QCOMPARE(d.state.active, 1);
        QCOMPARE(d.historyLabels().size(), history + 1);
        unchanged(d.state.layers[0], before);
        QCOMPARE(d.selection.mask(d.state.size), selection);
        const Layer &result = d.state.layers[1];
        const Layer raster = before.rasterized();
        const QRect region = d.selection.bounds().intersected(QRect(raster.offset, raster.pixels.size()));
        QImage expected = raster.pixels.copy(region.translated(-raster.offset));
        for (int y = 0; y < expected.height(); ++y)
            for (int x = 0; x < expected.width(); ++x)
                expected.setPixel(x, y, d.selection.mixPixel(0, expected.pixel(x, y),
                                                           region.topLeft() + QPoint(x, y)));
        QCOMPARE(result.pixels, expected);
        QCOMPARE(result.offset, region.topLeft());
        QCOMPARE(result.geometry, QTransform::fromTranslate(1, 1));
        const QImage applied = d.composite();
        d.undo();
        QCOMPARE(d.state.layers.size(), 1);
        QCOMPARE(d.state.active, 0);
        QCOMPARE(d.composite(), composite);
        QCOMPARE(d.selection.mask(d.state.size), selection);
        QVERIFY(!d.dirty);
        unchanged(d.state.layers[0], before);
        d.redo();
        QCOMPARE(d.state.layers.size(), 2);
        QCOMPARE(d.state.active, 1);
        QCOMPARE(d.composite(), applied);
    }
    void ordinaryTransformUndoKeepsTheVisibleTarget() {
        Document d;
        fixture(d);
        QVERIFY(d.addLayer("Other layer"));
        d.state.active = 0; // A target change itself does not create history.
        d.changed();
        Canvas canvas(&d);
        prepare(canvas);
        const int history = d.historyLabels().size();
        QVERIFY(canvas.beginTransform());
        QTest::keyClick(&canvas, Qt::Key_Right);
        QTest::keyClick(&canvas, Qt::Key_Down);
        QVERIFY(canvas.applyTransform());
        QCOMPARE(d.historyLabels().size(), history + 1);
        d.undo();
        QCOMPARE(d.state.active, 0);
        QCOMPARE(canvas.activeLayerTargetName(), QString("Kaynak nesne"));
        QVERIFY(canvas.beginTransform());
        QCOMPARE(canvas.activeLayerSourceSize(), QSize(80, 50));
        canvas.cancelTransform();
    }
    void clippedSelectionCopyBakesCoverageAndBecomesIndependent_data() {
        QTest::addColumn<bool>("apply");
        QTest::newRow("escape") << false;
        QTest::newRow("apply-undo") << true;
    }
    void clippedSelectionCopyBakesCoverageAndBecomesIndependent() {
        QFETCH(bool, apply);
        Document d;
        QVERIFY(d.create({240, 180}));
        d.beginEdit();
        Layer &base = d.state.layers[0];
        base.name = "Clipping base";
        base.pixels = QImage(80, 50, QImage::Format_ARGB32_Premultiplied);
        base.pixels.fill(Qt::white);
        QPainter coverage(&base.pixels);
        coverage.setCompositionMode(QPainter::CompositionMode_Source);
        coverage.fillRect(0, 0, 40, 50, QColor(255, 255, 255, 128));
        coverage.end();
        base.mask = QImage(base.pixels.size(), QImage::Format_ARGB32_Premultiplied);
        base.mask.fill(Qt::white);
        QPainter mask(&base.mask);
        mask.fillRect(20, 0, 10, 50, Qt::black);
        mask.end();
        base.offset = {30, 30};
        Layer source = base;
        source.name = "Clipped source";
        source.pixels.fill(Qt::red);
        source.mask.fill(Qt::white);
        source.mask.setPixelColor(5, 5, QColor(128, 128, 128));
        source.clipped = true;
        Layer upper = source;
        upper.name = "Other clipping child";
        upper.pixels.fill(Qt::transparent);
        upper.mask = {};
        d.state.layers.append(source);
        d.state.layers.append(upper);
        d.state.active = 1;
        d.commit("Clipped selection fixture");
        QVERIFY(d.setSelection(Selection(30, 30, 80, 50)));
        d.markSaved();
        const QVector<Layer> original = d.state.layers;
        const QImage before = d.composite();
        const QImage selection = d.selection.mask(d.state.size);
        const quint64 revision = d.revision();
        const int history = d.historyLabels().size();
        Canvas canvas(&d);
        prepare(canvas);
        QVERIFY(canvas.copySelectionAndTransform());
        for (int i = 0; i < 10; ++i)
            QTest::keyClick(&canvas, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(d.state.layers.size(), 3);
        QCOMPARE(d.state.active, 1);
        QCOMPARE(d.composite(), before);
        QCOMPARE(d.revision(), revision);
        if (!apply) {
            QTest::keyClick(&canvas, Qt::Key_Escape);
            QCOMPARE(d.state.layers.size(), 3);
            QCOMPARE(d.historyLabels().size(), history);
            QCOMPARE(d.state.active, 1);
            QCOMPARE(d.selection.mask(d.state.size), selection);
            for (int i = 0; i < original.size(); ++i)
                unchanged(d.state.layers[i], original[i]);
            return;
        }
        QVERIFY(canvas.applyTransform());
        QCOMPARE(d.state.layers.size(), 4);
        QCOMPARE(d.state.active, 3); // Above the complete original clipping stack.
        QCOMPARE(d.historyLabels().size(), history + 1);
        const Layer copied = d.state.layers[3];
        QVERIFY(!copied.clipped);
        for (int i = 0; i < original.size(); ++i)
            unchanged(d.state.layers[i], original[i]);
        QCOMPARE(qAlpha(copied.pixels.pixel(25, 15)), 0); // Base mask stays baked after moving away.
        const int partialAlpha = qAlpha(copied.pixels.pixel(5, 5));
        QVERIFY(partialAlpha >= 63 && partialAlpha <= 65); // Source mask × half-alpha base.
        const QImage after = d.composite();
        QCOMPARE(after.pixelColor(175, 45), QColor(Qt::red)); // Moved outside the base remains visible.
        QCOMPARE(qAlpha(after.pixel(155, 45)), 0); // Hidden source pixels never become visible.
        QCOMPARE(qAlpha(after.pixel(135, 35)), partialAlpha);
        QCOMPARE(d.selection.mask(d.state.size), selection);
        d.undo();
        QCOMPARE(d.state.layers.size(), 3);
        QCOMPARE(d.state.active, 1);
        QCOMPARE(d.composite(), before);
        QVERIFY(!d.dirty);
        for (int i = 0; i < original.size(); ++i)
            unchanged(d.state.layers[i], original[i]);
        d.redo();
        QCOMPARE(d.state.active, 3);
        QCOMPARE(d.composite(), after);
    }
    void moveOptionsExposeAppDefaultsAndAllowFixedTargetMode() {
        MainWindow window(false, false);
        auto *d = new Document;
        fixture(*d);
        window.addDocument(d);
        window.resize(900, 620);
        window.show();
        QTest::qWait(30);
        auto *canvas = window.currentCanvas();
        auto *autoSelect = window.findChild<QCheckBox *>("moveAutoSelect");
        auto *bounds = window.findChild<QCheckBox *>("moveShowBounds");
        QVERIFY(autoSelect && bounds);
        QVERIFY(canvas->autoSelectLayers());
        QVERIFY(canvas->showsLayerBounds());
        QVERIFY(autoSelect->isChecked() && bounds->isChecked());
        QVERIFY(autoSelect->isVisible() && bounds->isVisible());
        for (auto *control : {autoSelect, bounds}) {
            QVERIFY(window.rect().contains(QRect(control->mapTo(&window, QPoint()), control->size())));
            QVERIFY(!control->visibleRegion().isEmpty());
        }
        screenshot(window, "move-options-900x620");
        QTest::mouseClick(autoSelect, Qt::LeftButton);
        QVERIFY(!canvas->autoSelectLayers());
        QTest::mouseClick(bounds, Qt::LeftButton);
        QVERIFY(!canvas->showsLayerBounds());
        QVERIFY(!autoSelect->toolTip().isEmpty());
        QVERIFY(!bounds->toolTip().isEmpty());
    }
    void numericScaleRatioIsExplicitAndReversible() {
        Document d;
        fixture(d);
        d.beginEdit();
        TransformDialog dialog(&d);
        dialog.show();
        dialog.activateWindow();
        QTest::qWait(10);
        auto *ratio = dialog.findChild<QCheckBox *>("transformKeepRatio");
        auto *x = dialog.findChild<QDoubleSpinBox *>("scaleX");
        auto *y = dialog.findChild<QDoubleSpinBox *>("scaleY");
        QVERIFY(ratio && x && y);
        QVERIFY(dialog.keepAspectRatio());
        QVERIFY(ratio->isChecked());
        ratio->setFocus();
        QTRY_VERIFY(ratio->hasFocus());
        QTest::keyClick(ratio, Qt::Key_Space);
        QVERIFY(!dialog.keepAspectRatio());
        QTest::keyClick(ratio, Qt::Key_Space);
        QVERIFY(dialog.keepAspectRatio());
        QTest::keyClick(ratio, Qt::Key_Tab);
        QVERIFY(x->hasFocus() || x->isAncestorOf(QApplication::focusWidget()));
        x->setValue(150);
        QCOMPARE(y->value(), 150.0);
        QCOMPARE(dialog.transform().m11(), 1.5);
        QCOMPARE(dialog.transform().m22(), 1.5);
        auto *metrics = dialog.findChild<QLabel *>("transformMetrics");
        auto *anchor = dialog.findChild<QComboBox *>("transformAnchor");
        QVERIFY(metrics && anchor);
        QVERIFY(metrics->text().contains("80 × 50"));
        // Center scaling places the top/bottom at half pixels. The displayed
        // outward-rounded AABB is 76 high, while the local edge remains 75.
        QVERIFY(metrics->text().contains("120 × 76"));
        QCOMPARE(anchor->currentIndex(), 4);
        anchor->setCurrentIndex(0);
        QCOMPARE(dialog.transform().map(QPointF()), QPointF());
        QVERIFY(metrics->text().contains("120 × 75"));
        anchor->setCurrentIndex(4);
        y->setValue(125);
        QCOMPARE(x->value(), 125.0);
        dialog.setKeepAspectRatio(false);
        QVERIFY(!ratio->isChecked());
        x->setValue(160);
        QCOMPARE(y->value(), 125.0);
        dialog.setKeepAspectRatio(true);
        x->setValue(175);
        QCOMPARE(y->value(), 175.0);
        dialog.reject();
        d.cancelEdit();
        QVERIFY(d.state.layers[0].geometry.isIdentity());
    }
    void numericLabelsAreReadableOnActualScrollSurfaces_data() {
        QTest::addColumn<QString>("theme");
        QTest::newRow("focus") << QString("focus");
        QTest::newRow("paper") << QString("paper");
        QTest::newRow("custom") << QString("custom");
    }
    void numericLabelsAreReadableOnActualScrollSurfaces() {
        QFETCH(QString, theme);
        Document d;
        fixture(d);
        UiSettings settings = UiSettings::forPreset(theme == "custom" ? "focus" : theme);
        if (theme == "custom") {
            settings.background = QColor("#eee9de");
            settings.panel = QColor("#343d58");
            settings.canvas = QColor("#171e2d");
            settings.accent = QColor("#ffcc66");
        }
        QWidget parent;
        parent.setPalette(settings.palette());
        parent.setStyleSheet(settings.styleSheet());
        d.beginEdit();
        TransformDialog dialog(&d, &parent);
        dialog.show();
        auto *tabs = dialog.findChild<QTabWidget *>("transformTabs");
        QVERIFY(tabs);
        for (int tab = 0; tab < tabs->count(); ++tab) {
            tabs->setCurrentIndex(tab);
            auto *scroll = qobject_cast<QScrollArea *>(tabs->widget(tab));
            QVERIFY(scroll);
            auto *form = qobject_cast<QFormLayout *>(scroll->widget()->layout());
            QVERIFY(form);
            for (auto *field : scroll->widget()->findChildren<QDoubleSpinBox *>()) {
                auto *label = qobject_cast<QLabel *>(form->labelForField(field));
                QVERIFY(label);
                QCOMPARE(label->buddy(), field);
                QVERIFY(!field->accessibleName().isEmpty());
                readableLabel(dialog, label, scroll);
            }
            scroll->verticalScrollBar()->setValue(0);
            QTest::qWait(10);
            screenshot(dialog, "numeric-" + theme + (tab ? "-perspective" : "-affine"));
        }
        dialog.reject();
        d.cancelEdit();
    }
    void quickTransformSharesAvailabilityAndReason() {
        MainWindow window(false, false);
        window.show();
        auto *command = window.findChild<QAction *>("transform");
        auto *quick = window.findChild<QToolButton *>("quickTransform");
        QVERIFY(command && quick);
        QCOMPARE(quick->defaultAction(), command);
        QVERIFY(!command->isEnabled() && !quick->isEnabled());
        QVERIFY(!quick->toolTip().isEmpty());
        auto *d = new Document;
        fixture(*d);
        window.addDocument(d);
        QTRY_VERIFY(command->isEnabled() && quick->isEnabled());
        for (bool hidden : {false, true}) {
            d->state.layers[0].locked = !hidden;
            d->state.layers[0].visible = !hidden;
            d->changed();
            QTRY_VERIFY(!command->isEnabled() && !quick->isEnabled());
            QCOMPARE(quick->toolTip(), command->toolTip());
            QVERIFY(quick->toolTip().contains(hidden ? "görün" : "kilid", Qt::CaseInsensitive));
            command->trigger();
            QVERIFY(!window.currentCanvas()->isTransforming());
        }
        d->state.layers[0].locked = false;
        d->state.layers[0].visible = true;
        d->changed();
        QTRY_VERIFY(quick->isEnabled());
    }
    void imageLayerShortcutFitsEveryInspector_data() {
        QTest::addColumn<QString>("preset");
        for (const auto &entry : uiPresets())
            QTest::newRow(qPrintable(entry.id)) << entry.id;
    }
    void imageLayerShortcutFitsEveryInspector() {
        QFETCH(QString, preset);
        MainWindow window(false, false);
        auto *d = new Document;
        fixture(*d);
        window.addDocument(d);
        window.applyUiPreset(preset, false);
        window.resize(900, 620);
        window.show();
        QTest::qWait(30);
        QCOMPARE(window.size(), QSize(900, 620));
        auto *command = window.findChild<QAction *>("import");
        auto *button = window.findChild<QToolButton *>("addImageLayer");
        QVERIFY(command && button);
        QCOMPARE(button->defaultAction(), command);
        const QString fullLabel = "Görüntü katmanı ekle…";
        QCOMPARE(command->iconText(), fullLabel);
        for (bool enabled : {false, true}) {
            command->setEnabled(enabled); // QAction updates must retain the narrow label.
            QCOMPARE(button->isEnabled(), enabled);
            QCOMPARE(button->text().simplified(), fullLabel);
            QCOMPARE(button->accessibleName(), fullLabel);
            QCOMPARE(command->iconText(), fullLabel);
        }
        if (preset == "touch") {
            QVERIFY(button->isHidden()); // This preset exposes the same action beside Open.
            return;
        }
        QScrollArea *scroll = nullptr;
        for (auto *parent = button->parentWidget(); parent && !scroll; parent = parent->parentWidget())
            scroll = qobject_cast<QScrollArea *>(parent);
        QVERIFY(scroll);
        QTRY_VERIFY2(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width(),
                     qPrintable(QString("%1 inspector minimum=%2 viewport=%3 import=%4")
                                    .arg(preset).arg(scroll->widget()->minimumSizeHint().width())
                                    .arg(scroll->viewport()->width())
                                    .arg(button->minimumSizeHint().width())));
        scroll->ensureWidgetVisible(button, 0, 0);
        QTRY_VERIFY(button->visibleRegion().contains(button->rect()));
        QVERIFY(button->width() >= button->minimumSizeHint().width());
        QVERIFY(button->height() >= button->minimumSizeHint().height());
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(!d->dirty);
    }
    void transformedPaintRecoveryCancelAndBlankLayerPreserveSource() {
        MainWindow window(false, false);
        auto *d = new Document;
        fixture(*d);
        d->beginEdit();
        QVERIFY(d->setLayerTransform(QTransform::fromScale(1.5, 1.5)));
        d->commit("Scale source");
        d->markSaved();
        window.addDocument(d);
        window.show();
        auto *canvas = window.currentCanvas();
        auto *brush = window.findChild<QAction *>("tool" + QString::number(Canvas::Brush));
        QVERIFY(brush);
        brush->trigger();
        auto *banner = window.findChild<QFrame *>("paintBlockedBanner");
        auto *reason = window.findChild<QLabel *>("paintBlockedReason");
        auto *rasterize = window.findChild<QAction *>("rasterize");
        auto *blank = window.findChild<QAction *>("newPaintLayer");
        QVERIFY(banner && reason && rasterize && blank);
        QTRY_VERIFY(banner->isVisible());
        QVERIFY(!reason->text().isEmpty());
        const Layer before = d->state.layers[0];
        const QImage composite = d->composite();
        const int history = d->historyLabels().size();
        const quint64 revision = d->revision();
        bool canceled = false;
        QVERIFY(drive(rasterize, [&](QDialog *dialog) {
            auto *warning = qobject_cast<QMessageBox *>(dialog);
            if (warning && warning->objectName() == "rasterizeLayerDialog") {
                canceled = true;
                warning->button(QMessageBox::Cancel)->click();
            } else dialog->reject();
        }));
        QVERIFY(canceled);
        unchanged(d->state.layers[0], before);
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->historyLabels().size(), history);
        QCOMPARE(d->composite(), composite);
        QTRY_VERIFY(banner->isVisible());
        blank->trigger();
        QCOMPARE(d->state.layers.size(), 2);
        QCOMPARE(d->state.active, 1);
        QCOMPARE(d->historyLabels().size(), history + 1);
        unchanged(d->state.layers[0], before);
        QVERIFY(d->state.layers[1].geometry.isIdentity());
        QCOMPARE(d->composite(), composite);
        QTRY_VERIFY(!banner->isVisible());
        canvas->foreground = Qt::green;
        const QImage blankPixels = d->state.layers[1].pixels;
        QTest::mouseClick(canvas, Qt::LeftButton, {}, canvas->toWidget({100, 90}).toPoint());
        QVERIFY(d->state.layers[1].pixels != blankPixels);
        unchanged(d->state.layers[0], before);
        d->undo();
        d->undo();
        QCOMPARE(d->state.layers.size(), 1);
        unchanged(d->state.layers[0], before);
        QCOMPARE(d->composite(), composite);
    }
    void rasterizeConfirmationPreservesCanceledPreviewAndCanUndoAcceptance() {
        MainWindow window(false, false);
        auto *d = new Document;
        fixture(*d);
        d->beginEdit();
        QVERIFY(d->setLayerTransform(QTransform::fromScale(1.5, 1.5)));
        d->commit("Scaled source");
        d->markSaved();
        window.addDocument(d);
        window.show();
        auto *canvas = window.currentCanvas();
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        const QTransform preview = canvas->transformGeometry();
        const Layer before = d->state.layers[0];
        const QImage appearance = d->composite();
        const quint64 revision = d->revision();
        const int history = d->historyLabels().size();
        auto *rasterize = window.findChild<QAction *>("rasterize");
        QVERIFY(rasterize);
        QVERIFY(drive(rasterize, [](QDialog *dialog) {
            if (auto *warning = qobject_cast<QMessageBox *>(dialog))
                warning->button(QMessageBox::Cancel)->click();
            else dialog->reject();
        }));
        QVERIFY(canvas->isTransforming());
        QCOMPARE(canvas->transformGeometry(), preview);
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->historyLabels().size(), history);
        unchanged(d->state.layers[0], before);
        canvas->cancelTransform();
        bool accepted = false;
        QVERIFY(drive(rasterize, [&](QDialog *dialog) {
            auto *warning = qobject_cast<QMessageBox *>(dialog);
            if (warning && warning->objectName() == "rasterizeLayerDialog") {
                accepted = true;
                warning->button(QMessageBox::Yes)->click();
            } else dialog->reject();
        }));
        QVERIFY(accepted);
        QVERIFY(d->state.layers[0].geometry.isIdentity());
        QVERIFY(d->state.layers[0].mask.isNull());
        QCOMPARE(d->composite(), appearance);
        QCOMPARE(d->historyLabels().size(), history + 1);
        d->undo();
        unchanged(d->state.layers[0], before);
        QCOMPARE(d->composite(), appearance);
        QVERIFY(!d->dirty);
    }
    void independentLayerInsertionsPreserveClippingChildren_data() {
        QTest::addColumn<bool>("asImport");
        QTest::addColumn<int>("active");
        QTest::newRow("base-blank-paint") << false << 0;
        QTest::newRow("base-import-image") << true << 0;
        QTest::newRow("middle-child-blank-paint") << false << 1;
        QTest::newRow("middle-child-import-image") << true << 1;
    }
    void independentLayerInsertionsPreserveClippingChildren() {
        QFETCH(bool, asImport);
        QFETCH(int, active);
        MainWindow window(false, false);
        auto *d = new Document;
        QVERIFY(d->create({80, 60}));
        d->beginEdit();
        auto &base = d->state.layers[0];
        base.name = "Original clipping base";
        base.pixels.fill(Qt::red);
        Layer child = base;
        child.name = "Original clipped overlay";
        child.pixels.fill(Qt::blue);
        child.clipped = true;
        d->state.layers.append(child);
        Layer upper = child;
        upper.name = "Later clipped sibling";
        upper.pixels.fill(QColor(0, 255, 0, 128));
        d->state.layers.append(upper);
        d->state.active = active;
        d->commit("Paint recovery clipping fixture");
        d->markSaved();
        const QVector<Layer> original = d->state.layers;
        const QImage before = d->composite();
        const int history = d->historyLabels().size();
        window.addDocument(d);
        window.show();
        QImage expected = before;
        if (asImport) {
            QTemporaryDir directory;
            QVERIFY(directory.isValid());
            QImage image(7, 5, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            image.setPixelColor(1, 1, Qt::yellow);
            const QString path = directory.filePath("independent.png");
            QVERIFY(image.save(path));
            auto *import = window.findChild<QAction *>("import");
            QVERIFY(import && import->isEnabled());
            QVERIFY(drive(import, [&](QDialog *dialog) {
                if (auto *picker = qobject_cast<QFileDialog *>(dialog))
                    choose(picker, path);
                else dialog->reject();
            }));
            QPainter painter(&expected);
            painter.drawImage(QPoint(), image);
        } else {
            auto *blank = window.findChild<QAction *>("newPaintLayer");
            QVERIFY(blank && blank->isEnabled());
            blank->trigger();
        }
        QCOMPARE(d->state.layers.size(), 4);
        QCOMPARE(d->state.active, 3);
        QVERIFY(!d->state.layers[3].clipped);
        QCOMPARE(d->historyLabels().size(), history + 1);
        QCOMPARE(d->composite(), expected);
        for (int i = 0; i < original.size(); ++i)
            unchanged(d->state.layers[i], original[i]);
        d->undo();
        QCOMPARE(d->state.layers.size(), 3);
        QCOMPARE(d->state.active, active);
        QCOMPARE(d->composite(), before);
        for (int i = 0; i < original.size(); ++i)
            unchanged(d->state.layers[i], original[i]);
        QVERIFY(!d->dirty);
    }
    void scopeAndActionsRemainVisibleAt900By620() {
        MainWindow window(false, false);
        auto *d = new Document;
        fixture(*d);
        QVERIFY(d->setSelection(Selection(75, 65, 30, 20)));
        window.addDocument(d);
        window.resize(900, 620);
        window.show();
        QTest::qWait(30);
        QCOMPARE(window.size(), QSize(900, 620));
        auto *canvas = window.currentCanvas();
        canvas->fit();
        QVERIFY(canvas->beginTransform());
        auto *scope = canvas->findChild<QLabel *>("transformScope");
        auto *metrics = canvas->findChild<QLabel *>("transformMetrics");
        auto *copy = canvas->findChild<QPushButton *>("transformCopySelection");
        QVERIFY(scope && metrics && copy);
        for (QWidget *widget : {static_cast<QWidget *>(scope), static_cast<QWidget *>(metrics),
                                static_cast<QWidget *>(copy)}) {
            QVERIFY(widget->isVisible());
            QVERIFY(canvas->rect().contains(QRect(widget->mapTo(canvas, QPoint()), widget->size())));
        }
        QVERIFY(scope->text().contains(d->state.layers[0].name));
        QVERIFY(scope->text().contains("katman", Qt::CaseInsensitive));
        unobstructedContextWhenPossible(*canvas);
        const QString before = metrics->text();
        QTest::keyClick(canvas, Qt::Key_Right);
        QVERIFY(metrics->text() != before);
        unobstructedContextWhenPossible(*canvas);
        for (const char *id : {"transformNumeric", "transformCancel", "transformApply"}) {
            auto *button = canvas->findChild<QToolButton *>(id);
            QVERIFY(button && button->isVisible());
            QVERIFY(!button->accessibleName().isEmpty());
            QVERIFY(!button->toolTip().isEmpty());
            QVERIFY(canvas->rect().contains(QRect(button->mapTo(canvas, QPoint()), button->size())));
        }
        screenshot(window, "selection-scope-900x620");
        canvas->cancelTransform();
    }
    void imageLayerImport_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<int>("start"); // No document, blank canvas, populated canvas.
        for (const QString &format : {QString("PNG"), QString("JPEG")})
            for (int start = 0; start < 3; ++start)
                QTest::newRow(qPrintable(format + "-" + QString::number(start))) << format << start;
    }
    void imageLayerImport() {
        QFETCH(QString, format);
        QFETCH(int, start);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage source(31, 23, QImage::Format_RGB32);
        source.fill(QColor("#397fbd"));
        QPainter painter(&source);
        painter.fillRect(10, 5, 12, 11, QColor("#e7ac61"));
        painter.end();
        const QString path = directory.filePath(QString("object.") + (format == "PNG" ? "png" : "jpg"));
        QVERIFY(source.save(path, format.toLatin1().constData()));
        const QImage decoded = QImage(path).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QVERIFY(!decoded.isNull());
        MainWindow window(false, false);
        Document *existing = nullptr;
        Layer before;
        int history = 0;
        if (start) {
            existing = new Document;
            if (start == 2)
                fixture(*existing);
            else
                QVERIFY(existing->create({240, 180}));
            before = existing->state.layers[0];
            history = existing->historyLabels().size();
            window.addDocument(existing);
        }
        window.show();
        auto *import = window.findChild<QAction *>("import");
        QVERIFY(import && import->isEnabled());
        auto *addImage = window.findChild<QToolButton *>("addImageLayer");
        QVERIFY(addImage);
        QCOMPARE(addImage->defaultAction(), import);
        bool pickerSeen = false;
        QVERIFY(drive(import, [&](QDialog *dialog) {
            if (auto *picker = qobject_cast<QFileDialog *>(dialog)) {
                pickerSeen = true;
                choose(picker, path);
            } else dialog->reject();
        }));
        QVERIFY(pickerSeen);
        auto *tabs = window.findChild<QTabWidget *>("documents");
        QCOMPARE(tabs->count(), 1);
        auto *d = window.currentDocument();
        QVERIFY(d);
        if (start) {
            QCOMPARE(d, existing);
            QCOMPARE(d->state.layers.size(), 2);
            QCOMPARE(d->state.active, 1);
            QCOMPARE(d->historyLabels().size(), history + 1);
            unchanged(d->state.layers[0], before);
        } else {
            QCOMPARE(d->state.size, source.size());
            QCOMPARE(d->state.layers.size(), 1);
            QCOMPARE(d->state.active, 0);
        }
        QCOMPARE(d->state.layers[d->state.active].pixels, decoded);
        auto *help = window.findChild<QFrame *>("layerImportHelp");
        auto *dismiss = window.findChild<QToolButton *>("dismissLayerImportHelp");
        QVERIFY(help && dismiss);
        QTRY_VERIFY(help->isVisible());
        QTest::mouseClick(dismiss, Qt::LeftButton);
        QTRY_VERIFY(!help->isVisible());
        QVERIFY(drive(import, [&](QDialog *dialog) {
            if (auto *picker = qobject_cast<QFileDialog *>(dialog))
                choose(picker, path);
            else dialog->reject();
        }));
        QCOMPARE(tabs->count(), 1);
        QTRY_VERIFY(!help->isVisible()); // The mini-help does not reappear after dismissal.
    }
    void importCancelPreservesDocumentAndLiveTransform_data() {
        QTest::addColumn<int>("start");
        QTest::newRow("empty-app") << 0;
        QTest::newRow("blank-document") << 1;
        QTest::newRow("live-transform") << 2;
    }
    void importCancelPreservesDocumentAndLiveTransform() {
        QFETCH(int, start);
        MainWindow window(false, false);
        Document *d = nullptr;
        quint64 revision = 0;
        QStringList history;
        QImage composite;
        QTransform preview;
        if (start) {
            d = new Document;
            if (start == 2)
                fixture(*d);
            else
                QVERIFY(d->create({240, 180}));
            window.addDocument(d);
        }
        window.show();
        if (start == 2) {
            QVERIFY(window.currentCanvas()->beginTransform());
            QTest::keyClick(window.currentCanvas(), Qt::Key_Right);
            preview = window.currentCanvas()->transformGeometry();
        }
        if (d) {
            revision = d->revision();
            history = d->historyLabels();
            composite = d->composite();
        }
        auto *import = window.findChild<QAction *>("import");
        QVERIFY(import && import->isEnabled());
        bool canceled = false;
        QVERIFY(drive(import, [&](QDialog *dialog) {
            canceled = bool(qobject_cast<QFileDialog *>(dialog));
            dialog->reject();
        }));
        QVERIFY(canceled);
        QCOMPARE(window.currentDocument(), d);
        QCOMPARE(window.findChild<QTabWidget *>("documents")->count(), start ? 1 : 0);
        if (d) {
            QCOMPARE(d->revision(), revision);
            QCOMPARE(d->historyLabels(), history);
            QCOMPARE(d->composite(), composite);
            QCOMPARE(d->state.layers.size(), 1);
        }
        if (start == 2) {
            QVERIFY(window.currentCanvas()->isTransforming());
            QCOMPARE(window.currentCanvas()->transformGeometry(), preview);
            window.currentCanvas()->cancelTransform();
        }
    }
};
QTEST_MAIN(UsabilityTests)
#include "test_usability.moc"
