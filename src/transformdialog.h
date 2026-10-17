#pragma once
#include "document.h"
#include <QDialog>
#include <array>

class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QTabWidget;
class QLabel;
class QDialogButtonBox;
class TransformDialog : public QDialog {
    Q_OBJECT
  public:
    explicit TransformDialog(Document *document, QWidget *parent = nullptr);
    QTransform transform() const;
    bool keepAspectRatio() const;
    void setKeepAspectRatio(bool keep);
    void accept() override;

  private:
    Layer source;
    Document *document;
    QTabWidget *tabs;
    QCheckBox *keepRatio;
    QComboBox *anchor;
    QDoubleSpinBox *sx, *sy, *angle, *skewX, *skewY, *dx, *dy;
    std::array<QDoubleSpinBox *, 4> cornerX, cornerY;
    QLabel *message, *metrics;
    QDialogButtonBox *buttons;
    void preview();
    void updateMetrics();
};
