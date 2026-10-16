#include "recoveryfiles.h"
#include "recoveryidentity_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <thread>

using RecoveryIdentity = RecoveryFiles::Internal::Identity;
Q_DECLARE_METATYPE(RecoveryIdentity)

namespace {
bool writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const bool written = file.write(data) == data.size() && file.flush();
    file.close();
    return written && file.error() == QFileDevice::NoError;
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString ownerPath(const QString &staging) {
    return staging.left(staging.lastIndexOf('-')) + QStringLiteral(".lock");
}

QString identifier(const QString &staging) {
    const auto match = QRegularExpression(QStringLiteral("^\\.photodeck-(.*)-[0-9]+\\.pending$"))
                           .match(QFileInfo(staging).fileName());
    return match.captured(1);
}

struct AbandonedSession {
    QString staging, recovery;
    bool valid() const { return !staging.isEmpty() && !recovery.isEmpty(); }
};

QStringList sessionOutputPaths(QByteArray output) {
    // Windows CRT text-mode stdout expands LF to CRLF. Remove only protocol
    // line endings, preserving all characters belonging to either path.
    output.replace("\r\n", "\n");
    if (output.endsWith('\n'))
        output.chop(1);
    return QString::fromUtf8(output).split('\n');
}

AbandonedSession abandonSession(const QString &directory, int files = 1, bool cleanExit = false) {
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(),
                {cleanExit ? QStringLiteral("--retire-session") : QStringLiteral("--abandon-session"),
                 directory, QString::number(files)});
    if (!child.waitForFinished(15000) || child.exitStatus() != QProcess::NormalExit || child.exitCode())
        return {};
    const QStringList output = sessionOutputPaths(child.readAllStandardOutput());
    return output.size() == 2 ? AbandonedSession{output[0], output[1]} : AbandonedSession{};
}

int abandonSessionMain(const QString &directory, int count, bool cleanExit) {
    RecoveryFiles::Session session(directory);
    if (!session.valid())
        return 2;
    for (int index = 0; index < count; ++index)
        if (!writeFile(session.stagingPath(quint64(index)), "abandoned-stage"))
            return 3;
    if (!writeFile(session.stagingPath(0) + QStringLiteral(".aB12Cd"), "abandoned-savefile") ||
        !writeFile(session.recoveryPath(42), "complete-recovery"))
        return 4;
    const QByteArray output =
        (session.stagingPath(0) + '\n' + session.recoveryPath(42) + '\n').toUtf8();
    if (std::fwrite(output.constData(), 1, size_t(output.size()), stdout) != size_t(output.size()))
        return 5;
    std::fflush(stdout);
    if (cleanExit)
        return 0;
    // Model a process crash without relying on platform-specific signals. The
    // real QLockFile metadata survives, and QProcess waits until its owner dies.
    std::_Exit(0);
}
} // namespace

class RecoveryFilesTests : public QObject {
    Q_OBJECT
  private slots:
    void childSessionOutputAcceptsLfAndCrLf() {
        const QStringList paths{QStringLiteral("  staging path  "), QStringLiteral(" recovery path ")};
        QCOMPARE(sessionOutputPaths("  staging path  \n recovery path \n"), paths);
        QCOMPARE(sessionOutputPaths("  staging path  \r\n recovery path \r\n"), paths);
        QCOMPARE(sessionOutputPaths("  staging path  \n recovery path "), paths);
    }

