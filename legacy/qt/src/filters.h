#pragma once
#include <QDataStream>
#include <QImage>

struct FilterSettings {
    int gaussian = 0, sharpenAmount = 0, sharpenRadius = 2, sharpenThreshold = 0;
    int highPass = 0, noise = 0, mosaic = 1;
    bool monochromeNoise = true;
    quint32 noiseSeed = 1;
    bool valid() const;
    bool isIdentity() const;
    bool operator==(const FilterSettings &other) const;
    void write(QDataStream &stream) const;
    static bool read(QDataStream &stream, FilterSettings &settings);
};
struct MaskSettings {
    bool enabled = true;
    int density = 100, feather = 0;
    bool valid() const;
    bool operator==(const MaskSettings &other) const;
    void write(QDataStream &stream) const;
    static bool read(QDataStream &stream, MaskSettings &settings);
};
namespace Filters {
QImage gaussianBlur(const QImage &source, int radius);
QImage apply(const QImage &source, const FilterSettings &settings);
QImage maskCoverage(const QImage &mask, const MaskSettings &settings);
} // namespace Filters
