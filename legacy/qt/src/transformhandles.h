#pragma once
#include <QLineF>
#include <QPolygonF>
#include <QTransform>
#include <QtMath>
#include <cmath>

// Geometry only: source coordinates stay intact, and handle hit areas are measured in widget pixels.
class TransformHandles {
  public:
    enum Handle {
        None = -1,
        TopLeft,
        Top,
        TopRight,
        Right,
        BottomRight,
        Bottom,
        BottomLeft,
        Left,
        Rotate,
        Move
    };
    QSizeF size;
    QTransform geometry;
    QPointF offset;
    QRectF viewport;

    QPointF sourcePoint(Handle handle) const {
        const qreal w = size.width(), h = size.height();
        switch (handle) {
        case TopLeft:
            return {0, 0};
        case Top:
            return {w / 2, 0};
        case TopRight:
            return {w, 0};
        case Right:
            return {w, h / 2};
        case BottomRight:
            return {w, h};
        case Bottom:
            return {w / 2, h};
        case BottomLeft:
            return {0, h};
        case Left:
            return {0, h / 2};
        default:
            return {w / 2, h / 2};
        }
    }
    QPointF point(Handle handle) const { return geometry.map(sourcePoint(handle)) + offset; }
    QPolygonF polygon() const {
        return {point(TopLeft), point(TopRight), point(BottomRight), point(BottomLeft)};
    }
    QPointF widgetPoint(Handle handle, qreal zoom, QPointF pan) const {
        QPointF at = point(handle) * zoom + pan;
        if (handle == Rotate) {
            at = point(Top) * zoom + pan;
            QPointF direction = at - (point(Move) * zoom + pan);
            qreal length = std::hypot(direction.x(), direction.y());
            direction = length > .0001 ? direction / length : QPointF(0, -1);
            const QPointF base = at;
            at += direction * 28;
            // Shorten the stem near a viewport edge so a visible box keeps its rotation handle.
            if (viewport.isValid() && viewport.contains(base)) {
                const QRectF safe = viewport.adjusted(9, 9, -9, -9);
                at.setX(qBound(safe.left(), at.x(), safe.right()));
                at.setY(qBound(safe.top(), at.y(), safe.bottom()));
            }
        }
        return at;
    }
    Handle hitTest(QPointF widget, qreal zoom, QPointF pan) const {
        if (!finite(widget) || !std::isfinite(zoom) || zoom <= 0)
            return None;
        // Corners win when a small box causes adjacent handle hit areas to overlap.
        for (Handle handle : {Rotate, TopLeft, TopRight, BottomRight, BottomLeft, Top, Right,
                              Bottom, Left})
            if (QLineF(widget, widgetPoint(handle, zoom, pan)).length() <= 9)
                return handle;
        QTransform view;
        view.translate(pan.x(), pan.y());
        view.scale(zoom, zoom);
        return view.map(polygon()).containsPoint(widget, Qt::OddEvenFill) ? Move : None;
    }
    QTransform dragged(Handle handle, QPointF press, QPointF current,
                       Qt::KeyboardModifiers modifiers) const {
        if (!finite(press) || !finite(current) || size.isEmpty())
            return geometry;
        const bool shift = modifiers & Qt::ShiftModifier;
        QPointF delta = current - press;
        if (handle == Move) {
            if (shift) {
                if (qAbs(delta.x()) >= qAbs(delta.y()))
                    delta.setY(0);
                else
                    delta.setX(0);
            }
            return geometry * QTransform::fromTranslate(delta.x(), delta.y());
        }
        if (handle == Rotate) {
            const QPointF center = point(Move), a = press - center, b = current - center;
            if (std::hypot(a.x(), a.y()) < .0001 || std::hypot(b.x(), b.y()) < .0001)
                return geometry;
            qreal angle = qRadiansToDegrees(std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x()));
            if (shift) {
                const QPointF edge = point(TopRight) - point(TopLeft);
                const qreal initial = qRadiansToDegrees(std::atan2(edge.y(), edge.x()));
                angle = std::round((initial + angle) / 15) * 15 - initial;
            }
            const QPointF pivot = center - offset;
            QTransform rotation;
            rotation.translate(pivot.x(), pivot.y());
            rotation.rotate(angle);
            rotation.translate(-pivot.x(), -pivot.y());
            return geometry * rotation;
        }
        if (handle < TopLeft || handle > Left)
            return geometry;
        bool invertible = false;
        const QTransform inverse = geometry.inverted(&invertible);
        if (!invertible)
            return geometry;
        const QPointF localDelta = inverse.map(current - offset) - inverse.map(press - offset);
        const QPointF start = sourcePoint(handle);
        const bool horizontal = handle != Top && handle != Bottom;
        const bool vertical = handle != Left && handle != Right;
        const bool corner = horizontal && vertical;
        const QPointF anchor = (modifiers & Qt::AltModifier)
                                   ? sourcePoint(Move)
                                   : QPointF(size.width() - start.x(), size.height() - start.y());
        const QPointF span = start - anchor;
        qreal sx = horizontal ? (span.x() + localDelta.x()) / span.x() : 1;
        qreal sy = vertical ? (span.y() + localDelta.y()) / span.y() : 1;
        if ((corner && !shift) || (!corner && shift)) {
            qreal ratio;
            if (corner) {
                const QPointF origin = geometry.map(anchor) + offset;
                const QPointF vector = point(handle) - origin;
                const qreal squared = QPointF::dotProduct(vector, vector);
                ratio = squared > 1e-12
                            ? QPointF::dotProduct(vector + delta, vector) / squared
                            : 1;
            } else
                ratio = horizontal ? sx : sy;
            sx = sy = ratio;
        }
        if (!std::isfinite(sx) || !std::isfinite(sy))
            return geometry;
        // Crossing an anchor stops at a small positive size; deliberate mirroring is in Numeric.
        sx = qBound(.01, sx, 10000.0);
        sy = qBound(.01, sy, 10000.0);
        QTransform scale;
        scale.translate(anchor.x(), anchor.y());
        scale.scale(sx, sy);
        scale.translate(-anchor.x(), -anchor.y());
        return scale * geometry;
    }
    static bool finite(QPointF point) {
        return std::isfinite(point.x()) && std::isfinite(point.y());
    }
};
