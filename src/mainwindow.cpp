#include "mainwindow.h"
#include "recoverymanager.h"
#include "recoveryfiles.h"
#include "version.h"
#include "blending.h"
#include "effectdialog.h"
#include "outputdialog.h"
#include "tonedialog.h"
#include "transformdialog.h"
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragMoveEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLockFile>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPainterPath>
#include <QPrintDialog>
#include <QPrinter>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopeGuard>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTabBar>
#include <QToolBar>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
QString uiSettingsPath();
// These editor dialogs use action verbs even when the system Qt translations are absent.
// Retain the standard button roles so Enter, Escape and existing modal drivers still work.
void labelEditorDialogButtons(QDialogButtonBox *buttons, const QString &acceptText) {
    auto *accept = buttons->button(QDialogButtonBox::Ok);
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    accept->setText(acceptText);
    accept->setAccessibleName(QString(acceptText).remove('&'));
    cancel->setText("İ&ptal");
    cancel->setAccessibleName("İptal");
}
void labelEditorDialogButtons(QInputDialog &dialog, const QString &acceptText) {
    // QInputDialog creates its standard button box lazily.
    dialog.setOkButtonText(acceptText);
    labelEditorDialogButtons(dialog.findChild<QDialogButtonBox *>(), acceptText);
}
// Keep the editable name separate from the row's status text. A rename has no live
// document preview: only a valid, current editor can commit a single history entry.
enum LayerItemRole {
    LayerNameRole = Qt::UserRole, LayerStatusRole, LayerDocumentRole, LayerRevisionRole, LayerCountRole, LayerStackRole
};
struct LayerRenameTarget {
    QPointer<Document> document;
    int index = -1, count = 0;
    quint64 revision = 0;
    QString name;
};
class LayerNameEditor : public QLineEdit {
  public:
    explicit LayerNameEditor(QWidget *parent) : QLineEdit(parent) {
        setObjectName("layerNameEditor");
        setAccessibleName("Katman adı");
        setMaxLength(128);
    }
    LayerRenameTarget target;
    QString initialText;
    bool initialized = false, cancelled = false;
};
class LayerNameDelegate : public QStyledItemDelegate {
  public:
    explicit LayerNameDelegate(QObject *parent) : QStyledItemDelegate(parent) {
        connect(this, &QAbstractItemDelegate::closeEditor, this, [this](QWidget *closed) {
            if (auto *line = liveEditor(); line && line == closed)
                line->cancelled = true;
        });
    }
    std::function<LayerRenameTarget(const QModelIndex &)> targetForIndex;
    std::function<void(const LayerRenameTarget &, const QString &)> rename;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        QStyleOptionViewItem row(option);
        initStyleOption(&row, index);
        const auto *style = row.widget ? row.widget->style() : QApplication::style();
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &row, row.widget);
        row.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &row, painter, row.widget);
        QFont nameFont = row.font;
        // At Compact's 11 px size, thin hinted strokes can fade into the
        // selection tint on some system fonts. Keep the editable name distinct.
        if (nameFont.pixelSize() > 0 && nameFont.pixelSize() <= 11)
            nameFont.setWeight(QFont::DemiBold);
        QFont small = row.font;
        if (small.pixelSize() > 0)
            small.setPixelSize(qMax(9, small.pixelSize() - 1));
        else
            small.setPointSizeF(qMax(7.0, small.pointSizeF() - 1));
        const QFontMetrics mainMetrics(nameFont), smallMetrics(small);
        const int height = mainMetrics.height() + smallMetrics.height() + 2;
        QRect nameRect = textRect;
        nameRect.setTop(textRect.center().y() - height / 2);
        nameRect.setHeight(mainMetrics.height());
        QRect detailRect = nameRect.translated(0, mainMetrics.height() + 2);
        detailRect.setHeight(smallMetrics.height());
        // The stylesheet uses a tinted panel for selection, not the palette's
        // solid accent highlight. WindowText stays readable in both light/dark UIs.
        const QColor foreground = row.palette.color(QPalette::WindowText);
        painter->save();
        painter->setClipRect(option.rect);
        painter->setFont(nameFont);
        painter->setPen(foreground);
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                          mainMetrics.elidedText(index.data(LayerNameRole).toString(),
                                                 Qt::ElideRight, nameRect.width()));
        painter->setPen(row.palette.color(QPalette::PlaceholderText));
        painter->setFont(small);
        painter->drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter,
                          smallMetrics.elidedText(index.data(LayerStatusRole).toString(),
                                                  Qt::ElideRight, detailRect.width()));
        painter->restore();
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &index) const override {
        auto *line = new LayerNameEditor(parent);
        line->target = targetForIndex(index);
        editor = line;
        return line;
    }
    void setEditorData(QWidget *widget, const QModelIndex &) const override {
        auto *line = dynamic_cast<LayerNameEditor *>(widget);
        if (!line)
            return;
        // A scheduled thumbnail/status refresh must not overwrite text being typed.
        if (!line->initialized) {
            line->setText(line->target.name);
            line->initialText = line->text();
            line->selectAll();
            line->initialized = true;
        }
    }
    void setModelData(QWidget *widget, QAbstractItemModel *, const QModelIndex &) const override {
        auto *line = dynamic_cast<LayerNameEditor *>(widget);
        if (!line)
            return;
        const auto apply = rename;
        const auto target = line->target;
        const QString text = line->text();
        const bool changed = !line->cancelled && text != line->initialText;
        line->cancelled = true;
        if (changed && apply)
            apply(target, text);
    }
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == editor && !liveEditor())
            return false; // QWidget teardown must not request a derived-editor commit.
        if (event->type() == QEvent::FocusOut && watched == editor && isRenaming() &&
            static_cast<QFocusEvent *>(event)->reason() == Qt::MouseFocusReason) {
            const auto *view = qobject_cast<const QListWidget *>(parent());
            const auto *focus = QApplication::focusWidget();
            if (view && focus && (focus == view || view->isAncestorOf(focus))) {
                // QApplication assigns mouse focus before dispatching the press.
                // Keep this name pending until the list's guarded settlement; a
                // scrollbar focus path that has no row press gets a safe fallback.
                const QPointer<LayerNameDelegate> alive = this;
                const QPointer<QWidget> line = liveEditor();
                QTimer::singleShot(0, this, [alive, line] {
                    if (alive && line && alive->editor == line && !line->hasFocus())
                        alive->finishRename();
                });
                return false;
            }
        }
        return QStyledItemDelegate::eventFilter(watched, event);
    }
    bool isCheckIndicator(const QModelIndex &index, const QRect &rect, QPoint point) const {
        const auto *view = qobject_cast<const QListWidget *>(parent());
        QStyleOptionViewItem option;
        if (view) {
            option.initFrom(view);
            option.widget = view;
            option.decorationSize = view->iconSize();
        }
        initStyleOption(&option, index);
        option.rect = rect;
        const auto *style = view ? view->style() : QApplication::style();
        return style->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, view).contains(point);
    }
    bool isRenaming() const {
        const auto *line = liveEditor();
        return line && !line->cancelled;
    }
    QString pendingName() const {
        const auto *line = liveEditor();
        if (!line || line->cancelled || line->text() == line->initialText)
            return {};
        const QString name = line->text().trimmed().left(128);
        return name == line->target.name ? QString() : name;
    }
    void finishRename() {
        if (!isRenaming())
            return;
        auto *line = liveEditor();
        const QPointer<QWidget> survivingLine = line;
        const QPointer<LayerNameDelegate> alive = this;
        const auto apply = rename;
        const auto target = line->target;
        const QString text = line->text(), initial = line->initialText;
        line->cancelled = true;
        // Explicit command settlement does not need the view's commitData frame:
        // its editor bookkeeping cannot survive arbitrary synchronous deletion.
        if (apply && text != initial)
            apply(target, text);
        if (alive && survivingLine)
            emit alive->closeEditor(survivingLine, QAbstractItemDelegate::NoHint);
    }
    void cancelIfStale(Document *document) {
        if (!isRenaming())
            return;
        auto *line = liveEditor();
        const auto &target = line->target;
        if (target.document != document || !document || document->revision() != target.revision ||
            document->state.layers.size() != target.count || target.index < 0 ||
            target.index >= document->state.layers.size() ||
            document->state.layers[target.index].name != target.name) {
            line->cancelled = true;
            emit closeEditor(line, QAbstractItemDelegate::RevertModelCache);
        }
    }

  private:
    // QWidget sends teardown events before QObject clears QPointer. Keep the
    // guard at QObject level and test the live dynamic type before editor access.
    LayerNameEditor *liveEditor() const { return dynamic_cast<LayerNameEditor *>(editor.data()); }
    mutable QPointer<QObject> editor;
};
QIcon toolIcon(int kind, QColor color = QColor("#d5dee5")) {
    QIcon icon;
    for (int edge : {24, 32, 48, 64, 96}) {
        QPixmap pm(edge, edge);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.scale(edge / 28.0, edge / 28.0);
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
        case Canvas::EllipseSelect:
            p.setPen(QPen(color, 1.65, Qt::DashLine));
            p.drawEllipse(5, 5, 18, 18);
            break;
        case Canvas::Lasso:
            p.drawEllipse(4, 5, 18, 14);
            p.drawPolyline(QPolygonF{{12, 17}, {9, 23}, {14, 24}});
            break;
        case Canvas::PolygonLasso:
            p.setPen(QPen(color, 1.65, Qt::DashLine));
            p.drawPolygon(QPolygonF{{5, 22}, {7, 7}, {21, 4}, {24, 20}});
            break;
        case Canvas::Wand:
            p.drawLine(5, 23, 19, 9);
            p.drawLine(20, 2, 20, 6);
            p.drawLine(24, 6, 27, 6);
            p.drawLine(15, 5, 12, 2);
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
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}
QIcon commandIcon(const QString &name, QColor color) {
    QIcon icon;
    for (int edge : {20, 24, 32, 48}) {
        QPixmap pixmap(edge, edge);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.scale(edge / 24.0, edge / 24.0);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (name == "open") {
            painter.drawPolyline(QPolygonF{{3, 18}, {3, 6}, {9, 6}, {11, 8}, {20, 8}, {20, 11}});
            painter.drawPolygon(QPolygonF{{3, 19}, {7, 11}, {22, 11}, {18, 19}});
        } else if (name == "save") {
            painter.drawPolygon(QPolygonF{{4, 3}, {18, 3}, {21, 6}, {21, 21}, {4, 21}});
            painter.drawRect(QRectF(8, 3, 8, 6));
            painter.drawRect(QRectF(8, 14, 9, 7));
        } else if (name == "undo" || name == "redo") {
            if (name == "redo") {
                painter.translate(24, 0);
                painter.scale(-1, 1);
            }
            painter.drawPolyline(QPolygonF{{8, 5}, {3, 10}, {8, 15}});
            QPainterPath path;
            path.moveTo(3, 10);
            path.lineTo(13, 10);
            path.cubicTo(21, 10, 23, 19, 14, 20);
            painter.drawPath(path);
        } else if (name == "export") {
            painter.drawPolyline(QPolygonF{{4, 14}, {4, 21}, {20, 21}, {20, 14}});
            painter.drawLine(12, 16, 12, 3);
            painter.drawPolyline(QPolygonF{{7, 8}, {12, 3}, {17, 8}});
        } else if (name == "ui.customize" || name == "brushSettings") {
            painter.drawLine(4, 6, 20, 6);
            painter.drawLine(4, 12, 20, 12);
            painter.drawLine(4, 18, 20, 18);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(9, 6), 2, 2);
            painter.drawEllipse(QPointF(16, 12), 2, 2);
            painter.drawEllipse(QPointF(8, 18), 2, 2);
        } else if (name == "brushLibrary") {
            painter.drawRoundedRect(QRectF(4, 4, 16, 17), 2, 2);
            painter.drawLine(9, 4, 9, 21);
            painter.drawLine(13, 9, 17, 9);
            painter.drawLine(13, 13, 17, 13);
        } else if (name == "addLayer") {
            painter.drawLine(12, 5, 12, 19);
            painter.drawLine(5, 12, 19, 12);
        } else if (name == "duplicate") {
            painter.drawRoundedRect(QRectF(8, 8, 12, 12), 2, 2);
            painter.drawPolyline(QPolygonF{{16, 5}, {16, 3}, {3, 3}, {3, 16}, {5, 16}});
        } else if (name == "layerUp" || name == "layerDown") {
            if (name == "layerDown") {
                painter.translate(0, 24);
                painter.scale(1, -1);
            }
            painter.drawLine(12, 20, 12, 4);
            painter.drawPolyline(QPolygonF{{6, 10}, {12, 4}, {18, 10}});
        } else if (name == "lock") {
            painter.drawRoundedRect(QRectF(5, 10, 14, 11), 2, 2);
            painter.drawArc(QRectF(8, 3, 8, 14), 0, 180 * 16);
            painter.drawLine(12, 14, 12, 17);
        } else if (name == "deleteLayer") {
            painter.drawPolyline(QPolygonF{{6, 8}, {7, 21}, {17, 21}, {18, 8}});
            painter.drawLine(4, 6, 20, 6);
            painter.drawPolyline(QPolygonF{{9, 6}, {9, 3}, {15, 3}, {15, 6}});
            painter.drawLine(10, 10, 10, 17);
            painter.drawLine(14, 10, 14, 17);
        } else if (name == "swapColors") {
            painter.drawLine(4, 8, 20, 8);
            painter.drawPolyline(QPolygonF{{16, 4}, {20, 8}, {16, 12}});
            painter.drawLine(20, 17, 4, 17);
            painter.drawPolyline(QPolygonF{{8, 13}, {4, 17}, {8, 21}});
        }
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
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
QWidget *wrappingRow(QWidget *first, QWidget *second) {
    auto *widget = new QWidget;
    auto *layout = new QFormLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    // Qt's WrapLongRows fit check omits horizontalSpacing, then subtracts it
    // during placement. Budget the gap inside the label cell so a near-fitting
    // pair wraps instead of taking those pixels from the second control.
    auto *labelCell = new QWidget;
    auto *labelLayout = new QHBoxLayout(labelCell);
    labelLayout->setContentsMargins(0, 0, 6, 0);
    labelLayout->addWidget(first);
    layout->setHorizontalSpacing(0);
    layout->setVerticalSpacing(6);
    layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addRow(labelCell, second);
    return widget;
}
QSlider *slider() {
    auto *s = new QSlider(Qt::Horizontal);
    s->setRange(-100, 100);
    return s;
}
QString chooseSaveTarget(QWidget *parent, const QString &title, const QString &initial,
                         const QString &filters, QString *selected = nullptr) {
    QFileDialog dialog(parent, title);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(filters.split(";;"));
    auto suffix = [](const QString &filter) {
        return QRegularExpression("\\*\\.([A-Za-z0-9]+)").match(filter).captured(1);
    };
    dialog.setDefaultSuffix(suffix(dialog.selectedNameFilter()));
    dialog.selectFile(initial);
    QObject::connect(&dialog, &QFileDialog::filterSelected, &dialog,
                     [&](const QString &filter) { dialog.setDefaultSuffix(suffix(filter)); });
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return {};
    if (selected)
        *selected = dialog.selectedNameFilter();
    return dialog.selectedFiles().first();
}
// Cheap, content-based identities: selection, tool and palette changes do not re-render pixels.
QByteArray layerPreviewKey(const Layer &layer) {
    QByteArray key;
    QDataStream out(&key, QIODevice::WriteOnly);
    out << layer.pixels.cacheKey() << layer.mask.cacheKey() << layer.smartObjectId
        << layer.smartObjectSize << layer.geometry
        << layer.brightness << layer.contrast << layer.saturation << layer.blur;
    layer.tones.write(out);
    layer.filters.write(out);
    layer.maskSettings.write(out);
    return key;
}
QByteArray compositePreviewKey(const Document *document) {
    QByteArray key;
    QDataStream out(&key, QIODevice::WriteOnly);
    out << document->state.size;
    for (const auto &layer : document->state.layers)
        out << layerPreviewKey(layer) << layer.offset << layer.visible << layer.opacity
            << layer.blend << layer.clipped;
    return key;
}
// A drag identifies the live stack, never a QListWidget row that a refresh can repurpose.
// Cache keys cover image buffers cheaply; names/text/locks supplement the render identity.
QByteArray layerIdentityKey(const Layer &layer) {
    QByteArray key;
    QDataStream out(&key, QIODevice::WriteOnly);
    out << layerPreviewKey(layer) << layer.offset << layer.visible << layer.opacity
        << layer.blend << layer.clipped << layer.name << layer.locked << layer.text
        << layer.font << layer.color;
    return key;
}
QByteArray layerStackKey(const State &state, int ignore = -1) {
    QByteArray key;
    QDataStream out(&key, QIODevice::WriteOnly);
    out << state.size << qint32(state.layers.size());
    for (int i = 0; i < state.layers.size(); ++i)
        if (i != ignore)
            out << qint32(i) << layerIdentityKey(state.layers[i]);
    return key;
}
QByteArray layerStackKey(const Document *document, int ignore = -1) {
    return layerStackKey(document->state, ignore);
}
struct LayerDragTarget {
    QPointer<Document> document;
    QPointer<Canvas> canvas;
    int index = -1, count = 0, active = -1;
    quint64 revision = 0;
    bool editingMask = false, pendingGesture = false, editing = false;
    QTransform pendingTransform;
    QByteArray stackKey;
};
class LayerDropIndicator : public QWidget {
  public:
    using QWidget::QWidget;
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Highlight));
    }
};
class LayerListWidget : public QListWidget {
  public:
    explicit LayerListWidget(QWidget *parent = nullptr) : QListWidget(parent) {
        // Reordering is an in-list pointer gesture. There is no native drag or
        // transferable payload whose source must survive an external event loop.
        setDragEnabled(false);
        setAcceptDrops(false);
        setDragDropMode(QAbstractItemView::NoDragDrop);
        setAutoScroll(false);
        indicator = new LayerDropIndicator(viewport());
        indicator->setObjectName("layerDropIndicator");
        indicator->setAccessibleName("Katman bırakma konumu");
        indicator->setAttribute(Qt::WA_TransparentForMouseEvents);
        indicator->hide();
        scrollTimer.setInterval(70);
        connect(&scrollTimer, &QTimer::timeout, this, [this] {
            if (!sessionIsCurrent()) {
                invalidateDrag();
                return;
            }
            const QPoint global = dropGlobalPoint;
            const QPoint point = viewport()->mapFromGlobal(global);
            const QRect area = dragArea();
            if (!pointIsVisible(point)) {
                clearInsertion();
                return;
            }
            const int direction = point.y() < area.top() + 24 ? -1
                                  : point.y() > area.bottom() - 24 ? 1 : 0;
            const QPointer<LayerListWidget> alive = this;
            auto *scroll = verticalScrollBar();
            const int previous = scroll->value();
            scroll->setValue(previous + direction * scroll->singleStep());
            if (!alive)
                return;
            if (!sessionIsCurrent()) {
                invalidateDrag();
                return;
            }
            // A narrow inspector may clip this viewport inside its own scroll
            // area. Once the inner list reaches its limit, reveal the clipped
            // edge too, so the first/last insertion slot is actually reachable.
            const bool clipped = direction < 0 ? area.top() > viewport()->rect().top()
                                                : area.bottom() < viewport()->rect().bottom();
            if (direction && scroll->value() == previous && clipped)
                for (QWidget *parent = parentWidget(); parent; parent = parent->parentWidget())
                    if (auto *outer = qobject_cast<QScrollArea *>(parent)) {
                        auto *bar = outer->verticalScrollBar();
                        const int old = bar->value();
                        bar->setValue(old + direction * bar->singleStep());
                        if (!alive)
                            return;
                        if (!sessionIsCurrent()) {
                            invalidateDrag();
                            return;
                        }
                        if (bar->value() != old)
                            break;
                    }
            if (!sessionIsCurrent()) {
                invalidateDrag();
                return;
            }
            const QPoint moved = viewport()->mapFromGlobal(global);
            if (pointIsVisible(moved))
                showInsertion(moved);
            else
                clearInsertion();
        });
    }
    std::function<bool()> displayIsCurrent;
    std::function<bool(bool)> prepareDrag;
    std::function<LayerDragTarget(int)> captureTarget;
    std::function<bool(const LayerDragTarget &)> pressIsCurrent, targetIsCurrent;
    std::function<bool(const LayerDragTarget &, int)> moveLayer;
    void invalidateDrag() {
        suppressPointer |= bool(pressTarget.document) || bool(dragTarget.document);
        pressTarget = {};
        dragTarget = {};
        clearInsertion();
    }

