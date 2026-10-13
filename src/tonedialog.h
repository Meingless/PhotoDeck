#pragma once
#include "tones.h"
#include <QDialog>
#include <QWidget>

class CurveEditor : public QWidget {
    Q_OBJECT
  public:
    explicit CurveEditor(QWidget *parent = nullptr);
    QVector<QPoint> points() const { return curve; }
    void setPoints(const QVector<QPoint> &points);
    QPoint selectedPoint() const { return curve[selected]; }
    QPair<int, int> selectedInputRange() const;
    void setSelectedPoint(QPoint point);
    void removeSelectedPoint();
  signals:
    void changed();
    void pointSelected();

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    QVector<QPoint> curve{{0, 0}, {255, 255}};
    int selected = 0;
    QRectF graphRect() const;
    QPointF toWidget(QPoint point) const;
    QPoint toCurve(QPointF point) const;
};

class ToneDialog : public QDialog {
    Q_OBJECT
  public:
    explicit ToneDialog(const ToneSettings &settings, QWidget *parent = nullptr);
    ToneSettings settings() const { return current; }
  signals:
    void settingsChanged(const ToneSettings &settings);

  private:
    ToneSettings current;
    bool updating = false;
    void notify();
};
