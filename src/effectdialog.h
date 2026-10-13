#pragma once
#include "document.h"
#include <QDialog>
#include <QTimer>

class QCheckBox;
class FilterDialog : public QDialog {
    Q_OBJECT
  public:
    explicit FilterDialog(Document *document, QWidget *parent = nullptr);
    void accept() override;

  private:
    Document *document;
    FilterSettings original, current;
    QTimer previewTimer;
    QCheckBox *live;
    void preview();
};
class MaskDialog : public QDialog {
    Q_OBJECT
  public:
    explicit MaskDialog(Document *document, QWidget *parent = nullptr);
    void accept() override;

  private:
    Document *document;
    MaskSettings current;
};