  protected:
    void mousePressEvent(QMouseEvent *event) override {
        invalidateDrag();
        const bool left = event->button() == Qt::LeftButton;
        if (left)
            suppressPointer = false;
        pressPosition = event->position().toPoint();
        const QPointer<LayerListWidget> alive = this;
        const quint64 cancellation = cancelEpoch;
        const auto displayCurrent = displayIsCurrent;
        const auto prepare = prepareDrag;
        // Reject an old displayed row, rather than moving whatever replaced it.
        if (left && (!pointIsVisible(pressPosition) || (!displayCurrent || !displayCurrent()) || !alive)) {
            if (alive)
                suppressPointer = true;
            event->accept();
            return;
        }
        // An ordinary click on the active row keeps its preview alive (including
        // the first click of text double-click). Settle Canvas at the threshold;
        // a different-row click retains the existing row-change finalization.
        const QModelIndex pressed = indexAt(event->position().toPoint());
        const bool changeRow = pressed.isValid() && pressed.row() != currentRow();
        // Settlement may refresh the row, switch tabs, or destroy the window.
        if ((left || event->button() == Qt::RightButton) && prepare && !prepare(changeRow)) {
            if (alive)
                suppressPointer = true;
            event->accept();
            return;
        }
        if (!alive)
            return;
        if (cancelEpoch != cancellation) {
            suppressPointer |= left;
            event->accept();
            return;
        }
        const QModelIndex index = indexAt(event->position().toPoint());
        const bool check = index.isValid() &&
            static_cast<LayerNameDelegate *>(itemDelegate())->isCheckIndicator(
                index, visualRect(index), event->position().toPoint());
        const auto capture = captureTarget;
        const auto before = left && !check && index.isValid() && capture
                                ? capture(index.row()) : LayerDragTarget{};
        if (left && !check && index.isValid() && !before.document) {
            suppressPointer = true;
            event->accept();
            return;
        }
        QListWidget::mousePressEvent(event);
        if (!alive)
            return;
        if (cancelEpoch != cancellation) {
            suppressPointer |= left;
            event->accept();
            return;
        }
        if (before.document && capture) {
            const auto after = capture(index.row());
            // Selection callbacks can synchronously repurpose the row too.
            if (before.document == after.document && before.canvas == after.canvas &&
                before.index == after.index && before.count == after.count &&
                before.revision == after.revision && before.stackKey == after.stackKey)
                pressTarget = after;
            else
                suppressPointer = true;
        }
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if ((event->buttons() & Qt::LeftButton) && suppressPointer &&
            !pressTarget.document && !dragTarget.document) {
            event->accept();
            return;
        }
        if (!(event->buttons() & Qt::LeftButton) || (!pressTarget.document && !dragTarget.document)) {
            QListWidget::mouseMoveEvent(event);
            return;
        }
        event->accept();
        if (!dragTarget.document) {
            if ((event->position().toPoint() - pressPosition).manhattanLength() <
                QApplication::startDragDistance())
                return;
            if (!pressIsCurrent || !pressIsCurrent(pressTarget)) {
                invalidateDrag();
                return;
            }
            const auto pressed = pressTarget;
            const auto prepare = prepareDrag;
            const auto capture = captureTarget;
            const QPointer<LayerListWidget> alive = this;
            const quint64 cancellation = cancelEpoch;
            // Keep only a guarded row intent before settlement; the live drag
            // target is captured afterwards, never from indexAt a refreshed row.
            invalidateDrag();
            if (!prepare || !prepare(true) || !alive || cancelEpoch != cancellation)
                return;
            const auto settled = capture ? capture(pressed.count - 1 - pressed.index) : LayerDragTarget{};
            if (settled.document != pressed.document || settled.canvas != pressed.canvas ||
                settled.index != pressed.index || settled.count != pressed.count ||
                !targetIsCurrent || !targetIsCurrent(settled))
                return;
            dragTarget = settled;
            suppressPointer = true;
        }
        if (!sessionIsCurrent()) {
            invalidateDrag();
            return;
        }
        const QPoint point = event->position().toPoint();
        if (pointIsVisible(point))
            showInsertion(point);
        else
            clearInsertion(); // Keep the source so moving back into the list works.
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && dragTarget.document) {
            event->accept();
            const QPoint point = event->position().toPoint();
            const bool current = pointIsVisible(point) && sessionIsCurrent();
            const auto target = dragTarget;
            const auto move = moveLayer;
            const int slot = insertionSlot(point);
            const int fromRow = target.count - 1 - target.index;
            const int toRow = slot > fromRow ? slot - 1 : slot;
            const int toIndex = target.count - 1 - toRow;
            invalidateDrag();
            suppressPointer = false;
            // Commit may synchronously destroy this window. Do not touch it after.
            if (current && move)
                move(target, toIndex);
            return;
        }
        pressTarget = {};
        if (event->button() == Qt::LeftButton && suppressPointer) {
            suppressPointer = false;
            event->accept();
            return;
        }
        QListWidget::mouseReleaseEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape)
            ++cancelEpoch;
        if (event->key() == Qt::Key_Escape && (pressTarget.document || dragTarget.document)) {
            invalidateDrag();
            event->accept();
            return;
        }
        QListWidget::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent *event) override {
        ++cancelEpoch;
        invalidateDrag();
        QListWidget::focusOutEvent(event);
    }
    bool viewportEvent(QEvent *event) override {
        if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide) {
            ++cancelEpoch;
            invalidateDrag();
        }
        return QListWidget::viewportEvent(event);
    }
    void dragEnterEvent(QDragEnterEvent *event) override { event->ignore(); }
    void dragMoveEvent(QDragMoveEvent *event) override { event->ignore(); }
    void dropEvent(QDropEvent *event) override { event->ignore(); }

  private:
    LayerDragTarget pressTarget, dragTarget;
    QTimer scrollTimer;
    QWidget *indicator;
    bool suppressPointer = false;
    quint64 cancelEpoch = 0;
    QPoint dropGlobalPoint, pressPosition;
    bool sessionIsCurrent() const {
        return dragTarget.document && targetIsCurrent && targetIsCurrent(dragTarget);
    }
    QRect dragArea() const {
        // A scrolled outer inspector can clip the list without resizing its
        // viewport. Feedback and edge scrolling must stay in the actual window.
        return viewport()->visibleRegion().boundingRect();
    }
    bool pointIsVisible(QPoint point) const {
        return viewport()->rect().contains(point) && viewport()->visibleRegion().contains(point);
    }
    int insertionSlot(QPoint point) const {
        for (int row = 0; row < count(); ++row)
            if (point.y() <= visualItemRect(item(row)).center().y())
                return row;
        return count();
    }
    void showInsertion(QPoint point) {
        dropGlobalPoint = viewport()->mapToGlobal(point);
        const int slot = insertionSlot(point);
        int y = slot < count() ? visualItemRect(item(slot)).top() - spacing() / 2
                               : visualItemRect(item(count() - 1)).bottom() + spacing() / 2;
        const QRect area = dragArea();
        if (area.width() < 10 || area.height() < 5) {
            clearInsertion();
            return;
        }
        y = qBound(area.top() + 2, y, area.bottom() - 2);
        QPalette colors = palette();
        colors.setColor(QPalette::Window, colors.color(QPalette::Highlight));
        indicator->setPalette(colors);
        indicator->setGeometry(area.left() + 5, y - 1, area.width() - 10, 3);
        indicator->show();
        indicator->raise();
        if (point.y() < area.top() + 24 || point.y() > area.bottom() - 24) {
            // Pointer updates can arrive faster than the scroll interval. Keep
            // the pending tick instead of postponing it on every edge move.
            if (!scrollTimer.isActive())
                scrollTimer.start();
        } else
            scrollTimer.stop();
    }
    void clearInsertion() {
        scrollTimer.stop();
        indicator->hide();
    }
};

