#include "mainwindow.h"
#include "version.h"
#include <QAction>
#include <QCryptographicHash>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEnterEvent>
#include <QFileDialog>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

namespace {
// Qt disables automatic Alt mnemonics on macOS. Keep checking the label's
// declared access key there, while exercising its native keyboard behavior below.
QString declaredAccessKey(const QString &text) {
    for (int index = 0; index + 1 < text.size(); ++index) {
        if (text[index] != '&')
            continue;
        if (text[index + 1] == '&') {
            ++index;
            continue;
        }
        return QString(text[index + 1]).toUpper();
    }
    return {};
}
bool hasLabelAccessKey(const QString &text) {
    if (declaredAccessKey(text).isEmpty())
        return false;
#ifdef Q_OS_MACOS
    return QKeySequence::mnemonic(text).isEmpty();
#else
    return !QKeySequence::mnemonic(text).isEmpty();
#endif
}
double renderedContrast(QColor foreground, QColor background) {
    const auto luminance = [](QColor color) {
        const auto channel = [](double value) {
            return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
        };
        return .2126 * channel(color.redF()) + .7152 * channel(color.greenF()) +
               .0722 * channel(color.blueF());
    };
    const double first = luminance(foreground), second = luminance(background);
    return (qMax(first, second) + .05) / (qMin(first, second) + .05);
}
void addPresetRows() {
    QTest::addColumn<QString>("preset");
    for (const auto &preset : uiPresets())
        QTest::newRow(qPrintable(preset.id)) << preset.id;
}
QByteArray contents(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
bool driveDialogs(const std::function<void()> &open,
                  const std::function<void(QDialog *)> &handler) {
    QTimer driver, timeout;
    driver.setInterval(10);
    timeout.setSingleShot(true);
    QSet<QDialog *> alreadyVisible;
    for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible())
            alreadyVisible.insert(dialog);
    }
    auto currentDialog = [&]() -> QDialog * {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (dialog && !alreadyVisible.contains(dialog))
            return dialog;
        // QMessageBox::about() is modeless on macOS, so it is never activeModalWidget().
        for (auto *widget : QApplication::topLevelWidgets()) {
            dialog = qobject_cast<QDialog *>(widget);
            if (dialog && dialog->isVisible() && !alreadyVisible.contains(dialog))
                return dialog;
        }
        return nullptr;
    };
    QPointer<QDialog> last;
    bool seen = false, expired = false;
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *dialog = currentDialog();
        if (!dialog || dialog == last)
            return;
        last = dialog;
        seen = true;
        handler(dialog);
    });
    QObject::connect(&timeout, &QTimer::timeout, [&] {
        expired = true;
        if (auto *dialog = currentDialog())
            dialog->reject();
    });
    driver.start();
    timeout.start(5000);
    open();
    // A modeless action returns before the timer can inspect its dialog.
    if (!seen && !expired && !QTest::qWaitFor([&] { return seen || expired; }, 5000))
        return false;
    return seen && !expired;
}
bool driveDialogs(QAction *action, const std::function<void(QDialog *)> &handler) {
    return driveDialogs([action] { action->trigger(); }, handler);
}
QString recoveryDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery";
}
void paint(Document *d, QColor color) {
    d->beginEdit();
    d->state.layers[0].pixels.fill(color);
    d->commit("Test edit");
}
}
class UiResilienceTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName("PhotoDeckUiResilienceTests");
        QApplication::setOrganizationName("PhotoDeckTests");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QImageReader::setAllocationLimit(128);
    }
    void cleanup() {
        QDir(recoveryDirectory()).removeRecursively();
    }
    void dialogDriverHandlesModelessWindows_data() {
        QTest::addColumn<bool>("deferred");
        QTest::newRow("immediate") << false;
        QTest::newRow("queued") << true;
    }
    void dialogDriverHandlesModelessWindows() {
        QFETCH(bool, deferred);
        QDialog dialog;
        QAction action;
        connect(&action, &QAction::triggered, &dialog, [&] {
            if (deferred)
                QTimer::singleShot(0, &dialog, &QDialog::show);
            else
                dialog.show();
        });
        bool handled = false;
        QVERIFY(driveDialogs(&action, [&](QDialog *shown) {
            QCOMPARE(shown, &dialog);
            QVERIFY(!shown->isModal());
            QVERIFY(!QApplication::activeModalWidget());
            handled = true;
            shown->accept();
        }));
        QVERIFY(handled);
        QVERIFY(!dialog.isVisible());
    }
    void globalActionShortcutsAreUnique() {
        MainWindow w(false, false);
        QList<QPair<QKeySequence, QAction *>> registered;
        // MainWindow registers its window-wide actions here so they also work with hidden chrome.
        // Include disabled actions: document/selection changes can enable them together later.
        for (auto *action : w.QWidget::actions()) {
            if (action->shortcutContext() != Qt::WindowShortcut &&
                action->shortcutContext() != Qt::ApplicationShortcut)
                continue;
            for (const auto &shortcut : action->shortcuts()) {
                if (shortcut.isEmpty())
                    continue;
                for (const auto &[other, owner] : registered) {
                    if (owner == action)
                        continue;
                    const QString conflict =
                        QString("Global shortcut conflict: %1 (%2) and %3 (%4)")
                            .arg(owner->objectName(), other.toString(QKeySequence::PortableText),
                                 action->objectName(), shortcut.toString(QKeySequence::PortableText));
                    QVERIFY2(shortcut.matches(other) == QKeySequence::NoMatch &&
                                 other.matches(shortcut) == QKeySequence::NoMatch,
                             qPrintable(conflict));
                }
                registered.append({shortcut, action});
            }
        }
        QCOMPARE(w.findChild<QAction *>("export")->shortcut(), QKeySequence("Ctrl+Shift+E"));
        QCOMPARE(w.findChild<QAction *>("mergeVisible")->shortcut(),
                 QKeySequence("Ctrl+Alt+Shift+E"));
        QCOMPARE(w.findChild<QAction *>("mergeDown")->shortcut(), QKeySequence("Ctrl+E"));
    }
    void exportShortcutOpensDialogWithoutMerging_data() {
        QTest::addColumn<bool>("hideMenu");
        QTest::newRow("visible-menu") << false;
        QTest::newRow("hidden-menu") << true;
    }
    void exportShortcutOpensDialogWithoutMerging() {
        QFETCH(bool, hideMenu);
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({32, 24}, true));
        QVERIFY(d->addLayer("Keep separate"));
        d->markSaved();
        w.addDocument(d);
        w.show();
        if (hideMenu)
            w.menuBar()->hide();
        QSignalSpy exported(w.findChild<QAction *>("export"), &QAction::triggered);
        QSignalSpy merged(w.findChild<QAction *>("mergeVisible"), &QAction::triggered);
        const auto revision = d->revision();
        const auto history = d->historyLabels();
        const auto before = d->composite();
        // Cancel twice to verify the shortcut remains usable after the export dialog closes.
        for (int attempt = 1; attempt <= 2; ++attempt) {
            w.activateWindow();
            w.currentCanvas()->setFocus();
            QTest::qWait(30);
            QVERIFY(w.isActiveWindow());
            bool exportDialog = false;
            QVERIFY(driveDialogs(
                [&] {
                    QTest::keyClick(w.currentCanvas(), Qt::Key_E,
                                    Qt::ControlModifier | Qt::ShiftModifier);
                },
                [&](QDialog *dialog) {
                    exportDialog = qobject_cast<QFileDialog *>(dialog) &&
                                   dialog->windowTitle() == "Görüntüyü dışa aktar";
                    dialog->reject();
                }));
            QVERIFY(exportDialog);
            QCOMPARE(exported.count(), attempt);
            QCOMPARE(merged.count(), 0);
            QCOMPARE(d->revision(), revision);
            QCOMPARE(d->historyLabels(), history);
            QCOMPARE(d->state.layers.size(), 2);
            QCOMPARE(d->composite(), before);
            QVERIFY(!d->dirty);
        }
    }
    void mergeShortcutsPreserveAppearanceAndUseOneUndo_data() {
        QTest::addColumn<QString>("actionId");
        QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
        QTest::newRow("visible") << QString("mergeVisible")
                                 << Qt::KeyboardModifiers(Qt::ControlModifier | Qt::AltModifier |
                                                          Qt::ShiftModifier);
        QTest::newRow("down") << QString("mergeDown")
                              << Qt::KeyboardModifiers(Qt::ControlModifier);
    }
    void mergeShortcutsPreserveAppearanceAndUseOneUndo() {
        QFETCH(QString, actionId);
        QFETCH(Qt::KeyboardModifiers, modifiers);
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({32, 24}));
        QVERIFY(d->addLayer("Clipped"));
        QVERIFY(d->addLayer("Hidden"));
        d->beginEdit();
        d->state.layers[0].pixels.fill(QColor(40, 80, 200, 128));
        d->state.layers[0].offset = {-4, -3};
        d->state.layers[1].pixels.fill(Qt::red);
        d->state.layers[1].clipped = true;
        d->state.layers[1].opacity = .6;
        d->state.layers[2].pixels.fill(Qt::green);
        d->state.layers[2].visible = false;
        d->state.active = 1;
        d->commit("Shortcut fixture");
        d->markSaved();
        const auto before = d->composite();
        const auto original = d->state;
        const auto history = d->historyLabels();
        const auto revision = d->revision();
        w.addDocument(d);
        w.show();
        w.menuBar()->hide();
        w.activateWindow();
        w.currentCanvas()->setFocus();
        QTest::qWait(30);
        QVERIFY(w.isActiveWindow());
        auto *action = w.findChild<QAction *>(actionId);
        QVERIFY(action && action->isEnabled());
        QSignalSpy merged(action, &QAction::triggered);
        QSignalSpy exported(w.findChild<QAction *>("export"), &QAction::triggered);
        bool unexpectedDialog = false;
        QTimer dismissUnexpected;
        connect(&dismissUnexpected, &QTimer::timeout, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                unexpectedDialog = true;
                dialog->reject();
            }
        });
        dismissUnexpected.start(10);
        QTest::keyClick(w.currentCanvas(), Qt::Key_E, modifiers);
        dismissUnexpected.stop();
        QVERIFY(!unexpectedDialog);
        QCOMPARE(merged.count(), 1);
        QCOMPARE(exported.count(), 0);
        QCOMPARE(d->state.layers.size(), 2);
        QCOMPARE(d->composite(), before);
        QCOMPARE(d->state.layers[0].offset, QPoint(-4, -3));
        QCOMPARE(d->state.layers[1].name, QString("Hidden"));
        QCOMPARE(d->state.layers[1].pixels, original.layers[2].pixels);
        QVERIFY(!d->state.layers[1].visible);
        QCOMPARE(d->historyLabels().size(), history.size() + 1);
        QVERIFY(d->dirty);
        d->undo();
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->historyLabels(), history);
        QCOMPARE(d->state.layers.size(), original.layers.size());
        QCOMPARE(d->state.active, original.active);
        for (int i = 0; i < original.layers.size(); ++i) {
            QCOMPARE(d->state.layers[i].pixels, original.layers[i].pixels);
            QCOMPARE(d->state.layers[i].offset, original.layers[i].offset);
            QCOMPARE(d->state.layers[i].visible, original.layers[i].visible);
            QCOMPARE(d->state.layers[i].clipped, original.layers[i].clipped);
            QCOMPARE(d->state.layers[i].opacity, original.layers[i].opacity);
        }
        QCOMPARE(d->composite(), before);
        QVERIFY(!d->dirty);
        QVERIFY(d->canRedo());
        d->redo();
        QCOMPARE(d->state.layers.size(), 2);
        QCOMPARE(d->composite(), before);
    }
    void toolOptionsFollowToolsAndDocumentTabs() {
        MainWindow w(false, false);
        auto *first = new Document;
        QVERIFY(first->create({32, 24}, true));
        w.addDocument(first);
        w.show();
        QTest::qWait(20);
        auto *brush = w.findChild<QWidget *>("brushControls");
        auto *mode = w.findChild<QComboBox *>("selectionMode");
        auto *tolerance = w.findChild<QSpinBox *>("selectionTolerance");
        auto *contiguous = w.findChild<QCheckBox *>("contiguousSelection");
        QVERIFY(brush && mode && tolerance && contiguous);
        auto choose = [&](Canvas::Tool tool) {
            w.findChild<QAction *>("tool" + QString::number(tool))->trigger();
            QApplication::processEvents();
        };
        choose(Canvas::Move);
        QVERIFY(!brush->isVisible());
        QVERIFY(!mode->isVisible());
        QVERIFY(!tolerance->isVisible());
        choose(Canvas::Brush);
        QVERIFY(brush->isVisible());
        QVERIFY(!mode->isVisible());
        auto *flow = w.findChild<QSpinBox *>("brushFlow");
        auto *size = w.findChild<QSpinBox *>("brushSize");
        auto *hardness = w.findChild<QSpinBox *>("brushHardness");
        QVERIFY(flow && size && hardness && flow->isVisible());
        int amount = 31;
        for (auto tool : {Canvas::Fill, Canvas::Gradient, Canvas::Rectangle, Canvas::Ellipse}) {
            choose(tool);
            QVERIFY(flow->isVisible());
            QVERIFY(!size->isVisible());
            QVERIFY(!hardness->isVisible());
            flow->setValue(++amount);
            QCOMPARE(w.currentCanvas()->brush.flow, amount);
        }
        choose(Canvas::Wand);
        QVERIFY(!brush->isVisible());
        QVERIFY(mode->isVisible());
        QVERIFY(tolerance->isVisible());
        QVERIFY(contiguous->isVisible());
        auto *second = new Document;
        QVERIFY(second->create({32, 24}, true));
        w.addDocument(second);
        QApplication::processEvents();
        QCOMPARE(w.currentCanvas()->tool, Canvas::Move);
        QVERIFY(!brush->isVisible());
        QVERIFY(!mode->isVisible());
        QVERIFY(!tolerance->isVisible());
        w.findChild<QTabWidget *>("documents")->setCurrentIndex(0);
        QApplication::processEvents();
        QCOMPARE(w.currentCanvas()->tool, Canvas::Wand);
        QVERIFY(mode->isVisible());
        QVERIFY(tolerance->isVisible());
    }
    void selectionMenuAccessibleAtMinimumSize_data() { addPresetRows(); }
    void selectionMenuAccessibleAtMinimumSize() {
        QFETCH(QString, preset);
        MainWindow w(false, false);
        auto *document = new Document;
        QVERIFY(document->create({32, 24}, true));
        document->state.layers[0].pixels.fill(Qt::red);
        QVERIFY(document->setSelection(Selection(QRect(4, 3, 12, 10)), "Fixture"));
        QVERIFY(document->saveSelection("Stored"));
        document->markSaved();
        w.addDocument(document);
        w.applyUiPreset(preset, false);
        w.resize(900, 620);
        w.show();
        w.activateWindow();
        w.currentCanvas()->setFocus();
        QTest::qWait(30);
        auto *menu = w.findChild<QMenu *>("selectionMenu");
        auto *edit = w.findChild<QMenu *>("editMenu");
        QVERIFY(menu && edit);
        QCOMPARE(QString(menu->title()).remove('&'), QString("Seçim"));
        QCOMPARE(declaredAccessKey(menu->title()), QString("S"));
#ifdef Q_OS_MACOS
        QVERIFY(QKeySequence::mnemonic(menu->title()).isEmpty());
#else
        QCOMPARE(QKeySequence::mnemonic(menu->title()), QKeySequence("Alt+S"));
#endif
        const QRect menuPosition = w.menuBar()->actionGeometry(menu->menuAction());
        QVERIFY(!menuPosition.isEmpty());
        QVERIFY(w.menuBar()->rect().contains(menuPosition));
        const QStringList expected = {"selectAll", "deselect", "reselect", "saveSelection",
            "loadSelection", "removeSavedSelection", "invertSelection", "featherSelection",
            "growSelection", "shrinkSelection", "selectLayerPixels"};
        QStringList actual;
        QSet<QString> mnemonics;
        int separators = 0;
        for (auto *action : menu->actions()) {
            if (action->isSeparator()) {
                ++separators;
                continue;
            }
            actual << action->objectName();
            QVERIFY(!edit->actions().contains(action));
            const QString mnemonic = declaredAccessKey(action->text());
            QVERIFY2(hasLabelAccessKey(action->text()), qPrintable(action->objectName()));
            QVERIFY2(!mnemonics.contains(mnemonic), qPrintable(mnemonic));
            mnemonics.insert(mnemonic);
        }
        QCOMPARE(actual, expected);
        QCOMPARE(separators, 3);
        for (const auto *id : {"undo", "redo", "deleteSelection", "copy", "paste",
                               "brushSettings", "brushLibrary"})
            QVERIFY(edit->actions().contains(w.findChild<QAction *>(id)));
        QCOMPARE(w.findChild<QAction *>("selectAll")->shortcut(), QKeySequence(QKeySequence::SelectAll));
        QCOMPARE(w.findChild<QAction *>("deselect")->shortcut(), QKeySequence("Ctrl+D"));
        QCOMPARE(w.findChild<QAction *>("reselect")->shortcut(), QKeySequence("Ctrl+Shift+D"));
        QCOMPARE(w.findChild<QAction *>("invertSelection")->shortcut(), QKeySequence("Ctrl+Shift+I"));
        int alphaMenus = 0;
        for (auto *candidate : w.findChildren<QMenu *>())
            alphaMenus += candidate->actions().contains(w.findChild<QAction *>("selectLayerPixels"));
        QCOMPARE(alphaMenus, 2);
        auto openMenu = [&] {
#ifdef Q_OS_MACOS
            // Qt follows macOS convention and disables Alt-letter mnemonics.
            // Open the menu and verify arrow/Return navigation with real key events.
            menu->popup(w.menuBar()->mapToGlobal(menuPosition.bottomLeft()));
#else
            QTest::keyClick(w.currentCanvas(), Qt::Key_S, Qt::AltModifier);
#endif
            return QTest::qWaitFor([&] { return menu->isVisible(); });
        };
        const auto chooseAction = [&](const char *id, Qt::Key mnemonic) {
#ifdef Q_OS_MACOS
            Q_UNUSED(mnemonic);
            auto *target = w.findChild<QAction *>(id);
            QTest::keyClick(menu, Qt::Key_Home);
            for (int step = 0; step < menu->actions().size() && menu->activeAction() != target; ++step)
                QTest::keyClick(menu, Qt::Key_Down);
            QCOMPARE(menu->activeAction(), target);
            QTest::keyClick(menu, Qt::Key_Return);
#else
            Q_UNUSED(id);
            QTest::keyClick(menu, mnemonic);
#endif
        };
        QVERIFY(openMenu());
        for (auto *action : menu->actions()) {
            if (action->isSeparator())
                continue;
            const QRect actionRect = menu->actionGeometry(action);
            QVERIFY2(!actionRect.isEmpty() && menu->rect().contains(actionRect),
                     qPrintable(action->objectName()));
        }
        const QString screenshots = qEnvironmentVariable("PHOTODECK_CONTROL_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QVERIFY(QDir().mkpath(screenshots));
            QVERIFY(w.grab().save(screenshots + "/selection-window-" + preset + ".png"));
            QVERIFY(menu->grab().save(screenshots + "/selection-menu-" + preset + ".png"));
        }
        chooseAction("selectAll", Qt::Key_T);
        QTRY_VERIFY(!menu->isVisible());
        QCOMPARE(document->selection.bounds(), QRect(QPoint(), document->state.size));
        QVERIFY(openMenu());
        chooseAction("deselect", Qt::Key_K);
        QTRY_VERIFY(document->selection.isEmpty());
        QVERIFY(openMenu());
        chooseAction("reselect", Qt::Key_N);
        QTRY_VERIFY(!document->selection.isEmpty());
        QCOMPARE(document->selection.bounds(), QRect(QPoint(), document->state.size));
        // Esc must leave both document state and subsequent keyboard menu access intact.
        const auto revision = document->revision();
        QVERIFY(openMenu());
        QTest::keyClick(menu, Qt::Key_Escape);
        QTRY_VERIFY(!menu->isVisible());
        QCOMPARE(document->revision(), revision);
        QVERIFY(openMenu());
        QTest::keyClick(menu, Qt::Key_Escape);
        document->markSaved();
    }
    void transformOverlayContrastAtMinimumSize_data() {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QColor>("accent");
        QTest::addColumn<QColor>("panel");
        for (const auto &preset : uiPresets())
            QTest::newRow(qPrintable(preset.id)) << preset.id << QColor() << QColor();
        QTest::newRow("custom-dark-accent") << QString("focus") << QColor("#123025") << QColor();
        QTest::newRow("custom-light-accent") << QString("focus") << QColor("#fffde0") << QColor();
        QTest::newRow("custom-mid-accent") << QString("focus") << QColor("#777777") << QColor();
        QTest::newRow("custom-matching-panel") << QString("focus") << QColor("#555555") << QColor("#555555");
        QTest::newRow("custom-dark-panel") << QString("paper") << QColor("#101112") << QColor("#050708");
        QTest::newRow("custom-light-panel") << QString("focus") << QColor("#fafafa") << QColor("#ffffff");
    }
    void transformOverlayContrastAtMinimumSize() {
        QFETCH(QString, preset);
        QFETCH(QColor, accent);
        QFETCH(QColor, panel);
        MainWindow w(false, false);
        auto *document = new Document;
        document->makeDemo();
        document->state.active = 2;
        w.addDocument(document);
        auto settings = UiSettings::forPreset(preset);
        settings.accent = accent;
        settings.panel = panel;
        w.applyUiSettings(settings, true, false);
        w.resize(900, 620);
        w.show();
        QTest::qWait(30);
        auto *canvas = w.currentCanvas();
        canvas->fit();
        QVERIFY(canvas->beginTransform());
        QApplication::processEvents();
        auto *controls = canvas->findChild<QFrame *>("transformControls");
        auto *apply = controls->findChild<QToolButton *>("transformApply");
        auto *caption = controls->findChild<QLabel *>("transformCaption");
        auto *info = controls->findChild<QLabel *>("transformInfo");
        QVERIFY(controls && apply && caption && info);
        QVERIFY(controls->isVisible());
        QVERIFY(canvas->rect().contains(controls->geometry()));
        const auto colors = settings.resolved();
        const auto checkContrast = [](QWidget *widget, QPalette::ColorRole role, QColor surface) {
            widget->ensurePolished();
            const QColor foreground = widget->palette().color(role);
            const double ratio = renderedContrast(foreground, surface);
            return qMakePair(ratio >= 4.5,
                QString("%1: %2 on %3 is %4:1")
                    .arg(widget->objectName(), foreground.name(), surface.name()).arg(ratio, 0, 'f', 2));
        };
        for (auto *label : {caption, info}) {
            const auto result = checkContrast(label, QPalette::WindowText, colors.panel);
            QVERIFY2(result.first, qPrintable(result.second));
        }
        for (const auto *id : {"transformNumeric", "transformCancel", "transformApply"}) {
            auto *button = controls->findChild<QToolButton *>(id);
            QVERIFY(button && button->isVisible());
            QVERIFY(!button->accessibleName().isEmpty());
            const QColor surface = button == apply ? colors.accent : colors.panel.lighter(135);
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(button, &leave);
            QApplication::processEvents();
            const auto result = checkContrast(button, QPalette::ButtonText, surface);
            QVERIFY2(result.first, qPrintable(result.second));
            // Check real painted surfaces, so a stylesheet/palette mismatch cannot pass unnoticed.
            const QImage normal = button->grab().toImage();
            QCOMPARE(normal.pixelColor(4, button->height() / 2).rgba(), surface.rgba());
            if (button == apply && preset == "focus" && !accent.isValid() && !panel.isValid())
                QCOMPARE(button->palette().color(QPalette::ButtonText), QColor("#15221f"));
            if (button != apply) {
                QEnterEvent enter(QPointF(button->rect().center()), QPointF(button->rect().center()),
                                  QPointF(button->mapToGlobal(button->rect().center())));
                button->setAttribute(Qt::WA_UnderMouse, true);
                QApplication::sendEvent(button, &enter);
                QApplication::processEvents();
                const QColor hoverSurface = colors.panel.lighter(160);
                const QImage hover = button->grab().toImage();
                QCOMPARE(hover.pixelColor(4, button->height() / 2).rgba(), hoverSurface.rgba());
                // Qt keeps the normal palette while drawing a state-specific stylesheet color.
                // Capture the rendered text below instead of trusting its normal-state palette.
                QSet<QRgb> foregroundPixels;
                for (int y = 0; y < hover.height(); ++y)
                    for (int x = 0; x < hover.width(); ++x) {
                        const QColor color = hover.pixelColor(x, y);
                        if (renderedContrast(color, hoverSurface) >= 4.5)
                            foregroundPixels.insert(color.rgb());
                    }
                QVERIFY2(!foregroundPixels.isEmpty(), qPrintable(QString(id) + " hover text contrast"));
                button->setAttribute(Qt::WA_UnderMouse, false);
                QApplication::sendEvent(button, &leave);
            }
        }
        if (preset == "focus" && !accent.isValid() && !panel.isValid()) {
            const QString currentStyle = controls->styleSheet();
            const QImage current = controls->grab().toImage();
            // Pixel-for-pixel check against the original Focus overlay, including hover reset.
            controls->setStyleSheet(
                QString("QFrame#transformControls { background: %1; border: 1px solid %2; border-radius: 9px; }"
                        "QFrame#transformControls QLabel { color: %3; border: none; background: transparent; }"
                        "QLabel#transformCaption { color: %2; font-size: 11px; font-weight: 700; }"
                        "QLabel#transformInfo { font-size: 10px; }"
                        "QFrame#transformControls QToolButton { color: %4; background: %5; border: none; "
                        "border-radius: 5px; padding: 6px 9px; font-size: 11px; }"
                        "QFrame#transformControls QToolButton:hover { background: %6; }"
                        "QFrame#transformControls QToolButton#transformApply { color: #15221f; "
                        "background: %2; font-weight: 700; }")
                    .arg(colors.panel.name(), colors.accent.name(), colors.muted.name(),
                         colors.muted.lighter(160).name(), colors.panel.lighter(135).name(),
                         colors.panel.lighter(160).name()));
            QApplication::processEvents();
            QCOMPARE(controls->grab().toImage(), current);
            controls->setStyleSheet(currentStyle);
        }
        const QString screenshots = qEnvironmentVariable("PHOTODECK_CONTROL_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QVERIFY(QDir().mkpath(screenshots));
            const QString name = QString::fromLatin1(QTest::currentDataTag());
            QVERIFY(w.grab().save(screenshots + "/transform-window-" + name + ".png"));
            QVERIFY(controls->grab().save(screenshots + "/transform-controls-" + name + ".png"));
        }
        canvas->cancelTransform();
    }
    void focusPanelsLeaveCanvasUncovered_data() {
        QTest::addColumn<QSize>("windowSize");
        QTest::newRow("minimum") << QSize(900, 620);
        QTest::newRow("laptop") << QSize(1280, 800);
    }
    void focusPanelsLeaveCanvasUncovered() {
        QFETCH(QSize, windowSize);
        MainWindow w(false, false);
        auto *document = new Document;
        QVERIFY(document->create({32, 24}, true));
        w.addDocument(document);
        w.resize(windowSize);
        w.show();
        QTest::qWait(30);
        auto *workspace = w.findChild<QWidget *>("workspace");
        auto *tools = w.findChild<QFrame *>("floatingTools");
        auto *layers = w.findChild<QFrame *>("floatingLayers");
        QVERIFY(workspace && tools && layers);
        auto viewport = [&] {
            return QRect(w.currentCanvas()->mapTo(workspace, QPoint()), w.currentCanvas()->size());
        };
        QVERIFY(workspace->rect().contains(tools->geometry()));
        QVERIFY(workspace->rect().contains(layers->geometry()));
        QVERIFY(!viewport().intersects(tools->geometry()));
        QVERIFY(!viewport().intersects(layers->geometry()));
        const int fullWidth = viewport().width();
        auto *layerAction = w.findChild<QAction *>("ui.panel.layers");
        layerAction->setChecked(false);
        QApplication::processEvents();
        QVERIFY(layers->isHidden());
        QVERIFY(viewport().width() > fullWidth);
        auto *toolAction = w.findChild<QAction *>("ui.panel.tools");
        toolAction->setChecked(false);
        QApplication::processEvents();
        const int unclutteredWidth = viewport().width();
        QVERIFY(unclutteredWidth > fullWidth);
        layerAction->setChecked(true);
        toolAction->setChecked(true);
        QApplication::processEvents();
        QVERIFY(!viewport().intersects(tools->geometry()));
        QVERIFY(!viewport().intersects(layers->geometry()));
        QCOMPARE(viewport().width(), fullWidth);
        for (int i = 0; i < Canvas::ToolCount; ++i) {
            auto *button = w.findChild<QToolButton *>("toolButton" + QString::number(i));
            QVERIFY(button && !button->accessibleName().isEmpty());
        }
        for (int i = 0; i < uiPresets().size(); ++i)
            QCOMPARE(w.findChild<QAction *>("ui.preset." + uiPresets()[i].id)->shortcut(),
                     QKeySequence("Ctrl+Alt+" + QString::number((i + 1) % 10)));
    }
    void oldProjectsSaveAsCopy_data() {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<int>("version");
        QTest::newRow("v1") << QFINDTESTDATA("../examples/Horizon.pdeck") << 1;
        QTest::newRow("v2") << QFINDTESTDATA("fixtures/v2-demo.pdeck") << 2;
        QTest::newRow("v3") << QFINDTESTDATA("fixtures/v3-demo.pdeck") << 3;
        QTest::newRow("v4") << QFINDTESTDATA("fixtures/v4-demo.pdeck") << 4;
    }
    void oldProjectsSaveAsCopy() {
        QFETCH(QString, fixture);
        QFETCH(int, version);
        QVERIFY(!fixture.isEmpty());
        QTemporaryDir temp;
        const QString original = temp.filePath("legacy.pdeck"), copy = temp.filePath("copy.pdeck");
        QVERIFY(QFile::copy(fixture, original));
        const auto before = contents(original);
        MainWindow w(false, false);
        QVERIFY(w.openFile(original));
        auto *d = w.currentDocument();
        QCOMPARE(d->sourceVersion(), version);
        paint(d, Qt::red);
        auto *save = w.findChild<QAction *>("save");
        bool canceled = false;
        QVERIFY(driveDialogs(save, [&](QDialog *dialog) {
            auto *box = qobject_cast<QMessageBox *>(dialog);
            if (box && box->objectName() == "projectUpgradeWarning") {
                canceled = true;
                box->button(QMessageBox::Cancel)->click();
            } else dialog->reject();
        }));
        QVERIFY(canceled);
        QCOMPARE(contents(original), before);
        QVERIFY(d->dirty);
        bool warning = false, chooser = false;
        QVERIFY(driveDialogs(save, [&](QDialog *dialog) {
            if (dialog->objectName() == "projectUpgradeWarning") {
                warning = true;
                dialog->findChild<QPushButton *>("saveProjectCopy")->click();
            } else if (auto *files = qobject_cast<QFileDialog *>(dialog)) {
                chooser = true;
                auto *name = files->findChild<QLineEdit *>("fileNameEdit");
                auto *buttons = files->findChild<QDialogButtonBox *>();
                auto *saveCopy = buttons ? buttons->button(QDialogButtonBox::Save) : nullptr;
                QVERIFY(name && saveCopy);
                QCOMPARE(files->directory().absolutePath(), temp.path());
                // selectFile() leaves a visible, focused filename edit unchanged in Qt.
                // Replace the name as a user would, then exercise the real Save button.
                QTest::mouseClick(name, Qt::LeftButton);
                name->selectAll();
                QTest::keyClicks(name, QFileInfo(copy).fileName());
                QCOMPARE(files->selectedFiles(), QStringList{copy});
                QTRY_VERIFY(saveCopy->isEnabled());
                QTest::mouseClick(saveCopy, Qt::LeftButton);
            } else dialog->reject();
        }));
        QVERIFY(warning && chooser);
        QCOMPARE(contents(original), before);
        QCOMPARE(d->path, copy);
        QVERIFY(!d->dirty);
        Document loaded;
        QString error;
        QVERIFY2(loaded.load(copy, &error), qPrintable(error));
        QCOMPARE(loaded.sourceVersion(), loaded.projectSaveVersion());
        QCOMPARE(loaded.composite(), d->composite());
    }
    void namedSelectionDialogLabelsAndKeyboard_data() {
        QTest::addColumn<QString>("actionId");
        QTest::addColumn<QString>("dialogName");
        QTest::addColumn<QString>("verb");
        QTest::newRow("save") << QString("saveSelection") << QString("saveSelectionDialog")
                              << QString("Kaydet");
        QTest::newRow("load") << QString("loadSelection") << QString("loadSelectionDialog")
                              << QString("Yükle");
        QTest::newRow("remove") << QString("removeSavedSelection")
                                << QString("removeSavedSelectionDialog") << QString("Sil");
    }
    void namedSelectionDialogLabelsAndKeyboard() {
        QFETCH(QString, actionId);
        QFETCH(QString, dialogName);
        QFETCH(QString, verb);
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({40, 30}, true));
        const Selection stored(1, 1, 6, 6), current(2, 2, 10, 8);
        QVERIFY(d->setSelection(stored));
        QVERIFY(d->saveSelection("Alan"));
        QVERIFY(d->setSelection(current));
        d->markSaved();
        w.addDocument(d);
        w.show();
        QTest::qWait(30);
        auto *action = w.findChild<QAction *>(actionId);
        QVERIFY(action && action->isEnabled());
        const auto revision = d->revision();
        const auto history = d->historyLabels();
        const auto pixels = d->composite();
        const auto previous = d->state.previousSelection.mask(d->state.size);
        const QString screenshots = qEnvironmentVariable("PHOTODECK_DIALOG_SCREENSHOTS");
        // Dismiss after editing twice, first with Esc, then the real Cancel button.
        for (bool escape : {true, false}) {
            QVERIFY(driveDialogs(action, [&](QDialog *dialog) {
                QCOMPARE(dialog->objectName(), dialogName);
                QCOMPARE(dialog->accessibleName(), dialog->windowTitle());
                auto *buttons = dialog->findChild<QDialogButtonBox *>();
                QVERIFY(buttons);
                auto *accept = buttons->button(QDialogButtonBox::Ok);
                auto *cancel = buttons->button(QDialogButtonBox::Cancel);
                QVERIFY(accept && cancel && accept->isVisible() && cancel->isVisible());
                QCOMPARE(QString(accept->text()).remove('&'), verb);
                QCOMPARE(QString(cancel->text()).remove('&'), QString("İptal"));
                QCOMPARE(accept->accessibleName(), verb);
                QCOMPARE(cancel->accessibleName(), QString("İptal"));
                QVERIFY(hasLabelAccessKey(accept->text()));
                QVERIFY(hasLabelAccessKey(cancel->text()));
                QVERIFY(declaredAccessKey(accept->text()) != declaredAccessKey(cancel->text()));
                QWidget *field = nullptr;
                if (actionId == "saveSelection") {
                    auto *name = dialog->findChild<QLineEdit *>("savedSelectionName");
                    QVERIFY(name);
                    QCOMPARE(name->accessibleName(), QString("Kayıtlı seçim adı"));
                    name->setText("Yeni alan");
                    field = name;
                } else {
                    auto *names = dialog->findChild<QComboBox *>("savedSelectionChoice");
                    QVERIFY(names);
                    QCOMPARE(names->accessibleName(), QString("Kayıtlı seçim"));
                    QVERIFY(!names->isEditable());
                    field = names;
                    if (actionId == "loadSelection") {
                        auto *mode = dialog->findChild<QComboBox *>("savedSelectionMode");
                        QVERIFY(mode);
                        QCOMPARE(mode->accessibleName(), QString("Seçim yükleme işlemi"));
                        mode->setCurrentIndex(Selection::Subtract);
                    }
                }
                QVERIFY(field->hasFocus());
                bool buddy = false;
                for (auto *label : dialog->findChildren<QLabel *>())
                    buddy |= label->buddy() == field && hasLabelAccessKey(label->text());
                QVERIFY(buddy);
                if (escape && !screenshots.isEmpty()) {
                    QVERIFY(QDir().mkpath(screenshots));
                    QVERIFY(dialog->grab().save(screenshots + "/" + actionId + ".png"));
                }
                if (escape)
                    QTest::keyClick(field, Qt::Key_Escape);
                else
                    QTest::mouseClick(cancel, Qt::LeftButton);
                QCOMPARE(dialog->result(), int(QDialog::Rejected));
            }));
            QCOMPARE(d->revision(), revision);
            QCOMPARE(d->historyLabels(), history);
            QCOMPARE(d->composite(), pixels);
            QCOMPARE(d->selection.mask(d->state.size), current.mask(d->state.size));
            QCOMPARE(d->state.previousSelection.mask(d->state.size), previous);
            QCOMPARE(d->savedSelectionNames(), QStringList{"Alan"});
            QCOMPARE(d->state.savedSelections[0].selection.mask(d->state.size),
                     stored.mask(d->state.size));
            QVERIFY(!d->dirty);
        }
        // Enter keeps the original action's semantics and one-step undo behavior.
        QVERIFY(driveDialogs(action, [&](QDialog *dialog) {
            QWidget *field = nullptr;
            if (actionId == "saveSelection") {
                auto *name = dialog->findChild<QLineEdit *>("savedSelectionName");
                QVERIFY(name);
                name->setText("Yeni alan");
                field = name;
            } else {
                field = dialog->findChild<QComboBox *>("savedSelectionChoice");
                QVERIFY(field);
                if (actionId == "loadSelection") {
                    auto *mode = dialog->findChild<QComboBox *>("savedSelectionMode");
                    QVERIFY(mode);
                    mode->setCurrentIndex(Selection::Subtract);
                }
            }
            QTest::keyClick(field, Qt::Key_Return);
            QCOMPARE(dialog->result(), int(QDialog::Accepted));
        }));
        QCOMPARE(d->historyLabels().size(), history.size() + 1);
        if (actionId == "saveSelection") {
            QCOMPARE(d->savedSelectionNames(), QStringList({"Alan", "Yeni alan"}));
            QCOMPARE(d->state.savedSelections[1].selection.mask(d->state.size),
                     current.mask(d->state.size));
            QVERIFY(d->dirty);
        } else if (actionId == "loadSelection") {
            QCOMPARE(d->selection.mask(d->state.size),
                     current.combined(stored, Selection::Subtract, d->state.size).mask(d->state.size));
            QVERIFY(!d->dirty);
        } else {
            QVERIFY(d->savedSelectionNames().isEmpty() && d->dirty);
        }
        QCOMPARE(d->composite(), pixels);
        d->undo();
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->savedSelectionNames(), QStringList{"Alan"});
        QCOMPARE(d->selection.mask(d->state.size), current.mask(d->state.size));
        QVERIFY(!d->dirty);
    }
    void duplicateSelectionConfirmationDefaultsToCancellation_data() {
        QTest::addColumn<int>("dismissal");
        QTest::newRow("enter-cancels") << 0;
        QTest::newRow("escape-cancels") << 1;
        QTest::newRow("explicit-replace") << 2;
    }
    void duplicateSelectionConfirmationDefaultsToCancellation() {
        QFETCH(int, dismissal);
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({40, 30}, true));
        const Selection stored(1, 1, 6, 6), current(2, 2, 10, 8);
        QVERIFY(d->setSelection(stored));
        QVERIFY(d->saveSelection("Alan"));
        QVERIFY(d->setSelection(current));
        d->markSaved();
        w.addDocument(d);
        w.show();
        w.activateWindow();
        QTRY_VERIFY(w.isActiveWindow());
        const auto revision = d->revision();
        const auto history = d->historyLabels();
        const auto pixels = d->composite();
        int dialogs = 0;
        QVERIFY(driveDialogs(w.findChild<QAction *>("saveSelection"), [&](QDialog *dialog) {
            ++dialogs;
            if (auto *input = qobject_cast<QInputDialog *>(dialog)) {
                auto *name = input->findChild<QLineEdit *>("savedSelectionName");
                QVERIFY(name);
                name->setText("Alan");
                QTest::keyClick(name, Qt::Key_Return);
            } else {
                auto *box = qobject_cast<QMessageBox *>(dialog);
                QVERIFY(box);
                QCOMPARE(box->objectName(), QString("replaceSavedSelectionDialog"));
                auto *replace = box->button(QMessageBox::Yes);
                auto *cancel = box->button(QMessageBox::Cancel);
                QVERIFY(replace && cancel);
                QCOMPARE(QString(replace->text()).remove('&'), QString("Değiştir"));
                QCOMPARE(QString(cancel->text()).remove('&'), QString("İptal"));
                QCOMPARE(replace->accessibleName(), QString("Kayıtlı seçimi değiştir"));
                QCOMPARE(cancel->accessibleName(), QString("İptal"));
                QCOMPARE(box->defaultButton(), cancel);
                QCOMPARE(box->escapeButton(), cancel);
                // Modal visibility precedes activation/focus delivery on macOS.
                // Observe the production default focus after activation without
                // assigning it in the harness, which could hide a safety bug.
                QTRY_VERIFY(box->isActiveWindow());
                QTRY_VERIFY(cancel->hasFocus());
                const QString screenshots = qEnvironmentVariable("PHOTODECK_DIALOG_SCREENSHOTS");
                if (!screenshots.isEmpty()) {
                    QVERIFY(QDir().mkpath(screenshots));
                    QVERIFY(box->grab().save(screenshots + "/replaceSavedSelection.png"));
                }
                if (dismissal == 2)
                    QTest::mouseClick(replace, Qt::LeftButton);
                else
                    QTest::keyClick(box, dismissal == 0 ? Qt::Key_Return : Qt::Key_Escape);
            }
        }));
        QCOMPARE(dialogs, 2);
        QCOMPARE(d->savedSelectionNames(), QStringList{"Alan"});
        QCOMPARE(d->state.savedSelections[0].selection.mask(d->state.size),
                 (dismissal == 2 ? current : stored).mask(d->state.size));
        QCOMPARE(d->selection.mask(d->state.size), current.mask(d->state.size));
        QCOMPARE(d->composite(), pixels);
        QCOMPARE(d->dirty, dismissal == 2);
        if (dismissal == 2) {
            QCOMPARE(d->historyLabels().size(), history.size() + 1);
            d->undo();
            QCOMPARE(d->state.savedSelections[0].selection.mask(d->state.size),
                     stored.mask(d->state.size));
            QVERIFY(!d->dirty);
        } else {
            QCOMPARE(d->historyLabels(), history);
        }
        QCOMPARE(d->revision(), revision);
    }
    void layerPositionDialogLabelsAndKeyboard() {
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({40, 30}, true));
        w.addDocument(d);
        w.show();
        QTest::qWait(30);
        auto *action = w.findChild<QAction *>("positionLayer");
        QVERIFY(action && action->isEnabled());
        const auto revision = d->revision();
        const auto history = d->historyLabels();
        const auto pixels = d->composite();
        const auto bounds = d->activeLayerBounds();
        for (bool escape : {true, false}) {
            QVERIFY(driveDialogs(action, [&](QDialog *dialog) {
                QCOMPARE(dialog->objectName(), QString("layerPositionDialog"));
                QCOMPARE(dialog->accessibleName(), dialog->windowTitle());
                auto *x = dialog->findChild<QSpinBox *>("layerPositionX");
                auto *y = dialog->findChild<QSpinBox *>("layerPositionY");
                auto *buttons = dialog->findChild<QDialogButtonBox *>();
                QVERIFY(x && y && buttons);
                auto *apply = buttons->button(QDialogButtonBox::Ok);
                auto *cancel = buttons->button(QDialogButtonBox::Cancel);
                QVERIFY(apply && cancel);
                QCOMPARE(QString(apply->text()).remove('&'), QString("Uygula"));
                QCOMPARE(QString(cancel->text()).remove('&'), QString("İptal"));
                QCOMPARE(apply->accessibleName(), QString("Uygula"));
                QCOMPARE(cancel->accessibleName(), QString("İptal"));
                QCOMPARE(x->accessibleName(), QString("Dönüşmüş katman sol kenarı X"));
                QCOMPARE(y->accessibleName(), QString("Dönüşmüş katman üst kenarı Y"));
                QVERIFY(x->hasFocus());
                QTest::keyClick(x, Qt::Key_Tab);
                QVERIFY(y->hasFocus());
                for (auto *field : {x, y}) {
                    bool buddy = false;
                    for (auto *label : dialog->findChildren<QLabel *>())
                        buddy |= label->buddy() == field && hasLabelAccessKey(label->text());
                    QVERIFY(buddy);
                }
                x->setValue(-12);
                y->setValue(17);
                const QString screenshots = qEnvironmentVariable("PHOTODECK_DIALOG_SCREENSHOTS");
                if (escape && !screenshots.isEmpty()) {
                    QVERIFY(QDir().mkpath(screenshots));
                    QVERIFY(dialog->grab().save(screenshots + "/positionLayer.png"));
                }
                if (escape)
                    QTest::keyClick(y, Qt::Key_Escape);
                else
                    QTest::mouseClick(cancel, Qt::LeftButton);
                QCOMPARE(dialog->result(), int(QDialog::Rejected));
            }));
            QCOMPARE(d->revision(), revision);
            QCOMPARE(d->historyLabels(), history);
            QCOMPARE(d->activeLayerBounds(), bounds);
            QCOMPARE(d->composite(), pixels);
            QVERIFY(!d->dirty);
        }
        QVERIFY(driveDialogs(action, [&](QDialog *dialog) {
            auto *x = dialog->findChild<QSpinBox *>("layerPositionX");
            auto *y = dialog->findChild<QSpinBox *>("layerPositionY");
            QVERIFY(x && y);
            x->setValue(-12);
            y->setValue(17);
            QTest::keyClick(y, Qt::Key_Return);
            QCOMPARE(dialog->result(), int(QDialog::Accepted));
        }));
        QCOMPARE(d->activeLayerBounds().topLeft(), QPoint(-12, 17));
        QCOMPARE(d->historyLabels().size(), history.size() + 1);
        QVERIFY(d->dirty);
        d->undo();
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->activeLayerBounds(), bounds);
        QCOMPARE(d->composite(), pixels);
        QVERIFY(!d->dirty);
    }
    void namedSelectionDialogsAndReselectAreUndoable() {
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({40, 30}));
        w.addDocument(d);
        auto action = [&](const char *name) { return w.findChild<QAction *>(name); };
        QVERIFY(!action("reselect")->isEnabled());
        QVERIFY(!action("saveSelection")->isEnabled());
        QVERIFY(!action("loadSelection")->isEnabled());
        action("selectAll")->trigger();
        QVERIFY(action("saveSelection")->isEnabled());
        const quint64 selected = d->revision();
        QVERIFY(driveDialogs(action("saveSelection"), [&](QDialog *dialog) { dialog->reject(); }));
        QCOMPARE(d->revision(), selected);
        QVERIFY(d->savedSelectionNames().isEmpty() && !d->dirty);
        QVERIFY(driveDialogs(action("saveSelection"), [&](QDialog *dialog) {
            auto *input = qobject_cast<QInputDialog *>(dialog);
            QVERIFY(input);
            input->setTextValue("All pixels");
            input->accept();
        }));
        QCOMPARE(d->savedSelectionNames(), QStringList{"All pixels"});
        QVERIFY(d->dirty && action("loadSelection")->isEnabled());
        d->markSaved();
        action("deselect")->trigger();
        QVERIFY(action("reselect")->isEnabled());
        action("reselect")->trigger();
        QVERIFY(d->selection == QRect(0, 0, 40, 30));
        QVERIFY(!d->dirty);
        const quint64 beforeLoad = d->revision();
        QVERIFY(driveDialogs(action("loadSelection"), [&](QDialog *dialog) { dialog->reject(); }));
        QCOMPARE(d->revision(), beforeLoad);
        QVERIFY(driveDialogs(action("loadSelection"), [&](QDialog *dialog) {
            QCOMPARE(dialog->objectName(), QString("loadSelectionDialog"));
            auto *mode = dialog->findChild<QComboBox *>("savedSelectionMode");
            QVERIFY(mode);
            mode->setCurrentIndex(Selection::Subtract);
            dialog->accept();
        }));
        QVERIFY(!d->selection.isEmpty() && !d->selection.hasPixels());
        QVERIFY(!d->dirty);
        action("undo")->trigger();
        QVERIFY(d->selection == QRect(0, 0, 40, 30));
        const quint64 beforeReplace = d->revision();
        bool replacementWarning = false;
        QVERIFY(driveDialogs(action("saveSelection"), [&](QDialog *dialog) {
            if (auto *input = qobject_cast<QInputDialog *>(dialog)) {
                input->setTextValue("All pixels");
                input->accept();
            } else if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
                replacementWarning = true;
                box->button(QMessageBox::Cancel)->click();
            } else dialog->reject();
        }));
        QVERIFY(replacementWarning);
        QCOMPARE(d->revision(), beforeReplace);
        QVERIFY(driveDialogs(action("removeSavedSelection"), [&](QDialog *dialog) { dialog->reject(); }));
        QCOMPARE(d->revision(), beforeReplace);
        QVERIFY(driveDialogs(action("removeSavedSelection"), [&](QDialog *dialog) { dialog->accept(); }));
        QVERIFY(d->savedSelectionNames().isEmpty() && d->dirty);
        action("undo")->trigger();
        QCOMPARE(d->savedSelectionNames(), QStringList{"All pixels"});
        QVERIFY(!d->dirty);
        auto *second = new Document;
        QVERIFY(second->create({10, 10}));
        w.addDocument(second);
        QVERIFY(!action("reselect")->isEnabled());
        QVERIFY(!action("loadSelection")->isEnabled());
        QVERIFY(!action("saveSelection")->isEnabled());
    }
    void namedSelectionsProtectV5OriginalAndCopyAsV6() {
        QTemporaryDir temp;
        const QString original = temp.filePath("v5.pdeck"), copy = temp.filePath("named-v6.pdeck");
        const QString fixture = QFINDTESTDATA("fixtures/v5-demo.pdeck");
        QVERIFY(!fixture.isEmpty() && QFile::copy(fixture, original));
        const QByteArray baseline = contents(original);
        MainWindow w(false, false);
        QVERIFY(w.openFile(original));
        auto *d = w.currentDocument();
        QString error;
        QVERIFY(d->setSelection(Selection(10, 15, 30, 40)));
        QVERIFY(d->saveSelection("Sky", &error));
        const quint64 revision = d->revision();
        auto *save = w.findChild<QAction *>("save");
        bool warned = false;
        QVERIFY(driveDialogs(save, [&](QDialog *dialog) {
            auto *box = qobject_cast<QMessageBox *>(dialog);
            QVERIFY(box);
            QCOMPARE(box->objectName(), QString("projectUpgradeWarning"));
            QVERIFY(box->text().contains("v6") && box->text().contains("0.9.1"));
            warned = true;
            box->button(QMessageBox::Cancel)->click();
        }));
        QVERIFY(warned && d->dirty);
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->path, original);
        QCOMPARE(contents(original), baseline);
        bool chooser = false;
        QVERIFY(driveDialogs(save, [&](QDialog *dialog) {
            if (dialog->objectName() == "projectUpgradeWarning") {
                dialog->findChild<QPushButton *>("saveProjectCopy")->click();
            } else if (auto *files = qobject_cast<QFileDialog *>(dialog)) {
                chooser = true;
                auto *name = files->findChild<QLineEdit *>("fileNameEdit");
                auto *buttons = files->findChild<QDialogButtonBox *>();
                QVERIFY(name && buttons);
                QTest::mouseClick(name, Qt::LeftButton);
                name->selectAll();
                QTest::keyClicks(name, QFileInfo(copy).fileName());
                QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
            } else dialog->reject();
        }));
        QVERIFY(chooser);
        QCOMPARE(contents(original), baseline);
        QCOMPARE(d->path, copy);
        QCOMPARE(d->sourceVersion(), 6);
        QVERIFY(!d->dirty);
        Document loaded;
        QVERIFY2(loaded.load(copy, &error), qPrintable(error));
        QCOMPARE(loaded.savedSelectionNames(), QStringList{"Sky"});
        QVERIFY(loaded.state.savedSelections[0].selection == QRect(10, 15, 30, 40));
    }
    void newNamedSelectionProjectDisclosesCompatibilityAndCancelsSafely() {
        QTemporaryDir temp;
        const QString file = temp.filePath("new-v6.pdeck");
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({20, 10}));
        w.addDocument(d);
        QString error;
        QVERIFY(d->setSelection(Selection(1, 2, 3, 4)));
        QVERIFY(d->saveSelection("Small", &error));
        const quint64 revision = d->revision();
        auto *save = w.findChild<QAction *>("save");
        for (bool accept : {false, true}) {
            bool warning = false;
            QVERIFY(driveDialogs(save, [&](QDialog *dialog) {
                if (auto *files = qobject_cast<QFileDialog *>(dialog)) {
                    files->setDirectory(temp.path());
                    auto *name = files->findChild<QLineEdit *>("fileNameEdit");
                    auto *buttons = files->findChild<QDialogButtonBox *>();
                    QVERIFY(name && buttons);
                    QTest::mouseClick(name, Qt::LeftButton);
                    name->selectAll();
                    QTest::keyClicks(name, QFileInfo(file).fileName());
                    QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
                } else if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
                    QCOMPARE(box->objectName(), QString("savedSelectionFormatWarning"));
                    QVERIFY(box->text().contains("v6") && box->text().contains("0.9.1"));
                    warning = true;
                    box->button(accept ? QMessageBox::Save : QMessageBox::Cancel)->click();
                } else dialog->reject();
            }));
            QVERIFY(warning);
            QCOMPARE(QFileInfo::exists(file), accept);
            QCOMPARE(d->revision(), revision);
            QCOMPARE(d->dirty, !accept);
            QCOMPARE(d->path, accept ? file : QString());
        }
    }
    void externalConflictCancelThenExplicitOverwrite() {
        QTemporaryDir temp;
        const QString file = temp.filePath("project.pdeck");
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({32, 24}));
        QString error;
        QVERIFY(d->save(file, &error));
        w.addDocument(d);
        paint(d, Qt::red);
        Document external;
        QVERIFY(external.create({32, 24}));
        paint(&external, Qt::blue);
        QVERIFY(external.save(file, &error));
        const auto disk = contents(file);
        auto *save = w.findChild<QAction *>("save");
        bool canceled = false;
        QVERIFY(driveDialogs(save, [&](QDialog *dialog) {
            if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
                canceled = box->objectName() == "projectConflictWarning";
                box->button(QMessageBox::Cancel)->click();
            } else dialog->reject();
        }));
        QVERIFY(canceled);
        QCOMPARE(contents(file), disk);
        QVERIFY(d->dirty);
        bool approved = false;
        QVERIFY(driveDialogs(save, [&](QDialog *dialog) {
            if (dialog->objectName() == "projectConflictWarning") {
                approved = true;
                dialog->findChild<QPushButton *>("overwriteProject")->click();
            } else dialog->reject();
        }));
        QVERIFY(approved);
        QVERIFY(!d->dirty);
        Document reread;
        QVERIFY(reread.load(file, &error));
        QCOMPARE(reread.composite().pixelColor(0, 0), QColor(Qt::red));
    }
    void aboutAndHelpMatchCurrentBehavior() {
        MainWindow w(false, false);
        bool about = false, help = false;
        QString aboutTitle, aboutText;
        QVERIFY(driveDialogs(w.findChild<QAction *>("about"), [&](QDialog *dialog) {
            if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
                about = true;
                aboutTitle = box->windowTitle();
                aboutText = box->text();
                box->accept();
            } else dialog->reject();
        }));
        QVERIFY(about);
        QVERIFY2(aboutText.contains(PhotoDeck::Version),
                 qPrintable("About body is missing the version: " + aboutText));
        QVERIFY2(aboutText.contains("RGB ICC"),
                 qPrintable("About body is missing ICC support text: " + aboutText));
        // QMessageBox deliberately ignores its window title on macOS.
