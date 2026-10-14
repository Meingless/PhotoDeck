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
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
class QCheckBox;
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
    QTabWidget *tabs;
    QWidget *workspace = nullptr;
    QFrame *floatingTools = nullptr, *floatingLayers = nullptr;
    QToolButton *toolsHandle = nullptr, *layersHandle = nullptr;
    QToolBar *documentToolbar, *optionsToolbar, *toolsBar, *ribbonToolbar;
    QDockWidget *toolsDock, *colorDock, *layersDock, *historyDock, *filmDock, *brushDock;
    QWidget *layerPanel;
    QLabel *brandLabel;
    QListWidget *filmstrip;
    QComboBox *presetPicker;
    QMenu *presetMenu, *userPresetMenu;
    QVector<QPair<QString, UiSettings>> userPresets;
    UiSettings appearance;
    bool persistAppearance = true, applyingAppearance = false;
    QTimer uiSaveTimer;
    QPoint panelDragStart, panelStart;
    UiSettings captureUiSettings() const;
    void applyAppearanceColors();
    void arrangePreset();
    void positionOverlays();
    void customizeUi();
    void saveUiSettings();
    void loadUiSettings();
    void rebuildUserPresets();
    void saveUserPreset();
    void buildAppearanceExtras();
    QDockWidget *createDock(const QString &, const QString &, QWidget *);
    void queueUiSave();
    void syncPanelActions();
    QListWidget *layers, *history;
    QComboBox *blend;
    QSlider *opacity, *brightness, *contrast, *saturation;
    QSpinBox *sizeInput, *hardnessInput, *brushOpacityInput;
    QComboBox *selectionModeInput;
    QSpinBox *selectionToleranceInput;
    QCheckBox *contiguousSelectionInput;
    QToolButton *colorButton, *maskButton;
    QLabel *info, *status, *zoomLabel, *layerDetails;
    Histogram *histogram;
    QTimer refreshTimer, autosaveTimer;
    bool refreshing = false;
    QHash<Document *, QString> recoveryFiles;
    QHash<Document *, QString> recoveryStates;
    QHash<QString, QAction *> actions;
    void buildMenus();
    void buildTools();
    void buildInspector();
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
    void textDialog(QPoint point, bool edit = false);
    void renameLayer();
    void chooseColor();
    void batch();
    void recover();
    void autosave();
    void error(const QString &message);
    void mutate(const std::function<void(Document *)> &fn);
};
