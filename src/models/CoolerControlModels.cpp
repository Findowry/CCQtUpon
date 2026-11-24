#include "models/CoolerControlModels.h"

#include <QCollator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>

namespace ccqt {
namespace {

QCollator makeNaturalCollator()
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator;
}

std::optional<QJsonObject> parseRootObject(const QByteArray& data)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    return doc.object();
}

QString readTypeField(const QJsonObject& obj)
{
    const QJsonValue typeVal = obj.value(QStringLiteral("type"));
    if (typeVal.isString() && !typeVal.toString().isEmpty()) {
        return typeVal.toString();
    }
    const QJsonValue dTypeVal = obj.value(QStringLiteral("d_type"));
    if (dTypeVal.isString()) {
        return dTypeVal.toString();
    }
    return {};
}

quint8 clampU8(int value, quint8 fallback = 0)
{
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    Q_UNUSED(fallback)
    return static_cast<quint8>(value);
}

std::optional<double> optionalDouble(const QJsonObject& obj, const QString& key)
{
    const QJsonValue val = obj.value(key);
    if (val.isDouble()) {
        return val.toDouble();
    }
    return std::nullopt;
}

std::optional<quint32> optionalU32(const QJsonObject& obj, const QString& key)
{
    const QJsonValue val = obj.value(key);
    if (val.isDouble()) {
        const double d = val.toDouble();
        if (d >= 0.0) {
            return static_cast<quint32>(d);
        }
    }
    return std::nullopt;
}

std::optional<quint8> optionalU8(const QJsonObject& obj, const QString& key)
{
    const QJsonValue val = obj.value(key);
    if (val.isDouble()) {
        return clampU8(val.toInt());
    }
    return std::nullopt;
}

ChannelSetting parseSettingObject(const QJsonObject& obj)
{
    ChannelSetting setting;
    setting.channelName = obj.value(QStringLiteral("channel_name")).toString();

    const QJsonValue resetVal = obj.value(QStringLiteral("reset_to_default"));
    if (resetVal.isBool()) {
        setting.resetToDefault = resetVal.toBool(false);
    }

    if (const auto fixed = optionalU8(obj, QStringLiteral("speed_fixed"))) {
        setting.speedFixed = fixed;
    }

    const QJsonValue profileVal = obj.value(QStringLiteral("profile_uid"));
    if (profileVal.isString() && !profileVal.toString().isEmpty()) {
        setting.profileUid = profileVal.toString();
    }

    return setting;
}

int deviceCategoryPriority(const DeviceInfo& device)
{
    if (device.hasControllableChannels()) {
        return 0;
    }
    if (device.type == QLatin1String("CPU")) {
        return 1;
    }
    if (device.type == QLatin1String("GPU")) {
        return 2;
    }
    if (device.type == QLatin1String("Liquidctl")) {
        return 3;
    }
    if (device.type == QLatin1String("CustomSensors")) {
        return 4;
    }
    return 5;
}

} // namespace

bool DeviceInfo::hasControllableChannels() const
{
    return std::any_of(channels.cbegin(), channels.cend(), [](const ChannelInfo& ch) {
        return ch.isControllable();
    });
}

bool parseHandshake(const QByteArray& data)
{
    const auto root = parseRootObject(data);
    if (!root.has_value()) {
        return false;
    }
    return root->value(QStringLiteral("shake")).toBool(false);
}

std::optional<HealthInfo> parseHealth(const QByteArray& data)
{
    const auto root = parseRootObject(data);
    if (!root.has_value()) {
        return std::nullopt;
    }

    const QJsonValue statusVal = root->value(QStringLiteral("status"));
    const QJsonValue detailsVal = root->value(QStringLiteral("details"));
    if (!statusVal.isString() && !detailsVal.isObject()) {
        return std::nullopt;
    }

    HealthInfo info;
    info.status = statusVal.toString();

    if (detailsVal.isObject()) {
        const QJsonObject details = detailsVal.toObject();
        info.version = details.value(QStringLiteral("version")).toString();
        info.uptime = details.value(QStringLiteral("uptime")).toString();
        info.warnings = details.value(QStringLiteral("warnings")).toInt(0);
        info.errors = details.value(QStringLiteral("errors")).toInt(0);
    }

    const QJsonValue systemVal = root->value(QStringLiteral("system"));
    if (systemVal.isObject()) {
        info.systemName = systemVal.toObject().value(QStringLiteral("name")).toString();
    }

    info.valid = true;
    return info;
}

