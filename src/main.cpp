#include "api/CoolerControlClient.h"
#include "ui/AppTranslator.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileDevice>
#include <QSettings>

namespace {

void migrateLegacyIniToConf()
{
    const QSettings confSettings(QSettings::NativeFormat,
                                 QSettings::UserScope,
                                 QStringLiteral("ccqtupon"),
                                 QStringLiteral("ccqtupon"));
    const QSettings iniSettings(QSettings::IniFormat,
                                QSettings::UserScope,
                                QStringLiteral("ccqtupon"),
                                QStringLiteral("ccqtupon"));

    const QString confPath = confSettings.fileName();
    const QString iniPath = iniSettings.fileName();

    if (confPath.isEmpty() || iniPath.isEmpty() || confPath == iniPath) {
        return;
    }
    if (QFile::exists(confPath) || !QFile::exists(iniPath)) {
        return;
    }

    QSettings target(QSettings::NativeFormat,
                     QSettings::UserScope,
                     QStringLiteral("ccqtupon"),
                     QStringLiteral("ccqtupon"));
    const QStringList keys = iniSettings.allKeys();
    for (const QString& key : keys) {
        target.setValue(key, iniSettings.value(key));
    }
    target.sync();

    if (!target.value(QStringLiteral("connection/token")).toString().trimmed().isEmpty()) {
        QFile::setPermissions(confPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ccqtupon"));
    QApplication::setApplicationName(QStringLiteral("ccqtupon"));
    QApplication::setApplicationDisplayName(QStringLiteral("CCQt Upon"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QSettings::setDefaultFormat(QSettings::NativeFormat);

    migrateLegacyIniToConf();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Native Qt6 client for CoolerControl v5.1+"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    ccqt::AppTranslator translator;
    ccqt::CoolerControlClient client;
    ccqt::MainWindow window(&client, &translator);

    QObject::connect(&app,
                     &QApplication::aboutToQuit,
                     &client,
                     &ccqt::CoolerControlClient::disconnectFromDaemon);

    window.show();
    window.startInitialConnection();

    return app.exec();
}