QString recoveryDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery";
    QDir().mkpath(dir);
    return dir;
}
} // namespace
void Histogram::setImage(const QImage &im) {
    bins.fill(0);
    peak = 1;
    if (im.isNull()) {
        update();
        return;
    }
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
    persistSettings = useSettings;
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
    buildWorkflowBanners();
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
    connect(tabs, &QTabWidget::currentChanged, this, [this] {
        static_cast<LayerListWidget *>(layers)->invalidateDrag();
        refresh();
    });
    connect(tabs->tabBar(), &QTabBar::tabMoved, this, [this](int, int) { refresh(); });
    connect(tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    recoveryManager = new RecoveryManager(recoveryDir(), this);
    connect(recoveryManager, &RecoveryManager::failed, this,
            [this](Document *, const QString &message) {
                status->setText("Kurtarma kopyası yazılamadı: " + message);
            });
    autosaveTimer.setParent(this);
    autosaveTimer.setObjectName("autosaveTimer");
    autosaveTimer.setInterval(45000);
    connect(&autosaveTimer, &QTimer::timeout, this, &MainWindow::autosave);
    autosaveTimer.start();
    recoverySaveTimer.setParent(this);
    recoverySaveTimer.setObjectName("recoverySaveTimer");
    recoverySaveTimer.setSingleShot(true);
    recoverySaveTimer.setInterval(5000);
    connect(&recoverySaveTimer, &QTimer::timeout, this, &MainWindow::autosave);
    buildAppearanceExtras();
    uiSaveTimer.setSingleShot(true);
    uiSaveTimer.setInterval(300);
    connect(&uiSaveTimer, &QTimer::timeout, this, &MainWindow::saveUiSettings);
    if (useSettings)
        loadUiSettings();
    else
        applyUiPreset("focus", false);
    if (useSettings)
        loadBrushPresets();
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
    connect(a, &QAction::triggered, this, [this, id, fn](bool) {
        // Settlement can synchronously destroy this action and its connection.
        const auto command = fn;
        const QPointer<MainWindow> self(this);
        const QPointer<Canvas> canvas = currentCanvas();
        const QPointer<Document> target = canvas ? canvas->document : nullptr;
        const int active = target ? target->state.active : -1;
        const bool wasTransforming = canvas && canvas->isTransforming();
        const State expected = wasTransforming ? canvas->pendingTransformState() : State{};
        const QByteArray expectedStack = wasTransforming ? layerStackKey(expected) : QByteArray{};
        // These commands own a chooser/confirmation. Their cancellation must not
        // apply a pending transform, so settle only after the user accepts.
        if (canvas && id != "import" && id != "rasterize" && id != "replaceSmartObjectContents") {
            if ((id == "undo" || id == "redo") && canvas->isTransforming()) {
                canvas->cancelTransform();
                return;
            }
            const bool preservePreview = id == "fit" || id == "actual" || id == "zoomIn" ||
                                         id == "zoomOut" || id == "transform" ||
                                         id == "transformAdvanced" || id == "help" ||
                                         id == "about" || id.startsWith("ui.");
            if (!canvas->isTransforming() || !preservePreview) {
                const bool settled = canvas->finishPendingGesture();
                if (!self || !canvas || !target || currentCanvas() != canvas ||
                    canvas->document != target ||
                    (wasTransforming && !settled) ||
                    (wasTransforming
                         ? (target->state.active != expected.active ||
                            layerStackKey(target) != expectedStack || target->isEditing() ||
                            !target->selection.equivalent(expected.selection, expected.size))
                         : target->state.active != active) ||
                    canvas->hasPendingGesture())
                    return;
            }
        }
        command();
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
    file->addAction(action("import", "Görüntü katmanı ekle…", QKeySequence("Ctrl+Shift+O"),
                           [this] { importLayer(); }));
    file->addSeparator();
    file->addAction(action("save", "Projeyi kaydet", QKeySequence::Save,
                           [this] { saveDocument(currentDocument()); }));
    file->addAction(action("saveAs", "Projeyi farklı kaydet…", QKeySequence::SaveAs,
                           [this] { saveDocument(currentDocument(), true); }));
    file->addAction(
        action("export", "Dışa aktar…", QKeySequence("Ctrl+Shift+E"), [this] { exportDocument(); }));
    file->addAction(action("advancedRaster", "Boyut / DPI / ICC ile raster çıktı…", {},
                           [this] { exportAdvancedRaster(); }));
    file->addAction(action("exportPdf", "PDF dışa aktar…", {}, [this] { exportPdfDocument(); }));
    file->addAction(action("print", "Yazdır…", QKeySequence::Print, [this] { printDocument(); }));
    file->addSeparator();
    file->addAction(action("batch", "Toplu dönüştür…", {}, [this] { batch(); }));
    file->addAction(action("close", "Belgeyi kapat", QKeySequence::Close,
                           [this] { closeTab(tabs->currentIndex()); }));
    file->addSeparator();
    file->addAction(action("quit", "Çıkış", QKeySequence::Quit, [this] { close(); }));
    auto *edit = menuBar()->addMenu("&Düzenle");
    edit->setObjectName("editMenu");
    edit->addAction(action("undo", "Geri al", QKeySequence::Undo, [this] {
        if (currentDocument())
            currentDocument()->undo();
    }));
    edit->addAction(action("redo", "Yinele", QKeySequence::Redo, [this] {
        if (currentDocument())
            currentDocument()->redo();
    }));
    edit->addSeparator();
    auto *selection = menuBar()->addMenu("&Seçim");
    selection->setObjectName("selectionMenu");
    selection->addAction(action("selectAll", "&Tümünü seç", QKeySequence::SelectAll, [this] {
        if (currentDocument()) {
            currentDocument()->setSelection(Selection(QRect(QPoint(), currentDocument()->state.size)),
                                            "Tümü seçildi");
        }
    }));
    selection->addAction(action("deselect", "Seçimi &kaldır", QKeySequence("Ctrl+D"), [this] {
        if (currentDocument()) {
            currentDocument()->deselect();
        }
    }));
    selection->addAction(action("reselect", "Ye&niden seç", QKeySequence("Ctrl+Shift+D"), [this] {
        if (auto *d = currentDocument()) {
            QString e;
            if (!d->reselect(&e) && !e.isEmpty())
                error(e);
        }
    }));
    selection->addSeparator();
    selection->addAction(action("saveSelection", "Seçimi kay&det…", {}, [this] {
        if (auto *d = currentDocument()) {
            QInputDialog dialog(this);
            dialog.setObjectName("saveSelectionDialog");
            dialog.setWindowTitle("Seçimi kaydet");
            dialog.setAccessibleName(dialog.windowTitle());
            dialog.setLabelText("Seçim &adı:");
            dialog.setTextValue({});
            auto *nameInput = dialog.findChild<QLineEdit *>();
            nameInput->setObjectName("savedSelectionName");
            nameInput->setAccessibleName("Kayıtlı seçim adı");
            labelEditorDialogButtons(dialog, "&Kaydet");
            nameInput->setFocus();
            if (dialog.exec() != QDialog::Accepted)
                return;
            const QString name = dialog.textValue().trimmed();
            if (d->savedSelectionNames().contains(name)) {
                QMessageBox replace(QMessageBox::Question, "Kayıtlı seçimi değiştir",
                                    "Bu ad zaten kullanılıyor. Kayıtlı seçim değiştirilsin mi?",
                                    QMessageBox::Yes | QMessageBox::Cancel, this);
                replace.setObjectName("replaceSavedSelectionDialog");
                replace.setAccessibleName(replace.windowTitle());
                replace.button(QMessageBox::Yes)->setText("&Değiştir");
                replace.button(QMessageBox::Yes)->setAccessibleName("Kayıtlı seçimi değiştir");
                auto *cancel = replace.button(QMessageBox::Cancel);
                cancel->setText("İ&ptal");
                cancel->setAccessibleName("İptal");
                replace.setDefaultButton(QMessageBox::Cancel);
                replace.setEscapeButton(QMessageBox::Cancel);
                cancel->setFocus();
                if (replace.exec() != QMessageBox::Yes)
                    return;
            }
            QString e;
            if (!d->saveSelection(name, &e))
                error(e);
        }
    }));
    selection->addAction(action("loadSelection", "Kayıtlı seçimi &yükle…", {}, [this] {
        if (auto *d = currentDocument()) {
            QDialog dialog(this);
            dialog.setObjectName("loadSelectionDialog");
            dialog.setWindowTitle("Kayıtlı seçimi yükle");
            dialog.setAccessibleName(dialog.windowTitle());
            QFormLayout layout(&dialog);
            QComboBox names, mode;
            names.setObjectName("savedSelectionChoice");
            names.setAccessibleName("Kayıtlı seçim");
            mode.setObjectName("savedSelectionMode");
            mode.setAccessibleName("Seçim yükleme işlemi");
            names.addItems(d->savedSelectionNames());
            mode.addItems({"Yeni seçim", "Seçime ekle", "Seçimden çıkar", "Seçimle kesiştir"});
            layout.addRow("&Seçim:", &names);
            layout.addRow("İşle&m:", &mode);
            QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            labelEditorDialogButtons(&buttons, "&Yükle");
            layout.addRow(&buttons);
            connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            names.setFocus();
            if (dialog.exec() != QDialog::Accepted)
                return;
            QString e;
            if (!d->loadSelection(names.currentText(), Selection::Mode(mode.currentIndex()), &e))
                error(e);
        }
    }));
    selection->addAction(action("removeSavedSelection", "Kayıtlı &seçimi sil…", {}, [this] {
        if (auto *d = currentDocument()) {
            QInputDialog dialog(this);
            dialog.setObjectName("removeSavedSelectionDialog");
            dialog.setWindowTitle("Kayıtlı seçimi sil");
            dialog.setAccessibleName(dialog.windowTitle());
            dialog.setLabelText("Seçi&m:");
            dialog.setComboBoxItems(d->savedSelectionNames());
            dialog.setComboBoxEditable(false);
            auto *names = dialog.findChild<QComboBox *>();
            names->setObjectName("savedSelectionChoice");
            names->setAccessibleName("Kayıtlı seçim");
            labelEditorDialogButtons(dialog, "&Sil");
            names->setFocus();
            if (dialog.exec() != QDialog::Accepted)
                return;
            const QString name = dialog.textValue();
            QString e;
            if (!d->removeSavedSelection(name, &e))
                error(e);
        }
    }));
    selection->addSeparator();
    selection->addAction(
        action("invertSelection", "Seçimi ters çe&vir", QKeySequence("Ctrl+Shift+I"), [this] {
            if (auto *d = currentDocument()) {
                QString e;
                if (!d->setSelection(d->selection.inverted(d->state.size), "Seçim ters çevrildi", &e))
                    error(e);
            }
        }));
    selection->addAction(action("featherSelection", "Seçim kenarlarını y&umuşat…", {}, [this] {
        if (auto *d = currentDocument()) {
            if (d->selection.isEmpty())
                return;
            bool ok = false;
            int radius =
                QInputDialog::getInt(this, "Seçim yumuşatma", "Yarıçap (px)", 4, 1, 128, 1, &ok);
            if (ok) {
                QString e;
                if (!d->setSelection(d->selection.feathered(radius, d->state.size), "Seçim yumuşatıldı", &e))
                    error(e);
            }
        }
    }));
    for (const bool grow : {true, false}) {
        selection->addAction(action(
            grow ? "growSelection" : "shrinkSelection", grow ? "Seçimi &büyüt…" : "Seçimi küçü&lt…", {},
            [this, grow] {
                if (auto *d = currentDocument()) {
                    if (d->selection.isEmpty())
                        return;
                    bool ok = false;
                    int radius = QInputDialog::getInt(this, grow ? "Seçimi büyüt" : "Seçimi küçült",
                                                      "Piksel", 2, 1, 128, 1, &ok);
                    if (ok) {
                        QString e;
                        if (!d->setSelection(d->selection.grown(grow ? radius : -radius, d->state.size),
                                             grow ? "Seçim büyütüldü" : "Seçim küçültüldü", &e))
                            error(e);
                    }
                }
            }));
    }
    edit->addAction(
        action("deleteSelection", "Seçili pikselleri sil", QKeySequence(Qt::Key_Delete), [this] {
            if (currentCanvas())
                currentCanvas()->deleteSelection();
        }));
    edit->addAction(action("copy", "Birleşik görüntüyü kopyala", QKeySequence::Copy, [this] {
        if (auto *d = currentDocument()) {
            QImage im = d->selection.copyFrom(d->composite());
            if (im.isNull())
                return;
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
            Layer layer;
            layer.name = "Panodan";
            layer.pixels = im;
            QString e;
            if (!d->insertLayer(layer, "Panodan katman", &e))
                error(e);
        }
    }));
    auto *image = menuBar()->addMenu("&Görüntü");
    image->addAction(action("resize", "Görüntü boyutu…", {}, [this] { resizeDocument(); }));
    image->addAction(action("canvasSize", "Tuval boyutu…", QKeySequence("Ctrl+Alt+C"),
                            [this] { changeCanvasSize(); }));
    image->addAction(action("crop", "Seçime göre kırp", QKeySequence("Ctrl+Shift+X"), [this] {
        if (auto *d = currentDocument()) {
            QString err;
            if (!d->crop(d->selection.bounds(), &err))
                error(err);
            else
                currentCanvas()->fit();
        }
    }));
    image->addSeparator();
    image->addAction(action("rotateRight", "90° sağa döndür", {}, [this] {
        if (auto *d = currentDocument()) {
            QString e;
            if (!d->rotate(true, &e))
                error(e);
            else
                currentCanvas()->fit();
        }
    }));
    image->addAction(action("rotateLeft", "90° sola döndür", {}, [this] {
        if (auto *d = currentDocument()) {
            QString e;
            if (!d->rotate(false, &e))
                error(e);
            else
                currentCanvas()->fit();
        }
    }));
    image->addAction(action("flipH", "Yatay çevir", {}, [this] {
        if (currentDocument()) {
            QString e;
            if (!currentDocument()->flip(true, &e))
                error(e);
        }
    }));
    image->addAction(action("flipV", "Dikey çevir", {}, [this] {
        if (currentDocument()) {
            QString e;
            if (!currentDocument()->flip(false, &e))
                error(e);
        }
    }));
    auto *filters = menuBar()->addMenu("&Filtre");
    filters->addAction(
        action("photoFilters", "Gaussian, keskinleştirme ve klasik filtreler…", {}, [this] {
            auto *d = currentDocument();
            if (!d || d->state.layers[d->state.active].locked)
                return;
            d->beginEdit();
            FilterDialog dialog(d, this);
            if (dialog.exec() == QDialog::Accepted)
                d->commit("Klasik fotoğraf filtreleri");
            else
                d->cancelEdit();
        }));
    filters->addAction(action("resetFilters", "Klasik filtreleri sıfırla", {}, [this] {
        mutate([](Document *d) { d->adjustFilters(FilterSettings{}); });
    }));
    filters->addAction(action("tones", "Seviyeler, eğriler ve renk…", QKeySequence("Ctrl+L"), [this] {
        auto *d = currentDocument();
        if (!d || d->state.layers[d->state.active].locked)
            return;
        d->beginEdit();
        ToneDialog dialog(d->state.layers[d->state.active].tones, this);
        connect(&dialog, &ToneDialog::settingsChanged, d,
                [d](const ToneSettings &value) { d->adjustTones(value); });
        if (dialog.exec() == QDialog::Accepted) {
            d->adjustTones(dialog.settings());
            d->commit("Seviyeler, eğriler ve renk");
        } else
            d->cancelEdit();
    }));
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
    layer->setToolTipsVisible(true);
    layer->addAction(action("positionLayer", "Etkin katman konumu…", {}, [this] { positionLayer(); }));
    actions["positionLayer"]->setIconText("Konum…");
    auto *alignment = layer->addMenu("Etkin katmanı hizala");
    alignment->setObjectName("layerAlignmentMenu");
    alignment->setAccessibleName("Etkin katmanı açık hedefe hizala");
    struct AlignmentCommand {
        const char *id;
        const char *text;
        Document::LayerAlignment direction;
    };
    const AlignmentCommand commands[] = {
        {"Left", "Sola", Document::LayerAlignment::Left},
        {"Center", "Yatay ortaya", Document::LayerAlignment::Center},
        {"Right", "Sağa", Document::LayerAlignment::Right},
        {"Top", "Üste", Document::LayerAlignment::Top},
        {"Middle", "Dikey ortaya", Document::LayerAlignment::Middle},
        {"Bottom", "Alta", Document::LayerAlignment::Bottom}};
    for (auto target : {Document::AlignmentTarget::Canvas, Document::AlignmentTarget::Selection}) {
        const bool canvas = target == Document::AlignmentTarget::Canvas;
        auto *menu = alignment->addMenu(canvas ? "Tuval sınırlarına" : "Geçerli seçim sınırlarına");
        menu->setObjectName(canvas ? "alignCanvasMenu" : "alignSelectionMenu");
        for (const auto &command : commands) {
            const auto direction = command.direction;
            auto *a = action(QString(canvas ? "alignCanvas" : "alignSelection") + command.id,
                             command.text, {}, [this, direction, target] { alignLayer(direction, target); });
            a->setToolTip("Yalnızca etkin katmanı taşır; saydam/maske kenarları sınırın içindedir.");
            menu->addAction(a);
        }
    }
    connect(alignment, &QMenu::aboutToShow, this, [this] {
        finishLayerRename();
        if (auto *canvas = currentCanvas())
            canvas->finishPendingGesture();
        updateActionAvailability();
    });
    layer->addSeparator();
    layer->addAction(action("transform", "Katmanın tamamını dönüştür…", QKeySequence("Ctrl+T"),
                            [this] { transformLayer(); }));
    actions["transform"]->setIconText("Dönüştür…");
    layer->addAction(action("transformAdvanced", "Sayısal dönüşüm / perspektif…",
                            QKeySequence("Ctrl+Alt+T"), [this] { transformLayerNumerically(); }));
    layer->addAction(action("resetTransform", "Dönüşümü sıfırla", {}, [this] {
        if (auto *d = currentDocument()) {
            d->beginEdit();
            QString e;
            if (d->setLayerTransform({}, &e))
                d->commit("Dönüşüm sıfırlandı");
            else {
                d->cancelEdit();
                error(e);
            }
        }
    }));
    auto *smartObjects = layer->addMenu("Akıllı nesne");
    smartObjects->setObjectName("smartObjectMenu");
    smartObjects->setToolTipsVisible(true);
    smartObjects->addAction(action("convertSmartObject", "Akıllı nesneye dönüştür", {},
                                   [this] { convertSmartObject(); }));
    smartObjects->addAction(action("replaceSmartObjectContents", "İçeriği değiştir…", {},
                                   [this] { replaceSmartObjectContents(); }));
    layer->addAction(action("rasterize", "Görünümü rasterleştir…", {}, [this] { rasterizeLayer(); }));
    layer->addAction(action("newPaintLayer", "Yeni boş katmana boya", {}, [this] { newPaintLayer(); }));
    actions["newPaintLayer"]->setToolTip(
        "Kaynak katmanı değiştirmeden yeni, saydam bir piksel katmanı ekler ve Fırça aracına geçer.");
    layer->addAction(
        action("mergeVisible", "Görünür katmanları birleştir", QKeySequence("Ctrl+Alt+Shift+E"), [this] {
            if (auto *d = currentDocument()) {
                QString e;
                if (!d->mergeVisible(&e))
                    error(e);
            }
        }));
    layer->addAction(action("flatten", "Tuvale düzleştir (gizli katmanları kaldır)", {}, [this] {
        if (auto *d = currentDocument()) {
            QString e;
            if (!d->flatten(&e))
                error(e);
        }
    }));
    layer->addSeparator();
    layer->addAction(action("mergeDown", "Alta birleştir", QKeySequence("Ctrl+E"), [this] {
        if (auto *d = currentDocument()) {
            QString e;
            if (!d->mergeDown(&e))
                error(e);
        }
    }));
    layer->addAction(
        action("clipLayer", "Alt katmana kırp / kırpmayı kaldır", QKeySequence("Ctrl+Alt+G"), [this] {
            if (auto *d = currentDocument()) {
                QString e;
                if (!d->setClipping(!d->state.layers[d->state.active].clipped, &e))
                    error(e);
            }
        }));
    for (bool cut : {false, true})
        layer->addAction(action(cut ? "selectionCutLayer" : "selectionCopyLayer",
                                cut ? "Seçimi yeni katmana kes (maske/ayar uygulanır)"
                                    : "Seçimi yeni katmana kopyala (maske/ayar uygulanır)",
                                QKeySequence(cut ? "Ctrl+Shift+J" : "Ctrl+Alt+J"), [this, cut] {
                                    if (auto *d = currentDocument()) {
                                        QString e;
                                        if (!d->layerViaSelection(cut, &e))
                                            error(e);
                                    }
                                }));
    layer->addAction(action("addLayer", "Yeni katman", QKeySequence("Ctrl+Shift+N"), [this] {
        if (auto *d = currentDocument()) {
            QString e;
            if (!d->addLayer(QString("Katman %1").arg(d->state.layers.size() + 1), &e))
                error(e);
        }
    }));
    layer->addAction(action("duplicate", "Katmanı / seçimi çoğalt", QKeySequence("Ctrl+J"), [this] {
        if (currentDocument()) {
            QString e;
            if (!currentDocument()->layerViaSelection(false, &e))
                error(e);
        }
    }));
    layer->addAction(action("duplicateWhole", "Katmanın tamamını çoğalt", {}, [this] {
        if (auto *d = currentDocument()) {
            QString e;
            if (!d->duplicate(&e))
                error(e);
        }
    }));
    layer->addAction(action("selectLayerPixels", "Katman &alfasını seç", {}, [this] {
        if (auto *d = currentDocument()) {
            Layer source = d->state.layers[d->state.active];
            source.mask = {};
            source.blur = 0;
            source.filters = {};
            source.cache = {};
            QString e;
            if (!d->setSelection(Selection::fromAlpha(source.rendered(), source.renderedOffset(), d->state.size),
                                 "Katman saydamlığı seçildi", &e))
                error(e);
        }
    }));
    selection->addSeparator();
    selection->addAction(actions["selectLayerPixels"]);
    layer->addAction(action("rename", "Yeniden adlandır", QKeySequence(Qt::Key_F2),
                            [this] { renameLayer(); }));
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
    layer->addAction(action("maskSettings", "Maske yoğunluğu / kenar yumuşatma…", {}, [this] {
        auto *d = currentDocument();
        if (!d || d->state.layers[d->state.active].locked ||
            d->state.layers[d->state.active].mask.isNull())
            return;
        d->beginEdit();
        MaskDialog dialog(d, this);
        if (dialog.exec() == QDialog::Accepted)
            d->commit("Maske ayarları");
        else
            d->cancelEdit();
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
            "seçime uyar.\nCtrl+D seçimi kaldırır; Ctrl+Shift+D yeniden seçer. Seçim işlemleri geri alınabilir.\n"
            "Seçim menüsü: tümünü seç, kaldır, yeniden seç, ters çevir, yumuşat ve büyüt/küçült.\n"
            "Seçim → Seçimi kaydet / Kayıtlı seçimi yükle, adlandırılmış seçimleri projede saklar.\n"
            "Kayıtlı seçim içeren normal projeler v6, akıllı nesne içeren projeler v7 kullanır.\n"
            "Katman → Akıllı nesne → Akıllı nesneye dönüştür; İçeriği değiştir kaynak görüntüyü "
            "yenilerken çerçeveyi ve dönüşümü korur. Yeni görüntü özgün çözünürlüğünde gömülür; "
            "farklı en-boy oranı sabit çerçeveyi dolduracak şekilde esnetilir. Önceki EA2 v7 projeleri açamaz.\n"
            "Tekerlek: işaretçinin çevresinde yakınlaştırma. Orta tuş: gezinme.\n\n.pdeck "
            "proje dosyası katmanları, maskeleri ve renk ayarlarını korur.\nPNG/WebP saydamlığı korur; "
            "JPEG beyaz arka planla düzleştirilir.\nCtrl+Shift+E dışa aktarım penceresini açar. "
            "Ctrl+E alta, Ctrl+Alt+Shift+E görünür katmanları birleştirir.\n"
            "Ctrl+T katman dönüşümü, metni ve kaynak pikselleri korur. Enter dönüşümü uygular, "
            "Esc iptal eder. Ctrl+Alt+T sayısal dönüşüm ve perspektif ayarlarını açar. "
            "Görüntüyü yeniden boyutlandırma / 90° döndürme, çizim ve silme metni "
            "piksele dönüştürebilir.\nKilit, doğrudan katman düzenlemesini engeller; tüm görüntü dönüşümleri "
            "kilitli katmanları da dönüştürür.\n\nSınırlar: 16 MP tuval, 64 katman, 256 MiB katman "
            "verisi.\nGeçmiş: 30 adım / 128 MiB; büyük belgelerde adım sayısı azalır.\nDüzenleme sonrası yaklaşık "
            "5 saniyede kurtarma kopyası alınır; 45 saniyelik kontrol sürer. Etkin çizimin bitmesi "
            "beklenir. Kurtarma son değişikliklerin tamamını garanti etmez.\n\nTamamı bilgisayarınızda çalışır; AI, hesap ve ağ bağlantısı "
            "gerekmez.");
    }));
    help->addAction(action("about", "PhotoDeck hakkında", {}, [this] {
        const QString title = QString("PhotoDeck %1 · Erken erişim").arg(PhotoDeck::Version);
        // macOS message boxes omit window titles, so keep the version in the visible body too.
        QMessageBox::about(this, title,
                           QString("<h2>%1</h2><p>Hafif, yerel fotoğraf ve katman düzenleyici.</p><p>Qt "
                                   "6 / C++ · Linux / Windows / macOS</p><p>AI, reklam, telemetri ve "
                                   "abonelik içermez.</p><p>Bu sürüm RGB 8-bit raster düzenlemeye odaklanır; "
                                   "RGB ICC giriş ve çıktı profilleri desteklenir. PSD/RAW, CMYK, baskı "
                                   "provası, vektör yolları ve Photoshop eklentileri desteklenmez.</p>")
                               .arg(title));
    }));
}
void MainWindow::buildTools() {
    auto *top = addToolBar("Belge");
    top->setObjectName("documentToolbar");
    documentToolbar = top;
    top->setMovable(true);
    top->setIconSize({19, 19});
    top->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *brand = new QLabel("PhotoDeck");
    brandLabel = brand;
    brand->setObjectName("brandLabel");
    top->addWidget(brand);
    top->addSeparator();
    actions["open"]->setIconText("Aç");
    actions["import"]->setIconText("Görüntü katmanı ekle…");
    actions["import"]->setToolTip(
        "Açık belgeye bir görüntüyü ayrı katman olarak ekleyin; ardından Ctrl+T ile boyutlandırın. "
        "Ctrl+Shift+O");
    actions["save"]->setIconText("Kaydet");
    actions["export"]->setIconText("Dışa aktar");
    top->addAction(actions["open"]);
    top->addAction(actions["import"]);
    top->addAction(actions["save"]);
    top->addSeparator();
    top->addAction(actions["undo"]);
    top->addAction(actions["redo"]);
    for (const auto &id : {"undo", "redo"})
        if (auto *button = qobject_cast<QToolButton *>(top->widgetForAction(actions[id])))
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    top->addSeparator();
    top->addAction(actions["export"]);
    top->widgetForAction(actions["export"])->setProperty("primary", true);
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    top->addWidget(spacer);
    auto *local = new QLabel("●  Yerel çalışma");
    local->setObjectName("localLabel");
    local->setToolTip("Görüntüleriniz bu bilgisayarda kalır. Hesap veya bağlantı gerekmez.");
    top->addWidget(local);
    auto *options = addToolBar("Araç seçenekleri");
    options->setObjectName("options");
    optionsToolbar = options;
    options->setMovable(true);
    options->setIconSize({17, 17});
    options->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *contextLabel = new QLabel("TAŞI");
    contextLabel->setObjectName("toolbarCaption");
    options->addWidget(contextLabel);
    options->addSeparator();
    auto *moveControls = new QWidget;
    moveControls->setObjectName("moveControls");
    auto *moveLayout = new QHBoxLayout(moveControls);
    moveLayout->setContentsMargins(0, 0, 0, 0);
    moveLayout->setSpacing(10);
    moveAutoSelectInput = new QCheckBox("Tuvalden katman seç");
    moveAutoSelectInput->setObjectName("moveAutoSelect");
    moveAutoSelectInput->setToolTip(
        "Tıkladığınız görünür, kilidi açık katmanı seçer. Saydam alanları atlar; üstteki katman önceliklidir.");
    moveShowBoundsInput = new QCheckBox("Katman sınırlarını göster");
    moveShowBoundsInput->setObjectName("moveShowBounds");
    moveShowBoundsInput->setChecked(true);
    moveShowBoundsInput->setToolTip("Taşı aracında etkin hedefin adını ve kaynak alanı sınırını gösterir.");
    moveLayout->addWidget(moveAutoSelectInput);
    moveLayout->addWidget(moveShowBoundsInput);
    actions["ui.options.move"] = options->addWidget(moveControls);
    connect(moveAutoSelectInput, &QCheckBox::toggled, this, [this](bool checked) {
        if (!refreshing && currentCanvas()) {
            currentCanvas()->setAutoSelectLayers(checked);
            updateToolOptions();
        }
    });
    connect(moveShowBoundsInput, &QCheckBox::toggled, this, [this](bool checked) {
        if (!refreshing && currentCanvas())
            currentCanvas()->setShowLayerBounds(checked);
    });
    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(Qt::TopToolBarArea, options);
    auto *brushControls = new QWidget;
    brushControls->setObjectName("brushControls");
    auto *brushLayout = new QHBoxLayout(brushControls);
    brushLayout->setContentsMargins(0, 0, 0, 0);
    brushLayout->setSpacing(8);
    brushLayout->addWidget(new QLabel("Boyut"));
    sizeInput = new QSpinBox;
    sizeInput->setObjectName("brushSize");
    sizeInput->setRange(1, 512);
    sizeInput->setValue(32);
    sizeInput->setSuffix(" px");
    sizeInput->setFixedWidth(82);
    sizeInput->setAccessibleName("Fırça boyutu");
    sizeInput->setToolTip("Fırça çapı (piksel)");
    brushLayout->addWidget(sizeInput);
    brushLayout->addWidget(new QLabel("Sertlik"));
    hardnessInput = new QSpinBox;
    hardnessInput->setObjectName("brushHardness");
    hardnessInput->setRange(0, 100);
    hardnessInput->setValue(80);
    hardnessInput->setSuffix(" %");
    hardnessInput->setFixedWidth(76);
    hardnessInput->setAccessibleName("Fırça sertliği");
    brushLayout->addWidget(hardnessInput);
    brushOpacityInput = new QSpinBox;
    brushOpacityInput->setObjectName("brushFlow");
    brushOpacityInput->setRange(1, 100);
    brushOpacityInput->setValue(100);
    brushOpacityInput->setSuffix(" %");
    brushOpacityInput->setFixedWidth(76);
    brushOpacityInput->setAccessibleName("Fırça akışı");
    actions["ui.options.brush"] = options->addWidget(brushControls);
    auto *flowControls = row(new QLabel("Akış"), brushOpacityInput);
    flowControls->setObjectName("flowControls");
    actions["ui.options.flow"] = options->addWidget(flowControls);
    action("brushSettings", "Fırça / tablet ayarları…", QKeySequence("Ctrl+Alt+B"),
           [this] { customizeBrush(); });
    action("brushLibrary", "Fırça kütüphanesi…", QKeySequence("Ctrl+Alt+Shift+B"),
           [this] { showBrushLibrary(); });
    for (const auto &id : {"brushSettings", "brushLibrary"}) {
        auto *button = new QToolButton;
        button->setDefaultAction(actions[id]);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        actions[QString("ui.options.") + id] = options->addWidget(button);
    }
    actions["brushSettings"]->setIconText("Fırça");
    actions["brushLibrary"]->setIconText("Uçlar");
    actions["crop"]->setIconText("Kırp");
    if (auto *editMenu = findChild<QMenu *>("editMenu")) {
        editMenu->addSeparator();
        editMenu->addAction(actions["brushSettings"]);
        editMenu->addAction(actions["brushLibrary"]);
    }
    actions["ui.options.divider"] = options->addSeparator();
    auto *cropButton = new QToolButton;
    cropButton->setDefaultAction(actions["crop"]);
    cropButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    actions["ui.options.crop"] = options->addWidget(cropButton);
    auto *selectionMode = new QComboBox;
    selectionModeInput = selectionMode;
    selectionMode->setObjectName("selectionMode");
    selectionMode->addItems({"Yeni seçim", "Seçime ekle", "Seçimden çıkar", "Seçimle kesiştir"});
    actions["ui.options.selectionMode"] = options->addWidget(selectionMode);
    auto *tolerance = new QSpinBox;
    tolerance->setObjectName("selectionTolerance");
    selectionToleranceInput = tolerance;
    tolerance->setRange(0, 255);
    tolerance->setValue(24);
    tolerance->setPrefix("Tolerans: ");
    actions["ui.options.tolerance"] = options->addWidget(tolerance);
    auto *contiguous = new QCheckBox("Bağlı alan");
    contiguous->setObjectName("contiguousSelection");
    contiguousSelectionInput = contiguous;
    contiguous->setChecked(true);
    actions["ui.options.contiguous"] = options->addWidget(contiguous);
    auto *hint = new QLabel;
    hint->setObjectName("toolOptionsHint");
    hint->setMinimumWidth(0);
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    options->addWidget(hint);
    auto syncSelection = [this, selectionMode, tolerance, contiguous] {
        if (refreshing)
            return;
        if (auto *c = currentCanvas()) {
            c->selectionMode = Selection::Mode(selectionMode->currentIndex());
            c->selectionTolerance = tolerance->value();
            c->contiguousSelection = contiguous->isChecked();
        }
    };
    connect(selectionMode, &QComboBox::currentIndexChanged, this,
            [syncSelection](int) { syncSelection(); });
    connect(tolerance, &QSpinBox::valueChanged, this, [syncSelection](int) { syncSelection(); });
    connect(contiguous, &QCheckBox::toggled, this, [syncSelection](bool) { syncSelection(); });
    auto syncBrush = [this] {
        if (refreshing)
            return;
        if (auto *c = currentCanvas()) {
            c->brush.size = sizeInput->value();
            c->brush.hardness = hardnessInput->value();
            c->brush.flow = brushOpacityInput->value();
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
    toolPalette = new QWidget;
    toolPalette->setObjectName("toolPalette");
    toolGrid = new QGridLayout(toolPalette);
    toolGrid->setContentsMargins(2, 2, 2, 2);
    toolGrid->setHorizontalSpacing(3);
    toolGrid->setVerticalSpacing(3);
    toolGrid->setAlignment(Qt::AlignTop);
    toolScroll = new QScrollArea;
    toolScroll->setObjectName("toolPaletteScroll");
    toolScroll->setFrameShape(QFrame::NoFrame);
    toolScroll->setWidgetResizable(true);
    toolScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    toolScroll->setWidget(toolPalette);
    tools->addWidget(toolScroll);
    auto *group = new QActionGroup(this);
    group->setExclusive(true);
    QStringList names = {"Taşı",          "Dikdörtgen seçim",
                         "Kırp",          "Fırça",
                         "Kalem",         "Silgi",
                         "Klon damgası",  "Boya kovası",
                         "Degrade",       "Metin",
                         "Dikdörtgen",    "Elips",
                         "Damlalık",      "El",
                         "Eliptik seçim", "Serbest kement",
                         "Çokgen kement", "Sihirli değnek"};
    QStringList keys = {"V", "M", "C", "B", "P", "E",       "S", "F",       "G",
                        "T", "U", "O", "I", "H", "Shift+M", "L", "Shift+L", "W"};
    for (int i = 0; i < names.size(); ++i) {
        auto *a = action("tool" + QString::number(i), names[i] + " (" + keys[i] + ")",
                         QKeySequence(keys[i]), [this, i, names] {
                             if (auto *c = currentCanvas()) {
                                 c->setTool(Canvas::Tool(i));
                                 status->setText(names[i] +
                                                 (i == Canvas::Clone ? " · Alt + tıklama: kaynak" : ""));
                                 updateToolOptions();
                             }
                         });
        a->setIcon(toolIcon(i));
        a->setIconText(names[i]);
        a->setCheckable(true);
        group->addAction(a);
        if (i == 0)
            a->setChecked(true);
        a->setToolTip(names[i] + " · " + keys[i] +
                      (i == Canvas::Move ? " · Tuvalde ok: 1 px · Shift+ok: 10 px" : ""));
        auto *button = new QToolButton(toolPalette);
        button->setDefaultAction(a);
        button->setObjectName("toolButton" + QString::number(i));
        button->setProperty("pdToolButton", true);
        button->setAccessibleName(names[i]);
        toolButtons.append(button);
        toolGrid->addWidget(button, i / 2, i % 2);
    }
    colorButton = new QToolButton(toolPalette);
    colorButton->setObjectName("foreground");
    colorButton->setFixedSize(32, 32);
    colorButton->setAccessibleName("Ön plan rengi");
    colorButton->setToolTip("Ön plan rengini seç · I: tuvalden renk al");
    connect(colorButton, &QToolButton::clicked, this, &MainWindow::chooseColor);
    toolGrid->addWidget(colorButton, 9, 0, Qt::AlignCenter);
    auto *swap = action("swapColors", "Renkleri değiştir (X)", QKeySequence("X"), [this] {
        if (auto *c = currentCanvas()) {
            std::swap(c->foreground, c->background);
            refresh();
        }
    });
    auto *swapButton = new QToolButton(toolPalette);
    swapButton->setDefaultAction(swap);
    swapButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    swapButton->setAccessibleName("Ön ve arka plan renklerini değiştir");
    swapButton->setObjectName("swapColorsButton");
    toolGrid->addWidget(swapButton, 9, 1);
    toolsDock = createDock("Araçlar", "toolsDock", tools);
    toolsDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::LeftDockWidgetArea, toolsDock);
}
void MainWindow::updateToolOptions() {
    const auto *canvas = currentCanvas();
    const auto tool = canvas ? canvas->tool : Canvas::Move;
    const bool transforming = canvas && canvas->isTransforming();
    const bool brush = canvas && !transforming &&
                       (tool == Canvas::Brush || tool == Canvas::Pencil ||
                        tool == Canvas::Eraser || tool == Canvas::Clone);
    const bool flow = canvas && !transforming &&
                      (brush || tool == Canvas::Fill || tool == Canvas::Gradient ||
                       tool == Canvas::Rectangle || tool == Canvas::Ellipse);
    const bool selection = canvas && !transforming && canvas->isSelectionTool();
    const bool crop = canvas && !transforming &&
                      (tool == Canvas::Crop || selection || canvas->document->selection.hasPixels());
    for (const auto &id : {"brush", "brushSettings", "brushLibrary"})
        actions[QString("ui.options.") + id]->setVisible(brush);
    actions["ui.options.flow"]->setVisible(flow);
    actions["ui.options.divider"]->setVisible(flow && crop);
    actions["ui.options.crop"]->setVisible(crop);
    actions["ui.options.selectionMode"]->setVisible(selection);
    actions["ui.options.tolerance"]->setVisible(canvas && !transforming && tool == Canvas::Wand);
    actions["ui.options.contiguous"]->setVisible(canvas && !transforming && tool == Canvas::Wand);
    actions["ui.options.move"]->setVisible(canvas && !transforming && tool == Canvas::Move);
    if (canvas) {
        QSignalBlocker selectBlock(moveAutoSelectInput), boundsBlock(moveShowBoundsInput);
        moveAutoSelectInput->setChecked(canvas->autoSelectLayers());
        moveShowBoundsInput->setChecked(canvas->showsLayerBounds());
    }
    auto *caption = optionsToolbar->findChild<QLabel *>("toolbarCaption");
    caption->setText(transforming ? "DÖNÜŞTÜR"
                     : canvas ? actions["tool" + QString::number(tool)]->iconText().toUpper()
                              : "ARAÇ AYARLARI");
    QString hint;
    if (!canvas)
        hint = "Yeni belge oluşturun veya bir görüntü açın";
    else if (transforming)
        hint = "Enter: uygula · Esc / Geri al: iptal · Orta tuş: kaydır";
    else if (selection)
        hint = "Shift: ekle · Alt: çıkar";
    else if (tool == Canvas::Move)
        hint = canvas->autoSelectLayers()
                   ? "Tıklanan katmanı taşı · Ok: 1 px · Shift+ok: 10 px · Ctrl+T: dönüştür"
                   : QString("Hedef: %1 · Sürükle: taşı · Ctrl+T: dönüştür")
                         .arg(canvas->activeLayerTargetName());
    else if (tool == Canvas::Crop)
        hint = "Alanı sürükleyin, ardından Kırp";
    else if (tool == Canvas::Clone)
        hint = "Alt + tıklama: kaynak seç";
    else if (tool == Canvas::Text)
        hint = "Metin eklemek için tuvale tıklayın";
    else if (tool == Canvas::Hand)
        hint = "Tuvali sürükleyin · Tekerlek: yakınlaştır";
    else if (tool == Canvas::Picker)
        hint = "Ön plan rengini almak için tuvale tıklayın";
    else if (tool == Canvas::Rectangle || tool == Canvas::Ellipse)
        hint = "Shift: kare veya daire · X: renkleri değiştir";
    else if (!brush)
        hint = "Tuvalde uygulayın · X: renkleri değiştir";
    auto *hintLabel = optionsToolbar->findChild<QLabel *>("toolOptionsHint");
    hintLabel->setText(hint);
    hintLabel->setToolTip(hint);
    hintLabel->setAccessibleName(hint);
    caption->setToolTip(hint);
    updatePaintAvailability();
}

void MainWindow::buildInspector() {
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    panel->setObjectName("colorPanel");
    layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(12);
    auto *histogramTitle = new QLabel("TON DAĞILIMI");
    histogramTitle->setObjectName("sectionTitle");
    histogramTitle->setWordWrap(true);
    auto *rgbLabel = new QLabel("RGB");
    rgbLabel->setObjectName("metaBadge");
    layout->addWidget(row(histogramTitle, rgbLabel));
    histogram = new Histogram;
    histogram->setFixedHeight(56);
    layout->addWidget(histogram);
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(14);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    brightness = slider();
    contrast = slider();
    saturation = slider();
    brightness->setObjectName("brightness");
    contrast->setObjectName("contrast");
    saturation->setObjectName("saturation");
    auto adjustmentRow = [&](const QString &title, QSlider *control) {
        control->setAccessibleName(title);
        auto *value = new QLabel("0");
        value->setObjectName("valueLabel");
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setFixedWidth(30);
        connect(control, &QSlider::valueChanged, value,
                [value](int amount) { value->setText(QString::number(amount)); });
        form->addRow(title, row(control, value));
    };
    adjustmentRow("Parlaklık", brightness);
    adjustmentRow("Kontrast", contrast);
    adjustmentRow("Doygunluk", saturation);
    layout->addLayout(form);
    for (auto *s : {brightness, contrast, saturation}) {
        connect(s, &QSlider::sliderPressed, this, [this] {
            if (currentDocument()) {
                currentCanvas()->finishPendingGesture();
                currentDocument()->beginEdit();
            }
        });
        connect(s, &QSlider::valueChanged, this, [this, s](int v) {
            s->setToolTip(QString::number(v));
            if (refreshing || !currentDocument())
                return;
            currentCanvas()->finishPendingGesture();
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
    auto *reset = new QPushButton("Ayarları sıfırla");
    reset->setObjectName("resetColorAdjustments");
    reset->setFlat(true);
    connect(reset, &QPushButton::clicked, this, [this] {
        if (auto *d = currentDocument()) {
            currentCanvas()->finishPendingGesture();
            d->beginEdit();
            d->adjust(0, 0, 0);
            d->adjustTones(ToneSettings{});
            d->commit("Renk ayarları sıfırlandı");
        }
    });
    layout->addWidget(reset);
    auto *advancedTones = new QPushButton("Seviyeler,\neğriler ve renk…");
    advancedTones->setAccessibleName("Seviyeler, eğriler ve renk…");
    advancedTones->setObjectName("advancedTones");
    connect(advancedTones, &QPushButton::clicked, actions["tones"], &QAction::trigger);
    layout->addWidget(advancedTones);
    // Keep the controls' minimum layout inside a clipped, keyboard-scrollable
    // viewport when other docks or a larger font leave less vertical space.
    auto *colorScroll = new QScrollArea;
    colorScroll->setObjectName("colorPanelScroll");
    colorScroll->setWidgetResizable(true);
    colorScroll->setFrameShape(QFrame::NoFrame);
    colorScroll->setWidget(panel);
    colorDock = createDock("Renk & ton", "colorDock", colorScroll);
    addDockWidget(Qt::RightDockWidgetArea, colorDock);
    panel = new QWidget;
    layerPanel = panel;
    layout = new QVBoxLayout(panel);
    panel->setObjectName("layerPanel");
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    blend = new QComboBox;
    blend->addItems(Blending::names());
    // The popup can show long mode names without forcing the whole inspector
    // wider than its scroll viewport on native Windows fonts.
    blend->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    blend->setMinimumContentsLength(6);
    blend->setObjectName("blendMode");
    blend->setAccessibleName("Katman karışım modu");
    layout->addWidget(wrappingRow(new QLabel("Karışım"), blend));
    opacity = slider();
    opacity->setRange(0, 100);
    opacity->setValue(100);
    opacity->setObjectName("layerOpacity");
    opacity->setAccessibleName("Katman opaklığı");
    auto *opacityValue = new QLabel("100 %");
    opacityValue->setObjectName("valueLabel");
    opacityValue->setFixedWidth(42);
    opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(opacity, &QSlider::valueChanged, opacityValue,
            [opacityValue](int value) { opacityValue->setText(QString::number(value) + " %"); });
    layout->addWidget(row(new QLabel("Opaklık"), opacity, opacityValue));
    connect(blend, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (!refreshing)
            mutate([v](Document *d) {
                auto &l = d->state.layers[d->state.active];
                if (!l.locked)
                    l.blend = v;
            });
    });
    connect(opacity, &QSlider::sliderPressed, this, [this] {
        if (currentDocument()) {
            currentCanvas()->finishPendingGesture();
            currentDocument()->beginEdit();
        }
    });
    connect(opacity, &QSlider::valueChanged, this, [this](int v) {
        opacity->setToolTip(QString::number(v) + " %");
        if (refreshing || !currentDocument())
            return;
        auto *d = currentDocument();
        if (d->state.layers[d->state.active].locked)
            return;
        currentCanvas()->finishPendingGesture();
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
    auto *importButton = new QToolButton;
    importButton->setObjectName("addImageLayer");
    importButton->setDefaultAction(actions["import"]);
    // A native/fallback font can make the full action label wider than the
    // narrow inspector. Keep every word, and leave the toolbar's label alone.
    // QAction changes reapply iconText to a default-action button, so restore
    // this control's line break after availability/tooltip updates as well.
    const auto setImportLabel = [importButton] {
        importButton->setText("Görüntü katmanı\nekle…");
        importButton->setAccessibleName("Görüntü katmanı ekle…");
    };
    connect(actions["import"], &QAction::changed, importButton, setImportLabel);
    setImportLabel();
    importButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    importButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(importButton);
    auto *transformButton = new QToolButton;
    transformButton->setObjectName("quickTransform");
    transformButton->setDefaultAction(actions["transform"]);
    transformButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    transformButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    transformButton->setAttribute(Qt::WA_AlwaysShowToolTips);
    auto *toneButton = new QPushButton("Renk / ton…");
    toneButton->setObjectName("quickTones");
    connect(toneButton, &QPushButton::clicked, actions["tones"], &QAction::trigger);
    layout->addWidget(wrappingRow(transformButton, toneButton));
    auto *layerList = new LayerListWidget;
    layers = layerList;
    layers->setObjectName("layers");
    layers->setIconSize({50, 36});
    layers->setAccessibleName("Belge katmanları");
    layers->setMinimumHeight(130);
    layers->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layers->setTextElideMode(Qt::ElideRight);
    layers->setSpacing(3);
    layers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layers->setContextMenuPolicy(Qt::CustomContextMenu);
    layers->setToolTip("Sürükle: tek katmanı sırala · F2: yeniden adlandır · Sağ tık: işlemler");
    layers->installEventFilter(this);
    auto *nameDelegate = new LayerNameDelegate(layers);
    nameDelegate->targetForIndex = [this](const QModelIndex &index) {
        auto *d = currentDocument();
        const int layer = d ? d->state.layers.size() - 1 - index.row() : -1;
        if (!d || layer < 0 || layer >= d->state.layers.size())
            return LayerRenameTarget{};
        return LayerRenameTarget{d, layer, int(d->state.layers.size()), d->revision(),
                                 d->state.layers[layer].name};
    };
    nameDelegate->rename = [this](const LayerRenameTarget &target, const QString &value) {
        const QPointer<MainWindow> alive = this;
        const QPointer<Canvas> canvas = currentCanvas();
        if (canvas)
            canvas->finishPendingGesture();
        if (!alive || canvas != alive->currentCanvas())
            return;
        auto *d = alive->currentDocument();
        if (!d || d->isEditing() || target.document != d || d->revision() != target.revision ||
            d->state.layers.size() != target.count || target.index < 0 ||
            target.index >= d->state.layers.size() ||
            d->state.layers[target.index].name != target.name)
            return;
        const QString name = value == target.name ? target.name : value.trimmed().left(128);
        if (name.isEmpty() || name == target.name)
            return;
        d->beginEdit();
        d->state.layers[target.index].name = name;
        d->commit("Katman yeniden adlandırıldı");
    };
    layers->setItemDelegate(nameDelegate);
    layerList->displayIsCurrent = [this] {
        auto *d = currentDocument();
        const bool sameDocument = d && layers->count() == d->state.layers.size() &&
            layers->count() && layers->item(0)->data(LayerDocumentRole).value<Document *>() == d &&
            layers->item(0)->data(LayerCountRole).toInt() == d->state.layers.size();
        const bool current = sameDocument &&
            layers->item(0)->data(LayerRevisionRole).toULongLong() == d->revision() &&
            layers->item(0)->data(LayerStackRole).toByteArray() == layerStackKey(d);
        if (!current)
            refresh();
        return current;
    };
    layerList->prepareDrag = [this](bool finishGesture) {
        const QPointer<MainWindow> alive = this;
        const QPointer<Canvas> canvas = currentCanvas();
        const QPointer<Document> document = currentDocument();
        if (!canvas || !document)
            return false;
        const int active = document->state.active;
        if (active < 0 || active >= document->state.layers.size())
            return false;
        const Layer source = document->state.layers[active];
        const QByteArray order = layerStackKey(document, active);
        const QString name = static_cast<LayerNameDelegate *>(layers->itemDelegate())->pendingName();
        if (!finishGesture && name.isEmpty())
            return true;
        const bool gesture = canvas->hasPendingGesture(), transform = canvas->isTransforming();
        const bool painting = gesture && (canvas->tool == Canvas::Brush || canvas->tool == Canvas::Pencil ||
            canvas->tool == Canvas::Eraser || canvas->tool == Canvas::Clone ||
            canvas->tool == Canvas::Gradient || canvas->tool == Canvas::Rectangle ||
            canvas->tool == Canvas::Ellipse);
        const bool paintingMask = painting && document->editingMask;
        QSet<quint64> revisions;
        for (const auto &entry : document->historyDescriptions())
            revisions.insert(entry.revision);
        bool tabChanged = false;
        const auto tabConnection = connect(tabs, &QTabWidget::currentChanged, this,
                                           [&tabChanged] { tabChanged = true; });
        const auto disconnectTabs = qScopeGuard([tabConnection] { QObject::disconnect(tabConnection); });
        finishLayerRename();
        if (!alive || !canvas || !document || tabChanged || alive->currentCanvas() != canvas ||
            alive->currentDocument() != document)
            return false;
        if (finishGesture)
            canvas->finishPendingGesture();
        if (!alive || !canvas || !document || tabChanged || alive->currentCanvas() != canvas ||
            alive->currentDocument() != document)
            return false;
        bool sameRows = document->state.active == active &&
                        layerStackKey(document, active) == order;
        if (sameRows) {
            Layer settled = document->state.layers[active];
            // Permit only fields the pending operation can actually settle. The
            // original row must not turn into an unrelated replacement layer.
            if (!name.isEmpty() && settled.name == name)
                settled.name = source.name;
            if (transform)
                settled.geometry = source.geometry;
            if (painting) {
                if (paintingMask)
                    settled.mask = source.mask;
                else {
                    settled.pixels = source.pixels;
                    settled.text = source.text;
                }
            }
            sameRows = layerIdentityKey(settled) == layerIdentityKey(source);
        }
        int newRevisions = 0;
        for (const auto &entry : document->historyDescriptions())
            newRevisions += !revisions.contains(entry.revision);
        sameRows &= newRevisions <= int(!name.isEmpty()) + int(gesture);
        alive->refresh();
        return alive && sameRows;
    };
    layerList->captureTarget = [this](int row) {
        auto *d = currentDocument();
        auto *canvas = currentCanvas();
        const int index = d ? int(d->state.layers.size()) - 1 - row : -1;
        if (!d || !canvas || index < 0 ||
            index >= d->state.layers.size())
            return LayerDragTarget{};
        return LayerDragTarget{d, canvas, index, int(d->state.layers.size()), d->state.active,
                               d->revision(), d->editingMask, canvas->hasPendingGesture(), d->isEditing(),
                               canvas->isTransforming() ? canvas->transformGeometry() : QTransform{},
                               layerStackKey(d)};
    };
    layerList->pressIsCurrent = [layerList](const LayerDragTarget &target) {
        const auto current = layerList->captureTarget(target.count - 1 - target.index);
        return current.document == target.document && current.canvas == target.canvas &&
               current.index == target.index && current.count == target.count &&
               current.active == target.active && current.revision == target.revision &&
               current.editingMask == target.editingMask && current.pendingGesture == target.pendingGesture &&
               current.editing == target.editing && current.pendingTransform == target.pendingTransform &&
               current.stackKey == target.stackKey;
    };
    layerList->targetIsCurrent = [this](const LayerDragTarget &target) {
        const auto *d = currentDocument();
        return d && target.document == d && target.canvas == currentCanvas() &&
               !d->isEditing() && !target.canvas->hasPendingGesture() &&
               !static_cast<LayerNameDelegate *>(layers->itemDelegate())->isRenaming() &&
               target.revision == d->revision() && target.count == d->state.layers.size() &&
               target.index >= 0 && target.index < target.count &&
               target.active == d->state.active && target.editingMask == d->editingMask &&
               target.stackKey == layerStackKey(d);
    };
    layerList->moveLayer = [this, layerList](const LayerDragTarget &target, int to) {
        if (!layerList->targetIsCurrent(target))
            return false;
        auto *d = target.document.data();
        const QPointer<MainWindow> alive = this;
        const bool moved = target.index == to || d->reorderLayer(target.index, to);
        if (alive)
            alive->refresh();
        return moved;
    };
    connect(layers, &QListWidget::customContextMenuRequested, this,
            [this](QPoint position) { showLayerMenu(position); });
    layout->addWidget(layers, 1);
    connect(layers, &QListWidget::currentRowChanged, this, [this](int r) {
        if (refreshing || r < 0 || !currentDocument())
            return;
        static_cast<LayerListWidget *>(layers)->invalidateDrag();
        const QPointer<MainWindow> alive = this;
        const QPointer<Canvas> canvas = currentCanvas();
        const QPointer<Document> document = currentDocument();
        canvas->finishPendingGesture();
        if (!alive || !canvas || !document || alive->currentCanvas() != canvas ||
            alive->currentDocument() != document || r >= document->state.layers.size())
            return;
        document->state.active = document->state.layers.size() - 1 - r;
        document->editingMask = false;
        alive->refresh();
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
        const auto &layer = currentDocument()->state.layers[currentDocument()->state.active];
        if (!layer.text.isEmpty() && !layer.locked)
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
        b->setMinimumSize(30, 32);
        b->setIconSize({18, 18});
        b->setMaximumHeight(36);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        bl->addWidget(b);
    }
    layout->addWidget(buttons);
    maskButton = new QToolButton;
    maskButton->setDefaultAction(actions["mask"]);
    maskButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    maskButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    maskButton->setObjectName("layerMaskButton");
    layout->addWidget(maskButton);
    auto *propertiesTitle = new QLabel("KATMAN ÖZELLİKLERİ");
    propertiesTitle->setObjectName("sectionTitle");
    propertiesTitle->setWordWrap(true);
    layout->addWidget(propertiesTitle);
    layerDetails = new QLabel;
    layerDetails->setWordWrap(true);
    layerDetails->setObjectName("layerDetails");
    layout->addWidget(layerDetails);
    auto *positionButton = new QToolButton;
    positionButton->setObjectName("quickPosition");
    positionButton->setDefaultAction(actions["positionLayer"]);
    positionButton->setText("Konum…");
    positionButton->setAccessibleName("Etkin katmanın X ve Y konumunu düzenle");
    positionButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    positionButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *alignButton = new QToolButton;
    alignButton->setObjectName("quickAlign");
    alignButton->setText("Hizala");
    alignButton->setAccessibleName("Etkin katmanı tuvale veya seçim sınırlarına hizala");
    alignButton->setMenu(findChild<QMenu *>("layerAlignmentMenu"));
    alignButton->setPopupMode(QToolButton::InstantPopup);
    alignButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(wrappingRow(positionButton, alignButton));
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
    // Handle mouse activation before moving focus, so an inline rename cannot
    // commit a new branch before we have captured/validated the requested ID.
    history->setFocusPolicy(Qt::TabFocus);
    history->setAccessibleName("Belge geçmişi");
    history->setToolTip("Tıkla: bu adıma dön · Ok tuşları: adım seç · Enter / Boşluk: uygula");
    history->setSelectionMode(QAbstractItemView::SingleSelection);
    history->setEditTriggers(QAbstractItemView::NoEditTriggers);
    history->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    history->setTextElideMode(Qt::ElideRight);
    history->installEventFilter(this);
    history->viewport()->installEventFilter(this);
    history->setMinimumHeight(70);
    layout->addWidget(history);
    historyDock = createDock("Geçmiş", "historyDock", panel);
    addDockWidget(Qt::RightDockWidgetArea, historyDock);
}

void MainWindow::buildWorkflowBanners() {
    auto *workspaceLayout = qobject_cast<QVBoxLayout *>(workspace->layout());
    paintBlockedBanner = new QFrame;
    paintBlockedBanner->setObjectName("paintBlockedBanner");
    paintBlockedBanner->setFrameShape(QFrame::StyledPanel);
    auto *paintLayout = new QVBoxLayout(paintBlockedBanner);
    paintLayout->setContentsMargins(9, 6, 9, 6);
    paintLayout->setSpacing(5);
    paintBlockedReason = new QLabel;
    paintBlockedReason->setObjectName("paintBlockedReason");
    paintBlockedReason->setTextFormat(Qt::PlainText);
    paintBlockedReason->setWordWrap(true);
    paintLayout->addWidget(paintBlockedReason);
    auto *rasterize = new QToolButton;
    rasterize->setObjectName("paintRasterize");
    rasterize->setDefaultAction(actions["rasterize"]);
    rasterize->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto *paint = new QToolButton;
    paint->setObjectName("newPaintLayer");
    paint->setDefaultAction(actions["newPaintLayer"]);
    paint->setToolButtonStyle(Qt::ToolButtonTextOnly);
    paintLayout->addWidget(wrappingRow(rasterize, paint));
    workspaceLayout->insertWidget(0, paintBlockedBanner);
    paintBlockedBanner->hide();

    layerImportHelp = new QFrame;
    layerImportHelp->setObjectName("layerImportHelp");
    layerImportHelp->setFrameShape(QFrame::StyledPanel);
    auto *helpLayout = new QHBoxLayout(layerImportHelp);
    helpLayout->setContentsMargins(9, 6, 9, 6);
    auto *help = new QLabel(
        "Görüntü katmanı ekle → Hedef katmanı seç → Ctrl+T ile boyutlandır. "
        "Aç, görüntüyü ayrı belge olarak açar.");
    help->setTextFormat(Qt::PlainText);
    help->setWordWrap(true);
    helpLayout->addWidget(help, 1);
    auto *dismiss = new QToolButton;
    dismiss->setObjectName("dismissLayerImportHelp");
    dismiss->setText("×");
    dismiss->setAccessibleName("Katman ekleme yardımını kapat");
    dismiss->setToolTip("Bu yardımı kapat; tekrar gösterilmez");
    helpLayout->addWidget(dismiss);
    connect(dismiss, &QToolButton::clicked, this, [this] {
        layerImportHelpVisible = false;
        layerImportHelp->hide();
    });
    workspaceLayout->insertWidget(0, layerImportHelp);
    layerImportHelp->hide();
    if (persistSettings) {
        QSettings settings(uiSettingsPath(), QSettings::IniFormat);
        layerImportHelpSeen = settings.value("workflow/layerImportHelpSeen", false).toBool();
    }
}

void MainWindow::showLayerImportHelp() {
    if (layerImportHelpSeen || !layerImportHelp)
        return;
    layerImportHelpSeen = layerImportHelpVisible = true;
    layerImportHelp->show();
    if (persistSettings) {
        QSettings settings(uiSettingsPath(), QSettings::IniFormat);
        settings.setValue("workflow/layerImportHelpSeen", true);
        settings.sync();
    }
}

void MainWindow::updatePaintAvailability() {
    if (!paintBlockedBanner)
        return;
    const auto *canvas = currentCanvas();
    const auto *document = currentDocument();
    const bool present = canvas && document && !document->state.layers.isEmpty();
    const bool painting = present && !canvas->isTransforming() &&
                          (canvas->tool == Canvas::Brush || canvas->tool == Canvas::Pencil ||
                           canvas->tool == Canvas::Eraser || canvas->tool == Canvas::Clone ||
                           canvas->tool == Canvas::Fill || canvas->tool == Canvas::Gradient ||
                           canvas->tool == Canvas::Rectangle || canvas->tool == Canvas::Ellipse);
    QString reason;
    if (painting) {
        const auto &layer = document->state.layers[document->state.active];
        if (layer.locked)
            reason = QString("“%1” kilitli. Boyamak için kilidi açın veya yeni boş katmana geçin.")
                         .arg(layer.name);
        else if (!layer.visible)
            reason = QString("“%1” gizli. Boyamak için katmanı görünür yapın veya yeni boş katmana geçin.")
                         .arg(layer.name);
        else if (layer.isSmartObject() && !document->editingMask)
            reason = QString("“%1” bir akıllı nesne. Gömülü kaynağı korumak için İçeriği değiştir kullanın "
                             "veya yeni boş katmana boyayın. Görünümü rasterleştir, gömülü kaynak "
                             "korumasını ve akıllı nesne düzenlenebilirliğini kaldırır; Geri al geri yükler.")
                         .arg(layer.name);
        else if (!layer.geometry.isIdentity())
            reason = QString("“%1” kaynak koruyan dönüşüm içeriyor. Piksel veya maske boyamak için "
                             "görünümü rasterleştirin ya da yeni boş katmana geçin.")
                         .arg(layer.name);
    }
    paintBlockedReason->setText(reason);
    paintBlockedBanner->setAccessibleName(reason);
    paintBlockedBanner->setVisible(!reason.isEmpty());
    optionsToolbar->findChild<QWidget *>("brushControls")->setEnabled(reason.isEmpty());
    optionsToolbar->findChild<QWidget *>("flowControls")->setEnabled(reason.isEmpty());
    layerImportHelp->setVisible(present && layerImportHelpVisible && reason.isEmpty());
}

void MainWindow::updateLayerDetails() {
    const auto *canvas = currentCanvas();
    const auto *document = currentDocument();
    if (!canvas || !document || document->state.layers.isEmpty())
        return;
    // Copy only the displayed layer. Sharing the complete live layer list here can
    // detach it during downstream UI reads and invalidate refresh() references.
    const Layer active = [canvas, document] {
        if (canvas->isTransforming()) {
            const State displayed = canvas->pendingTransformState();
            return displayed.layers.at(displayed.active);
        }
        return document->state.layers.at(document->state.active);
    }();
    const QSize source = canvas->activeLayerSourceSize();
    const QRect bounds = canvas->activeLayerDisplayBounds();
    QString description = QString("Hedef: %1\n%8: %2 × %3 px\n"
                                  "Görünüm: %4 × %5 px (tuvale hizalı sınır)\nKonum: %6, %7")
                              .arg(canvas->activeLayerTargetName())
                              .arg(source.width()).arg(source.height())
                              .arg(bounds.width()).arg(bounds.height())
                              .arg(bounds.x()).arg(bounds.y())
                              .arg(active.isSmartObject() ? "Sabit çerçeve" : "Kaynak");
    if (active.isSmartObject())
        description += QString("\nAKILLI NESNE · Gömülü kaynak: %1 × %2 px\n"
                               "İçeriği değiştir: yeni görüntü sabit çerçeveyi dolduracak şekilde esnetilir.")
                           .arg(active.pixels.width()).arg(active.pixels.height());
    if (document->editingMask)
        description += "\nMASKE · Beyaz gösterir, siyah gizler. X: renkleri değiştir.";
    else if (!active.text.isEmpty() && !canvas->isTransforming())
        description += "\nDüzenlenebilir metin";
    layerDetails->setTextFormat(Qt::PlainText);
    layerDetails->setText(description);
    layerDetails->setToolTip(description);
}

void MainWindow::addDocument(Document *d) {
    d->setParent(this);
    recoveryManager->registerDocument(d);
    auto *canvas = new Canvas(d);
    canvas->showRulers = appearance.rulers;
    canvas->setAutoSelectLayers(true);
    {
        auto p = appearance.resolved();
        canvas->setUiColors(p.canvas, p.panel, p.muted, p.accent);
    }
    tabs->addTab(canvas, d->title);
    tabs->setCurrentWidget(canvas);
    connect(d, &Document::updated, this, [this, d] {
        static_cast<LayerListWidget *>(layers)->invalidateDrag();
        scheduleRecovery(d);
        updateActionAvailability();
        if (!refreshTimer.isActive())
            refreshTimer.start();
    });
    connect(canvas, &Canvas::zoomChanged, this, [this] {
        if (currentCanvas())
            zoomLabel->setText(QString::number(qRound(currentCanvas()->zoom * 100)) + " %  ");
    });
    connect(canvas, &Canvas::toolHint, status, &QLabel::setText);
    connect(canvas, &Canvas::transformSessionChanged, this, [this](bool) {
        static_cast<LayerListWidget *>(layers)->invalidateDrag();
        updateActionAvailability();
        updateToolOptions();
        updateLayerDetails();
    });
    connect(canvas, &Canvas::transformPreviewChanged, this, [this, canvas] {
        if (currentCanvas() == canvas) {
            updateActionAvailability();
            updateLayerDetails();
        }
    });
    connect(canvas, &Canvas::colorPicked, this, [this, canvas](QColor c) {
        canvas->foreground = c;
        refresh();
    });
    connect(canvas, &Canvas::textRequested, this, [this](QPoint p) { textDialog(p); });
    QTimer::singleShot(0, canvas, &Canvas::fit);
    scheduleRecovery(d);
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
MainWindow::~MainWindow() {
    // The delegate owns callbacks into this window; close its editor before our
    // member containers are destroyed and QWidget teardown can move focus.
    auto *layerList = static_cast<LayerListWidget *>(layers);
    layerList->invalidateDrag();
    layerList->displayIsCurrent = {};
    layerList->prepareDrag = {};
    layerList->captureTarget = {};
    layerList->pressIsCurrent = {};
    layerList->targetIsCurrent = {};
    layerList->moveLayer = {};
    auto *nameDelegate = static_cast<LayerNameDelegate *>(layers->itemDelegate());
    nameDelegate->cancelIfStale(nullptr);
    nameDelegate->targetForIndex = {};
    nameDelegate->rename = {};
    refreshTimer.stop();
    autosaveTimer.stop();
    recoverySaveTimer.stop();
    uiSaveTimer.stop();
    // QWidget base-class teardown can emit visibility signals after our member containers die.
    for (auto *sender : findChildren<QObject *>())
        QObject::disconnect(sender, nullptr, this, nullptr);
    QObject::disconnect(&refreshTimer, nullptr, this, nullptr);
    QObject::disconnect(&autosaveTimer, nullptr, this, nullptr);
    QObject::disconnect(&recoverySaveTimer, nullptr, this, nullptr);
    QObject::disconnect(&uiSaveTimer, nullptr, this, nullptr);
    // Reject publication and join before any GUI/model members are torn down. Direct
    // destruction is also used as crash simulation: preserve the last published file.
    delete recoveryManager;
    recoveryManager = nullptr;
}
void MainWindow::updateActionAvailability() {
    const auto *d = currentDocument();
    bool present = d && !d->state.layers.isEmpty();
    for (auto it = actions.begin(); it != actions.end(); ++it)
        if (it.key() != "new" && it.key() != "open" && it.key() != "import" &&
            it.key() != "quit" && it.key() != "demo" &&
            it.key() != "help" && it.key() != "about" && it.key() != "batch" &&
            it.key() != "brushLibrary" && !it.key().startsWith("ui."))
            it.value()->setEnabled(present);

    const bool activePresent = present && d->state.active >= 0 && d->state.active < d->state.layers.size();
    const bool positionable = activePresent && !d->state.layers[d->state.active].locked &&
                              d->state.layers[d->state.active].visible && !d->activeLayerBounds().isEmpty();
    actions["positionLayer"]->setEnabled(positionable);
    for (auto it = actions.begin(); it != actions.end(); ++it)
        if (it.key().startsWith("alignCanvas") || it.key().startsWith("alignSelection"))
            it.value()->setEnabled(positionable &&
                (!it.key().startsWith("alignSelection") || d->selection.hasPixels()));
    if (auto *menu = findChild<QMenu *>("alignSelectionMenu"))
        menu->menuAction()->setEnabled(positionable && d->selection.hasPixels());
    if (auto *menu = findChild<QMenu *>("alignCanvasMenu"))
        menu->menuAction()->setEnabled(positionable);
    if (auto *button = findChild<QToolButton *>("quickAlign"))
        button->setEnabled(positionable);

    const bool transforming = present && currentCanvas()->isTransforming();
    actions["undo"]->setText(transforming ? "Dönüşüm önizlemesini iptal et" : "Geri al");
    actions["redo"]->setText(transforming ? "Dönüşüm önizlemesini iptal et" : "Yinele");
    actions["undo"]->setEnabled(present && (d->canUndo() || transforming));

    actions["deselect"]->setEnabled(present && !d->selection.isEmpty());
    actions["reselect"]->setEnabled(present && d->canReselect());
    actions["saveSelection"]->setEnabled(present && !d->selection.isEmpty());
    actions["loadSelection"]->setEnabled(present && !d->savedSelectionNames().isEmpty());
    actions["removeSavedSelection"]->setEnabled(present && !d->savedSelectionNames().isEmpty());
    actions["invertSelection"]->setEnabled(present && !d->selection.isEmpty());
    actions["featherSelection"]->setEnabled(present && !d->selection.isEmpty());
    actions["growSelection"]->setEnabled(present && !d->selection.isEmpty());
    actions["shrinkSelection"]->setEnabled(present && !d->selection.isEmpty());
    actions["crop"]->setEnabled(present && d->selection.hasPixels());
    actions["tones"]->setEnabled(present && !d->state.layers[d->state.active].locked);
    actions["photoFilters"]->setEnabled(present && !d->state.layers[d->state.active].locked);
    actions["resetFilters"]->setEnabled(present && !d->state.layers[d->state.active].locked);
    actions["maskSettings"]->setEnabled(present && !d->state.layers[d->state.active].locked &&
                                        !d->state.layers[d->state.active].mask.isNull());
    QString transformReason;
    if (!activePresent)
        transformReason = "Dönüştürmek için önce bir belge açın veya oluşturun.";
    else if (d->state.layers[d->state.active].locked)
        transformReason = "Dönüştürmek için önce katmanın kilidini açın.";
    else if (!d->state.layers[d->state.active].visible)
        transformReason = "Dönüştürmek için önce katmanı görünür yapın.";
    else if (!d->state.layers[d->state.active].validGeometry())
        transformReason = "Bu katmanın geometrisi geçersiz; dönüşüm başlatılamıyor.";
    for (const auto &id : {"transform", "transformAdvanced"}) {
        actions[id]->setEnabled(transformReason.isEmpty());
        actions[id]->setToolTip(
            transformReason.isEmpty()
                ? (id == QString("transform")
                       ? "Etkin katmanın tamamını kaynak korunarak dönüştürür. Seçim kapsamı dönüşüm alanında gösterilir. Ctrl+T"
                       : "Etkin katmanın sayısal ölçek, açı ve perspektif ayarlarını açar. Ctrl+Alt+T")
                : transformReason);
        actions[id]->setStatusTip(actions[id]->toolTip());
    }
    const Layer *activeLayer = activePresent ? &d->state.layers[d->state.active] : nullptr;
    QString smartReason;
    if (!activeLayer)
        smartReason = "Önce bir belge açın veya oluşturun.";
    else if (activeLayer->locked)
        smartReason = "Önce katmanın kilidini açın.";
    else if (!activeLayer->visible)
        smartReason = "Önce katmanı görünür yapın.";
    QString convertReason = smartReason;
    if (convertReason.isEmpty() && activeLayer->isSmartObject())
        convertReason = "Bu katman zaten bir akıllı nesne.";
    else if (convertReason.isEmpty() && !activeLayer->text.isEmpty())
        convertReason = "Yalnızca piksel katmanları akıllı nesneye dönüştürülebilir. Metin için önce görünümü rasterleştirin.";
    actions["convertSmartObject"]->setEnabled(convertReason.isEmpty());
    actions["convertSmartObject"]->setToolTip(convertReason.isEmpty()
        ? "Piksel kaynağını projeye gömer; çerçeve, maske ve dönüşüm korunur. Kaynağa doğrudan boyanmaz."
        : convertReason);
    QString replaceReason = smartReason;
    if (replaceReason.isEmpty() && !activeLayer->isSmartObject())
        replaceReason = "Önce piksel katmanını akıllı nesneye dönüştürün.";
    if (replaceReason.isEmpty() && transforming) {
        const State pending = currentCanvas()->pendingTransformState();
        if (pending.active < 0 || pending.active >= pending.layers.size() ||
            !pending.layers[pending.active].isSmartObject())
            replaceReason = "Seçim kopyası bir piksel katmanıdır. İçeriği değiştir yalnızca akıllı nesne hedefinde kullanılabilir. Esc özgün katmana döner.";
    }
    actions["replaceSmartObjectContents"]->setEnabled(replaceReason.isEmpty());
    actions["replaceSmartObjectContents"]->setToolTip(replaceReason.isEmpty()
        ? "Yeni görüntüyü özgün çözünürlüğünde gömer; sabit çerçeveyi dolduracak şekilde esnetir. Konum, dönüşüm, maske ve efektler korunur."
        : replaceReason);
    for (const auto &id : {"convertSmartObject", "replaceSmartObjectContents"})
        actions[id]->setStatusTip(actions[id]->toolTip());
    for (const auto &id : {"resetTransform", "rasterize"})
        actions[id]->setEnabled(activePresent && !d->state.layers[d->state.active].locked &&
                                d->state.layers[d->state.active].visible);
    actions["rasterize"]->setToolTip(
        !activePresent ? "Önce bir belge açın veya oluşturun."
        : d->state.layers[d->state.active].locked ? "Önce katmanın kilidini açın."
        : !d->state.layers[d->state.active].visible ? "Önce katmanı görünür yapın."
        : d->state.layers[d->state.active].isSmartObject()
            ? "Görünümü piksele çevirir; akıllı nesne kimliği, gömülü kaynak ve içeriği değiştirme olanağı kaldırılır. Geri al geri yükler. Önce onayınız istenir."
            : "Görünümü piksele çevirir: dönüşüm, maske, renk ve filtre ayarları piksellere işlenir; metin düzenlenebilirliği kalkar. Önce onayınız istenir.");
    actions["import"]->setEnabled(true);
    actions["import"]->setToolTip(
        present
            ? "Açık belgeye görüntüyü ayrı katman olarak ekler; önceki katmanları korur. Ctrl+Shift+O"
            : "Belge yoksa görüntü yeni belge olarak açılır. Ctrl+Shift+O");
    actions["redo"]->setEnabled(present && (d->canRedo() || transforming));
    actions["deleteLayer"]->setEnabled(present && d->state.layers.size() > 1);
    actions["layerUp"]->setEnabled(present && d->state.active + 1 < d->state.layers.size());
    actions["layerDown"]->setEnabled(present && d->state.active > 0);
    actions["clipLayer"]->setEnabled(present && !d->state.layers[d->state.active].locked &&
                                     (d->state.active > 0 || d->state.layers[d->state.active].clipped));
    actions["mergeDown"]->setEnabled(present && d->state.active > 0);
    actions["selectionCutLayer"]->setEnabled(present && d->selection.hasPixels() &&
                                             !d->state.layers[d->state.active].locked);
    actions["selectionCopyLayer"]->setEnabled(present);
    actions["editText"]->setEnabled(present && !d->state.layers[d->state.active].text.isEmpty() &&
                                    !d->state.layers[d->state.active].locked);
    actions["mask"]->setEnabled(present && !d->state.layers[d->state.active].locked);
    actions["removeMask"]->setEnabled(present && !d->state.layers[d->state.active].mask.isNull() &&
                                      !d->state.layers[d->state.active].locked);
    actions["invertMask"]->setEnabled(present && !d->state.layers[d->state.active].mask.isNull() &&
                                      !d->state.layers[d->state.active].locked);
}
void MainWindow::refreshHistory() {
    auto *d = currentDocument();
    const bool changedDocument = historyDocument != d;
    const quint64 selected = !changedDocument && history->currentItem()
                                 ? history->currentItem()->data(Qt::UserRole).toULongLong()
                                 : 0;
    const QSignalBlocker blocker(history);
    if (changedDocument) {
        history->clear();
        historyDocument = d;
    }
    history->setEnabled(d != nullptr);
    const auto descriptions = d ? d->historyDescriptions() : QVector<Document::HistoryDescription>{};
    QSet<quint64> retained;
    for (const auto &entry : descriptions)
        retained.insert(entry.revision);
    for (int row = history->count() - 1; row >= 0; --row)
        if (!retained.contains(history->item(row)->data(Qt::UserRole).toULongLong()))
            delete history->takeItem(row);
    int selectedRow = -1;
    const int current = d ? d->currentHistoryIndex() : -1;
    for (int row = 0; row < descriptions.size(); ++row) {
        const auto &entry = descriptions[row];
        // Existing entries retain their objects, selection and scroll position.
        auto *item = row < history->count() ? history->item(row) : new QListWidgetItem(history);
        item->setData(Qt::UserRole, QVariant::fromValue(entry.revision));
        const bool active = row == current, future = row > current;
        const QString state = active ? "Geçerli adım" : future ? "Yineleme adımı" : "Önceki adım";
        item->setText((active ? "●  " : future ? "›  " : "    ") + entry.label +
                      (active ? " · Geçerli" : future ? " · Yinele" : ""));
        QFont font = history->font();
        font.setBold(active);
        font.setItalic(future);
        item->setFont(font);
        // Redo states stay enabled and use normal readable text in every theme.
        item->setForeground(history->palette().brush(QPalette::Text));
        item->setToolTip(entry.label + "\n" + state + " · Tıkla veya Enter / Boşluk ile uygula");
        item->setData(Qt::AccessibleTextRole, entry.label + ", " + state);
        if (entry.revision == selected)
            selectedRow = row;
    }
    // Keep an arrow-key browsing target through unrelated delayed refreshes.
    // Outside keyboard browsing, selection follows the actual current state.
    const int row = history->hasFocus() && selectedRow >= 0 ? selectedRow : current;
    if (history->currentRow() != row) {
        history->setCurrentRow(row);
        if (row >= 0)
            history->scrollToItem(history->item(row));
    }
}
bool MainWindow::navigateHistory(Document *document, quint64 revision) {
    QPointer<Document> target = document;
    if (!target || target != currentDocument() || target->revision() == revision)
        return false;
    const auto descriptions = target->historyDescriptions();
    const bool retained =
        std::any_of(descriptions.cbegin(), descriptions.cend(),
                    [revision](const auto &entry) { return entry.revision == revision; });
    if (!retained) {
        status->setText("Bu geçmiş adımı artık tutulmuyor.");
        refresh();
        return false;
    }
    // History navigation discards an unsubmitted name. Do this before focus can
    // commit it; the name editor does not own a live Document edit.
    static_cast<LayerNameDelegate *>(layers->itemDelegate())->cancelIfStale(nullptr);
    auto *canvas = currentCanvas();
    if (canvas->isTransforming())
        canvas->cancelTransform();
    else
        canvas->finishPendingGesture();
    // Finishing a brush/selection can trim history or replace its redo branch.
    // Resolve the original ID again, never an old row number.
    const bool jumped = target && target == currentDocument() && target->jumpToHistoryRevision(revision);
    if (!jumped)
        status->setText("İşlem tamamlandı; seçilen geçmiş adımı artık tutulmuyor.");
    refresh();
    return jumped;
}
void MainWindow::refresh() {
    refreshing = true;
    auto *d = currentDocument();
    auto *c = currentCanvas();
    static_cast<LayerNameDelegate *>(layers->itemDelegate())->cancelIfStale(d);
    bool present = d;
    updateActionAvailability();
    for (auto *s : {brightness, contrast, saturation, opacity})
        s->setEnabled(present);
    blend->setEnabled(present);
    maskButton->setEnabled(present);
    refreshHistory();
    for (int i = 0; i < tabs->count(); ++i) {
        auto *td = qobject_cast<Canvas *>(tabs->widget(i))->document;
        tabs->setTabText(i, td->title + (td->dirty ? " •" : ""));
    }
    if (!d) {
        layers->clear();
        filmstrip->clear();
        histogram->setImage({});
        zoomLabel->clear();
        setWindowTitle("PhotoDeck");
        info->clear();
        layerDetails->setText("Dosya → Yeni belge ile başlayın.");
        colorButton->setStyleSheet("");
        updateToolOptions();
        refreshing = false;
        return;
    }
    setWindowTitle(d->title + (d->dirty ? " *" : "") + " — PhotoDeck");
    auto &active = d->state.layers[d->state.active];
    brightness->setValue(active.brightness);
    contrast->setValue(active.contrast);
    saturation->setValue(active.saturation);
    opacity->setValue(qRound(active.opacity * 100));
    blend->setCurrentIndex(active.blend);
    for (auto *s : {brightness, contrast, saturation, opacity})
        s->setEnabled(!active.locked);
    blend->setEnabled(!active.locked);
    auto &previews = previewCaches[d];
    previews.layerKeys.resize(d->state.layers.size());
    previews.layerThumbnails.resize(d->state.layers.size());
    while (layers->count() > d->state.layers.size())
        delete layers->takeItem(layers->count() - 1);
    for (int i = d->state.layers.size() - 1; i >= 0; --i) {
        auto &l = d->state.layers[i];
        const QByteArray key = layerPreviewKey(l) + appearance.resolved().raised.name().toUtf8();
        // Canvas owns the live edit preview. Auxiliary panels catch up once the gesture commits.
        if (previews.layerThumbnails[i].isNull() ||
            (!d->isEditing() && previews.layerKeys[i] != key)) {
            QPixmap thumb(50, 36);
            thumb.fill(appearance.resolved().raised);
            QPainter p(&thumb);
            QImage preview = l.rendered().scaled(50, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage((50 - preview.width()) / 2, (36 - preview.height()) / 2, preview);
            p.end();
            previews.layerThumbnails[i] = QIcon(thumb);
            previews.layerKeys[i] = key;
        }
        const int row = d->state.layers.size() - 1 - i;
        auto *item = row < layers->count() ? layers->item(row) : new QListWidgetItem(layers);
        if (item->icon().cacheKey() != previews.layerThumbnails[i].cacheKey())
            item->setIcon(previews.layerThumbnails[i]);
        QStringList details{l.isSmartObject() ? "Akıllı nesne" : l.text.isEmpty() ? "Piksel" : "Metin"};
        if (!l.mask.isNull())
            details << (d->editingMask && i == d->state.active ? "Maske düzenleniyor" : "Maske");
        if (l.locked)
            details << "Kilitli";
        if (l.clipped)
            details << "Kırpılmış";
        const QString description = details.join(" · ");
        const QString displayName = (l.clipped ? "↳  " : "") + l.name;
        item->setText(displayName + "\n" + description);
        item->setData(LayerNameRole, displayName);
        item->setData(LayerStatusRole, description);
        item->setData(LayerDocumentRole, QVariant::fromValue(d));
        item->setData(LayerRevisionRole, QVariant::fromValue<qulonglong>(d->revision()));
        item->setData(LayerCountRole, int(d->state.layers.size()));
        const QString doubleClick = !l.text.isEmpty() && !l.locked
                                        ? "Çift tık: metni düzenle"
                                        : "Çift tık: yeniden adlandır";
        QString hint = l.name + "\n" + description + "\n" + doubleClick +
                       " · Sürükle: sırala · F2: yeniden adlandır · Sağ tık: katman işlemleri";
        if (l.isSmartObject())
            hint += QString("\nGömülü kaynak: %1 × %2 px · Sabit çerçeve: %3 × %4 px. "
                            "İçeriği değiştir, konumu ve dönüşümü korur.")
                        .arg(l.pixels.width()).arg(l.pixels.height())
                        .arg(l.sourceSize().width()).arg(l.sourceSize().height());
        if (l.locked)
            hint += "\nKilit, piksel/metin/maske düzenlemesini engeller. Ad ve görünürlük değişebilir; sıra da değiştirilebilir.";
        if (l.clipped)
            hint += "\nAltındaki kırpılmamış tabanın alfasını kullanır. Sıra değişikliği görünümü "
                    "ve kırpma ilişkisini değiştirebilir.";
        item->setToolTip(hint);
        item->setData(Qt::AccessibleTextRole, l.name + ", " + description);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        item->setCheckState(l.visible ? Qt::Checked : Qt::Unchecked);
        item->setSizeHint({180, qMax(54, appearance.fontSize * 2 + 28)});
    }
    layers->item(0)->setData(LayerStackRole, layerStackKey(d));
    layers->setCurrentRow(d->state.layers.size() - 1 - d->state.active);
    updateLayerDetails();
    maskButton->setText(active.mask.isNull() ? "◧  Katman\nmaskesi ekle"
                        : d->editingMask     ? "◧  Piksele\ngeri dön"
                                             : "◧  Maskeyi\ndüzenle");
    maskButton->setAccessibleName(active.mask.isNull() ? "Katman maskesi ekle"
                                  : d->editingMask     ? "Piksele geri dön"
                                                       : "Maskeyi düzenle");
    maskButton->setEnabled(!active.locked);
    auto ensureCompositePreview = [this](Document *document) -> PreviewCache & {
        auto &cached = previewCaches[document];
        const QByteArray key = compositePreviewKey(document);
        if (cached.histogramSample.isNull() || (!document->isEditing() && cached.compositeKey != key)) {
            const QImage composite = document->composite();
            cached.histogramSample = composite.scaled(256, 160, Qt::KeepAspectRatio,
                                                       Qt::FastTransformation);
            cached.filmThumbnail = QIcon(QPixmap::fromImage(composite.scaled(
                96, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            cached.compositeKey = key;
        }
        return cached;
    };
    if (colorDock && !colorDock->isHidden())
        histogram->setImage(ensureCompositePreview(d).histogramSample);
    info->setText(QString("%1 × %2 px  ·  %3 katman  ·  %4 MiB piksel  ")
                      .arg(d->state.size.width())
                      .arg(d->state.size.height())
                      .arg(d->state.layers.size())
                      .arg(d->imageBytes() / 1048576.0, 0, 'f', 1));
    zoomLabel->setText(QString::number(qRound(c->zoom * 100)) + " %  ");
    colorButton->setStyleSheet("QToolButton {background:" + c->foreground.name() + ";border:2px solid " +
                               appearance.resolved().line.name() + ";border-radius:7px;}");
    hardnessInput->setEnabled(c->brush.tipMask.isNull());
    sizeInput->setValue(c->brush.size);
    hardnessInput->setValue(c->brush.hardness);
    brushOpacityInput->setValue(c->brush.flow);
    {
        QSignalBlocker modeBlock(selectionModeInput), toleranceBlock(selectionToleranceInput),
            contiguousBlock(contiguousSelectionInput);
        selectionModeInput->setCurrentIndex(c->selectionMode);
        selectionToleranceInput->setValue(c->selectionTolerance);
        contiguousSelectionInput->setChecked(c->contiguousSelection);
    }
    selectionModeInput->setVisible(!c->isTransforming() && c->isSelectionTool());
    selectionToleranceInput->setVisible(!c->isTransforming() && c->tool == Canvas::Wand);
    contiguousSelectionInput->setVisible(!c->isTransforming() && c->tool == Canvas::Wand);
    actions["tool" + QString::number(c->tool)]->setChecked(true);
    if (filmDock && !filmDock->isHidden()) {
        QSignalBlocker block(filmstrip);
        while (filmstrip->count() > tabs->count())
            delete filmstrip->takeItem(filmstrip->count() - 1);
        for (int i = 0; i < tabs->count(); ++i) {
            auto *doc = qobject_cast<Canvas *>(tabs->widget(i))->document;
            const auto &cached = ensureCompositePreview(doc);
            auto *item = i < filmstrip->count() ? filmstrip->item(i) : new QListWidgetItem(filmstrip);
            if (item->icon().cacheKey() != cached.filmThumbnail.cacheKey())
                item->setIcon(cached.filmThumbnail);
            item->setText(doc->title);
            item->setSizeHint({120, 90});
        }
        filmstrip->setCurrentRow(tabs->currentIndex());
    }
    updateToolOptions();
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
    QStringList files =
        QFileDialog::getOpenFileNames(this, "Görüntü veya proje aç", {},
                                      "Görüntü / PhotoDeck (*.pdeck *.png *.jpg *.jpeg *.webp *.bmp "
                                      "*.gif *.tif *.tiff);;Tüm dosyalar (*)");
    for (const auto &file : files)
        openFile(file);
}
void MainWindow::importLayer() {
    const QPointer<MainWindow> self(this);
    const QPointer<Canvas> canvas = currentCanvas();
    const QPointer<Document> document = currentDocument();
    const quint64 revision = document ? document->revision() : 0;
    const int active = document ? document->state.active : -1;
    const QByteArray stack = document ? layerStackKey(document) : QByteArray{};
    const bool transforming = canvas && canvas->isTransforming();
    const QTransform preview = transforming ? canvas->transformGeometry() : QTransform{};
    const State expected = transforming ? canvas->pendingTransformState() : State{};
    QFileDialog dialog(this, "Görüntü katmanı ekle", {},
                       "Görüntüler (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff)");
    dialog.setObjectName("importLayerDialog");
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    if (!self || currentCanvas() != canvas || currentDocument() != document)
        return;
    const QString file = dialog.selectedFiles().first();
    if (!document) {
        const bool opened = openFile(file);
        if (self && opened)
            showLayerImportHelp();
        return;
    }
    // A chooser must never silently retarget an import after a tab, layer or
    // history change. Read the image before settling a source-preserving preview.
    if (!canvas || document->revision() != revision || document->state.active != active ||
        layerStackKey(document) != stack || canvas->isTransforming() != transforming ||
        (transforming && canvas->transformGeometry() != preview)) {
        status->setText("Hedef değişti; görüntü eklenmedi. Görüntü katmanı ekle işlemini yeniden açın.");
        return;
    }
    QString message;
    QImage image = Document::readImage(file, &message);
    if (image.isNull()) {
        error(message);
        return;
    }
    Layer layer;
    layer.name = QFileInfo(file).completeBaseName();
    layer.pixels = image;
    // Validate the entire insertion, including the pending selection-copy layer,
    // before a failed import could commit an unrelated transform preview.
    Document candidate;
    candidate.state = transforming ? expected : document->state;
    if (!candidate.insertLayerAboveClippingGroup(layer, "Görüntü katmanı eklendi", &message)) {
        error(message);
        return;
    }
    finishLayerRename();
    const bool settled = canvas->finishPendingGesture();
    if (!self || !canvas || !document || currentCanvas() != canvas || currentDocument() != document ||
        canvas->hasPendingGesture() || document->isEditing() || (transforming && !settled) ||
        (transforming && (document->state.active != expected.active ||
                          layerStackKey(document) != layerStackKey(expected) ||
                          !document->selection.equivalent(expected.selection, expected.size))) ||
        (!transforming && (document->state.active != active || layerStackKey(document) != stack)))
        return;
    const bool inserted = document->insertLayerAboveClippingGroup(layer, "Görüntü katmanı eklendi", &message);
    if (!self)
        return;
    if (!inserted) {
        error(message);
        return;
    }
    if (document && currentDocument() == document)
        showLayerImportHelp();
}

void MainWindow::convertSmartObject() {
    const QPointer<MainWindow> self(this);
    const QPointer<Document> document = currentDocument();
    if (!document)
        return;
    QString message;
    const bool converted = document->convertToSmartObject(&message);
    if (!self)
        return;
    if (!converted) {
        error(message);
        return;
    }
    refresh();
    if (self)
        status->setText("Akıllı nesne oluşturuldu. İçeriği değiştir, gömülü kaynağı sabit çerçevede yeniler.");
}

void MainWindow::replaceSmartObjectContents() {
    const QPointer<MainWindow> self(this);
    const QPointer<Canvas> canvas = currentCanvas();
    const QPointer<Document> document = currentDocument();
    if (!canvas || !document || document->state.active < 0 ||
        document->state.active >= document->state.layers.size())
        return;
    const int active = document->state.active;
    const Layer &source = document->state.layers[active];
    if (!source.isSmartObject() || source.locked || !source.visible)
        return;
    const quint64 revision = document->revision();
    const QByteArray stack = layerStackKey(document);
    const bool editingMask = document->editingMask;
    const bool transforming = canvas->isTransforming();
    const QTransform preview = transforming ? canvas->transformGeometry() : QTransform{};
    const State expected = transforming ? canvas->pendingTransformState() : document->state;
    if (expected.active < 0 || expected.active >= expected.layers.size() ||
        !expected.layers[expected.active].isSmartObject()) {
        status->setText("Seçim kopyası bir piksel katmanıdır; içeriği değiştirilemez. Esc özgün akıllı nesneye döner.");
        return;
    }
    QFileDialog dialog(this, "Akıllı nesne içeriğini değiştir", {},
                       "Görüntüler (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff)");
    dialog.setObjectName("replaceSmartObjectContentsDialog");
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setLabelText(QFileDialog::FileName, "Görüntü (sabit çerçeveye esnetilir):");
    dialog.setToolTip("Görüntü özgün çözünürlüğünde gömülür. Farklı en-boy oranı sabit çerçeveyi dolduracak şekilde esnetilir; dönüşüm ve maske korunur.");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    auto targetIsCurrent = [&] {
        return self && canvas && document && currentCanvas() == canvas && currentDocument() == document &&
               document->revision() == revision && document->state.active == active &&
               document->editingMask == editingMask && layerStackKey(document) == stack &&
               canvas->isTransforming() == transforming &&
               (!transforming || canvas->transformGeometry() == preview);
    };
    if (!targetIsCurrent()) {
        if (self)
            status->setText("Hedef değişti; içerik değiştirilmedi. İşlemi yeniden açın.");
        return;
    }
    QString message;
    const QImage image = Document::readImage(dialog.selectedFiles().first(), &message);
    if (image.isNull()) {
        error(message);
        return;
    }
    // Decode and validate the complete pending state before committing a live preview.
    // Failed images, oversized sources and selection-copy targets leave it untouched.
    Document candidate;
    candidate.state = expected;
    candidate.editingMask = editingMask;
    if (!candidate.replaceSmartObjectContents(image, &message)) {
        if (message.isEmpty())
            status->setText("Seçilen içerik aynı; değişiklik yapılmadı.");
        else
            error(message);
        return;
    }
    if (!targetIsCurrent())
        return;
    finishLayerRename();
    const bool settled = canvas->finishPendingGesture();
    if (!self || !canvas || !document || currentCanvas() != canvas || currentDocument() != document ||
        canvas->hasPendingGesture() || document->isEditing() || (transforming && !settled) ||
        document->editingMask != editingMask || document->state.active != expected.active ||
        layerStackKey(document) != layerStackKey(expected) ||
        !document->selection.equivalent(expected.selection, expected.size))
        return;
    const bool replaced = document->replaceSmartObjectContents(image, &message);
    if (!self)
        return;
    if (!replaced) {
        if (message.isEmpty())
            status->setText("Seçilen içerik aynı; değişiklik yapılmadı.");
        else
            error(message);
        return;
    }
    refresh();
    if (self)
        status->setText("Gömülü kaynak değiştirildi; yeni görüntü sabit çerçeveyi doldurur. Konum, dönüşüm ve maske korundu.");
}

void MainWindow::rasterizeLayer() {
    const QPointer<MainWindow> self(this);
    const QPointer<Canvas> canvas = currentCanvas();
    const QPointer<Document> document = currentDocument();
    if (!canvas || !document || document->state.layers.isEmpty())
        return;
    const int active = document->state.active;
    const auto &layer = document->state.layers[active];
    if (layer.locked || !layer.visible)
        return;
    const quint64 revision = document->revision();
    const QByteArray stack = layerStackKey(document);
    const bool transforming = canvas->isTransforming();
    const QTransform preview = transforming ? canvas->transformGeometry() : QTransform{};
    const State expected = transforming ? canvas->pendingTransformState() : document->state;
    const QString targetName = canvas->activeLayerTargetName();
    const bool smartObject = expected.layers[expected.active].isSmartObject();
    QMessageBox confirm(QMessageBox::Warning, "Görünümü rasterleştir",
                        QString("“%1” katmanının görünümü piksele çevrilsin mi?\n\n"
                                "Dönüşüm, katman maskesi, renk/ton ve filtre ayarları görünür "
                                "sonuca işlenir; ayrı ayrı düzenlenemez. Metin varsa metin "
                                "olarak düzenlenebilirliği kalkar. Katman opaklığı, karışım "
                                "modu ve kırpma ilişkisi korunur.\n\n"
                                "Sonra bu piksel katmanında boyayabilirsiniz. Geri al ile "
                                "rasterleştirme öncesine dönebilirsiniz.")
                            .arg(targetName) +
                            (smartObject
                                 ? "\n\nAkıllı nesne kimliği ve gömülü özgün kaynak kaldırılır; İçeriği değiştir ile düzenleme olanağı kalkar. Geri al, akıllı nesneyi ve gömülü kaynağını geri yükler."
                                 : QString()),
                        QMessageBox::Yes | QMessageBox::Cancel, this);
    confirm.setObjectName("rasterizeLayerDialog");
    confirm.setTextFormat(Qt::PlainText);
    confirm.button(QMessageBox::Yes)->setText("&Rasterleştir");
    confirm.button(QMessageBox::Yes)->setAccessibleName("Görünümü rasterleştir");
    confirm.button(QMessageBox::Cancel)->setText("İ&ptal");
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.setEscapeButton(QMessageBox::Cancel);
    if (confirm.exec() != QMessageBox::Yes)
        return;
    if (!self || !canvas || !document || currentCanvas() != canvas || currentDocument() != document ||
        document->revision() != revision || document->state.active != active ||
        layerStackKey(document) != stack || canvas->isTransforming() != transforming ||
        (transforming && canvas->transformGeometry() != preview)) {
        if (self)
            status->setText("Hedef değişti; rasterleştirme uygulanmadı. İşlemi yeniden açın.");
        return;
    }
    finishLayerRename();
    const bool settled = canvas->finishPendingGesture();
    if (!self || !canvas || !document || currentCanvas() != canvas || currentDocument() != document ||
        canvas->hasPendingGesture() || document->isEditing() || (transforming && !settled) ||
        document->state.active != expected.active ||
        layerStackKey(document) != layerStackKey(expected) ||
        !document->selection.equivalent(expected.selection, expected.size))
        return;
    QString message;
    const bool rasterized = document->rasterizeLayer(&message);
    if (!self)
        return;
    if (!rasterized)
        error(message);
    if (self)
        refresh();
}

void MainWindow::newPaintLayer() {
    const QPointer<MainWindow> self(this);
    const QPointer<Document> document = currentDocument();
    const QPointer<Canvas> canvas = currentCanvas();
    if (!document || !canvas)
        return;
    int active = document->state.active + 1;
    while (active < document->state.layers.size() && document->state.layers[active].clipped)
        ++active;
    const int count = document->state.layers.size() + 1;
    QString message;
    if (!document->addPaintLayer(QString("Boya katmanı %1").arg(document->state.layers.size() + 1), &message)) {
        if (self)
            error(message);
        return;
    }
    if (!self || !document || !canvas || currentDocument() != document || currentCanvas() != canvas ||
        document->state.active != active || document->state.layers.size() != count ||
        canvas->hasPendingGesture())
        return;
    canvas->setTool(Canvas::Brush);
    if (!self || !canvas || currentCanvas() != canvas)
        return;
    refresh();
    if (self)
        status->setText("Yeni boş katmanda boyayın; kaynak katman korundu.");
}
bool MainWindow::saveDocument(Document *d, bool saveAs) {
    if (!d)
        return false;
    if (currentCanvas() && currentCanvas()->document == d) {
        finishLayerRename();
        currentCanvas()->finishPendingGesture();
    }
    QString file = d->path;
    auto choose = [&](const QString &initial) {
        return chooseSaveTarget(this, "Katmanlı projeyi kaydet", initial,
                                "PhotoDeck projesi (*.pdeck)");
    };
    if (saveAs || file.isEmpty()) {
        file = choose(d->path.isEmpty() ? QFileInfo(d->title).completeBaseName() + ".pdeck"
                                       : d->path);
        if (file.isEmpty())
            return false;
    }
    Document::SaveOptions options;
    int acceptedFormatVersion = 0;
    while (true) {
        const int formatVersion = d->projectSaveVersion();
        const QString formatExplanation = formatVersion >= 7
            ? "Akıllı nesneler v7 gerektirir. Önceki PhotoDeck EA2 ve 0.9.1 sürümleri bu dosyayı açamaz."
            : formatVersion >= 6
                ? "Kayıtlı seçimler v6 gerektirir. Yayımlanan PhotoDeck 0.9.1 bu dosyayı açamaz."
                : QString();
        if (!file.endsWith(".pdeck", Qt::CaseInsensitive)) {
            error("Projeyi .pdeck uzantısıyla kaydedin.");
            return false;
        }
        if (d->requiresFormatUpgrade(file)) {
            QMessageBox warning(QMessageBox::Warning, "Eski proje biçimini koru",
                QString("Bu proje v%1 biçiminde açıldı. PhotoDeck yeni kayıtları v%2 biçiminde "
                        "yazar; eski uygulama sürümleri bu kopyayı açamayabilir.\n\nOrijinal dosyayı "
                        "korumak için farklı bir adla kaydedin.")
                    .arg(d->sourceVersion()).arg(formatVersion) +
                    (formatExplanation.isEmpty() ? QString() : "\n\n" + formatExplanation),
                QMessageBox::Cancel, this);
            warning.setObjectName("projectUpgradeWarning");
            auto *copy = warning.addButton("Kopya olarak kaydet…", QMessageBox::AcceptRole);
            copy->setObjectName("saveProjectCopy");
            warning.setDefaultButton(copy);
            warning.setEscapeButton(QMessageBox::Cancel);
            warning.exec();
            if (warning.clickedButton() != copy)
                return false;
            const QFileInfo original(file);
            file = choose(original.dir().filePath(original.completeBaseName() + "-v" +
                                                  QString::number(d->projectSaveVersion()) +
                                                  ".pdeck"));
            if (file.isEmpty())
                return false;
            acceptedFormatVersion = formatVersion;
            options = {};
            continue; // A path alias to the original must not bypass the compatibility check.
        }
        if (formatVersion >= 6 && d->sourceVersion() < formatVersion && acceptedFormatVersion < formatVersion) {
            QMessageBox warning(QMessageBox::Warning,
                formatVersion >= 7 ? "Akıllı nesneler için v7 proje biçimi"
                                   : "Kayıtlı seçimler için v6 proje biçimi",
                formatExplanation + (formatVersion >= 7
                    ? " Akıllı nesneleri açıkça rasterleştirirseniz normal kayıt, kalan özelliklere göre v5 veya v6 kullanır."
                    : " Kayıtlı seçimleri silerseniz normal kayıt yeniden v5 biçimini kullanır."),
                QMessageBox::Save | QMessageBox::Cancel, this);
            warning.setObjectName(formatVersion >= 7 ? "smartObjectFormatWarning" : "savedSelectionFormatWarning");
            warning.setDefaultButton(QMessageBox::Cancel);
            if (warning.exec() != QMessageBox::Save)
                return false;
            acceptedFormatVersion = formatVersion;
            if (d->projectSaveVersion() > formatVersion)
                continue;
        }
        QString e;
        if (d->save(file, &e, true, options))
            break;
        if (d->lastSaveStatus() != Document::SaveStatus::ExternalModification) {
            error(e);
            return false;
        }
        QMessageBox warning(QMessageBox::Warning, "Dosya dışarıda değişti",
            "Bu proje açıldıktan veya son kayıttan sonra diskte değiştirilmiş ya da silinmiş. "
            "Üzerine yazmak diskteki değişiklikleri kaybettirir.\n\nÇalışmanızı ayrı bir kopyaya "
            "kaydedebilir veya diskteki sürümün üzerine açıkça yazabilirsiniz.",
            QMessageBox::Cancel, this);
        warning.setObjectName("projectConflictWarning");
        auto *copy = warning.addButton("Kopya olarak kaydet…", QMessageBox::AcceptRole);
        copy->setObjectName("saveProjectCopy");
        auto *overwrite = warning.addButton("Disktekinin üzerine yaz", QMessageBox::DestructiveRole);
        overwrite->setObjectName("overwriteProject");
        warning.setDefaultButton(copy);
        warning.setEscapeButton(QMessageBox::Cancel);
        warning.exec();
        if (warning.clickedButton() == overwrite) {
            options.allowExternalOverwrite = true;
        } else if (warning.clickedButton() == copy) {
            const QFileInfo original(file);
            file = choose(original.dir().filePath(original.completeBaseName() + "-kopya.pdeck"));
            if (file.isEmpty())
                return false;
            options = {};
        } else {
            return false;
        }
    }
    recoveryManager->invalidate(d);
    deferredRecovery.remove(d);
    status->setText("Proje kaydedildi · " + QFileInfo(file).fileName());
    return true;
}
void MainWindow::customizeBrush() {
    auto *c = currentCanvas();
    if (!c)
        return;
    c->finishPendingGesture();
    QDialog dialog(this);
    dialog.setObjectName("brushSettingsDialog");
    dialog.setWindowTitle("Fırça ucu ve tablet");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto spin = [&](QString name, QString label, int minimum, int maximum, int value, QString suffix) {
        auto *box = new QSpinBox;
        box->setObjectName(name);
        box->setRange(minimum, maximum);
        box->setValue(value);
        box->setSuffix(suffix);
        form->addRow(label, box);
        return box;
    };
    auto *spacing = spin("brushSpacing", "Darbe aralığı (çap yüzdesi)", 1, 200, c->brush.spacing, " %");
    auto *angle = spin("brushAngle", "Uç açısı", -180, 180, c->brush.angle, "°");
    auto *roundness = spin("brushRoundness", "Yuvarlaklık", 5, 100, c->brush.roundness, " %");
    auto *minimum =
        spin("pressureMinimum", "En düşük basınç boyutu", 1, 100, c->brush.pressureMinimum, " %");
    auto *curve = new QDoubleSpinBox;
    curve->setObjectName("pressureCurve");
    curve->setRange(.25, 4);
    curve->setSingleStep(.25);
    curve->setValue(c->brush.pressureCurve);
    form->addRow("Basınç tepkisi (1: doğrusal)", curve);
    auto check = [&](QString name, QString label, bool value) {
        auto *box = new QCheckBox(label);
        box->setObjectName(name);
        box->setChecked(value);
        form->addRow(box);
        return box;
    };
    auto *size = check("pressureSize", "Basınç boyutu değiştirir", c->brush.pressureSize);
    auto *flow = check("pressureFlow", "Basınç akışı değiştirir", c->brush.pressureFlow);
    auto *tiltAngle = check("tiltAngle", "Kalem eğimi uç açısını değiştirir", c->brush.tiltAngle);
    auto *tiltRoundness =
        check("tiltRoundness", "Kalem eğimi yuvarlaklığı değiştirir", c->brush.tiltRoundness);
    auto *note = new QLabel("Ayarlar bu belgenin fırça, kalem, silgi ve klon araçları içindir. "
                            "Tablet basıncı/eğimi aygıtın ve sürücünün sağladığı veriye bağlıdır. "
                            "Fare tam basınçla çalışır.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Uygula");
    buttons->button(QDialogButtonBox::Cancel)->setText("İptal");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    c->brush.spacing = spacing->value();
    c->brush.angle = angle->value();
    c->brush.roundness = roundness->value();
    c->brush.pressureMinimum = minimum->value();
    c->brush.pressureCurve = curve->value();
    c->brush.pressureSize = size->isChecked();
    c->brush.pressureFlow = flow->isChecked();
    c->brush.tiltAngle = tiltAngle->isChecked();
    c->brush.tiltRoundness = tiltRoundness->isChecked();
    c->update();
}
void MainWindow::exportAdvancedRaster() {
    auto *d = currentDocument();
    if (!d)
        return;
    RasterOutputDialog dialog(d->state.size, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString filters = "PNG (*.png);;JPEG (*.jpg)";
    if (QImageWriter::supportedImageFormats().contains("webp"))
        filters += ";;WebP (*.webp)";
    if (QImageWriter::supportedImageFormats().contains("tiff"))
        filters += ";;TIFF (*.tiff)";
    QString file = chooseSaveTarget(this, "Raster görüntüyü dışa aktar",
                                    QFileInfo(d->title).completeBaseName() + ".png", filters);
    if (file.isEmpty())
        return;
    QString e;
    if (!d->exportRaster(file, dialog.options(), &e))
        error(e);
}
void MainWindow::exportPdfDocument() {
    auto *d = currentDocument();
    if (!d)
        return;
    PdfOutputDialog dialog(d->state.size, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString file = chooseSaveTarget(this, "PDF dışa aktar",
                                    QFileInfo(d->title).completeBaseName() + ".pdf", "PDF (*.pdf)");
    if (file.isEmpty())
        return;
    QString e;
    if (!d->exportPdf(file, dialog.options(), &e))
        error(e);
}
void MainWindow::printDocument() {
    auto *d = currentDocument();
    if (!d)
        return;
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(d->title);
    QPrintDialog dialog(&printer, this);
    dialog.setMinMax(1, 1);
    if (dialog.exec() != QDialog::Accepted)
        return;
    printer.setFullPage(true);
    QPainter painter(&printer);
    if (!Output::paintPage(painter, d->composite(),
                           Output::printableRect(printer.pageLayout(), printer.resolution())) ||
        !painter.end())
        error("Yazdırma tamamlanamadı. Yazıcı bağlantısını ve sürücüyü kontrol edin.");
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
    if (QImageWriter::supportedImageFormats().contains("tiff"))
        filters += ";;TIFF · RGB 8 bit (*.tiff)";
    QString selected;
    QString file = chooseSaveTarget(this, "Görüntüyü dışa aktar",
                                    QFileInfo(d->title).completeBaseName() + ".png", filters, &selected);
    if (file.isEmpty())
        return;
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
    if (index < 0 || index >= tabs->count())
        return;
    recoveryManager->pause();
    const auto resumeRecovery = qScopeGuard([this] { recoveryManager->resume(); });
    if (!confirmClose(index))
        return;
    auto *canvas = qobject_cast<Canvas *>(tabs->widget(index));
    auto *d = canvas->document;
    recoveryManager->forgetDocument(d);
    deferredRecovery.remove(d);
    previewCaches.remove(d);
    tabs->removeTab(index);
    delete canvas;
    delete d;
    refresh();
}
void MainWindow::closeEvent(QCloseEvent *e) {
    recoveryManager->pause();
    const auto resumeRecovery = qScopeGuard([this] { recoveryManager->resume(); });
    for (int i = 0; i < tabs->count(); ++i)
        if (!confirmClose(i)) {
            e->ignore();
            return;
        }
    saveUiSettings();
    // Earlier Discard choices become final only when every close prompt accepted.
    recoveryManager->stop(true);
    e->accept();
}
void MainWindow::alignLayer(Document::LayerAlignment alignment, Document::AlignmentTarget target) {
    finishLayerRename();
    // A transform preview may change the bounds. Finalize it before reading either
    // the layer or selection target; false also means there was no pending gesture.
    if (auto *canvas = currentCanvas())
        canvas->finishPendingGesture();
    if (auto *d = currentDocument()) {
        QString message;
        if (!d->alignLayer(alignment, target, &message))
            error(message);
    }
}
void MainWindow::positionLayer() {
    finishLayerRename();
    if (auto *canvas = currentCanvas())
        canvas->finishPendingGesture();
    QPointer<Document> d = currentDocument();
    if (!d || d->isEditing() || d->activeLayerBounds().isEmpty())
        return;
    const auto &layer = d->state.layers[d->state.active];
    if (layer.locked || !layer.visible)
        return;
    const auto revision = d->revision();
    const int active = d->state.active;
    const int count = d->state.layers.size();
    const QRect bounds = d->activeLayerBounds();
    QDialog dialog(this);
    dialog.setObjectName("layerPositionDialog");
    dialog.setWindowTitle("Etkin katman konumu");
    dialog.setAccessibleName(dialog.windowTitle());
    dialog.setMinimumWidth(360);
    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel("X / Y: dönüşmüş kaynak dikdörtgeninin sol üst köşesi. "
                                  "Saydam ve maskeli kenarlar dahildir; pikseller kırpılmaz.");
    description->setWordWrap(true);
    description->setMaximumWidth(360);
    layout->addWidget(description);
    auto *form = new QFormLayout;
    auto *x = new QSpinBox;
    auto *y = new QSpinBox;
    for (auto *input : {x, y}) {
        input->setRange(-Document::MaxCoordinate, Document::MaxCoordinate);
        input->setSuffix(" px");
    }
    x->setObjectName("layerPositionX");
    y->setObjectName("layerPositionY");
    x->setAccessibleName("Dönüşmüş katman sol kenarı X");
    y->setAccessibleName("Dönüşmüş katman üst kenarı Y");
    x->setValue(bounds.x());
    y->setValue(bounds.y());
    form->addRow("&X (sol):", x);
    form->addRow("&Y (üst):", y);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    labelEditorDialogButtons(buttons, "&Uygula");
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    x->setFocus();
    x->selectAll();
    if (dialog.exec() != QDialog::Accepted)
        return;
    // A queued document operation must not retarget a still-open numeric dialog.
    if (!d || currentDocument() != d || d->revision() != revision || d->state.active != active ||
        d->state.layers.size() != count || d->activeLayerBounds() != bounds) {
        status->setText("Katman değişti; konum uygulanmadı. Konum penceresini yeniden açın.");
        return;
    }
    QString message;
    if (!d->positionLayer(QPoint(x->value(), y->value()), &message))
        error(message);
}
void MainWindow::transformLayer() {
    finishLayerRename();
    if (auto *canvas = currentCanvas())
        canvas->beginTransform();
}
void MainWindow::transformLayerNumerically() {
    finishLayerRename();
    if (auto *canvas = currentCanvas(); canvas && canvas->isTransforming()) {
        canvas->showNumericTransform();
        return;
    }
    auto *d = currentDocument();
    if (!d || d->state.layers[d->state.active].locked)
        return;
    d->beginEdit();
    TransformDialog dialog(d, this);
    if (dialog.exec() == QDialog::Accepted) {
        QString e;
        if (d->setLayerTransform(dialog.transform(), &e))
            d->commit("Kaynak koruyan dönüşüm");
        else {
            d->cancelEdit();
            error(e);
        }
    } else
        d->cancelEdit();
}
void MainWindow::changeCanvasSize() {
    auto *d = currentDocument();
    if (!d)
        return;
    QDialog dialog(this);
    dialog.setObjectName("canvasSizeDialog");
    dialog.setWindowTitle("Tuval boyutu");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *width = new QSpinBox;
    width->setObjectName("canvasWidth");
    width->setRange(1, 16384);
    width->setValue(d->state.size.width());
    auto *height = new QSpinBox;
    height->setObjectName("canvasHeight");
    height->setRange(1, 16384);
    height->setValue(d->state.size.height());
    auto *anchor = new QComboBox;
    anchor->setObjectName("canvasAnchor");
    anchor->addItems({"Sol üst", "Üst orta", "Sağ üst", "Sol orta", "Merkez", "Sağ orta", "Sol alt",
                      "Alt orta", "Sağ alt"});
    anchor->setCurrentIndex(4);
    form->addRow("Genişlik (px)", width);
    form->addRow("Yükseklik (px)", height);
    form->addRow("Sabitleme noktası", anchor);
    layout->addLayout(form);
    auto *note = new QLabel("Katmanlar yeniden örneklenmez. Tuval dışındaki kaynak pikseller korunur.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString e;
    int index = anchor->currentIndex();
    if (!d->canvasSize({width->value(), height->value()}, {index % 3, index / 3}, &e))
        error(e);
    else
        currentCanvas()->fit();
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
    // Active-row double-click enters directly rather than through QAction's
    // gesture guard. Finalize before capturing the layer or opening the dialog.
    if (auto *canvas = currentCanvas())
        canvas->finishPendingGesture();
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
    currentCanvas()->finishPendingGesture();
    if (currentDocument()->isEditing())
        return;
    auto *delegate = static_cast<LayerNameDelegate *>(layers->itemDelegate());
    delegate->cancelIfStale(currentDocument());
    if (delegate->isRenaming())
        return;
    // Commands can run before the scheduled 25 ms refresh after undo/reorder.
    refresh();
    if (!layers->isVisible()) {
        actions["ui.panel.layers"]->setChecked(true);
        if (appearance.preset != "focus")
            layersDock->raise();
    }
    layers->setFocus();
    layers->scrollToItem(layers->currentItem());
    layers->editItem(layers->currentItem());
}
void MainWindow::finishLayerRename() {
    const QPointer<LayerNameDelegate> delegate =
        static_cast<LayerNameDelegate *>(layers->itemDelegate());
    // A focus-out commit must precede a command's revision/transaction capture.
    // In particular, it must not finish a transform session that just started.
    delegate->cancelIfStale(currentDocument());
    if (delegate)
        delegate->finishRename();
}
void MainWindow::showLayerMenu(const QPoint &position) {
    finishLayerRename();
    refresh();
    auto *item = layers->itemAt(position);
    if (!item || !currentDocument())
        return;
    layers->setCurrentItem(item);
    // Keep a same-row transform preview live until a chooser-backed command succeeds.
    if (!currentCanvas()->isTransforming())
        currentCanvas()->finishPendingGesture();
    QPointer<Document> target = currentDocument();
    const int index = target->state.active;
    const quint64 revision = target->revision();
    QMenu menu(this);
    menu.setObjectName("layerContextMenu");
    menu.setToolTipsVisible(true);
    auto append = [&](QMenu *parent, const QString &id) {
        auto *source = actions[id];
        auto *entry = parent->addAction(source->icon(), source->text());
        entry->setObjectName(id);
        entry->setEnabled(source->isEnabled());
        entry->setToolTip(source->toolTip());
        if (id == "layerUp" || id == "layerDown")
            entry->setToolTip("Tek katmanı taşır. Sıra değişikliği görünümü ve kırpma ilişkisini "
                              "değiştirebilir.");
        // Context commands always apply to the row that opened the menu. If a
        // tab/history/structural change occurs while it is open, discard the command.
        connect(entry, &QAction::triggered, this, [this, target, index, revision, source] {
            if (target && currentDocument() == target && target->state.active == index &&
                target->revision() == revision)
                source->trigger();
        });
    };
    append(&menu, "rename");
    if (!target->state.layers[index].text.isEmpty())
        append(&menu, "editText");
    append(&menu, "duplicateWhole");
    menu.addSeparator();
    append(&menu, "convertSmartObject");
    append(&menu, "replaceSmartObjectContents");
    append(&menu, "rasterize");
    menu.addSeparator();
    append(&menu, "layerUp");
    append(&menu, "layerDown");
    append(&menu, "clipLayer");
    auto *maskMenu = menu.addMenu("Katman maskesi");
    for (const auto &id : {"mask", "maskSettings", "invertMask", "removeMask"})
        append(maskMenu, id);
    menu.addSeparator();
    append(&menu, "lock");
    append(&menu, "deleteLayer");
    menu.exec(layers->viewport()->mapToGlobal(position));
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
void MainWindow::scheduleRecovery(Document *d) {
    if (d->isEditing())
        return;
    if (deferredRecovery.remove(d)) {
        recoverySaveTimer.start(0);
        return;
    }
    if (d->dirty && recoveryManager->publishedRevision(d) != d->revision() &&
        !recoverySaveTimer.isActive())
        recoverySaveTimer.start(5000);
}
void MainWindow::autosave() {
    for (int i = 0; i < tabs->count(); ++i) {
        auto *d = qobject_cast<Canvas *>(tabs->widget(i))->document;
        if (d->isEditing()) {
            deferredRecovery.insert(d);
            continue;
        }
        deferredRecovery.remove(d);
        recoveryManager->request(d);
    }
}
void MainWindow::recover() {
    QDir dir(recoveryDir());
    QStringList files = dir.entryList({"*.pdeck"}, QDir::Files);
    files.removeIf([&](const QString &name) {
        return !RecoveryFiles::mayRecover(dir.filePath(name));
    });
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
        recoveryManager->adoptRecovery(d, file, d->revision());
    }
}
void MainWindow::batch() {
    QStringList files =
        QFileDialog::getOpenFileNames(this, "Toplu işlem için görüntüleri seç", {},
                                      "Görüntüler (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff)");
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
    if (QImageWriter::supportedImageFormats().contains("tiff"))
        format->addItem("tiff");
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
namespace {
QString brushLibraryPath() {
    return QFileInfo(uiSettingsPath()).absolutePath() + "/brushes.pdbrush";
}
} // namespace
void MainWindow::loadBrushPresets() {
    if (!persistSettings)
        return;
    const QString file = brushLibraryPath();
    brushPresets.clear();
    brushLibraryLoadError.clear();
    if (QFileInfo::exists(file) && !BrushLibrary::read(file, &brushPresets, &brushLibraryLoadError))
        status->setText("Fırça kütüphanesi okunamadı; mevcut dosya korundu.");
}
bool MainWindow::storeBrushPresets(const QVector<BrushPreset> &presets) {
    QString e;
    if (!BrushLibrary::valid(presets, &e)) {
        error(e);
        return false;
    }
    if (persistSettings) {
        const QString file = brushLibraryPath();
        QLockFile lock(file + ".lock");
        if (!lock.tryLock(0)) {
            error("Fırça kütüphanesi başka bir oturumda kullanılıyor. Tekrar deneyin.");
            return false;
        }
        if (QFileInfo::exists(file)) {
            QVector<BrushPreset> disk;
            QString readError;
            if (BrushLibrary::read(file, &disk, &readError)) {
                if (!brushLibraryLoadError.isEmpty() || disk != brushPresets) {
                    error("Fırça kütüphanesi başka bir oturumda değişti. Kütüphane penceresini yeniden "
                          "açın.");
                    return false;
                }
            } else {
                if (brushLibraryLoadError.isEmpty()) {
                    error("Fırça dosyası değişmiş veya okunamıyor. Kütüphane penceresini yeniden açın.");
                    return false;
                }
                const QString backup = QFileInfo(file).absolutePath() + "/brushes-unreadable-" +
                                       QUuid::createUuid().toString(QUuid::WithoutBraces) + ".pdbrush";
                if (!QFile::copy(file, backup)) {
                    error("Okunamayan kütüphane yedeklenemedi; dosya değiştirilmedi.");
                    return false;
                }
            }
        } else if (!brushPresets.isEmpty() || !brushLibraryLoadError.isEmpty()) {
            error("Fırça dosyası kaldırılmış. Kütüphane penceresini yeniden açın.");
            return false;
        }
        if (!BrushLibrary::write(file, presets, &e)) {
            error(e);
            return false;
        }
    }
    brushPresets = presets;
    brushLibraryLoadError.clear();
    return true;
}
void MainWindow::applyBrushPreset(const BrushPreset &preset) {
    auto *c = currentCanvas();
    if (!c || !preset.valid())
        return;
    c->finishPendingGesture();
    c->brush = preset.settings;
    const Canvas::Tool tools[] = {Canvas::Brush, Canvas::Pencil, Canvas::Eraser, Canvas::Clone};
    c->setTool(tools[preset.tool]);
    refresh();
    c->update();
    status->setText("Fırça · " + preset.name);
}
void MainWindow::showBrushLibrary() {
    loadBrushPresets();
    QDialog dialog(this);
    dialog.setObjectName("brushLibraryDialog");
    dialog.setWindowTitle("Fırça kütüphanesi");
    dialog.resize(620, 540);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    auto *title = new QLabel("Fırçalarınız, tek yerde");
    title->setObjectName("dialogTitle");
    layout->addWidget(title);
    auto *note = new QLabel(
        brushLibraryLoadError.isEmpty()
            ? "Ayarları isimli presetlerle saklayın, taşıyın ve yeniden kullanın."
            : "Kütüphane okunamadı. İlk değişiklikten önce mevcut dosya ayrı bir yedeğe kopyalanacak.");
    note->setWordWrap(true);
    note->setObjectName("mutedLabel");
    layout->addWidget(note);
    auto *list = new QListWidget;
    list->setObjectName("brushPresetList");
    list->setIconSize({40, 40});
    layout->addWidget(list, 1);
    auto *details = new QLabel;
    details->setWordWrap(true);
    details->setTextFormat(Qt::PlainText);
    details->setObjectName("presetDetails");
    layout->addWidget(details);
    const auto builtins = BrushLibrary::builtins();
    auto populate = [&] {
        list->clear();
        for (int i = 0; i < builtins.size() + brushPresets.size(); ++i) {
            const auto &p = i < builtins.size() ? builtins[i] : brushPresets[i - builtins.size()];
            auto *item =
                new QListWidgetItem((i < builtins.size() ? "Yerleşik · " : "Kişisel · ") + p.name, list);
            item->setData(Qt::UserRole, i);
            item->setSizeHint({200, 48});
            if (!p.settings.tipMask.isNull())
                item->setIcon(QIcon(QPixmap::fromImage(p.settings.tipMask)));
            else
                item->setIcon(
                    toolIcon(p.tool == 1 ? Canvas::Pencil : Canvas::Brush, appearance.resolved().text));
        }
        if (list->count())
            list->setCurrentRow(0);
    };
    auto button = [&](QHBoxLayout *row, QString id, QString text) {
        auto *b = new QPushButton(text);
        b->setObjectName(id);
        row->addWidget(b);
        return b;
    };
    auto *editRow = new QHBoxLayout;
    layout->addLayout(editRow);
    auto *save = button(editRow, "saveBrushPreset", "Mevcut fırçayı kaydet…");
    auto *remove = button(editRow, "deleteBrushPreset", "Sil");
    auto *tipImage = button(editRow, "defineBrushImage", "Görselden uç…");
    auto *tipSelection = button(editRow, "defineBrushSelection", "Seçimden uç");
    save->setEnabled(currentCanvas());
    tipImage->setEnabled(currentCanvas());
    tipSelection->setEnabled(currentDocument());
    auto *files = new QHBoxLayout;
    layout->addLayout(files);
    auto *import = button(files, "importBrushPresets", "İçe aktar…");
    auto *exportAll = button(files, "exportBrushPresets", "Kütüphaneyi dışa aktar…");
    auto *exportOne = button(files, "exportBrushPreset", "Seçileni dışa aktar…");
    auto *footer = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Close);
    footer->button(QDialogButtonBox::Ok)->setText("Seçileni kullan");
    footer->button(QDialogButtonBox::Close)->setText("Kapat");
    footer->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(footer);
    auto selected = [&]() -> BrushPreset {
        const int row = list->currentRow();
        return row < 0                 ? BrushPreset{}
               : row < builtins.size() ? builtins[row]
                                       : brushPresets[row - builtins.size()];
    };
    connect(list, &QListWidget::currentRowChanged, &dialog, [&](int row) {
        remove->setEnabled(row >= builtins.size());
        footer->button(QDialogButtonBox::Ok)->setEnabled(row >= 0 && currentCanvas());
        exportOne->setEnabled(row >= 0);
        const auto p = selected();
        details->setText(
            row < 0 ? QString()
                    : QString("%1 px · %2% akış · %3% aralık · %4° açı · %5% yuvarlaklık\n%6")
                          .arg(p.settings.size)
                          .arg(p.settings.flow)
                          .arg(p.settings.spacing)
                          .arg(p.settings.angle)
                          .arg(p.settings.roundness)
                          .arg(p.settings.tipMask.isNull()
                                   ? "Eliptik uç"
                                   : "Görsel uç · sertlik yerine maskenin kendi kenarları kullanılır"));
    });
    connect(save, &QPushButton::clicked, &dialog, [&] {
        auto *c = currentCanvas();
        if (!c)
            return;
        bool ok = false;
        QString name = QInputDialog::getText(&dialog, "Fırçayı kaydet", "Preset adı (1–80 karakter)",
                                             QLineEdit::Normal, "Fırçam", &ok)
                           .trimmed()
                           .normalized(QString::NormalizationForm_C);
        if (!ok)
            return;
        int tool = c->tool == Canvas::Pencil   ? 1
                   : c->tool == Canvas::Eraser ? 2
                   : c->tool == Canvas::Clone  ? 3
                                               : 0;
        QVector<BrushPreset> next;
        QString e;
        if (!BrushLibrary::merge(brushPresets, {{name, tool, c->brush}}, &next, &e)) {
            error(e);
            return;
        }
        if (storeBrushPresets(next)) {
            populate();
            list->setCurrentRow(list->count() - 1);
        }
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        const int index = list->currentRow() - builtins.size();
        if (index < 0 || index >= brushPresets.size())
            return;
        const auto reply = QMessageBox::question(
            &dialog, "Preseti sil", "Seçilen kişisel fırça presetini silmek istiyor musunuz?");
        if (reply != QMessageBox::Yes)
            return;
        auto next = brushPresets;
        next.removeAt(index);
        if (storeBrushPresets(next))
            populate();
    });
    connect(import, &QPushButton::clicked, &dialog, [&] {
        const QString file = QFileDialog::getOpenFileName(&dialog, "Fırça kütüphanesini içe aktar", {},
                                                          "PhotoDeck fırçaları (*.pdbrush)");
        if (file.isEmpty())
            return;
        QVector<BrushPreset> incoming, next;
        QString e;
        if (!BrushLibrary::read(file, &incoming, &e) ||
            !BrushLibrary::merge(brushPresets, incoming, &next, &e)) {
            error(e);
            return;
        }
        if (storeBrushPresets(next))
            populate();
    });
    const auto output = [&](bool all) {
        QVector<BrushPreset> presets = all ? brushPresets : QVector<BrushPreset>{selected()};
        const QString file =
            chooseSaveTarget(&dialog, "Fırçaları dışa aktar", "PhotoDeck-fircalar.pdbrush",
                             "PhotoDeck fırçaları (*.pdbrush)");
        if (file.isEmpty())
            return;
        if (persistSettings &&
            QFileInfo(file).canonicalFilePath() == QFileInfo(brushLibraryPath()).canonicalFilePath()) {
            error("Uygulamanın etkin kütüphane dosyasına dışa aktarılamaz. Başka bir çıktı dosyası "
                  "seçin.");
            return;
        }
        QString e;
        if (!BrushLibrary::write(file, presets, &e))
            error(e);
    };
    connect(exportAll, &QPushButton::clicked, &dialog, [&] { output(true); });
    connect(exportOne, &QPushButton::clicked, &dialog, [&] { output(false); });
    const auto define = [&](QImage image) {
        const QImage mask = BrushSettings::defineTip(image);
        if (mask.isNull()) {
            error("Uç oluşturulamadı. Siyah/koyu alanlar boyar, beyaz ve saydam alanlar gizlenir.");
            return;
        }
        if (auto *c = currentCanvas()) {
            c->finishPendingGesture();
            c->brush.tipMask = mask;
            c->setTool(Canvas::Brush);
            refresh();
            c->update();
        }
        note->setText("Görsel uç hazır. Kalıcı saklamak için Mevcut fırçayı kaydet düğmesini kullanın.");
    };
    connect(tipImage, &QPushButton::clicked, &dialog, [&] {
        const QString file =
            QFileDialog::getOpenFileName(&dialog, "Görselden fırça ucu", {},
                                         "Görüntüler (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)");
        if (file.isEmpty())
            return;
        QString e;
        QImage image = Document::readImage(file, &e);
        if (image.isNull()) {
            error(e);
            return;
        }
        define(image);
    });
    connect(tipSelection, &QPushButton::clicked, &dialog, [&] {
        if (auto *d = currentDocument())
            define(d->selection.copyFrom(d->composite()));
    });
    connect(footer, &QDialogButtonBox::accepted, &dialog, [&] {
        applyBrushPreset(selected());
        dialog.accept();
    });
    connect(footer, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    populate();
    dialog.exec();
}
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
    auto *toolLayout = new QVBoxLayout(floatingTools);
    toolLayout->setContentsMargins(4, 4, 4, 8);
    toolLayout->setSpacing(4);
    toolsHandle = new QToolButton;
    toolsHandle->setObjectName("toolsHandle");
    toolsHandle->setText("⠿  ARAÇLAR");
    toolsHandle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toolsHandle->setToolTip("Araç panelini sürükleyin veya ok tuşlarıyla taşıyın.");
    toolsHandle->setAccessibleName("Araç panelini taşı");
    toolsHandle->setCursor(Qt::SizeAllCursor);
    toolsHandle->installEventFilter(this);
    toolLayout->addWidget(toolsHandle);
    floatingLayers = new QFrame(workspace);
    floatingLayers->setObjectName("floatingLayers");
    auto *layerLayout = new QVBoxLayout(floatingLayers);
    layerLayout->setContentsMargins(4, 4, 4, 4);
    layerLayout->setSpacing(0);
    layersHandle = new QToolButton;
    layersHandle->setObjectName("layersHandle");
    layersHandle->setText("⠿  KATMANLAR");
    layersHandle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
        connect(button, &QPushButton::clicked, this,
                [this, i] { applyBrushPreset(BrushLibrary::builtins()[i]); });
        presetLayout->addWidget(button);
    }
    brushLayout->addWidget(presets);
    auto *libraryButton = new QPushButton("Kütüphaneyi aç…");
    libraryButton->setObjectName("openBrushLibrary");
    connect(libraryButton, &QPushButton::clicked, actions["brushLibrary"], &QAction::trigger);
    brushLayout->addWidget(libraryButton);
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
        a->setShortcut(QKeySequence("Ctrl+Alt+" + QString::number(index % 10)));
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
        QString file = chooseSaveTarget(this, "Arayüz presetini dışa aktar", "PhotoDeck-gorunum.pdui",
                                        "PhotoDeck görünümü (*.pdui)");
        if (file.isEmpty())
            return;
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
            if (appearance.preset == "focus") {
                if (key == "tools")
                    appearance.focusToolsVisible = visible;
                if (key == "layers")
                    appearance.focusLayersVisible = visible;
                positionOverlays();
            }
            if (key == "film" || key == "color")
                refresh();
            queueUiSave();
        });
    }
    presetPicker = new QComboBox;
    presetPicker->setObjectName("presetPicker");
    presetPicker->setAccessibleName("Arayüz preseti");
    for (const auto &p : uiPresets()) {
        presetPicker->addItem(p.name, p.id);
        presetPicker->setItemData(presetPicker->count() - 1, p.description, Qt::ToolTipRole);
    }
    presetPicker->setToolTip("Çalışma alanı · Ctrl+Alt+1…0 ile preset değiştirin");
    presetPicker->setMinimumContentsLength(12);
    presetPicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    documentToolbar->addSeparator();
    documentToolbar->addWidget(presetPicker);
    connect(presetPicker, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!applyingAppearance && index >= 0)
            applyUiPreset(presetPicker->itemData(index).toString());
    });
    documentToolbar->addAction(actions["ui.customize"]);
    if (auto *button = qobject_cast<QToolButton *>(
            documentToolbar->widgetForAction(actions["ui.customize"])))
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
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
    if (persistSettings && !applyingAppearance)
        uiSaveTimer.start();
}
void MainWindow::saveUiSettings() {
    if (!persistSettings || applyingAppearance)
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
    if (resetLayout && appearance.dockState.isEmpty() && appearance.preset != "focus") {
        // Wait for typography and initial window geometry before preferring the
        // full controls. The smaller scroll minimum still fits crowded stacks.
        QTimer::singleShot(0, colorDock, [this, expected = appearance.toJson()] {
            if (appearance.toJson() != expected || colorDock->isFloating())
                return;
            const auto *scroll = qobject_cast<QScrollArea *>(colorDock->widget());
            scroll->widget()->ensurePolished();
            scroll->widget()->layout()->activate();
            // Match QScrollArea's height-for-width sizing. Wrapping form rows
            // can need more than minimumSizeHint even when their width fits.
            const QSize viewport = scroll->maximumViewportSize();
            // An existing vertical bar narrows the first height-for-width pass.
            // Budget for that width too, otherwise Qt can retain the bar even
            // when the full-width contents would fit without one.
            const int contentWidth = qMax(scroll->widget()->minimumSizeHint().width(),
                                          viewport.width() -
                                              scroll->verticalScrollBar()->sizeHint().width());
            const int contentHeight = qMax(scroll->widget()->minimumSizeHint().height(),
                                            scroll->widget()->heightForWidth(contentWidth));
            const int frameHeight = colorDock->height() - viewport.height();
            const int colorHeight = contentHeight + frameHeight;
            const int layerHeight = qMax(layersDock->minimumSizeHint().height(),
                                        colorDock->height() + layersDock->height() - colorHeight);
            resizeDocks({colorDock, layersDock}, {colorHeight, layerHeight},
                        Qt::Vertical);
        });
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
    // Touch's bottom tool grid leaves a short inspector at 900 × 620. Keep
    // its first layer row reachable; the same import action stays beside Open.
    findChild<QToolButton *>("addImageLayer")->setVisible(appearance.preset != "touch");
    histogram->setUiPalette(appearance.palette());
    brandLabel->setText("Photo<span style='color:" + p.accent.name() + "'>Deck</span>");
    for (int i = 0; i < Canvas::ToolCount; ++i)
        actions["tool" + QString::number(i)]->setIcon(toolIcon(i, p.text));
    for (const auto &id : QStringList{"open", "save", "undo", "redo", "export", "ui.customize",
                                      "brushSettings", "brushLibrary", "addLayer", "duplicate",
                                      "layerUp", "layerDown", "lock", "deleteLayer", "swapColors"})
        actions[id]->setIcon(commandIcon(id, id == "export" ? appearance.palette().color(
                                                                  QPalette::HighlightedText)
                                                            : p.text));
    actions["crop"]->setIcon(toolIcon(Canvas::Crop, p.text));
    toolsBar->setIconSize({appearance.iconSize, appearance.iconSize});
    const bool horizontalTools = toolsBar->orientation() == Qt::Horizontal;
    const int columns = horizontalTools ? (appearance.preset == "touch" ? 5 : 20)
                                        : (appearance.toolLabels ? 1 : 2);
    while (auto *item = toolGrid->takeAt(0))
        delete item;
    const QVector<int> order{0, 1, 14, 15, 16, 17, 2, 12, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13};
    int position = 0;
    for (int index : order) {
        auto *button = toolButtons[index];
        button->setIconSize({appearance.iconSize, appearance.iconSize});
        button->setToolButtonStyle(appearance.toolLabels ? Qt::ToolButtonTextBesideIcon
                                                        : Qt::ToolButtonIconOnly);
        button->setMinimumSize(appearance.iconSize + 14, appearance.iconSize + 14);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        toolGrid->addWidget(button, position / columns, position % columns);
        ++position;
    }
    toolGrid->addWidget(colorButton, position / columns, position % columns, Qt::AlignCenter);
    ++position;
    toolGrid->addWidget(toolPalette->findChild<QToolButton *>("swapColorsButton"), position / columns,
                        position % columns);
    toolGrid->invalidate();
    const QSize paletteSize = toolPalette->sizeHint();
    if (horizontalTools) {
        toolScroll->setMinimumWidth(120);
        toolScroll->setMaximumWidth(QWIDGETSIZE_MAX);
        toolScroll->setMinimumHeight(paletteSize.height() + 4);
        toolScroll->setMaximumHeight(paletteSize.height() + 18);
        toolScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        toolScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        toolsDock->setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
        toolScroll->setMinimumWidth(paletteSize.width() + 12);
        toolScroll->setMaximumWidth(paletteSize.width() + 12);
        toolScroll->setMinimumHeight(130);
        toolScroll->setMaximumHeight(QWIDGETSIZE_MAX);
        toolScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        toolScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        toolsDock->setMaximumWidth(paletteSize.width() + 28);
    }
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
    layerDetails->show();
    workspace->layout()->setContentsMargins(0, 0, 0, 0);
    if (appearance.preset == "focus") {
        toolsDock->setWidget(nullptr);
        layersDock->setWidget(nullptr);
        toolsDock->hide();
        layersDock->hide();
        colorDock->hide();
        toolsBar->setOrientation(Qt::Vertical);
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
    if (!floatingTools || appearance.preset != "focus") {
        if (workspace && workspace->layout())
            workspace->layout()->setContentsMargins(0, 0, 0, 0);
        return;
    }
    const int w = workspace->width(), h = workspace->height();
    if (w < 50 || h < 50)
        return;
    const int toolWidth = qMin(w - 24, qMax(toolScroll->minimumWidth() + 16,
                                            toolsHandle->sizeHint().width() + 16));
    const QSize toolsSize(toolWidth,
                          qMin(h - 24, toolPalette->sizeHint().height() +
                                            toolsHandle->sizeHint().height() + 24));
    const int toolsMargin = appearance.focusToolsVisible ? toolsSize.width() + 24 : 8;
    // Large, labelled tools and a wide inspector share the same horizontal
    // budget. Reserve the tab viewport's minimum before sizing the inspector;
    // otherwise QStackedWidget can become narrower than its canvas child.
    const int viewportMinimum = qMax(tabs->minimumWidth(), tabs->minimumSizeHint().width());
    const int layerWidth = qMin(appearance.panelWidth, qMax(220, w / 2 - 40));
    const int layerBudget = qMax(0, w - toolsMargin - 24 - viewportMinimum);
    const QSize layerSize(appearance.focusLayersVisible ? qMin(layerWidth, layerBudget) : layerWidth,
                          h - 24);
    const bool layersOnLeft = appearance.panelSide == "left";
    const int layersMargin = appearance.focusLayersVisible ? layerSize.width() + 24 : 8;
    // Keep the artwork in its own viewport. Floating panels can still be moved by the user.
    workspace->layout()->setContentsMargins(layersOnLeft ? layersMargin : toolsMargin, 0,
                                            layersOnLeft ? toolsMargin : layersMargin, 0);
    auto place = [&](QFrame *panel, QSize size, QPointF normalized, QPoint fallback) {
        const int maxX = qMax(0, w - size.width()), maxY = qMax(0, h - size.height());
        const QPoint at = normalized.x() < 0 || normalized.y() < 0
                              ? fallback
                              : QPoint(qRound(normalized.x() * maxX), qRound(normalized.y() * maxY));
        panel->setGeometry(qBound(0, at.x(), maxX), qBound(0, at.y(), maxY), size.width(),
                            size.height());
        panel->raise();
    };
    place(floatingTools, toolsSize, appearance.toolsPosition,
          {layersOnLeft ? w - toolsSize.width() - 12 : 12, 12});
    place(floatingLayers, layerSize, appearance.layersPosition,
          {layersOnLeft ? 12 : w - layerSize.width() - 12, 12});
}

bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    if (history && object == history->viewport() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            if (auto *item = history->itemAt(mouse->position().toPoint())) {
                const quint64 revision = item->data(Qt::UserRole).toULongLong();
                const QPointer<Document> target = historyDocument;
                history->setCurrentItem(item);
                if (navigateHistory(target, revision))
                    history->setFocus(Qt::MouseFocusReason);
            }
            return true;
        }
    }
    if (object == history && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Space) {
            if (auto *item = history->currentItem())
                navigateHistory(historyDocument, item->data(Qt::UserRole).toULongLong());
            return true;
        }
    }
    if (object == layers && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Menu ||
            (key->key() == Qt::Key_F10 && key->modifiers() == Qt::ShiftModifier)) {
            if (auto *item = layers->currentItem())
                showLayerMenu(layers->visualItemRect(item).center());
            return true;
        }
    }
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
