#pragma once
#include <QColor>
#include <QJsonObject>
#include <QPalette>
#include <QPointF>
#include <QString>
#include <QVector>

struct UiPreset {
    QString id, name, description;
    QColor background, panel, raised, canvas, text, muted, line, accent;
    bool light = false, labels = false;
    int fontSize = 12, iconSize = 22, radius = 5;
};
struct UiSettings {
    QString preset = "focus", theme = "preset", panelSide = "preset", fontFamily = "Sans Serif";
    QColor accent, canvas, panel, background;
    int fontSize = 12, iconSize = 22, radius = 10, panelWidth = 280;
    bool toolLabels = false, movablePanels = true, showMenu = true, showStatus = true, rulers = false;
    bool focusToolsVisible = true, focusLayersVisible = true;
    QByteArray dockState;
    QPointF toolsPosition = {-1, -1}, layersPosition = {-1, -1};
    static UiSettings forPreset(const QString &id);
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &json, UiSettings *out, QString *error);
    UiPreset resolved() const;
    QPalette palette() const;
    QString styleSheet() const;
};
const QVector<UiPreset> &uiPresets();
const UiPreset &uiPreset(const QString &id);
bool validUiPreset(const QString &id);
