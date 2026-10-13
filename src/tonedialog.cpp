#include "tonedialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

CurveEditor::CurveEditor(QWidget *parent) : QWidget(parent) {
    setMinimumSize(260, 220);
    setFocusPolicy(Qt::StrongFocus);
    setObjectName("curveEditor");
    setAccessibleName("Ton eğrisi");
    setToolTip(
        "Tıklayarak nokta ekleyin; ok tuşlarıyla değiştirin. PgUp/PgDn nokta seçer; Delete siler.");
}
QRectF CurveEditor::graphRect() const {
    return QRectF(rect()).adjusted(24, 12, -12, -24);
}
QPointF CurveEditor::toWidget(QPoint point) const {
    QRectF r = graphRect();
    return {r.left() + point.x() * r.width() / 255, r.bottom() - point.y() * r.height() / 255};
}
QPoint CurveEditor::toCurve(QPointF point) const {
    QRectF r = graphRect();
    return {qBound(0, qRound((point.x() - r.left()) * 255 / r.width()), 255),
            qBound(0, qRound((r.bottom() - point.y()) * 255 / r.height()), 255)};
}
void CurveEditor::setPoints(const QVector<QPoint> &points) {
    ToneSettings check;
    check.curves[0] = points;
    if (!check.valid())
        return;
    curve = points;
    selected = qMin(selected, int(curve.size() - 1));
    update();
    emit pointSelected();
}
QPair<int, int> CurveEditor::selectedInputRange() const {
    if (selected == 0)
        return {0, 0};
    if (selected == curve.size() - 1)
        return {255, 255};
    return {curve[selected - 1].x() + 1, curve[selected + 1].x() - 1};
}
void CurveEditor::setSelectedPoint(QPoint point) {
    auto range = selectedInputRange();
    point.setX(qBound(range.first, point.x(), range.second));
    point.setY(qBound(0, point.y(), 255));
    if (point == curve[selected])
        return;
    curve[selected] = point;
    update();
    emit pointSelected();
    emit changed();
}
void CurveEditor::removeSelectedPoint() {
    if (selected == 0 || selected == curve.size() - 1)
        return;
    curve.removeAt(selected);
    selected = qMin(selected, int(curve.size() - 1));
    update();
    emit pointSelected();
    emit changed();
}
void CurveEditor::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const QRectF r = graphRect();
    p.fillRect(r, palette().base());
    p.setPen(palette().mid().color());
    for (int i = 0; i <= 4; ++i) {
        p.drawLine(QPointF(r.left() + i * r.width() / 4, r.top()),
                   QPointF(r.left() + i * r.width() / 4, r.bottom()));
        p.drawLine(QPointF(r.left(), r.top() + i * r.height() / 4),
                   QPointF(r.right(), r.top() + i * r.height() / 4));
    }
    p.setPen(QPen(palette().mid().color(), 1, Qt::DashLine));
    p.drawLine(r.bottomLeft(), r.topRight());
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(palette().highlight().color(), 2));
    for (int i = 1; i < curve.size(); ++i)
        p.drawLine(toWidget(curve[i - 1]), toWidget(curve[i]));
    for (int i = 0; i < curve.size(); ++i) {
        p.setBrush(i == selected ? palette().highlight() : palette().base());
        p.drawEllipse(toWidget(curve[i]), 5, 5);
    }
    p.setPen(palette().text().color());
    p.drawText(QPointF(r.left(), height() - 4), "0");
    p.drawText(QPointF(r.right() - 24, height() - 4), "255");
    p.drawText(QPointF(0, r.top() + 12), "255");
    if (hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(palette().highlight().color(), 1, Qt::DotLine));
        p.drawRect(rect().adjusted(1, 1, -2, -2));
    }
}
void CurveEditor::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton || !graphRect().contains(e->position()))
        return;
    setFocus();
    int near = -1;
    for (int i = 0; i < curve.size(); ++i)
        if (QLineF(e->position(), toWidget(curve[i])).length() <= 10) {
            near = i;
            break;
        }
    if (near >= 0)
        selected = near;
    else {
        QPoint point = toCurve(e->position());
        auto at = std::lower_bound(curve.begin(), curve.end(), point.x(),
                                   [](QPoint p, int x) { return p.x() < x; });
        if (at != curve.end() && at->x() == point.x())
            selected = int(at - curve.begin());
        else if (curve.size() < 64) {
            selected = int(at - curve.begin());
            curve.insert(selected, point);
            emit changed();
        }
    }
    update();
    emit pointSelected();
}
void CurveEditor::mouseMoveEvent(QMouseEvent *e) {
    if (e->buttons() & Qt::LeftButton)
        setSelectedPoint(toCurve(e->position()));
}
void CurveEditor::keyPressEvent(QKeyEvent *e) {
    int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    QPoint point = selectedPoint();
    switch (e->key()) {
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        selected = qBound(0, selected + (e->key() == Qt::Key_PageUp ? -1 : 1), int(curve.size() - 1));
        emit pointSelected();
        update();
        e->accept();
        return;
    case Qt::Key_Left:
        point.rx() -= step;
        break;
    case Qt::Key_Right:
        point.rx() += step;
        break;
    case Qt::Key_Up:
        point.ry() += step;
        break;
    case Qt::Key_Down:
        point.ry() -= step;
        break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        removeSelectedPoint();
        e->accept();
        return;
    default:
        QWidget::keyPressEvent(e);
        return;
    }
    setSelectedPoint(point);
    e->accept();
}

