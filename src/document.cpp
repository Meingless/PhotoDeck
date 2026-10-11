#include "document.h"
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QSet>
#include <QtMath>
#include <cmath>

namespace {
qint64 bytes(const State &s) {
    qint64 n = 0;
    for (const auto &l : s.layers)
        n += l.pixels.sizeInBytes() + l.mask.sizeInBytes();
    return n;
}
void clearCaches(State &s) {
    for (auto &l : s.layers)
        l.cache = QImage();
}
QImage textImage(const QString &text, const QFont &font, const QColor &color) {
    QFontMetrics fm(font);
    QRect r = fm.boundingRect(QRect(0, 0, 8192, 8192), Qt::TextExpandTabs, text);
    QSize size(qMax(1, r.width() + 12), qMax(1, r.height() + 12));
    if (!Document::validSize(size))
        return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(font);
    p.setPen(color);
    p.drawText(QRect(6, 6, size.width() - 12, size.height() - 12), Qt::TextExpandTabs, text);
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

QImage Layer::rendered() const {
    if (mask.isNull() && brightness == 0 && contrast == 0 && saturation == 0 && blur == 0)
        return pixels;
    if (!cache.isNull() && cacheKey == pixels.cacheKey() && maskKey == mask.cacheKey() &&
        cacheB == brightness && cacheC == contrast && cacheS == saturation && cacheBlur == blur)
        return cache;
    cache = boxBlur(pixels, blur).convertToFormat(QImage::Format_ARGB32);
    double factor = std::pow(2.0, contrast / 50.0), sat = 1.0 + saturation / 100.0;
    for (int y = 0; y < cache.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(cache.scanLine(y));
        const auto *mr = mask.isNull() ? nullptr : reinterpret_cast<const QRgb *>(mask.constScanLine(y));
        for (int x = 0; x < cache.width(); ++x) {
            QRgb v = row[x];
            double r = qRed(v), g = qGreen(v), b = qBlue(v), lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            r = (lum + (r - lum) * sat - 127.5) * factor + 127.5 + brightness * 2.55;
            g = (lum + (g - lum) * sat - 127.5) * factor + 127.5 + brightness * 2.55;
            b = (lum + (b - lum) * sat - 127.5) * factor + 127.5 + brightness * 2.55;
            int a = qAlpha(v);
            if (mr)
                a = a * qRed(mr[x]) / 255;
            row[x] = qRgba(qBound(0, qRound(r), 255), qBound(0, qRound(g), 255),
                           qBound(0, qRound(b), 255), a);
        }
    }
    cache = cache.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    cacheKey = pixels.cacheKey();
    maskKey = mask.cacheKey();
    cacheB = brightness;
    cacheC = contrast;
    cacheS = saturation;
    cacheBlur = blur;
    return cache;
}
Document::Document(QObject *parent) : QObject(parent) {}
bool Document::validSize(QSize s) {
    return s.width() > 0 && s.height() > 0 && s.width() <= 16384 && s.height() <= 16384 &&
           qint64(s.width()) * s.height() <= MaxPixels;
}
QPainter::CompositionMode Document::compositionMode(int b) {
    switch (b) {
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
    default:
        return QPainter::CompositionMode_SourceOver;
    }
}
QImage Document::composite() const {
    if (!validSize(state.size))
        return {};
    QImage out(state.size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    for (const auto &l : state.layers)
        if (l.visible) {
            p.setOpacity(l.opacity);
            p.setCompositionMode(compositionMode(l.blend));
            p.drawImage(l.offset, l.rendered());
        }
    return out;
}
bool Document::allows(qint64 extra, QString *error) const {
    if (bytes(state) + extra <= MaxLayerBytes && state.layers.size() < 64)
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
    if (format != "png" && format != "jpeg" && format != "jpg" && format != "webp" && format != "bmp") {
        if (error)
            *error = "Desteklenen görüntüler: PNG, JPEG, WebP ve BMP. Katmanlı projeler için .pdeck "
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
    return out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
bool Document::importImage(const QString &file, bool asLayer, QString *error) {
    QImage image = readImage(file, error);
    if (image.isNull())
        return false;
    if (asLayer) {
        if (!allows(image.sizeInBytes(), error))
            return false;
        beginEdit();
    } else
        state = {image.size(), {}, 0};
    Layer l;
    l.name = QFileInfo(file).completeBaseName();
    l.pixels = image;
    state.layers.append(l);
    state.active = state.layers.size() - 1;
    editingMask = false;
    if (asLayer)
        commit("Görüntü katmanı eklendi");
    else {
        path.clear();
        title = QFileInfo(file).fileName();
        resetHistory("Görüntü açıldı");
    }
    return true;
}
bool Document::addLayer(const QString &name, QString *error) {
    if (!allows(qint64(state.size.width()) * state.size.height() * 4, error))
        return false;
    beginEdit();
    Layer l;
    l.name = name;
    l.pixels = QImage(state.size, QImage::Format_ARGB32_Premultiplied);
    l.pixels.fill(Qt::transparent);
    state.layers.insert(state.active + 1, l);
    ++state.active;
    editingMask = false;
    commit("Yeni katman");
    return true;
}
bool Document::duplicate(QString *error) {
    const Layer source = state.layers[state.active];
    if (!allows(source.pixels.sizeInBytes() + source.mask.sizeInBytes(), error))
        return false;
    beginEdit();
    Layer l = source;
    l.name += " kopya";
    l.cache = {};
    state.layers.insert(state.active + 1, l);
    ++state.active;
    commit("Katman çoğaltıldı");
    return true;
}
void Document::deleteLayer() {
    if (state.layers.size() <= 1)
        return;
    beginEdit();
    state.layers.removeAt(state.active);
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
    if (!allows(l.pixels.sizeInBytes(), error))
        return false;
    beginEdit();
    l.mask = QImage(l.pixels.size(), QImage::Format_ARGB32_Premultiplied);
    l.mask.fill(Qt::white);
    if (!selection.isEmpty()) {
        l.mask.fill(Qt::black);
        QPainter p(&l.mask);
        p.fillRect(selection.translated(-l.offset), Qt::white);
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
    beginEdit();
    state.size = rect.size();
    for (auto &l : state.layers)
        l.offset -= rect.topLeft();
    selection = {};
    commit("Tuval kırpıldı");
    return true;
}
bool Document::resize(QSize size, QString *error) {
    if (!validSize(size)) {
        if (error)
            *error = "Geçersiz boyut (en fazla 16 megapiksel).";
        return false;
    }
    qreal sx = qreal(size.width()) / state.size.width(), sy = qreal(size.height()) / state.size.height();
    qint64 needed = 0;
    for (const auto &l : state.layers) {
        QSize ls(qMax(1, qRound(l.pixels.width() * sx)), qMax(1, qRound(l.pixels.height() * sy)));
        if (!validSize(ls)) {
            if (error)
                *error = "Katman boyutu sınırı aşılıyor.";
            return false;
        }
        needed += qint64(ls.width()) * ls.height() * 4 * (l.mask.isNull() ? 1 : 2);
    }
    if (needed > MaxLayerBytes) {
        if (error)
            *error = "Yeniden boyutlandırma 256 MiB katman sınırını aşar.";
        return false;
    }
    beginEdit();
    for (auto &l : state.layers) {
        QSize ls(qMax(1, qRound(l.pixels.width() * sx)), qMax(1, qRound(l.pixels.height() * sy)));
        l.pixels = l.pixels.scaled(ls, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (!l.mask.isNull())
            l.mask = l.mask.scaled(ls, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        l.offset = {qRound(l.offset.x() * sx), qRound(l.offset.y() * sy)};
        l.text.clear();
    }
    state.size = size;
    selection = {};
    commit("Görüntü boyutu");
    return true;
}
bool Document::rotate(bool cw, QString *) {
    beginEdit();
    int w = state.size.width(), h = state.size.height();
    QTransform t;
    t.rotate(cw ? 90 : -90);
    for (auto &l : state.layers) {
        QSize old = l.pixels.size();
        l.pixels = l.pixels.transformed(t);
        if (!l.mask.isNull())
            l.mask = l.mask.transformed(t);
        l.offset = cw ? QPoint(h - l.offset.y() - old.height(), l.offset.x())
                      : QPoint(l.offset.y(), w - l.offset.x() - old.width());
        l.text.clear();
    }
    state.size.transpose();
    selection = {};
    commit("90° döndürme");
    return true;
}
void Document::flip(bool h) {
    beginEdit();
    for (auto &l : state.layers) {
        l.pixels = flippedImage(l.pixels, h);
        if (!l.mask.isNull())
            l.mask = flippedImage(l.mask, h);
        if (h)
            l.offset.setX(state.size.width() - l.offset.x() - l.pixels.width());
        else
            l.offset.setY(state.size.height() - l.offset.y() - l.pixels.height());
        l.text.clear();
    }
    selection = {};
    commit("Görüntü çevrildi");
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
bool Document::addText(QString text, QFont font, QColor color, QPoint at, QString *error) {
    QImage image = textImage(text, font, color);
    if (image.isNull()) {
        if (error)
            *error = "Metin görüntüsü boyut sınırını aşıyor.";
        return false;
    }
    if (!allows(image.sizeInBytes(), error))
        return false;
    beginEdit();
    Layer l;
    l.name = text.left(24);
    l.text = text;
    l.font = font;
    l.color = color;
    l.pixels = image;
    l.offset = at;
    state.layers.insert(state.active + 1, l);
    ++state.active;
    editingMask = false;
    commit("Metin eklendi");
    return true;
}
bool Document::editText(QString text, QFont font, QColor color, QString *error) {
    auto &l = state.layers[state.active];
    if (l.locked) {
        if (error)
            *error = "Katman kilitli.";
        return false;
    }
    QImage image = textImage(text, font, color);
    if (image.isNull() ||
        bytes(state) - l.pixels.sizeInBytes() - l.mask.sizeInBytes() + image.sizeInBytes() >
            MaxLayerBytes) {
        if (error)
            *error = "Metin görüntüsü boyut veya bellek sınırını aşıyor.";
        return false;
    }
    beginEdit();
    l.text = text;
    l.font = font;
    l.color = color;
    l.pixels = image;
    l.mask = {};
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
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_6_2);
    out << quint32(0x50444543) << quint32(1) << state.size << qint32(state.active)
        << qint32(state.layers.size());
    for (const auto &l : state.layers) {
        out << l.name << l.offset << l.visible << l.locked << double(l.opacity) << qint32(l.blend)
            << qint32(l.brightness) << qint32(l.contrast) << qint32(l.saturation) << qint32(l.blur)
            << l.text << l.font.toString() << l.color;
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
    if (magic != 0x50444543 || version != 1 || !validSize(pending.size) || count < 1 || count > 64 ||
        active < 0 || active >= count)
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
            blend < 0 || blend > 5 || qAbs(qint64(b)) > 100 || qAbs(qint64(c)) > 100 ||
            qAbs(qint64(s)) > 100 || blur < 0 || blur > 20 || qAbs(qint64(l.offset.x())) > 32768 ||
            qAbs(qint64(l.offset.y())) > 32768)
            return fail();
        l.opacity = opacity;
        l.blend = blend;
        l.brightness = b;
        l.contrast = c;
        l.saturation = s;
        l.blur = blur;
        l.font.fromString(font);
        if (!readImageRecord(in, l.pixels, remaining) || l.pixels.isNull() ||
            !readImageRecord(in, l.mask, remaining) ||
            (!l.mask.isNull() && l.mask.size() != l.pixels.size()))
            return fail();
        pending.layers.append(l);
    }
    if (in.status() != QDataStream::Ok || !f.atEnd())
        return fail();
    state = pending;
    path = file;
    title = QFileInfo(file).fileName();
    resetHistory("Proje açıldı");
    return true;
}
bool Document::exportImage(const QString &file, int quality, QString *error) {
    QImage image = composite();
    QByteArray format = QFileInfo(file).suffix().toLower().toLatin1();
    if (format == "jpg")
        format = "jpeg";
    if (!QImageWriter::supportedImageFormats().contains(format)) {
        if (error)
            *error = "Bu görüntü biçimi bu sistemde desteklenmiyor.";
        return false;
    }
    if (format == "jpeg") {
        QImage flattened(image.size(), QImage::Format_RGB32);
        flattened.fill(Qt::white);
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
    writer.setQuality(quality);
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
