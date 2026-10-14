#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QImageReader>
#include <QTimer>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("PhotoDeck");
    app.setOrganizationName("PhotoDeck");
    app.setApplicationVersion("0.7.0-ea.1");
    QImageReader::setAllocationLimit(128);
    app.setStyle("Fusion");
    app.setWindowIcon(QIcon(":/photodeck.png"));
    QFont font("Sans Serif", 10);
    app.setFont(font);
    QCommandLineParser parser;
    parser.setApplicationDescription("Hafif yerel katman ve fotoğraf düzenleyici");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("files", "Açılacak görüntüler veya .pdeck projeleri", "[files…]");
    parser.addOption({"screenshot", "Örnek pencereyi PNG olarak kaydet ve çık.", "path"});
    parser.addOption({"write-demo", "Örnek katmanlı projeyi kaydet ve çık.", "path"});
    parser.addOption({"preset", "Arayüz presetini uygula (ör. focus, compact).", "id"});
    parser.addOption({"no-settings", "Kullanıcı arayüz ayarlarını okuma veya yazma."});
    parser.process(app);
    if (parser.isSet("write-demo")) {
        Document d;
        d.makeDemo();
        QString e;
        bool ok = d.save(parser.value("write-demo"), &e);
        if (!ok)
            qCritical("%s", qPrintable(e));
        return ok ? 0 : 1;
    }
    MainWindow window(!parser.isSet("screenshot"), !parser.isSet("no-settings"));
    if (parser.isSet("preset")) {
        if (!validUiPreset(parser.value("preset"))) {
            qCritical("Bilinmeyen preset.");
            return 2;
        }
        window.applyUiPreset(parser.value("preset"), !parser.isSet("no-settings"));
    }
    for (const auto &file : parser.positionalArguments())
        window.openFile(file);
    if (!window.currentDocument())
        window.demo();
    window.show();
    if (parser.isSet("screenshot")) {
        QTimer::singleShot(1000, &window, [&] {
            window.saveScreenshot(parser.value("screenshot"));
            app.quit();
        });
    }
    return app.exec();
}
