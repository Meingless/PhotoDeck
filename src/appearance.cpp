#include "appearance.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

namespace {
double luminance(QColor color) {
    const auto linear = [](double c) {
        return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}
QColor readable(QColor text, QColor surface) {
    const double a = luminance(text), b = luminance(surface);
    if ((qMax(a, b) + .05) / (qMin(a, b) + .05) >= 4.5)
        return text;
    return b > .179 ? QColor(Qt::black) : QColor(Qt::white);
}
QColor blendColor(QColor a, QColor b, qreal weight) {
    return QColor::fromRgbF(a.redF() * (1 - weight) + b.redF() * weight,
                            a.greenF() * (1 - weight) + b.greenF() * weight,
                            a.blueF() * (1 - weight) + b.blueF() * weight);
}
} // namespace
const QVector<UiPreset> &uiPresets() {
    static const QVector<UiPreset> presets = {
        {"graphite", "Grafit", "Klasik araç rayı; sağda renk, katman ve geçmiş.", "#14181f", "#1e242d",
         "#2b3440", "#0e1218", "#e8edf3", "#a3afbf", "#343e4c", "#78d6ba", false, false, 12, 22, 5},
        {"paper", "Kağıt", "Sıcak açık yüzeyler ve isimli araçlar.", "#f5f3ef", "#fffdf8", "#e9e6df",
         "#e5e1d9", "#26352f", "#53675b", "#d3d2c9", "#315a47", true, true, 12, 22, 9},
        {"focus", "Odak", "Geniş tuval; yüzen araç ve katman panelleri.", "#0d1118", "#171e2b",
         "#263249", "#0c1119", "#edf2fb", "#a4b2ca", "#34425b", "#a6bdff", false, false, 12, 22, 10},
        {"film", "Fotoğraf Masası", "Fotoğraf şeridi ve ton ayarları ön planda.", "#211c1a", "#302825",
         "#443931", "#161413", "#efe5dc", "#c0ac9d", "#534438", "#e3b982", false, false, 12, 22, 3},
        {"workbench", "Çalışma Tezgahı", "Solda özellikler; sağda araçlar.", "#1c2633", "#2b394b",
         "#3c4e63", "#15202c", "#dfeafa", "#adc0d3", "#4a6179", "#8db9ee", false, false, 12, 22, 2},
        {"ribbon", "Şerit", "Üstte gruplandırılmış ve etiketli komutlar.", "#edf1f5", "#ffffff",
         "#e1e9f2", "#d7e0e8", "#22364b", "#536983", "#c8d5e3", "#4976c4", true, true, 12, 22, 4},
        {"ink", "Çizim Atölyesi", "Fırça uçları ve renk paleti; çizim için düzen.", "#251f2c", "#352b3e",
         "#493b56", "#1c1722", "#efe3f4", "#c6adcf", "#615071", "#d9b2e8", false, false, 12, 24, 7},
        {"compact", "Kompakt", "Yatay araç rayı ve sıkı yerleşim.", "#20272d", "#28343c", "#354852",
         "#151c22", "#dceaf0", "#a1b9c3", "#435761", "#81c6d8", false, false, 11, 18, 0},
        {"touch", "Dokunmatik", "Büyük etiketli düğmeler; altta araçlar.", "#eef3f3", "#ffffff",
         "#dfece9", "#d8e4e4", "#233b36", "#526e66", "#ccdcd5", "#206b57", true, true, 14, 30, 12},
        {"modular", "Modüler", "Bağımsız, taşınabilir panel blokları.", "#252d25", "#354033", "#485642",
         "#1c241c", "#e8efdc", "#bac8a4", "#56674b", "#bccd89", false, false, 12, 22, 8}};
    return presets;
}
bool validUiPreset(const QString &id) {
    for (const auto &p : uiPresets())
        if (p.id == id)
            return true;
    return false;
}
const UiPreset &uiPreset(const QString &id) {
    for (const auto &p : uiPresets())
        if (p.id == id)
            return p;
    return uiPresets()[2];
}
UiSettings UiSettings::forPreset(const QString &id) {
    const auto &p = uiPreset(id);
    UiSettings s;
    s.preset = p.id;
    s.fontSize = p.fontSize;
    s.iconSize = p.iconSize;
    s.radius = p.radius;
    s.toolLabels = p.labels;
    s.rulers = id != "focus";
    s.panelWidth = id == "compact" ? 245 : id == "focus" ? 300 : 280;
    return s;
}
QJsonObject UiSettings::toJson() const {
    auto point = [](QPointF p) { return QJsonArray{p.x(), p.y()}; };
    return {{"version", 1},
            {"fontFamily", fontFamily},
            {"panel", panel.isValid() ? panel.name() : QString()},
            {"background", background.isValid() ? background.name() : QString()},
            {"preset", preset},
            {"theme", theme},
            {"panelSide", panelSide},
            {"accent", accent.isValid() ? accent.name() : QString()},
            {"canvas", canvas.isValid() ? canvas.name() : QString()},
            {"fontSize", fontSize},
            {"iconSize", iconSize},
            {"radius", radius},
            {"panelWidth", panelWidth},
            {"toolLabels", toolLabels},
            {"movablePanels", movablePanels},
            {"showMenu", showMenu},
            {"showStatus", showStatus},
            {"rulers", rulers},
            {"focusToolsVisible", focusToolsVisible},
            {"focusLayersVisible", focusLayersVisible},
            {"dockState", QString::fromLatin1(dockState.toBase64())},
            {"toolsPosition", point(toolsPosition)},
            {"layersPosition", point(layersPosition)}};
}
bool UiSettings::fromJson(const QJsonObject &j, UiSettings *out, QString *error) {
    auto fail = [&]() {
        if (error)
            *error = "Preset biçimi, sürümü veya ayar değerleri geçersiz.";
        return false;
    };
    if (j.value("version").toInt(-1) != 1 || !j.value("preset").isString() ||
        !validUiPreset(j.value("preset").toString()))
        return fail();
    UiSettings s = forPreset(j["preset"].toString());
    auto number = [&](const char *key, int &value, int min, int max) {
        if (!j.contains(key))
            return true;
        auto v = j[key];
        if (!v.isDouble())
            return false;
        double n = v.toDouble();
        if (!std::isfinite(n) || n != std::floor(n) || n < min || n > max)
            return false;
        value = int(n);
        return true;
    };
    auto flag = [&](const char *key, bool &value) {
        if (!j.contains(key))
            return true;
        if (!j[key].isBool())
            return false;
        value = j[key].toBool();
        return true;
    };
    auto color = [&](const char *key, QColor &value) {
        if (!j.contains(key))
            return true;
        if (!j[key].isString())
            return false;
        QString v = j[key].toString();
        if (v.isEmpty())
            return true;
        if (v.size() != 7 || !v.startsWith('#') || !QColor(v).isValid())
            return false;
        value = QColor(v);
        return true;
    };
    auto point = [&](const char *key, QPointF &value) {
        if (!j.contains(key))
            return true;
        QJsonArray a = j[key].toArray();
        if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble())
            return false;
        double x = a[0].toDouble(), y = a[1].toDouble();
        if (!std::isfinite(x) || !std::isfinite(y) || x < -1 || x > 1 || y < -1 || y > 1)
            return false;
        value = {x, y};
        return true;
    };
    if (!number("fontSize", s.fontSize, 10, 20) || !number("iconSize", s.iconSize, 16, 36) ||
        !number("radius", s.radius, 0, 16) || !number("panelWidth", s.panelWidth, 220, 440) ||
        !flag("toolLabels", s.toolLabels) || !flag("movablePanels", s.movablePanels) ||
        !flag("showMenu", s.showMenu) || !flag("showStatus", s.showStatus) ||
        !flag("rulers", s.rulers) || !flag("focusToolsVisible", s.focusToolsVisible) ||
        !flag("focusLayersVisible", s.focusLayersVisible) || !color("accent", s.accent) ||
        !color("canvas", s.canvas) || !color("panel", s.panel) || !color("background", s.background) ||
        !point("toolsPosition", s.toolsPosition) || !point("layersPosition", s.layersPosition))
        return fail();
    for (auto key : {"theme", "panelSide"})
        if (j.contains(key) && !j[key].isString())
            return fail();
    s.theme = j.value("theme").toString("preset");
    s.panelSide = j.value("panelSide").toString("preset");
    if (!QStringList{"preset", "light", "dark"}.contains(s.theme) ||
        !QStringList{"preset", "left", "right"}.contains(s.panelSide))
        return fail();
    if (j.contains("dockState")) {
        if (!j["dockState"].isString())
            return fail();
        QString encoded = j["dockState"].toString();
        if (encoded.size() > 90000)
            return fail();
        auto decoded =
            QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.size() > 65536)
            return fail();
        s.dockState = decoded.decoded;
    }
    if (j.contains("fontFamily")) {
        if (!j["fontFamily"].isString() || j["fontFamily"].toString().isEmpty() ||
            j["fontFamily"].toString().size() > 128)
            return fail();
        s.fontFamily = j["fontFamily"].toString();
    }
    *out = s;
    return true;
}
UiPreset UiSettings::resolved() const {
    UiPreset p = uiPreset(preset);
    if (theme == "light" && !p.light) {
        const auto &b = uiPreset("paper");
        p.background = b.background;
        p.panel = b.panel;
        p.raised = b.raised;
        p.canvas = b.canvas;
        p.text = b.text;
        p.muted = b.muted;
        p.line = b.line;
        p.light = true;
    }
    if (theme == "dark" && p.light) {
        const auto &b = uiPreset("graphite");
        p.background = b.background;
        p.panel = b.panel;
        p.raised = b.raised;
        p.canvas = b.canvas;
        p.text = b.text;
        p.muted = b.muted;
        p.line = b.line;
        p.light = false;
    }
    if (accent.isValid())
        p.accent = accent;
    if (canvas.isValid())
        p.canvas = canvas;
    if (panel.isValid())
        p.panel = panel;
    if (background.isValid())
        p.background = background;
    p.text = readable(p.text, p.panel);
    if (panel.isValid()) {
        p.raised = blendColor(p.panel, p.text, .08);
        p.line = blendColor(p.panel, p.text, .2);
        p.muted = blendColor(p.panel, p.text, .65);
    }
    p.muted = readable(p.muted, p.panel);
    p.fontSize = fontSize;
    p.iconSize = iconSize;
    p.radius = radius;
    p.labels = toolLabels;
    return p;
}
QPalette UiSettings::palette() const {
    const auto p = resolved();
    QPalette pal;
    pal.setColor(QPalette::Window, p.panel);
    pal.setColor(QPalette::WindowText, p.text);
    pal.setColor(QPalette::Base, p.canvas);
    pal.setColor(QPalette::AlternateBase, p.raised);
    pal.setColor(QPalette::Text, readable(p.text, p.canvas));
    pal.setColor(QPalette::Button, p.panel);
    pal.setColor(QPalette::ButtonText, p.text);
    pal.setColor(QPalette::Highlight, p.accent);
    pal.setColor(QPalette::HighlightedText, readable(QColor(Qt::white), p.accent));
    pal.setColor(QPalette::Mid, p.line);
    pal.setColor(QPalette::PlaceholderText, p.muted);
    pal.setColor(QPalette::ToolTipBase, p.raised);
    pal.setColor(QPalette::ToolTipText, p.text);
    pal.setColor(QPalette::Disabled, QPalette::Text, p.muted);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, p.muted);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, p.muted);
    return pal;
}
QString UiSettings::styleSheet() const {
    const auto p = resolved();
    const bool darkAccentText = luminance(p.accent) > .179;
    const QString onAccent = darkAccentText ? "#000000" : "#ffffff";
    const auto mix = [](QColor a, QColor b, qreal weight) {
        return QColor::fromRgbF(a.redF() * (1 - weight) + b.redF() * weight,
                                a.greenF() * (1 - weight) + b.greenF() * weight,
                                a.blueF() * (1 - weight) + b.blueF() * weight);
    };
    const QColor hover = mix(p.panel, p.text, .07);
    const QColor selected = mix(p.panel, p.accent, p.light ? .15 : .19);
    QString css = R"(
      QWidget {color:@text; font-size:@fontpx;}
      QWidget[pdSurface="true"] {background:@panel;}
      QMainWindow,QDialog,QMessageBox {background:@bg;}
      QDialog QLabel,QDialog QCheckBox,QDialog QGroupBox,QMessageBox QLabel {color:@bgText;}
      QDialog QLabel#mutedLabel,QDialog QLabel#presetDetails {color:@bgMuted;}
      QMenuBar {background:@panel; padding:4px 10px; border-bottom:1px solid @line;}
      QMenuBar::item {padding:6px 10px; border-radius:5px; background:transparent;}
      QMenuBar::item:selected {background:@hover;}
      QMenu {background:@panel; border:1px solid @line; border-radius:8px; padding:7px;}
      QMenu::item {padding:8px 30px 8px 14px; border-radius:5px;}
      QMenu::item:selected {background:@selected; color:@selectedText;}
      QMenu::item:disabled {color:@muted;}
      QMenu::separator {height:1px; background:@line; margin:6px 8px;}
      QToolBar {background:@panel; border:0; padding:@padpx; spacing:5px;}
      QToolBar#documentToolbar {border-bottom:1px solid @line;}
      QToolBar::separator {background:@line; width:1px; height:1px; margin:4px;}
      QToolButton {border:1px solid transparent; border-radius:@radiuspx; padding:@padpx;}
      QToolButton:hover {background:@hover; border-color:@line;}
      QToolButton:checked {background:@selected; color:@selectedText; border-color:@accent;}
      QToolButton:focus {border-color:@panelFocus;}
      QToolButton:disabled {color:@muted;}
      QTabWidget::pane {border:0;}
      QTabBar {background:@bg;}
      QTabBar::tab {background:@panel; color:@muted; padding:10px 18px; margin:4px 2px 0; border:1px solid transparent; border-top-left-radius:@radiuspx; border-top-right-radius:@radiuspx;}
      QTabBar::tab:selected {background:@bg; color:@text; border-color:@line; border-bottom:2px solid @accent;}
      QTabBar::tab:hover:!selected {background:@hover; color:@text;}
      QTabBar::close-button {image:url(:/close.png); width:12px; height:12px;}
      QDockWidget {background:@panel; color:@muted;}
      QDockWidget::title {background:@panel; padding:10px; font-weight:600; border-bottom:1px solid @line;}
      QFrame#floatingTools,QFrame#floatingLayers {background:@panel; border:1px solid @line; border-radius:@radiuspx;}
      QToolButton#toolsHandle,QToolButton#layersHandle {background:transparent; color:@muted; padding:7px 9px; text-align:left; border-bottom:1px solid @line; border-radius:0;}
      QLabel#sectionTitle {font-size:@smallpx; font-weight:600; color:@muted; padding:5px 0;}
      QLabel#dialogTitle {font-size:@titlepx; font-weight:600; padding:3px 0;}
      QLabel#presetDetails,QLabel#layerDetails,QLabel#mutedLabel,QLabel#localLabel {color:@muted;}
      QLabel#brandLabel {font-size:19px; font-weight:600; padding:0 3px;}
      QListWidget {background:@panel; border:1px solid @line; border-radius:@radiuspx; padding:5px; outline:0;}
      QListWidget::item {padding:6px; border:1px solid transparent; border-radius:@radiuspx;}
      QListWidget::item:selected {background:@selected; color:@selectedText; border-color:@line;}
      QListWidget::item:hover {background:@hover;}
      QListWidget::item:focus {border-color:@accent;}
      QListWidget#history {color:@muted;}
      QTreeView,QTableView {background:@panel; color:@text; border:1px solid @line; selection-background-color:@selected; selection-color:@selectedText; outline:0;}
      QHeaderView::section {background:@panel; color:@muted; border:0; border-bottom:1px solid @line; padding:8px;}
      QComboBox,QSpinBox,QDoubleSpinBox,QLineEdit,QPlainTextEdit {color:@bgText; background:@bg; border:1px solid @line; border-radius:@radiuspx; padding:7px; selection-background-color:@accent; selection-color:@onAccent;}
      QComboBox:hover,QSpinBox:hover,QDoubleSpinBox:hover,QLineEdit:hover {border-color:@muted;}
      QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus,QLineEdit:focus,QPlainTextEdit:focus {border-color:@fieldFocus;}
      QComboBox:disabled,QSpinBox:disabled,QDoubleSpinBox:disabled,QLineEdit:disabled {color:@muted; background:@panel;}
      QComboBox::drop-down {border:0; width:23px;}
      QComboBox QAbstractItemView {background:@panel; color:@text; border:1px solid @line; padding:4px; selection-background-color:@selected; selection-color:@selectedText;}
      QCheckBox {spacing:9px; padding:3px 0;}
      QCheckBox::indicator {width:16px; height:16px; border:1px solid @line; border-radius:4px; background:@bg;}
      QCheckBox::indicator:hover {border-color:@accent;}
      QCheckBox::indicator:checked {background:@accent; border-color:@accent; image:url(@check);}
      QCheckBox::indicator:disabled {border-color:@line; background:@raised;}
      QCheckBox:focus {color:@bgText;} QCheckBox::indicator:focus {border-color:@fieldFocus;}
      QGroupBox {border:1px solid @line; border-radius:@radiuspx; margin-top:14px; padding:14px 10px 10px;}
      QGroupBox::title {subcontrol-origin:margin; left:10px; padding:0 5px; color:@muted; font-weight:600;}
      QSlider::groove:horizontal {height:5px; background:@bg; border-radius:2px;}
      QSlider::sub-page:horizontal {background:@accent; border-radius:2px;}
      QSlider::handle:horizontal {background:@text; border:2px solid @accent; width:11px; margin:-5px 0; border-radius:7px;}
      QSlider::handle:horizontal:focus {border-color:@text;}
      QPushButton {color:@raisedText; background:@raised; border:1px solid @line; border-radius:@radiuspx; padding:8px 12px; font-weight:500;}
      QPushButton:hover {background:@hover; border-color:@muted;}
      QPushButton:pressed {background:@selected;}
      QPushButton:default,QPushButton:focus {border-color:@panelFocus;}
      QPushButton[primary="true"] {background:@accent; color:@onAccent; border-color:@accent;}
      QPushButton[primary="true"]:hover {background:@accentHover;}
      QPushButton:disabled {background:@panel; color:@muted; border-color:@line;}
      QPushButton:flat {border:0; background:transparent; color:@muted;}
      QPushButton:flat:hover {background:@hover; color:@text;}
      QScrollArea {background:@panel; border:0;}
      QStatusBar {background:@panel; border-top:1px solid @line; padding:4px 8px; color:@muted;}
      QStatusBar::item {border:0;}
      QScrollBar:vertical {background:transparent; width:10px; margin:2px;}
      QScrollBar::handle:vertical {background:@line; min-height:28px; border-radius:4px;}
      QScrollBar::handle:vertical:hover {background:@muted;}
      QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}
      QScrollBar:horizontal {background:transparent; height:10px; margin:2px;}
      QScrollBar::handle:horizontal {background:@line; min-width:28px; border-radius:4px;}
      QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal {width:0;}
      QToolTip {background:@raised; color:@raisedText; border:1px solid @line; padding:6px;}
    )";
    const QVector<QPair<QString, QString>> values = {
        {"@onAccent", onAccent},
        {"@fieldFocus", readable(p.accent, p.background).name()},
        {"@panelFocus", readable(p.accent, p.panel).name()},
        {"@selectedText", readable(p.text, selected).name()},
        {"@bgText", readable(p.text, p.background).name()},
        {"@bgMuted", readable(p.muted, p.background).name()},
        {"@raisedText", readable(p.text, p.raised).name()},
        {"@accentHover",
         mix(p.accent, darkAccentText ? QColor(Qt::white) : QColor(Qt::black), .12).name()},
        {"@hover", hover.name()},
        {"@selected", selected.name()},
        {"@check", darkAccentText ? ":/check-dark.png" : ":/check-light.png"},
        {"@small", QString::number(qMax(10, fontSize - 1))},
        {"@title", QString::number(fontSize + 6)},
        {"@text", p.text.name()},
        {"@bg", p.background.name()},
        {"@panel", p.panel.name()},
        {"@raised", p.raised.name()},
        {"@canvas", p.canvas.name()},
        {"@muted", p.muted.name()},
        {"@line", p.line.name()},
        {"@accent", p.accent.name()},
        {"@font", QString::number(fontSize)},
        {"@radius", QString::number(radius)},
        {"@pad", QString::number(preset == "compact" ? 3 : 5)}};
    for (const auto &v : values)
        css.replace(v.first, v.second);
    return css;
}
