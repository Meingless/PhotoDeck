#pragma once
#include "output.h"
#include <QDialog>
class QSpinBox;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class RasterOutputDialog : public QDialog {
    Q_OBJECT
  public:
    explicit RasterOutputDialog(QSize source, QWidget *parent = nullptr);
    RasterExportOptions options() const;
    void accept() override;

  private:
    QSize original;
    QSpinBox *width, *height, *quality, *dpi;
    QComboBox *profile;
    QCheckBox *embedded, *ratio;
};
class PdfOutputDialog : public QDialog {
    Q_OBJECT
  public:
    explicit PdfOutputDialog(QSize source, QWidget *parent = nullptr);
    PdfExportOptions options() const;
    void accept() override;

  private:
    QSpinBox *dpi;
    QComboBox *paper;
    QDoubleSpinBox *margin;
    QCheckBox *landscape;
};
