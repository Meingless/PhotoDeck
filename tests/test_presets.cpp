#include "mainwindow.h"
#include <QAction>
#include <QBuffer>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <cmath>
namespace {
void mouse(Canvas &c, QEvent::Type type, QPointF point,
           Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QPointF p = c.toWidget(point);
    QMouseEvent e(type, p, c.mapToGlobal(p.toPoint()),
                  type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                  type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
    QApplication::sendEvent(&c, &e);
}
void tap(Canvas &c, QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    mouse(c, QEvent::MouseButtonPress, at, modifiers);
    mouse(c, QEvent::MouseButtonRelease, at, modifiers);
}
void ready(Canvas &c) {
    c.resize(400, 400);
    c.show();
    c.actualSize();
    c.setTool(Canvas::Brush);
    c.brush.size = 20;
    c.brush.hardness = 100;
    c.foreground = Qt::red;
}
QImage tip() {
    QImage source(10, 10, QImage::Format_ARGB32);
    source.fill(Qt::white);
    for (int y = 4; y <= 6; ++y)
        for (int x = 1; x <= 3; ++x)
            source.setPixelColor(x, y, Qt::black);
    return BrushSettings::defineTip(source);
}
bool modal(QAction *action, const std::function<void(QDialog *)> &handler) {
    QTimer driver, timeout;
    driver.setInterval(10);
    timeout.setInterval(5000);
    QPointer<QDialog> last;
    bool hit = false, expired = false;
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d || d == last)
            return;
        last = d;
        hit = true;
        QPointer<QDialog> safe = d;
        QTimer::singleShot(0, d, [safe, &handler] {
            if (safe)
                handler(safe);
        });
    });
    QObject::connect(&timeout, &QTimer::timeout, [&] {
        expired = true;
        if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            d->reject();
    });
    driver.start();
    timeout.start(5000);
    action->trigger();
    driver.stop();
    timeout.stop();
    return hit && !expired;
}
void click(QDialog *d, QDialogButtonBox::StandardButton button) {
    d->findChild<QDialogButtonBox *>()->button(button)->click();
}
} // namespace
class PresetTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }
    void settingsAndMasksRoundTrip() {
        BrushSettings a;
        a.size = 17;
        a.hardness = 21;
        a.flow = 37;
        a.spacing = 88;
        a.angle = -90;
        a.roundness = 55;
        a.pressureMinimum = 33;
        a.pressureCurve = 1.75;
        a.pressureSize = false;
        a.pressureFlow = true;
        a.tiltAngle = true;
        a.tiltRoundness = true;
        a.tipMask = tip();
        QVERIFY(a.valid());
        BrushSettings b;
        QString e;
        QVERIFY2(BrushSettings::fromJson(a.toJson(), &b, &e), qPrintable(e));
        QVERIFY(a == b);
        QTemporaryDir temp;
        QVector<BrushPreset> saved{{"Kişisel uç", 2, a}}, read;
        QVERIFY(BrushLibrary::write(temp.filePath("set.pdbrush"), saved, &e));
        QVERIFY2(BrushLibrary::read(temp.filePath("set.pdbrush"), &read, &e), qPrintable(e));
        QVERIFY(saved == read);
    }
    void defineTipDarknessAlphaAndAspect() {
        QImage source(4, 1, QImage::Format_ARGB32);
        source.fill(Qt::white);
        source.setPixelColor(0, 0, Qt::black);
        source.setPixelColor(1, 0, QColor(0, 0, 0, 128));
        source.setPixelColor(2, 0, QColor(0, 0, 0, 0));
        QImage m = BrushSettings::defineTip(source);
        QCOMPARE(m.format(), QImage::Format_Grayscale8);
        QCOMPARE(int(m.constScanLine(0)[0]), 255);
        QCOMPARE(int(m.constScanLine(0)[1]), 128);
        QCOMPARE(int(m.constScanLine(0)[2]), 0);
        QCOMPARE(int(m.constScanLine(0)[3]), 0);
        QImage large(1024, 256, QImage::Format_ARGB32);
        large.fill(Qt::black);
        QCOMPARE(BrushSettings::defineTip(large).size(), QSize(512, 128));
        source.fill(Qt::white);
        QVERIFY(BrushSettings::defineTip(source).isNull());
    }
    void invalidSettingsKeepTarget() {
        BrushSettings sentinel;
        sentinel.size = 42;
        BrushSettings out = sentinel;
        QString e;
        for (QString key : {"size", "flow", "angle", "pressureCurve", "pressureSize"}) {
            auto json = sentinel.toJson();
            json[key] = "bad";
            QVERIFY(!BrushSettings::fromJson(json, &out, &e));
            QVERIFY(out == sentinel);
        }
        auto json = sentinel.toJson();
        json["spacing"] = 0;
        QVERIFY(!BrushSettings::fromJson(json, &out, &e));
        json = sentinel.toJson();
        json["script"] = "arbitrary";
        QVERIFY(!BrushSettings::fromJson(json, &out, &e));
        json = sentinel.toJson();
        json["tipMaskPng"] = "file:///etc/passwd";
        QVERIFY(!BrushSettings::fromJson(json, &out, &e));
        QVERIFY(out == sentinel);
    }
    void hostileLibraryAndAtomicWriteProtection() {
        QTemporaryDir temp;
        QString path = temp.filePath("keep.pdbrush"), e;
        QVector<BrushPreset> sentinel{{"Keep", 0, {}}}, out = sentinel;
        QVERIFY(BrushLibrary::write(path, sentinel, &e));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray old = f.readAll();
        f.close();
        QVector<BrushPreset> invalid = sentinel;
        invalid[0].settings.size = 99999;
        QVERIFY(!BrushLibrary::write(path, invalid, &e));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), old);
        f.close();
        auto root = QJsonDocument::fromJson(old).object();
        root["version"] = 2;
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(root).toJson());
        f.close();
        QVERIFY(!BrushLibrary::read(path, &out, &e));
        QVERIFY(out == sentinel);
        root = QJsonDocument::fromJson(old).object();
        auto array = root["presets"].toArray();
        auto record = array[0].toObject();
        record["tool"] = .5;
        array.append(record);
        root["presets"] = array;
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(root).toJson());
        f.close();
        QVERIFY(!BrushLibrary::read(path, &out, &e));
        QVERIFY(out == sentinel);
        QString project = temp.filePath("keep.pdeck");
        QFile p(project);
        QVERIFY(p.open(QIODevice::WriteOnly));
        p.write("project");
        p.close();
        QVERIFY(!BrushLibrary::write(project, sentinel, &e));
        QVERIFY(p.open(QIODevice::ReadOnly));
        QCOMPARE(p.readAll(), QByteArray("project"));
    }
    void countBudgetAndDuplicateMerge() {
        QVector<BrushPreset> a{{"Ink", 0, {}}}, b{{"ink", 1, {}}}, out;
        QString e;
        QVERIFY(BrushLibrary::merge(a, b, &out, &e));
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[1].name, QString("ink (2)"));
        QCOMPARE(out[1].tool, 1);
        for (int i = 1; i < 128; ++i)
            a.append({QString("Tip %1").arg(i), 0, {}});
        out = b;
        QVERIFY(!BrushLibrary::merge(a, b, &out, &e));
        QVERIFY(out == b);
        BrushSettings full;
        full.tipMask = QImage(512, 512, QImage::Format_Grayscale8);
        full.tipMask.fill(255);
        QVector<BrushPreset> heavy;
        for (int i = 0; i < 33; ++i)
            heavy.append({QString::number(i), 0, full});
        QVERIFY(!BrushLibrary::valid(heavy, &e));
        a = {{"<img>", 0, {}}};
        QVERIFY(!BrushLibrary::valid(a, &e));
    }
    void customTipPaintCacheRotationAndUndo() {
        Document d;
        d.create({80, 80});
        Canvas c(&d);
        ready(c);
        c.brush.tipMask = tip();
        tap(c, {40, 40});
        QImage first = d.composite();
        QCOMPARE(first.pixelColor(35, 41), QColor(Qt::red));
        QCOMPARE(first.pixelColor(45, 41).alpha(), 0);
        QCOMPARE(d.historyLabels().size(), 2);
        d.undo();
        c.foreground = Qt::blue;
        tap(c, {40, 40});
        QCOMPARE(d.composite().pixelColor(35, 41), QColor(Qt::blue));
        d.undo();
        c.brush.angle = 90;
        tap(c, {40, 40});
        QVERIFY(d.composite() != first);
        d.undo();
        QCOMPARE(d.composite().pixelColor(35, 41).alpha(), 0);
    }
    void cloneTipOnlyCoversMaskAndDoesNotRotateSource() {
        Document d;
        d.create({80, 80});
        d.beginEdit();
        d.state.layers[0].pixels.fill(Qt::green);
        d.state.layers[0].pixels.setPixelColor(10, 20, Qt::red);
        d.state.layers[0].pixels.setPixelColor(15, 21, Qt::blue);
        d.state.layers[0].pixels.setPixelColor(25, 19, Qt::yellow);
        d.commit("Source");
        Canvas c(&d);
        ready(c);
        c.setTool(Canvas::Clone);
        c.brush.tipMask = tip();
        tap(c, {20, 20}, Qt::AltModifier);
        tap(c, {40, 40});
        QCOMPARE(d.composite().pixelColor(45, 41), QColor(Qt::green));
        // Source is sampled at the same relative pixel offset; only tip coverage changes.
        QCOMPARE(d.composite().pixelColor(35, 41), QColor(Qt::blue));
        QCOMPARE(d.composite().pixelColor(30, 40), QColor(Qt::green));
        d.undo();
        c.brush.angle = 180;
        tap(c, {40, 40});
        QCOMPARE(d.composite().pixelColor(45, 39), QColor(Qt::yellow));
        QCOMPARE(d.composite().pixelColor(30, 40), QColor(Qt::green));
        d.undo();
        QCOMPARE(d.composite().pixelColor(10, 20), QColor(Qt::red));
    }
    void transparentStrokeKeepsEditableTextMetadata() {
        Document d;
        d.create({80, 80});
        d.beginEdit();
        d.state.layers[0].text = "Editable";
        d.commit("Text marker");
        Canvas c(&d);
        ready(c);
        c.brush.tipMask = tip();
        c.foreground = QColor(255, 0, 0, 0);
        const auto labels = d.historyLabels();
        tap(c, {40, 40});
        QCOMPARE(d.state.layers[0].text, QString("Editable"));
        QCOMPARE(d.historyLabels(), labels);
    }
    void libraryUiSaveApplyAndCanvasIsolation() {
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({80, 80});
        w.addDocument(d);
        auto *c = w.currentCanvas();
        c->brush.size = 17;
        c->brush.hardness = 15;
        c->brush.flow = 37;
        c->brush.angle = 45;
        c->brush.tipMask = tip();
        const BrushSettings saved = c->brush;
        const QImage pixels = d->composite();
        auto *action = w.findChild<QAction *>("brushLibrary");
        QVERIFY(action);
        int step = 0;
        QVERIFY(modal(action, [&](QDialog *dialog) {
            if (auto *name = qobject_cast<QInputDialog *>(dialog)) {
                name->setTextValue("Fırçam");
                ++step;
                name->accept();
                return;
            }
            if (dialog->objectName() != "brushLibraryDialog") {
                dialog->reject();
                return;
            }
            if (step == 0) {
                ++step;
                dialog->findChild<QPushButton *>("saveBrushPreset")->click();
            } else {
                ++step;
                click(dialog, QDialogButtonBox::Close);
            }
        }));
        QCOMPARE(step, 3);
        c->brush = BrushSettings{};
        QVERIFY(modal(action, [&](QDialog *dialog) {
            dialog->findChild<QListWidget *>("brushPresetList")->setCurrentRow(3);
            click(dialog, QDialogButtonBox::Ok);
        }));
        QVERIFY(c->brush == saved);
        QCOMPARE(d->composite(), pixels);
        QVERIFY(!d->dirty);
        QVERIFY(!d->canUndo());
        auto *second = new Document;
        second->create({80, 80});
        w.addDocument(second);
        QVERIFY(w.currentCanvas()->brush == BrushSettings{});
        w.close();
    }
    void libraryUiImportExportAndSelectionTipPreserveDocument() {
        QTemporaryDir temp;
        QString e;
        BrushSettings settings;
        settings.size = 17;
        settings.flow = 37;
        settings.angle = 23;
        settings.tipMask = tip();
        QVector<BrushPreset> incoming{{"Incoming", 0, settings}}, read;
        const QString input = temp.filePath("incoming.pdbrush"),
                      output = temp.filePath("exported.pdbrush");
        QVERIFY(BrushLibrary::write(input, incoming, &e));
        MainWindow w(false, false);
        w.show();
        auto *d = new Document;
        d->create({80, 80}, true);
        w.addDocument(d);
        d->beginEdit();
        for (int y = 20; y < 26; ++y)
            for (int x = 10; x < 22; ++x)
                d->state.layers[0].pixels.setPixelColor(x, y, Qt::black);
        d->commit("Shape");
        d->markSaved();
        d->selection = Selection(QRect(10, 20, 12, 6));
        const QImage before = d->composite();
        const auto labels = d->historyLabels();
        auto *action = w.findChild<QAction *>("brushLibrary");
        int stage = 0;
        QVERIFY(modal(action, [&](QDialog *dialog) {
            if (auto *file = qobject_cast<QFileDialog *>(dialog)) {
                const QString path = stage == 1 ? input : output;
                file->setDirectory(temp.path());
                file->selectFile(path);
                if (auto *edit = file->findChild<QLineEdit *>("fileNameEdit"))
                    edit->setText(path);
                ++stage;
                QMetaObject::invokeMethod(file, "accept");
                return;
            }
            if (stage == 0) {
                ++stage;
                dialog->findChild<QPushButton *>("importBrushPresets")->click();
            } else if (stage == 2) {
                dialog->findChild<QListWidget *>("brushPresetList")->setCurrentRow(3);
                ++stage;
                dialog->findChild<QPushButton *>("exportBrushPreset")->click();
            } else
                click(dialog, QDialogButtonBox::Close);
        }));
        QCOMPARE(stage, 4);
        QVERIFY(BrushLibrary::read(output, &read, &e));
        QVERIFY(read == incoming);
        QVERIFY(modal(action, [&](QDialog *dialog) {
            dialog->findChild<QPushButton *>("defineBrushSelection")->click();
            click(dialog, QDialogButtonBox::Close);
        }));
        QCOMPARE(w.currentCanvas()->brush.tipMask.size(), QSize(12, 6));
        QCOMPARE(d->composite(), before);
        QCOMPARE(d->historyLabels(), labels);
        QVERIFY(!d->dirty);
        w.close();
    }
    void persistentLibraryConflictAndCorruptBackup() {
        const QString oldName = QCoreApplication::applicationName(),
                      oldOrg = QCoreApplication::organizationName();
        const QString unique = "PhotoDeckPreset-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QCoreApplication::setApplicationName(unique);
        QCoreApplication::setOrganizationName("PhotoDeckTest");
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(folder);
        const QString path = folder + "/brushes.pdbrush";
        QString e;
        QVector<BrushPreset> original{{"Original", 0, {}}}, external{{"External", 0, {}}}, disk;
        QVERIFY(BrushLibrary::write(path, original, &e));
        {
            MainWindow w(false, true);
            w.show();
            auto *d = new Document;
            d->create({80, 80});
            w.addDocument(d);
            int stage = 0;
            bool errorShown = false, changed = false;
            QVERIFY(modal(w.findChild<QAction *>("brushLibrary"), [&](QDialog *dialog) {
                if (auto *message = qobject_cast<QMessageBox *>(dialog)) {
                    errorShown = message->text().contains("değişti");
                    message->accept();
                    return;
                }
                if (auto *name = qobject_cast<QInputDialog *>(dialog)) {
                    name->setTextValue("New");
                    ++stage;
                    name->accept();
                    return;
                }
                if (stage == 0) {
                    changed = BrushLibrary::write(path, external, &e);
                    ++stage;
                    dialog->findChild<QPushButton *>("saveBrushPreset")->click();
                } else
                    click(dialog, QDialogButtonBox::Close);
            }));
            QVERIFY(changed);
            QVERIFY(errorShown);
            QVERIFY(BrushLibrary::read(path, &disk, &e));
            QVERIFY(disk == external);
            w.close();
        }
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("broken-library");
        f.close();
        {
            MainWindow w(false, true);
            w.show();
            auto *d = new Document;
            d->create({80, 80});
            w.addDocument(d);
            int stage = 0;
            QVERIFY(modal(w.findChild<QAction *>("brushLibrary"), [&](QDialog *dialog) {
                if (auto *name = qobject_cast<QInputDialog *>(dialog)) {
                    name->setTextValue("Recovered");
                    ++stage;
                    name->accept();
                    return;
                }
                if (stage == 0) {
                    ++stage;
                    dialog->findChild<QPushButton *>("saveBrushPreset")->click();
                } else
                    click(dialog, QDialogButtonBox::Close);
            }));
            QVERIFY(BrushLibrary::read(path, &disk, &e));
            QCOMPARE(disk.size(), 1);
            QCOMPARE(disk[0].name, QString("Recovered"));
            const auto backups = QDir(folder).entryList({"brushes-unreadable-*.pdbrush"}, QDir::Files);
            QCOMPARE(backups.size(), 1);
            QFile backup(folder + "/" + backups[0]);
            QVERIFY(backup.open(QIODevice::ReadOnly));
            QCOMPARE(backup.readAll(), QByteArray("broken-library"));
            w.close();
        }
        QVERIFY(folder.contains(unique));
        QDir(folder).removeRecursively();
        QCoreApplication::setApplicationName(oldName);
        QCoreApplication::setOrganizationName(oldOrg);
    }
    void modernPaletteKeepsCustomizedSurfacesReadable() {
        const auto brightness = [](QColor c) {
            const auto linear = [](double x) {
                return x <= .04045 ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4);
            };
            return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
        };
        for (QColor panel : {QColor(Qt::white), QColor(Qt::black), QColor("#777777")}) {
            UiSettings settings = UiSettings::forPreset("focus");
            settings.panel = panel;
            const UiPreset resolved = settings.resolved();
            double a = brightness(resolved.text), b = brightness(resolved.panel);
            QVERIFY((qMax(a, b) + .05) / (qMin(a, b) + .05) >= 4.5);
            a = brightness(resolved.muted);
            QVERIFY((qMax(a, b) + .05) / (qMin(a, b) + .05) >= 4.5);
            QVERIFY(!settings.styleSheet().contains('@'));
        }
    }
    void inspectorsFitWideNativeText_data() {
        QTest::addColumn<QString>("preset");
        for (const auto &entry : uiPresets())
            QTest::newRow(qPrintable(entry.id)) << entry.id;
    }
    void inspectorsFitWideNativeText() {
        QFETCH(QString, preset);
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({24, 24}, true));
        d->markSaved();
        w.addDocument(d);
        w.applyUiPreset(preset, false);
        w.resize(900, 620);
        w.show();
        QTest::qWait(30);
        auto *color = w.findChild<QScrollArea *>("colorPanelScroll");
        QScrollArea *layer = nullptr;
        for (auto *scroll : w.findChildren<QScrollArea *>())
            if (scroll->widget() && scroll->widget()->objectName() == "layerPanel")
                layer = scroll;
        QVERIFY(color && layer);
        // Reproduce wide native Windows text metrics without changing the
        // machine's installed fonts or reducing any control's honest hint.
        for (auto *scroll : {color, layer})
            for (auto *control : scroll->widget()->findChildren<QWidget *>()) {
                QFont font = control->font();
                const int stretch = qMax(100, font.stretch());
                const int digits = qMax(1, QFontMetrics(font).horizontalAdvance("000000"));
                const int target = qMax(1, font.pixelSize()) * 6;
                font.setStretch(qMax(stretch, qRound(stretch * double(target) / digits)));
                control->setFont(font);
            }
        QTest::qWait(30);
        for (auto *scroll : {color, layer}) {
            if (!scroll->isVisible())
                continue;
            QTRY_VERIFY2(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width(),
                         qPrintable(QString("%1 %2 minimum=%3 viewport=%4")
                                        .arg(preset, scroll->widget()->objectName())
                                        .arg(scroll->widget()->minimumSizeHint().width())
                                        .arg(scroll->viewport()->width())));
            QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        }
        for (const auto *id : {"quickPosition", "quickAlign", "quickTransform", "quickTones",
                               "layerMaskButton"}) {
            auto *button = w.findChild<QWidget *>(id);
            QVERIFY(button);
            layer->ensureWidgetVisible(button, 0, 0);
            QTRY_VERIFY(button->visibleRegion().contains(button->rect()));
            QVERIFY(button->width() >= button->minimumSizeHint().width());
            QVERIFY(button->height() >= button->minimumSizeHint().height());
        }
    }
    void colorInspectorKeepsControlsReachable_data() {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<int>("fontSize");
        QTest::addColumn<QSize>("windowSize");
        for (const auto &preset : uiPresets())
            for (int fontSize : {preset.fontSize, 20})
                for (QSize size : {QSize(900, 620), QSize(1280, 800)})
                    QTest::newRow(qPrintable(QString("%1-%2px-%3x%4")
                                                .arg(preset.id).arg(fontSize)
                                                .arg(size.width()).arg(size.height())))
                        << preset.id << fontSize << size;
    }
    void colorInspectorKeepsControlsReachable() {
        QFETCH(QString, preset);
        QFETCH(int, fontSize);
        QFETCH(QSize, windowSize);
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({24, 24}));
        d->beginEdit();
        d->state.layers[0].pixels.fill(QColor(80, 120, 160));
        d->commit("Test color");
        d->markSaved();
        w.addDocument(d);
        const QImage pixels = d->composite();
        const auto revision = d->revision();
        const auto history = d->historyLabels();
        UiSettings settings = UiSettings::forPreset(preset);
        settings.fontSize = fontSize;
        w.applyUiSettings(settings, true, false);
        w.resize(windowSize);
        w.show();
        w.activateWindow();
        // Exercise crowded dock stacks even in presets which initially hide Color.
        for (const auto &name : {"ui.panel.color", "ui.panel.layers"}) {
            auto *action = w.findChild<QAction *>(name);
            QVERIFY(action);
            action->setChecked(true);
        }
        if (preset == "paper")
            w.findChild<QAction *>("ui.panel.history")->setChecked(true);
        QTest::qWait(30);
        QCOMPARE(w.size(), windowSize);
        auto *dock = w.findChild<QDockWidget *>("colorDock");
        auto *scroll = w.findChild<QScrollArea *>("colorPanelScroll");
        QVERIFY(dock && scroll);
        QCOMPARE(dock->widget(), scroll);
        QVERIFY(scroll->isVisible());
        QVERIFY(dock->rect().contains(QRect(scroll->mapTo(dock, QPoint()), scroll->size())));
        for (auto *other : w.findChildren<QDockWidget *>())
            if (other != dock && other->isVisible() && !other->isFloating())
                QVERIFY2(!dock->geometry().intersects(other->geometry()),
                         qPrintable("Color overlaps " + other->objectName()));
        auto *brightness = w.findChild<QSlider *>("brightness");
        auto *contrast = w.findChild<QSlider *>("contrast");
        auto *saturation = w.findChild<QSlider *>("saturation");
        auto *reset = w.findChild<QPushButton *>("resetColorAdjustments");
        auto *advanced = w.findChild<QPushButton *>("advancedTones");
        QVERIFY(brightness && contrast && saturation && reset && advanced);
        const auto reachable = [&](QWidget *control) {
            const QPoint center = control->mapTo(scroll->viewport(), control->rect().center());
            return control->isVisible() && scroll->viewport()->rect().contains(center) &&
                   w.childAt(control->mapTo(&w, control->rect().center())) == control;
        };
        const auto focusWithTabs = [&](QWidget *control) {
            w.activateWindow();
            if (!QTest::qWaitForWindowActive(&w))
                return false;
            w.currentCanvas()->setFocus();
            for (int count = 0; count < 100 && !control->hasFocus(); ++count) {
                auto *focused = QApplication::focusWidget();
                if (!focused)
                    return false;
                QTest::keyClick(focused, Qt::Key_Tab);
            }
            return control->hasFocus();
        };
        // Real Tab traversal must reveal lower controls without explicit ensureWidgetVisible.
        QVERIFY(focusWithTabs(brightness));
        QTRY_VERIFY(reachable(brightness));
        for (auto *control : QList<QWidget *>{contrast, saturation, reset, advanced}) {
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
            QTRY_COMPARE(QApplication::focusWidget(), control);
            QTRY_VERIFY(reachable(control));
            QVERIFY(control->visibleRegion().contains(control->rect().center()));
        }
        // Scrolling and relayout are presentation-only operations.
        scroll->verticalScrollBar()->setValue(0);
        if (scroll->verticalScrollBar()->maximum() > 0) {
            auto *bar = scroll->verticalScrollBar();
            QTest::mouseClick(bar, Qt::LeftButton, {}, QPoint(bar->width() / 2, bar->height() - 3));
            QVERIFY(bar->value() > 0);
            QTest::keyClick(bar, Qt::Key_End);
            QCOMPARE(bar->value(), bar->maximum());
        }
        scroll->horizontalScrollBar()->setValue(scroll->horizontalScrollBar()->maximum());
        QCOMPARE(d->composite(), pixels);
        QCOMPARE(d->revision(), revision);
        QCOMPARE(d->historyLabels(), history);
        QVERIFY(!d->dirty);
        // Keyboard adjustment, reset and the advanced dialog still act on this document.
        QVERIFY(focusWithTabs(saturation));
        QTRY_VERIFY(reachable(saturation));
        QTest::keyClick(saturation, Qt::Key_PageUp);
        QCOMPARE(d->state.layers[0].saturation, saturation->pageStep());
        QVERIFY(d->composite() != pixels);
        QTest::keyClick(saturation, Qt::Key_Tab);
        QTRY_VERIFY(reset->hasFocus());
        QTRY_VERIFY(reachable(reset));
        QTest::keyClick(reset, Qt::Key_Space);
        QCOMPARE(d->state.layers[0].saturation, 0);
        QCOMPARE(d->composite(), pixels);
        QTest::keyClick(reset, Qt::Key_Tab);
        QTRY_VERIFY(advanced->hasFocus());
        QTRY_VERIFY(reachable(advanced));
        QAction open;
        connect(&open, &QAction::triggered, [&] { QTest::keyClick(advanced, Qt::Key_Space); });
        const auto beforeDialog = d->revision();
        QVERIFY(modal(&open, [&](QDialog *dialog) {
            auto *black = dialog->findChild<QSpinBox *>("inputBlack");
            QVERIFY(black);
            black->setValue(12);
            click(dialog, QDialogButtonBox::Cancel);
        }));
        QCOMPARE(d->revision(), beforeDialog);
        QCOMPARE(d->composite(), pixels);
        QVERIFY(modal(&open, [&](QDialog *dialog) {
            auto *black = dialog->findChild<QSpinBox *>("inputBlack");
            QVERIFY(black);
            black->setValue(12);
            click(dialog, QDialogButtonBox::Ok);
        }));
        QCOMPARE(d->state.layers[0].tones.inputBlack, 12);
        QVERIFY(focusWithTabs(reset));
        QTRY_VERIFY(reachable(reset));
        QTest::keyClick(reset, Qt::Key_Space);
        QVERIFY(d->state.layers[0].tones == ToneSettings{});
        QCOMPARE(d->composite(), pixels);
        d->markSaved();
        w.close();
    }
    void colorInspectorPreservesRoomyAndSavedLayouts() {
        {
            MainWindow transient(false, false);
            transient.applyUiPreset("paper", false);
            transient.applyUiPreset("modular", false);
        }
        QCoreApplication::processEvents();
        MainWindow w(false, false);
        auto *d = new Document;
        QVERIFY(d->create({24, 24}, true));
        d->markSaved();
        w.addDocument(d);
        const auto revision = d->revision();
        w.applyUiPreset("paper", false);
        w.resize(1280, 800);
        w.show();
        w.activateWindow();
        QTest::qWait(30);
        auto *dock = w.findChild<QDockWidget *>("colorDock");
        auto *scroll = w.findChild<QScrollArea *>("colorPanelScroll");
        auto *advanced = w.findChild<QPushButton *>("advancedTones");
        QVERIFY(dock && scroll && advanced);
        QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(advanced->visibleRegion().contains(advanced->rect()));
        dock->setFloating(true);
        dock->resize(292, 180);
        QTest::qWait(30);
        QVERIFY(dock->isFloating());
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        const QSize floatingSize = dock->size();
        UiSettings saved = w.uiSettings();
        saved.dockState = w.saveState(2);
        QVERIFY(!saved.dockState.isEmpty());
        auto largerFont = saved;
        largerFont.fontSize = 20;
        w.applyUiSettings(largerFont, false, false);
        QTest::qWait(30);
        QCOMPARE(dock->size(), floatingSize);
        w.applyUiPreset("graphite", false);
        QVERIFY(!dock->isFloating());
        w.applyUiSettings(saved, true, false);
        QTest::qWait(30);
        QVERIFY(dock->isFloating());
        QCOMPARE(dock->size(), floatingSize);
        QCOMPARE(dock->widget(), scroll);
        QCOMPARE(scroll->widget()->objectName(), QString("colorPanel"));
        dock->setFloating(false);
        auto *panelAction = w.findChild<QAction *>("ui.panel.color");
        QVERIFY(panelAction);
        panelAction->setChecked(false);
        QVERIFY(!dock->isVisible());
        panelAction->setChecked(true);
        QTRY_VERIFY(scroll->isVisible());
        QCOMPARE(d->revision(), revision);
        QVERIFY(!d->dirty);
        w.close();
    }
    void librarySurvivesRestart() {
        const QString oldName = QCoreApplication::applicationName(),
                      oldOrg = QCoreApplication::organizationName();
        const QString unique = "PhotoDeckPreset-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QCoreApplication::setApplicationName(unique);
        QCoreApplication::setOrganizationName("PhotoDeckTest");
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        {
            MainWindow w(false, true);
            w.show();
            auto *d = new Document;
            d->create({80, 80});
            w.addDocument(d);
            w.currentCanvas()->brush.angle = 75;
            auto *a = w.findChild<QAction *>("brushLibrary");
            int step = 0;
            QVERIFY(modal(a, [&](QDialog *dialog) {
                if (auto *n = qobject_cast<QInputDialog *>(dialog)) {
                    n->setTextValue("Persist");
                    ++step;
                    n->accept();
                } else if (step == 0) {
                    ++step;
                    dialog->findChild<QPushButton *>("saveBrushPreset")->click();
                } else {
                    ++step;
                    click(dialog, QDialogButtonBox::Close);
                }
            }));
            w.close();
        }
        {
            MainWindow w(false, true);
            w.show();
            auto *d = new Document;
            d->create({80, 80});
            w.addDocument(d);
            auto *a = w.findChild<QAction *>("brushLibrary");
            QVERIFY(modal(a, [&](QDialog *dialog) {
                auto *list = dialog->findChild<QListWidget *>("brushPresetList");
                list->setCurrentRow(3);
                click(dialog, QDialogButtonBox::Ok);
            }));
            QCOMPARE(w.currentCanvas()->brush.angle, 75);
            w.close();
        }
        QVERIFY(folder.contains(unique));
        QDir(folder).removeRecursively();
        QCoreApplication::setApplicationName(oldName);
        QCoreApplication::setOrganizationName(oldOrg);
    }
};
QTEST_MAIN(PresetTests)
#include "test_presets.moc"
