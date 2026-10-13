#include "transformdialog.h"
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

TransformDialog::TransformDialog(Document *d, QWidget *parent)
    : QDialog(parent), source(d->state.layers[d->state.active]), document(d) {
    setObjectName("transformDialog");
    setWindowTitle("Kaynak koruyan serbest dönüşüm");
    auto *layout = new QVBoxLayout(this);
    auto *note =
        new QLabel("Kaynak pikseller ve metin korunur; maske katmanla birlikte dönüşür. Köşe konumları "
                   "katman başlangıcına göre pikseldir. İptal önceki görünümü geri getirir.");
    note->setWordWrap(true);
    layout->addWidget(note);
    tabs = new QTabWidget;
    tabs->setObjectName("transformTabs");
    layout->addWidget(tabs);
    auto *affine = new QWidget;
    auto *form = new QFormLayout(affine);
    auto addPage = [this](QWidget *content, const QString &title) {
        auto *scroll = new QScrollArea;
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
        f->addRow(name, spin);
        return spin;
    };
    sx = field(form, "Yatay ölçek (%)", "scaleX", 100, -10000, 10000);
    sy = field(form, "Dikey ölçek (%)", "scaleY", 100, -10000, 10000);
    angle = field(form, "Açı (°)", "angle", 0, -360, 360);
    skewX = field(form, "Yatay eğme (°)", "skewX", 0, -80, 80);
    skewY = field(form, "Dikey eğme (°)", "skewY", 0, -80, 80);
    dx = field(form, "Yatay kaydırma (px)", "translateX", 0, -Document::MaxCoordinate,
               Document::MaxCoordinate);
    dy = field(form, "Dikey kaydırma (px)", "translateY", 0, -Document::MaxCoordinate,
               Document::MaxCoordinate);
    auto *perspective = new QWidget;
    auto *corners = new QFormLayout(perspective);
    addPage(perspective, "Perspektif / dört köşe");
    QPolygonF sourceCorners{{0, 0},
                            {qreal(source.pixels.width()), 0},
                            {qreal(source.pixels.width()), qreal(source.pixels.height())},
                            {0, qreal(source.pixels.height())}};
    const QStringList names{"Sol üst", "Sağ üst", "Sağ alt", "Sol alt"};
    for (int i = 0; i < 4; ++i) {
        QPointF at = source.geometry.map(sourceCorners[i]);
        cornerX[i] = field(corners, names[i] + " X", "cornerX" + QString::number(i), at.x(),
                           -Document::MaxCoordinate, Document::MaxCoordinate);
        cornerY[i] = field(corners, names[i] + " Y", "cornerY" + QString::number(i), at.y(),
                           -Document::MaxCoordinate, Document::MaxCoordinate);
    }
    message = new QLabel;
    message->setObjectName("transformMessage");
    message->setWordWrap(true);
    layout->addWidget(message);
    buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Uygula");
    buttons->button(QDialogButtonBox::Cancel)->setText("İptal");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &TransformDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (auto *spin : findChildren<QDoubleSpinBox *>())
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double) { preview(); });
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) {
            const auto &matrix = document->state.layers[document->state.active].geometry;
            const QPolygonF corners{{0, 0},
                                    {qreal(source.pixels.width()), 0},
                                    {qreal(source.pixels.width()), qreal(source.pixels.height())},
                                    {0, qreal(source.pixels.height())}};
            for (int i = 0; i < 4; ++i) {
                QSignalBlocker x(cornerX[i]), y(cornerY[i]);
                const QPointF point = matrix.map(corners[i]);
                cornerX[i]->setValue(point.x());
                cornerY[i]->setValue(point.y());
            }
        }
        preview();
    });
    resize(470, 460);
    preview();
}
QTransform TransformDialog::transform() const {
    if (tabs->currentIndex() == 1) {
        QPolygonF from{{0, 0},
                       {qreal(source.pixels.width()), 0},
                       {qreal(source.pixels.width()), qreal(source.pixels.height())},
                       {0, qreal(source.pixels.height())}};
        QPolygonF to;
        for (int i = 0; i < 4; ++i)
            to.append({cornerX[i]->value(), cornerY[i]->value()});
        QTransform result;
        if (!QTransform::quadToQuad(from, to, result))
            return QTransform(0, 0, 0, 0, 0, 0);
        return result;
    }
    QPointF center = QRectF(source.transformedBounds()).center();
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
    message->setText(
        valid ? "Uygula: tek geri alma adımı. Piksel boyaması için daha sonra rasterleştirebilirsiniz."
              : error + " Son geçerli önizleme korunur.");
}
void TransformDialog::accept() {
    if (document->setLayerTransform(transform()))
        QDialog::accept();
}
