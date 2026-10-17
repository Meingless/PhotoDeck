#include "recoveryfiles.h"
#include "recoveryidentity_p.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSysInfo>
#include <QUuid>
#include <filesystem>
#include <limits>
#include <system_error>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstring>
#include <new>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#endif

namespace {
constexpr int MaxSessionsPerCleanup = 64;
constexpr int MaxFilesPerCleanup = 512;
constexpr int MaxEntriesPerCleanup = 4096;
constexpr qint64 MaxLockBytes = 4096;

const QString &uuidPattern() {
    static const QString pattern =
        QStringLiteral("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");
    return pattern;
}

const QRegularExpression &lockPattern() {
    static const QRegularExpression pattern(
        QStringLiteral("^\\.photodeck-(%1)\\.lock$").arg(uuidPattern()));
    return pattern;
}

const QRegularExpression &recoveryPattern() {
    static const QRegularExpression pattern(
        QStringLiteral("^photodeck-(%1)-([0-9]+)\\.pdeck$").arg(uuidPattern()));
    return pattern;
}

bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}

bool validIdentifier(const QString &identifier) {
    const QUuid uuid(identifier);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == identifier;
}

QString lockPath(const QString &directory, const QString &identifier) {
    return QDir(directory).filePath(QStringLiteral(".photodeck-%1.lock").arg(identifier));
}

bool processIsGone(qint64 pid) {
    if (pid <= 0)
        return false;
#ifdef Q_OS_WIN
    if (quint64(pid) > std::numeric_limits<DWORD>::max())
        return false;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!process)
        return GetLastError() == ERROR_INVALID_PARAMETER;
    const bool gone = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    CloseHandle(process);
    return gone;
#else
    if (pid > std::numeric_limits<pid_t>::max())
        return false;
    return ::kill(pid_t(pid), 0) == -1 && errno == ESRCH;
#endif
}

QByteArray lockContents(const QString &path) {
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() <= 0 || info.size() > MaxLockBytes)
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.read(MaxLockBytes + 1);
    return bytes.size() <= MaxLockBytes ? bytes : QByteArray();
}

// Never use QLockFile's age heuristic: recovery serialization can legitimately
// take longer than its default stale-lock interval. Permission errors, foreign
// identities and malformed metadata cannot establish that the old owner has died.
bool ownerIsGone(const QString &path, QByteArray *verifiedContents = nullptr) {
    const QByteArray before = lockContents(path);
    if (before.isEmpty())
        return false;
    QLockFile candidate(path);
    candidate.setStaleLockTime(0);
    qint64 pid = 0;
    QString hostname, application;
    if (!candidate.getLockInfo(&pid, &hostname, &application) || application.isEmpty())
        return false;

    // Qt records machine and boot identifiers after the three public fields.
    // A hostname alone is insufficient: a shared directory can be mounted on
    // another machine with the same hostname. Exact machine identity takes
    // precedence over hostname differences; boot-only fallback is stricter.
    const QList<QByteArray> fields = before.split('\n');
    const RecoveryFiles::Internal::Identity owner{
        hostname, fields.size() > 3 ? fields[3].trimmed() : QByteArray(),
        fields.size() > 4 ? fields[4].trimmed() : QByteArray()};
    const RecoveryFiles::Internal::Identity local{
        QSysInfo::machineHostName(), QSysInfo::machineUniqueId(), QSysInfo::bootUniqueId()};
    if (!RecoveryFiles::Internal::ownerIdentityIsLocal(owner, local))
        return false;
    if (!processIsGone(pid) || before != lockContents(path))
        return false;
    if (verifiedContents)
        *verifiedContents = before;
    return true;
}

bool isStagingName(const QString &name, const QString &identifier) {
    // QSaveFile may leave its target plus a six-character temporary suffix if
    // the process dies before commit. No other extensions belong to this owner.
    const QRegularExpression pattern(
        QStringLiteral("^\\.photodeck-%1-(0|[1-9][0-9]{0,19})\\.pending(?:\\.[A-Za-z0-9]{6})?$")
            .arg(QRegularExpression::escape(identifier)));
    const auto match = pattern.match(name);
    bool validNumber = false;
    if (match.hasMatch())
        match.captured(1).toULongLong(&validNumber);
    return validNumber;
}

