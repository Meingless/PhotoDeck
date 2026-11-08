#include "transformdialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineF>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

namespace {
void dialogSurface(QWidget *widget) {
    // Scroll viewports and their plain content widgets must use the same
    // surface/text pair as the dialog, including with custom theme colors.
    widget->setProperty("pdSurface", "dialog");
    widget->setAttribute(Qt::WA_StyledBackground);
    widget->setAutoFillBackground(true);
}
void prepareForm(QFormLayout *form) {
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
}
} // namespace

TransformDialog::TransformDialog(Document *d, QWidget *parent)
    : QDialog(parent), source(d->state.layers[d->state.active]), document(d) {
    setObjectName("transformDialog");
    dialogSurface(this);
    setWindowTitle("Katmanı dönüştür · Sayısal ayarlar");
    setSizeGripEnabled(true);
    auto *layout = new QVBoxLayout(this);
    auto *note =
        new QLabel("Kaynak korunur; ölçek ve açı açılışa göredir. "
                   "İptal önceki önizlemeye döner.");
    note->setObjectName("transformNote");
    note->setToolTip("Kaynak pikseller ve düzenlenebilir metin korunur; maske katmanla birlikte dönüşür.");
    note->setWordWrap(true);
    layout->addWidget(note);
    tabs = new QTabWidget;
    tabs->setObjectName("transformTabs");
    tabs->setUsesScrollButtons(true);
    layout->addWidget(tabs);
    auto *affine = new QWidget;
    affine->setObjectName("transformAffinePage");
    auto *form = new QFormLayout(affine);
    prepareForm(form);
    auto addPage = [this](QWidget *content, const QString &title) {
        auto *scroll = new QScrollArea;
        scroll->setObjectName(content->objectName() + "Scroll");
        dialogSurface(scroll);
        dialogSurface(scroll->viewport());
        dialogSurface(content);
        scroll->setWidgetResizable(true);
        scroll->setWidget(content);
        scroll->setFrameShape(QFrame::NoFrame);
        tabs->addTab(scroll, title);
    };
    addPage(affine, "Ölçek / dönüş / eğme");
    auto field = [&](QFormLayout *f, QString name, QString id, double value, double low, double high) {
        auto *spin = new QDoubleSpinBox;
        spin->setObjectName(id);
        spin->setRange(low, high);
        spin->setDecimals(2);
        spin->setValue(value);
        spin->setAccessibleName(name);
        auto *label = new QLabel(name);
        label->setWordWrap(true);
        label->setBuddy(spin);
        f->addRow(label, spin);
        return spin;
    };
    keepRatio = new QCheckBox("Yatay / dikey ölçek oranını &koru");
    keepRatio->setObjectName("transformKeepRatio");
    keepRatio->setAccessibleName("Yatay ve dikey ölçek oranını koru");
    keepRatio->setToolTip("Açıkken iki ölçek aynı yüzdeyi kullanır. Kapatınca eksenler bağımsızdır. "
                          "Bu seçenek yalnızca bu sayısal alanları bağlar.");
    keepRatio->setAccessibleDescription(keepRatio->toolTip());
    keepRatio->setChecked(true);
    form->addRow(keepRatio);
    sx = field(form, "Yatay ölçek (%)", "scaleX", 100, -10000, 10000);
    sy = field(form, "Dikey ölçek (%)", "scaleY", 100, -10000, 10000);
    angle = field(form, "Açı (°)", "angle", 0, -360, 360);
    sx->setToolTip("Açılıştaki görünümün yatay ölçeği. Negatif yüzde yatay aynalar.");
    sy->setToolTip("Açılıştaki görünümün dikey ölçeği. Negatif yüzde dikey aynalar.");
    angle->setToolTip("Açılıştaki görünüme eklenen dönüş açısı.");
    anchor = new QComboBox;
    anchor->setObjectName("transformAnchor");
    const QStringList anchors{"Sol üst", "Üst orta", "Sağ üst", "Sol orta", "Merkez", "Sağ orta",
                              "Sol alt", "Alt orta", "Sağ alt"};
    anchor->addItems(anchors);
    anchor->setCurrentIndex(4);
    anchor->setAccessibleName("Sabitleme noktası");
    anchor->setToolTip("Ölçek, dönüş ve eğme bu noktayı sabit tutar; kaydırma buna eklenir. "
                       "Noktalar açılıştaki görünümün eksene hizalı sınırına göredir.");
    anchor->setAccessibleDescription(anchor->toolTip());
    auto *anchorLabel = new QLabel("&Sabitleme noktası");
    anchorLabel->setWordWrap(true);
    anchorLabel->setBuddy(anchor);
    form->addRow(anchorLabel, anchor);
    skewX = field(form, "Yatay eğme (°)", "skewX", 0, -80, 80);
    skewY = field(form, "Dikey eğme (°)", "skewY", 0, -80, 80);
    dx = field(form, "Yatay kaydırma (px)", "translateX", 0, -Document::MaxCoordinate,
               Document::MaxCoordinate);
    dy = field(form, "Dikey kaydırma (px)", "translateY", 0, -Document::MaxCoordinate,
               Document::MaxCoordinate);
    auto *perspective = new QWidget;
    perspective->setObjectName("transformPerspectivePage");
    auto *corners = new QFormLayout(perspective);
    prepareForm(corners);
    addPage(perspective, "Perspektif / dört köşe");
    auto *cornerNote = new QLabel("Köşe X / Y değerleri katman başlangıcına göre pikseldir. "
                                  "Bu sekmede köşeler bağımsızdır.");
    cornerNote->setWordWrap(true);
    corners->addRow(cornerNote);
    QPolygonF sourceCorners{{0, 0},
                            {qreal(source.sourceSize().width()), 0},
                            {qreal(source.sourceSize().width()), qreal(source.sourceSize().height())},
                            {0, qreal(source.sourceSize().height())}};
    const QStringList names{"Sol üst", "Sağ üst", "Sağ alt", "Sol alt"};
    for (int i = 0; i < 4; ++i) {
        QPointF at = source.geometry.map(sourceCorners[i]);
        cornerX[i] = field(corners, names[i] + " X", "cornerX" + QString::number(i), at.x(),
                           -Document::MaxCoordinate, Document::MaxCoordinate);
        cornerY[i] = field(corners, names[i] + " Y", "cornerY" + QString::number(i), at.y(),
                           -Document::MaxCoordinate, Document::MaxCoordinate);
    }
    metrics = new QLabel;
    metrics->setObjectName("transformMetrics");
    metrics->setWordWrap(true);
    metrics->setToolTip("Görünüm ölçüsü eksene hizalı sınırdır; dönüş ve perspektifi de kapsar. "
                        "Ölçekler üst ve sol kenar uzunluklarının kaynak boyutuna oranıdır. "
                        "Açı üst kenarın yönüdür.");
    metrics->setAccessibleDescription(metrics->toolTip());
    layout->addWidget(metrics);
    message = new QLabel;
    message->setObjectName("transformMessage");
    message->setWordWrap(true);
    message->setToolTip("Dönüşümden sonra piksel boyaması için Katman → Görünümü rasterleştir kullanılabilir.");
    layout->addWidget(message);
    buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Uygula");
    buttons->button(QDialogButtonBox::Cancel)->setText("İptal");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &TransformDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (auto *spin : findChildren<QDoubleSpinBox *>())
        if (spin != sx && spin != sy)
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double) { preview(); });
    auto linkScale = [this](QDoubleSpinBox *other, double value) {
        if (keepRatio->isChecked()) {
            QSignalBlocker blocker(other);
            other->setValue(value);
        }
        preview();
    };
    connect(sx, &QDoubleSpinBox::valueChanged, this,
            [linkScale, this](double value) { linkScale(sy, value); });
    connect(sy, &QDoubleSpinBox::valueChanged, this,
            [linkScale, this](double value) { linkScale(sx, value); });
    connect(keepRatio, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            QSignalBlocker blocker(sy);
            sy->setValue(sx->value());
            preview();
        }
    });
    connect(anchor, &QComboBox::currentIndexChanged, this, [this](int) { preview(); });
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) {
            const auto &matrix = document->state.layers[document->state.active].geometry;
            const QPolygonF corners{{0, 0},
                                    {qreal(source.sourceSize().width()), 0},
                                    {qreal(source.sourceSize().width()), qreal(source.sourceSize().height())},
                                    {0, qreal(source.sourceSize().height())}};
            for (int i = 0; i < 4; ++i) {
                QSignalBlocker x(cornerX[i]), y(cornerY[i]);
                const QPointF point = matrix.map(corners[i]);
                cornerX[i]->setValue(point.x());
                cornerY[i]->setValue(point.y());
            }
        }
        preview();
    });
    QWidget *previous = keepRatio;
    const QList<QWidget *> affineFields{sx, sy, angle, anchor, skewX, skewY, dx, dy};
    for (QWidget *field : affineFields) {
        setTabOrder(previous, field);
        previous = field;
    }
    for (int i = 0; i < 4; ++i) {
        setTabOrder(previous, cornerX[i]);
        setTabOrder(cornerX[i], cornerY[i]);
        previous = cornerY[i];
    }
    setTabOrder(previous, buttons->button(QDialogButtonBox::Ok));
    setTabOrder(buttons->button(QDialogButtonBox::Ok), buttons->button(QDialogButtonBox::Cancel));
    sx->setFocus();
    resize(510, 520);
    preview();
}
bool TransformDialog::keepAspectRatio() const {
    return keepRatio->isChecked();
}
void TransformDialog::setKeepAspectRatio(bool keep) {
    keepRatio->setChecked(keep);
}
QTransform TransformDialog::transform() const {
    if (tabs->currentIndex() == 1) {
        QPolygonF from{{0, 0},
                       {qreal(source.sourceSize().width()), 0},
                       {qreal(source.sourceSize().width()), qreal(source.sourceSize().height())},
                       {0, qreal(source.sourceSize().height())}};
        QPolygonF to;
        for (int i = 0; i < 4; ++i)
            to.append({cornerX[i]->value(), cornerY[i]->value()});
        QTransform result;
        if (!QTransform::quadToQuad(from, to, result))
            return QTransform(0, 0, 0, 0, 0, 0);
        return result;
    }
    const QRectF bounds(source.transformedBounds());
    const int point = anchor->currentIndex();
    const QPointF center(bounds.left() + bounds.width() * (point % 3) / 2,
                         bounds.top() + bounds.height() * (point / 3) / 2);
    QTransform delta;
    delta.translate(center.x() + dx->value(), center.y() + dy->value());
    delta.rotate(angle->value());
    delta.shear(std::tan(qDegreesToRadians(skewX->value())),
                std::tan(qDegreesToRadians(skewY->value())));
    delta.scale(sx->value() / 100, sy->value() / 100);
    delta.translate(-center.x(), -center.y());
    return source.geometry * delta;
}
void TransformDialog::preview() {
    Layer candidate = source;
    candidate.geometry = transform();
    QString error;
    bool valid = document->setLayerTransform(candidate.geometry, &error);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
    message->setText(valid ? "Uygula: tek geri alma adımı."
                           : error + " Son geçerli önizleme korunur.");
    updateMetrics();
}
void TransformDialog::updateMetrics() {
    const Layer &display = document->state.layers[document->state.active];
    const QRect bounds = display.transformedBounds();
    const auto &matrix = display.geometry;
    const QPointF origin = matrix.map(QPointF(0, 0));
    const QPointF horizontal = matrix.map(QPointF(source.sourceSize().width(), 0)) - origin;
    const QPointF vertical = matrix.map(QPointF(0, source.sourceSize().height())) - origin;
    const double scaleX = QLineF(QPointF(), horizontal).length() / source.sourceSize().width() * 100;
    const double scaleY = QLineF(QPointF(), vertical).length() / source.sourceSize().height() * 100;
    const double rotation = qRadiansToDegrees(std::atan2(horizontal.y(), horizontal.x()));
    const QString sourceLabel = source.isSmartObject() ? "Akıllı nesne çerçevesi" : "Kaynak";
    metrics->setText(QString("%8: %1 × %2 px · Görünüm sınırı: %3 × %4 px (eksene hizalı)\n"
                             "Üst / sol kenar: %%5 / %%6 · Üst kenar açısı: %7°")
                         .arg(source.sourceSize().width()).arg(source.sourceSize().height())
                         .arg(bounds.width()).arg(bounds.height())
                         .arg(scaleX, 0, 'f', 1).arg(scaleY, 0, 'f', 1).arg(rotation, 0, 'f', 1).arg(sourceLabel));
    if (source.isSmartObject())
        metrics->setText(metrics->text() + QString("\nGömülü kaynak: %1 × %2 px · İçeriği değiştir ile yenilenebilir.")
                                             .arg(source.pixels.width()).arg(source.pixels.height()));
}
void TransformDialog::accept() {
    if (document->setLayerTransform(transform()))
        QDialog::accept();
}
