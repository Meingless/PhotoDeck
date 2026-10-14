#include "document.h"
#include "blending.h"
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGlyphRun>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QSet>
#include <QTextLayout>
#include <QtMath>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace {
qint64 bytes(const State &s) {
    qint64 n = 0;
    for (const auto &l : s.layers)
        n += l.pixels.sizeInBytes() + l.mask.sizeInBytes();
    return n;
}
bool sameLayer(const Layer &a, const Layer &b) {
    return a.name == b.name && a.pixels == b.pixels && a.mask == b.mask && a.offset == b.offset &&
           a.visible == b.visible && a.locked == b.locked && a.opacity == b.opacity &&
           a.blend == b.blend && a.brightness == b.brightness && a.contrast == b.contrast &&
           a.saturation == b.saturation && a.blur == b.blur && a.text == b.text && a.font == b.font &&
           a.color == b.color && a.tones == b.tones && a.geometry == b.geometry &&
           a.clipped == b.clipped && a.filters == b.filters && a.maskSettings == b.maskSettings;
}
bool sameContent(const State &a, const State &b) {
    if (a.size != b.size || a.layers.size() != b.layers.size())
        return false;
    for (int i = 0; i < a.layers.size(); ++i)
        if (!sameLayer(a.layers[i], b.layers[i]))
            return false;
    return true;
}
bool validState(const State &s, QString *error) {
    auto fail = [&] {
        if (error)
            *error = "Belge verileri güvenli boyut veya konum sınırını aşıyor.";
        return false;
    };
    if (!Document::validSize(s.size) || s.layers.isEmpty() || s.layers.size() > 64 || s.active < 0 ||
        s.active >= s.layers.size() || bytes(s) > Document::MaxLayerBytes || s.layers.first().clipped)
        return fail();
    qint64 renderBytes = 0;
    for (const auto &l : s.layers) {
        if (!Document::validSize(l.pixels.size()) ||
            l.pixels.format() != QImage::Format_ARGB32_Premultiplied ||
            (!l.mask.isNull() && l.mask.size() != l.pixels.size()) || !Document::validOffset(l.offset) ||
            (!l.mask.isNull() && l.mask.format() != QImage::Format_ARGB32_Premultiplied) ||
            !std::isfinite(l.opacity) || l.opacity < 0 || l.opacity > 1 || l.blend < 0 ||
            l.blend >= Blending::Count || qAbs(qint64(l.brightness)) > 100 ||
            qAbs(qint64(l.contrast)) > 100 || qAbs(qint64(l.saturation)) > 100 || l.blur < 0 ||
            l.blur > 20 || l.name.size() > 16384 || l.text.size() > 16384 ||
            l.font.toString().size() > 16384 || !l.tones.valid() || !l.filters.valid() ||
            !l.maskSettings.valid() || !l.validGeometry())
            return fail();
        const QSize size = l.transformedBounds().size();
        renderBytes += qint64(size.width()) * size.height() * 4;
        if (renderBytes > Document::MaxLayerBytes)
            return fail();
    }
    return true;
}
bool offsetError(QString *error) {
    if (error)
        *error = "Dönüşüm katman konumu sınırını aşıyor. Katmanı tuvale yaklaştırın.";
    return false;
}
bool normalizeGeometry(State &state, QString *error) {
    qint64 required = 0;
    for (const auto &l : state.layers) {
        if (!l.validGeometry())
            return offsetError(error);
        required += l.geometry.isIdentity()
                        ? l.pixels.sizeInBytes() + l.mask.sizeInBytes()
                        : qint64(l.transformedBounds().width()) * l.transformedBounds().height() * 4;
    }
    if (required > Document::MaxLayerBytes) {
        if (error)
            *error = "Dönüşümün rasterleştirilmesi 256 MiB katman sınırını aşıyor.";
        return false;
    }
    for (auto &l : state.layers)
        if (!l.geometry.isIdentity())
            l = l.rasterized();
    return true;
}
void renderLayers(QImage &output, QPoint origin, const QVector<Layer> &layers) {
    for (int i = 0; i < layers.size();) {
        const Layer &base = layers[i];
        int end = i + 1;
        while (end < layers.size() && layers[end].clipped)
            ++end;
        if (base.visible) {
            QImage group = base.rendered();
            const QPoint at = base.renderedOffset();
            for (int child = i + 1; child < end; ++child)
                if (layers[child].visible)
                    Blending::clipCompose(group, layers[child].rendered(),
                                          layers[child].renderedOffset() - at, layers[child].opacity,
                                          layers[child].blend, at);
            Blending::compose(output, group, at - origin, base.opacity, base.blend, origin);
        }
        i = end;
    }
}
void clearCaches(State &s) {
    for (auto &l : s.layers)
        l.cache = QImage();
}
QImage textImage(const QString &text, const QFont &font, const QColor &color) {
    // Shape each paragraph with Qt; include glyph bearings and fallback fonts in the pixel bounds.
    QFontMetrics metrics(font);
    std::vector<std::pair<std::unique_ptr<QTextLayout>, qreal>> lines;
    QRectF bounds;
    qreal y = 0, width = 0;
    for (const auto &paragraph : text.split('\n')) {
        auto layout = std::make_unique<QTextLayout>(paragraph, font);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        option.setTabStopDistance(qMax(1, metrics.horizontalAdvance(' ')) * 8);
        layout->setTextOption(option);
        layout->beginLayout();
        QTextLine line = layout->createLine();
        if (line.isValid()) {
            line.setLineWidth(16384);
            line.setPosition({0, 0});
            width = qMax(width, line.naturalTextWidth());
        }
        layout->endLayout();
        for (const auto &run : layout->glyphRuns()) {
            const auto indexes = run.glyphIndexes();
            const auto positions = run.positions();
            for (int i = 0; i < indexes.size(); ++i) {
                QRectF ink =
                    run.rawFont().boundingRect(indexes[i]).translated(positions[i] + QPointF(0, y));
                if (!ink.isEmpty())
                    bounds = bounds.isNull() ? ink : bounds.united(ink);
            }
        }
        qreal top = y;
        y += line.isValid() ? line.height() : metrics.height();
        lines.emplace_back(std::move(layout), top);
    }
    bounds = bounds.united(QRectF(0, 0, qMax(1.0, width), qMax(1.0, y)));
    QRect pixels = bounds.toAlignedRect();
    QSize size(pixels.width() + 12, pixels.height() + 12);
    if (!Document::validSize(size))
        return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(color);
    for (const auto &entry : lines)
        entry.first->draw(&painter, {qreal(6 - pixels.left()), 6 - pixels.top() + entry.second});
    return image;
}

