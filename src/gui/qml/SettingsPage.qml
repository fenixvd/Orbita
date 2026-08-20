import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import Orbita

Kirigami.ScrollablePage {
    id: page

    title: qsTr("Настройки")

    actions: [
        Kirigami.Action {
            text: qsTr("Назад к Диску")
            icon.name: "go-previous"
            onTriggered: pageStack.pop()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.label: qsTr("Запуск")
                Kirigami.FormData.isSection: true
            }

            QQC2.Switch {
                Kirigami.FormData.label: qsTr("Значок в системном лотке:")
                checked: Settings.trayEnabled
                onToggled: Settings.trayEnabled = checked
            }

            QQC2.Label {
                text: Settings.trayEnabled
                      ? qsTr("Закрытие окна прячет его в лоток — Диск остаётся подключён.")
                      : qsTr("Без значка закрытие окна завершает работу и отключает Диск.")
                wrapMode: Text.WordWrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
            }

            QQC2.Switch {
                Kirigami.FormData.label: qsTr("Запускать вместе с системой:")
                checked: Settings.autostart
                onToggled: Settings.autostart = checked
            }

            QQC2.Switch {
                Kirigami.FormData.label: qsTr("Подключать Диск при запуске:")
                checked: Settings.automount
                onToggled: Settings.automount = checked
            }

            Kirigami.Separator {
                Kirigami.FormData.label: qsTr("Диск")
                Kirigami.FormData.isSection: true
            }

            RowLayout {
                Kirigami.FormData.label: qsTr("Точка подключения:")

                QQC2.TextField {
                    id: mountField
                    text: Settings.mountPoint
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 15
                    onEditingFinished: Settings.mountPoint = text
                }
                QQC2.Button {
                    icon.name: "folder-open"
                    text: qsTr("Обзор…")
                    // Здесь обычный системный диалог: точка подключения —
                    // папка на компьютере, а не на Диске.
                    onClicked: mountFolderDialog.open()
                }
                QQC2.Label {
                    // Переносить работающую файловую систему на ходу нельзя.
                    text: AppController.mounted ? qsTr("применится после отключения") : ""
                    opacity: 0.7
                    font: Kirigami.Theme.smallFont
                }
            }

            RowLayout {
                Kirigami.FormData.label: qsTr("Папка для загрузок:")

                QQC2.TextField {
                    id: uploadField
                    text: Settings.uploadFolder
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 15
                    onEditingFinished: Settings.uploadFolder = text
                }
                QQC2.Button {
                    icon.name: "folder-cloud"
                    text: qsTr("Обзор…")
                    // А здесь обзор по самому Диску: системный диалог о нём
                    // ничего не знает, тем более когда Диск не подключён.
                    onClicked: diskFolderDialog.openAt(Settings.uploadFolder)
                }
            }

            QQC2.SpinBox {
                Kirigami.FormData.label: qsTr("Проверять изменения каждые, с:")
                from: 15
                to: 3600
                stepSize: 15
                value: Settings.pollSeconds
                onValueModified: Settings.pollSeconds = value
            }

            QQC2.Label {
                Kirigami.FormData.label: qsTr("Состояние:")
                text: AppController.mounted ? qsTr("подключён") : qsTr("отключён")
            }

            QQC2.Label {
                Kirigami.FormData.label: qsTr("Занято на Диске:")
                text: AppController.spaceText.length > 0 ? AppController.spaceText
                                                         : qsTr("неизвестно")
            }

            Kirigami.Separator {
                Kirigami.FormData.label: qsTr("Кэш на устройстве")
                Kirigami.FormData.isSection: true
            }

            QQC2.Label {
                Kirigami.FormData.label: qsTr("Занимает:")
                text: Settings.cacheSizeText
            }

            QQC2.SpinBox {
                Kirigami.FormData.label: qsTr("Не больше, ГиБ:")
                from: 1
                to: 512
                value: Settings.cacheBudgetGiB
                onValueModified: Settings.cacheBudgetGiB = value
            }

            RowLayout {
                Kirigami.FormData.label: qsTr("Очистка:")

                QQC2.Button {
                    text: qsTr("Освободить место")
                    icon.name: "edit-clear-all"
                    // Отключаем при подключённом Диске: снести файлы из-под
                    // работающей файловой системы — верный способ получить
                    // ошибки чтения в открытых программах.
                    enabled: !AppController.mounted
                    onClicked: confirmClear.open()
                }
            }

            QQC2.Label {
                text: AppController.mounted
                      ? qsTr("Сначала отключите Диск.")
                      : qsTr("Скачанные файлы удалятся, закреплённые придётся загрузить заново.")
                wrapMode: Text.WordWrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
            }
        }
    }

    FolderDialog {
        id: mountFolderDialog
        title: qsTr("Куда подключать Диск")
        currentFolder: "file://" + Settings.mountPoint
        onAccepted: {
            const path = selectedFolder.toString().replace("file://", "")
            Settings.mountPoint = decodeURIComponent(path)
            mountField.text = Settings.mountPoint
        }
    }

    DiskFolderDialog {
        id: diskFolderDialog
        onAccepted: {
            Settings.uploadFolder = selectedPath
            uploadField.text = Settings.uploadFolder
        }
    }

    Kirigami.PromptDialog {
        id: confirmClear
        title: qsTr("Освободить место?")
        subtitle: qsTr("Скачанное содержимое и миниатюры будут удалены. "
                       + "Сами файлы на Диске останутся нетронутыми.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: Settings.clearCache()
    }
}