    void localIdentityPolicy_data() {
        QTest::addColumn<RecoveryIdentity>("owner");
        QTest::addColumn<RecoveryIdentity>("local");
        QTest::addColumn<bool>("eligible");
        const RecoveryIdentity local{QStringLiteral("local-host"), "machine-a", "boot-a"};
        QTest::newRow("same-machine") << local << local << true;
        QTest::newRow("same-machine-renamed-host")
            << RecoveryIdentity{QStringLiteral("old-host"), "machine-a", "boot-a"} << local << true;
        QTest::newRow("same-machine-host-case")
            << RecoveryIdentity{QStringLiteral("LOCAL-HOST"), "machine-a", "boot-a"} << local << true;
        QTest::newRow("same-machine-empty-owner-host")
            << RecoveryIdentity{{}, "machine-a", "boot-a"} << local << true;
        QTest::newRow("same-machine-empty-local-host")
            << local << RecoveryIdentity{{}, "machine-a", "boot-a"} << true;
        QTest::newRow("same-machine-different-boot")
            << RecoveryIdentity{QStringLiteral("old-host"), "machine-a", "boot-old"} << local << true;
        QTest::newRow("same-machine-no-owner-boot")
            << RecoveryIdentity{QStringLiteral("old-host"), "machine-a", {}} << local << true;
        QTest::newRow("same-machine-no-local-boot")
            << local << RecoveryIdentity{QStringLiteral("new-host"), "machine-a", {}} << true;
        QTest::newRow("different-machine-same-host-and-boot")
            << RecoveryIdentity{local.hostname, "machine-b", "boot-a"} << local << false;
        QTest::newRow("different-machine-other-host-same-boot")
            << RecoveryIdentity{QStringLiteral("other-host"), "machine-b", "boot-a"} << local << false;
        QTest::newRow("no-owner-identities")
            << RecoveryIdentity{local.hostname, {}, {}} << local << false;
        QTest::newRow("no-local-identities")
            << local << RecoveryIdentity{local.hostname, {}, {}} << false;
        QTest::newRow("same-boot-no-owner-machine")
            << RecoveryIdentity{local.hostname, {}, "boot-a"} << local << true;
        QTest::newRow("same-boot-no-local-machine")
            << local << RecoveryIdentity{local.hostname, {}, "boot-a"} << true;
        QTest::newRow("same-boot-no-machine-identities")
            << RecoveryIdentity{local.hostname, {}, "boot-a"}
            << RecoveryIdentity{local.hostname, {}, "boot-a"} << true;
        QTest::newRow("same-boot-other-host-no-owner-machine")
            << RecoveryIdentity{QStringLiteral("other-host"), {}, "boot-a"} << local << false;
        QTest::newRow("same-boot-other-host-no-local-machine")
            << local << RecoveryIdentity{QStringLiteral("other-host"), {}, "boot-a"} << false;
        QTest::newRow("same-boot-empty-owner-host-no-machine")
            << RecoveryIdentity{{}, {}, "boot-a"} << local << false;
        QTest::newRow("same-boot-empty-local-host-no-machine")
            << local << RecoveryIdentity{{}, {}, "boot-a"} << false;
        QTest::newRow("different-boot-no-owner-machine")
            << RecoveryIdentity{local.hostname, {}, "boot-b"} << local << false;
        QTest::newRow("empty-identities")
            << RecoveryIdentity{} << RecoveryIdentity{} << false;
    }

    void localIdentityPolicy() {
        QFETCH(RecoveryIdentity, owner);
        QFETCH(RecoveryIdentity, local);
        QFETCH(bool, eligible);
        QCOMPARE(RecoveryFiles::Internal::ownerIdentityIsLocal(owner, local), eligible);
    }

