#pragma once
#include "output.h"
#include <QDialog>
class QSpinBox;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class RasterOutputOptionsWidget;
class RasterOutputDialog : public QDialog {
    Q_OBJECT
  public:
    explicit RasterOutputDialog(QSize source, QWidget *parent = nullptr);
    RasterExportOptions options() const;
    void accept() override;

  private:
    RasterOutputOptionsWidget *settings;
};
class ExportDialog : public QDialog {
    Q_OBJECT
  public:
    explicit ExportDialog(QSize source, const QString &initialFile, QWidget *parent = nullptr);
    QString filePath() const;
    RasterExportOptions options() const;
    void accept() override;

  private:
    void synchronizeFormat();
    void browse();
    QLineEdit *path;
    QComboBox *format;
    QLabel *formatNote, *validation;
    RasterOutputOptionsWidget *settings;
    bool checking = false;
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
