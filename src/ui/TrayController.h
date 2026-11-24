#pragma once

#include "api/CoolerControlClient.h"
#include "models/CoolerControlModels.h"

#include <QObject>
#include <QString>
#include <QSystemTrayIcon>
#include <QVector>

#include <memory>

class QAction;
class QActionGroup;
class QIcon;
class QMenu;

namespace ccqt {

class TrayController : public QObject {
    Q_OBJECT

public:
    explicit TrayController(bool enabled = true, QObject* parent = nullptr);
    ~TrayController() override;

    bool isAvailable() const;

    void setMainWindowVisible(bool visible);
    void setMinimizeToTrayChecked(bool checked);
    void updateConnectionState(ConnectionState state, const HealthInfo& health);
    void updateModes(const QVector<ModeInfo>& modes, const QString& activeModeUid);
    void retranslateUi();

signals:
    void toggleMainWindowRequested();
    void modeActivationRequested(const QString& modeUid);
    void minimizeToTrayToggled(bool enabled);
    void settingsRequested();
    void quitRequested();

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    static QIcon createTrayIcon();
    void rebuildMenu();
    void updateTooltip();

    bool m_enabled = true;
    bool m_windowVisible = true;
    bool m_minimizeToTray = true;
    ConnectionState m_state = ConnectionState::Disconnected;
    HealthInfo m_health;
    QVector<ModeInfo> m_modes;
    QString m_activeModeUid;

    QSystemTrayIcon* m_trayIcon = nullptr;
    std::unique_ptr<QMenu> m_menu;
    QActionGroup* m_modeGroup = nullptr;
    QAction* m_toggleAction = nullptr;
    QAction* m_minimizeAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_quitAction = nullptr;
};

} // namespace ccqt
