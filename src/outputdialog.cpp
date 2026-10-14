#include "outputdialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
namespace {
void buttons(QDialog *d, QVBoxLayout *layout) {
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);
    box->button(QDialogButtonBox::Ok)->setText("Devam");
    box->button(QDialogButtonBox::Cancel)->setText("İptal");
    QObject::connect(box, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, d, &QDialog::reject);
}
QSpinBox *spin(QString name, int min, int max, int value) {
    auto *s = new QSpinBox;
    s->setObjectName(name);
    s->setRange(min, max);
    s->setValue(value);
    return s;
}
} // namespace
RasterOutputDialog::RasterOutputDialog(QSize size, QWidget *parent) : QDialog(parent), original(size) {
    setObjectName("rasterOutputDialog");
    setWindowTitle("Raster çıktı boyutu ve renk profili");
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    width = spin("outputWidth", 1, 16384, size.width());
    height = spin("outputHeight", 1, 16384, size.height());
    quality = spin("outputQuality", 1, 100, 95);
    dpi = spin("outputDpi", 72, 1200, 300);
    ratio = new QCheckBox("Oranı koru");
    ratio->setChecked(true);
    ratio->setObjectName("outputRatio");
    profile = new QComboBox;
    profile->setObjectName("outputProfile");
    profile->addItems({"sRGB", "Display P3", "Adobe RGB"});
    embedded = new QCheckBox("ICC profilini çıktıya ekle");
    embedded->setObjectName("embedProfile");
    embedded->setChecked(true);
    form->addRow("Genişlik (px)", width);
    form->addRow("Yükseklik (px)", height);
    form->addRow(ratio);
    form->addRow("Kalite", quality);
    form->addRow("Baskı yoğunluğu (DPI)", dpi);
    form->addRow("RGB çıktı profili", profile);
    form->addRow(embedded);
    auto *note =
        new QLabel("Çalışma belgesi sRGB / 8 bittir. Çıktı boyutu belgeyi değiştirmez. ICC ve DPI kaydı "
                   "dosya biçiminin desteğine bağlıdır; CMYK/proofing desteği değildir.");
    note->setWordWrap(true);
    layout->addWidget(note);
    connect(width, &QSpinBox::valueChanged, this, [this](int value) {
        if (ratio->isChecked()) {
            QSignalBlocker block(height);
            height->setValue(qMax(1, qRound(value * double(original.height()) / original.width())));
        }
    });
    connect(height, &QSpinBox::valueChanged, this, [this](int value) {
        if (ratio->isChecked()) {
            QSignalBlocker block(width);
            width->setValue(qMax(1, qRound(value * double(original.width()) / original.height())));
        }
    });
    buttons(this, layout);
    resize(480, 420);
}
RasterExportOptions RasterOutputDialog::options() const {
    RasterExportOptions o;
    o.size = {width->value(), height->value()};
    o.quality = quality->value();
    o.dpi = dpi->value();
    o.profile = profile->currentIndex();
    o.embedProfile = embedded->isChecked();
    return o;
}
void RasterOutputDialog::accept() {
    if (options().valid())
        QDialog::accept();
    else
        QMessageBox::warning(this, "Çıktı boyutu", "Çıktı 16 megapiksel sınırını aşıyor.");
}
PdfOutputDialog::PdfOutputDialog(QSize size, QWidget *parent) : QDialog(parent) {
    setObjectName("pdfOutputDialog");
    setWindowTitle("Tek sayfalı PDF çıktı");
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    paper = new QComboBox;
    paper->setObjectName("pdfPaper");
    paper->addItems({"A4", "Letter", "Görüntünün fiziksel boyutu"});
    dpi = spin("pdfDpi", 72, 600, 300);
    margin = new QDoubleSpinBox;
    margin->setObjectName("pdfMargin");
    margin->setRange(0, 50);
    margin->setValue(10);
    margin->setSuffix(" mm");
    landscape = new QCheckBox("Yatay sayfa");
    landscape->setObjectName("pdfLandscape");
    landscape->setChecked(size.width() > size.height());
    form->addRow("Kağıt", paper);
    form->addRow("DPI", dpi);
    form->addRow("Kenar boşluğu", margin);
    form->addRow(landscape);
    auto *note = new QLabel(
        "Görünür kompozisyon oranı korunarak sayfaya sığdırılır. Şeffaf alanlar beyaz kağıt üzerinde "
        "görünür. PDF düz raster görüntüdür; katmanlı/vektör PDF veya PDF/X değildir.");
    note->setWordWrap(true);
    layout->addWidget(note);
    buttons(this, layout);
    resize(460, 300);
}
PdfExportOptions PdfOutputDialog::options() const {
    PdfExportOptions o;
    o.dpi = dpi->value();
    o.paper = paper->currentIndex();
    o.marginMillimeters = margin->value();
    o.landscape = landscape->isChecked();
    return o;
}
void PdfOutputDialog::accept() {
    if (options().valid())
        QDialog::accept();
}
