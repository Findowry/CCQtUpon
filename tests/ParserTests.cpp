#include "api/CoolerControlClient.h"
#include "models/CoolerControlModels.h"

#include <QByteArray>
#include <QCoreApplication>
#include <iostream>

namespace {

int g_failures = 0;

void check(bool cond, const char* expr, int line)
{
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << expr << '\n';
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void testHandshakeAndHealth()
{
    CHECK(ccqt::parseHandshake(R"({"shake":true})"));
    CHECK(!ccqt::parseHandshake(R"({"shake":false})"));
    CHECK(!ccqt::parseHandshake("not json"));

    const auto health = ccqt::parseHealth(
        R"({"status":"Runnable","details":{"uptime":"01:41:13","version":"5.1.1","warnings":1,"errors":0},"system":{"name":"testhost"}})");
    CHECK(health.has_value());
    CHECK(health->valid);
    CHECK(health->status == QLatin1String("Runnable"));
    CHECK(health->version == QLatin1String("5.1.1"));
    CHECK(health->uptime == QLatin1String("01:41:13"));
    CHECK(health->systemName == QLatin1String("testhost"));
    CHECK(health->warnings == 1);
    CHECK(health->errors == 0);

    CHECK(!ccqt::parseHealth("{}").has_value());
}

void testDevicesAndSorting()
{
    const QByteArray payload = R"json({
      "devices": [
        {
          "name": "spd5118",
          "type": "Hwmon",
          "type_index": 5,
          "uid": "uid-spd-5",
          "info": {
            "channels": {},
            "temps": { "temp1": { "label": "Temp1", "number": 1 } },
            "temp_min": 0,
            "temp_max": 150,
            "model": null,
            "driver_info": { "drv_type": "Kernel", "name": "spd5118", "version": "7.3.5", "locations": [] }
          }
        },
        {
          "name": "spd5118",
          "d_type": "Hwmon",
          "type_index": 6,
          "uid": "uid-spd-6",
          "info": {
            "channels": {},
            "temps": { "temp1": { "label": "Temp1", "number": 1 } },
            "temp_min": 0,
            "temp_max": 150,
            "model": null,
            "driver_info": { "drv_type": "Kernel", "name": "spd5118", "version": "7.3.5" }
          }
        },
        {
          "name": "hp",
          "type": "Hwmon",
          "type_index": 2,
          "uid": "uid-hp",
          "info": {
            "channels": {
              "fan10": { "label": null, "speed_options": { "min_duty": 0, "max_duty": 100, "fixed_enabled": true, "extension": null } },
              "fan2": { "label": null, "speed_options": { "min_duty": 0, "max_duty": 100, "fixed_enabled": true, "extension": null } },
              "fan1": { "label": null, "speed_options": { "min_duty": 0, "max_duty": 100, "fixed_enabled": true, "extension": null } }
            },
            "temps": {},
            "temp_min": 0,
            "temp_max": 150,
            "model": null,
            "driver_info": { "drv_type": "Kernel", "name": "hp-wmi", "version": "7.3.5" }
          }
        },
        {
          "name": "nvme",
          "type": "Hwmon",
          "type_index": 4,
          "uid": "uid-nvme",
          "info": {
            "channels": {},
            "temps": {
              "temp10": { "label": "Sensor 10", "number": 10 },
              "temp2": { "label": "Sensor 2", "number": 2 },
              "temp1": { "label": "Composite", "number": 1 }
            },
            "temp_min": 0,
            "temp_max": 150,
            "model": "NVMe Test Drive",
            "driver_info": { "drv_type": "Kernel" }
          }
        },
        {
          "name": "Test GPU",
          "type": "GPU",
          "type_index": 1,
          "uid": "uid-gpu",
          "info": {
            "channels": {
              "GPU Load": { "label": "GPU Load", "speed_options": null },
              "GPU Power": { "label": "GPU Power", "speed_options": null }
            },
            "temps": { "GPU Temp": { "label": "GPU Temp", "number": 1 } },
            "temp_min": 0,
            "temp_max": 150,
            "driver_info": { "drv_type": "NVML", "name": "nvml", "version": "620.0" }
          }
        }
      ]
    })json";

