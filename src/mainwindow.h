#pragma once
#include "appearance.h"
#include "canvas.h"
#include <QComboBox>
#include <QDockWidget>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QSettings>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
class RecoveryManager;
class QCheckBox;
class QGridLayout;
class QScrollArea;
class Histogram : public QWidget {
  public:
    explicit Histogram(QWidget *parent = nullptr) : QWidget(parent) { setFixedHeight(70); }
    void setImage(const QImage &image);
    void setUiPalette(const QPalette &palette) {
        colors = palette;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    QPalette colors;
    QVector<int> bins = QVector<int>(128);
    int peak = 1;
};
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(bool recovery = true, bool useSettings = true);
    ~MainWindow() override;
    UiSettings uiSettings() const { return appearance; }
    void applyUiPreset(const QString &id, bool persist = true);
    void applyUiSettings(const UiSettings &settings, bool resetLayout = true, bool persist = true);
    bool exportUiPreset(const QString &path, QString *error);
    bool importUiPreset(const QString &path, QString *error);
    Document *currentDocument() const;
    Canvas *currentCanvas() const;
    void addDocument(Document *document);
    bool openFile(const QString &file);
    void demo();
    void saveScreenshot(const QString &path);

  protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

  private:
    QTabWidget *tabs = nullptr;
    QWidget *workspace = nullptr;
    QFrame *floatingTools = nullptr, *floatingLayers = nullptr;
    QToolButton *toolsHandle = nullptr, *layersHandle = nullptr;
    QToolBar *documentToolbar = nullptr, *optionsToolbar = nullptr, *toolsBar = nullptr,
             *ribbonToolbar = nullptr;
    QDockWidget *toolsDock = nullptr, *colorDock = nullptr, *layersDock = nullptr,
                *historyDock = nullptr, *filmDock = nullptr, *brushDock = nullptr;
    QWidget *layerPanel = nullptr;
    QWidget *toolPalette = nullptr;
    QGridLayout *toolGrid = nullptr;
    QScrollArea *toolScroll = nullptr;
    QVector<QToolButton *> toolButtons;
    QLabel *brandLabel = nullptr;
    QListWidget *filmstrip = nullptr;
    QComboBox *presetPicker = nullptr;
    QMenu *presetMenu = nullptr, *userPresetMenu = nullptr;
    QVector<QPair<QString, UiSettings>> userPresets;
    UiSettings appearance;
    bool persistSettings = true, applyingAppearance = false;
    QTimer uiSaveTimer;
    QPoint panelDragStart, panelStart;
    UiSettings captureUiSettings() const;
    void applyAppearanceColors();
    void arrangePreset();
    void positionOverlays();
    void customizeUi();
    void customizeBrush();
    void showBrushLibrary();
    void applyBrushPreset(const BrushPreset &preset);
    void loadBrushPresets();
    bool storeBrushPresets(const QVector<BrushPreset> &presets);
    QVector<BrushPreset> brushPresets;
    QString brushLibraryLoadError;
    void saveUiSettings();
    void loadUiSettings();
    void rebuildUserPresets();
    void saveUserPreset();
    void buildAppearanceExtras();
    QDockWidget *createDock(const QString &, const QString &, QWidget *);
    void queueUiSave();
    void syncPanelActions();
    QListWidget *layers = nullptr, *history = nullptr;
    QPointer<Document> historyDocument;
    void refreshHistory();
    bool navigateHistory(Document *document, quint64 revision);
    QComboBox *blend = nullptr;
    QSlider *opacity = nullptr, *brightness = nullptr, *contrast = nullptr, *saturation = nullptr;
    QSpinBox *sizeInput = nullptr, *hardnessInput = nullptr, *brushOpacityInput = nullptr;
    QComboBox *selectionModeInput = nullptr;
    QSpinBox *selectionToleranceInput = nullptr;
    QCheckBox *contiguousSelectionInput = nullptr;
    QToolButton *colorButton = nullptr, *maskButton = nullptr;
    QLabel *info = nullptr, *status = nullptr, *zoomLabel = nullptr, *layerDetails = nullptr;
    Histogram *histogram = nullptr;
    QTimer refreshTimer, autosaveTimer, recoverySaveTimer;
    struct PreviewCache {
        QByteArray compositeKey;
        QImage histogramSample;
        QIcon filmThumbnail;
        QVector<QByteArray> layerKeys;
        QVector<QIcon> layerThumbnails;
    };
    QHash<Document *, PreviewCache> previewCaches;
    QSet<Document *> deferredRecovery;
    void scheduleRecovery(Document *document);
    bool refreshing = false;
    RecoveryManager *recoveryManager = nullptr;
    QHash<QString, QAction *> actions;
    void buildMenus();
    void buildTools();
    void buildInspector();
    void updateToolOptions();
    QAction *action(const QString &id, const QString &text, const QKeySequence &shortcut,
                    std::function<void()> fn);
    void refresh();
    void updateActionAvailability();
    void newDocument();
    void chooseOpen();
    void importLayer();
    bool saveDocument(Document *d, bool saveAs = false);
    void exportDocument();
    void exportAdvancedRaster();
    void exportPdfDocument();
    void printDocument();
    bool confirmClose(int index);
    void closeTab(int index);
    void resizeDocument();
    void changeCanvasSize();
    void transformLayer();
    void transformLayerNumerically();
    void positionLayer();
    void alignLayer(Document::LayerAlignment alignment, Document::AlignmentTarget target);
    void textDialog(QPoint point, bool edit = false);
    void renameLayer();
    void finishLayerRename();
    void showLayerMenu(const QPoint &position);
    void chooseColor();
    void batch();
    void recover();
    void autosave();
    void error(const QString &message);
    void mutate(const std::function<void(Document *)> &fn);
};
