#include "canvas.h"
#include <QBitArray>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QtMath>
#include <cmath>

Canvas::Canvas(Document *d, QWidget *parent) : QWidget(parent), document(d) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(220, 220);
    setObjectName("canvas");
    connect(d, &Document::updated, this, [this] {
        cacheDirty = true;
        update();
    });
}
void Canvas::setUiColors(QColor canvas, QColor panel, QColor text, QColor accent) {
    canvasColor = canvas;
    rulerColor = panel;
    rulerText = text;
    accentColor = accent;
    update();
}
void Canvas::setTool(Tool value) {
    finishPendingGesture();
    tool = value;
    updateCursor();
    update();
}
bool Canvas::isSelectionTool() const {
    return tool == Select || tool == EllipseSelect || tool == Lasso || tool == PolygonLasso ||
           tool == Wand;
}
Selection::Mode Canvas::effectiveSelectionMode(Qt::KeyboardModifiers modifiers) const {
    const bool shift = modifiers & Qt::ShiftModifier, alt = modifiers & Qt::AltModifier;
    return shift && alt ? Selection::Intersect
           : shift      ? Selection::Add
           : alt        ? Selection::Subtract
                        : selectionMode;
}
void Canvas::selectArea(const Selection &area) {
    document->selection = selectionBefore.combined(area, gestureSelectionMode, document->state.size);
    selectionBefore = {};
    selectionPoints.clear();
    document->changed();
}
void Canvas::completePolygon() {
    if (!polygonActive)
        return;
    polygonActive = false;
    if (selectionPoints.size() >= 3) {
        QPainterPath path;
        path.addPolygon(QPolygonF(selectionPoints));
        path.closeSubpath();
        selectArea(Selection::fromPath(path, document->state.size));
    } else {
        document->selection = selectionBefore;
        selectionBefore = {};
        selectionPoints.clear();
        update();
    }
}
void Canvas::updateCursor() {
    setCursor(tool == Hand ? Qt::OpenHandCursor : (tool == Move ? Qt::ArrowCursor : Qt::CrossCursor));
}
QPointF Canvas::toImage(QPointF p) const {
    return (p - pan) / zoom;
}
QPointF Canvas::toWidget(QPointF p) const {
    return p * zoom + pan;
}
void Canvas::fit() {
    zoom = qBound(.02,
                  qMin(qreal(width() - 100) / document->state.size.width(),
                       qreal(height() - 100) / document->state.size.height()),
                  4.0);
    pan = {(width() - document->state.size.width() * zoom) / 2,
           (height() - document->state.size.height() * zoom) / 2};
    fitted = true;
    update();
    emit zoomChanged();
}
void Canvas::actualSize() {
    setZoom(1);
}
void Canvas::setZoom(qreal z, QPointF anchor) {
    if (anchor.isNull())
        anchor = QPointF(width() / 2.0, height() / 2.0);
    QPointF pixel = toImage(anchor);
    zoom = qBound(.02, z, 16.0);
    pan = anchor - pixel * zoom;
    fitted = false;
    update();
    emit zoomChanged();
}
void Canvas::resizeEvent(QResizeEvent *) {
    if (fitted || pan.isNull())
        fit();
}
void Canvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), canvasColor);
    QRectF imageRect(pan, QSizeF(document->state.size) * zoom);
    p.fillRect(imageRect.adjusted(-5, -5, 8, 8), QColor(0, 0, 0, 55));
    p.save();
    p.setClipRect(imageRect);
    p.fillRect(imageRect, QColor("#363b42"));
    const int step = 12;
    for (int y = qMax(24, int(imageRect.top())); y < qMin(height(), int(imageRect.bottom() + 1));
         y += step)
        for (int x = qMax(24, int(imageRect.left())); x < qMin(width(), int(imageRect.right() + 1));
             x += step)
            if (((x / step) + (y / step)) % 2 == 0)
                p.fillRect(x, y, step, step, QColor("#41464d"));
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom < 1);
    if (cacheDirty) {
        compositeCache = document->composite();
        cacheDirty = false;
    }
    p.drawImage(imageRect, compositeCache);
    p.restore();
    p.save();
    p.translate(pan);
    p.scale(zoom, zoom);
    const QRect selection = document->selection.bounds();
    if (document->selection.hasPixels()) {
        QImage outline = document->selection.boundaryImage();
        if (!outline.isNull())
            p.drawImage(selection.topLeft(), outline);
        else {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(Qt::black, 1 / zoom));
            p.drawRect(selection);
            p.setPen(QPen(Qt::white, 1 / zoom, Qt::DashLine));
            p.drawRect(selection);
        }
    }
    if (dragging && (tool == Select || tool == Crop || tool == EllipseSelect)) {
        QRectF area = preview.normalized();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 1 / zoom, Qt::DashLine));
        if (tool == EllipseSelect)
            p.drawEllipse(area);
        else
            p.drawRect(area);
        if (tool == Crop) {
            QPainterPath shade;
            shade.setFillRule(Qt::OddEvenFill);
            shade.addRect(QRectF(QPoint(), document->state.size));
            shade.addRect(area);
            p.fillPath(shade, QColor(0, 0, 0, 145));
        }
    }
    if ((dragging && tool == Lasso) || polygonActive) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 1 / zoom, Qt::DashLine));
        if (selectionPoints.size() > 1)
            p.drawPolyline(QPolygonF(selectionPoints));
        if (!selectionPoints.isEmpty())
            p.drawLine(selectionPoints.last(), hover);
    }
    if (dragging && (tool == Rectangle || tool == Ellipse)) {
        p.setBrush(foreground);
        p.setOpacity(brushOpacity / 100.0);
        p.setPen(Qt::NoPen);
        if (tool == Rectangle)
            p.drawRect(preview.normalized());
        else
            p.drawEllipse(preview.normalized());
    }
    if (dragging && tool == Gradient) {
        p.setPen(QPen(Qt::white, 1 / zoom, Qt::DashLine));
        p.drawLine(press, last);
    }
    if (tool == Brush || tool == Pencil || tool == Eraser || tool == Clone) {
        p.save();
        p.setTransform(tipTransform(hover), true);
        const qreal radius = stampDiameter(inputPressure) / 2;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 180), 2 / zoom));
        p.drawEllipse(QPointF(), radius, radius);
        p.setPen(QPen(Qt::white, 1 / zoom));
        p.drawEllipse(QPointF(), radius, radius);
        p.restore();
    }
    if (tool == Clone && hasClone) {
        p.setPen(QPen(accentColor, 1 / zoom));
        p.drawLine(cloneAnchor + QPoint(-8, 0), cloneAnchor + QPoint(8, 0));
        p.drawLine(cloneAnchor + QPoint(0, -8), cloneAnchor + QPoint(0, 8));
    }
    p.restore();
    if (!showRulers)
        return;
    // Rulers stay readable at any zoom.
    p.fillRect(0, 0, width(), 24, rulerColor);
    p.fillRect(0, 0, 24, height(), rulerColor);
    p.setPen(rulerText);
    QFont f = font();
    f.setPixelSize(9);
    p.setFont(f);
    int stride = zoom >= 2 ? 25 : (zoom >= .5 ? 100 : (zoom >= .15 ? 500 : 2000));
    for (int x = 0; x <= document->state.size.width(); x += stride) {
        qreal wx = toWidget({qreal(x), 0}).x();
        if (wx < 26 || wx > width())
            continue;
        p.drawLine(QPointF(wx, 17), QPointF(wx, 24));
        p.drawText(QPointF(wx + 3, 12), QString::number(x));
    }
    for (int y = 0; y <= document->state.size.height(); y += stride) {
        qreal wy = toWidget({0, qreal(y)}).y();
        if (wy < 28 || wy > height())
            continue;
        p.drawLine(QPointF(17, wy), QPointF(24, wy));
        p.save();
        p.translate(12, wy - 3);
        p.rotate(-90);
        p.drawText(0, 0, QString::number(y));
        p.restore();
    }
    p.fillRect(0, 0, 24, 24, rulerColor);
}
QImage &Canvas::target() {
    auto &l = document->state.layers[document->state.active];
    return document->editingMask ? l.mask : l.pixels;
}
qreal Canvas::stampDiameter(qreal pressure) const {
    qreal scale = 1;
    if (tabletActive && pressureSize) {
        const qreal minimum = qBound(1, pressureMinimum, 100) / 100.0;
        const qreal curve = std::isfinite(pressureCurve) ? qBound(.25, pressureCurve, 4.0) : 1;
        scale = minimum + (1 - minimum) * std::pow(qBound(0.0, pressure, 1.0), curve);
    }
    return qMax(1.0, qBound(1, brushSize, 512) * scale);
}
QTransform Canvas::tipTransform(QPointF position) const {
    qreal angle = qBound(-180, brushAngle, 180);
    qreal roundness = qBound(5, brushRoundness, 100) / 100.0;
    if (tabletActive) {
        if (tiltAngle && std::hypot(tabletTiltX, tabletTiltY) > .01)
            angle += qRadiansToDegrees(std::atan2(tabletTiltY, tabletTiltX));
        if (tiltRoundness)
            roundness *= qBound(
                .05, std::cos(qDegreesToRadians(qMin(85.0, std::hypot(tabletTiltX, tabletTiltY)))), 1.0);
    }
    QTransform tip;
    tip.translate(position.x(), position.y());
    tip.rotate(angle);
    tip.scale(1, roundness);
    return tip;
}
void Canvas::stamp(QPointF position, qreal pressure) {
    if (tabletActive && pressure <= 0)
        return;
    auto &l = document->state.layers[document->state.active];
    const QPointF at = position - l.offset;
    const qreal radius = stampDiameter(pressure) / 2;
    const QTransform tip = tipTransform(at);
    const bool erase = tool == Eraser || tabletEraser;
    qreal flow = qBound(1, brushOpacity, 100) / 100.0;
    if (tabletActive && pressureFlow) {
        const qreal curve = std::isfinite(pressureCurve) ? qBound(.25, pressureCurve, 4.0) : 1;
        flow *= std::pow(qBound(0.0, pressure, 1.0), curve);
    }
    const QRect affected =
        QRectF(at - QPointF(radius + 2, radius + 2), QSizeF(2 * radius + 4, 2 * radius + 4))
            .toAlignedRect();
    const bool edited = document->selection.paint(
        target(), l.offset,
        [&](QPainter &p) {
            p.setRenderHint(QPainter::Antialiasing, tool != Pencil);
            p.setOpacity(flow);
            QColor color = foreground;
            if (document->editingMask) {
                const int gray = erase ? 0 : qGray(color.rgb());
                color = QColor(gray, gray, gray);
            } else if (erase) {
                color = Qt::white;
                p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            }
            if (tool == Clone && !erase) {
                QPainterPath clip;
                clip.addEllipse(QPointF(), radius, radius);
                p.setClipPath(tip.map(clip), Qt::IntersectClip);
                const QPointF source = QPointF(cloneAnchor - l.offset) + (position - press);
                p.drawImage(at - QPointF(radius, radius), cloneSource,
                            QRectF(source - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius)));
            } else {
                p.setTransform(tip, true);
                p.setPen(Qt::NoPen);
                if (hardness >= 100 || tool == Pencil)
                    p.setBrush(color);
                else {
                    QRadialGradient gradient(QPointF(), radius);
                    gradient.setColorAt(0, color);
                    gradient.setColorAt(qBound(0, hardness, 99) / 100.0, color);
                    color.setAlpha(0);
                    gradient.setColorAt(1, color);
                    p.setBrush(gradient);
                }
                p.drawEllipse(QPointF(), radius, radius);
            }
        },
        affected);
    if (edited && !document->editingMask)
        l.text.clear();
}
void Canvas::stroke(QPointF from, QPointF to, bool final) {
    const qreal distance = QLineF(from, to).length();
    const auto place = [&](QPointF at, qreal pressure) {
        stamp(at, pressure);
        lastStamp = at;
        hasStrokeStamp = true;
        remainingStampDistance =
            qMax(1.0, stampDiameter(pressure) * qBound(1, brushSpacing, 200) / 100.0);
    };
    if (!hasStrokeStamp)
        place(from, lastInputPressure);
    qreal travelled = 0;
    while (distance - travelled + .000001 >= remainingStampDistance) {
        travelled += remainingStampDistance;
        const qreal t = qMin(1.0, travelled / distance);
        place(from + (to - from) * t, lastInputPressure + (inputPressure - lastInputPressure) * t);
    }
    remainingStampDistance -= qMax(0.0, distance - travelled);
    if (final && QLineF(lastStamp, to).length() > .001)
        place(to, inputPressure);
    document->changed();
}
void Canvas::fillAt(QPoint docPoint) {
    auto &l = document->state.layers[document->state.active];
    QPoint seed = docPoint - l.offset;
    QImage &im = target();
    QRect bounds = im.rect();
    if (!document->selection.isEmpty())
        bounds = bounds.intersected(document->selection.bounds().translated(-l.offset));
    if (!bounds.contains(seed) || document->selection.valueAt(docPoint) == 0)
        return;
    QRgb old = im.pixel(seed);
    QColor color = foreground;
    if (document->editingMask) {
        int g = qGray(color.rgb());
        color = {g, g, g};
    }
    QImage swatch(1, 1, QImage::Format_ARGB32_Premultiplied);
    swatch.fill(old);
    {
        QPainter blend(&swatch);
        blend.setOpacity(brushOpacity / 100.0);
        blend.fillRect(swatch.rect(), color);
    }
    QRgb replacement = swatch.pixel(0, 0);
    if (old == replacement)
        return;
    document->beginEdit();
    QBitArray visited(im.width() * im.height());
    bool edited = false;
    auto available = [&](int x, int y) {
        return !visited.testBit(y * im.width() + x) && im.pixel(x, y) == old &&
               document->selection.valueAt(QPoint(x, y) + l.offset) > 0;
    };
    QVector<QPoint> pending{seed};
    while (!pending.isEmpty()) {
        QPoint at = pending.takeLast();
        auto *row = reinterpret_cast<QRgb *>(im.scanLine(at.y()));
        if (!available(at.x(), at.y()))
            continue;
        int left = at.x(), right = at.x();
        while (left > bounds.left() && available(left - 1, at.y()))
            --left;
        while (right < bounds.right() && available(right + 1, at.y()))
            ++right;
        for (int x = left; x <= right; ++x) {
            visited.setBit(at.y() * im.width() + x);
            QRgb mixed = document->selection.mixPixel(row[x], replacement, QPoint(x, at.y()) + l.offset);
            edited |= mixed != row[x];
            row[x] = mixed;
        }
        for (int y : {at.y() - 1, at.y() + 1})
            if (y >= bounds.top() && y <= bounds.bottom()) {
                bool span = false;
                for (int x = left; x <= right; ++x) {
                    if (available(x, y)) {
                        if (!span)
                            pending.append({x, y});
                        span = true;
                    } else
                        span = false;
                }
            }
    }
    if (edited && !document->editingMask)
        l.text.clear();
    document->commit("Boya kovası");
}
void Canvas::mousePressEvent(QMouseEvent *e) {
    setFocus();
    hover = toImage(e->position());
    press = last = hover;
    widgetLast = e->position();
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && tool == Hand)) {
        panning = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (tool == Picker) {
        QImage image = document->composite();
        if (image.rect().contains(QPoint(qFloor(hover.x()), qFloor(hover.y()))))
            emit colorPicked(image.pixelColor(QPoint(qFloor(hover.x()), qFloor(hover.y()))));
        return;
    }
    if (tool == Text) {
        emit textRequested(QPoint(qFloor(hover.x()), qFloor(hover.y())));
        return;
    }
    if (tool == Clone && (e->modifiers() & Qt::AltModifier)) {
        cloneAnchor = QPoint(qFloor(hover.x()), qFloor(hover.y()));
        hasClone = true;
        emit toolHint("Klon kaynağı seçildi. Boyamak için sürükleyin.");
        update();
        return;
    }
    if (tool == PolygonLasso) {
        if (!polygonActive) {
            polygonActive = true;
            selectionBefore = document->selection;
            gestureSelectionMode = effectiveSelectionMode(e->modifiers());
            selectionPoints.clear();
        }
        if (selectionPoints.size() < 16384 &&
            (selectionPoints.isEmpty() || QLineF(selectionPoints.last(), hover).length() > .5))
            selectionPoints.append(hover);
        emit toolHint(
            "Çokgen kement: köşelere tıklayın; Enter veya çift tıklama bitirir, Esc iptal eder.");
        update();
        return;
    }
    if (tool == Wand) {
        selectionBefore = document->selection;
        gestureSelectionMode = effectiveSelectionMode(e->modifiers());
        const QImage image = document->composite();
        QPoint seed(qFloor(hover.x()), qFloor(hover.y()));
        if (!image.rect().contains(seed)) {
            selectionBefore = {};
            return;
        }
        selectArea(Selection::colorRange(image, seed, selectionTolerance, contiguousSelection));
        return;
    }
    if (isSelectionTool() || tool == Crop) {
        dragging = true;
        preview = QRectF(press, QSizeF());
        selectionBefore = document->selection;
        gestureSelectionMode =
            tool == Crop ? Selection::Replace : effectiveSelectionMode(e->modifiers());
        selectionPoints = {press};
        update();
        return;
    }
    auto &l = document->state.layers[document->state.active];
    if (l.locked || !l.visible) {
        emit toolHint("Düzenlemek için görünür, kilidi açık bir katman seçin.");
        return;
    }
    if (!l.geometry.isIdentity() && tool != Move) {
        emit toolHint(
            "Bu katmanın dönüşümü kaynak korur. Boyamak için Katman → Görünümü rasterleştir kullanın.");
        return;
    }
    if (tool == Clone && !tabletEraser && (document->editingMask || !hasClone)) {
        emit toolHint("Klonlama: önce Alt + tıklama ile piksel katmanından kaynak seçin.");
        return;
    }
    if (tool == Fill) {
        fillAt(QPoint(qFloor(hover.x()), qFloor(hover.y())));
        return;
    }
    document->beginEdit();
    hasStrokeStamp = false;
    lastInputPressure = inputPressure;
    dragging = true;
    initialOffset = l.offset;
    preview = QRectF(press, QSizeF());
    if (tool == Clone)
        cloneSource = l.pixels;
    if (tool == Brush || tool == Pencil || tool == Eraser || tool == Clone)
        stroke(press, press);
}
void Canvas::mouseMoveEvent(QMouseEvent *e) {
    hover = toImage(e->position());
    if (panning) {
        pan += e->position() - widgetLast;
        widgetLast = e->position();
        fitted = false;
        update();
        return;
    }
    if (dragging)
        advanceGesture(hover, e->modifiers());
    else
        update();
}
void Canvas::advanceGesture(QPointF position, Qt::KeyboardModifiers modifiers) {
    if (!dragging)
        return;
    hover = position;
    if (tool == Move) {
        auto &l = document->state.layers[document->state.active];
        QPoint offset = initialOffset + (hover - press).toPoint();
        Layer candidate = l;
        candidate.offset = {qBound(-Document::MaxCoordinate, offset.x(), Document::MaxCoordinate),
                            qBound(-Document::MaxCoordinate, offset.y(), Document::MaxCoordinate)};
        if (!candidate.validGeometry()) {
            emit toolHint("Katman konumu güvenli koordinat sınırını aşıyor.");
            return;
        }
        l.offset = candidate.offset;
        document->changed();
    } else if (tool == Brush || tool == Pencil || tool == Eraser || tool == Clone) {
        if (QLineF(last, hover).length() > .001)
            stroke(last, hover);
        else if (tabletActive && (pressureSize || pressureFlow) &&
                 qAbs(inputPressure - lastInputPressure) > .001) {
            stamp(hover, inputPressure);
            lastStamp = hover;
            remainingStampDistance =
                qMax(1.0, stampDiameter(inputPressure) * qBound(1, brushSpacing, 200) / 100.0);
            document->changed();
        }
    } else if (tool == Lasso) {
        if (selectionPoints.size() < 16384 && QLineF(selectionPoints.last(), hover).length() > .5)
            selectionPoints.append(hover);
        update();
    } else {
        preview = QRectF(press, hover);
        if ((modifiers & Qt::ShiftModifier) && (tool == Rectangle || tool == Ellipse)) {
            qreal side = qMax(qAbs(hover.x() - press.x()), qAbs(hover.y() - press.y()));
            preview.setBottomRight(press + QPointF(hover.x() < press.x() ? -side : side,
                                                   hover.y() < press.y() ? -side : side));
        }
        update();
    }
    last = hover;
    lastInputPressure = inputPressure;
}

