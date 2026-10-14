#include "brushpreset.h"
#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>
#include <cmath>
namespace {
bool fail(QString *error, const QString &text) {
    if (error)
        *error = text;
    return false;
}
bool validName(QString name) {
    if (name.isEmpty() || name.size() > 80 || name != name.trimmed() ||
        name != name.normalized(QString::NormalizationForm_C) ||
        QString::fromUtf8(name.toUtf8()) != name)
        return false;
    for (QChar c : name)
        if (c.isNull() || c.category() == QChar::Other_Control || c.category() == QChar::Other_Format ||
            c == '<' || c == '>')
            return false;
    return true;
}
bool keys(const QJsonObject &object, const QSet<QString> &allowed) {
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!allowed.contains(it.key()))
            return false;
    return true;
}
QImage normalized(QImage image) {
    image = image.convertToFormat(QImage::Format_Grayscale8);
    image.setColorSpace({});
    image.setDotsPerMeterX(0);
    image.setDotsPerMeterY(0);
    image.setOffset({});
    return image;
}
bool safeMaskPng(const QByteArray &png) {
    if (!png.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
        return false;
    qsizetype pos = 8;
    bool header = false, data = false;
    while (pos + 12 <= png.size()) {
        const quint32 length =
            qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(png.constData() + pos));
        if (length > quint32(png.size() - pos - 12))
            return false;
        const QByteArray kind = png.mid(pos + 4, 4);
        if (!header) {
            if (kind != "IHDR" || length != 13)
                return false;
            const auto *payload = reinterpret_cast<const uchar *>(png.constData() + pos + 8);
            const quint32 width = qFromBigEndian<quint32>(payload),
                          height = qFromBigEndian<quint32>(payload + 4);
            if (!width || !height || width > 512 || height > 512 || payload[8] != 8 || payload[9] != 0)
                return false;
            header = true;
        } else if (kind == "IDAT")
            data = true;
        else if (kind == "IEND")
            return length == 0 && data && pos + 12 == png.size();
        else if (!((kind == "pHYs" && length == 9) || (kind == "gAMA" && length == 4) ||
                   (kind == "sRGB" && length == 1) || (kind == "cHRM" && length == 32)))
            return false;
        pos += length + 12;
    }
    return false;
}
bool maskValid(const QImage &mask) {
    if (mask.isNull())
        return true;
    if (mask.format() != QImage::Format_Grayscale8 || mask.width() > 512 || mask.height() > 512)
        return false;
    for (int y = 0; y < mask.height(); ++y)
        for (int x = 0; x < mask.width(); ++x)
            if (mask.constScanLine(y)[x])
                return true;
    return false;
}
} // namespace
bool BrushSettings::valid() const {
    return size >= 1 && size <= 512 && hardness >= 0 && hardness <= 100 && flow >= 1 && flow <= 100 &&
           spacing >= 1 && spacing <= 200 && angle >= -180 && angle <= 180 && roundness >= 5 &&
           roundness <= 100 && pressureMinimum >= 1 && pressureMinimum <= 100 &&
           std::isfinite(pressureCurve) && pressureCurve >= .25 && pressureCurve <= 4 &&
           maskValid(tipMask);
}
bool BrushSettings::operator==(const BrushSettings &o) const {
    return size == o.size && hardness == o.hardness && flow == o.flow && spacing == o.spacing &&
           angle == o.angle && roundness == o.roundness && pressureMinimum == o.pressureMinimum &&
           pressureCurve == o.pressureCurve && pressureSize == o.pressureSize &&
           pressureFlow == o.pressureFlow && tiltAngle == o.tiltAngle &&
           tiltRoundness == o.tiltRoundness && tipMask == o.tipMask;
}
QJsonObject BrushSettings::toJson() const {
    QJsonObject o{{"size", size},
                  {"hardness", hardness},
                  {"flow", flow},
                  {"spacing", spacing},
                  {"angle", angle},
                  {"roundness", roundness},
                  {"pressureMinimum", pressureMinimum},
                  {"pressureCurve", pressureCurve},
                  {"pressureSize", pressureSize},
                  {"pressureFlow", pressureFlow},
                  {"tiltAngle", tiltAngle},
                  {"tiltRoundness", tiltRoundness}};
    if (!tipMask.isNull()) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!tipMask.save(&buffer, "PNG"))
            return {};
        o["tipMaskPng"] = QString::fromLatin1(bytes.toBase64());
    }
    return o;
}
bool BrushSettings::fromJson(const QJsonObject &o, BrushSettings *out, QString *error) {
    const QSet<QString> allowed{
        "size",      "hardness",        "flow",          "spacing",      "angle",
        "roundness", "pressureMinimum", "pressureCurve", "pressureSize", "pressureFlow",
        "tiltAngle", "tiltRoundness",   "tipMaskPng"};
    if (!out || !keys(o, allowed))
        return fail(error, "Fırça alanları tanınmıyor.");
    BrushSettings s;
    const auto integer = [&](const QString &key, int *target) {
        const auto v = o[key];
        const double n = v.toDouble();
        if (!v.isDouble() || !std::isfinite(n) || n != std::floor(n) || n < -10000 || n > 10000)
            return false;
        *target = int(n);
        return true;
    };
    if (!integer("size", &s.size) || !integer("hardness", &s.hardness) || !integer("flow", &s.flow) ||
        !integer("spacing", &s.spacing) || !integer("angle", &s.angle) ||
        !integer("roundness", &s.roundness) || !integer("pressureMinimum", &s.pressureMinimum) ||
        !o["pressureCurve"].isDouble())
        return fail(error, "Fırça sayı alanı geçersiz.");
    s.pressureCurve = o["pressureCurve"].toDouble();
    const auto boolean = [&](const QString &key, bool *target) {
        if (!o[key].isBool())
            return false;
        *target = o[key].toBool();
        return true;
    };
    if (!boolean("pressureSize", &s.pressureSize) || !boolean("pressureFlow", &s.pressureFlow) ||
        !boolean("tiltAngle", &s.tiltAngle) || !boolean("tiltRoundness", &s.tiltRoundness))
        return fail(error, "Fırça seçenek alanı geçersiz.");
    if (o.contains("tipMaskPng")) {
        if (!o["tipMaskPng"].isString())
            return fail(error, "Uç maskesi geçersiz.");
        const QString encoded = o["tipMaskPng"].toString();
        if (encoded.isEmpty() || encoded.size() > 524288)
            return fail(error, "Uç maskesi boyutu geçersiz.");
        const QByteArray png =
            QByteArray::fromBase64(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (QString::fromLatin1(png.toBase64()) != encoded)
            return fail(error, "Uç maskesi kodlaması geçersiz.");
        if (!safeMaskPng(png))
            return fail(error, "Uç maskesi PNG yapısı veya ek verileri desteklenmiyor.");
        QBuffer buffer;
        buffer.setData(png);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "PNG");
        reader.setAutoDetectImageFormat(false);
        const QSize dimensions = reader.size();
        if (dimensions.width() < 1 || dimensions.height() < 1 || dimensions.width() > 512 ||
            dimensions.height() > 512)
            return fail(error, "Uç maskesi 512 px sınırını aşıyor.");
        const QImage image = reader.read();
        if (image.isNull() || image.size() != dimensions || !image.isGrayscale() ||
            image.hasAlphaChannel())
            return fail(error, "Uç maskesi gri PNG olmalı.");
        s.tipMask = normalized(image);
    }
    if (!s.valid())
        return fail(error, "Fırça ayarları güvenli sınırları aşıyor veya uç boş.");
    *out = s;
    return true;
}
QImage BrushSettings::defineTip(const QImage &source) {
    if (source.isNull() || source.width() > 16384 || source.height() > 16384 ||
        qint64(source.width()) * source.height() > 16 * 1024 * 1024)
        return {};
    QImage image = source;
    if (qMax(image.width(), image.height()) > 512)
        image = image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image = image.convertToFormat(QImage::Format_ARGB32);
    QImage mask(image.size(), QImage::Format_Grayscale8);
    mask.fill(0);
    for (int y = 0; y < image.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        auto *dest = mask.scanLine(y);
        for (int x = 0; x < image.width(); ++x)
            dest[x] = uchar(((255 - qGray(row[x])) * qAlpha(row[x]) + 127) / 255);
    }
    mask = normalized(mask);
    return maskValid(mask) ? mask : QImage{};
}
bool BrushPreset::valid() const {
    return validName(name) && tool >= 0 && tool <= 3 && settings.valid();
}
namespace BrushLibrary {
QVector<BrushPreset> builtins() {
    BrushSettings hard;
    hard.hardness = 100;
    BrushSettings soft = hard;
    soft.hardness = 15;
    return {{"Yuvarlak", 0, hard}, {"Yumuşak", 0, soft}, {"Kalem", 1, hard}};
}
bool valid(const QVector<BrushPreset> &presets, QString *error) {
    if (presets.size() > MaxPresets)
        return fail(error, "En fazla 128 fırça preseti saklanabilir.");
    QSet<QString> names;
    qint64 bytes = 0;
    for (const auto &p : presets) {
        if (!p.valid())
            return fail(error, "Preset adı, aracı veya fırça ayarları geçersiz.");
        const QString key = p.name.toCaseFolded();
        if (names.contains(key))
            return fail(error, "Preset adları benzersiz olmalı.");
        names.insert(key);
        bytes += p.settings.tipMask.sizeInBytes();
    }
    return bytes <= MaxMaskBytes || fail(error, "Toplam uç maskesi 8 MiB sınırını aşıyor.");
}
bool read(const QString &file, QVector<BrushPreset> *out, QString *error) {
    if (!out)
        return fail(error, "Kütüphane hedefi geçersiz.");
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return fail(error, f.errorString());
    const QByteArray data = f.read(MaxFileBytes + 1);
    if (data.size() > MaxFileBytes)
        return fail(error, "Fırça dosyası 16 MiB sınırını aşıyor.");
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(data, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail(error, "Fırça dosyası JSON yapısı geçersiz.");
    const auto o = doc.object();
    if (!keys(o, {"format", "version", "presets"}) ||
        o["format"].toString() != "PhotoDeck-brush-library" || !o["version"].isDouble() ||
        o["version"].toDouble() != 1 || !o["presets"].isArray())
        return fail(error, "Fırça dosyası biçimi veya sürümü desteklenmiyor.");
    const auto array = o["presets"].toArray();
    if (array.size() > MaxPresets)
        return fail(error, "Fırça dosyasında çok fazla preset var.");
    QVector<BrushPreset> pending;
    for (const auto &value : array) {
        if (!value.isObject())
            return fail(error, "Preset kaydı geçersiz.");
        const auto p = value.toObject();
        if (!keys(p, {"name", "tool", "settings"}) || !p["name"].isString() || !p["tool"].isDouble() ||
            p["tool"].toDouble() != std::floor(p["tool"].toDouble()) || p["tool"].toDouble() < 0 ||
            p["tool"].toDouble() > 3 || !p["settings"].isObject())
            return fail(error, "Preset kaydı alanları geçersiz.");
        BrushPreset preset;
        preset.name = p["name"].toString();
        preset.tool = p["tool"].toInt();
        if (!BrushSettings::fromJson(p["settings"].toObject(), &preset.settings, error))
            return false;
        pending.append(preset);
    }
    if (!valid(pending, error))
        return false;
    *out = pending;
    return true;
}
bool write(const QString &file, const QVector<BrushPreset> &presets, QString *error) {
    if (QFileInfo(file).suffix().compare("pdbrush", Qt::CaseInsensitive) != 0)
        return fail(error, "Fırça çıktısı için .pdbrush uzantısını kullanın.");
    if (!valid(presets, error))
        return false;
    QJsonArray array;
    for (const auto &p : presets) {
        const auto settings = p.settings.toJson();
        if (settings.isEmpty())
            return fail(error, "Uç maskesi kodlanamadı.");
        array.append(QJsonObject{{"name", p.name}, {"tool", p.tool}, {"settings", settings}});
    }
    const QByteArray bytes =
        QJsonDocument(
            QJsonObject{{"format", "PhotoDeck-brush-library"}, {"version", 1}, {"presets", array}})
            .toJson();
    if (bytes.size() > MaxFileBytes)
        return fail(error, "Fırça dosyası boyut sınırını aşıyor.");
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return fail(error, f.errorString());
    if (f.write(bytes) != bytes.size() || !f.commit())
        return fail(error, f.errorString());
    return true;
}
bool merge(const QVector<BrushPreset> &current, const QVector<BrushPreset> &incoming,
           QVector<BrushPreset> *out, QString *error) {
    if (!out || !valid(current, error) || !valid(incoming, error))
        return false;
    auto pending = current;
    QSet<QString> names;
    for (const auto &p : current)
        names.insert(p.name.toCaseFolded());
    for (auto p : incoming) {
        const QString base = p.name;
        int suffix = 2;
        while (names.contains(p.name.toCaseFolded())) {
            const QString tail = " (" + QString::number(suffix++) + ")";
            p.name = base.left(80 - tail.size()) + tail;
        }
        names.insert(p.name.toCaseFolded());
        pending.append(p);
    }
    if (!valid(pending, error))
        return false;
    *out = pending;
    return true;
}
} // namespace BrushLibrary
