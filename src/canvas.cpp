#include "canvas.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QRadialGradient>
#include <QWheelEvent>
#include <QtMath>

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
    auto selection = document->selection;
    if (dragging && (tool == Select || tool == Crop))
        selection =
            preview.normalized().toAlignedRect().intersected(QRect(QPoint(), document->state.size));
    if (!selection.isEmpty()) {
        if (tool == Crop) {
            QPainterPath shade;
            shade.setFillRule(Qt::OddEvenFill);
            shade.addRect(QRectF(QPoint(), document->state.size));
            shade.addRect(selection);
            p.fillPath(shade, QColor(0, 0, 0, 145));
        }
        p.setPen(QPen(Qt::black, 1 / zoom));
        p.setBrush(Qt::NoBrush);
        p.drawRect(selection);
        QPen pen(Qt::white, 1 / zoom, Qt::DashLine);
        p.setPen(pen);
        p.drawRect(selection);
        if (tool == Crop) {
            p.setPen(QPen(QColor(255, 255, 255, 90), 1 / zoom));
            for (int i = 1; i < 3; ++i) {
                p.drawLine(QPointF(selection.left() + selection.width() * i / 3.0, selection.top()),
                           QPointF(selection.left() + selection.width() * i / 3.0, selection.bottom()));
                p.drawLine(QPointF(selection.left(), selection.top() + selection.height() * i / 3.0),
                           QPointF(selection.right(), selection.top() + selection.height() * i / 3.0));
            }
        }
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
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 180), 2 / zoom));
        p.drawEllipse(hover, brushSize / 2.0, brushSize / 2.0);
        p.setPen(QPen(Qt::white, 1 / zoom));
        p.drawEllipse(hover, brushSize / 2.0, brushSize / 2.0);
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
void Canvas::stroke(QPointF from, QPointF to) {
    auto &l = document->state.layers[document->state.active];
    QPointF a = from - l.offset, b = to - l.offset;
    QPainter p(&target());
    p.setRenderHint(QPainter::Antialiasing, tool != Pencil);
    if (!document->selection.isEmpty())
        p.setClipRect(document->selection.translated(-l.offset));
    p.setOpacity(brushOpacity / 100.0);
    QColor color = foreground;
    if (document->editingMask) {
        int gray = tool == Eraser ? 0 : qGray(color.rgb());
        color = QColor(gray, gray, gray);
    } else if (tool == Eraser)
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    qreal distance = QLineF(a, b).length();
    int count = qMax(1, qCeil(distance / qMax(1.0, brushSize * .16)));
    for (int i = 0; i <= count; ++i) {
        QPointF at = a + (b - a) * (qreal(i) / count);
        qreal radius = brushSize / 2.0;
        if (tool == Clone) {
            QPointF source = QPointF(cloneAnchor - l.offset) + (at - (press - l.offset));
            QPainterPath clip;
            clip.addEllipse(at, radius, radius);
            p.save();
            p.setClipPath(clip, Qt::IntersectClip);
            p.drawImage(at - QPointF(radius, radius), cloneSource,
                        QRectF(source - QPointF(radius, radius), QSizeF(brushSize, brushSize)));
            p.restore();
        } else {
            p.setPen(Qt::NoPen);
            if (hardness >= 100 || tool == Pencil) {
                p.setBrush(color);
            } else {
                QRadialGradient g(at, radius);
                g.setColorAt(0, color);
                g.setColorAt(hardness / 100.0, color);
                QColor edge = color;
                edge.setAlpha(0);
                g.setColorAt(1, edge);
                p.setBrush(g);
            }
            p.drawEllipse(at, radius, radius);
        }
    }
    p.end();
    l.text.clear();
    document->changed();
}
void Canvas::fillAt(QPoint docPoint) {
    auto &l = document->state.layers[document->state.active];
    QPoint seed = docPoint - l.offset;
    QImage &im = target();
    QRect bounds = im.rect();
    if (!document->selection.isEmpty())
        bounds = bounds.intersected(document->selection.translated(-l.offset));
    if (!bounds.contains(seed))
        return;
    QRgb old = im.pixel(seed);
    QColor color = foreground;
    if (document->editingMask) {
        int g = qGray(color.rgb());
        color = {g, g, g};
    }
    color.setAlphaF(color.alphaF() * brushOpacity / 100.0);
    QRgb replacement = qPremultiply(color.rgba());
    if (old == replacement)
        return;
    document->beginEdit();
    QVector<QPoint> pending{seed};
    while (!pending.isEmpty()) {
        QPoint at = pending.takeLast();
        auto *row = reinterpret_cast<QRgb *>(im.scanLine(at.y()));
        if (row[at.x()] != old)
            continue;
        int left = at.x(), right = at.x();
        while (left > bounds.left() && row[left - 1] == old)
            --left;
        while (right < bounds.right() && row[right + 1] == old)
            ++right;
        for (int x = left; x <= right; ++x)
            row[x] = replacement;
        for (int y : {at.y() - 1, at.y() + 1})
            if (y >= bounds.top() && y <= bounds.bottom()) {
                const auto *next = reinterpret_cast<const QRgb *>(im.constScanLine(y));
                bool span = false;
                for (int x = left; x <= right; ++x) {
                    if (next[x] == old) {
                        if (!span)
                            pending.append({x, y});
                        span = true;
                    } else
                        span = false;
                }
            }
    }
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
        if (image.rect().contains(hover.toPoint()))
            emit colorPicked(image.pixelColor(hover.toPoint()));
        return;
    }
    if (tool == Text) {
        emit textRequested(hover.toPoint());
        return;
    }
    if (tool == Clone && (e->modifiers() & Qt::AltModifier)) {
        cloneAnchor = hover.toPoint();
        hasClone = true;
        emit toolHint("Klon kaynağı seçildi. Boyamak için sürükleyin.");
        update();
        return;
    }
    if (tool == Select || tool == Crop) {
        dragging = true;
        preview = QRectF(press, QSizeF());
        document->selection = {};
        update();
        return;
    }
    auto &l = document->state.layers[document->state.active];
    if (l.locked || !l.visible) {
        emit toolHint("Düzenlemek için görünür, kilidi açık bir katman seçin.");
        return;
    }
    if (tool == Clone && (document->editingMask || !hasClone)) {
        emit toolHint("Klonlama: önce Alt + tıklama ile piksel katmanından kaynak seçin.");
        return;
    }
    if (tool == Fill) {
        fillAt(hover.toPoint());
        return;
    }
    document->beginEdit();
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
    if (!dragging) {
        update();
        return;
    }
    if (tool == Move) {
        auto &l = document->state.layers[document->state.active];
        QPoint off = initialOffset + (hover - press).toPoint();
        l.offset = {qBound(-32768, off.x(), 32768), qBound(-32768, off.y(), 32768)};
        document->changed();
    } else if (tool == Brush || tool == Pencil || tool == Eraser || tool == Clone)
        stroke(last, hover);
    else {
        preview = QRectF(press, hover);
        if ((e->modifiers() & Qt::ShiftModifier) && (tool == Rectangle || tool == Ellipse)) {
            qreal side = qMax(qAbs(hover.x() - press.x()), qAbs(hover.y() - press.y()));
            preview.setBottomRight(press + QPointF(hover.x() < press.x() ? -side : side,
                                                   hover.y() < press.y() ? -side : side));
        }
        update();
    }
    last = hover;
}
void Canvas::drawShape() {
    QRectF r = preview.normalized();
    if (r.width() < 1 || r.height() < 1)
        return;
    auto &l = document->state.layers[document->state.active];
    QPainter p(&target());
    p.setRenderHint(QPainter::Antialiasing);
    if (!document->selection.isEmpty())
        p.setClipRect(document->selection.translated(-l.offset));
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
    p.end();
    l.text.clear();
}
void Canvas::applyGradient() {
    if (QLineF(press, last).length() < 1)
        return;
    auto &l = document->state.layers[document->state.active];
    QPainter p(&target());
    if (!document->selection.isEmpty())
        p.setClipRect(document->selection.translated(-l.offset));
    QLinearGradient g(press - l.offset, last - l.offset);
    g.setColorAt(0, foreground);
    g.setColorAt(1, background);
    if (document->editingMask) {
        g.setColorAt(0,
                     QColor(qGray(foreground.rgb()), qGray(foreground.rgb()), qGray(foreground.rgb())));
        g.setColorAt(1,
                     QColor(qGray(background.rgb()), qGray(background.rgb()), qGray(background.rgb())));
    }
    p.setOpacity(brushOpacity / 100.0);
    p.fillRect(target().rect(), g);
    p.end();
    l.text.clear();
}
bool Canvas::finishPendingGesture() {
    if (panning) {
        panning = false;
        updateCursor();
    }
    if (!dragging)
        return false;
    dragging = false;
    if (tool == Select || tool == Crop) {
        document->selection =
            preview.normalized().toAlignedRect().intersected(QRect(QPoint(), document->state.size));
        document->changed();
        return true;
    }
    if (tool == Rectangle || tool == Ellipse)
        drawShape();
    if (tool == Gradient)
        applyGradient();
    QString label = tool == Move                           ? "Katman taşındı"
                    : tool == Eraser                       ? "Silgi"
                    : tool == Clone                        ? "Klon damgası"
                    : tool == Gradient                     ? "Degrade"
                    : tool == Rectangle || tool == Ellipse ? "Şekil çizildi"
                                                           : "Fırça";
    document->commit(label);
    cloneSource = {};
    return true;
}
void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && panning)) {
        panning = false;
        updateCursor();
        return;
    }
    if (e->button() == Qt::LeftButton)
        finishPendingGesture();
}
void Canvas::wheelEvent(QWheelEvent *e) {
    setZoom(zoom * std::pow(1.0015, e->angleDelta().y()), e->position());
    e->accept();
}
void Canvas::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        if (dragging) {
            dragging = false;
            document->cancelEdit();
        }
        document->selection = {};
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}
void Canvas::deleteSelection() {
    auto &l = document->state.layers[document->state.active];
    if (l.locked)
        return;
    document->beginEdit();
    QPainter p(&target());
    QRect region =
        document->selection.isEmpty() ? QRect(QPoint(), document->state.size) : document->selection;
    region.translate(-l.offset);
    if (document->editingMask)
        p.fillRect(region, Qt::black);
    else {
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(region, Qt::transparent);
    }
    p.end();
    l.text.clear();
    document->commit("Seçim silindi");
}
