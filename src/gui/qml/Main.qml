import QtQuick
import org.kde.kirigami as Kirigami
import Orbita

Kirigami.ApplicationWindow {
    id: root

    title: "Orbita"
    minimumWidth: Kirigami.Units.gridUnit * 40
    minimumHeight: Kirigami.Units.gridUnit * 28

    // Стрелки «назад/вперёд» в шапке: без них со вложенной страницы
    // (например, с корзины) некуда вернуться.
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar
    pageStack.globalToolBar.showNavigationButtons: Kirigami.ApplicationHeaderStyle.ShowBackButton

    // Страницы кладём по адресу файла, а не компонентом: компонент Kirigami
    // успевает создать вне сцены, из-за чего ругался на «объект вне сцены».
    readonly property url loginPage: Qt.resolvedUrl("LoginPage.qml")
    readonly property url browserPage: Qt.resolvedUrl("BrowserPage.qml")

    Component.onCompleted: pageStack.push(AppController.loggedIn ? browserPage : loginPage)

    Connections {
        target: AppController

        function onLoggedInChanged() {
            root.pageStack.replace(AppController.loggedIn ? browserPage : loginPage)
        }

        // Ошибки показываем всплывающим сообщением: они почти всегда сетевые
        // и не должны рвать работу с деревом.
        function onErrorOccurred(message) {
            root.showPassiveNotification(message, "long")
        }
    }
}
