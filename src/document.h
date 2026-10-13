#pragma once
#include "selection.h"
#include "tones.h"
#include <QColor>
#include <QFont>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QVector>
#include <functional>

struct Layer {
    QString name = "Katman";
    QImage pixels;
    QImage mask; // ARGB, grayscale: white reveals, black conceals.
    QPoint offset;
    bool visible = true, locked = false;
    qreal opacity = 1;
    int blend = 0;
    int brightness = 0, contrast = 0, saturation = 0, blur = 0;
    ToneSettings tones;
    QString text;
    QFont font;
    QColor color = Qt::white;
    mutable QImage cache;
    mutable qint64 cacheKey = 0, maskKey = 0;
    mutable int cacheB = 999, cacheC = 999, cacheS = 999, cacheBlur = 999;
    mutable ToneSettings cacheTones;
    QImage rendered() const;
};
struct State {
    QSize size;
    QVector<Layer> layers; // bottom to top
    int active = 0;
};
class Document : public QObject {
    Q_OBJECT
  public:
    static constexpr int MaxCoordinate = 1 << 24;
    static bool validOffset(QPoint offset);
    static constexpr qint64 MaxPixels = 16 * 1024 * 1024;
    static constexpr qint64 MaxLayerBytes = 256 * 1024 * 1024;
    static constexpr qint64 HistoryBudget = 128 * 1024 * 1024;
    explicit Document(QObject *parent = nullptr);
    State state;
    QString title = "Adsız", path;
    bool dirty = false;
    Selection selection;
    bool editingMask = false;
    QImage composite() const;
    static bool validSize(QSize size);
    static QImage readImage(const QString &path, QString *error);
    bool create(QSize size, bool white = false);
    bool importImage(const QString &path, bool asLayer, QString *error);
    bool addLayer(const QString &name, QString *error = nullptr);
    bool duplicate(QString *error);
    void deleteLayer();
    void reorder(int delta);
    bool addMask(QString *error);
    void removeMask();
    void invertMask();
    bool crop(QRect rect, QString *error);
    bool resize(QSize size, QString *error);
    bool rotate(bool clockwise, QString *error);
    bool flip(bool horizontal, QString *error = nullptr);
    void adjust(int b, int c, int s);
    bool adjustTones(const ToneSettings &settings, QString *error = nullptr);
    bool addText(QString text, QFont font, QColor color, QPoint at, QString *error);
    bool editText(QString text, QFont font, QColor color, QString *error = nullptr);
    void beginEdit();
    void commit(const QString &label);
    void cancelEdit();
    void changed();
    void undo();
    void redo();
    bool canUndo() const { return cursor > 0; }
    bool canRedo() const { return cursor + 1 < history.size(); }
    QStringList historyLabels() const;
    qint64 imageBytes() const;
    qint64 historyBytes() const;
    bool save(const QString &file, QString *error, bool markClean = true);
    bool load(const QString &file, QString *error);
    bool exportImage(const QString &file, int quality, QString *error);
    void markSaved();
    bool isEditing() const { return inEdit; }
    quint64 revision() const { return cursor >= 0 ? history[cursor].id : 0; }
    void markRecovered() {
        savedId = 0;
        dirty = true;
    }
    void makeDemo();
    static QPainter::CompositionMode compositionMode(int blend);
  signals:
    void updated();

  private:
    struct Entry {
        State state;
        QString label;
        quint64 id;
    };
    QVector<Entry> history;
    int cursor = -1;
    quint64 nextId = 1, savedId = 0;
    State before;
    bool inEdit = false;
    void resetHistory(const QString &label);
    void trimHistory();
    bool allows(qint64 extra, QString *error, int addedLayers = 1) const;
};
