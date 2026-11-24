#pragma once

#include "api/CoolerControlClient.h"
#include "models/CoolerControlModels.h"
#include "ui/AppTranslator.h"

#include <QHash>
#include <QMainWindow>
#include <QString>
#include <QVector>

class QAction;
class QActionGroup;
class QCloseEvent;
class QComboBox;
class QEvent;
class QGroupBox;
class QLabel;
class QListWidget;
class QMenu;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QToolBar;
class QVBoxLayout;

namespace ccqt {

class CoolingTabWidget;
class DeviceCardWidget;
class StatusBanner;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(CoolerControlClient* client,
               AppTranslator* translator,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    void startInitialConnection();

public slots:
    void openAuthDialog();
    void openAboutDialog();
    void openInCoolerControl();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onConnectionStateChanged(ccqt::ConnectionState state);
    void onConnectionError(const QString& message);
    void onAuthenticationRequired(const QString& reason);
    void onHealthUpdated(const ccqt::HealthInfo& health);
    void onDevicesUpdated(const QVector<ccqt::DeviceInfo>& devices);
    void onStatusUpdated(const QVector<ccqt::DeviceStatus>& statuses);
    void onModesUpdated(const QVector<ccqt::ModeInfo>& modes);
    void onActiveModesUpdated(const ccqt::ActiveModes& activeModes);
    void onProfilesUpdated(const QVector<ccqt::ProfileInfo>& profiles);
    void onFunctionsUpdated(const QVector<ccqt::FunctionInfo>& functions);
    void onOverridesUpdated(const ccqt::NameOverrides& overrides);
    void onDeviceSettingsUpdated(const QString& deviceUid,
                                 const QVector<ccqt::ChannelSetting>& settings);
    void onModeActivated(const QString& modeUid);
    void onChannelSettingUpdated(const QString& deviceUid, const QString& channelName);
    void onProfileUpdated(const QString& profileUid);
    void onProfileUpdateFailed(const QString& profileUid,
                               const QString& message,
                               bool isForbidden);
    void onWriteOperationFailed(const QString& deviceUid,
                                const QString& channelName,
                                const QString& message,
                                bool isForbidden);

private:
    void buildUi();
    void buildMenus();
    void buildToolBar();
    void connectClientSignals();

    void loadSettings();
    void saveWindowState();
    void saveConnectionSettings(const ConnectionConfig& cfg, bool saveToken);
    void setLanguageAndPersist(Language lang);

    void rebuildDeviceCards();
    void applyDeviceFilter();
    void refreshModesUi();
    void refreshDevicesListUi();
    void refreshProfilesListUi();
    void updateConnectionIndicators();
    void updateStatusBarText();
    void retranslateUi();

    QString formatProfileSummary(const ProfileInfo& profile) const;

    CoolerControlClient* m_client = nullptr;
    AppTranslator* m_translator = nullptr;

    // Settings state
    bool m_saveToken = true;
    QString m_selectedDeviceUid;
    QString m_lastUpdateTimeStr;

    // Live daemon state
    ConnectionState m_connState = ConnectionState::Disconnected;
    HealthInfo m_health;
    QVector<DeviceInfo> m_devices;
    NameOverrides m_overrides;
    QVector<ModeInfo> m_modes;
    ActiveModes m_activeModes;
    QVector<ProfileInfo> m_profiles;
    QHash<QString, QVector<ChannelSetting>> m_settingsByDevice;
    QHash<QString, DeviceStatus> m_lastStatusByDevice;

    // Menus & Actions
    QMenu* m_fileMenu = nullptr;
    QMenu* m_viewMenu = nullptr;
    QMenu* m_langMenu = nullptr;
    QMenu* m_helpMenu = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_refreshAction = nullptr;
    QAction* m_quitAction = nullptr;
    QActionGroup* m_langGroup = nullptr;
    QAction* m_langEnAction = nullptr;
    QAction* m_langTrAction = nullptr;
    QAction* m_aboutAction = nullptr;

    // Toolbar
    QToolBar* m_toolBar = nullptr;
    QLabel* m_modeToolbarLabel = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QWidget* m_statusDot = nullptr;
    QLabel* m_connBadgeLabel = nullptr;
    QAction* m_toolbarRefreshAction = nullptr;
    QAction* m_toolbarSettingsAction = nullptr;
    QAction* m_toolbarOpenCcAction = nullptr;

    // Central widgets
    StatusBanner* m_banner = nullptr;
    QTabWidget* m_tabs = nullptr;
    QSplitter* m_splitter = nullptr;
    CoolingTabWidget* m_coolingTab = nullptr;

    QGroupBox* m_modesGroup = nullptr;
    QListWidget* m_modesList = nullptr;
    QLabel* m_modesEmptyLabel = nullptr;

    QGroupBox* m_devicesGroup = nullptr;
    QListWidget* m_devicesList = nullptr;

    QGroupBox* m_profilesGroup = nullptr;
    QListWidget* m_profilesList = nullptr;

    QStackedWidget* m_rightStack = nullptr;
    QLabel* m_emptyStateLabel = nullptr;
    QPushButton* m_emptySettingsButton = nullptr;
    QScrollArea* m_cardsScroll = nullptr;
    QWidget* m_cardsContainer = nullptr;
    QVBoxLayout* m_cardsLayout = nullptr;
    QHash<QString, DeviceCardWidget*> m_cardsByUid;
};

} // namespace ccqt
