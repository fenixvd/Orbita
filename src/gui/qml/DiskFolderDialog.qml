import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Orbita

/// Обзор папок Яндекс.Диска.
///
/// Системный диалог выбора папки тут не годится: он показывает файловую
/// систему компьютера и ничего не знает о Диске, который может быть даже
/// не подключён. Поэтому свой — по локальному дереву, с обычными действиями.
Kirigami.Dialog {
    id: dialog

    /// Путь, выбранный пользователем; читается после accepted.
    property alias selectedPath: pathField.text

    title: qsTr("Выбор папки на Диске")
    standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
    preferredWidth: Kirigami.Units.gridUnit * 28
    preferredHeight: Kirigami.Units.gridUnit * 24

    /// Открывает обзор на указанной папке.
    function openAt(path) {
        folders.foldersOnly = true
        folders.cd(path && path.length > 0 ? path : "/")
        pathField.text = folders.path
        open()
    }

    FileListModel {
        id: folders
        foldersOnly: true

        // Сверяемся с сервером при переходе: папку могли создать с телефона.
        onPathChanged: {
            AppController.refreshDir(path)
            pathField.text = path
        }
    }

    Connections {
        target: AppController
        function onTreeChanged() { folders.refresh() }
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true

            QQC2.ToolButton {
                icon.name: "go-up"
                text: qsTr("Вверх")
                display: QQC2.AbstractButton.IconOnly
                enabled: !folders.atRoot
                onClicked: folders.up()
            }
            QQC2.ToolButton {
                icon.name: "folder-new"
                text: qsTr("Новая папка")
                display: QQC2.AbstractButton.IconOnly
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                onClicked: newFolder.open()
            }
            QQC2.ToolButton {
                icon.name: "view-refresh"
                text: qsTr("Обновить")
                display: QQC2.AbstractButton.IconOnly
                onClicked: AppController.refreshDir(folders.path)
            }
            Item { Layout.fillWidth: true }
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 14
            clip: true

            ListView {
                id: view
                model: folders
                currentIndex: -1

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 4
                    visible: view.count === 0
                    icon.name: "folder"
                    text: qsTr("Вложенных папок нет")
                    explanation: qsTr("Можно выбрать текущую или создать новую.")
                }

                delegate: QQC2.ItemDelegate {
                    id: row
                    width: view.width

                    required property string name
                    required property string path

                    // Один щелчок заходит внутрь — как в любом обзоре папок.
                    onClicked: folders.cd(row.path)

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: rowMenu.popup()
                    }

                    QQC2.Menu {
                        id: rowMenu
                        QQC2.MenuItem {
                            text: qsTr("Переименовать")
                            icon.name: "edit-rename"
                            onTriggered: {
                                renameFolder.targetPath = row.path
                                renameFolder.currentName = row.name
                                renameFolder.open()
                            }
                        }
                        QQC2.MenuItem {
                            text: qsTr("Удалить в корзину")
                            icon.name: "user-trash"
                            onTriggered: {
                                confirmDelete.targetPath = row.path
                                confirmDelete.targetName = row.name
                                confirmDelete.open()
                            }
                        }
                    }

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing
                        Kirigami.Icon {
                            source: "folder"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                        }
                        QQC2.Label {
                            text: row.name
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Kirigami.Icon {
                            source: "go-next"
                            opacity: 0.5
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                        }
                    }
                }
            }
        }

        // Ручной ввод остаётся: путь бывает проще вставить, чем искать.
        QQC2.TextField {
            id: pathField
            Layout.fillWidth: true
            placeholderText: qsTr("/Путь/на/Диске")
            onAccepted: folders.cd(text)
        }
    }

    Kirigami.PromptDialog {
        id: newFolder
        title: qsTr("Новая папка")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

        QQC2.TextField {
            id: newFolderName
            placeholderText: qsTr("Название")
            onAccepted: newFolder.accept()
        }

        onOpened: { newFolderName.text = ""; newFolderName.forceActiveFocus() }
        onAccepted: AppController.createFolder(folders.path, newFolderName.text)
    }

    Kirigami.PromptDialog {
        id: renameFolder
        title: qsTr("Переименовать папку")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

        property string targetPath: ""
        property string currentName: ""

        QQC2.TextField {
            id: renameFolderName
            onAccepted: renameFolder.accept()
        }

        onOpened: {
            renameFolderName.text = renameFolder.currentName
            renameFolderName.forceActiveFocus()
            renameFolderName.selectAll()
        }
        onAccepted: AppController.renameItem(renameFolder.targetPath, renameFolderName.text)
    }

    // Удаление папки уносит и всё её содержимое — спрашиваем прямо.
    Kirigami.PromptDialog {
        id: confirmDelete
        title: qsTr("Удалить папку?")
        subtitle: qsTr("«%1» отправится в корзину вместе с содержимым.")
                      .arg(confirmDelete.targetName)
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

        property string targetPath: ""
        property string targetName: ""

        onAccepted: AppController.removeItem(confirmDelete.targetPath)
    }
}