QImage boxBlur(const QImage &source, int radius) {
    if (!radius)
        return source;
    QImage horizontal(source.size(), QImage::Format_ARGB32_Premultiplied);
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    const int window = 2 * radius + 1, w = source.width(), h = source.height();
    for (int y = 0; y < h; ++y) {
        const auto *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        auto *out = reinterpret_cast<QRgb *>(horizontal.scanLine(y));
        qint64 r = 0, g = 0, b = 0, a = 0;
        auto add = [&](QRgb v, int sign) {
            r += qRed(v) * sign;
            g += qGreen(v) * sign;
            b += qBlue(v) * sign;
            a += qAlpha(v) * sign;
        };
        for (int x = -radius; x <= radius; ++x)
            add(in[qBound(0, x, w - 1)], 1);
        for (int x = 0; x < w; ++x) {
            out[x] = qRgba(r / window, g / window, b / window, a / window);
            add(in[qBound(0, x - radius, w - 1)], -1);
            add(in[qBound(0, x + radius + 1, w - 1)], 1);
        }
    }
    for (int x = 0; x < w; ++x) {
        qint64 r = 0, g = 0, b = 0, a = 0;
        auto add = [&](int y, int sign) {
            QRgb v = reinterpret_cast<const QRgb *>(horizontal.constScanLine(qBound(0, y, h - 1)))[x];
            r += qRed(v) * sign;
            g += qGreen(v) * sign;
            b += qBlue(v) * sign;
            a += qAlpha(v) * sign;
        };
        for (int y = -radius; y <= radius; ++y)
            add(y, 1);
        for (int y = 0; y < h; ++y) {
            reinterpret_cast<QRgb *>(result.scanLine(y))[x] =
                qRgba(r / window, g / window, b / window, a / window);
            add(y - radius, -1);
            add(y + radius + 1, 1);
        }
    }
    return result;
}
QImage flippedImage(const QImage &source, bool horizontal) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return source.flipped(horizontal ? Qt::Horizontal : Qt::Vertical);
#else
    return source.mirrored(horizontal, !horizontal);
#endif
}
bool writeImage(QDataStream &stream, const QImage &image) {
    QByteArray data;
    if (!image.isNull()) {
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG"))
            return false;
    }
    stream << quint32(data.size());
    return data.isEmpty() || stream.writeRawData(data.constData(), data.size()) == data.size();
}
bool readImageRecord(QDataStream &stream, QImage &image, qint64 &remaining) {
    quint32 length = 0;
    stream >> length;
    if (stream.status() != QDataStream::Ok || length > 80 * 1024 * 1024 ||
        length > stream.device()->bytesAvailable())
        return false;
    if (!length) {
        image = {};
        return true;
    }
    QByteArray data(length, Qt::Uninitialized);
    if (stream.readRawData(data.data(), length) != int(length))
        return false;
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    QSize size = reader.size();
    if (!Document::validSize(size) || qint64(size.width()) * size.height() * 4 > remaining)
        return false;
    image = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return false;
    remaining -= image.sizeInBytes();
    return true;
}
} // namespace

