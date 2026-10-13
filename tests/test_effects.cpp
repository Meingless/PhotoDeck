#include "effectdialog.h"
#include "mainwindow.h"
#include <QAction>
#include <QBuffer>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void pattern(Document &d) {
    d.beginEdit();
    d.state.layers[0].pixels.fill(Qt::transparent);
    QPainter p(&d.state.layers[0].pixels);
    p.fillRect(5, 5, 10, 10, QColor(180, 60, 40, 180));
    p.end();
    d.commit("Pattern");
    d.markSaved();
}
bool modal(QAction *action, const std::function<void(QDialog *)> &handler) {
    QTimer driver, watchdog;
    driver.setInterval(10);
    watchdog.setSingleShot(true);
    bool done = false;
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d)
            return;
        driver.stop();
        handler(d);
        done = true;
    });
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            d->reject();
    });
    driver.start();
    watchdog.start(5000);
    action->trigger();
    driver.stop();
    watchdog.stop();
    return done;
}
} // namespace
class EffectTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void identityAndConstantImages() {
        QImage image(20, 15, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(160, 80, 40, 128));
        FilterSettings s;
        QVERIFY(s.valid());
        QVERIFY(s.isIdentity());
        QCOMPARE(Filters::apply(image, s), image);
        QCOMPARE(Filters::gaussianBlur(image, 5), image);
        s.sharpenAmount = 200;
        QCOMPARE(Filters::apply(image, s), image);
        s = {};
        s.highPass = 3;
        QColor result = Filters::apply(image, s).pixelColor(10, 8);
        QCOMPARE(result.alpha(), 128);
        QVERIFY(qAbs(result.red() - 128) <= 1);
        QVERIFY(qAbs(result.green() - 128) <= 1);
        QVERIFY(qAbs(result.blue() - 128) <= 1);
    }
    void gaussianMatchesIndependentImpulseKernel() {
        QImage image(17, 17, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        image.setPixelColor(8, 8, Qt::white);
        QImage blurred = Filters::gaussianBlur(image, 2);
        QCOMPARE(qAlpha(blurred.pixel(8, 8)), 41);
        QCOMPARE(qAlpha(blurred.pixel(9, 8)), 25);
        QCOMPARE(qAlpha(blurred.pixel(7, 8)), 25);
        QCOMPARE(qAlpha(blurred.pixel(9, 9)), 15);
        QCOMPARE(qAlpha(blurred.pixel(11, 8)), 0);
        int total = 0;
        for (int y = 0; y < 17; ++y)
            for (int x = 0; x < 17; ++x) {
                QRgb p = blurred.pixel(x, y);
                total += qAlpha(p);
                QCOMPARE(qRed(p), qAlpha(p));
            }
        QVERIFY(qAbs(total - 255) <= 6);
        QCOMPARE(image.pixelColor(8, 8), QColor(Qt::white));
    }
    void transparentPixelsDoNotBleedHiddenColor() {
        QImage image(15, 15, QImage::Format_ARGB32);
        image.fill(qRgba(0, 0, 255, 0));
        image.setPixelColor(7, 7, QColor(255, 0, 0, 255));
        QImage blurred = Filters::gaussianBlur(image, 3);
        for (int y = 0; y < 15; ++y)
            for (int x = 0; x < 15; ++x) {
                QRgb p = blurred.pixel(x, y);
                QCOMPARE(qBlue(p), 0);
                QVERIFY(qRed(p) <= qAlpha(p));
            }
    }
    void mosaicAveragesPremultipliedCoverage() {
        QImage image(2, 1, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        image.setPixelColor(0, 0, Qt::red);
        FilterSettings s;
        s.mosaic = 2;
        QImage result = Filters::apply(image, s);
        QCOMPARE(result.pixel(0, 0), qRgba(128, 0, 0, 128));
        QCOMPARE(result.pixel(1, 0), result.pixel(0, 0));
    }
    void noiseStableSeedMonochromeAndColor() {
        QImage image(60, 30, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(128, 128, 128, 128));
        FilterSettings s;
        s.noise = 20;
        s.noiseSeed = 1234;
        QImage a = Filters::apply(image, s);
        QCOMPARE(Filters::apply(image, s), a);
        QVERIFY(a != image);
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x) {
                QRgb p = a.pixel(x, y);
                QCOMPARE(qAlpha(p), 128);
                QCOMPARE(qRed(p), qGreen(p));
                QCOMPARE(qGreen(p), qBlue(p));
            }
        s.noiseSeed = 9876;
        QVERIFY(Filters::apply(image, s) != a);
        s.monochromeNoise = false;
        QImage colored = Filters::apply(image, s);
        bool different = false;
        for (int y = 0; y < colored.height(); ++y)
            for (int x = 0; x < colored.width(); ++x) {
                QRgb p = colored.pixel(x, y);
                different |= qRed(p) != qGreen(p);
                QCOMPARE(qAlpha(p), 128);
            }
        QVERIFY(different);
    }
    void sharpenThresholdAndAlpha() {
        QImage image(30, 20, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(100, 100, 100, 128));
        for (int y = 0; y < 20; ++y)
            for (int x = 15; x < 30; ++x)
                image.setPixelColor(x, y, QColor(180, 180, 180, 128));
        FilterSettings s;
        s.sharpenAmount = 150;
        s.sharpenRadius = 3;
        QImage sharpened = Filters::apply(image, s);
        QVERIFY(sharpened.pixelColor(14, 10).red() < image.pixelColor(14, 10).red());
        QVERIFY(sharpened.pixelColor(15, 10).red() > image.pixelColor(15, 10).red());
        QCOMPARE(qAlpha(sharpened.pixel(15, 10)), 128);
        s.sharpenThreshold = 255;
        QCOMPARE(Filters::apply(image, s), image);
    }
    void maskDensityDisableAndTrueGaussianFeather() {
        QImage mask(20, 20, QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::black);
        {
            QPainter p(&mask);
            p.fillRect(10, 0, 10, 20, Qt::white);
        }
        MaskSettings s;
        QImage coverage = Filters::maskCoverage(mask, s);
        QCOMPARE(int(coverage.constScanLine(10)[2]), 0);
        QCOMPARE(int(coverage.constScanLine(10)[15]), 255);
        s.density = 50;
        coverage = Filters::maskCoverage(mask, s);
        QCOMPARE(int(coverage.constScanLine(10)[2]), 127);
        s.density = 100;
        s.feather = 3;
        coverage = Filters::maskCoverage(mask, s);
        QVERIFY(coverage.constScanLine(10)[9] > 0 && coverage.constScanLine(10)[9] < 255);
        QVERIFY(coverage.constScanLine(10)[10] > coverage.constScanLine(10)[9]);
        s.enabled = false;
        QVERIFY(Filters::maskCoverage(mask, s).isNull());
        s.enabled = true;
        s.density = 0;
        QVERIFY(Filters::maskCoverage(mask, s).isNull());
        mask.fill(Qt::white);
        s.density = 100;
        coverage = Filters::maskCoverage(mask, s);
        QCOMPARE(int(coverage.constScanLine(0)[0]), 255);
        QImage gray(50, 10, QImage::Format_Grayscale8);
        gray.fill(120);
        s.feather = 0;
        coverage = Filters::maskCoverage(gray, s);
        QCOMPARE(int(coverage.constScanLine(3)[20]), 120);
    }
    void sourceTextMaskCacheUndoAndRasterization() {
        Document d;
        d.create({100, 70});
        QString e;
        QVERIFY(d.addText("Türkçe İŞ", QFont("Sans", 16), Qt::red, {5, 5}, &e));
        QVERIFY(d.addMask(&e));
        d.markSaved();
        const Layer original = d.state.layers[d.state.active];
        const QImage before = d.composite();
        FilterSettings s;
        s.gaussian = 2;
        s.noise = 4;
        d.beginEdit();
        QVERIFY(d.adjustFilters(s, &e));
        d.commit("Filters");
        MaskSettings m;
        m.density = 40;
        m.feather = 2;
        d.beginEdit();
        QVERIFY(d.adjustMask(m, &e));
        d.commit("Mask settings");
        const Layer &active = d.state.layers[d.state.active];
        QCOMPARE(active.pixels, original.pixels);
        QCOMPARE(active.mask, original.mask);
        QCOMPARE(active.text, original.text);
        QImage filtered = d.composite();
        QVERIFY(filtered != before);
        QCOMPARE(d.composite(), filtered);
        d.undo();
        d.undo();
        QCOMPARE(d.composite(), before);
        QVERIFY(!d.dirty);
        d.redo();
        d.redo();
        QCOMPARE(d.composite(), filtered);
        QVERIFY(d.rasterizeLayer(&e));
        QCOMPARE(d.composite(), filtered);
        QVERIFY(d.state.layers[d.state.active].filters.isIdentity());
        QVERIFY(d.state.layers[d.state.active].mask.isNull());
        d.undo();
        QVERIFY(!d.state.layers[d.state.active].mask.isNull());
    }
    void maskDisableRestoresSourceAndRemovalResetsSettings() {
        Document d;
        d.create({20, 20}, true);
        QString e;
        QVERIFY(d.addMask(&e));
        d.beginEdit();
        d.state.layers[0].mask.fill(Qt::black);
        d.commit("Mask black");
        QCOMPARE(qAlpha(d.composite().pixel(5, 5)), 0);
        MaskSettings m;
        m.enabled = false;
        d.beginEdit();
        QVERIFY(d.adjustMask(m));
        d.commit("Disable");
        QCOMPARE(qAlpha(d.composite().pixel(5, 5)), 255);
        d.removeMask();
        QVERIFY(d.state.layers[0].maskSettings == MaskSettings{});
        QVERIFY(d.addMask(&e));
        QVERIFY(d.state.layers[0].maskSettings.enabled);
    }
    void invalidValuesAndStreamsCannotReplaceSettings() {
        FilterSettings s;
        s.gaussian = 17;
        QVERIFY(!s.valid());
        QImage image(4, 4, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QVERIFY(Filters::apply(image, s).isNull());
        Document d;
        d.create({4, 4}, true);
        QString e;
        QVERIFY(!d.adjustFilters(s, &e));
        QVERIFY(d.state.layers[0].filters.isIdentity());
        MaskSettings m;
        m.density = -1;
        QVERIFY(!m.valid());
        QVERIFY(!d.adjustMask(m, &e));
        s = {};
        s.gaussian = 3;
        s.noiseSeed = 0xffffffffu;
        QByteArray data;
        QDataStream out(&data, QIODevice::WriteOnly);
        s.write(out);
        FilterSettings decoded;
        QDataStream reader(data);
        QVERIFY(FilterSettings::read(reader, decoded));
        QVERIFY(decoded == s);
        data.chop(1);
        QDataStream truncated(data);
        FilterSettings previous = decoded;
        QVERIFY(!FilterSettings::read(truncated, decoded));
        QVERIFY(decoded == previous);
        d.state.layers[0].locked = true;
        s.gaussian = 2;
        QVERIFY(!d.adjustFilters(s));
    }
    void v5ProjectRoundTripAndAuthenticV4() {
        Document d;
        d.create({30, 20});
        pattern(d);
        QString e;
        QVERIFY(d.addMask(&e));
        FilterSettings s;
        s.gaussian = 2;
        s.sharpenAmount = 80;
        s.sharpenRadius = 3;
        s.highPass = 1;
        s.noise = 8;
        s.noiseSeed = 5678;
        s.mosaic = 2;
        s.monochromeNoise = false;
        MaskSettings m;
        m.density = 40;
        m.feather = 2;
        d.beginEdit();
        d.adjustFilters(s);
        d.adjustMask(m);
        d.commit("Effects");
        QTemporaryDir temp;
        QString file = temp.filePath("effects.pdeck");
        QVERIFY(d.save(file, &e));
        Document loaded;
        QVERIFY(loaded.load(file, &e));
        QVERIFY(loaded.state.layers[0].filters == s);
        QVERIFY(loaded.state.layers[0].maskSettings == m);
        QCOMPARE(loaded.composite(), d.composite());
        QString legacy = QFINDTESTDATA("fixtures/v4-demo.pdeck");
        QVERIFY(!legacy.isEmpty());
        QVERIFY2(loaded.load(legacy, &e), qPrintable(e));
        for (const auto &l : loaded.state.layers) {
            QVERIFY(l.filters.isIdentity());
            QVERIFY(l.maskSettings == MaskSettings{});
        }
    }
    void filterDialogPreviewOffCancelAcceptAndUndo() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({30, 20});
        pattern(*d);
        w.addDocument(d);
        QImage before = d->composite();
        int history = d->historyLabels().size();
        bool preview = false;
        auto *action = w.findChild<QAction *>("photoFilters");
        QVERIFY(action);
        QVERIFY(modal(action, [&](QDialog *dialog) {
            auto *filter = qobject_cast<FilterDialog *>(dialog);
            if (!filter) {
                dialog->reject();
                return;
            }
            filter->findChild<QSpinBox *>("gaussian")->setValue(3);
            QTRY_COMPARE_WITH_TIMEOUT(d->state.layers[0].filters.gaussian, 3, 2000);
            preview = d->composite() != before;
            filter->findChild<QCheckBox *>("liveFilterPreview")->setChecked(false);
            QCOMPARE(d->composite(), before);
            filter->reject();
        }));
        QVERIFY(preview);
        QCOMPARE(d->composite(), before);
        QCOMPARE(d->historyLabels().size(), history);
        QVERIFY(modal(action, [&](QDialog *dialog) {
            dialog->findChild<QSpinBox *>("gaussian")->setValue(2);
            QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok),
                              Qt::LeftButton);
        }));
        QVERIFY(!d->state.layers[0].filters.isIdentity());
        QCOMPARE(d->historyLabels().size(), history + 1);
        d->undo();
        QCOMPARE(d->composite(), before);
        w.close();
    }
    void maskDialogCancelAcceptAndMenuAvailability() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({20, 20}, true);
        w.addDocument(d);
        QString e;
        auto *action = w.findChild<QAction *>("maskSettings");
        QVERIFY(!action->isEnabled());
        QVERIFY(d->addMask(&e));
        d->beginEdit();
        d->state.layers[0].mask.fill(Qt::black);
        d->commit("Black mask");
        QTRY_VERIFY(action->isEnabled());
        QImage before = d->composite();
        QVERIFY(modal(action, [&](QDialog *dialog) {
            dialog->findChild<QSpinBox *>("maskDensity")->setValue(20);
            QVERIFY(d->composite() != before);
            dialog->reject();
        }));
        QCOMPARE(d->composite(), before);
        QVERIFY(modal(action, [&](QDialog *dialog) {
            dialog->findChild<QCheckBox *>("maskEnabled")->setChecked(false);
            dialog->accept();
        }));
        QCOMPARE(qAlpha(d->composite().pixel(5, 5)), 255);
        d->undo();
        QCOMPARE(d->composite(), before);
        d->undo();
        d->undo();
        d->markSaved();
        w.close();
    }
    void representativeFilterCost() {
        QImage image(1400, 1000, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(80, 140, 200));
        QElapsedTimer timer;
        timer.start();
        QImage blurred = Filters::gaussianBlur(image, 4);
        QVERIFY(!blurred.isNull());
        qInfo() << "1400x1000 exact Gaussian radius 4 ms:" << timer.elapsed();
    }
};
QTEST_MAIN(EffectTests)
#include "test_effects.moc"