#ifndef Q_OS_MACOS
        QVERIFY2(aboutTitle.contains(PhotoDeck::Version),
                 qPrintable("About title is missing the version: " + aboutTitle));
#endif
        QVERIFY(driveDialogs(w.findChild<QAction *>("help"), [&](QDialog *dialog) {
            if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
                help = box->text().contains("Ctrl+T katman dönüşümü") &&
                       box->text().contains("5 saniyede") &&
                       !box->text().contains("Dönüştürme, çizim ve silme");
                box->accept();
            } else dialog->reject();
        }));
        QVERIFY(help);
    }
    void earlyRecoveryAndDeferredGesture() {
        auto *w = new MainWindow(false, false);
        auto *d = new Document;
        QVERIFY(d->create({32, 24}));
        w->addDocument(d);
        auto *periodic = w->findChild<QTimer *>("autosaveTimer");
        auto *afterEdit = w->findChild<QTimer *>("recoverySaveTimer");
        QVERIFY(periodic && afterEdit);
        QCOMPARE(periodic->interval(), 45000);
        paint(d, Qt::red);
        QVERIFY(afterEdit->isActive());
        QCOMPARE(afterEdit->interval(), 5000);
        const QDir dir(recoveryDirectory());
        QVERIFY(dir.entryList({"*.pdeck"}, QDir::Files).isEmpty());
        d->beginEdit();
        d->state.layers[0].pixels.fill(Qt::cyan);
        d->changed();
        // Force the actual recovery timer to become due during a long gesture.
        afterEdit->start(1);
        QTest::qWait(30);
        QVERIFY(d->isEditing());
        QVERIFY(dir.entryList({"*.pdeck"}, QDir::Files).isEmpty());
        d->commit("Long stroke");
        QTRY_COMPARE_WITH_TIMEOUT(dir.entryList({"*.pdeck"}, QDir::Files).size(), 1, 1500);
        const QString snapshot = dir.filePath(dir.entryList({"*.pdeck"}, QDir::Files).first());
        Document recovered;
        QString error;
        QVERIFY2(recovered.load(snapshot, &error), qPrintable(error));
        QCOMPARE(recovered.composite().pixelColor(0, 0), QColor(Qt::cyan));
        QVERIFY(d->dirty);
        QVERIFY(d->path.isEmpty());
        delete w; // Crash simulation: no closeEvent, so the snapshot survives.
        QVERIFY(QFile::exists(snapshot));
    }
    void previewsReuseUnchangedPixelsAndDeferLiveStroke() {
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({1400, 1000}, true));
        w.addDocument(d);
        w.applyUiPreset("film", false);
        auto *layers = w.findChild<QListWidget *>("layers");
        auto *strip = w.findChild<QListWidget *>("filmstrip");
        QVERIFY(layers && strip && layers->count() == 1 && strip->count() == 1);
        const auto layerIcon = layers->item(0)->icon().cacheKey();
        const auto filmIcon = strip->item(0)->icon().cacheKey();
        for (int i = 0; i < 3; ++i) {
            d->selection = Selection(QRect(0, 0, 10 + i, 10));
            d->changed();
            QTest::qWait(35);
        }
        QCOMPARE(layers->item(0)->icon().cacheKey(), layerIcon);
        QCOMPARE(strip->item(0)->icon().cacheKey(), filmIcon);
        d->beginEdit();
        d->state.layers[0].pixels.fill(Qt::red);
        d->changed();
        QTest::qWait(35);
        QCOMPARE(layers->item(0)->icon().cacheKey(), layerIcon);
        QCOMPARE(strip->item(0)->icon().cacheKey(), filmIcon);
        d->commit("Stroke");
        QTRY_VERIFY(layers->item(0)->icon().cacheKey() != layerIcon);
        QTRY_VERIFY(strip->item(0)->icon().cacheKey() != filmIcon);
        QCOMPARE(d->composite().pixelColor(0, 0), QColor(Qt::red));
    }
};
QTEST_MAIN(UiResilienceTests)
#include "test_ui_resilience.moc"