void Canvas::drawShape() {
    QRectF r = preview.normalized();
    if (r.width() < 1 || r.height() < 1)
        return;
    auto &l = document->state.layers[document->state.active];
    bool edited = document->selection.paint(target(), l.offset, [&](QPainter &p) {
        p.setRenderHint(QPainter::Antialiasing);
        QColor c = foreground;
        if (document->editingMask) {
            int g = qGray(c.rgb());
            c = {g, g, g};
        }
        p.setOpacity(brushOpacity / 100.0);
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        r.translate(-l.offset);
        if (tool == Rectangle)
            p.drawRect(r);
        else
            p.drawEllipse(r);
    });
    if (edited && !document->editingMask)
        l.text.clear();
}
void Canvas::applyGradient() {
    if (QLineF(press, last).length() < 1)
        return;
    auto &l = document->state.layers[document->state.active];
    bool edited = document->selection.paint(target(), l.offset, [&](QPainter &p) {
        QLinearGradient g(press - l.offset, last - l.offset);
        g.setColorAt(0, foreground);
        g.setColorAt(1, background);
        if (document->editingMask) {
            g.setColorAt(
                0, QColor(qGray(foreground.rgb()), qGray(foreground.rgb()), qGray(foreground.rgb())));
            g.setColorAt(
                1, QColor(qGray(background.rgb()), qGray(background.rgb()), qGray(background.rgb())));
        }
        p.setOpacity(brushOpacity / 100.0);
        p.fillRect(target().rect(), g);
    });
    if (edited && !document->editingMask)
        l.text.clear();
}
bool Canvas::finishPendingGesture() {
    if (polygonActive) {
        completePolygon();
        tabletActive = tabletEraser = false;
        inputPressure = lastInputPressure = 1;
        return true;
    }
    if (panning) {
        panning = false;
        updateCursor();
    }
    if (!dragging) {
        tabletActive = tabletEraser = false;
        inputPressure = lastInputPressure = 1;
        return false;
    }
    const bool erasing = (tool == Brush || tool == Pencil || tool == Eraser || tool == Clone) &&
                         (tool == Eraser || tabletEraser);
    if (tool == Brush || tool == Pencil || tool == Eraser || tool == Clone)
        stroke(last, last, true);
    tabletActive = tabletEraser = false;
    inputPressure = lastInputPressure = 1;
    dragging = false;
    if (isSelectionTool() || tool == Crop) {
        if (tool == Lasso) {
            if (selectionPoints.size() >= 3) {
                QPainterPath path;
                path.addPolygon(QPolygonF(selectionPoints));
                path.closeSubpath();
                selectArea(Selection::fromPath(path, document->state.size));
            } else {
                document->selection = selectionBefore;
                selectionBefore = {};
                selectionPoints.clear();
                document->changed();
            }
        } else if (tool == EllipseSelect) {
            QPainterPath path;
            path.addEllipse(preview.normalized());
            selectArea(Selection::fromPath(path, document->state.size));
        } else {
            selectArea(Selection(preview.normalized().toAlignedRect().intersected(
                QRect(QPoint(), document->state.size))));
        }
        return true;
    }
    if (tool == Rectangle || tool == Ellipse)
        drawShape();
    if (tool == Gradient)
        applyGradient();
    QString label = tool == Move                           ? "Katman taşındı"
                    : erasing                              ? "Silgi"
                    : tool == Clone                        ? "Klon damgası"
                    : tool == Gradient                     ? "Degrade"
                    : tool == Rectangle || tool == Ellipse ? "Şekil çizildi"
                                                           : "Fırça";
    document->commit(label);
    cloneSource = {};
    tabletActive = tabletEraser = false;
    inputPressure = lastInputPressure = 1;
    return true;
}
void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && panning)) {
        pan += e->position() - widgetLast;
        widgetLast = e->position();
        panning = false;
        fitted = false;
        updateCursor();
        update();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        if (tool == PolygonLasso && polygonActive)
            return;
        advanceGesture(toImage(e->position()), e->modifiers());
        finishPendingGesture();
    }
}
void Canvas::tabletEvent(QTabletEvent *e) {
    if (tool != Brush && tool != Pencil && tool != Eraser && tool != Clone) {
        // Qt already emulates mouse gestures (including polygon double-clicks)
        // for these tools. Keep that platform behavior instead of consuming it.
        QWidget::tabletEvent(e);
        return;
    }
    const QPointF position = e->position();
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()) || !std::isfinite(e->pressure()) ||
        !std::isfinite(e->xTilt()) || !std::isfinite(e->yTilt()) ||
        qAbs(toImage(position).x()) > Document::MaxCoordinate ||
        qAbs(toImage(position).y()) > Document::MaxCoordinate) {
        e->accept();
        return;
    }
    if (e->type() == QEvent::TabletPress) {
        finishPendingGesture();
        tabletActive = true;
        tabletEraser = e->pointerType() == QPointingDevice::PointerType::Eraser;
    } else if (dragging && !tabletActive) {
        e->accept();
        return;
    }
    inputPressure = e->type() == QEvent::TabletRelease && e->pressure() <= 0
                        ? lastInputPressure
                        : qBound(0.0, e->pressure(), 1.0);
    tabletTiltX = qBound(-60.0, double(e->xTilt()), 60.0);
    tabletTiltY = qBound(-60.0, double(e->yTilt()), 60.0);
    const QEvent::Type type = e->type() == QEvent::TabletPress     ? QEvent::MouseButtonPress
                              : e->type() == QEvent::TabletRelease ? QEvent::MouseButtonRelease
                                                                   : QEvent::MouseMove;
    QMouseEvent pointer(type, position, e->globalPosition(), e->button(), e->buttons(), e->modifiers(),
                        e->pointingDevice());
    if (type == QEvent::MouseButtonPress)
        mousePressEvent(&pointer);
    else if (type == QEvent::MouseButtonRelease) {
        mouseReleaseEvent(&pointer);
        tabletActive = tabletEraser = false;
        inputPressure = lastInputPressure = 1;
    } else
        mouseMoveEvent(&pointer);
    // Accept native tablet events so Qt does not synthesize a second mouse stroke.
    e->accept();
}
void Canvas::wheelEvent(QWheelEvent *e) {
    setZoom(zoom * std::pow(1.0015, e->angleDelta().y()), e->position());
    e->accept();
}
void Canvas::keyPressEvent(QKeyEvent *e) {
    if (polygonActive && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        completePolygon();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        if (polygonActive || (dragging && isSelectionTool())) {
            polygonActive = dragging = false;
            document->selection = selectionBefore;
            selectionBefore = {};
            selectionPoints.clear();
            document->changed();
            e->accept();
            return;
        }
        if (dragging) {
            dragging = false;
            document->cancelEdit();
            tabletActive = tabletEraser = false;
            inputPressure = lastInputPressure = 1;
        }
        document->selection = {};
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}
void Canvas::mouseDoubleClickEvent(QMouseEvent *e) {
    if (tool == PolygonLasso && e->button() == Qt::LeftButton) {
        hover = toImage(e->position());
        if (polygonActive && selectionPoints.size() < 16384 &&
            QLineF(selectionPoints.last(), hover).length() > .5)
            selectionPoints.append(hover);
        completePolygon();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}
void Canvas::focusOutEvent(QFocusEvent *event) {
    finishPendingGesture();
    QWidget::focusOutEvent(event);
}
void Canvas::deleteSelection() {
    auto &l = document->state.layers[document->state.active];
    if (l.locked)
        return;
    if (!l.geometry.isIdentity()) {
        emit toolHint("Silmek için önce Katman → Görünümü rasterleştir kullanın.");
        return;
    }
    document->beginEdit();
    bool edited = document->selection.paint(target(), l.offset, [&](QPainter &p) {
        QRect region = document->selection.isEmpty() ? QRect(QPoint(), document->state.size)
                                                     : document->selection.bounds();
        region.translate(-l.offset);
        if (document->editingMask)
            p.fillRect(region, Qt::black);
        else {
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.fillRect(region, Qt::transparent);
        }
    });
    if (edited && !document->editingMask)
        l.text.clear();
    document->commit("Seçim silindi");
}
