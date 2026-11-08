#include "mainwindow.h"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <limits>
#include <memory>
#include <algorithm>

namespace {
void fixture(Document &d, int transform = 0, QSize source = {11, 7}) {
    QVERIFY(d.create({61, 43}));
    d.beginEdit();
    auto &layer = d.state.layers[0];
    layer.pixels = QImage(source, QImage::Format_ARGB32_Premultiplied);
    layer.pixels.fill(Qt::transparent);
    QPainter painter(&layer.pixels);
    painter.fillRect(3, 2, 2, 1, Qt::red);
    painter.end();
    layer.mask = QImage(source, QImage::Format_ARGB32_Premultiplied);
    layer.mask.fill(Qt::white);
    layer.mask.setPixelColor(3, 2, Qt::black);
    layer.offset = {13, 9};
    if (transform == 1) {
        layer.geometry.translate(-3.25, 4.5);
        layer.geometry.rotate(27);
        layer.geometry.scale(1.25, .8);
    } else if (transform == 2) {
        QVERIFY(QTransform::quadToQuad(QPolygonF{{0, 0}, {11, 0}, {11, 7}, {0, 7}},
                                      QPolygonF{{-2.5, 1.5}, {15.5, -3.25}, {12.5, 8.5}, {-4, 11}},
                                      layer.geometry));
    } else if (transform == 3) {
        layer.geometry.translate(2.5, -3.5);
        layer.geometry.scale(-1.5, 1.25);
        layer.geometry.shear(.2, -.1);
    }
    layer.opacity = .7;
    layer.brightness = 9;
    layer.filters.noise = 5;
    d.commit("Position fixture");
    d.markSaved();
}
void sourceUnchanged(const Layer &actual, const Layer &original) {
    QCOMPARE(actual.name, original.name);
    QCOMPARE(actual.pixels, original.pixels);
    QCOMPARE(actual.mask, original.mask);
    QCOMPARE(actual.geometry, original.geometry);
    QCOMPARE(actual.text, original.text);
    QCOMPARE(actual.font, original.font);
    QCOMPARE(actual.color, original.color);
    QCOMPARE(actual.opacity, original.opacity);
    QCOMPARE(actual.visible, original.visible);
    QCOMPARE(actual.locked, original.locked);
    QCOMPARE(actual.clipped, original.clipped);
    QCOMPARE(actual.blend, original.blend);
    QCOMPARE(actual.brightness, original.brightness);
    QCOMPARE(actual.contrast, original.contrast);
    QCOMPARE(actual.saturation, original.saturation);
    QCOMPARE(actual.blur, original.blur);
    QVERIFY(actual.tones == original.tones);
    QVERIFY(actual.filters == original.filters);
    QVERIFY(actual.maskSettings == original.maskSettings);
}
bool dialog(QAction *action, const std::function<void(QDialog *)> &handle) {
    bool seen = false;
    QTimer driver, timeout;
    driver.setInterval(10);
    timeout.setSingleShot(true);
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!modal || seen)
            return;
        seen = true;
        handle(modal);
    });
    QObject::connect(&timeout, &QTimer::timeout, [&] {
        if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            modal->reject();
    });
    driver.start();
    timeout.start(5000);
    action->trigger();
    return seen;
}
QLineEdit *nameEditor(MainWindow &window) {
    for (auto *editor : window.findChildren<QLineEdit *>("layerNameEditor"))
        if (editor->isVisible())
            return editor;
    return nullptr;
}
void key(QWidget *target, QEvent::Type type, int code,
         Qt::KeyboardModifiers modifiers = {}, bool repeat = false) {
    QKeyEvent event(type, code, modifiers, {}, repeat);
    QApplication::sendEvent(target, &event);
}
void mouse(Canvas *canvas, QEvent::Type type, QPointF point) {
    const QPointF local = canvas->toWidget(point);
    QMouseEvent event(type, local, canvas->mapToGlobal(local.toPoint()),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &event);
}
} // namespace

class PositioningTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName("PhotoDeckPositioningTests");
        QApplication::setOrganizationName("PhotoDeckTests");
    }
    void sixCommandsUseGeometricBounds_data() {
        QTest::addColumn<int>("direction");
        QTest::addColumn<int>("target");
        QTest::addColumn<int>("transform");
        for (int transform = 0; transform < 4; ++transform)
            for (int target = 0; target < 2; ++target)
                for (int direction = 0; direction < 6; ++direction)
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(transform).arg(target).arg(direction)))
                        << direction << target << transform;
    }
    void sixCommandsUseGeometricBounds() {
        QFETCH(int, direction);
        QFETCH(int, target);
        QFETCH(int, transform);
        Document d;
        fixture(d, transform);
        QVERIFY(d.setSelection(Selection(10, 8, 29, 19).feathered(2, d.state.size)));
        const QImage selection = d.selection.mask(d.state.size);
        const Layer original = d.state.layers[0];
        const QRect before = d.activeLayerBounds();
        const QRect geometric = original.geometry.mapRect(
            QRectF(QPointF(), QSizeF(original.pixels.size()))).toAlignedRect().translated(original.offset);
        QCOMPARE(before, geometric); // Sparse alpha and the black mask do not trim these bounds.
        const QRect destination = target ? d.selection.bounds() : QRect(QPoint(), d.state.size);
        QPoint expected = before.topLeft();
        switch (direction) {
        case 0: expected.setX(destination.x()); break;
        case 1: expected.setX(destination.x() + int(std::floor((destination.width() - before.width()) / 2.0))); break;
        case 2: expected.setX(destination.x() + destination.width() - before.width()); break;
        case 3: expected.setY(destination.y()); break;
        case 4: expected.setY(destination.y() + int(std::floor((destination.height() - before.height()) / 2.0))); break;
        case 5: expected.setY(destination.y() + destination.height() - before.height()); break;
        }
        const int history = d.historyLabels().size();
        QString error;
        QVERIFY2(d.alignLayer(Document::LayerAlignment(direction), Document::AlignmentTarget(target), &error),
                 qPrintable(error));
        QCOMPARE(d.activeLayerBounds(), QRect(expected, before.size()));
        QCOMPARE(d.historyLabels().size(), history + (expected != before.topLeft()));
        sourceUnchanged(d.state.layers[0], original);
        QCOMPARE(d.state.layers[0].pixels.cacheKey(), original.pixels.cacheKey());
        QCOMPARE(d.state.layers[0].mask.cacheKey(), original.mask.cacheKey());
        QCOMPARE(d.selection.mask(d.state.size), selection);
        const quint64 revision = d.revision();
        QVERIFY(d.alignLayer(Document::LayerAlignment(direction), Document::AlignmentTarget(target)));
        QCOMPARE(d.revision(), revision);
        if (expected != before.topLeft()) {
            QVERIFY(d.dirty);
            d.undo();
            QCOMPARE(d.state.layers[0].offset, original.offset);
            QCOMPARE(d.selection.mask(d.state.size), selection);
            QVERIFY(!d.dirty);
            d.redo();
            QCOMPARE(d.activeLayerBounds().topLeft(), expected);
            QCOMPARE(d.selection.mask(d.state.size), selection);
        }
    }
    void oddCenteringAndOversizedLayers() {
        Document d;
        fixture(d, 0, {12, 8});
        QVERIFY(d.alignLayer(Document::LayerAlignment::Center, Document::AlignmentTarget::Canvas));
        QVERIFY(d.alignLayer(Document::LayerAlignment::Middle, Document::AlignmentTarget::Canvas));
        QCOMPARE(d.activeLayerBounds().topLeft(), QPoint(24, 17));
        fixture(d, 0, {66, 48});
        QVERIFY(d.alignLayer(Document::LayerAlignment::Center, Document::AlignmentTarget::Canvas));
        QVERIFY(d.alignLayer(Document::LayerAlignment::Middle, Document::AlignmentTarget::Canvas));
        QCOMPARE(d.activeLayerBounds().topLeft(), QPoint(-3, -3));
        QVERIFY(d.alignLayer(Document::LayerAlignment::Right, Document::AlignmentTarget::Canvas));
        QVERIFY(d.alignLayer(Document::LayerAlignment::Bottom, Document::AlignmentTarget::Canvas));
        QCOMPARE(d.activeLayerBounds().topLeft(), QPoint(-5, -5));
    }
    void numericPositionKeepsTextMaskAndGeometry() {
        Document d;
        QVERIFY(d.create({200, 120}));
        QString error;
        QVERIFY(d.addText("Editable text", QFont("Sans Serif", 14), Qt::cyan, {12, 11}, &error));
        QVERIFY(d.addMask(&error));
        d.beginEdit();
        QTransform geometry;
        geometry.translate(-4.25, 5.5);
        geometry.rotate(-17);
        QVERIFY(d.setLayerTransform(geometry, &error));
        d.commit("Text transform");
        d.markSaved();
        const Layer original = d.state.layers[d.state.active];
        const quint64 before = d.revision();
        const int history = d.historyLabels().size();
        QVERIFY(d.positionLayer({-29, 37}, &error));
        QCOMPARE(d.activeLayerBounds().topLeft(), QPoint(-29, 37));
        QCOMPARE(d.historyLabels().size(), history + 1);
        sourceUnchanged(d.state.layers[d.state.active], original);
        QVERIFY(!d.state.layers[d.state.active].text.isEmpty());
        d.undo();
        QCOMPARE(d.revision(), before);
        QVERIFY(!d.dirty && d.canRedo());
        QVERIFY(d.positionLayer(d.activeLayerBounds().topLeft(), &error));
        QCOMPARE(d.revision(), before);
        QVERIFY(d.canRedo());
        d.redo();
        QCOMPARE(d.activeLayerBounds().topLeft(), QPoint(-29, 37));
    }
    void rejectedOperationsAreAtomic() {
        Document d;
        fixture(d, 1);
        auto reject = [&](const std::function<bool(QString *)> &operation) {
            const QPoint offset = d.state.layers.isEmpty() ? QPoint() : d.state.layers[0].offset;
            const quint64 revision = d.revision();
            const auto history = d.historyLabels();
            const bool dirty = d.dirty, editing = d.isEditing(), redo = d.canRedo();
            QString error;
            QVERIFY(!operation(&error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(d.revision(), revision);
            QCOMPARE(d.historyLabels(), history);
            QCOMPARE(d.dirty, dirty);
            QCOMPARE(d.isEditing(), editing);
            QCOMPARE(d.canRedo(), redo);
            if (!d.state.layers.isEmpty())
                QCOMPARE(d.state.layers[0].offset, offset);
        };
        auto move = [&](QString *error) { return d.positionLayer({1, 2}, error); };
        auto align = [&](QString *error) {
            return d.alignLayer(Document::LayerAlignment::Left, Document::AlignmentTarget::Selection, error);
        };
        reject(align); // No selection is not a canvas fallback.
        QVERIFY(d.setSelection(Selection(0, 0, 61, 43).inverted(d.state.size)));
        QVERIFY(!d.selection.isEmpty() && !d.selection.hasPixels());
        reject(align); // An active all-zero mask remains a distinct empty target.
        reject([&](QString *error) { return d.positionLayer({Document::MaxCoordinate + 1, 0}, error); });
        reject([&](QString *error) { return d.positionLayer({std::numeric_limits<int>::min(), 0}, error); });
        reject([&](QString *error) { return d.alignLayer(Document::LayerAlignment(99), Document::AlignmentTarget::Canvas, error); });
        reject([&](QString *error) { return d.alignLayer(Document::LayerAlignment::Left, Document::AlignmentTarget(99), error); });
        d.state.layers[0].locked = true;
        reject(move);
        d.state.layers[0].locked = false;
        d.state.layers[0].visible = false;
        reject(move);
        d.state.layers[0].visible = true;
        d.beginEdit();
        reject(move);
        d.cancelEdit();
        d.state.active = -1;
        reject(move);
        d.state.active = 4;
        reject(move);
        d.state.active = 0;
        d.state.layers[0].geometry = QTransform::fromScale(0, 0);
        reject(move);
        d.state.layers[0].geometry = {};
        d.state.layers[0].pixels = {};
        reject(move);
        d.state.layers.clear();
        reject(move);
    }
    void boundaryCoordinatesAndGeometryOffsetLimits() {
        Document d;
        fixture(d);
        QString error;
        QVERIFY(d.positionLayer({Document::MaxCoordinate, -Document::MaxCoordinate}, &error));
        QCOMPARE(d.activeLayerBounds().topLeft(), QPoint(Document::MaxCoordinate, -Document::MaxCoordinate));
        d.undo();
        QVERIFY(d.canRedo());
        const quint64 clean = d.revision();
        QVERIFY(!d.positionLayer({Document::MaxCoordinate + 1, 0}, &error));
        QCOMPARE(d.revision(), clean);
        QVERIFY(d.canRedo());
        d.beginEdit();
        QVERIFY(d.setLayerTransform(QTransform::fromTranslate(-3, 4), &error));
        d.commit("Translate geometry");
        const auto revision = d.revision();
        QVERIFY(!d.positionLayer({Document::MaxCoordinate, 0}, &error)); // Offset would overflow limit.
        QCOMPARE(d.revision(), revision);
        QVERIFY(!d.positionLayer({0, -Document::MaxCoordinate}, &error));
        QCOMPARE(d.revision(), revision);
    }
    void activeEmptySelectionDoesNotPreventCanvasOrNumericPosition() {
        Document d;
        fixture(d);
        QVERIFY(d.setSelection(Selection(0, 0, 61, 43).inverted(d.state.size)));
        const QImage zero = d.selection.mask(d.state.size);
        QVERIFY(d.alignLayer(Document::LayerAlignment::Left, Document::AlignmentTarget::Canvas));
        QVERIFY(d.positionLayer({-7, -8}));
        QCOMPARE(d.selection.mask(d.state.size), zero);
        d.undo();
        QCOMPARE(d.selection.mask(d.state.size), zero);
        QVERIFY(!d.selection.isEmpty() && !d.selection.hasPixels());
    }
    void positioningKeepsSelectionContextAndOtherLayers() {
        Document d;
        fixture(d, 1);
        QString error;
        QVERIFY(d.addLayer("Clipped foreground", &error));
        QVERIFY(d.setClipping(true, &error));
        QVERIFY(d.setSelection(Selection(3, 4, 20, 18).feathered(2, d.state.size)));
        QVERIFY(d.setSelection({7, 8, 14, 11}));
        QVERIFY(d.saveSelection("Coverage", &error));
        d.markSaved();
        const Layer base = d.state.layers[0], active = d.state.layers[1];
        const QImage selected = d.selection.mask(d.state.size);
        const QImage previous = d.state.previousSelection.mask(d.state.size);
        const QImage saved = d.state.savedSelections[0].selection.mask(d.state.size);
        for (int iteration = 0; iteration < 3; ++iteration) {
            if (iteration == 0)
                QVERIFY(d.positionLayer({-9, 12}, &error));
            else if (iteration == 1)
                d.undo();
            else
                d.redo();
            QCOMPARE(d.state.active, 1);
            sourceUnchanged(d.state.layers[0], base);
            sourceUnchanged(d.state.layers[1], active);
            QCOMPARE(d.state.layers[0].offset, base.offset);
            QCOMPARE(d.selection.mask(d.state.size), selected);
            QCOMPARE(d.state.previousSelection.mask(d.state.size), previous);
            QCOMPARE(d.state.savedSelections[0].selection.mask(d.state.size), saved);
            QCOMPARE(d.savedSelectionNames(), QStringList{"Coverage"});
            QCOMPARE(d.dirty, iteration != 1);
        }
    }
    void projectVersionAndRoundTripStayCompatible() {
        Document d;
        fixture(d, 2);
        QTemporaryDir temp;
        QString error;
        QVERIFY(d.positionLayer({-15, 23}));
        QCOMPARE(d.projectSaveVersion(), 5);
        const QString path = temp.filePath("position.pdeck");
        QVERIFY(d.save(path, &error));
        Document loaded;
        QVERIFY2(loaded.load(path, &error), qPrintable(error));
        QCOMPARE(loaded.sourceVersion(), 5);
        QCOMPARE(loaded.activeLayerBounds(), d.activeLayerBounds());
        sourceUnchanged(loaded.state.layers[0], d.state.layers[0]);
        QVERIFY(d.setSelection({2, 3, 13, 11}));
        QVERIFY(d.saveSelection("Target", &error));
        QVERIFY(d.positionLayer({9, -4}));
        QCOMPARE(d.projectSaveVersion(), 6);
        QVERIFY(d.save(temp.filePath("named-position.pdeck"), &error));
        QVERIFY(loaded.load(temp.filePath("named-position.pdeck"), &error));
        QCOMPARE(loaded.sourceVersion(), 6);
        QCOMPARE(loaded.activeLayerBounds(), d.activeLayerBounds());
        QCOMPARE(loaded.savedSelectionNames(), d.savedSelectionNames());
    }
    void realNumericDialogAcceptCancelNoopAndStaleLayer() {
        MainWindow w(false, false);
        auto *d = new Document;
        fixture(*d, 1);
        w.addDocument(d);
        w.resize(900, 620);
        w.show();
        QTest::qWait(35);
        auto *action = w.findChild<QAction *>("positionLayer");
        auto *button = w.findChild<QToolButton *>("quickPosition");
        QVERIFY(action && button && button->isEnabled());
        QVERIFY(!button->accessibleName().isEmpty());
        const QPoint start = d->activeLayerBounds().topLeft();
        const quint64 revision = d->revision();
        QVERIFY(dialog(action, [&](QDialog *modal) {
            QCOMPARE(modal->objectName(), QString("layerPositionDialog"));
            auto *x = modal->findChild<QSpinBox *>("layerPositionX");
            auto *y = modal->findChild<QSpinBox *>("layerPositionY");
            QVERIFY(x && y);
            QCOMPARE(QPoint(x->value(), y->value()), start);
            QVERIFY(!x->accessibleName().isEmpty() && !y->accessibleName().isEmpty());
            x->setValue(-25);
            y->setValue(17);
            QTest::mouseClick(modal->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel), Qt::LeftButton);
        }));
        QCOMPARE(d->revision(), revision);
        QVERIFY(dialog(action, [&](QDialog *modal) {
            auto *x = modal->findChild<QSpinBox *>("layerPositionX");
            x->setFocus();
            x->selectAll();
            QTest::keyClicks(x, "-25");
            auto *y = modal->findChild<QSpinBox *>("layerPositionY");
            y->setFocus();
            y->selectAll();
            QTest::keyClicks(y, "17");
            QTest::mouseClick(modal->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
        }));
        QCOMPARE(d->activeLayerBounds().topLeft(), QPoint(-25, 17));
        const quint64 moved = d->revision();
        QVERIFY(dialog(action, [&](QDialog *modal) { modal->accept(); }));
        QCOMPARE(d->revision(), moved);
        QVERIFY(dialog(action, [&](QDialog *modal) {
            modal->findChild<QSpinBox *>("layerPositionX")->setValue(50);
            d->beginEdit();
            d->state.layers[0].name = "Changed during dialog";
            d->commit("Queued change");
            modal->accept();
        }));
        QCOMPARE(d->activeLayerBounds().topLeft(), QPoint(-25, 17));
        d->undo(); // Queued change.
        d->undo(); // One position step.
        QCOMPARE(d->activeLayerBounds().topLeft(), start);
    }
    void realMenuTargetsAvailabilityAndPendingMove() {
        MainWindow w(false, false);
        auto *d = new Document;
        fixture(*d);
        w.addDocument(d);
        w.resize(900, 620);
        w.show();
        QTest::qWait(35);
        auto *canvas = w.currentCanvas();
        canvas->setAutoSelectLayers(false); // This scenario moves an explicitly selected sparse layer.
        canvas->actualSize();
        auto *canvasRight = w.findChild<QAction *>("alignCanvasRight");
        auto *selectionLeft = w.findChild<QAction *>("alignSelectionLeft");
        auto *selectionMenu = w.findChild<QMenu *>("alignSelectionMenu");
        QVERIFY(canvasRight && selectionLeft && selectionMenu);
        QVERIFY(canvasRight->isEnabled() && !selectionLeft->isEnabled());
        QVERIFY(!selectionMenu->menuAction()->isEnabled());
        QVERIFY(d->setSelection({5, 6, 25, 20}));
        QVERIFY(selectionLeft->isEnabled());
        auto *alignmentMenu = w.findChild<QMenu *>("layerAlignmentMenu");
        auto *alignButton = w.findChild<QToolButton *>("quickAlign");
        alignmentMenu->popup(alignButton->mapToGlobal(QPoint(0, alignButton->height())));
        QApplication::processEvents();
        QVERIFY(selectionMenu->menuAction()->isEnabled());
        QTest::mouseClick(alignmentMenu, Qt::LeftButton, {},
                          alignmentMenu->actionGeometry(selectionMenu->menuAction()).center());
        QTRY_VERIFY(selectionMenu->isVisible());
        QTest::mouseClick(selectionMenu, Qt::LeftButton, {},
                          selectionMenu->actionGeometry(selectionLeft).center());
        QTRY_VERIFY(!alignmentMenu->isVisible());
        QCOMPARE(d->activeLayerBounds().left(), 5);
        d->undo();
        const QPoint before = d->state.layers[0].offset;
        mouse(canvas, QEvent::MouseButtonPress, {17, 12});
        mouse(canvas, QEvent::MouseMove, {20, 14});
        QVERIFY(d->isEditing());
        const QPoint moved = d->state.layers[0].offset;
        QVERIFY(moved != before);
        const int history = d->historyLabels().size();
        canvasRight->trigger();
        QVERIFY(!d->isEditing());
        QCOMPARE(d->historyLabels().size(), history + 2); // Finish gesture, then align.
        QCOMPARE(d->activeLayerBounds().right(), d->state.size.width() - 1);
        QCOMPARE(d->activeLayerBounds().y(), moved.y());
        mouse(canvas, QEvent::MouseButtonRelease, {20, 14});
        d->undo();
        QCOMPARE(d->state.layers[0].offset, moved);
        d->undo();
        QCOMPARE(d->state.layers[0].offset, before);
        d->beginEdit();
        d->state.layers[0].locked = true;
        d->commit("Lock");
        QVERIFY(!canvasRight->isEnabled() && !selectionLeft->isEnabled());
        d->undo();
        d->beginEdit();
        d->state.layers[0].visible = false;
        d->commit("Hide");
        QVERIFY(!canvasRight->isEnabled() && !selectionLeft->isEnabled());
        d->undo();
        QVERIFY(d->setSelection(Selection(0, 0, 61, 43).inverted(d->state.size)));
        QVERIFY(canvasRight->isEnabled() && !selectionLeft->isEnabled());
    }
    void numericDialogReadsFinishedMoveBounds() {
        MainWindow w(false, false);
        auto *d = new Document;
        fixture(*d, 1);
        w.addDocument(d);
        w.resize(900, 620);
        w.show();
        QTest::qWait(35);
        auto *canvas = w.currentCanvas();
        canvas->setAutoSelectLayers(false); // The gesture exercises pending-move settlement, not hit testing.
        canvas->actualSize();
        const QPoint before = d->state.layers[0].offset;
        mouse(canvas, QEvent::MouseButtonPress, {17, 12});
        mouse(canvas, QEvent::MouseMove, {22, 15});
        const QPoint moved = d->activeLayerBounds().topLeft();
        QVERIFY(d->isEditing());
        const int history = d->historyLabels().size();
        QVERIFY(dialog(w.findChild<QAction *>("positionLayer"), [&](QDialog *modal) {
            QVERIFY(!d->isEditing());
            QCOMPARE(QPoint(modal->findChild<QSpinBox *>("layerPositionX")->value(),
                            modal->findChild<QSpinBox *>("layerPositionY")->value()), moved);
            modal->reject();
        }));
        QCOMPARE(d->historyLabels().size(), history + 1);
        QCOMPARE(d->activeLayerBounds().topLeft(), moved);
        mouse(canvas, QEvent::MouseButtonRelease, {22, 15});
        d->undo();
        QCOMPARE(d->state.layers[0].offset, before);
    }
    void scaledPreviewFinishesBeforePositioning_data() {
        QTest::addColumn<bool>("numeric");
        QTest::addColumn<bool>("accept");
        QTest::newRow("align-right") << false << true;
        QTest::newRow("numeric-accept") << true << true;
        QTest::newRow("numeric-cancel") << true << false;
    }
    void scaledPreviewFinishesBeforePositioning() {
        QFETCH(bool, numeric);
        QFETCH(bool, accept);
        MainWindow w(false, false);
        auto *d = new Document;
        fixture(*d, 1);
        QVERIFY(d->setSelection(Selection(3, 4, 20, 18).feathered(2, d->state.size)));
        QVERIFY(d->saveSelection("Soft coverage"));
        QVERIFY(d->setSelection({7, 8, 14, 11}));
        d->markSaved();
        const Layer source = d->state.layers[0];
        const QRect originalBounds = d->activeLayerBounds();
        const QImage selection = d->selection.mask(d->state.size);
        const QImage previous = d->state.previousSelection.mask(d->state.size);
        const QImage saved = d->state.savedSelections[0].selection.mask(d->state.size);
        auto verifySelections = [&] {
            QCOMPARE(d->state.active, 0);
            QCOMPARE(d->selection.mask(d->state.size), selection);
            QCOMPARE(d->state.previousSelection.mask(d->state.size), previous);
            QCOMPARE(d->savedSelectionNames(), QStringList{"Soft coverage"});
            QCOMPARE(d->state.savedSelections[0].selection.mask(d->state.size), saved);
        };
        const int history = d->historyLabels().size();
        const quint64 clean = d->revision();
        w.addDocument(d);
        w.resize(900, 620);
        w.show();
        QTest::qWait(35);
        auto *canvas = w.currentCanvas();
        canvas->actualSize();
        canvas->setZoom(5, canvas->toWidget(QPointF(d->state.size.width() / 2.0,
                                                   d->state.size.height() / 2.0)));
        QVERIFY(canvas->beginTransform());
        const QPointF handle = canvas->transformHandlePosition(TransformHandles::BottomRight);
        const QPointF target = QPointF(source.offset) + source.geometry.map(
            QPointF(source.pixels.width() * 1.6, source.pixels.height() * 1.6));
        mouse(canvas, QEvent::MouseButtonPress, canvas->toImage(handle));
        mouse(canvas, QEvent::MouseMove, target);
        const QTransform preview = canvas->transformGeometry();
        const QRect previewBounds = preview.mapRect(
            QRectF(QPointF(), QSizeF(source.pixels.size()))).toAlignedRect().translated(source.offset);
        QVERIFY(preview != source.geometry);
        QVERIFY(previewBounds.size() != originalBounds.size());
        QVERIFY(canvas->isTransforming());
        QVERIFY(!d->isEditing());
        QCOMPARE(d->state.layers[0].geometry, source.geometry);
        QCOMPARE(d->activeLayerBounds(), originalBounds);
        QCOMPARE(d->revision(), clean);
        QCOMPARE(d->historyLabels().size(), history);
        verifySelections();

        const QPoint destination = numeric ? QPoint(-12, 17)
                                           : QPoint(d->state.size.width() - previewBounds.width(),
                                                    previewBounds.y());
        if (numeric) {
            auto *action = w.findChild<QAction *>("positionLayer");
            QVERIFY(action && action->isEnabled());
            QVERIFY(dialog(action, [&](QDialog *modal) {
                QVERIFY(!canvas->isTransforming());
                QVERIFY(!d->isEditing());
                QCOMPARE(d->state.layers[0].geometry, preview);
                QCOMPARE(d->activeLayerBounds(), previewBounds);
                QCOMPARE(d->historyLabels().size(), history + 1);
                auto *x = modal->findChild<QSpinBox *>("layerPositionX");
                auto *y = modal->findChild<QSpinBox *>("layerPositionY");
                QVERIFY(x && y);
                QCOMPARE(QPoint(x->value(), y->value()), previewBounds.topLeft());
                x->setValue(destination.x());
                y->setValue(destination.y());
                if (accept)
                    modal->accept();
                else
                    modal->reject();
            }));
        } else {
            auto *action = w.findChild<QAction *>("alignCanvasRight");
            QVERIFY(action && action->isEnabled());
            action->trigger();
        }
        QVERIFY(!canvas->isTransforming());
        QVERIFY(!d->isEditing());
        const QRect resultBounds(accept ? destination : previewBounds.topLeft(), previewBounds.size());
        QCOMPARE(d->activeLayerBounds(), resultBounds);
        QCOMPARE(d->historyLabels().size(), history + 1 + int(accept));
        Layer transformed = source;
        transformed.geometry = preview;
        sourceUnchanged(d->state.layers[0], transformed);
        verifySelections();
        QVERIFY(d->dirty);
        const quint64 resultRevision = d->revision();
        // The old pointer release must not apply the gesture a second time.
        mouse(canvas, QEvent::MouseButtonRelease, target);
        QCOMPARE(d->revision(), resultRevision);
        QCOMPARE(d->activeLayerBounds(), resultBounds);
        if (accept) {
            d->undo(); // Positioning has its own step after the committed preview.
            QCOMPARE(d->activeLayerBounds(), previewBounds);
            QCOMPARE(d->state.layers[0].offset, source.offset);
            sourceUnchanged(d->state.layers[0], transformed);
            verifySelections();
            QVERIFY(d->dirty);
        }
        d->undo();
        QCOMPARE(d->revision(), clean);
        QCOMPARE(d->activeLayerBounds(), originalBounds);
        sourceUnchanged(d->state.layers[0], source);
        verifySelections();
        QVERIFY(!d->dirty);
        d->redo();
        QCOMPARE(d->activeLayerBounds(), previewBounds);
        sourceUnchanged(d->state.layers[0], transformed);
        verifySelections();
        if (accept)
            d->redo();
        QCOMPARE(d->revision(), resultRevision);
        QCOMPARE(d->activeLayerBounds(), resultBounds);
        sourceUnchanged(d->state.layers[0], transformed);
        verifySelections();
    }
    void inlineRenameFinishesBeforePositioning_data() {
        QTest::addColumn<bool>("numeric");
        QTest::addColumn<bool>("accept");
        QTest::newRow("align-right") << false << true;
        QTest::newRow("numeric-accept") << true << true;
        QTest::newRow("numeric-cancel") << true << false;
    }
    void inlineRenameFinishesBeforePositioning() {
        QFETCH(bool, numeric);
        QFETCH(bool, accept);
        MainWindow w(false, false);
        auto *d = new Document;
        fixture(*d, 1);
        w.addDocument(d);
        w.applyUiPreset("modular", false);
        w.resize(900, 620);
        w.show();
        w.activateWindow();
        QTest::qWait(40);
        const Layer source = d->state.layers[0];
        const QRect bounds = d->activeLayerBounds();
        const quint64 clean = d->revision();
        const int history = d->historyLabels().size();
        auto *layers = w.findChild<QListWidget *>("layers");
        QVERIFY(layers && layers->isVisible());
        layers->setFocus();
        QTest::keyClick(layers, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        const QString renamed = "Positioned draft";
        QTest::keyClicks(nameEditor(w), qPrintable(renamed));
        QCOMPARE(d->state.layers[0].name, source.name);
        QCOMPARE(d->revision(), clean);
        QCOMPARE(d->historyLabels().size(), history);

        const QPoint destination = numeric ? QPoint(-16, 21)
                                           : QPoint(d->state.size.width() - bounds.width(), bounds.y());
        if (numeric) {
            auto *action = w.findChild<QAction *>("positionLayer");
            QVERIFY(action && action->isEnabled());
            QVERIFY(dialog(action, [&](QDialog *modal) {
                // Commit the draft before the numeric dialog captures its stale-state guard.
                QCOMPARE(d->state.layers[0].name, renamed);
                QCOMPARE(d->historyLabels().size(), history + 1);
                QCOMPARE(d->activeLayerBounds(), bounds);
                QVERIFY(!nameEditor(w));
                auto *x = modal->findChild<QSpinBox *>("layerPositionX");
                auto *y = modal->findChild<QSpinBox *>("layerPositionY");
                QVERIFY(x && y);
                QCOMPARE(QPoint(x->value(), y->value()), bounds.topLeft());
                x->setValue(destination.x());
                y->setValue(destination.y());
                if (accept)
                    modal->accept();
                else
                    modal->reject();
            }));
        } else {
            auto *action = w.findChild<QAction *>("alignCanvasRight");
            QVERIFY(action && action->isEnabled());
            action->trigger();
        }
        QTRY_VERIFY(!nameEditor(w));
        const QRect resultBounds(accept ? destination : bounds.topLeft(), bounds.size());
        QCOMPARE(d->historyLabels().size(), history + 1 + int(accept));
        QCOMPARE(d->activeLayerBounds(), resultBounds);
        Layer renamedSource = source;
        renamedSource.name = renamed;
        sourceUnchanged(d->state.layers[0], renamedSource);
        const quint64 result = d->revision();
        if (accept) {
            d->undo(); // Position/alignment must leave the earlier rename intact.
            QCOMPARE(d->activeLayerBounds(), bounds);
            QCOMPARE(d->state.layers[0].offset, source.offset);
            sourceUnchanged(d->state.layers[0], renamedSource);
            QVERIFY(d->dirty);
        }
        d->undo();
        QCOMPARE(d->revision(), clean);
        QCOMPARE(d->activeLayerBounds(), bounds);
        sourceUnchanged(d->state.layers[0], source);
        QVERIFY(!d->dirty);
        d->redo();
        QCOMPARE(d->activeLayerBounds(), bounds);
        sourceUnchanged(d->state.layers[0], renamedSource);
        if (accept)
            d->redo();
        QCOMPARE(d->revision(), result);
        QCOMPARE(d->activeLayerBounds(), resultBounds);
        sourceUnchanged(d->state.layers[0], renamedSource);
    }
    void modelNudgesPreserveSourcesAndCoalesce_data() {
        QTest::addColumn<int>("transform");
        for (int i = 0; i < 4; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void modelNudgesPreserveSourcesAndCoalesce() {
        QFETCH(int, transform);
        Document d;
        fixture(d, transform);
        QVERIFY(d.setSelection(Selection(3, 4, 20, 18).feathered(2, d.state.size)));
        QVERIFY(d.saveSelection("Coverage"));
        QVERIFY(d.setSelection({8, 9, 11, 12}));
        d.markSaved();
        d.editingMask = true;
        const State source = d.state;
        const int count = d.historyDescriptions().size();
        const quint64 clean = d.revision();
        QVERIFY(d.nudgeLayer({}));
        QCOMPARE(d.revision(), clean);
        QVERIFY(!d.dirty);
        QVERIFY(d.nudgeLayer({1, 0}));
        quint64 previous = d.revision();
        for (int i = 0; i < 40; ++i) {
            QVERIFY(d.nudgeLayer({0, -10}, previous));
            QVERIFY(d.revision() > previous);
            const auto entries = d.historyDescriptions();
            QVERIFY(std::none_of(entries.cbegin(), entries.cend(), [previous](const auto &entry) {
                return entry.revision == previous;
            }));
            previous = d.revision();
        }
        QCOMPARE(d.historyDescriptions().size(), count + 1);
        QCOMPARE(d.state.layers[0].offset, source.layers[0].offset + QPoint(1, -400));
        sourceUnchanged(d.state.layers[0], source.layers[0]);
        QCOMPARE(d.selection.mask(d.state.size), source.selection.mask(d.state.size));
        QCOMPARE(d.state.previousSelection.mask(d.state.size), source.previousSelection.mask(d.state.size));
        QCOMPARE(d.state.savedSelections[0].selection.mask(d.state.size),
                 source.savedSelections[0].selection.mask(d.state.size));
        QVERIFY(d.editingMask && !d.isEditing() && d.dirty);
        d.undo();
        QCOMPARE(d.revision(), clean);
        QCOMPARE(d.state.layers[0].offset, source.layers[0].offset);
        QVERIFY(!d.dirty && d.canRedo());
        QVERIFY(d.nudgeLayer({}));
        QVERIFY(d.canRedo());
        d.redo();
        QCOMPARE(d.state.layers[0].offset, source.layers[0].offset + QPoint(1, -400));
    }
    void modelNudgesKeepEditableTextAndOtherLayers() {
        Document d;
        fixture(d);
        QFont font("DejaVu Sans");
        font.setPixelSize(16);
        QString error;
        QVERIFY(d.addText("Editable text", font, QColor("#154f76"), {7, 8}, &error));
        d.beginEdit();
        auto &text = d.state.layers[d.state.active];
        text.mask = QImage(text.pixels.size(), QImage::Format_ARGB32_Premultiplied);
        text.mask.fill(Qt::white);
        text.maskSettings.feather = 1;
        text.geometry.rotate(12);
        d.commit("Text source");
        QVERIFY(d.setSelection({2, 3, 5, 7}));
        d.markSaved();
        const State source = d.state;
        const int active = d.state.active;
        QVERIFY(!source.layers[active].text.isEmpty());
        QVERIFY(d.nudgeLayer({-10, 0}));
        QVERIFY(d.nudgeLayer({-10, 0}, d.revision()));
        QCOMPARE(d.state.layers[active].offset, source.layers[active].offset + QPoint(-20, 0));
        sourceUnchanged(d.state.layers[active], source.layers[active]);
        sourceUnchanged(d.state.layers[0], source.layers[0]);
        QCOMPARE(d.state.layers[0].offset, source.layers[0].offset);
        QCOMPARE(d.selection.mask(d.state.size), source.selection.mask(d.state.size));
        d.undo();
        QCOMPARE(d.state.layers[active].offset, source.layers[active].offset);
        sourceUnchanged(d.state.layers[active], source.layers[active]);
        QVERIFY(!d.dirty);
        d.redo();
        QCOMPARE(d.state.layers[active].offset, source.layers[active].offset + QPoint(-20, 0));
        sourceUnchanged(d.state.layers[active], source.layers[active]);
    }
    void modelNudgesRejectAtomicallyAndRespectBoundaries() {
        Document d;
        fixture(d);
        auto reject = [&](QPoint delta, quint64 merge = 0) {
            const State before = d.state;
            const quint64 revision = d.revision();
            const auto history = d.historyLabels();
            const bool dirty = d.dirty, editing = d.isEditing(), redo = d.canRedo();
            QString error;
            QVERIFY(!d.nudgeLayer(delta, merge, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(d.revision(), revision);
            QCOMPARE(d.historyLabels(), history);
            QCOMPARE(d.dirty, dirty);
            QCOMPARE(d.isEditing(), editing);
            QCOMPARE(d.canRedo(), redo);
            QCOMPARE(d.state.layers.size(), before.layers.size());
            if (!before.layers.isEmpty())
                QCOMPARE(d.state.layers[0].offset, before.layers[0].offset);
        };
        reject({std::numeric_limits<int>::max(), 0});
        reject({std::numeric_limits<int>::min(), 0});
        d.state.layers[0].locked = true;
        reject({1, 0});
        d.state.layers[0].locked = false;
        d.state.layers[0].visible = false;
        reject({1, 0});
        d.state.layers[0].visible = true;
        d.beginEdit();
        reject({1, 0});
        d.cancelEdit();
        d.state.active = -1;
        reject({1, 0});
        d.state.active = 9;
        reject({1, 0});
        d.state.active = 0;
        d.state.layers[0].geometry = QTransform::fromScale(0, 0);
        reject({1, 0});
        d.state.layers[0].geometry = {};
        const QImage pixels = d.state.layers[0].pixels;
        d.state.layers[0].pixels = {};
        reject({1, 0});
        d.state.layers[0].pixels = pixels;
        QVERIFY(d.positionLayer({Document::MaxCoordinate, -Document::MaxCoordinate}));
        reject({1, 0});
        reject({0, -10});
        QVERIFY(d.nudgeLayer({-1, 10}));
        quint64 revision = d.revision();
        d.markSaved();
        reject({1, 0}, revision);
        QVERIFY(d.nudgeLayer({-1, 0}));
        revision = d.revision();
        d.undo();
        d.redo();
        reject({1, 0}, revision);
        QVERIFY(d.nudgeLayer({-1, 0}));
        revision = d.revision();
        d.changed();
        reject({1, 0}, revision);
        QVERIFY(d.nudgeLayer({-1, 0}));
        revision = d.revision();
        d.state.layers[0].name = "Unannounced mutation";
        reject({1, 0}, revision);
        d.state.layers.clear();
        reject({1, 0});
    }
    void keyboardArrowsUseDocumentPixels_data() {
        QTest::addColumn<int>("transform");
        QTest::addColumn<qreal>("zoom");
        for (int transform = 0; transform < 4; ++transform)
            for (qreal zoom : {.25, 1., 4.})
                QTest::newRow(qPrintable(QString("%1-%2").arg(transform).arg(zoom))) << transform << zoom;
    }
    void keyboardArrowsUseDocumentPixels() {
        QFETCH(int, transform);
        QFETCH(qreal, zoom);
        Document d;
        fixture(d, transform);
        Canvas canvas(&d);
        canvas.resize(500, 400);
        canvas.show();
        canvas.setFocus();
        QTRY_VERIFY(canvas.hasFocus());
        canvas.setZoom(zoom);
        const Layer source = d.state.layers[0];
        const int count = d.historyLabels().size();
        QTest::keyClick(&canvas, Qt::Key_Right);
        QCOMPARE(d.state.layers[0].offset, source.offset + QPoint(1, 0));
        QTest::keyClick(&canvas, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(d.state.layers[0].offset, source.offset + QPoint(1, -10));
        QTest::keyClick(&canvas, Qt::Key_Left);
        QTest::keyClick(&canvas, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(d.state.layers[0].offset, source.offset);
        QCOMPARE(d.historyLabels().size(), count + 4);
        sourceUnchanged(d.state.layers[0], source);
        d.undo();
        QCOMPARE(d.state.layers[0].offset, source.offset + QPoint(0, -10));
    }
    void keyboardRepeatReleaseAndEscapeKeepOneCommittedStep() {
        Document d;
        fixture(d, 2);
        QVERIFY(d.setSelection({2, 3, 7, 9}));
        d.markSaved();
        Canvas canvas(&d);
        canvas.resize(500, 400);
        canvas.show();
        canvas.setFocus();
        QTRY_VERIFY(canvas.hasFocus());
        const QPoint start = d.state.layers[0].offset;
        const int count = d.historyLabels().size();
        key(&canvas, QEvent::KeyPress, Qt::Key_Right);
        for (int i = 0; i < 45; ++i) {
            const quint64 previous = d.revision();
            key(&canvas, QEvent::KeyRelease, Qt::Key_Right, {}, true);
            key(&canvas, QEvent::KeyPress, Qt::Key_Right, {}, true);
            QVERIFY(d.revision() > previous);
            QVERIFY(!d.isEditing());
        }
        QCOMPARE(d.state.layers[0].offset, start + QPoint(46, 0));
        QCOMPARE(d.historyLabels().size(), count + 1);
        key(&canvas, QEvent::KeyRelease, Qt::Key_Right);
        key(&canvas, QEvent::KeyPress, Qt::Key_Right, {}, true);
        QCOMPARE(d.state.layers[0].offset, start + QPoint(46, 0));
        QTest::keyClick(&canvas, Qt::Key_Right);
        QCOMPARE(d.historyLabels().size(), count + 2);
        QCOMPARE(d.state.layers[0].offset, start + QPoint(47, 0));
        key(&canvas, QEvent::KeyPress, Qt::Key_Right);
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QCOMPARE(d.state.layers[0].offset, start + QPoint(48, 0));
        QVERIFY(d.selection.isEmpty()); // Escape retains its existing deselect behavior.
        key(&canvas, QEvent::KeyPress, Qt::Key_Right, {}, true);
        QCOMPARE(d.state.layers[0].offset, start + QPoint(48, 0));
        d.undo(); // Deselect is its own ordinary history step.
        d.undo(); // The last committed nudge is reversed by Undo, not Escape.
        QCOMPARE(d.state.layers[0].offset, start + QPoint(47, 0));
        d.undo();
        d.undo();
        QCOMPARE(d.state.layers[0].offset, start);
        QVERIFY(!d.dirty);
    }
    void interruptedKeyboardRepeatsNeverAdoptAnotherTarget_data() {
        QTest::addColumn<QString>("transition");
        for (const QString &transition : {"focus", "tool", "layer", "layer-roundtrip", "mask-mode", "tab", "mark-saved", "save", "save-copy",
                                          "undo", "redo", "history", "refresh", "pixels", "selection"})
            QTest::newRow(qPrintable(transition)) << transition;
    }
    void interruptedKeyboardRepeatsNeverAdoptAnotherTarget() {
        QFETCH(QString, transition);
        MainWindow w(false, false);
        auto *d = new Document;
        fixture(*d);
        QVERIFY(d->duplicate(nullptr));
        w.addDocument(d);
        w.resize(900, 620);
        w.show();
        auto *canvas = w.currentCanvas();
        canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        key(canvas, QEvent::KeyPress, Qt::Key_Right);
        key(canvas, QEvent::KeyPress, Qt::Key_Right, {}, true);
        QVERIFY(!d->isEditing());
        QTemporaryDir files;
        QString error;
        if (transition == "focus") {
            auto *layers = w.findChild<QListWidget *>("layers");
            layers->setFocus();
            QTRY_VERIFY(layers->hasFocus());
            canvas->setFocus();
            QTRY_VERIFY(canvas->hasFocus());
        } else if (transition == "tool") {
            canvas->setTool(Canvas::Brush);
            canvas->setTool(Canvas::Move);
        } else if (transition == "layer" || transition == "layer-roundtrip") {
            auto *layers = w.findChild<QListWidget *>("layers");
            layers->setCurrentRow(1);
            QCOMPARE(d->state.active, 0);
            if (transition == "layer-roundtrip") {
                layers->setCurrentRow(0);
                QCOMPARE(d->state.active, 1);
            }
        } else if (transition == "mask-mode") {
            w.findChild<QAction *>("mask")->trigger();
        } else if (transition == "tab") {
            auto *other = new Document;
            fixture(*other);
            w.addDocument(other);
            other->markSaved();
            auto *tabs = w.findChild<QTabWidget *>("documents");
            canvas = w.currentCanvas();
            canvas->setFocus();
            QTRY_VERIFY(canvas->hasFocus());
            const QPoint otherStart = other->state.layers[0].offset;
            key(canvas, QEvent::KeyPress, Qt::Key_Right, {}, true);
            QCOMPARE(other->state.layers[0].offset, otherStart);
            tabs->setCurrentIndex(0);
            canvas = w.currentCanvas();
        } else if (transition == "mark-saved") {
            d->markSaved();
        } else if (transition == "save" || transition == "save-copy") {
            QVERIFY(files.isValid());
            QVERIFY(d->save(files.filePath("held.pdeck"), &error, transition != "save-copy"));
            const QPoint saved = d->state.layers[d->state.active].offset;
            Document reopened;
            QVERIFY(reopened.load(files.filePath("held.pdeck"), &error));
            QCOMPARE(reopened.state.layers[reopened.state.active].offset, saved);
        } else if (transition == "undo") {
            w.findChild<QAction *>("undo")->trigger();
        } else if (transition == "redo") {
            d->undo();
            w.findChild<QAction *>("redo")->trigger();
        } else if (transition == "history") {
            auto *history = w.findChild<QListWidget *>("history");
            QTest::qWait(40);
            history->setCurrentRow(1);
            QTest::keyClick(history, Qt::Key_Return);
        } else if (transition == "refresh") {
            d->changed();
        } else if (transition == "pixels") {
            d->state.layers[d->state.active].pixels.setPixelColor(0, 0, Qt::green);
        } else if (transition == "selection") {
            d->selection = Selection(4, 5, 6, 7); // Unannounced legacy context mutation.
        }
        canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        QVector<QPoint> positions;
        for (const auto &layer : d->state.layers)
            positions.append(layer.offset);
        const quint64 revision = d->revision();
        const int count = d->historyLabels().size();
        key(canvas, QEvent::KeyRelease, Qt::Key_Right, {}, true);
        key(canvas, QEvent::KeyPress, Qt::Key_Right, {}, true);
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->historyLabels().size(), count);
        for (int i = 0; i < positions.size(); ++i)
            QCOMPARE(d->state.layers[i].offset, positions[i]);
        QTest::keyClick(canvas, Qt::Key_Right);
        QCOMPARE(d->state.layers[d->state.active].offset,
                 positions[d->state.active] + QPoint(1, 0));
    }
    void keyboardMoveDoesNotStealOtherControlsOrTransformPreview() {
        MainWindow w(false, false);
        auto *d = new Document;
        fixture(*d, 1);
        QVERIFY(d->duplicate(nullptr));
        w.addDocument(d);
        w.resize(900, 620);
        w.show();
        auto *canvas = w.currentCanvas();
        canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        const QPoint start = d->state.layers[d->state.active].offset;
        const quint64 revision = d->revision();
        for (auto modifier : {Qt::ControlModifier, Qt::AltModifier, Qt::MetaModifier})
            QTest::keyClick(canvas, Qt::Key_Right, modifier);
        QCOMPARE(d->revision(), revision);
        canvas->setTool(Canvas::Brush);
        QTest::keyClick(canvas, Qt::Key_Right);
        QCOMPARE(d->revision(), revision);
        canvas->setTool(Canvas::Move);
        auto *layers = w.findChild<QListWidget *>("layers");
        layers->setFocus();
        QTest::keyClick(layers, Qt::Key_Down);
        QCOMPARE(d->revision(), revision);
        auto *history = w.findChild<QListWidget *>("history");
        history->setFocus();
        QTest::keyClick(history, Qt::Key_Up);
        QCOMPARE(d->revision(), revision);
        QLineEdit text("abcd", &w);
        text.show();
        text.setFocus();
        text.setCursorPosition(2);
        QTest::keyClick(&text, Qt::Key_Left);
        QCOMPARE(text.cursorPosition(), 1);
        QCOMPARE(d->revision(), revision);
        QMenu menu(&w);
        menu.addAction("First");
        menu.addAction("Second");
        menu.popup(w.mapToGlobal(QPoint(200, 100)));
        QApplication::processEvents();
        QTest::keyClick(&menu, Qt::Key_Down);
        QCOMPARE(d->revision(), revision);
        menu.hide();
        text.hide();
        // Popup dismissal restores activation asynchronously on some platforms.
        w.activateWindow();
        QTRY_VERIFY(w.isActiveWindow());
        canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        QVERIFY(canvas->beginTransform());
        const QTransform geometry = canvas->transformGeometry();
        QTest::keyClick(canvas, Qt::Key_Right);
        QVERIFY(canvas->transformGeometry() != geometry);
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->state.layers[d->state.active].offset, start);
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(!canvas->isTransforming());
        QCOMPARE(d->revision(), revision);
        key(canvas, QEvent::KeyPress, Qt::Key_Right, {}, true);
        QCOMPARE(d->revision(), revision);
    }
    void keyboardMoveDoesNotJoinMouseGestureOrDestroyedTarget() {
        auto d = std::make_unique<Document>();
        fixture(*d);
        Canvas canvas(d.get());
        canvas.resize(500, 400);
        canvas.show();
        canvas.setFocus();
        QTRY_VERIFY(canvas.hasFocus());
        canvas.actualSize();
        mouse(&canvas, QEvent::MouseButtonPress, {17, 12});
        mouse(&canvas, QEvent::MouseMove, {22, 15});
        const QPoint moved = d->state.layers[0].offset;
        const int count = d->historyLabels().size();
        QTest::keyClick(&canvas, Qt::Key_Right);
        QCOMPARE(d->state.layers[0].offset, moved);
        QCOMPARE(d->historyLabels().size(), count);
        mouse(&canvas, QEvent::MouseButtonRelease, {22, 15});
        QCOMPARE(d->historyLabels().size(), count + 1);
        key(&canvas, QEvent::KeyPress, Qt::Key_Right);
        canvas.hide();
        d.reset();
        key(&canvas, QEvent::KeyPress, Qt::Key_Right, {}, true);
        key(&canvas, QEvent::KeyRelease, Qt::Key_Right);
    }
    void reentrantKeyboardBoundariesCannotRearmHeldState_data() {
        QTest::addColumn<QString>("boundary");
        QTest::addColumn<bool>("repeat");
        for (const QString &boundary : {"finish", "tool", "focus", "release", "save", "history",
                                       "nested-target", "destroy-document", "destroy-canvas"})
            for (bool repeat : {false, true})
                QTest::newRow(qPrintable(boundary + (repeat ? "-repeat" : "-first"))) << boundary << repeat;
    }
    void reentrantKeyboardBoundariesCannotRearmHeldState() {
        QFETCH(QString, boundary);
        QFETCH(bool, repeat);
        QPointer<Document> d = new Document;
        fixture(*d);
        QVERIFY(d->duplicate(nullptr));
        QPointer<Canvas> canvas = new Canvas(d);
        canvas->resize(500, 400);
        canvas->show();
        canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        if (repeat)
            key(canvas, QEvent::KeyPress, Qt::Key_Right);
        QTemporaryDir files;
        QVERIFY(files.isValid());
        bool handled = false;
        connect(d, &Document::updated, this, [&] {
            if (handled)
                return;
            handled = true;
            if (boundary == "finish")
                canvas->finishPendingGesture();
            else if (boundary == "tool") {
                canvas->setTool(Canvas::Brush);
                canvas->setTool(Canvas::Move);
            } else if (boundary == "focus") {
                canvas->clearFocus();
                canvas->setFocus();
            } else if (boundary == "release") {
                key(canvas, QEvent::KeyRelease, Qt::Key_Right);
            } else if (boundary == "save") {
                QString error;
                QVERIFY(d->save(files.filePath("reentrant.pdeck"), &error));
            } else if (boundary == "history") {
                d->undo();
            } else if (boundary == "nested-target") {
                d->state.active = 0;
                QVERIFY(d->nudgeLayer({0, 1}));
            } else if (boundary == "destroy-document") {
                canvas->hide();
                delete d;
            } else if (boundary == "destroy-canvas") {
                delete canvas;
            }
        });
        key(canvas, QEvent::KeyPress, Qt::Key_Right, {}, repeat);
        QVERIFY(handled);
        if (canvas && d) {
            canvas->setFocus();
            QTRY_VERIFY(canvas->hasFocus());
            const quint64 revision = d->revision();
            const QPoint offset = d->state.layers[d->state.active].offset;
            key(canvas, QEvent::KeyPress, Qt::Key_Right, {}, true);
            QCOMPARE(d->revision(), revision);
            QCOMPARE(d->state.layers[d->state.active].offset, offset);
        } else if (canvas) {
            key(canvas, QEvent::KeyPress, Qt::Key_Right, {}, true);
        }
        delete canvas;
        delete d;
    }
    void closingWhileArrowHeldSeesCommittedPositionAndEndsRepeats() {
        MainWindow w(false, false);
        auto *d = new Document;
        fixture(*d);
        w.addDocument(d);
        w.resize(900, 620);
        w.show();
        auto *canvas = w.currentCanvas();
        canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        key(canvas, QEvent::KeyPress, Qt::Key_Right);
        const QPoint moved = d->state.layers[0].offset;
        const quint64 revision = d->revision();
        bool sawCommitted = false;
        QTimer::singleShot(0, &w, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box)
                return;
            sawCommitted = !d->isEditing() && d->dirty && d->state.layers[0].offset == moved;
            box->button(QMessageBox::Cancel)->click();
        });
        QVERIFY(!w.close());
        QVERIFY(sawCommitted);
        w.activateWindow();
        canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        key(canvas, QEvent::KeyPress, Qt::Key_Right, {}, true);
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->state.layers[0].offset, moved);
    }
    void keyboardVisualSmoke_data() {
        QTest::addColumn<QString>("preset");
        for (const QString &preset : {"focus", "paper", "touch"})
            QTest::newRow(qPrintable(preset)) << preset;
    }
    void keyboardVisualSmoke() {
        QFETCH(QString, preset);
        MainWindow w(false, false);
        w.demo();
        w.applyUiPreset(preset, false);
        w.resize(900, 620);
        w.show();
        QTest::qWait(40);
        auto *canvas = w.currentCanvas();
        auto *d = w.currentDocument();
        canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        const Layer before = d->state.layers[d->state.active];
        const QString hint = w.findChild<QLabel *>("toolOptionsHint")->text();
        QVERIFY(hint.contains("1 px") && hint.contains("10 px"));
        const QString screenshots = qEnvironmentVariable("PHOTODECK_NUDGING_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            QVERIFY(w.grab().save(screenshots + "/keyboard-" + preset + "-before.png"));
        }
        key(canvas, QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
        key(canvas, QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier, true);
        key(canvas, QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier, true);
        key(canvas, QEvent::KeyRelease, Qt::Key_Right, Qt::ShiftModifier);
        QTest::keyClick(canvas, Qt::Key_Down);
        QCOMPARE(d->state.layers[d->state.active].offset, before.offset + QPoint(30, 1));
        sourceUnchanged(d->state.layers[d->state.active], before);
        QTest::qWait(40);
        if (!screenshots.isEmpty())
            QVERIFY(w.grab().save(screenshots + "/keyboard-" + preset + "-after.png"));
        w.findChild<QAction *>("undo")->trigger();
        w.findChild<QAction *>("undo")->trigger();
        QCOMPARE(d->state.layers[d->state.active].offset, before.offset);
    }
    void compactControlsAcrossEveryPreset_data() {
        QTest::addColumn<QString>("preset");
        for (const auto &preset : uiPresets())
            QTest::newRow(qPrintable(preset.id)) << preset.id;
    }
    void compactControlsAcrossEveryPreset() {
        QFETCH(QString, preset);
        MainWindow w(false, false);
        auto *d = new Document;
        fixture(*d);
        w.addDocument(d);
        w.applyUiPreset(preset, false);
        w.resize(900, 620);
        w.show();
        QTest::qWait(40);
        QCOMPARE(w.size(), QSize(900, 620));
        auto *position = w.findChild<QToolButton *>("quickPosition");
        auto *align = w.findChild<QToolButton *>("quickAlign");
        QVERIFY(position && align && align->menu());
        QVERIFY(!position->accessibleName().isEmpty() && !align->accessibleName().isEmpty());
        QVERIFY(position->width() > 50 && align->width() > 50);
        QScrollArea *scroll = nullptr;
        for (QWidget *parent = position->parentWidget(); parent && !scroll; parent = parent->parentWidget())
            scroll = qobject_cast<QScrollArea *>(parent);
        QVERIFY(scroll);
        QVERIFY2(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width(),
                 qPrintable(QString("Inspector minimum width %1 exceeds viewport width %2 (%3)")
                                .arg(scroll->widget()->minimumSizeHint().width())
                                .arg(scroll->viewport()->width()).arg(preset)));
        QVERIFY(!position->geometry().intersects(align->geometry()));
        const QString screenshots = qEnvironmentVariable("PHOTODECK_POSITIONING_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            scroll->ensureWidgetVisible(position);
            QApplication::processEvents();
            QVERIFY(w.grab().save(screenshots + "/positioning-" + preset + ".png"));
        }
        QVERIFY(dialog(w.findChild<QAction *>("positionLayer"), [&](QDialog *modal) {
            QVERIFY(modal->width() < 900 && modal->height() < 620);
            auto *x = modal->findChild<QSpinBox *>("layerPositionX");
            QVERIFY(x && x->isVisible());
            if (!screenshots.isEmpty() && preset == "focus")
                QVERIFY(modal->grab().save(screenshots + "/positioning-numeric.png"));
            modal->reject();
        }));
    }
};
QTEST_MAIN(PositioningTests)
#include "test_positioning.moc"
