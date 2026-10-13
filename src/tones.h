#pragma once
#include <QDataStream>
#include <QPoint>
#include <QRgb>
#include <QVector>
#include <array>

struct ToneSettings {
    int inputBlack = 0, inputWhite = 255, outputBlack = 0, outputWhite = 255;
    double gamma = 1, exposure = 0;
    int hue = 0, vibrance = 0, redBalance = 0, greenBalance = 0, blueBalance = 0;
    bool inverted = false;
    int threshold = -1, posterize = 256;
    // Composite RGB followed by the individual R, G and B curves.
    std::array<QVector<QPoint>, 4> curves = {
        QVector<QPoint>{{0, 0}, {255, 255}}, QVector<QPoint>{{0, 0}, {255, 255}},
        QVector<QPoint>{{0, 0}, {255, 255}}, QVector<QPoint>{{0, 0}, {255, 255}}};
    bool valid() const;
    bool isIdentity() const;
    bool operator==(const ToneSettings &other) const;
    bool operator!=(const ToneSettings &other) const { return !(*this == other); }
    void write(QDataStream &stream) const;
    static bool read(QDataStream &stream, ToneSettings &result);
};

class ToneMapper {
  public:
    explicit ToneMapper(const ToneSettings &settings);
    QRgb map(QRgb pixel) const;

  private:
    ToneSettings settings;
    std::array<std::array<int, 256>, 3> tables;
};
