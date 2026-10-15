#include "mainwindow.h"
#include "version.h"
#include <QAction>
#include <QCryptographicHash>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QImageReader>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray contents(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
bool modal(QAction *action, const std::function<void(QDialog *)> &handler) {
    QTimer driver, timeout;
    driver.setInterval(10);
    QPointer<QDialog> last;
    bool seen = false, expired = false;
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog == last)
            return;
        last = dialog;
        seen = true;
        handler(dialog);
    });
    QObject::connect(&timeout, &QTimer::timeout, [&] {
        expired = true;
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    driver.start();
    timeout.start(5000);
    action->trigger();
    return seen && !expired;
}
QString recoveryDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery";
}
void paint(Document *d, QColor color) {
    d->beginEdit();
    d->state.layers[0].pixels.fill(color);
    d->commit("Test edit");
}
}
class UiResilienceTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName("PhotoDeckUiResilienceTests");
        QApplication::setOrganizationName("PhotoDeckTests");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QImageReader::setAllocationLimit(128);
    }
    void cleanup() {
        QDir(recoveryDirectory()).removeRecursively();
    }
    void toolOptionsFollowToolsAndDocumentTabs() {
        MainWindow w(false, false);
        auto *first = new Document;
        QVERIFY(first->create({32, 24}, true));
        w.addDocument(first);
        w.show();
        QTest::qWait(20);
        auto *brush = w.findChild<QWidget *>("brushControls");
        auto *mode = w.findChild<QComboBox *>("selectionMode");
        auto *tolerance = w.findChild<QSpinBox *>("selectionTolerance");
        auto *contiguous = w.findChild<QCheckBox *>("contiguousSelection");
        QVERIFY(brush && mode && tolerance && contiguous);
        auto choose = [&](Canvas::Tool tool) {
            w.findChild<QAction *>("tool" + QString::number(tool))->trigger();
            QApplication::processEvents();
        };
        choose(Canvas::Move);
        QVERIFY(!brush->isVisible());
        QVERIFY(!mode->isVisible());
        QVERIFY(!tolerance->isVisible());
        choose(Canvas::Brush);
        QVERIFY(brush->isVisible());
        QVERIFY(!mode->isVisible());
        auto *flow = w.findChild<QSpinBox *>("brushFlow");
        auto *size = w.findChild<QSpinBox *>("brushSize");
        auto *hardness = w.findChild<QSpinBox *>("brushHardness");
        QVERIFY(flow && size && hardness && flow->isVisible());
        int amount = 31;
        for (auto tool : {Canvas::Fill, Canvas::Gradient, Canvas::Rectangle, Canvas::Ellipse}) {
            choose(tool);
            QVERIFY(flow->isVisible());
            QVERIFY(!size->isVisible());
            QVERIFY(!hardness->isVisible());
            flow->setValue(++amount);
            QCOMPARE(w.currentCanvas()->brush.flow, amount);
        }
        choose(Canvas::Wand);
        QVERIFY(!brush->isVisible());
        QVERIFY(mode->isVisible());
        QVERIFY(tolerance->isVisible());
        QVERIFY(contiguous->isVisible());
        auto *second = new Document;
        QVERIFY(second->create({32, 24}, true));
        w.addDocument(second);
        QApplication::processEvents();
        QCOMPARE(w.currentCanvas()->tool, Canvas::Move);
        QVERIFY(!brush->isVisible());
        QVERIFY(!mode->isVisible());
        QVERIFY(!tolerance->isVisible());
        w.findChild<QTabWidget *>("documents")->setCurrentIndex(0);
        QApplication::processEvents();
        QCOMPARE(w.currentCanvas()->tool, Canvas::Wand);
        QVERIFY(mode->isVisible());
        QVERIFY(tolerance->isVisible());
    }
    void focusPanelsLeaveCanvasUncovered_data() {
        QTest::addColumn<QSize>("windowSize");
        QTest::newRow("minimum") << QSize(900, 620);
        QTest::newRow("laptop") << QSize(1280, 800);
    }
    void focusPanelsLeaveCanvasUncovered() {
        QFETCH(QSize, windowSize);
        MainWindow w(false, false);
        auto *document = new Document;
        QVERIFY(document->create({32, 24}, true));
        w.addDocument(document);
        w.resize(windowSize);
        w.show();
        QTest::qWait(30);
        auto *workspace = w.findChild<QWidget *>("workspace");
        auto *tools = w.findChild<QFrame *>("floatingTools");
        auto *layers = w.findChild<QFrame *>("floatingLayers");
        QVERIFY(workspace && tools && layers);
        auto viewport = [&] {
            return QRect(w.currentCanvas()->mapTo(workspace, QPoint()), w.currentCanvas()->size());
        };
        QVERIFY(workspace->rect().contains(tools->geometry()));
        QVERIFY(workspace->rect().contains(layers->geometry()));
        QVERIFY(!viewport().intersects(tools->geometry()));
        QVERIFY(!viewport().intersects(layers->geometry()));
        const int fullWidth = viewport().width();
        auto *layerAction = w.findChild<QAction *>("ui.panel.layers");
        layerAction->setChecked(false);
        QApplication::processEvents();
        QVERIFY(layers->isHidden());
        QVERIFY(viewport().width() > fullWidth);
        auto *toolAction = w.findChild<QAction *>("ui.panel.tools");
        toolAction->setChecked(false);
        QApplication::processEvents();
        const int unclutteredWidth = viewport().width();
        QVERIFY(unclutteredWidth > fullWidth);
        layerAction->setChecked(true);
        toolAction->setChecked(true);
        QApplication::processEvents();
        QVERIFY(!viewport().intersects(tools->geometry()));
        QVERIFY(!viewport().intersects(layers->geometry()));
        QCOMPARE(viewport().width(), fullWidth);
        for (int i = 0; i < Canvas::ToolCount; ++i) {
            auto *button = w.findChild<QToolButton *>("toolButton" + QString::number(i));
            QVERIFY(button && !button->accessibleName().isEmpty());
        }
        for (int i = 0; i < uiPresets().size(); ++i)
            QCOMPARE(w.findChild<QAction *>("ui.preset." + uiPresets()[i].id)->shortcut(),
                     QKeySequence("Ctrl+Alt+" + QString::number((i + 1) % 10)));
    }
    void oldProjectsSaveAsCopy_data() {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<int>("version");
        QTest::newRow("v1") << QFINDTESTDATA("../examples/Horizon.pdeck") << 1;
        QTest::newRow("v2") << QFINDTESTDATA("fixtures/v2-demo.pdeck") << 2;
        QTest::newRow("v3") << QFINDTESTDATA("fixtures/v3-demo.pdeck") << 3;
        QTest::newRow("v4") << QFINDTESTDATA("fixtures/v4-demo.pdeck") << 4;
    }
    void oldProjectsSaveAsCopy() {
        QFETCH(QString, fixture);
        QFETCH(int, version);
        QVERIFY(!fixture.isEmpty());
        QTemporaryDir temp;
        const QString original = temp.filePath("legacy.pdeck"), copy = temp.filePath("copy.pdeck");
        QVERIFY(QFile::copy(fixture, original));
        const auto before = contents(original);
        MainWindow w(false, false);
        QVERIFY(w.openFile(original));
        auto *d = w.currentDocument();
        QCOMPARE(d->sourceVersion(), version);
        paint(d, Qt::red);
        auto *save = w.findChild<QAction *>("save");
        bool canceled = false;
        QVERIFY(modal(save, [&](QDialog *dialog) {
            auto *box = qobject_cast<QMessageBox *>(dialog);
            if (box && box->objectName() == "projectUpgradeWarning") {
                canceled = true;
                box->button(QMessageBox::Cancel)->click();
            } else dialog->reject();
        }));
        QVERIFY(canceled);
        QCOMPARE(contents(original), before);
        QVERIFY(d->dirty);
        bool warning = false, chooser = false;
        QVERIFY(modal(save, [&](QDialog *dialog) {
            if (dialog->objectName() == "projectUpgradeWarning") {
                warning = true;
                dialog->findChild<QPushButton *>("saveProjectCopy")->click();
            } else if (auto *files = qobject_cast<QFileDialog *>(dialog)) {
                chooser = true;
                files->selectFile(copy);
                QMetaObject::invokeMethod(files, "accept", Qt::DirectConnection);
            } else dialog->reject();
        }));
        QVERIFY(warning && chooser);
        QCOMPARE(contents(original), before);
        QCOMPARE(d->path, copy);
        QVERIFY(!d->dirty);
        Document loaded;
        QString error;
        QVERIFY2(loaded.load(copy, &error), qPrintable(error));
        QCOMPARE(loaded.sourceVersion(), Document::NativeProjectVersion);
        QCOMPARE(loaded.composite(), d->composite());
    }
    void externalConflictCancelThenExplicitOverwrite() {
        QTemporaryDir temp;
        const QString file = temp.filePath("project.pdeck");
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({32, 24}));
        QString error;
        QVERIFY(d->save(file, &error));
        w.addDocument(d);
        paint(d, Qt::red);
        Document external;
        QVERIFY(external.create({32, 24}));
        paint(&external, Qt::blue);
        QVERIFY(external.save(file, &error));
        const auto disk = contents(file);
        auto *save = w.findChild<QAction *>("save");
        bool canceled = false;
        QVERIFY(modal(save, [&](QDialog *dialog) {
            if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
                canceled = box->objectName() == "projectConflictWarning";
                box->button(QMessageBox::Cancel)->click();
            } else dialog->reject();
        }));
        QVERIFY(canceled);
        QCOMPARE(contents(file), disk);
        QVERIFY(d->dirty);
        bool approved = false;
        QVERIFY(modal(save, [&](QDialog *dialog) {
            if (dialog->objectName() == "projectConflictWarning") {
                approved = true;
                dialog->findChild<QPushButton *>("overwriteProject")->click();
            } else dialog->reject();
        }));
        QVERIFY(approved);
        QVERIFY(!d->dirty);
        Document reread;
        QVERIFY(reread.load(file, &error));
        QCOMPARE(reread.composite().pixelColor(0, 0), QColor(Qt::red));
    }
    void aboutAndHelpMatchCurrentBehavior() {
        MainWindow w(false, false);
        bool about = false, help = false;
        QVERIFY(modal(w.findChild<QAction *>("about"), [&](QDialog *dialog) {
            if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
                about = box->windowTitle().contains(PhotoDeck::Version) &&
                        box->text().contains("RGB ICC");
                box->accept();
            } else dialog->reject();
        }));
        QVERIFY(about);
        QVERIFY(modal(w.findChild<QAction *>("help"), [&](QDialog *dialog) {
            if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
                help = box->text().contains("Ctrl+T katman dönüşümü") &&
                       box->text().contains("5 saniyede") &&
                       !box->text().contains("Dönüştürme, çizim ve silme");
                box->accept();
            } else dialog->reject();
        }));
        QVERIFY(help);
    }
    void earlyRecoveryAndDeferredGesture() {
        auto *w = new MainWindow(false, false);
        auto *d = new Document;
        QVERIFY(d->create({32, 24}));
        w->addDocument(d);
        auto *periodic = w->findChild<QTimer *>("autosaveTimer");
        auto *afterEdit = w->findChild<QTimer *>("recoverySaveTimer");
        QVERIFY(periodic && afterEdit);
        QCOMPARE(periodic->interval(), 45000);
        paint(d, Qt::red);
        QVERIFY(afterEdit->isActive());
        QCOMPARE(afterEdit->interval(), 5000);
        const QDir dir(recoveryDirectory());
        QVERIFY(dir.entryList({"*.pdeck"}, QDir::Files).isEmpty());
        d->beginEdit();
        d->state.layers[0].pixels.fill(Qt::cyan);
        d->changed();
        // Force the actual recovery timer to become due during a long gesture.
        afterEdit->start(1);
        QTest::qWait(30);
        QVERIFY(d->isEditing());
        QVERIFY(dir.entryList({"*.pdeck"}, QDir::Files).isEmpty());
        d->commit("Long stroke");
        QTRY_COMPARE_WITH_TIMEOUT(dir.entryList({"*.pdeck"}, QDir::Files).size(), 1, 1500);
        const QString snapshot = dir.filePath(dir.entryList({"*.pdeck"}, QDir::Files).first());
        Document recovered;
        QString error;
        QVERIFY2(recovered.load(snapshot, &error), qPrintable(error));
        QCOMPARE(recovered.composite().pixelColor(0, 0), QColor(Qt::cyan));
        QVERIFY(d->dirty);
        QVERIFY(d->path.isEmpty());
        delete w; // Crash simulation: no closeEvent, so the snapshot survives.
        QVERIFY(QFile::exists(snapshot));
    }
    void previewsReuseUnchangedPixelsAndDeferLiveStroke() {
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({1400, 1000}, true));
        w.addDocument(d);
        w.applyUiPreset("film", false);
        auto *layers = w.findChild<QListWidget *>("layers");
        auto *strip = w.findChild<QListWidget *>("filmstrip");
        QVERIFY(layers && strip && layers->count() == 1 && strip->count() == 1);
        const auto layerIcon = layers->item(0)->icon().cacheKey();
        const auto filmIcon = strip->item(0)->icon().cacheKey();
        for (int i = 0; i < 3; ++i) {
            d->selection = Selection(QRect(0, 0, 10 + i, 10));
            d->changed();
            QTest::qWait(35);
        }
        QCOMPARE(layers->item(0)->icon().cacheKey(), layerIcon);
        QCOMPARE(strip->item(0)->icon().cacheKey(), filmIcon);
        d->beginEdit();
        d->state.layers[0].pixels.fill(Qt::red);
        d->changed();
        QTest::qWait(35);
        QCOMPARE(layers->item(0)->icon().cacheKey(), layerIcon);
        QCOMPARE(strip->item(0)->icon().cacheKey(), filmIcon);
        d->commit("Stroke");
        QTRY_VERIFY(layers->item(0)->icon().cacheKey() != layerIcon);
        QTRY_VERIFY(strip->item(0)->icon().cacheKey() != filmIcon);
        QCOMPARE(d->composite().pixelColor(0, 0), QColor(Qt::red));
    }
};
QTEST_MAIN(UiResilienceTests)
#include "test_ui_resilience.moc"
