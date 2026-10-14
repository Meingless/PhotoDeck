#pragma once
#include <QImage>
#include <QJsonObject>
#include <QVector>

struct BrushSettings {
    int size = 32, hardness = 80, flow = 100, spacing = 16, angle = 0, roundness = 100;
    int pressureMinimum = 10;
    qreal pressureCurve = 1;
    bool pressureSize = true, pressureFlow = false, tiltAngle = false, tiltRoundness = false;
    QImage tipMask; // Optional grayscale coverage, white draws; longest edge <= 512.
    bool valid() const;
    bool operator==(const BrushSettings &other) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, BrushSettings *, QString *error);
    static QImage defineTip(const QImage &source); // Source darkness multiplied by alpha.
};
struct BrushPreset {
    QString name;
    int tool = 0; // Brush, pencil, eraser, clone.
    BrushSettings settings;
    bool valid() const;
    bool operator==(const BrushPreset &o) const {
        return name == o.name && tool == o.tool && settings == o.settings;
    }
};
namespace BrushLibrary {
constexpr int MaxPresets = 128;
constexpr qint64 MaxMaskBytes = 8 * 1024 * 1024;
constexpr qint64 MaxFileBytes = 16 * 1024 * 1024;
QVector<BrushPreset> builtins();
bool valid(const QVector<BrushPreset> &, QString *error);
bool read(const QString &file, QVector<BrushPreset> *presets, QString *error);
bool write(const QString &file, const QVector<BrushPreset> &presets, QString *error);
bool merge(const QVector<BrushPreset> &current, const QVector<BrushPreset> &incoming,
           QVector<BrushPreset> *result, QString *error);
} // namespace BrushLibrary
