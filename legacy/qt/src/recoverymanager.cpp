#include "recoverymanager.h"
#include "recoveryfiles.h"
#include <QDir>
#include <QElapsedTimer>
#include <QDebug>
#include <QFile>
#include <QSaveFile>
#include <QScopeGuard>
#include <QThread>
#include <algorithm>
#include <exception>
#include <filesystem>

namespace {
// Admission is taken on the GUI thread, before capture. It stays taken through
// result disposal; other windows wait with ID-only requests, never snapshots.
std::atomic_bool occupied{false};
void removePublishedFile(const QString &path) {
    if (path.isEmpty()) return;
    QFile file(path);
    if (file.remove()) return;
    std::error_code inspectionError;
    // A missing copy is expected after an earlier clean/save invalidation. Other
    // errors are actionable, but must never turn a successful project Save into
    // failure or weaken the epoch guard. Never remove a directory recursively.
    if (std::filesystem::exists(file.filesystemFileName(), inspectionError) || inspectionError)
        qWarning().noquote() << "Could not remove obsolete recovery copy:" << path
                             << file.errorString()
                             << "The copy may need to be removed from the recovery directory later.";
}
qint64 retainedBytes(const State &state) {
    qint64 bytes = 0;
    for (const auto &layer : state.layers)
        bytes += layer.pixels.sizeInBytes() + layer.mask.sizeInBytes();
    bytes += state.selection.coverageImage().sizeInBytes();
    bytes += state.previousSelection.coverageImage().sizeInBytes();
    for (const auto &saved : state.savedSelections)
        bytes += saved.selection.coverageImage().sizeInBytes();
    return bytes;
}
bool prepareSnapshot(const State &state, const QString &path, QString *error,
                     const std::atomic_bool &cancel) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    if (!Document::writeRecoverySnapshot(state, file, error, &cancel))
        return false;
    if (cancel.load(std::memory_order_relaxed))
        return false;
    // QSaveFile flushes/synchronizes/closes and publishes only the unique pending
    // file here, on the worker. GUI publication is a metadata-only native rename.
    if (!file.flush() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return !cancel.load(std::memory_order_relaxed);
}
}
struct RecoveryManager::Job {
    quint64 id = 0, epoch = 0, revision = 0;
    State snapshot;
    QString staging, target, error;
    std::atomic_bool cancel{false}, done{false};
    bool success = false;
    ~Job() { if (!staging.isEmpty()) QFile::remove(staging); }
};
RecoveryManager::RecoveryManager(const QString &directory, QObject *parent)
    : QObject(parent), directory(QDir(directory).absolutePath()) {
    setObjectName("recoveryManager");
    poll.setInterval(10);
    connect(&poll, &QTimer::timeout, this, &RecoveryManager::pump);
}
RecoveryManager::~RecoveryManager() {
    stop();
    // No mutex or GUI delivery is needed by the worker. PNG encoding and OS file
    // synchronization may be non-interruptible; cancellation is cooperative.
    releaseJob();
}
RecoveryManager::Entry *RecoveryManager::entry(Document *document) {
    auto id = ids.constFind(document);
    if (id == ids.cend()) return nullptr;
    auto found = entries.find(*id);
    return found == entries.end() ? nullptr : &found.value();
}
const RecoveryManager::Entry *RecoveryManager::entry(Document *document) const {
    auto id = ids.constFind(document);
    if (id == ids.cend()) return nullptr;
    auto found = entries.constFind(*id);
    return found == entries.cend() ? nullptr : &found.value();
}
void RecoveryManager::registerDocument(Document *document) {
    if (!accepting || !document || ids.contains(document)) return;
    Entry value;
    value.document = document;
    value.id = nextId++;
    value.wasDirty = document->dirty;
    ids.insert(document, value.id);
    entries.insert(value.id, value);
    connect(document, &Document::updated, this,
            [this, document] { documentChanged(document); });
    connect(document, &QObject::destroyed, this,
            [this, document] { forgetDocument(document); });
}
void RecoveryManager::adoptRecovery(Document *document, const QString &path, quint64 revision) {
    registerDocument(document);
    if (auto *value = entry(document)) {
        // Adoption is an initialization step, before this document starts its own
        // recovery history. Never retarget an already installed/in-flight job.
        if (!value->path.isEmpty() || (job && job->id == value->id)) {
            emit failed(document, "Belge kurtarma oturumu zaten başlatıldı.");
            return;
        }
        if (!session || !session->valid())
            session = std::make_unique<RecoveryFiles::Session>(directory);
        QString error;
        if (!session->valid() || !RecoveryFiles::mayRecover(path)) {
            emit failed(document, session->valid() ? "Kurtarma kopyası başka bir oturumda kullanılıyor."
                                                   : session->errorString());
            return;
        }
        // Claim an inactive old pathname into this session before any later clean
        // transition or close may delete it. A competing claimant can only fail.
        const QString claimed = session->recoveryPath(value->id);
        if (!RecoveryFiles::atomicReplace(path, claimed, &error)) {
            emit failed(document, error);
            return;
        }
        value->path = claimed;
        value->published = revision;
        value->requested = false;
    }
}
void RecoveryManager::documentChanged(Document *document) {
    auto *value = entry(document);
    if (!value || !accepting) return;
    if (!document->dirty) {
        if (value->wasDirty || value->published || (job && job->id == value->id))
            invalidate(document);
        value->wasDirty = false;
        return;
    }
    value->wasDirty = true;
    // Once A is active, newer revisions coalesce to one current-ID request. Do
    // not call that revision saved when A completes. An unfinished gesture is
    // skipped by dispatch and its completed updated() will resume the request.
    if (job && job->id == value->id && job->epoch == value->epoch &&
        job->revision != document->revision())
        value->requested = true;
    if (value->requested && !document->isEditing()) pump();
}
void RecoveryManager::request(Document *document) {
    auto *value = entry(document);
    if (!value || !accepting) return;
    const QPointer<RecoveryManager> self(this);
    const QPointer<Document> liveDocument(document);
    documentChanged(document); // May deliver a completed result to direct signal slots.
    if (!self || !liveDocument) return;
    value = entry(document);
    if (!value || !document->dirty || document->isEditing() || value->published == document->revision())
        return;
    // Repeated autosave callbacks during A do not queue A again. A failed attempt
    // is retried only by a later explicit schedule, never by completion itself.
    if (!job || job->id != value->id || job->epoch != value->epoch ||
        job->revision != document->revision())
        value->requested = true;
    pump();
}
void RecoveryManager::invalidate(Document *document) {
    auto *value = entry(document);
    if (!value) return;
    ++value->epoch;
    value->requested = false;
    value->published = 0;
    if (job && job->id == value->id)
        job->cancel.store(true, std::memory_order_relaxed);
    removePublishedFile(value->path);
}
void RecoveryManager::forgetDocument(Document *document) {
    const quint64 id = ids.value(document);
    invalidate(document);
    ids.remove(document);
    entries.remove(id);
}
void RecoveryManager::pause() { ++pauseDepth; }
void RecoveryManager::resume() {
    if (pauseDepth > 0) --pauseDepth;
    if (!pauseDepth) pump();
}
void RecoveryManager::stop(bool removePublished) {
    accepting = false;
    poll.stop();
    for (auto &value : entries) {
        ++value.epoch;
        value.requested = false;
        if (removePublished) {
            value.published = 0;
            removePublishedFile(value.path);
        }
    }
    if (job) {
        job->cancel.store(true, std::memory_order_relaxed);
        poll.start(); // Drain a canceled result even if an accepted window stays alive.
    }
}
QString RecoveryManager::recoveryPath(Document *document) const {
    const auto *value = entry(document);
    return value ? value->path : QString();
}
quint64 RecoveryManager::publishedRevision(Document *document) const {
    const auto *value = entry(document);
    return value ? value->published : 0;
}
bool RecoveryManager::busy() const { return bool(job); }
int RecoveryManager::pendingCount() const {
    int count = 0;
    for (const auto &value : entries) if (value.requested) ++count;
    return count;
}
int RecoveryManager::activeSnapshotCount() { return occupied.load(std::memory_order_acquire) ? 1 : 0; }
bool RecoveryManager::setBackend(Backend value) {
    if (job || pendingCount()) return false;
    backend = std::move(value);
    return true;
}
bool RecoveryManager::hasRunnable() const {
    for (const auto &value : entries)
        if (value.requested && value.document && value.document->dirty &&
            !value.document->isEditing()) return true;
    return false;
}
void RecoveryManager::releaseJob() {
    if (worker.joinable()) worker.join();
    job.reset();
    if (ownsSlot) {
        ownsSlot = false;
        occupied.store(false, std::memory_order_release);
    }
}
void RecoveryManager::pump() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (pumping) return;
    pumping = true;
    const QPointer<RecoveryManager> self(this);
    const auto resetPumping = qScopeGuard([self] { if (self) self->pumping = false; });
    if (!accepting) {
        if (job && job->done.load(std::memory_order_acquire)) releaseJob();
        if (!job) poll.stop();
        return;
    }
    if (pauseDepth) return;
    if (job) {
        if (!job->done.load(std::memory_order_acquire)) {
            if (!poll.isActive()) poll.start();
            return;
        }
        // Detach the completed value and release admission BEFORE external signals.
        // A direct slot may edit/save/close or delete this manager. No installed job
        // can then be consumed twice or accidentally join a newly dispatched worker.
        worker.join();
        auto completed = std::move(job);
        ownsSlot = false;
        occupied.store(false, std::memory_order_release);
        auto found = entries.find(completed->id);
        QPointer<Document> document;
        quint64 publishedRevision = 0;
        QString failure;
        if (found != entries.end() && found->epoch == completed->epoch && found->document &&
            found->document->dirty && !completed->cancel.load(std::memory_order_relaxed)) {
            document = found->document;
            QString error = completed->error;
            const bool ok = completed->success &&
                (backend.publish ? backend.publish(completed->staging, completed->target, &error)
                                 : RecoveryFiles::atomicReplace(completed->staging, completed->target,
                                                                &error));
            if (ok) {
                found->published = completed->revision;
                publishedRevision = completed->revision;
            } else if (document->revision() == completed->revision) {
                // An error for A is obsolete once B is current. Don't overwrite a
                // newer document's status, or spin retrying this failed revision.
                failure = error.isEmpty() ? "Kurtarma kopyası yazılamadı." : error;
            }
        }
        completed.reset(); // Clean staging before handing control to external slots.
        if (document && publishedRevision) emit published(document, publishedRevision);
        else if (document && !failure.isEmpty()) emit failed(document, failure);
        if (!self) return;
    }
    if (!accepting || pauseDepth) { if (!job) poll.stop(); return; }
    for (auto &value : entries)
        if (value.requested && value.document && value.published == value.document->revision())
            value.requested = false;
    if (!hasRunnable()) { poll.stop(); return; }
    bool expected = false;
    if (!occupied.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
        if (!poll.isActive()) poll.start();
        return;
    }
    ownsSlot = true;
    QPointer<Document> capturedDocument;
    try {
        // Stable ID round-robin is independent of QHash iteration and tab switching.
        QList<quint64> candidates;
        for (const auto &value : entries)
            if (value.requested && value.document && value.document->dirty &&
                !value.document->isEditing()) candidates.append(value.id);
        std::sort(candidates.begin(), candidates.end());
        auto next = std::upper_bound(candidates.begin(), candidates.end(), cursor);
        const quint64 id = next == candidates.end() ? candidates.first() : *next;
        cursor = id;
        auto &value = entries[id];
        value.requested = false;
        capturedDocument = value.document;
        if (!session || !session->valid())
            session = std::make_unique<RecoveryFiles::Session>(directory);
        if (!session->valid()) {
            const QString error = session->errorString();
            releaseJob();
            if (capturedDocument) emit failed(capturedDocument, error);
            if (!self) return;
            if (hasRunnable()) poll.start(); else poll.stop();
            return;
        }
        if (value.path.isEmpty()) value.path = session->recoveryPath(value.id);
        job = std::make_unique<Job>();
        job->id = id;
        job->epoch = value.epoch;
        job->revision = capturedDocument->revision();
        job->target = value.path;
        job->staging = session->stagingPath(nextJob++);
        QElapsedTimer capture;
        capture.start();
        job->snapshot = Document::recoverySnapshot(capturedDocument->state);
        const qint64 nanoseconds = capture.nsecsElapsed();
        const qint64 retained = retainedBytes(job->snapshot);
        const quint64 revision = job->revision;
        Job *output = job.get();
        auto prepare = backend.prepare;
        worker = std::thread([output, prepare] {
            try {
                output->success = prepare ? prepare(output->snapshot, output->staging, &output->error,
                                                    output->cancel)
                                          : prepareSnapshot(output->snapshot, output->staging,
                                                            &output->error, output->cancel);
            } catch (const std::exception &error) {
                output->error = QString::fromUtf8(error.what());
            } catch (...) {
                output->error = "Kurtarma kopyası yazılamadı.";
            }
            // Release all shared image references before the next snapshot can be admitted.
            output->snapshot = {};
            output->done.store(true, std::memory_order_release);
        });
        // The worker is installed before a direct handler can stop/delete us. Its
        // cooperative cancellation and join never depend on that handler returning.
        emit snapshotCaptured(id, revision, nanoseconds, retained);
        if (!self) return;
    } catch (const std::exception &error) {
        const QString message = QString::fromUtf8(error.what());
        if (job) job->cancel.store(true, std::memory_order_relaxed);
        releaseJob();
        if (capturedDocument) emit failed(capturedDocument, message);
        if (!self) return;
    }
    if (job || hasRunnable()) poll.start(); else poll.stop();
}
