#include "mainwindow.h"
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <cmath>

namespace {
QIcon toolIcon(int kind, QColor color = QColor("#d5dee5")) {
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    switch (kind) {
    case Canvas::Move:
        p.drawLine(14, 4, 14, 24);
        p.drawLine(4, 14, 24, 14);
        p.drawPolyline(QPolygonF{{10, 8}, {14, 4}, {18, 8}});
        p.drawPolyline(QPolygonF{{10, 20}, {14, 24}, {18, 20}});
        p.drawPolyline(QPolygonF{{8, 10}, {4, 14}, {8, 18}});
        p.drawPolyline(QPolygonF{{20, 10}, {24, 14}, {20, 18}});
        break;
    case Canvas::Select:
        p.setPen(QPen(color, 1.65, Qt::DashLine));
        p.drawRect(5, 6, 18, 16);
        break;
    case Canvas::Crop:
        p.drawPolyline(QPolygonF{{8, 3}, {8, 20}, {25, 20}});
        p.drawPolyline(QPolygonF{{3, 8}, {20, 8}, {20, 25}});
        break;
    case Canvas::Brush:
        p.drawPolyline(QPolygonF{{10, 17}, {18, 5}, {22, 8}, {13, 20}});
        p.drawArc(4, 16, 10, 9, 0, 180 * 16);
        p.drawLine(4, 24, 12, 24);
        break;
    case Canvas::Pencil:
        p.drawPolygon(QPolygonF{{5, 23}, {7, 16}, {19, 4}, {24, 9}, {12, 21}});
        p.drawLine(16, 7, 21, 12);
        break;
    case Canvas::Eraser:
        p.drawPolygon(QPolygonF{{5, 17}, {16, 5}, {24, 12}, {14, 23}, {11, 23}});
        p.drawLine(10, 12, 19, 19);
        p.drawLine(14, 23, 24, 23);
        break;
    case Canvas::Clone:
        p.drawRoundedRect(10, 4, 8, 12, 3, 3);
        p.drawPolyline(QPolygonF{{10, 14}, {5, 20}, {5, 24}, {23, 24}, {23, 20}, {18, 14}});
        p.drawLine(5, 20, 23, 20);
        break;
    case Canvas::Fill:
        p.drawPolygon(QPolygonF{{4, 14}, {13, 5}, {23, 15}, {14, 24}});
        p.drawLine(7, 12, 19, 12);
        p.drawLine(10, 7, 10, 3);
        p.drawLine(24, 18, 24, 23);
        break;
    case Canvas::Gradient: {
        QLinearGradient g(4, 0, 24, 0);
        g.setColorAt(0, color);
        g.setColorAt(1, QColor("#384851"));
        p.fillRect(5, 6, 18, 16, g);
        p.drawRect(5, 6, 18, 16);
        break;
    }
    case Canvas::Text:
        p.drawLine(5, 6, 23, 6);
        p.drawLine(14, 6, 14, 24);
        p.drawLine(10, 24, 18, 24);
        p.drawLine(5, 6, 5, 10);
        p.drawLine(23, 6, 23, 10);
        break;
    case Canvas::Rectangle:
        p.drawRoundedRect(5, 6, 18, 16, 2, 2);
        break;
    case Canvas::Ellipse:
        p.drawEllipse(5, 6, 18, 16);
        break;
    case Canvas::Picker:
        p.drawPolyline(QPolygonF{{6, 22}, {6, 18}, {19, 5}, {23, 9}, {10, 22}, {6, 22}});
        p.drawLine(15, 7, 21, 13);
        break;
    case Canvas::Hand:
        p.drawPolyline(QPolygonF{{8, 24},
                                 {4, 15},
                                 {6, 13},
                                 {10, 17},
                                 {10, 6},
                                 {13, 6},
                                 {13, 13},
                                 {13, 4},
                                 {16, 4},
                                 {16, 13},
                                 {16, 6},
                                 {19, 6},
                                 {19, 14},
                                 {19, 9},
                                 {22, 9},
                                 {22, 20},
                                 {18, 25}});
        break;
    }
    return QIcon(pm);
}
QWidget *row(QWidget *a, QWidget *b = nullptr, QWidget *c = nullptr) {
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(6);
    l->addWidget(a);
    if (b)
        l->addWidget(b);
    if (c)
        l->addWidget(c);
    return w;
}
QSlider *slider() {
    auto *s = new QSlider(Qt::Horizontal);
    s->setRange(-100, 100);
    return s;
}
QString recoveryDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery";
    QDir().mkpath(dir);
    return dir;
}
} // namespace
void Histogram::setImage(const QImage &im) {
    bins.fill(0);
    peak = 1;
    QImage small = im.scaled(256, 160, Qt::KeepAspectRatio, Qt::FastTransformation)
                       .convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < small.height(); ++y) {
        const auto *r = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        for (int x = 0; x < small.width(); ++x)
            if (qAlpha(r[x]))
                ++bins[qGray(r[x]) / 2];
    }
    for (int b : bins)
        peak = qMax(peak, b);
    update();
}
void Histogram::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), colors.color(QPalette::Base));
    p.setPen(colors.color(QPalette::Mid));
    p.drawLine(0, height() - 1, width(), height() - 1);
    p.setPen(colors.color(QPalette::Highlight));
    for (int i = 0; i < 128; ++i) {
        qreal x = i * width() / 128.0, h = (height() - 8) * std::log1p(bins[i]) / std::log1p(peak);
        p.drawLine(QPointF(x, height() - 2), QPointF(x, height() - 2 - h));
    }
}
MainWindow::MainWindow(bool recovery, bool useSettings) {
    persistAppearance = useSettings;
    setObjectName("PhotoDeck");
    setWindowTitle("PhotoDeck");
    resize(1380, 880);
    setMinimumSize(900, 620);
    setAcceptDrops(true);
    tabs = new QTabWidget;
    tabs->setTabsClosable(true);
    tabs->setMovable(true);
    tabs->setDocumentMode(true);
    tabs->setObjectName("documents");
    workspace = new QWidget;
    workspace->setObjectName("workspace");
    auto *workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->addWidget(tabs);
    workspace->installEventFilter(this);
    setCentralWidget(workspace);
    buildMenus();
    buildTools();
    buildInspector();
    status = new QLabel("Hazır");
    status->setMinimumWidth(80);
    info = new QLabel;
    zoomLabel = new QLabel;
    statusBar()->addWidget(status, 1);
    statusBar()->addPermanentWidget(info);
    statusBar()->addPermanentWidget(zoomLabel);
    auto *fit = new QPushButton("Sığdır");
    fit->setFlat(true);
    connect(fit, &QPushButton::clicked, this, [this] {
        if (currentCanvas())
            currentCanvas()->fit();
    });
    statusBar()->addPermanentWidget(fit);
    auto *actual = new QPushButton("1:1");
    actual->setFlat(true);
    connect(actual, &QPushButton::clicked, this, [this] {
        if (currentCanvas())
            currentCanvas()->actualSize();
    });
    statusBar()->addPermanentWidget(actual);
    refreshTimer.setSingleShot(true);
    refreshTimer.setInterval(25);
    connect(&refreshTimer, &QTimer::timeout, this, &MainWindow::refresh);
    connect(tabs, &QTabWidget::currentChanged, this, [this] { refresh(); });
    connect(tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    autosaveTimer.setParent(this);
    autosaveTimer.setObjectName("autosaveTimer");
    autosaveTimer.setInterval(45000);
    connect(&autosaveTimer, &QTimer::timeout, this, &MainWindow::autosave);
    autosaveTimer.start();
    buildAppearanceExtras();
    uiSaveTimer.setSingleShot(true);
    uiSaveTimer.setInterval(300);
    connect(&uiSaveTimer, &QTimer::timeout, this, &MainWindow::saveUiSettings);
    if (useSettings)
        loadUiSettings();
    else
        applyUiPreset("focus", false);
    if (recovery)
        QTimer::singleShot(0, this, &MainWindow::recover);
}
Document *MainWindow::currentDocument() const {
    auto *c = currentCanvas();
    return c ? c->document : nullptr;
}
Canvas *MainWindow::currentCanvas() const {
    return qobject_cast<Canvas *>(tabs->currentWidget());
}
QAction *MainWindow::action(const QString &id, const QString &text, const QKeySequence &shortcut,
                            std::function<void()> fn) {
    auto *a = new QAction(text, this);
    a->setObjectName(id);
    if (!shortcut.isEmpty())
        a->setShortcut(shortcut);
    connect(a, &QAction::triggered, this, [this, fn](bool) {
        if (auto *canvas = currentCanvas())
            canvas->finishPendingGesture();
        fn();
    });
    actions[id] = a;
    addAction(a); // Shortcuts remain reachable when users hide menus and toolbars.
    return a;
}
void MainWindow::buildMenus() {
    menuBar()->setNativeMenuBar(false);
    auto *file = menuBar()->addMenu("&Dosya");
    file->addAction(action("new", "Yeni belge…", QKeySequence::New, [this] { newDocument(); }));
    file->addAction(action("open", "Aç…", QKeySequence::Open, [this] { chooseOpen(); }));
    file->addAction(action("import", "Görüntüyü katman olarak ekle…", QKeySequence("Ctrl+Shift+O"),
                           [this] { importLayer(); }));
    file->addSeparator();
    file->addAction(action("save", "Projeyi kaydet", QKeySequence::Save,
                           [this] { saveDocument(currentDocument()); }));
    file->addAction(action("saveAs", "Projeyi farklı kaydet…", QKeySequence::SaveAs,
                           [this] { saveDocument(currentDocument(), true); }));
    file->addAction(
        action("export", "Dışa aktar…", QKeySequence("Ctrl+Shift+E"), [this] { exportDocument(); }));
    file->addSeparator();
    file->addAction(action("batch", "Toplu dönüştür…", {}, [this] { batch(); }));
    file->addAction(action("close", "Belgeyi kapat", QKeySequence::Close,
                           [this] { closeTab(tabs->currentIndex()); }));
    file->addSeparator();
    file->addAction(action("quit", "Çıkış", QKeySequence::Quit, [this] { close(); }));
    auto *edit = menuBar()->addMenu("&Düzenle");
    edit->addAction(action("undo", "Geri al", QKeySequence::Undo, [this] {
        if (currentDocument())
            currentDocument()->undo();
    }));
    edit->addAction(action("redo", "Yinele", QKeySequence::Redo, [this] {
        if (currentDocument())
            currentDocument()->redo();
    }));
    edit->addSeparator();
    edit->addAction(action("selectAll", "Tümünü seç", QKeySequence::SelectAll, [this] {
        if (currentDocument()) {
            currentDocument()->selection = QRect(QPoint(), currentDocument()->state.size);
            currentDocument()->changed();
        }
    }));
    edit->addAction(action("deselect", "Seçimi kaldır", QKeySequence("Ctrl+D"), [this] {
        if (currentDocument()) {
            currentDocument()->selection = {};
            currentDocument()->changed();
        }
    }));
    edit->addAction(
        action("deleteSelection", "Seçili pikselleri sil", QKeySequence(Qt::Key_Delete), [this] {
            if (currentCanvas())
                currentCanvas()->deleteSelection();
        }));
    edit->addAction(action("copy", "Birleşik görüntüyü kopyala", QKeySequence::Copy, [this] {
        if (auto *d = currentDocument()) {
            QImage im = d->composite();
            if (!d->selection.isEmpty())
                im = im.copy(d->selection);
            QApplication::clipboard()->setImage(im);
        }
    }));
    edit->addAction(action("paste", "Görüntüyü katman olarak yapıştır", QKeySequence::Paste, [this] {
        if (auto *d = currentDocument()) {
            QImage im =
                QApplication::clipboard()->image().convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (im.isNull())
                return;
            if (!Document::validSize(im.size()) ||
                d->imageBytes() + im.sizeInBytes() > Document::MaxLayerBytes ||
                d->state.layers.size() >= 64) {
                error("Pano görüntüsü boyut veya bellek sınırını aşıyor.");
                return;
            }
            mutate([im](Document *doc) {
                Layer l;
                l.name = "Panodan";
                l.pixels = im;
                doc->state.layers.insert(doc->state.active + 1, l);
                ++doc->state.active;
                doc->editingMask = false;
            });
        }
    }));
    auto *image = menuBar()->addMenu("&Görüntü");
    image->addAction(action("resize", "Görüntü boyutu…", {}, [this] { resizeDocument(); }));
    image->addAction(action("crop", "Seçime göre kırp", QKeySequence("Ctrl+Shift+X"), [this] {
        if (auto *d = currentDocument()) {
            QString err;
            if (!d->crop(d->selection, &err))
                error(err);
            else
                currentCanvas()->fit();
        }
    }));
    image->addSeparator();
    image->addAction(action("rotateRight", "90° sağa döndür", {}, [this] {
        if (auto *d = currentDocument()) {
            QString e;
            d->rotate(true, &e);
            currentCanvas()->fit();
        }
    }));
    image->addAction(action("rotateLeft", "90° sola döndür", {}, [this] {
        if (auto *d = currentDocument()) {
            QString e;
            d->rotate(false, &e);
            currentCanvas()->fit();
        }
    }));
    image->addAction(action("flipH", "Yatay çevir", {}, [this] {
        if (currentDocument())
            currentDocument()->flip(true);
    }));
    image->addAction(action("flipV", "Dikey çevir", {}, [this] {
        if (currentDocument())
            currentDocument()->flip(false);
    }));
    auto *filters = menuBar()->addMenu("&Filtre");
    filters->addAction(action("blur", "Bulanıklık…", {}, [this] {
        if (auto *d = currentDocument()) {
            auto &l = d->state.layers[d->state.active];
            if (l.locked)
                return;
            bool ok;
            int radius = QInputDialog::getInt(this, "Geri alınabilir bulanıklık",
                                              "Yarıçap (0: kapalı, 1–20 px)", l.blur, 0, 20, 1, &ok);
            if (ok)
                mutate([radius](Document *doc) { doc->state.layers[doc->state.active].blur = radius; });
        }
    }));
    filters->addAction(action("grayscale", "Siyah beyaz", {}, [this] {
        if (auto *d = currentDocument()) {
            d->beginEdit();
            auto &l = d->state.layers[d->state.active];
            d->adjust(l.brightness, l.contrast, -100);
            d->commit("Siyah beyaz");
        }
    }));
    auto *layer = menuBar()->addMenu("&Katman");
    layer->addAction(action("addLayer", "Yeni katman", QKeySequence("Ctrl+Shift+N"), [this] {
        if (auto *d = currentDocument()) {
            QString e;
            if (!d->addLayer(QString("Katman %1").arg(d->state.layers.size() + 1), &e))
                error(e);
        }
    }));
    layer->addAction(action("duplicate", "Katmanı çoğalt", QKeySequence("Ctrl+J"), [this] {
        if (currentDocument()) {
            QString e;
            if (!currentDocument()->duplicate(&e))
                error(e);
        }
    }));
    layer->addAction(action("rename", "Yeniden adlandır…", {}, [this] { renameLayer(); }));
    layer->addAction(action("editText", "Metni düzenle…", {}, [this] { textDialog({}, true); }));
    layer->addAction(action("deleteLayer", "Katmanı sil", {}, [this] {
        if (currentDocument())
            currentDocument()->deleteLayer();
    }));
    layer->addSeparator();
    layer->addAction(action("layerUp", "Üste taşı", {}, [this] {
        if (currentDocument())
            currentDocument()->reorder(1);
    }));
    layer->addAction(action("layerDown", "Alta taşı", {}, [this] {
        if (currentDocument())
            currentDocument()->reorder(-1);
    }));
    layer->addAction(action("lock", "Kilidi aç / kapat", {}, [this] {
        mutate([](Document *d) {
            auto &l = d->state.layers[d->state.active];
            l.locked = !l.locked;
        });
    }));
    layer->addAction(action("mask", "Maske ekle / düzenle", {}, [this] {
        if (auto *d = currentDocument()) {
            QString e;
            if (!d->addMask(&e)) {
                if (!e.isEmpty())
                    error(e);
            } else if (d->editingMask) {
                currentCanvas()->foreground = Qt::white;
                currentCanvas()->background = Qt::black;
                refresh();
            }
        }
    }));
    layer->addAction(action("invertMask", "Maskeyi ters çevir", {}, [this] {
        if (currentDocument())
            currentDocument()->invertMask();
    }));
    layer->addAction(action("removeMask", "Maskeyi kaldır", {}, [this] {
        if (currentDocument())
            currentDocument()->removeMask();
    }));
    auto *view = menuBar()->addMenu("&Görünüm");
    view->addAction(action("fit", "Ekrana sığdır", QKeySequence("Ctrl+0"), [this] {
        if (currentCanvas())
            currentCanvas()->fit();
    }));
    view->addAction(action("actual", "Gerçek boyut", QKeySequence("Ctrl+1"), [this] {
        if (currentCanvas())
            currentCanvas()->actualSize();
    }));
    view->addAction(action("zoomIn", "Yakınlaştır", QKeySequence::ZoomIn, [this] {
        if (currentCanvas())
            currentCanvas()->setZoom(currentCanvas()->zoom * 1.25);
    }));
    view->addAction(action("zoomOut", "Uzaklaştır", QKeySequence::ZoomOut, [this] {
        if (currentCanvas())
            currentCanvas()->setZoom(currentCanvas()->zoom / 1.25);
    }));
    auto *help = menuBar()->addMenu("&Yardım");
    help->addAction(action("demo", "Örnek çalışmayı aç", {}, [this] { demo(); }));
    help->addAction(action("help", "Araçlar ve kısayollar", QKeySequence::HelpContents, [this] {
        QMessageBox::information(
            this, "PhotoDeck kılavuzu",
            "V Taşı • M Seçim • C Kırp • B Fırça • P Kalem\nE Silgi • S Klon • F Kova • G Degrade • T "
            "Metin\nU Dikdörtgen • O Elips • I Damlalık • H El\n\nKlon damgası: Alt + tıklama ile aynı "
            "katmandan kaynak seçin.\nKırpma: alanı sürükleyin, sonra Ctrl+Shift+X.\nMetin: metin "
            "aracında tuvale tıklayın; katmana çift tıklayarak düzenleyin.\nMaske: beyaz gösterir, "
            "siyah gizler. X renkleri değiştirir.\nŞekil: Shift ile kare / daire. Tüm çizim araçları "
            "seçime uyar.\nTekerlek: işaretçinin çevresinde yakınlaştırma. Orta tuş: gezinme.\n\n.pdeck "
            "proje dosyası katmanları, maskeleri ve renk ayarlarını korur.\nPNG/WebP saydamlığı korur; "
            "JPEG beyaz arka planla düzleştirilir.\nDönüştürme, çizim ve silme metin katmanını piksele "
            "dönüştürür.\nKilit, doğrudan katman düzenlemesini engeller; tüm görüntü dönüşümleri "
            "kilitli katmanları da dönüştürür.\n\nSınırlar: 16 MP tuval, 64 katman, 256 MiB katman "
            "verisi.\nGeçmiş: 30 adım / 128 MiB; büyük belgelerde adım sayısı azalır.\n45 saniyede bir "
            "kurtarma kopyası alınır.\n\nTamamı bilgisayarınızda çalışır; AI, hesap ve ağ bağlantısı "
            "gerekmez.");
    }));
    help->addAction(action("about", "PhotoDeck hakkında", {}, [this] {
        QMessageBox::about(this, "PhotoDeck 0.2.0 · Erken erişim",
                           "<h2>PhotoDeck</h2><p>Hafif, yerel fotoğraf ve katman düzenleyici.</p><p>Qt "
                           "6 / C++ · Linux / Windows / macOS</p><p>AI, reklam, telemetri ve abonelik "
                           "içermez.</p><p>Bu sürüm RGB raster düzenlemeye odaklanır. PSD/RAW, CMYK/ICC "
                           "baskı yönetimi, vektör yolları ve Photoshop eklentileri desteklenmez.</p>");
    }));
}
void MainWindow::buildTools() {
    auto *top = addToolBar("Belge");
    top->setObjectName("documentToolbar");
    documentToolbar = top;
    top->setMovable(true);
    top->setIconSize({20, 20});
    auto *brand = new QLabel("  PhotoDeck  ");
    brandLabel = brand;
    brand->setObjectName("brandLabel");
    top->addWidget(brand);
    top->addSeparator();
    top->addAction(actions["open"]);
    top->addAction(actions["save"]);
    top->addSeparator();
    top->addAction(actions["undo"]);
    top->addAction(actions["redo"]);
    top->addSeparator();
    top->addAction(actions["export"]);
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    top->addWidget(spacer);
    auto *local = new QLabel("●  YEREL ÇALIŞMA  ");
    local->setObjectName("localLabel");
    top->addWidget(local);
    auto *options = addToolBar("Araç seçenekleri");
    options->setObjectName("options");
    optionsToolbar = options;
    options->setMovable(true);
    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(Qt::TopToolBarArea, options);
    options->addWidget(new QLabel("  Boyut  "));
    sizeInput = new QSpinBox;
    sizeInput->setRange(1, 512);
    sizeInput->setValue(32);
    sizeInput->setSuffix(" px");
    sizeInput->setFixedWidth(92);
    options->addWidget(sizeInput);
    options->addWidget(new QLabel("   Sertlik  "));
    hardnessInput = new QSpinBox;
    hardnessInput->setRange(0, 100);
    hardnessInput->setValue(80);
    hardnessInput->setSuffix(" %");
    hardnessInput->setFixedWidth(83);
    options->addWidget(hardnessInput);
    options->addWidget(new QLabel("   Akış  "));
    brushOpacityInput = new QSpinBox;
    brushOpacityInput->setRange(1, 100);
    brushOpacityInput->setValue(100);
    brushOpacityInput->setSuffix(" %");
    brushOpacityInput->setFixedWidth(83);
    options->addWidget(brushOpacityInput);
    options->addSeparator();
    options->addAction(actions["crop"]);
    auto syncBrush = [this] {
        if (auto *c = currentCanvas()) {
            c->brushSize = sizeInput->value();
            c->hardness = hardnessInput->value();
            c->brushOpacity = brushOpacityInput->value();
        }
    };
    connect(sizeInput, &QSpinBox::valueChanged, this, [syncBrush](int) { syncBrush(); });
    connect(hardnessInput, &QSpinBox::valueChanged, this, [syncBrush](int) { syncBrush(); });
    connect(brushOpacityInput, &QSpinBox::valueChanged, this, [syncBrush](int) { syncBrush(); });
    auto *tools = new QToolBar("Araçlar");
    tools->setObjectName("tools");
    toolsBar = tools;
    tools->setMovable(false);
    tools->setIconSize({26, 26});
    tools->setOrientation(Qt::Vertical);
    auto *group = new QActionGroup(this);
    group->setExclusive(true);
    QStringList names = {
        "Taşı",        "Dikdörtgen seçim", "Kırp",  "Fırça",      "Kalem", "Silgi",    "Klon damgası",
        "Boya kovası", "Degrade",          "Metin", "Dikdörtgen", "Elips", "Damlalık", "El"};
    QStringList keys = {"V", "M", "C", "B", "P", "E", "S", "F", "G", "T", "U", "O", "I", "H"};
    for (int i = 0; i < names.size(); ++i) {
        auto *a = action("tool" + QString::number(i), names[i] + " (" + keys[i] + ")",
                         QKeySequence(keys[i]), [this, i, names] {
                             if (auto *c = currentCanvas()) {
                                 c->setTool(Canvas::Tool(i));
                                 status->setText(names[i] +
                                                 (i == Canvas::Clone ? " · Alt + tıklama: kaynak" : ""));
                             }
                         });
        a->setIcon(toolIcon(i));
        a->setCheckable(true);
        group->addAction(a);
        if (i == 0)
            a->setChecked(true);
        tools->addAction(a);
    }
    tools->addSeparator();
    colorButton = new QToolButton;
    colorButton->setObjectName("foreground");
    colorButton->setFixedSize(36, 36);
    colorButton->setToolTip("Ön plan rengini seç");
    connect(colorButton, &QToolButton::clicked, this, &MainWindow::chooseColor);
    tools->addWidget(colorButton);
    auto *swap = action("swapColors", "Renkleri değiştir (X)", QKeySequence("X"), [this] {
        if (auto *c = currentCanvas()) {
            std::swap(c->foreground, c->background);
            refresh();
        }
    });
    swap->setText("⇄");
    tools->addAction(swap);
    toolsDock = createDock("Araçlar", "toolsDock", tools);
    toolsDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::LeftDockWidgetArea, toolsDock);
}
void MainWindow::buildInspector() {
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(9);
    auto *heading = new QLabel("RENK & TON");
    heading->setObjectName("sectionTitle");
    layout->addWidget(heading);
    histogram = new Histogram;
    histogram->setFixedHeight(56);
    layout->addWidget(histogram);
    auto *form = new QFormLayout;
    form->setSpacing(10);
    brightness = slider();
    contrast = slider();
    saturation = slider();
    brightness->setObjectName("brightness");
    contrast->setObjectName("contrast");
    saturation->setObjectName("saturation");
    form->addRow("Parlaklık", brightness);
    form->addRow("Kontrast", contrast);
    form->addRow("Doygunluk", saturation);
    layout->addLayout(form);
    for (auto *s : {brightness, contrast, saturation}) {
        connect(s, &QSlider::sliderPressed, this, [this] {
            if (currentDocument())
                currentDocument()->beginEdit();
        });
        connect(s, &QSlider::valueChanged, this, [this, s](int v) {
            s->setToolTip(QString::number(v));
            if (refreshing || !currentDocument())
                return;
            auto *d = currentDocument();
            bool keyboard = !s->isSliderDown();
            if (keyboard)
                d->beginEdit();
            d->adjust(brightness->value(), contrast->value(), saturation->value());
            if (keyboard)
                d->commit("Renk ayarı");
        });
        connect(s, &QSlider::sliderReleased, this, [this] {
            if (currentDocument())
                currentDocument()->commit("Renk ayarı");
        });
    }
    auto *reset = new QPushButton("Renk ayarlarını sıfırla");
    reset->setFlat(true);
    connect(reset, &QPushButton::clicked, this, [this] {
        if (auto *d = currentDocument()) {
            d->beginEdit();
            d->adjust(0, 0, 0);
            d->commit("Renk ayarları sıfırlandı");
        }
    });
    layout->addWidget(reset);
    colorDock = createDock("Renk & ton", "colorDock", panel);
    addDockWidget(Qt::RightDockWidgetArea, colorDock);
    panel = new QWidget;
    layerPanel = panel;
    layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(7);
    auto *heading2 = new QLabel("KATMANLAR");
    heading2->setObjectName("sectionTitle");
    layout->addWidget(heading2);
    blend = new QComboBox;
    blend->addItems({"Normal", "Çarp", "Ekran", "Kaplama", "Koyulaştır", "Açıklaştır"});
    blend->setObjectName("blendMode");
    layout->addWidget(blend);
    opacity = slider();
    opacity->setRange(0, 100);
    opacity->setValue(100);
    opacity->setObjectName("layerOpacity");
    layout->addWidget(row(new QLabel("Opaklık"), opacity));
    connect(blend, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (!refreshing)
            mutate([v](Document *d) {
                auto &l = d->state.layers[d->state.active];
                if (!l.locked)
                    l.blend = v;
            });
    });
    connect(opacity, &QSlider::sliderPressed, this, [this] {
        if (currentDocument())
            currentDocument()->beginEdit();
    });
    connect(opacity, &QSlider::valueChanged, this, [this](int v) {
        opacity->setToolTip(QString::number(v) + " %");
        if (refreshing || !currentDocument())
            return;
        auto *d = currentDocument();
        if (d->state.layers[d->state.active].locked)
            return;
        bool keyboard = !opacity->isSliderDown();
        if (keyboard)
            d->beginEdit();
        d->state.layers[d->state.active].opacity = v / 100.0;
        d->changed();
        if (keyboard)
            d->commit("Katman opaklığı");
    });
    connect(opacity, &QSlider::sliderReleased, this, [this] {
        if (currentDocument())
            currentDocument()->commit("Katman opaklığı");
    });
    layers = new QListWidget;
    layers->setObjectName("layers");
    layers->setIconSize({50, 36});
    layers->setMinimumHeight(100);
    layers->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layers->setTextElideMode(Qt::ElideRight);
    layers->setSpacing(3);
    layout->addWidget(layers, 1);
    connect(layers, &QListWidget::currentRowChanged, this, [this](int r) {
        if (refreshing || r < 0 || !currentDocument())
            return;
        currentCanvas()->finishPendingGesture();
        currentDocument()->state.active = currentDocument()->state.layers.size() - 1 - r;
        currentDocument()->editingMask = false;
        refresh();
    });
    connect(layers, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (refreshing || !currentDocument())
            return;
        int index = currentDocument()->state.layers.size() - 1 - layers->row(item);
        bool on = item->checkState() == Qt::Checked;
        mutate([index, on](Document *d) { d->state.layers[index].visible = on; });
    });
    connect(layers, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        if (!currentDocument())
            return;
        if (!currentDocument()->state.layers[currentDocument()->state.active].text.isEmpty())
            textDialog({}, true);
        else
            renameLayer();
    });
    auto *buttons = new QWidget;
    auto *bl = new QHBoxLayout(buttons);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(3);
    for (const auto &id :
         QStringList{"addLayer", "duplicate", "layerUp", "layerDown", "lock", "deleteLayer"}) {
        auto *b = new QToolButton;
        b->setDefaultAction(actions[id]);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setText(id == "addLayer"    ? "+"
                   : id == "duplicate" ? "▣"
                   : id == "layerUp"   ? "↑"
                   : id == "layerDown" ? "↓"
                   : id == "lock"      ? "◇"
                                       : "−");
        b->setMinimumSize(24, 29);
        b->setMaximumHeight(34);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        bl->addWidget(b);
    }
    layout->addWidget(buttons);
    maskButton = new QToolButton;
    maskButton->setDefaultAction(actions["mask"]);
    maskButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    maskButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(maskButton);
    layerDetails = new QLabel;
    layerDetails->setWordWrap(true);
    layerDetails->setObjectName("layerDetails");
    layout->addWidget(layerDetails);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(panel);
    layersDock = createDock("Katmanlar", "layersDock", scroll);
    layerPanel = scroll;
    addDockWidget(Qt::RightDockWidgetArea, layersDock);
    panel = new QWidget;
    layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 10, 10, 10);
    history = new QListWidget;
    history->setObjectName("history");
    history->setFocusPolicy(Qt::NoFocus);
    history->setMinimumHeight(70);
    layout->addWidget(history);
    historyDock = createDock("Geçmiş", "historyDock", panel);
    addDockWidget(Qt::RightDockWidgetArea, historyDock);
}

