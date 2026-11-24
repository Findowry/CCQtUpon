#include "api/CoolerControlClient.h"
#include "ui/AppTranslator.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QSettings>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ccqtupon"));
    QApplication::setApplicationName(QStringLiteral("ccqtupon"));
    QApplication::setApplicationDisplayName(QStringLiteral("CCQt Upon"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Native Qt6 client for CoolerControl v5.1+"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption noTrayOpt(QStringLiteral("no-tray"),
                                       QStringLiteral("Disable system tray icon and close directly."));
    const QCommandLineOption minimizedOpt(QStringLiteral("minimized"),
                                          QStringLiteral("Start minimized to system tray."));
    parser.addOption(noTrayOpt);
    parser.addOption(minimizedOpt);
    parser.process(app);

    const bool trayEnabled = !parser.isSet(noTrayOpt);

    ccqt::AppTranslator translator;
    ccqt::CoolerControlClient client;
    ccqt::MainWindow window(&client, &translator, trayEnabled);

    const bool startMinimized = parser.isSet(minimizedOpt) && window.isTrayAvailable();

    app.setQuitOnLastWindowClosed(false);
    QObject::connect(&app, &QApplication::aboutToQuit, &client, &ccqt::CoolerControlClient::disconnectFromDaemon);

    if (!startMinimized) {
        window.show();
    }
    window.startInitialConnection(startMinimized);

    return app.exec();
}
