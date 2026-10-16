#include "document.h"
#include "mainwindow.h"
#include "recoveryfiles.h"
#include "recoverymanager.h"
#include <QAction>
#include <QDialog>
#include <QFileDialog>
#include <QImageReader>
#include <QMessageBox>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

namespace {
QByteArray contents(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QString windowRecoveryDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery";
}
void paint(Document &document, QColor color) {
    document.beginEdit();
    document.state.layers[0].pixels.fill(color);
    document.commit("Recovery test edit");
}

// Every wait is canceled without GUI cooperation. ReleaseOnExit additionally releases
// the barrier on QVERIFY/QCOMPARE's early returns, before the manager joins its worker.
struct BarrierBackend {
    struct Observation {
        QColor before, after;
        QString name;
        std::thread::id thread;
        int activeSnapshots = 0;
    };
    mutable std::mutex mutex;
    std::condition_variable condition;
    QVector<Observation> observations;
    QSet<int> prepareFailures;
    int permits = 0, finished = 0, canceled = 0, publishAttempts = 0;
    int activePreparations = 0, maxActivePreparations = 0;
    bool released = false, failPublish = false;
    bool allPrepareOnWorker = true, allPublishOnGui = true;
    const std::thread::id guiThread = std::this_thread::get_id();

    int entered() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return observations.size();
    }
    int completed() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return finished;
    }
    int cancellationCount() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return canceled;
    }
    Observation observation(int index) const {
        const std::lock_guard<std::mutex> lock(mutex);
        return observations.at(index);
    }
    void allow(int count) {
        const std::lock_guard<std::mutex> lock(mutex);
        permits = qMax(permits, count);
        condition.notify_all();
    }
    void releaseAll() {
        const std::lock_guard<std::mutex> lock(mutex);
        released = true;
        condition.notify_all();
    }
    void failPreparation(int index) {
        const std::lock_guard<std::mutex> lock(mutex);
        prepareFailures.insert(index);
    }
    void setPublicationFailure(bool fail) {
        const std::lock_guard<std::mutex> lock(mutex);
        failPublish = fail;
    }
    bool prepare(const State &snapshot, const QString &path, QString *error,
                 const std::atomic_bool &cancel) {
        int index;
        bool injectedFailure;
        {
            std::unique_lock<std::mutex> lock(mutex);
            index = observations.size() + 1;
            ++activePreparations;
            maxActivePreparations = qMax(maxActivePreparations, activePreparations);
            Observation observation;
            observation.before = snapshot.layers[0].pixels.pixelColor(0, 0);
            observation.name = snapshot.layers[0].name;
            observation.thread = std::this_thread::get_id();
            observation.activeSnapshots = RecoveryManager::activeSnapshotCount();
            allPrepareOnWorker &= observation.thread != guiThread;
            observations.append(observation);
            condition.notify_all();
            while (!released && permits < index && !cancel.load(std::memory_order_relaxed))
                condition.wait_for(lock, std::chrono::milliseconds(2));
            observations[index - 1].after = snapshot.layers[0].pixels.pixelColor(0, 0);
            injectedFailure = prepareFailures.contains(index);
        }
        bool ok = false;
        if (!cancel.load(std::memory_order_relaxed)) {
            // Exercise the real immutable-snapshot encoder and QSaveFile staging commit.
            QSaveFile output(path);
            output.setDirectWriteFallback(false);
            if (!output.open(QIODevice::WriteOnly)) {
                *error = output.errorString();
            } else if (Document::writeRecoverySnapshot(snapshot, output, error, &cancel)) {
                if (injectedFailure) {
                    *error = "Injected staging failure";
                    output.cancelWriting();
                } else if (!cancel.load(std::memory_order_relaxed)) {
                    ok = output.commit();
                    if (!ok) *error = output.errorString();
                }
            }
        }
        {
            const std::lock_guard<std::mutex> lock(mutex);
            ++finished;
            --activePreparations;
            if (cancel.load(std::memory_order_relaxed)) ++canceled;
            condition.notify_all();
        }
        return ok;
    }
    bool publish(const QString &staging, const QString &target, QString *error) {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            ++publishAttempts;
            allPublishOnGui &= std::this_thread::get_id() == guiThread;
            if (failPublish) {
                *error = "Injected publication failure";
                return false;
            }
        }
        return RecoveryFiles::atomicReplace(staging, target, error);
    }
};
struct ReleaseOnExit {
    std::shared_ptr<BarrierBackend> backend;
    ~ReleaseOnExit() { backend->releaseAll(); }
};
RecoveryManager::Backend backendFor(const std::shared_ptr<BarrierBackend> &control) {
    RecoveryManager::Backend backend;
    backend.prepare = [control](const State &state, const QString &path, QString *error,
                                const std::atomic_bool &cancel) {
        return control->prepare(state, path, error, cancel);
    };
    backend.publish = [control](const QString &staging, const QString &target, QString *error) {
        return control->publish(staging, target, error);
    };
    return backend;
}

