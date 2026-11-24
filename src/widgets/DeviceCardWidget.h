#pragma once

#include "models/CoolerControlModels.h"

#include <QFrame>
#include <QHash>
#include <QString>
#include <QVector>

class QComboBox;
class QEvent;
class QLabel;
class QPushButton;

namespace ccqt {

class MetricBarWidget;

class DeviceCardWidget : public QFrame {
    Q_OBJECT

public:
    DeviceCardWidget(const DeviceInfo& device,
                     const NameOverrides& overrides,
                     bool disambiguateName,
                     QWidget* parent = nullptr);

    QString deviceUid() const;
    void setOverrides(const NameOverrides& overrides, bool disambiguateName);
    void updateProfiles(const QVector<ProfileInfo>& profiles);
    void updateSettings(const QVector<ChannelSetting>& settings);
    void updateStatus(const DeviceStatus& status);

    void setChannelPending(const QString& channelName, bool pending);
    void revertChannelSelection(const QString& channelName);

signals:
    void profileChangeRequested(const QString& deviceUid,
                                const QString& channelName,
                                const QString& profileUid);
    void resetChannelRequested(const QString& deviceUid, const QString& channelName);

protected:
    void changeEvent(QEvent* event) override;

private:
    struct ChannelRow {
        ChannelInfo info;
        QLabel* nameLabel = nullptr;
        QLabel* valueLabel = nullptr;
        MetricBarWidget* bar = nullptr;
        QComboBox* profileCombo = nullptr;
        QPushButton* resetButton = nullptr;
        QString activeProfileUid;
        std::optional<quint8> activeManualDuty;
        QString prevProfileUid;
        std::optional<quint8> prevManualDuty;
        bool pending = false;
    };

    struct TempRow {
        TempInfo info;
        QLabel* nameLabel = nullptr;
        QLabel* valueLabel = nullptr;
        MetricBarWidget* bar = nullptr;
    };

    void buildUi();
    void refreshHeaderText();
    void refreshSecondaryPalette();
    void populateChannelCombo(ChannelRow& row);
    QString formatProfileLabel(const ProfileInfo& profile) const;
    void retranslateUi();

    DeviceInfo m_device;
    NameOverrides m_overrides;
    bool m_disambiguateName = false;
    QVector<ProfileInfo> m_profiles;
    QHash<QString, ChannelSetting> m_settingsByChannel;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLabel* m_typeLabel = nullptr;
    QLabel* m_channelsSectionLabel = nullptr;
    QLabel* m_tempsSectionLabel = nullptr;

    QHash<QString, ChannelRow> m_channelRows;
    QHash<QString, TempRow> m_tempRows;
};

} // namespace ccqt
