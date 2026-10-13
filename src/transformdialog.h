#pragma once
#include "document.h"
#include <QDialog>
#include <array>

class QDoubleSpinBox;
class QTabWidget;
class QLabel;
class QDialogButtonBox;
class TransformDialog : public QDialog {
    Q_OBJECT
  public:
    explicit TransformDialog(Document *document, QWidget *parent = nullptr);
    QTransform transform() const;
    void accept() override;

  private:
    Layer source;
    Document *document;
    QTabWidget *tabs;
    QDoubleSpinBox *sx, *sy, *angle, *skewX, *skewY, *dx, *dy;
    std::array<QDoubleSpinBox *, 4> cornerX, cornerY;
    QLabel *message;
    QDialogButtonBox *buttons;
    void preview();
};