// Keep an unfinished orphan's metadata so another bounded startup pass can
// finish it. Final .pdeck files are deliberately outside this namespace.
bool removeStaging(const QString &directory, const QString &identifier, int *fileBudget,
                   int *entryBudget, const QString &ownerPath = {}, const QByteArray &owner = {}) {
    // Count all directory entries, rather than relying on a name filter whose
    // iterator could scan an unbounded number of unrelated files internally.
    QDirIterator entries(directory,
                         QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    bool complete = true;
    while (entries.hasNext()) {
        if (*entryBudget <= 0 || *fileBudget <= 0)
            return false;
        const QString path = entries.next();
        --*entryBudget;
        if (!isStagingName(entries.fileName(), identifier))
            continue;
        if (!ownerPath.isEmpty() && owner != lockContents(ownerPath))
            return false;
        const QFileInfo info = entries.fileInfo();
        // Do not follow links or recurse into a same-named directory.
        if (!info.isFile() || info.isSymLink()) {
            complete = false;
            continue;
        }
        --*fileBudget;
        if (!QFile::remove(path) && QFileInfo::exists(path))
            complete = false;
    }
    return complete;
}

void removeOrphans(const QString &directory) {
    int sessions = MaxSessionsPerCleanup;
    int files = MaxFilesPerCleanup;
    int entries = MaxEntriesPerCleanup;
    QDirIterator locks(directory,
                       QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    while (sessions > 0 && files > 0 && entries > 0 && locks.hasNext()) {
        const QString path = locks.next();
        --entries;
        const auto match = lockPattern().match(locks.fileName());
        if (!match.hasMatch() || !validIdentifier(match.captured(1)))
            continue;
        QByteArray owner;
        if (!ownerIsGone(path, &owner))
            continue;
        --sessions;
        if (removeStaging(directory, match.captured(1), &files, &entries, path, owner) &&
            owner == lockContents(path)) {
            QLockFile stale(path);
            stale.setStaleLockTime(0);
            // Qt's direct stale removal does not recheck identity. Invoke it
            // only after native process-liveness checks and complete cleanup.
            stale.removeStaleLockFile();
        }
    }
}

#ifdef Q_OS_WIN
QString extendedWindowsPath(const QString &path) {
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (native.startsWith(QStringLiteral("\\\\?\\")))
        return native;
    if (native.startsWith(QStringLiteral("\\\\")))
        return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    return QStringLiteral("\\\\?\\") + native;
}

QString windowsError(DWORD code) {
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    const QString message = length ? QString::fromWCharArray(buffer, int(length)).trimmed() : QString();
    if (buffer)
        LocalFree(buffer);
    return QStringLiteral("Windows error %1%2").arg(code).arg(
        message.isEmpty() ? QString() : QStringLiteral(": ") + message);
}

bool renameWindowsFile(const std::wstring &source, const std::wstring &target, DWORD *error) {
#ifdef FILE_RENAME_FLAG_POSIX_SEMANTICS
    // Legacy replacement refuses an open destination, even when every reader
    // grants FILE_SHARE_DELETE. POSIX replacement keeps those old handles valid
    // and makes subsequent opens resolve to the complete new file instead.
    const size_t nameBytes = target.size() * sizeof(wchar_t);
    if (nameBytes > std::numeric_limits<DWORD>::max() - sizeof(FILE_RENAME_INFO)) {
        *error = ERROR_FILENAME_EXCED_RANGE;
        return false;
    }
    const DWORD bufferBytes = DWORD(sizeof(FILE_RENAME_INFO) + nameBytes);
    auto buffer = std::make_unique<char[]>(bufferBytes);
    auto *rename = new (buffer.get()) FILE_RENAME_INFO{};
    rename->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
    rename->FileNameLength = DWORD(nameBytes);
    std::memcpy(rename->FileName, target.data(), nameBytes);
    HANDLE handle = CreateFileW(source.c_str(), DELETE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return false;
    }
    const bool renamed = SetFileInformationByHandle(handle, FileRenameInfoEx, rename, bufferBytes);
    const DWORD code = renamed ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (renamed)
        return true;
    // Older Windows/filesystems can reject this information class or flags.
    // Only then try the legacy atomic rename once; genuine access/sharing/I/O
    // failures are reported as-is. Never wait or retry a busy file on the GUI.
    if (code != ERROR_INVALID_PARAMETER && code != ERROR_INVALID_FUNCTION &&
        code != ERROR_NOT_SUPPORTED) {
        *error = code;
        return false;
    }
#endif
    // No COPY_ALLOWED fallback, attribute changes, or remove-then-rename window.
    if (MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
        return true;
    *error = GetLastError();
    return false;
}

bool sameWindowsObject(const QString &first, const QString &second, bool *same, DWORD *error) {
    const auto information = [error](const QString &path, BY_HANDLE_FILE_INFORMATION *info) {
        const std::wstring native = extendedWindowsPath(path).toStdWString();
        HANDLE handle =
            CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            *error = GetLastError();
            return false;
        }
        const bool success = GetFileInformationByHandle(handle, info);
        if (!success)
            *error = GetLastError();
        CloseHandle(handle);
        return success;
    };
    BY_HANDLE_FILE_INFORMATION firstInfo{}, secondInfo{};
    if (!information(first, &firstInfo) || !information(second, &secondInfo))
        return false;
    *same = firstInfo.dwVolumeSerialNumber == secondInfo.dwVolumeSerialNumber &&
            firstInfo.nFileIndexHigh == secondInfo.nFileIndexHigh &&
            firstInfo.nFileIndexLow == secondInfo.nFileIndexLow;
    return true;
}
#endif
} // namespace

namespace RecoveryFiles {
bool atomicReplace(const QString &staging, const QString &destination, QString *error) {
    const QFileInfo source(staging), target(destination);
    if (staging.isEmpty() || destination.isEmpty() || staging.contains(QChar::Null) ||
        destination.contains(QChar::Null))
        return fail(error, QStringLiteral("Recovery publication requires two valid file paths."));
    if (!source.isFile() || source.isSymLink())
        return fail(error, QStringLiteral("Recovery staging must be an existing regular file."));
    if (target.isSymLink() || (target.exists() && !target.isFile()))
        return fail(error, QStringLiteral("Recovery destination must be a regular file or a new path."));
    const QString sourceDirectory = source.absoluteDir().canonicalPath();
    const QString targetDirectory = target.absoluteDir().canonicalPath();
    if (sourceDirectory.isEmpty() || targetDirectory.isEmpty())
        return fail(error,
                    QStringLiteral("Recovery publication requires the same canonical directory."));
#ifdef Q_OS_WIN
    bool sameDirectory = false;
    DWORD identityError = 0;
    if (!sameWindowsObject(sourceDirectory, targetDirectory, &sameDirectory, &identityError))
        return fail(error, QStringLiteral("Could not inspect recovery directory: %1.")
                               .arg(windowsError(identityError)));
    if (!sameDirectory)
#else
    if (sourceDirectory != targetDirectory)
#endif
        return fail(error,
                    QStringLiteral("Recovery publication requires the same canonical directory."));
    const QString sourcePath = QDir(sourceDirectory).filePath(source.fileName());
    const QString targetPath = QDir(targetDirectory).filePath(target.fileName());
    if (sourcePath == targetPath)
        return fail(error, QStringLiteral("Recovery staging and destination must be distinct files."));
#ifndef Q_OS_WIN
    QFile sourceFile(sourcePath), targetFile(targetPath);
#endif
    if (target.exists()) {
#ifdef Q_OS_WIN
        bool sameFile = false;
        if (!sameWindowsObject(sourcePath, targetPath, &sameFile, &identityError))
            return fail(error, QStringLiteral("Could not inspect recovery destination: %1.")
                                   .arg(windowsError(identityError)));
        if (sameFile)
            return fail(error,
                        QStringLiteral("Recovery staging and destination refer to the same file."));
#else
        std::error_code equivalenceError;
        if (std::filesystem::equivalent(sourceFile.filesystemFileName(),
                                       targetFile.filesystemFileName(), equivalenceError))
            return fail(error,
                        QStringLiteral("Recovery staging and destination refer to the same file."));
        if (equivalenceError)
            return fail(error, QStringLiteral("Could not inspect recovery destination: %1 (%2).")
                                   .arg(QString::fromStdString(equivalenceError.message()))
                                   .arg(equivalenceError.value()));
#endif
    }
#ifdef Q_OS_WIN
    const std::wstring sourceNative = extendedWindowsPath(sourcePath).toStdWString();
    const std::wstring targetNative = extendedWindowsPath(targetPath).toStdWString();
    DWORD code = 0;
    if (!renameWindowsFile(sourceNative, targetNative, &code)) {
        return fail(error, QStringLiteral("Could not atomically publish recovery file: %1.")
                               .arg(windowsError(code)));
    }
#else
    std::error_code renameError;
    std::filesystem::rename(sourceFile.filesystemFileName(), targetFile.filesystemFileName(),
                            renameError);
    if (renameError)
        return fail(error, QStringLiteral("Could not atomically publish recovery file: %1 (%2).")
                               .arg(QString::fromStdString(renameError.message()))
                               .arg(renameError.value()));
#endif
    if (error)
        error->clear();
    return true;
}

bool mayRecover(const QString &path) {
    const QFileInfo file(path);
    if (!file.isFile() || file.isSymLink())
        return false;
    const auto match = recoveryPattern().match(file.fileName());
    if (!match.hasMatch() || !validIdentifier(match.captured(1)))
        return true;
    bool validNumber = false;
    const quint64 number = match.captured(2).toULongLong(&validNumber);
    if (!validNumber || QString::number(number) != match.captured(2))
        return false;
    const QString ownerPath = lockPath(file.absolutePath(), match.captured(1));
    const QFileInfo owner(ownerPath);
    // isSymLink also detects dangling links, which must not be mistaken for an
    // absent lock. Session UUIDs are never reused after ownership is released.
    return (!owner.exists() && !owner.isSymLink()) || ownerIsGone(ownerPath);
}

Session::Session(const QString &recoveryDirectory) {
    if (recoveryDirectory.isEmpty() || recoveryDirectory.contains(QChar::Null) ||
        !QDir().mkpath(recoveryDirectory)) {
        error = QStringLiteral("Could not create the recovery directory.");
        return;
    }
    directory = QDir(recoveryDirectory).canonicalPath();
    if (directory.isEmpty()) {
        error = QStringLiteral("Could not resolve the recovery directory.");
        return;
    }
    removeOrphans(directory);
    identifier = QUuid::createUuid().toString(QUuid::WithoutBraces);
    lock = std::make_unique<QLockFile>(lockPath(directory, identifier));
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0)) {
        error = QStringLiteral("Could not own the recovery staging session (lock error %1).")
                    .arg(int(lock->error()));
        lock.reset();
    }
}

