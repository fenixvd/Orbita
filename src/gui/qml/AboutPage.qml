import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Orbita

Kirigami.ScrollablePage {
    id: page

    title: qsTr("О программе")

    actions: [
        Kirigami.Action {
            text: qsTr("Назад к Диску")
            icon.name: "go-previous"
            onTriggered: pageStack.pop()
        }
    ]

    ColumnLayout {
        width: page.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Icon {
            source: "ru.rainedev.orbita"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Kirigami.Units.gridUnit
            Layout.preferredWidth: Kirigami.Units.iconSizes.enormous
            Layout.preferredHeight: Kirigami.Units.iconSizes.enormous
        }

        Kirigami.Heading {
            text: "Orbita"
            level: 1
            Layout.alignment: Qt.AlignHCenter
        }

        QQC2.Label {
            text: qsTr("версия %1").arg(AppController.version)
            opacity: 0.7
            Layout.alignment: Qt.AlignHCenter
        }

        QQC2.Label {
            text: qsTr("Клиент Яндекс.Диска для Linux")
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit * 2
            Layout.rightMargin: Kirigami.Units.gridUnit * 2
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.Label {
                Kirigami.FormData.label: qsTr("Разработчик:")
                text: "RaineDev"
            }

            QQC2.Label {
                Kirigami.FormData.label: qsTr("Лицензия:")
                text: qsTr("GNU General Public License, версия 2")
            }

            QQC2.Label {
                Kirigami.FormData.label: qsTr("Сайт:")
                text: "<a href=\"https://rainedev.ru\">rainedev.ru</a>"
                onLinkActivated: link => Qt.openUrlExternally(link)

                // Курсор-рука подсказывает, что ссылка живая.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
            }

            QQC2.Label {
                Kirigami.FormData.label: qsTr("Основано на:")
                text: "Qt 6 · Kirigami · FUSE 3 · SQLite"
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.gridUnit

            QQC2.Button {
                text: qsTr("Текст лицензии")
                icon.name: "license"
                onClicked: Qt.openUrlExternally("https://www.gnu.org/licenses/old-licenses/gpl-2.0.html")
            }
        }
    }
}
