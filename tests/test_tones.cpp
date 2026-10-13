#include "mainwindow.h"
#include "tonedialog.h"
#include <QAction>
#include <QBuffer>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QImageReader>
#include <QImageWriter>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

class ToneTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName("PhotoDeckToneTests");
        QApplication::setOrganizationName("PhotoDeckTests");
        QImageReader::setAllocationLimit(128);
    }
    void identityPreservesAllChannelsAndAlpha() {
        ToneSettings settings;
        QVERIFY(settings.valid());
        QVERIFY(settings.isIdentity());
        ToneMapper mapper(settings);
        for (int i = 0; i < 256; ++i) {
            QRgb color = qRgba(i, 255 - i, (i * 17) % 256, i);
            QCOMPARE(mapper.map(color), color);
        }
    }
    void levelsGammaAndOutputRange() {
        ToneSettings s;
        s.inputBlack = 40;
        s.inputWhite = 200;
        ToneMapper mapper(s);
        QCOMPARE(qRed(mapper.map(qRgb(40, 40, 40))), 0);
        QCOMPARE(qRed(mapper.map(qRgb(200, 200, 200))), 255);
        QCOMPARE(qRed(mapper.map(qRgb(120, 120, 120))), 128);
        s.gamma = 2;
        ToneMapper gamma(s);
        QVERIFY(qAbs(qRed(gamma.map(qRgb(120, 120, 120))) - 180) <= 1);
        s.outputBlack = 30;
        s.outputWhite = 210;
        ToneMapper out(s);
        QCOMPARE(qRed(out.map(qRgb(0, 0, 0))), 30);
        QCOMPARE(qRed(out.map(qRgb(255, 255, 255))), 210);
    }
    void exposureUsesLinearSrgb() {
        ToneSettings s;
        s.exposure = 1;
        ToneMapper mapper(s);
        // sRGB 128 is about .216 linear; +1 EV encodes about 176, not 255.
        QVERIFY(qAbs(qRed(mapper.map(qRgb(128, 128, 128))) - 176) <= 1);
        s.exposure = -1;
        ToneMapper dark(s);
        QVERIFY(qAbs(qRed(dark.map(qRgb(128, 128, 128))) - 92) <= 1);
        QCOMPARE(qAlpha(dark.map(qRgba(128, 128, 128, 67))), 67);
    }
    void rgbCurvesAndChannelIsolation() {
        ToneSettings s;
        s.curves[0] = {{0, 0}, {128, 200}, {255, 255}};
        ToneMapper all(s);
        QCOMPARE(all.map(qRgb(128, 128, 128)), qRgb(200, 200, 200));
        s = {};
        s.curves[1] = {{0, 0}, {128, 200}, {255, 255}};
        ToneMapper red(s);
        QCOMPARE(red.map(qRgb(128, 128, 128)), qRgb(200, 128, 128));
        s.curves[2] = {{0, 255}, {255, 0}};
        ToneMapper green(s);
        QCOMPARE(green.map(qRgb(128, 80, 128)), qRgb(200, 175, 128));
    }
    void hueVibranceAndChannelBalance() {
        ToneSettings s;
        s.hue = 120;
        ToneMapper hue(s);
        QCOMPARE(hue.map(qRgb(255, 0, 0)), qRgb(0, 255, 0));
        s = {};
        s.vibrance = -100;
        ToneMapper desaturate(s);
        QColor gray(desaturate.map(qRgb(200, 80, 60)));
        QCOMPARE(gray.red(), gray.green());
        QCOMPARE(gray.green(), gray.blue());
        s.vibrance = 100;
        ToneMapper vivid(s);
        QRgb neutral = vivid.map(qRgb(128, 128, 128));
        QCOMPARE(neutral, qRgb(128, 128, 128));
        QColor before(200, 120, 100), after(vivid.map(before.rgba()));
        QVERIFY(after.hsvSaturation() > before.hsvSaturation());
        s = {};
        s.redBalance = 20;
        s.blueBalance = -20;
        ToneMapper balance(s);
        QCOMPARE(balance.map(qRgb(128, 128, 128)), qRgb(179, 128, 77));
    }
    void effectsKeepAlphaAndQuantize() {
        ToneSettings s;
        s.inverted = true;
        ToneMapper inverse(s);
        QCOMPARE(inverse.map(qRgba(10, 80, 240, 65)), qRgba(245, 175, 15, 65));
        s = {};
        s.posterize = 2;
        ToneMapper two(s);
        QCOMPARE(two.map(qRgba(40, 170, 100, 70)), qRgba(0, 255, 0, 70));
        s = {};
        s.threshold = 128;
        ToneMapper threshold(s);
        QCOMPARE(threshold.map(qRgba(127, 127, 127, 30)), qRgba(0, 0, 0, 30));
        QCOMPARE(threshold.map(qRgba(129, 129, 129, 30)), qRgba(255, 255, 255, 30));
    }
    void invalidValuesFailBeforeMutation() {
        Document d;
        QVERIFY(d.create({10, 10}));
        QString error;
        ToneSettings s;
        s.gamma = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!s.valid());
        QVERIFY(!d.adjustTones(s, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(d.state.layers[0].tones.isIdentity());
        s = {};
        s.exposure = std::numeric_limits<double>::infinity();
        QVERIFY(!s.valid());
        s = {};
        s.inputBlack = 255;
        QVERIFY(!s.valid());
        s = {};
        s.outputBlack = 200;
        s.outputWhite = 100;
        QVERIFY(!s.valid());
        s = {};
        s.curves[0] = {{0, 0}, {120, 80}, {120, 120}, {255, 255}};
        QVERIFY(!s.valid());
        s = {};
        s.curves[0].append({256, 255});
        QVERIFY(!s.valid());
        s = {};
        s.curves[2][0] = {1, 0};
        QVERIFY(!s.valid());
        d.state.layers[0].locked = true;
        s = {};
        s.inverted = true;
        QVERIFY(!d.adjustTones(s));
        QVERIFY(d.state.layers[0].tones.isIdentity());
    }
    void serializationRejectsTruncationAndHugeCurve() {
        ToneSettings s;
        s.curves[3] = {{0, 30}, {50, 100}, {255, 220}};
        QByteArray blob;
        QDataStream writer(&blob, QIODevice::WriteOnly);
        writer.setVersion(QDataStream::Qt_6_2);
        s.write(writer);
        QDataStream reader(blob);
        reader.setVersion(QDataStream::Qt_6_2);
        ToneSettings decoded;
        QVERIFY(ToneSettings::read(reader, decoded));
        QVERIFY(decoded == s);
        QByteArray partial = blob.left(blob.size() - 1);
        QDataStream truncated(partial);
        decoded.exposure = 1;
        ToneSettings preserved = decoded;
        QVERIFY(!ToneSettings::read(truncated, decoded));
        QVERIFY(decoded == preserved);
        // Fixed scalar prefix: 4 ints, 2 doubles, 5 ints, bool, 2 ints = 61 bytes.
        blob[61] = char(0x7f);
        blob[62] = char(0xff);
        blob[63] = char(0xff);
        blob[64] = char(0xff);
        QDataStream huge(blob);
        QVERIFY(!ToneSettings::read(huge, decoded));
        QVERIFY(decoded == preserved);
    }
    void sourceTextMaskAndCacheArePreserved() {
        Document d;
        QVERIFY(d.create({100, 80}));
        QString error;
        QVERIFY(d.addText("Türkçe İŞ", QFont("Sans", 16), QColor(180, 70, 50), {2, 3}, &error));
        QVERIFY(d.addMask(&error));
        d.beginEdit();
        d.state.layers[d.state.active].mask.fill(QColor(100, 100, 100));
        d.commit("Maske boyandı");
        d.markSaved();
        const Layer original = d.state.layers[d.state.active];
        const QImage composite = d.composite();
        ToneSettings s;
        s.inverted = true;
        d.beginEdit();
        QVERIFY(d.adjustTones(s));
        d.commit("Ton");
        const Layer &active = d.state.layers[d.state.active];
        QCOMPARE(active.pixels, original.pixels);
        QCOMPARE(active.mask, original.mask);
        QCOMPARE(active.text, original.text);
        QVERIFY(d.composite() != composite);
        const QImage rendered = active.rendered();
        QCOMPARE(active.rendered(), rendered);
        s.redBalance = 30;
        d.beginEdit();
        QVERIFY(d.adjustTones(s));
        d.commit("Kırmızı");
        QVERIFY(d.state.layers[d.state.active].rendered() != rendered);
        d.undo();
        QCOMPARE(d.state.layers[d.state.active].rendered(), rendered);
        d.undo();
        QCOMPARE(d.composite(), composite);
        QVERIFY(!d.dirty);
        d.redo();
        QVERIFY(d.dirty);
        QCOMPARE(d.state.layers[d.state.active].rendered(), rendered);
    }
    void nativeCurrentRoundTripAndLegacyV1() {
        Document d;
        QVERIFY(d.create({20, 15}));
        d.state.layers[0].pixels.fill(QColor(120, 80, 60, 90));
        ToneSettings s;
        s.inputBlack = 20;
        s.inputWhite = 220;
        s.exposure = .4;
        s.gamma = 1.5;
        s.outputBlack = 15;
        s.outputWhite = 240;
        s.hue = 15;
        s.vibrance = 30;
        s.redBalance = 5;
        s.greenBalance = -4;
        s.blueBalance = 8;
        s.inverted = true;
        s.posterize = 20;
        s.curves[3] = {{0, 0}, {70, 100}, {255, 255}};
        d.beginEdit();
        QVERIFY(d.adjustTones(s));
        d.commit("Ton");
        QTemporaryDir temp;
        QString file = temp.filePath("toned.pdeck"), error;
        QVERIFY2(d.save(file, &error), qPrintable(error));
        Document restored;
        QVERIFY2(restored.load(file, &error), qPrintable(error));
        QVERIFY(restored.state.layers[0].tones == s);
        QCOMPARE(restored.composite(), d.composite());
        QCOMPARE(restored.state.layers[0].pixels, d.state.layers[0].pixels);
        QString legacy = QFINDTESTDATA("../examples/Horizon.pdeck");
        QVERIFY(!legacy.isEmpty());
        QVERIFY2(restored.load(legacy, &error), qPrintable(error));
        for (const Layer &layer : restored.state.layers)
            QVERIFY(layer.tones.isIdentity());
        QVERIFY(restored.save(file, &error));
        QVERIFY(d.load(file, &error));
        QCOMPARE(d.composite(), restored.composite());
    }
    void malformedV2DoesNotReplaceDocumentOrFile() {
        Document d;
        QVERIFY(d.create({20, 20}, true));
        QString error;
        QTemporaryDir temp;
        QString file = temp.filePath("safe.pdeck");
        QVERIFY(d.save(file, &error));
        QImage previous = d.composite();
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray valid = f.readAll();
        f.close();
        d.state.layers[0].tones.gamma = 0;
        QVERIFY(!d.save(file, &error));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), valid);
        f.close();
        d.state.layers[0].tones = {};
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(valid.left(valid.size() - 3)), qint64(valid.size() - 3));
        f.close();
        QVERIFY(!d.load(file, &error));
        QCOMPARE(d.composite(), previous);
        QCOMPARE(d.path, file);
        QVERIFY(d.state.layers[0].tones.isIdentity());
    }
    void tiffAndGifUseAvailableNativeCodecs() {
        QVERIFY(QImageWriter::supportedImageFormats().contains("tiff"));
        Document d;
        QVERIFY(d.create({12, 8}));
        QString error;
        QTemporaryDir temp;
        d.state.layers[0].pixels.fill(QColor(200, 100, 50, 128));
        for (QString extension : {"tif", "tiff"}) {
            QString path = temp.filePath("image." + extension);
            QVERIFY2(d.exportImage(path, 100, &error), qPrintable(error));
            QImage decoded = Document::readImage(path, &error);
            QCOMPARE(decoded.size(), d.state.size);
            QColor pixel = decoded.pixelColor(2, 2);
            QVERIFY(qAbs(pixel.red() - 200) <= 2);
            QCOMPARE(pixel.alpha(), 128);
        }
        QFile gif(temp.filePath("first-frame.gif"));
        QVERIFY(gif.open(QIODevice::WriteOnly));
        QByteArray data =
            QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7");
        QCOMPARE(gif.write(data), qint64(data.size()));
        gif.close();
        QImage decoded = Document::readImage(gif.fileName(), &error);
        QVERIFY2(!decoded.isNull(), qPrintable(error));
        QCOMPARE(decoded.size(), QSize(1, 1));
    }
    void curveMouseKeyboardAndNumericEditing() {
        CurveEditor editor;
        editor.resize(300, 260);
        editor.show();
        QTest::qWait(10);
        QPoint middle(156, 124);
        QTest::mouseClick(&editor, Qt::LeftButton, {}, middle);
        QCOMPARE(editor.points().size(), 3);
        QPoint previous = editor.selectedPoint();
        QTest::keyClick(&editor, Qt::Key_Up);
        QCOMPARE(editor.selectedPoint().y(), previous.y() + 1);
        editor.setSelectedPoint({300, -20});
        QCOMPARE(editor.selectedPoint(), QPoint(254, 0));
        QTest::keyClick(&editor, Qt::Key_Delete);
        QCOMPARE(editor.points().size(), 2);
        editor.setSelectedPoint({300, 40});
        QCOMPARE(editor.selectedPoint(), QPoint(255, 40));
        editor.removeSelectedPoint();
        QCOMPARE(editor.points().size(), 2);
    }
    void dialogPreviewCancelAndOneStepUndo() {
        MainWindow window(false, false);
        window.show();
        auto *d = new Document;
        QVERIFY(d->create({30, 30}, true));
        window.addDocument(d);
        QAction *action = window.findChild<QAction *>("tones");
        QVERIFY(action);
        QImage source = d->composite();
        bool handled = false;
        bool preview = false;
        QTimer driver;
        driver.setInterval(10);
        connect(&driver, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<ToneDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *spin = dialog->findChild<QSpinBox *>("outputWhite");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!spin || !buttons) {
                dialog->reject();
                return;
            }
            spin->setValue(160);
            preview = d->composite() != source && !d->dirty;
            QTest::mouseClick(buttons->button(QDialogButtonBox::Cancel), Qt::LeftButton);
            handled = true;
        });
        driver.start();
        action->trigger();
        driver.stop();
        QVERIFY(handled);
        QVERIFY(preview);
        QCOMPARE(d->composite(), source);
        QVERIFY(!d->dirty);
        QVERIFY(!d->canUndo());
        disconnect(&driver, nullptr, nullptr, nullptr);
        handled = false;
        connect(&driver, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<ToneDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *spin = dialog->findChild<QDoubleSpinBox *>("exposure");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!spin || !buttons) {
                dialog->reject();
                return;
            }
            spin->setValue(-1);
            spin->setValue(-2);
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            handled = true;
        });
        driver.start();
        action->trigger();
        driver.stop();
        QVERIFY(handled);
        QVERIFY(d->dirty);
        QVERIFY(d->canUndo());
        QCOMPARE(d->historyLabels().size(), 2);
        QVERIFY(d->composite() != source);
        d->undo();
        QCOMPARE(d->composite(), source);
        QVERIFY(!d->dirty);
        d->redo();
        QCOMPARE(d->state.layers[0].tones.exposure, -2.0);
        d->beginEdit();
        d->state.layers[0].locked = true;
        d->commit("Kilit");
        QTRY_VERIFY(!action->isEnabled());
        d->undo();
        d->undo();
        window.close();
    }
};
QTEST_MAIN(ToneTests)
#include "test_tones.moc"
