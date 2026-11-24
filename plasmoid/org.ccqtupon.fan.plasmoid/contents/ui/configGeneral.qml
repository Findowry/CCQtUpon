import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: configPage

    property alias cfg_daemonUrl: urlField.text
    property alias cfg_accessToken: tokenField.text
    property alias cfg_pollIntervalMs: pollSpin.value
    property int cfg_panelDisplayMode: 0

    Kirigami.FormLayout {
        QQC2.TextField {
            id: urlField
            Kirigami.FormData.label: i18n("Daemon URL:")
            placeholderText: "http://localhost:11987"
            Layout.fillWidth: true
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Access Token:")
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextField {
                id: tokenField
                Layout.fillWidth: true
                placeholderText: "cc_..."
                echoMode: showTokenBtn.checked ? TextInput.Normal : TextInput.Password
            }

            QQC2.ToolButton {
                id: showTokenBtn
                checkable: true
                icon.name: checked ? "visibility-off" : "visibility"
                QQC2.ToolTip.text: checked ? i18n("Hide token") : i18n("Show token")
                QQC2.ToolTip.visible: hovered
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.75
            text: i18n("Create a token in CoolerControl: Access -> Access Tokens -> enable Write Access -> Create Token.")
        }

        QQC2.SpinBox {
            id: pollSpin
            Kirigami.FormData.label: i18n("Poll interval (ms):")
            from: 500
            to: 10000
            stepSize: 250
            editable: true
        }

        QQC2.ComboBox {
            id: modeCombo
            Kirigami.FormData.label: i18n("Panel display:")
            textRole: "label"
            valueRole: "modeValue"
            model: [
                { modeValue: 0, label: i18n("Icon only") },
                { modeValue: 1, label: i18n("Fan RPM") },
                { modeValue: 2, label: i18n("Fan Duty (%)") }
            ]
            onActivated: function(idx) {
                configPage.cfg_panelDisplayMode = model[idx].modeValue
            }
            Component.onCompleted: {
                for (let i = 0; i < model.length; ++i) {
                    if (model[i].modeValue === configPage.cfg_panelDisplayMode) {
                        currentIndex = i
                        break
                    }
                }
            }
        }
    }
}
