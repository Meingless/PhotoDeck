#pragma once
#include <QImage>
#include <QPainter>
#include <QStringList>

namespace Blending {
constexpr int Count = 27;
QStringList names();
QPainter::CompositionMode painterMode(int mode);
void compose(QImage &destination, const QImage &source, QPoint offset, double opacity, int mode);
} // namespace Blending
