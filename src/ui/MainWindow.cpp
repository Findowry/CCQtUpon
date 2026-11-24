#include "ui/MainWindow.h"

#include "ui/AuthDialog.h"
#include "ui/TrayController.h"
#include "widgets/DeviceCardWidget.h"
#include "widgets/StatusBanner.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFileDevice>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace ccqt {
namespace {

class StatusDotWidget : public QWidget {
public:
    explicit StatusDotWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(14, 14);
    }

    void setColor(const QColor& color)
    {
        if (m_color != color) {
            m_color = color;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event)
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(2.5, 2.5, -2.5, -2.5);
        p.setPen(QPen(palette().color(QPalette::Mid), 0.8));
        p.setBrush(m_color);
        p.drawEllipse(r);
    }

private:
    QColor m_color = QColor(140, 140, 140);
};

} // namespace

MainWindow::MainWindow(CoolerControlClient* client,
                       AppTranslator* translator,
                       bool trayEnabled,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_client(client)
    , m_translator(translator)
{
    resize(1040, 700);
    setMinimumSize(760, 500);

    m_trayController = new TrayController(trayEnabled, this);

    buildUi();
    buildMenus();
    buildToolBar();
    connectClientSignals();
    loadSettings();
    retranslateUi();
    updateConnectionIndicators();

    if (m_trayController != nullptr) {
        m_trayController->setMinimizeToTrayChecked(m_minimizeToTray);
        connect(m_trayController, &TrayController::toggleMainWindowRequested, this, [this]() {
            if (isVisible() && !isMinimized()) {
                hide();
                m_trayController->setMainWindowVisible(false);
            } else {
                showNormal();
                raise();
                activateWindow();
                m_trayController->setMainWindowVisible(true);
            }
        });
        connect(m_trayController,
                &TrayController::modeActivationRequested,
                m_client,
                &CoolerControlClient::activateMode);
        connect(m_trayController, &TrayController::minimizeToTrayToggled, this, [this](bool enabled) {
            m_minimizeToTray = enabled;
            if (m_minimizeToTrayAction != nullptr && m_minimizeToTrayAction->isChecked() != enabled) {
                m_minimizeToTrayAction->setChecked(enabled);
            }
            QSettings settings;
            settings.setValue(QStringLiteral("ui/minimize_to_tray"), enabled);
        });
        connect(m_trayController, &TrayController::settingsRequested, this, [this]() {
            if (!isVisible()) {
                showNormal();
                raise();
                activateWindow();
                m_trayController->setMainWindowVisible(true);
            }
            openAuthDialog();
        });
        connect(m_trayController, &TrayController::quitRequested, this, &MainWindow::forceQuit);
    }
}

MainWindow::~MainWindow() = default;

bool MainWindow::isTrayAvailable() const
{
    return m_trayController != nullptr && m_trayController->isAvailable();
}

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    m_banner = new StatusBanner(central);
    connect(m_banner, &StatusBanner::settingsRequested, this, &MainWindow::openAuthDialog);
    connect(m_banner, &StatusBanner::retryRequested, m_client, &CoolerControlClient::retryNow);
    centralLayout->addWidget(m_banner);

    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->setChildrenCollapsible(false);

    // Left sidebar
    auto* sidebar = new QWidget(m_splitter);
    sidebar->setMinimumWidth(230);
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(10, 10, 6, 10);
    sideLayout->setSpacing(10);

    m_modesGroup = new QGroupBox(sidebar);
    auto* modesLayout = new QVBoxLayout(m_modesGroup);
    modesLayout->setContentsMargins(8, 8, 8, 8);
    modesLayout->setSpacing(4);
    m_modesList = new QListWidget(m_modesGroup);
    m_modesList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_modesList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item == nullptr) {
            return;
        }
        const QString uid = item->data(Qt::UserRole).toString();
        if (!uid.isEmpty()) {
            m_client->activateMode(uid);
        }
    });
    connect(m_modesList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item == nullptr) {
            return;
        }
        const QString uid = item->data(Qt::UserRole).toString();
        if (!uid.isEmpty()) {
            m_client->activateMode(uid);
        }
    });
    modesLayout->addWidget(m_modesList);
    m_modesEmptyLabel = new QLabel(m_modesGroup);
    m_modesEmptyLabel->setWordWrap(true);
    modesLayout->addWidget(m_modesEmptyLabel);
    sideLayout->addWidget(m_modesGroup, 2);

    m_devicesGroup = new QGroupBox(sidebar);
    auto* devicesLayout = new QVBoxLayout(m_devicesGroup);
    devicesLayout->setContentsMargins(8, 8, 8, 8);
    m_devicesList = new QListWidget(m_devicesGroup);
    m_devicesList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_devicesList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current == nullptr) {
            return;
        }
        const QString uid = current->data(Qt::UserRole).toString();
        if (m_selectedDeviceUid != uid) {
            m_selectedDeviceUid = uid;
            QSettings settings;
            settings.setValue(QStringLiteral("ui/selected_device"), m_selectedDeviceUid);
            applyDeviceFilter();
        }
    });
    devicesLayout->addWidget(m_devicesList);
    sideLayout->addWidget(m_devicesGroup, 4);

    m_profilesGroup = new QGroupBox(sidebar);
    auto* profilesLayout = new QVBoxLayout(m_profilesGroup);
    profilesLayout->setContentsMargins(8, 8, 8, 8);
    m_profilesList = new QListWidget(m_profilesGroup);
    m_profilesList->setSelectionMode(QAbstractItemView::NoSelection);
    profilesLayout->addWidget(m_profilesList);
    sideLayout->addWidget(m_profilesGroup, 3);

    m_splitter->addWidget(sidebar);

    // Right stacked area (empty welcome vs live cards scroll area)
    m_rightStack = new QStackedWidget(m_splitter);

    auto* emptyPage = new QWidget(m_rightStack);
    auto* emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->addStretch(1);
    m_emptyStateLabel = new QLabel(emptyPage);
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    m_emptyStateLabel->setWordWrap(true);
    emptyLayout->addWidget(m_emptyStateLabel, 0, Qt::AlignCenter);

    m_emptySettingsButton = new QPushButton(emptyPage);
    connect(m_emptySettingsButton, &QPushButton::clicked, this, &MainWindow::openAuthDialog);
    emptyLayout->addWidget(m_emptySettingsButton, 0, Qt::AlignCenter);
    emptyLayout->addStretch(1);
    m_rightStack->addWidget(emptyPage);

    m_cardsScroll = new QScrollArea(m_rightStack);
    m_cardsScroll->setWidgetResizable(true);
    m_cardsScroll->setFrameShape(QFrame::NoFrame);
    m_cardsContainer = new QWidget(m_cardsScroll);
    m_cardsLayout = new QVBoxLayout(m_cardsContainer);
    m_cardsLayout->setContentsMargins(8, 10, 12, 12);
    m_cardsLayout->setSpacing(10);
    m_cardsLayout->addStretch(1);
    m_cardsScroll->setWidget(m_cardsContainer);
    m_rightStack->addWidget(m_cardsScroll);

    m_splitter->addWidget(m_rightStack);
    m_splitter->setSizes({270, 770});

    centralLayout->addWidget(m_splitter, 1);
    setCentralWidget(central);

    statusBar()->setSizeGripEnabled(true);
}

