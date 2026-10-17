#include "mainwindow.h"
#include <QAction>
#include <QDir>
#include <QImageReader>
#include <QLineEdit>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

namespace {
void paint(Document &d, QColor color, const QString &label) {
    d.beginEdit();
    d.state.layers[0].pixels.fill(color);
    d.commit(label);
}
QVector<quint64> revisions(const Document &d) {
    QVector<quint64> ids;
    for (const auto &entry : d.historyDescriptions())
        ids.append(entry.revision);
    return ids;
}
QListWidget *history(MainWindow &w) {
    return w.findChild<QListWidget *>("history");
}
int rowFor(MainWindow &w, quint64 revision) {
    auto *list = history(w);
    for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->data(Qt::UserRole).toULongLong() == revision)
            return row;
    return -1;
}
void clickRow(MainWindow &w, int row) {
    auto *list = history(w);
    list->scrollToItem(list->item(row));
    const QPoint point = list->visualItemRect(list->item(row)).center();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, point, 0);
}
Document *fixture(MainWindow &w) {
    auto *d = new Document;
    if (!d->create({80, 60}, true))
        return nullptr;
    paint(*d, Qt::red, "Kırmızı boya");
    paint(*d, Qt::blue, "Mavi boya");
    d->markSaved();
    w.applyUiPreset("modular", false);
    w.addDocument(d);
    w.show();
    w.activateWindow();
    QTest::qWait(40);
    return d;
}
QLineEdit *nameEditor(MainWindow &w) {
    for (auto *editor : w.findChildren<QLineEdit *>("layerNameEditor"))
        if (editor->isVisible())
            return editor;
    return nullptr;
}
void screenshot(MainWindow &w, const QString &name) {
    const QString directory = qEnvironmentVariable("PHOTODECK_HISTORY_SCREENSHOT_DIR");
    if (!directory.isEmpty()) {
        QDir().mkpath(directory);
        w.grab().save(directory + "/" + name + ".png");
    }
}
} // namespace

class HistoryNavigationTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName("PhotoDeckHistoryNavigationTests");
        QApplication::setOrganizationName("PhotoDeckTests");
        QImageReader::setAllocationLimit(128);
    }
    void descriptionsIncludeRedoAndRemainCopies() {
        Document empty;
        QCOMPARE(empty.currentHistoryIndex(), -1);
        QVERIFY(empty.historyDescriptions().isEmpty());
        QVERIFY(!empty.jumpToHistoryRevision(0));
        Document d;
        QVERIFY(d.create({8, 8}));
        paint(d, Qt::red, "Red");
        paint(d, Qt::blue, "Blue");
        const auto ids = revisions(d);
        d.undo();
        QCOMPARE(d.currentHistoryIndex(), 1);
        QCOMPARE(d.historyLabels(), QStringList({"Yeni belge", "Red"}));
        auto descriptions = d.historyDescriptions();
        QCOMPARE(descriptions.size(), 3);
        QCOMPARE(descriptions[2].revision, ids[2]);
        QCOMPARE(descriptions[2].label, QString("Blue"));
        descriptions[0].label = "External metadata change";
        QCOMPARE(d.historyDescriptions()[0].label, QString("Yeni belge"));
        QSignalSpy updates(&d, &Document::updated);
        QVERIFY(d.jumpToHistoryRevision(ids[2]));
        QCOMPARE(updates.count(), 1);
        QCOMPARE(d.composite().pixelColor(2, 2), QColor(Qt::blue));
        QVERIFY(!d.canRedo());
        QVERIFY(d.canUndo());
        QCOMPARE(revisions(d), ids);
    }
    void jumpsRestoreSelectionsChannelsAndCleanContent() {
        Document d;
        QVERIFY(d.create({20, 20}, true));
        const quint64 initial = d.revision();
        const Selection selected(2, 3, 9, 7);
        QVERIFY(d.setSelection(selected));
        const quint64 selectionOnly = d.revision();
        QVERIFY(!d.dirty);
        QVERIFY(d.saveSelection("Subject"));
        const quint64 withChannel = d.revision();
        d.markSaved();
        QVERIFY(d.deselect());
        const quint64 deselected = d.revision();
        QVERIFY(!d.dirty);
        paint(d, Qt::green, "Green");
        const quint64 painted = d.revision();
        QVERIFY(d.dirty);
        d.editingMask = true;
        QSignalSpy updates(&d, &Document::updated);
        QVERIFY(d.jumpToHistoryRevision(selectionOnly));
        QCOMPARE(updates.count(), 1);
        QCOMPARE(d.selection.mask(d.state.size), selected.mask(d.state.size));
        QVERIFY(d.savedSelectionNames().isEmpty());
        QVERIFY(d.dirty); // The saved content includes its named channel.
        QVERIFY(!d.editingMask);
        QVERIFY(d.jumpToHistoryRevision(withChannel));
        QCOMPARE(d.savedSelectionNames(), QStringList({"Subject"}));
        QCOMPARE(d.state.savedSelections[0].selection.mask(d.state.size), selected.mask(d.state.size));
        QVERIFY(!d.dirty);
        QVERIFY(d.jumpToHistoryRevision(deselected));
        QVERIFY(d.selection.isEmpty());
        QVERIFY(d.canReselect());
        QCOMPARE(d.state.previousSelection.mask(d.state.size), selected.mask(d.state.size));
        QVERIFY(!d.dirty); // Selection-only history shares the saved content ID.
        QVERIFY(d.jumpToHistoryRevision(painted));
        QVERIFY(d.dirty);
        QVERIFY(d.jumpToHistoryRevision(initial));
        QVERIFY(d.selection.isEmpty());
        QVERIFY(d.savedSelectionNames().isEmpty());
        QVERIFY(d.dirty);
        QVERIFY(d.jumpToHistoryRevision(withChannel));
        QVERIFY(!d.dirty);
    }
    void invalidAndNoopJumpsAreInertEvenDuringEdit() {
        Document d;
        QVERIFY(d.create({8, 8}, true));
        paint(d, Qt::red, "Red");
        d.markSaved();
        const auto ids = revisions(d);
        d.beginEdit();
        d.state.layers[0].pixels.fill(Qt::blue);
        d.editingMask = true;
        const auto live = d.composite();
        QSignalSpy updates(&d, &Document::updated);
        QVERIFY(!d.jumpToHistoryRevision(0));
        QVERIFY(!d.jumpToHistoryRevision(std::numeric_limits<quint64>::max()));
        QVERIFY(!d.jumpToHistoryRevision(d.revision()));
        QCOMPARE(updates.count(), 0);
        QCOMPARE(d.composite(), live);
        QCOMPARE(revisions(d), ids);
        QVERIFY(d.isEditing());
        QVERIFY(d.editingMask);
        QVERIFY(!d.dirty);
        QVERIFY(d.jumpToHistoryRevision(ids[0]));
        QCOMPARE(updates.count(), 1);
        QVERIFY(!d.isEditing());
        QVERIFY(!d.editingMask);
        QCOMPARE(d.composite().pixelColor(2, 2), QColor(Qt::white));
        d.commit("Must not commit abandoned edit");
        QCOMPARE(updates.count(), 1);
        QCOMPARE(revisions(d), ids);
    }
    void trimmingBranchesAndResetCannotRetargetIds() {
        Document d;
        QVERIFY(d.create({8, 8}));
        const quint64 initial = d.revision();
        for (int i = 0; i < 40; ++i)
            paint(d, QColor(i + 1, 20, 30), QString::number(i));
        const auto entries = d.historyDescriptions();
        QCOMPARE(entries.size(), 30);
        QCOMPARE(d.currentHistoryIndex(), 29);
        const quint64 retained = entries[8].revision, future = entries.last().revision;
        QSignalSpy updates(&d, &Document::updated);
        QVERIFY(!d.jumpToHistoryRevision(initial));
        QCOMPARE(updates.count(), 0);
        QVERIFY(d.jumpToHistoryRevision(retained));
        QCOMPARE(d.currentHistoryIndex(), 8);
        QCOMPARE(d.composite().pixelColor(2, 2), QColor(entries[8].label.toInt() + 1, 20, 30));
        paint(d, Qt::yellow, "Branch");
        const auto branch = revisions(d);
        const auto image = d.composite();
        updates.clear();
        QVERIFY(!d.jumpToHistoryRevision(future));
        QCOMPARE(updates.count(), 0);
        QCOMPARE(revisions(d), branch);
        QCOMPARE(d.composite(), image);
        QVERIFY(!d.canRedo());
        QVERIFY(d.create({8, 8}, true));
        updates.clear();
        QVERIFY(!d.jumpToHistoryRevision(retained));
        QCOMPARE(updates.count(), 0);
        QCOMPARE(d.currentHistoryIndex(), 0);
    }
    void widgetMouseKeyboardRedoAndDirtyMarker() {
        MainWindow w(false, false);
        auto *d = fixture(w);
        QVERIFY(d);
        auto *list = history(w);
        QVERIFY(list->isVisible());
        QCOMPARE(list->count(), 3);
        QCOMPARE(list->currentRow(), 2);
        QVERIFY(list->item(2)->text().contains("Geçerli"));
        const auto ids = revisions(*d);
        clickRow(w, 0);
        QCOMPARE(d->revision(), ids[0]);
        QCOMPARE(d->currentHistoryIndex(), 0);
        QVERIFY(d->canRedo());
        QVERIFY(d->dirty);
        QVERIFY(w.windowTitle().contains('*'));
        QVERIFY(w.findChild<QTabWidget *>("documents")->tabText(0).contains("•"));
        QCOMPARE(list->count(), 3);
        QVERIFY(list->item(2)->text().contains("Yinele"));
        QVERIFY(list->item(2)->flags().testFlag(Qt::ItemIsEnabled));
        QVERIFY(list->item(2)->data(Qt::AccessibleTextRole).toString().contains("Yineleme"));
        QVERIFY(list->hasFocus());
        QTest::keyClick(list, Qt::Key_End);
        QCOMPARE(list->currentRow(), 2);
        QCOMPARE(d->revision(), ids[0]); // Arrows browse; Enter/Space activate.
        QTest::keyClick(list, Qt::Key_Return);
        QCOMPARE(d->revision(), ids[2]);
        QVERIFY(!d->dirty);
        QVERIFY(!w.windowTitle().contains('*'));
        QTest::keyClick(list, Qt::Key_Up);
        QTest::keyClick(list, Qt::Key_Space);
        QCOMPARE(d->revision(), ids[1]);
        QVERIFY(d->dirty && d->canRedo());
        screenshot(w, "history-redo-current");
        QTest::keyClick(list, Qt::Key_Down);
        QTest::keyClick(list, Qt::Key_Enter);
        QCOMPARE(d->revision(), ids[2]);
        QVERIFY(!d->dirty);
        screenshot(w, "history-saved-current");
    }
    void currentWidgetStepIsNoopWithPendingTransform() {
        MainWindow w(false, false);
        auto *d = fixture(w);
        auto *canvas = w.currentCanvas();
        canvas->setFocus();
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        const auto preview = canvas->transformGeometry();
        const auto ids = revisions(*d);
        QSignalSpy updates(d, &Document::updated);
        clickRow(w, 2);
        QCOMPARE(updates.count(), 0);
        QCOMPARE(revisions(*d), ids);
        QVERIFY(canvas->isTransforming());
        QCOMPARE(canvas->transformGeometry(), preview);
        QVERIFY(!d->dirty);
        canvas->cancelTransform();
    }
    void futureRowsRemainReadableInEveryPreset_data() {
        QTest::addColumn<QString>("preset");
        for (const auto &preset : uiPresets())
            QTest::newRow(qPrintable(preset.id)) << preset.id;
    }
    void futureRowsRemainReadableInEveryPreset() {
        QFETCH(QString, preset);
        MainWindow w(false, false);
        auto *d = fixture(w);
        d->undo();
        w.applyUiPreset(preset, false);
        w.resize(900, 620);
        w.findChild<QDockWidget *>("historyDock")->show();
        QTest::qWait(30);
        auto *list = history(w);
        QVERIFY(list->isVisible());
        QCOMPARE(list->count(), 3);
        const auto *future = list->item(2);
        QVERIFY(future->flags().testFlag(Qt::ItemIsEnabled));
        QCOMPARE(future->foreground().color(), list->palette().color(QPalette::Text));
        QVERIFY(future->text().contains("Yinele"));
        QVERIFY(list->item(1)->text().contains("Geçerli"));
        list->setFocus();
        QTest::keyClick(list, Qt::Key_End);
        QTest::keyClick(list, Qt::Key_Return);
        QCOMPARE(d->currentHistoryIndex(), 2);
        QVERIFY(!d->dirty);
        QTest::keyClick(list, Qt::Key_Up);
        QTest::keyClick(list, Qt::Key_Return);
        screenshot(w, "history-min-" + preset);
    }
    void widgetRetainsRowsAndKeyboardBrowseAcrossRefresh() {
        MainWindow w(false, false);
        auto *d = fixture(w);
        auto *list = history(w);
        auto *initialItem = list->item(0);
        list->setFocus();
        QTest::keyClick(list, Qt::Key_Home);
        QCOMPARE(list->currentRow(), 0);
        d->changed();
        QTest::qWait(40);
        QCOMPARE(list->item(0), initialItem);
        QCOMPARE(list->currentRow(), 0);
        QCOMPARE(d->currentHistoryIndex(), 2);
        QVERIFY(list->item(2)->text().contains("Geçerli"));
        QTest::keyClick(list, Qt::Key_Return);
        QCOMPARE(d->currentHistoryIndex(), 0);
    }
    void staleWidgetRowsBeforeRefreshAreRejected() {
        MainWindow w(false, false);
        auto *d = fixture(w);
        auto *list = history(w);
        const quint64 oldFuture = d->revision();
        d->undo();
        paint(*d, Qt::green, "New branch before refresh");
        // No event-loop wait: the old row still describes the removed redo state.
        QCOMPARE(list->item(2)->data(Qt::UserRole).toULongLong(), oldFuture);
        const auto branchRevision = d->revision();
        const auto ids = revisions(*d);
        QSignalSpy updates(d, &Document::updated);
        clickRow(w, 2);
        QCOMPARE(d->revision(), branchRevision);
        QCOMPARE(revisions(*d), ids);
        QCOMPARE(updates.count(), 0);
        QCOMPARE(list->item(2)->data(Qt::UserRole).toULongLong(), branchRevision);
        QVERIFY(!d->canRedo());
    }
    void validWidgetIdsSurviveTrimmingBeforeRefresh() {
        MainWindow w(false, false);
        auto *d = fixture(w);
        for (int i = 0; i < 27; ++i)
            paint(*d, QColor(i + 1, 30, 40), QString("Step %1").arg(i));
        auto *list = history(w);
        QTRY_COMPARE(list->count(), 30);
        const quint64 id = list->item(4)->data(Qt::UserRole).toULongLong();
        paint(*d, Qt::yellow, "Trim before refresh");
        QCOMPARE(list->item(4)->data(Qt::UserRole).toULongLong(), id);
        clickRow(w, 4);
        QCOMPARE(d->revision(), id);
        QCOMPARE(d->currentHistoryIndex(), 3);
        QCOMPARE(d->composite().pixelColor(2, 2), QColor(2, 30, 40));
    }
    void staleTrimmedWidgetRowDoesNotSelectReplacement() {
        MainWindow w(false, false);
        auto *d = fixture(w);
        for (int i = 0; i < 27; ++i)
            paint(*d, QColor(i + 1, 30, 40), QString("Step %1").arg(i));
        auto *list = history(w);
        QTRY_COMPARE(list->count(), 30);
        const quint64 stale = list->item(0)->data(Qt::UserRole).toULongLong();
        paint(*d, Qt::yellow, "Trim");
        const quint64 current = d->revision();
        QCOMPARE(list->item(0)->data(Qt::UserRole).toULongLong(), stale);
        QSignalSpy updates(d, &Document::updated);
        clickRow(w, 0);
        QCOMPARE(d->revision(), current);
        QCOMPARE(updates.count(), 0);
        QCOMPARE(list->count(), 30);
    }
    void widgetCancelsTransformWithoutExtraHistory() {
        MainWindow w(false, false);
        auto *d = fixture(w);
        auto *canvas = w.currentCanvas();
        const auto ids = revisions(*d);
        canvas->setFocus();
        QVERIFY(canvas->beginTransform());
        QTest::keyClick(canvas, Qt::Key_Right);
        QVERIFY(!canvas->transformGeometry().isIdentity());
        QSignalSpy updates(d, &Document::updated);
        clickRow(w, 1);
        QVERIFY(!canvas->isTransforming());
        QCOMPARE(d->revision(), ids[1]);
        QCOMPARE(d->state.layers[0].geometry, QTransform());
        QCOMPARE(revisions(*d), ids);
        QCOMPARE(updates.count(), 1);
        QVERIFY(d->canRedo());
        screenshot(w, "history-transform-cancelled");
    }
    void widgetCancelsInlineRenameBeforeFocusCanCommit() {
        MainWindow w(false, false);
        auto *d = fixture(w);
        const auto ids = revisions(*d);
        const QString originalName = d->state.layers[0].name;
        w.findChild<QAction *>("rename")->trigger();
        QTRY_VERIFY(nameEditor(w));
        nameEditor(w)->setText("Do not commit while navigating");
        QSignalSpy updates(d, &Document::updated);
        clickRow(w, 0);
        QCOMPARE(d->revision(), ids[0]);
        QCOMPARE(d->state.layers[0].name, originalName);
        QCOMPARE(revisions(*d), ids);
        QCOMPARE(updates.count(), 1);
        QTRY_VERIFY(!nameEditor(w));
    }
    void activeSelectionFinishesThenUsesOriginalRevisionId() {
        MainWindow w(false, false);
        auto *d = fixture(w);
        auto *canvas = w.currentCanvas();
        const auto ids = revisions(*d);
        canvas->setTool(Canvas::Select);
        canvas->setFocus();
        const QPoint from = canvas->toWidget({5, 6}).toPoint();
        const QPoint to = canvas->toWidget({35, 32}).toPoint();
        QTest::mousePress(canvas, Qt::LeftButton, {}, from, 0);
        QTest::mouseMove(canvas, to, 0);
        QSignalSpy updates(d, &Document::updated);
        clickRow(w, 1);
        QCOMPARE(d->revision(), ids[1]);
        QVERIFY(d->selection.isEmpty());
        QCOMPARE(d->historyDescriptions().size(), 4);
        QVERIFY(d->canRedo());
        QCOMPARE(updates.count(), 2); // Finish the selection, then one direct jump.
        const auto after = revisions(*d);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, to, 0);
        QCOMPARE(revisions(*d), after);
        QVERIFY(d->jumpToHistoryRevision(after.last()));
        QVERIFY(d->selection.hasPixels());
    }
    void selectionBranchInvalidatesFutureTargetWithoutWrongJump() {
        MainWindow w(false, false);
        auto *d = fixture(w);
        const quint64 removedFuture = d->revision();
        d->undo();
        QTest::qWait(40);
        auto *canvas = w.currentCanvas();
        canvas->setTool(Canvas::Select);
        canvas->setFocus();
        const QPoint from = canvas->toWidget({5, 6}).toPoint();
        const QPoint to = canvas->toWidget({35, 32}).toPoint();
        QTest::mousePress(canvas, Qt::LeftButton, {}, from, 0);
        QTest::mouseMove(canvas, to, 0);
        clickRow(w, rowFor(w, removedFuture));
        QVERIFY(d->revision() != removedFuture);
        QVERIFY(d->selection.hasPixels());
        QVERIFY(!d->canRedo());
        QCOMPARE(d->composite().pixelColor(2, 2), QColor(Qt::red));
        QVERIFY(!d->jumpToHistoryRevision(removedFuture));
        const auto after = revisions(*d);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, to, 0);
        QCOMPARE(revisions(*d), after);
    }
    void tabSwitchesKeepHistoryBoundToDisplayedDocument() {
        MainWindow w(false, false);
        auto *first = fixture(w);
        const auto firstIds = revisions(*first);
        auto *second = new Document;
        QVERIFY(second->create({8, 8}, true));
        paint(*second, Qt::green, "Second green");
        second->markSaved();
        w.addDocument(second);
        auto *tabs = w.findChild<QTabWidget *>("documents");
        QCOMPARE(history(w)->count(), 2);
        clickRow(w, 0);
        QCOMPARE(second->currentHistoryIndex(), 0);
        QCOMPARE(first->revision(), firstIds.last());
        tabs->setCurrentIndex(0);
        QCOMPARE(history(w)->count(), 3);
        clickRow(w, 1);
        QCOMPARE(first->revision(), firstIds[1]);
        QCOMPARE(second->currentHistoryIndex(), 0);
        tabs->setCurrentIndex(1);
        history(w)->setFocus();
        QTest::keyClick(history(w), Qt::Key_End);
        QTest::keyClick(history(w), Qt::Key_Return);
        QCOMPARE(second->currentHistoryIndex(), 1);
        QCOMPARE(first->revision(), firstIds[1]);
        QVERIFY(!second->dirty);
    }
};
QTEST_MAIN(HistoryNavigationTests)
#include "test_history_navigation.moc"
