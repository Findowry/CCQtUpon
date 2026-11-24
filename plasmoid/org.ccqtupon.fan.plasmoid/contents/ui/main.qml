import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

PlasmoidItem {
    id: root

    Plasmoid.backgroundHints: PlasmaCore.Types.DefaultBackground | PlasmaCore.Types.ConfigurableBackground

    // Dynamic state populated from CoolerControl v5.1+ REST API
    property var fanChannels: []
    property var profilesList: []
    property var overridesMap: ({})
    property var statusByKey: ({})
    property var tempsByDevice: ({})
    property var settingsByDevice: ({})

    property int telemetryTick: 0
    property int selectedModeTab: 0 // 0: Profile, 1: Manual, 2: Unmanaged

    // Draft UI choices keyed by fanKey so 1s status polls never reset user edits
    property var draftProfileByKey: ({})
    property var draftManualByKey: ({})
    property var pendingByKey: ({})

    property string connState: "disconnected" // no_token, connecting, connected, auth_error, offline, rate_limited
    property string errorBannerText: ""
    property int errorBannerType: Kirigami.MessageType.Error
    property string writeNoticeText: ""
    property int writeNoticeType: Kirigami.MessageType.Information

    property int apiGen: 0
    property bool statusInFlight: false
    property bool handshakeInFlight: false
    property bool authFailedForToken: false

    readonly property string daemonUrl: normalizeUrl(Plasmoid.configuration.daemonUrl)
    readonly property string accessToken: (Plasmoid.configuration.accessToken || "").trim()
    readonly property int pollMs: Math.max(500, Math.min(10000, Plasmoid.configuration.pollIntervalMs || 1000))
    readonly property int panelDisplayMode: Plasmoid.configuration.panelDisplayMode || 0

    onDaemonUrlChanged: restartConnection()
    onAccessTokenChanged: {
        authFailedForToken = false
        restartConnection()
    }

    Component.onCompleted: restartConnection()

    Plasmoid.status: {
        if (connState === "auth_error" || connState === "offline") {
            return PlasmaCore.Types.ActiveStatus
        }
        return PlasmaCore.Types.PassiveStatus
    }

    toolTipMainText: {
        if (connState === "connected") {
            return i18n("CCQt Upon — Connected")
        }
        if (connState === "auth_error") {
            return i18n("CCQt Upon — Authentication Required (401)")
        }
        if (connState === "no_token") {
            return i18n("CCQt Upon — Token Not Configured")
        }
        if (connState === "offline") {
            return i18n("CCQt Upon — Offline")
        }
        return i18n("CCQt Upon")
    }

    toolTipSubText: {
        const tick = root.telemetryTick
        if (connState !== "connected" && errorBannerText.length > 0) {
            return errorBannerText
        }
        if (fanChannels.length === 0) {
            return i18n("No controllable fan channels detected.")
        }
        let lines = []
        for (let i = 0; i < fanChannels.length; ++i) {
            const fan = fanChannels[i]
            const st = statusByKey[fan.key]
            const dutyTxt = (st && st.duty !== null && st.duty !== undefined)
                ? (Math.round(st.duty) + "%") : "—"
            const rpmTxt = (st && st.rpm !== null && st.rpm !== undefined)
                ? (st.rpm + " RPM") : "—"
            lines.push(fan.displayName + ": " + dutyTxt + " · " + rpmTxt)
        }
        return lines.join("\n") + (tick >= 0 ? "" : "")
    }

    Timer {
        id: pollTimer
        interval: root.pollMs
        repeat: true
        running: root.connState === "connected"
        onTriggered: root.fetchStatus()
    }

    Timer {
        id: reconnectTimer
        interval: 2000
        repeat: true
        running: root.connState === "offline" || root.connState === "auth_error"
        onTriggered: root.probeHandshake()
    }

    Timer {
        id: rateLimitTimer
        interval: 10000
        repeat: false
        onTriggered: {
            if (root.connState === "rate_limited" && !root.authFailedForToken) {
                root.restartConnection()
            }
        }
    }

    function normalizeUrl(raw) {
        let u = (raw || "http://localhost:11987").trim()
        while (u.endsWith("/")) {
            u = u.slice(0, -1)
        }
        return u
    }

    function fanKey(deviceUid, channelName) {
        return deviceUid + "\n" + channelName
    }

    function sendRequest(method, path, bodyObj, withAuth, callback) {
        const gen = root.apiGen
        const xhr = new XMLHttpRequest()
        xhr.timeout = 3500
        xhr.open(method, root.daemonUrl + path)
        xhr.setRequestHeader("Accept", "application/json")
        if (withAuth && root.accessToken.length > 0) {
            xhr.setRequestHeader("Authorization", "Bearer " + root.accessToken)
        }
        if (bodyObj !== null && bodyObj !== undefined) {
            xhr.setRequestHeader("Content-Type", "application/json")
        }
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE) {
                return
            }
            if (gen !== root.apiGen) {
                return
            }
            callback(xhr.status, xhr.responseText || "", xhr)
        }
        xhr.ontimeout = function() {
            if (gen !== root.apiGen) {
                return
            }
            callback(0, "", xhr)
        }
        xhr.onerror = function() {
            if (gen !== root.apiGen) {
                return
            }
            callback(0, "", xhr)
        }
        if (bodyObj !== null && bodyObj !== undefined) {
            xhr.send(JSON.stringify(bodyObj))
        } else {
            xhr.send()
        }
    }

    function restartConnection() {
        apiGen += 1
        statusInFlight = false
        handshakeInFlight = false
        rateLimitTimer.stop()
        writeNoticeText = ""

        if (!daemonUrl.startsWith("http://") && !daemonUrl.startsWith("https://")) {
            connState = "offline"
            errorBannerType = Kirigami.MessageType.Error
            errorBannerText = i18n("Invalid daemon URL. Configure http:// or https://.")
            return
        }

        if (accessToken.length === 0) {
            connState = "no_token"
            errorBannerType = Kirigami.MessageType.Information
            errorBannerText = i18n("Access Token not configured. Open widget settings to enter your cc_... token.")
            return
        }

        connState = "connecting"
        errorBannerText = ""
        fetchHealthAndSync()
    }

    function fetchHealthAndSync() {
        sendRequest("GET", "/health", null, true, function(status, text, xhr) {
            if (status === 401) {
                handleAuthFailure()
                return
            }
            if (status === 429) {
                handleRateLimit(xhr)
                return
            }
            if (status === 0 || status >= 400) {
                handleOffline()
                return
            }

            authFailedForToken = false
            connState = "connected"
            errorBannerText = ""
            refreshAllMetadata()
        })
    }

    function refreshAllMetadata() {
        if (connState !== "connected") {
            return
        }
        fetchOverrides()
        fetchProfiles()
        fetchDevices()
        fetchStatus()
    }

    function fetchOverrides() {
        sendRequest("GET", "/settings/overrides", null, true, function(status, text) {
            if (status === 200) {
                try {
                    const obj = JSON.parse(text)
                    if (obj && obj.devices) {
                        overridesMap = obj.devices
                        rebuildFanChannelsFromCache()
                    }
                } catch (e) {
                }
            }
        })
    }

    property var cachedDevices: []

    function fetchProfiles() {
        sendRequest("GET", "/profiles", null, true, function(status, text, xhr) {
            if (status === 401) { handleAuthFailure(); return }
            if (status === 429) { handleRateLimit(xhr); return }
            if (status === 0) { handleOffline(); return }
            if (status === 200) {
                try {
                    const obj = JSON.parse(text)
                    if (obj && Array.isArray(obj.profiles)) {
                        profilesList = obj.profiles
                    }
                } catch (e) {
                }
            }
        })
    }

    function fetchDevices() {
        sendRequest("GET", "/devices", null, true, function(status, text, xhr) {
            if (status === 401) { handleAuthFailure(); return }
            if (status === 429) { handleRateLimit(xhr); return }
            if (status === 0) { handleOffline(); return }
            if (status === 200) {
                try {
                    const obj = JSON.parse(text)
                    if (obj && Array.isArray(obj.devices)) {
                        cachedDevices = obj.devices
                        rebuildFanChannelsFromCache()
                        for (let i = 0; i < cachedDevices.length; ++i) {
                            const dev = cachedDevices[i]
                            if (deviceHasControllableFans(dev)) {
                                fetchDeviceSettings(dev.uid)
                            }
                        }
                    }
                } catch (e) {
                }
            }
        })
    }

    function fetchDeviceSettings(deviceUid) {
        const path = "/devices/" + encodeURIComponent(deviceUid) + "/settings"
        sendRequest("GET", path, null, true, function(status, text) {
            if (status === 200) {
                try {
                    const obj = JSON.parse(text)
                    if (obj && Array.isArray(obj.settings)) {
                        const copy = Object.assign({}, settingsByDevice)
                        copy[deviceUid] = obj.settings
                        settingsByDevice = copy
                        syncDraftsFromSettings()
                    }
                } catch (e) {
                }
            }
        })
    }

    function fetchStatus() {
        if (statusInFlight || connState !== "connected") {
            return
        }
        statusInFlight = true
        sendRequest("GET", "/status", null, true, function(status, text, xhr) {
            statusInFlight = false
            if (status === 401) { handleAuthFailure(); return }
            if (status === 429) { handleRateLimit(xhr); return }
            if (status === 0 || status >= 400) { handleOffline(); return }

            try {
                const obj = JSON.parse(text)
                if (!obj || !Array.isArray(obj.devices)) {
                    return
                }
                const nextStatus = Object.assign({}, statusByKey)
                const nextTemps = Object.assign({}, tempsByDevice)

                for (let i = 0; i < obj.devices.length; ++i) {
                    const d = obj.devices[i]
                    if (!d || !d.uid || !Array.isArray(d.status_history) || d.status_history.length === 0) {
                        continue
                    }
                    const latest = d.status_history[d.status_history.length - 1]
                    if (latest && Array.isArray(latest.channels)) {
                        for (let j = 0; j < latest.channels.length; ++j) {
                            const ch = latest.channels[j]
                            if (ch && ch.name) {
                                nextStatus[fanKey(d.uid, ch.name)] = {
                                    duty: (ch.duty !== undefined ? ch.duty : null),
                                    rpm: (ch.rpm !== undefined ? ch.rpm : null)
                                }
                            }
                        }
                    }
                    if (latest && Array.isArray(latest.temps)) {
                        const tMap = {}
                        for (let j = 0; j < latest.temps.length; ++j) {
                            const t = latest.temps[j]
                            if (t && t.name && t.temp !== undefined) {
                                tMap[t.name] = t.temp
                            }
                        }
                        nextTemps[d.uid] = tMap
                    }
                }

                statusByKey = nextStatus
                tempsByDevice = nextTemps
                telemetryTick += 1
            } catch (e) {
            }
        })
    }

    function probeHandshake() {
        if (handshakeInFlight) {
            return
        }
        handshakeInFlight = true
        sendRequest("GET", "/handshake", null, false, function(status, text) {
            handshakeInFlight = false
            if (status === 200) {
                let ok = false
                try {
                    const obj = JSON.parse(text)
                    ok = !!(obj && obj.shake)
                } catch (e) {
                    ok = false
                }
                if (ok) {
                    if (authFailedForToken) {
                        if (connState !== "auth_error") {
                            connState = "auth_error"
                            errorBannerType = Kirigami.MessageType.Error
                            errorBannerText = i18n("Authentication failed (HTTP 401). Check your Access Token in widget settings.")
                        }
                        return
                    }
                    connState = "connecting"
                    errorBannerText = ""
                    fetchHealthAndSync()
                    return
                }
            }
            if (connState !== "offline") {
                connState = "offline"
                errorBannerType = Kirigami.MessageType.Warning
                errorBannerText = i18n("coolercontrold is unreachable at %1. Retrying...", root.daemonUrl)
            }
        })
    }

    function handleAuthFailure() {
        authFailedForToken = true
        connState = "auth_error"
        errorBannerType = Kirigami.MessageType.Error
        errorBannerText = i18n("Authentication failed (HTTP 401). Check your Access Token in widget settings.")
    }

    function handleOffline() {
        connState = "offline"
        errorBannerType = Kirigami.MessageType.Warning
        errorBannerText = i18n("coolercontrold is unreachable at %1. Retrying...", root.daemonUrl)
    }

    function handleRateLimit(xhr) {
        let retrySec = 10
        if (xhr && xhr.getResponseHeader) {
            const raw = parseInt(xhr.getResponseHeader("Retry-After"), 10)
            if (!isNaN(raw) && raw > 0) {
                retrySec = Math.min(300, raw)
            }
        }
        connState = "rate_limited"
        errorBannerType = Kirigami.MessageType.Warning
        errorBannerText = i18n("Rate limited (HTTP 429). Retrying in %1s.", retrySec)
        rateLimitTimer.interval = retrySec * 1000
        rateLimitTimer.restart()
    }

    function deviceHasControllableFans(dev) {
        if (!dev || !dev.info || !dev.info.channels) {
            return false
        }
        const chMap = dev.info.channels
        const names = Object.keys(chMap)
        for (let i = 0; i < names.length; ++i) {
            const ch = chMap[names[i]]
            if (ch && ch.speed_options && ch.speed_options.fixed_enabled === true) {
                return true
            }
        }
        return false
    }

    function resolveDeviceLabel(dev) {
        const ov = overridesMap[dev.uid]
        if (ov && ov.name && ov.name.trim().length > 0) {
            return ov.name.trim()
        }
        if (dev.info && dev.info.model && dev.info.model.trim().length > 0) {
            return dev.info.model.trim()
        }
        return dev.name || dev.uid
    }

    function resolveChannelLabel(devUid, chName, infoLabel) {
        const ov = overridesMap[devUid]
        if (ov && ov.channels && ov.channels[chName] && ov.channels[chName].label) {
            const lbl = ov.channels[chName].label.trim()
            if (lbl.length > 0) {
                return lbl
            }
        }
        if (infoLabel && infoLabel.trim().length > 0) {
            return infoLabel.trim()
        }
        return chName
    }

    function naturalCompare(a, b) {
        return a.localeCompare(b, undefined, { numeric: true, sensitivity: "base" })
    }

    function rebuildFanChannelsFromCache() {
        let list = []
        for (let i = 0; i < cachedDevices.length; ++i) {
            const dev = cachedDevices[i]
            if (!dev || !dev.info || !dev.info.channels) {
                continue
            }
            const devLabel = resolveDeviceLabel(dev)
            const chNames = Object.keys(dev.info.channels).sort(naturalCompare)
            for (let j = 0; j < chNames.length; ++j) {
                const chName = chNames[j]
                const chInfo = dev.info.channels[chName]
                if (!chInfo || !chInfo.speed_options || chInfo.speed_options.fixed_enabled !== true) {
                    continue
                }
                const chLabel = resolveChannelLabel(dev.uid, chName, chInfo.label)
                const key = fanKey(dev.uid, chName)
                list.push({
                    key: key,
                    deviceUid: dev.uid,
                    deviceName: devLabel,
                    channelName: chName,
                    channelLabel: chLabel,
                    displayName: devLabel + " · " + chLabel,
                    minDuty: chInfo.speed_options.min_duty !== undefined ? chInfo.speed_options.min_duty : 0,
                    maxDuty: chInfo.speed_options.max_duty !== undefined ? chInfo.speed_options.max_duty : 100
                })
            }
        }
        fanChannels = list
        syncDraftsFromSettings()
    }

    function syncDraftsFromSettings() {
        const nextProf = Object.assign({}, draftProfileByKey)
        const nextMan = Object.assign({}, draftManualByKey)

        for (let i = 0; i < fanChannels.length; ++i) {
            const fan = fanChannels[i]
            const settings = settingsByDevice[fan.deviceUid] || []
            let matched = null
            for (let j = 0; j < settings.length; ++j) {
                if (settings[j].channel_name === fan.channelName) {
                    matched = settings[j]
                    break
                }
            }
            if (nextProf[fan.key] === undefined) {
                nextProf[fan.key] = (matched && matched.profile_uid) ? matched.profile_uid : "0"
            }
            if (nextMan[fan.key] === undefined) {
                if (matched && matched.speed_fixed !== undefined && matched.speed_fixed !== null) {
                    nextMan[fan.key] = matched.speed_fixed
                } else {
                    const st = statusByKey[fan.key]
                    nextMan[fan.key] = (st && st.duty !== null && st.duty !== undefined)
                        ? Math.round(st.duty)
                        : Math.max(fan.minDuty, Math.min(fan.maxDuty, 50))
                }
            }
        }

        draftProfileByKey = nextProf
        draftManualByKey = nextMan
        telemetryTick += 1
    }

    function findProfile(uid) {
        for (let i = 0; i < profilesList.length; ++i) {
            if (profilesList[i].uid === uid) {
                return profilesList[i]
            }
        }
        return null
    }

    function liveTempForProfile(prof) {
        if (!prof || !prof.temp_source) {
            return null
        }
        const devTemps = tempsByDevice[prof.temp_source.device_uid]
        if (!devTemps) {
            return null
        }
        const val = devTemps[prof.temp_source.temp_name]
        return val !== undefined ? val : null
    }

    function setKeyPending(key, pending) {
        const next = Object.assign({}, pendingByKey)
        next[key] = pending
        pendingByKey = next
    }

    function handleWriteResponse(key, devUid, status, text, xhr, successMsg) {
        setKeyPending(key, false)
        if (status === 401) { handleAuthFailure(); return }
        if (status === 429) { handleRateLimit(xhr); return }
        if (status === 0) { handleOffline(); return }

        if (status === 403) {
            writeNoticeType = Kirigami.MessageType.Warning
            writeNoticeText = i18n("Write access denied (HTTP 403). Your Access Token is read-only; enable Write Access in CoolerControl.")
            return
        }
        if (status >= 400) {
            let msg = "HTTP " + status
            try {
                const errObj = JSON.parse(text)
                if (errObj && errObj.error) {
                    msg = errObj.error
                }
            } catch (e) {
            }
            writeNoticeType = Kirigami.MessageType.Error
            writeNoticeText = i18n("Failed to apply change: %1", msg)
            return
        }

        writeNoticeType = Kirigami.MessageType.Positive
        writeNoticeText = successMsg
        fetchDeviceSettings(devUid)
        fetchStatus()
    }

    function applyProfile(devUid, chName, profileUid, displayName) {
        if (!profileUid) {
            return
        }
        const key = fanKey(devUid, chName)
        setKeyPending(key, true)
        writeNoticeText = ""
        const path = "/devices/" + encodeURIComponent(devUid)
                   + "/settings/" + encodeURIComponent(chName) + "/profile"
        sendRequest("PUT", path, { profile_uid: profileUid }, true, function(status, text, xhr) {
            handleWriteResponse(key, devUid, status, text, xhr,
                                i18n("Profile applied to %1", displayName))
        })
    }

    function applyManual(devUid, chName, duty, displayName) {
        const key = fanKey(devUid, chName)
        const clamped = Math.max(0, Math.min(100, Math.round(duty)))
        setKeyPending(key, true)
        writeNoticeText = ""
        const path = "/devices/" + encodeURIComponent(devUid)
                   + "/settings/" + encodeURIComponent(chName) + "/manual"
        sendRequest("PUT", path, { speed_fixed: clamped }, true, function(status, text, xhr) {
            handleWriteResponse(key, devUid, status, text, xhr,
                                i18n("Manual %1% applied to %2", clamped, displayName))
        })
    }

    function applyReset(devUid, chName, displayName) {
        const key = fanKey(devUid, chName)
        setKeyPending(key, true)
        writeNoticeText = ""
        const path = "/devices/" + encodeURIComponent(devUid)
                   + "/settings/" + encodeURIComponent(chName) + "/reset"
        sendRequest("PUT", path, null, true, function(status, text, xhr) {
            handleWriteResponse(key, devUid, status, text, xhr,
                                i18n("Reset %1 to hardware default", displayName))
        })
    }

    function resetAllFans() {
        for (let i = 0; i < fanChannels.length; ++i) {
            const fan = fanChannels[i]
            applyReset(fan.deviceUid, fan.channelName, fan.displayName)
        }
    }

    compactRepresentation: MouseArea {
        id: compactRoot

        readonly property bool isVertical: Plasmoid.formFactor === PlasmaCore.Types.Vertical
        readonly property bool showingIcon: root.panelDisplayMode === 0 || root.fanChannels.length === 0

        Layout.minimumWidth: showingIcon
            ? Kirigami.Units.iconSizes.small
            : (isVertical ? Kirigami.Units.gridUnit * 2.5 : compactGrid.implicitWidth + Kirigami.Units.smallSpacing * 2)
        Layout.minimumHeight: showingIcon
            ? Kirigami.Units.iconSizes.small
            : (isVertical ? compactGrid.implicitHeight + Kirigami.Units.smallSpacing : Kirigami.Units.iconSizes.small)
        Layout.preferredWidth: showingIcon
            ? Kirigami.Units.iconSizes.medium
            : compactGrid.implicitWidth + Kirigami.Units.smallSpacing * 2
        Layout.preferredHeight: showingIcon
            ? Kirigami.Units.iconSizes.medium
            : compactGrid.implicitHeight + Kirigami.Units.smallSpacing

        acceptedButtons: Qt.LeftButton
        onClicked: root.expanded = !root.expanded

        // Mode 0: Icon Only (sensors-fan -> speedometer -> preferences-system-performance -> Canvas fallback)
        Item {
            id: iconContainer
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height, Kirigami.Units.iconSizes.large)
            height: width
            visible: compactRoot.showingIcon

            property int candidateIndex: 0
            readonly property var candidates: ["sensors-fan", "speedometer", "preferences-system-performance"]

            Kirigami.Icon {
                id: themeIcon
                anchors.fill: parent
                source: iconContainer.candidateIndex < iconContainer.candidates.length
                    ? iconContainer.candidates[iconContainer.candidateIndex]
                    : ""
                visible: valid && iconContainer.candidateIndex < iconContainer.candidates.length
                onValidChanged: {
                    if (!valid && iconContainer.candidateIndex < iconContainer.candidates.length) {
                        iconContainer.candidateIndex += 1
                    }
                }
                Component.onCompleted: {
                    if (!valid && iconContainer.candidateIndex < iconContainer.candidates.length) {
                        iconContainer.candidateIndex += 1
                    }
                }
            }

            Canvas {
                anchors.fill: parent
                visible: !themeIcon.visible
                onVisibleChanged: if (visible) requestPaint()
                onWidthChanged: if (visible) requestPaint()
                onHeightChanged: if (visible) requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.clearRect(0, 0, width, height)
                    const cx = width * 0.5
                    const cy = height * 0.5
                    const r = Math.min(width, height) * 0.36
                    const col = Kirigami.Theme.textColor
                    ctx.strokeStyle = col
                    ctx.lineWidth = Math.max(1.8, width * 0.09)
                    ctx.lineCap = "round"
                    ctx.beginPath()
                    ctx.arc(cx, cy, r, Math.PI * 0.78, Math.PI * 2.22, false)
                    ctx.stroke()
                    ctx.beginPath()
                    ctx.moveTo(cx, cy)
                    ctx.lineTo(cx + r * 0.6, cy - r * 0.6)
                    ctx.stroke()
                }
            }
        }

        // Mode 1 (RPM) & Mode 2 (Duty %)
        GridLayout {
            id: compactGrid
            anchors.centerIn: parent
            visible: root.panelDisplayMode !== 0 && root.fanChannels.length > 0
            columns: compactRoot.isVertical ? 1 : Math.max(1, root.fanChannels.length)
            rows: compactRoot.isVertical ? Math.max(1, root.fanChannels.length) : 1
            columnSpacing: Kirigami.Units.smallSpacing * 1.5
            rowSpacing: 1

            Repeater {
                model: root.fanChannels
                delegate: PlasmaComponents3.Label {
                    required property var modelData
                    Layout.alignment: Qt.AlignCenter
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pointSize: compactRoot.isVertical
                        ? Math.max(7, Kirigami.Theme.smallFont.pointSize)
                        : Kirigami.Theme.defaultFont.pointSize
                    font.weight: Font.Medium
                    text: {
                        const _tick = root.telemetryTick
                        const st = root.statusByKey[modelData.key]
                        if (root.panelDisplayMode === 1) {
                            if (st && st.rpm !== null && st.rpm !== undefined) {
                                return compactRoot.isVertical ? String(st.rpm) : (st.rpm + " RPM")
                            }
                            return "—"
                        }
                        if (st && st.duty !== null && st.duty !== undefined) {
                            return Math.round(st.duty) + "%"
                        }
                        return "—"
                    }
                }
            }
        }
    }

    fullRepresentation: Item {
        id: fullRoot

        Layout.preferredWidth: Kirigami.Units.gridUnit * 23
        Layout.preferredHeight: Kirigami.Units.gridUnit * 25
        Layout.minimumWidth: Kirigami.Units.gridUnit * 19
        Layout.minimumHeight: Kirigami.Units.gridUnit * 17

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            // Top bar: Title + status + Refresh + Settings
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    level: 4
                    text: i18n("CCQt Upon")
                    Layout.fillWidth: true
                }

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: root.connState === "connected"
                        ? Kirigami.Theme.positiveTextColor
                        : (root.connState === "connecting" || root.connState === "rate_limited"
                            ? Kirigami.Theme.neutralTextColor
                            : Kirigami.Theme.negativeTextColor)
                }

                PlasmaComponents3.Label {
                    font: Kirigami.Theme.smallFont
                    opacity: 0.8
                    text: {
                        if (root.connState === "connected") return i18n("Connected")
                        if (root.connState === "auth_error") return i18n("Auth Error")
                        if (root.connState === "no_token") return i18n("No Token")
                        if (root.connState === "offline") return i18n("Offline")
                        if (root.connState === "rate_limited") return i18n("Rate Limited")
                        return i18n("Connecting...")
                    }
                }

                PlasmaComponents3.ToolButton {
                    icon.name: "view-refresh"
                    PlasmaComponents3.ToolTip.text: i18n("Refresh")
                    PlasmaComponents3.ToolTip.visible: hovered
                    onClicked: {
                        if (root.connState === "connected") {
                            root.refreshAllMetadata()
                        } else {
                            root.restartConnection()
                        }
                    }
                }

                PlasmaComponents3.ToolButton {
                    icon.name: "configure"
                    PlasmaComponents3.ToolTip.text: i18n("Configure")
                    PlasmaComponents3.ToolTip.visible: hovered
                    onClicked: {
                        const act = Plasmoid.internalAction("configure")
                        if (act) {
                            act.trigger()
                        }
                    }
                }
            }

            // Connection / Auth error inline banner
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.errorBannerText.length > 0
                type: root.errorBannerType
                text: root.errorBannerText
                actions: [
                    Kirigami.Action {
                        text: i18n("Configure")
                        icon.name: "configure"
                        onTriggered: {
                            const act = Plasmoid.internalAction("configure")
                            if (act) act.trigger()
                        }
                    }
                ]
            }

            // Write result notice banner (e.g. 403 read-only or applied confirmation)
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.writeNoticeText.length > 0
                type: root.writeNoticeType
                text: root.writeNoticeText
                showCloseButton: true
                onVisibleChanged: {
                    if (!visible) {
                        root.writeNoticeText = ""
                    }
                }
            }

            // Mode selector tabs: Profile | Manual | Unmanaged
            PlasmaComponents3.TabBar {
                id: modeTabBar
                Layout.fillWidth: true
                currentIndex: root.selectedModeTab
                onCurrentIndexChanged: root.selectedModeTab = currentIndex

                PlasmaComponents3.TabButton {
                    text: i18n("Profile")
                }
                PlasmaComponents3.TabButton {
                    text: i18n("Manual")
                }
                PlasmaComponents3.TabButton {
                    text: i18n("Unmanaged")
                }
            }

            // Empty placeholder when no controllable fans
            Kirigami.PlaceholderMessage {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.fanChannels.length === 0
                iconName: "speedometer"
                text: root.connState === "connected"
                    ? i18n("No controllable fan channels detected.")
                    : i18n("Waiting for CoolerControl daemon...")
            }

            // Scrollable fan cards for the selected mode
            PlasmaComponents3.ScrollView {
                id: cardsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.fanChannels.length > 0
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: cardsScroll.availableWidth
                    spacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: root.fanChannels
                        delegate: PlasmaComponents3.Frame {
                            id: fanCard
                            required property var modelData
                            Layout.fillWidth: true

                            readonly property var liveStatus: {
                                const _t = root.telemetryTick
                                return root.statusByKey[modelData.key] || null
                            }
                            readonly property var liveDuty: liveStatus ? liveStatus.duty : null
                            readonly property var liveRpm: liveStatus ? liveStatus.rpm : null
                            readonly property bool isPending: !!root.pendingByKey[modelData.key]

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: Kirigami.Units.smallSpacing

                                // Fan card header
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    PlasmaComponents3.Label {
                                        Layout.fillWidth: true
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        text: fanCard.modelData.displayName
                                    }

                                    PlasmaComponents3.Label {
                                        font.family: "monospace"
                                        text: {
                                            const d = (fanCard.liveDuty !== null && fanCard.liveDuty !== undefined)
                                                ? (Math.round(fanCard.liveDuty) + "%") : "—"
                                            const r = (fanCard.liveRpm !== null && fanCard.liveRpm !== undefined)
                                                ? (fanCard.liveRpm + " RPM") : "—"
                                            return d + " · " + r
                                        }
                                    }
                                }

                                // --- Mode 0: Profile ---
                                ColumnLayout {
                                    id: profileSection
                                    Layout.fillWidth: true
                                    visible: root.selectedModeTab === 0
                                    spacing: Kirigami.Units.smallSpacing

                                    readonly property string chosenProfileUid: {
                                        const _t = root.telemetryTick
                                        const d = root.draftProfileByKey[fanCard.modelData.key]
                                        return d !== undefined ? d : "0"
                                    }
                                    readonly property var chosenProfile: root.findProfile(chosenProfileUid)

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Kirigami.Units.smallSpacing

                                        PlasmaComponents3.ComboBox {
                                            id: profCombo
                                            Layout.fillWidth: true
                                            model: root.profilesList
                                            textRole: "name"
                                            valueRole: "uid"
                                            currentIndex: {
                                                for (let i = 0; i < root.profilesList.length; ++i) {
                                                    if (root.profilesList[i].uid === profileSection.chosenProfileUid) {
                                                        return i
                                                    }
                                                }
                                                return root.profilesList.length > 0 ? 0 : -1
                                            }
                                            onActivated: function(idx) {
                                                if (idx >= 0 && idx < root.profilesList.length) {
                                                    const next = Object.assign({}, root.draftProfileByKey)
                                                    next[fanCard.modelData.key] = root.profilesList[idx].uid
                                                    root.draftProfileByKey = next
                                                    root.telemetryTick += 1
                                                }
                                            }
                                        }

                                        PlasmaComponents3.Button {
                                            text: i18n("Apply")
                                            enabled: !fanCard.isPending && root.connState === "connected"
                                            onClicked: root.applyProfile(
                                                fanCard.modelData.deviceUid,
                                                fanCard.modelData.channelName,
                                                profileSection.chosenProfileUid,
                                                fanCard.modelData.displayName)
                                        }
                                    }

                                    MiniCurveCanvas {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: Kirigami.Units.gridUnit * 4.2
                                        profile: profileSection.chosenProfile
                                        liveTemp: root.liveTempForProfile(profileSection.chosenProfile)
                                        liveDuty: fanCard.liveDuty
                                        minDuty: fanCard.modelData.minDuty
                                        maxDuty: fanCard.modelData.maxDuty
                                    }
                                }

                                // --- Mode 1: Manual ---
                                ColumnLayout {
                                    id: manualSection
                                    Layout.fillWidth: true
                                    visible: root.selectedModeTab === 1
                                    spacing: Kirigami.Units.smallSpacing

                                    readonly property int targetDuty: {
                                        const _t = root.telemetryTick
                                        const v = root.draftManualByKey[fanCard.modelData.key]
                                        return v !== undefined ? Math.round(v) : 50
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Kirigami.Units.smallSpacing

                                        PlasmaComponents3.Label {
                                            Layout.fillWidth: true
                                            font: Kirigami.Theme.smallFont
                                            text: i18n("Target: %1%", manualSection.targetDuty)
                                        }

                                        PlasmaComponents3.Button {
                                            text: i18n("Apply")
                                            enabled: !fanCard.isPending && root.connState === "connected"
                                            onClicked: root.applyManual(
                                                fanCard.modelData.deviceUid,
                                                fanCard.modelData.channelName,
                                                manualSection.targetDuty,
                                                fanCard.modelData.displayName)
                                        }
                                    }

                                    PlasmaComponents3.Slider {
                                        Layout.fillWidth: true
                                        from: fanCard.modelData.minDuty
                                        to: fanCard.modelData.maxDuty
                                        stepSize: 1
                                        snapMode: PlasmaComponents3.Slider.SnapAlways
                                        value: manualSection.targetDuty
                                        onMoved: {
                                            const next = Object.assign({}, root.draftManualByKey)
                                            next[fanCard.modelData.key] = Math.round(value)
                                            root.draftManualByKey = next
                                            root.telemetryTick += 1
                                        }
                                    }
                                }

                                // --- Mode 2: Unmanaged ---
                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: root.selectedModeTab === 2
                                    spacing: Kirigami.Units.smallSpacing

                                    PlasmaComponents3.Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        font: Kirigami.Theme.smallFont
                                        opacity: 0.8
                                        text: i18n("Reset channel to hardware / BIOS default")
                                    }

                                    PlasmaComponents3.Button {
                                        text: i18n("Reset / Apply")
                                        enabled: !fanCard.isPending && root.connState === "connected"
                                        onClicked: root.applyReset(
                                            fanCard.modelData.deviceUid,
                                            fanCard.modelData.channelName,
                                            fanCard.modelData.displayName)
                                    }
                                }
                            }
                        }
                    }

                    // Reset All convenience button in Unmanaged tab when >1 fan
                    PlasmaComponents3.Button {
                        Layout.fillWidth: true
                        visible: root.selectedModeTab === 2 && root.fanChannels.length > 1
                        text: i18n("Reset All Fans to Hardware Default")
                        enabled: root.connState === "connected"
                        onClicked: root.resetAllFans()
                    }
                }
            }
        }
    }
}
