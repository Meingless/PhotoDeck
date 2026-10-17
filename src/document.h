#pragma once
#include "filters.h"
#include "output.h"
#include "selection.h"
#include "tones.h"
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QTransform>
#include <QVector>
#include <functional>
#include <atomic>
class QIODevice;

struct Layer {
    QString name = "Katman";
    QImage pixels;
    QImage mask; // ARGB, grayscale: white reveals, black conceals.
    QPoint offset;
    QTransform
        geometry; // Local source pixels -> local transformed coordinates; offset is document position.
    bool visible = true, locked = false;
    bool clipped = false; // A run of clipped layers belongs to the preceding non-clipped base.
    qreal opacity = 1;
    int blend = 0;
    int brightness = 0, contrast = 0, saturation = 0, blur = 0;
    ToneSettings tones;
    FilterSettings filters;
    MaskSettings maskSettings;
    QString text;
    QFont font;
    QColor color = Qt::white;
    mutable QImage cache;
    mutable qint64 cacheKey = 0, maskKey = 0;
    mutable int cacheB = 999, cacheC = 999, cacheS = 999, cacheBlur = 999;
    mutable ToneSettings cacheTones;
    mutable QTransform cacheGeometry;
    mutable FilterSettings cacheFilters;
    mutable MaskSettings cacheMaskSettings;
    QImage rendered() const;
    QRect transformedBounds() const;
    QPoint renderedOffset() const;
    bool validGeometry() const;
    Layer rasterized() const;
};
struct SavedSelection {
    QString name;
    Selection selection;
};
struct State {
    QSize size;
    QVector<Layer> layers; // bottom to top
    int active = 0;
    Selection selection = {}, previousSelection = {}; // Transient, undoable editing context.
    QVector<SavedSelection> savedSelections = {}; // Persistent named coverage channels.
};
class Document : public QObject {
    Q_OBJECT
  public:
    static constexpr int MaxCoordinate = 1 << 24;
    static bool validOffset(QPoint offset);
    static constexpr qint64 MaxPixels = 16 * 1024 * 1024;
    static constexpr qint64 MaxLayerBytes = 256 * 1024 * 1024;
    static constexpr qint64 HistoryBudget = 128 * 1024 * 1024;
    static constexpr qint64 MaxSelectionBytes = 64 * 1024 * 1024;
    static constexpr int MaxSavedSelections = 16;
    static constexpr int MaxSelectionNameLength = 128;
    static constexpr int NativeProjectVersion = 6; // Highest codec version; normal saves prefer v5.
    enum class SaveStatus { Success, Error, ExternalModification, FormatUpgradeRequired };
    struct SaveOptions {
        bool allowExternalOverwrite = false;
        bool allowFormatUpgrade = false;
    };
    explicit Document(QObject *parent = nullptr);
    State state;
    QString title = "Adsız", path;
    bool dirty = false;
    Selection &selection; // Compatibility alias for state.selection; prefer setSelection().
    bool editingMask = false;
    QImage composite() const;
    static bool validSize(QSize size);
    static QImage readImage(const QString &path, QString *error);
    bool create(QSize size, bool white = false);
    bool importImage(const QString &path, bool asLayer, QString *error);
    bool addLayer(const QString &name, QString *error = nullptr);
    // Independent transparent painting layer above the active clipping group.
    bool addPaintLayer(const QString &name, QString *error = nullptr);
    bool insertLayer(Layer layer, const QString &label, QString *error = nullptr);
    bool insertLayerAboveClippingGroup(Layer layer, const QString &label, QString *error = nullptr);
    bool duplicate(QString *error);
    void deleteLayer();
    void reorder(int delta);
    // Move one layer to a final bottom-to-top index; retain active identity. Like
    // up/down, locks allow order changes and a new bottom layer loses clipping.
    // Invalid/no-op requests and pending edits are inert, including redo history.
    bool reorderLayer(int from, int to);
    bool addMask(QString *error);
    void removeMask();
    void invertMask();
    bool crop(QRect rect, QString *error);
    bool resize(QSize size, QString *error);
    bool rotate(bool clockwise, QString *error);
    bool flip(bool horizontal, QString *error = nullptr);
    enum class LayerAlignment { Left, Center, Right, Top, Middle, Bottom };
    enum class AlignmentTarget { Canvas, Selection };
    // Outward-rounded transformed source rectangle in document coordinates, including
    // transparent/masked edges. Empty means absent/invalid geometry, not zero alpha.
    QRect activeLayerBounds() const;
    // X/Y is that rectangle's top-left. Only offset changes; these own one undo step.
    bool positionLayer(QPoint topLeft, QString *error = nullptr);
    // Immediate offset-only edit. A nonzero revision may extend only the latest
    // unchanged, unsaved nudge; rejected/no-op requests never alter history.
    bool nudgeLayer(QPoint delta, quint64 mergeRevision = 0, QString *error = nullptr);
    bool alignLayer(LayerAlignment alignment, AlignmentTarget target, QString *error = nullptr);
    bool setLayerTransform(const QTransform &transform, QString *error = nullptr);
    bool rasterizeLayer(QString *error = nullptr);
    bool canvasSize(QSize size, QPoint anchor, QString *error = nullptr);
    bool mergeVisible(QString *error = nullptr);
    bool flatten(QString *error = nullptr);
    bool setClipping(bool clipped, QString *error = nullptr);
    bool layerViaSelection(bool cut, QString *error = nullptr);
    // Detached copy preview: no document, editing context or history mutations.
    bool selectionCopyState(State &pending, QString *error = nullptr) const;
    bool mergeDown(QString *error = nullptr);
    void adjust(int b, int c, int s);
    bool adjustTones(const ToneSettings &settings, QString *error = nullptr);
    bool adjustFilters(const FilterSettings &settings, QString *error = nullptr);
    bool adjustMask(const MaskSettings &settings, QString *error = nullptr);
    bool addText(QString text, QFont font, QColor color, QPoint at, QString *error);
    bool editText(QString text, QFont font, QColor color, QString *error = nullptr);
    // Selection changes join undo history without dirtying pixels or named channels.
    bool setSelection(const Selection &selection, const QString &label = "Seçim değişti",
                      QString *error = nullptr);
    bool deselect();
    bool canReselect() const { return selection.isEmpty() && !state.previousSelection.isEmpty(); }
    bool reselect(QString *error = nullptr);
    QStringList savedSelectionNames() const;
    bool saveSelection(const QString &name, QString *error = nullptr);
    bool loadSelection(const QString &name, Selection::Mode mode = Selection::Replace,
                       QString *error = nullptr);
    bool removeSavedSelection(const QString &name, QString *error = nullptr);
    void beginEdit();
    void commit(const QString &label);
    void cancelEdit();
    void changed();
    void undo();
    void redo();
    bool canUndo() const { return cursor > 0; }
    bool canRedo() const { return cursor + 1 < history.size(); }
    // Copy-only navigation metadata for every retained entry, including redo states.
    // Revision IDs remain stable when old entries are trimmed; indexes do not.
    struct HistoryDescription {
        quint64 revision = 0;
        QString label;
    };
    QVector<HistoryDescription> historyDescriptions() const;
    int currentHistoryIndex() const { return cursor; }
    // Reject unknown/current revisions without changing state or an active edit.
    // A successful jump cancels any pending model edit and emits updated() once.
    bool jumpToHistoryRevision(quint64 revision);
    QStringList historyLabels() const;
    qint64 imageBytes() const;
    qint64 historyBytes() const;
    bool save(const QString &file, QString *error, bool markClean = true);
    bool save(const QString &file, QString *error, bool markClean, const SaveOptions &options);
    // Recovery consumes an immutable value, never the live GUI-owned Document. Metadata
    // containers are detached and render caches dropped; pixel/coverage buffers stay shared.
    static State recoverySnapshot(const State &state);
    static bool writeRecoverySnapshot(const State &snapshot, QIODevice &device, QString *error,
                                      const std::atomic_bool *cancel = nullptr);
    SaveStatus lastSaveStatus() const { return saveStatus; }
    // Zero denotes a new/imported document; a successful native save records projectSaveVersion().
    int sourceVersion() const { return sourceProjectVersion; }
    int projectSaveVersion() const { return state.savedSelections.isEmpty() ? 5 : 6; }
    bool requiresFormatUpgrade(const QString &file) const;
    bool load(const QString &file, QString *error);
    bool exportImage(const QString &file, int quality, QString *error);
    bool exportRaster(const QString &file, const RasterExportOptions &options, QString *error);
    bool exportPdf(const QString &file, const PdfExportOptions &options, QString *error);
    void markSaved();
    bool isEditing() const { return inEdit; }
    quint64 revision() const { return cursor >= 0 ? history[cursor].id : 0; }
    void markRecovered() {
        // A recovery snapshot is an unsaved working copy, never the backing project.
        path.clear();
        sourceFile = {};
        sourceProjectVersion = 0;
        savedId = 0;
        dirty = true;
    }
    void makeDemo();
    static QPainter::CompositionMode compositionMode(int blend);
  signals:
    void updated();

  private:
    struct FileVersion {
        QString absolutePath, resolvedPath;
        bool exists = false;
        qint64 size = 0;
        QDateTime modified;
        QByteArray identity, digest;
    };
    static bool readFileVersion(const QString &file, FileVersion &version, QString *error);
    static bool sameFileVersion(const FileVersion &a, const FileVersion &b);
    bool isSourceFile(const FileVersion &version) const;
    FileVersion sourceFile;
    int sourceProjectVersion = 0;
    SaveStatus saveStatus = SaveStatus::Success;
    struct Entry {
        State state;
        QString label;
        quint64 id, contentId;
    };
    QVector<Entry> history;
    int cursor = -1;
    quint64 nextId = 1, savedId = 0, lastNudgeRevision = 0;
    State before;
    bool inEdit = false;
    void resetHistory(const QString &label);
    void trimHistory();
    bool allows(qint64 extra, QString *error, int addedLayers = 1) const;
    bool prepareSelectionLayer(bool cut, State &pending, QString *error) const;
};
