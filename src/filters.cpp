#include "filters.h"
#include <QVector>
#include <QtMath>
#include <cmath>
#include <tuple>

bool FilterSettings::valid() const {
    return gaussian >= 0 && gaussian <= 16 && sharpenAmount >= 0 && sharpenAmount <= 300 &&
           sharpenRadius >= 1 && sharpenRadius <= 16 && sharpenThreshold >= 0 &&
           sharpenThreshold <= 255 && highPass >= 0 && highPass <= 16 && noise >= 0 && noise <= 100 &&
           mosaic >= 1 && mosaic <= 128;
}
bool FilterSettings::isIdentity() const {
    return gaussian == 0 && sharpenAmount == 0 && highPass == 0 && noise == 0 && mosaic == 1;
}
bool FilterSettings::operator==(const FilterSettings &o) const {
    return std::tie(gaussian, sharpenAmount, sharpenRadius, sharpenThreshold, highPass, noise, mosaic,
                    monochromeNoise, noiseSeed) == std::tie(o.gaussian, o.sharpenAmount, o.sharpenRadius,
                                                            o.sharpenThreshold, o.highPass, o.noise,
                                                            o.mosaic, o.monochromeNoise, o.noiseSeed);
}
void FilterSettings::write(QDataStream &s) const {
    s << qint32(gaussian) << qint32(sharpenAmount) << qint32(sharpenRadius) << qint32(sharpenThreshold)
      << qint32(highPass) << qint32(noise) << qint32(mosaic) << monochromeNoise << noiseSeed;
}
bool FilterSettings::read(QDataStream &s, FilterSettings &result) {
    FilterSettings p;
    s >> p.gaussian >> p.sharpenAmount >> p.sharpenRadius >> p.sharpenThreshold >> p.highPass >>
        p.noise >> p.mosaic >> p.monochromeNoise >> p.noiseSeed;
    if (s.status() != QDataStream::Ok || !p.valid())
        return false;
    result = p;
    return true;
}
bool MaskSettings::valid() const {
    return density >= 0 && density <= 100 && feather >= 0 && feather <= 16;
}
bool MaskSettings::operator==(const MaskSettings &o) const {
    return enabled == o.enabled && density == o.density && feather == o.feather;
}
void MaskSettings::write(QDataStream &s) const {
    s << enabled << qint32(density) << qint32(feather);
}
bool MaskSettings::read(QDataStream &s, MaskSettings &result) {
    MaskSettings p;
    s >> p.enabled >> p.density >> p.feather;
    if (s.status() != QDataStream::Ok || !p.valid())
        return false;
    result = p;
    return true;
}
namespace {
bool bounded(const QImage &im) {
    return !im.isNull() && im.width() <= 16384 && im.height() <= 16384 &&
           qint64(im.width()) * im.height() <= 16 * 1024 * 1024;
}
quint32 hash(quint32 value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
int noiseValue(int x, int y, quint32 seed, int channel, int amount) {
    quint32 v = hash(quint32(x) * 0x9e3779b9u ^ quint32(y) * 0x85ebca6bu ^ seed ^
                     quint32(channel) * 0xc2b2ae35u);
    return qRound((double(v & 65535) / 32767.5 - 1) * amount * 2.55);
}
QImage mosaic(const QImage &source, int block) {
    if (block <= 1)
        return source;
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    if (result.isNull())
        return {};
    for (int top = 0; top < source.height(); top += block)
        for (int left = 0; left < source.width(); left += block) {
            const int bottom = qMin(top + block, source.height()),
                      right = qMin(left + block, source.width());
            qint64 r = 0, g = 0, b = 0, a = 0;
            int n = (bottom - top) * (right - left);
            for (int y = top; y < bottom; ++y) {
                const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
                for (int x = left; x < right; ++x) {
                    QRgb p = row[x];
                    r += qRed(p);
                    g += qGreen(p);
                    b += qBlue(p);
                    a += qAlpha(p);
                }
            }
            QRgb color = qRgba((r + n / 2) / n, (g + n / 2) / n, (b + n / 2) / n, (a + n / 2) / n);
            for (int y = top; y < bottom; ++y) {
                auto *row = reinterpret_cast<QRgb *>(result.scanLine(y));
                for (int x = left; x < right; ++x)
                    row[x] = color;
            }
        }
    return result;
}
} // namespace
namespace Filters {
QImage gaussianBlur(const QImage &source, int radius) {
    if (!bounded(source) || radius < 0 || radius > 16)
        return {};
    QImage input = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!radius)
        return input;
    QVector<double> kernel(2 * radius + 1);
    double total = 0, sigma = qMax(.5, radius / 2.0);
    for (int i = -radius; i <= radius; ++i) {
        double v = std::exp(-double(i * i) / (2 * sigma * sigma));
        kernel[i + radius] = v;
        total += v;
    }
    for (double &v : kernel)
        v /= total;
    QImage horizontal(input.size(), QImage::Format_ARGB32_Premultiplied),
        result(input.size(), QImage::Format_ARGB32_Premultiplied);
    if (horizontal.isNull() || result.isNull())
        return {};
    for (int y = 0; y < input.height(); ++y) {
        const auto *src = reinterpret_cast<const QRgb *>(input.constScanLine(y));
        auto *dest = reinterpret_cast<QRgb *>(horizontal.scanLine(y));
        for (int x = 0; x < input.width(); ++x) {
            double r = 0, g = 0, b = 0, a = 0;
            for (int k = -radius; k <= radius; ++k) {
                QRgb p = src[qBound(0, x + k, input.width() - 1)];
                double w = kernel[k + radius];
                r += qRed(p) * w;
                g += qGreen(p) * w;
                b += qBlue(p) * w;
                a += qAlpha(p) * w;
            }
            int alpha = qRound(a);
            dest[x] = qRgba(qBound(0, qRound(r), alpha), qBound(0, qRound(g), alpha),
                            qBound(0, qRound(b), alpha), alpha);
        }
    }
    for (int y = 0; y < input.height(); ++y) {
        auto *dest = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < input.width(); ++x) {
            double r = 0, g = 0, b = 0, a = 0;
            for (int k = -radius; k <= radius; ++k) {
                QRgb p = reinterpret_cast<const QRgb *>(
                    horizontal.constScanLine(qBound(0, y + k, input.height() - 1)))[x];
                double w = kernel[k + radius];
                r += qRed(p) * w;
                g += qGreen(p) * w;
                b += qBlue(p) * w;
                a += qAlpha(p) * w;
            }
            int alpha = qRound(a);
            dest[x] = qRgba(qBound(0, qRound(r), alpha), qBound(0, qRound(g), alpha),
                            qBound(0, qRound(b), alpha), alpha);
        }
    }
    return result;
}
QImage apply(const QImage &source, const FilterSettings &s) {
    if (!bounded(source) || !s.valid())
        return {};
    if (s.isIdentity())
        return source;
    QImage image = mosaic(source.convertToFormat(QImage::Format_ARGB32_Premultiplied), s.mosaic);
    if (image.isNull())
        return {};
    if (s.noise)
        for (int y = 0; y < image.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                QRgb p = qUnpremultiply(row[x]);
                int alpha = qAlpha(p);
                if (!alpha)
                    continue;
                int r = noiseValue(x, y, s.noiseSeed, 0, s.noise),
                    g = s.monochromeNoise ? r : noiseValue(x, y, s.noiseSeed, 1, s.noise),
                    b = s.monochromeNoise ? r : noiseValue(x, y, s.noiseSeed, 2, s.noise);
                row[x] = qPremultiply(qRgba(qBound(0, qRed(p) + r, 255), qBound(0, qGreen(p) + g, 255),
                                            qBound(0, qBlue(p) + b, 255), alpha));
            }
        }
    if (s.gaussian)
        image = gaussianBlur(image, s.gaussian);
    if (image.isNull())
        return {};
    if (s.highPass || s.sharpenAmount) {
        auto detail = [&](int radius, bool highPass) {
            QImage blurred = gaussianBlur(image, radius);
            if (blurred.isNull()) {
                image = {};
                return;
            }
            for (int y = 0; y < image.height(); ++y) {
                auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
                const auto *soft = reinterpret_cast<const QRgb *>(blurred.constScanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    QRgb p = qUnpremultiply(row[x]), q = qUnpremultiply(soft[x]);
                    int alpha = qAlpha(p);
                    if (!alpha) {
                        row[x] = 0;
                        continue;
                    }
                    int delta = qMax(qAbs(qRed(p) - qRed(q)),
                                     qMax(qAbs(qGreen(p) - qGreen(q)), qAbs(qBlue(p) - qBlue(q))));
                    if (!highPass && delta < s.sharpenThreshold)
                        continue;
                    auto channel = [&](int v, int blur) {
                        return qBound(
                            0,
                            qRound(highPass ? 128 + v - blur : v + (v - blur) * s.sharpenAmount / 100.0),
                            255);
                    };
                    row[x] = qPremultiply(qRgba(channel(qRed(p), qRed(q)), channel(qGreen(p), qGreen(q)),
                                                channel(qBlue(p), qBlue(q)), alpha));
                }
            }
        };
        if (s.highPass)
            detail(s.highPass, true);
        if (!image.isNull() && s.sharpenAmount)
            detail(s.sharpenRadius, false);
    }
    return image;
}
QImage maskCoverage(const QImage &mask, const MaskSettings &s) {
    if (mask.isNull() || !s.valid() || !s.enabled || s.density == 0)
        return {};
    if (!bounded(mask))
        return {};
    const QImage input = mask.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!s.feather) {
        QImage result(mask.size(), QImage::Format_Grayscale8);
        if (result.isNull())
            return {};
        for (int y = 0; y < mask.height(); ++y) {
            uchar *dest = result.scanLine(y);
            const auto *src = reinterpret_cast<const QRgb *>(input.constScanLine(y));
            for (int x = 0; x < mask.width(); ++x)
                dest[x] = 255 - ((255 - qRed(src[x])) * s.density + 50) / 100;
        }
        return result;
    }
    QImage gray(mask.size(), QImage::Format_ARGB32_Premultiplied);
    if (gray.isNull())
        return {};
    for (int y = 0; y < mask.height(); ++y) {
        auto *dest = reinterpret_cast<QRgb *>(gray.scanLine(y));
        const auto *src = reinterpret_cast<const QRgb *>(input.constScanLine(y));
        for (int x = 0; x < mask.width(); ++x) {
            int v = qRed(src[x]);
            dest[x] = qRgb(v, v, v);
        }
    }
    if (s.feather)
        gray = gaussianBlur(gray, s.feather);
    QImage result(mask.size(), QImage::Format_Grayscale8);
    if (result.isNull() || gray.isNull())
        return {};
    for (int y = 0; y < mask.height(); ++y) {
        uchar *dest = result.scanLine(y);
        const auto *src = reinterpret_cast<const QRgb *>(gray.constScanLine(y));
        for (int x = 0; x < mask.width(); ++x)
            dest[x] = qBound(0, 255 - ((255 - qRed(src[x])) * s.density + 50) / 100, 255);
    }
    return result;
}
} // namespace Filters