void MainWindow::addDocument(Document *d) {
    d->setParent(this);
    auto *canvas = new Canvas(d);
    canvas->showRulers = appearance.rulers;
    {
        auto p = appearance.resolved();
        canvas->setUiColors(p.canvas, p.panel, p.muted, p.accent);
    }
    tabs->addTab(canvas, d->title);
    tabs->setCurrentWidget(canvas);
    connect(d, &Document::updated, this, [this] {
        if (!refreshTimer.isActive())
            refreshTimer.start();
    });
    connect(canvas, &Canvas::zoomChanged, this, [this] {
        if (currentCanvas())
            zoomLabel->setText(QString::number(qRound(currentCanvas()->zoom * 100)) + " %  ");
    });
    connect(canvas, &Canvas::toolHint, status, &QLabel::setText);
    connect(canvas, &Canvas::colorPicked, this, [this, canvas](QColor c) {
        canvas->foreground = c;
        refresh();
    });
    connect(canvas, &Canvas::textRequested, this, [this](QPoint p) { textDialog(p); });
    QTimer::singleShot(0, canvas, &Canvas::fit);
    refresh();
}
void MainWindow::demo() {
    auto *d = new Document;
    d->makeDemo();
    addDocument(d);
}
bool MainWindow::openFile(const QString &file) {
    for (int i = 0; i < tabs->count(); ++i) {
        auto *d = qobject_cast<Canvas *>(tabs->widget(i))->document;
        if (!d->path.isEmpty() &&
            QFileInfo(d->path).canonicalFilePath() == QFileInfo(file).canonicalFilePath()) {
            tabs->setCurrentIndex(i);
            return true;
        }
    }
    auto *d = new Document;
    QString e;
    bool ok = QFileInfo(file).suffix().compare("pdeck", Qt::CaseInsensitive) == 0
                  ? d->load(file, &e)
                  : d->importImage(file, false, &e);
    if (!ok) {
        delete d;
        error(e);
        return false;
    }
    addDocument(d);
    return true;
}
void MainWindow::refresh() {
    refreshing = true;
    auto *d = currentDocument();
    auto *c = currentCanvas();
    bool present = d;
    for (auto it = actions.begin(); it != actions.end(); ++it)
        if (it.key() != "new" && it.key() != "open" && it.key() != "quit" && it.key() != "demo" &&
            it.key() != "help" && it.key() != "about" && it.key() != "batch" &&
            !it.key().startsWith("ui."))
            it.value()->setEnabled(present);
    for (auto *s : {brightness, contrast, saturation, opacity})
        s->setEnabled(present);
    blend->setEnabled(present);
    maskButton->setEnabled(present);
    layers->clear();
    history->clear();
    for (int i = 0; i < tabs->count(); ++i) {
        auto *td = qobject_cast<Canvas *>(tabs->widget(i))->document;
        tabs->setTabText(i, td->title + (td->dirty ? " •" : ""));
    }
    if (!d) {
        setWindowTitle("PhotoDeck");
        info->clear();
        layerDetails->setText("Dosya → Yeni belge ile başlayın.");
        colorButton->setStyleSheet("");
        refreshing = false;
        return;
    }
    setWindowTitle(d->title + (d->dirty ? " *" : "") + " — PhotoDeck");
    actions["undo"]->setEnabled(d->canUndo());
    actions["redo"]->setEnabled(d->canRedo());
    actions["deleteLayer"]->setEnabled(d->state.layers.size() > 1);
    auto &active = d->state.layers[d->state.active];
    actions["editText"]->setEnabled(!active.text.isEmpty() && !active.locked);
    actions["removeMask"]->setEnabled(!active.mask.isNull() && !active.locked);
    actions["invertMask"]->setEnabled(!active.mask.isNull() && !active.locked);
    brightness->setValue(active.brightness);
    contrast->setValue(active.contrast);
    saturation->setValue(active.saturation);
    opacity->setValue(qRound(active.opacity * 100));
    blend->setCurrentIndex(active.blend);
    for (auto *s : {brightness, contrast, saturation, opacity})
        s->setEnabled(!active.locked);
    blend->setEnabled(!active.locked);
    for (int i = d->state.layers.size() - 1; i >= 0; --i) {
        auto &l = d->state.layers[i];
        QPixmap thumb(50, 36);
        thumb.fill(QColor("#38444f"));
        QPainter p(&thumb);
        QImage preview = l.rendered().scaled(50, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage((50 - preview.width()) / 2, (36 - preview.height()) / 2, preview);
        p.end();
        auto *item = new QListWidgetItem(QIcon(thumb), l.name + (l.locked ? "  ◇" : "") +
                                                           (!l.mask.isNull() ? "  ◧" : ""));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(l.visible ? Qt::Checked : Qt::Unchecked);
        item->setSizeHint({180, qMax(34, appearance.fontSize + 22)});
        layers->addItem(item);
    }
    layers->setCurrentRow(d->state.layers.size() - 1 - d->state.active);
    QStringList labels = d->historyLabels();
    for (const auto &l : labels.mid(qMax(0, int(labels.size() - 4))))
        history->addItem(l);
    history->scrollToBottom();
    layerDetails->setText(
        d->editingMask ? "MASKE DÜZENLEME · Beyaz gösterir, siyah gizler. X: renkleri değiştir."
                       : QString("%1 × %2 px · %3\nKonum: %4, %5")
                             .arg(active.pixels.width())
                             .arg(active.pixels.height())
                             .arg(active.text.isEmpty() ? "Piksel katmanı" : "Düzenlenebilir metin")
                             .arg(active.offset.x())
                             .arg(active.offset.y()));
    maskButton->setText(active.mask.isNull() ? "◧  Katman maskesi ekle"
                        : d->editingMask     ? "◧  Piksele geri dön"
                                             : "◧  Maskeyi düzenle");
    maskButton->setEnabled(!active.locked);
    histogram->setImage(d->composite());
    info->setText(QString("%1 × %2 px  ·  %3 katman  ·  %4 MiB piksel  ")
                      .arg(d->state.size.width())
                      .arg(d->state.size.height())
                      .arg(d->state.layers.size())
                      .arg(d->imageBytes() / 1048576.0, 0, 'f', 1));
    zoomLabel->setText(QString::number(qRound(c->zoom * 100)) + " %  ");
    colorButton->setStyleSheet("QToolButton {background:" + c->foreground.name() +
                               ";border:3px solid #d3ddd8;border-radius:7px;}");
    sizeInput->setValue(c->brushSize);
    hardnessInput->setValue(c->hardness);
    brushOpacityInput->setValue(c->brushOpacity);
    actions["tool" + QString::number(c->tool)]->setChecked(true);
    if (filmDock && !filmDock->isHidden()) {
        QSignalBlocker block(filmstrip);
        filmstrip->clear();
        for (int i = 0; i < tabs->count(); ++i) {
            auto *doc = qobject_cast<Canvas *>(tabs->widget(i))->document;
            auto *item =
                new QListWidgetItem(QIcon(QPixmap::fromImage(doc->composite().scaled(
                                        96, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation))),
                                    doc->title);
            item->setSizeHint({120, 90});
            filmstrip->addItem(item);
        }
        filmstrip->setCurrentRow(tabs->currentIndex());
    }
    refreshing = false;
}
void MainWindow::error(const QString &m) {
    if (!m.isEmpty())
        QMessageBox::warning(this, "PhotoDeck", m);
}
void MainWindow::mutate(const std::function<void(Document *)> &fn) {
    if (auto *d = currentDocument()) {
        currentCanvas()->finishPendingGesture();
        d->beginEdit();
        fn(d);
        d->commit("Katman özelliği");
    }
}
void MainWindow::newDocument() {
    QDialog dialog(this);
    dialog.setWindowTitle("Yeni belge");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit("Adsız");
    auto *w = new QSpinBox;
    auto *h = new QSpinBox;
    w->setRange(1, 16384);
    h->setRange(1, 16384);
    w->setValue(1600);
    h->setValue(1000);
    auto *bg = new QComboBox;
    bg->addItems({"Saydam", "Beyaz"});
    form->addRow("Belge adı", name);
    form->addRow("Genişlik (px)", w);
    form->addRow("Yükseklik (px)", h);
    form->addRow("Arka plan", bg);
    layout->addLayout(form);
    auto *note = new QLabel("En fazla 16 megapiksel. Boyut büyüdükçe RAM kullanımı artar.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto *d = new Document;
    if (!d->create({w->value(), h->value()}, bg->currentIndex() == 1)) {
        delete d;
        error("Boyut sınırı aşıldı (16 megapiksel).");
        return;
    }
    d->title = name->text().trimmed().isEmpty() ? "Adsız" : name->text().left(128);
    addDocument(d);
}
void MainWindow::chooseOpen() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, "Görüntü veya proje aç", {},
        "Görüntü / PhotoDeck (*.pdeck *.png *.jpg *.jpeg *.webp *.bmp);;Tüm dosyalar (*)");
    for (const auto &file : files)
        openFile(file);
}
void MainWindow::importLayer() {
    if (!currentDocument())
        return;
    QString file = QFileDialog::getOpenFileName(this, "Katman olarak ekle", {},
                                                "Görüntüler (*.png *.jpg *.jpeg *.webp *.bmp)");
    if (file.isEmpty())
        return;
    QString e;
    if (!currentDocument()->importImage(file, true, &e))
        error(e);
}
bool MainWindow::saveDocument(Document *d, bool saveAs) {
    if (!d)
        return false;
    if (currentCanvas() && currentCanvas()->document == d)
        currentCanvas()->finishPendingGesture();
    QString file = d->path;
    if (saveAs || file.isEmpty()) {
        file = QFileDialog::getSaveFileName(this, "Katmanlı projeyi kaydet", d->title + ".pdeck",
                                            "PhotoDeck projesi (*.pdeck)");
        if (file.isEmpty())
            return false;
        if (!file.endsWith(".pdeck", Qt::CaseInsensitive))
            file += ".pdeck";
    }
    QString e;
    if (!d->save(file, &e)) {
        error(e);
        return false;
    }
    if (recoveryFiles.contains(d)) {
        QFile::remove(recoveryFiles[d]);
        recoveryStates.remove(d);
    }
    status->setText("Proje kaydedildi · " + QFileInfo(file).fileName());
    return true;
}
void MainWindow::exportDocument() {
    auto *d = currentDocument();
    if (!d)
        return;
    currentCanvas()->finishPendingGesture();
    QString filters = "PNG · saydam (*.png);;JPEG · beyaz arka plan (*.jpg)";
    if (QImageWriter::supportedImageFormats().contains("webp"))
        filters += ";;WebP · saydam (*.webp)";
    filters += ";;BMP (*.bmp)";
    QString selected;
    QString file = QFileDialog::getSaveFileName(this, "Görüntüyü dışa aktar",
                                                QFileInfo(d->title).completeBaseName() + ".png", filters,
                                                &selected);
    if (file.isEmpty())
        return;
    if (QFileInfo(file).suffix().isEmpty())
        file += selected.contains("JPEG")   ? ".jpg"
                : selected.contains("WebP") ? ".webp"
                : selected.contains("BMP")  ? ".bmp"
                                            : ".png";
    int quality = 95;
    if (QFileInfo(file).suffix().toLower() == "jpg" || QFileInfo(file).suffix().toLower() == "jpeg" ||
        QFileInfo(file).suffix().toLower() == "webp") {
        bool ok;
        quality =
            QInputDialog::getInt(this, "Dışa aktarım kalitesi", "Kalite (1–100)", 95, 1, 100, 1, &ok);
        if (!ok)
            return;
    }
    QString e;
    if (!d->exportImage(file, quality, &e))
        error(e);
    else
        status->setText("Dışa aktarıldı · " + QFileInfo(file).fileName());
}
bool MainWindow::confirmClose(int index) {
    if (index < 0)
        return true;
    auto *canvas = qobject_cast<Canvas *>(tabs->widget(index));
    canvas->finishPendingGesture();
    auto *d = canvas->document;
    if (!d->dirty)
        return true;
    tabs->setCurrentIndex(index);
    auto response = QMessageBox::question(
        this, "Kaydedilmemiş değişiklikler", d->title + " belgesindeki değişiklikler kaydedilsin mi?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (response != QMessageBox::Save && response != QMessageBox::Discard)
        return false;
    if (response == QMessageBox::Save)
        return saveDocument(d);
    return true;
}
void MainWindow::closeTab(int index) {
    if (index < 0 || !confirmClose(index))
        return;
    auto *canvas = qobject_cast<Canvas *>(tabs->widget(index));
    auto *d = canvas->document;
    if (recoveryFiles.contains(d))
        QFile::remove(recoveryFiles.take(d));
    recoveryStates.remove(d);
    tabs->removeTab(index);
    delete canvas;
    delete d;
    refresh();
}
void MainWindow::closeEvent(QCloseEvent *e) {
    for (int i = 0; i < tabs->count(); ++i)
        if (!confirmClose(i)) {
            e->ignore();
            return;
        }
    saveUiSettings();
    for (const auto &file : recoveryFiles)
        QFile::remove(file);
    e->accept();
}
void MainWindow::resizeDocument() {
    auto *d = currentDocument();
    if (!d)
        return;
    QDialog dialog(this);
    dialog.setWindowTitle("Görüntü boyutu");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *w = new QSpinBox;
    auto *h = new QSpinBox;
    w->setRange(1, 16384);
    h->setRange(1, 16384);
    w->setValue(d->state.size.width());
    h->setValue(d->state.size.height());
    auto *lock = new QCheckBox("En boy oranını koru");
    lock->setChecked(true);
    form->addRow("Genişlik (px)", w);
    form->addRow("Yükseklik (px)", h);
    layout->addLayout(form);
    layout->addWidget(lock);
    qreal ratio = qreal(d->state.size.width()) / d->state.size.height();
    connect(w, &QSpinBox::valueChanged, &dialog, [h, lock, ratio](int value) {
        if (lock->isChecked()) {
            QSignalBlocker b(h);
            h->setValue(qRound(value / ratio));
        }
    });
    connect(h, &QSpinBox::valueChanged, &dialog, [w, lock, ratio](int value) {
        if (lock->isChecked()) {
            QSignalBlocker b(w);
            w->setValue(qRound(value * ratio));
        }
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString err;
    if (!d->resize({w->value(), h->value()}, &err))
        error(err);
    else
        currentCanvas()->fit();
}
void MainWindow::textDialog(QPoint at, bool edit) {
    auto *d = currentDocument();
    if (!d)
        return;
    auto &l = d->state.layers[d->state.active];
    if (edit && (l.text.isEmpty() || l.locked))
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(edit ? "Metni düzenle" : "Metin ekle");
    dialog.resize(450, 300);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QPlainTextEdit(edit ? l.text : "Yeni metin");
    text->selectAll();
    layout->addWidget(text);
    auto *fonts = new QFontComboBox;
    auto *size = new QSpinBox;
    size->setRange(6, 512);
    size->setValue(edit ? l.font.pixelSize() : 64);
    if (edit)
        fonts->setCurrentFont(l.font);
    auto *bold = new QCheckBox("Kalın");
    bold->setChecked(edit && l.font.bold());
    layout->addWidget(row(fonts, size, bold));
    QColor textColor = edit ? l.color : currentCanvas()->foreground;
    auto *colorPick = new QPushButton("Metin rengi");
    layout->addWidget(colorPick);
    connect(colorPick, &QPushButton::clicked, &dialog, [&] {
        QColor picked = QColorDialog::getColor(textColor, &dialog, "Metin rengi");
        if (picked.isValid())
            textColor = picked;
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || text->toPlainText().isEmpty())
        return;
    if (text->toPlainText().size() > 8192) {
        error("Metin en fazla 8192 karakter olabilir.");
        return;
    }
    QFont f = edit ? l.font : fonts->currentFont();
    f.setFamily(fonts->currentFont().family());
    f.setPixelSize(size->value());
    f.setBold(bold->isChecked());
    QColor color = textColor;
    if (edit) {
        QString e;
        if (!d->editText(text->toPlainText(), f, color, &e))
            error(e);
    } else {
        QString e;
        d->addText(text->toPlainText(), f, color, at, &e);
        error(e);
    }
}
void MainWindow::renameLayer() {
    if (!currentDocument())
        return;
    auto &l = currentDocument()->state.layers[currentDocument()->state.active];
    bool ok;
    QString name = QInputDialog::getText(this, "Katman adı", "Yeni ad", QLineEdit::Normal, l.name, &ok);
    if (ok && !name.trimmed().isEmpty())
        mutate([name](Document *d) { d->state.layers[d->state.active].name = name.left(128); });
}
void MainWindow::chooseColor() {
    if (!currentCanvas())
        return;
    QColor color = QColorDialog::getColor(currentCanvas()->foreground, this, "Ön plan rengi");
    if (color.isValid()) {
        currentCanvas()->foreground = color;
        refresh();
    }
}
void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent *e) {
    for (const auto &url : e->mimeData()->urls())
        if (url.isLocalFile())
            openFile(url.toLocalFile());
    e->acceptProposedAction();
}
void MainWindow::autosave() {
    for (int i = 0; i < tabs->count(); ++i) {
        auto *d = qobject_cast<Canvas *>(tabs->widget(i))->document;
        if (!d->dirty || d->isEditing())
            continue;
        QString signature = QString::number(d->revision());
        if (recoveryStates.value(d) == signature)
            continue;
        if (!recoveryFiles.contains(d))
            recoveryFiles[d] =
                recoveryDir() + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".pdeck";
        QString e;
        if (d->save(recoveryFiles[d], &e, false))
            recoveryStates[d] = signature;
        else
            status->setText("Kurtarma kopyası yazılamadı: " + e);
    }
}
void MainWindow::recover() {
    QDir dir(recoveryDir());
    QStringList files = dir.entryList({"*.pdeck"}, QDir::Files);
    if (files.isEmpty())
        return;
    auto choice = QMessageBox::question(
        this, "Kurtarma kopyaları",
        QString("Önceki oturumdan %1 kurtarma kopyası bulundu. Açmak ister misiniz?\nEvet: kurtar · "
                "Hayır: kopyaları sil · İptal: sonra sor")
            .arg(files.size()),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
    if (choice == QMessageBox::Cancel)
        return;
    for (const auto &name : files) {
        QString file = dir.filePath(name);
        if (choice == QMessageBox::No) {
            QFile::remove(file);
            continue;
        }
        auto *d = new Document;
        QString e;
        if (!d->load(file, &e)) {
            delete d;
            error("Kurtarma dosyası açılamadı: " + name + "\n" + e);
            continue;
        }
        d->path.clear();
        d->title = "Kurtarılan çalışma";
        d->markRecovered();
        addDocument(d);
        recoveryFiles[d] = file;
        recoveryStates[d] = QString::number(d->revision());
    }
}
void MainWindow::batch() {
    QStringList files = QFileDialog::getOpenFileNames(this, "Toplu işlem için görüntüleri seç", {},
                                                      "Görüntüler (*.png *.jpg *.jpeg *.webp *.bmp)");
    if (files.isEmpty())
        return;
    QString dir = QFileDialog::getExistingDirectory(this, "Çıktı klasörünü seç");
    if (dir.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle("Toplu dönüştür");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *format = new QComboBox;
    format->addItems({"png", "jpg"});
    if (QImageWriter::supportedImageFormats().contains("webp"))
        format->addItem("webp");
    auto *edge = new QSpinBox;
    edge->setRange(0, 16384);
    edge->setValue(1920);
    edge->setSpecialValueText("Boyutu koru");
    auto *quality = new QSpinBox;
    quality->setRange(1, 100);
    quality->setValue(95);
    auto *b = new QSpinBox;
    auto *c = new QSpinBox;
    auto *s = new QSpinBox;
    for (auto *input : {b, c, s})
        input->setRange(-100, 100);
    form->addRow("Biçim", format);
    form->addRow("En uzun kenar (px)", edge);
    form->addRow("Kalite", quality);
    form->addRow("Parlaklık", b);
    form->addRow("Kontrast", c);
    form->addRow("Doygunluk", s);
    layout->addLayout(form);
    auto *note = new QLabel("Orijinaller korunur. Küçük görseller büyütülmez.\nÇıktılar _photodeck "
                            "ekiyle ve benzersiz adlarla yazılır.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QProgressDialog progress("Görüntüler işleniyor…", "Durdur", 0, files.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    int done = 0;
    QStringList errors;
    for (int i = 0; i < files.size(); ++i) {
        progress.setValue(i);
        QApplication::processEvents();
        if (progress.wasCanceled())
            break;
        Document d;
        QString e;
        if (!d.importImage(files[i], false, &e)) {
            errors << QFileInfo(files[i]).fileName() + ": " + e;
            continue;
        }
        if (edge->value() > 0 && qMax(d.state.size.width(), d.state.size.height()) > edge->value()) {
            QSize size = d.state.size;
            size.scale(edge->value(), edge->value(), Qt::KeepAspectRatio);
            if (!d.resize(size, &e)) {
                errors << e;
                continue;
            }
        }
        d.beginEdit();
        d.adjust(b->value(), c->value(), s->value());
        d.commit("Toplu renk ayarı");
        QString base = QDir(dir).filePath(QFileInfo(files[i]).completeBaseName() + "_photodeck"),
                out = base + "." + format->currentText();
        for (int n = 2; QFileInfo::exists(out); ++n)
            out = base + "_" + QString::number(n) + "." + format->currentText();
        if (d.exportImage(out, quality->value(), &e))
            ++done;
        else
            errors << QFileInfo(files[i]).fileName() + ": " + e;
    }
    progress.setValue(files.size());
    QMessageBox::information(this, "Toplu işlem",
                             QString("%1 / %2 görüntü kaydedildi.\n%3")
                                 .arg(done)
                                 .arg(files.size())
                                 .arg(errors.isEmpty() ? dir : errors.join("\n")));
}
void MainWindow::saveScreenshot(const QString &path) {
    grab().save(path);
}

namespace {
QString uiSettingsPath() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/ui.ini";
}
} // namespace
QDockWidget *MainWindow::createDock(const QString &title, const QString &name, QWidget *widget) {
    auto *dock = new QDockWidget(title, this);
    dock->setObjectName(name);
    dock->setWidget(widget);
    widget->setProperty("pdSurface", true);
    if (auto *scroll = qobject_cast<QScrollArea *>(widget))
        if (scroll->widget())
            scroll->widget()->setProperty("pdSurface", true);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                      QDockWidget::DockWidgetClosable);
    connect(dock, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea) { queueUiSave(); });
    connect(dock, &QDockWidget::topLevelChanged, this, [this](bool) { queueUiSave(); });
    connect(dock, &QDockWidget::visibilityChanged, this, [this](bool) {
        if (!applyingAppearance) {
            syncPanelActions();
            queueUiSave();
        }
    });
    return dock;
}
void MainWindow::buildAppearanceExtras() {
    floatingTools = new QFrame(workspace);
    floatingTools->setObjectName("floatingTools");
    auto *toolLayout = new QHBoxLayout(floatingTools);
    toolLayout->setContentsMargins(5, 5, 5, 5);
    toolLayout->setSpacing(3);
    toolsHandle = new QToolButton;
    toolsHandle->setText("⠿");
    toolsHandle->setToolTip("Araç panelini sürükleyin veya ok tuşlarıyla taşıyın.");
    toolsHandle->setAccessibleName("Araç panelini taşı");
    toolsHandle->setCursor(Qt::SizeAllCursor);
    toolsHandle->installEventFilter(this);
    toolLayout->addWidget(toolsHandle);
    floatingLayers = new QFrame(workspace);
    floatingLayers->setObjectName("floatingLayers");
    auto *layerLayout = new QVBoxLayout(floatingLayers);
    layerLayout->setContentsMargins(5, 5, 5, 5);
    layerLayout->setSpacing(1);
    layersHandle = new QToolButton;
    layersHandle->setText("⠿  Katmanlar");
    layersHandle->setToolTip("Katman panelini sürükleyin veya ok tuşlarıyla taşıyın.");
    layersHandle->setAccessibleName("Katman panelini taşı");
    layersHandle->setCursor(Qt::SizeAllCursor);
    layersHandle->installEventFilter(this);
    layerLayout->addWidget(layersHandle);
    floatingTools->hide();
    floatingLayers->hide();
    filmstrip = new QListWidget;
    filmstrip->setObjectName("filmstrip");
    filmstrip->setViewMode(QListView::IconMode);
    filmstrip->setFlow(QListView::LeftToRight);
    filmstrip->setWrapping(false);
    filmstrip->setIconSize({96, 64});
    filmstrip->setMinimumHeight(100);
    filmstrip->setMaximumHeight(130);
    filmstrip->setSpacing(4);
    connect(filmstrip, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < tabs->count())
            tabs->setCurrentIndex(row);
    });
    filmDock = createDock("Açık fotoğraflar", "filmDock", filmstrip);
    addDockWidget(Qt::BottomDockWidgetArea, filmDock);
    filmDock->hide();
    auto *brushes = new QWidget;
    auto *brushLayout = new QVBoxLayout(brushes);
    brushLayout->setContentsMargins(10, 10, 10, 10);
    auto *presets = new QWidget;
    auto *presetLayout = new QHBoxLayout(presets);
    presetLayout->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        auto *button = new QPushButton(QStringList{"Yuvarlak", "Yumuşak", "Kalem"}[i]);
        connect(button, &QPushButton::clicked, this, [this, i] {
            hardnessInput->setValue(i == 1 ? 15 : 100);
            actions[i == 2 ? "tool4" : "tool3"]->trigger();
        });
        presetLayout->addWidget(button);
    }
    brushLayout->addWidget(presets);
    auto *colors = new QWidget;
    auto *colorsLayout = new QHBoxLayout(colors);
    colorsLayout->setContentsMargins(0, 0, 0, 0);
    for (const auto &color :
         QStringList{"#e8d4b3", "#81c2b3", "#536caa", "#d390a0", "#c7b5e3", "#dbe5e0", "#2b414e"}) {
        auto *button = new QToolButton;
        button->setFixedSize(27, 27);
        button->setStyleSheet("background:" + color + ";border-radius:12px;border:1px solid #687681;");
        button->setAccessibleName("Renk " + color);
        button->setToolTip(color);
        connect(button, &QToolButton::clicked, this, [this, color] {
            if (currentCanvas()) {
                currentCanvas()->foreground = QColor(color);
                refresh();
            }
        });
        colorsLayout->addWidget(button);
    }
    brushLayout->addWidget(colors);
    brushLayout->addStretch();
    brushDock = createDock("Fırça uçları ve palet", "brushDock", brushes);
    addDockWidget(Qt::RightDockWidgetArea, brushDock);
    brushDock->hide();
    ribbonToolbar = new QToolBar("Komut şeridi", this);
    ribbonToolbar->setObjectName("ribbonToolbar");
    ribbonToolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    addToolBar(Qt::TopToolBarArea, ribbonToolbar);
    for (int i : {0, 1, 2, 3, 5, 6, 9, 10}) {
        if (i == 3 || i == 9)
            ribbonToolbar->addSeparator();
        ribbonToolbar->addAction(actions["tool" + QString::number(i)]);
    }
    ribbonToolbar->addSeparator();
    ribbonToolbar->addAction(actions["resize"]);
    ribbonToolbar->hide();
    auto *view = menuBar()->addMenu("&Çalışma alanı");
    presetMenu = view->addMenu("Arayüz presetleri");
    auto *group = new QActionGroup(this);
    group->setExclusive(true);
    int index = 0;
    for (const auto &p : uiPresets()) {
        auto *a = action("ui.preset." + p.id, QString::number(++index) + " · " + p.name, {},
                         [this, id = p.id] { applyUiPreset(id); });
        a->setCheckable(true);
        a->setToolTip(p.description);
        group->addAction(a);
        presetMenu->addAction(a);
    }
    userPresetMenu = presetMenu->addMenu("Kendi presetlerim");
    view->addAction(action("ui.customize", "Görünümü özelleştir…", QKeySequence("Ctrl+Alt+P"),
                           [this] { customizeUi(); }));
    view->addAction(
        action("ui.savePreset", "Kendi presetim olarak kaydet…", {}, [this] { saveUserPreset(); }));
    view->addAction(action("ui.exportPreset", "Preset dışa aktar…", {}, [this] {
        QString file =
            QFileDialog::getSaveFileName(this, "Arayüz presetini dışa aktar", "PhotoDeck-gorunum.pdui",
                                         "PhotoDeck görünümü (*.pdui)");
        if (file.isEmpty())
            return;
        if (!file.endsWith(".pdui", Qt::CaseInsensitive))
            file += ".pdui";
        QString e;
        if (!exportUiPreset(file, &e))
            error(e);
    }));
    view->addAction(action("ui.importPreset", "Preset içe aktar…", {}, [this] {
        QString file = QFileDialog::getOpenFileName(this, "Arayüz presetini içe aktar", {},
                                                    "PhotoDeck görünümü (*.pdui *.json)");
        if (file.isEmpty())
            return;
        QString e;
        if (!importUiPreset(file, &e))
            error(e);
    }));
    view->addAction(
        action("ui.reset", "Varsayılan Odak düzenine dön", {}, [this] { applyUiPreset("focus"); }));
    auto *panels = view->addMenu("Paneller");
    for (const auto &entry : QVector<QPair<QString, QString>>{{"tools", "Araçlar"},
                                                              {"layers", "Katmanlar"},
                                                              {"color", "Renk & ton"},
                                                              {"history", "Geçmiş"},
                                                              {"film", "Fotoğraf şeridi"},
                                                              {"brush", "Fırça paleti"}}) {
        auto *a = action("ui.panel." + entry.first, entry.second, {}, [] {});
        a->setCheckable(true);
        panels->addAction(a);
        connect(a, &QAction::toggled, this, [this, key = entry.first](bool visible) {
            if (applyingAppearance)
                return;
            QWidget *target = nullptr;
            if (key == "tools")
                target =
                    appearance.preset == "focus" ? static_cast<QWidget *>(floatingTools) : toolsDock;
            else if (key == "layers")
                target =
                    appearance.preset == "focus" ? static_cast<QWidget *>(floatingLayers) : layersDock;
            else if (key == "color")
                target = colorDock;
            else if (key == "history")
                target = historyDock;
            else if (key == "film")
                target = filmDock;
            else
                target = brushDock;
            target->setVisible(visible);
            if (key == "film")
                refresh();
            queueUiSave();
        });
    }
    presetPicker = new QComboBox;
    presetPicker->setObjectName("presetPicker");
    presetPicker->setAccessibleName("Arayüz preseti");
    for (const auto &p : uiPresets())
        presetPicker->addItem(p.name, p.id);
    presetPicker->setMinimumContentsLength(10);
    presetPicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    documentToolbar->addSeparator();
    documentToolbar->addWidget(presetPicker);
    connect(presetPicker, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!applyingAppearance && index >= 0)
            applyUiPreset(presetPicker->itemData(index).toString());
    });
    documentToolbar->addAction(actions["ui.customize"]);
    for (auto *toolbar : {documentToolbar, optionsToolbar, ribbonToolbar}) {
        connect(toolbar, &QToolBar::visibilityChanged, this, [this](bool) { queueUiSave(); });
        connect(toolbar, &QToolBar::topLevelChanged, this, [this](bool) { queueUiSave(); });
    }
}
UiSettings MainWindow::captureUiSettings() const {
    UiSettings s = appearance;
    s.dockState = saveState(2);
    if (appearance.preset == "focus") {
        s.focusToolsVisible = !floatingTools->isHidden();
        s.focusLayersVisible = !floatingLayers->isHidden();
    }
    return s;
}
void MainWindow::queueUiSave() {
    if (persistAppearance && !applyingAppearance)
        uiSaveTimer.start();
}
void MainWindow::saveUiSettings() {
    if (!persistAppearance || applyingAppearance)
        return;
    QSettings settings(uiSettingsPath(), QSettings::IniFormat);
    settings.setValue("active",
                      QJsonDocument(captureUiSettings().toJson()).toJson(QJsonDocument::Compact));
    settings.setValue("geometry", saveGeometry());
    QJsonArray profiles;
    for (const auto &p : userPresets)
        profiles.append(QJsonObject{{"name", p.first}, {"settings", p.second.toJson()}});
    settings.setValue("userPresets", QJsonDocument(profiles).toJson(QJsonDocument::Compact));
    settings.sync();
    if (settings.status() != QSettings::NoError)
        status->setText("Görünüm ayarları kaydedilemedi.");
}
void MainWindow::loadUiSettings() {
    QSettings settings(uiSettingsPath(), QSettings::IniFormat);
    QByteArray data = settings.value("active").toByteArray();
    UiSettings s = UiSettings::forPreset("focus");
    QString e;
    if (data.size() < 131072) {
        auto doc = QJsonDocument::fromJson(data);
        if (!doc.isNull())
            UiSettings::fromJson(doc.object(), &s, &e);
    }
    QByteArray users = settings.value("userPresets").toByteArray();
    if (users.size() < 1048576) {
        auto array = QJsonDocument::fromJson(users).array();
        for (const auto &entry : array) {
            if (userPresets.size() >= 64)
                break;
            auto obj = entry.toObject();
            QString name = obj["name"].toString();
            UiSettings custom;
            if (!name.trimmed().isEmpty() && name.size() <= 80 &&
                UiSettings::fromJson(obj["settings"].toObject(), &custom, nullptr))
                userPresets.append({name, custom});
        }
    }
    applyUiSettings(s, true, false);
    QByteArray geometry = settings.value("geometry").toByteArray();
    if (geometry.size() < 65536 && !geometry.isEmpty())
        restoreGeometry(geometry);
    rebuildUserPresets();
    if (!e.isEmpty())
        status->setText("Geçersiz görünüm ayarı; Odak düzeni yüklendi.");
}
void MainWindow::applyUiPreset(const QString &id, bool persist) {
    if (!validUiPreset(id))
        return;
    applyUiSettings(UiSettings::forPreset(id), true, persist);
}
void MainWindow::applyUiSettings(const UiSettings &settings, bool resetLayout, bool persist) {
    UiSettings validated;
    if (!UiSettings::fromJson(settings.toJson(), &validated, nullptr))
        return;
    if (currentCanvas())
        currentCanvas()->finishPendingGesture();
    applyingAppearance = true;
    appearance = validated;
    if (resetLayout)
        arrangePreset();
    applyAppearanceColors();
    if (resetLayout && !appearance.dockState.isEmpty() && !restoreState(appearance.dockState, 2)) {
        appearance.dockState.clear();
        arrangePreset();
    }
    menuBar()->setVisible(appearance.showMenu);
    statusBar()->setVisible(appearance.showStatus);
    for (const auto &p : uiPresets())
        actions["ui.preset." + p.id]->setChecked(p.id == appearance.preset);
    presetPicker->setCurrentIndex(presetPicker->findData(appearance.preset));
    positionOverlays();
    if (appearance.preset == "focus") {
        floatingTools->setVisible(appearance.focusToolsVisible);
        floatingLayers->setVisible(appearance.focusLayersVisible);
    }
    applyingAppearance = false;
    syncPanelActions();
    refresh();
    if (persist)
        saveUiSettings();
}
void MainWindow::applyAppearanceColors() {
    QFont uiFont(appearance.fontFamily);
    uiFont.setPixelSize(appearance.fontSize);
    setFont(uiFont);
    setPalette(appearance.palette());
    setStyleSheet(appearance.styleSheet());
    auto p = appearance.resolved();
    histogram->setUiPalette(appearance.palette());
    brandLabel->setText("  <b>Photo<span style='color:" + p.accent.name() + "'>Deck</span></b>  ");
    for (int i = 0; i < 14; ++i)
        actions["tool" + QString::number(i)]->setIcon(toolIcon(i, p.text));
    int layerIconIndex = 0;
    for (const auto &id :
         QStringList{"addLayer", "duplicate", "layerUp", "layerDown", "lock", "deleteLayer"}) {
        QPixmap pixmap(24, 24);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setPen(p.text);
        QFont font("Sans Serif");
        font.setPixelSize(20);
        painter.setFont(font);
        painter.drawText(pixmap.rect(), Qt::AlignCenter,
                         QStringList{"+", "▣", "↑", "↓", "◇", "−"}[layerIconIndex++]);
        painter.end();
        actions[id]->setIcon(QIcon(pixmap));
    }
    toolsBar->setIconSize({appearance.iconSize, appearance.iconSize});
    toolsBar->setToolButtonStyle(appearance.toolLabels ? Qt::ToolButtonTextBesideIcon
                                                       : Qt::ToolButtonIconOnly);
    ribbonToolbar->setIconSize({appearance.iconSize, appearance.iconSize});
    QDockWidget::DockWidgetFeatures features = QDockWidget::DockWidgetClosable;
    if (appearance.movablePanels)
        features |= QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable;
    for (auto *dock : {toolsDock, colorDock, layersDock, historyDock, filmDock, brushDock})
        dock->setFeatures(features);
    toolsHandle->setEnabled(appearance.movablePanels);
    layersHandle->setEnabled(appearance.movablePanels);
    for (int i = 0; i < tabs->count(); ++i) {
        auto *canvas = qobject_cast<Canvas *>(tabs->widget(i));
        canvas->setUiColors(p.canvas, p.panel, p.muted, p.accent);
        canvas->showRulers = appearance.rulers;
        canvas->update();
    }
    if (!layersDock->isFloating() && appearance.preset != "focus")
        resizeDocks({layersDock, colorDock, historyDock},
                    {appearance.panelWidth, appearance.panelWidth, appearance.panelWidth},
                    Qt::Horizontal);
}
void MainWindow::arrangePreset() {
    floatingTools->hide();
    floatingLayers->hide();
    toolsBar->setParent(toolsDock);
    toolsDock->setWidget(toolsBar);
    layerPanel->setParent(layersDock);
    layersDock->setWidget(layerPanel);
    for (auto *dock : {toolsDock, colorDock, layersDock, historyDock, filmDock, brushDock}) {
        dock->setFloating(false);
        removeDockWidget(dock);
        dock->hide();
    }
    for (auto *toolbar : {documentToolbar, optionsToolbar, ribbonToolbar})
        removeToolBar(toolbar);
    addToolBar(Qt::TopToolBarArea, documentToolbar);
    documentToolbar->show();
    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(Qt::TopToolBarArea, optionsToolbar);
    optionsToolbar->show();
    ribbonToolbar->hide();
    auto side = appearance.panelSide == "left"     ? Qt::LeftDockWidgetArea
                : appearance.panelSide == "right"  ? Qt::RightDockWidgetArea
                : appearance.preset == "workbench" ? Qt::LeftDockWidgetArea
                                                   : Qt::RightDockWidgetArea;
    auto toolArea = appearance.preset == "workbench" ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea;
    toolsBar->setOrientation(Qt::Vertical);
    addDockWidget(toolArea, toolsDock);
    addDockWidget(side, colorDock);
    addDockWidget(side, layersDock);
    splitDockWidget(colorDock, layersDock, Qt::Vertical);
    addDockWidget(side, historyDock);
    splitDockWidget(layersDock, historyDock, Qt::Vertical);
    addDockWidget(Qt::BottomDockWidgetArea, filmDock);
    addDockWidget(side, brushDock);
    toolsDock->show();
    colorDock->show();
    layersDock->show();
    historyDock->hide();
    brushDock->hide();
    filmDock->hide();
    layerDetails->setVisible(appearance.preset != "focus");
    if (appearance.preset == "focus") {
        toolsDock->setWidget(nullptr);
        layersDock->setWidget(nullptr);
        toolsDock->hide();
        layersDock->hide();
        colorDock->hide();
        optionsToolbar->hide();
        toolsBar->setOrientation(Qt::Horizontal);
        toolsBar->setParent(floatingTools);
        floatingTools->layout()->addWidget(toolsBar);
        layerPanel->setParent(floatingLayers);
        floatingLayers->layout()->addWidget(layerPanel);
        toolsBar->show();
        layerPanel->show();
        floatingTools->show();
        floatingLayers->show();
    } else if (appearance.preset == "film") {
        filmDock->show();
    } else if (appearance.preset == "ribbon") {
        toolsDock->hide();
        optionsToolbar->hide();
        addToolBar(Qt::TopToolBarArea, ribbonToolbar);
        ribbonToolbar->show();
    } else if (appearance.preset == "ink") {
        colorDock->hide();
        brushDock->show();
        splitDockWidget(brushDock, layersDock, Qt::Vertical);
    } else if (appearance.preset == "compact") {
        addDockWidget(Qt::TopDockWidgetArea, toolsDock);
        toolsBar->setOrientation(Qt::Horizontal);
        optionsToolbar->hide();
    } else if (appearance.preset == "touch") {
        addDockWidget(Qt::BottomDockWidgetArea, toolsDock);
        toolsBar->setOrientation(Qt::Horizontal);
        optionsToolbar->hide();
        colorDock->hide();
    } else if (appearance.preset == "modular") {
        historyDock->show();
    }
    if (appearance.preset != "focus") {
        resizeDocks({layersDock, colorDock}, {400, 250}, Qt::Vertical);
        resizeDocks({layersDock, colorDock, historyDock},
                    {appearance.panelWidth, appearance.panelWidth, appearance.panelWidth},
                    Qt::Horizontal);
    }
}
void MainWindow::positionOverlays() {
    if (!floatingTools || appearance.preset != "focus")
        return;
    int w = workspace->width(), h = workspace->height();
    if (w < 50 || h < 50)
        return;
    QSize toolsSize(
        qMin(w - 24, qMax(340, toolsBar->sizeHint().width() + toolsHandle->sizeHint().width() + 24)),
        toolsBar->sizeHint().height() + 16);
    QSize layerSize(qMin(appearance.panelWidth, w - 24), qMin(qMax(260, h - 130), 420));
    auto place = [&](QFrame *panel, QSize size, QPointF normalized, QPoint fallback) {
        int maxX = qMax(0, w - size.width()), maxY = qMax(0, h - size.height());
        QPoint at = normalized.x() < 0 || normalized.y() < 0
                        ? fallback
                        : QPoint(qRound(normalized.x() * maxX), qRound(normalized.y() * maxY));
        panel->setGeometry(qBound(0, at.x(), maxX), qBound(0, at.y(), maxY), size.width(),
                           size.height());
        panel->raise();
    };
    place(floatingTools, toolsSize, appearance.toolsPosition,
          {(w - toolsSize.width()) / 2, h - toolsSize.height() - 18});
    place(floatingLayers, layerSize, appearance.layersPosition,
          {appearance.panelSide == "left" ? 18 : w - layerSize.width() - 18, 50});
}
bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    if (object == workspace && event->type() == QEvent::Resize) {
        positionOverlays();
        return false;
    }
    if (object != toolsHandle && object != layersHandle)
        return QMainWindow::eventFilter(object, event);
    if (!appearance.movablePanels)
        return false;
    auto *panel = object == toolsHandle ? floatingTools : floatingLayers;
    auto storePosition = [&] {
        QPointF p(qreal(panel->x()) / qMax(1, workspace->width() - panel->width()),
                  qreal(panel->y()) / qMax(1, workspace->height() - panel->height()));
        if (object == toolsHandle)
            appearance.toolsPosition = p;
        else
            appearance.layersPosition = p;
        queueUiSave();
    };
    auto move = [&](QPoint pos) {
        panel->move(qBound(0, pos.x(), qMax(0, workspace->width() - panel->width())),
                    qBound(0, pos.y(), qMax(0, workspace->height() - panel->height())));
        storePosition();
    };
    if (event->type() == QEvent::MouseButtonPress) {
        auto *e = static_cast<QMouseEvent *>(event);
        if (e->button() == Qt::LeftButton) {
            panelDragStart = e->globalPosition().toPoint();
            panelStart = panel->pos();
            return true;
        }
    }
    if (event->type() == QEvent::MouseMove) {
        auto *e = static_cast<QMouseEvent *>(event);
        if (e->buttons() & Qt::LeftButton) {
            move(panelStart + e->globalPosition().toPoint() - panelDragStart);
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto *e = static_cast<QKeyEvent *>(event);
        QPoint delta;
        if (e->key() == Qt::Key_Left)
            delta = {-10, 0};
        else if (e->key() == Qt::Key_Right)
            delta = {10, 0};
        else if (e->key() == Qt::Key_Up)
            delta = {0, -10};
        else if (e->key() == Qt::Key_Down)
            delta = {0, 10};
        else
            return false;
        move(panel->pos() + delta);
        return true;
    }
    return false;
}
void MainWindow::syncPanelActions() {
    if (!actions.contains("ui.panel.tools"))
        return;
    for (const auto &entry : QVector<QPair<QString, QWidget *>>{
             {"tools", appearance.preset == "focus" ? static_cast<QWidget *>(floatingTools) : toolsDock},
             {"layers",
              appearance.preset == "focus" ? static_cast<QWidget *>(floatingLayers) : layersDock},
             {"color", colorDock},
             {"history", historyDock},
             {"film", filmDock},
             {"brush", brushDock}}) {
        QSignalBlocker block(actions["ui.panel." + entry.first]);
        actions["ui.panel." + entry.first]->setChecked(!entry.second->isHidden());
    }
}
void MainWindow::rebuildUserPresets() {
    userPresetMenu->clear();
    for (const auto &profile : userPresets) {
        auto *a = userPresetMenu->addAction(profile.first);
        connect(a, &QAction::triggered, this, [this, s = profile.second] { applyUiSettings(s); });
    }
    userPresetMenu->setEnabled(!userPresets.isEmpty());
}
void MainWindow::saveUserPreset() {
    bool ok;
    QString name = QInputDialog::getText(this, "Kendi presetim", "Preset adı", QLineEdit::Normal,
                                         uiPreset(appearance.preset).name + " · kişisel", &ok)
                       .trimmed();
    if (!ok || name.isEmpty())
        return;
    if (name.size() > 80) {
        error("Preset adı en fazla 80 karakter olabilir.");
        return;
    }
    for (auto &p : userPresets)
        if (p.first == name) {
            p.second = captureUiSettings();
            rebuildUserPresets();
            saveUiSettings();
            return;
        }
    if (userPresets.size() >= 64) {
        error("En fazla 64 kişisel preset kaydedilebilir.");
        return;
    }
    userPresets.append({name, captureUiSettings()});
    rebuildUserPresets();
    saveUiSettings();
}
bool MainWindow::exportUiPreset(const QString &path, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QByteArray json = QJsonDocument(captureUiSettings().toJson()).toJson();
    if (file.write(json) != json.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
bool MainWindow::importUiPreset(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.size() > 131072) {
        if (error)
            *error = "Preset dosyası 128 KiB sınırını aşıyor.";
        return false;
    }
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    UiSettings s;
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = "Geçersiz JSON preset dosyası.";
        return false;
    }
    if (!UiSettings::fromJson(doc.object(), &s, error))
        return false;
    applyUiSettings(s);
    return true;
}
void MainWindow::customizeUi() {
    UiSettings original = captureUiSettings(), pending = original;
    QDialog dialog(this);
    dialog.setWindowTitle("Görünümü özelleştir");
    dialog.setObjectName("appearanceDialog");
    dialog.resize(490, qMin(720, QApplication::primaryScreen()->availableGeometry().height() - 100));
    auto *outerLayout = new QVBoxLayout(&dialog);
    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body);
    outerLayout->addWidget(scroll, 1);
    auto *form = new QFormLayout;
    auto *preset = new QComboBox;
    for (const auto &p : uiPresets())
        preset->addItem(p.name, p.id);
    preset->setObjectName("customPreset");
    auto *theme = new QComboBox;
    theme->addItems({"Presetin görünümü", "Açık", "Koyu"});
    auto *side = new QComboBox;
    side->addItems({"Presetin yerleşimi", "Sol", "Sağ"});
    auto *fontFamily = new QFontComboBox;
    auto *fontSize = new QSpinBox;
    fontSize->setRange(10, 20);
    fontSize->setObjectName("uiFontSize");
    auto *iconSize = new QSpinBox;
    iconSize->setRange(16, 36);
    auto *radius = new QSpinBox;
    radius->setRange(0, 16);
    auto *width = new QSpinBox;
    width->setRange(220, 440);
    auto *accent = new QPushButton("Vurgu rengi seç…");
    auto *canvas = new QPushButton("Tuval çevresi rengi seç…");
    auto *panelColor = new QPushButton("Panel rengi seç…");
    auto *backgroundColor = new QPushButton("Uygulama zemini rengi seç…");
    form->addRow("Temel preset", preset);
    form->addRow("Görünüm", theme);
    form->addRow("Panel tarafı", side);
    form->addRow("Yazı tipi", fontFamily);
    form->addRow("Yazı boyutu (px)", fontSize);
    form->addRow("Simge boyutu (px)", iconSize);
    form->addRow("Köşe yumuşaklığı", radius);
    form->addRow("Panel genişliği (px)", width);
    form->addRow("Renk", accent);
    form->addRow("Çalışma alanı", canvas);
    form->addRow("Panel yüzeyleri", panelColor);
    form->addRow("Zemin", backgroundColor);
    layout->addLayout(form);
    QVector<QPair<QCheckBox *, bool UiSettings::*>> flags;
    for (const auto &entry : QVector<QPair<QString, bool UiSettings::*>>{
             {"Araç adlarını göster", &UiSettings::toolLabels},
             {"Panelleri taşıma ve ayırmaya izin ver", &UiSettings::movablePanels},
             {"Menü çubuğunu göster", &UiSettings::showMenu},
             {"Durum çubuğunu göster", &UiSettings::showStatus},
             {"Cetvelleri göster", &UiSettings::rulers}}) {
        auto *box = new QCheckBox(entry.first);
        layout->addWidget(box);
        flags.append({box, entry.second});
    }
    auto *note = new QLabel("Değişiklikler canlı önizlenir. Panelleri başlıklarından taşıyabilirsiniz. "
                            "Ctrl+Alt+P bu pencereyi tekrar açar.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                         QDialogButtonBox::RestoreDefaults);
    buttons->button(QDialogButtonBox::Ok)->setText("Uygula");
    outerLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    bool filling = false;
    auto fill = [&] {
        filling = true;
        preset->setCurrentIndex(preset->findData(pending.preset));
        theme->setCurrentIndex(QStringList{"preset", "light", "dark"}.indexOf(pending.theme));
        side->setCurrentIndex(QStringList{"preset", "left", "right"}.indexOf(pending.panelSide));
        fontFamily->setCurrentFont(QFont(pending.fontFamily));
        fontSize->setValue(pending.fontSize);
        iconSize->setValue(pending.iconSize);
        radius->setValue(pending.radius);
        width->setValue(pending.panelWidth);
        for (const auto &f : flags)
            f.first->setChecked(pending.*(f.second));
        filling = false;
    };
    auto preview = [&] {
        if (filling)
            return;
        bool relocate =
            pending.panelSide != QStringList{"preset", "left", "right"}[side->currentIndex()];
        pending.theme = QStringList{"preset", "light", "dark"}[theme->currentIndex()];
        pending.panelSide = QStringList{"preset", "left", "right"}[side->currentIndex()];
        pending.fontFamily = fontFamily->currentFont().family();
        pending.fontSize = fontSize->value();
        pending.iconSize = iconSize->value();
        pending.radius = radius->value();
        pending.panelWidth = width->value();
        for (const auto &f : flags)
            pending.*(f.second) = f.first->isChecked();
        if (relocate)
            pending.dockState.clear();
        applyUiSettings(pending, relocate, false);
        uiSaveTimer.stop();
    };
    fill();
    connect(preset, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
        if (filling)
            return;
        pending = UiSettings::forPreset(preset->itemData(index).toString());
        fill();
        applyUiSettings(pending, true, false);
        uiSaveTimer.stop();
    });
    connect(fontFamily, &QFontComboBox::currentFontChanged, &dialog, [&](const QFont &) { preview(); });
    for (auto *spin : {fontSize, iconSize, radius, width})
        connect(spin, &QSpinBox::valueChanged, &dialog, [&](int) { preview(); });
    for (auto *combo : {theme, side})
        connect(combo, &QComboBox::currentIndexChanged, &dialog, [&](int) { preview(); });
    for (const auto &f : flags)
        connect(f.first, &QCheckBox::toggled, &dialog, [&](bool) { preview(); });
    connect(accent, &QPushButton::clicked, &dialog, [&] {
        QColor c = QColorDialog::getColor(pending.resolved().accent, &dialog, "Vurgu rengi");
        if (c.isValid()) {
            pending.accent = c;
            preview();
        }
    });
    connect(canvas, &QPushButton::clicked, &dialog, [&] {
        QColor c = QColorDialog::getColor(pending.resolved().canvas, &dialog, "Tuval çevresi");
        if (c.isValid()) {
            pending.canvas = c;
            preview();
        }
    });
    connect(panelColor, &QPushButton::clicked, &dialog, [&] {
        QColor c = QColorDialog::getColor(pending.resolved().panel, &dialog, "Panel yüzeyleri");
        if (c.isValid()) {
            pending.panel = c;
            preview();
        }
    });
    connect(backgroundColor, &QPushButton::clicked, &dialog, [&] {
        QColor c = QColorDialog::getColor(pending.resolved().background, &dialog, "Uygulama zemini");
        if (c.isValid()) {
            pending.background = c;
            preview();
        }
    });
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&] {
        pending = UiSettings::forPreset(pending.preset);
        fill();
        applyUiSettings(pending, true, false);
        uiSaveTimer.stop();
    });
    uiSaveTimer.stop();
    if (dialog.exec() == QDialog::Accepted) {
        preview();
        saveUiSettings();
    } else {
        applyUiSettings(original, true, false);
        uiSaveTimer.stop();
    }
}
