#include "api/CoolerControlClient.h"
#include "ui/AppTranslator.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileDevice>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSettings>

namespace {

QIcon resolveApplicationIcon()
{
    static constexpr const char* kThemeNames[] = {
        "sensors-fan",
        "speedometer",
        "preferences-system-performance",
        "utilities-system-monitor",
    };

    for (const char* name : kThemeNames) {
        const QIcon icon = QIcon::fromTheme(QLatin1String(name));
        if (!icon.isNull()) {
            return icon;
        }
    }

    // Minimalist fallback speedometer arc + needle when no icon theme is installed
    QIcon fallback;
    const int sizes[] = {32, 48, 64};
    for (const int sz : sizes) {
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);

        const qreal pad = sz * 0.14;
        const QRectF arcRect(pad, pad, sz - pad * 2.0, sz - pad * 2.0);
        const QColor stroke(52, 148, 214);
        p.setPen(QPen(stroke, std::max(2.0, sz * 0.09), Qt::SolidLine, Qt::RoundCap));
        p.drawArc(arcRect, 210 * 16, -240 * 16);

        const QPointF center(sz * 0.5, sz * 0.52);
        const QPointF tip(sz * 0.72, sz * 0.30);
        p.setPen(QPen(stroke, std::max(2.0, sz * 0.08), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(center, tip);

        p.setPen(Qt::NoPen);
        p.setBrush(stroke);
        p.drawEllipse(center, sz * 0.08, sz * 0.08);
        fallback.addPixmap(pm);
    }
    return fallback;
}

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
    QGuiApplication::setDesktopFileName(QStringLiteral("ccqtupon"));
    app.setWindowIcon(resolveApplicationIcon());
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
