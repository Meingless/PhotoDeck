#pragma once
#include "brushpreset.h"
#include "document.h"
#include "transformhandles.h"
#include <QPointer>
#include <QWidget>

class QFrame;
class QLabel;
class QToolButton;

class Canvas : public QWidget {
    Q_OBJECT
  public:
    enum Tool {
        Move,
        Select,
        Crop,
        Brush,
        Pencil,
        Eraser,
        Clone,
        Fill,
        Gradient,
        Text,
        Rectangle,
        Ellipse,
        Picker,
        Hand,
        EllipseSelect,
        Lasso,
        PolygonLasso,
        Wand,
        ToolCount
    };
    explicit Canvas(Document *document, QWidget *parent = nullptr);
    Document *document;
    Tool tool = Move;
    QColor foreground = QColor("#59c9b4"), background = Qt::white;
    BrushSettings brush;
    Selection::Mode selectionMode = Selection::Replace;
    int selectionTolerance = 24;
    bool contiguousSelection = true;
    bool isSelectionTool() const;
    qreal zoom = 1;
    bool showRulers = true;
    void setUiColors(QColor canvas, QColor panel, QColor text, QColor accent);
    void setTool(Tool tool);
    void fit();
    void actualSize();
    void setZoom(qreal value, QPointF anchor = {});
    QPointF toImage(QPointF point) const;
    QPointF toWidget(QPointF point) const;
    bool finishPendingGesture();
    bool hasPendingGesture() const {
        return dragging || panning || polygonActive || tabletActive || transformActive;
    }
    bool beginTransform();
    bool applyTransform();
    void cancelTransform();
    void showNumericTransform();
    bool isTransforming() const { return transformActive; }
    QTransform transformGeometry() const { return transformPreview; }
    QPointF transformHandlePosition(TransformHandles::Handle handle) const;
    void deleteSelection();
  signals:
    void toolHint(QString hint);
    void zoomChanged();
    void colorPicked(QColor color);
    void textRequested(QPoint point);
    void transformSessionChanged(bool active);

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void tabletEvent(QTabletEvent *) override;

  private:
    QColor canvasColor = QColor("#171b20"), rulerColor = QColor("#22272e"),
           rulerText = QColor("#84909b"), accentColor = QColor("#59c9b4");
    QPointF pan, press, last, widgetLast, hover;
    QPoint initialOffset, cloneAnchor;
    bool dragging = false, panning = false, hasClone = false;
    Selection selectionBefore;
    quint64 selectionRevision = 0;
    Selection::Mode gestureSelectionMode = Selection::Replace;
    QVector<QPointF> selectionPoints;
    bool polygonActive = false;
    void completePolygon();
    void selectArea(const Selection &area);
    Selection::Mode effectiveSelectionMode(Qt::KeyboardModifiers modifiers) const;
    QImage compositeCache, cloneSource;
    QRectF preview;
    bool cacheDirty = true, fitted = false;
    QImage &target();
    void advanceGesture(QPointF position, Qt::KeyboardModifiers modifiers);
    void stroke(QPointF from, QPointF to, bool final = false);
    void stamp(QPointF at, qreal pressure);
    qreal stampDiameter(qreal pressure) const;
    QTransform tipTransform(QPointF position) const;
    const QImage &coloredTip(QColor color);
    QImage tipCache;
    QColor tipCacheColor;
    qint64 tipCacheKey = 0;
    bool tabletActive = false, tabletEraser = false, hasStrokeStamp = false;
    qreal inputPressure = 1, lastInputPressure = 1, tabletTiltX = 0, tabletTiltY = 0;
    qreal remainingStampDistance = 0;
    QPointF lastStamp;
    void fillAt(QPoint point);
    void drawShape();
    void applyGradient();
    void updateCursor();
    QPointer<Document> keyboardDocument;
    int nudgeKey = 0;
    quint64 nudgeRevision = 0, nudgeGeneration = 0, nudgeUpdateRevision = 0;
    int nudgeUpdates = 0;
    bool applyingNudge = false;
    void endKeyboardNudge();
    bool transformActive = false, transformDragging = false;
    int transformLayer = -1;
    QTransform transformPreview;
    Layer transformSource;
    QPointer<Document> transformDocument;
    quint64 transformRevision = 0, transformGeneration = 0;
    QSize transformCanvasSize;
    qsizetype transformLayerCount = 0;
    TransformHandles transformGesture;
    TransformHandles::Handle transformHandle = TransformHandles::None;
    QPointF transformPress;
    QFrame *transformControls;
    QLabel *transformCaption, *transformInfo;
    QToolButton *transformApply, *transformCancel, *transformNumeric;
    TransformHandles transformBox() const;
    bool previewTransform(const QTransform &geometry);
    bool transformSourceMatches() const;
    void advanceTransform(QPointF position, Qt::KeyboardModifiers modifiers);
    void endTransformSession();
    void positionTransformControls();
    void paintTransform(QPainter &painter);
    void updateTransformCursor(QPointF widgetPosition);
};
