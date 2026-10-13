#include "blending.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace Blending {
QStringList names() {
    // Keep the first six IDs stable for legacy .pdeck files.
    return {"Normal",         "Çarp",           "Ekran",         "Kaplama",
            "Koyulaştır",     "Açıklaştır",     "Dağıt",         "Renk yakma",
            "Doğrusal yakma", "Daha koyu renk", "Renk soldurma", "Doğrusal soldurma",
            "Daha açık renk", "Yumuşak ışık",   "Sert ışık",     "Canlı ışık",
            "Doğrusal ışık",  "Pin ışığı",      "Sert karışım",  "Fark",
            "Dışlama",        "Çıkar",          "Böl",           "Ton",
            "Doygunluk",      "Renk",           "Parlaklık"};
}
QPainter::CompositionMode painterMode(int mode) {
    switch (mode) {
    case 1:
        return QPainter::CompositionMode_Multiply;
    case 2:
        return QPainter::CompositionMode_Screen;
    case 3:
        return QPainter::CompositionMode_Overlay;
    case 4:
        return QPainter::CompositionMode_Darken;
    case 5:
        return QPainter::CompositionMode_Lighten;
    case 7:
        return QPainter::CompositionMode_ColorBurn;
    case 10:
        return QPainter::CompositionMode_ColorDodge;
    case 13:
        return QPainter::CompositionMode_SoftLight;
    case 14:
        return QPainter::CompositionMode_HardLight;
    case 19:
        return QPainter::CompositionMode_Difference;
    case 20:
        return QPainter::CompositionMode_Exclusion;
    default:
        return QPainter::CompositionMode_SourceOver;
    }
}
namespace {
using RGB = std::array<double, 3>;
double lum(RGB c) {
    return .3 * c[0] + .59 * c[1] + .11 * c[2];
}
double sat(RGB c) {
    return *std::max_element(c.begin(), c.end()) - *std::min_element(c.begin(), c.end());
}
RGB setLum(RGB c, double value) {
    double change = value - lum(c);
    for (double &v : c)
        v += change;
    const double l = lum(c), n = *std::min_element(c.begin(), c.end()),
                 x = *std::max_element(c.begin(), c.end());
    if (n < 0)
        for (double &v : c)
            v = l + (v - l) * l / (l - n);
    if (x > 1)
        for (double &v : c)
            v = l + (v - l) * (1 - l) / (x - l);
    return c;
}
RGB setSat(RGB c, double value) {
    std::array<int, 3> order{0, 1, 2};
    std::sort(order.begin(), order.end(), [&](int a, int b) { return c[a] < c[b]; });
    int lo = order[0], mid = order[1], hi = order[2];
    if (c[hi] > c[lo]) {
        c[mid] = (c[mid] - c[lo]) * value / (c[hi] - c[lo]);
        c[hi] = value;
    } else
        c[mid] = c[hi] = 0;
    c[lo] = 0;
    return c;
}
double burn(double b, double s) {
    return b >= 1 ? 1 : s <= 0 ? 0 : 1 - std::min(1.0, (1 - b) / s);
}
double dodge(double b, double s) {
    return b <= 0 ? 0 : s >= 1 ? 1 : std::min(1.0, b / (1 - s));
}
RGB blend(RGB b, RGB s, int mode) {
    switch (mode) {
    case 9:
        return s[0] + s[1] + s[2] < b[0] + b[1] + b[2] ? s : b;
    case 12:
        return s[0] + s[1] + s[2] > b[0] + b[1] + b[2] ? s : b;
    case 23:
        return setLum(setSat(s, sat(b)), lum(b));
    case 24:
        return setLum(setSat(b, sat(s)), lum(b));
    case 25:
        return setLum(s, lum(b));
    case 26:
        return setLum(b, lum(s));
    }
    RGB result;
    for (int i = 0; i < 3; ++i) {
        double cb = b[i], cs = s[i], v = cs;
        switch (mode) {
        case 8:
            v = cb + cs - 1;
            break;
        case 11:
            v = cb + cs;
            break;
        case 15:
            v = cs < .5 ? burn(cb, 2 * cs) : dodge(cb, 2 * cs - 1);
            break;
        case 16:
            v = cb + 2 * cs - 1;
            break;
        case 17:
            v = cs < .5 ? std::min(cb, 2 * cs) : std::max(cb, 2 * cs - 1);
            break;
        case 18:
            v = (cs < .5 ? burn(cb, 2 * cs) : dodge(cb, 2 * cs - 1)) < .5 ? 0 : 1;
            break;
        case 21:
            v = cb - cs;
            break;
        case 22:
            v = cs <= 0 ? 1 : cb / cs;
            break;
        }
        result[i] = std::clamp(v, 0.0, 1.0);
    }
    return result;
}
quint32 noise(int x, int y) {
    quint32 v = quint32(x) * 0x9e3779b9u ^ quint32(y) * 0x85ebca6bu ^ 0x7f4a7c15u;
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}
} // namespace
void compose(QImage &destination, const QImage &source, QPoint offset, double opacity, int mode) {
    if (source.isNull() || destination.isNull() || !std::isfinite(opacity) || opacity <= 0)
        return;
    opacity = std::clamp(opacity, 0.0, 1.0);
    const bool native =
        mode <= 5 || mode == 7 || mode == 10 || mode == 13 || mode == 14 || mode == 19 || mode == 20;
    if (native || mode < 0 || mode >= Count) {
        QPainter p(&destination);
        p.setOpacity(opacity);
        p.setCompositionMode(painterMode(mode));
        p.drawImage(offset, source);
        return;
    }
    if (destination.format() != QImage::Format_ARGB32_Premultiplied)
        destination = destination.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage pixels = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QRect region = pixels.rect().translated(offset).intersected(destination.rect());
    for (int y = region.top(); y <= region.bottom(); ++y) {
        QRgb *dest = reinterpret_cast<QRgb *>(destination.scanLine(y));
        const QRgb *src = reinterpret_cast<const QRgb *>(pixels.constScanLine(y - offset.y()));
        for (int x = region.left(); x <= region.right(); ++x) {
            QRgb sp = qUnpremultiply(src[x - offset.x()]);
            double as = qAlpha(sp) / 255.0 * opacity;
            if (mode == 6)
                as = noise(x, y) / 4294967296.0 < as ? 1 : 0;
            if (as == 0)
                continue;
            QRgb bp = qUnpremultiply(dest[x]);
            double ab = qAlpha(bp) / 255.0;
            RGB cs{qRed(sp) / 255.0, qGreen(sp) / 255.0, qBlue(sp) / 255.0};
            RGB cb{qRed(bp) / 255.0, qGreen(bp) / 255.0, qBlue(bp) / 255.0};
            RGB blended = blend(cb, cs, mode);
            double alpha = as + ab * (1 - as);
            RGB c;
            for (int i = 0; i < 3; ++i)
                c[i] = as * (1 - ab) * cs[i] + as * ab * blended[i] + (1 - as) * ab * cb[i];
            const int a = qRound(alpha * 255);
            dest[x] = qRgba(qBound(0, qRound(c[0] * 255), a), qBound(0, qRound(c[1] * 255), a),
                            qBound(0, qRound(c[2] * 255), a), a);
        }
    }
}
} // namespace Blending