QRect Layer::transformedBounds() const {
    if (geometry.isIdentity())
        return pixels.rect();
    QRectF bounds = geometry.mapRect(QRectF(QPointF(), QSizeF(pixels.size())));
    if (!std::isfinite(bounds.left()) || !std::isfinite(bounds.top()) ||
        !std::isfinite(bounds.right()) || !std::isfinite(bounds.bottom()) ||
        qAbs(bounds.left()) > Document::MaxCoordinate || qAbs(bounds.top()) > Document::MaxCoordinate ||
        qAbs(bounds.right()) > Document::MaxCoordinate ||
        qAbs(bounds.bottom()) > Document::MaxCoordinate)
        return {};
    return bounds.toAlignedRect();
}
bool Layer::validGeometry() const {
    const double values[] = {geometry.m11(), geometry.m12(), geometry.m13(),
                             geometry.m21(), geometry.m22(), geometry.m23(),
                             geometry.m31(), geometry.m32(), geometry.m33()};
    for (double v : values)
        if (!std::isfinite(v) || qAbs(v) > Document::MaxCoordinate)
            return false;
    if (!geometry.isInvertible())
        return false;
    // A projective horizon must not cross the source rectangle: it would produce an unbounded image.
    double minimum = std::numeric_limits<double>::infinity(), maximum = -minimum;
    for (QPointF p : {QPointF(0, 0), QPointF(pixels.width(), 0), QPointF(0, pixels.height()),
                      QPointF(pixels.width(), pixels.height())}) {
        double w = geometry.m13() * p.x() + geometry.m23() * p.y() + geometry.m33();
        minimum = qMin(minimum, w);
        maximum = qMax(maximum, w);
    }
    if (!(minimum > 1e-8 || maximum < -1e-8))
        return false;
    QRect bounds = transformedBounds();
    return Document::validSize(bounds.size()) && Document::validOffset(offset + bounds.topLeft());
}
QPoint Layer::renderedOffset() const {
    return offset + transformedBounds().topLeft();
}
Layer Layer::rasterized() const {
    Layer layer = *this;
    layer.pixels = rendered();
    layer.offset = renderedOffset();
    layer.geometry = {};
    layer.mask = {};
    layer.text.clear();
    layer.brightness = layer.contrast = layer.saturation = layer.blur = 0;
    layer.tones = {};
    layer.filters = {};
    layer.maskSettings = {};
    layer.cache = {};
    return layer;
}
QImage Layer::rendered() const {
    if (!validGeometry() || pixels.format() != QImage::Format_ARGB32_Premultiplied ||
        (!mask.isNull() && mask.format() != QImage::Format_ARGB32_Premultiplied))
        return {};
    const bool activeMask = !mask.isNull() && maskSettings.enabled && maskSettings.density > 0;
    if (!activeMask && filters.isIdentity() && brightness == 0 && contrast == 0 && saturation == 0 &&
        blur == 0 && tones.isIdentity() && geometry.isIdentity())
        return pixels;
    if (!cache.isNull() && cacheKey == pixels.cacheKey() && maskKey == mask.cacheKey() &&
        cacheB == brightness && cacheC == contrast && cacheS == saturation && cacheBlur == blur &&
        cacheTones == tones && cacheGeometry == geometry && cacheFilters == filters &&
        cacheMaskSettings == maskSettings)
        return cache;
    cache = boxBlur(pixels, blur).convertToFormat(QImage::Format_ARGB32);
    double factor = std::pow(2.0, contrast / 50.0), sat = 1.0 + saturation / 100.0;
    ToneMapper mapper(tones);
    for (int y = 0; y < cache.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(cache.scanLine(y));
        for (int x = 0; x < cache.width(); ++x) {
            QRgb v = row[x];
            double r = qRed(v), g = qGreen(v), b = qBlue(v), lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            r = (lum + (r - lum) * sat - 127.5) * factor + 127.5 + brightness * 2.55;
            g = (lum + (g - lum) * sat - 127.5) * factor + 127.5 + brightness * 2.55;
            b = (lum + (b - lum) * sat - 127.5) * factor + 127.5 + brightness * 2.55;
            int a = qAlpha(v);
            row[x] = mapper.map(qRgba(qBound(0, qRound(r), 255), qBound(0, qRound(g), 255),
                                      qBound(0, qRound(b), 255), a));
        }
    }
    if (!filters.isIdentity())
        cache = Filters::apply(cache.convertToFormat(QImage::Format_ARGB32_Premultiplied), filters)
                    .convertToFormat(QImage::Format_ARGB32);
    if (cache.isNull())
        return {};
    if (activeMask) {
        QImage coverage = Filters::maskCoverage(mask, maskSettings);
        if (coverage.isNull())
            return {};
        for (int y = 0; y < cache.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(cache.scanLine(y));
            const uchar *mr = coverage.constScanLine(y);
            for (int x = 0; x < cache.width(); ++x) {
                QRgb p = row[x];
                int weight = mr[x];
                row[x] = qRgba(qRed(p), qGreen(p), qBlue(p), qAlpha(p) * weight / 255);
            }
        }
    }
    cache = cache.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!geometry.isIdentity()) {
        const QRect bounds = transformedBounds();
        QImage result(bounds.size(), QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        QPainter p(&result);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setTransform(geometry * QTransform::fromTranslate(-bounds.left(), -bounds.top()));
        p.drawImage(QPoint(), cache);
        p.end();
        cache = result;
    }
    cacheKey = pixels.cacheKey();
    maskKey = mask.cacheKey();
    cacheB = brightness;
    cacheC = contrast;
    cacheS = saturation;
    cacheBlur = blur;
    cacheTones = tones;
    cacheGeometry = geometry;
    cacheFilters = filters;
    cacheMaskSettings = maskSettings;
    return cache;
}
Document::Document(QObject *parent) : QObject(parent) {}
bool Document::validSize(QSize s) {
    return s.width() > 0 && s.height() > 0 && s.width() <= 16384 && s.height() <= 16384 &&
           qint64(s.width()) * s.height() <= MaxPixels;
}
QPainter::CompositionMode Document::compositionMode(int b) {
    return Blending::painterMode(b);
}
QImage Document::composite() const {
    if (!validSize(state.size))
        return {};
    QImage out(state.size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    renderLayers(out, {}, state.layers);
    return out;
}
bool Document::validOffset(QPoint offset) {
    return qAbs(qint64(offset.x())) <= MaxCoordinate && qAbs(qint64(offset.y())) <= MaxCoordinate;
}
bool Document::allows(qint64 extra, QString *error, int addedLayers) const {
    if (bytes(state) + extra <= MaxLayerBytes && state.layers.size() + addedLayers <= 64)
        return true;
    if (error)
        *error = "Bu işlem katman sınırını aşar (64 katman / 256 MiB piksel verisi).";
    return false;
}
void Document::resetHistory(const QString &label) {
    clearCaches(state);
    history.clear();
    cursor = 0;
    history.append({state, label, nextId++});
    savedId = history[0].id;
    dirty = false;
    inEdit = false;
    before = {};
    selection = {};
    editingMask = false;
    emit updated();
}
bool Document::create(QSize size, bool white) {
    if (!validSize(size))
        return false;
    state = {size, {}, 0};
    Layer l;
    l.name = white ? "Arka plan" : "Katman 1";
    l.pixels = QImage(size, QImage::Format_ARGB32_Premultiplied);
    l.pixels.fill(white ? Qt::white : Qt::transparent);
    state.layers.append(l);
    path.clear();
    resetHistory("Yeni belge");
    return true;
}
QImage Document::readImage(const QString &file, QString *error) {
    QImageReader reader(file);
    reader.setAutoTransform(true);
    const QByteArray format = reader.format().toLower();
    if (format != "png" && format != "jpeg" && format != "jpg" && format != "webp" && format != "bmp" &&
        format != "gif" && format != "tif" && format != "tiff") {
        if (error)
            *error = "Desteklenen görüntüler: PNG, JPEG, WebP, BMP, GIF ve TIFF. Katmanlı projeler için "
                     ".pdeck "
                     "kullanın.";
        return {};
    }
    if (!validSize(reader.size())) {
        if (error)
            *error = "Görüntü boyutu desteklenmiyor. Sınır: 16 megapiksel ve kenar başına 16384 piksel.";
        return {};
    }
    QImage out = reader.read();
    if (out.isNull()) {
        if (error)
            *error = reader.errorString();
        return {};
    }
    if (!validSize(out.size())) {
        if (error)
            *error = "Görüntü boyutu sınırı aşıldı.";
        return {};
    }
    return Output::workingRgb(out);
}
bool Document::insertLayer(Layer layer, const QString &label, QString *error) {
    if (state.layers.isEmpty() || !validSize(layer.pixels.size()) ||
        (!layer.mask.isNull() && layer.mask.size() != layer.pixels.size())) {
        if (error)
            *error = "Belge veya katman görüntüsü geçersiz.";
        return false;
    }
    layer.pixels = Output::workingRgb(layer.pixels);
    if (!layer.mask.isNull())
        layer.mask = layer.mask.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    layer.cache = {};
    State pending = state;
    pending.layers.insert(pending.active + 1, layer);
    ++pending.active;
    if (!validState(pending, error))
        return false;
    beginEdit();
    state = pending;
    editingMask = false;
    commit(label);
    return true;
}
bool Document::importImage(const QString &file, bool asLayer, QString *error) {
    QImage image = readImage(file, error);
    if (image.isNull())
        return false;
    Layer layer;
    layer.name = QFileInfo(file).completeBaseName();
    layer.pixels = image;
    if (asLayer)
        return insertLayer(layer, "Görüntü katmanı eklendi", error);
    state = {image.size(), {layer}, 0};
    path.clear();
    title = QFileInfo(file).fileName();
    resetHistory("Görüntü açıldı");
    return true;
}
bool Document::addLayer(const QString &name, QString *error) {
    if (!allows(qint64(state.size.width()) * state.size.height() * 4, error))
        return false;
    Layer layer;
    layer.name = name;
    layer.pixels = QImage(state.size, QImage::Format_ARGB32_Premultiplied);
    layer.pixels.fill(Qt::transparent);
    return insertLayer(layer, "Yeni katman", error);
}
bool Document::duplicate(QString *error) {
    if (state.layers.isEmpty()) {
        if (error)
            *error = "Belge boş.";
        return false;
    }
    Layer layer = state.layers[state.active];
    layer.name += " kopya";
    return insertLayer(layer, "Katman çoğaltıldı", error);
}
void Document::deleteLayer() {
    if (state.layers.size() <= 1)
        return;
    beginEdit();
    if (!state.layers[state.active].clipped)
        for (int i = state.active + 1; i < state.layers.size() && state.layers[i].clipped; ++i)
            state.layers[i].clipped = false;
    state.layers.removeAt(state.active);
    state.layers.first().clipped = false;
    state.active = qMin(state.active, int(state.layers.size() - 1));
    editingMask = false;
    commit("Katman silindi");
}
void Document::reorder(int delta) {
    int to = state.active + delta;
    if (to < 0 || to >= state.layers.size())
        return;
    beginEdit();
    state.layers.swapItemsAt(state.active, to);
    state.active = to;
    state.layers.first().clipped = false;
    commit("Katman sırası");
}
bool Document::addMask(QString *error) {
    auto &l = state.layers[state.active];
    if (l.locked)
        return false;
    if (!l.mask.isNull()) {
        editingMask = !editingMask;
        emit updated();
        return true;
    }
    if (!allows(l.pixels.sizeInBytes(), error, 0))
        return false;
    beginEdit();
    l.maskSettings = {};
    l.mask = QImage(l.pixels.size(), QImage::Format_ARGB32_Premultiplied);
    l.mask.fill(Qt::white);
    if (!selection.isEmpty()) {
        l.mask.fill(Qt::black);
        if (l.geometry.isIdentity())
            selection.paint(l.mask, l.offset,
                            [&](QPainter &p) { p.fillRect(l.mask.rect(), Qt::white); });
        else {
            for (int y = 0; y < l.mask.height(); ++y) {
                auto *row = reinterpret_cast<QRgb *>(l.mask.scanLine(y));
                for (int x = 0; x < l.mask.width(); ++x) {
                    QPointF point = l.geometry.map(QPointF(x + .5, y + .5)) + l.offset;
                    int v = selection.valueAt({qFloor(point.x()), qFloor(point.y())});
                    row[x] = qRgb(v, v, v);
                }
            }
        }
    }
    editingMask = true;
    commit("Katman maskesi");
    return true;
}
void Document::removeMask() {
    auto &l = state.layers[state.active];
    if (l.locked || l.mask.isNull())
        return;
    beginEdit();
    l.mask = {};
    l.maskSettings = {};
    editingMask = false;
    commit("Maske kaldırıldı");
}
void Document::invertMask() {
    auto &l = state.layers[state.active];
    if (l.locked || l.mask.isNull())
        return;
    beginEdit();
    l.mask.invertPixels(QImage::InvertRgb);
    commit("Maske ters çevrildi");
}
bool Document::crop(QRect rect, QString *error) {
    rect = rect.intersected(QRect(QPoint(), state.size));
    if (!validSize(rect.size())) {
        if (error)
            *error = "Kırpmak için tuval üzerinde bir alan seçin.";
        return false;
    }
    State pending = state;
    pending.size = rect.size();
    for (auto &l : pending.layers)
        l.offset -= rect.topLeft();
    if (!validState(pending, error))
        return false;
    beginEdit();
    state = pending;
    selection = {};
    commit("Tuval kırpıldı");
    return true;
}
bool Document::resize(QSize size, QString *error) {
    State pending = state;
    if (!normalizeGeometry(pending, error))
        return false;

    if (!validSize(size)) {
        if (error)
            *error = "Geçersiz boyut (en fazla 16 megapiksel).";
        return false;
    }
    qreal sx = qreal(size.width()) / pending.size.width(),
          sy = qreal(size.height()) / pending.size.height();
    qint64 needed = 0;
    for (const auto &l : pending.layers) {
        QSize ls(qMax(1, qRound(l.pixels.width() * sx)), qMax(1, qRound(l.pixels.height() * sy)));
        if (!validSize(ls)) {
            if (error)
                *error = "Katman boyutu sınırı aşılıyor.";
            return false;
        }
        qreal x = l.offset.x() * sx, y = l.offset.y() * sy;
        if (!std::isfinite(x) || !std::isfinite(y) || qAbs(x) > MaxCoordinate || qAbs(y) > MaxCoordinate)
            return offsetError(error);
        needed += qint64(ls.width()) * ls.height() * 4 * (l.mask.isNull() ? 1 : 2);
    }
    if (needed > MaxLayerBytes) {
        if (error)
            *error = "Yeniden boyutlandırma 256 MiB katman sınırını aşar.";
        return false;
    }
    beginEdit();
    for (auto &l : pending.layers) {
        QSize ls(qMax(1, qRound(l.pixels.width() * sx)), qMax(1, qRound(l.pixels.height() * sy)));
        l.pixels = l.pixels.scaled(ls, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (!l.mask.isNull())
            l.mask = l.mask.scaled(ls, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        l.offset = {qRound(l.offset.x() * sx), qRound(l.offset.y() * sy)};
        l.text.clear();
    }
    pending.size = size;
    selection = {};
    state = pending;
    commit("Görüntü boyutu");
    return true;
}
bool Document::rotate(bool cw, QString *error) {
    State pending = state;
    if (!normalizeGeometry(pending, error))
        return false;

    int w = pending.size.width(), h = pending.size.height();
    for (const auto &l : pending.layers) {
        QPoint offset = cw ? QPoint(h - l.offset.y() - l.pixels.height(), l.offset.x())
                           : QPoint(l.offset.y(), w - l.offset.x() - l.pixels.width());
        if (!validOffset(offset))
            return offsetError(error);
    }
    beginEdit();
    QTransform t;
    t.rotate(cw ? 90 : -90);
    for (auto &l : pending.layers) {
        QSize old = l.pixels.size();
        l.pixels = l.pixels.transformed(t);
        if (!l.mask.isNull())
            l.mask = l.mask.transformed(t);
        l.offset = cw ? QPoint(h - l.offset.y() - old.height(), l.offset.x())
                      : QPoint(l.offset.y(), w - l.offset.x() - old.width());
        l.text.clear();
    }
    pending.size.transpose();
    selection = {};
    state = pending;
    commit("90° döndürme");
    return true;
}
bool Document::flip(bool h, QString *error) {
    State pending = state;
    if (!normalizeGeometry(pending, error))
        return false;

    for (const auto &l : pending.layers) {
        QPoint offset = l.offset;
        if (h)
            offset.setX(pending.size.width() - l.offset.x() - l.pixels.width());
        else
            offset.setY(pending.size.height() - l.offset.y() - l.pixels.height());
        if (!validOffset(offset))
            return offsetError(error);
    }
    beginEdit();
    for (auto &l : pending.layers) {
        l.pixels = flippedImage(l.pixels, h);
        if (!l.mask.isNull())
            l.mask = flippedImage(l.mask, h);
        if (h)
            l.offset.setX(pending.size.width() - l.offset.x() - l.pixels.width());
        else
            l.offset.setY(pending.size.height() - l.offset.y() - l.pixels.height());
        l.text.clear();
    }
    selection = {};
    state = pending;
    commit("Görüntü çevrildi");
    return true;
}
bool Document::setLayerTransform(const QTransform &transform, QString *error) {
    if (state.layers.isEmpty() || state.layers[state.active].locked) {
        if (error)
            *error = "Katman kilitli veya belge boş.";
        return false;
    }
    State pending = state;
    pending.layers[pending.active].geometry = transform;
    if (!validState(pending, error)) {
        if (error)
            *error =
                "Dönüşüm geçersiz, sonsuz veya 16 MP / 256 MiB görünüm / koordinat sınırını aşıyor.";
        return false;
    }
    state.layers[state.active].geometry = transform;
    changed();
    return true;
}
bool Document::rasterizeLayer(QString *error) {
    if (state.layers.isEmpty() || state.layers[state.active].locked) {
        if (error)
            *error = "Katman kilitli veya belge boş.";
        return false;
    }
    const auto &l = state.layers[state.active];
    if (!l.validGeometry())
        return offsetError(error);
    Layer raster = l.rasterized();
    if (raster.pixels.isNull() ||
        !allows(raster.pixels.sizeInBytes() - l.pixels.sizeInBytes() - l.mask.sizeInBytes(), error, 0))
        return false;
    beginEdit();
    state.layers[state.active] = raster;
    editingMask = false;
    commit("Katman görünümü rasterleştirildi");
    return true;
}
bool Document::canvasSize(QSize size, QPoint anchor, QString *error) {
    if (!validSize(size) || anchor.x() < 0 || anchor.x() > 2 || anchor.y() < 0 || anchor.y() > 2) {
        if (error)
            *error = "Tuval boyutu veya sabitleme noktası geçersiz.";
        return false;
    }
    QPoint delta(qRound((size.width() - state.size.width()) * anchor.x() / 2.0),
                 qRound((size.height() - state.size.height()) * anchor.y() / 2.0));
    State pending = state;
    pending.size = size;
    for (auto &l : pending.layers)
        l.offset += delta;
    if (!validState(pending, error))
        return false;
    beginEdit();
    state = pending;
    selection = {};
    commit("Tuval boyutu değişti");
    return true;
}
bool Document::mergeVisible(QString *error) {
    QVector<bool> merge(state.layers.size(), false);
    QRect bounds;
    int upper = -1;
    for (int i = 0; i < state.layers.size();) {
        int end = i + 1;
        while (end < state.layers.size() && state.layers[end].clipped)
            ++end;
        if (state.layers[i].visible) {
            bounds = bounds.united(
                QRect(state.layers[i].renderedOffset(), state.layers[i].transformedBounds().size()));
            for (int j = i; j < end; ++j) {
                if (!state.layers[j].visible) {
                    if (error)
                        *error = "Gizli kırpılan katman içeren grubu birleştirmeden önce kırpma "
                                 "ilişkisini kaldırın veya katmanı görünür yapın.";
                    return false;
                }
                merge[j] = true;
                upper = j;
            }
        }
        i = end;
    }
    // Independent visible layers are bases too; each base contributes its complete extent.
    if (upper < 0) {
        if (error)
            *error = "Birleştirilecek görünür katman yok.";
        return false;
    }
    qint64 hiddenBytes = 0;
    for (int i = 0; i < state.layers.size(); ++i)
        if (!merge[i])
            hiddenBytes += state.layers[i].pixels.sizeInBytes() + state.layers[i].mask.sizeInBytes();
    if (!validSize(bounds.size()) || !validOffset(bounds.topLeft()) ||
        hiddenBytes + qint64(bounds.width()) * bounds.height() * 4 > MaxLayerBytes) {
        if (error)
            *error = "Birleşik katman boyut veya bellek sınırını aşıyor.";
        return false;
    }
    Layer merged;
    merged.name = "Birleştirilmiş";
    merged.offset = bounds.topLeft();
    merged.pixels = QImage(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    merged.pixels.fill(Qt::transparent);
    renderLayers(merged.pixels, bounds.topLeft(), state.layers);
    State pending = state;
    pending.layers.clear();
    int insertion = 0;
    for (int i = 0; i < state.layers.size(); ++i)
        if (!merge[i]) {
            if (i < upper)
                ++insertion;
            pending.layers.append(state.layers[i]);
        }
    pending.layers.insert(insertion, merged);
    pending.active = insertion;
    if (!validState(pending, error))
        return false;
    beginEdit();
    state = pending;
    editingMask = false;
    commit("Görünür katmanlar birleştirildi");
    return true;
}
bool Document::flatten(QString *error) {
    if (!validSize(state.size)) {
        if (error)
            *error = "Belge boyutu geçersiz.";
        return false;
    }
    Layer merged;
    merged.name = "Düzleştirilmiş";
    merged.pixels = composite();
    if (merged.pixels.isNull())
        return false;
    beginEdit();
    state.layers = {merged};
    state.active = 0;
    editingMask = false;
    commit("Belge düzleştirildi (saydamlık korundu)");
    return true;
}
bool Document::setClipping(bool clipped, QString *error) {
    if (state.layers.isEmpty() || state.layers[state.active].locked || (clipped && state.active == 0)) {
        if (error)
            *error = "Alt katmanı olmayan veya kilitli bir katman kırpılamaz.";
        return false;
    }
    beginEdit();
    state.layers[state.active].clipped = clipped;
    commit(clipped ? "Kırpma maskesi oluşturuldu" : "Kırpma maskesi kaldırıldı");
    return true;
}
bool Document::layerViaSelection(bool cut, QString *error) {
    if (selection.isEmpty() && !cut)
        return duplicate(error);
    if (state.layers.isEmpty() || !selection.hasPixels()) {
        if (error)
            *error = "Önce piksel içeren bir seçim yapın.";
        return false;
    }
    const int index = state.active;
    const Layer &original = state.layers[index];
    if (cut && (original.locked || original.clipped ||
                (index + 1 < state.layers.size() && state.layers[index + 1].clipped))) {
        if (error)
            *error = "Kesme için kilidi/kırpma ilişkisini kaldırın veya grubu birleştirin.";
        return false;
    }
    Layer raster = original.rasterized();
    QRect region = selection.bounds().intersected(QRect(raster.offset, raster.pixels.size()));
    if (region.isEmpty()) {
        if (error)
            *error = "Seçim etkin katmanla kesişmiyor.";
        return false;
    }
    Layer copied = raster;
    copied.name = original.name + (cut ? " seçim kesimi" : " seçim kopyası");
    copied.offset = region.topLeft();
    copied.pixels = raster.pixels.copy(region.translated(-raster.offset));
    bool hasPixels = false;
    for (int y = 0; y < copied.pixels.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(copied.pixels.scanLine(y));
        for (int x = 0; x < copied.pixels.width(); ++x) {
            row[x] = selection.mixPixel(0, row[x], copied.offset + QPoint(x, y));
            hasPixels |= qAlpha(row[x]) != 0;
        }
    }
    if (!hasPixels) {
        if (error)
            *error = "Seçimde etkin katmana ait görünür piksel yok.";
        return false;
    }
    if (cut)
        selection.paint(raster.pixels, raster.offset, [&](QPainter &p) {
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.fillRect(raster.pixels.rect(), Qt::transparent);
        });
    State pending = state;
    if (cut)
        pending.layers[index] = raster;
    // A new independent copy belongs above the active clipping stack, not inside its base/child
    // relation.
    int at = index + 1;
    if (!original.clipped)
        while (at < pending.layers.size() && pending.layers[at].clipped)
            ++at;
    copied.clipped = original.clipped;
    pending.layers.insert(at, copied);
    pending.active = at;
    if (!validState(pending, error))
        return false;
    beginEdit();
    state = pending;
    editingMask = false;
    commit(cut ? "Seçim yeni katmana kesildi" : "Seçim yeni katmana kopyalandı");
    return true;
}
bool Document::mergeDown(QString *error) {
    const int top = state.active;
    if (top <= 0 || state.layers[top].locked || state.layers[top - 1].locked ||
        !state.layers[top].visible || !state.layers[top - 1].visible || state.layers[top - 1].clipped) {
        if (error)
            *error = "Alta birleştirme için görünür, kilidi açık ve kırpma grubunun temeli olan bir alt "
                     "katman gerekir.";
        return false;
    }
    const Layer &base = state.layers[top - 1], &upper = state.layers[top];
    QRect bounds(base.renderedOffset(), base.transformedBounds().size());
    if (!upper.clipped)
        bounds = bounds.united(QRect(upper.renderedOffset(), upper.transformedBounds().size()));
    if (!validSize(bounds.size()) || !validOffset(bounds.topLeft())) {
        if (error)
            *error = "Birleşik alan boyut sınırını aşıyor.";
        return false;
    }
    Layer merged;
    merged.name = upper.name;
    merged.offset = bounds.topLeft();
    if (upper.clipped) {
        merged.pixels = base.rendered();
        merged.opacity = base.opacity;
        merged.blend = base.blend;
        Blending::clipCompose(merged.pixels, upper.rendered(), upper.renderedOffset() - bounds.topLeft(),
                              upper.opacity, upper.blend, bounds.topLeft());
    } else {
        merged.pixels = QImage(bounds.size(), QImage::Format_ARGB32_Premultiplied);
        merged.pixels.fill(Qt::transparent);
        renderLayers(merged.pixels, bounds.topLeft(), {base, upper});
    }
    State pending = state;
    pending.layers[top - 1] = merged;
    pending.layers.removeAt(top);
    pending.active = top - 1;
    if (!validState(pending, error))
        return false;
    QImage before(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    before.fill(Qt::transparent);
    QImage after = before;
    renderLayers(before, bounds.topLeft(), state.layers);
    renderLayers(after, bounds.topLeft(), pending.layers);
    if (before != after) {
        if (error)
            *error = "Bu harmanlama/kırpma zinciri alta birleştirmede görünümü değiştiriyor. Görünür "
                     "katmanları birleştir seçeneğini kullanın.";
        return false;
    }
    beginEdit();
    state = pending;
    editingMask = false;
    commit("Alta birleştirildi");
    return true;
}
void Document::adjust(int b, int c, int s) {
    auto &l = state.layers[state.active];
    if (l.locked)
        return;
    l.brightness = qBound(-100, b, 100);
    l.contrast = qBound(-100, c, 100);
    l.saturation = qBound(-100, s, 100);
    changed();
}
bool Document::adjustTones(const ToneSettings &settings, QString *error) {
    if (state.layers.isEmpty() || state.layers[state.active].locked || !settings.valid()) {
        if (error)
            *error = "Katman kilitli veya ton ayarları geçersiz.";
        return false;
    }
    state.layers[state.active].tones = settings;
    changed();
    return true;
}
bool Document::adjustFilters(const FilterSettings &settings, QString *error) {
    if (state.layers.isEmpty() || state.layers[state.active].locked || !settings.valid()) {
        if (error)
            *error = "Katman kilitli veya filtre ayarları geçersiz.";
        return false;
    }
    state.layers[state.active].filters = settings;
    changed();
    return true;
}
bool Document::adjustMask(const MaskSettings &settings, QString *error) {
    if (state.layers.isEmpty() || state.layers[state.active].locked ||
        state.layers[state.active].mask.isNull() || !settings.valid()) {
        if (error)
            *error = "Maske yok, katman kilitli veya maske ayarları geçersiz.";
        return false;
    }
    state.layers[state.active].maskSettings = settings;
    changed();
    return true;
}
bool Document::addText(QString text, QFont font, QColor color, QPoint at, QString *error) {
    if (!validOffset(at) || text.size() > 16384) {
        if (error)
            *error = "Metin uzunluğu veya konumu sınırı aşıyor.";
        return false;
    }
    QImage image = textImage(text, font, color);
    if (image.isNull()) {
        if (error)
            *error = "Metin görüntüsü boyut sınırını aşıyor.";
        return false;
    }
    if (!allows(image.sizeInBytes(), error))
        return false;
    Layer l;
    l.name = text.left(24);
    l.text = text;
    l.font = font;
    l.color = color;
    l.pixels = image;
    l.offset = at;
    return insertLayer(l, "Metin eklendi", error);
}
bool Document::editText(QString text, QFont font, QColor color, QString *error) {
    auto &l = state.layers[state.active];
    if (l.locked) {
        if (error)
            *error = "Katman kilitli.";
        return false;
    }
    if (text.size() > 16384) {
        if (error)
            *error = "Metin uzunluğu sınırı aşıyor.";
        return false;
    }
    QImage image = textImage(text, font, color);
    if (image.isNull() || bytes(state) - l.pixels.sizeInBytes() - l.mask.sizeInBytes() +
                                  image.sizeInBytes() * (l.mask.isNull() ? 1 : 2) >
                              MaxLayerBytes) {
        if (error)
            *error = "Metin görüntüsü boyut veya bellek sınırını aşıyor.";
        return false;
    }
    Layer candidate = l;
    candidate.pixels = image;
    candidate.mask = {};
    candidate.text = text;
    candidate.font = font;
    candidate.color = color;
    State pending = state;
    pending.layers[pending.active] = candidate;
    if (!validState(pending, error)) {
        if (error)
            *error = "Dönüşmüş metin boyut veya konum sınırını aşıyor.";
        return false;
    }
    beginEdit();
    l.text = text;
    l.font = font;
    l.color = color;
    QImage mask;
    if (!l.mask.isNull()) {
        mask = QImage(image.size(), QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::white);
        QPainter painter(&mask);
        painter.drawImage(0, 0, l.mask);
    }
    l.pixels = image;
    l.mask = mask;
    editingMask = false;
    commit("Metin düzenlendi");
    return true;
}
void Document::beginEdit() {
    if (inEdit)
        return;
    before = state;
    clearCaches(before);
    inEdit = true;
}
void Document::commit(const QString &label) {
    if (!inEdit)
        return;
    inEdit = false;
    if (sameContent(state, before)) {
        before = {};
        emit updated();
        return;
    }
    before = {};
    while (history.size() > cursor + 1)
        history.removeLast();
    State copy = state;
    clearCaches(copy);
    history.append({copy, label, nextId++});
    cursor = history.size() - 1;
    trimHistory();
    dirty = history[cursor].id != savedId;
    emit updated();
}
void Document::cancelEdit() {
    if (!inEdit)
        return;
    state = before;
    before = {};
    inEdit = false;
    emit updated();
}
void Document::changed() {
    emit updated();
}
void Document::undo() {
    if (inEdit)
        cancelEdit();
    if (!canUndo())
        return;
    state = history[--cursor].state;
    selection = {};
    editingMask = false;
    dirty = history[cursor].id != savedId;
    emit updated();
}
void Document::redo() {
    if (inEdit)
        cancelEdit();
    if (!canRedo())
        return;
    state = history[++cursor].state;
    selection = {};
    editingMask = false;
    dirty = history[cursor].id != savedId;
    emit updated();
}
QStringList Document::historyLabels() const {
    QStringList labels;
    for (int i = 0; i <= cursor; ++i)
        labels << history[i].label;
    return labels;
}
qint64 Document::imageBytes() const {
    return bytes(state);
}
qint64 Document::historyBytes() const {
    QSet<qint64> seen;
    for (const auto &layer : state.layers)
        for (const QImage *image : {&layer.pixels, &layer.mask})
            if (!image->isNull())
                seen.insert(image->cacheKey());
    qint64 total = 0;
    for (const auto &e : history)
        for (const auto &l : e.state.layers)
            for (const QImage *im : {&l.pixels, &l.mask})
                if (!im->isNull() && !seen.contains(im->cacheKey())) {
                    seen.insert(im->cacheKey());
                    total += im->sizeInBytes();
                }
    return total;
}
void Document::trimHistory() {
    while (history.size() > 1 && (history.size() > 30 || historyBytes() > HistoryBudget)) {
        history.removeFirst();
        --cursor;
    }
}
void Document::markSaved() {
    savedId = history[cursor].id;
    dirty = false;
    emit updated();
}
bool Document::save(const QString &file, QString *error, bool markClean) {
    if (!validState(state, error))
        return false;
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_6_2);
    out << quint32(0x50444543) << quint32(5) << state.size << qint32(state.active)
        << qint32(state.layers.size());
    for (const auto &l : state.layers) {
        out << l.name << l.offset << l.visible << l.locked << double(l.opacity) << qint32(l.blend)
            << qint32(l.brightness) << qint32(l.contrast) << qint32(l.saturation) << qint32(l.blur)
            << l.text << l.font.toString() << l.color;
        l.tones.write(out);
        out << l.geometry << l.clipped;
        l.filters.write(out);
        l.maskSettings.write(out);
        if (!writeImage(out, l.pixels) || !writeImage(out, l.mask)) {
            if (error)
                *error = "Katman görüntüsü yazılamadı.";
            return false;
        }
    }
    if (out.status() != QDataStream::Ok || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    if (markClean) {
        path = file;
        title = QFileInfo(file).fileName();
        markSaved();
    }
    return true;
}
bool Document::load(const QString &file, QString *error) {
    QFile f(file);
    auto fail = [&]() {
        if (error)
            *error = "Proje bozuk, desteklenmiyor veya güvenli boyut sınırını aşıyor.";
        return false;
    };
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    if (f.size() > 384 * 1024 * 1024)
        return fail();
    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_6_2);
    quint32 magic = 0, version = 0;
    qint32 active = 0, count = 0;
    State pending;
    in >> magic >> version >> pending.size >> active >> count;
    if (magic != 0x50444543 ||
        (version != 1 && version != 2 && version != 3 && version != 4 && version != 5) ||
        !validSize(pending.size) || count < 1 || count > 64 || active < 0 || active >= count)
        return fail();
    pending.active = active;
    qint64 remaining = MaxLayerBytes;
    // Bound each string before allocation: Qt's generic QString reader trusts its length.
    auto readString = [&](QString &s) -> bool {
        quint32 n = 0;
        in >> n;
        if (n == 0xffffffffu) {
            s.clear();
            return true;
        }
        if (n > 32768 || n % 2 || n > f.bytesAvailable())
            return false;
        QByteArray raw(n, Qt::Uninitialized);
        if (in.readRawData(raw.data(), n) != int(n))
            return false;
        s.clear();
        for (uint i = 0; i < n; i += 2)
            s.append(QChar((quint8(raw[i]) << 8) | quint8(raw[i + 1])));
        return true;
    };
    for (int i = 0; i < count; ++i) {
        Layer l;
        double opacity;
        qint32 blend, b, c, s, blur;
        QString font;
        if (!readString(l.name))
            return fail();
        in >> l.offset >> l.visible >> l.locked >> opacity >> blend >> b >> c >> s >> blur;
        if (!readString(l.text) || !readString(font))
            return fail();
        in >> l.color;
        if (in.status() != QDataStream::Ok || !std::isfinite(opacity) || opacity < 0 || opacity > 1 ||
            blend < 0 || blend >= (version == 1 ? 6 : Blending::Count) || qAbs(qint64(b)) > 100 ||
            qAbs(qint64(c)) > 100 || qAbs(qint64(s)) > 100 || blur < 0 || blur > 20 ||
            !validOffset(l.offset))
            return fail();
        l.opacity = opacity;
        l.blend = blend;
        l.brightness = b;
        l.contrast = c;
        l.saturation = s;
        l.blur = blur;
        l.font.fromString(font);
        if (version >= 2 && !ToneSettings::read(in, l.tones))
            return fail();
        if (version >= 3)
            in >> l.geometry;
        if (version >= 4)
            in >> l.clipped;
        if (version >= 5 &&
            (!FilterSettings::read(in, l.filters) || !MaskSettings::read(in, l.maskSettings)))
            return fail();
        if (!readImageRecord(in, l.pixels, remaining) || l.pixels.isNull() ||
            !readImageRecord(in, l.mask, remaining) ||
            (!l.mask.isNull() && l.mask.size() != l.pixels.size()))
            return fail();
        l.pixels = Output::workingRgb(l.pixels);
        pending.layers.append(l);
    }
    if (in.status() != QDataStream::Ok || !f.atEnd() || !validState(pending, error))
        return fail();
    state = pending;
    path = file;
    title = QFileInfo(file).fileName();
    resetHistory("Proje açıldı");
    return true;
}
bool Document::exportImage(const QString &file, int quality, QString *error) {
    RasterExportOptions options;
    options.quality = quality;
    return exportRaster(file, options, error);
}
bool Document::exportPdf(const QString &file, const PdfExportOptions &options, QString *error) {
    return Output::writePdf(composite(), file, title, options, error);
}
bool Document::exportRaster(const QString &file, const RasterExportOptions &options, QString *error) {
    if (!options.valid()) {
        if (error)
            *error = "Çıktı boyutu, DPI, kalite veya ICC seçimi geçersiz.";
        return false;
    }
    QImage image = composite();
    if (image.isNull()) {
        if (error)
            *error = "Belge görüntüsü oluşturulamadı.";
        return false;
    }
    if (options.size.isValid() && options.size != image.size())
        image = image.scaled(options.size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (options.embedProfile) {
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        if (options.profile)
            image = image.convertedToColorSpace(Output::profile(options.profile));
    }
    image.setDotsPerMeterX(qRound(options.dpi / 0.0254));
    image.setDotsPerMeterY(qRound(options.dpi / 0.0254));
    QByteArray format = QFileInfo(file).suffix().toLower().toLatin1();
    if (format == "jpg")
        format = "jpeg";
    if (format == "tif")
        format = "tiff";
    if (!QList<QByteArray>{"png", "jpeg", "webp", "bmp", "tiff"}.contains(format) ||
        !QImageWriter::supportedImageFormats().contains(format)) {
        if (error)
            *error = "Bu görüntü biçimi bu sistemde desteklenmiyor.";
        return false;
    }
    if (options.embedProfile && format == "bmp") {
        if (error)
            *error = "BMP çıktısında ICC kaydı desteklenmiyor. PNG/JPEG/TIFF kullanın veya profil "
                     "eklemeyi kapatın.";
        return false;
    }
    if (format == "jpeg") {
        QImage flattened(image.size(), QImage::Format_RGB32);
        flattened.fill(Qt::white);
        flattened.setColorSpace(image.colorSpace());
        flattened.setDotsPerMeterX(image.dotsPerMeterX());
        flattened.setDotsPerMeterY(image.dotsPerMeterY());
        QPainter p(&flattened);
        p.drawImage(0, 0, image);
        p.end();
        image = flattened;
    }
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QImageWriter writer(&f, format);
    writer.setQuality(options.quality);
    if (!writer.write(image)) {
        if (error)
            *error = writer.errorString();
        return false;
    }
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}
void Document::makeDemo() {
    create({1400, 1000});
    state.layers.clear();
    Layer sky;
    sky.name = "Gökyüzü";
    sky.pixels = QImage(state.size, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter p(&sky.pixels);
        QLinearGradient g(0, 0, 0, 1000);
        g.setColorAt(0, QColor("#172e47"));
        g.setColorAt(.48, QColor("#7ba9ab"));
        g.setColorAt(1, QColor("#c7cdc0"));
        p.fillRect(sky.pixels.rect(), g);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#e2d4b6"));
        p.drawEllipse(QPointF(1050, 225), 63, 63);
    }
    state.layers << sky;
    Layer hills;
    hills.name = "Dağ silüetleri";
    hills.pixels = QImage(state.size, QImage::Format_ARGB32_Premultiplied);
    hills.pixels.fill(Qt::transparent);
    {
        QPainter p(&hills.pixels);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#627f85"));
        p.drawPolygon(QPolygonF{{0, 620},
                                {200, 340},
                                {340, 510},
                                {590, 240},
                                {900, 600},
                                {1120, 390},
                                {1400, 560},
                                {1400, 1000},
                                {0, 1000}});
        p.setBrush(QColor("#aec4bf"));
        p.drawPolygon(QPolygonF{{475, 398}, {590, 240}, {731, 405}, {620, 355}, {582, 375}, {551, 338}});
        p.setBrush(QColor("#244e5a"));
        p.drawPolygon(QPolygonF{{0, 750},
                                {190, 640},
                                {360, 725},
                                {690, 475},
                                {930, 780},
                                {1150, 570},
                                {1400, 730},
                                {1400, 1000},
                                {0, 1000}});
        p.setBrush(QColor("#102e39"));
        p.drawPolygon(QPolygonF{{0, 825},
                                {285, 735},
                                {455, 905},
                                {700, 770},
                                {1020, 880},
                                {1270, 735},
                                {1400, 800},
                                {1400, 1000},
                                {0, 1000}});
    }
    state.layers << hills;
    state.active = 1;
    QFont f("Sans Serif");
    f.setPixelSize(112);
    f.setBold(true);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 14);
    QString e;
    addText("HORIZON", f, QColor("#f3efe5"), {210, 540}, &e);
    f.setPixelSize(23);
    f.setBold(false);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 7);
    addText("KENDİ YOLUNU BUL", f, QColor("#d4e3df"), {216, 692}, &e);
    title = "Horizon · örnek çalışma";
    path.clear();
    state.active = 1;
    resetHistory("Örnek çalışma");
}
