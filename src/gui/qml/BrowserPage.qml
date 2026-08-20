import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import Orbita

// Page, а не ScrollablePage: та принимает один дочерний элемент, и область
// приёма перетаскивания оставалась вне сцены.
Kirigami.Page {
    id: page

    padding: 0
    title: files.searching ? qsTr("Поиск: %1").arg(files.searchQuery)
                           : (files.path === "/" ? qsTr("Яндекс.Диск") : files.path)

    // Выделенные пути. Держим именно пути, а не индексы: список
    // перечитывается по сигналам, и индексы после этого уже не те.
    property var selection: []

    function isSelected(path) { return selection.indexOf(path) !== -1 }
    function toggleSelection(path) {
        const copy = selection.slice()
        const at = copy.indexOf(path)
        if (at === -1) copy.push(path); else copy.splice(at, 1)
        selection = copy
    }
    function clearSelection() { selection = [] }
    function selectionOr(path) { return selection.length > 0 ? selection : [path] }

    FileListModel {
        id: files

        // При переходе в папку сверяемся с Диском: только так замечаются
        // удаления и переименования, сделанные на другом устройстве.
        onPathChanged: {
            page.clearSelection()
            if (!searching)
                AppController.refreshDir(path)
        }
        Component.onCompleted: AppController.refreshDir(path)
    }

    Connections {
        target: AppController
        // Операции идут в других потоках — список обновляем по сигналу.
        function onTreeChanged() { files.refresh() }
    }

    actions: [
        Kirigami.Action {
            text: qsTr("Вверх")
            icon.name: "go-up"
            enabled: !files.atRoot || files.searching
            onTriggered: files.searching ? files.setSearch("") : files.up()
        },
        Kirigami.Action {
            text: qsTr("Новая папка")
            icon.name: "folder-new"
            enabled: !AppController.busy
            onTriggered: newFolderDialog.open()
        },
        Kirigami.Action {
            text: qsTr("Загрузить по ссылке")
            icon.name: "edit-download"
            enabled: !AppController.busy
            onTriggered: urlDialog.open()
        },
        Kirigami.Action {
            text: qsTr("Отправить файлы")
            icon.name: "document-send"
            enabled: !AppController.busy
            onTriggered: uploadDialog.open()
        },
        Kirigami.Action {
            text: qsTr("Сортировка")
            icon.name: "view-sort"
            Kirigami.Action {
                text: qsTr("По имени")
                checkable: true
                checked: files.sortBy === 0
                onTriggered: files.sortBy = 0
            }
            Kirigami.Action {
                text: qsTr("По размеру")
                checkable: true
                checked: files.sortBy === 1
                onTriggered: files.sortBy = 1
            }
            Kirigami.Action {
                text: qsTr("По дате")
                checkable: true
                checked: files.sortBy === 2
                onTriggered: files.sortBy = 2
            }
            Kirigami.Action {
                text: qsTr("В обратном порядке")
                checkable: true
                checked: files.sortDescending
                onTriggered: files.sortDescending = !files.sortDescending
            }
        },
        Kirigami.Action {
            text: qsTr("Корзина")
            icon.name: "user-trash"
            onTriggered: {
                AppController.loadTrash()
                pageStack.push(Qt.resolvedUrl("TrashPage.qml"))
            }
        },
        Kirigami.Action {
            text: qsTr("Обновить дерево")
            icon.name: "view-refresh"
            enabled: !AppController.busy
            onTriggered: AppController.startSync()
        },
        Kirigami.Action {
            text: AppController.mounted ? qsTr("Отключить") : qsTr("Подключить")
            icon.name: AppController.mounted ? "media-eject" : "drive-harddisk"
            onTriggered: AppController.mounted ? AppController.unmount() : AppController.mount()
        },
        Kirigami.Action {
            text: qsTr("Открыть папку")
            icon.name: "folder-open"
            enabled: AppController.mounted
            onTriggered: AppController.openMountPoint()
        },
        Kirigami.Action {
            text: qsTr("Настройки")
            icon.name: "configure"
            onTriggered: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
        },
        Kirigami.Action {
            text: qsTr("О программе")
            icon.name: "help-about"
            onTriggered: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
        },
        Kirigami.Action {
            text: qsTr("Выйти")
            icon.name: "system-log-out"
            onTriggered: AppController.logout()
        }
    ]

    header: ColumnLayout {
        spacing: 0

        QQC2.ToolBar {
            Layout.fillWidth: true
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.SearchField {
                    id: searchField
                    placeholderText: qsTr("Поиск по всему Диску")
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                    // Поиск идёт по локальной базе, поэтому можно искать
                    // прямо во время набора — сеть не задействована.
                    onTextChanged: files.setSearch(text)
                }

                QQC2.BusyIndicator {
                    running: AppController.busy
                    visible: AppController.busy
                    implicitHeight: Kirigami.Units.iconSizes.small
                    implicitWidth: Kirigami.Units.iconSizes.small
                }

                QQC2.Label {
                    text: AppController.status
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                // Только когда доля известна — иначе хватает вертушки.
                Rectangle {
                    visible: AppController.progress >= 0
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                    Layout.preferredHeight: Kirigami.Units.smallSpacing * 1.5
                    radius: height / 2
                    color: Qt.alpha(Kirigami.Theme.textColor, 0.15)

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * Math.max(0, Math.min(1, AppController.progress))
                        radius: height / 2
                        color: Kirigami.Theme.highlightColor
                        Behavior on width { NumberAnimation { duration: 120 } }
                    }
                }

                QQC2.Label {
                    visible: AppController.progress >= 0
                    text: Math.round(AppController.progress * 100) + "%"
                    opacity: 0.7
                    font: Kirigami.Theme.smallFont
                }

                // Квота Диска: копирование упирается именно в неё.
                QQC2.Label {
                    text: AppController.spaceText
                    visible: AppController.spaceText.length > 0
                    opacity: 0.7
                }

                Rectangle {
                    visible: AppController.spaceText.length > 0
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                    Layout.preferredHeight: Kirigami.Units.smallSpacing * 1.5
                    radius: height / 2
                    color: Kirigami.Theme.backgroundColor
                    border.width: 1
                    border.color: Qt.alpha(Kirigami.Theme.textColor, 0.2)

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.margins: 1
                        width: Math.max(0, (parent.width - 2) * AppController.spaceRatio)
                        radius: height / 2
                        color: AppController.spaceRatio > 0.9 ? Kirigami.Theme.negativeTextColor
                                                              : Kirigami.Theme.highlightColor
                    }
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !AppController.online
            type: Kirigami.MessageType.Warning
            text: qsTr("Нет связи с Яндекс.Диском. Изменения отправятся, когда сеть вернётся.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dropArea.containsDrag
            type: Kirigami.MessageType.Information
            text: qsTr("Отпустите — файлы уйдут в «%1»").arg(files.path)
        }
    }

    Shortcut {
        sequence: StandardKey.Delete
        onActivated: if (page.selection.length > 0) AppController.removeItems(page.selection)
    }
    Shortcut {
        sequence: "F2"
        onActivated: {
            if (page.selection.length !== 1)
                return
            const path = page.selection[0]
            renameDialog.targetPath = path
            renameDialog.currentName = path.substring(path.lastIndexOf("/") + 1)
            renameDialog.open()
        }
    }
    Shortcut {
        sequence: StandardKey.Back
        onActivated: files.up()
    }
    Shortcut {
        sequence: StandardKey.Refresh
        onActivated: AppController.refreshDir(files.path)
    }
    Shortcut {
        sequence: StandardKey.SelectAll
        onActivated: {
            const all = []
            for (let i = 0; i < files.rowCount(); ++i)
                all.push(files.data(files.index(i, 0), 258))   // PathRole
            page.selection = all
        }
    }
    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: page.clearSelection()
    }

    // Перетаскивание файлов из системы прямо в открытую папку Диска.
    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]
        // Поверх списка: перетаскивание не должно теряться на элементах,
        // а щелчки мыши DropArea всё равно пропускает сквозь себя.
        z: 5

        onDropped: (drop) => {
            if (drop.hasUrls) {
                AppController.uploadUrls(drop.urls, files.path)
                drop.acceptProposedAction()
            }
        }

        // Подсветка всей области, пока над ней держат файлы.
        Rectangle {
            anchors.fill: parent
            visible: dropArea.containsDrag
            z: 10
            color: Qt.alpha(Kirigami.Theme.highlightColor, 0.15)
            border.width: 2
            border.color: Kirigami.Theme.highlightColor
        }
    }

    QQC2.ScrollView {
        anchors.fill: parent
        clip: true

    ListView {
        id: view
        model: files
        currentIndex: -1

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: view.count === 0
            icon.name: files.searching ? "system-search" : "folder-sync"
            text: files.searching ? qsTr("Ничего не найдено") : qsTr("Здесь пусто")
            explanation: files.searching
                ? qsTr("Попробуйте другое название.")
                : qsTr("Перетащите сюда файлы или нажмите «Обновить дерево».")
        }

        delegate: QQC2.ItemDelegate {
            id: item
            width: view.width

            required property string name
            required property string path
            required property bool isDir
            required property string sizeText
            required property string stateText
            required property string iconName
            required property string publicUrl
            required property int state
            required property bool previewable
            required property string md5
            required property string modifiedText

            highlighted: page.isSelected(item.path)

            onClicked: (mouse) => {
                // Ctrl — добавить к выделению, обычный щелчок — открыть.
                if (mouse.modifiers & Qt.ControlModifier) {
                    page.toggleSelection(item.path)
                    return
                }
                page.clearSelection()
                if (item.isDir)
                    files.cd(item.path)
                else
                    AppController.openFile(item.path)
            }

            // Правая кнопка — всё, что можно сделать с файлом.
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: {
                    // По правой кнопке на невыделенном файле работаем с ним,
                    // а выделение сбрасываем — иначе непонятно, что затронется.
                    if (!page.isSelected(item.path))
                        page.clearSelection()
                    contextMenu.popup()
                }
            }

            QQC2.Menu {
                id: contextMenu

                QQC2.MenuItem {
                    text: item.isDir ? qsTr("Открыть папку") : qsTr("Открыть файл")
                    icon.name: "document-open"
                    onTriggered: item.isDir ? files.cd(item.path) : AppController.openFile(item.path)
                }
                QQC2.MenuSeparator {}
                QQC2.MenuItem {
                    text: page.selection.length > 1
                          ? qsTr("Сохранить на устройстве (%1)").arg(page.selection.length)
                          : qsTr("Сохранить на устройстве")
                    icon.name: "download"
                    onTriggered: AppController.pinItems(page.selectionOr(item.path))
                }
                QQC2.MenuItem {
                    text: qsTr("Убрать с устройства")
                    icon.name: "edit-delete-remove"
                    onTriggered: AppController.unpinItems(page.selectionOr(item.path))
                }
                QQC2.MenuSeparator {}
                QQC2.MenuItem {
                    text: item.publicUrl.length > 0 ? qsTr("Скопировать ссылку")
                                                    : qsTr("Поделиться ссылкой")
                    icon.name: "emblem-shared"
                    onTriggered: item.publicUrl.length > 0
                                 ? AppController.copyToClipboard(item.publicUrl)
                                 : AppController.publishItem(item.path)
                }
                QQC2.MenuItem {
                    text: qsTr("Поделиться с паролем или сроком…")
                    icon.name: "lock"
                    onTriggered: {
                        shareDialog.targetPath = item.path
                        shareDialog.open()
                    }
                }
                QQC2.MenuItem {
                    text: qsTr("Закрыть доступ")
                    icon.name: "object-locked"
                    visible: item.publicUrl.length > 0
                    height: visible ? implicitHeight : 0
                    onTriggered: AppController.unpublishItem(item.path)
                }
                QQC2.MenuSeparator {}
                QQC2.MenuItem {
                    text: qsTr("Создать копию")
                    icon.name: "edit-copy"
                    onTriggered: AppController.duplicateItem(item.path)
                }
                QQC2.MenuItem {
                    text: qsTr("Переименовать")
                    icon.name: "edit-rename"
                    onTriggered: {
                        renameDialog.targetPath = item.path
                        renameDialog.currentName = item.name
                        renameDialog.open()
                    }
                }
                QQC2.MenuItem {
                    text: page.selection.length > 1
                          ? qsTr("Удалить в корзину (%1)").arg(page.selection.length)
                          : qsTr("Удалить в корзину")
                    icon.name: "user-trash"
                    onTriggered: AppController.removeItems(page.selectionOr(item.path))
                }
                QQC2.MenuSeparator {}
                QQC2.MenuItem {
                    text: qsTr("Свойства")
                    icon.name: "documentinfo"
                    onTriggered: {
                        propertiesDialog.item = {
                            name: item.name, path: item.path, isDir: item.isDir,
                            sizeText: item.sizeText, stateText: item.stateText,
                            md5: item.md5, modifiedText: item.modifiedText,
                            publicUrl: item.publicUrl
                        }
                        propertiesDialog.open()
                    }
                }
            }

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                // Значок или миниатюра. Эскиз отдельной картинкой от Яндекса —
                // сам файл не скачивается.
                Item {
                    implicitWidth: Kirigami.Units.iconSizes.large
                    implicitHeight: Kirigami.Units.iconSizes.large

                    Kirigami.Icon {
                        anchors.fill: parent
                        source: item.iconName
                        visible: thumbnail.status !== Image.Ready
                    }

                    Image {
                        id: thumbnail
                        anchors.fill: parent
                        // Асинхронно: иначе прокрутка списка будет ждать сеть.
                        asynchronous: true
                        cache: true
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                        source: item.previewable ? "image://orbita" + item.path : ""
                        visible: status === Image.Ready
                    }
                }

                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true

                    QQC2.Label {
                        text: item.name
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    QQC2.Label {
                        text: item.isDir ? qsTr("папка") : item.sizeText + " · " + item.stateText
                        opacity: 0.6
                        font: Kirigami.Theme.smallFont
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                // Опубликованность видна сразу, без открытия меню.
                Kirigami.Icon {
                    source: "emblem-shared"
                    visible: item.publicUrl.length > 0
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }

                // Состояние содержимого — то, ради чего всё затевалось.
                Kirigami.Icon {
                    visible: !item.isDir
                    source: {
                        switch (item.state) {
                        case 1: return "vcs-normal"           // на устройстве
                        case 2: return "pin"                  // закреплён
                        case 3: return "vcs-locally-modified" // ждёт выгрузки
                        default: return "cloud-download"      // только в облаке
                        }
                    }
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }
            }
        }
    }

    } // ScrollView

    FileDialog {
        id: uploadDialog
        title: qsTr("Выберите файлы для отправки")
        fileMode: FileDialog.OpenFiles
        onAccepted: AppController.uploadUrls(selectedFiles, files.path)
    }

    Kirigami.PromptDialog {
        id: newFolderDialog
        title: qsTr("Новая папка")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

        QQC2.TextField {
            id: folderNameField
            placeholderText: qsTr("Название папки")
            onAccepted: newFolderDialog.accept()
        }

        onOpened: { folderNameField.text = ""; folderNameField.forceActiveFocus() }
        onAccepted: AppController.createFolder(files.path, folderNameField.text)
    }

    Kirigami.PromptDialog {
        id: renameDialog
        title: qsTr("Переименовать")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

        property string targetPath: ""
        property string currentName: ""

        QQC2.TextField {
            id: renameField
            onAccepted: renameDialog.accept()
        }

        onOpened: {
            renameField.text = renameDialog.currentName
            renameField.forceActiveFocus()
            renameField.selectAll()
        }
        onAccepted: AppController.renameItem(renameDialog.targetPath, renameField.text)
    }

    Kirigami.PromptDialog {
        id: propertiesDialog
        title: qsTr("Свойства")
        standardButtons: Kirigami.Dialog.Ok

        // Простой объект вместо ссылки на строку списка: список
        // перечитывается по сигналам, и ссылка успела бы протухнуть.
        property var item: ({})

        Kirigami.FormLayout {
            QQC2.Label {
                Kirigami.FormData.label: qsTr("Имя:")
                text: propertiesDialog.item.name || ""
            }
            QQC2.Label {
                Kirigami.FormData.label: qsTr("Путь:")
                text: propertiesDialog.item.path || ""
                wrapMode: Text.WrapAnywhere
                Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            }
            QQC2.Label {
                Kirigami.FormData.label: qsTr("Тип:")
                text: propertiesDialog.item.isDir ? qsTr("папка") : qsTr("файл")
            }
            QQC2.Label {
                Kirigami.FormData.label: qsTr("Размер:")
                visible: !propertiesDialog.item.isDir
                text: propertiesDialog.item.sizeText || ""
            }
            QQC2.Label {
                Kirigami.FormData.label: qsTr("Изменён:")
                text: propertiesDialog.item.modifiedText || qsTr("неизвестно")
            }
            QQC2.Label {
                Kirigami.FormData.label: qsTr("Состояние:")
                visible: !propertiesDialog.item.isDir
                text: propertiesDialog.item.stateText || ""
            }
            QQC2.TextField {
                Kirigami.FormData.label: qsTr("Контрольная сумма:")
                visible: (propertiesDialog.item.md5 || "").length > 0
                text: propertiesDialog.item.md5 || ""
                readOnly: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 20
            }
            QQC2.TextField {
                Kirigami.FormData.label: qsTr("Публичная ссылка:")
                visible: (propertiesDialog.item.publicUrl || "").length > 0
                text: propertiesDialog.item.publicUrl || ""
                readOnly: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 20
            }
        }
    }

    Kirigami.PromptDialog {
        id: shareDialog
        title: qsTr("Поделиться ссылкой")
        subtitle: qsTr("Пароль и срок можно не задавать — тогда ссылка обычная.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

        property string targetPath: ""

        ColumnLayout {
            QQC2.TextField {
                id: sharePassword
                placeholderText: qsTr("Пароль (необязательно)")
                echoMode: TextInput.Normal
                Layout.fillWidth: true
            }
            RowLayout {
                QQC2.Label { text: qsTr("Действует дней:") }
                QQC2.SpinBox {
                    id: shareDays
                    from: 0
                    to: 365
                    value: 0
                }
                QQC2.Label {
                    text: shareDays.value === 0 ? qsTr("без срока") : ""
                    opacity: 0.7
                }
            }
        }

        onOpened: { sharePassword.text = ""; shareDays.value = 0 }
        onAccepted: AppController.publishItem(shareDialog.targetPath, sharePassword.text,
                                              shareDays.value)
    }

    Kirigami.PromptDialog {
        id: urlDialog
        title: qsTr("Загрузить по ссылке")
        subtitle: qsTr("Файл скачает сам Яндекс — через ваш компьютер он не пройдёт.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

        ColumnLayout {
            QQC2.TextField {
                id: urlField
                placeholderText: qsTr("https://…")
                Layout.fillWidth: true
                onAccepted: urlDialog.accept()
            }
            QQC2.TextField {
                id: urlNameField
                placeholderText: qsTr("Имя файла (необязательно)")
                Layout.fillWidth: true
            }
        }

        onOpened: { urlField.text = ""; urlNameField.text = ""; urlField.forceActiveFocus() }
        onAccepted: AppController.uploadFromUrl(urlField.text, files.path, urlNameField.text)
    }
}