std::optional<QVector<DeviceInfo>> parseDevices(const QByteArray& data)
{
    const auto root = parseRootObject(data);
    if (!root.has_value()) {
        return std::nullopt;
    }

    const QJsonValue devicesVal = root->value(QStringLiteral("devices"));
    if (!devicesVal.isArray()) {
        return std::nullopt;
    }

    const QCollator collator = makeNaturalCollator();
    const QJsonArray devicesArr = devicesVal.toArray();
    QVector<DeviceInfo> result;
    result.reserve(devicesArr.size());

    for (const QJsonValue& item : devicesArr) {
        if (!item.isObject()) {
            continue;
        }
        const QJsonObject obj = item.toObject();
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        if (uid.isEmpty()) {
            continue;
        }

        DeviceInfo dev;
        dev.uid = uid;
        dev.name = obj.value(QStringLiteral("name")).toString();
        dev.type = readTypeField(obj);
        dev.typeIndex = clampU8(obj.value(QStringLiteral("type_index")).toInt(0));

        const QJsonObject infoObj = obj.value(QStringLiteral("info")).toObject();
        if (infoObj.value(QStringLiteral("model")).isString()) {
            dev.model = infoObj.value(QStringLiteral("model")).toString();
        }
        dev.tempMin = clampU8(infoObj.value(QStringLiteral("temp_min")).toInt(0));
        dev.tempMax = clampU8(infoObj.value(QStringLiteral("temp_max")).toInt(100));

        const QJsonObject drvObj = infoObj.value(QStringLiteral("driver_info")).toObject();
        dev.driverInfo.drvType = drvObj.value(QStringLiteral("drv_type")).toString();
        if (drvObj.value(QStringLiteral("name")).isString()) {
            dev.driverInfo.name = drvObj.value(QStringLiteral("name")).toString();
        }
        if (drvObj.value(QStringLiteral("version")).isString()) {
            dev.driverInfo.version = drvObj.value(QStringLiteral("version")).toString();
        }
        const QJsonArray locArr = drvObj.value(QStringLiteral("locations")).toArray();
        for (const QJsonValue& locVal : locArr) {
            if (locVal.isString()) {
                dev.driverInfo.locations.append(locVal.toString());
            }
        }

        const QJsonObject channelsObj = infoObj.value(QStringLiteral("channels")).toObject();
        dev.channels.reserve(channelsObj.size());
        for (auto it = channelsObj.constBegin(); it != channelsObj.constEnd(); ++it) {
            if (!it.value().isObject()) {
                continue;
            }
            const QJsonObject chObj = it.value().toObject();
            ChannelInfo ch;
            ch.name = it.key();
            if (chObj.value(QStringLiteral("label")).isString()) {
                ch.label = chObj.value(QStringLiteral("label")).toString();
            }
            if (chObj.value(QStringLiteral("speed_options")).isObject()) {
                const QJsonObject soObj = chObj.value(QStringLiteral("speed_options")).toObject();
                SpeedOptions so;
                so.minDuty = clampU8(soObj.value(QStringLiteral("min_duty")).toInt(0));
                so.maxDuty = clampU8(soObj.value(QStringLiteral("max_duty")).toInt(100));
                so.fixedEnabled = soObj.value(QStringLiteral("fixed_enabled")).toBool(false);
                if (soObj.value(QStringLiteral("extension")).isString()) {
                    so.extension = soObj.value(QStringLiteral("extension")).toString();
                }
                ch.speedOptions = so;
            }
            dev.channels.append(std::move(ch));
        }

        std::sort(dev.channels.begin(), dev.channels.end(), [&collator](const ChannelInfo& a, const ChannelInfo& b) {
            if (a.isControllable() != b.isControllable()) {
                return a.isControllable();
            }
            return collator.compare(a.name, b.name) < 0;
        });

        const QJsonObject tempsObj = infoObj.value(QStringLiteral("temps")).toObject();
        dev.temps.reserve(tempsObj.size());
        for (auto it = tempsObj.constBegin(); it != tempsObj.constEnd(); ++it) {
            if (!it.value().isObject()) {
                continue;
            }
            const QJsonObject tObj = it.value().toObject();
            TempInfo t;
            t.name = it.key();
            if (tObj.value(QStringLiteral("label")).isString()) {
                t.label = tObj.value(QStringLiteral("label")).toString();
            }
            t.number = clampU8(tObj.value(QStringLiteral("number")).toInt(0));
            dev.temps.append(std::move(t));
        }

        std::sort(dev.temps.begin(), dev.temps.end(), [&collator](const TempInfo& a, const TempInfo& b) {
            if (a.number != b.number) {
                return a.number < b.number;
            }
            return collator.compare(a.name, b.name) < 0;
        });

        result.append(std::move(dev));
    }

    return result;
}

