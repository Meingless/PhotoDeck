#include "brushpreset.h"
#include "document.h"
#include <QDataStream>
#include <QFile>
#include <QImageReader>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool writeBytes(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readBytes(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool sameLayer(const Layer &a, const Layer &b) {
    return a.name == b.name && a.pixels == b.pixels && a.mask == b.mask && a.offset == b.offset &&
           a.geometry == b.geometry && a.visible == b.visible && a.locked == b.locked &&
           a.clipped == b.clipped && a.opacity == b.opacity && a.blend == b.blend &&
           a.brightness == b.brightness && a.contrast == b.contrast &&
           a.saturation == b.saturation && a.blur == b.blur && a.tones == b.tones &&
           a.filters == b.filters && a.maskSettings == b.maskSettings && a.text == b.text &&
           a.font == b.font && a.color == b.color;
}
struct DocumentSnapshot {
    State state;
    QString path, title;
    bool dirty, canUndo, canRedo, editingMask, isEditing;
    int sourceVersion;
    Document::SaveStatus saveStatus;
    quint64 revision;
    qint64 historyBytes;
    QStringList history;
    QImage selection, composite;

    explicit DocumentSnapshot(const Document &d)
        : state(d.state), path(d.path), title(d.title), dirty(d.dirty), canUndo(d.canUndo()),
          canRedo(d.canRedo()), editingMask(d.editingMask), isEditing(d.isEditing()),
          sourceVersion(d.sourceVersion()), saveStatus(d.lastSaveStatus()),
          revision(d.revision()), historyBytes(d.historyBytes()), history(d.historyLabels()),
          selection(d.selection.mask(d.state.size)), composite(d.composite()) {}

    bool matches(const Document &d) const {
        if (state.size != d.state.size || state.active != d.state.active ||
            state.layers.size() != d.state.layers.size() || path != d.path || title != d.title ||
            dirty != d.dirty || canUndo != d.canUndo() || canRedo != d.canRedo() ||
            editingMask != d.editingMask || isEditing != d.isEditing() || revision != d.revision() ||
            sourceVersion != d.sourceVersion() || saveStatus != d.lastSaveStatus() ||
            historyBytes != d.historyBytes() || history != d.historyLabels() ||
            selection != d.selection.mask(d.state.size) || composite != d.composite())
            return false;
        for (qsizetype i = 0; i < state.layers.size(); ++i)
            if (!sameLayer(state.layers[i], d.state.layers[i]))
                return false;
        return true;
    }
};
bool prepareSentinel(Document &d, QString *error, const QString &nativePath = {}) {
    if (!d.create({8, 6}, true))
        return false;
    d.beginEdit();
    d.state.layers[0].name = "Keep source";
    d.state.layers[0].pixels.setPixelColor(2, 3, QColor(20, 80, 140));
    d.state.layers[0].mask = QImage(d.state.size, QImage::Format_ARGB32_Premultiplied);
    d.state.layers[0].mask.fill(QColor(180, 180, 180));
    d.commit("Keep edit");
    if (!nativePath.isEmpty()) {
        if (!d.save(nativePath, error))
            return false;
        d.beginEdit();
        d.state.layers[0].pixels.setPixelColor(5, 1, QColor(80, 40, 120));
        d.commit("Keep unsaved edit");
    }
    if (!d.addLayer("Keep redo", error))
        return false;
    d.undo();
    d.path = nativePath.isEmpty() ? QString("sentinel.pdeck") : nativePath;
    d.title = "Keep title";
    d.selection = QRect(1, 2, 3, 2);
    d.editingMask = true;
    return d.canUndo() && d.canRedo() && d.dirty;
}
quint32 nextRandom(quint32 &state) {
    // Deliberately fixed and independent of Qt/platform random generators.
    state = state * 1664525u + 1013904223u;
    return state;
}
QByteArray mutate(const QByteArray &source, quint32 &random) {
    QByteArray bytes = source;
    const int changes = 1 + int(nextRandom(random) % 4);
    for (int i = 0; i < changes; ++i) {
        const qsizetype at = qsizetype(nextRandom(random) % quint32(bytes.size()));
        bytes[at] = char(quint8(bytes[at]) ^ quint8(1 + nextRandom(random) % 255));
    }
    return bytes;
}
} // namespace

class ParserSafetyTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        // Keep this deterministic regression corpus small, including image-decoder allocations.
        QImageReader::setAllocationLimit(32);
    }
    void projectTruncationsAndLengthsAreTransactional() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        Document source, target;
        QString error;
        QVERIFY(prepareSentinel(source, &error));
        const QString validPath = temp.filePath("valid.pdeck");
        QVERIFY2(source.save(validPath, &error), qPrintable(error));
        const QByteArray valid = readBytes(validPath);
        QVERIFY(!valid.isEmpty() && valid.size() < 16384);
        Document control;
        QVERIFY2(control.load(validPath, &error), qPrintable(error));
        QCOMPARE(control.composite(), source.composite());

        QVERIFY(prepareSentinel(target, &error, temp.filePath("keep.pdeck")));
        const DocumentSnapshot before(target);
        const QString badPath = temp.filePath("malformed.pdeck");
        for (qsizetype length = 0; length < valid.size(); ++length) {
            const QByteArray context = QByteArray("project truncation at ") + QByteArray::number(length);
            QVERIFY(writeBytes(badPath, valid.left(length)));
            QVERIFY2(!target.load(badPath, &error), context.constData());
            QVERIFY2(before.matches(target), context.constData());
        }
        for (quint32 length : {1u, 32769u, 0x7ffffffeu, 0xfffffffeu}) {
            QByteArray bytes;
            QDataStream stream(&bytes, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_2);
            stream << quint32(0x50444543) << quint32(5) << QSize(8, 6) << qint32(0) << qint32(1)
                   << length;
            QVERIFY(writeBytes(badPath, bytes));
            QVERIFY(!target.load(badPath, &error));
            QVERIFY(before.matches(target));
        }
        QVERIFY(writeBytes(badPath, valid + QByteArray("unexpected trailing data")));
        QVERIFY(!target.load(badPath, &error));
        QVERIFY(before.matches(target));
        // Rejection must also preserve usable redo/undo state, not just its labels.
        target.redo();
        QCOMPARE(target.state.layers.size(), 2);
        target.undo();
        QCOMPARE(target.composite(), before.composite);
    }
    void deterministicProjectMutationsAreTransactional() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        Document source;
        QString error;
        QVERIFY(prepareSentinel(source, &error));
        const QString validPath = temp.filePath("valid.pdeck");
        QVERIFY2(source.save(validPath, &error), qPrintable(error));
        const QByteArray valid = readBytes(validPath);
        QVERIFY(!valid.isEmpty() && valid.size() < 16384);
        quint32 random = 0x50444543u;
        int rejected = 0;
        for (int sample = 0; sample < 256; ++sample) {
            Document target;
            QVERIFY(prepareSentinel(target, &error, temp.filePath("keep.pdeck")));
            const DocumentSnapshot before(target);
            const QByteArray context = QByteArray("project mutation ") + QByteArray::number(sample);
            const QString path = temp.filePath("mutated.pdeck");
            QVERIFY(writeBytes(path, mutate(valid, random)));
            if (!target.load(path, &error)) {
                ++rejected;
                QVERIFY2(before.matches(target), context.constData());
            } else {
                // Some byte changes are legal. A successful parse must remain saveable/reopenable.
                QVERIFY(Document::validSize(target.state.size));
                QVERIFY(target.state.layers.size() >= 1 && target.state.layers.size() <= 64);
                QVERIFY(target.imageBytes() <= Document::MaxLayerBytes);
                const QImage accepted = target.composite();
                const QString roundTrip = temp.filePath("roundtrip.pdeck");
                QVERIFY2(target.save(roundTrip, &error), context.constData());
                Document reopened;
                QVERIFY2(reopened.load(roundTrip, &error), context.constData());
                QCOMPARE(reopened.composite(), accepted);
            }
        }
        QVERIFY(rejected > 0);
    }
    void brushLibraryCorpusPreservesDestination() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        BrushSettings brush;
        brush.size = 17;
        brush.tipMask = QImage(5, 3, QImage::Format_Grayscale8);
        brush.tipMask.fill(200);
        const QVector<BrushPreset> validPresets{{"Small tip", 0, brush}};
        const QVector<BrushPreset> sentinel{{"Keep destination", 1, {}}};
        const QString validPath = temp.filePath("valid.pdbrush");
        QVERIFY2(BrushLibrary::write(validPath, validPresets, &error), qPrintable(error));
        // Remove optional JSON whitespace so every proper prefix is truly truncated.
        const QByteArray valid = readBytes(validPath).trimmed();
        QVERIFY(!valid.isEmpty() && valid.size() < 16384);
        QVector<BrushPreset> control;
        QVERIFY2(BrushLibrary::read(validPath, &control, &error), qPrintable(error));
        QVERIFY(control == validPresets);
        const QString path = temp.filePath("mutated.pdbrush");
        for (qsizetype length = 0; length < valid.size(); ++length) {
            QVector<BrushPreset> out = sentinel;
            const QByteArray context = QByteArray("brush truncation at ") + QByteArray::number(length);
            QVERIFY(writeBytes(path, valid.left(length)));
            QVERIFY2(!BrushLibrary::read(path, &out, &error), context.constData());
            QVERIFY2(out == sentinel, context.constData());
        }
        quint32 random = 0x42525553u;
        int rejected = 0;
        for (int sample = 0; sample < 256; ++sample) {
            QVector<BrushPreset> out = sentinel;
            const QByteArray context = QByteArray("brush mutation ") + QByteArray::number(sample);
            QVERIFY(writeBytes(path, mutate(valid, random)));
            if (!BrushLibrary::read(path, &out, &error)) {
                ++rejected;
                QVERIFY2(out == sentinel, context.constData());
            } else {
                QVERIFY2(BrushLibrary::valid(out, &error), context.constData());
                const QString roundTrip = temp.filePath("roundtrip.pdbrush");
                QVERIFY2(BrushLibrary::write(roundTrip, out, &error), context.constData());
                QVector<BrushPreset> reopened;
                QVERIFY2(BrushLibrary::read(roundTrip, &reopened, &error), context.constData());
                QVERIFY(reopened == out);
            }
        }
        QVERIFY(rejected > 0);
    }
};

QTEST_MAIN(ParserSafetyTests)
#include "test_parser_safety.moc"