    auto parsed = ccqt::parseDevices(payload);
    CHECK(parsed.has_value());
    CHECK(parsed->size() == 5);

    const QByteArray ovPayload = R"json({
      "devices": {
        "uid-hp": {
          "name": "Laptop Fans",
          "channels": {
            "fan1": { "label": "Left Fan" }
          }
        }
      }
    })json";
    const ccqt::NameOverrides overrides = ccqt::parseOverrides(ovPayload);
    CHECK(overrides.size() == 1);

    ccqt::sortDevicesForDisplay(*parsed, overrides);
    // Controllable device comes first, then GPU, then Hwmon.
    CHECK(parsed->at(0).uid == QLatin1String("uid-hp"));
    CHECK(parsed->at(1).uid == QLatin1String("uid-gpu"));

    // Natural sorting on fan channels: fan1, fan2, fan10
    const auto& hp = parsed->at(0);
    CHECK(hp.hasControllableChannels());
    CHECK(hp.channels.size() == 3);
    CHECK(hp.channels.at(0).name == QLatin1String("fan1"));
    CHECK(hp.channels.at(1).name == QLatin1String("fan2"));
    CHECK(hp.channels.at(2).name == QLatin1String("fan10"));
    CHECK(hp.temps.isEmpty());

    CHECK(ccqt::resolveDeviceDisplayName(hp, overrides) == QLatin1String("Laptop Fans"));
    CHECK(ccqt::resolveChannelDisplayName(hp.uid, QStringLiteral("fan1"), hp.channels.at(0).label, overrides)
          == QLatin1String("Left Fan"));
    CHECK(ccqt::resolveChannelDisplayName(hp.uid, QStringLiteral("fan2"), hp.channels.at(1).label, overrides)
          == QLatin1String("fan2"));

    // Duplicate detection for spd5118
    const QSet<QString> dups = ccqt::findDuplicateDeviceNames(*parsed, overrides);
    CHECK(dups.contains(QStringLiteral("spd5118")));
    CHECK(!dups.contains(QStringLiteral("NVMe Test Drive")));

    // Check d_type fallback on second spd5118 and numeric temp order on nvme
    for (const auto& d : *parsed) {
        if (d.uid == QLatin1String("uid-spd-6")) {
            CHECK(d.type == QLatin1String("Hwmon"));
            CHECK(ccqt::resolveDeviceDisplayName(d, overrides, true) == QLatin1String("spd5118 (#6)"));
        } else if (d.uid == QLatin1String("uid-nvme")) {
            CHECK(ccqt::resolveDeviceDisplayName(d, overrides) == QLatin1String("NVMe Test Drive"));
            CHECK(d.temps.size() == 3);
            CHECK(d.temps.at(0).name == QLatin1String("temp1"));
            CHECK(d.temps.at(1).name == QLatin1String("temp2"));
            CHECK(d.temps.at(2).name == QLatin1String("temp10"));
        } else if (d.uid == QLatin1String("uid-gpu")) {
            CHECK(!d.hasControllableChannels());
        }
    }
}

