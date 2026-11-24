#pragma once

#include "models/CoolerControlModels.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

#include <deque>
#include <optional>

class QButtonGroup;
class QComboBox;
class QEvent;
class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QSplitter;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace ccqt {

class CoolerControlClient;
class FanCurveEditorWidget;
class FanGaugeWidget;
class FanHistoryChartWidget;

class CoolingTabWidget : public QWidget {
    Q_OBJECT

public:
    explicit CoolingTabWidget(CoolerControlClient* client, QWidget* parent = nullptr);

    void setDevices(const QVector<DeviceInfo>& devices);
    void setOverrides(const NameOverrides& overrides);
    void setProfiles(const QVector<ProfileInfo>& profiles);
    void setFunctions(const QVector<FunctionInfo>& functions);
    void setDeviceSettings(const QString& deviceUid, const QVector<ChannelSetting>& settings);
    void updateStatus(const QVector<DeviceStatus>& statuses);

    void handleChannelWriteResult(const QString& deviceUid,
                                  const QString& channelName,
                                  bool success);
    void handleProfileWriteResult(const QString& profileUid, bool success);

    QByteArray saveSplitterState() const;
    bool restoreSplitterState(const QByteArray& state);

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onFanTreeSelectionChanged();
    void onModeButtonClicked(int id);
    void onProfileComboActivated(int index);
    void onApplyClicked();
    void onAddPointClicked();
    void onRemovePointClicked();

private:
    enum ControlMode {
        ModeProfile = 0,
        ModeManual = 1,
        ModeUnmanaged = 2
    };

    void buildUi();
    void rebuildFanTree();
    void updateFanTreeTelemetry();
    void refreshSidebarProfilesAndFunctions();
    void loadSelectedFanWorkspace();
    void syncModeButtonsAndPage(ControlMode mode);
    void loadProfileIntoEditor(const QString& profileUid);
    void refreshProfileCombo();
    void refreshProfileMetaAndLiveTemp();
    void populatePointsTable();
    void updateCurveLiveTelemetry();
    void updateManualControlsBounds(int minDuty, int maxDuty);
    void updateSelectedFanTelemetryHeader();
    void updateApplyEnabled();
    void refreshMutedPalettes();
    void retranslateUi();

    const DeviceInfo* findDevice(const QString& uid) const;
    const ChannelInfo* findChannel(const DeviceInfo& dev, const QString& channelName) const;
    const ProfileInfo* findProfile(const QString& uid) const;
    const FunctionInfo* findFunction(const QString& uid) const;
    std::optional<ChannelSetting> findChannelSetting(const QString& deviceUid,
                                                     const QString& channelName) const;
    static QString historyKey(const QString& deviceUid, const QString& channelName);

    CoolerControlClient* m_client = nullptr;

    QVector<DeviceInfo> m_devices;
    NameOverrides m_overrides;
    QVector<ProfileInfo> m_profiles;
    QVector<FunctionInfo> m_functions;
    QHash<QString, QVector<ChannelSetting>> m_settingsByDevice;
    QHash<QString, DeviceStatus> m_lastStatusByDevice;
    QHash<QString, std::deque<FanHistoryPoint>> m_historyByChannel;

    QString m_selectedDeviceUid;
    QString m_selectedChannelName;
    ControlMode m_currentMode = ModeProfile;
    ProfileInfo m_editingProfile;
    bool m_profileDirty = false;
    bool m_syncingPoints = false;
    bool m_syncingManual = false;
    bool m_pendingApply = false;
    bool m_pendingProfileChain = false;
    QString m_chainDeviceUid;
    QString m_chainChannelName;
    QString m_chainProfileUid;

    QSplitter* m_splitter = nullptr;

    // Left sidebar
    QGroupBox* m_fansGroup = nullptr;
    QTreeWidget* m_fansTree = nullptr;
    QLabel* m_fansEmptyLabel = nullptr;
    QHash<QString, QTreeWidgetItem*> m_fanItemsByKey;

    QGroupBox* m_profilesGroup = nullptr;
    QListWidget* m_profilesList = nullptr;

    QGroupBox* m_functionsGroup = nullptr;
    QListWidget* m_functionsList = nullptr;
    QLabel* m_functionsEmptyLabel = nullptr;

    // Right workspace
    QStackedWidget* m_rightStack = nullptr;
    QLabel* m_emptyWorkspaceLabel = nullptr;

    QLabel* m_fanTitleLabel = nullptr;
    QLabel* m_fanTelemetryLabel = nullptr;
    QButtonGroup* m_modeGroup = nullptr;
    QPushButton* m_modeProfileBtn = nullptr;
    QPushButton* m_modeManualBtn = nullptr;
    QPushButton* m_modeUnmanagedBtn = nullptr;
    QPushButton* m_applyBtn = nullptr;

    QStackedWidget* m_modeStack = nullptr;

    // Profile mode widgets
    QLabel* m_profileSelectLabel = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QLabel* m_profileTypeCaption = nullptr;
    QLabel* m_profileTypeValue = nullptr;
    QLabel* m_tempSourceCaption = nullptr;
    QLabel* m_tempSourceValue = nullptr;
    QLabel* m_functionCaption = nullptr;
    QLabel* m_functionValue = nullptr;

    QStackedWidget* m_profileInnerStack = nullptr;
    FanCurveEditorWidget* m_curveEditor = nullptr;
    QGroupBox* m_pointsGroup = nullptr;
    QTableWidget* m_pointsTable = nullptr;
    QPushButton* m_addPointBtn = nullptr;
    QPushButton* m_removePointBtn = nullptr;

    QLabel* m_fixedSpeedLabel = nullptr;
    QSlider* m_fixedSlider = nullptr;
    QSpinBox* m_fixedSpin = nullptr;

    QLabel* m_otherProfileInfoLabel = nullptr;

    // Manual mode widgets
    FanGaugeWidget* m_fanGauge = nullptr;
    QToolButton* m_manualMinusBtn = nullptr;
    QSlider* m_manualSlider = nullptr;
    QToolButton* m_manualPlusBtn = nullptr;
    QSpinBox* m_manualSpin = nullptr;
    FanHistoryChartWidget* m_historyChart = nullptr;

    // Unmanaged mode widgets
    QLabel* m_unmanagedTitleLabel = nullptr;
    QLabel* m_unmanagedDescLabel = nullptr;
};

} // namespace ccqt
