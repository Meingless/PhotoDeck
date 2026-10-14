#include "mainwindow.h"
#include <QAction>
#include <QBuffer>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
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
