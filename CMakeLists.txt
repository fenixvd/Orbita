cmake_minimum_required(VERSION 3.28)

project(Orbita
    VERSION 0.1.0
    DESCRIPTION "Клиент Яндекс.Диска для Linux"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core DBus Network Gui Widgets Quick QuickControls2)
# Значок в лотке по протоколу StatusNotifierItem — родной способ для Wayland.
find_package(KF6StatusNotifierItem REQUIRED)
# SQLite напрямую, а не через QtSql: соединение QtSql привязано к потоку,
# а файловая система работает из многих недолговечных потоков libfuse.
find_package(SQLite3 REQUIRED)
# Имя цели зависит от версии CMake: SQLite3::SQLite3 в новых, SQLite::SQLite3
# в тех, что ещё в Debian. Поддерживаем оба, иначе сборка там не проходит.
if(TARGET SQLite3::SQLite3)
    set(ORBITA_SQLITE SQLite3::SQLite3)
else()
    set(ORBITA_SQLITE SQLite::SQLite3)
endif()
qt_standard_project_setup(REQUIRES 6.5)

# FUSE опционален: без него собирается всё, кроме точки монтирования.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 IMPORTED_TARGET fuse3)

qt_add_library(orbita-core STATIC
    src/core/MetadataStore.cpp
    src/core/CacheManager.cpp
    src/core/TokenStore.cpp
    src/core/AuthManager.cpp
    src/core/DiskApi.cpp
    src/core/DiskOps.cpp
    src/core/Notifier.cpp
    src/core/Paths.cpp
    src/core/Naming.cpp
    # Заголовки с Q_OBJECT нужно перечислять явно, иначе AUTOMOC их не увидит.
    include/orbita/AuthManager.h
    include/orbita/DiskApi.h
)
target_include_directories(orbita-core PUBLIC include)
# Ядро линкуется и в плагин Dolphin, а тот собирается как разделяемая
# библиотека — без этого линковщик откажется.
set_target_properties(orbita-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(orbita-core PUBLIC Qt6::Core Qt6::DBus Qt6::Network ${ORBITA_SQLITE})
# client_id приложения на oauth.yandex.ru. Собрать со своим никто не мешает.
set(ORBITA_CLIENT_ID "31ce063bb5c94fafa1824d158bf25822" CACHE STRING
    "client_id приложения Яндекс OAuth")
target_compile_definitions(orbita-core PUBLIC
    ORBITA_VERSION="${PROJECT_VERSION}"
    ORBITA_CLIENT_ID="${ORBITA_CLIENT_ID}")

qt_add_executable(orbita src/main.cpp)
target_link_libraries(orbita PRIVATE orbita-core)

if(FUSE3_FOUND)
    target_sources(orbita-core PRIVATE src/fs/OrbitaFuse.cpp include/orbita/FuseMount.h)
    target_link_libraries(orbita-core PUBLIC PkgConfig::FUSE3)
    target_compile_definitions(orbita-core PUBLIC ORBITA_WITH_FUSE)
    message(STATUS "FUSE3 ${FUSE3_VERSION} — монтирование включено")
else()
    message(WARNING "fuse3 не найден — сборка без монтирования (установите fuse3-devel)")
endif()

qt_add_executable(orbita-gui
    src/gui/main.cpp
    src/gui/AppController.cpp
    src/gui/AppController.h
    src/gui/FileListModel.cpp
    src/gui/FileListModel.h
    src/gui/Workers.cpp
    src/gui/Workers.h
    src/gui/FileOpsWorker.cpp
    src/gui/FileOpsWorker.h
    src/gui/TrashModel.cpp
    src/gui/TrashModel.h
    src/gui/ThumbnailProvider.cpp
    src/gui/ThumbnailProvider.h
    src/gui/TrayIcon.cpp
    src/gui/TrayIcon.h
    src/gui/Settings.cpp
    src/gui/Settings.h
    src/gui/SingleInstance.cpp
    src/gui/SingleInstance.h
)
target_link_libraries(orbita-gui PRIVATE orbita-core Qt6::Gui Qt6::Widgets Qt6::Quick
                                         Qt6::QuickControls2 KF6::StatusNotifierItem)
# Генератор регистрации типов QML подключает заголовки как <AppController.h>.
target_include_directories(orbita-gui PRIVATE src/gui)

qt_add_qml_module(orbita-gui
    URI Orbita
    VERSION 1.0
    QML_FILES
        src/gui/qml/Main.qml
        src/gui/qml/LoginPage.qml
        src/gui/qml/BrowserPage.qml
        src/gui/qml/TrashPage.qml
        src/gui/qml/SettingsPage.qml
        src/gui/qml/DiskFolderDialog.qml
        src/gui/qml/AboutPage.qml
)

# QML переводится только в байткод. С опережающей компиляцией qmlcachegen
# порождает код, зовущий приватный QQmlPrivate::AOTCompiledContext, а приватные
# символы Qt помечены точной версией (Qt_6.11.1_PRIVATE_API) — собранный пакет
# не встанет на систему даже с соседней патч-версией Qt.
set_target_properties(orbita-gui PROPERTIES QT_QMLCACHEGEN_ARGUMENTS "--only-bytecode")

# Английский интерфейс. Исходные строки русские, перевод — в i18n/orbita_en.ts.
# Необязательно: без инструментов Linguist собирается всё, кроме переводов.
find_package(Qt6 COMPONENTS LinguistTools QUIET)
if(Qt6LinguistTools_FOUND)
    qt_add_translations(orbita-gui
        TS_FILES i18n/orbita_en.ts
        LUPDATE_OPTIONS -no-obsolete
        QM_FILES_OUTPUT_VARIABLE orbita_qm_files
    )
    install(FILES ${orbita_qm_files}
            DESTINATION ${CMAKE_INSTALL_DATADIR}/orbita/translations)
    target_compile_definitions(orbita-gui PRIVATE
        ORBITA_TRANSLATIONS_DIR="${CMAKE_INSTALL_FULL_DATADIR}/orbita/translations")
else()
    message(STATUS "Qt Linguist не найден — сборка без переводов")
endif()

qt_add_resources(orbita-gui "icons"
    PREFIX "/icons"
    BASE res/icons
    FILES res/icons/orbita.png
)

# Плагин эмблем для Dolphin. Необязателен: без KIO собирается всё остальное.
include(GNUInstallDirs)
find_package(KF6KIO QUIET)
find_package(KF6CoreAddons QUIET)
if(KF6KIO_FOUND AND KF6CoreAddons_FOUND)
    add_library(orbita-overlay MODULE src/kio/OrbitaOverlayPlugin.cpp)
    set_target_properties(orbita-overlay PROPERTIES AUTOMOC ON)
    target_link_libraries(orbita-overlay PRIVATE orbita-core KF6::KIOCore)

    # Каталог модулей Qt у каждого дистрибутива свой: lib64/qt6/plugins в
    # openSUSE и Fedora, lib/<арх>/qt6/plugins в Debian. Спрашиваем его
    # у самого Qt — ошибка здесь тихая: пакет встанет, а Dolphin не покажет
    # ни эмблем, ни меню, потому что искать будет не там.
    #
    # При сборке пакета для чужого дистрибутива путь задаётся вручную:
    #   -DORBITA_PLUGIN_DIR=lib/x86_64-linux-gnu/qt6/plugins   (Debian, Ubuntu)
    set(ORBITA_PLUGIN_DIR "" CACHE STRING
        "Каталог модулей Qt относительно префикса установки")
    if(NOT ORBITA_PLUGIN_DIR)
        if(DEFINED QT6_INSTALL_PLUGINS)
            set(ORBITA_PLUGIN_DIR ${QT6_INSTALL_PLUGINS})
        else()
            set(ORBITA_PLUGIN_DIR ${CMAKE_INSTALL_LIBDIR}/qt6/plugins)
        endif()
    endif()

    install(TARGETS orbita-overlay DESTINATION ${ORBITA_PLUGIN_DIR}/kf6/overlayicon)
    message(STATUS "Плагин эмблем для Dolphin включён → ${ORBITA_PLUGIN_DIR}")

    # Пункты меню — тоже плагином, а не .desktop-файлом: только так набор
    # действий зависит от того, лежит файл на Диске или вне его.
    add_library(orbita-fileitemaction MODULE src/kio/OrbitaFileItemAction.cpp)
    set_target_properties(orbita-fileitemaction PROPERTIES AUTOMOC ON)
    target_link_libraries(orbita-fileitemaction PRIVATE orbita-core KF6::KIOWidgets)
    install(TARGETS orbita-fileitemaction
            DESTINATION ${ORBITA_PLUGIN_DIR}/kf6/kfileitemaction)
else()
    message(STATUS "KF6 KIO не найден — плагин эмблем не собирается")
endif()

# Ярлык собирается из шаблона: системной установке нужен Exec из PATH,
# а запуску из сборочного каталога — полный путь, иначе портал xdg
# не находит приложение и значок в панели остаётся пустым.
set(ORBITA_EXEC "orbita-gui")
configure_file(res/ru.rainedev.orbita.desktop.in
               ${CMAKE_BINARY_DIR}/desktop/system/ru.rainedev.orbita.desktop @ONLY)
set(ORBITA_EXEC "${CMAKE_BINARY_DIR}/orbita-gui")
configure_file(res/ru.rainedev.orbita.desktop.in
               ${CMAKE_BINARY_DIR}/desktop/local/ru.rainedev.orbita.desktop @ONLY)

# Меню Dolphin вызывает консольный orbita, поэтому путь к нему подставляется
# так же, как и для ярлыка: из PATH при установке, полный — при запуске
# из сборочного каталога.
set(ORBITA_CLI "orbita")
set(ORBITA_GUI "orbita-gui")
configure_file(res/orbita-servicemenu.desktop.in
               ${CMAKE_BINARY_DIR}/desktop/system/orbita-servicemenu.desktop @ONLY)
set(ORBITA_CLI "${CMAKE_BINARY_DIR}/orbita")
set(ORBITA_GUI "${CMAKE_BINARY_DIR}/orbita-gui")
configure_file(res/orbita-servicemenu.desktop.in
               ${CMAKE_BINARY_DIR}/desktop/local/orbita-servicemenu.desktop @ONLY)

install(TARGETS orbita orbita-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Текст лицензии обязан ехать вместе с программой — этого требует сама GPL.
install(FILES LICENSE README.md README.en.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/orbita)
install(FILES ${CMAKE_BINARY_DIR}/desktop/system/ru.rainedev.orbita.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
# Значок ставится во все ходовые размеры: с одним лишь 1024x1024 меню
# приложений его не находит — на этом мы уже обожглись при ручной установке.
foreach(size 48 64 128 256 512)
    install(FILES res/icons/orbita-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME ru.rainedev.orbita.png)
endforeach()
install(FILES res/icons/orbita.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/1024x1024/apps
        RENAME ru.rainedev.orbita.png)

# Ставит значок и ярлык в домашний каталог — чтобы запускать из меню
# без установки всего проекта в систему.
add_custom_target(install-desktop
    COMMAND ${CMAKE_SOURCE_DIR}/tools/install-desktop.sh
            ${CMAKE_SOURCE_DIR}/res/icons/orbita.png
            ${CMAKE_BINARY_DIR}/desktop/local/ru.rainedev.orbita.desktop
            ${CMAKE_BINARY_DIR}/desktop/local/orbita-servicemenu.desktop
    COMMENT "Установка значка, ярлыка и меню Dolphin для текущего пользователя"
)

# Удобные таргеты, как в прошлой Орбите.
add_custom_target(run COMMAND orbita-gui USES_TERMINAL DEPENDS orbita-gui)
add_custom_target(run-cli COMMAND orbita USES_TERMINAL DEPENDS orbita)

# Тесты. Запуск: cmake --build build && ctest --test-dir build
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    foreach(test naming metadatastore cachemanager)
        qt_add_executable(tst_${test} tests/tst_${test}.cpp)
        target_link_libraries(tst_${test} PRIVATE orbita-core Qt6::Test)
        add_test(NAME ${test} COMMAND tst_${test})
    endforeach()
endif()

# ── Сборка пакетов ───────────────────────────────────────────────────────────
# Собрать: cmake --build build && cpack -G RPM --config build/CPackConfig.cmake
set(CPACK_PACKAGE_NAME "orbita")
set(CPACK_PACKAGE_VENDOR "RaineDev")
set(CPACK_PACKAGE_CONTACT "RaineDev <mrfenixvd@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/fenixvd/Orbita")
# Пакет ставится в /usr: плагины Dolphin ищутся только там, в /usr/local их
# не найдут ни Qt, ни KIO.
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)

set(CPACK_RPM_PACKAGE_LICENSE "GPL-2.0-only")
set(CPACK_RPM_PACKAGE_GROUP "Productivity/Networking/Other")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
# Зависимости не перечисляем руками: имена библиотек различаются в openSUSE
# и Fedora, а автоопределение по ELF даёт верные для каждой системы.
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
# Но модули QML загружаются во время работы и в бинарнике не видны — без
# этого пакет установится, а окно не откроется. Требуем их отдельно.
#
# Раскладка каталогов у дистрибутивов одна, а имена пакетов разные, поэтому
# просить хотелось бы по путям. Но dnf5 (Fedora 41+) по умолчанию не выкачивает
# filelists, и зависимость от каталога у него не разрешается вовсе — установка
# падает с «nothing provides /usr/lib64/qt6/qml/...». Значит для Fedora нужны
# имена пакетов, а для openSUSE (zypper пути умеет) оставляем пути.
if(EXISTS /etc/os-release)
    file(READ /etc/os-release ORBITA_OS_RELEASE)
endif()
if(ORBITA_OS_RELEASE MATCHES "(^|\n)ID(_LIKE)?=\"?[a-z ]*(fedora|rhel)")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "kf6-kirigami, kf6-qqc2-desktop-style, qt6-qtdeclarative")
else()
    set(CPACK_RPM_PACKAGE_REQUIRES
        "/usr/lib64/qt6/qml/org/kde/kirigami, /usr/lib64/qt6/qml/org/kde/desktop, /usr/lib64/qt6/qml/QtQuick/Controls, /usr/lib64/qt6/qml/QtQuick/Dialogs")
endif()
# Каталоги общие с системными пакетами — присваивать их себе нельзя.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/icons /usr/share/icons/hicolor /usr/share/icons/hicolor/48x48
    /usr/share/icons/hicolor/48x48/apps /usr/share/icons/hicolor/64x64
    /usr/share/icons/hicolor/64x64/apps /usr/share/icons/hicolor/128x128
    /usr/share/icons/hicolor/128x128/apps /usr/share/icons/hicolor/256x256
    /usr/share/icons/hicolor/256x256/apps /usr/share/icons/hicolor/512x512
    /usr/share/icons/hicolor/512x512/apps /usr/share/icons/hicolor/1024x1024
    /usr/share/icons/hicolor/1024x1024/apps /usr/share/applications
    /usr/lib64/qt6 /usr/lib64/qt6/plugins /usr/lib64/qt6/plugins/kf6)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
# То же самое для Debian: зависимости выводит dpkg-shlibdeps.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# То же про QML: в Debian модули лежат в пакетах с предсказуемыми именами.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qml6-module-org-kde-kirigami, qml6-module-org-kde-desktop, qml6-module-qtquick-controls, qml6-module-qtquick-dialogs, fuse3")

include(CPack)
