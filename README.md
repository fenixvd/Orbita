# Orbita

**Клиент Яндекс.Диска для Linux.** [English](README.en.md)

[![Сборка и тесты](https://github.com/fenixvd/Orbita/actions/workflows/build.yml/badge.svg)](https://github.com/fenixvd/Orbita/actions/workflows/build.yml)
[![Лицензия: GPL-2.0](https://img.shields.io/badge/лицензия-GPL--2.0-blue.svg)](LICENSE)

Официальный клиент под Linux — консольный демон, который умеет ровно одно:
зеркалить папку целиком. Хотите посмотреть, что лежит на Диске? Скачайте всё.
Два терабайта? Значит, два терабайта. Графического интерфейса нет, Wayland
он не видел, а последняя заметная новость о нём — что он всё ещё существует.

Orbita делает то, что в Windows работает много лет: **Диск виден целиком,
а место занимают только те файлы, которые вы открыли.**

![Главное окно Orbita](docs/screenshots/main-window.png)

## Возможности

- вход через Яндекс ID прямо в окне — без токенов в конфигах и танцев с curl;
- Диск подключается как обычная папка: чтение, запись, переименование,
  удаление в корзину;
- **чтение диапазонами** — открыть ISO на 6 ГБ, чтобы посмотреть заголовок,
  стоит килобайты, а не всю ночь;
- миниатюры изображений: их отдаёт сам Яндекс, файлы при этом не скачиваются;
- эмблемы состояния и меню «Orbita» прямо в Dolphin;
- корзина с восстановлением, публичные ссылки (в том числе с паролем и сроком),
  копирование силами Яндекса, закрепление файлов офлайн;
- поиск по всему Диску — мгновенный, по локальной базе;
- очередь повторной выгрузки: сорвавшаяся отправка не теряется;
- **защита от перезаписи**: если файл успели изменить с другого устройства,
  ваша версия ляжет рядом, а не затрёт чужую;
- значок в лотке, уведомления, автозапуск;
- русский и английский интерфейс.

## Как выглядит

| Настройки | «О программе» |
|---|---|
| ![Окно настроек](docs/screenshots/settings.png) | ![Окно «О программе»](docs/screenshots/about.png) |

| Меню значка в лотке | Выбор папки на Диске |
|---|---|
| ![Меню значка в системном лотке](docs/screenshots/tray-menu.png) | ![Выбор папки на Диске](docs/screenshots/remote-folder-picker.png) |

### В Dolphin

Эмблема говорит, где лежит содержимое: облако со стрелкой — только на Диске,
галочка — целиком на устройстве, у папок есть и промежуточное состояние.

![Эмблемы состояния в Dolphin](docs/screenshots/dolphin-emblems.png)

| Меню на своей папке | Меню на файле Диска |
|---|---|
| ![Пункт «Загрузить на Яндекс.Диск»](docs/screenshots/dolphin-menu-folder.png) | ![Сохранить на устройстве, поделиться ссылкой](docs/screenshots/dolphin-menu-file.png) |

## Установка

Готовые пакеты — на странице
[релизов](https://github.com/fenixvd/Orbita/releases). Скачайте свой и:

```sh
sudo zypper install ./orbita-0.1.0-1.x86_64.rpm   # openSUSE
sudo dnf install ./orbita-0.1.0-1.x86_64.rpm      # Fedora
sudo apt install ./orbita_0.1.0_amd64.deb         # Debian, Ubuntu
```

Для Arch — `packaging/PKGBUILD`.

После установки Dolphin нужно перезапустить (`kquitapp6 dolphin`), иначе он
не подхватит плагины эмблем и меню.

## Сборка

Зависимости для сборки:

```sh
# openSUSE
sudo zypper install gcc-c++ cmake ninja qt6-base-devel qt6-declarative-devel \
     kf6-kio-devel kf6-kstatusnotifieritem-devel fuse3-devel sqlite3-devel

# Fedora
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtdeclarative-devel \
     kf6-kio-devel kf6-kstatusnotifieritem-devel fuse3-devel sqlite-devel

# Debian, Ubuntu
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-declarative-dev \
     libkf6kio-dev libkf6statusnotifieritem-dev libfuse3-dev libsqlite3-dev

# Arch
sudo pacman -S gcc cmake ninja qt6-base qt6-declarative kio kstatusnotifieritem \
     fuse3 sqlite
```

Сама сборка:

```sh
git clone https://github.com/fenixvd/Orbita.git
cd Orbita
cmake -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build
sudo cmake --install build
```

Префикс обязан быть `/usr`: в `/usr/local` плагины Dolphin не ищутся ни Qt,
ни KIO. Пакеты собираются оттуда же: `cd build && cpack -G RPM` или `-G DEB`.

Проверено на openSUSE Tumbleweed, Fedora, Debian 13 и Arch.

## Настройка

Ничего настраивать не нужно — всё работает сразу после установки.

Если хочется использовать своё приложение с oauth.yandex.ru:

```json
// ~/.config/Orbita/config.json
{ "client_id": "..." }
```

## Команды

Интерфейс — основной способ работы. Консоль нужна для отладки, скриптов
и меню файлового менеджера.

| Команда | Что делает |
|---|---|
| `orbita login` | вход в аккаунт |
| `orbita doctor` | самопроверка: токен, база, монтирование, плагины |
| `orbita sync` | обойти Диск и наполнить базу метаданными |
| `orbita mount ~/Яндекс.Диск` | подключить Диск |
| `orbita mount --demo /tmp/проба` | выдуманное дерево, без учётной записи |
| `orbita upload файл…` | отправить на Диск |
| `orbita pin` / `unpin` | сохранить на устройстве / убрать |
| `orbita share` / `unshare` | публичная ссылка |

Размонтирование: `fusermount3 -u <точка>`.

## Как устроено

```
                  ┌───────────────┐
  файловый    ←── │   FUSE-слой   │  getattr/readdir — из базы, без сети
  менеджер        │  (OrbitaFuse) │  read — диапазонами, без загрузки целиком
                  └───────┬───────┘
                          │
          ┌───────────────┼────────────────┐
          ▼               ▼                ▼
  ┌───────────────┐ ┌────────────┐ ┌──────────────┐
  │ MetadataStore │ │CacheManager│ │   DiskApi    │
  │ дерево, SQLite│ │ LRU + pin  │ │ REST + OAuth │
  └───────────────┘ └────────────┘ └──────────────┘
```

Метаданные и содержимое живут отдельно. Всё дерево лежит в SQLite, поэтому
просмотр папок мгновенный и работает без сети; содержимое подтягивается
по мере обращения. Дерево из десяти тысяч файлов занимает около четырёх
мегабайт — примерно как одна фотография.

Состояния файла: `Placeholder` → `Cached` → `Pinned`, плюс `Dirty` для
изменённых локально.

## Чего ждать не стоит

Это ограничения API Яндекса, а не лень авторов:

- **уведомлений об изменениях нет.** Узнать, что файл добавили с телефона,
  можно только спросив. Orbita спрашивает раз в минуту и сверяет открытую
  папку при переходе в неё;
- **дописать кусок файла нельзя** — правка существующего файла требует
  скачать его целиком и залить заново;
- **во Flatpak эмблемы в Dolphin работать не будут**: файловый менеджер
  живёт на хосте, а плагин оказался бы в песочнице.

## Пути

| Что | Где |
|---|---|
| настройки и токен | `~/.config/Orbita/` |
| база метаданных | `~/.local/share/Orbita/metadata.db` |
| кэш содержимого и миниатюр | `~/.cache/Orbita/` |
| точка подключения | `~/Яндекс.Диск` (меняется в настройках) |

## Лицензия

GNU General Public License v2 — см. [LICENSE](LICENSE).

Orbita не связана с Яндексом. Это независимый клиент, использующий
публичный [API Яндекс.Диска](https://yandex.ru/dev/disk/).