void MainWindow::buildMenus()
{
    m_fileMenu = menuBar()->addMenu(QString());

    m_settingsAction = new QAction(this);
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::openAuthDialog);
    m_fileMenu->addAction(m_settingsAction);

    m_refreshAction = new QAction(this);
    m_refreshAction->setShortcut(QKeySequence::Refresh);
    connect(m_refreshAction, &QAction::triggered, this, [this]() {
        if (m_connState == ConnectionState::Connected) {
            m_client->refreshAllMetadata();
        } else {
            m_client->retryNow();
        }
    });
    m_fileMenu->addAction(m_refreshAction);

    m_fileMenu->addSeparator();

    m_minimizeToTrayAction = new QAction(this);
    m_minimizeToTrayAction->setCheckable(true);
    m_minimizeToTrayAction->setChecked(m_minimizeToTray);
    connect(m_minimizeToTrayAction, &QAction::toggled, this, [this](bool checked) {
        m_minimizeToTray = checked;
        if (m_trayController != nullptr) {
            m_trayController->setMinimizeToTrayChecked(checked);
        }
        QSettings settings;
        settings.setValue(QStringLiteral("ui/minimize_to_tray"), checked);
    });
    m_fileMenu->addAction(m_minimizeToTrayAction);

    m_fileMenu->addSeparator();

    m_quitAction = new QAction(this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &MainWindow::forceQuit);
    m_fileMenu->addAction(m_quitAction);

    m_viewMenu = menuBar()->addMenu(QString());
    m_langMenu = m_viewMenu->addMenu(QString());
    m_langGroup = new QActionGroup(this);
    m_langGroup->setExclusive(true);

    m_langEnAction = m_langMenu->addAction(QString());
    m_langEnAction->setCheckable(true);
    m_langGroup->addAction(m_langEnAction);
    connect(m_langEnAction, &QAction::triggered, this, [this]() {
        setLanguageAndPersist(Language::English);
    });

    m_langTrAction = m_langMenu->addAction(QString());
    m_langTrAction->setCheckable(true);
    m_langGroup->addAction(m_langTrAction);
    connect(m_langTrAction, &QAction::triggered, this, [this]() {
        setLanguageAndPersist(Language::Turkish);
    });
}