Session::~Session() {
    if (!valid())
        return;
    // A healthy session has at most one worker's stage and QSaveFile temporary.
    // Keep destruction bounded even if unrelated entries were added externally.
    int files = MaxFilesPerCleanup;
    int entries = MaxEntriesPerCleanup;
    const QString ownerPath = lockPath(directory, identifier);
    const QByteArray ownership = lockContents(ownerPath);
    const bool complete = removeStaging(directory, identifier, &files, &entries);
    lock->unlock();
    if (!complete && !ownership.isEmpty()) {
        // Unlock normally removes QLockFile's metadata. Preserve a retired
        // owner if some stages could not be removed, otherwise a later startup
        // could no longer prove that those remaining files are ours. The UUID
        // is never reused, workers have joined, and NewOnly cannot replace a
        // competing file. A live PID keeps this conservatively ineligible until
        // the owning process actually exits. An interrupted metadata write is
        // deliberately left unverifiable instead of guessing at ownership.
        QFile retired(ownerPath);
        if (retired.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (retired.write(ownership) != ownership.size() || !retired.flush())
                qWarning("Could not retain complete retired recovery ownership metadata.");
        } else if (!QFileInfo::exists(ownerPath)) {
            qWarning("Could not retain retired recovery ownership metadata.");
        }
    }
}

QString Session::stagingPath(quint64 jobId) const {
    return valid() ? QDir(directory).filePath(
                         QStringLiteral(".photodeck-%1-%2.pending").arg(identifier).arg(jobId))
                   : QString();
}

QString Session::recoveryPath(quint64 documentId) const {
    return valid() ? QDir(directory).filePath(
                         QStringLiteral("photodeck-%1-%2.pdeck").arg(identifier).arg(documentId))
                   : QString();
}

bool Session::valid() const { return lock && lock->isLocked(); }

QString Session::errorString() const { return error; }
} // namespace RecoveryFiles
