#pragma once

#include <QString>
#include <QtGlobal>
#include <memory>

class QLockFile;

namespace RecoveryFiles {
// The caller must finish and close staging before publishing it. Both paths must
// be distinct regular files (destination may not exist) in the same directory.
// A failed publication never removes or truncates the previous destination.
bool atomicReplace(const QString &staging, const QString &destination, QString *error = nullptr);

// Managed recovery files can be adopted only after their session has ended.
// Legacy filenames remain eligible. A missing lock means the unique session has
// released ownership; unreadable or otherwise unverifiable locks are preserved.
bool mayRecover(const QString &path);

class Session {
  public:
    explicit Session(const QString &recoveryDirectory);
    // The caller must join every worker using this session before destroying it.
    ~Session();

    QString stagingPath(quint64 jobId) const;
    QString recoveryPath(quint64 documentId) const;
    bool valid() const;
    QString errorString() const;

  private:
    Q_DISABLE_COPY_MOVE(Session)
    QString directory, identifier, error;
    std::unique_ptr<QLockFile> lock;
};
} // namespace RecoveryFiles
