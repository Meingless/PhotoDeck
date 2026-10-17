#pragma once
#include "document.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace RecoveryFiles { class Session; }

// GUI-owned coordinator. Queued work contains IDs only; just one process-wide slot
// may own a full immutable snapshot and encoder at a time. The worker never sees a
// Document/QWidget and shutdown never requires a queued GUI callback.
class RecoveryManager : public QObject {
    Q_OBJECT
  public:
    struct Backend {
        // Optional deterministic-test injection points (worker/GUI respectively).
        // prepare consumes only values and must cooperate with cancellation. publish
        // must be a synchronous file operation: no event processing, signals, manager
        // mutation, or other callbacks between validation and the native replacement.
        std::function<bool(const State &, const QString &, QString *, const std::atomic_bool &)> prepare;
        std::function<bool(const QString &, const QString &, QString *)> publish;
    };
    explicit RecoveryManager(const QString &directory, QObject *parent = nullptr);
    ~RecoveryManager() override;
    void registerDocument(Document *document);
    // Claim an inactive recovery for a newly registered document, before request().
    void adoptRecovery(Document *document, const QString &path, quint64 revision);
    void documentChanged(Document *document);
    void request(Document *document);
    void invalidate(Document *document); // Invalidate BEFORE removing its published file.
    void forgetDocument(Document *document);
    void pause();
    void resume();
    void stop(bool removePublished = false);
    QString recoveryPath(Document *document) const;
    quint64 publishedRevision(Document *document) const;
    bool busy() const;
    int pendingCount() const;
    static int activeSnapshotCount();
    bool setBackend(Backend backend); // Only when idle, before requesting work.
  signals:
    void published(Document *document, quint64 revision);
    void failed(Document *document, const QString &error);
    void snapshotCaptured(quint64 documentId, quint64 revision, qint64 nanoseconds,
                          qint64 retainedImageBytes);
  private:
    struct Entry {
        QPointer<Document> document;
        quint64 id = 0, epoch = 1, published = 0;
        QString path;
        bool requested = false, wasDirty = false;
    };
    struct Job;
    QHash<quint64, Entry> entries;
    QHash<Document *, quint64> ids;
    quint64 nextId = 1, nextJob = 1, cursor = 0;
    int pauseDepth = 0;
    bool accepting = true, ownsSlot = false, pumping = false;
    QString directory;
    Backend backend;
    std::unique_ptr<RecoveryFiles::Session> session;
    std::unique_ptr<Job> job;
    std::thread worker;
    QTimer poll;
    Entry *entry(Document *document);
    const Entry *entry(Document *document) const;
    void pump();
    void releaseJob();
    bool hasRunnable() const;
};
