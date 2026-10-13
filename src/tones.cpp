#include "tones.h"
#include <QColor>
#include <QtMath>
#include <cmath>
#include <tuple>

bool ToneSettings::operator==(const ToneSettings &o) const {
    return std::tie(inputBlack, inputWhite, outputBlack, outputWhite, gamma, exposure, hue, vibrance,
                    redBalance, greenBalance, blueBalance, inverted, threshold, posterize, curves) ==
           std::tie(o.inputBlack, o.inputWhite, o.outputBlack, o.outputWhite, o.gamma, o.exposure, o.hue,
                    o.vibrance, o.redBalance, o.greenBalance, o.blueBalance, o.inverted, o.threshold,
                    o.posterize, o.curves);
}
bool ToneSettings::isIdentity() const {
    return *this == ToneSettings{};
}
bool ToneSettings::valid() const {
    if (inputBlack < 0 || inputWhite > 255 || inputBlack >= inputWhite || outputBlack < 0 ||
        outputWhite > 255 || outputBlack > outputWhite || !std::isfinite(gamma) || gamma < .1 ||
        gamma > 10 || !std::isfinite(exposure) || exposure < -10 || exposure > 10 || hue < -180 ||
        hue > 180 || vibrance < -100 || vibrance > 100 || redBalance < -100 || redBalance > 100 ||
        greenBalance < -100 || greenBalance > 100 || blueBalance < -100 || blueBalance > 100 ||
        threshold < -1 || threshold > 255 || posterize < 2 || posterize > 256)
        return false;
    for (const auto &curve : curves) {
        if (curve.size() < 2 || curve.size() > 64 || curve.first().x() != 0 || curve.last().x() != 255)
            return false;
        int last = -1;
        for (QPoint point : curve) {
            if (point.x() <= last || point.x() > 255 || point.y() < 0 || point.y() > 255)
                return false;
            last = point.x();
        }
    }
    return true;
}
void ToneSettings::write(QDataStream &s) const {
    s << qint32(inputBlack) << qint32(inputWhite) << qint32(outputBlack) << qint32(outputWhite) << gamma
      << exposure << qint32(hue) << qint32(vibrance) << qint32(redBalance) << qint32(greenBalance)
      << qint32(blueBalance) << inverted << qint32(threshold) << qint32(posterize);
    for (const auto &curve : curves) {
        s << quint32(curve.size());
        for (QPoint point : curve)
            s << point;
    }
}
bool ToneSettings::read(QDataStream &s, ToneSettings &result) {
    ToneSettings pending;
    s >> pending.inputBlack >> pending.inputWhite >> pending.outputBlack >> pending.outputWhite >>
        pending.gamma >> pending.exposure >> pending.hue >> pending.vibrance >> pending.redBalance >>
        pending.greenBalance >> pending.blueBalance >> pending.inverted >> pending.threshold >>
        pending.posterize;
    for (auto &curve : pending.curves) {
        quint32 count = 0;
        s >> count;
        if (s.status() != QDataStream::Ok || count < 2 || count > 64)
            return false;
        curve.clear();
        for (quint32 i = 0; i < count; ++i) {
            QPoint point;
            s >> point;
            curve.append(point);
        }
    }
    if (s.status() != QDataStream::Ok || !pending.valid())
        return false;
    result = pending;
    return true;
}
namespace {
double interpolate(const QVector<QPoint> &points, double x) {
    for (int i = 1; i < points.size(); ++i)
        if (x <= points[i].x()) {
            const QPoint a = points[i - 1], b = points[i];
            return a.y() + (b.y() - a.y()) * (x - a.x()) / (b.x() - a.x());
        }
    return points.last().y();
}
double expose(double value, double gain) {
    double linear = value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
    linear = qBound(0.0, linear * gain, 1.0);
    return linear <= .0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1 / 2.4) - .055;
}
} // namespace
ToneMapper::ToneMapper(const ToneSettings &value) : settings(value.valid() ? value : ToneSettings{}) {
    const int balance[3] = {settings.redBalance, settings.greenBalance, settings.blueBalance};
    for (int i = 0; i < 256; ++i) {
        double v = qBound(
            0.0, double(i - settings.inputBlack) / (settings.inputWhite - settings.inputBlack), 1.0);
        v = std::pow(v, 1 / settings.gamma);
        if (settings.exposure != 0)
            v = expose(v, std::pow(2.0, settings.exposure));
        v = settings.outputBlack + v * (settings.outputWhite - settings.outputBlack);
        v = interpolate(settings.curves[0], v);
        for (int channel = 0; channel < 3; ++channel)
            tables[channel][i] = qBound(
                0, qRound(interpolate(settings.curves[channel + 1], v) + balance[channel] * 2.55), 255);
    }
}
QRgb ToneMapper::map(QRgb pixel) const {
    int r = tables[0][qRed(pixel)], g = tables[1][qGreen(pixel)], b = tables[2][qBlue(pixel)];
    if (settings.hue || settings.vibrance) {
        QColor color(r, g, b);
        float h, s, v;
        color.getHsvF(&h, &s, &v);
        h = h < 0 ? 0 : h;
        h = std::fmod(h + settings.hue / 360.0 + 1.0, 1.0);
        double saturation = settings.vibrance >= 0 ? s + settings.vibrance / 100.0 * s * (1 - s)
                                                   : s * (1 + settings.vibrance / 100.0);
        color = QColor::fromHsvF(h, qBound(0.0, saturation, 1.0), v);
        r = color.red();
        g = color.green();
        b = color.blue();
    }
    if (settings.inverted) {
        r = 255 - r;
        g = 255 - g;
        b = 255 - b;
    }
    if (settings.posterize != 256) {
        auto reduce = [&](int value) {
            return qRound(qRound(value * (settings.posterize - 1) / 255.0) * 255.0 /
                          (settings.posterize - 1));
        };
        r = reduce(r);
        g = reduce(g);
        b = reduce(b);
    }
    if (settings.threshold >= 0)
        r = g = b = (.2126 * r + .7152 * g + .0722 * b >= settings.threshold ? 255 : 0);
    return qRgba(r, g, b, qAlpha(pixel));
}