void testStatusParsing()
{
    const QByteArray payload = R"json({
      "devices": [
        {
          "type": "Hwmon",
          "type_index": 2,
          "uid": "uid-hp",
          "status_history": [
            {
              "timestamp": "2026-11-24T19:16:12+03:00",
              "channels": [{ "name": "fan1", "rpm": 1900, "duty": 25.0 }]
            },
            {
              "timestamp": "2026-11-24T19:16:13+03:00",
              "channels": [{ "name": "fan1", "rpm": 2200, "duty": 33.0 }]
            }
          ]
        },
        {
          "d_type": "GPU",
          "type_index": 1,
          "uid": "uid-gpu",
          "status_history": [
            {
              "timestamp": "2026-11-24T19:16:13+03:00",
              "temps": [{ "name": "GPU Temp", "temp": 76.5 }],
              "channels": [
                { "name": "GPU Load", "duty": 44.0 },
                { "name": "freq_graphics", "freq": 2385 },
                { "name": "GPU Power", "watts": 50.25 }
              ]
            }
          ]
        },
        {
          "type": "Hwmon",
          "type_index": 1,
          "uid": "uid-empty-history",
          "status_history": []
        }
      ]
    })json";

    const auto parsed = ccqt::parseStatus(payload);
    CHECK(parsed.has_value());
    CHECK(parsed->size() == 2); // uid-empty-history skipped safely

    const auto& hpSt = parsed->at(0);
    CHECK(hpSt.uid == QLatin1String("uid-hp"));
    CHECK(hpSt.timestamp == QLatin1String("2026-11-24T19:16:13+03:00"));
    CHECK(hpSt.temps.isEmpty()); // omitted temps array
    CHECK(hpSt.channels.size() == 1);
    CHECK(hpSt.channels.at(0).rpm == 2200u);
    CHECK(hpSt.channels.at(0).duty == 33.0);
    CHECK(!hpSt.channels.at(0).watts.has_value());

    const auto& gpuSt = parsed->at(1);
    CHECK(gpuSt.type == QLatin1String("GPU"));
    CHECK(gpuSt.temps.size() == 1);
    CHECK(gpuSt.temps.at(0).temp == 76.5);
    CHECK(gpuSt.channels.size() == 3);
    CHECK(gpuSt.channels.at(0).duty == 44.0);
    CHECK(!gpuSt.channels.at(0).rpm.has_value());
    CHECK(gpuSt.channels.at(1).freq == 2385u);
    CHECK(gpuSt.channels.at(2).watts == 50.25);
}

void testSettingsModesAndProfiles()
{
    const auto settings = ccqt::parseDeviceSettings(R"json({
      "settings": [
        { "channel_name": "fan1", "profile_uid": "p-graph" },
        { "channel_name": "fan2", "speed_fixed": 45 }
      ]
    })json");
    CHECK(settings.has_value());
    CHECK(settings->size() == 2);
    CHECK(settings->at(0).profileUid == QStringLiteral("p-graph"));
    CHECK(!settings->at(0).speedFixed.has_value());
    CHECK(settings->at(1).speedFixed == 45);
    CHECK(!settings->at(1).profileUid.has_value());

    const auto emptySettings = ccqt::parseDeviceSettings(R"({"settings":[]})");
    CHECK(emptySettings.has_value());
    CHECK(emptySettings->isEmpty());

    const auto emptyModes = ccqt::parseModes(R"({"modes":[]})");
    CHECK(emptyModes.has_value());
    CHECK(emptyModes->isEmpty());

    const auto modes = ccqt::parseModes(R"json({
      "modes": [
        {
          "uid": "mode-1",
          "name": "Silent",
          "device_settings": [
            ["uid-hp", [{ "channel_name": "fan1", "profile_uid": "p-graph" }]]
          ]
        }
      ]
    })json");
    CHECK(modes.has_value());
    CHECK(modes->size() == 1);
    CHECK(modes->at(0).name == QLatin1String("Silent"));
    CHECK(modes->at(0).deviceSettings.size() == 1);
    CHECK(modes->at(0).deviceSettings.at(0).deviceUid == QLatin1String("uid-hp"));
    CHECK(modes->at(0).deviceSettings.at(0).settings.at(0).profileUid == QStringLiteral("p-graph"));

    const auto activeNull = ccqt::parseActiveModes(R"({"current_mode_uid":null,"previous_mode_uid":null})");
    CHECK(activeNull.has_value());
    CHECK(activeNull->currentModeUid.isEmpty());
    CHECK(activeNull->previousModeUid.isEmpty());

    const auto activeSet = ccqt::parseActiveModes(R"({"current_mode_uid":"mode-1","previous_mode_uid":"mode-0"})");
    CHECK(activeSet.has_value());
    CHECK(activeSet->currentModeUid == QLatin1String("mode-1"));
    CHECK(activeSet->previousModeUid == QLatin1String("mode-0"));

    const auto profiles = ccqt::parseProfiles(R"json({
      "profiles": [
        { "uid": "0", "name": "Unmanaged", "function_uid": "0", "p_type": "Default" },
        { "uid": "p-fixed", "name": "Constant 50", "function_uid": "0", "p_type": "Fixed", "speed_fixed": 50 },
        {
          "uid": "p-graph",
          "name": "CPU Curve",
          "function_uid": "fn-1",
          "p_type": "Graph",
          "speed_profile": [[40.0, 20], [85.5, 100]],
          "temp_source": { "temp_name": "CPU Temp", "device_uid": "uid-cpu" },
          "temp_min": 35.0,
          "temp_max": 95.0
        },
        {
          "uid": "p-mix",
          "name": "Max Mix",
          "function_uid": "0",
          "p_type": "Mix",
          "member_profile_uids": ["p-fixed", "p-graph"],
          "mix_function_type": "Max"
        },
        {
          "uid": "p-overlay",
          "name": "Boost",
          "function_uid": "0",
          "p_type": "Overlay",
          "member_profile_uids": ["p-graph"],
          "offset_profile": [[50, 10]]
        }
      ]
    })json");
    CHECK(profiles.has_value());
    CHECK(profiles->size() == 5);
    CHECK(profiles->at(0).type == QLatin1String("Default"));
    CHECK(profiles->at(1).speedFixed == 50);
    CHECK(profiles->at(2).speedProfile.size() == 2);
    CHECK(profiles->at(2).tempSource.has_value());
    CHECK(profiles->at(2).tempSource->tempName == QLatin1String("CPU Temp"));
    CHECK(profiles->at(2).tempMin == 35.0);
    CHECK(profiles->at(2).tempMax == 95.0);
    CHECK(profiles->at(3).mixFunctionType == QLatin1String("Max"));
    CHECK(profiles->at(3).memberProfileUids.size() == 2);
    CHECK(profiles->at(4).type == QLatin1String("Overlay"));
}

