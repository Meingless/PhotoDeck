#include "blending.h"
#include "mainwindow.h"
#include "transformdialog.h"
#include <QDoubleSpinBox>
#include <QAction>
#include <QContextMenuEvent>
#include <QDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTabBar>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

namespace {
QLineEdit *nameEditor(MainWindow &window) {
    for (auto *line : window.findChildren<QLineEdit *>("layerNameEditor"))
        if (line->isVisible())
            return line;
    return nullptr;
}
Document *layerFixture(MainWindow &window) {
    auto *d = new Document;
    d->create({30, 20}, true);
    d->addLayer("Original");
    d->markSaved();
    window.addDocument(d);
    window.resize(1000, 720);
    window.show();
    QTest::qWait(20);
    return d;
}
// Send the same context event as a platform right-click after its mouse event.
// A timer drives the real, nested QMenu event loop, including Escape cancellation.
bool contextMenu(MainWindow &window, const std::function<void()> &open,
                 const std::function<void(QMenu *)> &inspect) {
    bool seen = false, expired = false;
    QTimer poll, timeout;
    poll.setInterval(5);
    timeout.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        auto *menu = window.findChild<QMenu *>("layerContextMenu");
        if (seen || !menu || !menu->isVisible())
            return;
        seen = true;
        inspect(menu);
        if (menu->isVisible())
            menu->close();
    });
    QObject::connect(&timeout, &QTimer::timeout, [&] {
        expired = true;
        if (auto *menu = window.findChild<QMenu *>("layerContextMenu"))
            menu->close();
    });
    poll.start();
    timeout.start(3000);
    open();
    return seen && !expired;
}
QStringList layerNames(const Document &document) {
    QStringList names;
    for (const auto &layer : document.state.layers)
        names << layer.name;
    return names;
}
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
    void arbitraryReorderPreservesPayloadContextAndUndoIdentity() {
        Document d;
        QVERIFY(d.create({30, 20}, true));
        d.addLayer("Masked");
        QString error;
        QVERIFY(d.addMask(&error));
        QVERIFY(d.setClipping(true));
        d.addLayer("Middle");
        QFont font;
        font.setPixelSize(12);
        QVERIFY(d.addText("Editable", font, Qt::yellow, {4, 3}, &error));
        d.beginEdit();
        auto &masked = d.state.layers[1];
        masked.offset = {-3, 4};
        masked.geometry = QTransform::fromScale(.8, 1.2);
        masked.opacity = .7;
        masked.locked = true;
        masked.visible = false;
        masked.blend = 4;
        masked.brightness = 8;
        masked.contrast = -7;
        masked.saturation = 12;
        masked.blur = 2;
        masked.tones.gamma = 1.2;
        masked.tones.hue = 18;
        masked.filters.gaussian = 1;
        masked.filters.noise = 4;
        masked.filters.noiseSeed = 7;
        masked.maskSettings.density = 75;
        d.commit("Fixture sources");
        QVERIFY(d.setSelection(Selection(QRect(2, 3, 5, 4)), "Fixture selection", &error));
        QVERIFY(d.saveSelection("Saved", &error));
        const State original = d.state;
        // Selecting a different row has no history entry. Undo must remember it.
        d.state.active = 1;
        d.editingMask = true;
        d.markSaved();
        const int history = d.historyLabels().size();
        QSignalSpy updates(&d, &Document::updated);
        QVERIFY(d.reorderLayer(1, 3));
        QCOMPARE(updates.size(), 1);
        QCOMPARE(layerNames(d), (QStringList{"Arka plan", "Middle", "Editable", "Masked"}));
        QCOMPARE(d.state.active, 3);
        QVERIFY(d.editingMask);
        QCOMPARE(d.historyLabels().size(), history + 1);
        QVERIFY(d.dirty);
        const auto &moved = d.state.layers[3];
        QCOMPARE(moved.name, original.layers[1].name);
        QCOMPARE(moved.pixels, original.layers[1].pixels);
        QCOMPARE(moved.mask, original.layers[1].mask);
        QCOMPARE(moved.offset, original.layers[1].offset);
        QCOMPARE(moved.geometry, original.layers[1].geometry);
        QCOMPARE(moved.opacity, original.layers[1].opacity);
        QCOMPARE(moved.brightness, original.layers[1].brightness);
        QCOMPARE(moved.contrast, original.layers[1].contrast);
        QCOMPARE(moved.saturation, original.layers[1].saturation);
        QCOMPARE(moved.blur, original.layers[1].blur);
        QVERIFY(moved.tones == original.layers[1].tones);
        QVERIFY(moved.filters == original.layers[1].filters);
        QCOMPARE(moved.maskSettings.density, original.layers[1].maskSettings.density);
        QVERIFY(moved.locked && !moved.visible && moved.clipped);
        QCOMPARE(moved.blend, original.layers[1].blend);
        QCOMPARE(d.state.layers[2].text, original.layers[3].text);
        QCOMPARE(d.state.layers[2].font, original.layers[3].font);
        QVERIFY(d.selection.equivalent(original.selection, d.state.size));
        QVERIFY(d.state.previousSelection.equivalent(original.previousSelection, d.state.size));
        QCOMPARE(d.savedSelectionNames(), QStringList{"Saved"});
        d.undo();
        QCOMPARE(layerNames(d), (QStringList{"Arka plan", "Masked", "Middle", "Editable"}));
        QCOMPARE(d.state.active, 1);
        QVERIFY(!d.editingMask); // Undo follows the existing mask-editing policy.
        QVERIFY(!d.dirty);
        d.redo();
        QCOMPARE(d.state.active, 3);
        QCOMPARE(d.state.layers[3].name, QString("Masked"));
        QVERIFY(d.selection.equivalent(original.selection, d.state.size));
    }
    void arbitraryReorderBoundariesNoopRedoAndPendingEdit() {
        Document d;
        d.create({30, 20});
        d.addLayer("Middle");
        d.addLayer("Top");
        d.reorderLayer(2, 0);
        d.undo();
        d.markSaved();
        const auto revision = d.revision();
        const auto descriptions = d.historyDescriptions();
        QSignalSpy updates(&d, &Document::updated);
        for (const auto pair : {QPair<int, int>{-1, 0}, {0, -1}, {3, 0}, {0, 3}, {1, 1}})
            QVERIFY(!d.reorderLayer(pair.first, pair.second));
        QCOMPARE(d.revision(), revision);
        QCOMPARE(d.historyDescriptions().size(), descriptions.size());
        QCOMPARE(updates.size(), 0);
        QVERIFY(d.canRedo());
        QVERIFY(!d.dirty);
        d.beginEdit();
        d.state.layers[1].offset = {3, 4};
        QVERIFY(!d.reorderLayer(1, 2));
        QVERIFY(d.isEditing());
        QCOMPARE(d.state.layers[1].offset, QPoint(3, 4));
        QCOMPARE(d.revision(), revision);
        d.cancelEdit();
        d.redo();
        QCOMPARE(layerNames(d), (QStringList{"Top", "Katman 1", "Middle"}));
    }
    void indistinguishableReorderKeepsContentCleanAndRedo() {
        Document d;
        d.create({8, 8});
        d.addLayer("Same");
        d.beginEdit();
        d.state.layers[1] = d.state.layers[0];
        d.commit("Indistinguishable fixture");
        d.addLayer("Future");
        d.undo();
        d.state.active = 0;
        d.markSaved();
        const auto revision = d.revision();
        const auto descriptions = d.historyDescriptions();
        const auto labels = d.historyLabels();
        const QImage composite = d.composite();
        QVERIFY(d.reorderLayer(0, 1));
        QCOMPARE(d.state.active, 1);
        QCOMPARE(d.state.layers[0].name, d.state.layers[1].name);
        QCOMPARE(d.state.layers[0].pixels, d.state.layers[1].pixels);
        QCOMPARE(d.composite(), composite);
        QCOMPARE(d.revision(), revision);
        QCOMPARE(d.historyLabels(), labels);
        QCOMPARE(d.historyDescriptions().size(), descriptions.size());
        QVERIFY(!d.dirty);
        QVERIFY(d.canRedo());
        d.redo();
        QCOMPARE(d.state.layers.size(), 3);
        QCOMPARE(d.state.layers[2].name, QString("Future"));
    }
    void arbitraryReorderKeepsNonmovingActiveAndBottomClipping() {
        Document d;
        stack(d);
        d.addLayer("Top");
        d.setClipping(true);
        d.state.active = 1;
        d.editingMask = false;
        QVERIFY(d.reorderLayer(2, 0));
        QCOMPARE(layerNames(d), (QStringList{"Top", "Katman 1", "Clipped"}));
        QCOMPARE(d.state.active, 2);
        QVERIFY(!d.state.layers.first().clipped);
        QVERIFY(d.state.layers[2].clipped);
        d.undo();
        QVERIFY(d.state.layers[2].clipped);
        QCOMPARE(d.state.active, 1);
        QVERIFY(d.reorderLayer(0, 2));
        QCOMPARE(d.state.active, 0);
        QVERIFY(!d.state.layers.first().clipped);
        d.undo();
        QCOMPARE(d.state.active, 1);
        QVERIFY(d.state.layers[1].clipped);
    }
    void inlineRenamePreservesSourcesAndIsOneUndo() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        QString error;
        QVERIFY(d->addMask(&error));
        QVERIFY(d->setClipping(true));
        d->beginEdit();
        d->state.layers[1].locked = true;
        d->state.layers[1].geometry = QTransform::fromScale(.8, .9);
        d->commit("Fixture lock and geometry");
        d->selection = {1, 2, 5, 6};
        d->markSaved();
        const Layer original = d->state.layers[1];
        const int count = d->historyLabels().size();
        const quint64 revision = d->revision();
        auto *list = w.findChild<QListWidget *>("layers");
        QTest::qWait(40);
        list->setFocus();
        QTest::keyClick(list, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        auto *line = nameEditor(w);
        QCOMPARE(line->text(), QString("Original"));
        QCOMPARE(line->maxLength(), 128);
        QTest::keyClicks(line, " Renamed ");
        // The ordinary delayed document refresh cannot reset partially typed text.
        d->changed();
        QTest::qWait(60);
        QCOMPARE(line->text(), QString(" Renamed "));
        QCOMPARE(d->revision(), revision);
        QTest::keyClick(line, Qt::Key_Return);
        QTRY_COMPARE(d->state.layers[1].name, QString("Renamed"));
        QTRY_VERIFY(!nameEditor(w));
        QCOMPARE(d->historyLabels().size(), count + 1);
        QCOMPARE(d->state.layers[1].pixels, original.pixels);
        QCOMPARE(d->state.layers[1].mask, original.mask);
        QCOMPARE(d->state.layers[1].geometry, original.geometry);
        QVERIFY(d->state.layers[1].locked && d->state.layers[1].clipped);
        QCOMPARE(d->state.active, 1);
        QCOMPARE(d->selection, QRect(1, 2, 5, 6));
        QVERIFY(d->editingMask);
        list->setFocus();
        QTest::keyClick(list, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(d->state.layers[1].name, QString("Original"));
        QCOMPARE(d->historyLabels().size(), count);
        w.findChild<QAction *>("redo")->trigger();
        QCOMPARE(d->state.layers[1].name, QString("Renamed"));
    }
    void inlineRenameThenTransformShortcutCommitsBeforeSessionCapture() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        auto *canvas = w.currentCanvas();
        const int history = d->historyLabels().size();
        QSignalSpy sessions(canvas, &Canvas::transformSessionChanged);
        list->setFocus();
        QTest::keyClick(list, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        QTest::keyClicks(nameEditor(w), "Before transform");
        QTest::keyClick(nameEditor(w), Qt::Key_T, Qt::ControlModifier);
        QTRY_COMPARE(d->state.layers[1].name, QString("Before transform"));
        QVERIFY(!nameEditor(w));
        QVERIFY(canvas->isTransforming());
        QCOMPARE(sessions.size(), 1);
        QVERIFY(sessions[0][0].toBool());
        QCOMPARE(d->historyLabels().size(), history + 1);
        QTest::keyClick(canvas, Qt::Key_Right);
        QTest::keyClick(canvas, Qt::Key_Return);
        QVERIFY(!canvas->isTransforming());
        QCOMPARE(d->state.layers[1].geometry, QTransform::fromTranslate(1, 0));
        QCOMPARE(d->historyLabels().size(), history + 2);
        d->undo();
        QCOMPARE(d->state.layers[1].name, QString("Before transform"));
        QCOMPARE(d->state.layers[1].geometry, QTransform());
        d->undo();
        QCOMPARE(d->state.layers[1].name, QString("Original"));
    }
    void inlineRenameThenTransformPreservesNoopAndStaleGuards_data() {
        QTest::addColumn<QString>("draft");
        QTest::addColumn<bool>("escape");
        QTest::addColumn<bool>("stale");
        QTest::newRow("unchanged") << QString("Original") << false << false;
        QTest::newRow("blank") << QString("   ") << false << false;
        QTest::newRow("escape") << QString("Cancelled") << true << false;
        QTest::newRow("stale") << QString("Stale draft") << false << true;
    }
    void inlineRenameThenTransformPreservesNoopAndStaleGuards() {
        QFETCH(QString, draft);
        QFETCH(bool, escape);
        QFETCH(bool, stale);
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        list->setFocus();
        QTest::keyClick(list, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        QTest::keyClicks(nameEditor(w), draft);
        if (stale) {
            d->beginEdit();
            d->state.layers[1].opacity = .5;
            d->commit("External change");
        }
        const auto revision = d->revision();
        const int history = d->historyLabels().size();
        auto *canvas = w.currentCanvas();
        QSignalSpy sessions(canvas, &Canvas::transformSessionChanged);
        if (escape) {
            QTest::keyClick(nameEditor(w), Qt::Key_Escape);
            QTest::keyClick(list, Qt::Key_T, Qt::ControlModifier);
        } else {
            QTest::keyClick(nameEditor(w), Qt::Key_T, Qt::ControlModifier);
        }
        QVERIFY(canvas->isTransforming());
        QVERIFY(!nameEditor(w));
        QCOMPARE(sessions.size(), 1);
        QVERIFY(sessions[0][0].toBool());
        QCOMPARE(d->state.layers[1].name, QString("Original"));
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->historyLabels().size(), history);
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(!canvas->isTransforming());
        QCOMPARE(d->revision(), revision);
    }
    void inlineRenameThenSaveShortcutIncludesPendingName() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        QTemporaryDir dir;
        QString error;
        const QString file = dir.filePath("renamed.pdeck");
        QVERIFY(d->save(file, &error));
        auto *list = w.findChild<QListWidget *>("layers");
        list->setFocus();
        QTest::keyClick(list, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        QTest::keyClicks(nameEditor(w), "Saved name");
        QTest::keyClick(nameEditor(w), Qt::Key_S, Qt::ControlModifier);
        QTRY_COMPARE(d->state.layers[1].name, QString("Saved name"));
        QVERIFY(!nameEditor(w));
        QVERIFY(!d->dirty);
        Document loaded;
        QVERIFY(loaded.load(file, &error));
        QCOMPARE(loaded.state.layers[1].name, QString("Saved name"));
    }
    void inlineRenameThenNumericTransformKeepsSeparateUndoSteps() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        const int history = d->historyLabels().size();
        auto *list = w.findChild<QListWidget *>("layers");
        list->setFocus();
        QTest::keyClick(list, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        QTest::keyClicks(nameEditor(w), "Before numeric");
        bool handled = false, nameCommitted = false;
        QTimer timer;
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<TransformDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            handled = true;
            nameCommitted = d->state.layers[1].name == "Before numeric" && !nameEditor(w);
            dialog->findChild<QDoubleSpinBox *>("translateX")->setValue(3);
            dialog->accept();
        });
        timer.start();
        QTest::keyClick(nameEditor(w), Qt::Key_T, Qt::ControlModifier | Qt::AltModifier);
        timer.stop();
        QVERIFY(handled);
        QVERIFY(nameCommitted);
        QCOMPARE(d->state.layers[1].name, QString("Before numeric"));
        QCOMPARE(d->state.layers[1].geometry, QTransform::fromTranslate(3, 0));
        QCOMPARE(d->historyLabels().size(), history + 2);
        d->undo();
        QCOMPARE(d->state.layers[1].name, QString("Before numeric"));
        QCOMPARE(d->state.layers[1].geometry, QTransform());
        d->undo();
        QCOMPARE(d->state.layers[1].name, QString("Original"));
    }
    void inlineRenameThenExplicitUndoDiscardsDraft() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        list->setFocus();
        QTest::keyClick(list, Qt::Key_F2);
        QTRY_VERIFY(nameEditor(w));
        QTest::keyClicks(nameEditor(w), "Unsubmitted draft");
        // An explicit document command differs from the editor's local Ctrl+Z.
        w.findChild<QAction *>("undo")->trigger();
        QTRY_VERIFY(!nameEditor(w));
        QCOMPARE(d->state.layers.size(), 1);
        w.findChild<QAction *>("redo")->trigger();
        QCOMPARE(d->state.layers.size(), 2);
        QCOMPARE(d->state.layers[1].name, QString("Original"));
        QTest::qWait(40);
        QCOMPARE(d->state.layers[1].name, QString("Original"));
    }
    void inlineRenameCancelBlankUnchangedAndTextUndoAreNoops() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        const quint64 revision = d->revision();
        const int count = d->historyLabels().size();
        auto begin = [&] {
            list->setFocus();
            QTest::keyClick(list, Qt::Key_F2);
        };
        begin();
        QTRY_VERIFY(nameEditor(w));
        QTest::keyClicks(nameEditor(w), "Cancelled");
        QTest::keyClick(nameEditor(w), Qt::Key_Escape);
        QTRY_VERIFY(!nameEditor(w));
        begin();
        QTRY_VERIFY(nameEditor(w));
        QTest::keyClick(nameEditor(w), Qt::Key_Return);
        QTRY_VERIFY(!nameEditor(w));
        begin();
        QTRY_VERIFY(nameEditor(w));
        nameEditor(w)->setText("   ");
        QTest::keyClick(nameEditor(w), Qt::Key_Return);
        QTRY_VERIFY(!nameEditor(w));
        begin();
        QTRY_VERIFY(nameEditor(w));
        QTest::keyClicks(nameEditor(w), "Replacement");
        QTest::keyClick(nameEditor(w), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(d->revision(), revision); // Ctrl+Z belongs to the name editor.
        QCOMPARE(nameEditor(w)->text(), QString("Original"));
        QTest::keyClick(nameEditor(w), Qt::Key_Escape);
        QTRY_VERIFY(!nameEditor(w));
        QCOMPARE(d->state.layers[1].name, QString("Original"));
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->historyLabels().size(), count);
        QVERIFY(!d->dirty);
    }
    void unchangedLegacyNamesAreNotTrimmedOrTruncated() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        for (const auto &name : {QString("  Legacy name  "), QString(200, QChar('x'))}) {
            d->beginEdit();
            d->state.layers[1].name = name;
            d->commit("Legacy fixture");
            d->markSaved();
            const quint64 revision = d->revision();
            QTest::qWait(40);
            w.findChild<QAction *>("rename")->trigger();
            QTRY_VERIFY(nameEditor(w));
            QTest::keyClick(nameEditor(w), Qt::Key_Return);
            QTRY_VERIFY(!nameEditor(w));
            QCOMPARE(d->state.layers[1].name, name);
            QCOMPARE(d->revision(), revision);
            QVERIFY(!d->dirty);
        }
    }
    void renameCannotJoinAnotherLiveEdit() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        const quint64 revision = d->revision();
        w.findChild<QAction *>("rename")->trigger();
        QTRY_VERIFY(nameEditor(w));
        nameEditor(w)->setText("Unrelated draft");
        d->beginEdit();
        d->state.layers[1].opacity = .5;
        QTest::keyClick(nameEditor(w), Qt::Key_Return);
        QTRY_VERIFY(!nameEditor(w));
        QCOMPARE(d->state.layers[1].name, QString("Original"));
        QCOMPARE(d->revision(), revision);
        QVERIFY(d->isEditing());
        d->cancelEdit();
        QCOMPARE(d->state.layers[1].opacity, 1.0);
    }
    void inlineRenameFocusTabAndCloseDoNotRetarget() {
        MainWindow w(false, false);
        auto *first = layerFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        w.findChild<QAction *>("rename")->trigger();
        QTRY_VERIFY(nameEditor(w));
        nameEditor(w)->setText("Focus committed");
        const QPoint otherRow = list->visualItemRect(list->item(1)).center();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, otherRow);
        QTRY_COMPARE(first->state.layers[1].name, QString("Focus committed"));
        QCOMPARE(first->state.active, 0);
        first->markSaved();
        auto *second = new Document;
        QVERIFY(second->create({20, 20}));
        second->title = "Second";
        w.addDocument(second);
        auto *tabs = w.findChild<QTabWidget *>("documents");
        tabs->setCurrentIndex(0);
        list->setCurrentRow(0);
        w.findChild<QAction *>("rename")->trigger();
        QTRY_VERIFY(nameEditor(w));
        nameEditor(w)->setText("Must not reach second");
        tabs->setCurrentIndex(1);
        QTRY_VERIFY(!nameEditor(w));
        QCOMPARE(first->state.layers[1].name, QString("Focus committed"));
        QCOMPARE(second->state.layers[0].name, QString("Katman 1"));
        w.findChild<QAction *>("rename")->trigger();
        QTRY_VERIFY(nameEditor(w));
        nameEditor(w)->setText("Closing tab draft");
        QPointer<Document> closing = second;
        QVERIFY(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 1)));
        QTRY_VERIFY(!nameEditor(w));
        QVERIFY(!closing);
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(first->state.layers[1].name, QString("Focus committed"));
    }
    void destroyingWindowCancelsPendingRename() {
        auto window = std::make_unique<MainWindow>(false, false);
        QPointer<Document> d = layerFixture(*window);
        window->findChild<QAction *>("rename")->trigger();
        QTRY_VERIFY(nameEditor(*window));
        nameEditor(*window)->setText("Uncommitted on teardown");
        int updates = 0;
        connect(d, &Document::updated, [&] { ++updates; });
        window.reset();
        QVERIFY(!d);
        QCOMPARE(updates, 0);
    }
    void inlineRenameInvalidatesAfterDeletionAndCanRepeat() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        const QString baseName = d->state.layers[0].name;
        w.findChild<QAction *>("rename")->trigger();
        QTRY_VERIFY(nameEditor(w));
        nameEditor(w)->setText("Deleted layer draft");
        // An external command can remove the target before the pending name is committed.
        w.findChild<QAction *>("deleteLayer")->trigger();
        QTRY_VERIFY(!nameEditor(w));
        QCOMPARE(d->state.layers.size(), 1);
        QCOMPARE(d->state.layers[0].name, baseName);
        w.findChild<QAction *>("undo")->trigger();
        QTRY_COMPARE(d->state.layers.size(), 2);
        QCOMPARE(d->state.layers[1].name, QString("Original"));
        QTest::qWait(40);
        w.findChild<QAction *>("rename")->trigger();
        QTRY_VERIFY(nameEditor(w));
        nameEditor(w)->setText("After delete undo");
        QTest::keyClick(nameEditor(w), Qt::Key_Return);
        QTRY_COMPARE(d->state.layers[1].name, QString("After delete undo"));
    }
    void immediateReorderRenameUsesActiveLayer() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        QVERIFY(d->addLayer("Third"));
        d->reorder(-1);
        QCOMPARE(d->state.active, 1);
        const int count = d->historyLabels().size();
        w.findChild<QAction *>("rename")->trigger();
        QTRY_VERIFY(nameEditor(w));
        QCOMPARE(nameEditor(w)->text(), QString("Third"));
        QTest::keyClicks(nameEditor(w), "Third renamed");
        QTest::keyClick(nameEditor(w), Qt::Key_Return);
        QTRY_COMPARE(d->state.layers[1].name, QString("Third renamed"));
        QCOMPARE(d->state.layers[2].name, QString("Original"));
        QCOMPARE(d->historyLabels().size(), count + 1);
    }
    void openingContextMenuCommitsNameBeforeChangingRows() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        w.findChild<QAction *>("rename")->trigger();
        QTRY_VERIFY(nameEditor(w));
        QTest::keyClicks(nameEditor(w), "Before context");
        const int count = d->historyLabels().size();
        QVERIFY(contextMenu(w, [&] {
            const QPoint point = list->visualItemRect(list->item(1)).center();
            QContextMenuEvent event(QContextMenuEvent::Mouse, point,
                                    list->viewport()->mapToGlobal(point));
            QApplication::sendEvent(list->viewport(), &event);
        }, [&](QMenu *menu) {
            QCOMPARE(d->state.active, 0);
            QCOMPARE(d->state.layers[1].name, QString("Before context"));
            QTest::keyClick(menu, Qt::Key_Escape);
        }));
        QTRY_VERIFY(!nameEditor(w));
        QCOMPARE(d->historyLabels().size(), count + 1);
        QCOMPARE(d->state.layers[1].name, QString("Before context"));
    }
    void layerContextMenuTargetsClickedRowAndCanCancel() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        QTest::qWait(40);
        QCOMPARE(d->state.active, 1);
        auto open = [&] {
            const QPoint point = list->visualItemRect(list->item(1)).center();
            QTest::mouseClick(list->viewport(), Qt::RightButton, Qt::NoModifier, point);
            QContextMenuEvent event(QContextMenuEvent::Mouse, point,
                                    list->viewport()->mapToGlobal(point));
            QApplication::sendEvent(list->viewport(), &event);
        };
        const quint64 revision = d->revision();
        QVERIFY(contextMenu(w, open, [&](QMenu *menu) {
            QCOMPARE(d->state.active, 0);
            QVERIFY(!menu->findChild<QAction *>("layerDown")->isEnabled());
            QVERIFY(menu->findChild<QAction *>("layerUp")->isEnabled());
            QTest::keyClick(menu, Qt::Key_Escape);
        }));
        QCOMPARE(d->revision(), revision);
        QVERIFY(contextMenu(w, open, [&](QMenu *menu) {
            auto *entry = menu->findChild<QAction *>("rename");
            QVERIFY(entry && entry->isEnabled());
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                              menu->actionGeometry(entry).center());
        }));
        QTRY_VERIFY(nameEditor(w));
        QCOMPARE(nameEditor(w)->text(), d->state.layers[0].name);
        nameEditor(w)->setText("Context base");
        QTest::keyClick(nameEditor(w), Qt::Key_Return);
        QTRY_COMPARE(d->state.layers[0].name, QString("Context base"));
        QCOMPARE(d->state.layers[1].name, QString("Original"));
        const int count = d->historyLabels().size();
        list->setFocus();
        QVERIFY(contextMenu(w, [&] { QTest::keyClick(list, Qt::Key_F10, Qt::ShiftModifier); },
                            [&](QMenu *menu) {
            auto *entry = menu->findChild<QAction *>("deleteLayer");
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                              menu->actionGeometry(entry).center());
        }));
        QCOMPARE(d->state.layers.size(), 1);
        QCOMPARE(d->historyLabels().size(), count + 1);
        QCOMPARE(d->state.layers[0].name, QString("Original"));
        w.findChild<QAction *>("undo")->trigger();
        QCOMPARE(d->state.layers.size(), 2);
        QCOMPARE(d->state.layers[0].name, QString("Context base"));
    }
    void layerContextMenuLocksAndStaleCommandsAreSafe() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        QFont font;
        font.setPixelSize(12);
        QVERIFY(d->addText("Caption", font, Qt::red, {} , nullptr));
        QString error;
        QVERIFY(d->addMask(&error));
        w.findChild<QAction *>("lock")->trigger();
        auto *list = w.findChild<QListWidget *>("layers");
        QTRY_VERIFY(list->count() == d->state.layers.size() &&
                    list->item(0)->text().contains("Metin"));
        QVERIFY(list->item(0)->text().contains("Maske"));
        QVERIFY(list->item(0)->text().contains("Kilitli"));
        QVERIFY(list->item(0)->toolTip().contains("Ad ve görünürlük"));
        const auto revision = d->revision();
        list->setFocus();
        QVERIFY(contextMenu(w, [&] { QTest::keyClick(list, Qt::Key_Menu); }, [&](QMenu *menu) {
            QVERIFY(!menu->findChild<QAction *>("editText")->isEnabled());
            QVERIFY(!menu->findChild<QAction *>("mask")->isEnabled());
            QVERIFY(!menu->findChild<QAction *>("removeMask")->isEnabled());
            QVERIFY(menu->findChild<QAction *>("rename")->isEnabled());
            // A history change while the menu is open invalidates its captured target.
            d->undo();
            menu->findChild<QAction *>("deleteLayer")->trigger();
        }));
        QCOMPARE(d->state.layers.size(), 3);
        QVERIFY(!d->state.layers[2].locked);
        QVERIFY(d->revision() != revision);
    }
    void pixelDoubleClickRenamesAndTextDoubleClickEdits() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        auto *list = w.findChild<QListWidget *>("layers");
        QTest::qWait(40);
        const QPoint pixel = list->visualItemRect(list->item(0)).center();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
        QTRY_VERIFY(nameEditor(w));
        QTest::keyClick(nameEditor(w), Qt::Key_Escape);
        QTRY_VERIFY(!nameEditor(w));
        QFont font;
        font.setPixelSize(12);
        QVERIFY(d->addText("Caption", font, Qt::red, {}, nullptr));
        QTest::qWait(40);
        bool seen = false;
        QTimer closer;
        connect(&closer, &QTimer::timeout, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                seen = dialog->findChild<QPlainTextEdit *>();
                dialog->reject();
            }
        });
        closer.start(10);
        const QPoint text = list->visualItemRect(list->item(0)).center();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, text);
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, text);
        QVERIFY(seen);
        QVERIFY(!nameEditor(w));
        QCOMPARE(d->state.layers[2].text, QString("Caption"));
    }
    void layerRenameRemainsReachableInAllMinimumSizePresets() {
        MainWindow w(false, false);
        auto *d = layerFixture(w);
        w.resize(900, 620);
        const auto revision = d->revision();
        auto *list = w.findChild<QListWidget *>("layers");
        for (const auto &preset : uiPresets()) {
            w.applyUiPreset(preset.id, false);
            QTest::qWait(30);
            QVERIFY2(list->isVisible(), qPrintable(preset.id));
            QVERIFY2(list->viewport()->height() >= 40, qPrintable(preset.id));
            QVERIFY2(list->viewport()->width() >= 120, qPrintable(preset.id));
            const QImage row = list->viewport()->grab(list->visualItemRect(list->currentItem()))
                                   .toImage().convertToFormat(QImage::Format_RGB32);
            const QRgb textColor = w.uiSettings().resolved().text.rgb();
            int readableText = 0;
            for (int y = 0; y < row.height(); ++y)
                for (int x = 85; x < row.width() - 5; ++x)
                    readableText += qAbs(qRed(row.pixel(x, y)) - qRed(textColor)) < 30 &&
                                    qAbs(qGreen(row.pixel(x, y)) - qGreen(textColor)) < 30 &&
                                    qAbs(qBlue(row.pixel(x, y)) - qBlue(textColor)) < 30;
            QVERIFY2(readableText > 4,
                     qPrintable(QString("%1 readable=%2 row=%3x%4 viewport=%5x%6")
                                    .arg(preset.id).arg(readableText)
                                    .arg(row.width()).arg(row.height())
                                    .arg(list->viewport()->width()).arg(list->viewport()->height())));
            w.findChild<QAction *>("ui.panel.layers")->setChecked(false);
            QVERIFY(!list->isVisible());
            w.findChild<QAction *>("rename")->trigger();
            QTRY_VERIFY(nameEditor(w));
            QVERIFY(list->isVisible());
            QVERIFY(nameEditor(w)->width() >= 60);
            QTest::keyClick(nameEditor(w), Qt::Key_Escape);
            QTRY_VERIFY(!nameEditor(w));
            QCOMPARE(d->revision(), revision);
        }
    }
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
