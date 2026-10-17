#include "mainwindow.h"
#include <QAction>
#include <QDebug>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QtTest>
#include <memory>

namespace {
QLineEdit *nameEditor(MainWindow &window) {
    for (auto *line : window.findChildren<QLineEdit *>("layerNameEditor"))
        if (line->isVisible())
            return line;
    return nullptr;
}
// Document::updated deliberately schedules the inspector refresh. A fixed sleep
// can expire before its timer is delivered on a busy runner. Wait for the public
// history row IDs and layer selection that the same refresh publishes instead.
void settleLayerRows(MainWindow &window) {
    QApplication::processEvents();
    auto *document = window.currentDocument();
    auto *layers = window.findChild<QListWidget *>("layers");
    auto *history = window.findChild<QListWidget *>("history");
    QVERIFY(document && layers && history);
    QTRY_COMPARE(layers->count(), document->state.layers.size());
    QTRY_COMPARE(history->count(), document->historyDescriptions().size());
    const int current = document->currentHistoryIndex();
    QVERIFY(current >= 0 && current < history->count());
    QTRY_COMPARE(history->item(current)->data(Qt::UserRole).toULongLong(), document->revision());
    QTRY_COMPARE(layers->currentRow(), document->state.layers.size() - 1 - document->state.active);
    // Updating row sizes and scrollbars can enqueue a second layout pass. Drain
    // it before recording synthetic pointer positions in the resized inspector.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QApplication::processEvents();
}
Document *layerFixture(MainWindow &window) {
    auto *d = new Document;
    d->create({30, 20}, true);
    d->addLayer("Original");
    d->markSaved();
    window.addDocument(d);
    window.resize(1000, 720);
    window.show();
    window.activateWindow();
    if (!QTest::qWaitFor([&] { return window.isActiveWindow(); }))
        QTest::qFail("Layer fixture window did not become active", __FILE__, __LINE__);
    settleLayerRows(window);
    return d;
}
class EditorTeardownProbe : public QObject {
  public:
    bool sawWidgetBase = false;
  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Destroy)
            sawWidgetBase = dynamic_cast<QWidget *>(object) && !dynamic_cast<QLineEdit *>(object);
        return false;
    }
};
class ItemStyleProbe : public QStyledItemDelegate {
  public:
    QStyleOptionViewItem option(QListWidget *list, int row) const {
        QStyleOptionViewItem value;
        value.initFrom(list);
        value.widget = list;
        value.decorationSize = list->iconSize();
        initStyleOption(&value, list->model()->index(row, 0));
        value.rect = list->visualItemRect(list->item(row));
        return value;
    }
};
QPoint checkboxPoint(QListWidget *list, int row) {
    const auto option = ItemStyleProbe().option(list, row);
    return list->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, list).center();
}
struct DragResult {
    bool started = false, entered = false, moved = false, released = false;
};
// Send the same QWidget press/threshold/move/release sequence as the in-list
// gesture. A callback can change the document between input events.
DragResult dragLayer(QListWidget *list, QPoint from, QPoint to,
                     const std::function<void()> &during = {}, bool release = true,
                     const std::function<void()> &hover = {}) {
    DragResult result;
    auto *indicator = list->findChild<QWidget *>("layerDropIndicator");
    QTest::mousePress(list->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    const QPoint threshold = from + QPoint(QApplication::startDragDistance() + 4, 0);
    QMouseEvent begin(QEvent::MouseMove, threshold, list->viewport()->mapToGlobal(threshold),
                      Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &begin);
    result.started = indicator->isVisible();
    if (during)
        during();
    QMouseEvent move(QEvent::MouseMove, to, list->viewport()->mapToGlobal(to),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &move);
    result.entered = result.moved = indicator->isVisible();
    if (hover)
        hover();
    if (!release)
        QTest::keyClick(list, Qt::Key_Escape);
    result.released = release;
    QTest::mouseRelease(list->viewport(), Qt::LeftButton, Qt::NoModifier, to);
    return result;
}
QStringList layerNames(const Document &document) {
    QStringList names;
    for (const auto &layer : document.state.layers)
        names << layer.name;
    return names;
}
QPoint rowDragPoint(QListWidget *list, int row) {
    const QRect rect = list->visualItemRect(list->item(row));
    return {qMin(rect.right() - 5, rect.left() + 110), rect.center().y()};
}
Document *dragFixture(MainWindow &window) {
    auto *document = layerFixture(window);
    document->addLayer("Middle");
    document->addLayer("Top");
    document->markSaved();
    settleLayerRows(window);
    return document;
}
} // namespace

class LayerDragTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void dragSingleLayerIsOneUndoWithCorrectVisualOrder() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const int history = d->historyLabels().size();
        const auto result = dragLayer(list, rowDragPoint(list, 0),
                                     list->visualItemRect(list->item(2)).bottomLeft() + QPoint(110, -2));
        QVERIFY(result.started);
        QVERIFY(result.entered && result.moved && result.released);
        QCOMPARE(layerNames(*d), (QStringList{"Arka plan", "Top", "Original", "Middle"}));
        QCOMPARE(d->state.active, 1);
        QCOMPARE(list->currentRow(), 2);
        QCOMPARE(d->historyLabels().size(), history + 1);
        w.findChild<QAction *>("undo")->trigger();
        QCOMPARE(layerNames(*d), (QStringList{"Arka plan", "Original", "Middle", "Top"}));
        QCOMPARE(d->state.active, 3);
        w.findChild<QAction *>("redo")->trigger();
        QCOMPARE(d->state.layers[1].name, QString("Top"));
        QCOMPARE(d->state.active, 1);
    }
    void releaseOnVisibleInsertionLineCommits() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const QPoint start = rowDragPoint(list, 0);
        QTest::mousePress(list->viewport(), Qt::LeftButton, {}, start);
        const QPoint move = rowDragPoint(list, 2);
        QMouseEvent event(QEvent::MouseMove, move, list->viewport()->mapToGlobal(move),
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &event);
        auto *line = list->findChild<QWidget *>("layerDropIndicator");
        QVERIFY(line->isVisible());
        const QPoint release = line->geometry().center();
        QVERIFY(list->viewport()->visibleRegion().contains(release));
        const int history = d->historyLabels().size();
        QTest::mouseRelease(list->viewport(), Qt::LeftButton, {}, release);
        QCOMPARE(d->historyLabels().size(), history + 1);
        QCOMPARE(d->state.layers[d->state.active].name, QString("Top"));
        QVERIFY(d->state.active < 3);
    }
    void dragNoopAndCancellationPreserveRedoAndCleanState() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        d->reorderLayer(3, 1);
        d->undo();
        d->markSaved();
        settleLayerRows(w);
        const auto revision = d->revision();
        const int history = d->historyDescriptions().size();
        auto result = dragLayer(list, rowDragPoint(list, 0), rowDragPoint(list, 0));
        QVERIFY(result.started && result.released);
        QCOMPARE(d->revision(), revision);
        QVERIFY(d->canRedo());
        QVERIFY(!d->dirty);
        result = dragLayer(list, rowDragPoint(list, 0), rowDragPoint(list, 2), {}, false);
        QVERIFY(result.started && result.entered && !result.released);
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->historyDescriptions().size(), history);
        QVERIFY(d->canRedo());
        QVERIFY(!d->dirty);
    }
    void dragActiveMaskAndLockedLayerPreservesIdentity() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        QString error;
        QVERIFY(d->addMask(&error));
        QVERIFY(d->setClipping(true));
        d->beginEdit();
        d->state.layers[3].locked = true;
        d->commit("Fixture lock");
        d->selection = QRect(1, 2, 6, 5);
        d->markSaved();
        const Layer source = d->state.layers[3];
        settleLayerRows(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const auto result = dragLayer(list, rowDragPoint(list, 0), rowDragPoint(list, 2));
        QVERIFY(result.started && result.released);
        QCOMPARE(d->state.active, 2);
        QVERIFY(d->editingMask);
        QVERIFY(d->state.layers[2].locked && d->state.layers[2].clipped);
        QCOMPARE(d->state.layers[2].mask, source.mask);
        QCOMPARE(d->state.layers[2].pixels, source.pixels);
        QCOMPARE(d->selection, QRect(1, 2, 6, 5));
        d->undo();
        QCOMPARE(d->state.active, 3);
        QVERIFY(!d->editingMask);
        QCOMPARE(d->state.layers[3].mask, source.mask);
        QCOMPARE(d->selection, QRect(1, 2, 6, 5));
    }
    void dragRejectsClosedSourceDocument() {
        MainWindow w(false, false);
        QPointer<Document> source = dragFixture(w);
        auto *sourceCanvas = w.currentCanvas();
        auto *other = new Document;
        other->create({20, 10});
        w.addDocument(other);
        auto *tabs = w.findChild<QTabWidget *>("documents");
        tabs->setCurrentWidget(sourceCanvas);
        settleLayerRows(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const auto revision = other->revision();
        const auto result = dragLayer(list, rowDragPoint(list, 0), rowDragPoint(list, 2), [&] {
            w.findChild<QAction *>("close")->trigger();
            QVERIFY(!source);
        });
        QVERIFY(result.started);
        QVERIFY(!result.entered && !result.moved);
        QCOMPARE(w.currentDocument(), other);
        QCOMPARE(other->revision(), revision);
    }
    void dragSourceWindowCanBeDestroyedDuringGesture() {
        auto window = std::make_unique<MainWindow>(false, false);
        dragFixture(*window);
        QPointer<QListWidget> list = window->findChild<QListWidget *>("layers");
        QPointer<QWidget> viewport = list->viewport();
        const QPoint from = rowDragPoint(list, 0);
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, from);
        const QPoint point = from + QPoint(QApplication::startDragDistance() + 4, 0);
        QMouseEvent move(QEvent::MouseMove, point, viewport->mapToGlobal(point),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        QVERIFY(list->findChild<QWidget *>("layerDropIndicator")->isVisible());
        window.reset();
        QVERIFY(!list && !viewport && !window);
        QWidget releaseTarget;
        QTest::mouseRelease(&releaseTarget, Qt::LeftButton);
    }
    void dragFeedbackAndAutoscrollAtWindowSizes_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<QString>("preset");
        QTest::newRow("focus-small") << QSize(900, 620) << QString("focus");
        QTest::newRow("focus-roomy") << QSize(1380, 880) << QString("focus");
        QTest::newRow("paper-roomy") << QSize(1380, 880) << QString("paper");
        QTest::newRow("touch-small") << QSize(900, 620) << QString("touch");
    }
    void dragFeedbackAndAutoscrollAtWindowSizes() {
        QFETCH(QSize, size);
        QFETCH(QString, preset);
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        w.applyUiPreset(preset, false);
        for (int i = 4; i < 20; ++i)
            d->addLayer(QString("Layer %1").arg(i));
        d->markSaved();
        w.resize(size);
        settleLayerRows(w);
        QCOMPARE(w.size(), size);
        auto *list = w.findChild<QListWidget *>("layers");
        auto *scroll = list->verticalScrollBar();
        QVERIFY(scroll->maximum() > 0);
        scroll->setValue(0);
        const int history = d->historyLabels().size();
        bool marker = false;
        const QRect visible = list->viewport()->visibleRegion().boundingRect();
        QVERIFY(visible.width() > 20 && visible.height() > 20);
        const QPoint point(visible.center().x(), visible.bottom() - 4);
        const auto result = dragLayer(list, rowDragPoint(list, 0), point, {}, true, [&] {
            QTRY_VERIFY2(scroll->value() > 0,
                         qPrintable(QString("Autoscroll stalled: initial area %1,%2 %3x%4, current area %5,%6 %7x%8, "
                                            "hover %9,%10, scroll maximum %11, focus %12, DPR %13")
                                        .arg(visible.x()).arg(visible.y()).arg(visible.width()).arg(visible.height())
                                        .arg(list->viewport()->visibleRegion().boundingRect().x())
                                        .arg(list->viewport()->visibleRegion().boundingRect().y())
                                        .arg(list->viewport()->visibleRegion().boundingRect().width())
                                        .arg(list->viewport()->visibleRegion().boundingRect().height())
                                        .arg(point.x()).arg(point.y()).arg(scroll->maximum())
                                        .arg(list->hasFocus()).arg(w.devicePixelRatioF())));
            const QImage image = list->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
            const QColor accent = list->palette().color(QPalette::Highlight);
            const qreal imageRatio = image.devicePixelRatio();
            const QRect visibleRect = list->viewport()->visibleRegion().boundingRect();
            const QRect visiblePixels = QRectF(QPointF(visibleRect.topLeft()) * imageRatio,
                                              QSizeF(visibleRect.size()) * imageRatio)
                                            .toAlignedRect().intersected(image.rect());
            for (int y = visiblePixels.top(); y <= visiblePixels.bottom(); ++y) {
                int matching = 0;
                for (int x = visiblePixels.left(); x <= visiblePixels.right(); ++x)
                    matching += image.pixelColor(x, y).rgb() == accent.rgb();
                if (matching > visiblePixels.width() * .8) {
                    marker = true;
                    break;
                }
            }
            auto *line = list->findChild<QWidget *>("layerDropIndicator");
            QVERIFY(line && line->isVisible());
            const QPoint inWindow = line->mapTo(&w, line->rect().center());
            const QImage windowImage = w.grab().toImage();
            const qreal ratio = w.devicePixelRatioF();
            QCOMPARE(windowImage.pixelColor(qRound(inWindow.x() * ratio),
                                            qRound(inWindow.y() * ratio)).rgb(), accent.rgb());
            const QString directory = qEnvironmentVariable("PHOTODECK_LAYER_DRAG_SHOTS");
            if (!directory.isEmpty()) {
                QDir().mkpath(directory);
                const QString file = directory + "/layer-drag-" + QTest::currentDataTag() + "-" +
                                     QString::number(w.devicePixelRatioF()) + "x.png";
                QVERIFY(w.grab().save(file));
            }
        });
        QVERIFY(result.started && result.released);
        QVERIFY(marker);
        QCOMPARE(d->historyLabels().size(), history + 1);
        QCOMPARE(d->state.layers[d->state.active].name, QString("Layer 19"));
        QVERIFY(d->state.active < 19);
        const QString directory = qEnvironmentVariable("PHOTODECK_LAYER_DRAG_SHOTS");
        if (!directory.isEmpty()) {
            const QString file = directory + "/layer-drag-" + QTest::currentDataTag() + "-" +
                                 QString::number(w.devicePixelRatioF()) + "x-after.png";
            QVERIFY(w.grab().save(file));
        }
    }
    void autoscrollCallbacksCannotResurrectOrUseDeletedSource_data() {
        QTest::addColumn<QString>("change");
        for (const QString &value : {QString("window-delete"), QString("mutation"), QString("focus")})
            QTest::newRow(qPrintable(value)) << value;
    }
    void autoscrollCallbacksCannotResurrectOrUseDeletedSource() {
        QFETCH(QString, change);
        auto window = std::make_unique<MainWindow>(false, false);
        QPointer<Document> document = dragFixture(*window);
        for (int i = 4; i < 20; ++i)
            document->addLayer(QString("Layer %1").arg(i));
        document->markSaved();
        settleLayerRows(*window);
        QPointer<QListWidget> list = window->findChild<QListWidget *>("layers");
        QPointer<QWidget> viewport = list->viewport();
        const QPoint start = rowDragPoint(list, 0);
        QTest::mousePress(viewport, Qt::LeftButton, {}, start);
        bool changed = false;
        connect(list->verticalScrollBar(), &QScrollBar::valueChanged, qApp, [&](int) {
            if (changed)
                return;
            changed = true;
            if (change == "window-delete")
                window.reset();
            else if (change == "mutation")
                document->addLayer("Intervening layer");
            else
                window->currentCanvas()->setFocus();
        });
        const QRect visible = viewport->visibleRegion().boundingRect();
        const QPoint edge(visible.center().x(), visible.bottom() - 3);
        QMouseEvent move(QEvent::MouseMove, edge, viewport->mapToGlobal(edge),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        QTRY_VERIFY(changed);
        if (change == "window-delete") {
            QVERIFY(!window && !document && !list && !viewport);
            QWidget target;
            QTest::mouseRelease(&target, Qt::LeftButton);
            return;
        }
        QVERIFY(!list->findChild<QWidget *>("layerDropIndicator")->isVisible());
        const auto revision = document->revision();
        const auto names = layerNames(*document);
        QTest::mouseRelease(viewport, Qt::LeftButton, {}, edge);
        QCOMPARE(document->revision(), revision);
        QCOMPARE(layerNames(*document), names);
    }
    void repeatedEdgeMovesDoNotStarveAutoscroll_data() {
        QTest::addColumn<int>("scrollMode");
        QTest::newRow("per-item") << int(QAbstractItemView::ScrollPerItem);
        QTest::newRow("per-pixel") << int(QAbstractItemView::ScrollPerPixel);
    }
    void repeatedEdgeMovesDoNotStarveAutoscroll() {
        QFETCH(int, scrollMode);
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        w.applyUiPreset("touch", false);
        for (int i = 4; i < 20; ++i)
            d->addLayer(QString("Layer %1").arg(i));
        d->markSaved();
        w.resize(900, 620);
        settleLayerRows(w);
        auto *list = w.findChild<QListWidget *>("layers");
        list->setVerticalScrollMode(QAbstractItemView::ScrollMode(scrollMode));
        auto *scroll = list->verticalScrollBar();
        QVERIFY(scroll->maximum() > 0);
        scroll->setValue(0);
        const auto revision = d->revision();
        const auto names = layerNames(*d);
        const int history = d->historyLabels().size();
        const QPoint start = rowDragPoint(list, 0);
        QTest::mousePress(list->viewport(), Qt::LeftButton, {}, start);
        const auto move = [list](QPoint point) {
            QMouseEvent event(QEvent::MouseMove, point, list->viewport()->mapToGlobal(point),
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(list->viewport(), &event);
        };
        move(start + QPoint(QApplication::startDragDistance() + 4, 0));
        QVERIFY(list->findChild<QWidget *>("layerDropIndicator")->isVisible());
        QTimer repeatedMoves;
        repeatedMoves.setTimerType(Qt::PreciseTimer);
        repeatedMoves.setInterval(5);
        int moves = 0;
        QPoint edge;
        connect(&repeatedMoves, &QTimer::timeout, list, [&] {
            const QRect visible = list->viewport()->visibleRegion().boundingRect();
            edge = {visible.center().x(), visible.bottom() - 3};
            move(edge);
            ++moves;
        });
        // Ordinary pointer updates arrive faster than the 70 ms scroll tick.
        // Keep sending them until progress; a reset-on-move timer never fires.
        repeatedMoves.start();
        const bool progressed = QTest::qWaitFor([&] { return scroll->value() > 0 && moves >= 5; }, 1500);
        repeatedMoves.stop();
        QTest::keyClick(list, Qt::Key_Escape);
        QTest::mouseRelease(list->viewport(), Qt::LeftButton, {}, edge);
        QVERIFY(moves >= 5);
        QVERIFY2(progressed, "Continuous edge pointer moves must not postpone the autoscroll tick");
        QCOMPARE(d->revision(), revision);
        QCOMPARE(layerNames(*d), names);
        QCOMPARE(d->historyLabels().size(), history);
        QVERIFY(!d->dirty);
    }
    void narrowTouchAutoscrollReachesBothStackExtremes() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        w.applyUiPreset("touch", false);
        for (int i = 4; i < 20; ++i)
            d->addLayer(QString("Layer %1").arg(i));
        d->markSaved();
        w.resize(900, 620);
        settleLayerRows(w);
        auto *list = w.findChild<QListWidget *>("layers");
        auto *scroll = list->verticalScrollBar();
        const int history = d->historyLabels().size();
        const auto move = [list](QPoint point) {
            QMouseEvent event(QEvent::MouseMove, point, list->viewport()->mapToGlobal(point),
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(list->viewport(), &event);
        };
        const auto shot = [&](const QString &name) {
            const QString directory = qEnvironmentVariable("PHOTODECK_LAYER_DRAG_SHOTS");
            if (!directory.isEmpty()) {
                QDir().mkpath(directory);
                QVERIFY(w.grab().save(directory + "/layer-drag-touch-extreme-" + name + "-" +
                                     QString::number(w.devicePixelRatioF()) + "x.png"));
            }
        };
        for (int direction : {1, -1}) {
            const int source = direction > 0 ? 0 : list->count() - 1;
            const QPoint start = rowDragPoint(list, source);
            QVERIFY(list->viewport()->visibleRegion().contains(start));
            QTest::mousePress(list->viewport(), Qt::LeftButton, {}, start);
            move(start + QPoint(QApplication::startDragDistance() + 4, 0));
            QVERIFY(list->findChild<QWidget *>("layerDropIndicator")->isVisible());
            bool reached = false;
            QPoint drop;
            for (int attempt = 0; attempt < 100; ++attempt) {
                const QRect visible = list->viewport()->visibleRegion().boundingRect();
                const QRect boundary = list->visualItemRect(list->item(direction > 0 ? list->count() - 1 : 0));
                drop = {visible.center().x(), direction > 0 ? boundary.bottom() + 1 : boundary.top() - 1};
                reached = scroll->value() == (direction > 0 ? scroll->maximum() : scroll->minimum()) &&
                          list->viewport()->visibleRegion().contains(drop);
                if (reached)
                    break;
                const QPoint edge(visible.center().x(), direction > 0 ? visible.bottom() - 2 : visible.top() + 2);
                move(edge);
                QTest::qWait(80);
            }
            QString diagnostics;
            QDebug debug(&diagnostics);
            debug << (direction > 0 ? "Last row and below-last slot must be visible at inner maximum"
                                   : "First row and above-first slot must be visible at inner minimum")
                  << "inner" << scroll->value() << scroll->minimum() << scroll->maximum()
                  << "step" << scroll->singleStep()
                  << "viewport" << list->viewport()->rect()
                  << "visible" << list->viewport()->visibleRegion()
                  << "boundary" << list->visualItemRect(list->item(direction > 0 ? list->count() - 1 : 0))
                  << "drop" << drop << "marker" << list->findChild<QWidget *>("layerDropIndicator")->isVisible();
            for (QWidget *parent = list->parentWidget(); parent; parent = parent->parentWidget())
                if (auto *outer = qobject_cast<QScrollArea *>(parent))
                    debug << "outer" << outer->objectName() << outer->verticalScrollBar()->value()
                          << outer->verticalScrollBar()->minimum() << outer->verticalScrollBar()->maximum();
            QVERIFY2(reached, qPrintable(diagnostics));
            move(drop);
            auto *line = list->findChild<QWidget *>("layerDropIndicator");
            QVERIFY(line->isVisible());
            const QImage image = w.grab().toImage();
            const QPoint inWindow = line->mapTo(&w, line->rect().center());
            const qreal ratio = w.devicePixelRatioF();
            QCOMPARE(image.pixelColor(qRound(inWindow.x() * ratio), qRound(inWindow.y() * ratio)).rgb(),
                     list->palette().color(QPalette::Highlight).rgb());
            shot(direction > 0 ? "bottom-marker" : "top-marker");
            QTest::mouseRelease(list->viewport(), Qt::LeftButton, {}, drop);
            QCOMPARE(d->state.active, direction > 0 ? 0 : 19);
            QCOMPARE(d->state.layers[d->state.active].name, QString("Layer 19"));
            QCOMPARE(d->historyLabels().size(), history + (direction > 0 ? 1 : 2));
            settleLayerRows(w);
            shot(direction > 0 ? "bottom-after" : "top-after");
        }
    }
    void clippedTouchDropIsRejected() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        w.applyUiPreset("touch", false);
        for (int i = 4; i < 20; ++i)
            d->addLayer(QString("Layer %1").arg(i));
        d->markSaved();
        w.resize(900, 620);
        settleLayerRows(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const auto revision = d->revision();
        const auto names = layerNames(*d);
        const QRect visible = list->viewport()->visibleRegion().boundingRect();
        QVERIFY(visible.bottom() < list->viewport()->rect().bottom());
        const QPoint hidden(visible.center().x(), visible.bottom() + 2);
        const auto result = dragLayer(list, rowDragPoint(list, 0), hidden);
        QVERIFY(result.started && !result.entered);
        QCOMPARE(d->revision(), revision);
        QCOMPARE(layerNames(*d), names);
        QVERIFY(!d->dirty);
    }
    void belowThresholdClickAndOutsideReleaseDoNotReorder() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const auto names = layerNames(*d);
        const auto revision = d->revision();
        const QPoint click = rowDragPoint(list, 1);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, click);
        QCOMPARE(d->state.active, 2);
        QCOMPARE(layerNames(*d), names);
        QCOMPARE(d->revision(), revision);
        QVERIFY(!list->findChild<QWidget *>("layerDropIndicator")->isVisible());
        const auto result = dragLayer(list, click, {-10, -10});
        QVERIFY(result.started && !result.entered);
        QCOMPARE(layerNames(*d), names);
        QCOMPARE(d->revision(), revision);
        QVERIFY(!d->dirty);
    }
    void leavingAndReenteringListKeepsSource() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const auto result = dragLayer(list, rowDragPoint(list, 0), rowDragPoint(list, 2), [&] {
            QMouseEvent outside(QEvent::MouseMove, QPoint(-10, -10),
                                list->viewport()->mapToGlobal(QPoint(-10, -10)),
                                Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(list->viewport(), &outside);
            QVERIFY(!list->findChild<QWidget *>("layerDropIndicator")->isVisible());
        });
        QVERIFY(result.started && result.entered);
        QCOMPARE(d->state.active, 2);
        QCOMPARE(d->state.layers[2].name, QString("Top"));
    }
    void focusLossAndLostMouseGrabCancelGesture_data() {
        QTest::addColumn<bool>("focus");
        QTest::newRow("focus-loss") << true;
        QTest::newRow("lost-grab") << false;
    }
    void focusLossAndLostMouseGrabCancelGesture() {
        QFETCH(bool, focus);
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const auto names = layerNames(*d);
        const auto revision = d->revision();
        const auto result = dragLayer(list, rowDragPoint(list, 0), rowDragPoint(list, 2), [&] {
            if (focus)
                w.currentCanvas()->setFocus();
            else {
                QEvent lost(QEvent::UngrabMouse);
                QApplication::sendEvent(list->viewport(), &lost);
            }
        });
        QVERIFY(result.started && !result.entered);
        QCOMPARE(layerNames(*d), names);
        QCOMPARE(d->revision(), revision);
        QVERIFY(!d->dirty);
    }
    void checkboxClickStillTogglesOneHistoryStep() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const auto names = layerNames(*d);
        const int history = d->historyLabels().size();
        const QPoint checkbox = checkboxPoint(list, 0);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, checkbox);
        QVERIFY(!d->state.layers[3].visible);
        QCOMPARE(layerNames(*d), names);
        QCOMPARE(d->historyLabels().size(), history + 1);
        d->undo();
        QVERIFY(d->state.layers[3].visible);
    }
    void pressSettlementCannotRetargetOrUseDestroyedWindow_data() {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("change");
        for (const auto &kind : {QString("rename"), QString("transform")})
            for (const auto &change : {QString("delete-window"), QString("switch-tab"),
                                      QString("replace-layer")})
                QTest::newRow(qPrintable(kind + "-" + change)) << kind << change;
    }
    void pressSettlementCannotRetargetOrUseDestroyedWindow() {
        QFETCH(QString, kind);
        QFETCH(QString, change);
        auto window = std::make_unique<MainWindow>(false, false);
        QPointer<Document> document = dragFixture(*window);
        QPointer<Canvas> canvas = window->currentCanvas();
        auto *other = new Document;
        other->create({20, 10});
        window->addDocument(other);
        auto *tabs = window->findChild<QTabWidget *>("documents");
        tabs->setCurrentWidget(canvas);
        settleLayerRows(*window);
        QPointer<QListWidget> list = window->findChild<QListWidget *>("layers");
        QPointer<QWidget> viewport = list->viewport();
        if (kind == "rename") {
            list->setFocus();
            QTest::keyClick(list, Qt::Key_F2);
            QTRY_VERIFY(nameEditor(*window));
            nameEditor(*window)->setText("Settled name");
        } else {
            QVERIFY(canvas->beginTransform());
            QTest::keyClick(canvas, Qt::Key_Right);
        }
        bool changed = false;
        QObject::connect(document, &Document::updated, qApp, [&] {
            if (changed)
                return;
            changed = true;
            if (change == "delete-window") {
                window.reset();
            } else if (change == "switch-tab") {
                tabs->setCurrentIndex(1);
            } else {
                document->deleteLayer();
                document->addLayer("Replacement");
            }
        });
        const QPoint point = rowDragPoint(list, 0);
        // QTest marks this press spontaneous, exercising Qt's pre-dispatch
        // mouse focus transfer before the list receives its own event.
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, point);
        if (kind == "transform") {
            QVERIFY(!changed); // Plain active-row clicks keep the existing preview.
            const QPoint threshold = point + QPoint(QApplication::startDragDistance() + 4, 0);
            QMouseEvent move(QEvent::MouseMove, threshold, viewport->mapToGlobal(threshold),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &move);
        }
        QVERIFY(changed);
        if (change == "delete-window") {
            QVERIFY(!window && !canvas && !document && !list);
            QWidget releaseTarget;
            QTest::mouseRelease(&releaseTarget, Qt::LeftButton);
            return;
        }
        const auto revision = window->currentDocument()->revision();
        const auto names = layerNames(*window->currentDocument());
        const QPoint moved = point + QPoint(QApplication::startDragDistance() + 4, 0);
        QMouseEvent move(QEvent::MouseMove, moved, viewport->mapToGlobal(moved),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        QVERIFY(!list->findChild<QWidget *>("layerDropIndicator")->isVisible());
        QCOMPARE(window->currentDocument()->revision(), revision);
        QCOMPARE(layerNames(*window->currentDocument()), names);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, moved);
        QCOMPARE(window->currentDocument()->revision(), revision);
        QCOMPARE(layerNames(*window->currentDocument()), names);
    }
    void editorWidgetBaseTeardownDoesNotAccessDerivedEditor() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        list->setFocus();
        QTest::keyClick(list, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        QPointer<QWidget> line = nameEditor(w);
        nameEditor(w)->setText("Uncommitted name");
        const auto revision = d->revision();
        EditorTeardownProbe probe;
        line->installEventFilter(&probe);
        // QWidget emits Destroy after the QLineEdit/custom editor destructors,
        // while the delegate's installed event filter is still attached.
        delete line.data();
        QVERIFY(probe.sawWidgetBase);
        QVERIFY(!line);
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->state.layers[3].name, QString("Top"));
        QTest::keyClick(list, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        QTest::keyClick(nameEditor(w), Qt::Key_Escape);
    }
    void cancelledNameSettlementCannotLeakCheckboxRelease() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, rowDragPoint(list, 0));
        QTest::keyClick(list, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        nameEditor(w)->setText("Settled name");
        bool cancelled = false;
        connect(d, &Document::updated, &w, [&] {
            if (!cancelled) {
                cancelled = true;
                w.currentCanvas()->setFocus();
            }
        });
        const int history = d->historyLabels().size();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, checkboxPoint(list, 0));
        QVERIFY(cancelled);
        QCOMPARE(d->state.layers[3].name, QString("Settled name"));
        QVERIFY(d->state.layers[3].visible);
        QCOMPARE(d->historyLabels().size(), history + 1);
    }
    void thresholdSettlementCannotResurrectCancelledInput_data() {
        QTest::addColumn<QString>("cancel");
        for (const QString &value : {QString("focus"), QString("hide"), QString("ungrab"), QString("escape")})
            QTest::newRow(qPrintable(value)) << value;
    }
    void thresholdSettlementCannotResurrectCancelledInput() {
        QFETCH(QString, cancel);
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *canvas = w.currentCanvas();
        auto *list = w.findChild<QListWidget *>("layers");
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        bool cancelled = false;
        connect(d, &Document::updated, &w, [&] {
            if (cancelled)
                return;
            cancelled = true;
            if (cancel == "focus")
                canvas->setFocus();
            else if (cancel == "hide")
                list->hide();
            else if (cancel == "ungrab") {
                QEvent event(QEvent::UngrabMouse);
                QApplication::sendEvent(list->viewport(), &event);
            } else
                QTest::keyClick(list, Qt::Key_Escape);
        });
        const auto names = layerNames(*d);
        const int history = d->historyLabels().size();
        const auto result = dragLayer(list, rowDragPoint(list, 0), rowDragPoint(list, 2));
        QVERIFY(cancelled);
        QVERIFY(!result.started && !result.entered);
        QCOMPARE(layerNames(*d), names);
        QCOMPARE(d->historyLabels().size(), history + 1); // The preview alone settled.
    }
    void mouseFocusWithoutRowPressSettlesNameThroughFallback() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        list->setFocus();
        QTest::keyClick(list, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        nameEditor(w)->setText("Fallback name");
        const int history = d->historyLabels().size();
        list->setFocus(Qt::MouseFocusReason); // Includes scrollbar paths with no row press.
        QTRY_COMPARE(d->state.layers[3].name, QString("Fallback name"));
        QTRY_VERIFY(!nameEditor(w));
        QCOMPARE(d->historyLabels().size(), history + 1);
        QVERIFY(!list->findChild<QWidget *>("layerDropIndicator")->isVisible());
    }
    void staleDisplayedRowDoesNotStartDrag() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const QPoint from = rowDragPoint(list, 0), to = rowDragPoint(list, 2);
        d->addLayer("New top before refresh");
        const auto revision = d->revision();
        const auto names = layerNames(*d);
        const auto result = dragLayer(list, from, to);
        QVERIFY(!result.started);
        QCOMPARE(d->revision(), revision);
        QCOMPARE(layerNames(*d), names);
    }
    void rejectedStaleCheckboxPressCannotLeakItsRelease() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const QPoint row = rowDragPoint(list, 0), checkbox = checkboxPoint(list, 0);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, row);
        QVERIFY(d->reorderLayer(3, 1)); // Do not allow the delayed list refresh yet.
        const auto revision = d->revision();
        const auto names = layerNames(*d);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, checkbox);
        QCOMPARE(d->revision(), revision);
        QCOMPARE(layerNames(*d), names);
        for (const auto &layer : d->state.layers)
            QVERIFY(layer.visible);
    }
    void sourceMutationBeforeThresholdCannotRetargetDrag() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const QPoint from = rowDragPoint(list, 0);
        QTest::mousePress(list->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        d->addLayer("Inserted before threshold");
        const auto revision = d->revision();
        const auto names = layerNames(*d);
        const QPoint point = from + QPoint(QApplication::startDragDistance() + 4, 0);
        QMouseEvent move(QEvent::MouseMove, point, list->viewport()->mapToGlobal(point),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &move);
        QTest::mouseRelease(list->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(layerNames(*d), names);
        QCOMPARE(d->revision(), revision);
    }
    void dragRejectsChangesBeforeRelease_data() {
        QTest::addColumn<QString>("change");
        QTest::newRow("commit") << QString("commit");
        QTest::newRow("tab-return") << QString("tab-return");
        QTest::newRow("selection-preview") << QString("selection-preview");
        QTest::newRow("source-close") << QString("source-close");
    }
    void dragRejectsChangesBeforeRelease() {
        QFETCH(QString, change);
        MainWindow w(false, false);
        QPointer<Document> d = dragFixture(w);
        auto *canvas = w.currentCanvas();
        auto *list = w.findChild<QListWidget *>("layers");
        auto *tabs = w.findChild<QTabWidget *>("documents");
        auto *other = new Document;
        other->create({20, 10});
        w.addDocument(other);
        tabs->setCurrentWidget(canvas);
        settleLayerRows(w);
        QStringList expected;
        quint64 revision = 0;
        const auto result = dragLayer(list, rowDragPoint(list, 0), rowDragPoint(list, 2), {}, true, [&] {
            if (change == "commit") {
                d->beginEdit();
                d->state.layers[3].opacity = .6;
                d->commit("Nested change after enter");
            } else if (change == "tab-return") {
                tabs->setCurrentIndex(1);
                tabs->setCurrentWidget(canvas);
            } else if (change == "source-close") {
                w.findChild<QAction *>("close")->trigger();
                QVERIFY(!d);
            } else {
                canvas->setTool(Canvas::Select);
                const QPoint point = canvas->toWidget({2, 2}).toPoint();
                QMouseEvent press(QEvent::MouseButtonPress, point, canvas->mapToGlobal(point),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &press);
                QVERIFY(canvas->hasPendingGesture());
            }
            if (d) {
                expected = layerNames(*d);
                revision = d->revision();
            }
        });
        QVERIFY(result.started && result.entered && result.moved);
        if (d) {
            QCOMPARE(layerNames(*d), expected);
            QCOMPARE(d->revision(), revision);
            if (canvas->hasPendingGesture())
                QTest::keyClick(canvas, Qt::Key_Escape);
        }
        QVERIFY(!other->dirty);
    }
    void dragRejectsInterveningMutation_data() {
        QTest::addColumn<QString>("change");
        for (const char *change : {"commit", "undo-redo", "delete", "add", "direct-swap",
                                   "pending-edit", "active", "mask", "transform", "selection-preview", "crop-preview", "lasso-preview", "tab-return"})
            QTest::newRow(change) << QString(change);
    }
    void dragRejectsInterveningMutation() {
        QFETCH(QString, change);
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        auto *canvas = w.currentCanvas();
        auto *tabs = w.findChild<QTabWidget *>("documents");
        Document *other = nullptr;
        if (change == "tab-return") {
            other = new Document;
            other->create({30, 20});
            w.addDocument(other);
            tabs->setCurrentWidget(canvas);
            settleLayerRows(w);
        }
        QStringList expected;
        quint64 revision = 0;
        const auto result = dragLayer(list, rowDragPoint(list, 0), rowDragPoint(list, 2), [&] {
            if (change == "commit") {
                d->beginEdit();
                d->state.layers[3].name = "Changed";
                d->commit("Nested change");
            } else if (change == "undo-redo") {
                d->undo();
                d->redo();
            } else if (change == "delete") {
                d->deleteLayer();
            } else if (change == "add") {
                d->addLayer("Added");
            } else if (change == "direct-swap") {
                d->state.layers.swapItemsAt(1, 2);
            } else if (change == "pending-edit") {
                d->beginEdit();
                d->state.layers[3].offset = {4, 5};
            } else if (change == "active") {
                d->state.active = 1;
            } else if (change == "mask") {
                d->editingMask = true;
            } else if (change == "transform") {
                QVERIFY(canvas->beginTransform());
            } else if (change.endsWith("-preview")) {
                canvas->setTool(change == "crop-preview" ? Canvas::Crop
                                : change == "lasso-preview" ? Canvas::PolygonLasso : Canvas::Select);
                const QPoint point = canvas->toWidget({2, 2}).toPoint();
                QMouseEvent press(QEvent::MouseButtonPress, point, canvas->mapToGlobal(point),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &press);
                QVERIFY(canvas->hasPendingGesture());
                QVERIFY(!d->isEditing());
            } else if (change == "tab-return") {
                tabs->setCurrentIndex(1);
                tabs->setCurrentWidget(canvas);
            }
            expected = layerNames(*d);
            revision = d->revision();
        });
        QVERIFY(result.started);
        QVERIFY(!result.entered && !result.moved);
        QCOMPARE(layerNames(*d), expected);
        QCOMPARE(d->revision(), revision);
        if (d->isEditing())
            d->cancelEdit();
        if (canvas->isTransforming())
            canvas->cancelTransform();
        else if (canvas->hasPendingGesture())
            QTest::keyClick(canvas, Qt::Key_Escape);
    }
    void layerDropRejectsExternalWithoutActiveSession() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const auto names = layerNames(*d);
        const auto revision = d->revision();
        QMimeData mime;
        mime.setData("application/x-photodeck-layer-reorder", "forged");
        const QPoint point = rowDragPoint(list, 1);
        QDragEnterEvent enter(point, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &enter);
        QVERIFY(!enter.isAccepted());
        QDropEvent drop(point, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &drop);
        QVERIFY(!drop.isAccepted());
        QCOMPARE(layerNames(*d), names);
        QCOMPARE(d->revision(), revision);
        QVERIFY(!d->dirty);
    }
    void dragRejectsExternalAndCrossWindowInput() {
        MainWindow w(false, false), other(false, false);
        auto *d = dragFixture(w);
        auto *foreign = dragFixture(other);
        auto *list = w.findChild<QListWidget *>("layers");
        auto *otherList = other.findChild<QListWidget *>("layers");
        const auto original = layerNames(*d), foreignOriginal = layerNames(*foreign);
        QMimeData external;
        external.setData("application/x-photodeck-layer-reorder", "forged");
        const QPoint point = rowDragPoint(list, 2);
        for (const auto action : {Qt::MoveAction, Qt::CopyAction}) {
            QDragEnterEvent enter(point, action, &external, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(list->viewport(), &enter);
            QVERIFY(!enter.isAccepted());
        }
        // Another list receives a move/release without the original press. It
        // cannot become the recipient of this list's gesture or layer payload.
        const QPoint from = rowDragPoint(list, 0), to = rowDragPoint(otherList, 1);
        QTest::mousePress(list->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QMouseEvent move(QEvent::MouseMove, to, otherList->viewport()->mapToGlobal(to),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(otherList->viewport(), &move);
        QTest::mouseRelease(otherList->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QTest::keyClick(list, Qt::Key_Escape);
        QCOMPARE(layerNames(*d), original);
        QCOMPARE(layerNames(*foreign), foreignOriginal);
    }
    void dragSettlesInlineNameAndTransformBeforeSourceCapture() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        list->setFocus();
        QTest::keyClick(list, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        QTest::keyClicks(nameEditor(w), "Renamed top");
        const int history = d->historyLabels().size();
        auto result = dragLayer(list, rowDragPoint(list, 0), rowDragPoint(list, 2));
        QVERIFY(result.started && result.released);
        QVERIFY(!nameEditor(w));
        QCOMPARE(d->state.layers[2].name, QString("Renamed top"));
        QCOMPARE(d->historyLabels().size(), history + 2);
        d->undo();
        QCOMPARE(d->state.layers[3].name, QString("Renamed top"));
        d->undo();
        QCOMPARE(d->state.layers[3].name, QString("Top"));
        settleLayerRows(w);
        auto *canvas = w.currentCanvas();
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        const int transformHistory = d->historyLabels().size();
        result = dragLayer(list, rowDragPoint(list, 0), rowDragPoint(list, 2));
        QVERIFY(result.started && result.released);
        QVERIFY(!canvas->isTransforming());
        QCOMPARE(d->state.layers[2].geometry, QTransform::fromTranslate(1, 0));
        QCOMPARE(d->historyLabels().size(), transformHistory + 2);
    }
    void checkboxDragAndInlineEditorCannotStartReorder() {
        MainWindow w(false, false);
        auto *d = dragFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const auto names = layerNames(*d);
        const QPoint checkbox = checkboxPoint(list, 0);
        auto result = dragLayer(list, checkbox, rowDragPoint(list, 2));
        QVERIFY(!result.started);
        QCOMPARE(layerNames(*d), names);
        list->setFocus();
        QTest::keyClick(list, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        auto *editor = nameEditor(w);
        QTest::mousePress(editor, Qt::LeftButton, Qt::NoModifier, editor->rect().center());
        QTest::mouseMove(editor, editor->rect().center() + QPoint(30, 0));
        QTest::mouseRelease(editor, Qt::LeftButton, Qt::NoModifier, editor->rect().center() + QPoint(30, 0));
        QVERIFY(!list->findChild<QWidget *>("layerDropIndicator")->isVisible());
        QCOMPARE(layerNames(*d), names);
        QTest::keyClick(editor, Qt::Key_Escape);
        list->setFocus();
        const int previous = d->state.active;
        QTest::keyClick(list, Qt::Key_Down);
        QCOMPARE(d->state.active, qMax(0, previous - 1));
    }
};
QTEST_MAIN(LayerDragTests)
#include "test_layer_drag.moc"
