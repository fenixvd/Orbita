import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Orbita

Kirigami.ScrollablePage {
    id: page

    title: qsTr("Корзина")

    actions: [
        Kirigami.Action {
            text: qsTr("Назад к Диску")
            icon.name: "go-previous"
            onTriggered: pageStack.pop()
        },
        Kirigami.Action {
            text: qsTr("Обновить")
            icon.name: "view-refresh"
            enabled: !AppController.busy
            onTriggered: AppController.loadTrash()
        },
        Kirigami.Action {
            text: qsTr("Очистить корзину")
            icon.name: "trash-empty"
            enabled: !AppController.busy && !AppController.trash.empty
            onTriggered: confirmEmpty.open()
        }
    ]

    ListView {
        id: view
        model: AppController.trash
        currentIndex: -1

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: view.count === 0
            icon.name: "user-trash"
            text: qsTr("Корзина пуста")
        }

        delegate: QQC2.ItemDelegate {
            id: item
            width: view.width
            hoverEnabled: true

            required property string name
            required property string path
            required property string sizeText
            required property string iconName
            required property string originPath

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    source: item.iconName
                    implicitWidth: Kirigami.Units.iconSizes.medium
                    implicitHeight: Kirigami.Units.iconSizes.medium
                }

                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true

                    QQC2.Label {
                        text: item.name
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    // Откуда файл удалён — без этого непонятно, что именно
                    // восстановится и куда.
                    QQC2.Label {
                        text: item.originPath.length > 0
                              ? item.sizeText + " · " + item.originPath
                              : item.sizeText
                        opacity: 0.6
                        font: Kirigami.Theme.smallFont
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }

                QQC2.ToolButton {
                    icon.name: "edit-undo"
                    text: qsTr("Восстановить")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: qsTr("Восстановить на прежнее место")
                    QQC2.ToolTip.visible: hovered
                    onClicked: AppController.restoreFromTrash(item.path)
                }

                QQC2.ToolButton {
                    icon.name: "edit-delete-shred"
                    text: qsTr("Удалить навсегда")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: qsTr("Удалить навсегда")
                    QQC2.ToolTip.visible: hovered
                    onClicked: AppController.deleteFromTrash(item.path)
                }
            }
        }
    }

    // Очистка корзины необратима — спрашиваем прямо.
    Kirigami.PromptDialog {
        id: confirmEmpty
        title: qsTr("Очистить корзину?")
        subtitle: qsTr("Всё её содержимое будет удалено безвозвратно.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: AppController.emptyTrash()
    }
}
