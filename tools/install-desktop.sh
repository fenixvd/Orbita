#!/bin/sh
# Ставит значок и ярлык для текущего пользователя, без установки в систему.
#
# Значок раскладывается по стандартным размерам темы hicolor: меню приложений
# ищет именно их, и одного исходника 1024x1024 ему недостаточно.
set -eu

ICON_SOURCE="$1"
DESKTOP_FILE="$2"
SERVICE_MENU="${3:-}"
APP_ID="ru.rainedev.orbita"

ICON_ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor"
APPS_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"

mkdir -p "$APPS_DIR" "$ICON_ROOT"
cp "$DESKTOP_FILE" "$APPS_DIR/$APP_ID.desktop"

# Без index.theme каталог темой не считается, и значок не находится —
# а кэш при этом создаётся и лишь усугубляет дело.
if [ ! -f "$ICON_ROOT/index.theme" ] && [ -f /usr/share/icons/hicolor/index.theme ]; then
    cp /usr/share/icons/hicolor/index.theme "$ICON_ROOT/index.theme"
fi

for size in 16 22 24 32 48 64 128 256 512; do
    dir="$ICON_ROOT/${size}x${size}/apps"
    mkdir -p "$dir"
    if command -v magick >/dev/null 2>&1; then
        magick "$ICON_SOURCE" -resize "${size}x${size}" "$dir/$APP_ID.png"
    else
        cp "$ICON_SOURCE" "$dir/$APP_ID.png"
    fi
done

# Исходник кладём как есть — пригодится для крупных плиток.
mkdir -p "$ICON_ROOT/1024x1024/apps"
cp "$ICON_SOURCE" "$ICON_ROOT/1024x1024/apps/$APP_ID.png"

# Пункты меню раньше ставились .desktop-файлом; теперь их даёт плагин,
# поэтому старый файл убираем — иначе в Dolphin будет два подменю Orbita.
rm -f "${XDG_DATA_HOME:-$HOME/.local/share}/kio/servicemenus/orbita-servicemenu.desktop"

# Без обновления кэшей меню продолжит показывать пустой квадрат.
command -v gtk-update-icon-cache >/dev/null 2>&1 && \
    gtk-update-icon-cache -qtf "$ICON_ROOT" 2>/dev/null || true
command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "$APPS_DIR" 2>/dev/null || true
command -v kbuildsycoca6 >/dev/null 2>&1 && kbuildsycoca6 --noincremental >/dev/null 2>&1 || true

echo "Значок и ярлык установлены для пользователя."
