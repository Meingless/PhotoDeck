#include "canvas.h"
#include "transformdialog.h"
#include <QBitArray>
#include <QDialogButtonBox>
#include <QCursor>
#include <QFontMetrics>
#include <QFocusEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRadialGradient>
#include <QTabletEvent>
#include <QScrollArea>
#include <QRegion>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>
#include <cmath>
#include <limits>

namespace {
double relativeLuminance(QColor color) {
    const auto linear = [](double channel) {
        return channel <= .04045 ? channel / 12.92 : std::pow((channel + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) +
           .0722 * linear(color.blueF());
}
double contrastRatio(QColor first, QColor second) {
    const double a = relativeLuminance(first), b = relativeLuminance(second);
    return (qMax(a, b) + .05) / (qMin(a, b) + .05);
}
QColor readableOverlayText(QColor preferred, QColor surface) {
    // Stylesheets render 8-bit RGB values; evaluate the exact colors they will paint.
    preferred = QColor(preferred.name());
    surface = QColor(surface.name());
    // Keep the established palette, including Focus, whenever it meets WCAG AA for small text.
    // Only the foreground changes; custom accent and surface colors remain exactly as chosen.
    if (contrastRatio(preferred, surface) >= 4.5)
        return preferred;
    return contrastRatio(Qt::black, surface) >= contrastRatio(Qt::white, surface)
               ? QColor(Qt::black)
               : QColor(Qt::white);
}
bool matchesTransformTarget(const Document *target, int layer, quint64 revision, QSize canvasSize,
                            qsizetype layerCount, const Layer &source) {
    if (!target || target->isEditing() || target->revision() != revision ||
        target->state.size != canvasSize || target->state.layers.size() != layerCount ||
        target->state.active != layer || layer < 0 || layer >= target->state.layers.size())
        return false;
    const Layer &a = target->state.layers[layer], &b = source;
    return a.pixels.cacheKey() == b.pixels.cacheKey() && a.mask.cacheKey() == b.mask.cacheKey() &&
           a.smartObjectId == b.smartObjectId && a.smartObjectSize == b.smartObjectSize &&
           a.geometry == b.geometry && a.offset == b.offset && a.visible == b.visible &&
           a.locked == b.locked && a.clipped == b.clipped && a.opacity == b.opacity &&
           a.blend == b.blend && a.brightness == b.brightness && a.contrast == b.contrast &&
           a.saturation == b.saturation && a.blur == b.blur && a.tones == b.tones &&
           a.filters == b.filters && a.maskSettings == b.maskSettings && a.text == b.text &&
           a.font == b.font && a.color == b.color;
}
} // namespace

Canvas::Canvas(Document *d, QWidget *parent) : QWidget(parent), document(d), keyboardDocument(d) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    // Leave room for compact controls without overrunning the short tab viewport
    // above Touch's bottom dock at the supported minimum window size.
    setMinimumSize(180, 100);
    setObjectName("canvas");
    transformControls = new QFrame(this);
    transformControls->setObjectName("transformControls");
    auto *controlsLayout = new QVBoxLayout(transformControls);
    controlsLayout->setContentsMargins(12, 9, 12, 9);
    controlsLayout->setSpacing(5);
    auto *buttons = new QGridLayout;
    buttons->setSpacing(6);
    transformCaption = new QLabel("DÖNÜŞTÜR");
    transformCaption->setObjectName("transformCaption");
    buttons->addWidget(transformCaption, 0, 0);
    buttons->setColumnStretch(1, 1);
    auto button = [buttons](QString name, QString text, QString help) {
        auto *control = new QToolButton;
        control->setObjectName(name);
        control->setText(text);
        control->setToolTip(help);
        control->setAccessibleName(help);
        control->setFocusPolicy(Qt::NoFocus);
        buttons->addWidget(control, 0, buttons->columnCount());
        return control;
    };
    transformNumeric = button("transformNumeric", "Sayısal…",
                              "Sayısal dönüşüm (Ctrl+Alt+T): ölçek, açı, eğme, aynalama ve perspektif");
    transformCancel = button("transformCancel", "İptal · Esc", "Dönüşümü iptal et (Esc)");
    transformApply = button("transformApply", "Uygula ↵", "Dönüşümü uygula (Enter)");
    controlsLayout->addLayout(buttons);
    transformInfo = new QLabel;
    transformInfo->setObjectName("transformInfo");
    transformInfo->setWordWrap(true);
    controlsLayout->addWidget(transformInfo);
    transformControls->hide();
    transformContext = new QFrame(this);
    transformContext->setObjectName("transformContext");
    auto *contextFrameLayout = new QVBoxLayout(transformContext);
    contextFrameLayout->setContentsMargins(0, 0, 0, 0);
    auto *contextScroll = new QScrollArea;
    contextScroll->setObjectName("transformContextScroll");
    contextScroll->setFrameShape(QFrame::NoFrame);
    contextScroll->setWidgetResizable(true);
    contextScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    transformContextContent = new QWidget;
    transformContextContent->setObjectName("transformContextContent");
    auto *contextLayout = new QVBoxLayout(transformContextContent);
    contextLayout->setContentsMargins(10, 7, 10, 7);
    contextLayout->setSpacing(4);
    transformScope = new QLabel;
    transformScope->setObjectName("transformScope");
    transformScope->setTextFormat(Qt::PlainText);
    transformScope->setWordWrap(true);
    transformScope->setMinimumWidth(0);
    transformScope->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    contextLayout->addWidget(transformScope);
    transformMetrics = new QLabel;
    transformMetrics->setObjectName("transformMetrics");
    transformMetrics->setTextFormat(Qt::PlainText);
    transformMetrics->setWordWrap(true);
    transformMetrics->setMinimumWidth(0);
    transformMetrics->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    transformCopySelection = new QPushButton("Seçimi kopyala ve dönüştür");
    transformCopySelection->setObjectName("transformCopySelection");
    transformCopySelection->setToolTip(
        "Seçili görünümü yeni bir piksel katmanına kopyala ve dönüştür. Özgün katman korunur; Esc kopyayı da iptal eder.");
    transformCopySelection->setAccessibleName("Seçimi yeni katmana kopyala ve dönüştür; özgün katman korunur");
    transformCopySelection->setFocusPolicy(Qt::StrongFocus);
    contextLayout->addWidget(transformCopySelection);
    contextLayout->addWidget(transformMetrics);
    auto *help = new QLabel("Köşe: oranı koru · Shift: serbest · Alt: merkez\n↻ Yuvarlak ok: döndür · Shift: 15°");
    help->setObjectName("transformHelp");
    help->setWordWrap(true);
    help->setMinimumWidth(0);
    help->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    contextLayout->addWidget(help);
    contextScroll->setWidget(transformContextContent);
    contextFrameLayout->addWidget(contextScroll);
    transformContext->hide();
    connect(transformCopySelection, &QPushButton::clicked, this, &Canvas::copySelectionAndTransform);
    connect(transformApply, &QToolButton::clicked, this, &Canvas::applyTransform);
    connect(transformCancel, &QToolButton::clicked, this, &Canvas::cancelTransform);
    connect(transformNumeric, &QToolButton::clicked, this, &Canvas::showNumericTransform);
    setUiColors(canvasColor, rulerColor, rulerText, accentColor);
    connect(d, &QObject::destroyed, this, [this] { endKeyboardNudge(); });
    connect(d, &Document::updated, this, [this] {
        if (!applyingNudge || ++nudgeUpdates != 1)
            endKeyboardNudge();
        else
            nudgeUpdateRevision = keyboardDocument ? keyboardDocument->revision() : 0;
        cacheDirty = true;
        if (!transformActive)
            updateCursor();
        update();
        // A preview belongs to one unchanged layer. Refresh/focus alone never dismisses it.
        if (transformActive && transformSourceMatches()) {
            updateTransformContext();
            positionTransformControls();
        }
        if (transformActive && !transformSourceMatches()) {
            const QPointer<Canvas> self(this);
            const quint64 ended = transformGeneration + 1;
            cancelTransform();
            if (self && transformGeneration == ended && !transformActive)
                emit toolHint("Belge değiştiği için dönüşüm önizlemesi kapatıldı.");
        }
    });
}
void Canvas::setUiColors(QColor canvas, QColor panel, QColor text, QColor accent) {
    canvasColor = canvas;
    rulerColor = panel;
    rulerText = text;
    accentColor = accent;
    const QColor buttonSurface = panel.lighter(135), hoverSurface = panel.lighter(160);
    const QColor infoText = readableOverlayText(text, panel);
    const QColor captionText = readableOverlayText(accent, panel);
    const QColor buttonText = readableOverlayText(text.lighter(160), buttonSurface);
    const QColor hoverText = readableOverlayText(text.lighter(160), hoverSurface);
    const QColor applyText = readableOverlayText(QColor("#15221f"), accent);
    transformControls->setStyleSheet(
        QString("QFrame#transformControls { background: %1; border: 1px solid %2; border-radius: 9px; }"
                "QFrame#transformControls QLabel { color: %3; border: none; background: transparent; }"
                "QLabel#transformCaption { color: %7; font-size: 11px; font-weight: 700; }"
                "QLabel#transformInfo { font-size: 10px; }"
                "QFrame#transformControls QToolButton { color: %4; background: %5; border: none; "
                "border-radius: 5px; padding: 6px 9px; font-size: 11px; }"
                "QFrame#transformControls QToolButton:hover { color: %9; background: %6; }"
                "QFrame#transformControls QToolButton#transformApply { color: %8; "
                "background: %2; font-weight: 700; }")
            .arg(panel.name(), accent.name(), infoText.name(), buttonText.name(),
                 buttonSurface.name(), hoverSurface.name(), captionText.name(), applyText.name(),
                 hoverText.name()));
    transformContext->setStyleSheet(
        QString("QFrame#transformContext { background: %1; border: 1px solid %2; border-radius: 7px; }"
                "QFrame#transformContext QWidget#transformContextContent, QFrame#transformContext QScrollArea, QFrame#transformContext QScrollArea > QWidget > QWidget { background: %1; border: none; }"
                "QFrame#transformContext QLabel { color: %3; border: none; background: transparent; font-size: 11px; }"
                "QFrame#transformContext QPushButton { color: %4; background: %5; border: none; border-radius: 5px; padding: 5px; font-size: 11px; }")
            .arg(panel.name(), accent.name(), infoText.name(), buttonText.name(), buttonSurface.name()));
    update();
}
void Canvas::setAutoSelectLayers(bool enabled) {
    autoSelectEnabled = enabled;
    updateCursor();
    update();
}
void Canvas::setShowLayerBounds(bool enabled) {
    layerBoundsVisible = enabled;
    update();
}
void Canvas::setTool(Tool value) {
    const QPointer<Canvas> self(this);
    finishPendingGesture();
    if (!self)
        return;
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
    const Selection result = selectionBefore.combined(area, gestureSelectionMode, document->state.size);
    selectionBefore = {};
    selectionPoints.clear();
    if (selectionRevision != document->revision()) {
        update();
        return; // A newer undo/edit superseded this unfinished gesture.
    }
    QString error;
    if (!document->setSelection(result, "Seçim yapıldı", &error))
        emit toolHint(error);
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
        selectionBefore = {};
        selectionPoints.clear();
        update();
    }
}
void Canvas::updateCursor() {
    if (transformActive) {
        updateTransformCursor(toWidget(hover));
        return;
    }
    setCursor(tool == Hand ? Qt::OpenHandCursor : (tool == Move ? Qt::ArrowCursor : Qt::CrossCursor));
    if (tool == Move) {
        const QString target = QString("Hedef: %1 · Tuvalden katman seçimi %2. %3")
                                   .arg(activeLayerTargetName(), autoSelectEnabled ? "açık" : "kapalı",
                                        autoSelectEnabled ? "Görünür, kilidi açık içeriğe tıklayıp sürükleyin."
                                                          : "Etkin katmanı taşımak için sürükleyin.");
        setToolTip(target);
        setAccessibleDescription(target);
    }
}
QPointF Canvas::toImage(QPointF p) const {
    return (p - pan) / zoom;
}
QPointF Canvas::toWidget(QPointF p) const {
    return p * zoom + pan;
}
void Canvas::fit() {
    transformDragging = false;
    zoom = qBound(.02,
                  qMin(qreal(width() - 100) / document->state.size.width(),
                       qreal(height() - 100) / document->state.size.height()),
                  4.0);
    pan = {(width() - document->state.size.width() * zoom) / 2,
           (height() - document->state.size.height() * zoom) / 2};
    fitted = true;
    positionTransformControls();
    update();
    emit zoomChanged();
}
void Canvas::actualSize() {
    setZoom(1);
}
void Canvas::setZoom(qreal z, QPointF anchor) {
    transformDragging = false;
    if (anchor.isNull())
        anchor = QPointF(width() / 2.0, height() / 2.0);
    QPointF pixel = toImage(anchor);
    zoom = qBound(.02, z, 16.0);
    pan = anchor - pixel * zoom;
    fitted = false;
    positionTransformControls();
    update();
    emit zoomChanged();
}
void Canvas::resizeEvent(QResizeEvent *) {
    if (transformActive)
        updateTransformContext();
    if (fitted || pan.isNull())
        fit();
    positionTransformControls();
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
        if (transformActive) {
            Document previewDocument;
            previewDocument.state = previewState();
            previewDocument.state.layers[previewDocument.state.active].geometry = transformPreview;
            compositeCache = previewDocument.composite();
        } else
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
        p.setOpacity(brush.flow / 100.0);
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
    if (!transformActive && (tool == Brush || tool == Pencil || tool == Eraser || tool == Clone)) {
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
    if (!transformActive && tool == Clone && hasClone) {
        p.setPen(QPen(accentColor, 1 / zoom));
        p.drawLine(cloneAnchor + QPoint(-8, 0), cloneAnchor + QPoint(8, 0));
        p.drawLine(cloneAnchor + QPoint(0, -8), cloneAnchor + QPoint(0, 8));
    }
    p.restore();
    if (!showRulers) {
        paintMoveTarget(p);
        paintTransform(p);
        return;
    }
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
    paintMoveTarget(p);
    paintTransform(p);
}
QImage &Canvas::target() {
    auto &l = document->state.layers[document->state.active];
    return document->editingMask ? l.mask : l.pixels;
}
qreal Canvas::stampDiameter(qreal pressure) const {
    qreal scale = 1;
    if (tabletActive && brush.pressureSize) {
        const qreal minimum = qBound(1, brush.pressureMinimum, 100) / 100.0;
        const qreal curve =
            std::isfinite(brush.pressureCurve) ? qBound(.25, brush.pressureCurve, 4.0) : 1;
        scale = minimum + (1 - minimum) * std::pow(qBound(0.0, pressure, 1.0), curve);
    }
    return qMax(1.0, qBound(1, brush.size, 512) * scale);
}
QTransform Canvas::tipTransform(QPointF position) const {
    qreal angle = qBound(-180, brush.angle, 180);
    qreal roundness = qBound(5, brush.roundness, 100) / 100.0;
    if (tabletActive) {
        if (brush.tiltAngle && std::hypot(tabletTiltX, tabletTiltY) > .01)
            angle += qRadiansToDegrees(std::atan2(tabletTiltY, tabletTiltX));
        if (brush.tiltRoundness)
            roundness *= qBound(
                .05, std::cos(qDegreesToRadians(qMin(85.0, std::hypot(tabletTiltX, tabletTiltY)))), 1.0);
    }
    QTransform tip;
    tip.translate(position.x(), position.y());
    tip.rotate(angle);
    tip.scale(1, roundness);
    return tip;
}
const QImage &Canvas::coloredTip(QColor color) {
    if (tipCacheKey == brush.tipMask.cacheKey() && tipCacheColor == color && !tipCache.isNull())
        return tipCache;
    tipCache = QImage(brush.tipMask.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < tipCache.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(tipCache.scanLine(y));
        const auto *mask = brush.tipMask.constScanLine(y);
        for (int x = 0; x < tipCache.width(); ++x) {
            const int alpha = (mask[x] * color.alpha() + 127) / 255;
            row[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), alpha));
        }
    }
    tipCacheKey = brush.tipMask.cacheKey();
    tipCacheColor = color;
    return tipCache;
}
void Canvas::stamp(QPointF position, qreal pressure) {
    if (document->state.layers[document->state.active].isSmartObject() && !document->editingMask)
        return;
    if (tabletActive && pressure <= 0)
        return;
    auto &l = document->state.layers[document->state.active];
    const QPointF at = position - l.offset;
    const qreal radius = stampDiameter(pressure) / 2;
    const QTransform tip = tipTransform(at);
    const bool custom = !brush.tipMask.isNull();
    if (custom && (brush.tipMask.format() != QImage::Format_Grayscale8 || brush.tipMask.width() > 512 ||
                   brush.tipMask.height() > 512)) {
        emit toolHint("Fırça uç maskesi geçersiz.");
        return;
    }
    const qreal longest = custom ? qMax(brush.tipMask.width(), brush.tipMask.height()) : 1;
    const QSizeF footprint = custom ? QSizeF(2 * radius * brush.tipMask.width() / longest,
                                             2 * radius * brush.tipMask.height() / longest)
                                    : QSizeF(2 * radius, 2 * radius);
    const QRectF tipRect(QPointF(-footprint.width() / 2, -footprint.height() / 2), footprint);
    const bool erase = tool == Eraser || tabletEraser;
    qreal flow = qBound(1, brush.flow, 100) / 100.0;
    if (tabletActive && brush.pressureFlow) {
        const qreal curve =
            std::isfinite(brush.pressureCurve) ? qBound(.25, brush.pressureCurve, 4.0) : 1;
        flow *= std::pow(qBound(0.0, pressure, 1.0), curve);
    }
    const QRect affected =
        QRectF(at - QPointF(radius + 2, radius + 2), QSizeF(2 * radius + 4, 2 * radius + 4))
            .toAlignedRect();
    const QRect textRegion = affected.intersected(target().rect());
    const bool protectText = !document->editingMask && !l.text.isEmpty();
    const QImage beforeText =
        protectText && !textRegion.isEmpty() ? target().copy(textRegion) : QImage{};
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
            if (custom) {
                p.setRenderHint(QPainter::SmoothPixmapTransform, tool != Pencil);
                if (tool == Clone && !erase) {
                    // Sample source pixels in layer coordinates; only the coverage rotates/scales.
                    QImage dab(affected.size(), QImage::Format_ARGB32_Premultiplied);
                    dab.fill(Qt::transparent);
                    const QPointF sourceOrigin = QPointF(cloneAnchor - l.offset) +
                                                 (QPointF(affected.topLeft()) + l.offset - press);
                    QPainter clone(&dab);
                    clone.drawImage(QPointF(), cloneSource, QRectF(sourceOrigin, dab.size()));
                    clone.end();
                    QImage coverage(dab.size(), QImage::Format_ARGB32_Premultiplied);
                    coverage.fill(Qt::transparent);
                    QPainter maskPainter(&coverage);
                    QTransform localTip = tip;
                    localTip.setMatrix(tip.m11(), tip.m12(), tip.m13(), tip.m21(), tip.m22(), tip.m23(),
                                       tip.dx() - affected.left(), tip.dy() - affected.top(), tip.m33());
                    maskPainter.setTransform(localTip);
                    maskPainter.setRenderHint(QPainter::SmoothPixmapTransform, tool != Pencil);
                    maskPainter.drawImage(tipRect, coloredTip(Qt::white));
                    maskPainter.end();
                    QPainter clipped(&dab);
                    clipped.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                    clipped.drawImage(0, 0, coverage);
                    clipped.end();
                    p.drawImage(affected.topLeft(), dab);
                } else {
                    p.setTransform(tip, true);
                    p.drawImage(tipRect, coloredTip(color));
                }
            } else if (tool == Clone && !erase) {
                QPainterPath clip;
                clip.addEllipse(QPointF(), radius, radius);
                p.setClipPath(tip.map(clip), Qt::IntersectClip);
                const QPointF source = QPointF(cloneAnchor - l.offset) + (position - press);
                p.drawImage(at - QPointF(radius, radius), cloneSource,
                            QRectF(source - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius)));
            } else {
                p.setTransform(tip, true);
                p.setPen(Qt::NoPen);
                if (brush.hardness >= 100 || tool == Pencil)
                    p.setBrush(color);
                else {
                    QRadialGradient gradient(QPointF(), radius);
                    gradient.setColorAt(0, color);
                    gradient.setColorAt(qBound(0, brush.hardness, 99) / 100.0, color);
                    color.setAlpha(0);
                    gradient.setColorAt(1, color);
                    p.setBrush(gradient);
                }
                p.drawEllipse(QPointF(), radius, radius);
            }
        },
        affected);
    if (edited && !document->editingMask && (!protectText || beforeText != target().copy(textRegion)))
        l.text.clear();
}
void Canvas::stroke(QPointF from, QPointF to, bool final) {
    const qreal distance = QLineF(from, to).length();
    const auto place = [&](QPointF at, qreal pressure) {
        stamp(at, pressure);
        lastStamp = at;
        hasStrokeStamp = true;
        remainingStampDistance =
            qMax(1.0, stampDiameter(pressure) * qBound(1, brush.spacing, 200) / 100.0);
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
    if (l.isSmartObject() && !document->editingMask)
        return;
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
        blend.setOpacity(brush.flow / 100.0);
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
    endKeyboardNudge();
    setFocus();
    hover = toImage(e->position());
    press = last = hover;
    widgetLast = e->position();
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && tool == Hand && !transformActive)) {
        transformDragging = false;
        panning = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (transformActive) {
        transformGesture = transformBox();
        transformHandle = transformGesture.hitTest(e->position(), zoom, pan);
        transformDragging = transformHandle != TransformHandles::None;
        transformPress = hover;
        updateTransformCursor(e->position());
        update();
        e->accept();
        return;
    }
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
            selectionRevision = document->revision();
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
        selectionRevision = document->revision();
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
        selectionRevision = document->revision();
        gestureSelectionMode =
            tool == Crop ? Selection::Replace : effectiveSelectionMode(e->modifiers());
        selectionPoints = {press};
        update();
        return;
    }
    if (tool == Move && autoSelectEnabled) {
        const int hit = layerAt(hover);
        if (hit < 0) {
            emit toolHint("Burada görünür, kilidi açık bir katman yok. Taşınacak içeriğe tıklayın.");
            return;
        }
        if (hit != document->state.active) {
            const QPointer<Canvas> self(this);
            const QPointer<Document> target = document;
            document->state.active = hit;
            document->editingMask = false;
            document->changed();
            if (!self || !target || document != target || document->state.active != hit ||
                document->isEditing() || tool != Move)
                return;
        }
    }
    auto &l = document->state.layers[document->state.active];
    if (l.locked || !l.visible) {
        emit toolHint("Düzenlemek için görünür, kilidi açık bir katman seçin.");
        return;
    }
    if (l.isSmartObject() && !document->editingMask && tool != Move) {
        emit toolHint("Akıllı nesnenin gömülü kaynağı korunur. İçeriği değiştir kullanın veya yeni boş katmana boyayın. "
                      "Görünümü rasterleştir, gömülü kaynak korumasını kaldırır.");
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
        positionTransformControls();
        update();
        return;
    }
    if (transformActive) {
        const QPointer<Canvas> self(this);
        const quint64 generation = transformGeneration;
        advanceTransform(hover, e->modifiers());
        if (!self)
            return;
        if (!transformActive || generation != transformGeneration) {
            updateCursor();
            update();
            return;
        }
        updateTransformCursor(e->position());
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
        else if (tabletActive && (brush.pressureSize || brush.pressureFlow) &&
                 qAbs(inputPressure - lastInputPressure) > .001) {
            stamp(hover, inputPressure);
            lastStamp = hover;
            remainingStampDistance =
                qMax(1.0, stampDiameter(inputPressure) * qBound(1, brush.spacing, 200) / 100.0);
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
    if (l.isSmartObject() && !document->editingMask)
        return;
    bool edited = document->selection.paint(target(), l.offset, [&](QPainter &p) {
        p.setRenderHint(QPainter::Antialiasing);
        QColor c = foreground;
        if (document->editingMask) {
            int g = qGray(c.rgb());
            c = {g, g, g};
        }
        p.setOpacity(brush.flow / 100.0);
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
    if (l.isSmartObject() && !document->editingMask)
        return;
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
        p.setOpacity(brush.flow / 100.0);
        p.fillRect(target().rect(), g);
    });
    if (edited && !document->editingMask)
        l.text.clear();
}
bool Canvas::finishPendingGesture() {
    const bool nudged = nudgeKey != 0;
    endKeyboardNudge();
    if (transformActive)
        return applyTransform();
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
        return nudged;
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
    if (panning && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
        pan += e->position() - widgetLast;
        widgetLast = e->position();
        panning = false;
        fitted = false;
        updateCursor();
        positionTransformControls();
        update();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        if (transformActive) {
            const QPointer<Canvas> self(this);
            const quint64 generation = transformGeneration;
            advanceTransform(toImage(e->position()), e->modifiers());
            if (!self)
                return;
            if (!transformActive || generation != transformGeneration) {
                updateCursor();
                update();
                e->accept();
                return;
            }
            transformDragging = false;
            transformHandle = TransformHandles::None;
            updateTransformCursor(e->position());
            update();
            e->accept();
            return;
        }
        if (tool == PolygonLasso && polygonActive)
            return;
        advanceGesture(toImage(e->position()), e->modifiers());
        finishPendingGesture();
    }
}
void Canvas::tabletEvent(QTabletEvent *e) {
    if (transformActive) {
        if (TransformHandles::finite(e->position()) && std::isfinite(e->pressure()) &&
            std::isfinite(e->xTilt()) && std::isfinite(e->yTilt())) {
            const QEvent::Type type = e->type() == QEvent::TabletPress
                                          ? QEvent::MouseButtonPress
                                      : e->type() == QEvent::TabletRelease
                                          ? QEvent::MouseButtonRelease
                                          : QEvent::MouseMove;
            QMouseEvent pointer(type, e->position(), e->globalPosition(), e->button(),
                                e->buttons(), e->modifiers(), e->pointingDevice());
            if (type == QEvent::MouseButtonPress)
                mousePressEvent(&pointer);
            else if (type == QEvent::MouseButtonRelease)
                mouseReleaseEvent(&pointer);
            else
                mouseMoveEvent(&pointer);
        }
        // Transform mode owns pen and eraser input, regardless of the previously selected tool.
        e->accept();
        return;
    }
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
    transformDragging = false;
    setZoom(zoom * std::pow(1.0015, e->angleDelta().y()), e->position());
    e->accept();
}
void Canvas::endKeyboardNudge() {
    ++nudgeGeneration;
    nudgeKey = 0;
    nudgeRevision = 0;
}
void Canvas::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == nudgeKey) {
        // X11 and other platforms can send synthetic releases between repeats.
        if (!e->isAutoRepeat())
            endKeyboardNudge();
        e->accept();
        return;
    }
    QWidget::keyReleaseEvent(e);
}
void Canvas::keyPressEvent(QKeyEvent *e) {
    if (!keyboardDocument || document != keyboardDocument) {
        endKeyboardNudge();
        QWidget::keyPressEvent(e);
        return;
    }
    if (transformActive) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            applyTransform();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            cancelTransform();
            e->accept();
            return;
        }
        QPointF delta;
        const qreal step = e->modifiers() & Qt::ShiftModifier ? 10 : 1;
        if (e->key() == Qt::Key_Left)
            delta.setX(-step);
        else if (e->key() == Qt::Key_Right)
            delta.setX(step);
        else if (e->key() == Qt::Key_Up)
            delta.setY(-step);
        else if (e->key() == Qt::Key_Down)
            delta.setY(step);
        if (!delta.isNull()) {
            transformDragging = false;
            previewTransform(transformPreview * QTransform::fromTranslate(delta.x(), delta.y()));
            e->accept();
            return;
        }
    }
    const bool arrow = e->key() == Qt::Key_Left || e->key() == Qt::Key_Right ||
                       e->key() == Qt::Key_Up || e->key() == Qt::Key_Down;
    const bool reserved = e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (tool == Move && hasFocus() && arrow && !reserved) {
        // Repeats without their original press are stale after a focus, target,
        // tool, history or save transition. They must never adopt a new target.
        if (!e->isAutoRepeat())
            endKeyboardNudge();
        if ((!e->isAutoRepeat() || nudgeKey == e->key()) && !dragging && !panning &&
            !tabletActive && !applyingNudge && !document->isEditing()) {
            const int step = e->modifiers() & Qt::ShiftModifier ? 10 : 1;
            const QPoint delta = e->key() == Qt::Key_Left    ? QPoint(-step, 0)
                                 : e->key() == Qt::Key_Right ? QPoint(step, 0)
                                 : e->key() == Qt::Key_Up    ? QPoint(0, -step)
                                                            : QPoint(0, step);
            const QPointer<Canvas> self(this);
            const QPointer<Document> target = keyboardDocument;
            const int active = target->state.active;
            const quint64 generation = nudgeGeneration;
            nudgeUpdates = 0;
            nudgeUpdateRevision = 0;
            QString error;
            nudgeKey = e->key(); // A synchronous real release must invalidate even the first press.
            applyingNudge = true;
            const bool moved = target->nudgeLayer(delta, nudgeRevision, &error);
            // updated() is synchronous: a listener can close this tab, change
            // focus/tool/target, or make another edit before the call returns.
            if (!self)
                return;
            applyingNudge = false;
            if (moved && target && document == target && generation == nudgeGeneration &&
                nudgeUpdates == 1 && target->revision() == nudgeUpdateRevision &&
                target->state.active == active && hasFocus() && tool == Move) {
                nudgeKey = e->key();
                nudgeRevision = nudgeUpdateRevision;
            } else {
                endKeyboardNudge();
                if (!moved)
                    emit toolHint(error);
            }
        }
        e->accept();
        return;
    }
    if (reserved || (e->key() != Qt::Key_Shift && !arrow))
        endKeyboardNudge();
    if (polygonActive && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        completePolygon();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        if (polygonActive || (dragging && (isSelectionTool() || tool == Crop))) {
            polygonActive = dragging = false;
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
            e->accept();
            return;
        }
        document->deselect();
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}
void Canvas::mouseDoubleClickEvent(QMouseEvent *e) {
    if (transformActive && e->button() == Qt::LeftButton) {
        if (transformBox().hitTest(e->position(), zoom, pan) == TransformHandles::Move)
            applyTransform();
        e->accept();
        return;
    }
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
    if (transformActive) {
        transformDragging = panning = false;
        transformHandle = TransformHandles::None;
        updateCursor();
        update();
    } else
        finishPendingGesture();
    QWidget::focusOutEvent(event);
}
void Canvas::deleteSelection() {
    auto &l = document->state.layers[document->state.active];
    if (l.locked)
        return;
    if ((l.isSmartObject() && !document->editingMask) || !l.geometry.isIdentity()) {
        emit toolHint(l.isSmartObject() && !document->editingMask
                          ? "Akıllı nesnenin gömülü kaynağı silinemez. Görünümü rasterleştir kaynak korumasını kaldırır; Geri al geri yükler."
                          : "Silmek için önce Katman → Görünümü rasterleştir kullanın.");
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

int Canvas::layerAt(QPointF position) const {
    if (!TransformHandles::finite(position) ||
        !QRectF(QPointF(), QSizeF(document->state.size)).contains(position))
        return -1;
    const auto alphaAt = [position](const Layer &layer) {
        if (!layer.visible || layer.opacity <= 0 || !layer.validGeometry())
            return 0;
        const QImage image = layer.rendered();
        const QPointF local = position - layer.renderedOffset();
        const QPoint pixel(qFloor(local.x()), qFloor(local.y()));
        return image.rect().contains(pixel) ? qRound(qAlpha(image.pixel(pixel)) * layer.opacity) : 0;
    };
    for (int i = document->state.layers.size() - 1; i >= 0; --i) {
        const Layer &layer = document->state.layers[i];
        if (layer.locked || !alphaAt(layer))
            continue;
        if (layer.clipped) {
            int base = i - 1;
            while (base > 0 && document->state.layers[base].clipped)
                --base;
            if (base < 0 || !alphaAt(document->state.layers[base]))
                continue;
        }
        return i;
    }
    return -1;
}
void Canvas::paintMoveTarget(QPainter &p) {
    if (transformActive || tool != Move || document->state.active < 0 ||
        document->state.active >= document->state.layers.size())
        return;
    const Layer &layer = document->state.layers[document->state.active];
    p.save();
    const QRect viewport = showRulers ? rect().adjusted(24, 24, 0, 0) : rect();
    p.setClipRect(viewport);
    if (layerBoundsVisible && layer.visible && layer.validGeometry()) {
        TransformHandles box{QSizeF(layer.sourceSize()), layer.geometry, QPointF(layer.offset), {}};
        QPolygonF outline;
        for (QPointF point : box.polygon())
            outline.append(toWidget(point));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 170), 3));
        p.drawPolygon(outline);
        p.setPen(QPen(accentColor, 1, Qt::DashLine));
        p.drawPolygon(outline);
    }
    QFont textFont = font();
    textFont.setPixelSize(11);
    p.setFont(textFont);
    const QString hint = QString("Hedef: %1 · Tuvalden seçim %2%3")
                             .arg(layer.name, autoSelectEnabled ? "açık" : "kapalı",
                                  layer.locked ? " · Kilitli" : !layer.visible ? " · Gizli" : "");
    const int maxWidth = qMax(0, viewport.width() - 16);
    const QString text = QFontMetrics(textFont).elidedText(hint, Qt::ElideMiddle, qMax(0, maxWidth - 16));
    const QRect area(viewport.topLeft() + QPoint(8, 8),
                     QSize(qMin(maxWidth, QFontMetrics(textFont).horizontalAdvance(text) + 16), 25));
    p.setPen(Qt::NoPen);
    p.setBrush(rulerColor);
    p.drawRoundedRect(area, 5, 5);
    p.setPen(readableOverlayText(rulerText, rulerColor));
    p.drawText(area.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    p.restore();
}
const Layer *Canvas::previewLayer() const {
    if (transformActive) {
        if (transformSelectionCopy && transformCopyState.active >= 0 &&
            transformCopyState.active < transformCopyState.layers.size())
            return &transformCopyState.layers[transformCopyState.active];
        return &transformSource;
    }
    const int active = document->state.active;
    return active >= 0 && active < document->state.layers.size() ? &document->state.layers[active] : nullptr;
}
State Canvas::previewState() const {
    State state = document->state;
    if (transformActive && transformSelectionCopy) {
        state.layers.insert(transformCopyState.active, transformCopyState.layers[transformCopyState.active]);
        state.active = transformCopyState.active;
    }
    return state;
}
State Canvas::pendingTransformState() const {
    State pending = previewState();
    if (transformActive && pending.active >= 0 && pending.active < pending.layers.size())
        pending.layers[pending.active].geometry = transformPreview;
    return pending;
}
QSize Canvas::activeLayerSourceSize() const {
    const Layer *layer = previewLayer();
    return layer ? layer->sourceSize() : QSize();
}
QRect Canvas::activeLayerDisplayBounds() const {
    const Layer *layer = previewLayer();
    if (!layer)
        return {};
    Layer preview = *layer;
    if (transformActive)
        preview.geometry = transformPreview;
    return preview.transformedBounds().translated(preview.offset);
}
QString Canvas::activeLayerTargetName() const {
    const Layer *layer = previewLayer();
    return layer ? layer->name : QString();
}
bool Canvas::copySelectionAndTransform() {
    const QPointer<Canvas> self(this);
    if (!document->selection.hasPixels()) {
        emit toolHint("Önce piksel içeren bir seçim yapın.");
        return false;
    }
    if (transformActive && transformSelectionCopy)
        return true;
    if (!transformActive && !beginTransform())
        return false;
    if (!self || !transformSourceMatches())
        return false;
    State copied;
    QString error;
    if (!document->selectionCopyState(copied, &error)) {
        emit toolHint(error);
        return false;
    }
    transformSelectionCopy = true;
    transformSelection = document->selection;
    transformCopyState = copied;
    transformPreview = copied.layers[copied.active].geometry;
    transformDragging = false;
    transformHandle = TransformHandles::None;
    cacheDirty = true;
    updateTransformContext();
    positionTransformControls();
    setFocus();
    update();
    const quint64 generation = transformGeneration;
    emit transformPreviewChanged();
    if (!self || !transformActive || generation != transformGeneration)
        return false;
    emit toolHint("Seçim yeni piksel katmanına kopyalanacak. Özgün katman ve seçim korunur; Enter uygular, Esc kopyayı da iptal eder.");
    return true;
}
void Canvas::updateTransformContext(int availableWidth) {
    if (!transformActive)
        return;
    const QString fullName = activeLayerTargetName();
    transformScope->ensurePolished();
    const int nameWidth = qMax(50, (availableWidth > 0 ? availableWidth : qMin(540, width() - 16)) - 70);
    const QString name = QFontMetrics(transformScope->font()).elidedText(fullName, Qt::ElideMiddle, nameWidth);
    QString scope = QString("Hedef: %1\nKapsam: %2")
                        .arg(name, transformSelectionCopy
                                ? "Seçim kopyası · Yeni piksel katmanı"
                                : "Katmanın tamamı");
    if (transformSelectionCopy)
        scope += "\nÖzgün katman yerinde kalır; metin/maske/efektleri korunur. Kopya, görünümün pikselleridir. Esc kopyayı da iptal eder.";
    else if (document->selection.hasPixels())
        scope += "\nSeçim sabit kalır; yalnız seçili pikseller dönüştürülmez.";
    transformScope->setText(scope);
    transformScope->setAccessibleName("Hedef: " + fullName + "\n" + scope.mid(scope.indexOf("Kapsam:")));
    transformScope->setToolTip("Hedef: " + fullName);
    const QSize source = activeLayerSourceSize();
    const QRect bounds = activeLayerDisplayBounds();
    const TransformHandles box = transformBox();
    const QPointF top = box.point(TransformHandles::TopRight) - box.point(TransformHandles::TopLeft);
    const QPointF left = box.point(TransformHandles::BottomLeft) - box.point(TransformHandles::TopLeft);
    const qreal sx = 100 * std::hypot(top.x(), top.y()) / qMax(1, source.width());
    const qreal sy = 100 * std::hypot(left.x(), left.y()) / qMax(1, source.height());
    const qreal angle = qRadiansToDegrees(std::atan2(top.y(), top.x()));
    const Layer *sourceLayer = previewLayer();
    const QString sourceLabel = sourceLayer && sourceLayer->isSmartObject() ? "Sabit çerçeve" : "Kaynak";
    transformMetrics->setText(
        QString("%10: %1 × %2 px · Görünüm (AABB): %3 × %4 px\nX: %5 · Y: %6 · Kenar ölçeği: %7% / %8% · Açı: %9°")
            .arg(source.width()).arg(source.height()).arg(bounds.width()).arg(bounds.height())
            .arg(bounds.x()).arg(bounds.y()).arg(sx, 0, 'f', 1).arg(sy, 0, 'f', 1).arg(angle, 0, 'f', 1).arg(sourceLabel));
    transformMetrics->setToolTip("Görünüm: belge eksenlerine hizalı, dışa yuvarlanan sınır (AABB). Ölçek: üst ve sol kenarın kaynak boyutuna oranı; açı: üst kenarın yönü. Perspektif/eğmede yerel genişlik ve yükseklik değildir.");
    transformInfo->setText(QString("Görünüm (AABB): %1 × %2 px · Kenar: %3% / %4% · Açı: %5°")
                              .arg(bounds.width()).arg(bounds.height())
                              .arg(sx, 0, 'f', 1).arg(sy, 0, 'f', 1).arg(angle, 0, 'f', 1));
    transformCopySelection->setVisible(!transformSelectionCopy && document->selection.hasPixels());
    transformCopySelection->setText("Seçimi kopyala ve dönüştür");
    setAccessibleDescription(scope + "\n" + transformMetrics->text());
}
TransformHandles Canvas::transformBox() const {
    if (!transformActive || transformLayer < 0 || transformLayer >= document->state.layers.size())
        return {};
    const Layer &layer = *previewLayer();
    return {QSizeF(layer.sourceSize()), transformPreview, QPointF(layer.offset),
            showRulers ? QRectF(rect().adjusted(24, 24, 0, 0)) : QRectF(rect())};
}
QPointF Canvas::transformHandlePosition(TransformHandles::Handle handle) const {
    return transformBox().widgetPoint(handle, zoom, pan);
}
bool Canvas::beginTransform() {
    const QPointer<Canvas> self(this);
    const QPointer<Document> target = keyboardDocument; // This canvas's original document lifetime.
    const quint64 previous = transformGeneration;
    if (!target || document != target)
        return false;
    if (transformActive) {
        setFocus();
        return self && transformActive && transformGeneration == previous && transformSourceMatches();
    }
    finishPendingGesture();
    if (!self || !target || document != target || transformGeneration != previous)
        return false;
    if (document->state.layers.isEmpty() || document->state.active < 0 ||
        document->state.active >= document->state.layers.size())
        return false;
    const Layer &layer = document->state.layers[document->state.active];
    if (document->isEditing() || layer.locked || !layer.visible || !layer.validGeometry()) {
        emit toolHint("Dönüştürmek için görünür, kilidi açık bir katman seçin ve diğer düzenlemeyi bitirin.");
        return false;
    }
    const quint64 generation = ++transformGeneration;
    transformDocument = target;
    transformLayer = document->state.active;
    transformPreview = layer.geometry;
    transformSource = layer;
    transformSource.cache = {};
    transformRevision = document->revision();
    transformCanvasSize = document->state.size;
    transformLayerCount = document->state.layers.size();
    transformSelection = document->selection;
    transformSelectionCopy = false;
    transformActive = true;
    transformDragging = false;
    transformHandle = TransformHandles::None;
    transformInfo->setText("Köşe: oranı koru · Shift: serbest · Alt: merkez · Yuvarlak ok: döndür · Shift: 15°");
    updateTransformContext();
    positionTransformControls();
    transformControls->show();
    transformContext->show();
    transformContext->raise();
    transformControls->raise();
    cacheDirty = true;
    setFocus();
    if (!self || transformGeneration != generation || !transformActive)
        return false;
    if (!transformSourceMatches()) {
        cancelTransform();
        return false;
    }
    updateCursor();
    update();
    emit toolHint(QString("%1 katmanının tamamı dönüştürülüyor. Enter uygular, Esc iptal eder; kaynak ve maske korunur.").arg(transformSource.name));
    if (!self || transformGeneration != generation || !transformActive)
        return false;
    if (!transformSourceMatches()) {
        cancelTransform();
        return false;
    }
    emit transformPreviewChanged();
    if (!self || transformGeneration != generation || !transformActive)
        return false;
    emit transformSessionChanged(true);
    if (!self || transformGeneration != generation || !transformActive)
        return false;
    if (!transformSourceMatches()) {
        cancelTransform();
        return false;
    }
    return true;
}
void Canvas::endTransformSession() {
    const quint64 ended = ++transformGeneration;
    transformDocument.clear();
    transformActive = transformDragging = false;
    transformHandle = TransformHandles::None;
    transformLayer = -1;
    transformSource = {};
    transformCopyState = {};
    transformSelection = {};
    transformSelectionCopy = false;
    transformControls->hide();
    transformContext->hide();
    setToolTip({});
    setAccessibleDescription({});
    cacheDirty = true;
    updateCursor();
    update();
    const QPointer<Canvas> self(this);
    emit transformPreviewChanged();
    if (self && transformGeneration == ended)
        emit transformSessionChanged(false);
}
bool Canvas::applyTransform() {
    if (!transformActive)
        return false;
    const QPointer<Canvas> self(this);
    const QPointer<Document> target = transformDocument;
    const QTransform geometry = transformPreview;
    const int layer = transformLayer;
    const Layer source = transformSource;
    const quint64 revision = transformRevision, ended = transformGeneration + 1;
    const QSize canvasSize = transformCanvasSize;
    const qsizetype layerCount = transformLayerCount;
    const bool current = transformSourceMatches();
    const bool selectionCopy = transformSelectionCopy;
    const Selection selectionSource = transformSelection;
    State candidateState = previewState();
    endTransformSession();
    // Ending a session is synchronous: a listener may delete the canvas/target,
    // retarget it, or begin (and even cancel) an entirely new preview.
    if (!self || transformGeneration != ended)
        return false;
    if (!current || !target || document != target ||
        !matchesTransformTarget(target, layer, revision, canvasSize, layerCount, source) ||
        (selectionCopy && !target->selection.equivalent(selectionSource, canvasSize))) {
        emit toolHint("Katman değiştiği için dönüşüm uygulanmadı.");
        return false;
    }
    // Validate before opening the real transaction. setLayerTransform emits
    // updated(), so calling it on the target would expose a half-finished edit
    // to callbacks that can close the tab or start a different transaction.
    Document candidate;
    candidate.state = candidateState;
    QString error;
    if (!candidate.setLayerTransform(geometry, &error)) {
        emit toolHint(error);
        return false;
    }
    target->beginEdit();
    if (selectionCopy) {
        target->state.layers.insert(candidate.state.active, candidate.state.layers[candidate.state.active]);
        target->state.active = candidate.state.active;
        target->editingMask = false;
    } else
        target->state.layers[layer].geometry = geometry;
    target->commit(selectionCopy ? "Seçim kopyalandı ve dönüştürüldü" : "Tuvalde kaynak koruyan dönüşüm");
    if (self && target && document == target && transformGeneration == ended)
        emit toolHint(selectionCopy ? "Seçim kopyası dönüştürüldü · Özgün katman ve seçim korundu."
                                    : "Dönüşüm uygulandı · Kaynak pikseller, metin ve maske korundu.");
    return self && target && document == target && transformGeneration == ended && !transformActive;
}
void Canvas::cancelTransform() {
    if (!transformActive)
        return;
    const QPointer<Canvas> self(this);
    const quint64 ended = transformGeneration + 1;
    endTransformSession();
    if (self && transformGeneration == ended)
        emit toolHint("Dönüşüm iptal edildi.");
}
bool Canvas::transformSourceMatches() const {
    return transformDocument && document == transformDocument &&
           matchesTransformTarget(transformDocument, transformLayer, transformRevision,
                                  transformCanvasSize, transformLayerCount, transformSource) &&
           (!transformSelectionCopy || document->selection.equivalent(transformSelection, document->state.size));
}
bool Canvas::previewTransform(const QTransform &geometry) {
    if (!transformActive)
        return false;
    Document candidate;
    candidate.state = previewState();
    QString error;
    if (!candidate.setLayerTransform(geometry, &error)) {
        transformInfo->setText("Sınır aşıldı; son geçerli önizleme korunuyor. Daha küçük bir dönüşüm deneyin.");
        emit toolHint(error);
        return false;
    }
    transformInfo->setText("Köşe: oranı koru · Shift: serbest · Alt: merkez · Yuvarlak ok: döndür · Shift: 15°");
    QTransform normalized = geometry;
    bool unchanged = true;
    const Layer &source = *previewLayer();
    const QSize sourceSize = source.sourceSize();
    for (QPointF point : {QPointF(), QPointF(sourceSize.width(), 0),
                          QPointF(0, sourceSize.height()),
                          QPointF(sourceSize.width(), sourceSize.height())})
        unchanged &= QLineF(geometry.map(point), source.geometry.map(point)).length() < 1e-7;
    if (unchanged)
        normalized = source.geometry;
    if (transformPreview == normalized) {
        updateTransformContext();
        return true;
    }
    transformPreview = normalized;
    updateTransformContext();
    cacheDirty = true;
    positionTransformControls();
    update();
    emit transformPreviewChanged();
    return true;
}
void Canvas::advanceTransform(QPointF position, Qt::KeyboardModifiers modifiers) {
    if (transformActive && transformDragging && TransformHandles::finite(position))
        previewTransform(transformGesture.dragged(transformHandle, transformPress, position, modifiers));
}
void Canvas::positionTransformControls() {
    if (!transformActive)
        return;
    const int available = qMax(180, width() - 32);
    const int controlsWidth = qMin(540, available);
    const qreal top = showRulers ? 36 : 12;
    const bool showInfo = controlsWidth >= 300 && height() >= top + 76 + 12;
    transformInfo->setVisible(showInfo);
    auto *controlsLayout = transformControls->layout();
    auto *buttons = qobject_cast<QGridLayout *>(controlsLayout->itemAt(0)->layout());
    const auto setButtonRows = [&](int rows) {
        if (buttons->property("buttonRows").toInt() == rows)
            return;
        while (auto *item = buttons->takeAt(0))
            delete item;
        for (int i = 0; i < 5; ++i)
            buttons->setColumnStretch(i, 0);
        if (rows == 3) {
            buttons->addWidget(transformNumeric, 0, 0);
            buttons->addWidget(transformCancel, 1, 0);
            buttons->addWidget(transformApply, 2, 0);
        } else if (rows == 2) {
            buttons->addWidget(transformNumeric, 0, 0, 1, 2);
            buttons->addWidget(transformCancel, 1, 0);
            buttons->addWidget(transformApply, 1, 1);
        } else {
            buttons->addWidget(transformCaption, 0, 0);
            buttons->setColumnStretch(1, 1);
            buttons->addWidget(transformNumeric, 0, 2);
            buttons->addWidget(transformCancel, 0, 3);
            buttons->addWidget(transformApply, 0, 4);
        }
        buttons->setProperty("buttonRows", rows);
        controlsLayout->invalidate();
    };
    setButtonRows(1);
    const auto setButtonLabels = [&](bool compact) {
        transformCaption->setVisible(!compact);
        transformNumeric->setText(compact ? "Sayısal" : "Sayısal…");
        transformCancel->setText(compact ? "İptal" : "İptal · Esc");
        transformApply->setText(compact ? "Uygula" : "Uygula ↵");
        for (auto *button : {transformNumeric, transformCancel, transformApply}) {
            button->ensurePolished();
            // Native styles may offer a smaller minimumSizeHint than the
            // complete label needs. Never let the row elide an action button.
            button->setMinimumSize(button->sizeHint());
        }
        // updateGeometry() reaches the widget's outer layout, but not this
        // nested grid. Discard its cached row metrics before measuring labels.
        buttons->invalidate();
        controlsLayout->invalidate();
    };
    setButtonLabels(controlsWidth < 390);
    const auto rowWidth = [&] {
        const auto margins = controlsLayout->contentsMargins();
        const auto frame = transformControls->contentsMargins();
        return buttons->minimumSize().width() + margins.left() + margins.right() +
               frame.left() + frame.right();
    };
    if (!transformCaption->isHidden() && rowWidth() > controlsWidth)
        setButtonLabels(true);
    // Keep words at narrow widths by giving actions two or three rows.
    if (rowWidth() > controlsWidth)
        setButtonRows(2);
    if (rowWidth() > controlsWidth)
        setButtonRows(3);
    // At extreme width/height/font combinations even three complete words cannot
    // fit. Retain the established accessible glyph fallback only in that case.
    if (rowWidth() > controlsWidth || transformControls->minimumSizeHint().height() > height()) {
        transformNumeric->setText("123");
        transformCancel->setText("×");
        transformApply->setText("✓");
        for (auto *button : {transformNumeric, transformCancel, transformApply})
            button->setMinimumSize(button->sizeHint());
        buttons->invalidate();
        controlsLayout->invalidate();
        setButtonRows(1);
        if (rowWidth() > controlsWidth)
            setButtonRows(2);
    }
    controlsLayout->invalidate();
    const QRectF viewport = showRulers ? QRectF(rect().adjusted(24, 24, 0, 0)) : QRectF(rect());
    const TransformHandles box = transformBox();
    QVector<QRectF> targets;
    for (int i = TransformHandles::TopLeft; i <= TransformHandles::Move; ++i) {
        const QPointF point = box.widgetPoint(TransformHandles::Handle(i), zoom, pan);
        const QRectF target = QRectF(point - QPointF(9, 9), QSizeF(18, 18)).intersected(viewport);
        if (!target.isEmpty())
            targets.append(target);
    }
    struct Placement {
        QPoint position;
        int covered = std::numeric_limits<int>::max();
        qreal distance = std::numeric_limits<qreal>::max();
    };
    const auto place = [&](int controlsHeight, int clearance) {
        const QRectF bounds = box.polygon().boundingRect();
        const qreal above = toWidget(bounds.topLeft()).y() - controlsHeight - 16;
        const qreal below = toWidget(bounds.bottomRight()).y() + 16;
        qreal y = height() - controlsHeight - 16;
        if (below + controlsHeight <= height() - 12)
            y = below;
        else if (above >= top)
            y = above;
        // A native-style two-row fallback may consume the bottom gutter in a
        // short viewport. Keep the whole panel in the canvas before adding gaps.
        const int minY = qMin(qRound(top), qMax(0, height() - controlsHeight));
        const int maxY = qMax(minY, height() - controlsHeight - 12);
        const int maxX = width() - controlsWidth, centerX = maxX / 2;
        const int preferredY = qBound(minY, qRound(y), maxY);
        QVector<int> columns{centerX, qMin(16, maxX), qMax(0, maxX - 16), 0, maxX};
        QVector<int> rows{preferredY, minY, maxY};
        // Include every target edge, so a free placement between targets or at
        // a viewport edge is found even when centered placement cannot fit.
        for (const QRectF &target : targets) {
            columns.append(qBound(0, qFloor(target.left()) - controlsWidth - clearance, maxX));
            columns.append(qBound(0, qCeil(target.right()) + clearance, maxX));
            rows.append(qBound(minY, qFloor(target.top()) - controlsHeight - clearance, maxY));
            rows.append(qBound(minY, qCeil(target.bottom()) + clearance, maxY));
        }
        Placement best;
        for (int x : columns)
            for (int row : rows) {
                const QRectF area = QRectF(x, row, controlsWidth, controlsHeight)
                                        .adjusted(-clearance, -clearance, clearance, clearance);
                int covered = 0;
                for (const QRectF &target : targets)
                    covered += area.intersects(target);
                const qreal distance = qAbs(row - preferredY) + qAbs(x - centerX);
                if (covered < best.covered || (covered == best.covered && distance < best.distance))
                    best = {{x, row}, covered, distance};
                if (!best.covered && best.distance == 0)
                    return best;
            }
        return best;
    };
    const auto bestPlacement = [&](int controlsHeight) {
        Placement result = place(controlsHeight, 3);
        if (result.covered) {
            const Placement snug = place(controlsHeight, 0);
            if (snug.covered < result.covered)
                result = snug;
        }
        return result;
    };
    int controlsHeight = qMax(showInfo ? 76 : 48, transformControls->minimumSizeHint().height());
    Placement best = bestPlacement(controlsHeight);
    if (showInfo && best.covered) {
        // A short, wide viewport (for example Touch's bottom-dock layout) can
        // fit the buttons below the handles, but not the two-row help panel.
        // Keep the help whenever it fits without hiding a pointer target.
        transformInfo->hide();
        controlsLayout->invalidate();
        const int compactHeight = qMax(48, transformControls->minimumSizeHint().height());
        const Placement compactPlacement = bestPlacement(compactHeight);
        if (compactPlacement.covered < best.covered) {
            controlsHeight = compactHeight;
            best = compactPlacement;
        } else
            transformInfo->show();
    }
    transformControls->setGeometry(QRect(best.position, QSize(controlsWidth, controlsHeight)));
    // Trying the help row can relayout a visible, compact panel before hiding
    // it again. If the final frame size is unchanged, setGeometry does not
    // resize it, so settle the invalidated layout now instead of leaving the
    // buttons squeezed to their padding until the next event-loop turn.
    transformControls->layout()->activate();
    positionTransformContext();
}
void Canvas::positionTransformContext() {
    if (!transformActive)
        return;
    auto *layout = transformContextContent->layout();
    const TransformHandles box = transformBox();
    const QRect viewport = showRulers ? rect().adjusted(24, 24, 0, 0) : rect();
    QVector<QRectF> obstacles{QRectF(transformControls->geometry())};
    for (int i = TransformHandles::TopLeft; i <= TransformHandles::Move; ++i) {
        const QPointF at = box.widgetPoint(TransformHandles::Handle(i), zoom, pan);
        const QRectF target = QRectF(at - QPointF(10, 10), QSizeF(20, 20)).intersected(viewport);
        if (!target.isEmpty())
            obstacles.append(target);
    }
    struct Placement {
        QRect rect;
        int covered = std::numeric_limits<int>::max();
        int content = -1;
    } best;
    const int preferredWidth = qMin(540, qMax(160, width() - 16));
    QVector<int> widths{preferredWidth};
    for (int candidate : {420, 320, 240, 180, 160})
        if (candidate < preferredWidth && !widths.contains(candidate))
            widths.append(candidate);
    // Prefer all the context, but shrink the scroll viewport before allowing it
    // to hide a pointer target. Scope stays first; measurements/help remain scrollable.
    for (int contextWidth : widths) {
        updateTransformContext(contextWidth);
        transformCopySelection->setText(contextWidth < 290 ? "Seçimi kopyala\nve dönüştür"
                                                           : "Seçimi kopyala ve dönüştür");
        layout->invalidate();
        const int desiredHeight = qMax(44, layout->totalHeightForWidth(contextWidth - 18) + 2);
        const int maximumHeight = qMin(desiredHeight, qMax(44, height() - 8));
        const int minHeight = qMin(44, maximumHeight);
        for (int contextHeight = maximumHeight; contextHeight >= minHeight;
             contextHeight = contextHeight == minHeight ? minHeight - 1 : qMax(minHeight, contextHeight - 8)) {
            const int top = showRulers ? 28 : 4;
            const int right = qMax(0, width() - contextWidth);
            const int bottom = qMax(0, height() - contextHeight);
            const int minY = qMin(top, bottom);
            QVector<int> columns{qMin(8, right), right, 0, right / 2};
            QVector<int> rows{minY, bottom};
            for (const QRectF &target : obstacles) {
                columns.append(qBound(0, qFloor(target.left()) - contextWidth - 3, right));
                columns.append(qBound(0, qCeil(target.right()) + 3, right));
                rows.append(qBound(minY, qFloor(target.top()) - contextHeight - 3, bottom));
                rows.append(qBound(minY, qCeil(target.bottom()) + 3, bottom));
            }
            for (int y : rows)
                for (int x : columns) {
                    const QRect area(x, y, contextWidth, contextHeight);
                    int covered = 0;
                    for (int i = 0; i < obstacles.size(); ++i)
                        if (QRectF(area).intersects(obstacles[i]))
                            covered += i == 0 ? 100 : 1;
                    const int content = contextWidth * contextHeight;
                    if (covered < best.covered || (covered == best.covered && content > best.content))
                        best = {area, covered, content};
                }
        }
    }
    updateTransformContext(best.rect.width());
    transformCopySelection->setText(best.rect.width() < 290 ? "Seçimi kopyala\nve dönüştür"
                                                           : "Seçimi kopyala ve dönüştür");
    layout->invalidate();
    transformContextContent->setMinimumHeight(qMax(44, layout->totalHeightForWidth(best.rect.width() - 18)));
    transformContext->setGeometry(best.rect);
    // A pathological minimum viewport may have no room even for the compact
    // scope. Keep handles and actions clickable through the unavoidable overlap.
    if (best.covered) {
        QRegion visible(transformContext->rect());
        for (const QRectF &obstacle : obstacles)
            visible -= QRegion(obstacle.toAlignedRect().translated(-best.rect.topLeft()));
        transformContext->setMask(visible);
    } else
        transformContext->clearMask();
    layout->activate();
    transformContext->layout()->activate();
}
void Canvas::paintTransform(QPainter &p) {
    if (!transformActive)
        return;
    const TransformHandles box = transformBox();
    QPolygonF outline;
    for (QPointF point : box.polygon())
        outline.append(toWidget(point));
    p.save();
    p.setClipRect(showRulers ? rect().adjusted(24, 24, 0, 0) : rect());
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 170), 3));
    p.drawPolygon(outline);
    p.setPen(QPen(accentColor, 1.2));
    p.drawPolygon(outline);
    const QPointF rotation = box.widgetPoint(TransformHandles::Rotate, zoom, pan);
    p.drawLine(box.widgetPoint(TransformHandles::Top, zoom, pan), rotation);
    for (int i = TransformHandles::TopLeft; i <= TransformHandles::Rotate; ++i) {
        const auto handle = TransformHandles::Handle(i);
        const QPointF at = box.widgetPoint(handle, zoom, pan);
        p.setBrush(transformDragging && transformHandle == handle ? accentColor : rulerColor);
        p.setPen(QPen(QColor(0, 0, 0, 150), 3));
        if (handle == TransformHandles::Rotate)
            p.drawEllipse(at, 8, 8);
        else
            p.drawRect(QRectF(at - QPointF(4, 4), QSizeF(8, 8)));
        p.setPen(QPen(accentColor, 1.4));
        if (handle == TransformHandles::Rotate)
            p.drawEllipse(at, 8, 8);
        else
            p.drawRect(QRectF(at - QPointF(4, 4), QSizeF(8, 8)));
        if (handle == TransformHandles::Rotate) {
            QFont iconFont = font();
            iconFont.setPixelSize(16);
            p.setFont(iconFont);
            p.drawText(QRectF(at - QPointF(9, 10), QSizeF(18, 20)), Qt::AlignCenter, "↻");
            QFont labelFont = font();
            labelFont.setPixelSize(10);
            p.setFont(labelFont);
            QRectF label(at + QPointF(12, -9), QSizeF(49, 18));
            if (label.right() > width() - 3)
                label.moveRight(at.x() - 12);
            p.fillRect(label, rulerColor);
            p.setPen(readableOverlayText(rulerText, rulerColor));
            p.drawText(label, Qt::AlignCenter, "Döndür");
        }
    }
    const QPointF center = box.widgetPoint(TransformHandles::Move, zoom, pan);
    p.setPen(QPen(QColor(0, 0, 0, 170), 3));
    p.drawLine(center - QPointF(5, 0), center + QPointF(5, 0));
    p.drawLine(center - QPointF(0, 5), center + QPointF(0, 5));
    p.setPen(QPen(accentColor, 1));
    p.drawLine(center - QPointF(5, 0), center + QPointF(5, 0));
    p.drawLine(center - QPointF(0, 5), center + QPointF(0, 5));
    p.restore();
}
void Canvas::updateTransformCursor(QPointF position) {
    const auto handle = transformDragging ? transformHandle : transformBox().hitTest(position, zoom, pan);
    if (handle == TransformHandles::Move)
        setCursor(transformDragging ? Qt::ClosedHandCursor : Qt::SizeAllCursor);
    else if (handle == TransformHandles::Rotate) {
        static const QCursor rotateCursor = [] {
            QPixmap icon(32, 32);
            icon.fill(Qt::transparent);
            QPainter painter(&icon);
            painter.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            path.arcMoveTo(QRectF(7, 7, 18, 18), 30);
            path.arcTo(QRectF(7, 7, 18, 18), 30, 285);
            const QPointF end = path.currentPosition();
            path.moveTo(end + QPointF(-6, -1));
            path.lineTo(end);
            path.lineTo(end + QPointF(-1, -6));
            painter.setPen(QPen(Qt::black, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
            painter.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
            return QCursor(icon, 16, 16);
        }();
        setCursor(rotateCursor);
        setToolTip("Yuvarlak ok tutamacını sürükleyerek döndürün. Shift: 15° adımlar.");
    } else if (handle >= TransformHandles::TopLeft && handle <= TransformHandles::Left) {
        // Choose a screen-relative resize cursor, including on an already rotated layer.
        const QPointF vector = transformBox().widgetPoint(handle, zoom, pan) -
                               transformBox().widgetPoint(TransformHandles::Move, zoom, pan);
        const qreal angle = qRadiansToDegrees(std::atan2(vector.y(), vector.x()));
        const int sector = (qRound(angle / 45) % 4 + 4) % 4;
        const Qt::CursorShape shapes[] = {Qt::SizeHorCursor, Qt::SizeFDiagCursor,
                                         Qt::SizeVerCursor, Qt::SizeBDiagCursor};
        setCursor(shapes[sector]);
    } else
        setCursor(Qt::ArrowCursor);
    if (handle != TransformHandles::Rotate)
        setToolTip("Katmanı dönüştür: köşeler boyutlandırır; yuvarlak ok döndürür. Enter uygular, Esc iptal eder.");
}

void Canvas::showNumericTransform() {
    if (!transformActive)
        return;
    const QPointer<Canvas> self(this);
    const quint64 generation = transformGeneration;
    transformDragging = false;
    Document previewDocument;
    previewDocument.state = previewState();
    previewDocument.state.layers[previewDocument.state.active].geometry = transformPreview;
    previewDocument.beginEdit();
    const QTransform before = transformPreview;
    connect(&previewDocument, &Document::updated, this, [this, &previewDocument] {
        if (transformActive)
            previewTransform(previewDocument.state.layers[previewDocument.state.active].geometry);
    });
    TransformDialog dialog(&previewDocument, window());
    dialog.setKeepAspectRatio(numericKeepAspectRatio);
    dialog.setWindowTitle("Katmanı dönüştür · Sayısal önizleme");
    if (auto *buttons = dialog.findChild<QDialogButtonBox *>())
        buttons->button(QDialogButtonBox::Ok)->setText("Önizlemeye uygula");
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (!self || generation != transformGeneration)
        return;
    numericKeepAspectRatio = dialog.keepAspectRatio();
    if (transformActive)
        previewTransform(accepted ? dialog.transform() : before);
    if (self)
        setFocus();
}
