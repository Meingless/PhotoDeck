#include "mainwindow.h"
#include <QAction>
#include <QDataStream>
#include <QFile>
#include <QImageReader>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

namespace {
QByteArray contents(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool writeBytes(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
Selection soft(QSize size, int variant = 0) {
    QImage mask(size, QImage::Format_Grayscale8);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x)
            mask.scanLine(y)[x] = (x < 2 || y < 2) ? 0 : (x * 19 + y * 11 + variant) % 256;
    return Selection::fromMask(mask);
}
QPoint at(Canvas &canvas, int x, int y) {
    return canvas.toWidget({qreal(x), qreal(y)}).toPoint();
}
void drag(Canvas &canvas, QPoint from, QPoint to) {
    QTest::mousePress(&canvas, Qt::LeftButton, {}, from);
    QTest::mouseMove(&canvas, to);
    QTest::mouseRelease(&canvas, Qt::LeftButton, {}, to);
}
struct Snapshot {
    quint64 revision;
    QStringList history, names;
    QString path, title;
    bool dirty, undo, redo, currentActive, previousActive;
    QImage current, previous, composite;
    QVector<QImage> saved;
    explicit Snapshot(const Document &d)
        : revision(d.revision()), history(d.historyLabels()), names(d.savedSelectionNames()),
          path(d.path), title(d.title), dirty(d.dirty), undo(d.canUndo()), redo(d.canRedo()),
          currentActive(!d.selection.isEmpty()), previousActive(!d.state.previousSelection.isEmpty()),
          current(d.selection.mask(d.state.size)), previous(d.state.previousSelection.mask(d.state.size)),
          composite(d.composite()) {
        for (const auto &channel : d.state.savedSelections)
            saved.append(channel.selection.mask(d.state.size));
    }
    bool matches(const Document &d) const {
        if (revision != d.revision() || history != d.historyLabels() || names != d.savedSelectionNames() ||
            path != d.path || title != d.title || dirty != d.dirty || undo != d.canUndo() ||
            redo != d.canRedo() || currentActive != !d.selection.isEmpty() ||
            previousActive != !d.state.previousSelection.isEmpty() ||
            current != d.selection.mask(d.state.size) ||
            previous != d.state.previousSelection.mask(d.state.size) || composite != d.composite())
            return false;
        for (int i = 0; i < saved.size(); ++i)
            if (saved[i] != d.state.savedSelections[i].selection.mask(d.state.size))
                return false;
        return true;
    }
};
QByteArray extension(const std::function<void(QDataStream &)> &write) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_2);
    write(stream);
    return result;
}
QByteArray v6FromV5(QByteArray bytes, const QByteArray &extension) {
    bytes[7] = 6;
    return bytes + extension;
}
// Read the released v5 field layout, preserving every non-image byte exactly.
// Embedded PNGs are decoded/re-encoded on this runtime: compatible PNG encoders
// can produce different compressed bytes for identical pixels and metadata.
QByteArray canonicalV5Images(const QByteArray &bytes) {
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_2);
    quint32 magic = 0, version = 0;
    QSize size;
    qint32 active = 0, count = 0;
    in >> magic >> version >> size >> active >> count;
    if (in.status() != QDataStream::Ok || magic != 0x50444543 || version != 5 || count < 1 ||
        count > 64)
        return {};
    QByteArray result;
    QDataStream out(&result, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_2);
    qint64 metadataStart = 0;
    for (int i = 0; i < count; ++i) {
        QString name, text, font;
        QPoint offset;
        bool visible = false, locked = false, clipped = false;
        double opacity = 0;
        qint32 blend = 0, brightness = 0, contrast = 0, saturation = 0, blur = 0;
        QColor color;
        QTransform geometry;
        ToneSettings tones;
        FilterSettings filters;
        MaskSettings maskSettings;
        in >> name >> offset >> visible >> locked >> opacity >> blend >> brightness >> contrast >>
            saturation >> blur >> text >> font >> color;
        if (!ToneSettings::read(in, tones))
            return {};
        in >> geometry >> clipped;
        if (!FilterSettings::read(in, filters) || !MaskSettings::read(in, maskSettings))
            return {};
        const qint64 metadataEnd = in.device()->pos();
        if (out.writeRawData(bytes.constData() + metadataStart, metadataEnd - metadataStart) !=
            metadataEnd - metadataStart)
            return {};
        for (int imageIndex = 0; imageIndex < 2; ++imageIndex) {
            quint32 length = 0;
            in >> length;
            if (in.status() != QDataStream::Ok || length > in.device()->bytesAvailable())
                return {};
            QByteArray png(length, Qt::Uninitialized);
            if (in.readRawData(png.data(), length) != qint64(length))
                return {};
            QImage image;
            if (length) {
                image = QImage::fromData(png, "PNG");
                if (image.isNull())
                    return {};
                image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            }
            out << image;
        }
        metadataStart = in.device()->pos();
    }
    return in.status() == QDataStream::Ok && in.device()->atEnd() && out.status() == QDataStream::Ok
               ? result
               : QByteArray();
}
} // namespace

class SelectionWorkflowTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName("PhotoDeckSelectionWorkflowTests");
        QApplication::setOrganizationName("PhotoDeckTests");
        QImageReader::setAllocationLimit(128);
    }
    void selectionHistoryIsTransientAndNoopsKeepRedo() {
        Document d;
        QVERIFY(d.create({30, 20}));
        const auto hard = Selection(3, 4, 12, 10), feather = hard.feathered(2, d.state.size);
        const quint64 initial = d.revision();
        QVERIFY(d.setSelection(hard));
        QVERIFY(d.revision() != initial && d.canUndo() && !d.dirty);
        const quint64 first = d.revision();
        QVERIFY(d.setSelection(Selection::fromMask(hard.mask(d.state.size))));
        QCOMPARE(d.revision(), first); // Rectangle and identical byte coverage are the same selection.
        QVERIFY(d.setSelection(hard));
        QCOMPARE(d.revision(), first);
        QVERIFY(d.setSelection(feather));
        QVERIFY(d.deselect());
        QVERIFY(d.canReselect());
        const quint64 deselected = d.revision();
        QVERIFY(d.deselect());
        QCOMPARE(d.revision(), deselected);
        QVERIFY(d.reselect());
        QCOMPARE(d.selection.mask(d.state.size), feather.mask(d.state.size));
        d.undo();
        QVERIFY(d.selection.isEmpty() && d.canReselect() && !d.dirty);
        QVERIFY(d.setSelection({}));
        QVERIFY(d.canRedo());
        d.redo();
        QCOMPARE(d.selection.mask(d.state.size), feather.mask(d.state.size));
        QVERIFY(!d.dirty);
        d.undo();
        QVERIFY(d.setSelection(hard));
        QVERIFY(!d.canRedo());
        QVERIFY(!d.dirty);
    }
    void rectangleEquivalenceUsesCanvasCoverageAndKeepsActiveState() {
        const QSize canvas(30, 20);
        QVERIFY(Selection(-5, -8, 50, 50).equivalent(Selection(0, 0, 30, 20), canvas));
        QVERIFY(Selection(-5, 3, 10, 6).equivalent(Selection(0, 3, 5, 6), canvas));
        QVERIFY(Selection(50, 50, 3, 3).equivalent(Selection(-5, -5, 2, 2), canvas));
        QVERIFY(!Selection(50, 50, 3, 3).equivalent(Selection(0, 0, 1, 1), canvas));
        QVERIFY(!Selection(3, 4, 12, 10).equivalent(Selection(3, 4, 12, 11), canvas));
        QVERIFY(!Selection().equivalent(Selection(0, 0, 30, 20), canvas));
        QVERIFY(!Selection().equivalent(Selection(50, 50, 3, 3), canvas));
        const Selection hard(-5, 3, 10, 6);
        QVERIFY(hard.equivalent(Selection::fromMask(hard.mask(canvas)), canvas));
        // Preserve the existing mask(canvas) comparison semantics for invalid canvases too.
        QVERIFY(Selection(1, 1, 2, 2).equivalent(Selection(3, 3, 4, 4), QSize()));
    }
    void activeEmptyIsDifferentFromUnrestrictedAcrossUndoAndReselect() {
        Document d;
        QVERIFY(d.create({20, 10}));
        const Selection zero = Selection(0, 0, 20, 10).inverted(d.state.size);
        QVERIFY(d.setSelection(zero));
        QVERIFY(!d.selection.isEmpty() && !d.selection.hasPixels());
        QCOMPARE(d.selection.valueAt({8, 5}), 0);
        d.undo();
        QVERIFY(d.selection.isEmpty());
        QCOMPARE(d.selection.valueAt({8, 5}), 255);
        d.redo();
        QVERIFY(!d.selection.isEmpty() && !d.selection.hasPixels());
        QVERIFY(d.deselect());
        QVERIFY(d.reselect());
        QVERIFY(!d.selection.isEmpty() && !d.selection.hasPixels());
    }
    void pixelLayerUndoAndCanceledEditKeepCoverage() {
        Document d;
        QVERIFY(d.create({30, 20}));
        const Selection coverage = soft(d.state.size);
        QVERIFY(d.setSelection(coverage));
        const QImage selected = d.selection.mask(d.state.size);
        d.beginEdit();
        d.state.layers[0].pixels.fill(Qt::red);
        d.commit("Paint");
        d.undo();
        QCOMPARE(d.selection.mask(d.state.size), selected);
        QCOMPARE(qAlpha(d.composite().pixel(5, 5)), 0);
        d.redo();
        QCOMPARE(d.selection.mask(d.state.size), selected);
        QString error;
        QVERIFY(d.addLayer("New layer", &error));
        d.undo();
        QCOMPARE(d.selection.mask(d.state.size), selected);
        const Snapshot before(d);
        d.beginEdit();
        d.selection = {};
        d.state.previousSelection = {};
        d.state.layers[0].pixels.fill(Qt::blue);
        d.cancelEdit();
        QVERIFY(before.matches(d));
        // A legacy direct assignment is captured at the next pixel operation boundary too.
        d.selection = coverage.feathered(1, d.state.size);
        const QImage legacy = d.selection.mask(d.state.size);
        d.beginEdit();
        d.state.layers[0].pixels.fill(Qt::green);
        d.commit("Legacy caller paint");
        d.undo();
        QCOMPARE(d.selection.mask(d.state.size), legacy);
    }
    void namedSelectionsUsePersistentSaveIdentityAndUndo() {
        Document d;
        QVERIFY(d.create({30, 20}));
        QVERIFY(d.setSelection(soft(d.state.size)));
        QString error;
        QVERIFY(d.saveSelection("Sky", &error));
        QVERIFY(d.dirty);
        QCOMPARE(d.projectSaveVersion(), 6);
        d.markSaved();
        const quint64 saved = d.revision();
        QVERIFY(d.saveSelection("Sky", &error));
        QCOMPARE(d.revision(), saved);
        QVERIFY(d.deselect());
        QVERIFY(!d.dirty);
        QVERIFY(d.loadSelection("Sky", Selection::Replace, &error));
        QVERIFY(!d.dirty);
        QVERIFY(d.removeSavedSelection("Sky", &error));
        QVERIFY(d.dirty);
        QCOMPARE(d.projectSaveVersion(), 5);
        d.undo();
        QCOMPARE(d.savedSelectionNames(), QStringList{"Sky"});
        QVERIFY(!d.dirty);
        d.redo();
        QVERIFY(d.savedSelectionNames().isEmpty() && d.dirty);
    }
    void invalidNamedSelectionOperationsAreTransactional() {
        Document d;
        QVERIFY(d.create({30, 20}));
        QString error;
        QVERIFY(!d.saveSelection("No selection", &error));
        QVERIFY(d.setSelection(Selection(3, 4, 12, 10)));
        for (int i = 0; i < Document::MaxSavedSelections; ++i)
            QVERIFY(d.saveSelection(QString::number(i), &error));
        d.undo();
        const Snapshot before(d);
        for (const QString &name : {QString(), QString("  "), QString(" leading"),
                                    QString(Document::MaxSelectionNameLength + 1, QChar('x')),
                                    QString("line\nbreak"), QString::fromLatin1("null\0name", 9)}) {
            QVERIFY(!d.saveSelection(name, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(before.matches(d));
        }
        QVERIFY(!d.loadSelection("0", Selection::Mode(99), &error));
        QVERIFY(before.matches(d));
        QVERIFY(!d.loadSelection("Missing", Selection::Replace, &error));
        QVERIFY(!d.removeSavedSelection("Missing", &error));
        QVERIFY(before.matches(d));
        d.redo();
        const Snapshot atLimit(d);
        QVERIFY(!d.saveSelection("Overflow", &error));
        QVERIFY(atLimit.matches(d));
        d.beginEdit();
        QVERIFY(!d.setSelection({}, "Blocked", &error));
        QVERIFY(!d.saveSelection("Blocked", &error));
        QVERIFY(!d.removeSavedSelection("0", &error));
        d.cancelEdit();
        QVERIFY(atLimit.matches(d));
    }
    void namedSelectionLoadCombinesRealCoverage() {
        Document d;
        QVERIFY(d.create({30, 20}));
        const Selection a = soft(d.state.size), b = Selection(4, 3, 10, 8).feathered(2, d.state.size);
        QString error;
        QVERIFY(d.setSelection(a));
        QVERIFY(d.saveSelection("A", &error));
        for (auto mode : {Selection::Replace, Selection::Add, Selection::Subtract, Selection::Intersect}) {
            QVERIFY(d.setSelection(b));
            const QImage expected = b.combined(a, mode, d.state.size).mask(d.state.size);
            QVERIFY(d.loadSelection("A", mode, &error));
            QCOMPARE(d.selection.mask(d.state.size), expected);
            d.undo();
            QCOMPARE(d.selection.mask(d.state.size), b.mask(d.state.size));
        }
    }
    void documentTransformsMapEverySelectionChannel_data() {
        QTest::addColumn<QString>("operation");
        for (const auto &op : {"crop", "resize", "clockwise", "counterclockwise", "horizontal", "vertical", "canvas"})
            QTest::newRow(op) << QString(op);
    }
    void documentTransformsMapEverySelectionChannel() {
        QFETCH(QString, operation);
        Document d;
        QVERIFY(d.create({30, 20}));
        const Selection a = soft(d.state.size), b = soft(d.state.size, 101);
        QString error;
        QVERIFY(d.setSelection(a));
        QVERIFY(d.saveSelection("A", &error));
        QVERIFY(d.setSelection(b));
        const QSize originalSize = d.state.size;
        const QImage originalA = a.mask(originalSize), originalB = b.mask(originalSize);
        auto expected = [&](const QImage &source) {
            if (operation == "crop")
                return source.copy(QRect(5, 3, 17, 11));
            if (operation == "resize")
                return source.scaled(QSize(45, 30), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            if (operation == "clockwise" || operation == "counterclockwise") {
                QTransform t;
                t.rotate(operation == "clockwise" ? 90 : -90);
                return source.transformed(t);
            }
            if (operation == "horizontal" || operation == "vertical")
                return source.mirrored(operation == "horizontal", operation == "vertical");
            QImage enlarged(40, 26, QImage::Format_Grayscale8);
            enlarged.fill(0);
            for (int y = 0; y < source.height(); ++y)
                std::copy_n(source.constScanLine(y), source.width(), enlarged.scanLine(y + 3) + 5);
            return enlarged;
        };
        bool ok = operation == "crop" ? d.crop(QRect(5, 3, 17, 11), &error)
                  : operation == "resize" ? d.resize({45, 30}, &error)
                  : operation == "clockwise" ? d.rotate(true, &error)
                  : operation == "counterclockwise" ? d.rotate(false, &error)
                  : operation == "horizontal" ? d.flip(true, &error)
                  : operation == "vertical" ? d.flip(false, &error)
                  : d.canvasSize({40, 26}, {1, 1}, &error);
        QVERIFY2(ok, qPrintable(error));
        QCOMPARE(d.selection.mask(d.state.size), expected(originalB));
        QCOMPARE(d.state.previousSelection.mask(d.state.size), expected(originalA));
        QCOMPARE(d.state.savedSelections[0].selection.mask(d.state.size), expected(originalA));
        d.undo();
        QCOMPARE(d.state.size, originalSize);
        QCOMPARE(d.selection.mask(d.state.size), originalB);
        QCOMPARE(d.state.previousSelection.mask(d.state.size), originalA);
        QCOMPARE(d.state.savedSelections[0].selection.mask(d.state.size), originalA);
        d.redo();
        QCOMPARE(d.selection.mask(d.state.size), expected(originalB));
    }
    void transformsKeepUnrestrictedAndActiveEmptyDistinct() {
        for (bool active : {false, true}) {
            Document d;
            QVERIFY(d.create({20, 10}));
            QString error;
            if (active) {
                QVERIFY(d.setSelection(Selection(0, 0, 20, 10).inverted(d.state.size)));
                QVERIFY(d.saveSelection("Empty", &error));
            }
            QVERIFY(d.resize({24, 14}, &error));
            QVERIFY(d.flip(true, &error));
            QVERIFY(d.rotate(true, &error));
            QVERIFY(d.crop(QRect(2, 3, 9, 12), &error));
            QVERIFY(d.canvasSize({13, 16}, {1, 1}, &error));
            QCOMPARE(d.selection.isEmpty(), !active);
            QCOMPARE(d.selection.valueAt({5, 5}), active ? 0 : 255);
            if (active)
                QVERIFY(!d.state.savedSelections[0].selection.hasPixels());
        }
        Document outside;
        QVERIFY(outside.create({20, 10}));
        QVERIFY(outside.setSelection(Selection(15, 5, 4, 4)));
        QString error;
        QVERIFY(outside.crop(QRect(0, 0, 5, 4), &error));
        QVERIFY(!outside.selection.isEmpty() && !outside.selection.hasPixels());
    }
    void gesturesAreOneUndoStepAndAbortPreservesCoverage() {
        Document d;
        QVERIFY(d.create({100, 100}, true));
        Canvas c(&d);
        c.resize(500, 500);
        c.show();
        c.fit();
        c.setTool(Canvas::Select);
        const quint64 start = d.revision();
        drag(c, at(c, 10, 10), at(c, 50, 50));
        QCOMPARE(d.historyLabels().size(), 2);
        d.undo();
        QCOMPARE(d.revision(), start);
        QVERIFY(d.selection.isEmpty());
        d.redo();
        const QImage selected = d.selection.mask(d.state.size);
        const quint64 first = d.revision();
        drag(c, at(c, 10, 10), at(c, 50, 50));
        QCOMPARE(d.revision(), first);
        for (auto tool : {Canvas::EllipseSelect, Canvas::Lasso, Canvas::PolygonLasso, Canvas::Crop}) {
            c.setTool(tool);
            QTest::mousePress(&c, Qt::LeftButton, {}, at(c, 20, 20));
            QTest::mouseMove(&c, at(c, 70, 70));
            QTest::keyClick(&c, Qt::Key_Escape);
            QTest::mouseRelease(&c, Qt::LeftButton, {}, at(c, 70, 70));
            QCOMPARE(d.revision(), first);
            QCOMPARE(d.selection.mask(d.state.size), selected);
        }
        c.setTool(Canvas::Brush);
        const QImage pixels = d.state.layers[0].pixels;
        QTest::mousePress(&c, Qt::LeftButton, {}, at(c, 30, 30));
        QTest::mouseMove(&c, at(c, 40, 40));
        QTest::keyClick(&c, Qt::Key_Escape);
        QTest::mouseRelease(&c, Qt::LeftButton, {}, at(c, 40, 40));
        QCOMPARE(d.selection.mask(d.state.size), selected);
        QCOMPARE(d.state.layers[0].pixels, pixels);
        QCOMPARE(d.revision(), first);
        c.setTool(Canvas::Select);
        QTest::mousePress(&c, Qt::LeftButton, {}, at(c, 20, 20));
        QTest::mouseMove(&c, at(c, 70, 70));
        d.undo();
        const quint64 undone = d.revision();
        QTest::mouseRelease(&c, Qt::LeftButton, {}, at(c, 70, 70));
        QCOMPARE(d.revision(), undone);
        QVERIFY(d.selection.isEmpty());
    }
    void v5CompatibilityAndV6NamedRoundtrip() {
        QTemporaryDir dir;
        const QString original = dir.filePath("original.pdeck"), copy = dir.filePath("named-v6.pdeck");
        const QString fixture = QFINDTESTDATA("fixtures/v5-demo.pdeck");
        QVERIFY(!fixture.isEmpty() && QFile::copy(fixture, original));
        const QByteArray baseline = contents(original);
        const QByteArray compatibleBaseline = canonicalV5Images(baseline);
        QVERIFY(!compatibleBaseline.isEmpty());
        Document d;
        QString error;
        QVERIFY2(d.load(original, &error), qPrintable(error));
        QCOMPARE(d.sourceVersion(), 5);
        QCOMPARE(d.projectSaveVersion(), 5);
        QVERIFY(!d.requiresFormatUpgrade(original));
        QVERIFY(d.setSelection(Selection(3, 4, 12, 10)));
        QVERIFY(!d.dirty);
        QVERIFY2(d.save(original, &error), qPrintable(error));
        const QByteArray savedV5 = contents(original);
        QCOMPARE(canonicalV5Images(savedV5), compatibleBaseline);
        const Selection feather = Selection(8, 9, 20, 20).feathered(3, d.state.size);
        QVERIFY(d.setSelection(feather));
        QVERIFY(d.saveSelection("Feather", &error));
        QVERIFY(d.setSelection(Selection(0, 0, d.state.size.width(), d.state.size.height()).inverted(d.state.size)));
        QVERIFY(d.saveSelection("Active empty", &error));
        QVERIFY(d.setSelection(Selection(5, 6, 12, 15)));
        QVERIFY(d.saveSelection("Rectangle", &error));
        QCOMPARE(d.projectSaveVersion(), 6);
        QVERIFY(d.requiresFormatUpgrade(original));
        const Snapshot before(d);
        QVERIFY(!d.save(original, &error));
        QCOMPARE(d.lastSaveStatus(), Document::SaveStatus::FormatUpgradeRequired);
        QVERIFY(before.matches(d));
        QCOMPARE(contents(original), savedV5); // Rejected upgrades must preserve exact disk bytes.
        QVERIFY2(d.save(copy, &error), qPrintable(error));
        QCOMPARE(d.sourceVersion(), 6);
        Document reopened;
        QVERIFY2(reopened.load(copy, &error), qPrintable(error));
        QCOMPARE(reopened.savedSelectionNames(), d.savedSelectionNames());
        QVERIFY(reopened.selection.isEmpty() && !reopened.canReselect());
        QVERIFY(reopened.loadSelection("Feather", Selection::Replace, &error));
        QCOMPARE(reopened.selection.mask(reopened.state.size), feather.mask(reopened.state.size));
        QVERIFY(!reopened.dirty);
        QVERIFY(reopened.loadSelection("Active empty", Selection::Replace, &error));
        QVERIFY(!reopened.selection.isEmpty() && !reopened.selection.hasPixels());
        QVERIFY(reopened.loadSelection("Rectangle", Selection::Replace, &error));
        QCOMPARE(reopened.selection.bounds(), QRect(5, 6, 12, 15));
        for (const auto &name : reopened.savedSelectionNames())
            QVERIFY(reopened.removeSavedSelection(name, &error));
        QCOMPARE(reopened.projectSaveVersion(), 5);
        QVERIFY(!reopened.requiresFormatUpgrade(copy));
        QVERIFY(reopened.save(copy, &error));
        QCOMPARE(reopened.sourceVersion(), 5);
        QCOMPARE(canonicalV5Images(contents(copy)), compatibleBaseline);
        QCOMPARE(contents(fixture), baseline);
    }
    void recoveryContextIsExplicitAndDoesNotAdoptSnapshot() {
        Document d;
        QVERIFY(d.create({30, 20}));
        QString error;
        QTemporaryDir dir;
        const QString original = dir.filePath("work.pdeck"), recovery = dir.filePath("recovery.pdeck");
        QVERIFY(d.save(original, &error));
        const Selection first = soft(d.state.size);
        QVERIFY(d.setSelection(first));
        QVERIFY(d.setSelection(Selection(4, 5, 8, 9)));
        const Snapshot before(d);
        QVERIFY(d.save(recovery, &error, false));
        QVERIFY(before.matches(d));
        QCOMPARE(d.sourceVersion(), 5);
        QCOMPARE(d.projectSaveVersion(), 5);
        Document recovered;
        QVERIFY2(recovered.load(recovery, &error), qPrintable(error));
        QCOMPARE(recovered.sourceVersion(), 6);
        QCOMPARE(recovered.selection.mask(recovered.state.size), before.current);
        QCOMPARE(recovered.state.previousSelection.mask(recovered.state.size), before.previous);
        recovered.markRecovered();
        QVERIFY(recovered.path.isEmpty() && recovered.dirty);
        QVERIFY(recovered.deselect());
        QVERIFY(recovered.reselect());
        QCOMPARE(recovered.selection.mask(recovered.state.size), before.current);
        QVERIFY(recovered.save(dir.filePath("normal.pdeck"), &error));
        QCOMPARE(recovered.sourceVersion(), 5);
    }
    void selectionParserBoundsAndTruncationsAreTransactional() {
        Document base;
        QVERIFY(base.create({8, 6}));
        QTemporaryDir dir;
        QString error;
        const QString valid = dir.filePath("base.pdeck"), bad = dir.filePath("bad.pdeck");
        QVERIFY(base.save(valid, &error));
        const QByteArray v5 = contents(valid);
        const QByteArray legal = extension([](QDataStream &s) {
            s << quint8(1) << quint32(1) << QString("Soft") << quint8(2) << QSize(8, 6) << quint32(48);
            for (int i = 0; i < 48; ++i)
                s << quint8(i * 5);
            s << quint8(1) << QRect(1, 2, 3, 2) << quint8(0);
        });
        Document sentinel;
        QVERIFY(sentinel.create({8, 6}));
        QVERIFY(sentinel.setSelection(soft(sentinel.state.size)));
        QVERIFY(sentinel.saveSelection("Keep", &error));
        QVERIFY(sentinel.setSelection(Selection(2, 1, 3, 2)));
        sentinel.undo();
        const Snapshot before(sentinel);
        for (qsizetype cut = 0; cut < legal.size(); ++cut) {
            QVERIFY(writeBytes(bad, v6FromV5(v5, legal.left(cut))));
            QVERIFY2(!sentinel.load(bad, &error), qPrintable(QString::number(cut)));
            QVERIFY(before.matches(sentinel));
        }
        const QVector<QByteArray> malformed{
            extension([](QDataStream &s) { s << quint8(2) << quint32(0); }),
            extension([](QDataStream &s) { s << quint8(0) << quint32(17); }),
            extension([](QDataStream &s) { s << quint8(0) << quint32(1) << quint32(258); }),
            extension([](QDataStream &s) { s << quint8(0) << quint32(1) << QString("A") << quint8(3); }),
            extension([](QDataStream &s) { s << quint8(0) << quint32(1) << QString("A") << quint8(0); }),
            extension([](QDataStream &s) { s << quint8(0) << quint32(1) << QString("A") << quint8(1) << QRect(-1, 0, 2, 2); }),
            extension([](QDataStream &s) { s << quint8(0) << quint32(1) << QString("A") << quint8(2) << QSize(7, 6) << quint32(42); }),
            extension([](QDataStream &s) { s << quint8(0) << quint32(1) << QString("A") << quint8(2) << QSize(8, 6) << quint32(0xffffffffu); }),
            extension([](QDataStream &s) { s << quint8(0) << quint32(2) << QString("A") << quint8(1) << QRect(0, 0, 2, 2) << QString("A") << quint8(1) << QRect(0, 0, 2, 2); })
        };
        for (const auto &bytes : malformed) {
            QVERIFY(writeBytes(bad, v6FromV5(v5, bytes)));
            QVERIFY(!sentinel.load(bad, &error));
            QVERIFY(before.matches(sentinel));
        }
        QVERIFY(writeBytes(bad, v6FromV5(v5, legal)));
        QVERIFY2(sentinel.load(bad, &error), qPrintable(error));
        QCOMPARE(sentinel.savedSelectionNames(), QStringList{"Soft"});
        QVERIFY(sentinel.selection == QRect(1, 2, 3, 2));
        QCOMPARE(sentinel.state.savedSelections[0].selection.valueAt({7, 5}), 235);
    }
    void paddedSelectionRowsCountAgainstDecodedBudget() {
        QTemporaryDir dir;
        const QSize canvas(4093, 4097);
        const qint64 packed = qint64(canvas.width()) * canvas.height();
        const qint64 decoded = qint64((canvas.width() + 3) & ~3) * canvas.height();
        QVERIFY(packed * 4 <= Document::MaxSelectionBytes);
        QVERIFY(decoded * 4 > Document::MaxSelectionBytes);
        Document base;
        QString error;
        QVERIFY(base.create(canvas));
        const QString file = dir.filePath("base.pdeck");
        QVERIFY(base.save(file, &error));
        QByteArray header = contents(file);
        header[7] = 6;
        for (int count : {3, 4}) {
            const QString path = dir.filePath(QString::number(count) + ".pdeck");
            QFile out(path);
            QVERIFY(out.open(QIODevice::WriteOnly));
            QCOMPARE(out.write(header), header.size());
            QDataStream stream(&out);
            stream.setVersion(QDataStream::Qt_6_2);
            stream << quint8(0) << quint32(count);
            const QByteArray row(canvas.width(), char(170));
            for (int i = 0; i < count; ++i) {
                stream << QString::number(i) << quint8(2) << canvas << quint32(packed);
                for (int y = 0; y < canvas.height(); ++y)
                    QCOMPARE(stream.writeRawData(row.constData(), row.size()), row.size());
            }
            out.close();
            Document sentinel;
            QVERIFY(sentinel.create({8, 6}));
            QVERIFY(sentinel.setSelection(Selection(1, 2, 3, 2)));
            QVERIFY(sentinel.saveSelection("Keep", &error));
            QVERIFY(sentinel.deselect());
            sentinel.undo();
            const Snapshot before(sentinel);
            if (count == 3) {
                QVERIFY2(sentinel.load(path, &error), qPrintable(error));
                QCOMPARE(sentinel.savedSelectionNames().size(), 3);
            } else {
                QVERIFY(!sentinel.load(path, &error));
                QVERIFY(before.matches(sentinel));
            }
        }
    }
    void selectionBudgetAndSharedHistoryAreBounded() {
        Document shared;
        QVERIFY(shared.create({200, 200}));
        QString error;
        const Selection coverage = soft(shared.state.size);
        QVERIFY(shared.setSelection(coverage));
        for (int i = 0; i < 10; ++i)
            QVERIFY(shared.saveSelection(QString::number(i), &error));
        QCOMPARE(shared.historyBytes(), qint64(0));
        for (const auto &name : shared.savedSelectionNames())
            QVERIFY(shared.removeSavedSelection(name, &error));
        QVERIFY(shared.deselect());
        shared.beginEdit();
        shared.state.previousSelection = {};
        shared.commit("Drop reselect");
        QCOMPARE(shared.historyBytes(), coverage.coverageImage().sizeInBytes());

        Document large;
        QVERIFY(large.create({2048, 2048}));
        QImage mask(large.state.size, QImage::Format_Grayscale8);
        mask.fill(170);
        QVERIFY(large.setSelection(Selection::fromMask(mask)));
        for (int i = 0; i < 15; ++i)
            QVERIFY(large.saveSelection(QString::number(i), &error));
        const Snapshot atBudget(large);
        QVERIFY(!large.saveSelection("Over budget", &error));
        QVERIFY(atBudget.matches(large));
        mask.fill(90);
        QVERIFY(!large.setSelection(Selection::fromMask(mask), "Over budget", &error));
        QVERIFY(atBudget.matches(large));
        QVERIFY(!large.resize({4096, 4096}, &error));
        QVERIFY(atBudget.matches(large));
        QVERIFY(large.create({4096, 2048})); // Isolate the undo-budget stress case.
        mask = QImage(large.state.size, QImage::Format_Grayscale8);
        for (int i = 0; i < 40; ++i) {
            mask.fill(i);
            QVERIFY(large.setSelection(Selection::fromMask(mask)));
            QVERIFY(large.historyBytes() <= Document::HistoryBudget);
        }
        QVERIFY(large.canUndo());
        QVERIFY(large.historyLabels().size() < 30); // The byte budget trimmed before the count limit.
    }
};
QTEST_MAIN(SelectionWorkflowTests)
#include "test_selection_workflow.moc"
