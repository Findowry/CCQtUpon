#pragma once

#include <QByteArray>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtTypes>

#include <optional>

namespace ccqt {

struct HealthInfo {
    QString status;
    QString version;
    QString uptime;
    QString systemName;
    int warnings = 0;
    int errors = 0;
    bool valid = false;
};

struct SpeedOptions {
    quint8 minDuty = 0;
    quint8 maxDuty = 100;
    bool fixedEnabled = false;
    QString extension;
};

struct ChannelInfo {
    QString name;
    QString label;
    std::optional<SpeedOptions> speedOptions;

    bool isControllable() const
    {
        return speedOptions.has_value() && speedOptions->fixedEnabled;
    }
};

struct TempInfo {
    QString name;
    QString label;
    quint8 number = 0;
};

struct DriverInfo {
    QString drvType;
    QString name;
    QString version;
    QStringList locations;
};

struct DeviceInfo {
    QString uid;
    QString name;
    QString type;
    quint8 typeIndex = 0;
    QString model;
    quint8 tempMin = 0;
    quint8 tempMax = 100;
    quint8 profileMinLength = 2;
    quint8 profileMaxLength = 21;
    DriverInfo driverInfo;
    QVector<ChannelInfo> channels;
    QVector<TempInfo> temps;

    bool hasControllableChannels() const;
};

struct TempReading {
    QString name;
    double temp = 0.0;
};

struct ChannelReading {
    QString name;
    std::optional<quint32> rpm;
    std::optional<double> duty;
    std::optional<quint32> freq;
    std::optional<double> watts;
    std::optional<quint8> pwmMode;
};

struct DeviceStatus {
    QString uid;
    QString type;
    quint8 typeIndex = 0;
    QString timestamp;
    QVector<TempReading> temps;
    QVector<ChannelReading> channels;
};

struct ChannelSetting {
    QString channelName;
    std::optional<QString> profileUid;
    std::optional<quint8> speedFixed;
    bool resetToDefault = false;
};

struct ModeDeviceSettings {
    QString deviceUid;
    QVector<ChannelSetting> settings;
};

struct ModeInfo {
    QString uid;
    QString name;
    QVector<ModeDeviceSettings> deviceSettings;
};

struct ActiveModes {
    QString currentModeUid;
    QString previousModeUid;
};

struct TempSource {
    QString deviceUid;
    QString tempName;
};

struct ProfileInfo {
    QString uid;
    QString name;
    QString functionUid;
    QString type;
    std::optional<quint8> speedFixed;
    QVector<QPair<double, quint8>> speedProfile;
    std::optional<TempSource> tempSource;
    std::optional<double> tempMin;
    std::optional<double> tempMax;
    QStringList memberProfileUids;
    QString mixFunctionType;
    QVector<QPair<quint8, qint8>> offsetProfile;
};

struct FunctionInfo {
    QString uid;
    QString name;
    QString type;
    quint8 dutyMinimum = 2;
    quint8 dutyMaximum = 100;
    quint8 stepSizeMinDecreasing = 0;
    quint8 stepSizeMaxDecreasing = 0;
    bool thresholdHopping = true;
    bool bypassMinAtExtremes = false;
    std::optional<double> deviance;
    std::optional<bool> onlyDownward;
    std::optional<quint8> responseDelay;
};

struct FanHistoryPoint {
    qint64 timestampMs = 0;
    std::optional<double> duty;
    std::optional<quint32> rpm;
};

struct DeviceOverride {
    QString name;
    QHash<QString, QString> channelLabels;
};

using NameOverrides = QHash<QString, DeviceOverride>;

bool parseHandshake(const QByteArray& data);
std::optional<HealthInfo> parseHealth(const QByteArray& data);
std::optional<QVector<DeviceInfo>> parseDevices(const QByteArray& data);
std::optional<QVector<DeviceStatus>> parseStatus(const QByteArray& data);
std::optional<QVector<ChannelSetting>> parseDeviceSettings(const QByteArray& data);
std::optional<QVector<ModeInfo>> parseModes(const QByteArray& data);
std::optional<ActiveModes> parseActiveModes(const QByteArray& data);
std::optional<QVector<ProfileInfo>> parseProfiles(const QByteArray& data);
std::optional<QVector<FunctionInfo>> parseFunctions(const QByteArray& data);
NameOverrides parseOverrides(const QByteArray& data);
QByteArray serializeProfile(const ProfileInfo& profile);

QString resolveDeviceDisplayName(const DeviceInfo& device,
                                 const NameOverrides& overrides,
                                 bool disambiguate = false);
QSet<QString> findDuplicateDeviceNames(const QVector<DeviceInfo>& devices,
                                       const NameOverrides& overrides);
QString resolveChannelDisplayName(const QString& deviceUid,
                                  const QString& rawName,
                                  const QString& infoLabel,
                                  const NameOverrides& overrides);
void sortDevicesForDisplay(QVector<DeviceInfo>& devices, const NameOverrides& overrides);

} // namespace ccqt
