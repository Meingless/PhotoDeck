#pragma once
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRect>
#include <functional>

// Selection coverage uses document coordinates. No selection means unrestricted editing;
// an active, all-zero mask means no pixels may be edited. Keep these states distinct.
class Selection {
  public:
    enum Mode { Replace, Add, Subtract, Intersect };
    Selection() = default;
    Selection(QRect rectangle) : rectangle(rectangle) {}
    Selection(int x, int y, int w, int h) : rectangle(x, y, w, h) {}
    bool isEmpty() const { return rectangle.isEmpty() && coverage.isNull(); }
    bool hasPixels() const { return coverage.isNull() ? !rectangle.isEmpty() : nonzero; }
    QRect bounds() const { return rectangle; }
    bool contains(int x, int y) const { return !isEmpty() && valueAt({x, y}) > 0; }
    int valueAt(QPoint point) const;
    bool operator==(const QRect &rect) const { return coverage.isNull() && rectangle == rect; }
    static Selection fromPath(const QPainterPath &path, QSize size);
    static Selection colorRange(const QImage &image, QPoint seed, int tolerance, bool contiguous);
    Selection combined(const Selection &other, Mode mode, QSize size) const;
    Selection inverted(QSize size) const;
    Selection feathered(int radius, QSize size) const;
    Selection grown(int amount, QSize size) const;
    static Selection fromAlpha(const QImage &image, QPoint offset, QSize canvas);
    QImage mask(QSize size) const;
    QImage boundaryImage() const;
    QImage copyFrom(const QImage &image) const;
    bool paint(QImage &image, QPoint documentOffset, const std::function<void(QPainter &)> &operation,
               QRect affected = {}) const;
    QRgb mixPixel(QRgb before, QRgb after, QPoint documentPoint) const;

  private:
    QRect rectangle;
    QImage coverage; // Grayscale8; null for a rectangular or unrestricted selection.
    mutable QImage boundary;
    bool nonzero = false;
    static Selection fromMask(QImage mask);
};
