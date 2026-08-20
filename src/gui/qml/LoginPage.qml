import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Orbita

Kirigami.Page {
    title: qsTr("Вход в Яндекс.Диск")

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 28)
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Icon {
            source: "folder-cloud"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Kirigami.Units.iconSizes.huge
            Layout.preferredHeight: Kirigami.Units.iconSizes.huge
        }

        Kirigami.Heading {
            text: qsTr("Orbita")
            level: 1
            Layout.alignment: Qt.AlignHCenter
        }

        QQC2.Button {
            text: qsTr("Войти через Яндекс")
            icon.name: "globe"
            visible: !AppController.waitingForCode
            enabled: !AppController.busy || AppController.waitingForCode
            Layout.alignment: Qt.AlignHCenter
            onClicked: AppController.startLogin()
        }

        // Приложениям типа «для доступа к API» Яндекс показывает код на своей
        // странице — его и вставляют сюда.
        ColumnLayout {
            visible: AppController.waitingForCode
            spacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true

            QQC2.Label {
                text: qsTr("Разрешите доступ в браузере и вставьте показанный код:")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true

                QQC2.TextField {
                    id: codeField
                    placeholderText: qsTr("Код подтверждения")
                    Layout.fillWidth: true
                    onAccepted: AppController.submitCode(text)
                }

                QQC2.Button {
                    text: qsTr("Подтвердить")
                    enabled: codeField.text.length > 0
                    onClicked: AppController.submitCode(codeField.text)
                }
            }

            QQC2.Button {
                text: qsTr("Скопировать ссылку, если браузер не открылся")
                flat: true
                visible: AppController.authUrl.length > 0
                onClicked: AppController.copyToClipboard(AppController.authUrl)
            }
        }

        QQC2.Label {
            text: AppController.status
            opacity: 0.7
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
    }
}