    void publishesNewReplacementAndUnicode_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("ascii") << QStringLiteral("recovery");
        QTest::newRow("unicode") << QString::fromUtf8("çalışma-日本語-🖼️");
    }

    void publishesNewReplacementAndUnicode() {
        QFETCH(QString, name);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString staging = directory.filePath(name + QStringLiteral(".pending"));
        const QString destination = directory.filePath(name + QStringLiteral(".pdeck"));
        const QByteArray oldData("old\0complete", 12), newData("new\0complete\xff", 13);
        QString error = QStringLiteral("old error");
        QVERIFY(writeFile(staging, oldData));
        QVERIFY2(RecoveryFiles::atomicReplace(staging, destination, &error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(!QFileInfo::exists(staging));
        QCOMPARE(readFile(destination), oldData);
        QVERIFY(writeFile(staging, newData));
        QVERIFY2(RecoveryFiles::atomicReplace(staging, destination, &error), qPrintable(error));
        QCOMPARE(readFile(destination), newData);
        QVERIFY(!QFileInfo::exists(staging));
    }

    void rejectsMissingStagingWithoutChangingDestination() {
        QTemporaryDir directory;
        const QString staging = directory.filePath("missing.pending");
        const QString destination = directory.filePath("recovery.pdeck");
        QVERIFY(writeFile(destination, "previous-complete"));
        QString error;
        QVERIFY(!RecoveryFiles::atomicReplace(staging, destination, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readFile(destination), QByteArray("previous-complete"));
        QVERIFY(!RecoveryFiles::atomicReplace(staging, destination, nullptr));
    }

    void rejectsDirectoryAndSamePathTargets() {
        QTemporaryDir directory;
        const QString staging = directory.filePath("stage.pending");
        const QString destination = directory.filePath("target.pdeck");
        QVERIFY(writeFile(staging, "complete"));
        QVERIFY(QDir().mkdir(destination));
        QString error;
        QVERIFY(!RecoveryFiles::atomicReplace(staging, destination, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(QFileInfo(destination).isDir());
        QCOMPARE(readFile(staging), QByteArray("complete"));
        QVERIFY(!RecoveryFiles::atomicReplace(staging, staging, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readFile(staging), QByteArray("complete"));
        QVERIFY(!RecoveryFiles::atomicReplace(destination, staging, &error));
        QCOMPARE(readFile(staging), QByteArray("complete"));
    }

    void rejectsHardLinkedSameFile() {
        QTemporaryDir directory;
        const QString staging = directory.filePath("stage.pending");
        const QString destination = directory.filePath("recovery.pdeck");
        QVERIFY(writeFile(staging, "complete"));
        QFile source(staging), target(destination);
        std::error_code linkError;
        std::filesystem::create_hard_link(source.filesystemFileName(), target.filesystemFileName(),
                                          linkError);
        if (linkError)
            QSKIP("The temporary filesystem does not support creating hard links.");
        QString error;
        QVERIFY(!RecoveryFiles::atomicReplace(staging, destination, &error));
        QVERIFY2(error.contains("same file"), qPrintable(error));
        QCOMPARE(readFile(staging), QByteArray("complete"));
        QCOMPARE(readFile(destination), QByteArray("complete"));
    }

    void rejectsDifferentCanonicalDirectories() {
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir("other"));
        const QString staging = directory.filePath("stage.pending");
        const QString destination = directory.filePath("other/recovery.pdeck");
        QVERIFY(writeFile(staging, "new-complete"));
        QVERIFY(writeFile(destination, "old-complete"));
        QString error;
        QVERIFY(!RecoveryFiles::atomicReplace(staging, destination, &error));
        QVERIFY(error.contains("same canonical directory"));
        QCOMPARE(readFile(staging), QByteArray("new-complete"));
        QCOMPARE(readFile(destination), QByteArray("old-complete"));
    }

    void publishesUnderLongUnicodeDirectoryPaths() {
        QTemporaryDir directory;
        QString nested = directory.path();
        for (int level = 0; level < 12; ++level)
            nested += QString::fromUtf8("/long-unicode-çalışma-🖼️-%1").arg(level);
        QVERIFY(nested.size() > 260);
        QVERIFY(QDir().mkpath(nested));
        const QString staging = QDir(nested).filePath(QString::fromUtf8("新規-🖼️.pending"));
        const QString destination = QDir(nested).filePath(QString::fromUtf8("復旧-🖼️.pdeck"));
        QVERIFY(writeFile(staging, "first-complete"));
        QString error;
        QVERIFY2(RecoveryFiles::atomicReplace(staging, destination, &error), qPrintable(error));
        QVERIFY(writeFile(staging, "second-complete"));
        QVERIFY2(RecoveryFiles::atomicReplace(staging, destination, &error), qPrintable(error));
        QCOMPARE(readFile(destination), QByteArray("second-complete"));
    }

    void acceptsCanonicalDirectoryAliases() {
#ifdef Q_OS_WIN
        QSKIP("Creating native directory symlinks may require Windows developer privileges.");
#else
        QTemporaryDir directory;
        const QString actual = directory.filePath("actual");
        const QString alias = directory.filePath("alias");
        QVERIFY(QDir().mkdir(actual));
        QVERIFY(QFile::link(actual, alias));
        const QString staging = QDir(actual).filePath("stage.pending");
        const QString destination = QDir(alias).filePath("recovery.pdeck");
        QVERIFY(writeFile(staging, "complete"));
        QString error;
        QVERIFY2(RecoveryFiles::atomicReplace(staging, destination, &error), qPrintable(error));
        QCOMPARE(readFile(destination), QByteArray("complete"));
#endif
    }

    void rejectsSymlinksWithoutFollowingThem() {
#ifdef Q_OS_WIN
        QSKIP("QFile::link creates a Windows shortcut, not a native symlink.");
#else
        QTemporaryDir directory;
        const QString backing = directory.filePath("backing.pdeck");
        const QString staging = directory.filePath("stage.pending");
        const QString destination = directory.filePath("link.pdeck");
        QVERIFY(writeFile(backing, "project-backing"));
        QVERIFY(writeFile(staging, "stage-complete"));
        QVERIFY(QFile::link(backing, destination));
        QString error;
        QVERIFY(!RecoveryFiles::atomicReplace(staging, destination, &error));
        QCOMPARE(readFile(backing), QByteArray("project-backing"));
        QCOMPARE(readFile(staging), QByteArray("stage-complete"));
        QVERIFY(QFileInfo(destination).isSymLink());
        QVERIFY(!RecoveryFiles::atomicReplace(destination, staging, &error));
        QCOMPARE(readFile(staging), QByteArray("stage-complete"));
        QVERIFY(QFile::remove(destination));
        QVERIFY(QFile::link(directory.filePath("absent"), destination));
        QVERIFY(!RecoveryFiles::atomicReplace(staging, destination, &error));
        QVERIFY(QFileInfo(destination).isSymLink());
#endif
    }

    void reportsNativeErrorsAndRetainsStaging() {
        QTemporaryDir directory;
        const QString staging = directory.filePath("stage.pending");
        QVERIFY(writeFile(staging, "stage-complete"));
        QString error;
        const QString impossible = directory.filePath(QString(1024, QLatin1Char('x')) + ".pdeck");
        QVERIFY(!RecoveryFiles::atomicReplace(staging, impossible, &error));
        QVERIFY2(error.contains("Could not atomically publish"), qPrintable(error));
        QVERIFY(error.contains('(') || error.contains("Windows error"));
        QCOMPARE(readFile(staging), QByteArray("stage-complete"));
    }

    void concurrentReadersOnlySeeCompleteOldOrNewBytes() {
        QTemporaryDir directory;
        const QString staging = directory.filePath("stage.pending");
        const QString destination = directory.filePath("recovery.pdeck");
        const QByteArray oldData(256 * 1024, '\x13'), newData(384 * 1024, '\xa7');
        QVERIFY(writeFile(destination, oldData));
        std::atomic<bool> stop{false}, invalid{false}, ready{false};
        std::atomic<int> reads{0};
        std::thread reader([&] {
            ready.store(true);
            while (!stop.load()) {
                QFile file(destination);
                if (!file.open(QIODevice::ReadOnly)) {
                    invalid.store(true);
                    break;
                }
                const QByteArray bytes = file.readAll();
                if (bytes != oldData && bytes != newData) {
                    invalid.store(true);
                    break;
                }
                reads.fetch_add(1);
            }
        });
        while (!ready.load())
            std::this_thread::yield();
        QString error;
        bool published = true;
        for (int iteration = 0; iteration < 100 && published; ++iteration) {
            published = writeFile(staging, iteration % 2 ? oldData : newData) &&
                        RecoveryFiles::atomicReplace(staging, destination, &error);
        }
        stop.store(true);
        reader.join();
        QVERIFY2(published, qPrintable(error));
        QVERIFY(reads.load() > 0);
        QVERIFY(!invalid.load());
    }

    void sessionsOwnUniqueFlatNamespacesAndRemoveOnlyTheirStages() {
        QTemporaryDir directory;
        RecoveryFiles::Session foreign(directory.path());
        QVERIFY2(foreign.valid(), qPrintable(foreign.errorString()));
        const QString foreignStage = foreign.stagingPath(1);
        QVERIFY(writeFile(foreignStage, "foreign-stage"));
        QVERIFY(writeFile(foreign.recoveryPath(1), "foreign-final"));
        QVERIFY(!RecoveryFiles::mayRecover(foreign.recoveryPath(1)));
        const QString arbitrary = directory.filePath("unrelated.pending");
        QVERIFY(writeFile(arbitrary, "unrelated"));
        QString ownStage, ownRecovery, ownLock, unrelatedVariant;
        {
            RecoveryFiles::Session own(directory.path());
            QVERIFY2(own.valid(), qPrintable(own.errorString()));
            ownStage = own.stagingPath(std::numeric_limits<quint64>::max());
            ownRecovery = own.recoveryPath(2);
            ownLock = ownerPath(ownStage);
            unrelatedVariant = ownStage + ".backup-copy";
            QVERIFY(ownStage != foreignStage);
            QCOMPARE(QFileInfo(ownStage).absolutePath(), directory.path());
            QCOMPARE(QFileInfo(ownRecovery).absolutePath(), directory.path());
            QVERIFY(QFileInfo::exists(ownLock));
            QVERIFY(writeFile(ownStage, "own-stage"));
            QVERIFY(writeFile(ownStage + ".aB12Cd", "savefile-temp"));
            QVERIFY(writeFile(ownRecovery, "own-final"));
            QVERIFY(writeFile(unrelatedVariant, "arbitrary-extension"));
            QVERIFY(!RecoveryFiles::mayRecover(ownRecovery));
        }
        QVERIFY(!QFileInfo::exists(ownStage));
        QVERIFY(!QFileInfo::exists(ownStage + ".aB12Cd"));
        QVERIFY(!QFileInfo::exists(ownLock));
        QCOMPARE(readFile(ownRecovery), QByteArray("own-final"));
        QVERIFY(RecoveryFiles::mayRecover(ownRecovery));
        QCOMPARE(readFile(unrelatedVariant), QByteArray("arbitrary-extension"));
        QCOMPARE(readFile(foreignStage), QByteArray("foreign-stage"));
        QCOMPARE(readFile(foreign.recoveryPath(1)), QByteArray("foreign-final"));
        QVERIFY(!RecoveryFiles::mayRecover(foreign.recoveryPath(1)));
        QCOMPARE(readFile(arbitrary), QByteArray("unrelated"));
    }

    void oldLiveLocksRemainOwned() {
        QTemporaryDir directory;
        RecoveryFiles::Session foreign(directory.path());
        QVERIFY(foreign.valid());
        const QString staging = foreign.stagingPath(1);
        QVERIFY(writeFile(staging, "active-stage"));
        QVERIFY(writeFile(foreign.recoveryPath(1), "active-final"));
        QFile lock(ownerPath(staging));
        QVERIFY(lock.open(QIODevice::ReadWrite));
        QVERIFY(lock.setFileTime(QDateTime::currentDateTimeUtc().addDays(-7),
                                 QFileDevice::FileModificationTime));
        lock.close();
        RecoveryFiles::Session own(directory.path());
        QVERIFY(own.valid());
        QCOMPARE(readFile(staging), QByteArray("active-stage"));
        QVERIFY(QFileInfo::exists(ownerPath(staging)));
        QVERIFY(!RecoveryFiles::mayRecover(foreign.recoveryPath(1)));
    }

    void removesOnlyProvablyDeadManagedStagesAndSavefileVariants() {
        QTemporaryDir directory;
        const AbandonedSession abandoned = abandonSession(directory.path());
        QVERIFY(abandoned.valid());
        QVERIFY(QFileInfo::exists(ownerPath(abandoned.staging)));
        const QString prefix = abandoned.staging.left(abandoned.staging.lastIndexOf('-') + 1);
        const QStringList unrelated = {
            abandoned.staging + ".backup-copy", abandoned.staging + ".abcdefg",
            abandoned.staging + ".a!bC12", prefix + "01.pending",
            prefix + "18446744073709551616.pending",
            directory.filePath("unrelated.pending"), directory.filePath(".photodeck-not-a-uuid.lock"),
            directory.filePath(".photodeck-not-a-uuid-1.pending")};
        for (const QString &path : unrelated)
            QVERIFY(writeFile(path, "unrelated"));
        QVERIFY(RecoveryFiles::mayRecover(abandoned.recovery));
        RecoveryFiles::Session current(directory.path());
        QVERIFY2(current.valid(), qPrintable(current.errorString()));
        QVERIFY(!QFileInfo::exists(abandoned.staging));
        QVERIFY(!QFileInfo::exists(abandoned.staging + ".aB12Cd"));
        QVERIFY(!QFileInfo::exists(ownerPath(abandoned.staging)));
        QCOMPARE(readFile(abandoned.recovery), QByteArray("complete-recovery"));
        QVERIFY(RecoveryFiles::mayRecover(abandoned.recovery));
        for (const QString &path : unrelated)
            QCOMPARE(readFile(path), QByteArray("unrelated"));
    }

    void emptyHostnameStillParsesAsLockMetadata() {
        QTemporaryDir directory;
        const AbandonedSession abandoned = abandonSession(directory.path());
        QVERIFY(abandoned.valid());
        const QString lock = ownerPath(abandoned.staging);
        QList<QByteArray> fields = readFile(lock).split('\n');
        QVERIFY(fields.size() >= 5);
        fields[2].clear();
        QVERIFY(writeFile(lock, fields.join('\n')));
        QLockFile candidate(lock);
        qint64 pid = 0;
        QString hostname, application;
        QVERIFY(candidate.getLockInfo(&pid, &hostname, &application));
        QCOMPARE(pid, fields[0].toLongLong());
        QVERIFY(pid > 0);
        QVERIFY(hostname.isEmpty());
        QVERIFY(!application.isEmpty());
    }

    void recoversDeadLocalMachineDespiteHostnameChanges_data() {
        QTest::addColumn<QByteArray>("kind");
        QTest::newRow("hostname-renamed") << QByteArray("renamed");
        QTest::newRow("hostname-case") << QByteArray("case");
        QTest::newRow("hostname-unset") << QByteArray("unset");
    }

    void recoversDeadLocalMachineDespiteHostnameChanges() {
        QFETCH(QByteArray, kind);
        if (QSysInfo::machineUniqueId().isEmpty())
            QSKIP("This platform does not expose a machine identity.");
        QTemporaryDir directory;
        const AbandonedSession abandoned = abandonSession(directory.path());
        QVERIFY(abandoned.valid());
        const QString lock = ownerPath(abandoned.staging);
        QList<QByteArray> fields = readFile(lock).split('\n');
        QVERIFY(fields.size() >= 5);
        QCOMPARE(fields[3], QSysInfo::machineUniqueId());
        const QByteArray currentHostname = QSysInfo::machineHostName().toUtf8();
        if (kind == "unset") {
            fields[2].clear();
        } else if (kind == "case") {
            fields[2] = currentHostname.toUpper();
            if (fields[2] == currentHostname)
                fields[2] = currentHostname.toLower();
            if (fields[2] == currentHostname)
                QSKIP("The local hostname has no case-sensitive characters.");
        } else {
            fields[2] = currentHostname + ".previous-name";
        }
        QVERIFY(fields[2] != currentHostname || kind == "unset");
        QVERIFY(writeFile(lock, fields.join('\n')));

        // Only the hostname is edited. The identity and dead PID come from a
        // real child session, as they would after a rename or spelling change.
        QVERIFY(RecoveryFiles::mayRecover(abandoned.recovery));
        RecoveryFiles::Session current(directory.path());
        QVERIFY2(current.valid(), qPrintable(current.errorString()));
        QVERIFY(!QFileInfo::exists(abandoned.staging));
        QVERIFY(!QFileInfo::exists(abandoned.staging + ".aB12Cd"));
        QVERIFY(!QFileInfo::exists(lock));
        QCOMPARE(readFile(abandoned.recovery), QByteArray("complete-recovery"));
        QVERIFY(RecoveryFiles::mayRecover(abandoned.recovery));
    }

    void matchingMachineAndChangedHostnameStillRequireDeadPid() {
        if (QSysInfo::machineUniqueId().isEmpty())
            QSKIP("This platform does not expose a machine identity.");
        QTemporaryDir directory;
        const AbandonedSession abandoned = abandonSession(directory.path());
        QVERIFY(abandoned.valid());
        const QString lock = ownerPath(abandoned.staging);
        QList<QByteArray> fields = readFile(lock).split('\n');
        QVERIFY(fields.size() >= 5);
        QCOMPARE(fields[3], QSysInfo::machineUniqueId());
        fields[0] = QByteArray::number(QCoreApplication::applicationPid());
        fields[2] = QSysInfo::machineHostName().toUtf8() + ".previous-name";
        const QByteArray metadata = fields.join('\n');
        QVERIFY(writeFile(lock, metadata));
        QVERIFY(!RecoveryFiles::mayRecover(abandoned.recovery));
        RecoveryFiles::Session current(directory.path());
        QVERIFY(current.valid());
        QCOMPARE(readFile(abandoned.staging), QByteArray("abandoned-stage"));
        QCOMPARE(readFile(abandoned.staging + ".aB12Cd"), QByteArray("abandoned-savefile"));
        QCOMPARE(readFile(lock), metadata);
        QCOMPARE(readFile(abandoned.recovery), QByteArray("complete-recovery"));
        QVERIFY(!RecoveryFiles::mayRecover(abandoned.recovery));
    }

    void preservesForeignOrIncompleteActualOwnership_data() {
        QTest::addColumn<QByteArray>("kind");
        QTest::newRow("foreign-machine-same-host") << QByteArray("foreign-machine");
        QTest::newRow("foreign-machine-changed-host") << QByteArray("foreign-machine-host");
        QTest::newRow("foreign-machine-matching-boot") << QByteArray("foreign-machine-boot");
        QTest::newRow("no-identities-same-host") << QByteArray("no-identities");
        QTest::newRow("no-identities-changed-host") << QByteArray("no-identities-host");
        QTest::newRow("foreign-boot-without-machine") << QByteArray("foreign-boot");
        QTest::newRow("matching-boot-changed-host-without-machine") << QByteArray("boot-host");
        QTest::newRow("missing-host-without-machine") << QByteArray("missing-host");
        QTest::newRow("missing-application-with-real-identities") << QByteArray("missing-app");
        QTest::newRow("invalid-pid-with-real-identities") << QByteArray("invalid-pid");
        QTest::newRow("zero-pid-with-real-identities") << QByteArray("zero-pid");
        QTest::newRow("overflow-pid-with-real-identities") << QByteArray("overflow-pid");
    }

    void preservesForeignOrIncompleteActualOwnership() {
        QFETCH(QByteArray, kind);
        if (kind.startsWith("foreign-machine") && QSysInfo::machineUniqueId().isEmpty())
            QSKIP("This platform does not expose a machine identity.");
        if ((kind == "foreign-machine-boot" || kind == "boot-host") &&
            QSysInfo::bootUniqueId().isEmpty())
            QSKIP("This platform does not expose a boot identity.");
        QTemporaryDir directory;
        const AbandonedSession abandoned = abandonSession(directory.path());
        QVERIFY(abandoned.valid());
        const QString lock = ownerPath(abandoned.staging);
        QList<QByteArray> fields = readFile(lock).split('\n');
        QVERIFY(fields.size() >= 5);
        fields[2] = QSysInfo::machineHostName().toUtf8();
        if (kind.startsWith("foreign-machine")) {
            fields[3] = QSysInfo::machineUniqueId() + "-foreign";
            if (kind == "foreign-machine-boot")
                fields[4] = QSysInfo::bootUniqueId();
            else
                fields[4].clear();
            if (kind == "foreign-machine-host")
                fields[2] += ".other-host";
        } else if (kind.startsWith("no-identities")) {
            fields[3].clear();
            fields[4].clear();
            if (kind == "no-identities-host")
                fields[2] += ".other-host";
        } else if (kind == "foreign-boot" || kind == "boot-host") {
            fields[3].clear();
            fields[4] = kind == "boot-host" ? QSysInfo::bootUniqueId()
                                           : QSysInfo::bootUniqueId() + "-foreign";
            if (kind == "boot-host")
                fields[2] += ".other-host";
        } else if (kind == "missing-host") {
            fields[2].clear();
            fields[3].clear();
        } else if (kind == "missing-app") {
            fields[1].clear();
        } else if (kind == "invalid-pid") {
            fields[0] = "invalid";
        } else if (kind == "zero-pid") {
            fields[0] = "0";
        } else if (kind == "overflow-pid") {
            fields[0] = "18446744073709551616";
        }
        const QByteArray metadata = fields.join('\n');
        QVERIFY(writeFile(lock, metadata));
        QVERIFY(!RecoveryFiles::mayRecover(abandoned.recovery));
        RecoveryFiles::Session current(directory.path());
        QVERIFY(current.valid());
        QCOMPARE(readFile(abandoned.staging), QByteArray("abandoned-stage"));
        QCOMPARE(readFile(abandoned.staging + ".aB12Cd"), QByteArray("abandoned-savefile"));
        QCOMPARE(readFile(lock), metadata);
        QCOMPARE(readFile(abandoned.recovery), QByteArray("complete-recovery"));
        QVERIFY(!RecoveryFiles::mayRecover(abandoned.recovery));
    }

    void localBootIdentityStillRequiresDeadPid_data() {
        QTest::addColumn<bool>("livePid");
        QTest::newRow("dead-pid") << false;
        QTest::newRow("live-pid") << true;
    }

    void localBootIdentityStillRequiresDeadPid() {
        QFETCH(bool, livePid);
        if (QSysInfo::bootUniqueId().isEmpty())
            QSKIP("This platform does not expose a boot identity.");
        QTemporaryDir directory;
        const AbandonedSession abandoned = abandonSession(directory.path());
        QVERIFY(abandoned.valid());
        const QString lock = ownerPath(abandoned.staging);
        QList<QByteArray> fields = readFile(lock).split('\n');
        QVERIFY(fields.size() >= 5);
        QCOMPARE(fields[4], QSysInfo::bootUniqueId());
        fields[2] = QSysInfo::machineHostName().toUtf8();
        fields[3].clear();
        if (livePid)
            fields[0] = QByteArray::number(QCoreApplication::applicationPid());
        const QByteArray metadata = fields.join('\n');
        QVERIFY(writeFile(lock, metadata));
        QCOMPARE(RecoveryFiles::mayRecover(abandoned.recovery), !livePid);
        RecoveryFiles::Session current(directory.path());
        QVERIFY(current.valid());
        if (livePid) {
            QCOMPARE(readFile(abandoned.staging), QByteArray("abandoned-stage"));
            QCOMPARE(readFile(abandoned.staging + ".aB12Cd"), QByteArray("abandoned-savefile"));
            QCOMPARE(readFile(lock), metadata);
        } else {
            QVERIFY(!QFileInfo::exists(abandoned.staging));
            QVERIFY(!QFileInfo::exists(abandoned.staging + ".aB12Cd"));
            QVERIFY(!QFileInfo::exists(lock));
        }
        QCOMPARE(readFile(abandoned.recovery), QByteArray("complete-recovery"));
        QCOMPARE(RecoveryFiles::mayRecover(abandoned.recovery), !livePid);
    }

    void preservesUnverifiableAndForeignHostOwnership_data() {
        QTest::addColumn<QByteArray>("metadata");
        QTest::newRow("malformed") << QByteArray("invalid-lock-metadata\n");
        QTest::newRow("foreign-host")
            << QByteArray("2147483647\nPhotoDeck\nforeign-host.invalid\n\n\n");
        QTest::newRow("same-host-unknown-machine")
            << (QByteArray("2147483647\nPhotoDeck\n") + QSysInfo::machineHostName().toUtf8() +
                "\n\n\n");
        QTest::newRow("same-host-other-machine")
            << (QByteArray("2147483647\nPhotoDeck\n") + QSysInfo::machineHostName().toUtf8() +
                "\nforeign-machine-id\nforeign-boot-id\n");
        QTest::newRow("live-pid-other-name")
            << (QByteArray::number(QCoreApplication::applicationPid()) + "\nother-program\n" +
                QSysInfo::machineHostName().toUtf8() + "\n\n\n");
    }

    void preservesUnverifiableAndForeignHostOwnership() {
        QFETCH(QByteArray, metadata);
        QTemporaryDir directory;
        const AbandonedSession abandoned = abandonSession(directory.path());
        QVERIFY(abandoned.valid());
        QVERIFY(writeFile(ownerPath(abandoned.staging), metadata));
        RecoveryFiles::Session current(directory.path());
        QVERIFY(current.valid());
        QCOMPARE(readFile(abandoned.staging), QByteArray("abandoned-stage"));
        QCOMPARE(readFile(abandoned.staging + ".aB12Cd"), QByteArray("abandoned-savefile"));
        QCOMPARE(readFile(ownerPath(abandoned.staging)), metadata);
        QVERIFY(!RecoveryFiles::mayRecover(abandoned.recovery));
    }

    void boundedCleanupKeepsMetadataUntilAllStagesAreRemoved() {
        QTemporaryDir directory;
        const AbandonedSession abandoned = abandonSession(directory.path(), 600);
        QVERIFY(abandoned.valid());
        {
            RecoveryFiles::Session first(directory.path());
            QVERIFY(first.valid());
            QVERIFY(QFileInfo::exists(ownerPath(abandoned.staging)));
            const auto remaining = QDir(directory.path()).entryList(
                {QStringLiteral(".photodeck-%1-*.pending*").arg(identifier(abandoned.staging))},
                QDir::Files | QDir::Hidden);
            QVERIFY(!remaining.isEmpty());
            QVERIFY(remaining.size() < 601);
        }
        RecoveryFiles::Session second(directory.path());
        QVERIFY(second.valid());
        const auto remaining = QDir(directory.path()).entryList(
            {QStringLiteral(".photodeck-%1-*.pending*").arg(identifier(abandoned.staging))},
            QDir::Files | QDir::Hidden);
        QVERIFY(remaining.isEmpty());
        QVERIFY(!QFileInfo::exists(ownerPath(abandoned.staging)));
        QCOMPARE(readFile(abandoned.recovery), QByteArray("complete-recovery"));
    }

    void incompleteNormalExitRetainsDiscoverableOwnership() {
        QTemporaryDir directory;
        const AbandonedSession retired = abandonSession(directory.path(), 600, true);
        QVERIFY(retired.valid());
        QVERIFY(QFileInfo::exists(ownerPath(retired.staging)));
        const QString pattern =
            QStringLiteral(".photodeck-%1-*.pending*").arg(identifier(retired.staging));
        const auto before = QDir(directory.path()).entryList({pattern}, QDir::Files | QDir::Hidden);
        QVERIFY(!before.isEmpty());
        QVERIFY(before.size() < 601);
        RecoveryFiles::Session current(directory.path());
        QVERIFY(current.valid());
        QVERIFY(QDir(directory.path()).entryList({pattern}, QDir::Files | QDir::Hidden).isEmpty());
        QVERIFY(!QFileInfo::exists(ownerPath(retired.staging)));
        QCOMPARE(readFile(retired.recovery), QByteArray("complete-recovery"));
    }

    void sameNamedDirectoryPreventsDiscardingOrphanMetadata() {
        QTemporaryDir directory;
        const AbandonedSession abandoned = abandonSession(directory.path());
        QVERIFY(abandoned.valid());
        const QString blocked =
            abandoned.staging.left(abandoned.staging.lastIndexOf('-') + 1) + "99.pending";
        QVERIFY(QDir().mkdir(blocked));
        RecoveryFiles::Session current(directory.path());
        QVERIFY(current.valid());
        QVERIFY(!QFileInfo::exists(abandoned.staging));
        QVERIFY(QFileInfo(blocked).isDir());
        QVERIFY(QFileInfo::exists(ownerPath(abandoned.staging)));
        QVERIFY(RecoveryFiles::mayRecover(abandoned.recovery));
    }

    void legacyFilesAndEndedSessionsRemainRecoverable() {
        QTemporaryDir directory;
        const QString legacy = directory.filePath("recovery-legacy.pdeck");
        QVERIFY(writeFile(legacy, "legacy"));
        QVERIFY(RecoveryFiles::mayRecover(legacy));
        QVERIFY(!RecoveryFiles::mayRecover(directory.filePath("missing.pdeck")));
        QString final;
        {
            RecoveryFiles::Session current(directory.path());
            QVERIFY(current.valid());
            final = current.recoveryPath(123);
            QVERIFY(writeFile(final, "current"));
            QVERIFY(!RecoveryFiles::mayRecover(final));
        }
        QVERIFY(RecoveryFiles::mayRecover(final));
        QCOMPARE(readFile(final), QByteArray("current"));
    }

    void malformedManagedDocumentNumberIsIneligible_data() {
        QTest::addColumn<QString>("number");
        QTest::newRow("overflow") << QStringLiteral("18446744073709551616");
        QTest::newRow("too-long") << QStringLiteral("184467440737095516160");
        QTest::newRow("leading-zero") << QStringLiteral("01");
    }

    void malformedManagedDocumentNumberIsIneligible() {
        QFETCH(QString, number);
        QTemporaryDir directory;
        RecoveryFiles::Session current(directory.path());
        QVERIFY(current.valid());
        const QString malformed = directory.filePath(
            QStringLiteral("photodeck-%1-%2.pdeck").arg(identifier(current.stagingPath(1)), number));
        QVERIFY(writeFile(malformed, "malformed"));
        QVERIFY(!RecoveryFiles::mayRecover(malformed));
    }

    void invalidSessionsProvideNoWritablePaths() {
        QTemporaryDir directory;
        const QString file = directory.filePath("file");
        QVERIFY(writeFile(file, "regular"));
        RecoveryFiles::Session invalid(file + "/not-a-directory");
        QVERIFY(!invalid.valid());
        QVERIFY(!invalid.errorString().isEmpty());
        QVERIFY(invalid.stagingPath(1).isEmpty());
        QVERIFY(invalid.recoveryPath(1).isEmpty());
        RecoveryFiles::Session empty({});
        QVERIFY(!empty.valid());
        QVERIFY(!empty.errorString().isEmpty());
    }
};

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() == 4 &&
        (application.arguments()[1] == "--abandon-session" ||
         application.arguments()[1] == "--retire-session"))
        return abandonSessionMain(application.arguments()[2], application.arguments()[3].toInt(),
                                  application.arguments()[1] == "--retire-session");
    RecoveryFilesTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_recovery_files.moc"