void testInvalidInputs()
{
    CHECK(!ccqt::parseDevices("[]").has_value());
    CHECK(!ccqt::parseDevices("{broken").has_value());
    CHECK(!ccqt::parseStatus("null").has_value());
    CHECK(!ccqt::parseDeviceSettings("{}").has_value());
    CHECK(!ccqt::parseModes("{\"modes\":123}").has_value());
    CHECK(!ccqt::parseActiveModes("{}").has_value());
    CHECK(!ccqt::parseProfiles("{\"profiles\":null}").has_value());
    CHECK(ccqt::parseOverrides("invalid").isEmpty());
}

void testUrlAndTlsHelpers()
{
    QString norm;
    CHECK(ccqt::CoolerControlClient::isValidBaseUrl(QStringLiteral("http://localhost:11987/"), &norm));
    CHECK(norm == QLatin1String("http://localhost:11987"));
    CHECK(ccqt::CoolerControlClient::isValidBaseUrl(QStringLiteral("https://127.0.0.1:11987///"), &norm));
    CHECK(norm == QLatin1String("https://127.0.0.1:11987"));
    CHECK(!ccqt::CoolerControlClient::isValidBaseUrl(QStringLiteral("ftp://localhost:11987")));
    CHECK(!ccqt::CoolerControlClient::isValidBaseUrl(QStringLiteral("http://")));

    CHECK(ccqt::CoolerControlClient::isLocalHost(QStringLiteral("localhost")));
    CHECK(ccqt::CoolerControlClient::isLocalHost(QStringLiteral("LOCALHOST")));
    CHECK(ccqt::CoolerControlClient::isLocalHost(QStringLiteral("127.0.0.1")));
    CHECK(ccqt::CoolerControlClient::isLocalHost(QStringLiteral("::1")));
    CHECK(ccqt::CoolerControlClient::isLocalHost(QStringLiteral("[::1]")));
    CHECK(!ccqt::CoolerControlClient::isLocalHost(QStringLiteral("192.168.1.50")));
    CHECK(!ccqt::CoolerControlClient::isLocalHost(QStringLiteral("example.com")));
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    testHandshakeAndHealth();
    testDevicesAndSorting();
    testStatusParsing();
    testSettingsModesAndProfiles();
    testInvalidInputs();
    testUrlAndTlsHelpers();

    if (g_failures != 0) {
        std::cerr << g_failures << " assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All ParserTests passed.\n";
    return 0;
}