std::optional<QVector<DeviceStatus>> parseStatus(const QByteArray& data)
{
    const auto root = parseRootObject(data);
    if (!root.has_value()) {
        return std::nullopt;
    }

    const QJsonValue devicesVal = root->value(QStringLiteral("devices"));
    if (!devicesVal.isArray()) {
        return std::nullopt;
    }

    const QJsonArray devicesArr = devicesVal.toArray();
    QVector<DeviceStatus> result;
    result.reserve(devicesArr.size());

    for (const QJsonValue& item : devicesArr) {
        if (!item.isObject()) {
            continue;
        }
        const QJsonObject obj = item.toObject();
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        if (uid.isEmpty()) {
            continue;
        }

        const QJsonArray history = obj.value(QStringLiteral("status_history")).toArray();
        if (history.isEmpty()) {
            continue;
        }

        const QJsonValue lastVal = history.last();
        if (!lastVal.isObject()) {
            continue;
        }
        const QJsonObject lastObj = lastVal.toObject();

        DeviceStatus st;
        st.uid = uid;
        st.type = readTypeField(obj);
        st.typeIndex = clampU8(obj.value(QStringLiteral("type_index")).toInt(0));
        st.timestamp = lastObj.value(QStringLiteral("timestamp")).toString();

        const QJsonArray tempsArr = lastObj.value(QStringLiteral("temps")).toArray();
        st.temps.reserve(tempsArr.size());
        for (const QJsonValue& tVal : tempsArr) {
            if (!tVal.isObject()) {
                continue;
            }
            const QJsonObject tObj = tVal.toObject();
            const QString tName = tObj.value(QStringLiteral("name")).toString();
            const QJsonValue tempVal = tObj.value(QStringLiteral("temp"));
            if (tName.isEmpty() || !tempVal.isDouble()) {
                continue;
            }
            TempReading reading;
            reading.name = tName;
            reading.temp = tempVal.toDouble();
            st.temps.append(std::move(reading));
        }

        const QJsonArray chArr = lastObj.value(QStringLiteral("channels")).toArray();
        st.channels.reserve(chArr.size());
        for (const QJsonValue& cVal : chArr) {
            if (!cVal.isObject()) {
                continue;
            }
            const QJsonObject cObj = cVal.toObject();
            const QString cName = cObj.value(QStringLiteral("name")).toString();
            if (cName.isEmpty()) {
                continue;
            }
            ChannelReading ch;
            ch.name = cName;
            ch.rpm = optionalU32(cObj, QStringLiteral("rpm"));
            ch.duty = optionalDouble(cObj, QStringLiteral("duty"));
            ch.freq = optionalU32(cObj, QStringLiteral("freq"));
            ch.watts = optionalDouble(cObj, QStringLiteral("watts"));
            ch.pwmMode = optionalU8(cObj, QStringLiteral("pwm_mode"));
            st.channels.append(std::move(ch));
        }

        result.append(std::move(st));
    }

    return result;
}

std::optional<QVector<ChannelSetting>> parseDeviceSettings(const QByteArray& data)
{
    const auto root = parseRootObject(data);
    if (!root.has_value()) {
        return std::nullopt;
    }

    const QJsonValue settingsVal = root->value(QStringLiteral("settings"));
    if (!settingsVal.isArray()) {
        return std::nullopt;
    }

    const QJsonArray arr = settingsVal.toArray();
    QVector<ChannelSetting> result;
    result.reserve(arr.size());

    for (const QJsonValue& item : arr) {
        if (!item.isObject()) {
            continue;
        }
        ChannelSetting setting = parseSettingObject(item.toObject());
        if (!setting.channelName.isEmpty()) {
            result.append(std::move(setting));
        }
    }

    return result;
}

