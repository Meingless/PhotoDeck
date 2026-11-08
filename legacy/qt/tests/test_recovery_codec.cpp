#include "document.h"
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
// Frozen 58a121d writer, deliberately independent of the extracted implementation.
// Exact bytes protect the ordinary-save v5/v6 policy and stream/PNG field ordering.
void oldImage(QDataStream &out, const QImage &image) {
    QByteArray data;
    if (!image.isNull()) {
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) qFatal("Test PNG encode failed");
    }
    out << quint32(data.size());
    if (!data.isEmpty()) out.writeRawData(data.constData(), data.size());
}
void oldSelection(QDataStream &out, const Selection &selection) {
    const QImage &mask = selection.coverageImage();
    const quint8 kind = selection.isEmpty() ? 0 : mask.isNull() ? 1 : 2;
    out << kind;
    if (kind == 1) out << selection.bounds();
    else if (kind == 2) {
        out << mask.size() << quint32(qint64(mask.width()) * mask.height());
        for (int y = 0; y < mask.height(); ++y)
            out.writeRawData(reinterpret_cast<const char *>(mask.constScanLine(y)), mask.width());
    }
}
QByteArray oldBytes(const State &state, bool markClean) {
    const bool context = !markClean &&
        (!state.selection.isEmpty() || !state.previousSelection.isEmpty());
    const int version = context || !state.savedSelections.isEmpty() ? 6 : 5;
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_2);
    out << quint32(0x50444543) << quint32(version) << state.size << qint32(state.active)
        << qint32(state.layers.size());
    for (const auto &l : state.layers) {
        out << l.name << l.offset << l.visible << l.locked << double(l.opacity) << qint32(l.blend)
            << qint32(l.brightness) << qint32(l.contrast) << qint32(l.saturation) << qint32(l.blur)
            << l.text << l.font.toString() << l.color;
        l.tones.write(out);
        out << l.geometry << l.clipped;
        l.filters.write(out);
        l.maskSettings.write(out);
        oldImage(out, l.pixels);
        oldImage(out, l.mask);
    }
    if (version >= 6) {
        out << quint8(context ? 1 : 0) << quint32(state.savedSelections.size());
        for (const auto &saved : state.savedSelections) {
            out << saved.name;
            oldSelection(out, saved.selection);
        }
        if (context) {
            oldSelection(out, state.selection);
            oldSelection(out, state.previousSelection);
        }
    }
    return bytes;
}
QImage coverage(QSize size, int phase) {
    QImage mask(size, QImage::Format_Grayscale8);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x)
            mask.scanLine(y)[x] = (x * 17 + y * 31 + phase) & 255;
    return mask;
}
void fixture(Document &document, bool named, bool context) {
    if (!document.create({37, 29})) qFatal("Fixture create failed");
    document.beginEdit();
    Layer &base = document.state.layers[0];
    for (int y = 0; y < base.pixels.height(); ++y)
        for (int x = 0; x < base.pixels.width(); ++x)
            base.pixels.setPixelColor(x, y, QColor((x * 11) & 255, (y * 13) & 255, 71, 191));
    base.name = QString::fromUtf8("Katman 日本語 🎨");
    base.offset = {-3, 5};
    base.opacity = .625;
    base.brightness = -12;
    base.contrast = 14;
    base.saturation = 27;
    base.blur = 1;
    base.text = "Text metadata";
    base.font = QFont("Sans Serif", 11);
    base.color = QColor(11, 47, 91, 171);
    base.geometry.rotate(7);
    base.mask = QImage(base.pixels.size(), QImage::Format_ARGB32_Premultiplied);
    base.mask.fill(QColor(177, 177, 177));
    Layer top = base;
    top.name = "Clipped hidden";
    top.offset = {6, -4};
    top.clipped = true;
    top.visible = false;
    top.locked = true;
    top.blend = 3;
    top.geometry = {};
    top.mask = {};
    document.state.layers.append(top);
    document.state.active = 1;
    if (named) document.state.savedSelections = {
        {"Hard", Selection(QRect(2, 3, 9, 7))},
        {QString::fromUtf8("柔らかい 🎭"), Selection::fromMask(coverage(document.state.size, 7))}};
    if (context) {
        document.state.selection = Selection::fromMask(coverage(document.state.size, 19));
        document.state.previousSelection = Selection(QRect(5, 6, 11, 9));
    }
    document.commit("Fixture");
}
QByteArray contents(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
class LimitedDevice : public QIODevice {
  public:
    qint64 remaining;
    explicit LimitedDevice(qint64 limit) : remaining(limit) { open(QIODevice::WriteOnly); }
    bool isSequential() const override { return true; }
  protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *, qint64 length) override {
        if (length > remaining) { setErrorString("Injected device failure"); return -1; }
        remaining -= length;
        return length;
    }
};
}
class RecoveryCodecTests : public QObject {
    Q_OBJECT
  private slots:
    void exactBytes_data() {
        QTest::addColumn<bool>("named");
        QTest::addColumn<bool>("context");
        QTest::addColumn<bool>("markClean");
        for (bool named : {false, true})
            for (bool context : {false, true})
                for (bool clean : {false, true})
                    QTest::newRow(qPrintable(QString("named%1-context%2-clean%3")
                        .arg(named).arg(context).arg(clean))) << named << context << clean;
    }
    void exactBytes() {
        QFETCH(bool, named);
        QFETCH(bool, context);
        QFETCH(bool, markClean);
        Document document;
        fixture(document, named, context);
        QTemporaryDir directory;
        const QString path = directory.filePath("project.pdeck");
        const State original = document.state;
        const quint64 revision = document.revision();
        const auto status = document.lastSaveStatus();
        const QString oldTitle = document.title;
        QString error;
        QVERIFY2(document.save(path, &error, markClean), qPrintable(error));
        QCOMPARE(contents(path), oldBytes(original, markClean));
        if (!markClean) {
            QCOMPARE(document.revision(), revision);
            QCOMPARE(document.lastSaveStatus(), status);
            QCOMPARE(document.title, oldTitle);
            QVERIFY(document.path.isEmpty());
            QVERIFY(document.dirty);
            QCOMPARE(document.sourceVersion(), 0);
        }
        // The static worker codec must exactly match pre-extraction recovery, too.
        QByteArray bytes;
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY2(Document::writeRecoverySnapshot(Document::recoverySnapshot(original), buffer, &error),
                 qPrintable(error));
        QCOMPARE(bytes, oldBytes(original, false));
        const QString recovery = directory.filePath("recovery.pdeck");
        QFile file(recovery);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        Document loaded;
        QVERIFY2(loaded.load(recovery, &error), qPrintable(error));
        QCOMPARE(loaded.state.layers[0].pixels, original.layers[0].pixels);
        QCOMPARE(loaded.state.layers[0].mask, original.layers[0].mask);
        QCOMPARE(loaded.state.layers[0].geometry, original.layers[0].geometry);
        QCOMPARE(loaded.state.layers[1].clipped, original.layers[1].clipped);
        QCOMPARE(loaded.state.savedSelections.size(), original.savedSelections.size());
        QVERIFY(loaded.state.selection.equivalent(original.selection, original.size));
        QVERIFY(loaded.state.previousSelection.equivalent(original.previousSelection, original.size));
    }
    void snapshotDetachesMetadataSharesOnlyPayload() {
        Document document;
        fixture(document, true, true);
        document.composite();
        document.state.selection.boundaryImage();
        const qint64 pixels = document.state.layers[0].pixels.cacheKey();
        const qint64 mask = document.state.layers[0].mask.cacheKey();
        const qint64 selection = document.state.selection.coverageImage().cacheKey();
        const State &live = document.state;
        const auto *liveLayers = live.layers.constData();
        const auto *liveNamed = live.savedSelections.constData();
        State snapshot = Document::recoverySnapshot(live);
        QVERIFY(snapshot.layers.constData() != liveLayers);
        QVERIFY(snapshot.savedSelections.constData() != liveNamed);
        QCOMPARE(snapshot.layers[0].pixels.cacheKey(), pixels);
        QCOMPARE(snapshot.layers[0].mask.cacheKey(), mask);
        QCOMPARE(snapshot.selection.coverageImage().cacheKey(), selection);
        QVERIFY(snapshot.layers[0].cache.isNull());
        QVERIFY(!document.state.layers[0].cache.isNull());
        document.beginEdit();
        document.state.layers[0].pixels.fill(Qt::red);
        document.state.layers[0].name = "Changed";
        document.state.savedSelections[0].name = "Renamed";
        document.state.selection = {};
        document.commit("After snapshot");
        QCOMPARE(snapshot.layers[0].pixels.cacheKey(), pixels);
        QCOMPARE(snapshot.layers[0].mask.cacheKey(), mask);
        QCOMPARE(snapshot.selection.coverageImage().cacheKey(), selection);
        QCOMPARE(snapshot.layers[0].name, QString::fromUtf8("Katman 日本語 🎨"));
        QCOMPARE(snapshot.savedSelections[0].name, "Hard");
    }
    void cancellationAndDeviceFailureAreValueOnly() {
        Document document;
        fixture(document, true, true);
        const auto revision = document.revision();
        const auto status = document.lastSaveStatus();
        const State snapshot = Document::recoverySnapshot(document.state);
        std::atomic_bool cancel{true};
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        QString error;
        QVERIFY(!Document::writeRecoverySnapshot(snapshot, buffer, &error, &cancel));
        QVERIFY(bytes.isEmpty());
        LimitedDevice failure(64);
        QVERIFY(!Document::writeRecoverySnapshot(snapshot, failure, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(document.revision(), revision);
        QCOMPARE(document.lastSaveStatus(), status);
        QVERIFY(document.dirty);
        QVERIFY(document.path.isEmpty());
    }
};
QTEST_MAIN(RecoveryCodecTests)
#include "test_recovery_codec.moc"
