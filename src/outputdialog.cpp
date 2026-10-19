#include "outputdialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
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
QByteArray rasterFormat(const QString &suffix) {
    QByteArray format = suffix.toLower().toLatin1();
    if (format == "jpg")
        format = "jpeg";
    if (format == "tif")
        format = "tiff";
    return QList<QByteArray>{"png", "jpeg", "webp", "bmp", "tiff"}.contains(format)
               ? format
               : QByteArray{};
}
QString outputPath(const QString &input, const QByteArray &format) {
    QString path = QDir::fromNativeSeparators(input.trimmed());
    if (path.isEmpty() || format.isEmpty())
        return {};
    const QFileInfo file(path);
    const QByteArray existingFormat = rasterFormat(file.suffix());
    if (existingFormat != format) {
        const QString suffix = format == "jpeg" ? "jpg" : QString::fromLatin1(format);
        // A known raster extension follows the format picker. Preserve other
        // dotted names, including .pdeck, by appending the export extension.
        path = existingFormat.isEmpty()
                   ? path + "." + suffix
                   : file.dir().filePath(file.completeBaseName() + "." + suffix);
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
bool sameOptions(const RasterExportOptions &a, const RasterExportOptions &b) {
    return a.size == b.size && a.quality == b.quality && a.dpi == b.dpi && a.profile == b.profile &&
           a.embedProfile == b.embedProfile;
}
} // namespace
class RasterOutputOptionsWidget : public QWidget {
  public:
    explicit RasterOutputOptionsWidget(QSize size, QWidget *parent = nullptr)
        : QWidget(parent), original(size) {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *form = new QFormLayout;
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
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
        auto *note = new QLabel(
            "Çalışma belgesi sRGB / 8 bittir. Çıktı boyutu belgeyi değiştirmez. ICC ve DPI kaydı "
            "dosya biçiminin desteğine bağlıdır; CMYK/proofing desteği değildir.");
        note->setWordWrap(true);
        layout->addWidget(note);
        connect(width, &QSpinBox::valueChanged, this, [this](int value) {
            if (ratio->isChecked() && original.width() > 0 && original.height() > 0) {
                QSignalBlocker blockHeight(height);
                const int requested = qRound(value * double(original.height()) / original.width());
                height->setValue(requested);
                // A bound on the paired dimension must constrain the edited one too.
                if (height->value() != requested) {
                    QSignalBlocker blockWidth(width);
                    width->setValue(
                        qRound(height->value() * double(original.width()) / original.height()));
                }
            }
        });
        connect(height, &QSpinBox::valueChanged, this, [this](int value) {
            if (ratio->isChecked() && original.width() > 0 && original.height() > 0) {
                QSignalBlocker blockWidth(width);
                const int requested = qRound(value * double(original.width()) / original.height());
                width->setValue(requested);
                if (width->value() != requested) {
                    QSignalBlocker blockHeight(height);
                    height->setValue(
                        qRound(width->value() * double(original.height()) / original.width()));
                }
            }
        });
    }
    RasterExportOptions options() const {
        RasterExportOptions o;
        o.size = {width->value(), height->value()};
        o.quality = quality->value();
        o.dpi = dpi->value();
        o.profile = profile->currentIndex();
        o.embedProfile = !bmp && embedded->isChecked();
        return o;
    }
    void setFormat(const QByteArray &format) {
        quality->setEnabled(format == "jpeg" || format == "webp");
        quality->setToolTip(quality->isEnabled() ? "JPEG / WebP kalitesi (1–100)"
                                               : "Kalite seçimi JPEG ve WebP çıktısında kullanılır.");
        const bool nextBmp = format == "bmp";
        if (nextBmp && !bmp) {
            rememberedEmbed = embedded->isChecked();
            embedded->setChecked(false);
        } else if (!nextBmp && bmp) {
            embedded->setChecked(rememberedEmbed);
        }
        bmp = nextBmp;
        embedded->setEnabled(!bmp);
        profile->setEnabled(!bmp && embedded->isChecked());
        if (!profileConnection) {
            profileConnection = connect(embedded, &QCheckBox::toggled, this,
                                        [this](bool checked) { profile->setEnabled(!bmp && checked); });
        }
    }

  private:
    QSize original;
    QSpinBox *width, *height, *quality, *dpi;
    QComboBox *profile;
    QCheckBox *embedded, *ratio;
    bool bmp = false, rememberedEmbed = true;
    QMetaObject::Connection profileConnection;
};
RasterOutputDialog::RasterOutputDialog(QSize size, QWidget *parent) : QDialog(parent) {
    setObjectName("rasterOutputDialog");
    setWindowTitle("Raster çıktı boyutu ve renk profili");
    auto *layout = new QVBoxLayout(this);
    settings = new RasterOutputOptionsWidget(size, this);
    layout->addWidget(settings);
    buttons(this, layout);
    resize(480, 420);
}
RasterExportOptions RasterOutputDialog::options() const { return settings->options(); }
void RasterOutputDialog::accept() {
    if (options().valid())
        QDialog::accept();
    else
        QMessageBox::warning(this, "Çıktı boyutu", "Çıktı 16 megapiksel sınırını aşıyor.");
}
ExportDialog::ExportDialog(QSize size, const QString &initialFile, QWidget *parent) : QDialog(parent) {
    setObjectName("exportDialog");
    setWindowTitle("Görüntüyü dışa aktar");
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("Görünür kompozisyonu görüntü dosyasına aktarın. "
                            "Katmanlı çalışma belgesi korunur.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    auto *destination = new QGroupBox("Dosya ve biçim");
    auto *destinationForm = new QFormLayout(destination);
    destinationForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    path = new QLineEdit;
    path->setObjectName("exportPath");
    path->setClearButtonEnabled(true);
    path->setAccessibleName("Dışa aktarım dosya yolu");
    path->setToolTip("Klasör ve dosya adı. Dosya uzantısı seçilen biçime göre düzenlenir.");
    QString initial = initialFile.isEmpty() ? "Adsız.png" : initialFile;
    if (QFileInfo(initial).isRelative()) {
        QString directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (directory.isEmpty() || !QDir(directory).exists())
            directory = QDir::homePath();
        initial = QDir(directory).filePath(initial);
    }
    path->setText(QDir::fromNativeSeparators(initial));
    auto *location = new QWidget;
    auto *locationLayout = new QHBoxLayout(location);
    locationLayout->setContentsMargins(0, 0, 0, 0);
    locationLayout->addWidget(path, 1);
    auto *browseButton = new QPushButton("Gözat…");
    browseButton->setObjectName("exportBrowse");
    browseButton->setAccessibleName("Dışa aktarım klasörünü seç");
    browseButton->setAutoDefault(false);
    locationLayout->addWidget(browseButton);
    destinationForm->addRow("Konum / dosya", location);
    format = new QComboBox;
    format->setObjectName("exportFormat");
    const auto supported = QImageWriter::supportedImageFormats();
    for (const auto &entry : QList<QPair<QString, QByteArray>>{{"PNG", "png"}, {"JPEG", "jpeg"},
                                                            {"WebP", "webp"}, {"BMP", "bmp"},
                                                            {"TIFF", "tiff"}}) {
        if (supported.contains(entry.second))
            format->addItem(entry.first, entry.second);
    }
    const int initialFormat = format->findData(rasterFormat(QFileInfo(initial).suffix()));
    if (initialFormat >= 0)
        format->setCurrentIndex(initialFormat);
    destinationForm->addRow("Biçim", format);
    formatNote = new QLabel;
    formatNote->setObjectName("exportFormatNote");
    formatNote->setWordWrap(true);
    destinationForm->addRow(formatNote);
    contentLayout->addWidget(destination);
    auto *output = new QGroupBox("Görüntü seçenekleri");
    auto *outputLayout = new QVBoxLayout(output);
    settings = new RasterOutputOptionsWidget(size, output);
    outputLayout->addWidget(settings);
    contentLayout->addWidget(output);
    contentLayout->addStretch();
    scroll->setWidget(content);
    // QScrollArea enables an opaque palette background on its child by default.
    // Keep the application's light/dark stylesheet surface visible behind labels.
    content->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    layout->addWidget(scroll, 1);
    validation = new QLabel;
    validation->setObjectName("exportValidation");
    validation->setTextFormat(Qt::PlainText);
    validation->setWordWrap(true);
    validation->hide();
    layout->addWidget(validation);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Cancel)->setText("İptal");
    auto *submit = box->addButton("Dışa aktar", QDialogButtonBox::AcceptRole);
    submit->setObjectName("exportSubmit");
    submit->setDefault(true);
    submit->setEnabled(format->count() > 0);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &ExportDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseButton, &QPushButton::clicked, this, &ExportDialog::browse);
    connect(format, &QComboBox::currentIndexChanged, this, &ExportDialog::synchronizeFormat);
    connect(path, &QLineEdit::editingFinished, this, &ExportDialog::synchronizeFormat);
    connect(path, &QLineEdit::textChanged, validation, &QWidget::hide);
    synchronizeFormat();
    setMinimumSize(480, 420);
    resize(660, 560);
}
QString ExportDialog::filePath() const {
    return outputPath(path->text(), format->currentData().toByteArray());
}
RasterExportOptions ExportDialog::options() const { return settings->options(); }
void ExportDialog::synchronizeFormat() {
    const QByteArray selected = format->currentData().toByteArray();
    settings->setFormat(selected);
    if (selected == "jpeg")
        formatNote->setText("JPEG: kayıplı sıkıştırma. Saydam alanlar beyaz arka plana birleştirilir.");
    else if (selected == "webp")
        formatNote->setText("WebP: kalite sıkıştırmayı belirler. Saydamlık korunur.");
    else if (selected == "png")
        formatNote->setText("PNG: kayıpsız görüntü ve saydamlık. Kalite ayarı kullanılmaz.");
    else if (selected == "bmp")
        formatNote->setText("BMP: RGB görüntü. Saydamlık ve ICC profili korunmaz; "
                            "kalite ayarı kullanılmaz.");
    else if (selected == "tiff")
        formatNote->setText("TIFF: tek görüntü, RGB 8 bit. Kodlama sistemin Qt eklentisine bağlıdır; "
                            "kalite ayarı kullanılmaz.");
    else
        formatNote->setText("Bu sistemde desteklenen bir görüntü kodlayıcısı bulunamadı.");
    // Do not turn an incomplete directory entry into a hidden image filename.
    const QString entered = QDir::fromNativeSeparators(path->text().trimmed());
    if (!entered.isEmpty() && !entered.endsWith('/') && !QFileInfo(entered).isDir()) {
        const QString normalized = filePath();
        if (!normalized.isEmpty() && normalized != path->text())
            path->setText(normalized);
    }
}
void ExportDialog::browse() {
    const QPointer<ExportDialog> self(this);
    const QString entered = path->text().trimmed();
    const QFileInfo current(entered);
    QString directory = current.isDir() ? current.absoluteFilePath() : current.absolutePath();
    if (!QDir(directory).exists())
        directory = QDir::homePath();
    const QString chosen =
        QFileDialog::getExistingDirectory(this, "Dışa aktarım klasörünü seç", directory);
    if (!self || chosen.isEmpty())
        return;
    QString name = current.isDir() ? QString() : current.fileName();
    if (name.isEmpty())
        name = "Adsız";
    path->setText(QDir(chosen).filePath(name));
    synchronizeFormat();
}
void ExportDialog::accept() {
    if (checking)
        return;
    checking = true;
    const bool presented = isVisible();
    const QPointer<ExportDialog> self(this);
    auto reset = qScopeGuard([self] {
        if (self)
            self->checking = false;
    });
    auto invalid = [this](const QString &message) {
        validation->setText(message);
        validation->show();
    };
    const QString entered = QDir::fromNativeSeparators(path->text().trimmed());
    if (entered.isEmpty() || entered.endsWith('/') || QFileInfo(entered).isDir()) {
        invalid("Klasörle birlikte bir dosya adı girin.");
        path->setFocus();
        return;
    }
    synchronizeFormat();
    const QString target = filePath();
    const QByteArray selected = format->currentData().toByteArray();
    const RasterExportOptions selectedOptions = options();
    if (selected.isEmpty() || !QImageWriter::supportedImageFormats().contains(selected)) {
        invalid("Seçilen görüntü biçimi bu sistemde desteklenmiyor.");
        return;
    }
    if (!selectedOptions.valid()) {
        invalid("Çıktı boyutu 16 megapiksel sınırını aşıyor. Genişliği veya yüksekliği azaltın.");
        return;
    }
    const QFileInfo file(target), directory(file.absolutePath());
    if (!directory.exists() || !directory.isDir() || !directory.isWritable()) {
        invalid("Hedef klasör bulunamadı veya yazılabilir değil. Başka bir konum seçin.");
        return;
    }
    if (file.exists() && !file.isFile()) {
        invalid("Bu konum normal bir dosya değil. Başka bir dosya adı seçin.");
        return;
    }
    if (file.exists() || file.isSymLink()) {
        QMessageBox overwrite(
            QMessageBox::Warning, "Dosyanın üzerine yazılsın mı?",
            target + "\n\nBu dosya zaten var. Dışa aktarılan görüntüyle değiştirilsin mi?",
            QMessageBox::Yes | QMessageBox::Cancel, this);
        overwrite.setObjectName("exportOverwriteDialog");
        overwrite.setTextFormat(Qt::PlainText);
        overwrite.button(QMessageBox::Yes)->setText("Üzerine yaz");
        overwrite.button(QMessageBox::Cancel)->setText("İptal");
        overwrite.setDefaultButton(QMessageBox::Cancel);
        overwrite.setEscapeButton(QMessageBox::Cancel);
        const int answer = overwrite.exec();
        if (!self || answer != QMessageBox::Yes || (presented && !isVisible()))
            return;
    }
    // A nested overwrite prompt must not approve a different destination or
    // different export settings changed by another event while it was open.
    if (target != filePath() || selected != format->currentData().toByteArray() ||
        !sameOptions(selectedOptions, options())) {
        invalid("Dışa aktarım ayarları değişti. Ayarları kontrol edip yeniden Dışa aktar'a basın.");
        return;
    }
    QDialog::accept();
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