void ToneDialog::notify() {
    if (!updating && current.valid())
        emit settingsChanged(current);
}
ToneDialog::ToneDialog(const ToneSettings &settings, QWidget *parent)
    : QDialog(parent), current(settings) {
    setObjectName("toneDialog");
    setWindowTitle("Geri alınabilir ton ve renk ayarları");
    resize(480, 540);
    auto *layout = new QVBoxLayout(this);
    auto *explanation = new QLabel("Etkin katman üzerinde canlı önizleme. Kaynak pikseller korunur.");
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs);
    auto form = [&](QString name) {
        auto *page = new QWidget;
        auto *f = new QFormLayout(page);
        tabs->addTab(page, name);
        return f;
    };
    auto addInt = [&](QFormLayout *f, QString label, QString id, int min, int max,
                      int ToneSettings::*member) {
        auto *spin = new QSpinBox;
        spin->setObjectName(id);
        spin->setRange(min, max);
        spin->setValue(current.*member);
        f->addRow(label, spin);
        connect(spin, &QSpinBox::valueChanged, this, [this, member](int value) {
            current.*member = value;
            notify();
        });
        return spin;
    };
    auto addDouble = [&](QFormLayout *f, QString label, QString id, double min, double max,
                         double ToneSettings::*member) {
        auto *spin = new QDoubleSpinBox;
        spin->setObjectName(id);
        spin->setRange(min, max);
        spin->setDecimals(2);
        spin->setSingleStep(.1);
        spin->setValue(current.*member);
        f->addRow(label, spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, member](double value) {
            current.*member = value;
            notify();
        });
    };
    auto *levels = form("Seviyeler / pozlama");
    auto *black = addInt(levels, "Giriş siyahı", "inputBlack", 0, 254, &ToneSettings::inputBlack);
    auto *white = addInt(levels, "Giriş beyazı", "inputWhite", 1, 255, &ToneSettings::inputWhite);
    black->setMaximum(current.inputWhite - 1);
    white->setMinimum(current.inputBlack + 1);
    connect(black, &QSpinBox::valueChanged, white, [white](int v) { white->setMinimum(v + 1); });
    connect(white, &QSpinBox::valueChanged, black, [black](int v) { black->setMaximum(v - 1); });
    addDouble(levels, "Orta ton (gamma)", "gamma", .1, 10, &ToneSettings::gamma);
    auto *outBlack = addInt(levels, "Çıkış siyahı", "outputBlack", 0, 255, &ToneSettings::outputBlack);
    auto *outWhite = addInt(levels, "Çıkış beyazı", "outputWhite", 0, 255, &ToneSettings::outputWhite);
    outBlack->setMaximum(current.outputWhite);
    outWhite->setMinimum(current.outputBlack);
    connect(outBlack, &QSpinBox::valueChanged, outWhite, [outWhite](int v) { outWhite->setMinimum(v); });
    connect(outWhite, &QSpinBox::valueChanged, outBlack, [outBlack](int v) { outBlack->setMaximum(v); });
    addDouble(levels, "Pozlama (EV)", "exposure", -10, 10, &ToneSettings::exposure);
    auto *curves = form("Eğriler");
    auto *channel = new QComboBox;
    channel->setObjectName("curveChannel");
    channel->addItems({"RGB", "Kırmızı", "Yeşil", "Mavi"});
    curves->addRow("Kanal", channel);
    auto *editor = new CurveEditor;
    editor->setPoints(current.curves[0]);
    curves->addRow(editor);
    auto *input = new QSpinBox;
    input->setObjectName("curveInput");
    input->setRange(0, 0);
    auto *output = new QSpinBox;
    output->setObjectName("curveOutput");
    output->setRange(0, 255);
    auto syncPoint = [editor, input, output] {
        QSignalBlocker a(input), b(output);
        auto range = editor->selectedInputRange();
        input->setRange(range.first, range.second);
        input->setValue(editor->selectedPoint().x());
        output->setValue(editor->selectedPoint().y());
    };
    syncPoint();
    curves->addRow("Nokta girişi", input);
    curves->addRow("Nokta çıkışı", output);
    connect(editor, &CurveEditor::pointSelected, this, syncPoint);
    connect(input, &QSpinBox::valueChanged, editor,
            [editor, output](int v) { editor->setSelectedPoint({v, output->value()}); });
    connect(output, &QSpinBox::valueChanged, editor,
            [editor, input](int v) { editor->setSelectedPoint({input->value(), v}); });
    connect(editor, &CurveEditor::changed, this, [this, editor, channel] {
        current.curves[channel->currentIndex()] = editor->points();
        notify();
    });
    connect(channel, &QComboBox::currentIndexChanged, editor,
            [this, editor](int index) { editor->setPoints(current.curves[index]); });
    auto *remove = new QPushButton("Seçili noktayı sil");
    curves->addRow(remove);
    connect(remove, &QPushButton::clicked, editor, &CurveEditor::removeSelectedPoint);
    auto *resetCurve = new QPushButton("Bu kanalın eğrisini sıfırla");
    curves->addRow(resetCurve);
    connect(resetCurve, &QPushButton::clicked, this, [this, editor, channel] {
        current.curves[channel->currentIndex()] = {{0, 0}, {255, 255}};
        editor->setPoints(current.curves[channel->currentIndex()]);
        notify();
    });
    auto *color = form("Renk");
    addInt(color, "Ton kaydırma (°)", "hue", -180, 180, &ToneSettings::hue);
    addInt(color, "Canlılık", "vibrance", -100, 100, &ToneSettings::vibrance);
    addInt(color, "Camgöbeği / kırmızı", "redBalance", -100, 100, &ToneSettings::redBalance);
    addInt(color, "Macenta / yeşil", "greenBalance", -100, 100, &ToneSettings::greenBalance);
    addInt(color, "Sarı / mavi", "blueBalance", -100, 100, &ToneSettings::blueBalance);
    auto *effects = form("Renk efektleri");
    auto *invert = new QCheckBox("Renkleri ters çevir");
    invert->setObjectName("inverted");
    invert->setChecked(current.inverted);
    effects->addRow(invert);
    connect(invert, &QCheckBox::toggled, this, [this](bool v) {
        current.inverted = v;
        notify();
    });
    auto *threshold =
        addInt(effects, "Siyah beyaz eşiği", "threshold", -1, 255, &ToneSettings::threshold);
    threshold->setSpecialValueText("Kapalı");
    auto *poster = addInt(effects, "Posterize (kanal başına seviye)", "posterize", 2, 256,
                          &ToneSettings::posterize);
    poster->setToolTip("256: kapalı; 2: her kanalda yalnızca iki seviye.");
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