// Handles each nested dialog once, with an independent escape timer. Always click a
// button even if a test observation fails, so a failure never leaves an exec() hung.
bool driveDialogs(const std::function<void()> &action,
                  const std::function<void(QDialog *)> &handler) {
    QTimer driver, timeout;
    driver.setInterval(5);
    timeout.setSingleShot(true);
    QList<QPointer<QDialog>> handled;
    bool expired = false, seen = false;
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        for (const auto &previous : handled) if (previous == dialog) return;
        handled.append(dialog);
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
    action();
    return seen && !expired;
}
void clickMessage(QMessageBox *box, QMessageBox::StandardButton button) {
    if (auto *target = box->button(button)) target->click();
    else box->reject();
}
void chooseFile(QFileDialog *dialog, const QString &path) {
    dialog->selectFile(path);
    QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
}
Document *addDirtyDocument(MainWindow &window, QColor color, const QString &title) {
    auto *document = new Document;
    if (!document->create({16, 12}, true)) {
        delete document;
        return nullptr;
    }
    document->title = title;
    window.addDocument(document);
    paint(*document, color);
    return document;
}
void richFixture(Document &document, bool selections) {
    document.beginEdit();
    auto &layer = document.state.layers[0];
    layer.name = "Snapshot A";
    layer.pixels.fill(QColor(31, 67, 139, 211));
    layer.mask = QImage(layer.pixels.size(), QImage::Format_ARGB32_Premultiplied);
    layer.mask.fill(QColor(123, 123, 123));
    layer.offset = {-2, 3};
    layer.geometry.translate(1.25, -.5);
    layer.opacity = .73;
    layer.locked = true;
    layer.blend = 3;
    layer.brightness = 7;
    layer.contrast = -9;
    layer.saturation = 11;
    layer.tones.gamma = 1.2;
    layer.tones.curves[0] = {{0, 0}, {100, 120}, {255, 255}};
    layer.filters.noise = 4;
    layer.filters.noiseSeed = 17;
    layer.maskSettings.density = 83;
    layer.maskSettings.feather = 1;
    layer.text = "Recovery metadata";
    layer.font = QFont("Sans Serif", 11);
    layer.color = QColor(90, 30, 170, 222);
    Layer top;
    top.name = "Hidden clipped layer";
    top.pixels = QImage(document.state.size, QImage::Format_ARGB32_Premultiplied);
    top.pixels.fill(Qt::green);
    top.visible = false;
    top.clipped = true;
    document.state.layers.append(top);
    document.state.active = 1;
    if (selections) {
        QImage coverage(document.state.size, QImage::Format_Grayscale8);
        coverage.fill(0);
        coverage.setPixelColor(2, 3, QColor(111, 111, 111));
        document.state.selection = Selection::fromMask(coverage);
        document.state.previousSelection = Selection(QRect(1, 2, 4, 5));
        document.state.savedSelections.append({"Soft channel", Selection::fromMask(coverage)});
    }
    document.commit("Rich snapshot fixture");
}
void compareState(const State &actual, const State &expected) {
    QCOMPARE(actual.size, expected.size);
    QCOMPARE(actual.active, expected.active);
    QCOMPARE(actual.layers.size(), expected.layers.size());
    for (int i = 0; i < actual.layers.size(); ++i) {
        const auto &a = actual.layers[i];
        const auto &e = expected.layers[i];
        QCOMPARE(a.name, e.name);
        QCOMPARE(a.pixels, e.pixels);
        QCOMPARE(a.mask, e.mask);
        QCOMPARE(a.offset, e.offset);
        QCOMPARE(a.geometry, e.geometry);
        QCOMPARE(a.visible, e.visible);
        QCOMPARE(a.locked, e.locked);
        QCOMPARE(a.clipped, e.clipped);
        QCOMPARE(a.opacity, e.opacity);
        QCOMPARE(a.blend, e.blend);
        QCOMPARE(a.brightness, e.brightness);
        QCOMPARE(a.contrast, e.contrast);
        QCOMPARE(a.saturation, e.saturation);
        QCOMPARE(a.blur, e.blur);
        QVERIFY(a.tones == e.tones);
        QVERIFY(a.filters == e.filters);
        QVERIFY(a.maskSettings == e.maskSettings);
        QCOMPARE(a.text, e.text);
        QCOMPARE(a.font, e.font);
        QCOMPARE(a.color, e.color);
    }
    QVERIFY(actual.selection.equivalent(expected.selection, expected.size));
    QVERIFY(actual.previousSelection.equivalent(expected.previousSelection, expected.size));
    QCOMPARE(actual.savedSelections.size(), expected.savedSelections.size());
    for (int i = 0; i < actual.savedSelections.size(); ++i) {
        QCOMPARE(actual.savedSelections[i].name, expected.savedSelections[i].name);
        QVERIFY(actual.savedSelections[i].selection.equivalent(expected.savedSelections[i].selection,
                                                               expected.size));
    }
}
}

class RecoveryTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName("PhotoDeckRecoveryTests");
        QApplication::setOrganizationName("PhotoDeckTests");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QImageReader::setAllocationLimit(128);
        qRegisterMetaType<Document *>();
    }
    void cleanup() {
        QCOMPARE(RecoveryManager::activeSnapshotCount(), 0);
        QDir(windowRecoveryDirectory()).removeRecursively();
    }
    void snapshotSharesImageBuffersAndDetachesMetadata() {
        Document document;
        QVERIFY(document.create({16, 12}, true));
        richFixture(document, true);
        document.state.layers[0].rendered();
        document.state.selection.boundaryImage();
        document.state.savedSelections[0].selection.boundaryImage();
        const auto pixelKey = document.state.layers[0].pixels.cacheKey();
        const auto maskKey = document.state.layers[0].mask.cacheKey();
        const auto coverageKey = document.state.selection.coverageImage().cacheKey();
        const auto savedCoverageKey = document.state.savedSelections[0].selection.coverageImage().cacheKey();
        const auto cacheKey = document.state.layers[0].cache.cacheKey();
        QVERIFY(!document.state.layers[0].cache.isNull());
        State snapshot = Document::recoverySnapshot(document.state);
        QVERIFY(snapshot.layers.constData() != document.state.layers.constData());
        QVERIFY(snapshot.savedSelections.constData() != document.state.savedSelections.constData());
        QCOMPARE(snapshot.layers[0].pixels.cacheKey(), pixelKey);
        QCOMPARE(snapshot.layers[0].mask.cacheKey(), maskKey);
        QCOMPARE(snapshot.selection.coverageImage().cacheKey(), coverageKey);
        QCOMPARE(snapshot.savedSelections[0].selection.coverageImage().cacheKey(), savedCoverageKey);
        QVERIFY(snapshot.layers[0].cache.isNull());
        QCOMPARE(document.state.layers[0].cache.cacheKey(), cacheKey);
        const QColor before = snapshot.layers[0].pixels.pixelColor(0, 0);
        document.state.layers[0].pixels.fill(Qt::cyan);
        document.state.layers[0].mask.fill(Qt::white);
        document.state.layers[0].name = "Live mutation";
        document.state.layers[0].tones.curves[0][1] = {100, 150};
        document.state.savedSelections[0].name = "Renamed live channel";
        document.state.selection = Selection(QRect(0, 0, 1, 1));
        QCOMPARE(snapshot.layers[0].pixels.pixelColor(0, 0), before);
        QCOMPARE(snapshot.layers[0].mask.pixelColor(0, 0), QColor(123, 123, 123));
        QCOMPARE(snapshot.layers[0].name, QString("Snapshot A"));
        QCOMPARE(snapshot.layers[0].tones.curves[0][1], QPoint(100, 120));
        QCOMPARE(snapshot.savedSelections[0].name, QString("Soft channel"));
        QCOMPARE(snapshot.selection.coverageImage().cacheKey(), coverageKey);
    }
    void immutableSnapshotCoalescesLatestAndKeepsGuiResponsive_data() {
        QTest::addColumn<bool>("selections");
        QTest::newRow("v5-masks-and-metadata") << false;
        QTest::newRow("v6-selection-context-and-named-channel") << true;
    }
    void immutableSnapshotCoalescesLatestAndKeepsGuiResponsive() {
        QFETCH(bool, selections);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Document document;
        QVERIFY(document.create({16, 12}, true));
        richFixture(document, selections);
        const State original = document.state;
        const auto revisionA = document.revision();
        RecoveryManager manager(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager.setBackend(backendFor(control)));
        manager.registerDocument(&document);
        QSignalSpy captured(&manager, &RecoveryManager::snapshotCaptured);
        QSignalSpy published(&manager, &RecoveryManager::published);
        QSignalSpy failed(&manager, &RecoveryManager::failed);
        manager.request(&document);
        QTRY_COMPARE(control->entered(), 1);
        QCOMPARE(captured.count(), 1);
        QCOMPARE(RecoveryManager::activeSnapshotCount(), 1);
        QVERIFY(captured[0][2].toLongLong() >= 0);
        QVERIFY(captured[0][3].toLongLong() > 0);
        int ticks = 0;
        bool queuedAction = false;
        QTimer heartbeat;
        heartbeat.setInterval(1);
        connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
        heartbeat.start();
        QTimer::singleShot(0, &manager, [&] { queuedAction = true; });
        for (int i = 0; i < 20; ++i) {
            document.beginEdit();
            document.state.layers[0].name = QString("Latest B %1").arg(i);
            document.state.layers[0].pixels.fill(QColor(150 + i, 20, 70));
            document.state.layers[0].mask.fill(QColor(200, 200, 200));
            document.commit("Coalesced newer edit");
            manager.request(&document);
            QVERIFY(manager.pendingCount() <= 1);
            QCOMPARE(captured.count(), 1);
        }
        const auto revisionB = document.revision();
        QVERIFY(revisionB != revisionA);
        QTRY_VERIFY(queuedAction && ticks >= 3);
        QCOMPARE(published.count(), 0);
        QCOMPARE(control->entered(), 1);
        control->allow(1);
        QTRY_COMPARE(published.count(), 1);
        QTRY_COMPARE(control->entered(), 2);
        QCOMPARE(published[0][1].toULongLong(), revisionA);
        QCOMPARE(manager.publishedRevision(&document), revisionA);
        const QString path = manager.recoveryPath(&document);
        Document recoveredA;
        QString error;
        QVERIFY2(recoveredA.load(path, &error), qPrintable(error));
        QCOMPARE(recoveredA.sourceVersion(), selections ? 6 : 5);
        compareState(recoveredA.state, original);
        QCOMPARE(control->observation(0).before, control->observation(0).after);
        QCOMPARE(captured.count(), 2);
        QCOMPARE(captured[1][1].toULongLong(), revisionB);
        control->allow(2);
        QTRY_COMPARE(published.count(), 2);
        QTRY_VERIFY(!manager.busy());
        QCOMPARE(manager.publishedRevision(&document), revisionB);
        QCOMPARE(manager.pendingCount(), 0);
        QCOMPARE(failed.count(), 0);
        QVERIFY(document.dirty);
        QVERIFY(document.path.isEmpty());
        Document recoveredB;
        QVERIFY2(recoveredB.load(path, &error), qPrintable(error));
        compareState(recoveredB.state, document.state);
        QVERIFY(control->allPrepareOnWorker);
        QVERIFY(control->allPublishOnGui);
    }
    void globalSlotBoundsSnapshotsAndPendingIds() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        std::vector<std::unique_ptr<Document>> documents;
        for (int i = 0; i < 6; ++i) {
            auto document = std::make_unique<Document>();
            QVERIFY(document->create({16, 12}, true));
            paint(*document, QColor(30 + i, 80, 120));
            documents.push_back(std::move(document));
        }
        RecoveryManager first(directory.path()), second(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(first.setBackend(backendFor(control)));
        QVERIFY(second.setBackend(backendFor(control)));
        QSignalSpy capturedFirst(&first, &RecoveryManager::snapshotCaptured);
        QSignalSpy capturedSecond(&second, &RecoveryManager::snapshotCaptured);
        bool globallyBounded = true;
        auto observe = [&] { globallyBounded &= RecoveryManager::activeSnapshotCount() == 1; };
        connect(&first, &RecoveryManager::snapshotCaptured, this, observe);
        connect(&second, &RecoveryManager::snapshotCaptured, this, observe);
        for (int i = 0; i < 6; ++i) {
            auto &manager = i < 3 ? first : second;
            manager.registerDocument(documents[i].get());
            manager.request(documents[i].get());
        }
        QTRY_COMPARE(control->entered(), 1);
        for (int edit = 0; edit < 40; ++edit) {
            for (int i = 0; i < 6; ++i) {
                auto &manager = i < 3 ? first : second;
                paint(*documents[i], QColor(70 + edit, i * 20, 120));
                manager.request(documents[i].get());
            }
            QVERIFY(first.pendingCount() <= 3);
            QVERIFY(second.pendingCount() <= 3);
            QCOMPARE(first.pendingCount() + second.pendingCount(), 6);
            QCOMPARE(capturedFirst.count() + capturedSecond.count(), 1);
            QCOMPARE(RecoveryManager::activeSnapshotCount(), 1);
        }
        control->releaseAll();
        QTRY_VERIFY(!first.busy() && !second.busy() && !first.pendingCount() && !second.pendingCount());
        QCOMPARE(capturedFirst.count() + capturedSecond.count(), 7);
        for (int i = 0; i < 6; ++i) {
            auto &manager = i < 3 ? first : second;
            QCOMPARE(manager.publishedRevision(documents[i].get()), documents[i]->revision());
            QVERIFY(QFile::exists(manager.recoveryPath(documents[i].get())));
        }
        QVERIFY(globallyBounded);
        QCOMPARE(control->maxActivePreparations, 1);
        for (int i = 0; i < control->entered(); ++i)
            QCOMPARE(control->observation(i).activeSnapshots, 1);
        QCOMPARE(RecoveryManager::activeSnapshotCount(), 0);
    }
    void roundRobinDoesNotStarveColdTabs() {
        QTemporaryDir directory;
        Document hot, cold, third;
        QVERIFY(hot.create({16, 12}, true));
        QVERIFY(cold.create({16, 12}, true));
        QVERIFY(third.create({16, 12}, true));
        paint(hot, Qt::red);
        paint(cold, Qt::green);
        paint(third, Qt::blue);
        RecoveryManager manager(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager.setBackend(backendFor(control)));
        manager.registerDocument(&hot);
        manager.registerDocument(&cold);
        manager.registerDocument(&third);
        QSignalSpy captured(&manager, &RecoveryManager::snapshotCaptured);
        manager.pause();
        manager.request(&hot);
        manager.request(&cold);
        manager.request(&third);
        QCOMPARE(captured.count(), 0);
        manager.resume();
        for (int turn = 1; turn <= 3; ++turn) {
            QTRY_COMPARE(control->entered(), turn);
            QCOMPARE(captured[turn - 1][0].toULongLong(), quint64(turn));
            for (int edit = 0; edit < 10; ++edit) {
                paint(hot, QColor(80 + turn * 10 + edit, 20, 30));
                manager.request(&hot);
            }
            control->allow(turn);
        }
        QTRY_COMPARE(control->entered(), 4);
        QCOMPARE(captured[3][0].toULongLong(), quint64(1));
        QCOMPARE(manager.publishedRevision(&cold), cold.revision());
        QCOMPARE(manager.publishedRevision(&third), third.revision());
        control->allow(4);
        QTRY_VERIFY(!manager.busy() && !manager.pendingCount());
        QCOMPARE(manager.publishedRevision(&hot), hot.revision());
    }
    void cleanTransitionInvalidatesQueuedCompletion_data() {
        QTest::addColumn<bool>("saveToFile");
        QTest::newRow("mark-saved") << false;
        QTest::newRow("document-save") << true;
    }
    void cleanTransitionInvalidatesQueuedCompletion() {
        QFETCH(bool, saveToFile);
        QTemporaryDir directory;
        Document document;
        QVERIFY(document.create({16, 12}, true));
        paint(document, Qt::red);
        RecoveryManager manager(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager.setBackend(backendFor(control)));
        manager.registerDocument(&document);
        QSignalSpy published(&manager, &RecoveryManager::published);
        QSignalSpy failed(&manager, &RecoveryManager::failed);
        control->allow(1);
        manager.request(&document);
        QTRY_COMPARE(published.count(), 1);
        const QString path = manager.recoveryPath(&document);
        QVERIFY(QFile::exists(path));
        paint(document, Qt::blue);
        manager.request(&document);
        QTRY_COMPARE(control->entered(), 2);
        manager.pause();
        control->allow(2);
        QTRY_COMPARE(control->completed(), 2);
        if (saveToFile) {
            QString error;
            QVERIFY2(document.save(directory.filePath("saved.pdeck"), &error), qPrintable(error));
        } else document.markSaved();
        QVERIFY(!document.dirty);
        QVERIFY(!QFile::exists(path));
        QCOMPARE(manager.publishedRevision(&document), quint64(0));
        manager.resume();
        QTRY_VERIFY(!manager.busy());
        QCOMPARE(published.count(), 1);
        QCOMPARE(failed.count(), 0);
        QVERIFY(!QFile::exists(path));
        QCOMPARE(manager.pendingCount(), 0);
    }
    void cleanupFailureCannotTurnSuccessfulSaveIntoFailure_data() {
        QTest::addColumn<bool>("stopFirst");
        QTest::newRow("save-clean-invalidation") << false;
        QTest::newRow("accepted-stop-cleanup") << true;
    }
    void cleanupFailureCannotTurnSuccessfulSaveIntoFailure() {
        QFETCH(bool, stopFirst);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Document document;
        QVERIFY(document.create({16, 12}, true));
        paint(document, Qt::red);
        RecoveryManager manager(directory.filePath("recovery"));
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager.setBackend(backendFor(control)));
        manager.registerDocument(&document);
        QSignalSpy published(&manager, &RecoveryManager::published);
        QSignalSpy failed(&manager, &RecoveryManager::failed);
        control->allow(1);
        manager.request(&document);
        QTRY_COMPARE(published.count(), 1);
        const QString recovery = manager.recoveryPath(&document);
        QVERIFY(QFile::exists(recovery));
        paint(document, Qt::blue);
        manager.request(&document);
        QTRY_COMPARE(control->entered(), 2);
        QCOMPARE(control->completed(), 1);
        // A directory at the exact old pathname reliably makes QFile::remove fail,
        // including when the suite runs as root. Its child must never be removed.
        QVERIFY(QFile::remove(recovery));
        QVERIFY(QDir().mkpath(recovery));
        QFile sentinel(QDir(recovery).filePath("keep.txt"));
        QVERIFY(sentinel.open(QIODevice::WriteOnly));
        QCOMPARE(sentinel.write("keep"), qint64(4));
        sentinel.close();
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Could not remove obsolete recovery copy:.*" +
                                                QRegularExpression::escape(recovery) + ".*"));
        if (stopFirst) manager.stop(true);
        QString error;
        const QString savedPath = directory.filePath("saved.pdeck");
        QVERIFY2(document.save(savedPath, &error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(document.lastSaveStatus(), Document::SaveStatus::Success);
        QVERIFY(!document.dirty);
        QCOMPARE(document.path, savedPath);
        QCOMPARE(manager.publishedRevision(&document), quint64(0));
        QCOMPARE(manager.pendingCount(), 0);
        QTRY_VERIFY(!manager.busy());
        QCOMPARE(control->completed(), 2);
        QCOMPARE(control->cancellationCount(), 1);
        QCOMPARE(published.count(), 1);
        QCOMPARE(failed.count(), 0);
        QVERIFY(QFileInfo(recovery).isDir());
        QCOMPARE(contents(sentinel.fileName()), QByteArray("keep"));
        Document saved;
        QVERIFY2(saved.load(savedPath, &error), qPrintable(error));
        QCOMPARE(saved.state.layers[0].pixels.pixelColor(0, 0), QColor(Qt::blue));
        QVERIFY(QDir(directory.filePath("recovery"))
                    .entryList({"*.pending*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
    void nestedPauseHoldsCaptureAndPublication() {
        QTemporaryDir directory;
        Document document;
        QVERIFY(document.create({16, 12}, true));
        paint(document, Qt::red);
        RecoveryManager manager(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager.setBackend(backendFor(control)));
        manager.registerDocument(&document);
        QSignalSpy captured(&manager, &RecoveryManager::snapshotCaptured);
        QSignalSpy published(&manager, &RecoveryManager::published);
        manager.pause();
        manager.pause();
        manager.request(&document);
        QCOMPARE(manager.pendingCount(), 1);
        manager.resume();
        QCoreApplication::processEvents();
        QCOMPARE(captured.count(), 0);
        manager.resume();
        QTRY_COMPARE(control->entered(), 1);
        manager.pause();
        manager.pause();
        control->allow(1);
        QTRY_COMPARE(control->completed(), 1);
        manager.resume();
        QCoreApplication::processEvents();
        QCOMPARE(published.count(), 0);
        manager.resume();
        QTRY_COMPARE(published.count(), 1);
        QTRY_VERIFY(!manager.busy());
    }
    void failuresKeepLastGoodFileAndDoNotSpin_data() {
        QTest::addColumn<bool>("publication");
        QTest::newRow("staging-write-failure") << false;
        QTest::newRow("atomic-publication-failure") << true;
    }
    void failuresKeepLastGoodFileAndDoNotSpin() {
        QFETCH(bool, publication);
        QTemporaryDir directory;
        Document document;
        QVERIFY(document.create({16, 12}, true));
        paint(document, Qt::red);
        RecoveryManager manager(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager.setBackend(backendFor(control)));
        manager.registerDocument(&document);
        QSignalSpy published(&manager, &RecoveryManager::published);
        QSignalSpy failed(&manager, &RecoveryManager::failed);
        control->allow(1);
        manager.request(&document);
        QTRY_COMPARE(published.count(), 1);
        const QString path = manager.recoveryPath(&document);
        const QByteArray before = contents(path);
        const auto revisionA = manager.publishedRevision(&document);
        QVERIFY(!before.isEmpty());
        paint(document, Qt::blue);
        if (publication) control->setPublicationFailure(true);
        else control->failPreparation(2);
        manager.request(&document);
        QTRY_COMPARE(control->entered(), 2);
        control->allow(2);
        QTRY_COMPARE(failed.count(), 1);
        QTRY_VERIFY(!manager.busy());
        QCOMPARE(contents(path), before);
        QCOMPARE(manager.publishedRevision(&document), revisionA);
        QCOMPARE(manager.pendingCount(), 0);
        QCOMPARE(published.count(), 1);
        QVERIFY(QDir(directory.path()).entryList({"*.pending*"}, QDir::Files | QDir::Hidden).isEmpty());
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
        heartbeat.start();
        QTRY_VERIFY(ticks >= 6);
        QCOMPARE(control->entered(), 2);
        QCOMPARE(failed.count(), 1);
        control->setPublicationFailure(false);
        manager.request(&document); // A later explicit due callback may retry.
        QTRY_COMPARE(control->entered(), 3);
        control->allow(3);
        QTRY_COMPARE(published.count(), 2);
        QTRY_VERIFY(!manager.busy());
        QCOMPARE(manager.publishedRevision(&document), document.revision());
        QVERIFY(contents(path) != before);
    }
    void failedSessionCreationRetriesOnlyOnAnExplicitRequestAfterRepair() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString recoveryDirectory = directory.filePath("blocked-recovery");
        QFile blocker(recoveryDirectory);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        QCOMPARE(blocker.write("regular file blocks directory"), qint64(29));
        blocker.close();
        Document document;
        QVERIFY(document.create({16, 12}, true));
        paint(document, Qt::red);
        RecoveryManager manager(recoveryDirectory);
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager.setBackend(backendFor(control)));
        manager.registerDocument(&document);
        QSignalSpy failed(&manager, &RecoveryManager::failed);
        QSignalSpy published(&manager, &RecoveryManager::published);
        QSignalSpy captured(&manager, &RecoveryManager::snapshotCaptured);
        manager.request(&document);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(published.count(), 0);
        QCOMPARE(captured.count(), 0);
        QCOMPARE(control->entered(), 0);
        QCOMPARE(manager.pendingCount(), 0);
        QVERIFY(!manager.busy());
        QCOMPARE(RecoveryManager::activeSnapshotCount(), 0);
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
        heartbeat.start();
        QTRY_VERIFY(ticks >= 6);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(control->entered(), 0);
        QVERIFY(QFile::remove(recoveryDirectory));
        QVERIFY(QDir().mkpath(recoveryDirectory));
        control->allow(1);
        manager.request(&document);
        QTRY_COMPARE(published.count(), 1);
        QTRY_VERIFY(!manager.busy());
        QCOMPARE(failed.count(), 1);
        QCOMPARE(captured.count(), 1);
        QCOMPARE(manager.publishedRevision(&document), document.revision());
        QVERIFY(QFile::exists(manager.recoveryPath(&document)));
    }
    void staleFailureDoesNotEmitOrDisplaceNewerRecovery() {
        QTemporaryDir directory;
        Document document;
        QVERIFY(document.create({16, 12}, true));
        paint(document, Qt::red);
        RecoveryManager manager(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        control->failPreparation(1);
        QVERIFY(manager.setBackend(backendFor(control)));
        manager.registerDocument(&document);
        QSignalSpy published(&manager, &RecoveryManager::published);
        QSignalSpy failed(&manager, &RecoveryManager::failed);
        manager.request(&document);
        QTRY_COMPARE(control->entered(), 1);
        paint(document, Qt::blue); // The automatic updated connection queues B.
        QCOMPARE(manager.pendingCount(), 1);
        control->allow(1);
        QTRY_COMPARE(control->entered(), 2);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(published.count(), 0);
        control->allow(2);
        QTRY_COMPARE(published.count(), 1);
        QTRY_VERIFY(!manager.busy());
        QCOMPARE(failed.count(), 0);
        QCOMPARE(manager.publishedRevision(&document), document.revision());
        Document recovered;
        QString error;
        QVERIFY2(recovered.load(manager.recoveryPath(&document), &error), qPrintable(error));
        QCOMPARE(recovered.state.layers[0].pixels.pixelColor(0, 0), QColor(Qt::blue));
    }
    void adoptionClaimsOnlyInactiveOrLegacyRecovery_data() {
        QTest::addColumn<bool>("legacy");
        QTest::newRow("managed-session-ownership") << false;
        QTest::newRow("legacy-unowned-file") << true;
    }
    void adoptionClaimsOnlyInactiveOrLegacyRecovery() {
        QFETCH(bool, legacy);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Document original, recovered;
        QVERIFY(original.create({16, 12}, true));
        QVERIFY(recovered.create({16, 12}, true));
        paint(original, Qt::red);
        paint(recovered, Qt::red);
        auto owner = std::make_unique<RecoveryManager>(directory.path());
        RecoveryManager claimant(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(owner->setBackend(backendFor(control)));
        QSignalSpy ownerPublished(owner.get(), &RecoveryManager::published);
        QSignalSpy failed(&claimant, &RecoveryManager::failed);
        QString source;
        QString error;
        if (legacy) {
            source = directory.filePath("legacy.pdeck");
            QVERIFY2(original.save(source, &error, false), qPrintable(error));
        } else {
            owner->registerDocument(&original);
            control->allow(1);
            owner->request(&original);
            QTRY_COMPARE(ownerPublished.count(), 1);
            QTRY_VERIFY(!owner->busy());
            source = owner->recoveryPath(&original);
        }
        const QByteArray before = contents(source);
        QVERIFY(!before.isEmpty());
        const QString unrelated = directory.filePath("unrelated.pdeck");
        QVERIFY(QFile::copy(source, unrelated));
        const quint64 recoveredRevision = recovered.revision();
        if (!legacy) {
            QVERIFY(!RecoveryFiles::mayRecover(source));
            claimant.adoptRecovery(&recovered, source, recoveredRevision);
            QCOMPARE(failed.count(), 1);
            QCOMPARE(claimant.publishedRevision(&recovered), quint64(0));
            QVERIFY(claimant.recoveryPath(&recovered).isEmpty());
            QCOMPARE(contents(source), before);
            QCOMPARE(contents(unrelated), before);
        }
        owner.reset(); // Session ownership ends while its published file survives.
        QVERIFY(RecoveryFiles::mayRecover(source));
        claimant.adoptRecovery(&recovered, source, recoveredRevision);
        QCOMPARE(failed.count(), legacy ? 0 : 1);
        const QString claimed = claimant.recoveryPath(&recovered);
        QVERIFY(!claimed.isEmpty());
        QVERIFY(claimed != source);
        QVERIFY(!QFile::exists(source));
        QCOMPARE(contents(claimed), before);
        QCOMPARE(claimant.publishedRevision(&recovered), recoveredRevision);
        QVERIFY(!RecoveryFiles::mayRecover(claimed));
        Document reread;
        QVERIFY2(reread.load(claimed, &error), qPrintable(error));
        QCOMPARE(reread.state.layers[0].pixels.pixelColor(0, 0), QColor(Qt::red));
        claimant.invalidate(&recovered);
        QCOMPARE(claimant.publishedRevision(&recovered), quint64(0));
        QVERIFY(!QFile::exists(claimed));
        QVERIFY(!QFile::exists(source));
        QCOMPARE(contents(unrelated), before);
    }
    void pointerReuseCannotPublishAnEarlierLifetime() {
        struct ReusedDocument {
            alignas(Document) unsigned char storage[sizeof(Document)];
            Document *current = nullptr;
            Document *construct() { return current = new (storage) Document; }
            void destroy() { if (current) { current->~Document(); current = nullptr; } }
            ~ReusedDocument() { destroy(); }
        } storage;
        QTemporaryDir directory;
        RecoveryManager manager(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager.setBackend(backendFor(control)));
        QSignalSpy published(&manager, &RecoveryManager::published);
        QSignalSpy failed(&manager, &RecoveryManager::failed);
        QSignalSpy captured(&manager, &RecoveryManager::snapshotCaptured);
        auto *first = storage.construct();
        QVERIFY(first->create({16, 12}, true));
        paint(*first, Qt::red);
        manager.registerDocument(first);
        manager.request(first);
        QTRY_COMPARE(control->entered(), 1);
        const QString oldPath = manager.recoveryPath(first);
        storage.destroy();
        auto *second = storage.construct();
        QCOMPARE(second, first);
        QVERIFY(second->create({16, 12}, true));
        paint(*second, Qt::blue);
        manager.registerDocument(second);
        manager.request(second);
        control->allow(1);
        QTRY_COMPARE(control->entered(), 2);
        QCOMPARE(captured.count(), 2);
        QVERIFY(captured[0][0].toULongLong() != captured[1][0].toULongLong());
        control->allow(2);
        QTRY_COMPARE(published.count(), 1);
        QTRY_VERIFY(!manager.busy());
        QCOMPARE(failed.count(), 0);
        QVERIFY(!QFile::exists(oldPath));
        QVERIFY(manager.recoveryPath(second) != oldPath);
        Document recovered;
        QString error;
        QVERIFY2(recovered.load(manager.recoveryPath(second), &error), qPrintable(error));
        QCOMPARE(recovered.state.layers[0].pixels.pixelColor(0, 0), QColor(Qt::blue));
        storage.destroy();
    }
    void windowSaveInvalidatesBeforeQueuedCompletion_data() {
        QTest::addColumn<bool>("saveAs");
        QTest::newRow("save-action") << false;
        QTest::newRow("save-as-action") << true;
    }
    void windowSaveInvalidatesBeforeQueuedCompletion() {
        QFETCH(bool, saveAs);
        QTemporaryDir directory;
        MainWindow window(false, false);
        auto *document = new Document;
        QVERIFY(document->create({16, 12}, true));
        QString error;
        QVERIFY2(document->save(directory.filePath("original.pdeck"), &error), qPrintable(error));
        window.addDocument(document);
        auto *manager = window.findChild<RecoveryManager *>();
        QVERIFY(manager);
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager->setBackend(backendFor(control)));
        QSignalSpy published(manager, &RecoveryManager::published);
        paint(*document, Qt::red);
        control->allow(1);
        manager->request(document);
        QTRY_COMPARE(published.count(), 1);
        const QString recovery = manager->recoveryPath(document);
        paint(*document, Qt::blue);
        manager->request(document);
        QTRY_COMPARE(control->entered(), 2);
        manager->pause();
        control->allow(2);
        QTRY_COMPARE(control->completed(), 2);
        if (saveAs) {
            bool chosen = false;
            QVERIFY(driveDialogs([&] { window.findChild<QAction *>("saveAs")->trigger(); },
                                 [&](QDialog *dialog) {
                if (auto *file = qobject_cast<QFileDialog *>(dialog)) {
                    chosen = true;
                    chooseFile(file, directory.filePath("copy.pdeck"));
                } else dialog->reject();
            }));
            QVERIFY(chosen);
            QCOMPARE(document->path, directory.filePath("copy.pdeck"));
        } else window.findChild<QAction *>("save")->trigger();
        QVERIFY(!document->dirty);
        QVERIFY(!QFile::exists(recovery));
        QCOMPARE(manager->publishedRevision(document), quint64(0));
        manager->resume();
        QTRY_VERIFY(!manager->busy());
        QCOMPARE(published.count(), 1);
        QVERIFY(!QFile::exists(recovery));
        Document saved;
        QVERIFY2(saved.load(document->path, &error), qPrintable(error));
        QCOMPARE(saved.state.layers[0].pixels.pixelColor(0, 0), QColor(Qt::blue));
    }
    void canceledOrFailedSaveKeepsPriorRecovery_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("cancel-save-as") << true;
        QTest::newRow("failed-native-save") << false;
    }
    void canceledOrFailedSaveKeepsPriorRecovery() {
        QFETCH(bool, cancel);
        QTemporaryDir directory;
        MainWindow window(false, false);
        auto *document = addDirtyDocument(window, Qt::red, "Keep recovery");
        QVERIFY(document);
        auto *manager = window.findChild<RecoveryManager *>();
        QVERIFY(manager);
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager->setBackend(backendFor(control)));
        QSignalSpy published(manager, &RecoveryManager::published);
        control->allow(1);
        manager->request(document);
        QTRY_COMPARE(published.count(), 1);
        const QString recovery = manager->recoveryPath(document);
        const QByteArray before = contents(recovery);
        const auto revisionA = manager->publishedRevision(document);
        paint(*document, Qt::blue);
        manager->request(document);
        QTRY_COMPARE(control->entered(), 2);
        manager->pause();
        control->allow(2);
        QTRY_COMPARE(control->completed(), 2);
        if (!cancel) document->path = directory.filePath("missing/project.pdeck");
        bool expectedDialog = false, retainedDuringDialog = false;
        QVERIFY(driveDialogs([&] {
            window.findChild<QAction *>(cancel ? "saveAs" : "save")->trigger();
        }, [&](QDialog *dialog) {
            expectedDialog = cancel ? bool(qobject_cast<QFileDialog *>(dialog))
                                    : bool(qobject_cast<QMessageBox *>(dialog));
            retainedDuringDialog = contents(recovery) == before &&
                                   manager->publishedRevision(document) == revisionA;
            dialog->reject();
        }));
        QVERIFY(expectedDialog);
        QVERIFY(retainedDuringDialog);
        QVERIFY(document->dirty);
        QCOMPARE(contents(recovery), before);
        QCOMPARE(manager->publishedRevision(document), revisionA);
        manager->resume();
        QTRY_COMPARE(published.count(), 2);
        QTRY_VERIFY(!manager->busy());
        QCOMPARE(manager->publishedRevision(document), document->revision());
    }
    void discardTabAndAcceptedWindowCloseInvalidateQueuedResults_data() {
        QTest::addColumn<bool>("wholeWindow");
        QTest::newRow("discard-one-tab") << false;
        QTest::newRow("accept-full-window-close") << true;
    }
    void discardTabAndAcceptedWindowCloseInvalidateQueuedResults() {
        QFETCH(bool, wholeWindow);
        MainWindow window(false, false);
        auto *first = addDirtyDocument(window, Qt::red, "First");
        auto *second = addDirtyDocument(window, Qt::green, "Second");
        QVERIFY(first && second);
        auto *manager = window.findChild<RecoveryManager *>();
        auto *tabs = window.findChild<QTabWidget *>("documents");
        QVERIFY(manager && tabs);
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager->setBackend(backendFor(control)));
        QSignalSpy published(manager, &RecoveryManager::published);
        control->allow(2);
        manager->request(first);
        manager->request(second);
        QTRY_COMPARE(published.count(), 2);
        const QString firstPath = manager->recoveryPath(first);
        const QString secondPath = manager->recoveryPath(second);
        paint(*first, Qt::blue);
        manager->request(first);
        QTRY_COMPARE(control->entered(), 3);
        QPointer<Document> firstLifetime(first), secondLifetime(second);
        tabs->setCurrentIndex(0);
        int prompts = 0;
        bool retainedWhilePaused = true, completedWhileModal = false, closeAccepted = false;
        QVERIFY(driveDialogs([&] {
            if (wholeWindow) closeAccepted = window.close();
            else window.findChild<QAction *>("close")->trigger();
        }, [&](QDialog *dialog) {
            auto *box = qobject_cast<QMessageBox *>(dialog);
            if (!box) { dialog->reject(); return; }
            ++prompts;
            control->allow(3);
            completedWhileModal = QTest::qWaitFor([&] { return control->completed() == 3; });
            retainedWhilePaused &= QFile::exists(firstPath) && QFile::exists(secondPath) &&
                                   published.count() == 2;
            clickMessage(box, QMessageBox::Discard);
        }));
        QVERIFY(completedWhileModal);
        QVERIFY(retainedWhilePaused);
        QCOMPARE(prompts, wholeWindow ? 2 : 1);
        QTRY_VERIFY(!manager->busy());
        QCOMPARE(published.count(), 2);
        QVERIFY(!QFile::exists(firstPath));
        if (wholeWindow) {
            QVERIFY(closeAccepted);
            QVERIFY(!QFile::exists(secondPath));
            QCOMPARE(manager->pendingCount(), 0);
        } else {
            QVERIFY(firstLifetime.isNull());
            QVERIFY(!secondLifetime.isNull());
            QCOMPARE(tabs->count(), 1);
            QVERIFY(QFile::exists(secondPath));
        }
    }
    void discardThenCancelCloseKeepsAllLiveRecoveriesAndResumes() {
        MainWindow window(false, false);
        auto *first = addDirtyDocument(window, Qt::red, "First");
        auto *second = addDirtyDocument(window, Qt::green, "Second");
        QVERIFY(first && second);
        auto *manager = window.findChild<RecoveryManager *>();
        auto *tabs = window.findChild<QTabWidget *>("documents");
        QVERIFY(manager && tabs);
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager->setBackend(backendFor(control)));
        QSignalSpy published(manager, &RecoveryManager::published);
        control->allow(2);
        manager->request(first);
        manager->request(second);
        QTRY_COMPARE(published.count(), 2);
        const QString firstPath = manager->recoveryPath(first);
        const QString secondPath = manager->recoveryPath(second);
        const auto firstBefore = contents(firstPath), secondBefore = contents(secondPath);
        paint(*first, Qt::blue);
        manager->request(first);
        QTRY_COMPARE(control->entered(), 3);
        QPointer<Document> firstLifetime(first), secondLifetime(second);
        int prompts = 0;
        bool pausedInEveryDialog = true, closeAccepted = true, completedWhileModal = false;
        QVERIFY(driveDialogs([&] { closeAccepted = window.close(); }, [&](QDialog *dialog) {
            auto *box = qobject_cast<QMessageBox *>(dialog);
            if (!box) { dialog->reject(); return; }
            ++prompts;
            control->allow(3);
            completedWhileModal = QTest::qWaitFor([&] { return control->completed() == 3; });
            pausedInEveryDialog &= contents(firstPath) == firstBefore &&
                                   contents(secondPath) == secondBefore && published.count() == 2;
            clickMessage(box, prompts == 1 ? QMessageBox::Discard : QMessageBox::Cancel);
        }));
        QVERIFY(completedWhileModal);
        QVERIFY(pausedInEveryDialog);
        QCOMPARE(prompts, 2);
        QVERIFY(!closeAccepted);
        QVERIFY(firstLifetime && secondLifetime);
        QCOMPARE(tabs->count(), 2);
        QVERIFY(QFile::exists(firstPath));
        QCOMPARE(contents(secondPath), secondBefore);
        QTRY_COMPARE(published.count(), 3);
        QTRY_VERIFY(!manager->busy());
        QCOMPARE(manager->publishedRevision(first), first->revision());
        QCOMPARE(manager->publishedRevision(second), second->revision());
        // It remains an operational window after the canceled close sequence.
        paint(*second, Qt::yellow);
        manager->request(second);
        QTRY_COMPARE(control->entered(), 4);
        control->allow(4);
        QTRY_COMPARE(published.count(), 4);
        QTRY_VERIFY(!manager->busy());
    }
    void directWindowDestructionPreservesPublishedAndJoinsCancellableWorker() {
        auto control = std::make_shared<BarrierBackend>();
        auto window = std::make_unique<MainWindow>(false, false);
        auto *document = addDirtyDocument(*window, Qt::red, "Crash simulation");
        QVERIFY(document);
        auto *manager = window->findChild<RecoveryManager *>();
        QVERIFY(manager);
        ReleaseOnExit release{control};
        QVERIFY(manager->setBackend(backendFor(control)));
        QSignalSpy published(manager, &RecoveryManager::published);
        control->allow(1);
        manager->request(document);
        QTRY_COMPARE(published.count(), 1);
        const QString path = manager->recoveryPath(document);
        const QByteArray before = contents(path);
        QVERIFY(!before.isEmpty());
        paint(*document, Qt::blue);
        manager->request(document);
        QTRY_COMPARE(control->entered(), 2);
        QCOMPARE(control->completed(), 1);
        window.reset(); // No event-loop delivery: cancellation itself must allow join().
        QCOMPARE(control->completed(), 2);
        QCOMPARE(control->cancellationCount(), 1);
        QCOMPARE(RecoveryManager::activeSnapshotCount(), 0);
        QCOMPARE(contents(path), before);
        QVERIFY(RecoveryFiles::mayRecover(path));
        QCOMPARE(QDir(windowRecoveryDirectory()).entryList({"*.pending*"}, QDir::Files | QDir::Hidden).size(), 0);
    }
    void publishedHandlerMayRequestTheNextRevision() {
        QTemporaryDir directory;
        Document document;
        QVERIFY(document.create({16, 12}, true));
        paint(document, Qt::red);
        RecoveryManager manager(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager.setBackend(backendFor(control)));
        manager.registerDocument(&document);
        QSignalSpy published(&manager, &RecoveryManager::published);
        QSignalSpy failed(&manager, &RecoveryManager::failed);
        bool completedDetachedBeforeSignal = false;
        connect(&manager, &RecoveryManager::published, &manager,
                [&](Document *publishedDocument, quint64) {
            if (published.count() != 1) return;
            completedDetachedBeforeSignal = !manager.busy() &&
                                            RecoveryManager::activeSnapshotCount() == 0;
            paint(*publishedDocument, Qt::blue);
            manager.request(publishedDocument);
        }, Qt::DirectConnection);
        control->allow(1);
        manager.request(&document);
        QTRY_COMPARE(control->entered(), 2);
        QVERIFY(completedDetachedBeforeSignal);
        QCOMPARE(published.count(), 1);
        QCOMPARE(control->completed(), 1);
        bool queuedAction = false;
        QTimer::singleShot(0, &manager, [&] { queuedAction = true; });
        QTRY_VERIFY(queuedAction);
        QCOMPARE(control->entered(), 2);
        control->allow(2);
        QTRY_COMPARE(published.count(), 2);
        QTRY_VERIFY(!manager.busy());
        QCOMPARE(failed.count(), 0);
        QCOMPARE(manager.publishedRevision(&document), document.revision());
        QCOMPARE(control->observation(1).before, QColor(Qt::blue));
    }
    void failedHandlerMayMutateOrDestroyManager_data() {
        QTest::addColumn<QString>("operation");
        QTest::newRow("edit-and-request") << QString("request");
        QTest::newRow("invalidate-and-stop") << QString("stop");
        QTest::newRow("delete-manager") << QString("delete");
    }
    void failedHandlerMayMutateOrDestroyManager() {
        QFETCH(QString, operation);
        QTemporaryDir directory;
        Document document;
        QVERIFY(document.create({16, 12}, true));
        paint(document, Qt::red);
        auto manager = std::make_unique<RecoveryManager>(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        control->failPreparation(1);
        QVERIFY(manager->setBackend(backendFor(control)));
        manager->registerDocument(&document);
        QSignalSpy failed(manager.get(), &RecoveryManager::failed);
        QSignalSpy published(manager.get(), &RecoveryManager::published);
        bool detachedBeforeSignal = false;
        connect(manager.get(), &RecoveryManager::failed, this,
                [&](Document *failedDocument, const QString &) {
            detachedBeforeSignal = !manager->busy() &&
                                   RecoveryManager::activeSnapshotCount() == 0;
            if (operation == "request") {
                paint(*failedDocument, Qt::blue);
                manager->request(failedDocument);
            } else if (operation == "stop") {
                manager->invalidate(failedDocument);
                manager->stop();
            } else manager.reset();
        }, Qt::DirectConnection);
        control->allow(1);
        manager->request(&document);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(detachedBeforeSignal);
        if (operation == "request") {
            QTRY_COMPARE(control->entered(), 2);
            QCOMPARE(published.count(), 0);
            control->allow(2);
            QTRY_COMPARE(published.count(), 1);
            QTRY_VERIFY(!manager->busy());
            QCOMPARE(manager->publishedRevision(&document), document.revision());
        } else {
            QCOMPARE(control->entered(), 1);
            QCOMPARE(published.count(), 0);
            QCOMPARE(RecoveryManager::activeSnapshotCount(), 0);
            if (operation == "delete") QVERIFY(!manager);
            else {
                manager->request(&document);
                QCOMPARE(manager->pendingCount(), 0);
                QVERIFY(!manager->busy());
            }
        }
    }
    void reentrantRequestSurvivesDeletionDuringDocumentChanged_data() {
        QTest::addColumn<bool>("deleteManager");
        QTest::newRow("delete-target-manager") << true;
        QTest::newRow("delete-target-document") << false;
    }
    void reentrantRequestSurvivesDeletionDuringDocumentChanged() {
        QFETCH(bool, deleteManager);
        QTemporaryDir directory;
        Document blockerDocument;
        auto targetDocument = std::make_unique<Document>();
        QVERIFY(blockerDocument.create({16, 12}, true));
        QVERIFY(targetDocument->create({16, 12}, true));
        paint(blockerDocument, Qt::red);
        paint(*targetDocument, Qt::blue);
        RecoveryManager blocker(directory.path());
        auto target = std::make_unique<RecoveryManager>(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(blocker.setBackend(backendFor(control)));
        QVERIFY(target->setBackend(backendFor(control)));
        blocker.registerDocument(&blockerDocument);
        target->registerDocument(targetDocument.get());
        QSignalSpy targetCaptured(target.get(), &RecoveryManager::snapshotCaptured);
        QSignalSpy targetPublished(target.get(), &RecoveryManager::published);
        QSignalSpy blockerPublished(&blocker, &RecoveryManager::published);
        bool deletionHandlerRan = false, pendingRequestExercised = false;
        connect(target.get(), &RecoveryManager::snapshotCaptured, this,
                [&](quint64, quint64, qint64, qint64) {
            deletionHandlerRan = true;
            if (deleteManager) target.reset();
            else targetDocument.reset();
        }, Qt::DirectConnection);
        connect(&blocker, &RecoveryManager::published, this, [&](Document *, quint64) {
            pendingRequestExercised = target->pendingCount() == 1 &&
                                      RecoveryManager::activeSnapshotCount() == 0;
            // The ID was already pending while blocker held global admission.
            // documentChanged() now pumps synchronously inside request(), and its
            // snapshotCaptured handler destroys request()'s receiver or argument.
            target->request(targetDocument.get());
        }, Qt::DirectConnection);
        blocker.request(&blockerDocument);
        QTRY_COMPARE(control->entered(), 1);
        target->request(targetDocument.get());
        QCOMPARE(target->pendingCount(), 1);
        QCOMPARE(targetCaptured.count(), 0);
        control->allow(1);
        QTRY_COMPARE(blockerPublished.count(), 1);
        QVERIFY(pendingRequestExercised);
        QVERIFY(deletionHandlerRan);
        QCOMPARE(targetCaptured.count(), 1);
        QCOMPARE(targetPublished.count(), 0);
        QTRY_COMPARE(control->completed(), 2);
        if (deleteManager) QVERIFY(!target);
        else {
            QVERIFY(!targetDocument);
            QTRY_VERIFY(!target->busy());
            QCOMPARE(target->pendingCount(), 0);
        }
        QCOMPARE(RecoveryManager::activeSnapshotCount(), 0);
    }
    void snapshotCapturedHandlerMayStopOrDeleteManager_data() {
        QTest::addColumn<bool>("destroy");
        QTest::newRow("stop-manager") << false;
        QTest::newRow("delete-manager") << true;
    }
    void snapshotCapturedHandlerMayStopOrDeleteManager() {
        QFETCH(bool, destroy);
        QTemporaryDir directory;
        Document document;
        QVERIFY(document.create({16, 12}, true));
        paint(document, Qt::red);
        auto manager = std::make_unique<RecoveryManager>(directory.path());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager->setBackend(backendFor(control)));
        manager->registerDocument(&document);
        QSignalSpy captured(manager.get(), &RecoveryManager::snapshotCaptured);
        QSignalSpy published(manager.get(), &RecoveryManager::published);
        bool installedBeforeSignal = false;
        connect(manager.get(), &RecoveryManager::snapshotCaptured, this,
                [&](quint64, quint64, qint64, qint64) {
            installedBeforeSignal = manager->busy() &&
                                    RecoveryManager::activeSnapshotCount() == 1;
            if (destroy) manager.reset();
            else manager->stop();
        }, Qt::DirectConnection);
        manager->request(&document);
        QVERIFY(installedBeforeSignal);
        QCOMPARE(captured.count(), 1);
        QTRY_COMPARE(control->completed(), 1);
        QCOMPARE(control->cancellationCount(), 1);
        QCOMPARE(published.count(), 0);
        if (destroy) QVERIFY(!manager);
        else QTRY_VERIFY(!manager->busy());
        QCOMPARE(RecoveryManager::activeSnapshotCount(), 0);
        QVERIFY(QDir(directory.path()).entryList({"*.pdeck"}, QDir::Files).isEmpty());
    }
    void actualTimersDeferUnfinishedGesturesUntilCommitted() {
        MainWindow window(false, false);
        auto *document = addDirtyDocument(window, Qt::red, "Deferred stroke");
        QVERIFY(document);
        auto *manager = window.findChild<RecoveryManager *>();
        auto *periodic = window.findChild<QTimer *>("autosaveTimer");
        auto *afterEdit = window.findChild<QTimer *>("recoverySaveTimer");
        QVERIFY(manager && periodic && afterEdit);
        QCOMPARE(periodic->interval(), 45000);
        QCOMPARE(afterEdit->interval(), 5000);
        QVERIFY(afterEdit->isActive());
        auto control = std::make_shared<BarrierBackend>();
        ReleaseOnExit release{control};
        QVERIFY(manager->setBackend(backendFor(control)));
        QSignalSpy captured(manager, &RecoveryManager::snapshotCaptured);
        QSignalSpy published(manager, &RecoveryManager::published);
        document->beginEdit();
        document->state.layers[0].pixels.fill(Qt::cyan);
        document->changed();
        QVERIFY(QMetaObject::invokeMethod(afterEdit, "timeout", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(periodic, "timeout", Qt::DirectConnection));
        QCoreApplication::processEvents();
        QCOMPARE(captured.count(), 0);
        QCOMPARE(manager->pendingCount(), 0);
        QVERIFY(document->isEditing());
        document->commit("Finish deferred stroke");
        QTRY_COMPARE(control->entered(), 1);
        QCOMPARE(captured.count(), 1);
        QCOMPARE(control->observation(0).before, QColor(Qt::cyan));
        control->allow(1);
        QTRY_COMPARE(published.count(), 1);
        QTRY_VERIFY(!manager->busy());
        Document recovered;
        QString error;
        QVERIFY2(recovered.load(manager->recoveryPath(document), &error), qPrintable(error));
        QCOMPARE(recovered.state.layers[0].pixels.pixelColor(0, 0), QColor(Qt::cyan));
        QVERIFY(document->dirty);
    }
};
QTEST_MAIN(RecoveryTests)
#include "test_recovery.moc"
