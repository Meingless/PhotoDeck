#include "selection.h"
#include <QVector>
#include <algorithm>
#include <deque>

namespace {
bool validCanvas(QSize size) {
    return size.width() > 0 && size.height() > 0 && size.width() <= 16384 && size.height() <= 16384 &&
           qint64(size.width()) * size.height() <= 16 * 1024 * 1024;
}
} // namespace
int Selection::valueAt(QPoint point) const {
    if (isEmpty())
        return 255;
    if (!coverage.isNull()) {
        if (!coverage.rect().contains(point))
            return 0;
        return coverage.constScanLine(point.y())[point.x()];
    }
    return rectangle.contains(point) ? 255 : 0;
}
Selection Selection::fromMask(QImage image) {
    Selection result;
    if (image.isNull() || !validCanvas(image.size()))
        return result;
    result.coverage = image.convertToFormat(QImage::Format_Grayscale8);
    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const uchar *row = result.coverage.constScanLine(y);
        for (int x = 0; x < image.width(); ++x)
            if (row[x]) {
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = y;
            }
    }
    // Empty active selections retain a valid bound but their coverage is zero everywhere.
    result.rectangle = right >= 0 ? QRect(QPoint(left, top), QPoint(right, bottom)) : image.rect();
    result.nonzero = right >= 0;
    return result;
}
bool Selection::equivalent(const Selection &other, QSize canvas) const {
    if (isEmpty() || other.isEmpty())
        return isEmpty() == other.isEmpty();
    if (coverage.isNull() && other.coverage.isNull()) {
        if (!validCanvas(canvas))
            return true; // Both mask(canvas) results would be null for an invalid canvas.
        const QRect canvasRect(QPoint(), canvas);
        const QRect a = rectangle.intersected(canvasRect), b = other.rectangle.intersected(canvasRect);
        return (a.isEmpty() && b.isEmpty()) || a == b;
    }
    if (rectangle == other.rectangle && coverage == other.coverage)
        return true;
    return mask(canvas) == other.mask(canvas);
}
Selection Selection::clipped(QSize canvas) const {
    if (isEmpty() || !validCanvas(canvas))
        return *this;
    if (!coverage.isNull())
        return coverage.size() == canvas ? *this : translated({}, canvas);
    QRect clipped = rectangle.intersected(QRect(QPoint(), canvas));
    if (!clipped.isEmpty())
        return Selection(clipped);
    QImage zero(canvas, QImage::Format_Grayscale8);
    zero.fill(0);
    return fromMask(zero);
}
Selection Selection::translated(QPoint delta, QSize target) const {
    if (isEmpty() || !validCanvas(target))
        return *this;
    if (coverage.isNull())
        return Selection(rectangle.translated(delta)).clipped(target);
    QImage image(target, QImage::Format_Grayscale8);
    image.fill(0);
    QRect region = coverage.rect().translated(delta).intersected(image.rect());
    for (int y = region.top(); y <= region.bottom(); ++y)
        std::copy_n(coverage.constScanLine(y - delta.y()) + region.left() - delta.x(),
                    region.width(), image.scanLine(y) + region.left());
    return fromMask(image);
}
Selection Selection::cropped(QRect region, QSize source) const {
    if (isEmpty() || !validCanvas(region.size()))
        return *this;
    if (coverage.isNull())
        return translated(-region.topLeft(), region.size());
    return fromMask(mask(source).copy(region));
}
Selection Selection::resized(QSize source, QSize target) const {
    if (isEmpty() || !validCanvas(source) || !validCanvas(target))
        return *this;
    if (source == target)
        return *this;
    return fromMask(mask(source).scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}
Selection Selection::rotated(bool clockwise, QSize source) const {
    if (isEmpty() || !validCanvas(source))
        return *this;
    if (coverage.isNull()) {
        const QRect &r = rectangle;
        return Selection(clockwise
                             ? QRect(source.height() - r.y() - r.height(), r.x(), r.height(), r.width())
                             : QRect(r.y(), source.width() - r.x() - r.width(), r.height(), r.width()))
            .clipped(source.transposed());
    }
    QTransform transform;
    transform.rotate(clockwise ? 90 : -90);
    return fromMask(mask(source).transformed(transform));
}
Selection Selection::flipped(bool horizontal, QSize canvas) const {
    if (isEmpty() || !validCanvas(canvas))
        return *this;
    if (coverage.isNull()) {
        QRect r = rectangle;
        if (horizontal)
            r.moveLeft(canvas.width() - rectangle.x() - rectangle.width());
        else
            r.moveTop(canvas.height() - rectangle.y() - rectangle.height());
        return Selection(r).clipped(canvas);
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return fromMask(mask(canvas).flipped(horizontal ? Qt::Horizontal : Qt::Vertical));
#else
    return fromMask(mask(canvas).mirrored(horizontal, !horizontal));
#endif
}
Selection Selection::fromPath(const QPainterPath &path, QSize size) {
    if (!validCanvas(size))
        return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillPath(path, Qt::white);
    p.end();
    return fromMask(image);
}
QImage Selection::mask(QSize size) const {
    if (!validCanvas(size))
        return {};
    if (!coverage.isNull() && coverage.size() == size)
        return coverage;
    QImage image(size, QImage::Format_Grayscale8);
    for (int y = 0; y < size.height(); ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < size.width(); ++x)
            row[x] = valueAt({x, y});
    }
    return image;
}
Selection Selection::combined(const Selection &other, Mode mode, QSize size) const {
    if (mode == Replace || (isEmpty() && mode == Add))
        return other;
    if (!validCanvas(size))
        return *this;
    QImage image(size, QImage::Format_Grayscale8);
    for (int y = 0; y < size.height(); ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            int a = valueAt({x, y}), b = other.valueAt({x, y});
            row[x] = mode == Add        ? qMax(a, b)
                     : mode == Subtract ? (a * (255 - b) + 127) / 255
                                        : (a * b + 127) / 255;
        }
    }
    return fromMask(image);
}
Selection Selection::inverted(QSize size) const {
    QImage image = mask(size);
    if (image.isNull())
        return *this;
    for (int y = 0; y < image.height(); ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x)
            row[x] = 255 - row[x];
    }
    return fromMask(image);
}
Selection Selection::feathered(int radius, QSize size) const {
    if (radius <= 0 || radius > 128 || isEmpty())
        return *this;
    QImage source = mask(size);
    if (source.isNull())
        return *this;
    QImage horizontal(size, QImage::Format_Grayscale8), output(size, QImage::Format_Grayscale8);
    const int window = 2 * radius + 1;
    for (int y = 0; y < size.height(); ++y) {
        const uchar *row = source.constScanLine(y);
        uchar *dest = horizontal.scanLine(y);
        int sum = 0;
        for (int x = 0; x <= radius && x < size.width(); ++x)
            sum += row[x];
        for (int x = 0; x < size.width(); ++x) {
            dest[x] = (sum + window / 2) / window;
            if (x - radius >= 0)
                sum -= row[x - radius];
            if (x + radius + 1 < size.width())
                sum += row[x + radius + 1];
        }
    }
    for (int x = 0; x < size.width(); ++x) {
        int sum = 0;
        for (int y = 0; y <= radius && y < size.height(); ++y)
            sum += horizontal.constScanLine(y)[x];
        for (int y = 0; y < size.height(); ++y) {
            output.scanLine(y)[x] = (sum + window / 2) / window;
            if (y - radius >= 0)
                sum -= horizontal.constScanLine(y - radius)[x];
            if (y + radius + 1 < size.height())
                sum += horizontal.constScanLine(y + radius + 1)[x];
        }
    }
    return fromMask(output);
}
Selection Selection::colorRange(const QImage &source, QPoint seed, int tolerance, bool contiguous) {
    if (!validCanvas(source.size()) || !source.rect().contains(seed) || tolerance < 0 || tolerance > 255)
        return {};
    QImage pixels = source.convertToFormat(QImage::Format_ARGB32);
    const QRgb sample = pixels.pixel(seed);
    auto matches = [&](int x, int y) {
        QRgb p = reinterpret_cast<const QRgb *>(pixels.constScanLine(y))[x];
        int delta = qAbs(qAlpha(p) - qAlpha(sample));
        if (qAlpha(p) || qAlpha(sample))
            delta = qMax(delta,
                         qMax(qAbs(qRed(p) - qRed(sample)),
                              qMax(qAbs(qGreen(p) - qGreen(sample)), qAbs(qBlue(p) - qBlue(sample)))));
        return delta <= tolerance;
    };
    QImage image(pixels.size(), QImage::Format_Grayscale8);
    image.fill(0);
    if (!contiguous) {
        for (int y = 0; y < image.height(); ++y) {
            uchar *row = image.scanLine(y);
            for (int x = 0; x < image.width(); ++x)
                row[x] = matches(x, y) ? 255 : 0;
        }
    } else {
        QVector<QPoint> pending{seed};
        while (!pending.isEmpty()) {
            QPoint point = pending.takeLast();
            uchar *row = image.scanLine(point.y());
            if (row[point.x()] || !matches(point.x(), point.y()))
                continue;
            int left = point.x(), right = point.x();
            while (left > 0 && !row[left - 1] && matches(left - 1, point.y()))
                --left;
            while (right + 1 < image.width() && !row[right + 1] && matches(right + 1, point.y()))
                ++right;
            std::fill(row + left, row + right + 1, 255);
            for (int y : {point.y() - 1, point.y() + 1}) {
                if (y < 0 || y >= image.height())
                    continue;
                bool span = false;
                for (int x = left; x <= right; ++x) {
                    bool available = !image.constScanLine(y)[x] && matches(x, y);
                    if (available && !span)
                        pending.append({x, y});
                    span = available;
                }
            }
        }
    }
    return fromMask(image);
}
Selection Selection::fromAlpha(const QImage &image, QPoint offset, QSize canvas) {
    if (image.isNull() || !validCanvas(canvas))
        return {};
    QImage mask(canvas, QImage::Format_Grayscale8);
    mask.fill(0);
    QRect region = image.rect().translated(offset).intersected(mask.rect());
    for (int y = region.top(); y <= region.bottom(); ++y) {
        uchar *row = mask.scanLine(y);
        for (int x = region.left(); x <= region.right(); ++x)
            row[x] = qAlpha(image.pixel(x - offset.x(), y - offset.y()));
    }
    return fromMask(mask);
}
Selection Selection::grown(int amount, QSize size) const {
    if (!amount || amount < -128 || amount > 128 || isEmpty())
        return *this;
    QImage source = mask(size);
    if (source.isNull())
        return *this;
    const int radius = qAbs(amount);
    QImage horizontal(size, QImage::Format_Grayscale8), output(size, QImage::Format_Grayscale8);
    // A separable square max/min filter; its cost is linear in the canvas pixel count.
    auto filter = [=](int length, const std::function<int(int)> &get,
                      const std::function<void(int, int)> &put) {
        std::deque<std::pair<int, int>> window;
        for (int i = -radius; i < length + radius; ++i) {
            int v = i >= 0 && i < length ? get(i) : 0;
            while (!window.empty() &&
                   (amount > 0 ? window.back().second <= v : window.back().second >= v))
                window.pop_back();
            window.emplace_back(i, v);
            while (window.front().first < i - 2 * radius)
                window.pop_front();
            int at = i - radius;
            if (at >= 0 && at < length)
                put(at, window.front().second);
        }
    };
    for (int y = 0; y < size.height(); ++y)
        filter(
            size.width(), [&](int x) { return source.constScanLine(y)[x]; },
            [&](int x, int v) { horizontal.scanLine(y)[x] = v; });
    for (int x = 0; x < size.width(); ++x)
        filter(
            size.height(), [&](int y) { return horizontal.constScanLine(y)[x]; },
            [&](int y, int v) { output.scanLine(y)[x] = v; });
    return fromMask(output);
}
QImage Selection::boundaryImage() const {
    if (coverage.isNull())
        return {};
    if (!boundary.isNull())
        return boundary;
    boundary = QImage(rectangle.size(), QImage::Format_ARGB32_Premultiplied);
    boundary.fill(Qt::transparent);
    for (int y = rectangle.top(); y <= rectangle.bottom(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(boundary.scanLine(y - rectangle.top()));
        for (int x = rectangle.left(); x <= rectangle.right(); ++x) {
            if (valueAt({x, y}) < 128)
                continue;
            if (valueAt({x - 1, y}) < 128 || valueAt({x + 1, y}) < 128 || valueAt({x, y - 1}) < 128 ||
                valueAt({x, y + 1}) < 128)
                row[x - rectangle.left()] = ((x + y) / 4) % 2 ? qRgb(255, 255, 255) : qRgb(0, 0, 0);
        }
    }
    return boundary;
}
QRgb Selection::mixPixel(QRgb before, QRgb after, QPoint point) const {
    int weight = valueAt(point);
    if (weight == 0)
        return before;
    if (weight == 255)
        return after;
    auto mix = [weight](int a, int b) { return (a * (255 - weight) + b * weight + 127) / 255; };
    return qRgba(mix(qRed(before), qRed(after)), mix(qGreen(before), qGreen(after)),
                 mix(qBlue(before), qBlue(after)), mix(qAlpha(before), qAlpha(after)));
}
bool Selection::paint(QImage &image, QPoint offset, const std::function<void(QPainter &)> &op,
                      QRect affected) const {
    if (!isEmpty() && !hasPixels())
        return false;
    QRect region = image.rect();
    if (!affected.isEmpty())
        region = region.intersected(affected);
    if (!isEmpty())
        region = region.intersected(rectangle.translated(-offset));
    if (region.isEmpty())
        return false;
    if (isEmpty() || coverage.isNull()) {
        QPainter p(&image);
        p.setClipRect(region);
        op(p);
        return true;
    }
    QImage changed = image.copy(region);
    {
        QPainter p(&changed);
        p.translate(-region.topLeft());
        op(p);
    }
    bool edited = false;
    for (int y = region.top(); y <= region.bottom(); ++y) {
        QRgb *dest = reinterpret_cast<QRgb *>(image.scanLine(y));
        const QRgb *src = reinterpret_cast<const QRgb *>(changed.constScanLine(y - region.top()));
        for (int x = region.left(); x <= region.right(); ++x) {
            QRgb mixed = mixPixel(dest[x], src[x - region.left()], QPoint(x, y) + offset);
            edited |= mixed != dest[x];
            dest[x] = mixed;
        }
    }
    return edited;
}
QImage Selection::copyFrom(const QImage &image) const {
    if (isEmpty())
        return image;
    if (!hasPixels())
        return {};
    QRect region = rectangle.intersected(image.rect());
    if (region.isEmpty())
        return {};
    QImage result = image.copy(region).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < result.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x)
            row[x] = mixPixel(0, row[x], QPoint(x, y) + region.topLeft());
    }
    return result;
}
