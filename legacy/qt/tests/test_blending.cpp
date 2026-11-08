#include "blending.h"
#include "document.h"
#include <QTemporaryDir>
#include <QtTest>

class BlendTests : public QObject {
    Q_OBJECT
  private slots:
    void numericModes_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<QColor>("expected");
        QTest::newRow("dissolve-opaque") << 6 << QColor(100, 200, 50);
        QTest::newRow("burn") << 7 << QColor(115, 57, 0);
        QTest::newRow("linear-burn") << 8 << QColor(45, 45, 0);
        QTest::newRow("darker-color-tie") << 9 << QColor(200, 100, 50);
        QTest::newRow("dodge") << 10 << QColor(255, 255, 62);
        QTest::newRow("linear-dodge") << 11 << QColor(255, 255, 100);
        QTest::newRow("lighter-color-tie") << 12 << QColor(200, 100, 50);
        QTest::newRow("soft-light") << 13 << QColor(191, 134, 26);
        QTest::newRow("hard-light") << 14 << QColor(157, 188, 20);
        QTest::newRow("vivid-light") << 15 << QColor(185, 232, 0);
        QTest::newRow("linear-light") << 16 << QColor(145, 245, 0);
        QTest::newRow("pin-light") << 17 << QColor(200, 145, 50);
        QTest::newRow("hard-mix") << 18 << QColor(255, 255, 0);
        QTest::newRow("difference") << 19 << QColor(100, 100, 0);
        QTest::newRow("exclusion") << 20 << QColor(143, 143, 80);
        QTest::newRow("subtract") << 21 << QColor(100, 0, 0);
        QTest::newRow("divide") << 22 << QColor(255, 128, 255);
    }
    void numericModes() {
        QFETCH(int, mode);
        QFETCH(QColor, expected);
        QImage base(2, 2, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(200, 100, 50));
        QImage source(2, 2, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(100, 200, 50));
        Blending::compose(base, source, {}, 1, mode);
        QColor actual = base.pixelColor(0, 0);
        QVERIFY2(qAbs(actual.red() - expected.red()) <= 2, qPrintable(actual.name()));
        QVERIFY2(qAbs(actual.green() - expected.green()) <= 2, qPrintable(actual.name()));
        QVERIFY2(qAbs(actual.blue() - expected.blue()) <= 2, qPrintable(actual.name()));
        QCOMPARE(actual.alpha(), 255);
    }
    void componentModes_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<QColor>("expected");
        QTest::newRow("hue") << 23 << QColor(100, 100, 100);
        QTest::newRow("saturation") << 24 << QColor(100, 100, 100);
        QTest::newRow("color") << 25 << QColor(212, 52, 52);
        QTest::newRow("luminosity") << 26 << QColor(88, 88, 88);
    }
    void componentModes() {
        QFETCH(int, mode);
        QFETCH(QColor, expected);
        QImage base(2, 2, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(100, 100, 100));
        QImage source(2, 2, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(200, 40, 40));
        Blending::compose(base, source, {}, 1, mode);
        QCOMPARE(base.pixelColor(0, 0), expected);
    }
    void transparencyOpacityAndOffset_data() {
        QTest::addColumn<int>("mode");
        for (int i = 0; i < Blending::Count; ++i)
            if (i != 6)
                QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void transparencyOpacityAndOffset() {
        QFETCH(int, mode);
        QImage base(12, 12, QImage::Format_ARGB32_Premultiplied);
        base.fill(Qt::transparent);
        QImage source(3, 3, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(200, 120, 40, 128));
        Blending::compose(base, source, {5, 5}, .5, mode);
        QCOMPARE(qAlpha(base.pixel(0, 0)), 0);
        QVERIFY(qAbs(qAlpha(base.pixel(6, 6)) - 64) <= 1);
        QColor result = base.pixelColor(6, 6);
        QVERIFY(qAbs(result.red() - 200) <= 4);
        QVERIFY(qAbs(result.green() - 120) <= 4);
        QVERIFY(qAbs(result.blue() - 40) <= 4);
        QImage unchanged = base;
        Blending::compose(base, source, {5, 5}, 0, mode);
        QCOMPARE(base, unchanged);
    }
    void customAlphaCompositionMatchesIndependentArithmetic() {
        QImage base(1, 1, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(160, 80, 40, 128));
        QImage source(1, 1, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(100, 160, 60, 128));
        const QColor before = base.pixelColor(0, 0), top = source.pixelColor(0, 0);
        Blending::compose(base, source, {}, .5, 21);
        const double a = top.alphaF() * .5, b = before.alphaF();
        const double outputAlpha = a + b * (1 - a);
        auto reference = [&](double cb, double cs) {
            return (a * (1 - b) * cs + a * b * std::max(0.0, cb - cs) + (1 - a) * b * cb) / outputAlpha;
        };
        QColor output = base.pixelColor(0, 0);
        QVERIFY(qAbs(output.alphaF() - outputAlpha) < .01);
        QVERIFY(qAbs(output.redF() - reference(before.redF(), top.redF())) < .015);
        QVERIFY(qAbs(output.greenF() - reference(before.greenF(), top.greenF())) < .015);
    }
    void dissolveIsStableAndUsesAlphaCoverage() {
        QImage base(200, 200, QImage::Format_ARGB32_Premultiplied);
        base.fill(Qt::transparent);
        QImage source(base.size(), QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(240, 80, 40, 128));
        QImage other = base;
        Blending::compose(base, source, {}, .5, 6);
        Blending::compose(other, source, {}, .5, 6);
        QCOMPARE(base, other);
        int visible = 0;
        for (int y = 0; y < base.height(); ++y)
            for (int x = 0; x < base.width(); ++x) {
                int a = qAlpha(base.pixel(x, y));
                QVERIFY(a == 0 || a == 255);
                visible += a == 255;
            }
        QVERIFY(visible > 9000 && visible < 11000);
    }
    void projectPreservesEveryBlendAndLegacyIds() {
        QCOMPARE(Blending::names().size(), Blending::Count);
        QTemporaryDir temp;
        QString error, file = temp.filePath("blend.pdeck");
        Document d;
        QVERIFY(d.create({3, 3}, true));
        QVERIFY(d.addLayer("Top", &error));
        d.state.layers[1].pixels.fill(QColor(170, 90, 40, 160));
        for (int mode = 0; mode < Blending::Count; ++mode) {
            d.state.layers[1].blend = mode;
            QVERIFY2(d.save(file, &error), qPrintable(error));
            Document loaded;
            QVERIFY2(loaded.load(file, &error), qPrintable(error));
            QCOMPARE(loaded.state.layers[1].blend, mode);
            QCOMPARE(loaded.composite(), d.composite());
        }
    }
};
QTEST_MAIN(BlendTests)
#include "test_blending.moc"
