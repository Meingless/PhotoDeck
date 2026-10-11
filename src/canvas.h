#pragma once
#include "document.h"
#include <QWidget>

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
        Hand
    };
    explicit Canvas(Document *document, QWidget *parent = nullptr);
    Document *document;
    Tool tool = Move;
    QColor foreground = QColor("#59c9b4"), background = Qt::white;
    int brushSize = 32, hardness = 80, brushOpacity = 100;
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
    void deleteSelection();
  signals:
    void toolHint(QString hint);
    void zoomChanged();
    void colorPicked(QColor color);
    void textRequested(QPoint point);

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

  private:
    QColor canvasColor = QColor("#171b20"), rulerColor = QColor("#22272e"),
           rulerText = QColor("#84909b"), accentColor = QColor("#59c9b4");
    QPointF pan, press, last, widgetLast, hover;
    QPoint initialOffset, cloneAnchor;
    bool dragging = false, panning = false, hasClone = false;
    QImage compositeCache, cloneSource;
    QRectF preview;
    bool cacheDirty = true, fitted = false;
    QImage &target();
    void advanceGesture(QPointF position, Qt::KeyboardModifiers modifiers);
    void stroke(QPointF from, QPointF to);
    void fillAt(QPoint point);
    void drawShape();
    void applyGradient();
    void updateCursor();
};
