#include "document.h"
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray contents(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool replaceContents(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
void edit(Document &document, QColor color = Qt::red) {
    document.beginEdit();
    document.state.layers[0].pixels.fill(color);
    document.commit("Test edit");
}
int storedVersion(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_2);
    quint32 magic = 0, version = 0;
    in >> magic >> version;
    return magic == 0x50444543 && in.status() == QDataStream::Ok ? int(version) : 0;
}
} // namespace

class SaveSafetyTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName("PhotoDeckSaveSafetyTests");
        QApplication::setOrganizationName("PhotoDeckTests");
        QImageReader::setAllocationLimit(128);
    }
    void nativeSaveAndLoadTrackVersionAndFreshBaseline() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath("work.pdeck");
        QString error;
        Document d;
        QCOMPARE(d.sourceVersion(), 0);
        QVERIFY(d.create({12, 10}));
        edit(d);
        QVERIFY2(d.save(file, &error), qPrintable(error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::Success);
        QCOMPARE(d.sourceVersion(), Document::NativeProjectVersion);
        QCOMPARE(storedVersion(file), 5);
        QVERIFY(!d.dirty);
        QVERIFY(!d.requiresFormatUpgrade(file));
        edit(d, Qt::green);
        QVERIFY2(d.save(file, &error), qPrintable(error));
        Document loaded;
        QVERIFY2(loaded.load(file, &error), qPrintable(error));
        QCOMPARE(loaded.sourceVersion(), 5);
        QCOMPARE(loaded.composite(), d.composite());
        edit(loaded, Qt::blue);
        QVERIFY2(loaded.save(file, &error), qPrintable(error));
        edit(loaded, Qt::yellow);
        QVERIFY2(loaded.save(file, &error), qPrintable(error));
        QVERIFY(loaded.create({8, 8}));
        QCOMPARE(loaded.sourceVersion(), 0);
        QVERIFY(loaded.path.isEmpty());
        QVERIFY(loaded.save(file, &error));
        const QString image = dir.filePath("image.png");
        QVERIFY(loaded.composite().save(image));
        QVERIFY(loaded.importImage(image, false, &error));
        QCOMPARE(loaded.sourceVersion(), 0);
        QVERIFY(loaded.path.isEmpty());
    }
    void independentEditorCannotBeSilentlyOverwritten() {
        QTemporaryDir dir;
        const QString file = dir.filePath("shared.pdeck");
        QString error;
        Document first;
        QVERIFY(first.create({12, 10}));
        QVERIFY(first.save(file, &error));
        Document second;
        QVERIFY(second.load(file, &error));
        edit(first, Qt::red);
        edit(second, Qt::blue);
        QVERIFY(second.save(file, &error));
        const QByteArray external = contents(file);
        const quint64 revision = first.revision();
        const QStringList history = first.historyLabels();
        const QImage local = first.composite();
        QVERIFY(!first.save(file, &error));
        QCOMPARE(first.lastSaveStatus(), Document::SaveStatus::ExternalModification);
        QVERIFY(!error.isEmpty());
        QCOMPARE(contents(file), external);
        QCOMPARE(first.path, file);
        QCOMPARE(first.revision(), revision);
        QCOMPARE(first.historyLabels(), history);
        QCOMPARE(first.composite(), local);
        QVERIFY(first.dirty);
        QVERIFY(first.canUndo());
        Document::SaveOptions overwrite;
        overwrite.allowExternalOverwrite = true;
        QVERIFY2(first.save(file, &error, true, overwrite), qPrintable(error));
        QVERIFY(!first.dirty);
        Document read;
        QVERIFY(read.load(file, &error));
        QCOMPARE(read.composite(), local);
        edit(first, Qt::green);
        QVERIFY2(first.save(file, &error), qPrintable(error));
    }
    void hashDetectsSameSizeSameTimestampInPlaceEdit() {
        QTemporaryDir dir;
        const QString file = dir.filePath("same-metadata.pdeck");
        QString error;
        Document d;
        QVERIFY(d.create({16, 16}));
        QVERIFY(d.save(file, &error));
        edit(d);
        QFile external(file);
        QVERIFY(external.open(QIODevice::ReadWrite));
        const QDateTime modified = external.fileTime(QFileDevice::FileModificationTime);
        QByteArray changed = external.readAll();
        QVERIFY(!changed.isEmpty());
        changed[changed.size() - 1] = char(changed.back() ^ 1);
        QVERIFY(external.seek(0));
        QCOMPARE(external.write(changed), qint64(changed.size()));
        QVERIFY(external.flush());
        QVERIFY(external.setFileTime(modified, QFileDevice::FileModificationTime));
        external.close();
        QCOMPARE(QFileInfo(file).lastModified(), modified);
        QCOMPARE(QFileInfo(file).size(), qint64(changed.size()));
        QVERIFY(!d.save(file, &error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::ExternalModification);
        QCOMPARE(contents(file), changed);
        QVERIFY(d.dirty);
    }
    void deletionNeedsExplicitOverwriteAndRecoveryKeepsConflict() {
        QTemporaryDir dir;
        const QString file = dir.filePath("deleted.pdeck");
        const QString recovery = dir.filePath("recovery.pdeck");
        QString error;
        Document d;
        QVERIFY(d.create({12, 12}));
        QVERIFY(d.save(file, &error));
        edit(d);
        const quint64 revision = d.revision();
        QVERIFY(QFile::remove(file));
        QVERIFY(!d.save(file, &error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::ExternalModification);
        QVERIFY(!QFileInfo::exists(file));
        QVERIFY(d.save(recovery, &error, false));
        QCOMPARE(d.path, file);
        QCOMPARE(d.revision(), revision);
        QCOMPARE(d.sourceVersion(), 5);
        QVERIFY(d.dirty);
        Document snapshot;
        QVERIFY(snapshot.load(recovery, &error));
        QCOMPARE(snapshot.composite(), d.composite());
        QVERIFY(!d.save(file, &error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::ExternalModification);
        Document::SaveOptions overwrite;
        overwrite.allowExternalOverwrite = true;
        QVERIFY(d.save(file, &error, true, overwrite));
        QVERIFY(QFileInfo::exists(file));
        QVERIFY(!d.dirty);
    }
    void snapshotCannotOverwriteSourceEvenWithOptions() {
        QTemporaryDir dir;
        const QString file = dir.filePath("original.pdeck");
        QString error;
        Document d;
        QVERIFY(d.create({12, 12}));
        QVERIFY(d.save(file, &error));
        const QByteArray original = contents(file);
        edit(d);
        Document::SaveOptions overwrite;
        overwrite.allowExternalOverwrite = true;
        overwrite.allowFormatUpgrade = true;
        QVERIFY(!d.save(file, &error, false, overwrite));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::Error);
        QVERIFY(!error.isEmpty());
        QCOMPARE(contents(file), original);
        QVERIFY(d.dirty);
    }
    void recoveredCopyCanReuseSnapshotWithoutAdoptingItAsProject() {
        QTemporaryDir dir;
        const QString original = dir.filePath("original.pdeck");
        const QString snapshot = dir.filePath("recovery.pdeck");
        const QString copy = dir.filePath("recovered-copy.pdeck");
        QString error;
        Document d;
        QVERIFY(d.create({12, 12}));
        QVERIFY(d.save(original, &error));
        const QByteArray originalBytes = contents(original);
        edit(d);
        QVERIFY(d.save(snapshot, &error, false));
        Document recovered;
        QVERIFY(recovered.load(snapshot, &error));
        recovered.markRecovered();
        QVERIFY(recovered.path.isEmpty());
        QCOMPARE(recovered.sourceVersion(), 0);
        QVERIFY(recovered.dirty);
        edit(recovered, Qt::green);
        QVERIFY2(recovered.save(snapshot, &error, false), qPrintable(error));
        edit(recovered, Qt::blue);
        QVERIFY2(recovered.save(snapshot, &error, false), qPrintable(error));
        QVERIFY(recovered.path.isEmpty());
        QCOMPARE(recovered.sourceVersion(), 0);
        QVERIFY(recovered.dirty);
        Document verify;
        QVERIFY(verify.load(snapshot, &error));
        QCOMPARE(verify.composite(), recovered.composite());
        QCOMPARE(contents(original), originalBytes);
        QVERIFY(recovered.save(copy, &error));
        QCOMPARE(recovered.path, copy);
        QCOMPARE(recovered.sourceVersion(), 5);
        QVERIFY(!recovered.dirty);
        QCOMPARE(contents(original), originalBytes);
    }
    void saveAsEscapesConflictAndTracksNewDestination() {
        QTemporaryDir dir;
        const QString original = dir.filePath("original.pdeck");
        const QString copy = dir.filePath("copy.pdeck");
        QString error;
        Document d;
        QVERIFY(d.create({12, 12}));
        QVERIFY(d.save(original, &error));
        edit(d);
        const QByteArray external("independent replacement");
        QVERIFY(replaceContents(original, external));
        QVERIFY(d.save(copy, &error));
        QCOMPARE(d.path, copy);
        QCOMPARE(contents(original), external);
        QCOMPARE(d.sourceVersion(), 5);
        edit(d, Qt::green);
        QVERIFY(replaceContents(original, "another external edit"));
        QVERIFY(d.save(copy, &error));
        edit(d, Qt::blue);
        const QByteArray copyExternal("independent new destination edit");
        QVERIFY(replaceContents(copy, copyExternal));
        QVERIFY(!d.save(copy, &error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::ExternalModification);
        QCOMPARE(contents(copy), copyExternal);
    }
    void canonicalAliasesCannotBypassConflict() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("nested"));
        const QString file = dir.filePath("original.pdeck");
        const QString alias = dir.filePath("nested/../original.pdeck");
        QString error;
        Document d;
        QVERIFY(d.create({12, 12}));
        QVERIFY(d.save(file, &error));
        edit(d);
        const QByteArray external("independent edit");
        QVERIFY(replaceContents(file, external));
        QVERIFY(!d.save(alias, &error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::ExternalModification);
        QCOMPARE(contents(file), external);
        QVERIFY(QFile::remove(file));
        QVERIFY(!d.save(alias, &error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::ExternalModification);
        QVERIFY(!QFileInfo::exists(file));
    }
    void symbolicAliasesAndRetargetingAreProtected() {
#ifndef Q_OS_UNIX
        QSKIP("This test requires Unix symbolic links.");
#else
        QTemporaryDir dir;
        const QString file = dir.filePath("original.pdeck");
        const QString alias = dir.filePath("alias.pdeck");
        const QString other = dir.filePath("other.pdeck");
        QString error;
        Document source;
        QVERIFY(source.create({12, 12}));
        QVERIFY(source.save(file, &error));
        QVERIFY(QFile::link(file, alias));
        Document loaded;
        QVERIFY(loaded.load(alias, &error));
        edit(loaded);
        QVERIFY2(loaded.save(file, &error), qPrintable(error));
        QVERIFY(QFileInfo(alias).isSymLink());
        edit(loaded, Qt::green);
        QVERIFY2(loaded.save(alias, &error), qPrintable(error));
        QVERIFY(QFileInfo(alias).isSymLink());
        edit(loaded, Qt::blue);
        const QByteArray original = contents(file);
        QVERIFY(QFile::copy(file, other));
        QVERIFY(QFile::remove(alias));
        QVERIFY(QFile::link(other, alias));
        QVERIFY(!loaded.save(alias, &error));
        QCOMPARE(loaded.lastSaveStatus(), Document::SaveStatus::ExternalModification);
        QCOMPARE(contents(file), original);
        QCOMPARE(contents(other), original);
        QVERIFY(QFile::remove(alias));
        QVERIFY(QFile::link(file, alias));
        QVERIFY(QFile::remove(file));
        QVERIFY(!loaded.save(alias, &error));
        QCOMPARE(loaded.lastSaveStatus(), Document::SaveStatus::ExternalModification);
        QVERIFY(!QFileInfo::exists(file));
#endif
    }
    void legacyOriginalRequiresConsent_data() {
        QTest::addColumn<int>("version");
        QTest::addColumn<QString>("fixture");
        QTest::newRow("v1") << 1 << QFINDTESTDATA("../examples/Horizon.pdeck");
        QTest::newRow("v2") << 2 << QFINDTESTDATA("fixtures/v2-demo.pdeck");
        QTest::newRow("v3") << 3 << QFINDTESTDATA("fixtures/v3-demo.pdeck");
        QTest::newRow("v4") << 4 << QFINDTESTDATA("fixtures/v4-demo.pdeck");
    }
    void legacyOriginalRequiresConsent() {
        QFETCH(int, version);
        QFETCH(QString, fixture);
        QVERIFY(!fixture.isEmpty());
        QTemporaryDir dir;
        const QString file = dir.filePath("legacy.pdeck");
        const QString snapshot = dir.filePath("recovery.pdeck");
        const QString copy = dir.filePath("legacy-v5.pdeck");
        QVERIFY(QFile::copy(fixture, file));
        const QByteArray original = contents(file);
        QString error;
        Document d;
        QVERIFY2(d.load(file, &error), qPrintable(error));
        QCOMPARE(d.sourceVersion(), version);
        QVERIFY(d.requiresFormatUpgrade(file));
        QVERIFY(d.requiresFormatUpgrade(dir.filePath("./legacy.pdeck")));
        QVERIFY(!d.requiresFormatUpgrade(copy));
        edit(d);
        const quint64 revision = d.revision();
        QVERIFY(!d.save(file, &error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::FormatUpgradeRequired);
        QVERIFY(!error.isEmpty());
        QCOMPARE(contents(file), original);
        QCOMPARE(d.sourceVersion(), version);
        QCOMPARE(d.revision(), revision);
        QVERIFY(d.dirty);
        QVERIFY(d.save(snapshot, &error, false));
        QCOMPARE(storedVersion(snapshot), 5);
        QCOMPARE(d.sourceVersion(), version);
        QCOMPARE(d.path, file);
        QVERIFY(d.dirty);
        QVERIFY(!d.save(file, &error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::FormatUpgradeRequired);
        QCOMPARE(contents(file), original);
        QVERIFY(d.save(copy, &error));
        QCOMPARE(storedVersion(copy), 5);
        QCOMPARE(contents(file), original);
        QCOMPARE(d.sourceVersion(), 5);
        QCOMPARE(d.path, copy);
        QVERIFY(!d.dirty);
        QVERIFY(!d.requiresFormatUpgrade(copy));
        Document explicitUpgrade;
        QVERIFY(explicitUpgrade.load(file, &error));
        edit(explicitUpgrade, Qt::green);
        Document::SaveOptions consent;
        consent.allowFormatUpgrade = true;
        QVERIFY2(explicitUpgrade.save(file, &error, true, consent), qPrintable(error));
        QCOMPARE(storedVersion(file), 5);
        QCOMPARE(explicitUpgrade.sourceVersion(), 5);
        QVERIFY(!explicitUpgrade.dirty);
    }
    void externalLegacyEditRequiresBothIndependentPermissions() {
        const QString fixture = QFINDTESTDATA("fixtures/v4-demo.pdeck");
        QVERIFY(!fixture.isEmpty());
        QTemporaryDir dir;
        const QString file = dir.filePath("legacy.pdeck");
        QVERIFY(QFile::copy(fixture, file));
        QString error;
        Document d;
        QVERIFY(d.load(file, &error));
        edit(d);
        const QByteArray external("externally replaced legacy project");
        QVERIFY(replaceContents(file, external));
        QVERIFY(!d.save(file, &error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::ExternalModification);
        Document::SaveOptions consent;
        consent.allowExternalOverwrite = true;
        QVERIFY(!d.save(file, &error, true, consent));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::FormatUpgradeRequired);
        consent.allowExternalOverwrite = false;
        consent.allowFormatUpgrade = true;
        QVERIFY(!d.save(file, &error, true, consent));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::ExternalModification);
        QCOMPARE(contents(file), external);
        QVERIFY(d.dirty);
        consent.allowExternalOverwrite = true;
        QVERIFY(d.save(file, &error, true, consent));
        QCOMPARE(storedVersion(file), 5);
        QCOMPARE(d.sourceVersion(), 5);
    }
    void failuresKeepIdentityHistoryAndDirtyState() {
        QTemporaryDir dir;
        const QString file = dir.filePath("original.pdeck");
        const QString corrupt = dir.filePath("corrupt.pdeck");
        QString error;
        Document d;
        QVERIFY(d.create({12, 12}));
        QVERIFY(d.save(file, &error));
        edit(d);
        const QByteArray original = contents(file);
        const QImage image = d.composite();
        const quint64 revision = d.revision();
        const QStringList history = d.historyLabels();
        QVERIFY(replaceContents(corrupt, "not a valid project"));
        QVERIFY(!d.load(corrupt, &error));
        QVERIFY(!d.create({0, 12}));
        QVERIFY(!d.save(dir.filePath("absent/failure.pdeck"), &error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::Error);
        QCOMPARE(d.path, file);
        QCOMPARE(d.sourceVersion(), 5);
        QCOMPARE(d.revision(), revision);
        QCOMPARE(d.historyLabels(), history);
        QCOMPARE(d.composite(), image);
        QCOMPARE(contents(file), original);
        QVERIFY(d.dirty);
        QVERIFY(d.canUndo());
        d.state.layers[0].opacity = 2;
        QVERIFY(!d.save(file, &error));
        QCOMPARE(contents(file), original);
        QCOMPARE(d.sourceVersion(), 5);
        QVERIFY(d.dirty);
        d.state.layers[0].opacity = 1;
        const QByteArray external("new independent edit");
        QVERIFY(replaceContents(file, external));
        QVERIFY(!d.save(file, &error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::ExternalModification);
        QCOMPARE(contents(file), external);
    }
};
QTEST_MAIN(SaveSafetyTests)
#include "test_save_safety.moc"
