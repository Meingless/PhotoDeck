#include "effectdialog.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

FilterDialog::FilterDialog(Document *d, QWidget *parent)
    : QDialog(parent), document(d), original(d->state.layers[d->state.active].filters),
      current(original) {
    setObjectName("filterDialog");
    setWindowTitle("Kaynak koruyan klasik filtreler");
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel("Etkin katmanın kaynak pikselleri korunur. Sıra: pikselleştirme → gren → "
                            "Gaussian → high-pass → keskinleştirme.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *content = new QWidget;
    auto *form = new QFormLayout(content);
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    layout->addWidget(scroll);
    auto field = [&](QString label, QString id, int min, int max, int FilterSettings::*member) {
        auto *spin = new QSpinBox;
        spin->setObjectName(id);
        spin->setRange(min, max);
        spin->setValue(current.*member);
        form->addRow(label, spin);
        connect(spin, &QSpinBox::valueChanged, this, [this, member](int value) {
            current.*member = value;
            previewTimer.start();
        });
    };
    field("Gaussian yarıçapı (px)", "gaussian", 0, 16, &FilterSettings::gaussian);
    field("Keskinleştirme (%)", "sharpenAmount", 0, 300, &FilterSettings::sharpenAmount);
    field("Keskinleştirme yarıçapı (px)", "sharpenRadius", 1, 16, &FilterSettings::sharpenRadius);
    field("Keskinleştirme eşiği", "sharpenThreshold", 0, 255, &FilterSettings::sharpenThreshold);
    field("High-pass yarıçapı (px)", "highPass", 0, 16, &FilterSettings::highPass);
    field("Gren miktarı (%)", "noise", 0, 100, &FilterSettings::noise);
    field("Pikselleştirme blok boyutu", "mosaic", 1, 128, &FilterSettings::mosaic);
    auto *mono = new QCheckBox("Tek renkli gren");
    mono->setObjectName("monochromeNoise");
    mono->setChecked(current.monochromeNoise);
    form->addRow(mono);
    connect(mono, &QCheckBox::toggled, this, [this](bool value) {
        current.monochromeNoise = value;
        previewTimer.start();
    });
    auto *seed = new QLabel(QString::number(current.noiseSeed));
    seed->setObjectName("noiseSeed");
    form->addRow("Gren dizisi", seed);
    auto *random = new QPushButton("Yeni gren dizisi");
    random->setObjectName("newNoiseSeed");
    form->addRow(random);
    connect(random, &QPushButton::clicked, this, [this, seed] {
        current.noiseSeed = QRandomGenerator::global()->generate();
        seed->setText(QString::number(current.noiseSeed));
        previewTimer.start();
    });
    live = new QCheckBox("Canlı önizleme");
    live->setObjectName("liveFilterPreview");
    live->setChecked(true);
    layout->addWidget(live);
    previewTimer.setSingleShot(true);
    previewTimer.setInterval(60);
    connect(&previewTimer, &QTimer::timeout, this, &FilterDialog::preview);
    connect(live, &QCheckBox::toggled, this, [this](bool enabled) {
        previewTimer.stop();
        if (enabled)
            preview();
        else
            document->adjustFilters(original);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText("Uygula");
    buttons->button(QDialogButtonBox::Cancel)->setText("İptal");
    connect(buttons, &QDialogButtonBox::accepted, this, &FilterDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(510, 500);
}
void FilterDialog::preview() {
    if (live->isChecked())
        document->adjustFilters(current);
}
void FilterDialog::accept() {
    previewTimer.stop();
    if (document->adjustFilters(current))
        QDialog::accept();
}

MaskDialog::MaskDialog(Document *d, QWidget *parent)
    : QDialog(parent), document(d), current(d->state.layers[d->state.active].maskSettings) {
    setObjectName("maskDialog");
    setWindowTitle("Kaynak koruyan maske ayarları");
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel("Maske pikselleri değişmez. Yoğunluk 0 maskeyi etkisizleştirir; Gaussian "
                            "yumuşatma kenarları bulanıklaştırır.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto *enabled = new QCheckBox("Maskeyi etkinleştir");
    enabled->setObjectName("maskEnabled");
    enabled->setChecked(current.enabled);
    form->addRow(enabled);
    auto *density = new QSpinBox;
    density->setObjectName("maskDensity");
    density->setRange(0, 100);
    density->setValue(current.density);
    density->setSuffix(" %");
    form->addRow("Yoğunluk", density);
    auto *feather = new QSpinBox;
    feather->setObjectName("maskFeather");
    feather->setRange(0, 16);
    feather->setValue(current.feather);
    feather->setSuffix(" px");
    form->addRow("Kenar yumuşatma", feather);
    connect(enabled, &QCheckBox::toggled, this, [this](bool value) {
        current.enabled = value;
        document->adjustMask(current);
    });
    connect(density, &QSpinBox::valueChanged, this, [this](int value) {
        current.density = value;
        document->adjustMask(current);
    });
    connect(feather, &QSpinBox::valueChanged, this, [this](int value) {
        current.feather = value;
        document->adjustMask(current);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText("Uygula");
    buttons->button(QDialogButtonBox::Cancel)->setText("İptal");
    connect(buttons, &QDialogButtonBox::accepted, this, &MaskDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
void MaskDialog::accept() {
    if (document->adjustMask(current))
        QDialog::accept();
}