void MainWindow::buildToolBar()
{
    m_toolBar = addToolBar(QStringLiteral("MainToolBar"));
    m_toolBar->setObjectName(QStringLiteral("MainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);

    m_modeToolbarLabel = new QLabel(m_toolBar);
    m_modeToolbarLabel->setContentsMargins(6, 0, 6, 0);
    m_toolBar->addWidget(m_modeToolbarLabel);

    m_modeCombo = new QComboBox(m_toolBar);
    m_modeCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_modeCombo->setMinimumContentsLength(18);
    connect(m_modeCombo, &QComboBox::activated, this, [this](int index) {
        const QString uid = m_modeCombo->itemData(index).toString();
        if (!uid.isEmpty()) {
            m_client->activateMode(uid);
        }
    });
    m_toolBar->addWidget(m_modeCombo);

    auto* spacer = new QWidget(m_toolBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(spacer);

    m_statusDot = new StatusDotWidget(m_toolBar);
    m_toolBar->addWidget(m_statusDot);

    m_connBadgeLabel = new QLabel(m_toolBar);
    m_connBadgeLabel->setContentsMargins(4, 0, 10, 0);
    m_toolBar->addWidget(m_connBadgeLabel);

    m_toolbarRefreshAction = m_toolBar->addAction(QString());
    connect(m_toolbarRefreshAction, &QAction::triggered, m_refreshAction, &QAction::trigger);

    m_toolbarSettingsAction = m_toolBar->addAction(QString());
    connect(m_toolbarSettingsAction, &QAction::triggered, m_settingsAction, &QAction::trigger);
}

void MainWindow::connectClientSignals()
{
    connect(m_client,
            &CoolerControlClient::connectionStateChanged,
            this,
            &MainWindow::onConnectionStateChanged);
    connect(m_client, &CoolerControlClient::connectionError, this, &MainWindow::onConnectionError);
    connect(m_client,
            &CoolerControlClient::authenticationRequired,
            this,
            &MainWindow::onAuthenticationRequired);
    connect(m_client, &CoolerControlClient::healthUpdated, this, &MainWindow::onHealthUpdated);
    connect(m_client, &CoolerControlClient::devicesUpdated, this, &MainWindow::onDevicesUpdated);
    connect(m_client, &CoolerControlClient::statusUpdated, this, &MainWindow::onStatusUpdated);
    connect(m_client, &CoolerControlClient::modesUpdated, this, &MainWindow::onModesUpdated);
    connect(m_client,
            &CoolerControlClient::activeModesUpdated,
            this,
            &MainWindow::onActiveModesUpdated);
    connect(m_client, &CoolerControlClient::profilesUpdated, this, &MainWindow::onProfilesUpdated);
    connect(m_client,
            &CoolerControlClient::overridesUpdated,
            this,
            &MainWindow::onOverridesUpdated);
    connect(m_client,
            &CoolerControlClient::deviceSettingsUpdated,
            this,
            &MainWindow::onDeviceSettingsUpdated);
    connect(m_client, &CoolerControlClient::modeActivated, this, &MainWindow::onModeActivated);
    connect(m_client,
            &CoolerControlClient::channelSettingUpdated,
            this,
            &MainWindow::onChannelSettingUpdated);
    connect(m_client,
            &CoolerControlClient::writeOperationFailed,
            this,
            &MainWindow::onWriteOperationFailed);
}

void MainWindow::loadSettings()
{
    QSettings settings;

    const QString langCode = settings.value(QStringLiteral("ui/language")).toString();
    Language lang = AppTranslator::defaultSystemLanguage();
    if (!langCode.isEmpty()) {
        lang = AppTranslator::fromCode(langCode);
    }
    if (m_translator != nullptr) {
        m_translator->setLanguage(lang);
    }
    if (m_langEnAction != nullptr && m_langTrAction != nullptr) {
        m_langEnAction->setChecked(lang == Language::English);
        m_langTrAction->setChecked(lang == Language::Turkish);
    }

    m_minimizeToTray = settings.value(QStringLiteral("ui/minimize_to_tray"), true).toBool();
    if (m_minimizeToTrayAction != nullptr) {
        m_minimizeToTrayAction->setChecked(m_minimizeToTray);
    }

    m_selectedDeviceUid = settings.value(QStringLiteral("ui/selected_device")).toString();
    m_saveToken = settings.value(QStringLiteral("connection/save_token"), true).toBool();

    const QByteArray geom = settings.value(QStringLiteral("ui/geometry")).toByteArray();
    if (!geom.isEmpty()) {
        restoreGeometry(geom);
    }
    const QByteArray splitState = settings.value(QStringLiteral("ui/splitter_state")).toByteArray();
    if (!splitState.isEmpty()) {
        m_splitter->restoreState(splitState);
    }

    ConnectionConfig cfg;
    const QString savedUrl = settings.value(QStringLiteral("connection/url"),
                                            QStringLiteral("http://localhost:11987"))
                                 .toString();
    QString normalized;
    cfg.baseUrl = CoolerControlClient::isValidBaseUrl(savedUrl, &normalized)
        ? normalized
        : QStringLiteral("http://localhost:11987");
    cfg.accessToken = settings.value(QStringLiteral("connection/token")).toString().trimmed();
    cfg.authMode = AuthMode::AccessToken;
    m_client->setConfig(cfg, false);
}

void MainWindow::saveWindowState()
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/splitter_state"), m_splitter->saveState());
    settings.setValue(QStringLiteral("ui/minimize_to_tray"), m_minimizeToTray);
    settings.setValue(QStringLiteral("ui/selected_device"), m_selectedDeviceUid);
    settings.sync();
}

void MainWindow::saveConnectionSettings(const ConnectionConfig& cfg, bool saveToken)
{
    m_saveToken = saveToken;
    QSettings settings;
    settings.setValue(QStringLiteral("connection/url"), cfg.baseUrl);
    settings.setValue(QStringLiteral("connection/save_token"), saveToken);
    if (saveToken && !cfg.accessToken.isEmpty()) {
        settings.setValue(QStringLiteral("connection/token"), cfg.accessToken);
    } else {
        settings.remove(QStringLiteral("connection/token"));
    }
    settings.sync();

    if (saveToken && !cfg.accessToken.isEmpty()) {
        QFile::setPermissions(settings.fileName(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

void MainWindow::setLanguageAndPersist(Language lang)
{
    if (m_translator != nullptr) {
        m_translator->setLanguage(lang);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("ui/language"), AppTranslator::toCode(lang));
    settings.sync();
}

void MainWindow::startInitialConnection(bool startMinimized)
{
    const ConnectionConfig cfg = m_client->config();
    if (cfg.accessToken.isEmpty()) {
        m_client->connectToDaemon();
        if (!startMinimized) {
            QTimer::singleShot(0, this, &MainWindow::openAuthDialog);
        }
    } else {
        m_client->connectToDaemon();
    }
}

void MainWindow::openAuthDialog()
{
    AuthDialog dlg(m_client, m_translator, m_client->config(), m_saveToken, this);
    connect(&dlg, &AuthDialog::languageChanged, this, [this](Language lang) {
        QSettings settings;
        settings.setValue(QStringLiteral("ui/language"), AppTranslator::toCode(lang));
        settings.sync();
        if (m_langEnAction != nullptr && m_langTrAction != nullptr) {
            m_langEnAction->setChecked(lang == Language::English);
            m_langTrAction->setChecked(lang == Language::Turkish);
        }
    });

    if (dlg.exec() == QDialog::Accepted) {
        const ConnectionConfig newCfg = dlg.resultConfig();
        saveConnectionSettings(newCfg, dlg.saveToken());
        m_banner->clear();
        m_client->setConfig(newCfg, true);
    }
}

void MainWindow::forceQuit()
{
    m_forceQuit = true;
    saveWindowState();
    m_client->disconnectFromDaemon();
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveWindowState();

    if (!m_forceQuit && m_minimizeToTray && m_trayController != nullptr
        && m_trayController->isAvailable()) {
        event->ignore();
        hide();
        m_trayController->setMainWindowVisible(false);
        return;
    }

    m_client->disconnectFromDaemon();
    event->accept();
    QApplication::quit();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void MainWindow::onConnectionStateChanged(ConnectionState state)
{
    m_connState = state;
    if (state == ConnectionState::Connected) {
        m_banner->clear();
    }
    updateConnectionIndicators();
    updateStatusBarText();
    if (m_trayController != nullptr) {
        m_trayController->updateConnectionState(m_connState, m_health);
    }
}

void MainWindow::onConnectionError(const QString& message)
{
    const StatusBanner::Severity sev = (m_connState == ConnectionState::RateLimited)
        ? StatusBanner::Severity::Warning
        : StatusBanner::Severity::Error;
    m_banner->showMessage(sev, message, true, true, false);
}

void MainWindow::onAuthenticationRequired(const QString& reason)
{
    m_banner->showMessage(StatusBanner::Severity::Error, reason, true, true, false);
}

void MainWindow::onHealthUpdated(const HealthInfo& health)
{
    m_health = health;
    updateConnectionIndicators();
    if (m_trayController != nullptr) {
        m_trayController->updateConnectionState(m_connState, m_health);
    }
}

void MainWindow::onDevicesUpdated(const QVector<DeviceInfo>& devices)
{
    m_devices = devices;
    sortDevicesForDisplay(m_devices, m_overrides);
    refreshDevicesListUi();
    rebuildDeviceCards();
}

void MainWindow::onStatusUpdated(const QVector<DeviceStatus>& statuses)
{
    m_lastUpdateTimeStr = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    for (const DeviceStatus& st : statuses) {
        m_lastStatusByDevice.insert(st.uid, st);
        DeviceCardWidget* card = m_cardsByUid.value(st.uid, nullptr);
        if (card != nullptr) {
            card->updateStatus(st);
        }
    }
    updateStatusBarText();
}

void MainWindow::onModesUpdated(const QVector<ModeInfo>& modes)
{
    m_modes = modes;
    refreshModesUi();
    if (m_trayController != nullptr) {
        m_trayController->updateModes(m_modes, m_activeModes.currentModeUid);
    }
}

void MainWindow::onActiveModesUpdated(const ActiveModes& activeModes)
{
    m_activeModes = activeModes;
    refreshModesUi();
    updateStatusBarText();
    if (m_trayController != nullptr) {
        m_trayController->updateModes(m_modes, m_activeModes.currentModeUid);
    }
}

void MainWindow::onProfilesUpdated(const QVector<ProfileInfo>& profiles)
{
    m_profiles = profiles;
    refreshProfilesListUi();
    for (DeviceCardWidget* card : std::as_const(m_cardsByUid)) {
        card->updateProfiles(m_profiles);
    }
}

void MainWindow::onOverridesUpdated(const NameOverrides& overrides)
{
    m_overrides = overrides;
    sortDevicesForDisplay(m_devices, m_overrides);
    refreshDevicesListUi();
    const QSet<QString> dups = findDuplicateDeviceNames(m_devices, m_overrides);
    for (int i = 0; i < m_devices.size(); ++i) {
        const DeviceInfo& dev = m_devices.at(i);
        if (DeviceCardWidget* card = m_cardsByUid.value(dev.uid, nullptr)) {
            const bool disambiguate = dups.contains(resolveDeviceDisplayName(dev, m_overrides, false));
            card->setOverrides(m_overrides, disambiguate);
            m_cardsLayout->removeWidget(card);
            m_cardsLayout->insertWidget(i, card);
        }
    }
}

void MainWindow::onDeviceSettingsUpdated(const QString& deviceUid,
                                         const QVector<ChannelSetting>& settings)
{
    m_settingsByDevice.insert(deviceUid, settings);
    if (DeviceCardWidget* card = m_cardsByUid.value(deviceUid, nullptr)) {
        card->updateSettings(settings);
    }
}

void MainWindow::onModeActivated(const QString& modeUid)
{
    QString name = modeUid;
    for (const ModeInfo& m : std::as_const(m_modes)) {
        if (m.uid == modeUid) {
            name = m.name;
            break;
        }
    }
    statusBar()->showMessage(tr("Mode '%1' activated").arg(name), 4000);
}

void MainWindow::onChannelSettingUpdated(const QString& deviceUid, const QString& channelName)
{
    if (DeviceCardWidget* card = m_cardsByUid.value(deviceUid, nullptr)) {
        card->setChannelPending(channelName, false);
    }
    const QString label = resolveChannelDisplayName(deviceUid, channelName, {}, m_overrides);
    statusBar()->showMessage(tr("Channel %1 updated").arg(label), 4000);
}

void MainWindow::onWriteOperationFailed(const QString& deviceUid,
                                        const QString& channelName,
                                        const QString& message,
                                        bool isForbidden)
{
    if (!deviceUid.isEmpty() && !channelName.isEmpty()) {
        if (DeviceCardWidget* card = m_cardsByUid.value(deviceUid, nullptr)) {
            card->revertChannelSelection(channelName);
        }
    }
    // Revert mode combo and tray selection if a mode activation failed
    refreshModesUi();
    if (m_trayController != nullptr) {
        m_trayController->updateModes(m_modes, m_activeModes.currentModeUid);
    }

    const StatusBanner::Severity sev =
        isForbidden ? StatusBanner::Severity::Warning : StatusBanner::Severity::Error;
    m_banner->showMessage(sev, message, isForbidden, false, true);
}

void MainWindow::rebuildDeviceCards()
{
    for (DeviceCardWidget* oldCard : std::as_const(m_cardsByUid)) {
        m_cardsLayout->removeWidget(oldCard);
        oldCard->deleteLater();
    }
    m_cardsByUid.clear();

    if (m_devices.isEmpty()) {
        m_rightStack->setCurrentIndex(0);
        return;
    }

    const QSet<QString> dups = findDuplicateDeviceNames(m_devices, m_overrides);
    const int insertBase = 0;
    int offset = 0;
    for (const DeviceInfo& dev : std::as_const(m_devices)) {
        const bool disambiguate = dups.contains(resolveDeviceDisplayName(dev, m_overrides, false));
        auto* card = new DeviceCardWidget(dev, m_overrides, disambiguate, m_cardsContainer);
        card->updateProfiles(m_profiles);
        if (m_settingsByDevice.contains(dev.uid)) {
            card->updateSettings(m_settingsByDevice.value(dev.uid));
        }
        if (m_lastStatusByDevice.contains(dev.uid)) {
            card->updateStatus(m_lastStatusByDevice.value(dev.uid));
        }

        connect(card,
                &DeviceCardWidget::profileChangeRequested,
                m_client,
                &CoolerControlClient::setChannelProfile);
        connect(card,
                &DeviceCardWidget::resetChannelRequested,
                m_client,
                &CoolerControlClient::resetChannelSetting);

        m_cardsLayout->insertWidget(insertBase + offset, card);
        m_cardsByUid.insert(dev.uid, card);
        ++offset;
    }

    m_rightStack->setCurrentIndex(1);
    applyDeviceFilter();
}

void MainWindow::applyDeviceFilter()
{
    for (auto it = m_cardsByUid.begin(); it != m_cardsByUid.end(); ++it) {
        const bool visible = m_selectedDeviceUid.isEmpty() || it.key() == m_selectedDeviceUid;
        it.value()->setVisible(visible);
    }
}

void MainWindow::refreshModesUi()
{
    // Sidebar modes list
    m_modesList->clear();
    const bool hasModes = !m_modes.isEmpty();
    m_modesList->setVisible(hasModes);
    m_modesEmptyLabel->setVisible(!hasModes);

    for (const ModeInfo& m : std::as_const(m_modes)) {
        const bool isActive = (m.uid == m_activeModes.currentModeUid);
        const QString text = isActive ? QStringLiteral("✓ %1").arg(m.name) : m.name;
        auto* item = new QListWidgetItem(text, m_modesList);
        item->setData(Qt::UserRole, m.uid);
        QFont f = item->font();
        f.setBold(isActive);
        item->setFont(f);
        if (isActive) {
            m_modesList->setCurrentItem(item);
        }
    }

    // Toolbar combo
    if (m_modeCombo->view() && m_modeCombo->view()->isVisible()) {
        return;
    }
    m_modeCombo->clear();
    if (!hasModes) {
        m_modeCombo->addItem(tr("No modes available"), QString());
        m_modeCombo->setEnabled(false);
        return;
    }

    m_modeCombo->setEnabled(true);
    int activeIdx = -1;
    if (m_activeModes.currentModeUid.isEmpty()) {
        m_modeCombo->addItem(tr("No active mode"), QString());
        activeIdx = 0;
    }
    for (const ModeInfo& m : std::as_const(m_modes)) {
        m_modeCombo->addItem(m.name, m.uid);
        if (m.uid == m_activeModes.currentModeUid) {
            activeIdx = m_modeCombo->count() - 1;
        }
    }
    if (activeIdx >= 0) {
        m_modeCombo->setCurrentIndex(activeIdx);
    }
}

void MainWindow::refreshDevicesListUi()
{
    const bool wasBlocked = m_devicesList->blockSignals(true);
    m_devicesList->clear();

    auto* allItem = new QListWidgetItem(tr("All Devices (%1)").arg(m_devices.size()), m_devicesList);
    allItem->setData(Qt::UserRole, QString());

    QListWidgetItem* itemToSelect = allItem;
    const QSet<QString> dups = findDuplicateDeviceNames(m_devices, m_overrides);

    for (const DeviceInfo& dev : std::as_const(m_devices)) {
        const bool disambiguate = dups.contains(resolveDeviceDisplayName(dev, m_overrides, false));
        const QString label = resolveDeviceDisplayName(dev, m_overrides, disambiguate);
        auto* item = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(label, dev.type),
                                         m_devicesList);
        item->setData(Qt::UserRole, dev.uid);
        if (dev.uid == m_selectedDeviceUid) {
            itemToSelect = item;
        }
    }

    if (itemToSelect == allItem) {
        m_selectedDeviceUid.clear();
    }
    m_devicesList->setCurrentItem(itemToSelect);
    m_devicesList->blockSignals(wasBlocked);
}

void MainWindow::refreshProfilesListUi()
{
    m_profilesList->clear();
    for (const ProfileInfo& p : std::as_const(m_profiles)) {
        auto* item = new QListWidgetItem(formatProfileSummary(p), m_profilesList);
        item->setData(Qt::UserRole, p.uid);
    }
}

QString MainWindow::formatProfileSummary(const ProfileInfo& profile) const
{
    const QString translatedType = tr(profile.type.toUtf8().constData());
    if (profile.type == QLatin1String("Fixed") && profile.speedFixed.has_value()) {
        return QStringLiteral("%1 · %2 · %3%").arg(profile.name, translatedType).arg(*profile.speedFixed);
    }
    if (profile.type == QLatin1String("Graph") && profile.tempMin.has_value()
        && profile.tempMax.has_value()) {
        return QStringLiteral("%1 · %2 · %3–%4 °C")
            .arg(profile.name, translatedType)
            .arg(QString::number(*profile.tempMin, 'f', 0),
                 QString::number(*profile.tempMax, 'f', 0));
    }
    if (profile.type == QLatin1String("Mix") && !profile.mixFunctionType.isEmpty()) {
        return QStringLiteral("%1 · %2 (%3)").arg(profile.name, translatedType, profile.mixFunctionType);
    }
    return QStringLiteral("%1 · %2").arg(profile.name, translatedType);
}

void MainWindow::updateConnectionIndicators()
{
    auto* dot = static_cast<StatusDotWidget*>(m_statusDot);
    switch (m_connState) {
    case ConnectionState::Connected: {
        dot->setColor(QColor(68, 185, 105));
        const QString ver = m_health.version.isEmpty() ? QStringLiteral("?") : m_health.version;
        m_connBadgeLabel->setText(tr("Connected · v%1").arg(ver));
        break;
    }
    case ConnectionState::Connecting:
        dot->setColor(QColor(224, 155, 40));
        m_connBadgeLabel->setText(tr("Connecting..."));
        break;
    case ConnectionState::Offline:
        dot->setColor(QColor(214, 70, 66));
        m_connBadgeLabel->setText(tr("Offline — Reconnecting..."));
        break;
    case ConnectionState::AuthError:
        dot->setColor(QColor(214, 70, 66));
        m_connBadgeLabel->setText(tr("Auth Error (401)"));
        break;
    case ConnectionState::RateLimited:
        dot->setColor(QColor(224, 155, 40));
        m_connBadgeLabel->setText(tr("Rate Limited (429)"));
        break;
    case ConnectionState::Disconnected:
        dot->setColor(QColor(140, 140, 140));
        m_connBadgeLabel->setText(tr("Disconnected"));
        break;
    }
}

void MainWindow::updateStatusBarText()
{
    const QString url = m_client->config().baseUrl;
    QString text;
    if (m_connState == ConnectionState::Connected) {
        const QString ts = m_lastUpdateTimeStr.isEmpty() ? QStringLiteral("—") : m_lastUpdateTimeStr;
        text = tr("URL: %1  |  Polling: 1s  |  Updated: %2").arg(url, ts);
    } else {
        text = tr("URL: %1  |  Waiting for daemon...").arg(url);
    }

    for (const ModeInfo& m : std::as_const(m_modes)) {
        if (m.uid == m_activeModes.currentModeUid) {
            text += QStringLiteral("  |  ") + tr("Active Mode: %1").arg(m.name);
            break;
        }
    }
    statusBar()->showMessage(text);
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("CCQt Upon"));

    m_fileMenu->setTitle(tr("&File"));
    m_settingsAction->setText(tr("Connection Settings..."));
    m_refreshAction->setText(tr("Refresh"));
    m_minimizeToTrayAction->setText(tr("Minimize to Tray on Close"));
    m_quitAction->setText(tr("Quit"));

    m_viewMenu->setTitle(tr("&View"));
    m_langMenu->setTitle(tr("&Language"));
    m_langEnAction->setText(tr("English"));
    m_langTrAction->setText(tr("Turkish (Türkçe)"));

    m_modeToolbarLabel->setText(tr("Active Mode:"));
    m_toolbarRefreshAction->setText(tr("Refresh"));
    m_toolbarSettingsAction->setText(tr("Connection Settings"));

    m_modesGroup->setTitle(tr("Modes"));
    m_modesEmptyLabel->setText(tr("No saved modes found in CoolerControl."));
    m_devicesGroup->setTitle(tr("Devices"));
    m_profilesGroup->setTitle(tr("Profiles"));

    m_emptyStateLabel->setText(
        tr("Connect to coolercontrold to view live sensors, temperatures, fan speeds, modes, and profiles."));
    m_emptySettingsButton->setText(tr("Open Connection Settings"));

    refreshModesUi();
    refreshDevicesListUi();
    refreshProfilesListUi();
    updateConnectionIndicators();
    updateStatusBarText();

    for (auto it = m_lastStatusByDevice.constBegin(); it != m_lastStatusByDevice.constEnd(); ++it) {
        if (DeviceCardWidget* card = m_cardsByUid.value(it.key(), nullptr)) {
            card->updateStatus(it.value());
        }
    }

    if (m_trayController != nullptr) {
        m_trayController->retranslateUi();
    }
}

} // namespace ccqt