std::optional<QVector<ModeInfo>> parseModes(const QByteArray& data)
{
    const auto root = parseRootObject(data);
    if (!root.has_value()) {
        return std::nullopt;
    }

    const QJsonValue modesVal = root->value(QStringLiteral("modes"));
    if (!modesVal.isArray()) {
        return std::nullopt;
    }

    const QJsonArray arr = modesVal.toArray();
    QVector<ModeInfo> result;
    result.reserve(arr.size());

    for (const QJsonValue& item : arr) {
        if (!item.isObject()) {
            continue;
        }
        const QJsonObject obj = item.toObject();
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        if (uid.isEmpty()) {
            continue;
        }

        ModeInfo mode;
        mode.uid = uid;
        mode.name = obj.value(QStringLiteral("name")).toString();

        const QJsonArray dsArr = obj.value(QStringLiteral("device_settings")).toArray();
        for (const QJsonValue& pairVal : dsArr) {
            if (!pairVal.isArray()) {
                continue;
            }
            const QJsonArray pair = pairVal.toArray();
            if (pair.size() < 2 || !pair.at(0).isString() || !pair.at(1).isArray()) {
                continue;
            }
            ModeDeviceSettings mds;
            mds.deviceUid = pair.at(0).toString();
            const QJsonArray sArr = pair.at(1).toArray();
            mds.settings.reserve(sArr.size());
            for (const QJsonValue& sVal : sArr) {
                if (sVal.isObject()) {
                    ChannelSetting cs = parseSettingObject(sVal.toObject());
                    if (!cs.channelName.isEmpty()) {
                        mds.settings.append(std::move(cs));
                    }
                }
            }
            mode.deviceSettings.append(std::move(mds));
        }

        result.append(std::move(mode));
    }

    return result;
}

std::optional<ActiveModes> parseActiveModes(const QByteArray& data)
{
    const auto root = parseRootObject(data);
    if (!root.has_value()) {
        return std::nullopt;
    }

    if (!root->contains(QStringLiteral("current_mode_uid"))
        && !root->contains(QStringLiteral("previous_mode_uid"))) {
        return std::nullopt;
    }

    ActiveModes active;
    if (root->value(QStringLiteral("current_mode_uid")).isString()) {
        active.currentModeUid = root->value(QStringLiteral("current_mode_uid")).toString();
    }
    if (root->value(QStringLiteral("previous_mode_uid")).isString()) {
        active.previousModeUid = root->value(QStringLiteral("previous_mode_uid")).toString();
    }
    return active;
}

std::optional<QVector<ProfileInfo>> parseProfiles(const QByteArray& data)
{
    const auto root = parseRootObject(data);
    if (!root.has_value()) {
        return std::nullopt;
    }

    const QJsonValue profilesVal = root->value(QStringLiteral("profiles"));
    if (!profilesVal.isArray()) {
        return std::nullopt;
    }

    const QJsonArray arr = profilesVal.toArray();
    QVector<ProfileInfo> result;
    result.reserve(arr.size());

    for (const QJsonValue& item : arr) {
        if (!item.isObject()) {
            continue;
        }
        const QJsonObject obj = item.toObject();
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        if (uid.isEmpty()) {
            continue;
        }

        ProfileInfo p;
        p.uid = uid;
        p.name = obj.value(QStringLiteral("name")).toString();
        p.functionUid = obj.value(QStringLiteral("function_uid")).toString();
        p.type = obj.value(QStringLiteral("p_type")).toString();
        if (p.type.isEmpty()) {
            p.type = QStringLiteral("Default");
        }

        p.speedFixed = optionalU8(obj, QStringLiteral("speed_fixed"));
        p.tempMin = optionalDouble(obj, QStringLiteral("temp_min"));
        p.tempMax = optionalDouble(obj, QStringLiteral("temp_max"));

        const QJsonValue spVal = obj.value(QStringLiteral("speed_profile"));
        if (spVal.isArray()) {
            const QJsonArray spArr = spVal.toArray();
            p.speedProfile.reserve(spArr.size());
            for (const QJsonValue& ptVal : spArr) {
                if (!ptVal.isArray()) {
                    continue;
                }
                const QJsonArray pt = ptVal.toArray();
                if (pt.size() >= 2 && pt.at(0).isDouble() && pt.at(1).isDouble()) {
                    p.speedProfile.append({pt.at(0).toDouble(), clampU8(pt.at(1).toInt())});
                }
            }
        }

        const QJsonValue tsVal = obj.value(QStringLiteral("temp_source"));
        if (tsVal.isObject()) {
            const QJsonObject tsObj = tsVal.toObject();
            TempSource ts;
            ts.deviceUid = tsObj.value(QStringLiteral("device_uid")).toString();
            ts.tempName = tsObj.value(QStringLiteral("temp_name")).toString();
            if (!ts.deviceUid.isEmpty() || !ts.tempName.isEmpty()) {
                p.tempSource = ts;
            }
        }

        const QJsonValue membersVal = obj.value(QStringLiteral("member_profile_uids"));
        if (membersVal.isArray()) {
            for (const QJsonValue& mVal : membersVal.toArray()) {
                if (mVal.isString()) {
                    p.memberProfileUids.append(mVal.toString());
                }
            }
        }

        const QJsonValue mixVal = obj.value(QStringLiteral("mix_function_type"));
        if (mixVal.isString()) {
            p.mixFunctionType = mixVal.toString();
        }

        result.append(std::move(p));
    }

    return result;
}

NameOverrides parseOverrides(const QByteArray& data)
{
    NameOverrides overrides;
    const auto root = parseRootObject(data);
    if (!root.has_value()) {
        return overrides;
    }

    const QJsonObject devicesObj = root->value(QStringLiteral("devices")).toObject();
    for (auto it = devicesObj.constBegin(); it != devicesObj.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject devObj = it.value().toObject();
        DeviceOverride devOverride;
        if (devObj.value(QStringLiteral("name")).isString()) {
            devOverride.name = devObj.value(QStringLiteral("name")).toString().trimmed();
        }
        const QJsonObject chObj = devObj.value(QStringLiteral("channels")).toObject();
        for (auto chIt = chObj.constBegin(); chIt != chObj.constEnd(); ++chIt) {
            if (!chIt.value().isObject()) {
                continue;
            }
            const QJsonValue labelVal = chIt.value().toObject().value(QStringLiteral("label"));
            if (labelVal.isString() && !labelVal.toString().trimmed().isEmpty()) {
                devOverride.channelLabels.insert(chIt.key(), labelVal.toString().trimmed());
            }
        }
        if (!devOverride.name.isEmpty() || !devOverride.channelLabels.isEmpty()) {
            overrides.insert(it.key(), std::move(devOverride));
        }
    }

    return overrides;
}

QString resolveDeviceDisplayName(const DeviceInfo& device,
                                 const NameOverrides& overrides,
                                 bool disambiguate)
{
    QString base;
    const auto ovIt = overrides.constFind(device.uid);
    if (ovIt != overrides.constEnd() && !ovIt->name.trimmed().isEmpty()) {
        base = ovIt->name.trimmed();
    } else if (!device.model.trimmed().isEmpty()) {
        base = device.model.trimmed();
    } else if (!device.name.trimmed().isEmpty()) {
        base = device.name.trimmed();
    } else {
        base = device.uid;
    }

    if (disambiguate) {
        return QStringLiteral("%1 (#%2)").arg(base).arg(device.typeIndex);
    }
    return base;
}

QSet<QString> findDuplicateDeviceNames(const QVector<DeviceInfo>& devices,
                                       const NameOverrides& overrides)
{
    QHash<QString, int> counts;
    for (const DeviceInfo& dev : devices) {
        const QString base = resolveDeviceDisplayName(dev, overrides, false);
        counts[base] += 1;
    }
    QSet<QString> duplicates;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > 1) {
            duplicates.insert(it.key());
        }
    }
    return duplicates;
}

QString resolveChannelDisplayName(const QString& deviceUid,
                                  const QString& rawName,
                                  const QString& infoLabel,
                                  const NameOverrides& overrides)
{
    const auto ovIt = overrides.constFind(deviceUid);
    if (ovIt != overrides.constEnd()) {
        const QString overridden = ovIt->channelLabels.value(rawName);
        if (!overridden.isEmpty()) {
            return overridden;
        }
    }
    if (!infoLabel.trimmed().isEmpty()) {
        return infoLabel.trimmed();
    }
    return rawName;
}

void sortDevicesForDisplay(QVector<DeviceInfo>& devices, const NameOverrides& overrides)
{
    const QCollator collator = makeNaturalCollator();
    std::sort(devices.begin(), devices.end(), [&](const DeviceInfo& a, const DeviceInfo& b) {
        const int pa = deviceCategoryPriority(a);
        const int pb = deviceCategoryPriority(b);
        if (pa != pb) {
            return pa < pb;
        }
        const QString nameA = resolveDeviceDisplayName(a, overrides, false);
        const QString nameB = resolveDeviceDisplayName(b, overrides, false);
        const int cmp = collator.compare(nameA, nameB);
        if (cmp != 0) {
            return cmp < 0;
        }
        if (a.typeIndex != b.typeIndex) {
            return a.typeIndex < b.typeIndex;
        }
        return a.uid < b.uid;
    });
}

} // namespace ccqt
